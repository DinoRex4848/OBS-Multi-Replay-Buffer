#include "hotkeys.hpp"
#include "capture.hpp"
#include "identifiers.hpp"
#include "preferences.hpp"
#include <algorithm>
#include <iterator>
#include <string>

namespace replay {
static_assert(std::size(ids::hotkeys) == hotkeyCount);
static_assert(presetOf(HotkeyAction::Start) == -1 && presetOf(HotkeyAction::Stop) == -1);
static_assert(presetOf(saveAction(0)) == 0 && presetOf(saveAction(maxPresets - 1)) == maxPresets - 1);
static_assert(presetOf(saveAction(maxPresets)) == -1);
static_assert(int(saveAction(2)) == 4); // Preset 3 has always been saved as "hotkey4".

namespace {
std::string description(int slot, bool used)
{
  if (slot == int(HotkeyAction::Start)) return "Multi-Length Replay: Start Buffering";
  if (slot == int(HotkeyAction::Stop)) return "Multi-Length Replay: Stop Buffering";
  return "Multi-Length Replay: Save Replay " + std::to_string(presetOf(HotkeyAction(slot)) + 1) +
         (used ? "" : " (no preset)");
}
bool bound(obs_hotkey_id id)
{
  auto *bindings = obs_hotkey_save(id);
  const bool result = obs_data_array_count(bindings) > 0;
  obs_data_array_release(bindings);
  return result;
}
} // namespace

Hotkeys::Hotkeys(Handler handler) : handler_(std::move(handler))
{
  for (int slot = 0; slot < hotkeyCount; ++slot) {
    contexts_[slot] = {this, HotkeyAction(slot)};
    ids_[slot] = obs_hotkey_register_frontend(ids::hotkeys[slot], description(slot, slot < usedCount()).c_str(),
                                              pressed, &contexts_[slot]);
  }
  registered_ = true;
}
Hotkeys::~Hotkeys() { unregister(); }
void Hotkeys::unregister()
{
  if (!registered_) return;
  registered_ = false;
  for (auto id : ids_) obs_hotkey_unregister(id);
  ids_.fill(OBS_INVALID_HOTKEY_ID);
  presets_ = 0;
}
void Hotkeys::setPresetCount(int count)
{
  if (!registered_) return;
  presets_ = std::clamp(count, 0, maxPresets);
  for (int slot = int(HotkeyAction::SavePreset1); slot < hotkeyCount; ++slot)
    obs_hotkey_set_description(ids_[slot], description(slot, slot < usedCount()).c_str());
}
void Hotkeys::removePreset(int preset)
{
  if (!registered_ || preset < 0 || preset >= presets_) return;
  for (int i = preset; i + 1 < presets_; ++i) {
    auto *bindings = obs_hotkey_save(id(saveAction(i + 1)));
    obs_hotkey_load(id(saveAction(i)), bindings);
    obs_data_array_release(bindings);
  }
  auto *none = obs_data_array_create();
  obs_hotkey_load(id(saveAction(presets_ - 1)), none);
  obs_data_array_release(none);
  setPresetCount(presets_ - 1);
}
void Hotkeys::load(obs_data_t *settings)
{
  if (!registered_) return;
  for (int slot = 0; slot < hotkeyCount; ++slot) {
    auto *bindings = obs_data_get_array(settings, prefs::hotkeyKey(slot).c_str());
    if (!bindings) bindings = obs_data_array_create();
    obs_hotkey_load(ids_[slot], bindings);
    obs_data_array_release(bindings);
  }
}
void Hotkeys::save(obs_data_t *settings) const
{
  if (!registered_) return;
  for (int slot = 0; slot < hotkeyCount; ++slot) {
    // Unused save hotkeys are stored only if the user bound them anyway.
    if (slot >= usedCount() && !bound(ids_[slot])) continue;
    auto *bindings = obs_hotkey_save(ids_[slot]);
    obs_data_set_array(settings, prefs::hotkeyKey(slot).c_str(), bindings);
    obs_data_array_release(bindings);
  }
}
bool Hotkeys::contains(obs_hotkey_id id) const
{
  return id != OBS_INVALID_HOTKEY_ID && std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}
void Hotkeys::pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *context = static_cast<Context *>(data);
  context->owner->handler_(context->action, clockNow());
}
} // namespace replay

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
std::string description(int slot)
{
  if (slot == int(HotkeyAction::Start)) return "Multi-Length Replay: Start Buffering";
  if (slot == int(HotkeyAction::Stop)) return "Multi-Length Replay: Stop Buffering";
  return "Multi-Length Replay: Save Replay " + std::to_string(presetOf(HotkeyAction(slot)) + 1);
}
} // namespace

Hotkeys::Hotkeys(Handler handler) : handler_(std::move(handler))
{
  ids_.fill(OBS_INVALID_HOTKEY_ID);
  for (int i = 0; i < hotkeyCount; ++i) contexts_[i] = {this, HotkeyAction(i)};
  registered_ = true;
  registerSlot(int(HotkeyAction::Start));
  registerSlot(int(HotkeyAction::Stop));
}
Hotkeys::~Hotkeys() { unregister(); }
void Hotkeys::registerSlot(int slot)
{
  ids_[slot] = obs_hotkey_register_frontend(ids::hotkeys[slot], description(slot).c_str(), pressed, &contexts_[slot]);
}
void Hotkeys::unregister()
{
  if (!registered_) return;
  registered_ = false;
  for (int slot = 0; slot < registeredCount(); ++slot) obs_hotkey_unregister(ids_[slot]);
  ids_.fill(OBS_INVALID_HOTKEY_ID);
  presets_ = 0;
}
void Hotkeys::setPresetCount(int count)
{
  if (!registered_) return;
  count = std::clamp(count, 0, maxPresets);
  while (presets_ < count) {
    registerSlot(registeredCount());
    ++presets_;
  }
  while (presets_ > count) {
    --presets_;
    const int slot = registeredCount();
    obs_hotkey_unregister(ids_[slot]);
    ids_[slot] = OBS_INVALID_HOTKEY_ID;
  }
}
void Hotkeys::removePreset(int preset)
{
  if (!registered_ || preset < 0 || preset >= presets_) return;
  for (int i = preset; i + 1 < presets_; ++i) {
    auto *bindings = obs_hotkey_save(id(saveAction(i + 1)));
    obs_hotkey_load(id(saveAction(i)), bindings);
    obs_data_array_release(bindings);
  }
  setPresetCount(presets_ - 1);
}
void Hotkeys::load(obs_data_t *settings)
{
  for (int slot = 0; slot < registeredCount(); ++slot) {
    auto *bindings = obs_data_get_array(settings, prefs::hotkeyKey(slot).c_str());
    if (!bindings) bindings = obs_data_array_create();
    obs_hotkey_load(ids_[slot], bindings);
    obs_data_array_release(bindings);
  }
}
void Hotkeys::save(obs_data_t *settings) const
{
  for (int slot = 0; slot < registeredCount(); ++slot) {
    auto *bindings = obs_hotkey_save(ids_[slot]);
    obs_data_set_array(settings, prefs::hotkeyKey(slot).c_str(), bindings);
    obs_data_array_release(bindings);
  }
}
bool Hotkeys::contains(obs_hotkey_id id) const
{
  return id != OBS_INVALID_HOTKEY_ID &&
         std::find(ids_.begin(), ids_.begin() + registeredCount(), id) != ids_.begin() + registeredCount();
}
void Hotkeys::pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *context = static_cast<Context *>(data);
  context->owner->handler_(context->action, clockNow());
}
} // namespace replay

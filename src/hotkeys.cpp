#include "hotkeys.hpp"
#include "capture.hpp"
#include "identifiers.hpp"
#include "preferences.hpp"
#include <algorithm>
#include <iterator>

namespace replay {
static_assert(std::size(ids::hotkeys) == hotkeyCount);
static_assert(hotkeyCount == int(HotkeyAction::SavePreset1) + presetCount);
static_assert(presetOf(HotkeyAction::Start) == -1 && presetOf(HotkeyAction::Stop) == -1);
static_assert(presetOf(saveAction(0)) == 0 && presetOf(HotkeyAction::SavePreset3) == presetCount - 1);

namespace {
constexpr const char *descriptions[hotkeyCount] = {
    "Multi-Length Replay: Start Buffering", "Multi-Length Replay: Stop Buffering",
    "Multi-Length Replay: Save Replay 1", "Multi-Length Replay: Save Replay 2", "Multi-Length Replay: Save Replay 3"};
}

Hotkeys::Hotkeys(Handler handler) : handler_(std::move(handler))
{
  for (int i = 0; i < hotkeyCount; ++i) {
    contexts_[i] = {this, HotkeyAction(i)};
    ids_[i] = obs_hotkey_register_frontend(ids::hotkeys[i], descriptions[i], pressed, &contexts_[i]);
  }
  registered_ = true;
}
Hotkeys::~Hotkeys() { unregister(); }
void Hotkeys::unregister()
{
  if (!registered_) return;
  registered_ = false;
  for (auto id : ids_) obs_hotkey_unregister(id);
}
void Hotkeys::load(obs_data_t *settings)
{
  for (int i = 0; i < hotkeyCount; ++i) {
    auto *bindings = obs_data_get_array(settings, prefs::hotkeyKey(i).c_str());
    if (!bindings) bindings = obs_data_array_create();
    obs_hotkey_load(ids_[i], bindings);
    obs_data_array_release(bindings);
  }
}
void Hotkeys::save(obs_data_t *settings) const
{
  for (int i = 0; i < hotkeyCount; ++i) {
    auto *bindings = obs_hotkey_save(ids_[i]);
    obs_data_set_array(settings, prefs::hotkeyKey(i).c_str(), bindings);
    obs_data_array_release(bindings);
  }
}
bool Hotkeys::contains(obs_hotkey_id id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }
void Hotkeys::pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
  if (!pressed) return;
  auto *context = static_cast<Context *>(data);
  context->owner->handler_(context->action, clockNow());
}
} // namespace replay

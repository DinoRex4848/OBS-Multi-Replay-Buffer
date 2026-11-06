#pragma once
#include "constants.hpp"
#include <obs.h>
#include <array>
#include <functional>

namespace replay {
// Order matches ids::hotkeys and the saved "hotkeyN" binding keys: Start, Stop,
// then saveAction(0) to saveAction(maxPresets - 1).
enum class HotkeyAction { Start, Stop, SavePreset1 };
constexpr int hotkeyCount = int(HotkeyAction::SavePreset1) + maxPresets;
constexpr HotkeyAction saveAction(int preset) { return HotkeyAction(int(HotkeyAction::SavePreset1) + preset); }
// The preset an action saves, or -1.
constexpr int presetOf(HotkeyAction action)
{
  const int preset = int(action) - int(HotkeyAction::SavePreset1);
  return preset >= 0 && preset < maxPresets ? preset : -1;
}

// The plugin's OBS frontend hotkeys. All of them, including a save hotkey for every
// possible preset, are registered at startup: OBS lists hotkeys in registration order,
// so this keeps them together at the top of its Hotkeys settings. Save hotkeys of
// presets that do not exist are labeled "(no preset)" and do nothing.
class Hotkeys {
public:
  // Runs on OBS's hotkey thread. `pressed` is the press time, taken before any
  // hand-off so a save's clip ends when the key was pressed.
  using Handler = std::function<void(HotkeyAction, Time pressed)>;
  explicit Hotkeys(Handler handler);
  ~Hotkeys();
  Hotkeys(const Hotkeys &) = delete;
  Hotkeys &operator=(const Hotkeys &) = delete;
  void unregister();
  // Marks presets [0, count) as existing and relabels the save hotkeys to match.
  void setPresetCount(int count);
  int presetCount() const { return presets_; }
  // Moves the bindings of the presets after `preset` up by one, then unbinds the last
  // preset's save hotkey and marks it unused.
  void removePreset(int preset);
  // Bindings are saved with the plugin's profile settings, under prefs::hotkeyKey.
  void load(obs_data_t *settings);
  void save(obs_data_t *settings) const;
  bool contains(obs_hotkey_id id) const;
  obs_hotkey_id id(HotkeyAction action) const { return ids_[int(action)]; }

private:
  struct Context {
    Hotkeys *owner = nullptr;
    HotkeyAction action = HotkeyAction::Start;
  };
  // Slots of Start, Stop and the existing presets' save hotkeys.
  int usedCount() const { return int(HotkeyAction::SavePreset1) + presets_; }
  static void pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed);
  Handler handler_;
  // Fixed storage: OBS's hotkey thread holds pointers into contexts_.
  std::array<Context, hotkeyCount> contexts_{};
  std::array<obs_hotkey_id, hotkeyCount> ids_{};
  int presets_ = 0;
  bool registered_ = false;
};
} // namespace replay

#pragma once
#include "constants.hpp"
#include <obs.h>
#include <array>
#include <functional>

namespace replay {
// Order matches ids::hotkeys and the saved "hotkeyN" binding keys.
enum class HotkeyAction { Start, Stop, SavePreset1, SavePreset2, SavePreset3 };
constexpr int hotkeyCount = 5;
constexpr HotkeyAction saveAction(int preset) { return HotkeyAction(int(HotkeyAction::SavePreset1) + preset); }
// The preset an action saves, or -1.
constexpr int presetOf(HotkeyAction action)
{
  const int preset = int(action) - int(HotkeyAction::SavePreset1);
  return preset >= 0 && preset < presetCount ? preset : -1;
}

// The plugin's OBS frontend hotkeys.
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
  static void pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed);
  Handler handler_;
  std::array<Context, hotkeyCount> contexts_{};
  std::array<obs_hotkey_id, hotkeyCount> ids_{};
  bool registered_ = false;
};
} // namespace replay

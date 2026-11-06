#pragma once

// Names stored in users' OBS profiles, dock layouts and hotkey bindings, or used by
// themes and the UI tests. Renaming any of them breaks existing installations.
namespace replay::ids {
constexpr const char *profileSection = "MultiLengthReplay";
constexpr const char *profileKey = "Settings";
constexpr const char *dock = "obs-multi-replay-dock";
constexpr const char *startButton = "replayBufferButton";
constexpr const char *settingsDialog = "multiReplaySettingsDialog";
constexpr const char *settingsButton = "multiReplaySettingsButton";
constexpr const char *settingsAction = "multiReplaySettingsAction";
// Hotkey names, in HotkeyAction order.
constexpr const char *hotkeys[] = {"MultiReplay.Start", "MultiReplay.Stop", "MultiReplay.Preset1",
                                   "MultiReplay.Preset2", "MultiReplay.Preset3"};
} // namespace replay::ids

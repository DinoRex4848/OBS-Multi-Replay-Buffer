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
constexpr const char *hotkeysButton = "multiReplayHotkeysButton";
// Hotkey names, in HotkeyAction order: Start, Stop, then one per preset slot.
constexpr const char *hotkeys[] = {
    "MultiReplay.Start",   "MultiReplay.Stop",    "MultiReplay.Preset1", "MultiReplay.Preset2", "MultiReplay.Preset3",
    "MultiReplay.Preset4", "MultiReplay.Preset5", "MultiReplay.Preset6", "MultiReplay.Preset7", "MultiReplay.Preset8"};
} // namespace replay::ids

#pragma once
#include "constants.hpp"
#include <obs-data.h>
#include <util/config-file.h>
#include <array>
#include <string>

namespace replay {
// Settings edited in the Tools window: changed as a draft, applied while stopped.
struct ReplaySettings {
  double budgetGb = defaultBudgetGb;
  std::string folder; // Blank: OBS's recording folder.
  bool shareEncoder = true;
  bool separateFolders = false;
  std::array<std::string, presetCount> presetFolders{}; // Blank: OBS's recording folder.
  bool operator==(const ReplaySettings &other) const
  {
    return budgetGb == other.budgetGb && folder == other.folder && shareEncoder == other.shareEncoder &&
           separateFolders == other.separateFolders && presetFolders == other.presetFolders;
  }
  bool operator!=(const ReplaySettings &other) const { return !(*this == other); }
};
// A dock preset. Name and length edits are saved immediately.
struct Preset {
  std::string name;
  int seconds = 0;
};
using Presets = std::array<Preset, presetCount>;
Preset defaultPreset(int index);

// One JSON object per OBS profile, stored under ids::profileSection/ids::profileKey.
// Its keys are part of every existing installation; see preferences.cpp.
namespace prefs {
// Fills in defaults for a profile that has no saved replay settings yet.
void applyDefaults(obs_data_t *data);
ReplaySettings readSettings(obs_data_t *data);
Presets readPresets(obs_data_t *data);
void write(obs_data_t *data, const ReplaySettings &settings, const Presets &presets);
// Key holding the bindings of hotkey `index`, in HotkeyAction order.
std::string hotkeyKey(int index);

// New reference to the profile's saved JSON, or an empty object.
obs_data_t *loadFromProfile(config_t *config);
// Saves to disk. On failure the in-memory profile is restored, because OBS saves
// that config itself later and must never write rejected edits.
bool saveToProfile(config_t *config, obs_data_t *data);
} // namespace prefs
} // namespace replay

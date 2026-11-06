#include "preferences.hpp"
#include "identifiers.hpp"

namespace replay {
namespace {
// Profile JSON keys. Never rename: existing profiles store these.
constexpr const char *budgetKey = "budget_gb";
constexpr const char *folderKey = "folder";
constexpr const char *shareEncoderKey = "share_encoder";
constexpr const char *separateFoldersKey = "separate_folders";
std::string presetFolderKey(int index) { return "preset_folder" + std::to_string(index); }
std::string secondsKey(int index) { return "seconds" + std::to_string(index); }
std::string nameKey(int index) { return "name" + std::to_string(index); }
} // namespace

Preset defaultPreset(int index) { return {"Replay " + std::to_string(index + 1), defaultPresetSeconds[index]}; }

namespace prefs {
std::string hotkeyKey(int index) { return "hotkey" + std::to_string(index); }
void applyDefaults(obs_data_t *data)
{
  obs_data_set_default_double(data, budgetKey, defaultBudgetGb);
  obs_data_set_default_string(data, folderKey, "");
  obs_data_set_default_bool(data, shareEncoderKey, true);
  for (int i = 0; i < presetCount; ++i) {
    const auto preset = defaultPreset(i);
    obs_data_set_default_int(data, secondsKey(i).c_str(), preset.seconds);
    obs_data_set_default_string(data, nameKey(i).c_str(), preset.name.c_str());
  }
}
ReplaySettings readSettings(obs_data_t *data)
{
  ReplaySettings settings;
  settings.budgetGb = obs_data_get_double(data, budgetKey);
  settings.folder = obs_data_get_string(data, folderKey);
  settings.shareEncoder = obs_data_get_bool(data, shareEncoderKey);
  settings.separateFolders = obs_data_get_bool(data, separateFoldersKey);
  for (int i = 0; i < presetCount; ++i) settings.presetFolders[i] = obs_data_get_string(data, presetFolderKey(i).c_str());
  return settings;
}
Presets readPresets(obs_data_t *data)
{
  Presets presets;
  for (int i = 0; i < presetCount; ++i)
    presets[i] = {obs_data_get_string(data, nameKey(i).c_str()), int(obs_data_get_int(data, secondsKey(i).c_str()))};
  return presets;
}
void write(obs_data_t *data, const ReplaySettings &settings, const Presets &presets)
{
  // Key order matches earlier releases, so saved profiles read the same.
  obs_data_set_double(data, budgetKey, settings.budgetGb);
  obs_data_set_string(data, folderKey, settings.folder.c_str());
  obs_data_set_bool(data, shareEncoderKey, settings.shareEncoder);
  obs_data_set_bool(data, separateFoldersKey, settings.separateFolders);
  for (int i = 0; i < presetCount; ++i) {
    obs_data_set_string(data, presetFolderKey(i).c_str(), settings.presetFolders[i].c_str());
    obs_data_set_int(data, secondsKey(i).c_str(), presets[i].seconds);
    obs_data_set_string(data, nameKey(i).c_str(), presets[i].name.c_str());
  }
}
obs_data_t *loadFromProfile(config_t *config)
{
  const char *json = config ? config_get_string(config, ids::profileSection, ids::profileKey) : nullptr;
  auto *data = json ? obs_data_create_from_json(json) : nullptr;
  return data ? data : obs_data_create();
}
bool saveToProfile(config_t *config, obs_data_t *data)
{
  if (!config) return false;
  const bool hadValue = config_has_user_value(config, ids::profileSection, ids::profileKey);
  const char *previous = config_get_string(config, ids::profileSection, ids::profileKey);
  const std::string previousJson = previous ? previous : "";
  config_set_string(config, ids::profileSection, ids::profileKey, obs_data_get_json(data));
  if (config_save_safe(config, "tmp", "bak") == CONFIG_SUCCESS) return true;
  if (hadValue)
    config_set_string(config, ids::profileSection, ids::profileKey, previousJson.c_str());
  else
    config_remove_value(config, ids::profileSection, ids::profileKey);
  return false;
}
} // namespace prefs
} // namespace replay

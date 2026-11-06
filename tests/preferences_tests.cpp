#include "preferences.hpp"
#include "test_support.hpp"
#include <iostream>
#include <utility>
#include <vector>

using namespace replay;
namespace {
// Saved by version 0.1.2; every later version must read it the same way.
const char *savedByOlderRelease =
    R"({"budget_gb":0.25,"folder":"/clips/Shared","share_encoder":false,"separate_folders":true,)"
    R"("preset_folder0":"/clips/Preset 1 α","seconds0":2,"name0":"Short α",)"
    R"("preset_folder1":"/clips/Preset 2 β","seconds1":4,"name1":"Medium β",)"
    R"("preset_folder2":"","seconds2":6,"name2":"Long γ",)"
    R"("hotkey0":[{"key":"OBS_KEY_F13"}],"hotkey1":[],"hotkey2":[],"hotkey3":[],"hotkey4":[]})";
std::vector<std::string> keys(obs_data_t *data)
{
  std::vector<std::string> names;
  for (auto *item = obs_data_first(data); item; obs_data_item_next(&item))
    names.push_back(obs_data_item_get_name(item));
  return names;
}
} // namespace
int main()
{
  try {
    // A profile that never saved replay settings.
    auto *data = obs_data_create();
    prefs::applyDefaults(data);
    const auto defaults = prefs::readSettings(data);
    CHECK(defaults == ReplaySettings{});
    CHECK(defaults.budgetGb == 6 && defaults.folder.empty() && defaults.shareEncoder && !defaults.separateFolders);
    // ...starts with two presets.
    CHECK(prefs::presetCount(data) == 2 && defaults.presetFolders.size() == 2);
    const auto presets = prefs::readPresets(data);
    CHECK(presets.size() == 2);
    CHECK(presets[0].name == "Replay 1" && presets[0].seconds == 60);
    CHECK(presets[1].name == "Replay 2" && presets[1].seconds == 240);
    CHECK(newPreset(2).name == "Replay 3" && newPreset(2).seconds == 60);
    obs_data_release(data);

    // Releases before encoder sharing existed saved no share_encoder key: they now share.
    data = obs_data_create_from_json(R"({"budget_gb":2,"folder":"/old"})");
    prefs::applyDefaults(data);
    CHECK(prefs::readSettings(data).shareEncoder && prefs::readSettings(data).budgetGb == 2);
    // ...and always had three presets, falling back to that release's default lengths.
    CHECK(prefs::presetCount(data) == 3);
    const auto legacyDefaults = prefs::readPresets(data);
    CHECK(legacyDefaults.size() == 3 && legacyDefaults[2].name == "Replay 3" && legacyDefaults[2].seconds == 600);
    obs_data_release(data);

    data = obs_data_create_from_json(savedByOlderRelease);
    CHECK(data);
    prefs::applyDefaults(data);
    const auto settings = prefs::readSettings(data);
    CHECK(settings.budgetGb == 0.25 && settings.folder == "/clips/Shared" && !settings.shareEncoder);
    CHECK(settings.separateFolders && settings.presetFolders[0] == "/clips/Preset 1 α" &&
          settings.presetFolders[1] == "/clips/Preset 2 β" && settings.presetFolders[2].empty());
    const auto saved = prefs::readPresets(data);
    CHECK(saved.size() == 3 && settings.presetFolders.size() == 3);
    CHECK(saved[0].name == "Short α" && saved[0].seconds == 2 && saved[2].name == "Long γ" && saved[2].seconds == 6);
    obs_data_release(data);

    // Writing produces the same keys, in the same order, as earlier releases, then the count.
    data = obs_data_create();
    prefs::write(data, settings, saved);
    const std::vector<std::string> expected = {"budget_gb",      "folder",      "share_encoder",  "separate_folders",
                                               "preset_folder0", "seconds0",    "name0",          "preset_folder1",
                                               "seconds1",       "name1",       "preset_folder2", "seconds2",
                                               "name2",          "preset_count"};
    CHECK(keys(data) == expected);
    for (int i = 0; i < 10; ++i) CHECK(prefs::hotkeyKey(i) == "hotkey" + std::to_string(i));
    auto *reread = obs_data_create_from_json(obs_data_get_json(data));
    CHECK(prefs::readSettings(reread) == settings);
    obs_data_release(reread);
    obs_data_release(data);

    // Eight presets round-trip; a removed preset's keys are not written.
    ReplaySettings eight = settings;
    Presets many;
    eight.presetFolders.clear();
    for (int i = 0; i < maxPresets; ++i) {
      many.push_back({"P" + std::to_string(i + 1), 10 + i});
      eight.presetFolders.push_back("/clips/" + std::to_string(i + 1));
    }
    data = obs_data_create();
    prefs::write(data, eight, many);
    reread = obs_data_create_from_json(obs_data_get_json(data));
    prefs::applyDefaults(reread);
    CHECK(prefs::presetCount(reread) == 8 && prefs::readSettings(reread) == eight);
    const auto manyRead = prefs::readPresets(reread);
    CHECK(manyRead.size() == 8 && manyRead[7].name == "P8" && manyRead[7].seconds == 17);
    obs_data_release(reread);
    obs_data_release(data);
    ReplaySettings two = settings;
    two.presetFolders.resize(2);
    data = obs_data_create();
    prefs::write(data, two, Presets(many.begin(), many.begin() + 2));
    CHECK(obs_data_get_int(data, "preset_count") == 2 && !obs_data_has_user_value(data, "seconds2") &&
          !obs_data_has_user_value(data, "name2") && !obs_data_has_user_value(data, "preset_folder2"));
    obs_data_release(data);

    // Out-of-range counts are clamped.
    for (const auto &[json, count] : std::vector<std::pair<const char *, int>>{
             {R"({"preset_count":0})", 1}, {R"({"preset_count":12})", 8}, {R"({"preset_count":5})", 5}}) {
      data = obs_data_create_from_json(json);
      prefs::applyDefaults(data);
      CHECK(prefs::presetCount(data) == count && int(prefs::readPresets(data).size()) == count &&
            int(prefs::readSettings(data).presetFolders.size()) == count);
      obs_data_release(data);
    }
    std::cout << "PASS: profile settings keys, defaults, preset count, older-release JSON and round trip\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}

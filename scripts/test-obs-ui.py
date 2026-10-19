#!/usr/bin/env python3
"""Run one test-driver OBS in a scratch profile; never use normal OBS paths."""
import argparse
import base64
import configparser
import json
import os
import pathlib
import shutil
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=pathlib.Path, default=repo / "build-ui-test")
parser.add_argument("--template", type=pathlib.Path, default=repo / "build/ui-config")
parser.add_argument("--scratch", type=pathlib.Path, default=repo / "build/compact-ui")
parser.add_argument("--scenario", choices=["layout", "restore", "encoder-restore", "upgrade", "settings", "folders", "folders-restore", "independent", "shared", "reset", "production"], default="layout")
parser.add_argument("--simple", action="store_true", help="Use Simple HQ NVENC AV1 with one AAC track")
parser.add_argument("--flatpak", action="store_true", help="Run the production smoke in the normal Flatpak runtime")
parser.add_argument("--filename-format")
parser.add_argument("--filename-prefix")
parser.add_argument("--filename-suffix")
parser.add_argument("--without-spaces", action="store_true")
args = parser.parse_args()
if args.flatpak and args.scenario != "production":
    parser.error("--flatpak is for the production smoke; use test-flatpak.sh for SDK UI scenarios")
build, template, scratch = (p.resolve() for p in (args.build, args.template, args.scratch))
required_mode = "OFF" if args.scenario == "production" else "ON"
if f"REPLAY_INTEGRATION_TEST:BOOL={required_mode}" not in (build / "CMakeCache.txt").read_text():
    parser.error(f"This scenario requires REPLAY_INTEGRATION_TEST={required_mode}")
if not scratch.is_relative_to(repo) or scratch == repo or scratch == template or template.is_relative_to(scratch):
    parser.error("Scratch must be a distinct directory below this repository")
if subprocess.run(["pgrep", "-x", "obs"], stdout=subprocess.DEVNULL).returncode == 0:
    parser.error("Close the other OBS instance before running GPU/UI tests")
if args.flatpak and "com.obsproject.Studio" in subprocess.check_output(
        ["flatpak", "ps", "--columns=application"], text=True).splitlines():
    parser.error("Close the other Flatpak OBS instance before running GPU/UI tests")
config_root = scratch / "config"
if args.scenario not in ["restore", "encoder-restore", "folders-restore"]:
    if config_root.exists():
        parser.error("Use a fresh scratch directory, or --scenario restore to check its saved layout")
    shutil.copytree(template, config_root)
elif not config_root.exists():
    parser.error("Restore requires the scratch configuration from a previous layout run")
clips = scratch / ("restore-clips" if args.scenario in ["restore", "encoder-restore", "folders-restore"] else "clips")
recordings = scratch / "recordings"
clips.mkdir(parents=True, exist_ok=True)
recordings.mkdir(parents=True, exist_ok=True)


def read_ini(path):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.optionxform = str
    config.read(path)
    return config


def write_ini(path, config):
    with path.open("w") as output:
        config.write(output, space_around_delimiters=False)


global_ini = config_root / "obs-studio/global.ini"
global_config = read_ini(global_ini)
if not global_config.has_section("Locations"):
    global_config.add_section("Locations")
for key in ["Configuration", "SceneCollections", "Profiles", "PluginManagerSettings"]:
    global_config.set("Locations", key, str(config_root))
write_ini(global_ini, global_config)
profiles = list((config_root / "obs-studio/basic/profiles").glob("*/basic.ini"))
if not profiles:
    parser.error("Template must contain an isolated OBS profile")
if args.scenario == "upgrade":
    user_config = read_ini(config_root / "obs-studio/user.ini")
    legacy_state = base64.b64decode(user_config.get("BasicWindow", "DockState"))
    if "obs-multi-replay-settings-dock".encode("utf-16-be") not in legacy_state:
        parser.error("Upgrade requires --template pointing to a saved two-dock scratch configuration")
    legacy_settings = {path: json.loads(read_ini(path).get("MultiLengthReplay", "Settings")) for path in profiles}
for path in profiles:
    profile = read_ini(path)
    if args.scenario == "production":
        profile.remove_section("MultiLengthReplay")
    if args.simple:
        profile.set("Output", "Mode", "Simple")
        profile.set("SimpleOutput", "RecQuality", "HQ")
        profile.set("SimpleOutput", "RecEncoder", "nvenc_av1")
        profile.set("SimpleOutput", "RecAudioEncoder", "aac")
        profile.set("SimpleOutput", "RecTracks", "1")
    if args.filename_format is not None:
        profile.set("Output", "FilenameFormatting", args.filename_format)
    if args.filename_prefix is not None:
        profile.set("SimpleOutput", "RecRBPrefix", args.filename_prefix)
    if args.filename_suffix is not None:
        profile.set("SimpleOutput", "RecRBSuffix", args.filename_suffix)
    if args.without_spaces:
        # Deliberately disagree so the test proves which mode's flag is read.
        simple = profile.get("Output", "Mode") != "Advanced"
        profile.set("SimpleOutput", "FileNameWithoutSpace", "true" if simple else "false")
        profile.set("AdvOut", "RecFileNameWithoutSpace", "false" if simple else "true")
    for section, key in [("AdvOut", "RecFilePath"), ("AdvOut", "FFFilePath"), ("SimpleOutput", "FilePath")]:
        if not profile.has_section(section):
            profile.add_section(section)
        profile.set(section, key, str(recordings))
    write_ini(path, profile)
    check = read_ini(path)
    for section, key in [("AdvOut", "RecFilePath"), ("AdvOut", "FFFilePath"), ("SimpleOutput", "FilePath")]:
        assert pathlib.Path(check.get(section, key)).resolve() == recordings
assert all(pathlib.Path(value).resolve() == config_root for value in read_ini(global_ini)["Locations"].values())
for marker in (config_root / "obs-studio").rglob(".unclean_shutdown"):
    marker.unlink()
if args.scenario == "settings":
    profile_dir = config_root / "obs-studio/basic/profiles/MultiReplaySettingsTest"
    shutil.copytree(profiles[0].parent, profile_dir)
    profile = read_ini(profile_dir / "basic.ini")
    profile.set("General", "Name", "Replay Settings Test")
    if not profile.has_section("MultiLengthReplay"):
        profile.add_section("MultiLengthReplay")
    profile.set("MultiLengthReplay", "Settings", json.dumps({
        "budget_gb": 0.33, "folder": str(clips),
        "seconds0": 2, "seconds1": 4, "seconds2": 6,
    }))
    write_ini(profile_dir / "basic.ini", profile)
data_root = scratch / "data/obs-multi-replay"
data_root.mkdir(parents=True, exist_ok=True)
shutil.copy2(build / "multi-replay-writer", data_root / "multi-replay-writer")
shutil.copytree(repo / "data/locale", data_root / "locale", dirs_exist_ok=True)
env = dict(os.environ)
env.update(XDG_CONFIG_HOME=str(config_root), OBS_PLUGINS_PATH=str(build),
           OBS_PLUGINS_DATA_PATH=str(data_root.parent), REPLAY_TEST_OUTPUT=str(clips), QT_QPA_PLATFORM="xcb")
env.pop("REPLAY_TEST_SHARED", None)
env.pop("REPLAY_TEST_LAYOUT", None)
env.pop("REPLAY_TEST_SETTINGS", None)
env.pop("REPLAY_TEST_FOLDERS", None)
env.pop("REPLAY_TEST_FOLDER_BASE", None)
if args.scenario in ["layout", "restore", "encoder-restore", "upgrade"]:
    env["REPLAY_TEST_LAYOUT"] = "1" if args.scenario == "layout" else "restore"
elif args.scenario in ["shared", "reset"]:
    env["REPLAY_TEST_SHARED"] = "reset" if args.scenario == "reset" else "1"
elif args.scenario == "settings":
    env["REPLAY_TEST_SETTINGS"] = "1"
elif args.scenario in ["folders", "folders-restore"]:
    env["REPLAY_TEST_FOLDERS"] = "restore" if args.scenario == "folders-restore" else "1"
    env["REPLAY_TEST_FOLDER_BASE"] = str(scratch / "clips")
if args.scenario == "production":
    env.pop("REPLAY_TEST_OUTPUT", None)
    command = [sys.executable, str(repo / "tests/obs_load_smoke.py"), "--config", str(config_root),
               "--build", str(build), "--data", str(data_root.parent), "--output", str(scratch)]
    if args.flatpak:
        command.append("--flatpak")
    subprocess.run(command + ["--baseline"], env=env, check=True)
    subprocess.run(command, env=env, check=True)
    for path in profiles:
        settings = json.loads(read_ini(path).get("MultiLengthReplay", "Settings"))
        assert [settings[f"seconds{i}"] for i in range(3)] == [60, 240, 600], settings
        assert settings["budget_gb"] == 6 and settings["share_encoder"], settings
        assert not settings["separate_folders"] and all(not settings[f"preset_folder{i}"] for i in range(3)), settings
    print(f"PASS: production module, manual startup, defaults and baseline allocations; {scratch}")
    sys.exit(0)
log_path = scratch / f"obs-{args.scenario}.log"
with log_path.open("w") as log:
    process = subprocess.Popen(["obs", "--multi", "--disable-missing-files-check"], env=env, stdout=log, stderr=log)
    try:
        code = process.wait(timeout=90)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        raise SystemExit(f"OBS did not exit orderly; inspect {log_path}")
if code:
    raise SystemExit(f"OBS exited with code {code}; inspect {log_path}")
report_path = clips / "report.json"
report = json.loads(report_path.read_text())
assert report["memory_after_stop"] == 0, report
if args.scenario == "reset":
    expected_warning = ("OBS recreated its recording encoder. A recording started now would use a second encoder; "
                        "restart buffering to share again.")
    assert report["errors"] == [expected_warning], report["errors"]
else:
    assert not report["errors"], report["errors"]
assert not report["frontend_active_at_close"], report
failed_saves = [result for result in report["saved"] if result["error"]]
if args.scenario == "folders":
    assert len(failed_saves) == 3, failed_saves
    assert all(any(str(clips / name) in result["error"] for result in failed_saves)
               for name in ["Missing", "Disconnect me", "Read only"]), failed_saves
else:
    assert not failed_saves, failed_saves
steps = {step["step"]: step for step in report["steps"]}
if args.scenario in ["shared", "independent"]:
    sharing = args.scenario == "shared"
    saved = json.loads(read_ini(profiles[0]).get("MultiLengthReplay", "Settings"))
    assert saved["share_encoder"] == sharing, saved
    for step in report["steps"]:
        assert step["applied_share_encoder"] == sharing and step["separate_encoder_checked"] != sharing, step
        if step["collecting"]:
            assert step["shared"] == sharing, step
    if sharing:
        assert steps["recording paused 6.5 s"]["paused"], steps
        assert not steps["recording resumed"]["paused"], steps
if args.scenario == "reset":
    assert expected_warning in steps["after OBS settings were applied"]["notice"], steps
    assert steps["sharing the new encoder"]["shared"], steps
    assert expected_warning not in steps["sharing the new encoder"]["notice"], steps
if args.scenario == "layout":
    compact = steps["compact stopped controls"]
    assert compact["replay_dock_registered"] and compact["controls_contained"], compact
    assert compact["settings_action_registered"] and not compact["settings_dock_registered"], compact
    assert compact["settings_windows"] == 1 and compact["settings_tabs"] == 2, compact
    assert compact["settings_shortcut_visible"] and compact["settings_shortcut_enabled"], compact
    assert not steps["settings hidden at startup"]["settings_visible"], steps
    assert compact["minimum_width"] <= 320 and compact["minimum_height"] <= 300, compact
    assert compact["width"] <= 320 and compact["height"] <= 300, compact
    assert compact["settings_tab"] == 0 and not compact["apply_enabled"], compact
    assert steps["diagnostics tab"]["diagnostics_fit"], steps["diagnostics tab"]
    for name in ["dock shortcut while buffering", "draining controls", "docked settings shortcut"]:
        step = steps[name]
        assert step["settings_shortcut_visible"] and step["settings_shortcut_enabled"], step
        assert step["settings_visible"] and step["settings_tab"] == 0 and step["settings_windows"] == 1, step
        assert step["controls_contained"], step
    assert steps["dock shortcut while buffering"]["collecting"], steps
    assert steps["docked settings shortcut"]["controls_docked_right"], steps
    hidden = steps["hotkey with dock and window hidden"]
    assert hidden["collecting"] and not hidden["controls_visible"] and not hidden["settings_visible"], hidden
    for name in ["active default theme", "active Acri theme", "active Rachni theme", "active Light theme"]:
        step = steps[name]
        assert step["button_class"] == "state-active" and step["settings_locked"], step
        assert step["record_button_class"] == "state-active", step
        assert step["buffer_button_color"] == step["record_button_color"], step
    assert steps["draining controls"]["button_class"] == "", steps
    assert steps["stopped after draining"]["button_class"] == "", steps
    assert steps["replay dock layout saved"]["controls_docked_right"], steps
    assert len(report["saved"]) == 3, report
    assert any(" (2)" in result["path"] for result in report["saved"]), report
elif args.scenario in ["restore", "encoder-restore", "upgrade"]:
    step = steps["restored layout and profile"]
    assert step["replay_dock_registered"], step
    if args.scenario != "encoder-restore":
        assert step["controls_docked_right"], step
    assert step["settings_action_registered"] and not step["settings_dock_registered"], step
    assert step["preset_seconds"] == [2, 4, 6], step
    assert not step["collecting"] and not step["settings_visible"], step
    for path in profiles:
        saved = json.loads(read_ini(path).get("MultiLengthReplay", "Settings"))
        assert step["applied_share_encoder"] == saved.get("share_encoder", True), step
        assert step["separate_encoder_checked"] != step["applied_share_encoder"], step
    if args.scenario == "upgrade":
        for path, expected in legacy_settings.items():
            saved = json.loads(read_ini(path).get("MultiLengthReplay", "Settings"))
            assert all(saved[key] == value for key, value in expected.items()), path
            assert saved["separate_folders"] == expected.get("separate_folders", False), path
            assert all(saved[f"preset_folder{i}"] == expected.get(f"preset_folder{i}", "") for i in range(3)), path
elif args.scenario == "settings":
    assert len(report["checks"]) >= 32, report["checks"]
    assert steps["settings while buffering"]["settings_locked"], steps
    assert steps["pending settings before shutdown"]["settings_dirty"], steps
    original = json.loads(read_ini(profiles[0]).get("MultiLengthReplay", "Settings"))
    assert original["budget_gb"] == 0.8 and original["folder"] == str(clips) and not original["share_encoder"], original
    switched = json.loads(read_ini(profile_dir / "basic.ini").get("MultiLengthReplay", "Settings"))
    assert switched["budget_gb"] == 0.25 and switched["folder"] == str(clips) and not switched["share_encoder"], switched
    for settings in [original, switched]:
        assert not settings["separate_folders"] and all(not settings[f"preset_folder{i}"] for i in range(3)), settings
    assert report["saved"] and all(pathlib.Path(result["path"]).parent == clips for result in report["saved"]), report
elif args.scenario == "folders":
    assert len(report["checks"]) >= 60, report["checks"]
    assert len(report["expected_folder_errors"]) == 3, report
    expected_counts = {recordings: 9, clips / "Shared": 3, clips / "Preset 1 α": 5,
                       clips / "Preset 2 β": 5, clips / "Preset 3 γ": 2}
    for folder, count in expected_counts.items():
        assert len(list(folder.glob("*.mkv"))) == count, (folder, count)
    successful = [result for result in report["saved"] if not result["error"]]
    assert len(successful) == 24 and all(result["requested"] == 2 for result in successful), report
    assert {pathlib.Path(result["path"]).parent for result in successful} == set(expected_counts), report
    assert not list((clips / "Read only").glob("*.mkv")), report
    assert not (clips / "Missing").exists() and not (clips / "Unapplied").exists(), report
    saved = json.loads(read_ini(profiles[0]).get("MultiLengthReplay", "Settings"))
    assert saved["separate_folders"] and saved["folder"] == str(clips / "Shared"), saved
    assert [saved[f"preset_folder{i}"] for i in range(3)] == [str(clips / name) for name in ["Preset 1 α", "Preset 2 β", "Preset 3 γ"]], saved
elif args.scenario == "folders-restore":
    assert "preset folders survive restart" in report["checks"], report
    assert steps["restored preset folders"]["separate_folders"], steps
    assert not steps["restored preset folders"]["collecting"], steps
print(f"PASS: {args.scenario}; report {report_path}; log {log_path}")

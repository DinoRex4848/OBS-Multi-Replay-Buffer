#!/usr/bin/env python3
"""Install the validated Flatpak payload, optionally migrating replay settings only."""
import argparse
import configparser
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import uuid

REPO = pathlib.Path(__file__).resolve().parent.parent
APP = "com.obsproject.Studio"
SECTION = "MultiLengthReplay"


def replay_section(text):
    headers = list(re.finditer(r"(?m)^\[([^\]\r\n]+)\][ \t]*\r?$", text))
    matches = [(m.start(), headers[i + 1].start() if i + 1 < len(headers) else len(text))
               for i, m in enumerate(headers) if m[1] == SECTION]
    if len(matches) > 1:
        raise RuntimeError("Duplicate replay settings sections; refusing ambiguous migration")
    return matches[0] if matches else None


def migrate_settings(source, target):
    """Preserve every byte outside the plugin section, including key case/comments."""
    old = source.read_bytes().decode("utf-8")
    current = target.read_bytes().decode("utf-8")
    span = replay_section(old)
    if not span:
        raise RuntimeError(f"No replay settings found in {source}")
    block = old[span[0]:span[1]]
    parsed = configparser.ConfigParser(interpolation=None, strict=True)
    parsed.read_string(block)
    if not isinstance(json.loads(parsed.get(SECTION, "Settings")), dict):
        raise RuntimeError("Replay settings must contain a JSON object")
    existing = replay_section(current)
    if existing:
        other = configparser.ConfigParser(interpolation=None, strict=True)
        other.read_string(current[existing[0]:existing[1]])
        if json.loads(other.get(SECTION, "Settings")) != json.loads(parsed.get(SECTION, "Settings")):
            raise RuntimeError("Flatpak profile already has different replay settings; preserving them")
        return current.encode("utf-8")
    ending = "\r\n" if "\r\n" in current else "\n"
    separator = "" if not current or current.endswith(ending * 2) else ending if current.endswith(ending) else ending * 2
    return (current + separator + block).encode("utf-8")


def verify_package(package):
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("target") != "flatpak" or manifest.get("architecture") != "x86_64" or manifest.get("integration_test") is not False:
        raise RuntimeError("This is not an x86_64 production Flatpak package")
    if manifest.get("ffmpeg_abi") != {"avformat": 62, "avcodec": 62, "avutil": 60}:
        raise RuntimeError("Package does not use FFmpeg 8")
    listed = manifest["files"]
    required = {"bin/64bit/obs-multi-replay.so", "data/multi-replay-writer", "data/locale/en-US.ini"}
    actual = set()
    for path in package.rglob("*"):
        if path.is_symlink():
            raise RuntimeError(f"Symlink in package: {path}")
        if path.is_file() and path.relative_to(package).as_posix() != "manifest.json":
            actual.add(path.relative_to(package).as_posix())
    if actual != set(listed) or not required.issubset(actual):
        raise RuntimeError("Package contents do not match the manifest")
    for relative, expected in listed.items():
        path = pathlib.PurePosixPath(relative)
        if path.is_absolute() or ".." in path.parts:
            raise RuntimeError("Invalid path in package manifest")
        if hashlib.sha256((package / relative).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Damaged package file: {relative}")
    if not os.access(package / "data/multi-replay-writer", os.X_OK):
        raise RuntimeError("The writer helper is not executable")
    return manifest


def atomic_write(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".replay-settings-", delete=False) as output:
            temporary = pathlib.Path(output.name)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        temporary.chmod(path.stat().st_mode & 0o777)
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def install(package, destination, backup_root, source_profile=None, target_profile=None):
    verify_package(package)
    if destination.is_symlink():
        raise RuntimeError("Refusing to replace a symlinked plugin directory")
    migrated = migrate_settings(source_profile, target_profile) if source_profile else None
    original = target_profile.read_bytes() if source_profile else None
    destination.parent.mkdir(parents=True, exist_ok=True)
    tag = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    backup = backup_root / tag
    backup.mkdir(parents=True)
    stage = destination.parent / (".obs-multi-replay-stage-" + tag)
    previous = destination.parent / (".obs-multi-replay-old-" + tag)
    published = False
    settings_written = False
    try:
        if destination.exists():
            shutil.copytree(destination, backup / "plugin")
        if target_profile:
            shutil.copytree(target_profile.parent, backup / "profile")
        shutil.copytree(package, stage)
        verify_package(stage)
        if destination.exists():
            os.replace(destination, previous)
        os.replace(stage, destination)
        published = True
        if migrated is not None and migrated != original:
            atomic_write(target_profile, migrated)
            settings_written = True
        (backup / "installation.json").write_text(json.dumps({
            "destination": str(destination), "source_profile": str(source_profile) if source_profile else None,
            "target_profile": str(target_profile) if target_profile else None,
            "settings_migrated": migrated is not None,
        }, indent=2) + "\n")
    except BaseException:
        if settings_written:
            atomic_write(target_profile, original)
        if published and destination.exists():
            shutil.rmtree(destination)
        if previous.exists():
            os.replace(previous, destination)
        raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    if previous.exists():
        shutil.rmtree(previous)
    return backup


def obs_running():
    result = subprocess.check_output(["flatpak", "ps", "--columns=application"], text=True)
    return APP in result.splitlines() or subprocess.run(
        ["pgrep", "-x", "obs"], stdout=subprocess.DEVNULL).returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, default=REPO / "dist/flatpak-x86_64/obs-multi-replay")
    parser.add_argument("--migrate-profile", help="Matching legacy and Flatpak profile directory name, e.g. Untitled")
    args = parser.parse_args()
    if obs_running():
        parser.error("Close OBS normally before installing; no files were changed")
    manifest = verify_package(args.package)
    commit = subprocess.check_output(["flatpak", "info", "--show-commit", APP], text=True).strip()
    if commit != manifest["app_commit"]:
        parser.error("OBS has changed since this package was built; rebuild for the installed app first")
    config = pathlib.Path.home() / ".var/app" / APP / "config/obs-studio"
    target = config / "plugins/obs-multi-replay"
    source_profile = target_profile = None
    if args.migrate_profile:
        if args.migrate_profile in (".", "..") or pathlib.Path(args.migrate_profile).name != args.migrate_profile:
            parser.error("Profile must be a single directory name")
        legacy = pathlib.Path(os.environ.get("XDG_CONFIG_HOME", pathlib.Path.home() / ".config")) / "obs-studio"
        source_profile = legacy / "basic/profiles" / args.migrate_profile / "basic.ini"
        target_profile = config / "basic/profiles" / args.migrate_profile / "basic.ini"
        if not source_profile.is_file() or not target_profile.is_file():
            parser.error("Both matching profiles must already exist; no files were changed")
    backup = install(args.package.resolve(), target, config / "replay-plugin-backups", source_profile, target_profile)
    print(f"Installed: {target}\nBackup: {backup}")
    if source_profile:
        print("Copied only the replay settings and hotkeys; other profile content was preserved.")
    print("Restart OBS and open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings.")


if __name__ == "__main__":
    main()

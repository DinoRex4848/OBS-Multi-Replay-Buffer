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
import contextlib
import fcntl
import platform
import struct

REPO = pathlib.Path(__file__).resolve().parent.parent
APP = "com.obsproject.Studio"
SECTION = "MultiLengthReplay"


def version_tuple(value):
    if not isinstance(value, str) or not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise RuntimeError("Invalid plugin version in installation metadata")
    return tuple(map(int, value.split(".")))


def host_run(arguments):
    try:
        return subprocess.run(arguments, text=True, capture_output=True, check=True,
                              timeout=30).stdout.strip()
    except FileNotFoundError as error:
        raise RuntimeError("Flatpak is required. Install Flatpak and OBS Studio first.") from error
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stderr.strip() or "Flatpak command failed") from error


def configuration_root():
    return pathlib.Path.home().resolve() / ".var/app" / APP / "config/obs-studio"


def check_target(manifest):
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("This installer requires an x86_64 Linux system")
    rows = host_run(["flatpak", "list", "--app", "--columns=application,arch,branch,installation"]).splitlines()
    matches = [row.split("\t") for row in rows if row.split("\t")[0] == APP]
    if not matches:
        raise RuntimeError("Flatpak OBS Studio is not installed. Install it before running setup.")
    if len(matches) != 1:
        raise RuntimeError("Multiple Flatpak OBS installations were found. Keep one user/system/branch installation before running setup.")
    _, arch, branch, installation = matches[0]
    if arch != "x86_64":
        raise RuntimeError("64-bit x86_64 Flatpak OBS Studio is required")
    scope = {"user": "--user", "system": "--system"}.get(installation, "--installation=" + installation)
    reference = f"{APP}/{arch}/{branch}"
    commit = host_run(["flatpak", "info", scope, "--show-commit", reference])
    if commit != manifest["app_commit"]:
        raise RuntimeError(f"This installer was built for Flatpak OBS {manifest['obs_version']} "
                           f"(commit {manifest['app_commit'][:12]}). Installed commit: {commit[:12]}. "
                           "Download an installer built for your OBS version. No plugin files were changed.")
    return f"Flatpak OBS {manifest['obs_version']} ({installation}, {branch})"


def installation_status(destination, incoming):
    version_tuple(incoming)
    if not destination.exists():
        return "Install", None
    metadata = destination / "manifest.json"
    current = None
    if metadata.is_file():
        current = json.loads(metadata.read_text()).get("plugin_version")
        if current is not None:
            if version_tuple(current) > version_tuple(incoming):
                raise RuntimeError(f"Version {current} is installed. Downgrading to {incoming} is blocked.")
    return ("Repair" if current == incoming else "Update"), current


def reject_symlinks(path):
    for item in (path, *path.parents):
        if item.is_symlink():
            raise RuntimeError(f"Refusing a symlinked installation path: {item}")


@contextlib.contextmanager
def installation_lock(destination):
    reject_symlinks(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    lock = destination.parent / ".obs-multi-replay.lock"
    descriptor = os.open(lock, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "w") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("Another plugin installer is already running") from error
        yield


def write_journal(path, value):
    temporary = path.with_suffix(".tmp")
    reject_symlinks(temporary)
    with temporary.open("w") as stream:
        json.dump(value, stream)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def recover(destination, target_profile=None):
    """Rollback an interrupted publish before accepting another operation."""
    journal = destination.parent / ".obs-multi-replay-transaction.json"
    reject_symlinks(journal)
    if not journal.exists():
        return
    record = json.loads(journal.read_text())
    tag = record["tag"]
    if not re.fullmatch(r"[0-9]{8}-[0-9]{6}-[a-f0-9]{8}", tag):
        raise RuntimeError("Invalid recovery metadata; retain the plugin backups and contact the maintainer")
    stage = destination.parent / (".obs-multi-replay-stage-" + tag)
    previous = destination.parent / (".obs-multi-replay-old-" + tag)
    for path in (stage, previous):
        reject_symlinks(path)
    if record.get("profile"):
        if not target_profile or str(target_profile) != record["profile"]:
            raise RuntimeError("An interrupted profile migration needs recovery. Rerun the original migration command.")
        if record["state"] != "committed":
            atomic_write(target_profile, bytes.fromhex(record["original_profile"]))
    if record["state"] != "committed":
        if previous.exists():
            if destination.exists():
                shutil.rmtree(destination)
            os.replace(previous, destination)
        elif not record["had_previous"] and destination.exists():
            shutil.rmtree(destination)
    if stage.exists():
        shutil.rmtree(stage)
    if previous.exists():
        shutil.rmtree(previous)
    journal.unlink()


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
    reject_symlinks(package)
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
    for name in ("bin/64bit/obs-multi-replay.so", "data/multi-replay-writer"):
        with (package / name).open("rb") as stream:
            header = stream.read(20)
        if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != 62:
            raise RuntimeError(f"Not an x86_64 ELF binary: {name}")
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
    with installation_lock(destination):
        recover(destination, target_profile)
        details = verify_package(package)
        if "plugin_version" in details:
            installation_status(destination, details["plugin_version"])
        return _install(package, destination, backup_root, source_profile, target_profile)


def _install(package, destination, backup_root, source_profile=None, target_profile=None):
    verify_package(package)
    if destination.is_symlink():
        raise RuntimeError("Refusing to replace a symlinked plugin directory")
    migrated = migrate_settings(source_profile, target_profile) if source_profile else None
    original = target_profile.read_bytes() if source_profile else None
    destination.parent.mkdir(parents=True, exist_ok=True)
    tag = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    backup = backup_root / tag
    reject_symlinks(backup)
    backup.mkdir(parents=True)
    stage = destination.parent / (".obs-multi-replay-stage-" + tag)
    previous = destination.parent / (".obs-multi-replay-old-" + tag)
    published = False
    settings_written = False
    journal = destination.parent / ".obs-multi-replay-transaction.json"
    record = {"tag": tag, "state": "publishing", "had_previous": destination.exists(),
              "profile": str(target_profile) if source_profile else None,
              "original_profile": original.hex() if original is not None else None}
    try:
        if destination.exists():
            shutil.copytree(destination, backup / "plugin")
        if target_profile:
            shutil.copytree(target_profile.parent, backup / "profile")
        shutil.copytree(package, stage)
        verify_package(stage)
        write_journal(journal, record)
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
        record["state"] = "committed"
        write_journal(journal, record)
    except BaseException:
        if settings_written:
            atomic_write(target_profile, original)
        if published and destination.exists():
            shutil.rmtree(destination)
        if previous.exists():
            os.replace(previous, destination)
        journal.unlink(missing_ok=True)
        raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    if previous.exists():
        shutil.rmtree(previous)
    journal.unlink(missing_ok=True)
    return backup


def uninstall(destination, backup_root):
    with installation_lock(destination):
        recover(destination)
        if not destination.exists():
            return None
        tag = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
        backup = backup_root / tag
        reject_symlinks(backup)
        backup.mkdir(parents=True)
        shutil.copytree(destination, backup / "plugin")
        journal = destination.parent / ".obs-multi-replay-transaction.json"
        record = {"tag": tag, "state": "publishing", "had_previous": True, "profile": None}
        previous = destination.parent / (".obs-multi-replay-old-" + tag)
        write_journal(journal, record)
        try:
            os.replace(destination, previous)
            record["state"] = "committed"
            write_journal(journal, record)
        except BaseException:
            recover(destination)
            raise
        recover(destination)
        return backup


def perform(package, action, config=None):
    config = config or configuration_root()
    target = config / "plugins/obs-multi-replay"
    if obs_running():
        raise RuntimeError("Close OBS normally before installing or removing the plugin")
    if action == "Remove":
        return uninstall(target, config / "replay-plugin-backups")
    manifest = verify_package(package)
    check_target(manifest)
    if obs_running():
        raise RuntimeError("Close OBS before continuing")
    return install(package, target, config / "replay-plugin-backups")


def obs_running():
    result = host_run(["flatpak", "ps", "--columns=application"]) if shutil.which("flatpak") else ""
    for path in pathlib.Path("/proc").glob("[0-9]*/comm"):
        try:
            if path.read_text().strip() in ("obs", "obs64"):
                return True
        except OSError:
            continue
    return APP in result.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, default=REPO / "dist/flatpak-x86_64/obs-multi-replay")
    parser.add_argument("--migrate-profile", help="Matching legacy and Flatpak profile directory name, e.g. Untitled")
    parser.add_argument("--uninstall", action="store_true", help="Remove only this plugin, retaining settings and backups")
    args = parser.parse_args()
    if obs_running():
        parser.error("Close OBS normally before installing; no files were changed")
    if args.uninstall:
        backup = perform(args.package, "Remove")
        print(f"Plugin removed. Profiles, hotkeys and recordings are preserved. Backup: {backup}")
        return
    manifest = verify_package(args.package)
    check_target(manifest)
    config = configuration_root()
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

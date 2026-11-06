"""Transactional install engine for the Flatpak OBS plugin, shipped in PluginFiles beside installer.py."""
import contextlib
import datetime
import fcntl
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import uuid

APP = "com.obsproject.Studio"
# Must match scripts/project.py FFMPEG_ABI; this file ships alone, so it keeps its own copy.
FFMPEG_ABI = {"avformat": 62, "avcodec": 62, "avutil": 60}


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
        raise RuntimeError("Flatpak OBS Studio is not installed. This package cannot install into native RPM/deb OBS. "
                           "Use a plugin build matching your native distribution, or install Flatpak OBS first. "
                           "Your existing OBS configuration has not been changed.")
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


def recover(destination):
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
        # Only the retired developer --migrate-profile command wrote this.
        raise RuntimeError("An interrupted settings migration from an old developer installer needs manual recovery. "
                           "The previous plugin and profile are in replay-plugin-backups.")
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


def verify_package(package):
    reject_symlinks(package)
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("target") != "flatpak" or manifest.get("architecture") != "x86_64" or manifest.get("integration_test") is not False:
        raise RuntimeError("This is not an x86_64 production Flatpak package")
    if manifest.get("ffmpeg_abi") != FFMPEG_ABI:
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


def install(package, destination, backup_root):
    with installation_lock(destination):
        recover(destination)
        details = verify_package(package)
        if "plugin_version" in details:
            installation_status(destination, details["plugin_version"])
        return _install(package, destination, backup_root)


def _install(package, destination, backup_root):
    if destination.is_symlink():
        raise RuntimeError("Refusing to replace a symlinked plugin directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    tag = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    backup = backup_root / tag
    reject_symlinks(backup)
    backup.mkdir(parents=True)
    stage = destination.parent / (".obs-multi-replay-stage-" + tag)
    previous = destination.parent / (".obs-multi-replay-old-" + tag)
    published = False
    journal = destination.parent / ".obs-multi-replay-transaction.json"
    record = {"tag": tag, "state": "publishing", "had_previous": destination.exists()}
    try:
        if destination.exists():
            shutil.copytree(destination, backup / "plugin")
        shutil.copytree(package, stage)
        verify_package(stage)
        write_journal(journal, record)
        if destination.exists():
            os.replace(destination, previous)
        os.replace(stage, destination)
        published = True
        (backup / "installation.json").write_text(json.dumps({"destination": str(destination)}, indent=2) + "\n")
        record["state"] = "committed"
        write_journal(journal, record)
    except BaseException:
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
        record = {"tag": tag, "state": "publishing", "had_previous": True}
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
    check_target(verify_package(package))
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

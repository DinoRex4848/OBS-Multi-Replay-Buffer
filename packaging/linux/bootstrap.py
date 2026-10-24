"""Embedded in the single-file shell installer; uses only Python's standard library."""
import hashlib
import io
import os
import pathlib
import subprocess
import sys
import tarfile
import tempfile


def main():
    if os.environ.get("SUDO_USER") or os.environ.get("SUDO_UID") or os.geteuid() == 0:
        raise RuntimeError("Run this installer as your desktop user, without sudo.")
    bundle, digest, *arguments = sys.argv[1:]
    action = "install"
    if "--uninstall" in arguments:
        arguments.remove("--uninstall")
        action = "uninstall"
    if any(arg not in ("--non-interactive", "--help", "-h") for arg in arguments):
        raise RuntimeError("Usage: sh INSTALLER.run [--uninstall] [--non-interactive]")
    if "--help" in arguments or "-h" in arguments:
        print("Usage: sh INSTALLER.run [--uninstall] [--non-interactive]\n"
              "Linux x86_64, matching Flatpak OBS, Python 3.10+. Run without sudo.")
        return 0
    with open(bundle, "rb") as source:
        # Marker is an entire line, so it cannot match the embedded Python code.
        for line in source:
            if line == b"__REPLAY_ARCHIVE_BELOW__\n":
                break
        else:
            raise RuntimeError("Installer payload is missing. Download the entire .run file again.")
        data = source.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError("Installer is damaged. Download it again and verify its .sha256 file.")
    with tempfile.TemporaryDirectory(prefix="obs-multi-replay-") as directory:
        root = pathlib.Path(directory)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            seen = set()
            total = 0
            for entry in archive.getmembers():
                path = pathlib.PurePosixPath(entry.name)
                total += entry.size
                if (not path.parts or path.is_absolute() or ".." in path.parts or "\\" in entry.name
                        or not (entry.isfile() or entry.isdir()) or entry.name in seen
                        or total > 512 * 1024 * 1024 or len(seen) > 10000):
                    raise RuntimeError("Unsafe installer archive. No plugin files were changed.")
                seen.add(entry.name)
            if {name.split("/")[0] for name in seen} != {"install.sh", "uninstall.sh", "README.txt", "PluginFiles"}:
                raise RuntimeError("Unexpected installer archive layout.")
            # Members have been checked: no links, special files or escaping paths.
            for entry in archive.getmembers():
                entry.mode &= 0o777
                archive.extract(entry, root)
        state = pathlib.Path(os.environ.get("XDG_STATE_HOME", ""))
        if not state.is_absolute():
            state = pathlib.Path.home() / ".local/state"
        return subprocess.call([sys.executable, str(root / "PluginFiles/installer.py"),
                                "--action", action, "--log-directory", str(state / "obs-multi-replay/logs"),
                                *arguments])


try:
    sys.exit(main())
except (OSError, ValueError, tarfile.TarError, RuntimeError) as error:
    print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    print("Cancelled.", file=sys.stderr)
    sys.exit(130)

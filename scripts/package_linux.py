#!/usr/bin/env python3
"""Create the four-entry offline script package from a verified Flatpak payload."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import tarfile

import replay_installer as core

REPO = pathlib.Path(__file__).resolve().parents[1]


def package_scripts(payload, output):
    details = core.verify_package(payload)
    version = re.search(r"project\(obs-multi-replay VERSION ([\d.]+)", (REPO / "CMakeLists.txt").read_text())[1]
    if details["plugin_version"] != version:
        raise RuntimeError("Payload version does not match CMake; rebuild first")
    stage = output / "linux-installer"
    if stage.exists():
        shutil.rmtree(stage)
    private = stage / "PluginFiles"
    private.mkdir(parents=True)
    shutil.copytree(payload, private / "payload/obs-multi-replay")
    shutil.copy2(REPO / "scripts/replay_installer.py", private / "replay_installer.py")
    shutil.copy2(REPO / "packaging/linux/installer.py", private / "installer.py")
    for name in ("install.sh", "uninstall.sh", "README.txt"):
        shutil.copy2(REPO / "packaging/linux" / name, stage / name)
    for name in ("install.sh", "uninstall.sh"):
        (stage / name).chmod(0o755)
    archive = output / f"obs-multi-replay-{version}-linux-flatpak-x86_64.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        for entry in sorted(stage.iterdir()):
            tar.add(entry, arcname=entry.name)
    archive.with_name(archive.name + ".sha256").write_text(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n")
    (output / "linux-build.json").write_text(json.dumps({k: v for k, v in details.items() if k != "files"}, indent=2) + "\n")
    (output / "linux-installer-notices.txt").write_text((REPO / "LICENSE").read_text() + "\n" + (REPO / "docs/THIRD_PARTY.md").read_text())
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, default=REPO / "dist/flatpak-x86_64/obs-multi-replay")
    parser.add_argument("--output", type=pathlib.Path, default=REPO / "dist")
    args = parser.parse_args()
    print(package_scripts(args.package, args.output))

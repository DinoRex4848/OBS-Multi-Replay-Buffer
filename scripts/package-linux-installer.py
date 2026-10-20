#!/usr/bin/env python3
"""Build the offline ELF setup wizard from a validated production Flatpak payload."""
import argparse
import hashlib
import json
import importlib.metadata
import pathlib
import re
import subprocess
import sys
import tarfile

import replay_installer as core

REPO = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, default=REPO / "dist/flatpak-x86_64/obs-multi-replay")
    parser.add_argument("--output", type=pathlib.Path, default=REPO / "dist")
    args = parser.parse_args()
    metadata = core.verify_package(args.package)
    version = re.search(r"project\(obs-multi-replay VERSION ([\d.]+)", (REPO / "CMakeLists.txt").read_text())[1]
    if metadata["plugin_version"] != version:
        raise RuntimeError("Payload version does not match CMake; rebuild before packaging")
    stage = REPO / "build-linux-installer"
    stage.mkdir(exist_ok=True)
    archive = stage / "payload.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        for path in sorted(args.package.rglob("*")):
            output.add(path, arcname=path.relative_to(args.package).as_posix(), recursive=False)
    notices = stage / "installer-notices.txt"
    notices.write_text("This installer bundles Python, PySide6/Qt, Shiboken6 and PyInstaller.\n"
                       "The plugin is GPL-2.0-or-later; see its LICENSE.\n\n")
    with notices.open("a") as output:
        for name in ("PySide6-Essentials", "shiboken6", "pyinstaller"):
            distribution = importlib.metadata.distribution(name)
            output.write(f"\n{name} {distribution.version}\n")
            output.write(f"License: {distribution.metadata.get('License', 'See included license files')}\n")
            output.write("\n".join(distribution.metadata.get_all("Project-URL") or []) + "\n")
            for file in distribution.files or []:
                if any(part.lower() in ("licenses", "license", "copying", "copyright") or
                       part.upper().startswith(("LICENSE", "COPYING", "COPYRIGHT")) for part in file.parts):
                    path = distribution.locate_file(file)
                    if path.is_file():
                        output.write(f"\n{file}\n{path.read_text(errors='replace')}\n")
        # The PySide/Shiboken wheels have SPDX metadata but no license texts.
        # Debian/Ubuntu supplies the full standard texts and Python's notices.
        # Also retain the builder's system-library copyright files, because
        # PyInstaller may collect their shared libraries into the executable.
        common = pathlib.Path("/usr/share/common-licenses")
        for name in ("LGPL-3", "GPL-3", "LGPL-2.1", "GPL-2", "BSD", "Apache-2.0"):
            path = common / name
            if not path.is_file():
                raise RuntimeError("Build on Ubuntu 22.04 with its common license texts installed")
            output.write(f"\n{name}\n{path.read_text()}\n")
        python_notice = pathlib.Path(f"/usr/share/doc/python{sys.version_info.major}.{sys.version_info.minor}/copyright")
        if not python_notice.is_file():
            raise RuntimeError("Python runtime copyright/license text is missing on the builder")
        output.write("\nPython runtime license:\n" + python_notice.read_text() + "\n")
        output.write("\nSystem library/build environment copyright notices:\n")
        for path in sorted(pathlib.Path("/usr/share/doc").glob("*/copyright")):
            output.write(f"\n{path.parent.name}\n{path.read_text(errors='replace')}\n")
    name = f"obs-multi-replay-{version}-linux-flatpak-x86_64-setup.run"
    subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile",
                    "--name", name, "--distpath", str(args.output), "--workpath", str(stage / "work"),
                    "--specpath", str(stage), "--paths", str(REPO / "scripts"),
                    "--add-data", f"{archive}:.", "--add-data", f"{notices}:.",
                    str(REPO / "packaging/linux/setup.py")], check=True)
    executable = args.output / name
    executable.chmod(0o755)
    executable.with_suffix(".run.sha256").write_text(f"{hashlib.sha256(executable.read_bytes()).hexdigest()}  {name}\n")
    import shutil
    shutil.copy2(notices, args.output / "linux-installer-notices.txt")
    (args.output / "linux-build.json").write_text(json.dumps(
        {key: value for key, value in metadata.items() if key != "files"}, indent=2) + "\n")
    print(executable)


if __name__ == "__main__":
    main()

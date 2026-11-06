#!/usr/bin/env python3
"""Package a Flatpak OBS build as the offline Linux installers (.tar.gz and single-file .run).

  package_linux.py --build build-flatpak --metadata-only   record the Flatpak SDK/app facts of a build
  package_linux.py --build build-flatpak                    create dist/ payload, archive and .run
"""
import argparse
import hashlib
import json
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile

import project
from project import FFMPEG_ABI, FLATPAK_APP, REPO

sys.path.insert(0, str(REPO / "packaging/linux"))
import replay_installer as core  # noqa: E402  (shipped beside installer.py, not in scripts/)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_checksum(path):
    path.with_name(path.name + ".sha256").write_text(f"{digest(path)}  {path.name}\n")


def metadata(build):
    """Facts about the installed Flatpak OBS this build was made against."""
    def info(option):
        return subprocess.check_output(["flatpak", "info", option, FLATPAK_APP], text=True).strip()

    app = pathlib.Path(info("--show-location")) / "files"
    obs = re.search(r'set\(PACKAGE_VERSION "([\d.]+)"\)',
                    (app / "lib/cmake/libobs/libobsConfigVersion.cmake").read_text())[1]
    ffmpeg = re.search(r'#define FFMPEG_VERSION "([^"]+)"', (app / "include/libavutil/ffversion.h").read_text())[1]
    qt = re.search(r'QT_VERSION_STR "([^"]+)"', (app / "include/qt/QtCore/qconfig.h").read_text())[1]
    cache = (build / "CMakeCache.txt").read_text()
    for key in ["OBS_LIBRARY", "REPLAY_FRONTEND", "avformat_LIBRARY", "avcodec_LIBRARY", "avutil_LIBRARY"]:
        if not re.search(rf"^{key}:FILEPATH=/app/lib/", cache, re.MULTILINE):
            raise RuntimeError(f"{key} was not built against the installed Flatpak's /app/lib")
    return {"target": "flatpak", "app_id": FLATPAK_APP, "architecture": "x86_64",
            "app_commit": info("--show-commit"), "sdk": info("--show-sdk"),
            "obs_version": obs, "ffmpeg_version": ffmpeg, "qt_version": qt,
            "ffmpeg_abi": dict(FFMPEG_ABI),
            "integration_test": "REPLAY_INTEGRATION_TEST:BOOL=ON" in cache}


def check_binary(source, ffmpeg_abi):
    header = subprocess.check_output(["readelf", "-h", str(source)], text=True)
    if "ELF64" not in header or "Advanced Micro Devices X86-64" not in header:
        raise RuntimeError(f"Not an x86_64 ELF binary: {source}")
    dynamic = subprocess.check_output(["readelf", "-d", str(source)], text=True)
    for component, major in ffmpeg_abi.items():
        if f"[lib{component}.so.{major}]" not in dynamic:
            raise RuntimeError(f"Wrong FFmpeg dependency in {source}: {component}")
    if ".deps/" in dynamic or str(REPO) in dynamic:
        raise RuntimeError(f"Local build paths in runtime search path: {source}")


def package_payload(build):
    """Stage the plugin files with a hashed manifest under dist/flatpak-x86_64."""
    details = json.loads((build / "flatpak-build.json").read_text())
    cache = (build / "CMakeCache.txt").read_text()
    if details["integration_test"] or "REPLAY_INTEGRATION_TEST:BOOL=OFF" not in cache:
        raise RuntimeError("Refusing to package an integration test driver")
    if metadata(build) != details:
        raise RuntimeError("The installed OBS changed since this build; rebuild before packaging")
    root = REPO / "dist/flatpak-x86_64"
    root.mkdir(parents=True, exist_ok=True)
    payload = root / project.PLUGIN
    stage = root / f".{project.PLUGIN}-stage"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "bin/64bit").mkdir(parents=True)
    (stage / "data").mkdir()
    binaries = {"bin/64bit/obs-multi-replay.so": build / "obs-multi-replay.so",
                "data/multi-replay-writer": build / "multi-replay-writer"}
    for destination, source in binaries.items():
        check_binary(source, details["ffmpeg_abi"])
        shutil.copy2(source, stage / destination)
    (stage / "data/multi-replay-writer").chmod(0o755)
    shutil.copytree(REPO / "data/locale", stage / "data/locale")
    project.copy_user_docs(stage)
    details["plugin_version"] = project.version()
    details["files"] = {str(p.relative_to(stage)): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
    (stage / "manifest.json").write_text(json.dumps(details, indent=2) + "\n")
    if payload.exists():
        shutil.rmtree(payload)
    stage.rename(payload)
    return payload


def package_run(archive):
    """One offline download; stdin stays connected to the confirmation prompt."""
    bootstrap = (REPO / "packaging/linux/bootstrap.py").read_text()
    payload = archive.read_bytes()
    header = ("#!/bin/sh\n"
              "if ! command -v python3 >/dev/null 2>&1; then\n"
              "  echo 'ERROR: Python 3.10+ is required. Install it using your distribution package manager.' >&2\n"
              "  exit 1\nfi\n"
              "if ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then\n"
              "  echo 'ERROR: Python 3.10+ is required; your python3 is too old.' >&2\n"
              "  exit 1\nfi\n"
              f"python3 -c {shlex.quote(bootstrap)} \"$0\" {hashlib.sha256(payload).hexdigest()} \"$@\"\n"
              "exit $?\n__REPLAY_ARCHIVE_BELOW__\n").encode()
    installer = archive.with_name(archive.name.removesuffix(".tar.gz") + ".run")
    installer.write_bytes(header + payload)
    installer.chmod(0o755)
    write_checksum(installer)
    return installer


def package_scripts(payload, output):
    """Create the four-entry .tar.gz (install.sh, uninstall.sh, README.txt, PluginFiles) and the .run."""
    details = core.verify_package(payload)
    version = project.version()
    if details["plugin_version"] != version:
        raise RuntimeError("Payload version does not match CMake; rebuild first")
    stage = output / "linux-installer"
    if stage.exists():
        shutil.rmtree(stage)
    private = stage / "PluginFiles"
    private.mkdir(parents=True)
    shutil.copytree(payload, private / "payload" / project.PLUGIN)
    for name in ("replay_installer.py", "installer.py"):
        shutil.copy2(REPO / "packaging/linux" / name, private / name)
    for name in ("install.sh", "uninstall.sh", "README.txt"):
        shutil.copy2(REPO / "packaging/linux" / name, stage / name)
    for name in ("install.sh", "uninstall.sh"):
        (stage / name).chmod(0o755)
    archive = output / f"{project.PLUGIN}-{version}-linux-flatpak-x86_64.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        for entry in sorted(stage.iterdir()):
            tar.add(entry, arcname=entry.name)
    write_checksum(archive)
    (output / "linux-build.json").write_text(json.dumps({k: v for k, v in details.items() if k != "files"}, indent=2) + "\n")
    (output / "linux-installer-notices.txt").write_text((REPO / "LICENSE").read_text() + "\n" + (REPO / "docs/THIRD_PARTY.md").read_text())
    package_run(archive)
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build-flatpak")
    parser.add_argument("--metadata-only", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    if args.metadata_only:
        (build / "flatpak-build.json").write_text(json.dumps(metadata(build), indent=2) + "\n")
    else:
        print(package_scripts(package_payload(build), REPO / "dist"))

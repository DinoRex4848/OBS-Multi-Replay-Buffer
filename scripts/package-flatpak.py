#!/usr/bin/env python3
"""Package an OBS Flatpak build separately from the Fedora RPM payload."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

REPO = pathlib.Path(__file__).resolve().parent.parent
APP = "com.obsproject.Studio"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metadata(build):
    def info(option):
        return subprocess.check_output(["flatpak", "info", option, APP], text=True).strip()

    app = pathlib.Path(info("--show-location")) / "files"
    obs = re.search(r'set\(PACKAGE_VERSION "([\d.]+)"\)',
                    (app / "lib/cmake/libobs/libobsConfigVersion.cmake").read_text())[1]
    ffmpeg = re.search(r'#define FFMPEG_VERSION "([^"]+)"',
                       (app / "include/libavutil/ffversion.h").read_text())[1]
    qt = re.search(r'QT_VERSION_STR "([^"]+)"',
                   (app / "include/qt/QtCore/qconfig.h").read_text())[1]
    cache = (build / "CMakeCache.txt").read_text()
    for key in ["OBS_LIBRARY", "REPLAY_FRONTEND", "avformat_LIBRARY", "avcodec_LIBRARY", "avutil_LIBRARY"]:
        if not re.search(rf"^{key}:FILEPATH=/app/lib/", cache, re.MULTILINE):
            raise RuntimeError(f"{key} was not built against the installed Flatpak's /app/lib")
    return {"target": "flatpak", "app_id": APP, "architecture": "x86_64",
            "app_commit": info("--show-commit"), "sdk": info("--show-sdk"),
            "obs_version": obs, "ffmpeg_version": ffmpeg, "qt_version": qt,
            "ffmpeg_abi": {"avformat": 62, "avcodec": 62, "avutil": 60},
            "integration_test": "REPLAY_INTEGRATION_TEST:BOOL=ON" in cache}


def package(build):
    details = json.loads((build / "flatpak-build.json").read_text())
    cache = (build / "CMakeCache.txt").read_text()
    if details["integration_test"] or "REPLAY_INTEGRATION_TEST:BOOL=OFF" not in cache:
        raise RuntimeError("Refusing to package an integration test driver")
    if metadata(build) != details:
        raise RuntimeError("The installed OBS changed since this build; rebuild before packaging")
    version = re.search(r"project\(obs-multi-replay VERSION ([\d.]+)", (REPO / "CMakeLists.txt").read_text())[1]
    root = REPO / "dist/flatpak-x86_64"
    root.mkdir(parents=True, exist_ok=True)
    payload = root / "obs-multi-replay"
    stage = root / ".obs-multi-replay-stage"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "bin/64bit").mkdir(parents=True)
    (stage / "data").mkdir()
    binaries = {"bin/64bit/obs-multi-replay.so": build / "obs-multi-replay.so",
                "data/multi-replay-writer": build / "multi-replay-writer"}
    for destination, source in binaries.items():
        header = subprocess.check_output(["readelf", "-h", str(source)], text=True)
        if "ELF64" not in header or "Advanced Micro Devices X86-64" not in header:
            raise RuntimeError(f"Not an x86_64 ELF binary: {source}")
        dynamic = subprocess.check_output(["readelf", "-d", str(source)], text=True)
        for component, major in details["ffmpeg_abi"].items():
            if f"[lib{component}.so.{major}]" not in dynamic:
                raise RuntimeError(f"Wrong FFmpeg dependency in {source}: {component}")
        if ".deps/" in dynamic or str(REPO) in dynamic:
            raise RuntimeError(f"Local build paths in runtime search path: {source}")
        shutil.copy2(source, stage / destination)
    (stage / "data/multi-replay-writer").chmod(0o755)
    shutil.copytree(REPO / "data/locale", stage / "data/locale")
    for name in ["README.md", "LICENSE", "CHANGELOG.md"]:
        shutil.copy2(REPO / name, stage / name)
    shutil.copytree(REPO / "docs", stage / "docs")
    details["plugin_version"] = version
    details["files"] = {str(p.relative_to(stage)): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
    (stage / "manifest.json").write_text(json.dumps(details, indent=2) + "\n")
    if payload.exists():
        shutil.rmtree(payload)
    stage.rename(payload)
    from package_linux import package_scripts
    print(package_scripts(payload, REPO / "dist"))



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build-flatpak")
    parser.add_argument("--metadata-only", action="store_true")
    args = parser.parse_args()
    if args.metadata_only:
        (args.build / "flatpak-build.json").write_text(json.dumps(metadata(args.build), indent=2) + "\n")
    else:
        package(args.build.resolve())

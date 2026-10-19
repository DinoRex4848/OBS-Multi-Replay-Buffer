#!/usr/bin/env python3
"""Package the native module with OBS's user plugin directory layout."""
import argparse
import pathlib
import shutil
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=pathlib.Path, default=pathlib.Path("build"))
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parent.parent
build = args.build.resolve()
cache = (build / "CMakeCache.txt").read_text()
if "REPLAY_INTEGRATION_TEST:BOOL=OFF" not in cache:
    raise SystemExit("Refusing to package a build with the isolated UI test driver enabled")
package = repo / "dist/fedora-x86_64/obs-multi-replay"
(package / "bin/64bit").mkdir(parents=True, exist_ok=True)
(package / "data").mkdir(parents=True, exist_ok=True)
shutil.copy2(build / "obs-multi-replay.so", package / "bin/64bit")
shutil.copy2(build / "multi-replay-writer", package / "data")
shutil.copytree(repo / "data/locale", package / "data/locale", dirs_exist_ok=True)
for name in ["README.md", "LICENSE", "CHANGELOG.md"]:
    shutil.copy2(repo / name, package / pathlib.Path(name).name)
shutil.copytree(repo / "docs", package / "docs", dirs_exist_ok=True)
archive = repo / "dist/obs-multi-replay-0.1.0-fedora44-x86_64.tar.gz"
with tarfile.open(archive, "w:gz") as output:
    output.add(package, arcname="obs-multi-replay")
print(archive)

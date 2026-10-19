#!/usr/bin/env bash
# Builds, tests and packages the Fedora plugin inside a Fedora container, so the
# host needs neither development packages nor sudo. Requires rootless podman.
# The container release defaults to the host's, so the result matches host OBS.
#   ./scripts/build-container.sh [build-directory-name]
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_name=${1:-build-container}
release=${FEDORA_RELEASE:-$(. /etc/os-release && echo "$VERSION_ID")}
jobs=${BUILD_JOBS:-$(nproc)}
exec podman run --rm --security-opt label=disable -v "$repo_dir":/src \
  "registry.fedoraproject.org/fedora:$release" sh -euc "
    dnf install -y -q --setopt=install_weak_deps=False gcc-c++ cmake ninja-build obs-studio-devel qt6-qtbase-devel ffmpeg-free-devel python3
    cmake -S /src -B /src/$build_name -G Ninja -DCMAKE_BUILD_TYPE=Release -DREPLAY_INTEGRATION_TEST=OFF
    cmake --build /src/$build_name --parallel $jobs
    ctest --test-dir /src/$build_name --output-on-failure
    python3 /src/scripts/package-fedora.py --build /src/$build_name
    rpm -q obs-studio-devel qt6-qtbase-devel libavformat-free-devel gcc-c++
  "

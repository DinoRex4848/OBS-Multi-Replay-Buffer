#!/usr/bin/env bash
# Build against the installed OBS app, never the host's OBS/Qt/FFmpeg packages.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=OFF
build_name=build-flatpak
if [[ ${1:-} == --ui-test ]]; then
  mode=ON
  build_name=build-flatpak-ui
  shift
fi
if (( $# )); then
  echo "Usage: $0 [--ui-test]" >&2
  exit 2
fi
sdk=$(flatpak info --show-sdk com.obsproject.Studio)
app_commit=$(flatpak info --show-commit com.obsproject.Studio)
if ! flatpak info "$sdk" >/dev/null 2>&1; then
  echo "Install the matching SDK first: flatpak install --user flathub $sdk" >&2
  exit 1
fi
flatpak run --devel --filesystem="$repo_dir" --command=sh com.obsproject.Studio -euc '
  repo_dir=$1
  build_dir=$2
  mode=$3
  export PKG_CONFIG_PATH=/app/lib/pkgconfig
  cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/app -DFFMPEG_ROOT=/app \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH=/app/lib -DREPLAY_LOCAL_DEPS= \
    -DBUILD_PLUGIN=ON -DBUILD_TESTING=ON -DREPLAY_INTEGRATION_TEST="$mode"
  cmake --build "$build_dir" --parallel
  ctest --test-dir "$build_dir" --output-on-failure
  for helper in obs-nvenc-test obs-ffmpeg-mux; do
    ln -sfn "/app/bin/$helper" "$build_dir/$helper"
  done
' sh "$repo_dir" "$repo_dir/$build_name" "$mode"
if [[ $(flatpak info --show-commit com.obsproject.Studio) != "$app_commit" ]]; then
  echo "OBS changed during the build. Run the build again against the updated app." >&2
  exit 1
fi
python3 "$repo_dir/scripts/package-flatpak.py" --build "$repo_dir/$build_name" --metadata-only
if [[ $mode == OFF ]]; then
  python3 "$repo_dir/scripts/package-flatpak.py" --build "$repo_dir/$build_name"
fi

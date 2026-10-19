#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$repo_dir/dist/fedora-x86_64/obs-multi-replay"
destination="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins/obs-multi-replay"
if [[ ! -f "$source_dir/bin/64bit/obs-multi-replay.so" ]]; then
  echo "Build and package the Fedora plugin first (see README.md)." >&2
  exit 1
fi
mkdir -p "$destination"
cp -a "$source_dir/." "$destination/"
echo "Installed to $destination. Restart OBS and open Docks > Multi-Length Replay."

#!/usr/bin/env bash
# Run existing tests within one OBS namespace so window/process ownership agrees.
# --runtime runs compiled probes without the SDK, matching ordinary OBS use.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
runtime=(--devel)
if [[ ${1:-} == --runtime ]]; then
  runtime=()
  shift
fi
if (( ! $# )); then
  echo "Usage: $0 [--runtime] COMMAND [ARGUMENT ...]" >&2
  exit 2
fi
if flatpak ps --columns=application | rg -qx com.obsproject.Studio || pgrep -x obs >/dev/null; then
  echo "Close OBS normally before running GPU/UI tests." >&2
  exit 1
fi
exec flatpak run "${runtime[@]}" --socket=x11 --filesystem="$repo_dir" --command=sh com.obsproject.Studio -euc '
  cd "$1"
  shift
  export OBS_ROOT=/app
  export QT_QPA_PLATFORM=xcb
  exec "$@"
' sh "$repo_dir" "$@"

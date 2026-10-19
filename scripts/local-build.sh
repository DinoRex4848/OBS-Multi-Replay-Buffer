#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps_dir="$repo_dir/.deps/root"
export LD_LIBRARY_PATH="$deps_dir/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$deps_dir/usr/bin/cmake" "$@"

#!/usr/bin/env bash
# Run only on an isolated builder: installs the pinned release target and SDK.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target="$repo_dir/packaging/linux/flatpak-target.json"
app_ref=$(python3 -c 'import json,sys; t=json.load(open(sys.argv[1])); print(t["app_id"]+"/"+t["architecture"]+"/"+t["branch"])' "$target")
commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["app_commit"])' "$target")
sdk=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sdk"])' "$target")
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user --noninteractive -y flathub "$app_ref" "$sdk"
flatpak update --user --noninteractive -y --commit="$commit" "$app_ref"
test "$(flatpak info --user --show-commit "$app_ref")" = "$commit"

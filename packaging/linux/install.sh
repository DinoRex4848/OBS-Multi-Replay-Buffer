#!/bin/sh
# All paths are relative to this extracted package, never the working directory.
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
private="$package_dir/PluginFiles"
noninteractive=0
for argument do
    if [ "$argument" = --non-interactive ]; then noninteractive=1; fi
done
status=0
if [ ! -f "$private/installer.py" ] || [ ! -f "$private/replay_installer.py" ]; then
    message="ERROR: Extract the entire archive before running this script."
    status=1
elif ! command -v python3 >/dev/null 2>&1; then
    message="ERROR: Python 3.10 or newer is required. Install it with your distribution's package manager."
    status=1
elif ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then
    message="ERROR: Python 3.10 or newer is required. Your python3 is too old."
    status=1
else
    python3 "$private/installer.py" --action install "$@"
    status=$?
fi
if [ "$status" -ne 0 ] && [ -n "${message:-}" ]; then
    printf '%s\n' "$message"
    if mkdir -p "$private/Logs"; then
        printf '%s\n' "$message" > "$private/Logs/bootstrap-$$.log"
        printf 'Log: %s\n' "$private/Logs/bootstrap-$$.log"
    fi
fi
if [ "$noninteractive" -eq 0 ] && [ -t 0 ] && [ -t 1 ]; then
    printf 'Press Enter to close: '
    IFS= read -r answer || :
fi
exit "$status"

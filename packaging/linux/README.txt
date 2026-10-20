OBS Multi-Length Replay - Linux x86_64 / Flatpak OBS

1. Close OBS. Extract this entire archive into a writable folder.
2. Open a terminal in the extracted folder and run: sh ./install.sh
3. Restart OBS. Open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings.

Run install.sh again to update or repair. Run sh ./uninstall.sh to remove only
this plugin. Keep this package, or use a newer package's removal script.
Scripts may also be opened with "Run in Terminal" if your file manager supports it.

Requirements: Python 3.10 or newer, Flatpak, and matching x86_64 Flatpak OBS.
No Python packages, Qt installer runtime, development SDK or sudo is needed.
Install missing Python/Flatpak prerequisites using your distribution's package
manager. The plugin package must match your EXACT OBS Flatpak commit. See
PluginFiles/payload/obs-multi-replay/manifest.json or the release's linux-build.json.
A mismatched OBS update needs a matching plugin build. Native RPM/deb OBS is
not supported by this installer. User and system Flatpak OBS are supported;
multiple matching app installations are reported as ambiguous.

The installer displays its action and destination before asking to continue.
For automation use: sh ./install.sh --non-interactive
Removal also accepts --non-interactive, even if OBS or the payload has changed.
The destination is your Flatpak OBS configuration's plugins/obs-multi-replay.
Profiles, scenes, hotkeys, replay preferences and recordings are not changed.
Existing plugin backups are retained in replay-plugin-backups beside plugins.

All supporting files and licenses are in PluginFiles. Logs are in PluginFiles/Logs.
Send the error and its log if something fails. Interrupted operations recover
when you rerun the script. Retain recovery files if permissions prevent recovery.
This plugin is experimental; hardware-dependent capture testing is separate
from installation and production-load checks.

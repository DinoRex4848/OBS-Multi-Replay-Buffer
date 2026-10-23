# Installing Multi-Length Replay

Close OBS normally before installing, repairing or removing the plugin. Installers
replace only this plugin's binaries and data. Profiles, scenes, replay preferences,
hotkeys, recordings and other plugins are preserved. No installer changes OBS itself.
This revision is experimental; see [local validation](INSTALLER_REVIEW.md) before sharing.

## Windows 10/11 x64: normally installed OBS

Download **`obs-multi-replay-0.1.2-windows-x64-setup.exe`**. Double-click it and approve
the administrator prompt at startup. Check the detected OBS folder and click Install.
No ZIP extraction, Python, SDK, compiler or additional PowerShell installation is
needed. The package includes the writer's dependencies and works offline.

Setup checks OBS's application and uninstall registry keys, then Program Files.
If necessary, browse to the OBS root containing `bin\64bit\obs64.exe`. The plugin
requires OBS 32.1 or newer; Windows builds target OBS 32.1.1. The minimum is not a
promise of compatibility with every future release. Portable marker files cause
setup to direct you to the separate ZIP instead.

The plugin goes into `C:\ProgramData\obs-studio\plugins\obs-multi-replay`.
Installer support and the uninstaller go into `C:\Program Files\OBS Multi Replay Buffer`.
Run setup again to update or repair. Remove **OBS Multi-Length Replay** through
Windows **Installed Apps**; the original download and OBS executable are not needed
for removal. Setup logs are `Setup Log*.txt` in `%TEMP%`, or the path given by `/LOG`.

For a pre-elevated automation shell:

```bat
obs-multi-replay-0.1.2-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OBSROOT="D:\OBS Studio" /LOG="D:\setup.log"
```

There is no `/ALLUSERS` or secondary elevation flow. Interactive UAC approval and
denial still require native manual acceptance before publication. Signing is not
configured; Windows may identify an unsigned build as an unknown publisher.

## Windows x64: portable OBS

Extract the **entire Windows ZIP** into a writable folder. It contains `Install.cmd`,
`Uninstall.cmd`, `README.txt` and `PluginFiles/`. Double-click **Install.cmd**, choose
**Portable**, select the OBS root, check the action/destination, and confirm.
Run it again for update/repair. Run **Uninstall.cmd**, choosing Portable and the same
folder, to remove the plugin. If OBS moved, select its current folder.

The portable paths are `obs-plugins\64bit\obs-multi-replay.dll` and
`data\obs-plugins\obs-multi-replay`. Portable installation does not register an
Installed Apps entry. Protected folders request administrator access; ordinary
writable portable folders do not. Logs are in `PluginFiles/Logs`.

```bat
Install.cmd -Mode Portable -ObsRoot "D:\Portable OBS" -NonInteractive
Uninstall.cmd -Mode Portable -ObsRoot "D:\Moved OBS" -NonInteractive
```

Noninteractive scripts fail if elevation is needed. The scripts retain Standard
mode for existing automation, but normal desktop installs should use the EXE and
Installed Apps together. Keep the ZIP or use a newer ZIP for portable removal.
Execution-policy overrides apply only to the launched PowerShell process.

## Linux x86_64: Flatpak OBS

Download **`obs-multi-replay-0.1.2-linux-flatpak-x86_64.run`**, open a terminal in
the download folder, and run:

```sh
sh ./obs-multi-replay-0.1.2-linux-flatpak-x86_64.run
# Update or repair: run the same command again.
# Remove only the plugin:
sh ./obs-multi-replay-0.1.2-linux-flatpak-x86_64.run --uninstall
```

Run as your desktop user, **without sudo**. Requirements are Python 3.10+, Flatpak,
and the matching x86_64 OBS Flatpak. The installer checks its embedded archive,
extracts it temporarily, shows the action/destination, and asks for confirmation.
It downloads nothing, changes no Flatpak permissions, and installs no host packages.
Logs persist in `${XDG_STATE_HOME:-~/.local/state}/obs-multi-replay/logs`.
Add `--non-interactive` for automation. Use `--help` for the short command summary.

The supported binary target is **Flatpak OBS 32.2.2**, exact commit
`21b35765922fb5c8b92ccdccedc0d8ca709b12f86a961780350199082da822ef`, using its
FFmpeg 8 ABI. Check the accompanying `linux-build.json` for each build. A different
OBS deployment requires a matching plugin build; the installer will not downgrade
OBS or bypass the commit check. User and system Flatpak installs are detected;
multiple OBS installations are refused as ambiguous. Uninstall does not require
the original OBS commit.

The destination is
`~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-multi-replay`.
Plugin backups remain under `replay-plugin-backups` beside `plugins`.
No settings migration is performed; the developer CLI retains explicit migration.

The `.tar.gz` is an alternative: extract it entirely into a writable folder and
run `sh ./install.sh` or `sh ./uninstall.sh`. Its logs stay in `PluginFiles/Logs`.
Both Linux downloads use the same verified payload and transaction engine.

## Linux native RPM/deb OBS

The Flatpak package cannot be used with native OBS, even on the same distribution.
Do not copy its `.so` into a native OBS folder. Native Qt/OBS/FFmpeg libraries must
match the plugin build. Fedora 44 / RPM OBS 32.1.1 has historical runtime evidence
and a separate [developer build path](DEVELOPMENT.md#fedora-rpm-obs). Its archive is
developer tooling, not a tested general Linux installer. No deb package, other
distribution, ARM build or macOS installer is currently offered.

## Recovery and delivery

Both engines verify payload hashes/architecture, refuse running OBS and concurrent
operations, block known downgrades, and stage changes with rollback/recovery
journals. After interruption, rerun the installer for the same target. Retain
recovery files if permissions prevent recovery and include the error/log in a report.
Recognized obsolete 0.1.0 Windows setup files/registration are retired without
executing their old uninstaller; unfamiliar files are retained and reported.

Build Windows with `scripts/build-windows.ps1` (Visual Studio/OBS prerequisites and
Inno Setup 6). Repackage staging files using `scripts/package-windows.ps1`, then
`scripts/package-windows-setup.ps1`. Build Linux using `scripts/build-flatpak.sh`
against the matching app/SDK; `scripts/package_linux.py` creates both Linux formats.

CI checks the ordinary Windows wizard, extracted portable scripts, dependency
loading, isolated OBS startup/shutdown, Linux transactions and production loading.
Those checks must pass on this revision before sharing binaries. A future matching
version tag prepares an **unpublished draft prerelease**, with all four assets,
checksums, notices and Linux compatibility metadata. No publication is automatic.

# Installing Multi-Length Replay

Close OBS normally before installing, repairing or removing the plugin. Installers
replace only this plugin's binaries and data. Profiles, scenes, replay preferences,
hotkeys, recordings and other plugins are preserved. This revision is experimental;
see [validation](INSTALLER_REVIEW.md) before sharing.

## Windows 10/11 x64

Download **`obs-multi-replay-0.1.2-windows-x64.zip`** and extract the **entire ZIP**
into a writable folder. It contains only `install.bat`, `uninstall.bat`, `README.txt`
and `PluginFiles/`. Double-click **install.bat** and:

1. Accept **Normal** (the default) for normally installed OBS, or choose **Portable**.
2. Check the detected OBS folder and install/update/repair action. If OBS is not
   detected, enter its root folder containing `bin\64bit\obs64.exe`.
3. Confirm and approve administrator access if requested.
4. Restart OBS. Open **Docks → Multi-Length Replay** and **Tools → Multi-Buffer Settings**.

Run `install.bat` again to update or repair. Run **uninstall.bat**, selecting the same
mode/folder, to remove only the plugin. Keep this package for removal, or use a newer
package. There is no new Installed Apps entry. Interactive errors and the log path
remain visible until you press Enter. Logs are under `PluginFiles/Logs`.

Built-in Windows PowerShell 5.1 is used. No Python, compiler, SDK or additional
PowerShell installation is needed. The ZIP includes the writer's dependencies and
works offline. Its writer EXE and DLLs are required plugin files, not a setup program.
OBS 32.1 or newer x64 is required; the tested Windows build target is OBS 32.1.1.
The minimum is not a promise of compatibility with every future OBS release.

Normal OBS detection checks the OBS application key's default value, uninstall
InstallLocation values and Program Files, including both registry views. Normal
installation targets `C:\ProgramData\obs-studio\plugins\obs-multi-replay` and requests
administrator access when needed. A selected folder with a portable marker is
refused in Normal mode with instructions to choose Portable.

Portable installation targets `obs-plugins\64bit\obs-multi-replay.dll` and
`data\obs-plugins\obs-multi-replay` inside the selected OBS root. Writable portable
folders do not need elevation; protected ones request administrator access. If
portable OBS moved, select its current folder for removal.

From Command Prompt, for automation:

```bat
install.bat -Mode Standard -NonInteractive
install.bat -Mode Portable -ObsRoot "D:\Portable OBS" -NonInteractive
uninstall.bat -Mode Portable -ObsRoot "D:\Moved OBS" -NonInteractive
```

Noninteractive mode skips confirmation/closing pauses and fails if elevation is
needed; use an elevated shell for normal/protected targets. Execution-policy
overrides apply only to the launched PowerShell process. Removal works without a
valid payload or an OBS executable. Old script/EXE installations are supported;
recognized obsolete EXE support/registration is retired without executing the old
uninstaller. Unfamiliar files are retained and reported.

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

Build Windows with `scripts/build-windows.ps1` using the Visual Studio/OBS
prerequisites. `scripts/package-windows.ps1` repackages compiled staging files.
No graphical setup compiler is needed. Build Linux using `scripts/build-flatpak.sh`
against the matching app/SDK; `scripts/package_linux.py` creates both Linux formats.

CI checks the extracted Windows BAT launchers, dependency loading, isolated OBS
startup/shutdown, Linux transactions and production loading. Those checks must pass
on this revision before sharing binaries. A future matching version tag prepares
an **unpublished draft prerelease**, with the Windows ZIP, Linux run/tar, checksums,
notices and Linux compatibility metadata. No publication is automatic.

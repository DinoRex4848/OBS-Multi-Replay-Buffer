# Script installers

Download the Windows ZIP or Linux Flatpak `.tar.gz`, close OBS, and extract the
**entire archive into a writable folder**. Each archive has exactly three files
and one folder, without another enclosing folder:

| Windows | Linux |
| --- | --- |
| `Install.cmd` | `install.sh` |
| `Uninstall.cmd` | `uninstall.sh` |
| `README.txt` | `README.txt` |
| `PluginFiles/` | `PluginFiles/` |

All supporting engines, compiled binaries, manifests, licenses and documentation
are inside `PluginFiles`. Logs go into `PluginFiles/Logs`. Keep the extracted
package for removal, or use a newer package's uninstall script.

## Windows x64

Double-click **Install.cmd**. Choose normal or portable OBS, check the detected
folder/action, and confirm. OBS 32.1 or newer is required; the tested Windows SDK
and acceptance runtime are OBS 32.1.1. That minimum does not guarantee compatibility
with every future OBS version. Windows PowerShell 5.1 is used; no Python, compiler,
SDK or additional PowerShell installation is needed.

Normal installations use `C:\ProgramData\obs-studio\plugins\obs-multi-replay` and
request administrator access. Portable installations use the selected OBS root's
`obs-plugins\64bit` and `data\obs-plugins\obs-multi-replay` paths, requesting elevation
only for protected folders. Run **Uninstall.cmd** and choose the same mode. If
portable OBS moved, give its current folder. Standard removal remains possible
after OBS is removed; portable removal only needs the selected root, not obs64.exe.

For automation, from Command Prompt:

```bat
Install.cmd -Mode Standard -NonInteractive
Install.cmd -Mode Portable -ObsRoot "D:\Portable OBS" -NonInteractive
Uninstall.cmd -Mode Portable -ObsRoot "D:\Moved OBS" -NonInteractive
```

Noninteractive mode skips confirmation and closing pauses, and fails instead of
opening a UAC prompt. Elevate the calling shell for normal/protected targets.
Interactive errors stay visible until Enter. Scripts use a process-local execution
policy override; they do not change system execution policy.

There is no new Installed Apps registration or separate setup uninstaller. Older
script and EXE installs are supported. After successful replacement/removal, the
engine retires recognized old EXE registrations and support files, without running
the old uninstaller. Unfamiliar files are retained and reported. Use the new scripts,
not a retained obsolete EXE uninstaller, if cleanup reports a problem.

## Linux x86_64: Flatpak OBS

Requirements: **Python 3.10+**, Flatpak, and the matching x86_64 OBS Flatpak.
No Python packages, bundled Qt installer runtime, development SDK or sudo are
needed. Install missing prerequisites using your distribution's package manager.

From a terminal in the extracted folder:

```sh
sh ./install.sh
sh ./uninstall.sh
# Automation, without confirmation or closing pauses:
sh ./install.sh --non-interactive
sh ./uninstall.sh --non-interactive
```

Shell files are executable in the archive; a file manager's **Run in Terminal**
option may also work. Terminal invocation is supported independently of X11/Wayland.
Run as your desktop user, without sudo. The destination is
`~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-multi-replay`.

The package must match the **exact OBS Flatpak commit**, recorded in
`linux-build.json` and the embedded payload manifest. A newer OBS app may require
a matching plugin build. The installer rejects missing, incompatible or ambiguous
Flatpak targets before replacing files. Native RPM/deb OBS is outside this package's
scope. Removal works without a matching OBS commit or valid install payload.
Plugin backups remain under `replay-plugin-backups` beside the configuration's
`plugins` folder. Settings migration is not performed by these launchers; the
repository's developer CLI keeps explicit migration support.

## Updates, errors and recovery

Run the latest install script again for **Update** or same-version **Repair**.
Known newer installations cannot be downgraded. Legacy installs without version
metadata are identified as updates. Both engines check hashes, architecture and
OBS compatibility, refuse operations while OBS runs, lock concurrent operations,
and stage replacements with rollback/recovery journals.

Profiles, scenes, hotkeys, replay settings, recordings and other plugins remain
unchanged. Removal deletes only this plugin and recognized installer support;
retained backups remain. After an interrupted operation, rerun the script for the
same mode/folder. Keep recovery files if a permissions/filesystem error prevents
recovery. Include the error and `PluginFiles/Logs` when reporting failures.

## Building and GitHub delivery

Windows: run `scripts/build-windows.ps1` with Visual Studio/OBS prerequisites from
[DEVELOPMENT.md](DEVELOPMENT.md). Repackage compiled staging files with
`scripts/package-windows.ps1`. Linux: run `bash scripts/build-flatpak.sh` against
the matching Flatpak SDK, or repackage its validated staging payload with
`python3 scripts/package_linux.py`.

The only user-facing packages are `obs-multi-replay-VERSION-windows-x64.zip` and
`obs-multi-replay-VERSION-linux-flatpak-x86_64.tar.gz`. CI validates extracted
scripts, production dependencies, archive structure/hashes and isolated Linux
OBS loading. Fedora's native development archive remains separate developer tooling.
A version tag matching CMake creates an **unpublished draft prerelease** with the
archives, checksums, notices and Linux compatibility metadata after all jobs pass.
Published releases are never changed. The old `v0.1.0` graphical draft/tag remain
historical; current script packages use `v0.1.1`. See [VALIDATION.md](VALIDATION.md)
for test evidence and remaining interactive/hardware checks.

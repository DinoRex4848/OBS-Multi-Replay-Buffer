# Graphical installers

Each release has one offline installer per platform:

- `obs-multi-replay-VERSION-windows-x64-setup.exe`
- `obs-multi-replay-VERSION-linux-flatpak-x86_64-setup.run`

Close OBS first. Setup detects **Install**, **Update**, or **Repair**. Download and run the newest installer to update; no background updater or update service is installed. Settings, profiles, scenes, hotkeys and recordings are preserved. Older versions cannot replace a newer installation when version metadata is available. Installations made by the original scripts are supported, even if their version is unknown.

## Windows

Run the EXE, choose normal or portable OBS, and confirm the OBS root folder containing `bin\64bit\obs64.exe`. OBS 32.1 or newer and Windows x64 are required. Setup includes the plugin, writer helper, FFmpeg/runtime DLLs and dependency notices. No development tools are needed on the recipient's PC.

Normal installations request administrator access and use `C:\ProgramData\obs-studio\plugins\obs-multi-replay`. Older copies of this plugin directly inside the selected OBS folder are removed in the same transaction to avoid duplicate loading. Installer support files are kept in Program Files. Remove the plugin through **Windows Settings → Apps → Installed apps → OBS Multi-Length Replay**.

Portable installations use OBS's relative `obs-plugins\64bit` and `data\obs-plugins\obs-multi-replay` paths. Writable OBS folders need no elevation; protected folders request it. The local `obs-multi-replay-setup\unins000.exe` removes the plugin. The uninstaller resolves the portable OBS root from its own location so it can follow a moved OBS folder. Separate portable installations keep separate removal tools.

The first EXE is unsigned. Windows may identify it as an unknown publisher; producing an EXE alone does not establish publisher identity or SmartScreen reputation. No signing certificate is configured.

## Linux: Flatpak OBS

Download the `.run`, enable **Allow executing file as program** in its file properties, and open it. The equivalent terminal commands are:

```bash
chmod +x obs-multi-replay-0.1.0-linux-flatpak-x86_64-setup.run
./obs-multi-replay-0.1.0-linux-flatpak-x86_64-setup.run
```

The wizard bundles Python and Qt, works offline, and does not need the Flatpak SDK, host Python, a terminal window, or administrator access. The host needs an x86_64 desktop Linux system with glibc 2.35 or newer and normal X11/Wayland desktop libraries. PyInstaller extracts its runtime into temporary storage; that filesystem must permit executable files and symbolic links. Native RPM/deb OBS is outside this installer's scope.

The destination remains `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-multi-replay`. Setup supports user or system Flatpak OBS. If multiple branches/installations are present it refuses an ambiguous target. The package must match the **exact OBS Flatpak commit**, not just its version number; `linux-build.json` records the target. An OBS update may therefore require a new matching plugin installer. Setup explains a mismatch before changing the plugin.

Use **Remove plugin** in the same wizard to uninstall. Removal remains available when OBS has changed since the installer was built. Plugin backups remain under `replay-plugin-backups` next to the plugin configuration. Profiles are never migrated automatically; the developer CLI retains explicit `--migrate-profile` support.

Both engines stage replacements and keep an installation journal. Failed publication restores the previous files. After a terminated installation, rerun setup for the same target to recover. Retain backups/journals if recovery reports a permissions or filesystem error.

## Building

Windows: install Visual Studio/CMake as described in the README and **Inno Setup 6.7.3**. Then run:

```powershell
./scripts/build-windows.ps1
# Repackage an existing compiled staging directory:
./scripts/package-windows.ps1 -BuildSetup
```

`scripts/install-inno-ci.ps1` downloads the official compiler and verifies its pinned SHA-256. `tests/windows_setup_tests.ps1` downloads a checksum-pinned official OBS runtime and exercises the real EXE on a disposable Windows builder. It installs into ProgramData and must not be run against a personal plugin installation. Add `-SmokeObs` for an OBS startup test on a Windows desktop with working graphics. UAC prompts still need interactive native validation.

Linux: first build the production payload with `bash scripts/build-flatpak.sh`. Package on **Ubuntu 22.04 / Python 3.10** to retain the documented glibc baseline:

```bash
python3 -m venv build-installer-venv
build-installer-venv/bin/pip install -r packaging/linux/requirements.txt
build-installer-venv/bin/python scripts/package-linux-installer.py
```

The builder needs `libpython3.10`, `binutils` and the Qt desktop libraries listed in the workflow. `--package` selects a different validated staging directory. Packaging verifies ELF architecture, hashes, helper permissions, production mode and version before bundling. It creates the `.run`, checksum, dependency notices and `linux-build.json`. `--verify-payload` and `--smoke-test` check the executable without installing anything.

## GitHub delivery

The build workflow runs engine tests, Qt wizard tests, Windows EXE acceptance tests and an isolated production OBS Flatpak load smoke. Installers and notices are downloadable workflow artifacts even without a release.

A pushed `vVERSION` tag must match CMake. After all platform jobs pass, automation verifies both installer checksums and creates a **draft prerelease**. Reruns may update that draft; an already published release is never modified. Review the draft, native Windows/UAC results and remaining checks in `VALIDATION.md` before publishing it. Implementation does not push tags or publish releases.

The installer framework and bundled runtime licenses accompany the assets. Plugin runtime dependencies and source locations are described in `THIRD_PARTY.md`.

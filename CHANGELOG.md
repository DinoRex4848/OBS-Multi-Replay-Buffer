# Changelog

## 0.1.6

- Ask for confirmation before the trash button removes a preset.
- Keep the plugin's hotkeys together at the top of **OBS Settings → Hotkeys**. All eight save hotkeys are now always listed, so adding a preset no longer puts its hotkey at the bottom until OBS restarts; hotkeys of presets that do not exist are marked "(no preset)" and do nothing.
- Simpler downloads: one ZIP per system. The Windows ZIP has `install.bat`; the Linux ZIP extracts to a folder with `install.sh`. The Linux `.run` and `.tar.gz` downloads are no longer made.
- Add an **Open OBS Hotkeys…** button to **Tools → Multi-Buffer Settings** that opens OBS's Settings on the Hotkeys page.

## 0.1.5

### Added

- Choose between one and eight replay presets. Click **+** in the dock to add one (it starts at 60 seconds) and the trash button to remove one; the presets below move up with their folders and hotkeys. Presets can be changed only while buffering is stopped.
- New profiles start with two presets, 60 and 240 seconds. Profiles from earlier versions keep their three presets, names, lengths, folders and hotkeys.
- Save hotkeys `MultiReplay.Preset4` to `Preset8`. OBS's Hotkeys settings list one save hotkey per existing preset.

### Cleanup

Internal cleanup with no intended change to the plugin's behavior, settings or installers.

- Split the 1,600-line plugin source into a module entry point, a controller, the dock and settings views, and settings, hotkey and status-text modules. The UI test driver is now a separate file built only for tests.
- Rename `settings.*` to `recording_encoders.*` and `mux.*` to `writer_process.*`; name the capture and writer limits; remove unused fields and code.
- Run the writer remux test and new status-text and profile-settings tests automatically.
- Packages now include only user documentation (README, changelog, user guide, installation guide and third-party notes), not developer notes.
- Remove developer-only scripts that releases do not use: the Fedora tarball and installer, the RPM-to-Flatpak profile migration, the `.deps` toolchain wrapper and the source snapshot packager.
- Document a known issue: with Simple Output and shared encoding, starting replay while a recording is already running fails.

## 0.1.2

- Provide simple install.bat/uninstall.bat launchers with normal/portable OBS detection, elevation only as needed, and preserved transaction safeguards.
- Fix loading in OBS 32.2: the Windows module no longer imports FFmpeg (it needed OBS 32.1's `avutil-59.dll`), and packaging rejects any module that does.
- Relaunch for administrator access with `-File` instead of `-EncodedCommand`, which Microsoft Defender blocked as a trojan before UAC appeared.
- Show the elevated step's result in the original window, accept OneDrive folders while still refusing links, and explain the SmartScreen prompt in the README.
- Add a single-file offline Linux Flatpak installer with persistent logs and archive integrity checks, retaining exact OBS commit/ABI restrictions.
- Exercise the extracted Windows BAT package and isolated OBS startup in CI; retain transaction, rollback and settings-preservation checks.
- Require the Windows ZIP and both Linux formats in draft-release validation.

## 0.1.1 (draft testing build)

- Replace graphical EXE/`.run` setup with offline Windows `.cmd` and Linux `.sh` install/uninstall archives.
- Keep exactly three visible files and one `PluginFiles` folder in each archive.
- Retain update/repair, compatibility and integrity checks, rollback, logs, and scoped removal.
- Retire recognized legacy EXE uninstall support after successful migration; preserve unfamiliar files.
- Test extracted launchers and production dependencies in CI. Releases remain unpublished drafts.

## 0.1.0 (historical draft)

Initial public source snapshot and experimental graphical installer builds; no release was published.

- Three configurable replay presets backed by one encoded RAM history.
- Shared recording encoders by default, with an option for separate replay encoding.
- Native OBS replay dock, per-preset hotkeys, and a separate Settings/Diagnostics window.
- Shared or per-preset save folders with per-profile preferences.
- Direct MKV output, OBS-style filenames, partial-clip feedback, and no-overwrite saves.
- Memory accounting, bounded save admission, writer failure handling, and stop/drain behavior.
- Linux Flatpak/Fedora and Windows build tooling, development packaging, automated tests, and CI.
- Public user, development, architecture, and validation documentation.
- Consistent rejection of rooted replay filenames on Windows and Linux, including Windows drive-root-relative paths.

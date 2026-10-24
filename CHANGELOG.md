# Changelog

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

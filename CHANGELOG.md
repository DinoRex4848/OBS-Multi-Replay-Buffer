# Changelog

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

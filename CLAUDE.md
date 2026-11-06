# Working on OBS Multi Replay Buffer

An OBS Studio plugin (C++17, Qt 6, libobs, FFmpeg) that keeps one encoded RAM history and saves three replay lengths from it. Start with [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the source layout and commands, and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how capture, saving and the writer process fit together.

## Build and test

- Fastest compile and test, with no host packages needed: `FEDORA_RELEASE=44 ./scripts/build-container.sh` (podman).
- Flatpak OBS build, matching what users run: `./scripts/build-flatpak.sh`. Add `--ui-test` for the UI test driver build.
- Python installer and release tests: `python3 tests/flatpak_installer_tests.py`, `python3 tests/release_assets_tests.py`. Run `tests/linux_package_tests.py` as a throwaway user or in a container, because it writes under `$HOME`.
- Windows can only be built and tested by the GitHub Actions `windows` job; there is no MSVC here.
- Behavior checks against real OBS: the isolated UI scenarios in DEVELOPMENT.md. Compare their `report.json` files before and after a refactor.

## Rules that protect existing users

- Never rename anything in `src/identifiers.hpp` or the JSON keys in `src/preferences.cpp`. They are stored in users' profiles, dock layouts and hotkey bindings.
- Keep user-visible text and number formatting unchanged unless the change is intended. `text::fixed` deliberately rounds like `QString::number`.
- Never package or install a `REPLAY_INTEGRATION_TEST=ON` build; the packaging scripts refuse one.
- The Linux module must link FFmpeg 8 (avformat/avcodec 62, avutil 60). The Windows module must not import FFmpeg at all; only the writer helper may.
- The version lives only in `CMakeLists.txt`. Python scripts read it through `scripts/project.py`.
- `.ps1`, `.bat` and `.cmd` files use CRLF line endings (see `.gitattributes`); edit them without converting endings.

## Running OBS for tests

Never point a test OBS at the real configuration or recording folder. `scripts/test-obs-ui.py` rewrites `global.ini` `[Locations]` and every recording path to a scratch folder, and refuses to run while another OBS is open. Keep it that way. A tray-hidden OBS ignores `close()`; show the main window first. Wait for each test OBS to exit before starting the next.

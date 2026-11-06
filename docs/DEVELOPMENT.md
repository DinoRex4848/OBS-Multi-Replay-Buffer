# Developing OBS Multi Replay Buffer

[README](../README.md) · [User guide](USER_GUIDE.md) · [Architecture](ARCHITECTURE.md) · [Validation](VALIDATION.md)

The project uses C++17, CMake, libobs/the OBS frontend API, Qt 6 Widgets, and FFmpeg libraries. The module retains encoded packets; a separate helper writes MKV files. It does not invoke an external FFmpeg encoder to save replays.

Installers and CI artifacts are experimental; see [installer instructions](INSTALLERS.md). The commands below build the plugin and offline packages. Tag automation creates only an unpublished draft prerelease.

## Get the source

```bash
git clone https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer.git
cd OBS-Multi-Replay-Buffer
```

Use a build environment matching the OBS installation that will load the plugin. Flatpak and native RPM binaries are not interchangeable. The tested platforms and remaining gaps are listed in [Validation](VALIDATION.md).

| Component | Requirement |
| --- | --- |
| Compiler | C++17; GCC on Linux or Visual Studio 2022 C++ tools on Windows |
| CMake | 3.24+ for the plugin; 3.28+ for the Windows OBS SDK build |
| OBS | Runtime minimum 32.1; development targets 32.1.1 RPM/Windows and 32.2.2 Flatpak |
| Qt | Qt 6.8+ Widgets, matching OBS's ABI |
| FFmpeg on Linux | Matching FFmpeg 8 headers and runtime: avformat/avcodec 62, avutil 60 |
| FFmpeg on Windows | OBS 32.1.1's pinned dependency package |
| Tooling | Git, Python 3, Ninja on Linux; Windows SDK on Windows |

Normal replay saving uses FFmpeg libraries. The `ffmpeg` and `ffprobe` command-line tools are needed only for media verification.

## Flatpak OBS on Linux

Install x86_64 OBS from Flathub and its matching SDK. The locally tested OBS 32.2.2 deployment uses Freedesktop SDK 25.08, Qt 6.11.1, and FFmpeg 8.1.2.

```bash
flatpak install --user flathub "$(flatpak info --show-sdk com.obsproject.Studio)"
./scripts/build-flatpak.sh
```

The script builds against the installed app's `/app` headers/libraries, runs the CTest suite, and creates the same `.run` and `.tar.gz` installers as a release under `dist/`. It records the OBS deployment commit and payload hashes. If OBS changes during the build or before installation, rebuild for that deployment. A matching SDK alone does not establish compatibility with a different OBS version.

To install your own build, close OBS first and run the installer it produced:

```bash
sh dist/obs-multi-replay-*-linux-flatpak-x86_64.run
```

The installer checks hashes and the current app commit, refuses changes while OBS is running, and backs up an existing plugin before replacement. The destination is:

```text
~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-multi-replay/
  bin/64bit/obs-multi-replay.so
  data/multi-replay-writer
  data/locale/en-US.ini
```

Restart OBS, then open **Docks → Multi-Length Replay** and **Tools → Multi-Buffer Settings**. The SDK is needed for building, not for normal plugin use.

Backups are stored under `~/.var/app/com.obsproject.Studio/config/obs-studio/replay-plugin-backups/`. For rollback, close OBS and replace only the plugin directory with the backup's `plugin/` contents. To remove the plugin, run the installer with `--uninstall`. Profiles and saved clips remain.

## Fedora RPM OBS (compile and test only)

Native RPM OBS is not a packaged target; releases ship for Flatpak OBS and Windows. Fedora's packages are still the quickest way to compile the plugin and run the unit tests, and CI does exactly that. Install the development packages from your configured Fedora/RPM Fusion repositories:

```bash
sudo dnf install gcc-c++ cmake ninja-build obs-studio-devel qt6-qtbase-devel ffmpeg-free-devel python3
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DREPLAY_INTEGRATION_TEST=OFF
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
python3 tests/flatpak_installer_tests.py
```

If your OBS installation uses RPM Fusion's full FFmpeg packages, use its matching `ffmpeg-devel` rather than mixing incompatible development packages. The CMake dependency check rejects Linux headers or libraries with the wrong FFmpeg major versions. Encoding is supplied by OBS's encoder plugins, not the package used for muxing headers.

Alternatively, use rootless Podman:

```bash
FEDORA_RELEASE=44 ./scripts/build-container.sh
```

This installs build dependencies inside the container, builds into `build-container/`, and runs CTest. The host needs no development packages or sudo. `FEDORA_RELEASE=44` explicitly selects the tested distribution even when the host is not Fedora.

## Windows x64

Install Visual Studio 2022 with **Desktop development with C++**, a Windows SDK, Git, CMake 3.28 or newer. Run from the repository root in PowerShell:

```powershell
./scripts/build-windows.ps1
```

The script obtains OBS 32.1.1 source and its pinned Windows/Qt dependencies, builds an OBS SDK, builds the plugin/helper, runs CTest, and creates the batch-installer ZIP under `dist/`. Downloads require internet access. `-ObsSource` and `-Sdk` can point to existing compatible development trees; the matching dependency archives must still be present in the OBS source tree.

The helper's FFmpeg and release MSVC runtime files are staged with it. One offline batch-installer ZIP supports normal and portable OBS, with three top-level files and `PluginFiles`. The [publication CI run](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/39344761328) passed the MSVC build, core tests, installer fixtures, and packaging. Those checks are not native Windows OBS acceptance.

The Windows archive provides `install.bat` and `uninstall.bat`; their shared console backend is inside `PluginFiles`. Choose normal or portable OBS interactively, or pass `-Mode Standard|Portable -ObsRoot "path" -NonInteractive` for CI. Run noninteractive standard/protected operations from an elevated shell. Logs stay in `PluginFiles/Logs`. Normal installation targets `C:\ProgramData\obs-studio\plugins\obs-multi-replay`; portable installation targets the selected OBS tree. Use `tests/windows_package_tests.ps1` only on a disposable Windows builder: it installs into ProgramData and exercises the actual archived BAT launchers.

Native package acceptance exercises install/update/repair/removal, migration from EXE support, portable relocation, paths with spaces/apostrophes/brackets, and production DLL/writer dependency loading. Interactive UAC approval/denial, protected portable folders, real OBS module lifecycle/captures/playback, and global hotkeys remain manual checks.

## Automated tests

History and naming tests are registered with CTest. They cover earlier-keyframe selection, warmup, shared packet lifetimes, reordered video, audio boundaries, pressure/admission behavior, and filename formatting/collisions.

```bash
ctest --test-dir build-native --output-on-failure
python3 tests/flatpak_installer_tests.py
```

For an existing Flatpak build, run CTest in the matching SDK without launching the OBS UI:

```bash
flatpak run --devel --filesystem="$PWD" --command=ctest com.obsproject.Studio \
  --test-dir "$PWD/build-flatpak" --output-on-failure
```

The Flatpak installer tests use temporary fixtures and do not install into your OBS profile. They check install/update/repair/downgrade, rollback and interrupted-transaction recovery, corrupted payloads, locking, symlinks, and refusal of test-driver packages. Windows installer fixtures run under Windows PowerShell:

```powershell
./tests/windows_installer_tests.ps1
```

For Linux's FFmpeg configuration guards, run inside the matching Flatpak SDK:

```bash
flatpak run --devel --filesystem="$PWD" --command=python3 com.obsproject.Studio \
  "$PWD/tests/ffmpeg_dependency_tests.py" --prefix /app
```

## Native media and mux probes

These tests need the appropriate OBS runtime, graphics environment, and encoder. Close other OBS instances before GPU/UI tests. They generate media under the chosen build directory; they do not need your normal scene collection.

For Flatpak:

```bash
./scripts/test-flatpak.sh --runtime build-flatpak/obs_probe \
  "$PWD/build-flatpak/multi-replay-writer" "$PWD/build-flatpak/probe-av1" av1 fast
python3 scripts/verify-clips.py build-flatpak/probe-av1 --codec av1
./scripts/test-flatpak.sh build-flatpak/mux_tests \
  "$PWD/build-flatpak/multi-replay-writer" "$PWD/build-flatpak/fault-writer" \
  "$PWD/build-flatpak/probe-av1/normal-av1.mkv" "$PWD/build-flatpak/mux-check"
ffmpeg -v error -xerror -i build-flatpak/mux-check/good.mkv -map 0 -f null -
```

For native Fedora, create `obs-nvenc-test` and `obs-ffmpeg-mux` symlinks in the build directory pointing to the installed helpers in `/usr/bin`, then run the same executables from `build-native/` directly. `OBS_ROOT` can override the probe's OBS runtime prefix.

The codec argument accepts `av1`, `h264`, `hevc`, or `x264`. `full` selects 3840×2160/60; another size argument, such as `fast`, selects 640×360/60. An optional final mode selects `long`, `pressure`, `video-only`, or `shared`; the long run exceeds ten minutes. Shared mode exercises both recording start orders and pause/resume. Use a fresh output directory for each probe/mux run.

The clip verifier uses host `ffmpeg` and `ffprobe`. Its four-track title check expects the probe's Game/Mic/Discord/Desktop track names. Use it with probe outputs, not arbitrary user recordings without adapting that expectation.

On Windows with OBS installed, run `./scripts/probe-windows.ps1`, then verify the clips using Python and the FFmpeg command-line tools. This script exercises native encoders and needs compatible hardware.

## Isolated OBS UI tests

Production builds must have **`REPLAY_INTEGRATION_TEST=OFF`**. A separate ON build embeds automated UI behavior and must never be installed or distributed as a normal plugin. The packaging tools reject ON builds.

For Flatpak, create the driver build with:

```bash
./scripts/build-flatpak.sh --ui-test
```

The launcher requires a separate test-only OBS configuration template: a directory containing `obs-studio/`, an initialized test profile under `basic/profiles/`, and a blank scene collection. Initialize this in an isolated OBS configuration, with recording paths pointing at scratch directories. No ready-made profile, personal scene collection, or credentials are included in Git.

Place your isolated template at `build/ui-config`, then run:

```bash
./scripts/test-flatpak.sh python3 scripts/test-obs-ui.py --build build-flatpak-ui \
  --template build/ui-config --scratch build-flatpak/ui-layout --scenario layout
./scripts/test-flatpak.sh python3 scripts/test-obs-ui.py --build build-flatpak-ui \
  --template build/ui-config --scratch build-flatpak/ui-layout --scenario restore
./scripts/test-flatpak.sh python3 scripts/test-obs-ui.py --build build-flatpak-ui \
  --template build/ui-config --scratch build-flatpak/ui-settings --scenario settings
python3 scripts/test-obs-ui.py --build build-flatpak --template build/ui-config \
  --scratch build-flatpak/ui-production --scenario production --flatpak
```

The launcher copies the template and rewrites configuration/recording paths into a distinct scratch directory. Use a fresh scratch directory for each new scenario, except paired restore runs that intentionally reuse the earlier configuration. It refuses concurrent OBS instances. Flatpak's production smoke sets the scratch configuration inside the sandbox and tracks the test instance for cleanup.

Additional scenarios cover per-preset folders, encoder-mode restart, shared/independent capture, encoder reset, and upgrading a saved legacy two-dock layout. See `python3 scripts/test-obs-ui.py --help`. UI tests require an X11/XWayland display and compatible encoding hardware; they are not part of ordinary hosted CI.

## Packaging and CI

Generated archives live in ignored `dist/`; none are committed. GitHub provides source archives for every tag.

[Native builds](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml) runs Fedora and MSVC builds with their tests; the Fedora job produces no artifacts. Windows also tests the extracted BAT package against an official checksum-pinned OBS runtime, including actual DLL/writer loading, EXE migration, relocation, cancellation, corrupted payload refusal and removal without OBS/payload. Linux runs transaction/package tests on Fedora and Ubuntu; Ubuntu builds the production archive against the pinned Flatpak SDK and tests shell install/repair/removal with isolated user files, followed by an isolated OBS load/shutdown smoke.

Linux packaging requires Python 3.10+ and its standard library, not a virtualenv or GUI packaging tools. Both a single-file `.run` and the extracted tar format are produced. `bash scripts/build-flatpak.sh` produces the payload, the four-entry tar archive and the `.run`. `python3 scripts/package_linux.py --build build-flatpak` repackages an existing production build. The plugin version comes only from `CMakeLists.txt`; Python scripts read it through `scripts/project.py`, which also lists the documents copied into user packages. Run `python3 tests/linux_package_tests.py` for temporary fixtures and `python3 tests/linux_package_tests.py --production-archive PATH` only against the matching installed Flatpak OBS; the latter uses isolated HOME files while reading the real Flatpak installation.

Version tags matching CMake create unpublished draft prereleases with the Windows ZIP and Linux run/tar assets, checksums, notices and Linux compatibility metadata. Interactive UAC and hardware capture remain manual checks. The 0.1.2 local changes and unrun acceptance checks are recorded in [INSTALLER_REVIEW.md](INSTALLER_REVIEW.md).

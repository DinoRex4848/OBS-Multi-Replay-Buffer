# Developing OBS Multi Replay Buffer

[README](../README.md) · [User guide](USER_GUIDE.md) · [Architecture](ARCHITECTURE.md) · [Validation](VALIDATION.md)

The project uses C++17, CMake, libobs/the OBS frontend API, Qt 6 Widgets, and FFmpeg libraries. The module retains encoded packets; a separate helper writes MKV files. It does not invoke an external FFmpeg encoder to save replays.

There is no published release or ready-to-use installer. The commands below are for developers building and testing the experimental source. Packaging scripts can produce local archives; they do not publish a release.

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

The script builds against the installed app's `/app` headers/libraries, runs history and filename tests, and creates a development package under `dist/`. It records the OBS deployment commit and payload hashes. If OBS changes during the build or before installation, rebuild for that deployment. A matching SDK alone does not establish compatibility with a different OBS version.

To install your own build, close OBS first:

```bash
python3 scripts/install-flatpak.py
```

The installer checks hashes and the current app commit, refuses changes while OBS is running, and backs up an existing plugin before replacement. The destination is:

```text
~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-multi-replay/
  bin/64bit/obs-multi-replay.so
  data/multi-replay-writer
  data/locale/en-US.ini
```

Restart OBS, then open **Docks → Multi-Length Replay** and **Tools → Multi-Buffer Settings**. The SDK is needed for building, not for normal plugin use.

For an optional migration from an existing native OBS profile with the same profile directory name, use `--migrate-profile PROFILE_DIRECTORY`. This copies only the replay section, including preset and hotkey preferences. Different existing destination replay settings are preserved and cause migration to be refused. A fresh installation does not need migration.

Backups are stored under `~/.var/app/com.obsproject.Studio/config/obs-studio/replay-plugin-backups/`. For rollback, close OBS and replace only the plugin directory with the backup's `plugin/` contents. If settings were migrated, restore the backed-up profile's `basic.ini` as well. For removal, delete only the `plugins/obs-multi-replay` directory while OBS is closed. Profiles and saved clips remain.

## Fedora RPM OBS

The native path was tested on Fedora 44 with RPM OBS 32.1.1. Install release-compatible development packages from your configured Fedora/RPM Fusion repositories:

```bash
sudo dnf install gcc-c++ cmake ninja-build obs-studio-devel qt6-qtbase-devel ffmpeg-free-devel python3
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DREPLAY_INTEGRATION_TEST=OFF
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
python3 tests/flatpak_installer_tests.py
python3 scripts/package-fedora.py --build build-native
```

If your OBS installation uses RPM Fusion's full FFmpeg packages, use its matching `ffmpeg-devel` rather than mixing incompatible development packages. The CMake dependency check rejects Linux headers or libraries with the wrong FFmpeg major versions. Encoding is supplied by OBS's encoder plugins, not the package used for muxing headers.

Alternatively, use rootless Podman:

```bash
FEDORA_RELEASE=44 ./scripts/build-container.sh
```

This installs build dependencies inside the container, builds into `build-container/`, runs CTest, and packages into `dist/`. `FEDORA_RELEASE=44` explicitly selects the tested distribution even when the host is not Fedora.

To install a locally built Fedora package, close OBS and run:

```bash
./scripts/install-fedora.sh
```

It installs under `~/.config/obs-studio/plugins/obs-multi-replay/`, using the same `bin/64bit` and `data` layout as the Flatpak package. This path is for RPM OBS. Remove only that plugin directory while OBS is closed to uninstall; saved clips and per-profile preferences remain.

`scripts/local-build.sh` is a legacy convenience wrapper for a developer-created `.deps/root` toolchain. That toolchain is not included. Use the native or container commands above for a clean checkout.

## Windows x64

Install Visual Studio 2022 with **Desktop development with C++**, a Windows SDK, Git, and CMake 3.28 or newer. Run from the repository root in PowerShell:

```powershell
./scripts/build-windows.ps1
```

The script obtains OBS 32.1.1 source and its pinned Windows/Qt dependencies, builds an OBS SDK, builds the plugin/helper, runs CTest, and creates development ZIPs under `dist/`. Downloads require internet access. `-ObsSource` and `-Sdk` can point to existing compatible development trees; the matching dependency archives must still be present in the OBS source tree.

The helper's FFmpeg and release MSVC runtime files are staged with it. A manual-layout ZIP and a portable-installer development ZIP are produced. The [publication CI run](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/39344761328) passed the MSVC build, core tests, installer fixtures, and packaging. Those checks are not native Windows OBS acceptance.

The installer source under `packaging/windows/` provides `Install.cmd`/`Uninstall.cmd` for a normal installation and `Install-Portable.cmd`/`Uninstall-Portable.cmd` for portable OBS. These launchers require a compiled, packaged payload. They are not an installer download supplied by this source repository. Normal installation targets `C:\ProgramData\obs-studio\plugins\obs-multi-replay`; portable installation targets the selected OBS tree. Close OBS before testing either path and follow the package's development README.

Native acceptance still needs module load/unload, actual replay captures and playback, standard/portable install/update/uninstall, UAC, paths with spaces, runtime dependencies, global hotkeys, and rollback testing on Windows.

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

The Flatpak installer tests use temporary fixtures and do not install into your OBS profile. They check successful/repeated migration, rollback, corrupted payloads, conflicting settings, and refusal of test-driver packages. Windows installer fixtures run under Windows PowerShell:

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

Generated archives live in ignored `dist/`; none are committed. The source packager requires a Git checkout with at least one commit:

```bash
python3 scripts/package-source.py
```

It creates ZIP/tar source snapshots, a Git bundle, and checksums. It includes non-ignored uncommitted files and the checkout's history, so inspect those before sharing a source package. It does not alter the source checkout's branch or index. A downloaded GitHub source ZIP has no Git history; clone the repository to use this script.

[Native builds](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml) runs on pushes, pull requests, and manual dispatch. Fedora builds the plugin/helper, runs CTest and installer fixtures, and packages a development archive. Windows runs installer fixtures, builds against the OBS SDK, runs CTest, and stages development ZIPs. Artifacts are CI outputs; this workflow does not create tags or releases.

Use the repository's `.clang-format` for C++ changes; the existing formatting was generated with clang-format 23. Keep changes focused and include relevant test results when contributing. Document which checks are synthetic, build-only, or real OBS runtime tests.

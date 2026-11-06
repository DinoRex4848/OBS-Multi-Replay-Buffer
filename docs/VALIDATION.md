# Validation and known limits

[README](../README.md) · [User guide](USER_GUIDE.md) · [Reproduction commands](DEVELOPMENT.md)

Updated 2026-11-06. This is an experimental plugin. The sections below say what is checked automatically, what was measured during development and what still needs real-world testing. Raw machine-specific logs and installation records stay local rather than published.

## Current defaults

Three presets of **60/240/600 seconds**, a **6 GB decimal** RAM budget for new profiles, and **shared recording encoders by default**. Checking **Use separate replay encoder** enables independent encoding. Saved profile preferences keep their values.

## Checked on every change

The [Native builds workflow](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml) runs on every push and pull request:

- **Fedora 44:** compiles the plugin and writer against Fedora's OBS packages and runs all CTest tests ([list](DEVELOPMENT.md#automated-tests)), including a real remux through the writer. Also runs the Linux installer transaction and extracted-package tests.
- **Windows (MSVC, OBS 32.1.1 SDK):** compiles, runs the portable CTest tests and the PowerShell installer fixtures, then builds the release ZIP. The ZIP's `install.bat`/`uninstall.bat` are tested against the official checksum-pinned OBS 32.1.1 runtime: install, update, repair, removal, portable relocation, damaged payloads, concurrency, dependency loading of the installed DLL and writer, and an isolated OBS startup with the plugin.
- **Ubuntu 22.04 with the pinned Flatpak OBS 32.2.2:** builds the production plugin in the matching SDK, then installs, repairs and removes it with both the `.run` and the `.tar.gz` using isolated user files. Finally it loads and closes production OBS with an isolated configuration, checking that the normal configuration is unchanged.
- **Release gates:** a version tag must match CMake. Draft releases require the Windows ZIP, both Linux formats, matching checksums and notices, and the `.run` must embed exactly the validated archive.

Hosted CI does not exercise real capture with GPU encoders, desktop hotkeys, interactive UAC or real drives.

## Isolated OBS UI scenarios

`scripts/test-obs-ui.py` drives the real dock, settings window and OBS frontend in a scratch configuration (see [development](DEVELOPMENT.md#isolated-obs-ui-tests)). On 2026-11-06 all eight scenarios passed on Flatpak OBS 32.2.2 with NVENC AV1, before and after the source restructuring: `layout`, `restore`, `settings`, `folders`, `folders-restore`, `independent`, `shared` and `reset`. Their step-by-step reports matched, apart from timings and paths. They cover:

- compact layout and theme styling;
- 32 settings-window checks, 60 per-preset folder checks and failed destinations;
- profile switching, restart persistence, hotkeys and encoder sharing with recording pause/resume;
- the encoder reset warning.

Configurable presets (one to eight) add the `presets` and `presets-restore` scenarios, which passed on 2026-11-06 with the same setup. They check that a new profile starts with two presets, that **+** stops at eight, and that removing preset 3 moves the later names, lengths, folders and hotkey bindings up. They also check that unapplied window edits survive, that presets lock while buffering, that the last preset saves to its own folder, and that everything is restored after a restart. With that change, every earlier scenario above passed again; they now add a third preset first, because new profiles start with two.

The `shared` and `folders` scenarios need the Advanced NVENC template, as the next section explains.

## Known issue found during testing

With **Simple Output** and shared encoding, starting the replay buffer while a recording is already running fails after about five seconds with "Replay encoder stopped delivering packets". The UI scenario reproduces this with both x264 and NVENC Simple profiles. Advanced Output, and starting replay before recording, work. This predates the restructuring; until it is fixed, Simple Output users can start replay first or check **Use separate replay encoder**.

## Installer acceptance for 1.5.0

The CI-built Windows ZIP and Linux `.run` for the presets change passed the release checks and were installed and used in OBS on Windows and on Linux Flatpak OBS 32.2.2 before release.

## Installer acceptance for 0.1.2

Supported scope: Windows 10/11 x64 with normal or explicitly portable OBS, built against the OBS 32.1.1 SDK; Linux x86_64 Flatpak OBS 32.2.2 at the exact app commit recorded in `linux-build.json`, using its FFmpeg 8 ABI. Native RPM/deb OBS is not a packaged target.

A native, non-admin Windows 11 run found two problems, both fixed in 0.1.2:

- Microsoft Defender blocked the elevated `-EncodedCommand` relaunch before UAC appeared. It now relaunches with `-File`. UAC install, repair and uninstall then passed from Desktop and Downloads.
- Steam OBS 32.2.2 could not load the module, because it imported OBS 32.1's FFmpeg 7 `avutil-59.dll` while OBS 32.2 ships FFmpeg 8. The module no longer imports FFmpeg, and packaging fails if it ever does. A rebuilt package then installed, loaded and started a replay session in Steam OBS 32.2.2.

The 0.1.0 graphical setup's reported stall matched a lock held while waiting for an elevated copy of itself; that setup was replaced in 0.1.1. Hosted builders run as administrators, so UAC approval/denial and protected portable folders remain manual checks.

## Development measurements

**Flatpak OBS 32.2.2** (Qt 6.11.1, FFmpeg 8.1.2, Freedesktop SDK 25.08, NVIDIA encoders, SDR):

- **Codec probes:** AV1, H.264 and HEVC each produced eight replays plus one recording with four FLAC tracks. All 27 files started on video keyframes and decoded fully.
- **Shared recording:** both start orders, pause/resume and independent stopping worked, with paused time removed from the replays.
- **Settings and folders:** Apply/OK/Cancel, pending edits, persistence errors, profile changes, busy locks and per-preset destinations, including failed destinations that leave other presets working.
- **Production smoke:** the normal runtime loaded the plugin, it stayed stopped at startup, and ordinary configuration files were unchanged.

**Fedora 44 / RPM OBS 32.1.1** (2026-10-12, NVIDIA):

- A 4K/60 AV1 capture exceeding ten minutes produced fully decoded clips of about **60.58, 240.58 and 600.58 seconds**, showing earlier-keyframe starts for the default lengths.
- A shared recording/replay session exceeding eleven minutes, including a pause, produced full-length clips with the paused time removed; managed memory returned to zero after draining.
- Video-only output, AAC output, overlapping saves, recording coexistence and stop-after-save were exercised. Pressure tests produced explicitly partial clips within the managed ledger and let accepted saves finish. These were not high-motion RSS benchmarks.

Those full-length runs were not repeated on Flatpak OBS 32.2.2. Routed hotkey tests do not measure unfocused keyboard delivery on Wayland.

## Remaining validation

- Native Windows interactive use: real AV1/H.264/HEVC saves, UAC denial, protected portable folders, real drive and permission failures.
- Full-length 60/240/600-second Flatpak saves with real content, measured A/V synchronization, and unfocused global hotkeys on KDE/Wayland and Windows.
- Long high-motion sessions, process RSS versus the managed ledger, multi-gigabyte buffer pressure, save floods, and slow or disconnected drives.
- Fractional frame rates, HDR display correctness, surround audio, more AAC/Opus/PCM boundary cases, non-NVIDIA encoders and open-GOP streams.
- Streaming combined with recording/replay, starting shared replay while recording is paused, streaming-encoder fallbacks, and sleep/resume or clock changes.
- Scene-collection transitions, built-in replay-buffer conflicts, and settings-inheritance edge cases beyond the tested profiles.
- Other OBS releases, distributions, architectures and macOS.

Successful decode does not prove perceptual A/V synchronization. The writer keeps boundary audio and reordered video references, so clip durations are not frame-exact. The 6 GB default is an allocation-accounting budget, not total process memory. Profile/scene changes and shutdown use bounded draining; abrupt process termination has no guaranteed recovery.

Known shared-mode behavior: a replay or recording that joins a running encoder starts at its next keyframe; recording pauses also pause replay collection; and saves near a pause can end about 0.2 seconds early. Encoder discovery uses frontend creation names that may change between OBS versions. User-visible behavior is described in the [user guide](USER_GUIDE.md).

# Validation and known limits

[README](../README.md) · [User guide](USER_GUIDE.md) · [Reproduction commands](DEVELOPMENT.md)

Updated 2026-10-19. This is an experimental implementation. The summaries below distinguish historical development results, checks on the publication snapshot, and work still needed. Raw machine-specific logs and installation records are retained locally rather than published.

## Current defaults

The source uses three presets of **60/240/600 seconds**, a **6 GB decimal** RAM budget for new profiles, and **shared recording encoders by default**. Checking **Use separate replay encoder** enables independent encoding. Saved profile preferences keep their values. Older test runs used a 16 GB default or independent encoding by default; those historical defaults do not describe the current UI.

## Publication checks

On 2026-10-19, the publication source was built from a separate clean checkout using the installed OBS 32.2.2 Flatpak SDK. The production module, writer, and test executables compiled; both CTest cases (history and naming) and all five temporary Flatpak installer fixtures passed. The dependency guards rejected both old FFmpeg headers and a mismatched runtime. The Flatpak development archive was generated successfully with its hash manifest. Python syntax and shell syntax checks also passed. The media probe retains an upstream deprecation warning for `obs_add_data_path`.

The first hosted Windows run exposed path-separator assumptions in the ZIP and filename fixtures. Those comparisons now accept the platform's path representation. Filename validation and the mux writer also reject drive-root-relative Windows paths consistently with Linux rooted paths. The Linux build/core tests were rerun, and mux tests using generated H.264/FLAC media passed rooted-path rejection, collision protection, missing directory/helper, simulated disk-full, and stalled-writer checks; the successful output fully decoded.

GPU/OBS UI acceptance from earlier development is listed separately below; those sessions were not repeated for this publication.

The [publication CI run](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/39344761328) passed for both Fedora and Windows on source commit `fee9324`. Fedora compiled the plugin/helper, passed history/naming and Flatpak installer fixtures, and packaged its development archive. Windows compiled the plugin/helper with MSVC against the OBS 32.1.1 SDK, passed history/naming and Windows PowerShell installer fixtures, and generated both development ZIP layouts. The follow-up commit only records these results in documentation.

The [GitHub Actions workflow](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml) retains those checks for future changes. Hosted CI does not exercise a real Windows OBS session, desktop hotkeys, or GPU/video/audio behavior.

## Graphical installer checks

The new offline installers reuse shared transaction engines and preserve the published filename/path behavior. Local preparation passed 15 Linux engine tests, four Qt wizard/archive tests and five release-gate tests. Windows engine fixtures passed under PowerShell 7.6.6 on Linux; Inno Setup 6.7.3 compiled the setup definition against non-executable fixtures. A production Linux `.run` was built on Ubuntu 22.04 and launched on Ubuntu and Fedora, including Fedora Wayland/X11. Isolated production OBS baseline/plugin runs preserved normal configuration hashes and had the same single remaining OBS allocation at shutdown.

The delivery workflow builds and tests the actual Windows EXE on Windows Server 2022 and the Linux `.run` on Ubuntu 22.04. Native acceptance covers dependency loading, standard and portable install/repair/removal, portable relocation, Installed Apps registration and configuration preservation. Linux CI covers payload verification, transaction/wizard tests and an isolated production OBS load smoke. The [installer CI run](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/39355321697) passed all Fedora, Windows and Linux jobs on commit `52a7191`. Windows tested the compiled production EXE against the checksum-pinned official OBS 32.1.1 runtime, including a portable path with spaces, apostrophes and brackets. Linux built with the matching Flatpak SDK on Ubuntu 22.04, verified the bundled payload and opened the frozen wizard offscreen and through X11. Its production OBS baseline and plugin runs both exited normally with one remaining OBS allocation, and normal configuration hashes stayed unchanged. Interactive UAC and real Windows OBS capture are still separate manual checks.

## Flatpak OBS 32.2.2 development validation

The latest local runtime work used x86_64 OBS 32.2.2, Qt 6.11.1, and FFmpeg 8.1.2 from the installed app, with Freedesktop SDK 25.08. Native codec probes used NVIDIA encoders and SDR; verification used FFmpeg to decode all selected streams.

| Area | Recorded result |
| --- | --- |
| Build and dependencies | Production and separate UI-driver builds compiled. History/naming tests passed. Configuration rejected old FFmpeg headers and incompatible runtime majors. |
| Codec probes | AV1, H.264, and HEVC each produced eight replays plus one recording with four FLAC tracks; all 27 files started on video keyframes and decoded fully. |
| Shared recording | Both recording/replay start orders, pause/resume, and independent stopping passed. Paused time was removed; six shared replays and two recordings decoded in the base Flatpak run. |
| Settings transactions | The final shared-default follow-up passed 32 settings checks, including Apply/OK/Cancel, pending edits, persistence errors, profile changes, busy locks, and reopen behavior. |
| Per-preset destinations | Routing checks exercised default/custom destinations, separate/shared modes, persistence, failed destinations, and independent completion/error handling. Other presets continued when one destination failed. |
| Dock and settings | Compact layout, theme styling, the dock Settings button, hidden-UI routed hotkeys, settings-window reuse, and restart persistence passed in isolated configurations. |
| Encoder preferences | Both saved modes reloaded correctly with the inverted checkbox; new profiles defaulted to sharing. |
| Mux failures | Direct remuxing, filename collisions, missing directories/helper, simulated disk full, and bounded writer stalls passed. Successful outputs decoded. |
| Production smoke | Normal Flatpak runtime loaded the production plugin, stayed stopped on startup, and preserved ordinary configuration files. Baseline and plugin runs had the same single remaining OBS allocation at exit. |
| Installer fixtures | Five tests covered update/migration, repetition, rollback after a profile-write failure, damaged payloads, conflicting settings, and refusal of test-driver payloads. |

The full 60/240/600-second and ten-minute shared runs below were performed on the earlier Fedora environment. They were not repeated at full length for Flatpak OBS 32.2.2. Routed hotkey tests are not a measurement of unfocused keyboard delivery on Wayland.

## Historical Fedora 44 / OBS 32.1.1 validation

Development on 2026-10-12 used x86_64 Fedora 44, RPM OBS 32.1.1, Qt 6.11.2, FFmpeg 8, and NVIDIA hardware. GPU and UI runs used isolated configurations and generated test media.

- A 4K/60 AV1 capture exceeding ten minutes produced fully decoded clips of approximately **60.58, 240.58, and 600.58 seconds**, demonstrating earlier-keyframe starts for the default lengths.
- A shared recording/replay session exceeding eleven minutes, including a recording pause, produced full-length replay clips with the paused time removed. Managed packet memory returned to zero after draining.
- AV1, H.264, and HEVC with multiple FLAC tracks, video-only output, AAC output, overlapping saves, recording coexistence, both start orders, and stop-after-save were exercised.
- Pressure tests produced explicitly partial clips within the managed ledger, preserved pinned data, and allowed accepted saves to finish. These were not high-motion capacity/RSS benchmarks.
- Simple Output sharing and frontend encoder reset were exercised. Reset produced the intended warning and sharing recovered after replay restarted.
- Mux tests covered no-overwrite collision handling, missing folders/helper, simulated full disk, and stalled helper termination.
- A clean Fedora container build used distribution development packages and passed its core tests and packaging step.

The final publication source contains later Flatpak/UI work. Historical Fedora results describe the code and environment at the time, not a new runtime acceptance of every later change.

## Windows evidence and limits

Earlier development cross-compiled the code with MinGW-w64. The writer, core tests, and mux tests were partly exercised under Wine, including child-process/pipes, stall handling, exclusive file creation, and no-overwrite publication. Later UI changes received Windows-target syntax checks.

The older MinGW/Wine results alone do not establish an MSVC build or native Windows OBS runtime behavior. The publication and installer CI above now establish the MSVC build, core tests, packaging and compiled EXE acceptance. The native setup test resolves the actual plugin DLL imports using the official OBS runtime; an additional check starts the installed writer with EOF input and verifies its expected protocol error. Installer filesystem fixtures still use synthetic payloads, while compiled setup acceptance uses production binaries. A real interactive OBS load/capture session remains outstanding.

## Remaining validation

- Native Windows interactive OBS load/unload and real AV1/H.264/HEVC saves; interactive UAC approval/denial and protected portable folders; real hardware, drive and permission failures. Automated production EXE acceptance covers install/repair/removal, portable relocation, runtime DLL loading and registration; engine fixtures cover updates, rollback and path failures.
- Full-length 60/240/600-second Flatpak saves with real content, perceptual/measured A/V synchronization, and unfocused global hotkeys on KDE/Wayland and Windows.
- Long high-motion sessions, process RSS versus the managed ledger, multi-gigabyte buffer pressure, save floods, and slow or disconnected destination drives.
- Fractional frame rates, HDR display correctness, surround audio, additional AAC/Opus/PCM boundary cases, non-NVIDIA encoders, and open-GOP streams.
- Streaming combined with recording/replay, starting shared replay while recording is already paused, streaming-encoder fallback combinations, and sleep/resume or clock changes.
- Scene-collection transitions and built-in replay-buffer conflicts under real use, plus settings-inheritance edge cases beyond the exercised profiles.
- Compatibility with other OBS releases, distributions, architectures, and macOS.

Successful decode does not prove perceptual A/V synchronization. The writer retains boundary audio and reordered video references, so clip durations are not frame-exact. The 6 GB default is an allocation-accounting budget, not total process memory. Profile/scene changes and shutdown use bounded draining; abrupt process termination has no guaranteed recovery.

Known shared-mode behavior includes next-keyframe joining, recording pauses stopping replay collection, and saves near a pause sometimes ending about 0.2 seconds early. Encoder discovery uses frontend creation names that may change with OBS. Detailed user-visible behavior is documented in the [user guide](USER_GUIDE.md).

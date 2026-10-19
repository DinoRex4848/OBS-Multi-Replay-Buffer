# Architecture

[README](../README.md) · [Development](DEVELOPMENT.md) · [Validation](VALIDATION.md)

The plugin retains one rolling history of encoded packets and selects ranges from it for each save. Presets do not have separate ring buffers, and saving does not re-encode video or write a longer intermediate recording.

```text
OBS recording settings and encoders
              |
     encoded output callbacks
              |
  shared packet history + RAM ledger
              |
  preset save -> pinned packet snapshot
              |
      bounded FIFO save queue
              |
  writer helper process -> incomplete MKV -> final MKV
```

## Encoding and OBS integration

New profiles default to sharing OBS's recording encoders. **Use separate replay encoder** selects independent encoders when checked. Its UI value is the inverse of the existing `share_encoder` profile key; explicit saved values retain their meaning.

The shared path first examines encoders attached to `obs_frontend_get_recording_output()`. Before OBS attaches them, it can find them by the creation names used by the frontend, including `advanced_video_recording`, `adv_record_audio_N`, and the corresponding Simple Output names. This depends on OBS frontend implementation details and must be rechecked for new OBS versions. If sharing is unavailable, capture falls back visibly to independent encoding. Streaming-encoder recording configurations are not shared.

The plugin adds its own output consumer using public libobs APIs. It does not replace frontend encoder pointers, force ordinary recording to start, or change the user's recording quality/keyframe interval. Idle encoders receive the settings/media configuration expected by recording; running shared encoders remain untouched. Separate mode creates one replay video encoder for all presets and one audio encoder per selected mix.

Recording pauses affect shared encoders. The replay timeline removes paused time using the timestamps supplied by libobs. Request endpoints use that same timeline. A consumer joining a running encoder starts at the next keyframe, so recording or replay can begin up to a keyframe interval late. Applying OBS settings can replace the frontend encoders; the active replay session keeps its references and warns that restarting is needed to restore sharing.

## Packet ownership and memory

`settings.cpp` maps supported OBS recording settings and freezes a session fingerprint. Advanced Standard Output inherits encoder configuration, track selection/names, and rescaling. Simple Output has explicit NVIDIA/x264 mappings.

`capture.cpp` registers encoded AV and video-only outputs. AV callbacks retain OBS packet references. Video-only callbacks expose temporary encoder storage, so each payload is copied once into shared history. Codec headers are frozen after the first encoded frame, when NVENC extradata is available.

`history.cpp` maintains packet history, a keyframe index, and a conservative allocation ledger. Multiple snapshots share payload ownership. Accepted jobs pin the data they need. Time and pressure eviction remove whole keyframe groups; pinned data is not discarded merely to shorten the visible history.

The default budget is 6 GB decimal. It includes conservative packet/job accounting and a 32 MiB controlled writer/format reserve. Compressed packets have an 8 MiB safety bound. This does not cap total OBS RSS or dependency/encoder/GPU memory. If accepted jobs or a minimum playable keyframe group prevent recovery under pressure, collection stops and accepted work drains.

## Save selection and queueing

Hotkey callbacks capture request time before UI dispatch. Every accepted save keeps its requested endpoint even if the disk writer starts later. The queue admits at most 128 simultaneous accepted jobs, also subject to memory availability. Requests are not intentionally deduplicated.

Clip starts use the latest usable keyframe at or before the desired start. Video selection retains a decode-order prefix containing required reordered references. Audio packets that straddle the starting boundary are kept. Timestamps use a common origin; actual duration can extend beyond the nominal request.

Accepted jobs allow a 500 ms media settlement window and a two-second wall-time deadline for delayed packets. Snapshot metadata reserves 4,096 settling references. Stop rejects new requests, allows accepted requests to settle, and then releases encoders. Warmup, pressure, and settlement shortfalls are reported as partial where applicable.

## Writer isolation and filenames

`mux.cpp` launches `multi-replay-writer` using `posix_spawn` on Linux or `CreateProcessW` on Windows and sends compressed packets through a bounded pipe. `writer.cpp` links FFmpeg libraries and writes Matroska directly. A helper process allows stalled I/O to be terminated without cancelling an unsafe in-process writer thread.

Files are created exclusively under incomplete names. Successful finalization publishes them without replacing an existing file, using Linux `renameat2(RENAME_NOREPLACE)` or Windows `MoveFileW`. Filename collisions get new suffixes. Failed jobs never acquire a success name. The watchdog imposes a ten-second inactivity/finalization bound; shutdown also has bounded settlement/drain waits. This is best-effort orderly shutdown, not crash recovery.

`naming.cpp` evaluates OBS's filename tokens at the original request timestamp. Filename format, prefix/suffix, space preference, and relevant video context are frozen at buffer start. Requested seconds and an optional partial marker are appended. Relative date subfolders are allowed; invalid path components are sanitized.

## UI, preferences, and destinations

`plugin.cpp` owns the replay controller, stable hotkeys, native dock, settings window, and status refresh. Encoding and disk writing run separately from the UI. The existing dock identifier and profile settings remain compatible.

Tools and the dock's Settings button open the same modeless Settings/Diagnostics window. Draft edits are separate from applied values. Apply persists preferences before promoting the draft; failure restores the previous in-memory configuration. Cancel/close discard the draft. Busy sessions lock editing without blocking Diagnostics. Profile changes close the window and discard pending edits.

The original `folder` preference remains the shared destination override. `separate_folders` and `preset_folder0` through `preset_folder2` add per-preset routing. Blank per-preset paths independently resolve to OBS's recording directory. Destinations are frozen at session start, and each accepted job carries its own folder and completion identity. A destination failure affects that preset's save and is not cleared by another preset's success.

Profile and scene-collection changes stop/drain the old session. A profile change additionally waits for encoder release so OBS can apply video settings. Shutdown removes the Tools action/dialog and drains work before OBS tears down media. Test-only UI automation is compiled only with `REPLAY_INTEGRATION_TEST=ON`; normal packages require OFF.

## Dependency environments and references

Linux configuration checks both FFmpeg 8 headers and loaded runtime majors. Flatpak builds consume the installed OBS application's development files under `/app`, record its deployment commit, and use its matching SDK. RPM builds consume the native environment. Windows builds use OBS's pinned dependencies and exported CMake SDK targets.

Implementation research used the following primary sources:

- [OBS 32.1.1 AdvancedOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.1.1/frontend/utility/AdvancedOutput.cpp) and [SimpleOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.1.1/frontend/utility/SimpleOutput.cpp): recording encoder construction, settings, tracks, and rescaling.
- [OBS 32.2.2 AdvancedOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/utility/AdvancedOutput.cpp) and [SimpleOutput.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/utility/SimpleOutput.cpp): the frontend names used by the Flatpak target.
- [obs-output.c](https://github.com/obsproject/obs-studio/blob/32.1.1/libobs/obs-output.c) and [obs-encoder.c](https://github.com/obsproject/obs-studio/blob/32.1.1/libobs/obs-encoder.c): lifecycle, pause, interleaving, packet ownership, and encoder sharing.
- [NVENC integration](https://github.com/obsproject/obs-studio/blob/32.1.1/plugins/obs-nvenc/nvenc.c) and [OBS's FFmpeg muxer](https://github.com/obsproject/obs-studio/blob/32.1.1/plugins/obs-ffmpeg/ffmpeg-mux/ffmpeg-mux.c): headers, keyframes, audio metadata, and container parameters.
- [Official OBS plugin template](https://github.com/obsproject/obs-plugintemplate): module/CMake/Qt integration patterns.

These design choices do not establish compatibility with every encoder or OBS release. See [Validation](VALIDATION.md) for measured results and outstanding work.

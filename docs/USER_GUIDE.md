# Using OBS Multi Replay Buffer

[README](../README.md) · [Development](DEVELOPMENT.md) · [Validation](VALIDATION.md)

This guide describes the experimental source version. No release or ready-to-use installer is published. Start with the [development guide](DEVELOPMENT.md) if you need to build and install it.

## Find the controls

Open **Docks → Multi-Length Replay** for the replay controls. Open **Tools → Multi-Buffer Settings**, or use the dock's **Settings** button, for settings and diagnostics. These are the current labels inside OBS.

The dock can be moved, floated, or tabbed using OBS's normal dock controls. OBS remembers its placement. Hiding the dock or closing the settings window does not stop buffering or disable save hotkeys.

The settings window has two tabs:

- **Settings:** replay memory budget, encoder mode, and destinations.
- **Diagnostics:** live history/memory/queue information, active encoding mode, resolved destinations, and the latest save or warning.

Opening Settings again brings back the same window and preserves its selected tab and pending edits. Reopening a closed window starts on Settings. The shortcut stays available while buffering or draining, so you can still inspect Diagnostics.

## First session

1. Configure OBS's recording encoder, quality, audio tracks, and recording folder. The plugin inherits supported recording settings.
2. Stop OBS's built-in replay buffer. Both replay buffers cannot run together.
3. Set your preset names and lengths in the replay dock. The three initial lengths are **60, 240, and 600 seconds**. Durations can be edited from 1 to 86,400 seconds; the RAM budget determines how much history can actually be retained.
4. Open Multi-Buffer Settings, choose your preferences, and click **Apply** or **OK**.
5. Click **Start Buffering**. The button uses OBS's active replay-button styling while the session is active.
6. Wait for playable history, then click **Save** beside the desired preset. Hover over Save to see whether the available duration is full or partial.
7. Click **Stop Buffering** when finished. New saves are refused while accepted requests finish; Start becomes available after draining.

Buffering starts manually after OBS opens. A start hotkey uses the last applied settings, including when the settings window contains unapplied edits.

## Hotkeys

Assign keys under **OBS Settings → Hotkeys**. They ship unassigned.

| Action | Hotkey label | Internal identifier |
| --- | --- | --- |
| Start buffering | Multi-Length Replay: Start Buffering | `MultiReplay.Start` |
| Stop buffering | Multi-Length Replay: Stop Buffering | `MultiReplay.Stop` |
| Save preset 1 | Multi-Length Replay: Save Replay 1 | `MultiReplay.Preset1` |
| Save preset 2 | Multi-Length Replay: Save Replay 2 | `MultiReplay.Preset2` |
| Save preset 3 | Multi-Length Replay: Save Replay 3 | `MultiReplay.Preset3` |

Renaming a preset or changing its duration keeps that preset's hotkey. Settings and bindings are stored per OBS profile. Unfocused desktop hotkeys, particularly on Wayland, need hands-on validation; routed hotkey tests do not prove that a desktop will deliver every key while another application is focused.

## Encoder modes and recording

One replay video encoder serves all three presets in either mode.

| Setting | Behavior | Trade-off |
| --- | --- | --- |
| **Use separate replay encoder** unchecked | Shares OBS's recording video/audio encoders where available; default for new profiles | Recording pauses also pause replay collection |
| **Use separate replay encoder** checked | Creates separate encoders using supported recording settings | Recording and replay can run independently, but use two video encodes together |

In shared mode, paused time is cut out of replay clips just as it is in the recording. Saves made during a pause end at the last delivered media near the pause; observed clips can end about 0.2 seconds before it. The buffering button remains active during the pause.

Whichever shared consumer starts second joins at the next keyframe. A recording started while replay is already buffering can therefore start late by up to a keyframe interval. Setting a **1–2 second keyframe interval** in your recording encoder reduces that delay and the extra footage at replay starts. The plugin does not change that setting for you.

Applying OBS recording settings during a shared session can recreate OBS's recording encoders. Replay keeps its existing encoders and warns that a new recording would use a second encoder. Stop and restart buffering to pick up the new settings and restore sharing.

Recording configurations that use the streaming encoder cannot share that encoder through this plugin. If sharing is unavailable, the session uses an independent encoder and reports it. Check Diagnostics for the actual session mode. Existing profiles keep their saved preference; the default does not overwrite it.

Advanced Standard Output has the broadest support. Simple Output mappings cover NVIDIA and x264. Custom FFmpeg output and Simple Lossless are rejected. See [validation and compatibility limits](VALIDATION.md) for remaining combinations.

## Save destinations

Leave **Save folder** blank to use OBS's recording folder, or select one shared folder for all three presets.

Enable **Use separate folders for each preset** to choose three destinations independently. Each blank preset field uses **OBS's recording folder**, even when a custom shared folder is saved. Renaming or changing a preset's duration keeps its folder assignment. Switching folder modes remembers both sets of paths.

Destinations are fixed when buffering starts. Stop buffering, apply changes, and start again to use different destinations. Flatpak OBS must have permission to access the chosen folders inside its sandbox.

Shared-folder mode requires an existing destination before buffering can start. In separate-folder mode, a missing or inaccessible destination fails only the affected preset's save; buffering and other presets continue. A failed save reports the preset and destination and is never redirected to another folder. Its error remains until that preset succeeds or buffering restarts, even if another preset saves successfully.

## Settings and profiles

**Apply** saves changes without closing the settings window. **OK** saves and closes. **Cancel**, Escape, and the close button discard changes since the last Apply. If saving fails, the draft remains visible with an error and applied preferences stay unchanged.

Settings lock while buffering or draining. Stop the session to change them. Profile switching closes the window, discards its pending edits, stops the old session, and loads the new profile's preferences. Scene-collection changes also stop and drain the old replay session. The new session stays stopped until you start it.

Recording settings and filename preferences are captured at session start. Restart buffering after changing them.

## RAM budget and partial clips

New profiles default to **6 GB decimal**, or 6,000,000,000 bytes. Saved profiles keep their existing budget.

The budget covers the plugin's accounted packet history, job bookkeeping, and controlled writer buffers. It is **not a limit on total OBS process memory**: OBS, Qt, encoders, GPU allocations, and dependency internals use memory separately. Higher bitrate or more complex footage fills the budget faster. A large requested duration does not guarantee that it fits.

Before the history fills, saves contain the available playable footage and are marked `partial`. Under memory pressure, the plugin removes older whole keyframe groups that no accepted save needs. That can shorten subsequent clips. If pinned saves or the minimum playable group prevent recovery, collection stops and accepted saves drain; restart is manual.

Each safely accepted request gets its own output file. The queue permits up to **128 simultaneous accepted jobs**, subject to memory admission. Rejected requests are reported immediately. A save request records its ending time when triggered, so queue delays do not move the requested endpoint.

## Clip boundaries and filenames

Clips start at the latest usable keyframe at or before the requested start. They can be slightly longer than the requested duration, or shorter during warmup and memory pressure. Audio packets and reordered video dependencies can also extend container duration. This is not a frame-exact editing tool.

Filenames inherit OBS's format, replay prefix/suffix, and space preference, then add the requested duration:

```text
Replay 2026-10-19 19-30-00 60s.mkv
Replay 2026-10-19 19-30-00 60s partial.mkv
Replay 2026-10-19 19-30-00 60s (2).mkv
```

Timestamps refer to the save request, not the later disk-write time. Without spaces, filenames and collision suffixes use underscores. Relative date subfolders in OBS's filename format are supported. Actual duration and partial reasons appear in tooltips and the OBS log.

The writer creates `*.incomplete.mkv` files and publishes final names only after successful finalization. Existing files are never overwritten. Failed or interrupted saves can leave incomplete files; they are not successful saves or a crash-recovery guarantee.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Dock or Tools entry is missing | Confirm the plugin matches the installed OBS variant, restart OBS, and inspect its startup log for module/dependency errors. Flatpak and RPM packages are different. |
| Buffering cannot start | Stop the built-in replay buffer; check the recording encoder/output mode and shared destination. Read the error in Diagnostics. |
| Save is disabled just after starting | Wait for the first playable keyframe. Hover over Save for readiness. |
| Clips are shorter than requested | Check warmup, memory pressure, recording pauses, and the partial reason. A longer buffer may need more RAM or a lower recording bitrate. |
| Only one preset fails to save | Check that preset's directory, permissions, available space, and Flatpak filesystem access. |
| Recording starts late while buffering | Shared consumers join at the next keyframe. Shorten the recording keyframe interval if appropriate. |
| Replay stops advancing during a recording pause | This is shared-mode behavior. Enable the separate replay encoder for independent collection. |
| Warning about a second encoder | OBS recreated its recording encoder. Restart replay buffering after applying OBS settings. |
| Settings cannot be edited | Wait for buffering and accepted saves to stop. Diagnostics remains available. |
| OBS takes time to close with saves pending | Shutdown allows bounded settlement and writer draining. A stalled writer can extend this to roughly 27 seconds. |

The status line is deliberately short; hover over it for the full message. Success feedback lasts five seconds. Errors and actionable warnings remain until resolved, and Diagnostics keeps the latest save details.

When reporting a bug, include your commit/version, OS, OBS version and installation type, recording settings, actual encoder mode, and reproduction steps. Share only the relevant log excerpt, with personal details removed. See [outstanding validation work](VALIDATION.md#remaining-validation) before relying on an untested combination.

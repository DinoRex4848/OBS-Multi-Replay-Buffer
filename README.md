# OBS Multi Replay Buffer

Save several replay lengths from one encoded memory buffer in OBS Studio, with a separate save button and hotkey for each preset.

[![Native builds](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml/badge.svg)](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml)
[Installer instructions](docs/INSTALLERS.md) · [User guide](docs/USER_GUIDE.md) · [Build from source](docs/DEVELOPMENT.md) · [Validation and limitations](docs/VALIDATION.md) · [Changelog](CHANGELOG.md)

**Experimental — installers under validation.** Version `0.1.2` adds an offline Windows setup and a single-file Linux Flatpak installer. Native Windows acceptance of this revision is still pending. Release automation prepares an unpublished draft for testing.

<p align="center">
  <img src="docs/images/replay-dock.png" alt="Multi-Length Replay dock with three preset save buttons, Start Buffering, and Settings" width="360">
</p>

*The dock with customized 2, 4, and 6 second presets. New profiles start with 60, 240, and 600 seconds.*

## What you can do

- Keep three editable replay lengths ready at once: one minute, four minutes, and ten minutes by default.
- Save each preset with its own button or OBS hotkey, including while the dock is hidden.
- Share OBS's recording encoders by default, or choose separate replay encoders when recording and replay need to run independently.
- Preserve selected audio tracks and save directly to MKV without re-encoding the replay or first writing a longer recording.
- Use OBS's recording folder, one shared destination, or a separate folder for each preset.
- Set a replay RAM budget, see live diagnostics, and save clearly labeled partial clips while the buffer warms up or under memory pressure.
- Use OBS's filename format and replay prefix/suffix, with collision handling that preserves existing files.

## Install or update

Close OBS, then choose the download for your OBS installation from a successful [Native builds](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/workflows/build.yml) run:

- **Normally installed Windows OBS:** double-click `obs-multi-replay-0.1.2-windows-x64-setup.exe`, approve the initial administrator prompt, check the detected OBS folder, then install. Remove through Windows **Installed Apps**.
- **Portable Windows OBS:** extract the entire Windows ZIP, run `Install.cmd`, and choose **Portable** and your OBS folder. Use `Uninstall.cmd` for removal.
- **Linux Flatpak OBS:** run `sh ./obs-multi-replay-0.1.2-linux-flatpak-x86_64.run` in a terminal, without sudo. Add `--uninstall` for removal. Python 3.10+ and a matching x86_64 Flatpak OBS are required.
- **Native Linux RPM/deb OBS:** the Flatpak installer cannot install into native OBS. Fedora 44 has a separate developer build path; a general native Linux installer is not offered.

Run the same installer again to update or repair. Profiles, scenes, replay settings, hotkeys and recordings are preserved. Linux packages must match the exact OBS commit in `linux-build.json`; they do not change OBS or install host dependencies. See [installer instructions](docs/INSTALLERS.md) for compatibility and logs, and the [local review](docs/INSTALLER_REVIEW.md) for this revision's test evidence. Old drafts are superseded; draft releases are visible to repository maintainers until published.

## Current platform status

| Platform | Status | Build environment |
| --- | --- | --- |
| Linux x86_64, Flatpak OBS | Latest local build and runtime testing targets OBS 32.2.2 | Matching Freedesktop 25.08 SDK; OBS-provided Qt 6 and FFmpeg 8 |
| Linux x86_64, Fedora RPM OBS | Historical runtime testing on Fedora 44 with OBS 32.1.1; Fedora build CI included | OBS/Qt development packages and FFmpeg 8 |
| Windows 10/11 x64 | Previous MSVC/CMD/dependency checks passed; new EXE native acceptance and real OBS capture remain outstanding | Visual Studio 2022 and the OBS 32.1.1 SDK |

The plugin checks for OBS **32.1 or newer**, but that is not a guarantee of compatibility with every newer version. NVIDIA AV1, H.264, and HEVC with SDR and multiple FLAC tracks have the strongest Linux evidence. Other platforms, encoders, and recording modes need further testing. Read the [validation summary](docs/VALIDATION.md) for what was actually exercised.

## Make your first replay

After building and installing the plugin for your OBS installation:

1. Choose your recording encoder, quality, audio tracks, and recording folder in OBS Settings.
2. Open **Docks → Multi-Length Replay**. This is the plugin's current name inside OBS.
3. Open **Tools → Multi-Buffer Settings**, or click **Settings** in the dock. New profiles use a **6 GB decimal** replay budget and share recording encoders. Apply your changes.
4. Adjust the three preset names and durations, then click **Start Buffering**.
5. Click a preset's **Save** button, or assign its shortcut under **OBS Settings → Hotkeys**. Saves become available once playable history exists; early saves can be shorter than requested.
6. Click **Stop Buffering** to stop collecting and finish accepted saves.

Sharing also shares recording pauses: replay collects nothing while the recording is paused, and paused time is removed from the saved timeline. Select **Use separate replay encoder** if replay should continue through recording pauses. This adds encoding work when recording and replay run together.

See the [user guide](docs/USER_GUIDE.md) for save folders, hotkeys, clip boundaries, settings, and troubleshooting. OBS's built-in replay buffer must be stopped while using this plugin.

## Build from source

Clone this repository, then follow the instructions for your OBS installation:

```bash
git clone https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer.git
cd OBS-Multi-Replay-Buffer
```

- **Flatpak OBS:** install its matching SDK and run `./scripts/build-flatpak.sh`.
- **Fedora RPM OBS:** install the native development packages, or build with rootless Podman.
- **Windows:** install the C++ build tools and run `./scripts/build-windows.ps1` in PowerShell.

The [development guide](docs/DEVELOPMENT.md) includes prerequisites, complete commands, developer installation instructions, and tests. Flatpak and RPM builds use different library environments and are not interchangeable. Compiled plugins, dependencies, and recordings are kept out of Git.

## Feedback and license

Use [Issues](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/issues) for bugs and suggestions. Include the commit or plugin version, operating system, OBS version and installation type, encoder mode, and steps to reproduce. Include the relevant Diagnostics message or OBS log excerpt after removing personal paths and private information.

OBS Multi Replay Buffer is licensed under [GNU GPL v2.0 or later](LICENSE). Dependencies retain their own licenses; see [third-party notes](docs/THIRD_PARTY.md). This is an independent plugin project and is not an official OBS Studio component.

## AI assistance

OBS Multi Replay Buffer was made with substantial assistance from ChatGPT and OpenAI Codex, including help writing and debugging code, developing tests, and preparing documentation. I'm still learning to code, and I want to be transparent that this project reflects that collaboration.

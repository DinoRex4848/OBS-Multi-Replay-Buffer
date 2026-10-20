# Third-party notes

## Project license

OBS Multi Replay Buffer is licensed under **GNU GPL v2.0 or later**. The repository's [LICENSE](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/blob/main/LICENSE) contains the GPL v2 text.

OBS integration and recording-setting mappings were developed against OBS Studio 32.1.1 and checked for the Flatpak OBS 32.2.2 target. OBS Studio is GPL-2.0-or-later. Specific upstream implementation references are listed in [Architecture](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/blob/main/docs/ARCHITECTURE.md).

## Dependencies

| Dependency | Use | Upstream |
| --- | --- | --- |
| OBS Studio / libobs / frontend API | Plugin lifecycle, encoded outputs, recording settings, hotkeys, and docks | [OBS Studio source](https://github.com/obsproject/obs-studio) |
| Qt 6 | Native dock and settings UI | [Qt source](https://code.qt.io/cgit/qt/) |
| FFmpeg libraries | Muxing encoded packets into MKV | [FFmpeg source and licensing](https://ffmpeg.org/legal.html) |
| OBS dependency packages | Windows build SDK dependencies | [obs-deps](https://github.com/obsproject/obs-deps) |

The source repository does not include dependency binaries or downloaded SDKs. Linux RPM packages use installed system libraries; Flatpak packages use the libraries supplied by the installed OBS app. Their source and license notices remain available through the respective distribution/app packages.

Windows development packaging stages FFmpeg DLLs and the release MSVC runtime beside the writer helper. The build script also collects available dependency license/copyright notices. Dependencies retain their own licenses. Before publishing any binary release, verify the notices and corresponding source/source-offer requirements for the exact dependency builds distributed; creating a development ZIP is not a completed release review.

No binary release is currently published. Installer source and CI artifacts support testing. Windows script-package and historical setup acceptance results and remaining release checks are recorded in `VALIDATION.md`; creating a draft release does not publish it.

The script installers use built-in Windows PowerShell and, on Linux, the user's Python 3 standard library. They do not bundle a graphical installer runtime. Inno Setup, PySide6, Shiboken6 and PyInstaller are not used by the current packaging. The plugin still uses OBS's Qt libraries for its own dock/settings UI. Windows dependency notices accompany the archive under `PluginFiles` and as `windows-installer-notices.txt`. Linux supplies the plugin license and dependency/source references as `linux-installer-notices.txt`; OBS's runtime libraries are not redistributed in the Linux archive.

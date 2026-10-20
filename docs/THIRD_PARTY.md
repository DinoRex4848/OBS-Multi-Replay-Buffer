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

No binary release is currently published. Installer source and CI artifacts support testing. Compiled Windows setup acceptance results and remaining release checks are recorded in `VALIDATION.md`; creating a draft release does not publish it.

The graphical Windows installer is generated with Inno Setup (https://jrsoftware.org/). The graphical Linux installer bundles Python (https://www.python.org/), PySide6/Qt and Shiboken6 (https://code.qt.io/cgit/pyside/pyside-setup.git/ and https://code.qt.io/cgit/qt/), and the PyInstaller bootloader (https://github.com/pyinstaller/pyinstaller). The pinned packaging versions are in `packaging/linux/requirements.txt`. Exact installed distribution license texts are collected into `linux-installer-notices.txt`, embedded in the wizard and included with release assets. Qt/PySide/Shiboken LGPL/GPL terms and the PyInstaller bootloader exception remain applicable; installer runtime libraries are distinct from OBS's plugin runtime.

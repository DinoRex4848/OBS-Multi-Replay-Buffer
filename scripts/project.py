"""Facts about the project shared by the build and release scripts."""
import pathlib
import re
import shutil

REPO = pathlib.Path(__file__).resolve().parents[1]
PLUGIN = "obs-multi-replay"
FLATPAK_APP = "com.obsproject.Studio"
# FFmpeg 8 sonames the Linux build must use; also checked in CMakeLists.txt and
# packaging/linux/replay_installer.py, which cannot import this file.
FFMPEG_ABI = {"avformat": 62, "avcodec": 62, "avutil": 60}
# Documentation copied into user packages. Developer notes stay in the repository.
USER_DOCS = ["README.md", "LICENSE", "CHANGELOG.md", "docs/USER_GUIDE.md", "docs/INSTALLERS.md",
             "docs/THIRD_PARTY.md", "docs/images"]


def version():
    """The plugin version from CMakeLists.txt, the single source of truth."""
    text = (REPO / "CMakeLists.txt").read_text()
    return re.search(rf"project\({PLUGIN} VERSION ([0-9]+\.[0-9]+\.[0-9]+)", text)[1]


def copy_user_docs(destination):
    for name in USER_DOCS:
        source, target = REPO / name, destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, target)
        else:
            shutil.copy2(source, target)

#!/usr/bin/env python3
"""Validate versioned installer assets and create/update only a draft prerelease."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import stat
import tarfile
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]


def version():
    return re.search(r"project\(obs-multi-replay VERSION ([\d.]+)", (REPO / "CMakeLists.txt").read_text())[1]


def check_tag(reference):
    if reference.startswith("refs/tags/"):
        if reference != "refs/tags/v" + version():
            raise RuntimeError(f"Tag {reference} does not match CMake version {version()}")


def validate_archive(path):
    windows = path.suffix == ".zip"
    scripts = ("Install.cmd", "Uninstall.cmd") if windows else ("install.sh", "uninstall.sh")
    expected = {*scripts, "README.txt", "PluginFiles"}
    content = {}
    if windows:
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                name = entry.filename.replace("\\", "/").rstrip("/")
                if stat.S_ISLNK(entry.external_attr >> 16):
                    raise RuntimeError("Links are not allowed in release archives")
                if not entry.filename.replace("\\", "/").endswith("/"):
                    if name in content:
                        raise RuntimeError("Duplicate archive path")
                    content[name] = archive.read(entry)
    else:
        with tarfile.open(path, "r:gz") as archive:
            for entry in archive:
                if not (entry.isfile() or entry.isdir()):
                    raise RuntimeError("Links/special files are not allowed in release archives")
                if entry.isfile():
                    if entry.name in content:
                        raise RuntimeError("Duplicate archive path")
                    content[entry.name] = archive.extractfile(entry).read()
                    if (entry.name in scripts or entry.name.endswith("/multi-replay-writer")) and not entry.mode & 0o111:
                        raise RuntimeError("Linux executable permission is missing")
    for name in content:
        parts = pathlib.PurePosixPath(name)
        if parts.is_absolute() or ".." in parts.parts or ":" in name:
            raise RuntimeError("Unsafe archive path")
    if {name.split("/")[0] for name in content} != expected:
        raise RuntimeError("Archive must contain exactly three files and PluginFiles")
    for name in (*scripts, "README.txt"):
        if name not in content:
            raise RuntimeError("Missing top-level script or README")
    for name in scripts:
        if windows:
            if b"\r\n" not in content[name] or b"\n" in content[name].replace(b"\r\n", b""):
                raise RuntimeError("CMD scripts must use CRLF")
        elif b"\r" in content[name]:
            raise RuntimeError("Shell scripts must use LF")
    prefix = "PluginFiles/payload/obs-multi-replay/"
    manifest_name = "PluginFiles/package.json" if windows else prefix + "manifest.json"
    manifest = json.loads(content[manifest_name].decode("utf-8-sig"))
    payload_version = manifest["version" if windows else "plugin_version"]
    if payload_version != version():
        raise RuntimeError("Archive payload version does not match CMake")
    listed = {f["path"]: f["sha256"] for f in manifest["files"]} if windows else manifest["files"]
    actual = {n[len(prefix):] for n in content if n.startswith(prefix) and n != prefix + "manifest.json"}
    if actual != set(listed):
        raise RuntimeError("Payload contents differ from manifest")
    for name, digest in listed.items():
        if hashlib.sha256(content[prefix + name]).hexdigest().lower() != digest.lower():
            raise RuntimeError("Payload hash mismatch: " + name)


def prepare(directory):
    required = [f"obs-multi-replay-{version()}-windows-x64.zip",
                f"obs-multi-replay-{version()}-linux-flatpak-x86_64.tar.gz"]
    lines = []
    files = []
    for name in required:
        path = directory / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksum = path.with_name(name + ".sha256")
        if checksum.read_text(encoding="utf-8-sig").strip().lower() != f"{digest}  {name}".lower():
            raise RuntimeError(f"Checksum mismatch: {name}")
        validate_archive(path)
        lines.append(f"{digest}  {name}\n")
        files.extend((path, checksum))
    for name in ("linux-installer-notices.txt", "windows-installer-notices.txt", "linux-build.json"):
        path = directory / name
        if not path.is_file():
            raise RuntimeError(f"Missing release notice/metadata: {name}")
        files.append(path)
    checksums = directory / "SHA256SUMS"
    checksums.write_text("".join(lines))
    license_path = directory / "LICENSE"
    license_path.write_bytes((REPO / "LICENSE").read_bytes())
    files.extend((checksums, license_path))
    metadata = json.loads((directory / "linux-build.json").read_text())
    body = directory / "release-notes.txt"
    body.write_text(f"Experimental OBS Multi-Length Replay {version()}\n\n"
                    "Extract the ENTIRE archive for your platform into a writable folder. Rerun its install script to update; "
                    "settings, hotkeys and recordings are preserved. Close OBS before installation or removal.\n\n"
                    "Windows: x64 OBS 32.1 or newer; standard and portable installations. "
                    "Run Install.cmd or Uninstall.cmd. Built-in Windows PowerShell is used.\n\n"
                    f"Linux: Flatpak OBS {metadata['obs_version']} x86_64, commit `{metadata['app_commit']}`. "
                    "Python 3.10+ and Flatpak are required. Run `sh ./install.sh` or `sh ./uninstall.sh`, without sudo. "
                    "Other OBS commits require a matching installer. Native RPM/deb OBS is not targeted.\n\n"
                    "Review docs/INSTALLERS.md and docs/VALIDATION.md for acceptance results and remaining manual checks.\n")
    return files, body


def draft(directory, reference):
    if not reference.startswith("refs/tags/"):
        raise RuntimeError("Draft releases require a version tag")
    check_tag(reference)
    files, body = prepare(directory)
    tag = reference.removeprefix("refs/tags/")
    existing = subprocess.run(["gh", "release", "view", tag, "--json", "isDraft"], capture_output=True, text=True)
    if existing.returncode == 0:
        if not json.loads(existing.stdout)["isDraft"]:
            raise RuntimeError("Refusing to change an already published release")
        subprocess.run(["gh", "release", "edit", tag, "--draft", "--prerelease", "--notes-file", str(body)], check=True)
        subprocess.run(["gh", "release", "upload", tag, "--clobber", *map(str, files)], check=True)
    else:
        subprocess.run(["gh", "release", "create", tag, "--draft", "--prerelease", "--verify-tag",
                        "--title", f"OBS Multi-Length Replay {version()}", "--notes-file", str(body), *map(str, files)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "prepare", "draft"))
    parser.add_argument("--ref", required=True)
    parser.add_argument("--directory", type=pathlib.Path, default=REPO / "dist")
    args = parser.parse_args()
    check_tag(args.ref)
    if args.command == "prepare":
        prepare(args.directory)
    elif args.command == "draft":
        draft(args.directory, args.ref)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Validate versioned installer assets and create/update only a draft prerelease."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

REPO = pathlib.Path(__file__).resolve().parents[1]


def version():
    return re.search(r"project\(obs-multi-replay VERSION ([\d.]+)", (REPO / "CMakeLists.txt").read_text())[1]


def check_tag(reference):
    if reference.startswith("refs/tags/"):
        if reference != "refs/tags/v" + version():
            raise RuntimeError(f"Tag {reference} does not match CMake version {version()}")


def prepare(directory):
    required = [f"obs-multi-replay-{version()}-windows-x64-setup.exe",
                f"obs-multi-replay-{version()}-linux-flatpak-x86_64-setup.run"]
    lines = []
    files = []
    for name in required:
        path = directory / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksum = path.with_name(name + ".sha256")
        if checksum.read_text(encoding="utf-8-sig").strip().lower() != f"{digest}  {name}".lower():
            raise RuntimeError(f"Checksum mismatch: {name}")
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
                    "Download and run the installer for your platform. Rerun the latest installer to update; "
                    "settings, hotkeys and recordings are preserved. Close OBS before installation or removal.\n\n"
                    "Windows: x64 OBS 32.1 or newer; standard and portable installations. "
                    "The EXE is unsigned.\n\n"
                    f"Linux: Flatpak OBS {metadata['obs_version']} x86_64, commit `{metadata['app_commit']}`. "
                    "Enable executable permission for the .run file, then open it. "
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

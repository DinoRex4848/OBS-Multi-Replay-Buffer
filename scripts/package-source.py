#!/usr/bin/env python3
"""Package current source, including uncommitted changes, without altering this checkout's Git history."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

repo = pathlib.Path(__file__).resolve().parent.parent
version = re.search(r"project\(obs-multi-replay VERSION ([0-9.]+)", (repo / "CMakeLists.txt").read_text()).group(1)
prefix = f"obs-multi-replay-{version}"
dist = repo / "dist"
dist.mkdir(exist_ok=True)


def git(*arguments, cwd=repo):
    return subprocess.check_output(["git", *arguments], cwd=cwd)


files = sorted(set(git("ls-files", "--cached", "--others", "--exclude-standard", "-z").decode().split("\0")) - {""})
base_commit = git("rev-parse", "HEAD").decode().strip()
refs = git("for-each-ref", "--format=%(refname) %(objectname)", "refs/heads", "refs/tags").decode().splitlines()
with tempfile.TemporaryDirectory(prefix="obs-replay-source-") as temporary:
    snapshot = pathlib.Path(temporary) / "snapshot"
    # Work in a private copy: the user's branch, index and working files stay
    # untouched. The bundle still contains the earlier reviewed commits.
    subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", "--no-checkout", str(repo), str(snapshot)], check=True)
    for ref in refs:
        name, value = ref.split(" ", 1)
        git("update-ref", name, value, cwd=snapshot)
    git("remote", "remove", "origin", cwd=snapshot)
    manifest = {"version": version, "base_commit": base_commit, "files": {}}
    for name in files:
        source = repo / name
        if not source.is_file():
            continue
        destination = snapshot / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        manifest["files"][name] = hashlib.sha256(source.read_bytes()).hexdigest()
    (snapshot / "SOURCE_SNAPSHOT.json").write_text(json.dumps(manifest, indent=2) + "\n")
    git("add", "--all", cwd=snapshot)
    # The generated manifest is ignored in normal checkouts but belongs in the
    # distribution snapshot, including when the source checkout is clean.
    git("add", "--force", "SOURCE_SNAPSHOT.json", cwd=snapshot)
    git("-c", "user.name=Source distribution snapshot", "-c", "user.email=source-snapshot@localhost",
        "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgsign=false", "commit", "--quiet", "-m",
        "Snapshot current source for distribution", cwd=snapshot)
    git("bundle", "create", str(dist / f"{prefix}-source.bundle"), "--all", cwd=snapshot)
    packaged_files = [snapshot / name for name in manifest["files"]] + [snapshot / "SOURCE_SNAPSHOT.json"]
    with tarfile.open(dist / f"{prefix}-source.tar.gz", "w:gz") as archive:
        for path in packaged_files:
            archive.add(path, arcname=prefix + "/" + path.relative_to(snapshot).as_posix())
    with zipfile.ZipFile(dist / f"{prefix}-source.zip", "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in packaged_files:
            archive.write(path, arcname=prefix + "/" + path.relative_to(snapshot).as_posix())

# Include existing platform packages as well as all refreshed source formats.
archives = sorted(p for p in dist.iterdir() if p.name.endswith((".tar.gz", ".zip", ".bundle")))
(dist / "SHA256SUMS").write_text("".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in archives))
for extension in ["tar.gz", "zip", "bundle"]:
    print(dist / f"{prefix}-source.{extension}")

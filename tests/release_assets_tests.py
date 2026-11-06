#!/usr/bin/env python3
"""Validate release gates without contacting GitHub or publishing anything."""
import hashlib
import zipfile
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("release", pathlib.Path(__file__).resolve().parents[1] / "scripts/release-assets.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        for windows in (True, False):
            suffix = "windows-x64.zip" if windows else "linux-flatpak-x86_64.zip"
            path = self.root / f"obs-multi-replay-{release.version()}-{suffix}"
            # Linux ZIPs hold everything in one folder named like the ZIP.
            folder = "" if windows else path.name.removesuffix(".zip") + "/"
            prefix = "PluginFiles/payload/obs-multi-replay/"
            files = {"data/test": hashlib.sha256(b"fixture").hexdigest()}
            manifest = {"version" if windows else "plugin_version": release.version(),
                        "files": [{"path": k, "sha256": v} for k, v in files.items()] if windows else files}
            content = {prefix + "data/test": b"fixture", "README.txt": b"fixture README"}
            manifest_name = "PluginFiles/package.json" if windows else prefix + "manifest.json"
            content[manifest_name] = json.dumps(manifest).encode()
            for name in (("install.bat", "uninstall.bat") if windows else ("install.sh", "uninstall.sh")):
                content[name] = b"@echo off\r\n" if windows else b"#!/bin/sh\n"
            with zipfile.ZipFile(path, "w") as archive:
                if folder:
                    archive.writestr(zipfile.ZipInfo(folder), b"")
                for name, data in content.items():
                    entry = zipfile.ZipInfo(folder + name)
                    entry.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
                    archive.writestr(entry, data)
            path.with_name(path.name + ".sha256").write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")
        self.linux = next(self.root.glob("*linux*.zip"))
        self.windows = next(self.root.glob("*windows*.zip"))
        for name in ("linux-installer-notices.txt", "windows-installer-notices.txt"):
            (self.root / name).write_text("fixture notices")
        (self.root / "linux-build.json").write_text(json.dumps({"obs_version": "32.2.2", "app_commit": "a" * 64}))

    def test_version_tag_must_match(self):
        release.check_tag("refs/tags/v" + release.version())
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            release.check_tag("refs/tags/v999.0.0")

    def test_all_assets_and_notices_required(self):
        files, body = release.prepare(self.root)
        self.assertEqual(len(files), 9)
        self.assertIn("32.2.2", body.read_text())
        (self.root / "windows-installer-notices.txt").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing release"):
            release.prepare(self.root)

    def test_changed_asset_refused(self):
        path = self.windows
        path.write_bytes(b"damaged")
        with self.assertRaisesRegex(RuntimeError, "Checksum mismatch"):
            release.prepare(self.root)

    def test_linux_files_must_be_in_one_folder(self):
        release.validate_archive(self.linux)
        with zipfile.ZipFile(self.linux, "a") as archive:
            archive.writestr("install.sh", "#!/bin/sh\n")
        with self.assertRaisesRegex(RuntimeError, "one folder"):
            release.validate_archive(self.linux)

    def test_linux_executable_permission_required(self):
        folder = self.linux.name.removesuffix(".zip") + "/"
        with zipfile.ZipFile(self.linux) as source:
            entries = [(e, source.read(e)) for e in source.infolist()]
        self.linux.unlink()
        with zipfile.ZipFile(self.linux, "w") as dest:
            for entry, data in entries:
                if entry.filename == folder + "install.sh":
                    entry.external_attr = 0o100644 << 16
                dest.writestr(entry, data)
        with self.assertRaisesRegex(RuntimeError, "executable permission"):
            release.validate_archive(self.linux)

    def test_wrong_archive_layout_refused(self):
        path = self.windows
        with zipfile.ZipFile(path, "a") as archive:
            archive.writestr("unexpected.txt", "extra")
        with self.assertRaisesRegex(RuntimeError, "exactly three"):
            release.validate_archive(path)

    def test_extra_empty_root_directory_refused(self):
        path = self.windows
        with zipfile.ZipFile(path, "a") as archive:
            archive.writestr("extra/", b"")
        with self.assertRaisesRegex(RuntimeError, "exactly three"):
            release.validate_archive(path)

    def test_payload_tampering_refused(self):
        path = self.windows
        replacement = self.root / "tampered-windows-x64.zip"
        with zipfile.ZipFile(path) as source, zipfile.ZipFile(replacement, "w") as dest:
            for entry in source.infolist():
                dest.writestr(entry, b"damaged" if entry.filename.endswith("data/test") else source.read(entry))
        with self.assertRaisesRegex(RuntimeError, "Payload hash"):
            release.validate_archive(replacement)

    def test_published_release_never_modified(self):
        with mock.patch.object(release.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, '{"isDraft":false}')) as run:
            with self.assertRaisesRegex(RuntimeError, "already published"):
                release.draft(self.root, "refs/tags/v" + release.version())
            self.assertEqual(run.call_count, 1)

    def test_branch_cannot_create_release(self):
        with mock.patch.object(release.subprocess, "run") as run:
            with self.assertRaisesRegex(RuntimeError, "version tag"):
                release.draft(self.root, "refs/heads/main")
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()

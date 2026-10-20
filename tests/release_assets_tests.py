#!/usr/bin/env python3
"""Validate release gates without contacting GitHub or publishing anything."""
import hashlib
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
        for suffix in ("windows-x64-setup.exe", "linux-flatpak-x86_64-setup.run"):
            path = self.root / f"obs-multi-replay-{release.version()}-{suffix}"
            path.write_bytes(b"fixture asset")
            path.with_name(path.name + ".sha256").write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")
        for name in ("linux-installer-notices.txt", "windows-installer-notices.txt"):
            (self.root / name).write_text("fixture notices")
        (self.root / "linux-build.json").write_text(json.dumps({"obs_version": "32.2.2", "app_commit": "a" * 64}))

    def test_version_tag_must_match(self):
        release.check_tag("refs/tags/v" + release.version())
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            release.check_tag("refs/tags/v999.0.0")

    def test_both_assets_and_notices_required(self):
        files, body = release.prepare(self.root)
        self.assertEqual(len(files), 9)
        self.assertIn("32.2.2", body.read_text())
        (self.root / "windows-installer-notices.txt").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing release"):
            release.prepare(self.root)

    def test_changed_asset_refused(self):
        path = next(self.root.glob("*.exe"))
        path.write_bytes(b"damaged")
        with self.assertRaisesRegex(RuntimeError, "Checksum mismatch"):
            release.prepare(self.root)

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

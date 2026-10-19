#!/usr/bin/env python3
"""Exercise real installation transactions using temporary package/profile fixtures."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("installer", pathlib.Path(__file__).resolve().parents[1] / "scripts/install-flatpak.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.package = self.root / "package"
        files = {}
        for name in ["bin/64bit/obs-multi-replay.so", "data/multi-replay-writer", "data/locale/en-US.ini"]:
            path = self.package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"new payload")
            path.chmod(0o755)
            files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.package / "manifest.json").write_text(json.dumps({
            "target": "flatpak", "architecture": "x86_64", "integration_test": False,
            "ffmpeg_abi": {"avformat": 62, "avcodec": 62, "avutil": 60}, "files": files}))
        self.destination = self.root / "plugins/obs-multi-replay"
        self.destination.mkdir(parents=True)
        (self.destination / "old.so").write_text("old payload")
        self.other = self.destination.parent / "unrelated.so"
        self.other.write_text("keep me")
        self.source = self.root / "legacy/basic.ini"
        self.source.parent.mkdir()
        self.settings = {"share_encoder": True, "seconds0": 60, "hotkey2": [{"key": "OBS_KEY_F13"}]}
        self.source.write_text('[Output]\nMode=Advanced\n[MultiLengthReplay]\nSettings=' + json.dumps(self.settings) + '\n')
        self.target = self.root / "flatpak/Untitled/basic.ini"
        self.target.parent.mkdir(parents=True)
        self.original = b"; preserve comments\r\n[Output]\r\nMode=Advanced\r\n[AdvOut]\r\nRecTracks=15\r\n"
        self.target.write_bytes(self.original)
        (self.target.parent / "recordEncoder.json").write_text("encoder settings")

    def run_install(self):
        return installer.install(self.package, self.destination, self.root / "backups", self.source, self.target)

    def test_update_and_section_only_migration(self):
        backup = self.run_install()
        self.assertTrue(self.target.read_bytes().startswith(self.original))
        self.assertIn(b"OBS_KEY_F13", self.target.read_bytes())
        self.assertEqual((backup / "profile/basic.ini").read_bytes(), self.original)
        self.assertEqual((backup / "plugin/old.so").read_text(), "old payload")
        self.assertFalse((self.destination / "old.so").exists())
        self.assertEqual(self.other.read_text(), "keep me")
        self.run_install()  # Repeated migration is idempotent.

    def test_rollback_after_profile_publish_failure(self):
        with mock.patch.object(installer, "atomic_write", side_effect=OSError("simulated profile write failure")):
            with self.assertRaises(OSError):
                self.run_install()
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")
        self.assertEqual(self.other.read_text(), "keep me")
        self.assertFalse(list(self.destination.parent.glob(".obs-multi-replay-*")))

    def test_changed_payload_refused_before_mutation(self):
        (self.package / "data/multi-replay-writer").write_text("damaged")
        with self.assertRaises(RuntimeError):
            self.run_install()
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertTrue((self.destination / "old.so").exists())

    def test_existing_settings_preserved(self):
        current = self.original + b'\n[MultiLengthReplay]\nSettings={"share_encoder":false}\n'
        self.target.write_bytes(current)
        with self.assertRaises(RuntimeError):
            self.run_install()
        self.assertEqual(self.target.read_bytes(), current)
        self.assertTrue((self.destination / "old.so").exists())

    def test_ui_test_payload_refused(self):
        manifest = self.package / "manifest.json"
        data = json.loads(manifest.read_text())
        data["integration_test"] = True
        manifest.write_text(json.dumps(data))
        with self.assertRaises(RuntimeError):
            self.run_install()


if __name__ == "__main__":
    unittest.main()

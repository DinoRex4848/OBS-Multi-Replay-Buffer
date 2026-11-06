#!/usr/bin/env python3
"""Exercise real installation transactions using temporary package and profile fixtures."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
import shutil
import os
from unittest import mock

spec = importlib.util.spec_from_file_location("installer", pathlib.Path(__file__).resolve().parents[1] / "packaging/linux/replay_installer.py")
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
            path.write_bytes(b"\x7fELF\x02\x01" + bytes(12) + b"\x3e\x00" + b"new payload")
            path.chmod(0o755)
            files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.package / "manifest.json").write_text(json.dumps({
            "target": "flatpak", "architecture": "x86_64", "integration_test": False,
            "plugin_version": "0.1.0", "app_commit": "a" * 64, "obs_version": "32.2.2",
            "ffmpeg_abi": {"avformat": 62, "avcodec": 62, "avutil": 60}, "files": files}))
        self.destination = self.root / "plugins/obs-multi-replay"
        self.destination.mkdir(parents=True)
        (self.destination / "old.so").write_text("old payload")
        self.other = self.destination.parent / "unrelated.so"
        self.other.write_text("keep me")
        # The installer never reads or writes OBS profiles; tests assert this file is untouched.
        self.target = self.root / "flatpak/Untitled/basic.ini"
        self.target.parent.mkdir(parents=True)
        self.original = (b"; preserve comments\r\n[Output]\r\nMode=Advanced\r\n"
                         b'[MultiLengthReplay]\r\nSettings={"share_encoder":true}\r\n')
        self.target.write_bytes(self.original)

    def run_install(self):
        return installer.install(self.package, self.destination, self.root / "backups")

    def test_update_replaces_payload_and_keeps_backup(self):
        backup = self.run_install()
        self.assertEqual((backup / "plugin/old.so").read_text(), "old payload")
        self.assertFalse((self.destination / "old.so").exists())
        self.assertTrue((self.destination / "data/multi-replay-writer").is_file())
        self.assertEqual(self.other.read_text(), "keep me")
        self.assertEqual(self.target.read_bytes(), self.original)
        self.run_install()  # Repairing the same version is idempotent.

    def test_rollback_after_commit_failure(self):
        original = installer.write_journal

        def fail_commit(path, record):
            if record["state"] == "committed":
                raise OSError("simulated commit failure")
            return original(path, record)
        with mock.patch.object(installer, "write_journal", side_effect=fail_commit):
            with self.assertRaises(OSError):
                self.run_install()
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")
        self.assertEqual(self.other.read_text(), "keep me")
        self.assertFalse(list(self.destination.parent.glob(".obs-multi-replay-*")))

    def test_changed_payload_refused_before_mutation(self):
        (self.package / "data/multi-replay-writer").write_text("damaged")
        with self.assertRaises(RuntimeError):
            self.run_install()
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertTrue((self.destination / "old.so").exists())

    def test_retired_migration_journal_refused(self):
        journal = self.destination.parent / ".obs-multi-replay-transaction.json"
        installer.write_journal(journal, {"tag": "20261019-120000-123456ab", "state": "publishing",
                                          "had_previous": True, "profile": str(self.target)})
        with self.assertRaisesRegex(RuntimeError, "manual recovery"):
            self.run_install()
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")

    def test_ui_test_payload_refused(self):
        manifest = self.package / "manifest.json"
        data = json.loads(manifest.read_text())
        data["integration_test"] = True
        manifest.write_text(json.dumps(data))
        with self.assertRaises(RuntimeError):
            self.run_install()

    def test_install_update_repair_and_downgrade(self):
        self.assertEqual(installer.installation_status(self.destination, "0.1.0"), ("Update", None))
        shutil.rmtree(self.destination)
        self.assertEqual(installer.installation_status(self.destination, "0.1.0"), ("Install", None))
        installer.install(self.package, self.destination, self.root / "backups")
        self.assertEqual(installer.installation_status(self.destination, "0.1.0"), ("Repair", "0.1.0"))
        self.assertEqual(installer.installation_status(self.destination, "0.2.0"), ("Update", "0.1.0"))
        with self.assertRaisesRegex(RuntimeError, "Downgrading"):
            installer.installation_status(self.destination, "0.0.9")
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_scoped_removal_and_reinstallation(self):
        backup = installer.uninstall(self.destination, self.root / "backups")
        self.assertFalse(self.destination.exists())
        self.assertEqual((backup / "plugin/old.so").read_text(), "old payload")
        self.assertEqual(self.other.read_text(), "keep me")
        self.assertEqual(self.target.read_bytes(), self.original)
        installer.install(self.package, self.destination, self.root / "backups")
        self.assertTrue((self.destination / "data/multi-replay-writer").stat().st_mode & 0o111)

    def test_concurrent_install_refused(self):
        with installer.installation_lock(self.destination):
            with self.assertRaisesRegex(RuntimeError, "already running"):
                self.run_install()

    def test_interrupted_publication_recovered(self):
        tag = "20261019-120000-123456ab"
        old = self.destination.parent / (".obs-multi-replay-old-" + tag)
        self.destination.rename(old)
        shutil.copytree(self.package, self.destination)
        journal = self.destination.parent / ".obs-multi-replay-transaction.json"
        installer.write_journal(journal, {"tag": tag, "state": "publishing", "had_previous": True})
        installer.recover(self.destination)
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")
        self.assertFalse(journal.exists())

    def test_removal_commit_failure_restores_plugin(self):
        original = installer.write_journal
        calls = []
        def fail_commit(path, record):
            calls.append(record["state"])
            if record["state"] == "committed":
                raise OSError("simulated commit failure")
            return original(path, record)
        with mock.patch.object(installer, "write_journal", side_effect=fail_commit):
            with self.assertRaisesRegex(OSError, "commit failure"):
                installer.uninstall(self.destination, self.root / "backups")
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")

    def test_running_obs_refuses_both_operations(self):
        with mock.patch.object(installer, "obs_running", return_value=True):
            for action in ("Install", "Remove"):
                with self.assertRaisesRegex(RuntimeError, "Close OBS"):
                    installer.perform(self.package, action, self.root)
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")

    def test_flatpak_target_checks(self):
        metadata = installer.verify_package(self.package)
        row = f"{installer.APP}\tx86_64\tstable\tuser"
        with mock.patch.object(installer, "host_run", side_effect=[row, "a" * 64]):
            self.assertIn("32.2.2", installer.check_target(metadata))
        with mock.patch.object(installer, "host_run", side_effect=[row.replace("user", "system"), "a" * 64]) as run:
            installer.check_target(metadata)
            self.assertIn("--system", run.call_args[0][0])
        for rows, message in (("", "not installed"), (row + "\n" + row, "Multiple"),
                              (row.replace("x86_64", "aarch64"), "x86_64")):
            with mock.patch.object(installer, "host_run", return_value=rows):
                with self.assertRaisesRegex(RuntimeError, message):
                    installer.check_target(metadata)
        with mock.patch.object(installer, "host_run", side_effect=[row, "b" * 64]):
            with self.assertRaisesRegex(RuntimeError, "Installed commit"):
                installer.check_target(metadata)

    def test_staging_permission_failure_preserves_installation(self):
        with mock.patch.object(installer.shutil, "copytree", side_effect=PermissionError("denied")):
            with self.assertRaises(PermissionError):
                self.run_install()
        self.assertEqual((self.destination / "old.so").read_text(), "old payload")
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_symlink_target_refused(self):
        link = self.root / "link"
        link.symlink_to(self.destination, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "symlink"):
            installer.install(self.package, link, self.root / "backups")


if __name__ == "__main__":
    unittest.main()

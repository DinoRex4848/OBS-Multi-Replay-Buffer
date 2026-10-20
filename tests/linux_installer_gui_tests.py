#!/usr/bin/env python3
"""Exercise the real Qt wizard against isolated plugin/profile fixtures."""
import importlib.util
import io
import os
import pathlib
import tarfile
import unittest
from unittest import mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6 import QtCore, QtWidgets, QtTest
import flatpak_installer_tests as fixtures

spec = importlib.util.spec_from_file_location("setup_gui", pathlib.Path(__file__).resolve().parents[1] / "packaging/linux/setup.py")
gui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gui)


class WizardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def setUp(self):
        self.fixture = fixtures.InstallTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        for name, value in (("configuration_root", self.fixture.root), ("check_target", "Flatpak OBS 32.2.2 (user, stable)"),
                            ("obs_running", False)):
            patch = mock.patch.object(gui.core, name, return_value=value)
            patch.start()
            self.addCleanup(patch.stop)
        for name in ("information", "critical", "question"):
            patch = mock.patch.object(QtWidgets.QMessageBox, name, return_value=QtWidgets.QMessageBox.Yes)
            patch.start()
            self.addCleanup(patch.stop)
        self.window = gui.create_window(self.fixture.package)
        self.window.show()
        self.addCleanup(self.window.close)

    def wait_operation(self):
        for _ in range(250):
            self.app.processEvents()
            if self.window.worker is None:
                return
            QtTest.QTest.qWait(10)
        self.fail("Installer worker did not finish")

    def test_cancel_leaves_existing_installation(self):
        self.assertEqual(self.window.install_button.text(), "Update")
        self.window.reject()
        self.assertFalse(self.window.isVisible())
        self.assertTrue((self.fixture.destination / "old.so").exists())

    def test_install_repair_remove_through_wizard(self):
        self.window.start_install()
        self.window.reject()  # Cannot close a transaction halfway through.
        self.assertTrue(self.window.isVisible())
        self.wait_operation()
        self.assertEqual(self.window.install_button.text(), "Repair")
        self.window.remove()
        self.wait_operation()
        self.assertFalse(self.fixture.destination.exists())
        self.assertEqual(self.window.install_button.text(), "Install")
        self.assertEqual(self.fixture.target.read_bytes(), self.fixture.original)

    def test_compatibility_error_still_allows_removal(self):
        with mock.patch.object(gui.core, "check_target", side_effect=RuntimeError("Unsupported OBS commit")):
            self.window.refresh()
        self.assertFalse(self.window.install_button.isEnabled())
        self.assertTrue(self.window.remove_button.isEnabled())
        self.assertIn("Unsupported OBS commit", self.window.details.text())

    def test_archive_traversal_and_links_rejected(self):
        for name, kind in (("../escape", tarfile.REGTYPE), ("link", tarfile.SYMTYPE)):
            archive = self.fixture.root / "bad.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                entry = tarfile.TarInfo(name)
                entry.type = kind
                entry.size = 1 if kind == tarfile.REGTYPE else 0
                output.addfile(entry, io.BytesIO(b"x") if entry.size else None)
            with self.assertRaisesRegex(RuntimeError, "Invalid installer archive"):
                gui.unpack_payload(archive, self.fixture.root / "extract")
        self.assertFalse((self.fixture.root / "escape").exists())


if __name__ == "__main__":
    unittest.main()

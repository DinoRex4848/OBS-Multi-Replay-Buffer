#!/usr/bin/env python3
"""Standalone graphical Flatpak plugin installer (no host Python required)."""
import argparse
import json
import pathlib
import sys
import tarfile
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
import replay_installer as core


def unpack_payload(archive, destination):
    """The payload archive preserves ELF modes; never extract links or special files."""
    with tarfile.open(archive, "r:gz") as source:
        seen = set()
        for entry in source:
            name = pathlib.PurePosixPath(entry.name)
            if name.is_absolute() or ".." in name.parts or entry.name in seen or not (entry.isdir() or entry.isfile()):
                raise RuntimeError("Invalid installer archive entry")
            seen.add(entry.name)
            target = destination.joinpath(*name.parts)
            if entry.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(entry) as content, target.open("wb") as output:
                    import shutil
                    shutil.copyfileobj(content, output)
                target.chmod(0o755 if entry.mode & 0o111 else 0o644)
    core.verify_package(destination)


def create_window(package):
    from PySide6 import QtCore, QtWidgets

    class Worker(QtCore.QThread):
        finished_operation = QtCore.Signal(bool, str)

        def __init__(self, action, parent):
            super().__init__(parent)
            self.action = action

        def run(self):
            try:
                backup = core.perform(package, self.action)
                message = ("Plugin removed. Your OBS settings, hotkeys and recordings are preserved."
                           if self.action == "Remove" else
                           "Installation complete. Restart OBS, then open Docks → Multi-Length Replay "
                           "and Tools → Multi-Buffer Settings.")
                if backup:
                    message += f"\n\nBackup: {backup}"
                self.finished_operation.emit(True, message)
            except Exception as error:
                self.finished_operation.emit(False, str(error))

    class Window(QtWidgets.QDialog):
        def __init__(self):
            super().__init__()
            self.worker = None
            self.setWindowTitle("OBS Multi-Length Replay Setup")
            self.resize(620, 440)
            layout = QtWidgets.QVBoxLayout(self)
            title = QtWidgets.QLabel("OBS Multi-Length Replay")
            title.setStyleSheet("font-size: 24px; font-weight: bold")
            layout.addWidget(title)
            self.details = QtWidgets.QLabel()
            self.details.setTextFormat(QtCore.Qt.PlainText)
            self.details.setWordWrap(True)
            self.details.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
            layout.addWidget(self.details)
            layout.addStretch()
            license_button = QtWidgets.QPushButton("License and notices")
            license_button.clicked.connect(self.show_license)
            layout.addWidget(license_button)
            buttons = QtWidgets.QHBoxLayout()
            self.refresh_button = QtWidgets.QPushButton("Check again")
            self.refresh_button.clicked.connect(self.refresh)
            self.remove_button = QtWidgets.QPushButton("Remove plugin")
            self.remove_button.clicked.connect(self.remove)
            self.install_button = QtWidgets.QPushButton("Install")
            self.install_button.setDefault(True)
            self.install_button.clicked.connect(self.start_install)
            self.close_button = QtWidgets.QPushButton("Close")
            self.close_button.clicked.connect(self.reject)
            for button in (self.refresh_button, self.remove_button, self.install_button, self.close_button):
                buttons.addWidget(button)
            layout.addLayout(buttons)
            self.refresh()

        def show_license(self):
            dialog = QtWidgets.QDialog(self)
            dialog.setWindowTitle("License and third-party notices")
            dialog.resize(680, 500)
            layout = QtWidgets.QVBoxLayout(dialog)
            text = QtWidgets.QPlainTextEdit()
            text.setReadOnly(True)
            notices = [package / "LICENSE", package / "docs/THIRD_PARTY.md"]
            bundled = pathlib.Path(__file__).resolve().parent / "installer-notices.txt"
            notices.append(bundled)
            text.setPlainText("\n\n".join(p.read_text() for p in notices if p.is_file()))
            layout.addWidget(text)
            dialog.exec()

        def refresh(self):
            config = core.configuration_root()
            target = config / "plugins/obs-multi-replay"
            self.remove_button.setEnabled(target.exists())
            self.install_button.setEnabled(False)
            try:
                metadata = core.verify_package(package)
                self.action, current = core.installation_status(target, metadata["plugin_version"])
                self.install_button.setText(self.action)
                target_info = core.check_target(metadata)
                self.details.setText(f"Package version: {metadata['plugin_version']}\n"
                                     f"Detected: {target_info}\n"
                                     f"Installed: {current or ('older script installation' if target.exists() else 'not installed')}\n"
                                     f"Action: {self.action}\n\nDestination: {target}\n\n"
                                     "Close OBS before continuing. Settings, hotkeys and recordings are preserved.")
                self.install_button.setEnabled(True)
            except Exception as error:
                self.details.setText(f"Setup cannot install yet:\n\n{error}\n\nDestination: {target}")

        def start_install(self):
            self.start(self.action)

        def remove(self):
            if QtWidgets.QMessageBox.question(self, "Remove plugin?",
                    "Remove Multi-Length Replay? OBS settings, hotkeys and recordings will be kept.") == QtWidgets.QMessageBox.Yes:
                self.start("Remove")

        def start(self, action):
            for button in (self.refresh_button, self.remove_button, self.install_button, self.close_button):
                button.setEnabled(False)
            self.details.setText(f"{action} in progress…")
            self.worker = Worker(action, self)
            self.worker.finished_operation.connect(self.completed)
            self.worker.start()

        def completed(self, success, message):
            self.worker.wait()
            self.worker.deleteLater()
            self.worker = None
            self.refresh()
            self.close_button.setEnabled(True)
            self.refresh_button.setEnabled(True)
            if success:
                QtWidgets.QMessageBox.information(self, "Complete", message)
            else:
                QtWidgets.QMessageBox.critical(self, "Setup could not finish", message)

        def reject(self):
            if self.worker is None:
                super().reject()

    return Window()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, help="Developer-only unpacked payload override")
    parser.add_argument("--verify-payload", action="store_true", help="Check the embedded payload without installing")
    parser.add_argument("--smoke-test", action="store_true", help="Open and close the wizard without installing")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="obs-replay-setup-") as temporary:
        package = args.package
        if package is None:
            package = pathlib.Path(temporary) / "payload"
            package.mkdir()
            unpack_payload(pathlib.Path(__file__).resolve().parent / "payload.tar.gz", package)
        if args.verify_payload:
            print(json.dumps(core.verify_package(package), indent=2))
            return 0
        from PySide6 import QtCore, QtWidgets
        application = QtWidgets.QApplication(sys.argv[:1])
        application.setApplicationName("OBS Multi-Length Replay Setup")
        window = create_window(package)
        window.show()
        if args.smoke_test:
            QtCore.QTimer.singleShot(500, application.quit)
        return application.exec()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        if "--verify-payload" in sys.argv:
            print(str(error), file=sys.stderr)
            sys.exit(1)
        from PySide6 import QtWidgets
        application = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv[:1])
        QtWidgets.QMessageBox.critical(None, "Setup could not start", str(error))
        sys.exit(1)

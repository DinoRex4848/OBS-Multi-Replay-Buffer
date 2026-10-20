#!/usr/bin/env python3
"""Console frontend shipped beside the shared engine in PluginFiles."""
import argparse
import contextlib
import datetime
import json
import os
import pathlib
import sys
import traceback
import uuid

import replay_installer as core


class Tee:
    def __init__(self, console, log):
        self.console, self.log = console, log

    def write(self, text):
        self.console.write(text)
        self.log.write(text)
        self.flush()
        return len(text)

    def flush(self):
        self.console.flush()
        self.log.flush()


def run(args, private):
    if os.environ.get("SUDO_USER") or os.environ.get("SUDO_UID"):
        raise RuntimeError("Run this script as your desktop user, without sudo.")
    config = core.configuration_root()
    target = config / "plugins/obs-multi-replay"
    if core.obs_running():
        raise RuntimeError("Close OBS normally before installing or removing the plugin.")
    package = private / "payload/obs-multi-replay"
    if args.action == "install":
        manifest = core.verify_package(package)
        detected = core.check_target(manifest)
        action, current = core.installation_status(target, manifest["plugin_version"])
        print(f"Detected: {detected}\nInstalled: {current or ('unknown legacy version' if target.exists() else 'not installed')}")
        print(f"Action: {action} version {manifest['plugin_version']}")
    else:
        print("Action: Remove Multi-Length Replay only")
    print(f"Destination: {target}\nProfiles, scenes, hotkeys and recordings are preserved.")
    if not args.non_interactive and input("Continue? [y/N] ").strip().lower() not in ("y", "yes"):
        print("Cancelled. No plugin files were changed.")
        return 0
    backup = core.perform(package, "Remove" if args.action == "uninstall" else "Install")
    if backup:
        print(f"Backup: {backup}")
    print("Plugin removed." if args.action == "uninstall" else
          "Complete. Restart OBS, then open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings.")
    return 0


def main():
    private = pathlib.Path(__file__).resolve().parent
    logs = private / "Logs"
    try:
        core.reject_symlinks(logs)
        logs.mkdir(exist_ok=True)
        path = logs / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8] + ".log")
        with path.open("x") as log, contextlib.redirect_stdout(Tee(sys.stdout, log)), contextlib.redirect_stderr(Tee(sys.stderr, log)):
            print(f"OBS Multi-Length Replay\nLog: {path}")
            parser = argparse.ArgumentParser(description=__doc__)
            parser.add_argument("--action", choices=("install", "uninstall"), required=True)
            parser.add_argument("--non-interactive", action="store_true", help="Confirm the operation without prompts or closing pauses")
            args = parser.parse_args()
            try:
                return run(args, private)
            except (EOFError, KeyboardInterrupt):
                print("Cancelled.")
                return 130
            except Exception as error:
                print(f"ERROR: {error}\nLog: {path}", file=sys.stderr)
                traceback.print_exc(file=log)
                return 1
    except OSError as error:
        print(f"ERROR: Could not create a log: {error}. Extract the entire archive into a writable folder.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

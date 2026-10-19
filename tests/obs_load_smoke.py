#!/usr/bin/env python3
"""Load the production module in an isolated OBS and close only that process's X11 window."""
import ctypes as c
import ctypes.util
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time

repo = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument("--baseline", action="store_true")
parser.add_argument("--flatpak", action="store_true", help="Use the normal OBS Flatpak runtime, without its SDK")
parser.add_argument("--config", type=pathlib.Path, default=repo / "build/production-ui-config")
parser.add_argument("--build", type=pathlib.Path, default=repo / "build")
parser.add_argument("--data", type=pathlib.Path, default=repo / "build/ui-data")
parser.add_argument("--output", type=pathlib.Path, default=repo / "build")
args = parser.parse_args()


def normal_configuration():
    root = pathlib.Path.home() / ".var/app/com.obsproject.Studio/config/obs-studio"
    files = [root / "global.ini", root / "user.ini"]
    for directory in ["basic", "plugin_manager"]:
        files.extend(p for p in (root / directory).rglob("*") if p.is_file())
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in files if p.is_file()}


original_configuration = normal_configuration() if args.flatpak else None
env = dict(os.environ)
env.update(XDG_CONFIG_HOME=str(args.config.resolve()),
           OBS_PLUGINS_PATH=str(args.build.resolve()),
           OBS_PLUGINS_DATA_PATH=str(args.data.resolve()), QT_QPA_PLATFORM="xcb")
if args.baseline:
    env.pop("OBS_PLUGINS_PATH", None); env.pop("OBS_PLUGINS_DATA_PATH", None)
# Previous intentional timeout smoke may have left the isolated crash marker.
for marker in (args.config / "obs-studio").rglob(".unclean_shutdown"):
    marker.unlink()
log_path = args.output / ("baseline-ui-clean.log" if args.baseline else "production-ui-clean.log")
log = open(log_path, "w")
instance_path = args.output / ("baseline.instance" if args.baseline else "production.instance")
pid_path = (args.output / ("baseline.pid" if args.baseline else "production.pid")).resolve()
if args.flatpak:
    if "com.obsproject.Studio" in subprocess.check_output(["flatpak", "ps", "--columns=application"], text=True).splitlines():
        raise RuntimeError("Close the other OBS instance before the normal-runtime smoke test")
    pid_path.unlink(missing_ok=True)
    passed = ["QT_QPA_PLATFORM", "OBS_PLUGINS_PATH", "OBS_PLUGINS_DATA_PATH"]
    with instance_path.open("w") as instance:
        command = ["flatpak", "run", "--socket=x11", f"--filesystem={repo}", f"--instance-id-fd={instance.fileno()}"]
        command += [f"--env={key}={env[key]}" for key in passed if key in env]
        if args.baseline:
            command += ["--unset-env=OBS_PLUGINS_PATH", "--unset-env=OBS_PLUGINS_DATA_PATH"]
        command += ["--command=sh", "com.obsproject.Studio", "-c",
                    'printf "%s\\n" "$$" > "$1"; export XDG_CONFIG_HOME="$2"; shift 2; exec /app/bin/obs "$@"',
                    "sh", str(pid_path), str(args.config.resolve()), "--multi", "--disable-missing-files-check"]
        process = subprocess.Popen(command, env=env, stdout=log, stderr=log, pass_fds=(instance.fileno(),))
else:
    process = subprocess.Popen(["obs", "--multi", "--disable-missing-files-check"], env=env, stdout=log, stderr=log)
try:
    time.sleep(5)
    x = c.CDLL(ctypes.util.find_library("X11"))
    x.XOpenDisplay.argtypes = [c.c_char_p]; x.XOpenDisplay.restype = c.c_void_p
    x.XDefaultRootWindow.argtypes = [c.c_void_p]; x.XDefaultRootWindow.restype = c.c_ulong
    x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]; x.XInternAtom.restype = c.c_ulong
    x.XGetWindowProperty.argtypes = [c.c_void_p,c.c_ulong,c.c_ulong,c.c_long,c.c_long,c.c_int,c.c_ulong,c.POINTER(c.c_ulong),c.POINTER(c.c_int),c.POINTER(c.c_ulong),c.POINTER(c.c_ulong),c.POINTER(c.POINTER(c.c_ubyte))]
    x.XFree.argtypes = [c.c_void_p]
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Cannot open X display")
    def atom(name):
        return x.XInternAtom(display, name.encode(), False)
    def property_values(window, name):
        actual = c.c_ulong(); fmt = c.c_int(); count = c.c_ulong(); remaining = c.c_ulong(); data = c.POINTER(c.c_ubyte)()
        status = x.XGetWindowProperty(display, window, atom(name), 0, 4096, False, 0, c.byref(actual), c.byref(fmt), c.byref(count), c.byref(remaining), c.byref(data))
        values = list(c.cast(data, c.POINTER(c.c_ulong))[:count.value]) if status == 0 and fmt.value == 32 and bool(data) else []
        if data:
            x.XFree(data)
        return values
    windows = property_values(x.XDefaultRootWindow(display), "_NET_CLIENT_LIST")
    window_pid = int(pid_path.read_text()) if args.flatpak else process.pid
    owned = [w for w in windows if property_values(w, "_NET_WM_PID") == [window_pid]]
    if not owned:
        raise RuntimeError("Isolated OBS window not found")
    class ClientMessage(c.Structure):
        _fields_ = [("type",c.c_int),("serial",c.c_ulong),("send_event",c.c_int),("display",c.c_void_p),("window",c.c_ulong),("message_type",c.c_ulong),("format",c.c_int),("data",c.c_long*5)]
    class Event(c.Union):
        _fields_ = [("client",ClientMessage),("padding",c.c_long*24)]
    x.XSendEvent.argtypes = [c.c_void_p,c.c_ulong,c.c_int,c.c_long,c.POINTER(Event)]
    x.XFlush.argtypes = [c.c_void_p]
    for window in owned:
        event = Event(); event.client.type = 33; event.client.send_event = True; event.client.display = display; event.client.window = window
        event.client.message_type = atom("WM_PROTOCOLS"); event.client.format = 32; event.client.data[0] = atom("WM_DELETE_WINDOW")
        x.XSendEvent(display, window, False, 0, c.byref(event))
    x.XFlush(display)
    code = process.wait(timeout=15)
    if code:
        raise RuntimeError(f"OBS exited with code {code}")
    log.close()
    text = log_path.read_text()
    assert ("obs-multi-replay.so" in text) != args.baseline
    import re
    leaks = int(re.search(r"Number of memory leaks: (\d+)", text).group(1))
    baseline_path = args.output / "baseline-ui-clean.json"
    if not args.baseline and baseline_path.exists():
        assert leaks <= json.loads(baseline_path.read_text())["obs_allocations_remaining"], "Plugin increased OBS's shutdown allocation count"
    assert "[multi-replay] Session" not in text, "Buffer unexpectedly started"
    report = {"baseline": args.baseline, "module_loaded": not args.baseline, "manual_startup": True, "orderly_exit": True, "obs_allocations_remaining": leaks}
    if args.flatpak:
        assert normal_configuration() == original_configuration, "Normal OBS configuration changed during the isolated smoke"
        report["normal_configuration_unchanged"] = True
    log_path.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS:", report)
finally:
    if process.poll() is None:
        if args.flatpak and instance_path.exists() and instance_path.read_text().strip():
            subprocess.run(["flatpak", "kill", instance_path.read_text().strip()], check=False)
        process.kill(); process.wait()
    log.close()

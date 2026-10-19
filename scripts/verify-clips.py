#!/usr/bin/env python3
"""Decode every selected stream, validate keyframe starts and track metadata."""
import argparse
import json
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=pathlib.Path)
parser.add_argument("--codec", choices=["av1", "h264", "hevc"])
parser.add_argument("--tracks", type=int, default=4)
parser.add_argument("--report", type=pathlib.Path)
args = parser.parse_args()
files = sorted(args.directory.glob("*.mkv"))
if not files:
    raise SystemExit("No MKV clips found")
results = []
for path in files:
    if ".incomplete." in path.name:
        raise SystemExit(f"Incomplete clip: {path}")
    info = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)
    ]))
    streams = info["streams"]
    assert len(streams) == args.tracks + 1, path
    assert streams[0]["codec_type"] == "video", path
    if args.codec:
        assert streams[0]["codec_name"] == args.codec, path
    if args.tracks == 4:
        titles = [s.get("tags", {}).get("title", "") for s in streams[1:]]
        expected = ["Game", "Mic", "Discord", "Desktop"]
        if path.name.startswith("normal-"):
            expected = ["Normal " + s for s in expected]
        assert titles == expected, (path, titles)
    packets = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0", "-read_intervals", "%+#1",
        "-show_packets", "-of", "json", str(path)
    ]))["packets"]
    assert packets and "K" in packets[0]["flags"], path
    subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path),
                    "-map", "0", "-f", "null", "-"], check=True)
    results.append({"path": str(path), "codec": streams[0]["codec_name"],
                    "tracks": len(streams) - 1, "duration": info["format"].get("duration"),
                    "all_streams_decoded": True, "starts_on_keyframe": True})
    print(f"PASS {path.name}", flush=True)
if args.report:
    args.report.write_text(json.dumps(results, indent=2) + "\n")

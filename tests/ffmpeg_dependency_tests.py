#!/usr/bin/env python3
"""Verify Linux configuration rejects old headers and wrong runtime libraries."""
import argparse
import json
import pathlib
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--prefix", type=pathlib.Path, default=pathlib.Path("/app"))
parser.add_argument("--report", type=pathlib.Path)
args = parser.parse_args()
results = []
with tempfile.TemporaryDirectory(prefix="ffmpeg-guards-") as temporary:
    root = pathlib.Path(temporary)
    headers = root / "headers"
    for component, major in [("avformat", 61), ("avcodec", 62), ("avutil", 60)]:
        directory = headers / f"lib{component}"
        directory.mkdir(parents=True)
        (directory / f"{component}.h").write_text(
            f"#define LIB{component.upper()}_VERSION_MAJOR {major}\nunsigned {component}_version(void);\n")
    wrong = root / "wrong-format.cpp"
    wrong.write_text('extern "C" unsigned avformat_version(void) { return 61U << 16; }\n')
    library = root / "libavformat-wrong.so"
    subprocess.run(["c++", "-shared", "-fPIC", str(wrong), "-o", str(library)], check=True)
    for name, override in [("old_headers", f"-DFFMPEG_INCLUDE_DIR={headers}"),
                           ("wrong_runtime", f"-Davformat_LIBRARY={library}")]:
        command = ["cmake", "-S", str(repo), "-B", str(root / name), "-G", "Ninja",
                   f"-DCMAKE_PREFIX_PATH={args.prefix}", f"-DFFMPEG_ROOT={args.prefix}",
                   "-DBUILD_PLUGIN=OFF", "-DBUILD_TESTING=OFF", override]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode == 0 or "Linux requires matching FFmpeg 8 headers and runtime" not in result.stdout:
            raise RuntimeError(f"{name} was not rejected correctly:\n{result.stdout}")
        results.append({"case": name, "configuration_rejected": True})
        print(f"PASS: {name} rejected before building")
if args.report:
    args.report.write_text(json.dumps(results, indent=2) + "\n")

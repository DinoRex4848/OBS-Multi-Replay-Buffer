#!/usr/bin/env python3
"""Create a minimal isolated OBS configuration template for the UI and load tests.

The default profile uses Simple Output with x264, enough for the production load
smoke on hosted builders. --advanced-nvenc uses Advanced Output with NVENC AV1 and
2-second keyframes, like a typical recording setup; the shared and folders UI
scenarios need it, because a shared replay can only start at a keyframe.
"""
import argparse
import json
import pathlib

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("directory", type=pathlib.Path, help="Fresh directory below the repository, named build*")
parser.add_argument("--advanced-nvenc", action="store_true", help="Advanced Output, NVENC AV1, 2 s keyframes")
args = parser.parse_args()
root = args.directory.resolve()
repo = pathlib.Path(__file__).resolve().parents[1]
if not root.is_relative_to(repo) or not root.relative_to(repo).parts[0].startswith("build") or root.exists():
    raise SystemExit("Choose a fresh build directory below the repository")
config = root / "obs-studio"
profile = config / "basic/profiles/Test"
profile.mkdir(parents=True)
(config / "global.ini").write_text("[General]\nFirstRun=true\n[Basic]\nProfile=Test\nProfileDir=Test\nSceneCollection=Test\nSceneCollectionFile=Test\n")
(config / "user.ini").write_text("[General]\nFirstRun=true\n[Basic]\nProfile=Test\nProfileDir=Test\nSceneCollection=Test\nSceneCollectionFile=Test\n[BasicWindow]\nPreviewEnabled=false\n")
video = "[Video]\nBaseCX=640\nBaseCY=360\nOutputCX=640\nOutputCY=360\nFPSType=0\nFPSCommon=30\n"
simple = "[SimpleOutput]\nRecQuality=HQ\nRecEncoder=x264\nRecAudioEncoder=aac\n"
if args.advanced_nvenc:
    (profile / "basic.ini").write_text(
        "[General]\nName=Test\n[Output]\nMode=Advanced\n" + video + simple +
        "[AdvOut]\nRecType=Standard\nRecEncoder=obs_nvenc_av1_tex\nRecAudioEncoder=ffmpeg_aac\n"
        "RecFormat2=mkv\nRecTracks=1\nRecUseRescale=false\n")
    (profile / "recordEncoder.json").write_text(json.dumps(
        {"rate_control": "CQP", "cqp": 24, "preset2": "p5", "keyint_sec": 2}))
else:
    (profile / "basic.ini").write_text("[General]\nName=Test\n[Output]\nMode=Simple\n" + video + simple)
scenes = config / "basic/scenes"
scenes.mkdir()
(scenes / "Test.json").write_text(json.dumps({
    "name": "Test", "current_scene": "Scene", "current_program_scene": "Scene",
    "scene_order": [{"name": "Scene"}], "current_transition": "Fade", "transition_duration": 300,
    "transitions": [], "quick_transitions": [], "groups": [], "canvases": [],
    "sources": [{"name": "Scene", "id": "scene", "versioned_id": "scene",
                 "uuid": "8d64b2f8-4241-42db-9014-000000000001",
                 "canvas_uuid": "6c69626f-6273-4c00-9d88-c5136d61696e",
                 "settings": {"items": [], "id_counter": 0, "custom_size": False}}],
}, indent=2))

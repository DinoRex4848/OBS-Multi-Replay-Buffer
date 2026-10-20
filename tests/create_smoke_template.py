#!/usr/bin/env python3
"""Create a minimal isolated OBS profile for production load tests on builders."""
import pathlib
import json
import sys

root = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(__file__).resolve().parents[1]
if not root.is_relative_to(repo) or not root.relative_to(repo).parts[0].startswith("build") or root.exists():
    raise SystemExit("Choose a fresh build directory below the repository")
config = root / "obs-studio"
profile = config / "basic/profiles/Test"
profile.mkdir(parents=True)
(config / "global.ini").write_text("[General]\nFirstRun=true\n[Basic]\nProfile=Test\nProfileDir=Test\nSceneCollection=Test\nSceneCollectionFile=Test\n")
(config / "user.ini").write_text("[General]\nFirstRun=true\n[Basic]\nProfile=Test\nProfileDir=Test\nSceneCollection=Test\nSceneCollectionFile=Test\n[BasicWindow]\nPreviewEnabled=false\n")
(profile / "basic.ini").write_text("[General]\nName=Test\n[Output]\nMode=Simple\n[Video]\nBaseCX=640\nBaseCY=360\nOutputCX=640\nOutputCY=360\nFPSType=0\nFPSCommon=30\n[SimpleOutput]\nRecQuality=HQ\nRecEncoder=x264\nRecAudioEncoder=aac\n")
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

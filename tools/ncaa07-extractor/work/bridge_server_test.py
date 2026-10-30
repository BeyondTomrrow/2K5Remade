"""Background stand-in for an open Blender: runs the add-on's poll loop for N s,
then reports the scene. blender -b --python work/bridge_server_test.py -- 90"""
import sys
import time
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender_addon'))
import ncaa07_bridge as nb

seconds = float(sys.argv[sys.argv.index('--') + 1])
bpy.ops.wm.read_factory_settings(use_empty=True)
end = time.time() + seconds
while time.time() < end:
    nb._poll()
    time.sleep(0.5)
print('SCENE', {c.name: len(c.all_objects) for c in bpy.context.scene.collection.children})

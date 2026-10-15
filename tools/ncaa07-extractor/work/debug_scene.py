import sys
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender_addon'))
import ncaa07_bridge as nb
args = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=args[0])
for ob in bpy.data.objects:
    if ob in before:
        continue
    if ob.type != 'MESH' or 'ncaa_group' not in ob:
        print('OBJ', ob.name, ob.type, [c.name for c in ob.users_collection], ob.data.name if ob.data else None,
              len(ob.data.vertices) if ob.type == 'MESH' else '', dict((k, ob[k]) for k in ob.keys() if k.startswith('ncaa')))

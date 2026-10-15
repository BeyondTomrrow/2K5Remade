"""Run with Blender --background --python this.py -- path.glb; import QA only."""
import json
import sys
import bpy

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=sys.argv[sys.argv.index('--')+1])
armatures = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o.name.startswith('part_')]
assert len(armatures) == 1 and len(armatures[0].data.bones) == 75
assert len(meshes) == 137
assert all(any(m.type == 'ARMATURE' and m.object == armatures[0] for m in o.modifiers) for o in meshes)
assert all(all(abs(sum(g.weight for g in v.groups)-1)<1e-6 for v in o.data.vertices) for o in meshes)
print('PLAYER_RIG_IMPORT_QA',json.dumps({'armatures':1,'bones':75,'meshes':len(meshes),
      'vertices':sum(len(o.data.vertices) for o in meshes),'all_meshes_bound':True,'all_weights_normalized':True}))

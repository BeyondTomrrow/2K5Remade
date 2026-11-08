"""blender -b --python work/render_anim.py -- <glb> <out_prefix> [action_index]
Renders 6 frames of one animation (front-left view) into <out_prefix>_N.png."""
import sys
import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args[0])
for ob in list(bpy.data.objects):
    if ob.name in bpy.data.collections.get('glTF_not_exported', bpy.data.collections.new('x')).objects:
        bpy.data.objects.remove(ob, do_unlink=True)
rig = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
actions = sorted(bpy.data.actions, key=lambda a: a.name)
act = actions[int(args[2]) if len(args) > 2 else 0]
rig.animation_data_create()
rig.animation_data.action = act
f0, f1 = [int(v) for v in act.frame_range]
print('ACTION', act.name, f0, f1)
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
scene.render.resolution_x = 360; scene.render.resolution_y = 480
scene.world = bpy.data.worlds.new('w'); scene.world.color = (.3, .3, .3)
bpy.ops.object.light_add(type='SUN'); bpy.context.object.rotation_euler = (0.6, 0.2, -0.4); bpy.context.object.data.energy = 3
bpy.ops.mesh.primitive_plane_add(size=20)
bpy.ops.object.camera_add()
cam = bpy.context.object; scene.camera = cam; cam.data.type = 'ORTHO'; cam.data.ortho_scale = 3.2
for k in range(6):
    f = f0 + (f1 - f0) * k // 5
    scene.frame_set(f)
    meshes = [o for o in scene.objects if o.type == 'MESH' and o.find_armature()]
    deps = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in meshes:
        ev = o.evaluated_get(deps)
        pts += [ev.matrix_world @ v.co for i, v in enumerate(ev.data.vertices) if i % 25 == 0]
    c = sum(pts, Vector()) / len(pts)
    cam.location = c + Vector((2.5, -6, 0.6))
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = f'{args[1]}_{k}.png'
    bpy.ops.render.render(write_still=True)

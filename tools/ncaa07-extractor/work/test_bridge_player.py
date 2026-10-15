"""blender -b --python work/test_bridge_player.py -- <glb> <render.png> [Group=Option ...]"""
import sys
from pathlib import Path
import bpy
from mathutils import Vector

here = Path(__file__).resolve().parents[1] / 'blender_addon'
sys.path.insert(0, str(here))
import ncaa07_bridge as nb

args = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
nb.register()
rig = nb.import_player(args[0], 'Player')
for spec in [s for s in args[2:] if "=" in s]:
    g, o = spec.split('=', 1)
    nb.set_option(rig, g, o)
scene = bpy.context.scene
for spec in [s for s in args[2:] if s.startswith('hide:')]:
    for ob in scene.objects:
        if ob.get('ncaa_category') == spec[5:] or ob.get('ncaa_material') == spec[5:]:
            ob.hide_render = True
cols = {c.name: len(c.objects) for c in bpy.data.collections}
visible = [o for o in scene.objects if o.type == 'MESH' and not o.hide_render]
print('RESULT', {'collections': cols, 'visible': len(visible),
                 'hidden': len([o for o in scene.objects if o.type == 'MESH' and o.hide_render]),
                 'rig_bones': len(rig.data.bones) if rig else 0})
for ob in scene.objects:
    if ob.type == 'MESH' and ob.hide_render:
        ob.hide_viewport = True
pts = [o.matrix_world @ Vector(c) for o in visible for c in o.bound_box]
lo = Vector([min(p[i] for p in pts) for i in range(3)]); hi = Vector([max(p[i] for p in pts) for i in range(3)])
center = (lo + hi) / 2; size = max(hi - lo)
import os
view = Vector((0.6, 3, 0.6)) if os.environ.get('NCAA_BACK') else Vector((-3, -1.2, 0.5)) if os.environ.get('NCAA_SIDE') else Vector((0.6, -3, 0.4))
bpy.ops.object.camera_add(location=center + view * size)
cam = bpy.context.object
cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam.data.type = 'ORTHO'; cam.data.ortho_scale = size * 1.15
scene.camera = cam
bpy.ops.object.light_add(type='SUN'); bpy.context.object.rotation_euler = (0.6, 0.2, -0.4); bpy.context.object.data.energy = 3
scene.world = bpy.data.worlds.new('w'); scene.world.color = (.3, .3, .3)
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
import os
if os.environ.get('NCAA_CYCLES'):
    scene.render.engine = 'CYCLES'; scene.cycles.samples = 16
scene.render.resolution_x = int(os.environ.get('NCAA_W', 700)); scene.render.resolution_y = int(os.environ.get('NCAA_H', 900))
scene.render.filepath = args[1]
bpy.ops.render.render(write_still=True)

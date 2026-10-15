"""blender -b --python work/render_stadium.py -- <glb> <out prefix>
Imports through the add-on's stadium path; renders an overview and a field close-up."""
import sys, math
from pathlib import Path
import bpy
from mathutils import Vector
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender_addon'))
import ncaa07_bridge as nb
args = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
nb.register()
objs = nb.import_stadium(args[0])
scene = bpy.context.scene
print('COLLECTIONS', [c.name for c in bpy.data.collections])
print('OBJECTS', len(objs), sorted({o.name.split('.')[0] for o in objs if o.type == 'MESH'})[:60])
bpy.ops.object.light_add(type='SUN'); bpy.context.object.rotation_euler = (0.5, 0.2, -0.4); bpy.context.object.data.energy = 4
scene.world = bpy.data.worlds.new('w'); scene.world.color = (.45, .5, .6)
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
scene.render.resolution_x, scene.render.resolution_y = 1200, 800
for tag, loc, target, lens in (('overview', (150, -170, 130), (0, 0, 0), 30),
                               ('field', (45, -70, 30), (0, -35, 0), 35),
                               ('endzone', (0, -20, 25), (0, -60, 0), 30)):
    bpy.ops.object.camera_add(location=loc); cam = bpy.context.object
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = lens; cam.data.clip_end = 5000; scene.camera = cam
    scene.render.filepath = f'{args[1]}_{tag}.png'
    bpy.ops.render.render(write_still=True)

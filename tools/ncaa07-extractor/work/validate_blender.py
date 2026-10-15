import bpy, sys, json, math
from pathlib import Path
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
source, output = Path(args[0]).resolve(), Path(args[1]).resolve()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
points = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector(tuple(min(p[i] for p in points) for i in range(3)))
hi = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center, size = (lo + hi) / 2, hi - lo
print('NCAA_BLENDER_CHECK',json.dumps({'file': str(source), 'meshes': len(meshes),
 'vertices':sum(len(o.data.vertices) for o in meshes), 'faces':sum(len(o.data.polygons) for o in meshes),
 'bounds':[list(lo),list(hi)]}))
mat=bpy.data.materials.new('Inspection clay')
mat.diffuse_color=(0.55,0.65,0.72,1)
bpy.ops.object.camera_add(location=center+Vector((1.15,-1.6,1.25))*max(size))
cam=bpy.context.object
cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=max(size)*1.65
bpy.context.scene.camera=cam
scene=bpy.context.scene
scene.render.engine='CYCLES'
scene.cycles.samples=16
scene.cycles.use_denoising=True
bpy.ops.object.light_add(type='SUN', location=(0,0,100))
bpy.context.object.rotation_euler=(0.25,-0.4,-0.3)
bpy.context.object.data.energy=3
scene.display.shading.light='STUDIO'
scene.display.shading.color_type='MATERIAL'
scene.display.shading.show_shadows=True
scene.display.shading.show_cavity=True
scene.display.shading.cavity_type='BOTH'
if scene.world is None: scene.world=bpy.data.worlds.new('Inspection world')
scene.world.color=(0.15,0.15,0.15)
scene.render.resolution_x=1000;scene.render.resolution_y=760;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(output)
bpy.ops.render.render(write_still=True)
if len(args)>2:bpy.ops.wm.save_as_mainfile(filepath=str(Path(args[2])))

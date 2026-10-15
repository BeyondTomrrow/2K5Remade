import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

args=sys.argv[sys.argv.index('--')+1:]
source,output=map(Path,args[:2])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source.resolve()))
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
image_nodes=[]
for mat in bpy.data.materials:
    if not mat.use_nodes:continue
    nodes=[n for n in mat.node_tree.nodes if n.type=='TEX_IMAGE']
    if nodes:
        mat.node_tree.nodes.active=nodes[0]
        image_nodes.extend(nodes)
for node in image_nodes:
    assert node.image and node.image.size[0]>0 and node.image.size[1]>0
assert len(meshes)==87 and len(image_nodes)==58
report=dict(meshes=len(meshes),materials=len(bpy.data.materials),
            material_image_nodes=len(image_nodes),loaded_images=len({n.image.name for n in image_nodes}),
            vertices=sum(len(o.data.vertices) for o in meshes),
            faces=sum(len(o.data.polygons) for o in meshes))
print('TEXTURED_STADIUM_CHECK',json.dumps(report))
output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
points=[o.matrix_world@Vector(c) for o in meshes for c in o.bound_box]
lo=Vector(tuple(min(p[i] for p in points) for i in range(3)))
hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
center=(lo+hi)/2;size=max(hi-lo)
bpy.ops.object.camera_add(location=center+Vector((1.15,-1.6,1.25))*size)
cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=size*1.6
scene=bpy.context.scene;scene.camera=cam
scene.render.engine='BLENDER_WORKBENCH'
scene.display.shading.light='STUDIO';scene.display.shading.color_type='TEXTURE'
scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(output.resolve())
bpy.ops.render.render(write_still=True)

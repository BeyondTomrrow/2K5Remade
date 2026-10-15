"""Blender-side assembly of decoded source GLBs; invoked with -- job.json."""
import bpy
import json
import math
import sys
from pathlib import Path
from mathutils import Vector, Matrix

job = json.loads(Path(sys.argv[sys.argv.index('--')+1]).read_text(encoding='utf-8'))
bpy.ops.wm.read_factory_settings(use_empty=True)

def imported(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(Path(path).resolve()))
    return [o for o in bpy.data.objects if o not in before]

def join_meshes(objects, name):
    if not objects: return None
    bpy.ops.object.select_all(action='DESELECT')
    for ob in objects: ob.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.join()
    ob=bpy.context.object;ob.name=name
    return ob

if job['kind']=='player':
    body=imported(job['body'])
    rig=next(o for o in body if o.type=='ARMATURE')
    head=imported(job['head'])
    headrig=next(o for o in head if o.type=='ARMATURE')
    head_meshes=[o for o in head if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
    body_meshes=[o for o in body if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]
    for ob in head_meshes:
        world=ob.matrix_world.copy()
        ob.parent=rig;ob.matrix_world=world
        for mod in ob.modifiers:
            if mod.type=='ARMATURE':mod.object=rig
    helpers=set()
    for arm in (rig,headrig):
        for bone in arm.pose.bones:
            if bone.custom_shape:helpers.add(bone.custom_shape)
            bone.custom_shape=None
    bpy.data.objects.remove(headrig,do_unlink=True)
    for helper in helpers:
        if helper.name in bpy.data.objects:bpy.data.objects.remove(helper,do_unlink=True)
    rig.name='NCAA07_Player_Rig';rig.show_in_front=True;rig.data.display_type='STICK'
    torso=next((ob for ob in body_meshes if ob.data.materials and ob.data.materials[0].name.startswith('torsoSleeve')),None)
    if torso:
        for ob in body_meshes:
            mat=ob.data.materials[0].name if ob.data.materials else ''
            if mat.startswith(('tN_','shN_','decal_two')):
                bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
                mod=ob.modifiers.new('Fit source number overlay to configured jersey','SHRINKWRAP')
                mod.target=torso;mod.wrap_method='NEAREST_SURFACEPOINT';mod.wrap_mode='ON_SURFACE';mod.offset=.002
                bpy.ops.object.modifier_apply(modifier=mod.name)
                ob['authoring_repair']='Source number mesh projected onto configured jersey to avoid buried/copanar decals'
    equipment=[];skin=[]
    for ob in body_meshes:
        mat=ob.data.materials[0].name.lower() if ob.data.materials else ''
        (equipment if any(s in mat for s in ('helmet','facemask','chinstrap','tab')) else skin).append(ob)
    joined=[join_meshes(skin,'Player_Body'),join_meshes(head_meshes,'Player_Head'),join_meshes(equipment,'Player_Helmet')]
    for ob in joined:
        if ob:
            for polygon in ob.data.polygons:polygon.use_smooth=True
    for ob in list(bpy.context.scene.objects):
        if ob.type=='EMPTY' and not ob.children:bpy.data.objects.remove(ob,do_unlink=True)
    bpy.context.scene['source_configuration']='One option from each verified source Swap group; source shadow meshes omitted'
    bpy.context.scene['rig_status']='Source hierarchy retained; partial-cluster deformation is still a preview interpretation'
    visible=[o for o in joined if o]
else:
    stadium=imported(job['stadium']) if job.get('stadium') else []
    field=[]
    for layer in job['layers']:
        obs=imported(layer['file'])
        roots=[o for o in obs if o.parent not in obs]
        for root in roots:root.location.z+=layer['z']
        meshes=[o for o in obs if o.type=='MESH']
        for ob in meshes:ob.name='Field_'+layer['role']+'_'+ob.name
        field+=meshes
        if layer['role']=='turf':
            for ob in meshes:
                for mat in ob.data.materials:
                    shader=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
                    links=mat.node_tree.links
                    shader.inputs['Alpha'].default_value=1
                    for link in list(shader.inputs['Alpha'].links):links.remove(link)
                    mat['source']='FLDDATA fieldnoise.ea3 grayscale detail; editable reconstruction of runtime grass color'
        if layer['role']=='endzone':
            for root in roots:
                duplicate=root.copy();duplicate.data=root.data.copy() if root.data else None
                bpy.context.collection.objects.link(duplicate)
                duplicate.matrix_world=Matrix.Rotation(math.pi,4,'Z')@duplicate.matrix_world
                for ob in meshes:
                    child=ob.copy();child.data=ob.data.copy();bpy.context.collection.objects.link(child);child.parent=duplicate
                    field.append(child)
    if job.get('goalpost'):
        for y,angle in [(-60,0),(60,math.pi)]:
            obs=imported(job['goalpost'])
            for ob in [o for o in obs if o.parent not in obs]:
                ob.matrix_world=Matrix.Translation((0,y,0))@Matrix.Rotation(angle,4,'Z')@ob.matrix_world
    bpy.context.scene['field_source']='FLDDATA: native turf geometry, lines, numbers, team logo and end-zone mesh/texture'
    bpy.context.scene['assembly']='Shared source coordinates; centimetric layer offsets prevent coplanar z-fighting; opposite end zone is rotated 180 degrees'
    visible=[o for o in stadium if o.type=='MESH']+field

points=[o.matrix_world@Vector(c) for o in visible for c in o.bound_box]
bpy.context.view_layer.update()
points=[o.matrix_world@Vector(c) for o in visible for c in o.bound_box]
lo=Vector([min(p[i] for p in points)for i in range(3)]);hi=Vector([max(p[i]for p in points)for i in range(3)])
center=(lo+hi)*.5;size=hi-lo
if job['kind']=='player':
    direction=Vector((1,-3,.7));scale=max(size)*1.25
else:
    direction=Vector((1.1,-1.5,1.6));scale=max(size)*1.3
bpy.ops.object.camera_add(location=center+direction*max(size))
camera=bpy.context.object;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=scale
scene=bpy.context.scene;scene.camera=camera
scene.world=bpy.data.worlds.new('World');scene.world.color=(.25,.25,.25)
bpy.ops.object.light_add(type='SUN');bpy.context.object.rotation_euler=(.35,-.4,-.3);bpy.context.object.data.energy=3
scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
scene.render.resolution_x=1200;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
bpy.ops.file.pack_all()
if job.get('glb'):
    bpy.ops.export_scene.gltf(filepath=job['glb'],export_format='GLB',export_cameras=False,export_lights=False)
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=max(size)*1.6
        area.spaces.active.region_3d.view_location=center
        area.spaces.active.region_3d.view_rotation=camera.rotation_euler.to_quaternion()
        area.spaces.active.region_3d.view_perspective='PERSP'
        area.spaces.active.shading.type='MATERIAL'
        area.spaces.active.shading.use_scene_world=False
        area.spaces.active.shading.use_scene_lights=False
        area.spaces.active.clip_end=max(1000,max(size)*20)
for helper in bpy.context.scene.objects:
    if helper.type in ('LIGHT','CAMERA'):helper.hide_set(True)
bpy.ops.object.select_all(action='DESELECT')
if job['kind']=='player':
    rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.wm.save_as_mainfile(filepath=job['blend'])
if job.get('render'):
    scene.render.filepath=job['render'];bpy.ops.render.render(write_still=True)
print('ASSEMBLY_RESULT',json.dumps({'kind':job['kind'],'meshes':len([o for o in scene.objects if o.type=='MESH']),
      'armatures':len([o for o in scene.objects if o.type=='ARMATURE']),'bounds':[list(lo),list(hi)]}))

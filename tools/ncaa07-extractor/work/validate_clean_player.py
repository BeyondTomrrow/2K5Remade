import bpy,json,sys
from pathlib import Path
import numpy as np
root=Path(sys.argv[sys.argv.index('--')+1])
bpy.ops.wm.open_mainfile(filepath=str(root/'Alabama_Player_Clean.blend'))
rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
assert len(rigs)==1 and len(rigs[0].data.bones)==75
assert {o.name for o in meshes}=={'Player_Body','Player_Head','Player_Helmet'}
rig=rigs[0];body=bpy.data.objects['Player_Body']
assert all(any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)for o in meshes)
def points():
    bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();evaluated=body.evaluated_get(deps);m=evaluated.to_mesh()
    p=np.array([v.co[:]for v in m.vertices]);evaluated.to_mesh_clear();return p
before=points();bone=rig.pose.bones['relbow'];bone.rotation_mode='XYZ';bone.rotation_euler.z=.45
after=points();delta=np.linalg.norm(after-before,axis=1)
assert delta.max()>.02 and np.count_nonzero(delta>1e-5)>10
bone.rotation_euler.z=0
report={'mesh_objects':[o.name for o in meshes],'source_bones':75,
 'body_vertices':len(before),'elbow_pose_moved_vertices':int(np.count_nonzero(delta>1e-5)),
 'maximum_pose_displacement':float(delta.max()),'packed_images':sum(bool(im.packed_file)for im in bpy.data.images),
 'unpacked_file_images':[im.name for im in bpy.data.images if im.source=='FILE' and not im.packed_file],
 'limits':'Source partial-cluster deformation remains experimental; this checks editable Blender skin mechanics'}
assert not report['unpacked_file_images']
(root/'player_validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))

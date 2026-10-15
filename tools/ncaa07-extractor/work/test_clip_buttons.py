"""blender -b --python work/test_clip_buttons.py -- <animation group glb>"""
import sys
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender_addon'))
import ncaa07_bridge as nb

glb = sys.argv[sys.argv.index('--') + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
nb.register()
rig = nb.import_player(glb, 'Group')
names = rig.get('ncaa_actions') or []
print('CLIPS', len(names), names[:3])
bpy.context.view_layer.objects.active = rig
r = bpy.ops.ncaa07.all_clips()
track = rig.animation_data.nla_tracks.get('NCAA07 all clips')
print('ALL', r, len(track.strips) if track else 0, bpy.context.scene.frame_start, bpy.context.scene.frame_end)
bpy.context.scene.frame_set(500)
print('POSE at 500', [round(v, 3) for v in rig.pose.bones['rknee'].matrix.to_euler()])

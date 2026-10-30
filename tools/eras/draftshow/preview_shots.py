"""Offline preview of the draft show's camera shots and actor placement (no game run).

    python -I tools/eras/draftshow/preview_shots.py [out.png]

Draws the exported draft_menu room (war-room desk, monitors and static figures hidden), the
trophy-set podium and the commissioner (posed by a retail clip, CPU skinned by the feasibility
research's posed.py) from the same eye / target / focal values the game uses
(src/nfl2k5_eras_draftshow.c k_shot[], k_place). Retail pixels: the output stays in logs/.
"""
import math, os, sys
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'eras', 'research', 'draftfx', 'scripts'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'eras', 'research', 'office', 'scripts'))
import numpy as np
from PIL import Image, ImageDraw
import render as R
import posed as P

ROOM = os.path.join(ROOT, 'build', 'eras', 'office', 'draft_menu_o23c5', 'draft_menu.gltf')
SET = os.path.join(ROOT, 'build', 'eras', 'draftfx', 'cutscene_o2725c0', 'cutscene.gltf')
MAN = os.path.join(ROOT, 'build', 'eras', 'draftfx', 'commish01_o2725c7', 'commish01.gltf')
HIDE = {'table_scissor', 'monitor_left_group', 'monitor_center_group', 'monitor_right_group', 'LM_screen_group',
        'CM_screen_group', 'RM_screen_group', 'PDA_group', 'PDA_shadow', 'LM_shadow', 'CM_shadow', 'RM_shadow',
        'GM_group', 'vip01', 'vip02'}
SET_SHOW = {'podium', 'Microphone'}

# keep in step with src/nfl2k5_eras_draftshow.c
SCALE = 6.0                      # scene units per centimetre (draft_menu)
STAGE_Y = 474.0                  # stage floor top
PODIUM = (-1750.0, -4900.0)      # podium x, z (stage left, near the front edge)
COMMISH = (-1750.0, -5130.0)     # behind the podium, facing the audience (+z)
PLAYER_END = (-1180.0, -5130.0)  # where the player stops beside the commissioner
SHOTS = {                        # eye, target, focal mm
    'wide': ((0.0, 1500.0, 2600.0), (0.0, 1250.0, -5600.0), 35.0),
    'board': ((0.0, 2000.0, -2300.0), (0.0, 2050.0, -6074.0), 35.0),
    'podium': ((-1750.0, 1520.0, -3300.0), (-1750.0, 1400.0, -5130.0), 45.0),
    'two': ((-1250.0, 1450.0, -2500.0), (-1450.0, 1250.0, -5130.0), 35.0),
    'push': ((-1250.0, 1320.0, -3350.0), (-1250.0, 1290.0, -5130.0), 42.0),
}


def fov(focal):
    return math.degrees(2 * math.atan(30.0 / (2 * focal)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'logs', 'draftshow', 'offline_shots.png')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rm, rt = R.load_gltf(ROOM, {})
    sm, st = R.load_gltf(SET, {})
    mats = list(rm)
    tris = [t for t in rt if t[5] not in HIDE]
    base = len(mats)
    mats += sm
    for t, pos, uv, col, mi, name in st:
        if name not in SET_SHOW:
            continue
        p = pos * SCALE + np.array([PODIUM[0], STAGE_Y, PODIUM[1]])
        tris.append((t, p, uv, col, mi + base, name))
    cm, cprims, ctables = P.load_skinned(MAN)
    pose, _clip = P.clip_pose('archive:2514/3', 'player', 1.0)
    base = len(mats)
    mats += cm
    for xz, yaw in ((COMMISH, 0.0), (PLAYER_END, -35.0)):
        for t, pos, uv, col, mi, name in P.posed_tris2(cm, cprims, ctables, pose,
                                                       xform=(SCALE, yaw, (xz[0], STAGE_Y + 107.0 * SCALE, xz[1]))):
            tris.append((t, pos, uv, col, mi + base, name))
    tiles = []
    for name, (eye, tgt, focal) in SHOTS.items():
        im = R.render(mats, tris, eye, tgt, W=480, H=270, fov_deg=fov(focal), light=1.5)
        ImageDraw.Draw(im).text((4, 4), '%s  eye %s  focal %.0f' % (name, eye, focal), fill=(255, 255, 0))
        tiles.append(im)
    sheet = Image.new('RGB', (960, 270 * ((len(tiles) + 1) // 2)))
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % 2) * 480, (i // 2) * 270))
    sheet.save(out)
    print(out)


if __name__ == '__main__':
    main()

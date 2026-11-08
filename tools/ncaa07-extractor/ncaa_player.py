"""Complete NCAA 07 player export: one skeleton, every part placed, every option kept.

Player TMDLs store each vertex in the space of a skinning cluster (a bone, or a
bone blended with its parent), so a plain mesh export piles helmets, facemasks,
hair and faces on top of each other at the origin. This exporter places every
part with the source skeleton (ncaa_rig_analysis), puts the face model on the
same armature, and tags every mesh with the Swap group/option it belongs to:

  ncaa_category  Body / Head / Helmet / Hair / Decal / Shadow
  ncaa_group     Swap group (Head, FaceMask, LeftHand ...) or "Base"
  ncaa_option    option within the group (HelmetStd, 2Bar, Frame3 ...)
  ncaa_selected  1 for the configured player, 0 for alternatives

The Blender side (ncaa_blender_scene.py) shows the configured player joined
into a few clean meshes and files every alternative into a hidden "Variants"
collection, so nothing has to be moved out of the way.

Textures are not stored in player models; the game binds them at runtime from
PLYDATA gear textures, a team's UNIFORM bank and PLADATA player art. Every
material is bound here by name (MATERIAL_TEXTURES), per option where the
texture depends on it (hair styles).
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from ncaa_player_options import read_options
from ncaa_rig_analysis import load, bone_matrices, partial_matrices, cluster_points
from ncaa_rig import GLB

DEFAULT_CHOICES = {
    'Standard': 'Default', 'Head': 'HelmetStd', 'HelmGroove': 'HelmetStd', 'EnvMap': 'HelmetStd',
    'Face': 'Default', 'Numbers': 'DoubleTorsoShou', 'LeftHand': 'Default', 'RightHand': 'Default',
    'LeftElbow': 'Default', 'RightElbow': 'Default', 'Visor': None, 'Turf': None,
    'FaceMask': '2Bar', 'FaceMaskLow': '2Bar', 'NeckRollPart': None, 'FaceMaskTabs': 'Default',
    'HelmetInner': 'HelmetInnerStd', 'DecalSleeve': 'Regular', 'LeftShoe': 'Default',
    'RightShoe': 'Default', 'ChinStrap': 'Default', 'HelmDecal': 'Default', 'Name': 'Default',
    'PrideStickers': None, 'HelmSideNum': 'Double', 'LeftCollarDecal': 'Default',
    'RightCollarDeca': 'Default', 'LeftSideHipDeca': 'Default', 'RightSideHipDec': 'Default',
    'HelmBackNumSing': None, 'HelmBackNumDoub': 'Left1Right1', 'Mouthpiece': None,
    'Cleats': 'Default', 'HelmetLogo': None,   # the logo is already in the HELMET atlas
    'LeftWrist': 'Bare', 'RightWrist': 'Bare',
    'LeftShoeDecal': 'Nike', 'RightShoeDecal': 'Nike', 'HelmetPadPie': 'HelmetStd',
    'Shadow': None,
    # face model (fm*)
    'EyeL': 'EyeL', 'EyeR': 'EyeR', 'HighLODFace': 'Face', 'Chinstrap': None, 'Eyelash': 'Eyelash',
}
# Groups whose choice follows the helmet (Head) choice.
FOLLOW_HELMET = ('HelmGroove', 'EnvMap', 'HelmetPadPie')
HELMET_INNER = {'HelmetStd': 'HelmetInnerStd', 'HelmetAdams': 'HelmetInnerAdam',
                'HelmetSchutt': 'HelmetInnerSchu', 'HelmetRevo': 'HelmetInnerRevo'}
HAIR_TEXTURES = {'CornRow': 'CORNROW', 'Bald': 'BALD', 'Afro': 'AFRO', 'Straight': 'STRAIGHT',
                 'FlatTop': 'FLATTOP', 'BigEars': 'BUZZ', 'Mullet': 'MULLET',
                 'Dreadlocks': 'DREADS', 'ColdHood': 'COLDHOOD'}
# material -> (bank, texture, alpha); banks: gear (PLYDATA), uniform (UNIFORM), art (PLADATA)
MATERIAL_TEXTURES = {
    'torsoSleeve': ('uniform', 'TORSOSLEEVE', False), 'hipThigh': ('uniform', 'HIPTHIGH', False),
    'helmet_outer': ('uniform', 'HELMET', False), 'helmet_outer_jawpiece': ('uniform', 'HELMET', False),
    'facemask': ('uniform', 'FMASK', False), 'shin_Lt': ('uniform', 'SOCKS', False),
    'shin_Rt': ('uniform', 'SOCKS', False), 'name': ('uniform', 'NAME', True),
    'tN_Left': ('uniform', 'NUM1', True), 'tN_Rt': ('uniform', 'NUM2', True), 'tN_Mid': ('uniform', 'NUM0', True),
    'shN_Left': ('uniform', 'NUM1', True), 'shN_Rt': ('uniform', 'NUM2', True), 'shN_Mid': ('uniform', 'NUM0', True),
    'sN_Left': ('uniform', 'NUM1', True), 'sN_Rt': ('uniform', 'NUM2', True), 'sN_Mid': ('uniform', 'NUM0', True),
    'helmet_numb_lt': ('uniform', 'NUM1', True), 'helmet_numb_rt': ('uniform', 'NUM2', True),
    'helmet_numb_center': ('uniform', 'NUM0', True),
    'helmet_groove_overlay': ('gear', 'STANDARD', True),
    'decal_two': ('uniform', 'DECAL', True), 'decal_collarbone_Lt': ('uniform', 'DECAL1', True),
    'decal_collarbone_Rt': ('uniform', 'DECAL1', True), 'decal_hips_Lt': ('uniform', 'DECAL1', True),
    'decal_hips_Rt': ('uniform', 'DECAL1', True), 'decal_sleeves': ('uniform', 'DECAL1', True),
    # default.xbe material table (0x5F25D0): the shoe decal is drawn from the helmet atlas
    'decal_shoe': ('uniform', 'HELMET', True),
    'pride_stickers': ('uniform', 'PRIDE', True), 'pride_stickers_alpha': ('gear', 'PRIDEALPHA', True),
    'helmetInner': ('gear', 'HelmetInner', False), 'chinstrap': ('gear', 'CHINSTRAP', False),
    'tab': ('gear', 'TABS', False), 'visor': ('gear', 'VISOR', True), 'armpad_Rt': ('gear', 'ARMPAD', False),
    'armpad_Left': ('gear', 'ARMPAD', False), 'elbowpad_lower_Left': ('gear', 'ELBOWPAD', False),
    'elbowpad_lower_Rt': ('gear', 'ELBOWPAD', False), 'hand_Left': ('gear', 'GLOVE', False),
    'hand_Rt': ('gear', 'GLOVE', False), 'ankleShoe_Rt': ('gear', 'SHOE', False),
    'ankleShoe_Left': ('gear', 'SHOE', False), 'cleats_ankleShoe_Rt': ('gear', 'SHOE', False),
    'cleats_ankleShoe_Left': ('gear', 'SHOE', False), 'turf': ('gear', 'TURF', False),
    'binky': ('gear', 'BINKY', False), 'faceShade': ('gear', 'FACESHADE', True),
    'wrist_Rt': ('gear', 'SpecArm2', False), 'wrist_Left': ('gear', 'SpecArm2', False),
    'elbow_lower_Left': ('gear', 'SpecArm2', False), 'elbow_lower_Rt': ('gear', 'SpecArm2', False),
    'elbow_upper_Left': ('gear', 'SpecArm2', False), 'elbow_upper_Rt': ('gear', 'SpecArm2', False),
    'face': ('art', 'YOUNGFACEWHITE', False), 'eye_Left': ('art', 'EYE', False),
    'eye_Right': ('art', 'EYE', False), 'facemasklow': ('uniform', 'FMASKL', True),
    # 32-bone old-era bodies (h*, 40s/50s, 60s, 60s/70s): jersey, pants and
    # helmet are laid out for the full uniform textures; only the shoe+sock
    # piece uses its family bank's small atlas (PLYDATA 0C-0F, team palette)
    # (default.xbe material table 0x5F3080): their own uniform entries in
    # UNIFORM.DAT, the 76 historic-team banks 0x221-0x2E0 (HELMET, TORSO,
    # SHOULDER, HIP, SHOE, NUM0-9); head -> BALD from player art
    'head': ('art', 'BALD', False), 'helmet': ('historic', 'HELMET', False),
    'torso': ('historic', 'TORSO', False), 'shoulder': ('historic', 'SHOULDER', False),
    'hip': ('historic', 'HIP', False), 'shoe': ('historic', 'SHOE', False), 'arm': ('gear', 'SpecArm2', False),
    # the coach (PLYDATA 22)
    'suit_jacket': ('coach', 'JACKET', False), 'suit_pants': ('coach', 'PANTS', False),
    'suit_shirt': ('coach', 'SHIRTTIE', False),
}
# low-detail player family -> its texture bank in PLYDATA (archive order)
LOW_BANKS = (('60s_70s', '0000000f.3ds'), ('60s', '0000000e.3ds'), ('40s_50s', '0000000d.3ds'), ('h', '0000000c.3ds'))
SKIN_TINT = [.58, .32, .20, 1]
UNTEXTURED_COLORS = {'lashes': [.05, .04, .03, 1]}     # no texture in any bank
DECAL_LIFT = 0.003          # source units are metres (a player is ~2.0 tall)
SKIN_DETAIL = {'wrist_Rt', 'wrist_Left', 'elbow_lower_Left', 'elbow_lower_Rt', 'elbow_upper_Left', 'elbow_upper_Rt'}


def quaternion(m) -> np.ndarray:
    """glTF quaternion (x, y, z, w) of a rotation matrix."""
    m = np.asarray(m, dtype=float)
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def add_animations(glb, bones, animations, order='ZYX'):
    """glTF animations from decoded clips. Each clip: {'name', 'frames':
    [(header, values)], 'rig': [30 channel names], 'fps'}. A channel triple is
    the bone's local rotation in the skeleton's own convention (raw i16,
    65536 = turn, Rz@Ry@Rx); the frame header moves the root."""
    import math
    from ncaa_rig_math import euler
    from ncaa_anim import root_motion
    index = {b['name']: b['index'] for b in bones}
    for anim in animations:
        frames, rig, fps = anim['frames'], anim['rig'], anim.get('fps', 30.0)
        if not frames:
            continue
        times = glb.accessor([[i / fps] for i in range(len(frames))], 1, bounds=True)
        samplers, channels = [], []
        for slot, name in enumerate(rig):
            # channels 88/89 (leye y/z) are left unscaled by the game's MMX loop
            if name not in index or 3 * slot + 2 >= 88:
                continue
            quats = []
            for header, values in frames:
                raw = [int(v) for v in values[3 * slot:3 * slot + 3]]
                quats.append(quaternion(np.array(euler(raw, order, math.tau / 65536))))
            for i in range(1, len(quats)):                       # shortest path between keys
                if np.dot(quats[i], quats[i - 1]) < 0:
                    quats[i] = -quats[i]
            out = glb.accessor(quats, 4)
            samplers.append({'input': times, 'output': out, 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': index[name], 'path': 'rotation'}})
        root = index.get('IDEAL_OUT')
        motion = [root_motion(h) for h, _ in frames]
        if anim.get('mirror') and all(motion):            # the codec mirrors channels, not the root
            motion = [(-m[0], m[1], m[2], -m[3]) for m in motion]
        if root is not None and all(motion):
            out = glb.accessor([[m[0], m[1], m[2]] for m in motion], 3)
            samplers.append({'input': times, 'output': out, 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': root, 'path': 'translation'}})
            yaw = [quaternion(np.array(euler([0, int(m[3] * 65536), 0], order, math.tau / 65536))) for m in motion]
            for i in range(1, len(yaw)):
                if np.dot(yaw[i], yaw[i - 1]) < 0:
                    yaw[i] = -yaw[i]
            samplers.append({'input': times, 'output': glb.accessor(yaw, 4), 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': root, 'path': 'rotation'}})
        glb.doc.setdefault('animations', []).append({'name': anim['name'], 'samplers': samplers,
                                                     'channels': channels})


def category(material: str, group: str) -> str:
    m = material.lower()
    if group == 'Shadow' or m == 'shadow':
        return 'Shadow'
    if m == 'helmet_groove_overlay':
        return 'Decal'                  # seam line and ear hole drawn over the shell (gear STANDARD)
    if m == 'lambert1':
        return 'Effect'                 # runtime shader pass; no source texture
    if m == 'hair' or group == 'NeckRollPart' and m == 'hair':
        return 'Hair'
    if m in ('face', 'eye_left', 'eye_right', 'lashes', 'faceshade', 'binky'):
        return 'Head'
    if any(k in m for k in ('helmet', 'facemask', 'chinstrap', 'visor', 'pride')) or m == 'tab':
        return 'Helmet'
    if m.startswith(('tn_', 'shn_', 'sn_', 'decal', 'name')):
        return 'Decal'
    return 'Body'


def resolve_choices(groups: dict, choices: dict | None) -> dict:
    """The configured option of every group (None: no option shown)."""
    wanted = dict(DEFAULT_CHOICES)
    wanted.update(choices or {})
    head = wanted.get('Head')
    if head in HELMET_INNER:
        for g in FOLLOW_HELMET:
            wanted[g] = head
        wanted['HelmetInner'] = HELMET_INNER[head]
    elif head:                                         # a hair style: no helmet parts at all
        for g in FOLLOW_HELMET + ('HelmetInner', 'FaceMask', 'FaceMaskLow', 'FaceMaskTabs', 'ChinStrap',
                                  'HelmDecal', 'HelmSideNum', 'HelmBackNumSing', 'HelmBackNumDoub',
                                  'HelmetLogo', 'Visor', 'PrideStickers'):
            wanted[g] = None
    result = {}
    for group, options in groups.items():
        choice = wanted.get(group, next(iter(options), None))
        if choice is not None and choice not in options:
            choice = next(iter(options), None)
        result[group] = choice
    return result


def memberships(groups: dict) -> dict:
    """node -> {group: [options listing it]}. Parts are often shared: the helmet
    shell is listed by all four helmet options of Head and of EnvMap."""
    result = {}
    for group, options in groups.items():
        for option, nodes in options.items():
            for node in nodes:
                result.setdefault(node, {}).setdefault(group, []).append(option)
    return result


def node_tags(groups: dict, chosen: dict) -> dict:
    """node -> (group, option, selected, memberships). Shown when, for every
    group listing it, that group's chosen option is one of those listing it."""
    tags = {}
    for node, member in memberships(groups).items():
        selected = all(chosen.get(g) in opts for g, opts in member.items())
        group = next(iter(member))
        option = chosen.get(group) if chosen.get(group) in member[group] else member[group][0]
        tags[node] = (group, option, selected, member)
    return tags


class Textures:
    """PNG for a (material, option) from the runtime texture banks."""

    def __init__(self, gear=None, uniform=None, art=None, skin_face='YOUNGFACEWHITE', coach=None, historic=None):
        from ncaa_assemble import TextureBank
        self.banks = {}
        self.historic_numbers = False
        for key, path in (('gear', gear), ('uniform', uniform), ('art', art), ('coach', coach),
                          ('historic', historic)):
            if path and Path(path).is_file():
                self.banks[key] = TextureBank(path)
        self.skin_face = skin_face
        self.cache = {}
        self.number = 12                         # jersey number drawn by the NUM placeholders
        self.missing = set()
        self.low_path = None

    def use_model(self, model_name: str, folder: Path):
        """Low-detail players take textures from their family's bank."""
        from ncaa_assemble import TextureBank
        stem = model_name.lower()
        historic = any(k in stem for k in ('40s', '60s')) or stem.startswith('h')
        if historic != self.historic_numbers:      # numbers come from the historic entry
            self.historic_numbers = historic
            self.cache = {k: v for k, v in self.cache.items() if not k[1].startswith('NUM')}
        path = None
        for key, file in LOW_BANKS:
            if key in stem if key != 'h' else stem.startswith('h'):
                path = Path(folder) / file
                break
        if path != self.low_path:
            self.low_path = path
            self.banks.pop('low', None)
            self.cache = {k: v for k, v in self.cache.items() if k[0] != 'low'}
            if path and path.is_file():
                self.banks['low'] = TextureBank(path)

    # stadium/sideline bodies (llstadiumobj): their 32x32 textures carry no
    # palette of their own; the game colours them with the team's UNIFORM
    # PALETTE set (12 palettes). Each one takes the palette whose colours best
    # match the full-size uniform texture of the same piece.
    LOW_REFERENCE = {'TORSO0': ('uniform', 'TORSOSLEEVE'), 'THIGH0': ('uniform', 'HIPTHIGH'),
                     'HELMET0': ('uniform', 'HELMET'), 'SHOE0': ('gear', 'SHOE')}

    def _team_palettes(self):
        from ncaa_textures import checked
        uni = self.banks.get('uniform')
        if uni is None or 'PALETTE' not in uni.names:
            return []
        f = uni.meta['files'][uni.names['PALETTE']]
        return [checked(uni.data, p['data_offset'], p['size'])
                for p in uni.meta['palettes'][f['palette_index']:f['palette_index'] + f['palette_count']]]

    def _low_png(self, bank, name):
        import io
        from PIL import Image
        palettes = self._team_palettes()
        ref = self.LOW_REFERENCE.get(name)
        target = None
        if ref and ref[0] in self.banks and ref[1] in self.banks[ref[0]].names:
            try:
                im = Image.open(io.BytesIO(self.banks[ref[0]].png(ref[1]))).convert('RGB').resize((16, 16))
                target = np.asarray(im, dtype=float).reshape(-1, 3).mean(0)
            except Exception:
                target = None
        best, best_d = None, None
        for pal in palettes:
            try:
                png = bank.png(name, pal)
            except Exception:
                continue
            im = Image.open(io.BytesIO(png)).convert('RGB').resize((16, 16))
            colour = np.asarray(im, dtype=float).reshape(-1, 3).mean(0)
            d = float(np.sum((colour - target) ** 2)) if target is not None else -float(colour.std())
            if best is None or d < best_d:
                best, best_d = png, d
        return best

    def lookup(self, material: str, option: str | None):
        if material == 'hair':
            name = HAIR_TEXTURES.get(option or '')
            bank = 'gear' if name == 'COLDHOOD' else 'art'
            entry = (bank, name, True) if name else None
        elif material == 'face':
            entry = ('art', self.skin_face, False)
        else:
            entry = MATERIAL_TEXTURES.get(material)
        if entry and entry[1] and entry[1].startswith('NUM') and self.number is not None:
            # NUM0/1/2 are placeholders the game fills per player: left = tens,
            # right = ones, the centred (single-digit) part = the only digit
            n = int(self.number) % 100
            digit = n % 10 if material.endswith(('_Mid', '_center')) else (
                n // 10 if material.endswith(('_Left', '_lt')) else n % 10)
            entry = (entry[0], f'NUM{digit}', entry[2])
        if entry and self.historic_numbers and entry[0] == 'uniform' and entry[1].startswith('NUM'):
            entry = ('historic',) + entry[1:]
        return entry

    def png(self, material: str, option: str | None):
        entry = self.lookup(material, option)
        if not entry or not entry[1]:
            return None
        bank, name, alpha = entry
        key = (bank, name)
        if key in self.cache:
            return self.cache[key]
        result = None
        b = self.banks.get(bank)
        if bank == 'low' and b is not None and name in b.names:
            result = self._low_png(b, name)
            self.cache[key] = result
            if result is None:
                self.missing.add(f'{bank}:{name}')
            return result
        if b is not None and name in b.names:
            try:
                result = b.png(name)
            except ValueError:
                # palette-free texture: the bank's own PALETTE entry, else skin
                palettes = []
                if 'PALETTE' in b.names:
                    palettes.append((b, 'PALETTE'))
                art = self.banks.get('art')
                if art is not None and self.skin_face in art.names:
                    palettes.append((art, self.skin_face))
                for pb, pname in palettes:
                    try:
                        result = b.png(name, pb.palette(pname))
                        break
                    except Exception:
                        result = None
        if result is None:
            self.missing.add(f'{bank}:{name}')
        self.cache[key] = result
        return result

    def binding(self, material: str, option: str | None):
        png = self.png(material, option)
        if png is None:
            return None
        entry = self.lookup(material, option)
        return {'png': png, 'alpha': bool(entry[2]), 'texture': entry[1],
                'tint': SKIN_TINT if material in SKIN_DETAIL else None}


def _materials(path: Path):
    from ncaa_formats import decode_model
    from ncaa_textures import material_references
    data = path.read_bytes()
    model = decode_model(data, str(path))
    names = {}
    if 'Matl' in model['sections']:
        for m in material_references(data, model['sections']['Matl'], {'files': []}):
            names[m['material_index']] = m['name']
    return {mesh['node_index']: names.get(mesh['material'], f"material_{mesh['material']}")
            for mesh in model['meshes']}, model


def export_player(body: Path, output: Path, face: Path | None = None, choices: dict | None = None,
                  textures: Textures | None = None, include_variants: bool = True,
                  order: str = 'ZYX', mode: str = 'game', animations: list | None = None,
                  number: int | None = None) -> dict:
    """One GLB: the source skeleton, the configured player and (optionally)
    every alternative part, tagged for the Blender organizer."""
    body = Path(body)
    if number is not None:
        if textures:
            textures.number = number
        if int(number) < 10:            # single-digit parts instead of the two-digit ones
            choices = dict(choices or {})
            for g, value in (('Numbers', DEFAULT_CHOICES['Numbers'].replace('Double', 'Single')),
                             ('HelmSideNum', 'Single'), ('HelmBackNumSing', 'Center'), ('HelmBackNumDoub', None)):
                choices.setdefault(g, value)
    sources = [body] + ([Path(face)] if face else [])
    first = load(body)
    bones = first['skeleton']['bones']
    worlds = bone_matrices(first, order)
    glb = GLB()
    glb.doc['materials'] = []
    material_index = {}
    report = {'output': str(output), 'model': first['skeleton']['model_name'], 'parts': 0, 'variant_parts': 0,
              'vertices': 0, 'triangles': 0, 'choices': {}, 'bones': len(bones)}

    # skeleton nodes 0..n-1 as translation/rotation/scale (animatable)
    for bone in bones:
        parent = bone['parent']
        local = worlds[bone['index']] if parent is None else np.linalg.inv(worlds[parent]) @ worlds[bone['index']]
        scale = np.linalg.norm(local[:3, :3], axis=0)
        node = {'name': bone['name'], 'translation': local[:3, 3].tolist(),
                'rotation': quaternion(local[:3, :3] / scale).tolist(), 'scale': scale.tolist(),
                'extras': {'sourceBoneIndex': bone['index']}}
        kids = [b['index'] for b in bones if b['parent'] == bone['index']]
        if kids:
            node['children'] = kids
        glb.doc['nodes'].append(node)
    inverse = np.linalg.inv(worlds)
    ibm = glb.accessor(inverse.transpose(0, 2, 1).reshape(-1, 16), 16)
    glb.doc['skins'].append({'name': 'NCAA07_Skeleton', 'inverseBindMatrices': ibm, 'skeleton': 0,
                             'joints': list(range(len(bones)))})
    root_children = [0]

    for source in sources:
        model = first if source == body else load(source)
        sbones = model['skeleton']['bones']
        if [b['name'] for b in sbones] != [b['name'] for b in bones]:
            raise ValueError(f'{source.name}: skeleton differs from {body.name}')
        if textures:
            textures.use_model(model['skeleton']['model_name'], source.parent)
        groups = read_options(source.read_bytes())
        chosen = resolve_choices(groups, choices)
        report['choices'].update({g: c for g, c in chosen.items() if c is not None})
        tags = node_tags(groups, chosen)
        materials, _ = _materials(source)
        frames = partial_matrices(model, order, mode)
        positions = cluster_points(model, frames)
        rows = model['rows']
        joints = np.zeros((len(rows), 4), dtype=np.uint16)
        weights = np.zeros((len(rows), 4))
        for i, row in enumerate(rows):
            bone = int(row[3]); parent = sbones[bone]['parent']; w = row[4] / 4096
            joints[i, 0], weights[i, 0] = bone, w
            if parent is None:
                weights[i, 0] = 1
            else:
                joints[i, 1], weights[i, 1] = parent, 1 - w
        normal_frames = np.linalg.inv(frames[:, :3, :3]).transpose(0, 2, 1)
        normals = np.einsum('nij,nj->ni', normal_frames[rows[:, 2].astype(int)], rows[:, 8:11])
        normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-30)
        from ncaa_formats import decode_model
        parts = {m['node_index']: m for m in decode_model(source.read_bytes(), str(source))['meshes']}

        for node in np.unique(rows[:, 0]).astype(int):
            node = int(node)
            group, option, selected, member = tags.get(node, ('Base', '', True, {}))
            material = materials.get(node, 'material')
            cat = category(material, group)
            if cat == 'Decal' and textures and textures.binding(material, option) is None:
                cat = 'Effect'          # e.g. the name plate: its texture is made at runtime from the roster
            if cat in ('Shadow', 'Effect'):
                selected = False
            if not selected and not include_variants:
                continue
            indices = np.flatnonzero(rows[:, 0] == node)
            lookup = np.full(len(rows), -1)
            lookup[indices] = np.arange(len(indices))
            faces = model['faces'][np.isin(model['faces'][:, 0], indices)]
            if not len(faces):
                continue
            key = (material, option if material == 'hair' else '')
            if key not in material_index:
                material_index[key] = len(glb.doc['materials'])
                mat = {'name': material + (f'_{option}' if key[1] else ''), 'doubleSided': True,
                       'pbrMetallicRoughness': {'metallicFactor': 0, 'roughnessFactor': .8}}
                bind = textures.binding(material, option) if textures else None
                if bind:
                    ti = glb.image(bind['png'], bind['texture'])
                    if bind['alpha']:
                        # decals and numbers are clamped in the game; repeating them
                        # tiles one logo across the whole chest or helmet
                        glb.doc.setdefault('samplers', [])
                        if not glb.doc['samplers']:
                            glb.doc['samplers'].append({'wrapS': 33071, 'wrapT': 33071})
                        glb.doc['textures'][ti]['sampler'] = 0
                    mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': ti}
                    if bind['tint']:
                        mat['pbrMetallicRoughness']['baseColorFactor'] = bind['tint']
                    if bind['alpha']:
                        mat['alphaMode'] = 'MASK'; mat['alphaCutoff'] = .35
                    mat['extras'] = {'sourceTexture': bind['texture']}
                elif material in UNTEXTURED_COLORS:
                    mat['pbrMetallicRoughness']['baseColorFactor'] = UNTEXTURED_COLORS[material]
                glb.doc['materials'].append(mat)
            placed = positions[indices]
            if cat == 'Decal':
                # numbers/decals lie exactly on the jersey (the game draws them as a
                # later pass); lift them 3 mm along their normals so they don't
                # z-fight with the jersey in Blender or any other renderer
                placed = placed + normals[indices] * DECAL_LIFT
            attributes = {'POSITION': glb.accessor(placed, 3, target=34962, bounds=True),
                          'NORMAL': glb.accessor(normals[indices], 3, target=34962),
                          'JOINTS_0': glb.accessor(joints[indices], 4, '<u2', 34962),
                          'WEIGHTS_0': glb.accessor(weights[indices], 4, target=34962)}
            part = parts.get(node, {})
            if 'uv' in part:
                attributes['TEXCOORD_0'] = glb.accessor([part['uv'][int(rows[i, 1])] for i in indices], 2,
                                                       target=34962)
            index_acc = glb.accessor(lookup[faces].reshape(-1), 1, '<u4', 34963)
            label = (f'{group}_{option}_' if group != 'Base' else '') + material
            mesh = len(glb.doc['meshes'])
            glb.doc['meshes'].append({'name': label, 'primitives': [{
                'attributes': attributes, 'indices': index_acc, 'material': material_index[key], 'mode': 4}]})
            ni = len(glb.doc['nodes'])
            glb.doc['nodes'].append({'name': label, 'mesh': mesh, 'skin': 0, 'extras': {
                'ncaa_category': cat, 'ncaa_group': group, 'ncaa_option': option,
                'ncaa_selected': int(bool(selected)), 'ncaa_material': material,
                'ncaa_options': json.dumps(member),
                'ncaa_source': source.name, 'ncaa_node': node}})
            root_children.append(ni)
            report['parts'] += 1
            report['variant_parts'] += 0 if selected else 1
            report['vertices'] += len(indices)
            report['triangles'] += len(faces)

    root = len(glb.doc['nodes'])
    glb.doc['nodes'].append({'name': report['model'].replace('.ea3', ''), 'children': root_children,
                             'extras': {'ncaa_kind': 'player', 'ncaa_choices': json.dumps(report['choices'])}})
    glb.doc['scenes'][0]['nodes'] = [root]
    glb.doc['extras'] = {'ncaa_kind': 'player', 'sources': [s.name for s in sources],
                         'choices': report['choices'],
                         'coordinates': 'player Y-up (glTF native)',
                         'skinning': 'game cluster palette (default.xbe 0x2C3E0) at bind; partial clusters as parent/child linear weights'}
    if textures and textures.missing:
        report['missing_textures'] = sorted(textures.missing)
    if animations:
        add_animations(glb, bones, animations, order)
        report['animations'] = [a['name'] for a in animations]
    glb.write(Path(output))
    return report


def is_player(path: Path) -> bool:
    try:
        data = Path(path).read_bytes()
        from ncaa_formats import sections
        _, table = sections(data)
        return all(k in table for k in ('Skel', 'Clst', 'Geom'))
    except Exception:
        return False


def default_textures(scratch: Path, players: Path, uniform_index: int = 9, historic_index: int = 0x2A0) -> Textures:
    scratch = Path(scratch)
    return Textures(gear=Path(players) / '00000001.3ds',
                    uniform=scratch / 'uniform_raw' / f'{uniform_index:08x}.3ds',
                    art=scratch / 'player_art_raw' / '00000000.3ds',
                    coach=Path(players) / '00000022.3ds',
                    historic=scratch / 'uniform_raw' / f'{historic_index:08x}.3ds')


def face_for(body: Path) -> Path | None:
    """The face model that goes with a 75-bone body (fm* in the same folder)."""
    folder = Path(body).parent
    try:
        bones = load(Path(body))['skeleton']['bones']
    except Exception:
        return None
    if len(bones) != 75:
        return None
    from ncaa_formats import sections
    for tmd in sorted(folder.glob('*.tmd')):
        if tmd == Path(body):
            continue
        try:
            r, table = sections(tmd.read_bytes())
            if 'Info' in table and r.string(table['Info']['offset']).startswith('fm'):
                return tmd
        except Exception:
            continue
    return None


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('body', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--face', type=Path)
    p.add_argument('--scratch', type=Path, default=Path(r'H:\NCAA07_Extractor_Work'))
    p.add_argument('--uniform', type=int, default=9)
    p.add_argument('--choice', action='append', default=[], help='Group=Option')
    p.add_argument('--no-variants', action='store_true')
    a = p.parse_args()
    tex = default_textures(a.scratch, a.body.parent, a.uniform)
    face = a.face or face_for(a.body)
    choices = dict(c.split('=', 1) for c in a.choice)
    print(json.dumps(export_player(a.body, a.output, face, choices, tex, not a.no_variants), indent=2))

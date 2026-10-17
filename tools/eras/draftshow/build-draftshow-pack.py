"""My NFL Eras draft show: the stage, the podium set, the commissioner, the suited player and the
baked animation poses, all built from the player's own disc (docs/eras/results/M11-draftshow.md).

    python -I tools/eras/draftshow/build-draftshow-pack.py           write the files
    python -I tools/eras/draftshow/build-draftshow-pack.py --check   list what is there

Writes (gitignored: retail data, never committed):

  mods/eras/office/files/erasdraft.iff
        SCNE draft_menu   byte copy of Franchise's Draft room (fr.iff, archive entry 23, chunk 5)
        SCNE cutscene     the Super Bowl MVP trophy set (outer 2725 chunk 0): wooden podium,
                          microphone stands, folding chairs; its STAGE platform quad is reshaped
                          (same topology: 4 vertices) into a unit quad facing +z with 0..1 texture
                          coordinates: the jersey prop the runtime places between the player's hands
        SCNE commish01    byte copy of the commissioner (outer 2725 chunk 7, skinned, 25 joints
                          on the player skeleton)
        SCNE draftply1    the commissioner re-dressed as the drafted player: the same mesh and
                          skin, renamed, the coat and trousers palettes turned charcoal, the grey
                          hair darkened (palette-only edits: same size, same layout)
        SCNE dhd01..dhdNN the player's heads (M12): byte copies of sideline head-coach packages
                          (outers 348-383, SCNE coach, renamed 'coach' -> 'dhdNN' in every name:
                          dhdNNBodyGrp1 / dhdNNLodGrp1 / dhdNNHeadGrp1), a set spanning the skin
                          tones; the runtime hides the body and LOD nodes and draws the 29-joint
                          coachHeadGrp1 attached to the suited player's thorax / neck / head joints.
        The office's extra-pack hook already serves mods\\eras\\office\\files\\*.iff from pack G,
        so the game opens erasdraft.iff by name under the draft show's own context (ERASDRAFT).

  mods/eras/draft/files/draftposes.bin
        retail SMCD clips sampled with the 2K Football Mod Tools sampler (player channel map,
        completed twist joints) at their own 15 Hz frames: per frame 25 quaternions (w, x, y, z)
        in the commish01 joint order, then the root trajectory lanes 0..2 in centimetres.
        Layout: "DSPOSE01", u32 clip count; per clip: char name[32], u32 frames, f32 fps,
        u32 reserved, then frames * (25 * 4 + 3) floats.

  mods/eras/draft/files/draftheads.csv
        the heads in erasdraft.iff: scene name, coach outer, skin tone (median face colour r,g,b
        of the head texture) and its group (light / medium / dark), for the hand and neck tint.

  mods/eras/draft/files/draftjerseys.bin
        one jersey per first-round pick: the drafting team's 1980 home torso (kit file
        <code>h<style>.iff, the back half) with the pick number in the kit's own jersey digits,
        quantised to the platform texture's format (P8 128x128, one level, swizzled) so the
        runtime can write it over that texture. Layout: "DSJRSY01", u32 count; per pick: u32 pick,
        char team[24], u8 pixels[16384], u8 palette[1024].

Needs the 2K Football Mod Tools checkout in .tmp-2k-football-mod-tools (scene decoding, the VC-LZ
re-encoder and the animation sampler), and build/eras/draftfx/inventory.json (the archive
inventory made by the feasibility research; regenerate with nfl_resource_scan.py).
"""
import colorsys, dataclasses, hashlib, importlib.util, os, struct, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
MODTOOLS = os.path.join(ROOT, '.tmp-2k-football-mod-tools')
INVENTORY = os.path.join(ROOT, 'build', 'eras', 'draftfx', 'inventory.json')
INDEX = os.path.join(ROOT, 'original', 'disc', 'vc_53450030', '0')
PACK = os.path.join(ROOT, 'mods', 'eras', 'office', 'files', 'erasdraft.iff')
POSES = os.path.join(ROOT, 'mods', 'eras', 'draft', 'files', 'draftposes.bin')
JERSEYS = os.path.join(ROOT, 'mods', 'eras', 'draft', 'files', 'draftjerseys.bin')
PICKS = os.path.join(ROOT, 'mods', 'eras', 'draft', 'picks_1981.csv')
HEADS_CSV = os.path.join(ROOT, 'mods', 'eras', 'draft', 'files', 'draftheads.csv')
# Head-coach packages whose heads the suited player wears (M12): (outer, tone group). Picked off a
# sheet of the 36 head textures (build/eras/draftshow/heads/sheet.png) to span the skin tones.
HEADS = (
    (348, 'dark'), (353, 'dark'), (359, 'dark'), (367, 'dark'), (382, 'dark'),
    (355, 'medium'), (362, 'medium'), (376, 'medium'),
    (349, 'light'), (357, 'light'), (371, 'light'), (377, 'light'),
)
TEAMS_CSV = os.path.join(ROOT, 'data', 'eras', 'runtime', '1980', 'teams.csv')
# 1980 team -> kit asset code (docs/eras/research/ERA-LOGOS.md section 3)
KIT_CODE = {
    'colts': '11', 'bills': '03', 'dolphins': '14', 'patriots': '16', 'jets': '19', 'bengals': '06', 'browns': '30',
    'oilers_titans': '28', 'steelers': '22', 'broncos': '08', 'chiefs': '13', 'raiders': '20', 'chargers': '24',
    'seahawks': '26', 'cowboys': '07', 'giants': '18', 'eagles': '21', 'cardinals': '00', 'washington': '29',
    'bears': '05', 'lions': '09', 'packers': '10', 'vikings': '15', 'buccaneers': '27', 'falcons': '01',
    'rams': '23', 'saints': '17', '49ers': '25',
}

# (name in the pack, clip identity) -- docs/eras/research/DRAFT-SHOW-FEASIBILITY.md 4.2
CLIPS = (
    ('idle', 'archive:2514/3'),      # anthemstandplayer: standing, small weight shifts (7.0 s)
    ('loop', 'archive:2530/1'),      # cointossloopplayer01: standing at the coin toss (6.48 s)
    ('walk', 'archive:2761/1'),      # fpcoinrunhomeplayer01: captains walking out (6.85 s)
    ('shake', 'archive:2763/7'),     # fphandshakeplayer: handshake (3.98 s)
    ('shake2', 'archive:2531/1'),    # cointossshakeplayer01: coin-toss handshake (6.85 s)
    ('raise', 'archive:2723/2'),     # ex021player02: helmet raised overhead with both hands (4.42 s)
    ('trophy', 'archive:2725/3'),    # ex023player01: MVP trophy presentation (9.03 s)
)


def load_office_module():
    p = os.path.join(ROOT, 'tools', 'eras', 'build-office-pack.py')
    spec = importlib.util.spec_from_file_location('build_office_pack', p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def modtools():
    if not os.path.isdir(MODTOOLS):
        sys.exit('the 2K Football Mod Tools are not in %s' % MODTOOLS)
    sys.path.insert(0, MODTOOLS)
    sys.path.insert(0, os.path.join(MODTOOLS, 'tools'))
    from mod_editor.core import nfl2k5_models as M
    from mod_editor.core import nfl2k5_animation as A
    from mod_editor.core import nfl2k5_scne_builder as B
    return M, A, B


def pad16(b):
    return b + b'\0' * (-len(b) & 15)


# ---- the suited player: palette edits on a copy of commish01 ---------------------------------

def recolour(pal, rule):
    """pal: 1024 bytes BGRA; returns the edited palette."""
    out = bytearray(pal)
    for i in range(256):
        b, g, r, a = out[4 * i:4 * i + 4]
        h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
        nr, ng, nb = rule(r, g, b, h, l, s)
        out[4 * i:4 * i + 3] = bytes((max(0, min(255, int(nb))), max(0, min(255, int(ng))), max(0, min(255, int(nr)))))
    return bytes(out)


def coat_rule(r, g, b, h, l, s):
    # the navy cloth (blue hues) -> charcoal; the shirt (white), the tie (red) and buttons stay
    if 0.50 < h < 0.75 and s > 0.15:
        v = l * 255 * 0.50
        return v * 1.02, v, v * 0.98
    return r, g, b


def pants_rule(r, g, b, h, l, s):
    # khaki trousers -> charcoal, matching the coat; the belt (dark brown) goes black
    v = l * 255 * 0.42
    return v * 1.02, v, v * 0.98


def face_rule(r, g, b, h, l, s):
    # grey hair -> dark brown; skin, eyes and teeth stay
    if s < 0.14 and 0.28 < l < 0.78:
        v = l * 255 * 0.30
        return v * 1.25, v * 1.0, v * 0.8
    return r, g, b


def make_player(M, B, src, key):
    resource = src.resource(key)
    span = src.span(resource)
    resource_, decoded, scene = src.parse(key)
    d = bytearray(decoded)
    old, new = 'commish01'.encode('utf-16-le'), 'draftply1'.encode('utf-16-le')
    n = 0
    i = d.find(old)
    while i >= 0:
        d[i:i + len(old)] = new
        n += 1
        i = d.find(old, i + 2)
    if not n:
        sys.exit('commish01: name not found')
    system = int(resource.word_08)
    video = int(resource.word_0c)
    inv = M._tools_module('nfl_scne_inventory')
    rules = {'coat': coat_rule, 'pants': pants_rule, 'face': face_rule}
    done = []
    for m in scene['materials']:
        rule = rules.get(m.get('name'))
        if not rule or m.get('texture_index') is None:
            continue
        t = scene['embedded_textures'][int(m['texture_index'])]
        info = inv.texture_info(decoded, int(t['descriptor_offset']), scene['name'], int(t['index']))
        if info.format_name != 'P8':
            continue
        p = system + info.palette_offset
        d[p:p + 1024] = recolour(bytes(d[p:p + 1024]), rule)
        done.append(m['name'])
    body = span[0x20:]
    tag = struct.unpack_from('<I', body, 4)[0]
    bits = body[8]
    chunk, info = B.compressed_chunk('SCNE', bytes(d), system, video, stream_tag=tag, offset_bits=bits)
    return chunk, n, done


# ---- the heads: renamed copies of head-coach packages ----------------------------------------

def head_skin(M, src, key, dec, scene):
    """Median face colour (r, g, b) of the package's head texture."""
    import numpy as np
    sys.path.insert(0, os.path.join(MODTOOLS, 'tools'))
    import nfl_tset_png_import as T
    import nfl_txtr as X
    inv = M._tools_module('nfl_scne_inventory')
    resource = src.resource(key)
    system, video = int(resource.word_08), int(resource.word_0c)
    mat = next(m for m in scene['materials'] if m.get('name') == 'headShader1')
    t = scene['embedded_textures'][int(mat['texture_index'])]
    info = inv.texture_info(dec, int(t['descriptor_offset']), scene['name'], int(t['index']))
    w, h = info.width, info.height
    pal = T.parse_palette(dec, system + info.palette_offset)
    idx = X.unswizzle_2d(dec[video + info.pixel_offset:video + info.pixel_offset + w * h], w, h, 1)
    rgb = np.frombuffer(T.rgba_from_indices(idx, pal), np.uint8).reshape(h, w, 4)[..., :3].astype(float)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    m = (r > g) & (g >= b * 0.9) & (r > 40)
    med = np.median(rgb[m], axis=0) if m.sum() > 100 else rgb.reshape(-1, 3).mean(0)
    return tuple(int(x) for x in med)


def make_heads(M, B, src):
    """[(scene name, chunk)], csv rows: a coach package per HEADS entry, 'coach' -> 'dhdNN'."""
    out, rows = [], []
    old = 'coach'.encode('utf-16-le')
    for k, (outer, group) in enumerate(HEADS):
        key = 'o%dc0' % outer
        name = 'dhd%02d' % (k + 1)
        resource = src.resource(key)
        span = src.span(resource)
        _r, decoded, scene = src.parse(key)
        if scene['name'] != 'coach' or len(scene['shapes']) != 3:
            sys.exit('%s: not a coach package (%s, %d shapes)' % (key, scene['name'], len(scene['shapes'])))
        d = bytearray(decoded)
        new = name.encode('utf-16-le')
        n, i = 0, d.find(old)
        while i >= 0:
            d[i:i + len(old)] = new
            n += 1
            i = d.find(old, i + 2)
        skin = head_skin(M, src, key, decoded, scene)
        system, video = int(resource.word_08), int(resource.word_0c)
        body = span[0x20:]
        tag = struct.unpack_from('<I', body, 4)[0]
        bits = body[8]
        chunk, _info = B.compressed_chunk('SCNE', bytes(d), system, video, stream_tag=tag, offset_bits=bits)
        out.append((name, chunk))
        rows.append('%s,%d,%d,%d,%d,%s' % (name, outer, skin[0], skin[1], skin[2], group))
        print('  head %s <- outer %d (%d names, %d bytes, skin %s, %s)' % (name, outer, n, len(chunk), skin, group))
    return out, rows


# ---- poses -----------------------------------------------------------------------------------

def bake_poses(A):
    src = A.AnimationSource(__import__('pathlib').Path(INDEX), __import__('pathlib').Path(INVENTORY))
    out = bytearray(b'DSPOSE01' + struct.pack('<I', len(CLIPS)))
    rows = []
    for name, ident in CLIPS:
        clip = dataclasses.replace(src.load(ident), family='player')
        r = clip.roots[0]
        fps = (r.frames - 1) / r.duration if r.duration > 0 else 15.0
        out += struct.pack('<32sIfI', name.encode(), r.frames, fps, 0)
        for f in range(r.frames):
            t = min(f / fps, r.duration * 0.9999)
            pose = A.sample_pose(clip, t, loop=False)
            if len(pose) != 25:
                sys.exit('%s: %d joints' % (ident, len(pose)))
            for q in pose:
                out += struct.pack('<4f', *q)
            traj = struct.unpack_from('<3h', clip.body, r.trajectory + r.stride * f)
            out += struct.pack('<3f', *(v * 0.125 for v in traj))
        rows.append('%-7s %-16s %-22s %3d frames %.2f s' % (name, ident, clip.name, r.frames, r.duration))
    return bytes(out), rows


# ---- the jersey prop: the set's STAGE quad reshaped, and the jersey textures ----------------

def make_set(M, B, src, key):
    """The trophy set with its STAGE platform as a unit quad (x, y in -0.5..0.5, z = 0, facing +z,
    texture 0..1). Same vertex count, triangles and layout; positions, normals, UVs and the
    shape's scale / offset / UV constant / bounding sphere change."""
    resource = src.resource(key)
    span = src.span(resource)
    _r, decoded, scene = src.parse(key)
    d = bytearray(decoded)
    shape = next(s for s in scene['shapes'] if s['name'] == 'STAGE')
    lanes = M._shape_lanes(scene, shape, decoded)
    if lanes.vertex_count != 4 or lanes.position_format != 'NORMSHORT3' or not lanes.texcoord:
        sys.exit('STAGE: unexpected layout %s' % (lanes,))
    old = M.read_positions(decoded, shape, lanes)
    rec = int(shape['record_offset'])
    struct.pack_into('<f', d, rec + 0x10, 1.0)                    # position scale
    struct.pack_into('<3f', d, rec + 0x20, 0.0, 0.0, 0.0)         # position offset
    struct.pack_into('<4f', d, rec + 0x30, 0.5, 0.5, 0.5, 0.5)    # uv = lane * 0.5 + 0.5
    struct.pack_into('<4f', d, rec + 0x00, 0.0, 0.0, 0.0, 1.0)    # bounding sphere centre
    struct.pack_into('<f', d, rec + 0x48, 0.75)                   # and radius
    base0 = lanes.stream_offsets[lanes.position_stream]
    tex = lanes.texcoord
    base1 = lanes.stream_offsets[tex[0]]
    for i, (x, y, z) in enumerate(old):
        # old x -> x, old -z -> y (the quad faced +y; now +z): the far edge is the top
        nx = 0.5 if x > 0 else -0.5
        ny = 0.5 if z < 0 else -0.5
        struct.pack_into('<3h', d, base0 + i * lanes.position_stride + lanes.position_offset,
                         M.encode_normshort(nx), M.encode_normshort(ny), 0)
        if lanes.normal:
            n = lanes.normal
            struct.pack_into('<I', d, lanes.stream_offsets[n[0]] + i * n[2] + n[1], M.encode_normpacked3(0.0, 0.0, 1.0))
        u = 1.0 if nx > 0 else -1.0                               # uv 1 / 0
        v = -1.0 if ny > 0 else 1.0                               # top of the image at the top
        struct.pack_into('<2h', d, base1 + i * tex[2] + tex[1], M.encode_normshort(u), M.encode_normshort(v))
    body = span[0x20:]
    chunk, _info = B.compressed_chunk('SCNE', bytes(d), int(resource.word_08), int(resource.word_0c),
                                      stream_tag=struct.unpack_from('<I', body, 4)[0], offset_bits=body[8])
    return chunk


def kit_file(office, code, style):
    for name in ('%sh%d.iff' % (code, style), '%sh0.iff' % code):
        try:
            return name, office.archive_file(name)
        except SystemExit:
            continue
    return None, None


def kit_torso_and_digits(office, data):
    """(torso RGBA 512x256, {digit: RGBA 64x64}) from a Unif kit file."""
    import re
    import numpy as np
    sys.path.insert(0, os.path.join(MODTOOLS, 'tools'))
    import nfl_tset_png_import as T
    import nfl_txtr as X
    o = data.find(b'TSET')
    h = struct.unpack_from('<4s7I', data, o)
    dec = office.vclz(data[o + 0x20:o + 0x20 + h[1]])
    clean, _mud = T.decode_tset_levels(dec)
    torso = np.frombuffer(bytes(clean[0].rgba), np.uint8).reshape(clean[0].height, clean[0].width, 4)
    digits = {}
    for m in re.finditer(rb'TXTR', data):
        o = m.start()
        h = struct.unpack_from('<4s7I', data, o)
        if h[4] != 0xFEEDBEEF or h[3] != 6464:
            continue
        dec = office.vclz(data[o + 0x20:o + 0x20 + h[1]])
        sysb, video = dec[:h[2]], dec[h[2]:]
        name = sysb[0x20:0x28].decode('utf-16-le', 'ignore').split('\0')[0]
        if len(name) != 2 or not name.isdigit():
            continue
        idx = X.unswizzle_2d(video[:4096], 64, 64, 1)
        pal = T.parse_palette(video, 5440)
        digits[chr(int(name))] = np.frombuffer(T.rgba_from_indices(idx, pal), np.uint8).reshape(64, 64, 4)
    return torso, digits


def compose_jersey(torso, digits, number):
    """The back of the jersey (right half of the torso atlas) with the number, 128x128 RGBA."""
    import numpy as np
    from PIL import Image
    back = Image.fromarray(torso[:, 256:512].copy(), 'RGBA')
    back.putalpha(255)
    text = str(number)
    if all(c in digits for c in text):
        w = 92 if len(text) == 1 else 74
        glyphs = [Image.fromarray(digits[c], 'RGBA').resize((w, 120), Image.LANCZOS) for c in text]
        total = sum(g.width for g in glyphs) - 8 * (len(glyphs) - 1)
        x = (256 - total) // 2
        for g in glyphs:
            back.alpha_composite(g, (x, 82))
            x += g.width - 8
    return np.asarray(back.resize((128, 128), Image.LANCZOS).convert('RGBA'))


def make_jerseys(office):
    import csv
    sys.path.insert(0, os.path.join(MODTOOLS, 'tools'))
    import nfl_tset_png_import as T
    import nfl_txtr as X
    styles = {}
    with open(TEAMS_CSV, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            try:
                styles[row['id']] = int(row.get('style') or 0)
            except ValueError:
                styles[row['id']] = 0
    picks = []
    with open(PICKS, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            if row['round'] == '1':
                picks.append((int(row['pick']), row['team']))
    out = bytearray(b'DSJRSY01' + struct.pack('<I', len(picks)))
    cache, rows = {}, []
    for pick, team in picks:
        code = KIT_CODE.get(team)
        if team not in cache:
            name, data = kit_file(office, code, styles.get(team, 0)) if code else (None, None)
            cache[team] = (name, kit_torso_and_digits(office, data) if data else None)
        name, kit = cache[team]
        if kit:
            rgba = compose_jersey(kit[0], kit[1], pick)
        else:
            import numpy as np
            rgba = np.full((128, 128, 4), 200, np.uint8)
        levels = [T.MipLevel(0, 128, 128, rgba.tobytes())]
        palette, linear, _q = T.quantize_levels(levels, 256)
        pixels = X.swizzle_2d(linear[0], 128, 128, 1)
        out += struct.pack('<I24s', pick, team.encode()) + pixels + T.palette_bytes(palette)
        rows.append('%2d %-14s %s' % (pick, team, name or 'NO KIT'))
        if pick in (1, 2):
            from PIL import Image
            os.makedirs(os.path.join(ROOT, 'logs', 'draftshow'), exist_ok=True)
            Image.fromarray(rgba, 'RGBA').resize((256, 256)).save(os.path.join(ROOT, 'logs', 'draftshow', 'jersey_pick%d.png' % pick))
    return bytes(out), rows


def make_audio_copies():
    """PCM copies of the user's draft clips (mods/eras/draft/audio -> draft/files/audio, same names, .wav):
    the game plays them with MCI, whose MPEG path refused the chime's file (it opens the WAVs).
    Needs ffmpeg on PATH; without it the game falls back to the MP3s."""
    import shutil, subprocess
    src = os.path.join(ROOT, 'mods', 'eras', 'draft', 'audio')
    dst = os.path.join(ROOT, 'mods', 'eras', 'draft', 'files', 'audio')
    ff = shutil.which('ffmpeg')
    if not ff or not os.path.isdir(src):
        print('audio copies: skipped (%s)' % ('no ffmpeg' if not ff else 'no audio folder'))
        return
    n = 0
    for base, _dirs, files in os.walk(src):
        for f in files:
            if not f.lower().endswith('.mp3'):
                continue
            out = os.path.join(dst, os.path.relpath(base, src), f[:-4] + '.wav')
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(os.path.join(base, f)):
                continue
            subprocess.run([ff, '-loglevel', 'error', '-y', '-i', os.path.join(base, f), '-ar', '44100', '-ac', '2',
                            '-c:a', 'pcm_s16le', out], check=True)
            n += 1
    print('audio copies: %d converted into %s' % (n, dst))


def main():
    office = load_office_module()
    M, A, B = modtools()
    if '--check' in sys.argv:
        for p in (PACK, POSES, JERSEYS):
            if not os.path.exists(p):
                print('%s: MISSING' % p)
                continue
            data = open(p, 'rb').read()
            print('%s: %d bytes, sha1 %s' % (p, len(data), hashlib.sha1(data).hexdigest()[:12]))
            if p == PACK:
                for o, tag, rec in office.records(data):
                    print('  %08X %s %8d %s' % (o, tag, len(rec) - 0x20, office.record_name(tag, rec)))
        return
    src = M.ModelSource(__import__('pathlib').Path(INDEX), __import__('pathlib').Path(INVENTORY))
    fr = office.archive_file('fr.iff')
    room = None
    for o, tag, rec in office.records(fr):
        if tag == 'SCNE' and office.record_name(tag, rec) == 'draft_menu':
            room = rec
            break
    if room is None:
        sys.exit('fr.iff: no SCNE draft_menu')
    podium = make_set(M, B, src, 'o2725c0')
    commish = src.span(src.resource('o2725c7'))
    player, renamed, recoloured = make_player(M, B, src, 'o2725c7')
    recs = [('draft_menu', room), ('cutscene', podium), ('commish01', commish), ('draftply1', player)]
    heads, head_rows = make_heads(M, B, src)
    recs += heads
    for want, rec in recs:
        got = office.record_name('SCNE', rec)
        if got != want:
            sys.exit('record %s reads back as %r' % (want, got))
    os.makedirs(os.path.dirname(PACK), exist_ok=True)
    with open(PACK, 'wb') as f:
        f.write(b''.join(pad16(r) for _, r in recs))
    print('wrote %s: %s (player: %d names, palettes %s)' % (PACK, ', '.join('%s %d' % (n, len(r)) for n, r in recs),
                                                           renamed, ', '.join(recoloured)))
    os.makedirs(os.path.dirname(HEADS_CSV), exist_ok=True)
    with open(HEADS_CSV, 'w') as f:
        f.write('name,outer,r,g,b,group\n' + ''.join(r + '\n' for r in head_rows))
    print('wrote %s (%d heads)' % (HEADS_CSV, len(head_rows)))
    poses, rows = bake_poses(A)
    os.makedirs(os.path.dirname(POSES), exist_ok=True)
    with open(POSES, 'wb') as f:
        f.write(poses)
    print('wrote %s (%d bytes)' % (POSES, len(poses)))
    for r in rows:
        print('  ' + r)
    make_audio_copies()
    jerseys, rows = make_jerseys(office)
    with open(JERSEYS, 'wb') as f:
        f.write(jerseys)
    print('wrote %s (%d bytes)' % (JERSEYS, len(jerseys)))
    for r in rows:
        print('  ' + r)


if __name__ == '__main__':
    main()

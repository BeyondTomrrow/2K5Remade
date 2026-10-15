"""Complete stadiums: structure + the stadium's own field + props, one GLB.

Everything here follows the game's own data, read from default.xbe:

* league.dat holds the TEAM and STAD tables (EA DB, '!' run-length packed).
* A stadium file's archive slot (entry // 12 in STADIUMS.DAT) is STAD.SRES.
* 0x401760 runs  select TGID, TMFL, TL35, TEZ1, TEZ2 from TEAM where SGID = stadium
  and loads FLDDATA entries  midfield 0x4c+TMFL, end zones 0x445+TEZ1/TEZ2,
  35-yard logos 0x281+TL35 (255 = none).
* 0x401ae0 picks the turf from STAD.SFTY (fair weather, warm day): a colour
  layer (fieldmednoise) and a grain layer (fieldnoise).
* STAD.SGPT picks the goal post colour (0 yellow, 1 white).
Field units are yards, Z up, the field runs along Y (end lines at +-60).
"""
from __future__ import annotations

import io
import json
import math
import re
import struct
from pathlib import Path

# SFTY -> (fieldnoise entry, fieldmednoise entry) for a dry day >= 65F (0x401ae0)
TURF = {0: (0x00, 0x0d), 1: (0x03, 0x10), 2: (0x09, 0x16), 3: (0x0a, 0x18),
        4: (0x0b, 0x0d), 5: (0x0c, 0x1a)}
TURF_NAMES = {0: 'grass', 1: 'grass', 2: 'artificial turf', 3: 'blue turf',
              4: 'grass', 5: 'dark grass'}
GOALPOST = {0: 0x2e, 1: 0x31}             # HiResGOALPOST_yellow / _white
SKY = {'FAIR': 0x1a, 'NIGHT': 0x1d, 'SNOW': 0x1e}
PYLON, BENCH, WATER, GATORADE = 0x35, 0x36, 0x39, 0x38
SIDELINE = 160 / 3 / 2                     # 53 1/3 yards wide: 26.67


# ------------------------------------------------------------------ league.dat

def _unrle(b: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(b):
        if b[i] == 0x21:                   # '!' value count
            out += bytes([b[i + 1]]) * b[i + 2]
            i += 3
        else:
            out.append(b[i])
            i += 1
    return bytes(out)


def _db_tables(data: bytes, wanted=('TEAM', 'STAD')) -> dict:
    """Minimal little-endian EA DB reader (see tools/team-creator/eadb.py)."""
    base = data.find(b'DB\x00\x08')
    ntables = struct.unpack_from('<I', data, base + 0x10)[0]
    lst = base + 0x18
    start = lst + ntables * 8
    out = {}
    for t in range(ntables):
        name = data[lst + t * 8:lst + t * 8 + 4].decode('latin-1')
        if name not in wanted:
            continue
        h = start + struct.unpack_from('<I', data, lst + t * 8 + 4)[0]
        rec_bytes = struct.unpack_from('<I', data, h + 8)[0]
        capacity, count = struct.unpack_from('<HH', data, h + 20)
        fields = []
        for k in range(data[h + 28]):
            ftype, foff = struct.unpack_from('<II', data, h + 40 + k * 16)
            fname = data[h + 48 + k * 16:h + 52 + k * 16].decode('latin-1')
            fbits = struct.unpack_from('<I', data, h + 52 + k * 16)[0]
            fields.append((fname, ftype, foff, fbits))
        rec0 = h + 40 + len(fields) * 16
        rows = []
        for r in range(count):
            rec = data[rec0 + r * rec_bytes:rec0 + (r + 1) * rec_bytes]
            n = int.from_bytes(rec, 'little')
            row = {}
            for fname, ftype, foff, fbits in fields:
                if ftype == 0:
                    raw = rec[foff // 8:(foff + fbits) // 8]
                    row[fname] = raw.split(b'\0', 1)[0].decode('latin-1')
                else:
                    row[fname] = (n >> foff) & ((1 << fbits) - 1)
            rows.append(row)
        out[name] = rows
    return out


_LEAGUE = {}


def league(game) -> dict:
    """TEAM and STAD rows from <game>/data/league.dat (cached)."""
    game = Path(game)
    path = next((p for p in (game / 'data' / 'league.dat', game / 'league.dat') if p.is_file()), None)
    if path is None:
        raise FileNotFoundError(f'league.dat not found under {game}')
    if path not in _LEAGUE:
        d = path.read_bytes()
        i = 0
        while True:
            i = d.find(b'DATA', i)
            if i < 0:
                raise ValueError('no database in league.dat')
            size = int.from_bytes(d[i + 4:i + 8], 'little')
            raw = _unrle(d[i + 8:i + 8 + size])
            if raw[:4] == b'DB\x00\x08':
                _LEAGUE[path] = _db_tables(raw)
                break
            i += 4
    return _LEAGUE[path]


def stadium_slot(source: Path) -> int | None:
    """STADIUMS.DAT entry -> stadium slot (STAD.SRES); None for the create-a-stadium kit."""
    try:
        entry = int(Path(source).stem, 16)
    except ValueError:
        return None
    return entry // 12 if entry < 0xb28 else None


def stadium_info(game, slot) -> dict:
    """What the game loads for a stadium slot: field, logos, goal posts."""
    info = {'slot': slot, 'sfty': 0, 'sgpt': 0, 'midfield': None, 'endzones': [], 'logo35': None}
    if slot is None:
        return info
    tables = league(game)
    stad = next((s for s in tables['STAD'] if s['SRES'] == slot), None)
    if not stad:
        return info
    info.update(stadium=re.sub(r'[\x80-\xff]', '', stad['SNAM']).strip(), city=stad['SCIT'], sgid=stad['SGID'],
                sfty=stad['SFTY'], sgpt=stad['SGPT'])
    teams = [t for t in tables['TEAM'] if t['SGID'] == stad['SGID']]
    teams.sort(key=lambda t: (t['TTYP'] != 0, t['TGID']))   # the school, not a legend/custom copy
    if teams:
        t = teams[0]
        info.update(team=f"{t['TDNA']} {t['TMNA']}", tgid=t['TGID'],
                    midfield=0x4c + t['TMFL'],
                    endzones=[0x445 + t['TEZ1'], 0x445 + t['TEZ2']],
                    logo35=None if t['TL35'] == 255 else 0x281 + t['TL35'])
    return info


# ------------------------------------------------------------------ parts

def _texture_png(tmd: Path):
    from ncaa_formats import sections
    from ncaa_textures import parse_mmap, decode_texture
    from PIL import Image
    b = tmd.read_bytes()
    _, t = sections(b)
    s = t['Text']
    raw = b[s['offset']:s['offset'] + s['size']]
    return Image.open(io.BytesIO(decode_texture(raw, parse_mmap(raw), 0))).convert('RGB')


def turf_png(field_raw: Path, sfty: int, size=1024) -> bytes:
    """Colour layer stretched over the field with the grain layer tiled 4x
    (its UVs run -2..2), grain applied around its mean so colour is kept."""
    from PIL import Image, ImageChops, ImageStat
    noise, colour = TURF.get(sfty, TURF[0])
    base = _texture_png(field_raw / f'{colour:08x}.tmd').resize((size, size), Image.BICUBIC)
    grain = _texture_png(field_raw / f'{noise:08x}.tmd').convert('L')
    tile = Image.new('L', (size, size))
    step = size // 4
    g = grain.resize((step, step), Image.BICUBIC)
    for x in range(4):
        for y in range(4):
            tile.paste(g, (x * step, y * step))
    mean = ImageStat.Stat(tile).mean[0] or 1
    # out = base * (1 + 0.6 * (grain/mean - 1))
    lut = [max(0, min(255, int(round(128 * (1 + 0.6 * (v / mean - 1)))))) for v in range(256)]
    mult = tile.point(lut).convert('RGB')
    out = ImageChops.multiply(base, mult)
    out = Image.eval(out, lambda v: min(255, v * 2))
    buf = io.BytesIO()
    out.save(buf, 'PNG')
    return buf.getvalue()


def _only_on_field(model, limit=70):
    """35-yard logo files park unused copies hundreds of yards away."""
    for mesh in model['meshes']:
        P = mesh['positions']
        mesh['faces'] = [f for f in mesh['faces'] if all(abs(P[v][0]) < limit and abs(P[v][1]) < limit for v in f)]
    model['meshes'] = [m for m in model['meshes'] if m['faces']]
    return model


def _matrix(tx=0., ty=0., tz=0., rz=0.):
    """Z-up placement (yards) as a glTF column-major matrix in the Y-up frame
    the exporter's root node converts to: R(-90x) . M . R(+90x)."""
    c, s = math.cos(rz), math.sin(rz)
    # Z-up: rotate about Z, translate. Conjugated: Z-up z -> glTF y, y -> -z.
    # glTF rotation about +Y by rz, translation (tx, tz, -ty)
    return [c, 0, -s, 0,
            0, 1, 0, 0,
            s, 0, c, 0,
            tx, tz, -ty, 1]


def props_layout():
    """Prop placements. Goal posts sit on the end lines (+-60) as in the game;
    pylons are at the four corners of each end zone (college rules). Bench and
    table spots are a standard sideline layout, not read from the game."""
    out = []
    for y, rz in ((-60, 0.0), (60, math.pi)):
        out.append(('goalpost', None, y, rz))
    for x in (-SIDELINE, SIDELINE):
        for y in (-60, -50, 50, 60):
            out.append(('pylon', x, y, 0.0))
    for side, rz in ((-1, -math.pi / 2), (1, math.pi / 2)):
        x = side * 35.0                    # inside the team-area box (+-39.2)
        for y in (-10, -3.3, 3.3, 10):
            out.append(('bench', x, y, rz))
        out.append(('water', x, 15, rz))
        out.append(('gatorade', x, -15, rz))
    return out


# ------------------------------------------------------------------ merge

def _read_glb(path: Path):
    data = path.read_bytes()
    jlen = struct.unpack_from('<I', data, 12)[0]
    doc = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from('<I', data, off)[0]
    return doc, data[off + 8:off + 8 + blen]


def merge_glbs(parts, output: Path, extras=None):
    """parts: [(glb path, group name, glTF matrix or None)]. Each part's scene
    sits under a node named after its group; groups become top nodes."""
    out = {'asset': {'version': '2.0', 'generator': 'NCAA07 stadium assembler'},
           'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'buffers': [],
           'bufferViews': [], 'accessors': [], 'materials': [], 'images': [], 'textures': [], 'samplers': []}
    binary = bytearray()
    groups = {}
    for path, group, matrix, name in parts:
        doc, blob = _read_glb(Path(path))
        while len(binary) % 4:
            binary.append(0)
        base = len(binary)
        binary.extend(blob)
        o = {k: len(out[k]) for k in ('nodes', 'meshes', 'bufferViews', 'accessors', 'materials',
                                       'images', 'textures', 'samplers')}
        for v in doc.get('bufferViews', []):
            v = dict(v, buffer=0, byteOffset=v.get('byteOffset', 0) + base)
            out['bufferViews'].append(v)
        for a in doc.get('accessors', []):
            out['accessors'].append(dict(a, bufferView=a['bufferView'] + o['bufferViews']))
        for im in doc.get('images', []):
            out['images'].append(dict(im, bufferView=im['bufferView'] + o['bufferViews']))
        out['samplers'] += doc.get('samplers', [])
        for t in doc.get('textures', []):
            t = dict(t, source=t['source'] + o['images'])
            if 'sampler' in t:
                t['sampler'] += o['samplers']
            out['textures'].append(t)
        for m in doc.get('materials', []):
            m = json.loads(json.dumps(m))
            pbr = m.get('pbrMetallicRoughness', {})
            if 'baseColorTexture' in pbr:
                pbr['baseColorTexture']['index'] += o['textures']
            out['materials'].append(m)
        for me in doc.get('meshes', []):
            me = json.loads(json.dumps(me))
            for p in me['primitives']:
                p['attributes'] = {k: v + o['accessors'] for k, v in p['attributes'].items()}
                p['indices'] += o['accessors']
                if 'material' in p:
                    p['material'] += o['materials']
            out['meshes'].append(me)
        for n in doc.get('nodes', []):
            n = dict(n)
            if 'mesh' in n:
                n['mesh'] += o['meshes']
            if 'children' in n:
                n['children'] = [c + o['nodes'] for c in n['children']]
            out['nodes'].append(n)
        roots = [r + o['nodes'] for r in doc['scenes'][doc.get('scene', 0)]['nodes']]
        holder = {'name': name, 'children': roots}
        if matrix:
            holder['matrix'] = matrix
        out['nodes'].append(holder)
        held = len(out['nodes']) - 1
        if group not in groups:
            out['nodes'].append({'name': group, 'children': []})
            groups[group] = len(out['nodes']) - 1
            out['scenes'][0]['nodes'].append(groups[group])
        out['nodes'][groups[group]]['children'].append(held)
    for k in ('images', 'textures', 'samplers'):
        if not out[k]:
            del out[k]
    out['buffers'] = [{'byteLength': len(binary)}]
    if extras:
        out['extras'] = extras
    js = json.dumps(out, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    with Path(output).open('wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(binary)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
        f.write(struct.pack('<II', len(binary), 0x004E4942) + bytes(binary))
    return output


# ------------------------------------------------------------------ build

def build_stadium(source, game, scratch, output: Path, sky=False, log=print) -> dict:
    """One GLB: Stadium (structure), Field (turf, lines, numbers, midfield,
    35-yard logos, both end zones), Props (goal posts, pylons, benches,
    tables) and, optionally, Sky (the dome the game draws behind it)."""
    import tempfile
    from ncaa_formats import decode_model, export_glb
    source, scratch, output = Path(source), Path(scratch), Path(output)
    field_raw, stadata = scratch / 'field_raw', scratch / 'stadata_raw'
    model = decode_model(source.read_bytes(), str(source))
    code, _, weather = model['name'].replace('.ea3', '').partition('_')
    info = stadium_info(game, stadium_slot(source)) if game else {'sfty': 0, 'sgpt': 0, 'midfield': None,
                                                                  'endzones': [], 'logo35': None}
    work = Path(tempfile.mkdtemp(prefix='stadium_', dir=scratch))
    parts = []

    def part(model_or_path, group, name, matrix=None, prepare=None):
        m = model_or_path
        if isinstance(m, Path):
            m = decode_model(m.read_bytes(), str(m))
        if prepare:
            m = prepare(m)
        if not m['meshes']:
            return
        dest = work / f'{len(parts):02d}_{name}.glb'
        export_glb(m, dest)
        parts.append((dest, group, matrix, name))

    part(model, 'Stadium', 'Structure')
    # turf: the colour layer's 128-vertex grid with the composed texture
    noise, colour = TURF.get(info['sfty'], TURF[0])
    turf = decode_model((field_raw / f'{colour:08x}.tmd').read_bytes(), str(field_raw / f'{colour:08x}.tmd'))
    turf['_override_png'] = turf_png(field_raw, info['sfty'])
    part(turf, 'Field', 'Turf')
    part(field_raw / '0000004a.tmd', 'Field', 'Lines', _matrix(tz=.006))
    part(field_raw / '00000037.tmd', 'Field', 'Numbers', _matrix(tz=.010))
    if info['midfield'] is not None:
        part(field_raw / f"{info['midfield']:08x}.tmd", 'Field', 'Midfield_logo', _matrix(tz=.012))
    if info['logo35'] is not None:
        part(field_raw / f"{info['logo35']:08x}.tmd", 'Field', 'Field_logos', _matrix(tz=.012), _only_on_field)
    for k, ez in enumerate(info['endzones']):
        part(field_raw / f'{ez:08x}.tmd', 'Field', f'Endzone_{k + 1}', _matrix(tz=.012, rz=math.pi * k))
    models = {'goalpost': stadata / f"{GOALPOST.get(info['sgpt'], 0x2e):08x}.tmd",
              'pylon': stadata / f'{PYLON:08x}.tmd', 'bench': stadata / f'{BENCH:08x}.tmd',
              'water': stadata / f'{WATER:08x}.tmd', 'gatorade': stadata / f'{GATORADE:08x}.tmd'}
    cache = {}
    for kind, x, y, rz in props_layout():
        if not models[kind].is_file():
            continue
        if kind not in cache:
            dest = work / f'prop_{kind}.glb'
            export_glb(decode_model(models[kind].read_bytes(), str(models[kind])), dest)
            cache[kind] = dest
        parts.append((cache[kind], 'Props', _matrix(tx=x or 0., ty=y, rz=rz), kind.capitalize()))
    if sky:
        sky_entry = stadata / f"{SKY.get(weather, 0x1a):08x}.tmd"
        if sky_entry.is_file():
            part(sky_entry, 'Sky', 'Sky')
    extras = {'stadium': info.get('stadium', code), 'team': info.get('team'), 'weather': weather,
              'turf': TURF_NAMES.get(info['sfty'], 'grass'),
              'goalposts': 'white' if info['sgpt'] == 1 else 'yellow',
              'field_entries': {'midfield': info['midfield'], 'endzones': info['endzones'], 'logo35': info['logo35']},
              'units': 'yards, Z up, field along Y'}
    merge_glbs(parts, output, extras)
    log(f"{extras['stadium']}: {extras['turf']}, {len(info['endzones'])} end zones, "
        f"{'team field' if info['midfield'] is not None else 'plain field'}")
    return {'file': str(output), 'model': model['name'], 'parts': len(parts), 'kind': 'stadium', **extras}

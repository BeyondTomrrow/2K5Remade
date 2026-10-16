"""Los Angeles Memorial Coliseum for NFL 2K5, from NCAA Football 07's USC stadium.

    python tools/eras/stadiums/coliseum_build.py [--code s60] [--base s24] [--out DIR] [--only dd]

Writes the nine stadium bundles ``<code><t><w>.iff`` (t: d day, a afternoon, n night; w: d dry, r rain, s snow)
to ``data/eras/stadiums/coliseum/build/`` plus ``build_report.json``. Each bundle is the retail bundle of the base
venue (default s24, QUALCOMM STADIUM, the stand-in the Eras table used) with its ``stadium`` scene rebuilt:

* kept from retail, because the engine reads them: every material (the executable looks some up by name:
  ``crowd``, ``jumbo_tron``, ``digit_*``, ``banner_*``), every marker (sideline people, cameras), the sideline
  props (``sideline_*``), pylons (``pyG*``), the field-level banners (``banners_*``) and the sideline yard markers;
  the field, detail layer, cityscape, intro cameras and the loose TXTR chunks of the bundle stay byte for byte;
* removed: the Qualcomm bowl (``groupNN`` shapes), its scoreboards and the Chargers wall pads (``explode_ps_*``);
* added: the NCAA 07 Coliseum (STADIUMS.DAT 0x600 FAIR for day/afternoon, 0x604 NIGHT for night), converted to
  static SCNE shapes in spatial cells, each NCAA texture as a P8 texture with its own material; the NCAA crowd
  cards use the game's animated ``crowd`` material (same atlas convention, U and V swapped), the two peristyle
  video boards use ``jumbo_tron`` (the live feed).

Coordinates: NCAA yards, Z up, field along +Y  ->  2K5 centimetres, Y up, field along Z:
X = -91.44 x, Y = 91.44 z, Z = 91.44 y (a rotation, so faces keep their winding and text reads the right way;
the press box ends up on -x, the home sideline, as in every retail stadium; the peristyle at +z, the south end).

EXPERIMENTAL. Only the user's own discs are read; nothing is written to the disc image or the game folder.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
for p in (HERE, ROOT / "tools" / "team-creator" / "vendor", ROOT / "tools" / "team-creator"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

import nfl_txtr as T  # noqa: E402
import scne as S  # noqa: E402
import p8  # noqa: E402
import ncaa_source as N  # noqa: E402

DISC = ROOT / "original" / "disc"
OUT = ROOT / "data" / "eras" / "stadiums" / "coliseum" / "build"
YD = 91.44

VARIANTS = [t + w for t in "dan" for w in "drs"]

# NCAA parts that are not stadium geometry (field light rigs, helper meshes) or that the game draws itself
SKIP_MATERIALS = {"lambert1", "USC_Shirt01_bmp_MATERIAL"}     # the Trojan jerseys on the peristyle stand
SKIP_PREFIXES = ("SB_", "CLK")                       # NCAA scoreboard digits (static textures in 2K5)
SKIP_SUBSTRINGS = ("LIGHTRIG_FIELD", "LIGHTRIG_CROWD")
JUMBO = {"JUMBOTRON"}
SELF_LIT = {"LIGHTS01_BMP_MATERIAL"}                  # the light-tower lamps: LIGHT_ materials stay bright at night
BIG_TEXTURES = {"USC_Tower01_bmp"}            # the peristyle keeps 256x256; everything else 128 at most
# USC's own marks become the game's team banners (the home team's art at runtime)
TEAM_BANNERS = {"USC_Sign01_bmp_MATERIAL", "USC_Sign02_bmp_MATERIAL", "USC_EdgeWall03_bmp_FILE1Material"}
AD_BANNERS = {"BNR_G", "BNR_S"}                       # NCAA's runtime ad placeholders -> banner_corp cells
EA_BOARD = "RAIL"                                     # the scoreboard texture with the EA SPORTS logos

# retail shapes kept in the stadium scene (the retail field-level banners are replaced by the NCAA ones)
KEEP_PREFIXES = ("sideline_", "pyG")
YARD_MATERIALS = {"yardfront", "yardside"}

# lighting per time of day / weather, on the NCAA vertex colours (retail s24: afternoon cement is 0.92/0.85/0.77
# of day; rain and snow are the mod tools' overcast multipliers)
TOD_TINT = {"d": (1.0, 1.0, 1.0), "a": (0.95, 0.88, 0.80), "n": (1.0, 1.0, 1.0)}
WEATHER_TINT = {"d": (1.0, 1.0, 1.0), "r": (0.80, 0.83, 0.88), "s": (0.90, 0.92, 0.96)}

CELL = 4000.0       # cm: the stadium is split into shapes on a 40 m grid (frustum culling per shape)

# the retail banner atlases (s24 banners_corp01 / banners_fan01): banner_corp is 2 x 4 ad cells; the team
# banners hold two banners (V halves); banner_home_player two by two (U and V halves)
FAN_BANNERS = [("banner_home_team", 1.0, 0.5), ("banner_away_team", 1.0, 0.5), ("banner_home_team", 1.0, 0.5),
               ("banner_home_player", 0.5, 0.5)]
CORP_CELLS = [(u, v) for v in (0.0, 0.25, 0.5, 0.75) for u in (0.0, 0.5)]


def ncaa_to_2k5(p):
    x, y, z = p
    return (-YD * x, YD * z, YD * y)


def short_name(texture):
    return "co_" + "".join(ch if ch.isalnum() else "_" for ch in texture).strip("_")[:24]


# --- the converted Coliseum ----------------------------------------------------------------------------------

class Tri:
    __slots__ = ("mat", "p", "uv", "c")

    def __init__(self, mat, p, uv, c):
        self.mat, self.p, self.uv, self.c = mat, p, uv, c


def _clamp01(v):
    return min(max(v, 0.0), 1.0)


def classify(part, counters):
    """(material key, kind, uv function) for an NCAA part, or None to leave it out."""
    m = part.material
    if m in SKIP_MATERIALS or part.shader == "Flat" or m.startswith(SKIP_PREFIXES) or \
            any(s in m for s in SKIP_SUBSTRINGS):
        return None
    if m.startswith("STDCROWD_CROWD_"):                 # home/away crowd and the band sections: the game's crowd
        return "crowd", "crowd", lambda u, v: (v, u)    # NCAA: v picks the strip, u runs along (2K5: the reverse)
    if m in JUMBO:
        return "jumbo_tron", "jumbo", lambda u, v: (0.625 * u, 0.875 * v)   # the retail feed rectangle
    if m in AD_BANNERS:
        cu, cv = CORP_CELLS[counters["corp"] % len(CORP_CELLS)]
        counters["corp"] += 1
        return "banner_corp", "banner", lambda u, v, cu=cu, cv=cv: (cu + 0.5 * _clamp01(u), cv + 0.25 * _clamp01(v))
    field_level = max(p[2] for p in part.positions) <= 12.0
    if m in TEAM_BANNERS or (m == "RAIL" and field_level):
        name, su, sv = FAN_BANNERS[counters["fan"] % len(FAN_BANNERS)]
        half = 0.5 * (counters["fan"] // len(FAN_BANNERS) % 2)
        counters["fan"] += 1
        return name, "banner", lambda u, v, su=su, sv=sv, half=half: (su * u, half + sv * _clamp01(v))
    if part.texture is None:
        return None
    key = ("LIGHT_" if m in SELF_LIT else "") + short_name(part.texture)
    return key, ("lit" if m in SELF_LIT else "static"), None


def convert_source(src, tint):
    """[Tri] and {material key: (texture name, kind)} for one NCAA model."""
    tris, mats, counters = [], {}, collections.Counter()
    for part in src.parts:
        c = classify(part, counters)
        if c is None:
            continue
        key, kind, uvf = c
        mats.setdefault(key, (part.texture, kind))
        P = [ncaa_to_2k5(p) for p in part.positions]
        zbias = part.texture is not None and part.texture.upper().startswith("ZBIAS")
        if zbias and part.normals:
            # NCAA ZBIAS decals lie on their walls: lift them 3 cm along the normal (mapped like the positions)
            P = [(x - 3.0 * n[0], y + 3.0 * n[2], z + 3.0 * n[1]) for (x, y, z), n in zip(P, part.normals)]
        for f in part.faces:
            uv = [part.uvs[i] for i in f]
            if uvf is not None:
                uv = [uvf(u, v) for u, v in uv]
            if kind in ("static", "lit"):                    # move the triangle's UVs next to the origin
                du = math.floor(min(u for u, _ in uv))
                dv = math.floor(min(v for _, v in uv))
                uv = [(u - du, v - dv) for u, v in uv]
            elif kind == "banner" and key != "banner_corp":  # team banners repeat along U only
                du = math.floor(min(u for u, _ in uv))
                uv = [(u - du, v) for u, v in uv]
            col = []
            for i in f:
                r, g, b, a = part.colours[i]
                if kind == "crowd":
                    r = g = b = 0.86                               # retail crowd vertices are 220 grey
                elif kind == "jumbo":
                    r, g, b = 0.8, 0.9, 1.0                        # retail jumbo_tron vertices: 204, 229, 255
                col.append(tuple(int(max(0, min(255, round(255 * ch * t)))) for ch, t in zip((r, g, b), tint)) + (255,))
            tris.append(Tri(key, [P[i] for i in f], uv, col))
    return tris, mats


def cover_ea_logos(img):
    """The NCAA scoreboard texture with its EA SPORTS panel painted over in the board's own dark face colour."""
    from PIL import Image
    a = np.asarray(img.convert("RGBA")).copy()
    h = a.shape[0]
    top = a[: int(h * 0.45), :, :3].reshape(-1, 3).astype(int)
    darkish = top[top.sum(axis=1) < 200]
    face = np.median(darkish, axis=0) if len(darkish) else np.array((20, 20, 20))
    a[int(h * 0.42):, :, :3] = face.astype(np.uint8)          # logos start at 42%
    return Image.fromarray(a, "RGBA")


# --- building the stadium scene -------------------------------------------------------------------------------

def keep_shape(sc, shape):
    if shape.name.startswith(KEEP_PREFIXES):
        return True
    mats = {sc.materials[sm.material].name for sm in shape.submeshes}
    return bool(mats) and mats <= (YARD_MATERIALS | {"nozwrite_grass06"}) and bool(mats & YARD_MATERIALS)


def build_stadium_scene(sc, tris, mats, textures, report):
    template_shape = sc.shape("group42")
    template_node = next(n for n in sc.nodes if n.shape_name == "group42")
    opaque_tmpl = sc.materials[sc.material_index("cement01")]
    cut_tmpl = sc.materials[sc.material_index("rail01")]
    lit_tmpl = sc.materials[sc.material_index("LIGHT_boxseat01")]
    tex_tmpl = sc.textures[opaque_tmpl.texture]

    removed = [s.name for s in sc.shapes if not keep_shape(sc, s)]
    for name in removed:
        sc.remove_shape(name)
    report["removed_shapes"] = len(removed)
    report["kept_shapes"] = [s.name for s in sc.shapes]

    # materials and textures
    mat_index = {}
    tex_bytes = 0
    for key, (tname, kind) in sorted(mats.items()):
        if kind in ("crowd", "jumbo", "banner"):
            mat_index[key] = sc.material_index(key)
            continue
        img = textures[tname]
        if tname == EA_BOARD:
            img = cover_ea_logos(img)
        alpha = np.asarray(img.convert("RGBA"))[..., 3]
        opaque = bool(alpha.min() >= 250)
        cap = 256 if tname in BIG_TEXTURES else 128
        rec, pixels, palette = p8.encode(img, tex_tmpl.record, cap=cap, opaque=opaque)
        sc.textures.append(S.Texture(bytearray(rec), pixels, palette))
        tex_bytes += len(pixels) + 1024
        tmpl = lit_tmpl if kind == "lit" else (opaque_tmpl if opaque else cut_tmpl)
        sc.materials.append(S.Material(bytearray(tmpl.record), key, len(sc.textures) - 1, None))
        mat_index[key] = len(sc.materials) - 1
    report["new_texture_bytes"] = tex_bytes
    report["new_materials"] = len([k for k in mats if mats[k][1] not in ("crowd", "jumbo", "banner")])

    # spatial cells -> shapes (one submesh per material)
    cells = collections.defaultdict(list)
    for t in tris:
        cx = sum(p[0] for p in t.p) / 3.0
        cz = sum(p[2] for p in t.p) / 3.0
        cells[(math.floor(cx / CELL), math.floor(cz / CELL), t.mat == "jumbo_tron")].append(t)
    n_shapes = n_verts = n_words = 0
    for (ix, iz, jumbo), group in sorted(cells.items()):
        by_mat = collections.defaultdict(list)
        for t in group:
            by_mat[t.mat].append(t)
        positions, colours, uvs, vindex = [], [], [], {}
        subs = []
        for key, mts in sorted(by_mat.items()):
            strips = []
            for t in mts:
                ids = []
                for p, uv, c in zip(t.p, t.uv, t.c):
                    k = (tuple(round(v, 2) for v in p), tuple(round(v, 5) for v in uv), c)
                    if k not in vindex:
                        vindex[k] = len(positions)
                        positions.append(p)
                        uvs.append(uv)
                        colours.append(c)
                    ids.append(vindex[k])
                strips.append(ids)
            words = S.encode_words(S.TRIANGLE_STRIP, S.strips_to_indices(strips))
            subs.append((mat_index[key], words))
            n_words += len(words) // 4
        if len(positions) >= 0xFFFF:
            raise SystemExit(f"cell {ix},{iz}: too many vertices ({len(positions)})")
        name = f"coliseum_{'jt' if jumbo else ''}{ix + 50:02d}_{iz + 50:02d}"
        shape = S.static_shape(template_shape, name, positions, colours, uvs, subs)
        sc.shapes.append(shape)
        sc.nodes.append(S.node_for(template_node, name, name))
        n_shapes += 1
        n_verts += len(positions)
    report.update(new_shapes=n_shapes, new_vertices=n_verts, new_push_words=n_words, triangles=len(tris))
    shrink_unused_textures(sc, report)
    nudge_sideline(sc, report)
    return sc


PROTECTED = ("digit_", "jumbo_tron", "crowd", "banner_", "LIGHT_")


def shrink_unused_textures(sc, report):
    """Retail textures only the removed Qualcomm bowl drew become 8x8 copies (their materials stay, by name)."""
    from PIL import Image
    used = {sm.material for s in sc.shapes for sm in s.submeshes}
    keep = set()
    for i, m in enumerate(sc.materials):
        if m.texture is not None and (i in used or m.name.startswith(PROTECTED)):
            keep.add(m.texture)
    saved = 0
    for i, t in enumerate(sc.textures):
        if i in keep or (t.width <= 8 and t.height <= 8):
            continue
        rgba = p8.decode(t.record, t.pixels, t.palette)
        img = Image.fromarray(rgba, "RGBA").resize((8, 8), Image.BOX)
        rec, pixels, palette = p8.encode(img, t.record, cap=8, opaque=False)
        saved += len(t.pixels) - len(pixels)
        sc.textures[i] = S.Texture(bytearray(rec), pixels, palette)
    report["retail_texture_bytes_saved"] = saved


# --- retail pieces fitted to the Coliseum ----------------------------------------------------------------------

WALL_X = 4600.0         # cm: the NCAA field wall is at 51 yards across (x = +-4663)
PROP_START = 3000.0


def _squeeze(x):
    ax = abs(x)
    if ax <= PROP_START:
        return x
    k = (WALL_X - PROP_START) / (5110.0 - PROP_START)     # s24's deepest prop reaches 5101
    return math.copysign(PROP_START + (ax - PROP_START) * k, x)


def nudge_sideline(sc, report):
    """The retail benches reach 51 m across; the Coliseum wall stands at 46.6 m. Props and the sideline people's
    markers beyond 30 m are squeezed toward the field so nothing stands in the wall."""
    moved = 0
    for shape in sc.shapes:
        if not shape.name.startswith("sideline_"):
            continue
        pts = [(_squeeze(x), y, z) for x, y, z in S.shape_positions(shape)]
        S.set_positions(shape, pts)
        moved += 1
    marks = 0
    for m in sc.markers:
        x, y, z = struct.unpack_from("<3f", m.record, 0x10)
        if PROP_START < abs(x) < 6000.0:
            struct.pack_into("<f", m.record, 0x10, _squeeze(x))
            marks += 1
    report["sideline"] = dict(props=moved, markers=marks)


GRASS_ELLIPSE = (19000.0, 24500.0, 1700.0)      # cm: half-width, half-length, z centre (the bowl plus a margin)


def green_cityscape(sc, report):
    """Exposition Park: the base venue's parking lots and roads inside the Coliseum's footprint become lawn."""
    shape = sc.shapes[0]
    P = S.shape_positions(shape)
    names = [sc.materials[sm.material].name for sm in shape.submeshes]
    if "grass_premipped" not in names:
        report["cityscape"] = "no grass material; left as retail"
        return
    gi = names.index("grass_premipped")
    rx, rz, cz = GRASS_ELLIPSE
    moved = collections.defaultdict(list)
    keep = {}
    for k, sm in enumerate(shape.submeshes):
        tris_keep, prims = [], S.decode_words(sm.words)
        for mode, ix in prims:
            if mode != S.TRIANGLE_STRIP:
                raise SystemExit("cityscape: only strips handled")
            for j in range(len(ix) - 2):
                a, b, c = ix[j], ix[j + 1], ix[j + 2]
                if j & 1:
                    a, b = b, a
                if len({a, b, c}) < 3:
                    continue
                cx = (P[a][0] + P[b][0] + P[c][0]) / 3
                czz = (P[a][2] + P[b][2] + P[c][2]) / 3
                inside = (cx / rx) ** 2 + ((czz - cz) / rz) ** 2 < 1.0
                if inside and k != gi and names[k] in ("parking_premipped", "road_premipped", "industrial_premipped",
                                                        "industrial2_premipped", "groundDirt"):
                    moved[k].append([a, b, c])
                else:
                    tris_keep.append([a, b, c])
        keep[k] = tris_keep
    n = sum(len(v) for v in moved.values())
    if not n:
        report["cityscape"] = "nothing inside the footprint"
        return
    keep[gi] += [t for v in moved.values() for t in v]
    subs = []
    for k, sm in enumerate(shape.submeshes):
        if not keep[k]:
            continue
        words = S.encode_words(S.TRIANGLE_STRIP, S.strips_to_indices(keep[k]))
        rec = bytearray(sm.record)
        struct.pack_into("<HH", rec, 0x7C, len(words) // 4, 0)
        subs.append(S.Submesh(rec, words))
    shape.submeshes = subs
    struct.pack_into("<H", shape.record, 0x54, len(subs))
    report["cityscape"] = dict(triangles_to_grass=n)


def compress_chunk(kind, decoded, system_bytes, video_bytes, template_span):
    head = T.HEADER.unpack_from(template_span, 0)
    tag = struct.unpack_from("<I", template_span, T.HEADER.size + 4)[0]
    bits = template_span[T.HEADER.size + 8]
    encoded = T.compress_vc_lz(bytes(decoded), stream_tag=tag, offset_bits=bits, verify_roundtrip=False,
                               max_candidate_comparisons=10 ** 11)[0]
    stored = S.align(len(encoded), 16)
    body = encoded + bytes(stored - len(encoded))
    minimum = T.minimum_vc_lz_overlap_scratch(body, stored, len(decoded))
    scratch = S.align(max(minimum, stored - len(encoded), 16), 16)
    back, info = T.decompress_vc_lz(body, len(decoded))
    if back != bytes(decoded):
        raise SystemExit("compressed scene failed its decode check")
    return T.HEADER.pack(kind.encode("ascii"), stored, system_bytes, video_bytes, head[4], scratch, 0, 0) + body


def build_bundle(retail, tris, mats, textures, report):
    chunks = T.parse_chunks(retail, allow_trailing=True)
    out = bytearray()
    for c in chunks:
        span = retail[c.offset:c.end_offset]
        if c.kind == "SCNE":
            body, _ = T.decode_chunk(retail, c)
            name = S._utf16z(body, S._rel(body, 0x10))
            if name == "stadium":
                sc = S.parse(body, c.system_bytes, secondary=True)
                build_stadium_scene(sc, tris, mats, textures, report)
                decoded, system, video = S.serialize(sc)
                report["scene"] = dict(retail_system=c.system_bytes, retail_video=c.video_bytes,
                                       system=system, video=video)
                span = compress_chunk("SCNE", decoded, system, video, span)
                report["scene"]["stored"] = len(span)
            elif name == "cityscape":
                sc = S.parse(body, c.system_bytes, secondary=True)
                green_cityscape(sc, report)
                decoded, system, video = S.serialize(sc)
                span = compress_chunk("SCNE", decoded, system, video, span)
        out += span
    tail = retail[chunks[-1].end_offset:]
    out += tail
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--code", default="s60", help="venue asset code of the new files (default s60)")
    ap.add_argument("--base", default="s24", help="retail venue whose bundles are the base (default s24)")
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--only", nargs="*", default=None, help="variants to build, e.g. dd nd")
    a = ap.parse_args(argv)
    import game_assets as G
    archive = G.Archive(DISC)
    a.out.mkdir(parents=True, exist_ok=True)
    sources = {"day": N.load(N.USC_DAY), "night": N.load(N.USC_NIGHT)}
    report = dict(code=a.code, base=a.base, source=dict(day=str(N.USC_DAY), night=str(N.USC_NIGHT)), bundles={})
    for v in a.only or VARIANTS:
        t, w = v[0], v[1]
        src = sources["night" if t == "n" else "day"]
        tint = tuple(x * y for x, y in zip(TOD_TINT[t], WEATHER_TINT[w]))
        tris, mats = convert_source(src, tint)
        retail = archive.read(f"{a.base}{v}.iff")
        rep = {}
        data = build_bundle(retail, tris, mats, src.textures, rep)
        path = a.out / f"{a.code}{v}.iff"
        path.write_bytes(data)
        rep.update(file=path.name, size=len(data), retail_size=len(retail),
                   sha256=hashlib.sha256(data).hexdigest())
        report["bundles"][v] = rep
        print(f"{path.name}: {len(data)} bytes (retail {len(retail)}), scene {rep['scene']}, "
              f"{rep['new_shapes']} shapes, {rep['new_vertices']} vertices", flush=True)
    (a.out / "build_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()

"""Render the REAL NFL 2K5 player model (body + helmet) wearing a uniform.

This is the 3D preview behind the Team Creator's "Preview on player (3D)" button.
It does NOT draw a cartoon: it decodes the game's own skinned player meshes from
the retail disc (``original/disc``) -- the high-detail body ``hi_body`` and head
``hi_head`` scenes of the player body set (outer entry 3, chunks 114 and 115) --
and rasterises them with the SAME software renderer that previews the stadiums
(``tools/eras/stadiums/render_preview.render``), textured with the SAME uniform
textures the Team Creator generates (splayer / helmet00 / numbers, painted by
``logos.paint_*``).

Pipeline:
  * ``PlayerModel`` extracts, once, the body + head mesh (bind-pose positions,
    UVs and triangles per submesh, grouped by which game texture the submesh
    samples) straight from the disc archive. No private cache or 55 MB index is
    needed: it scans only outer entry 3 for the body-set chunk offsets and feeds
    the compressed scene spans to the mod tools' ``ModelSpanSource``.
  * ``render_player(model, textures, ...)`` maps a dict of {texture name: RGBA
    numpy array} onto the mesh via its UVs and renders a clean front (and back)
    view on a neutral background.
  * ``textures_for_uniform(...)`` runs ``logos.paint_*`` on a base team's own
    splayer/helmet00 to produce the designed uniform's textures.

Needs: the retail disc beside the tool (``original/disc``), NumPy + Pillow (the
2K5-Mod-Studio runtime python has both), and the mod tools under
``analysis/2k-football-mod-tools``.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
GAME = HERE.parent.parent
MOD_TOOLS = GAME / "analysis" / "2k-football-mod-tools"
STADIUMS = GAME / "tools" / "eras" / "stadiums"
for p in (HERE, HERE / "vendor", MOD_TOOLS, MOD_TOOLS / "tools", STADIUMS):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

import render_preview as RP            # noqa: E402  (software rasteriser: render(), look_at())
import nfl_outer                       # noqa: E402
from mod_editor.core import nfl2k5_models as M   # noqa: E402
from nfl_scne_gltf import decode_batches, gltf_topology  # noqa: E402

HDR = struct.Struct("<4s7I")
BODY_OUTER = 3                          # retail player body set lives in outer entry 3
# chunk names on the retail disc: 113 lo_body, 114 hi_body, 115 hi_head
HI_BODY_KEY = "o3c114"
HI_HEAD_KEY = "o3c115"

# ---- which game texture each material samples -----------------------------------
# The whole hi_body samples the single "splayer" atlas (jersey, pants, sleeves,
# arm skin, pads, shoes and socks are all packed into it -- see logos._splayer_region).
# The head's helmet shell / accessories / logo / faceshield sample "helmet00".
SKIN_TONE = (214, 170, 140, 255)        # flat fallback where the disc has no per-team texture here
FACEMASK_TONE = (176, 176, 180, 255)
MOUTH_TONE = (40, 40, 40, 255)


def texture_for_material(name: str) -> str:
    """The logical texture a material samples: 'splayer', 'helmet00', 'number',
    'skin', 'facemask', 'mouth', or 'skip' (variants we do not draw)."""
    n = name
    if n.startswith(('FOREARM_', 'WRIST_', 'ELBOW_', 'TURFTAPE_', 'PLAYERNAME')): return 'skip'
    if n == 'NUMBER_L' or n.startswith('NUMBER_shoulder_B') or n.startswith('NUMBER_sleeve_B'): return 'skip'
    if n == 'UNIF_jersey': return 'jersey00'
    if n == 'UNIF_pants': return 'pants00'
    if n == 'UNIF_sleeve': return 'sleeve00'
    if n == 'SOCKS': return 'socks00'
    if n in ('SHOE_L','SHOE_R'): return 'shoes01'
    if n == 'HI_faceshield': return 'skip'
    # duplicate / alternate parts we must not draw (they overlap the base set)
    if n.startswith("NECKROLL"):
        return "skip"
    if n.startswith("SKIN_L_hand_") or n.startswith("SKIN_R_hand_"):
        return "skip"                   # keep only the base SKIN_L_hand / SKIN_R_hand
    if n in ("SHOE_both", "SHOE_tapes", "SHOE_spikes"):
        return "skip"
    # head: keep helmet variant A + facemask 00 only; drop B/C variants & other masks
    if n.startswith("HI_HELMET_B") or n.startswith("HI_HELMET_C") or n in ("HELMET_B_accessories", "HELMET_C_accessories"):
        return "skip"
    if n in ("HI_faceshield_C", "MOUTHPIECE_C", "LOGO_helmet_C") or n.startswith("HI_HELMET_C"):
        return "skip"
    if n.startswith("NUMBER_helmet_B") or n.startswith("NUMBER_helmet_C") or n.startswith("NUMBER_helmet_D"):
        return "skip"
    if n.startswith("FACEMASK") and n != "FACEMASK00":
        return "skip"
    if n in ("HAIR_dreads",):
        return "skip"
    # textures
    if n.startswith("NUMBER"):
        return "number"
    if n.startswith("FACEMASK"):
        return "facemask"
    if n.startswith("MOUTHPIECE"):
        return "mouth"
    if ("HELMET" in n) or n.startswith("LOGO_helmet") or n.startswith("HI_faceshield") or n == "LOGO_nfl" or n.startswith("HI_HELMET_flag"):
        return "helmet00"
    if n.startswith("SKIN") or "eye" in n.lower() or n.startswith("HAIR"):
        return "skin"
    # everything else on the body samples the splayer atlas
    return "splayer"


# ---- disc model extraction ------------------------------------------------------

def _scan_outer_chunks(archive, outer_index: int):
    """{chunk_index: (offset, stored_size, kind)} for one outer entry, using the
    same bounded-wrapper walk as tools/nfl_resource_scan.py (no inventory file)."""
    entry = archive.entries[outer_index]
    out: dict[int, tuple[int, int, str]] = {}
    off = 0
    idx = 0

    def bounded(hdr, at):
        kind = hdr[:4]
        stored = struct.unpack_from("<I", hdr, 4)[0]
        return (all(0x20 <= b <= 0x7E for b in kind) and stored != 0
                and at + 0x20 + stored <= entry.size), stored

    while entry.size - off >= 0x20:
        hdr = nfl_outer.read_entry_range(archive, entry, off, 0x20)
        ok, stored = bounded(hdr, off)
        if not ok:
            # skip a bounded all-zero gap to the next 0x10-aligned wrapper
            scan_end = min(entry.size, off + 0x100000)
            cur = off
            nxt = None
            while cur < scan_end:
                size = min(0x4000, scan_end - cur)
                blk = nfl_outer.read_entry_range(archive, entry, cur, size)
                rel = next((i for i, v in enumerate(blk) if v), None)
                if rel is None:
                    cur += size
                    continue
                cand = cur + rel
                if cand % 0x10 == 0 and entry.size - cand >= 0x20:
                    h2 = nfl_outer.read_entry_range(archive, entry, cand, 0x20)
                    ok2, _ = bounded(h2, cand)
                    nxt = cand if ok2 else None
                break
            if nxt is None:
                break
            off = nxt
            continue
        out[idx] = (off, stored, hdr[:4].decode("ascii"))
        off += 0x20 + stored
        idx += 1
    return out, entry


class Submesh:
    __slots__ = ("P", "uv", "col", "tris", "texkind", "material")

    def __init__(self, P, uv, col, tris, texkind, material):
        self.P = P
        self.uv = uv
        self.col = col
        self.tris = tris
        self.texkind = texkind
        self.material = material


class PlayerModel:
    """The real player body + head mesh, decoded once from the retail disc."""

    def __init__(self, disc_dir: str | Path):
        disc_dir = Path(disc_dir)
        pack0 = disc_dir / "vc_53450030" / "0"
        archive = nfl_outer.parse_archive(pack0)
        chunks, entry = _scan_outer_chunks(archive, BODY_OUTER)
        spans = {}
        for chunk in (114, 115):
            off, stored, kind = chunks[chunk]
            if kind != "SCNE":
                raise RuntimeError(f"o{BODY_OUTER}c{chunk} is {kind}, not SCNE")
            spans[f"o{BODY_OUTER}c{chunk}"] = nfl_outer.read_entry_range(archive, entry, off, 0x20 + stored)
        self.source = M.ModelSpanSource(spans)
        self.submeshes: list[Submesh] = []
        self._load(HI_BODY_KEY)
        body_count = len(self.submeshes)
        self._load(HI_HEAD_KEY)
        # Body and head resources use different authoring origins. Align their
        # original head bind anchors rather than dropping the head at the hip.
        anchors = []
        for key in (HI_BODY_KEY, HI_HEAD_KEY):
            _, decoded, scene = self.source.parse(key)
            shape = scene['shapes'][0]
            lanes = M._shape_lanes(scene, shape, decoded)
            skin = M.decode_skin(decoded, shape, lanes, scene['submeshes'])
            if skin is None: raise RuntimeError('Player head bind anchors are unavailable')
            anchors.append(np.array(next(t['absolute'] for t in skin.transforms if t['name']=='head')))
        delta = anchors[0] - anchors[1]
        for mesh in self.submeshes[body_count:]: mesh.P = mesh.P + delta
        allP = np.concatenate([s.P for s in self.submeshes], axis=0)
        self.center = allP.mean(axis=0)
        self.lo = allP.min(axis=0)
        self.hi = allP.max(axis=0)

    def _load(self, key: str):
        _res, decoded, scene = self.source.parse(key)
        shapes = {s["index"]: s for s in scene["shapes"]}
        for sm in scene["submeshes"]:
            mat = sm["material_name"]
            texkind = texture_for_material(mat)
            if texkind == "skip":
                continue
            shape = shapes[sm["shape_index"]]
            lanes = M._shape_lanes(scene, shape, decoded)
            positions = M.read_positions(decoded, shape, lanes)
            P = np.array(positions, np.float64)
            if lanes.texcoord is not None:
                pairs = M.read_lane_2h(decoded, shape, lanes.texcoord, lanes.vertex_count)
                uv = np.array([M.uv_to_gltf(u, v, lanes.uv_scale, lanes.uv_offset) for u, v in pairs], np.float64)
                # The splayer atlas (jersey/pants/arms/shoes) samples with the U
                # axis mirrored relative to how the P8 decoder lays the image out;
                # without this the jersey draws from the pants (white) region.
                # Verified by rendering: U-flip puts the red jersey on the chest and
                # the white striped pants on the legs. helmet00 / numbers are correct
                # as-is, so only splayer-textured submeshes are mirrored.
                if texkind == "splayer":
                    uv[:, 0] = 1.0 - uv[:, 0]
            else:
                uv = np.zeros((lanes.vertex_count, 2), np.float64)
            # vertex colour lane is baked lighting; use a neutral boost so the
            # texture reads at full strength (render() multiplies rgb*col*1.15).
            col = np.full((lanes.vertex_count, 4), 0.87, np.float32)
            tris = []
            for xbox_mode, raw in decode_batches(decoded, int(sm["command_offset"]), int(sm["primary_command_word_count"])):
                gltf_mode, idx, _c = gltf_topology(xbox_mode, raw)
                if not idx:
                    continue
                if gltf_mode == 4:                         # triangles
                    for k in range(0, len(idx) - 2, 3):
                        tris.append((idx[k], idx[k + 1], idx[k + 2]))
                elif gltf_mode == 5:                       # triangle strip
                    for k in range(len(idx) - 2):
                        a, b, c = idx[k], idx[k + 1], idx[k + 2]
                        if k & 1:
                            a, b = b, a
                        if a != b and b != c and a != c:
                            tris.append((a, b, c))
            if tris:
                self.submeshes.append(Submesh(P, uv, col, np.array(tris, np.int32), texkind, mat))


# ---- rendering ------------------------------------------------------------------

def _as_rgba_u8(img) -> np.ndarray:
    """PIL image or numpy array -> HxWx4 uint8."""
    if isinstance(img, np.ndarray):
        a = img
    else:
        a = np.array(img.convert("RGBA"))
    if a.dtype != np.uint8:
        a = a.astype(np.uint8)
    return a


def _flat(color) -> np.ndarray:
    return np.array([[list(color)]], np.uint8)


def _groups(model: PlayerModel, textures: dict):
    """render_preview groups: (P, uv, col, tris, tex_rgba, name) per submesh."""
    tex_cache = {}

    def tex(kind):
        if kind in tex_cache:
            return tex_cache[kind]
        if kind == "splayer" and "splayer" in textures:
            t = _as_rgba_u8(textures["splayer"])
        elif kind == "helmet00" and "helmet00" in textures:
            t = _as_rgba_u8(textures["helmet00"])
        elif kind == "number" and "number" in textures:
            t = _as_rgba_u8(textures["number"])
        elif kind == "facemask":
            t = _flat(textures.get("facemask_color", FACEMASK_TONE))
        elif kind == "mouth":
            t = _flat(MOUTH_TONE)
        elif kind == "skin":
            t = _flat(textures.get("skin", SKIN_TONE))
        else:
            t = _flat(SKIN_TONE)
        tex_cache[kind] = t
        return t

    for s in model.submeshes:
        yield s.P, s.uv, s.col, s.tris, tex(s.texkind), s.material


def render_player(model: PlayerModel, textures: dict, *, w=360, h=560,
                  views=("front", "back"), sky=(236, 238, 242)):
    """Render the player; returns {view: HxWx3 uint8 image}."""
    groups = list(_groups(model, textures))
    c = model.center
    # frame height to the body; +y is up. The player faces +z (facemask at +z),
    # so the FRONT camera sits on the +z side looking toward -z.
    height = float(model.hi[1] - model.lo[1])
    cy = (model.lo[1] + model.hi[1]) / 2.0
    cx = (model.lo[0] + model.hi[0]) / 2.0
    cz = (model.lo[2] + model.hi[2]) / 2.0
    fov = 40
    # distance so the full height fits the view with a small margin
    dist = (height * 0.62) / np.tan(np.radians(fov) / 2) * 1.10
    ele = height * 0.30           # a mild downward hero angle
    out = {}
    cams = {
        "front": dict(eye=(cx, cy + ele, cz + dist), target=(cx, cy, cz), fov=fov),
        "back":  dict(eye=(cx, cy + ele, cz - dist), target=(cx, cy, cz), fov=fov),
        "left":  dict(eye=(cx - dist, cy + ele, cz), target=(cx, cy, cz), fov=fov),
        "right": dict(eye=(cx + dist, cy + ele, cz), target=(cx, cy, cz), fov=fov),
    }
    for v in views:
        out[v] = RP.render(groups, cams[v], w, h, sky=sky)
    return out


# ---- uniform textures from a design spec ----------------------------------------

def _decode_uniform_textures(archive, code: str, away: bool):
    """{name: PIL RGBA} of splayer / helmet00 / number digits from a base team's file."""
    import game_assets as G
    from PIL import Image
    data = archive.read(f"{code}{'a' if away else 'h'}0.iff")
    if data is None:
        raise FileNotFoundError(f"uniform file for base team {code} not on disc")
    out = {}
    for c in G.T.parse_chunks(data, allow_trailing=True):
        if c.kind != "TXTR" or not c.compressed:
            continue
        dec, _ = G.T.decode_chunk(data, c)
        try:
            info = G.T.parse_texture(dec, c)
        except Exception:
            continue
        if info.format_code == 0x0B and info.name in ("splayer", "helmet00", "48"):
            out[info.name] = Image.frombytes("RGBA", (info.width, info.height),
                                             G.T.texture_to_rgba(dec, c, info))
    return out


def textures_for_uniform(archive, base_code: str, spec_side: dict, away: bool):
    """Run the Team Creator painters to make the designed uniform's textures.

    ``archive`` is game_assets.Archive, ``spec_side`` is one side of
    ``creator.uniforms_to_spec()`` (home/away), with jersey_color, pants_color,
    helmet_color, helmet_stripe, etc.  Returns the texture dict render_player wants.
    """
    import logos
    base = _decode_uniform_textures(archive, base_code, away)
    bc = _base_colors(base_code)
    textures: dict = {}
    if "splayer" in base:
        textures["splayer"] = logos.paint_splayer(base["splayer"], spec_side, bc, away)
    if "helmet00" in base:
        hs = logos.paint_helmet_shell(base["helmet00"], spec_side["helmet_color"])
        if spec_side.get("helmet_stripe"):
            hs = logos.draw_helmet_stripe(hs, spec_side["helmet_stripe"])
        textures["helmet00"] = hs
    if "48" in base:
        textures["number"] = logos.paint_number(base["48"], spec_side)
    fm = spec_side.get("facemask_color")
    if fm:
        textures["facemask_color"] = tuple(int(x) for x in fm) + (255,)
    return textures


def _base_colors(code: str):
    """[(r,g,b),(r,g,b)] of a base team, read like creator.base_colors (by abbr)."""
    import json
    try:
        teams = json.loads((HERE / "teams_2k5.json").read_text())
        abbr = next(t[1] for t in teams if t[0] == code)
        p = GAME / "mods" / "teams" / abbr / "team.json"
        c = json.loads(p.read_text(encoding="utf-8"))["colors"]

        def hx(s):
            s = s.lstrip("#")
            return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))
        return [hx(c["primary"]), hx(c["secondary"])]
    except Exception:
        return [(170, 0, 0), (255, 255, 255)]


# ---- self-test ------------------------------------------------------------------

if __name__ == "__main__":
    import argparse
    from PIL import Image
    import game_assets as G
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc", default=str(GAME / "original" / "disc"))
    ap.add_argument("--out", default=str(HERE / "preview_out"))
    ap.add_argument("--base", default="13")
    ap.add_argument("--designed", action="store_true")
    args = ap.parse_args()
    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    print("decoding player model from disc...")
    model = PlayerModel(args.disc)
    print(f"  body+head: {len(model.submeshes)} submeshes, bbox {model.lo.round(1)}..{model.hi.round(1)}")
    arc = G.Archive(Path(args.disc))
    if args.designed:
        spec = {"jersey_color": [140, 20, 35], "jersey_trim": [245, 245, 245],
                "pants_color": [245, 245, 245],
                "pant_stripe": {"color": [140, 20, 35], "width": 14, "style": "solid"},
                "number_color": [245, 245, 245], "number_outline": [140, 20, 35],
                "helmet_color": [140, 20, 35], "helmet_stripe": {"color": [245, 245, 245], "width": 22},
                "facemask_color": [200, 200, 205]}
        textures = textures_for_uniform(arc, args.base, spec, away=False)
        tag = "designed"
    else:
        textures = _decode_uniform_textures(arc, args.base, away=False)
        tag = "raw"
    imgs = render_player(model, textures, views=("front", "back"))
    for v, im in imgs.items():
        path = outdir / f"player_{tag}_{v}.png"
        Image.fromarray(im, "RGB").save(path)
        print("wrote", path)

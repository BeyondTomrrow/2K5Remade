"""Team-neutral art for the Coliseum bundles (coliseum_build.py), replacing what the QUALCOMM (s24) clone carries.

How retail 2K5 dresses a field (default.xbe, see docs/eras/research/COLISEUM-IMPORT.md "Team-neutral v2"):

* the end zones (`endzone_{N,S}_{L,M,R}`) and the midfield logo (`center_logo`) are textures **inside each
  stadium's `field` scene**, painted for that stadium's own team (s24: "SAN DIEGO" and the Chargers helmet);
* the only runtime override is `ct<prefix><w>.iff` (0x62CA0, format at 0xE610B8), with the prefix taken from the
  *stadium row* +0x14. It is filled only for the fictional / created-team rows (95..99, 51..85); every NFL row
  has "", so `ctd.iff` is never found and the baked art stays. The binder (0x9C5DE..0x9C66B) looks the seven
  names (table 0x4F0090) up as loaded TXTRs and swaps the field material's texture. Nothing is keyed by the
  home team, so a retail-style route for "whoever is at home" does not exist without code;
* `pad_north` / `pad_south` (goal-post pads) are loose TXTRs of the stadium bundle;
* `banner_home_team`, `banner_away_team`, `banner_home_player` and `banner_corp` are ordinary stadium-scene
  materials: the executable never looks them up by name (no such string in default.xbe), so their art is
  whatever the bundle carries (s24: "Let's Go CHARGERS", Chargers players' fan banners).

So the Coliseum gets neutral art of its own: "LOS ANGELES" end zones, no midfield logo, plain pads, neutral fan
banners, and a Los Angeles basin backdrop (San Gabriel-style ridge, low city, palms) instead of Mission Valley's
hills and trolley. Everything is painted for the dry variant and carried to the rain / snow variants with a
per-channel linear fit of the retail dry -> variant texture pair (the way retail darkens or whitens them).
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

import numpy as np

FONTS = Path(r"C:\Windows\Fonts")
# backdrop style (ncaa_stadiums.py sets one per venue): the default is the Coliseum's LA basin
SKYLINE = dict(seed=1923, ridge=True, foothills=True, palms=14, conifers=0, tall=0.12, tall_max=10,
               label="LA basin backdrop; trolley removed")
_RETAIL_DRY = {}


# --- helpers ------------------------------------------------------------------------------------------------

def _font(name, size):
    from PIL import ImageFont
    for n in (name, "impact.ttf", "arialbd.ttf"):
        p = FONTS / n
        if p.is_file():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def scene_rgba(sc, material):
    import p8
    t = sc.textures[sc.materials[sc.material_index(material)].texture]
    return p8.decode(t.record, t.pixels, t.palette).copy()


def put_scene_texture(sc, material, rgba, opaque):
    """Replace the texture a material draws (every material sharing it follows), same size, P8."""
    import p8
    import scne as S
    from PIL import Image
    ti = sc.materials[sc.material_index(material)].texture
    old = sc.textures[ti]
    img = Image.fromarray(np.asarray(rgba, np.uint8), "RGBA")
    rec, pixels, palette = p8.encode(img, old.record, cap=max(old.width, old.height), opaque=opaque)
    sc.textures[ti] = S.Texture(bytearray(rec), pixels, palette)


def patch_texture_in_place(body, system_bytes, sc, material, rgba, opaque):
    """Overwrite a scene texture's pixels and palette inside the decoded scene ``body`` (bytearray), keeping its
    size, mip count, offsets and every other byte: for scenes that are not re-serialized (the field)."""
    import p8
    import scne as S
    from PIL import Image
    ti = sc.materials[sc.material_index(material)].texture
    t = sc.textures[ti]
    w, h, mips = t.width, t.height, t.mips
    base = Image.fromarray(np.asarray(rgba, np.uint8), "RGBA")
    if base.size != (w, h):
        base = base.resize((w, h), Image.LANCZOS)
    levels = []
    for k in range(mips):
        lw, lh = max(1, w >> k), max(1, h >> k)
        a = np.asarray(base if k == 0 else base.resize((lw, lh), Image.BOX), np.uint8).copy()
        a[..., 3] = 255 if opaque else np.where(a[..., 3] >= 128, 255, 0)
        levels.append(a)
    palette, idx = p8.quantize(levels)
    import nfl_txtr as T
    pixels = b"".join(T.swizzle_2d(i.tobytes(), i.shape[1], i.shape[0], 1) for i in idx)
    if len(pixels) != len(t.pixels):
        raise SystemExit(f"{material}: {len(pixels)} texel bytes != {len(t.pixels)}")
    pal = palette[:, [2, 1, 0, 3]].tobytes()
    pix_at, pal_at = struct.unpack_from("<II", t.record, 4)
    body[system_bytes + pix_at:system_bytes + pix_at + len(pixels)] = pixels
    body[system_bytes + pal_at:system_bytes + pal_at + 1024] = pal
    sc.textures[ti] = S.Texture(t.record, pixels, pal)


def fit_transfer(dry, var):
    """Per-channel a*x+b mapping retail dry -> retail variant (opaque texels of both)."""
    dry = np.asarray(dry, np.float64)
    var = np.asarray(var, np.float64)
    m = (dry[..., 3] > 127) & (var[..., 3] > 127)
    coef = []
    for ch in range(3):
        x, y = dry[..., ch][m], var[..., ch][m]
        if len(x) < 16 or x.std() < 1e-3:
            coef.append((1.0, 0.0))
            continue
        a, b = np.polyfit(x, y, 1)
        coef.append((a, b))

    def apply(rgba):
        out = np.asarray(rgba, np.float64).copy()
        for ch, (a, b) in enumerate(coef):
            out[..., ch] = a * out[..., ch] + b
        return np.clip(out, 0, 255).astype(np.uint8)
    apply.coef = coef
    return apply


def retail_dry(archive, base):
    """{(scene, material) or ('loose', name): RGBA} of the retail dry-day bundle (the fit's reference)."""
    if base in _RETAIL_DRY:
        return _RETAIL_DRY[base]
    import nfl_txtr as T
    import scne as S
    data = archive.read(f"{base}dd.iff")
    out = {}
    for c in T.parse_chunks(data, allow_trailing=True):
        body, _ = T.decode_chunk(data, c)
        if c.kind == "SCNE":
            name = S._utf16z(body, S._rel(body, 0x10))
            if name in ("field", "cityscape", "stadium"):
                sc = S.parse(body, c.system_bytes, secondary=True)
                for m in sc.materials:
                    if m.texture is not None and m.name in NEEDED.get(name, ()):
                        out[(name, m.name)] = scene_rgba(sc, m.name)
        elif c.kind == "TXTR":
            ti = T.parse_texture(body, c)
            if ti.name in PADS:
                out[("loose", ti.name)] = loose_rgba(body, c, ti)
    _RETAIL_DRY[base] = out
    return out


NEEDED = {"field": ("endzone_N_L", "endzone_N_M", "endzone_N_R", "center_logo"),
          "cityscape": ("hill",),
          "stadium": ("banner_home_team", "banner_away_team", "banner_home_player")}
PADS = ("pad_north", "pad_south")


# --- end zones and midfield ----------------------------------------------------------------------------------

ENDZONE_TEXT = "LOS ANGELES"


def endzone_art(ref):
    """768x128 RGBA (three 256x128 thirds) for the dry field: the retail grass, "LOS ANGELES" in white block
    letters with a dark keyline, no underline bar. ``ref`` = the retail dry thirds side by side."""
    from PIL import Image, ImageDraw, ImageFilter
    h, w = ref.shape[0], ref.shape[1]
    clean = ref[:, :80, :3]                                   # s24 N_L: columns 0..85 are bare grass
    tile = np.concatenate([clean, clean[:, ::-1]], axis=1)
    reps = int(math.ceil(w / tile.shape[1]))
    grass = np.tile(tile, (1, reps, 1))[:, :w].astype(np.float64)
    # letters: rendered big, squeezed into the retail lettering box (x 95..690, y 8..120)
    font = _font("impact.ttf", 220)
    probe = Image.new("L", (10, 10))
    l, t, r, b = (int(math.floor(v)) for v in ImageDraw.Draw(probe).textbbox((0, 0), ENDZONE_TEXT, font=font,
                                                                              stroke_width=12))
    txt = Image.new("L", (r - l + 8, b - t + 8), 0)
    key = Image.new("L", txt.size, 0)
    ImageDraw.Draw(key).text((4 - l, 4 - t), ENDZONE_TEXT, font=font, fill=255, stroke_width=12, stroke_fill=255)
    ImageDraw.Draw(txt).text((4 - l, 4 - t), ENDZONE_TEXT, font=font, fill=255)
    box_w, box_h = 600, 108
    natural = (r - l) / max(b - t, 1) * box_h * 1.1        # a short name keeps block proportions (LOS ANGELES fills)
    box_w = int(min(box_w, max(300, natural)))
    key = key.resize((box_w, box_h), Image.LANCZOS)
    txt = txt.resize((box_w, box_h), Image.LANCZOS)
    x0, y0 = (w - box_w) // 2, (h - box_h) // 2
    km = np.zeros((h, w))
    tm = np.zeros((h, w))
    km[y0:y0 + box_h, x0:x0 + box_w] = np.asarray(key, np.float64) / 255.0
    tm[y0:y0 + box_h, x0:x0 + box_w] = np.asarray(txt, np.float64) / 255.0
    lum = grass.mean(axis=2)
    paint = 0.86 + 0.14 * (lum - lum.mean()) / max(lum.std(), 1.0) / 3.0     # paint on grass: a little texture
    white = np.array((228, 232, 222), np.float64)[None, None, :] * paint[..., None]
    dark = np.array((28, 34, 30), np.float64)[None, None, :] * paint[..., None]
    out = grass * (1 - km[..., None]) + dark * km[..., None]
    out = out * (1 - tm[..., None]) + white * tm[..., None]
    rgba = np.dstack([np.clip(out, 0, 255).astype(np.uint8), np.full((h, w), 255, np.uint8)])
    return rgba


def neutral_field(body, system_bytes, sc, variant, dry, report):
    """The field scene is patched in place (texels and palettes only; the scene is not re-serialized)."""
    ref = np.concatenate([dry[("field", m)] for m in NEEDED["field"][:3]], axis=1)
    art = endzone_art(ref)
    cur = np.concatenate([scene_rgba(sc, m) for m in NEEDED["field"][:3]], axis=1)
    if variant[1] != "d":
        art = fit_transfer(ref, cur)(art)
    for k, m in enumerate(NEEDED["field"][:3]):          # endzone_S_* share these three textures
        patch_texture_in_place(body, system_bytes, sc, m, art[:, 256 * k:256 * (k + 1)], opaque=True)
    logo = scene_rgba(sc, "center_logo")
    logo[...] = 0                                         # no midfield logo (alpha-tested: nothing drawn)
    patch_texture_in_place(body, system_bytes, sc, "center_logo", logo, opaque=False)
    report["field"] = dict(endzones=ENDZONE_TEXT, center_logo="none (transparent)")


# --- goal-post pads (loose TXTR) -------------------------------------------------------------------------------

def loose_rgba(body, chunk, ti):
    import nfl_txtr as T
    return np.frombuffer(T.texture_to_rgba(body, chunk, ti), np.uint8).reshape(ti.height, ti.width, 4).copy()


def neutral_pad(rgba):
    """The retail pad without its vertical CHARGERS lettering (columns 55..70 refilled from 35..50; the ESPN tag
    stays), desaturated to a neutral slate."""
    out = rgba.copy()
    out[:, 55:71] = rgba[:, 35:51]
    rgb = out[..., :3].astype(np.float64)
    lum = rgb.mean(axis=2, keepdims=True)
    slate = lum * np.array((0.92, 0.97, 1.08))[None, None, :]
    out[..., :3] = np.clip(0.25 * rgb + 0.75 * slate, 0, 255).astype(np.uint8)
    return out


def replace_loose_txtr(data, chunk, compress, report):
    """The chunk span with its P8 texture repainted neutral (same size, mips and descriptor)."""
    import nfl_txtr as T
    import p8
    from PIL import Image
    body, _ = T.decode_chunk(data, chunk)
    ti = T.parse_texture(body, chunk)
    if ti.name not in PADS or ti.format_code != p8.P8_FORMAT:
        return None
    art = neutral_pad(loose_rgba(body, chunk, ti))
    rec = bytearray(0x20)
    struct.pack_into("<I", rec, 0x0C, 0x29)
    _, pixels, palette = p8.encode(Image.fromarray(art, "RGBA"), rec, cap=max(ti.width, ti.height), opaque=True)
    want = sum(max(1, ti.width >> k) * max(1, ti.height >> k) for k in range(ti.mip_levels))
    if len(pixels) != want:
        raise SystemExit(f"{ti.name}: mip chain {len(pixels)} != retail {want}")
    out = bytearray(body)
    v = chunk.system_bytes
    out[v + ti.pixel_offset:v + ti.pixel_offset + want] = pixels
    out[v + ti.palette_offset:v + ti.palette_offset + 1024] = palette
    report.setdefault("pads", []).append(ti.name)
    # stored uncompressed, like retail's own detail_normal chunk (header +0x10 = 0: no VC-LZ stream)
    return T.HEADER.pack(b"TXTR", len(out), chunk.system_bytes, chunk.video_bytes, 0, 0, 0, 0) + bytes(out)


# --- fan banners ------------------------------------------------------------------------------------------------

BANNERS = {      # material: (cells across, cells down, [(text, ink)])
    "banner_home_team": (1, 2, [("LET'S GO\nL.A.!", (20, 30, 90)), ("DE-FENSE!", (150, 20, 20))]),
    "banner_away_team": (1, 2, [("WELCOME\nTO L.A.", (20, 70, 30)), ("BEAT 'EM!", (20, 20, 20))]),
    "banner_home_player": (2, 2, [("TOUCH-\nDOWN!", (150, 20, 20)), ("WE LOVE\nFOOTBALL", (20, 30, 90)),
                                  ("#1\nFANS", (20, 20, 20)), ("COLISEUM\nCROWD", (20, 70, 30))]),
}


def banner_art(ref, cols, rows, items):
    """The retail sheets' shapes and folds (their text lifted off), new hand-lettered slogans."""
    from PIL import Image, ImageDraw, ImageFilter
    h, w = ref.shape[:2]
    out = ref.copy()
    ch, cw = h // rows, w // cols
    for k, (text, ink) in enumerate(items):
        cx, cy = (k % cols) * cw, (k // cols) * ch
        cell = ref[cy:cy + ch, cx:cx + cw]
        alpha = cell[..., 3]
        lum = cell[..., :3].astype(np.float64).mean(axis=2)
        lit = lum[alpha > 127]
        floor = 0.88 * (np.percentile(lit, 85) if len(lit) else 230.0)
        folds = np.maximum(lum, floor)                      # the old lettering and logos lifted to sheet white
        sheet = Image.fromarray(np.clip(folds, 0, 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(1.2))
        s = np.asarray(sheet, np.float64)[..., None] / max(floor / 0.88, 1.0)
        s = np.clip(s, 0.0, 1.0)
        base = np.clip(s * np.array((246, 244, 236))[None, None, :], 0, 255)
        # the text, fitted to the sheet's inner box
        ys, xs = np.nonzero(alpha > 127)
        if len(xs) == 0:
            continue
        bx0, bx1, by0, by1 = xs.min(), xs.max(), ys.min(), ys.max()
        iw, ih = int((bx1 - bx0) * 0.84), int((by1 - by0) * 0.72)
        font = _font("comicbd.ttf", 96)
        probe = ImageDraw.Draw(Image.new("L", (8, 8)))
        l, t, r, b = (int(math.floor(v)) for v in probe.multiline_textbbox((0, 0), text, font=font, align="center",
                                                                             spacing=4))
        big = Image.new("L", (r - l + 8, b - t + 8), 0)
        ImageDraw.Draw(big).multiline_text((4 - l, 4 - t), text, font=font, fill=255, align="center", spacing=4)
        sc = min(iw / big.width, ih / big.height)
        tw, th = max(1, int(big.width * sc)), max(1, int(big.height * sc))
        big = big.resize((tw, th), Image.LANCZOS)
        m = np.zeros(alpha.shape, np.float64)
        ox = bx0 + ((bx1 - bx0) - tw) // 2
        oy = by0 + ((by1 - by0) - th) // 2
        m[oy:oy + th, ox:ox + tw] = np.asarray(big, np.float64) / 255.0
        m = np.minimum(m * 1.4, 1.0)
        rgb = base * (1 - m[..., None]) + (np.array(ink, np.float64)[None, None, :] * s) * m[..., None]
        out[cy:cy + ch, cx:cx + cw, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return out


def neutral_banners(sc, variant, dry, report):
    done = []
    for mat, (cols, rows, items) in BANNERS.items():
        try:
            cur = scene_rgba(sc, mat)
        except Exception:
            continue
        ref = dry[("stadium", mat)]
        art = banner_art(ref, cols, rows, items)
        if variant[1] != "d":
            art = fit_transfer(ref, cur)(art)
        art[..., 3] = cur[..., 3]
        put_scene_texture(sc, mat, art, opaque=False)
        done.append(mat)
    report["banners"] = done


# --- the backdrop -------------------------------------------------------------------------------------------------

def _periodic(n, w, seed, harmonics):
    rng = np.random.default_rng(seed)
    x = np.arange(w) / w * 2 * math.pi
    y = np.zeros(w)
    for k in range(1, harmonics + 1):
        y += rng.normal() / k ** 1.1 * np.sin(k * x + rng.uniform(0, 2 * math.pi))
    return (y - y.min()) / max(y.max() - y.min(), 1e-9) * n


def skyline_art(ref):
    """128x128 RGBA in the retail `hill` layout: rows 0..55 are the backdrop wall (V 0.004..0.434, tiled along
    U, so everything here is periodic in x), rows 64..127 the San Diego trolley sprite (left transparent: gone).
    The backdrop: a hazy San Gabriel-style ridge, a nearer brown foothill line, a low Los Angeles city band with
    a few mid-rise blocks, and a tree / palm line at the foot. Colours keyed to the retail hills' tones."""
    h, w = ref.shape[:2]
    st = SKYLINE
    rng = np.random.default_rng(st.get("seed", 1923))
    out = np.zeros((h, w, 4), np.float64)
    bottom = 56                                   # rows 0..55 drawn
    far = 10 + _periodic(14, w, 7, 9)             # far ridge top (row index): peaks around rows 10..24
    near = 27 + _periodic(7, w, 11, 6)            # foothills
    haze_far = np.array(st.get("ridge_colour", (150, 158, 172)))
    haze_near = np.array(st.get("foothill_colour", (140, 132, 112)))
    for x in range(w):
        if st.get("ridge", True):
            for y in range(int(far[x]), bottom):
                t = (y - far[x]) / max(bottom - far[x], 1)
                out[y, x, :3] = haze_far * (0.95 + 0.08 * t) + rng.normal(0, 3, 3)
                out[y, x, 3] = 255
        if st.get("foothills", True):
            for y in range(int(near[x]), bottom):
                out[y, x, :3] = haze_near * (0.92 + 0.1 * rng.random()) + rng.normal(0, 4, 3)
                out[y, x, 3] = 255
    # low city band: blocks of varying height, a few mid-rises
    x = 0
    while x < w:
        bw = int(rng.integers(3, 8))
        tall = rng.random() < st.get("tall", 0.12)
        top = 41 - int(rng.integers(0, 4)) - (int(rng.integers(5, st.get("tall_max", 10))) if tall else 0)
        tone = np.array((178, 172, 160)) * (0.8 + 0.25 * rng.random())
        for xx in range(x, min(x + bw, w)):
            for y in range(top, 50):
                c = tone * (0.93 if (xx - x) == bw - 1 else 1.0)
                if tall and (y - top) % 3 == 1 and (xx - x) % 2 == 1:
                    c = c * 0.72                  # window rows
                out[y, xx, :3] = c + rng.normal(0, 3, 3)
                out[y, xx, 3] = 255
        x += bw + int(rng.integers(0, 2))
    # tree line and palms at the foot
    canopy = 46 + _periodic(4, w, 23, 12)
    green = np.array((62, 78, 50))
    for x in range(w):
        for y in range(int(canopy[x]), bottom):
            out[y, x, :3] = green * (0.75 + 0.35 * rng.random())
            out[y, x, 3] = 255
    for px in rng.choice(w, st.get("palms", 14), replace=False) if st.get("palms", 14) else []:
        top = int(rng.integers(30, 38))
        for y in range(top + 2, int(canopy[px]) + 1):
            out[y, px, :3] = (88, 76, 60)
            out[y, px, 3] = 255
        for dx, dy in ((-2, 1), (-1, 0), (0, 0), (1, 0), (2, 1), (-1, 1), (1, 1), (0, 1), (-3, 2), (3, 2), (0, 2)):
            xx = (px + dx) % w
            out[top + dy, xx, :3] = green * (0.8 + 0.3 * rng.random())
            out[top + dy, xx, 3] = 255
    for px in rng.choice(w, st.get("conifers", 0), replace=False) if st.get("conifers", 0) else []:
        top = int(rng.integers(28, 38))                   # evergreens (Pacific Northwest)
        for y in range(top, int(canopy[px]) + 1):
            half = (y - top) // 3
            for dx in range(-half, half + 1):
                out[y, (px + dx) % w, :3] = np.array((40, 62, 44)) * (0.8 + 0.3 * rng.random())
                out[y, (px + dx) % w, 3] = 255
    rgba = np.clip(out, 0, 255).astype(np.uint8)
    # match the retail backdrop's overall tone (its opaque texels in the same rows)
    r = ref[:bottom][ref[:bottom, :, 3] > 127][:, :3].astype(np.float64)
    o = rgba[:bottom][rgba[:bottom, :, 3] > 127][:, :3].astype(np.float64)
    if len(r) and len(o):
        gain = r.mean(axis=0) / np.maximum(o.mean(axis=0), 1.0)
        gain = np.clip(0.5 + 0.5 * gain, 0.7, 1.3)          # halfway: keep the LA haze, share the lighting
        rgba[..., :3] = np.clip(rgba[..., :3] * gain[None, None, :], 0, 255).astype(np.uint8)
    return rgba


def neutral_cityscape(sc, variant, dry, report):
    try:
        cur = scene_rgba(sc, "hill")
    except Exception:
        report["skyline"] = "no hill material"
        return
    ref = dry[("cityscape", "hill")]
    art = skyline_art(ref)
    if variant[1] != "d":
        art = fit_transfer(ref, cur)(art)
    put_scene_texture(sc, "hill", art, opaque=False)
    report["skyline"] = SKYLINE.get("label", "neutral backdrop; trolley removed")

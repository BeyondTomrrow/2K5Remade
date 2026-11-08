"""A new team's logo on its uniforms' textures (P8: 256-colour, swizzled, mips).

In every NFL 2K5 uniform file the helmet textures ("helmet00", "helmet02",
256x256) share one layout: the logo twice on the shell (the upper one mirrored,
for the other side of the helmet) and the team name on the bumper plate. The
128x128 "logo" and 64x64 "flipchip" are the logo alone; "chiclet" is a colour
chip. apply_logo() repaints those from the player's logo image.
"""
from __future__ import annotations

import colorsys
import statistics
import struct

from PIL import Image, ImageDraw, ImageFont

import game_assets as G

T = G.T

# Helmet atlas regions (256x256): the two logo spots and the name plate.
HELMET_LOGO_TOP = (36, 56, 120, 112)       # mirrored on the model
HELMET_LOGO_BOTTOM = (36, 144, 120, 200)
HELMET_NAME = (190, 60, 251, 82)
# marks that keep their real colours: the flag, the NFL shield, the maker's logos
HELMET_KEEP = ((98, 100, 145, 152), (180, 82, 256, 256), (180, 0, 256, 60))


def _decode_p8(dec: bytes, c, info) -> Image.Image:
    return Image.frombytes("RGBA", (info.width, info.height), T.texture_to_rgba(dec, c, info))


def _encode_p8(dec: bytearray, c, info, img: Image.Image) -> None:
    """img (RGBA, the texture's size) -> the texture's indices for every mip
    level and its palette, in place in the decoded chunk."""
    img = img.convert("RGBA")
    alpha = img.getchannel("A")
    q = img.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:255 * 3]
    video = c.system_bytes
    # palette: 256 BGRA entries; index 255 = transparent
    pb = bytearray()
    for i in range(255):
        r, g, b = pal[i * 3:i * 3 + 3] if i * 3 + 2 < len(pal) else (0, 0, 0)
        pb += bytes((b, g, r, 255))
    pb += bytes((0, 0, 0, 0))
    dec[video + info.palette_offset: video + info.palette_offset + 1024] = pb
    off = video + info.pixel_offset
    w, h = info.width, info.height
    level_img, level_a = q, alpha
    for level in range(max(1, info.mip_levels)):
        idx = bytearray(level_img.tobytes())
        a = level_a.tobytes()
        for i in range(len(idx)):
            if a[i] < 128:
                idx[i] = 255
        sw = T.swizzle_2d(bytes(idx), w, h, 1)
        dec[off:off + w * h] = sw
        off += w * h
        w, h = max(1, w // 2), max(1, h // 2)
        level_img = img.convert("RGB").resize((w, h), Image.LANCZOS).quantize(palette=q, dither=Image.Dither.NONE)
        level_a = alpha.resize((w, h), Image.LANCZOS)


def _fit(logo: Image.Image, box, flip=False) -> tuple[Image.Image, tuple[int, int]]:
    bw, bh = box[2] - box[0], box[3] - box[1]
    lg = logo.copy()
    lg.thumbnail((bw, bh), Image.LANCZOS)
    if flip:
        lg = lg.transpose(Image.FLIP_LEFT_RIGHT)
    return lg, (box[0] + (bw - lg.width) // 2, box[1] + (bh - lg.height) // 2)


def _font(size):
    for name in ("arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def recolor_image(img: Image.Image, f, keep=()) -> Image.Image:
    """Every pixel through the colour mapper, except in the keep boxes."""
    img = img.copy()
    px = img.load()
    cache = {}
    counts = img.crop((0, 0, 140, 256)).convert("RGB").getcolors(140 * 256) or [(0, (0, 0, 0))]
    shell = max(counts)[1]

    def near_shell(r, g, b):
        return abs(r - shell[0]) + abs(g - shell[1]) + abs(b - shell[2]) < 60
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if any(k[0] <= x < k[2] and k[1] <= y < k[3] for k in keep) and not near_shell(r, g, b):
                continue
            c = cache.get((r, g, b))
            if c is None:
                c = cache[(r, g, b)] = f((r, g, b))
            px[x, y] = c + (a,)
    return img


def helmet(img: Image.Image, logo: Image.Image, name: str, color) -> Image.Image:
    img = img.copy()
    # the shell: the commonest colour of the logo side of the atlas
    counts = img.crop((0, 0, 140, 256)).convert("RGB").getcolors(140 * 256)
    shell = max(counts)[1] + (255,)
    d = ImageDraw.Draw(img)
    for box, flip in ((HELMET_LOGO_TOP, True), (HELMET_LOGO_BOTTOM, False)):
        d.rectangle(box, fill=shell)
        lg, at = _fit(logo, box, flip)
        img.alpha_composite(lg, at)
    x0, y0, x1, y1 = HELMET_NAME
    d.rectangle(HELMET_NAME, fill=(255, 255, 255, 255))
    text = name.upper()[:12]
    size = 18
    while size > 7:
        f = _font(size)
        l, t, r, b = d.textbbox((0, 0), text, font=f)
        if r - l <= x1 - x0 - 4 and b - t <= y1 - y0 - 2:
            break
        size -= 1
    d.text(((x0 + x1 - (r - l)) // 2 - l, (y0 + y1 - (b - t)) // 2 - t), text, font=f, fill=tuple(color) + (255,))
    return img


# ---- splayer (on-model jersey+pants) tweaks, opt-in via creator.json ----------
#
# The "splayer" texture (256x128) carries the base team's jersey, pants and a
# chest/hip logo. apply_logo() leaves it untouched for every team UNLESS the
# creator.json names regions to edit, so the generic path is byte-for-byte
# unchanged for teams that do not opt in.
#
#   "splayer_logo_box": [x0,y0,x1,y1]  (or a list of such boxes) -- erase the
#        base team's chest/hip logo by filling the box with the median colour
#        of the surrounding fabric, so no stray logo rides on the uniform.
#   "away_pants_white_box": [x0,y0,x1,y1] (or a list) -- recolour a region to
#        white, shading preserved, for teams whose base pants came out coloured
#        on the AWAY uniform but should be white. Applied only to away files.

def _norm_boxes(b):
    if not b:
        return []
    if isinstance(b[0], (list, tuple)):
        return [tuple(x) for x in b]
    return [tuple(b)]


def _clip_box(box, w, h):
    x0, y0, x1, y1 = box
    return (max(0, min(x0, w)), max(0, min(y0, h)), max(0, min(x1, w)), max(0, min(y1, h)))


def blank_logo_boxes(img: Image.Image, boxes, ring: int = 6) -> Image.Image:
    """Each box filled with the median colour of the fabric around it (a ring
    just outside the box), skipping near-white logo pixels so the fill picks up
    the jersey/pants colour, not the logo it is covering."""
    img = img.convert("RGBA").copy()
    px = img.load()
    d = ImageDraw.Draw(img)
    for box in boxes:
        x0, y0, x1, y1 = _clip_box(box, img.width, img.height)
        rs, gs, bs = [], [], []
        for y in range(max(0, y0 - ring), min(img.height, y1 + ring)):
            for x in range(max(0, x0 - ring), min(img.width, x1 + ring)):
                if x0 <= x < x1 and y0 <= y < y1:
                    continue
                r, g, b, a = px[x, y]
                if r > 200 and g > 200 and b > 200:        # skip the white logo
                    continue
                rs.append(r); gs.append(g); bs.append(b)
        fill = ((int(statistics.median(rs)), int(statistics.median(gs)), int(statistics.median(bs)), 255)
                if rs else (128, 128, 128, 255))
        d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=fill)
    return img


def whiten_boxes(img: Image.Image, boxes) -> Image.Image:
    """Recolour the saturated (coloured) pixels of each box to white, keeping
    their shading as lightness so fabric folds survive; low-saturation pixels
    (already white/grey stripes and trim) are left as they are."""
    img = img.convert("RGBA").copy()
    px = img.load()
    for box in boxes:
        x0, y0, x1, y1 = _clip_box(box, img.width, img.height)
        for y in range(y0, y1):
            for x in range(x0, x1):
                r, g, b, a = px[x, y]
                h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
                if s <= 0.12:                               # already white/grey trim
                    continue
                nl = max(0.0, min(1.0, 0.72 + 0.55 * l))    # lift & desaturate -> white
                v = int(nl * 255)
                px[x, y] = (v, v, v, a)
    return img


# ---- granular uniform painting (opt-in via a "uniform" spec) ------------------
#
# The weak path recolours whole palettes by hue (muddy). The painter below
# instead PAINTS each region of the on-model uniform to a chosen colour,
# keeping the fabric's shading as luminance, so a jersey/pants/number/helmet
# comes out as the designed colour, not a hue-shift of the base team's.
#
# Region map (found by decoding base team 13's home+away splayer, numbers and
# helmet00 and viewing them -- see the Team Creator uniform-editor notes):
#   splayer 256x128 : LEFT half x[0,128) = PANTS, RIGHT half x[128,256) = JERSEY
#                     (same atlas home and away; home pants white, away red, so
#                     painting is luminance-driven per side, not hue-keyed).
#                     cleats/skin block bottom-left ~x[0,42] y[86,128] (kept);
#                     the red hip/hem strip x[42,128] y[88,128] is team-coloured
#                     in BOTH home and away, so it is painted with the JERSEY
#                     colour. chest/hip logo blanked by splayer_logo_box.
#   numbers 48..57, an48..57 (64x64), hn48..57 (32x32): glyph fill + outline on
#                     a transparent ground -> number_color / number_outline.
#   helmet00/02 256x256: red shell -> helmet_color (greys/vents and the
#                     flag/NFL/maker KEEP boxes preserved); a vertical CENTER
#                     STRIPE sits at x[~147,171] (centre 159), y[18,222], found
#                     by comparing the Dolphins(14) and Cowboys(07) helmets.

NUMBER_NAMES = ({str(n) for n in range(48, 58)}
                | {f"hn{n}" for n in range(48, 58)}
                | {f"an{n}" for n in range(48, 58)})
HELMET_STRIPE_CENTER = 159                 # atlas x of the crown-stripe centre
HELMET_STRIPE_Y = (18, 222)                # atlas y span of the crown stripe


def _hls(rgb):
    return colorsys.rgb_to_hls(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255)


def _lum(r, g, b):
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0


def _hue_near(h, ref_h, tol=0.07):
    d = abs(h - ref_h)
    return min(d, 1 - d) <= tol


def _colorize(target, L, Lmid, contrast=0.9, lo=0.03, hi=0.98):
    """target colour shaded by how a source pixel's luminance L sits relative
    to the region's median Lmid: the region's midtone lands on the target, folds
    vary around it. Neutral (white/grey/black) targets get softened contrast so
    a dark source patch does not turn a white jersey muddy grey."""
    th, tl, ts = _hls(target)
    if ts < 0.12:
        contrast = min(contrast, 0.45)
    out_l = max(lo, min(hi, tl + (L - Lmid) * contrast))
    r, g, b = colorsys.hls_to_rgb(th, out_l, ts)
    return (round(r * 255), round(g * 255), round(b * 255))


def _splayer_region(x, y, W, H):
    """'pants', 'jersey', or None (kept), for a splayer pixel; by fraction of
    the atlas so it holds at any mip/size."""
    fx, fy = x / W, y / H
    if fx < 0.164 and fy >= 0.672:         # cleats / skin, bottom-left
        return None
    if fy >= 0.672 and fx < 0.5:           # red hip/hem strip (team-coloured both sides)
        return "jersey"
    if fx < 0.5:                           # left half above the hem = pants
        return "pants"
    if fy < 0.75:                          # right half upper = jersey body
        return "jersey"
    return "pants"                         # right half lower = pant-top / belt


def paint_splayer(img: Image.Image, uniform: dict, base_colors, is_away: bool) -> Image.Image:
    """Paint the on-model jersey+pants atlas to the uniform's colours."""
    img = img.convert("RGBA").copy()
    px = img.load()
    W, H = img.size
    sec_h = _hls(tuple(base_colors[1]))[0] if base_colors and len(base_colors) > 1 else None
    pri_h = _hls(tuple(base_colors[0]))[0] if base_colors else None
    jersey = tuple(uniform["jersey_color"])
    pants = tuple(uniform.get("pants_color", uniform["jersey_color"]))
    jtrim = tuple(uniform.get("jersey_trim", (255, 255, 255)))
    pstripe = uniform.get("pant_stripe")
    pstripe_c = tuple(pstripe["color"]) if pstripe and pstripe.get("color") else None

    def is_secondary(h, l, s):
        return (sec_h is not None and s >= 0.20 and _hue_near(h, sec_h, 0.08)
                and not (pri_h is not None and _hue_near(h, pri_h, 0.05)) and 0.12 < l < 0.9)

    def mid(region):
        Ls = []
        for y in range(H):
            for x in range(W):
                if _splayer_region(x, y, W, H) != region:
                    continue
                r, g, b, a = px[x, y]
                if a < 8:
                    continue
                h, l, s = _hls((r, g, b))
                if l < 0.1 or is_secondary(h, l, s):
                    continue
                Ls.append(_lum(r, g, b))
        return statistics.median(Ls) if Ls else 0.5
    midL = {"pants": mid("pants"), "jersey": mid("jersey")}

    for y in range(H):
        for x in range(W):
            region = _splayer_region(x, y, W, H)
            if region is None:
                continue
            r, g, b, a = px[x, y]
            if a < 8:
                continue
            h, l, s = _hls((r, g, b))
            if is_secondary(h, l, s):
                tgt = (pstripe_c if pstripe_c else pants) if region == "pants" else jtrim
            else:
                tgt = pants if region == "pants" else jersey
            px[x, y] = _colorize(tgt, _lum(r, g, b), midL[region]) + (a,)

    # a crisp pant stripe down the leg (recolouring the base's thin stripe
    # alone reads too faintly); calibrated on the 256x128 atlas.
    if pstripe_c and pstripe.get("style", "solid") != "none":
        sw = max(2, int(pstripe.get("width", 14) * W / 256))
        cx = int(0.186 * W)
        y1 = int(0.69 * H)
        for y in range(0, y1):
            for x in range(cx - sw // 2, cx + sw - sw // 2):
                if not (0 <= x < W):
                    continue
                r, g, b, a = px[x, y]
                if a < 8:
                    continue
                px[x, y] = _colorize(pstripe_c, _lum(r, g, b), midL["pants"], 0.6) + (a,)
    return img


def paint_number(img: Image.Image, uniform: dict) -> Image.Image:
    """Repaint a number glyph: its fill -> number_color, its outline edge ->
    number_outline, keeping the antialiased alpha."""
    img = img.convert("RGBA").copy()
    px = img.load()
    W, H = img.size
    ncol = tuple(uniform["number_color"])
    nout = tuple(uniform.get("number_outline", (255, 255, 255)))
    cnt = {}
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a > 240:
                cnt[(r, g, b)] = cnt.get((r, g, b), 0) + 1
    if not cnt:
        return img
    fill = max(cnt, key=cnt.get)
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a < 8:
                continue
            d = abs(r - fill[0]) + abs(g - fill[1]) + abs(b - fill[2])
            px[x, y] = (ncol if d < 140 else nout) + (a,)
    return img


def paint_helmet_shell(img: Image.Image, helmet_color, keep=HELMET_KEEP) -> Image.Image:
    """Recolour the saturated shell to helmet_color (shading kept); greys
    (vents/screws) and the KEEP boxes (flag/NFL/maker) are left alone."""
    img = img.convert("RGBA").copy()
    px = img.load()
    W, H = img.size
    counts = img.crop((0, 0, 140, 256)).convert("RGB").getcolors(140 * 256)
    sh = _hls(max(counts)[1])[0]
    Ls = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a < 8:
                continue
            h, l, s = _hls((r, g, b))
            if s > 0.25 and _hue_near(h, sh, 0.09):
                Ls.append(_lum(r, g, b))
    Lm = statistics.median(Ls) if Ls else 0.5
    for y in range(H):
        for x in range(W):
            if any(k[0] <= x < k[2] and k[1] <= y < k[3] for k in keep):
                continue
            r, g, b, a = px[x, y]
            if a < 8:
                continue
            h, l, s = _hls((r, g, b))
            if s > 0.25 and _hue_near(h, sh, 0.12):
                px[x, y] = _colorize(tuple(helmet_color), _lum(r, g, b), Lm) + (a,)
    return img


def draw_helmet_stripe(img: Image.Image, stripe: dict, keep=HELMET_KEEP) -> Image.Image:
    """A solid vertical centre stripe down the crown (its atlas location found
    empirically); the KEEP marks are never overpainted."""
    img = img.convert("RGBA").copy()
    px = img.load()
    w = int(stripe.get("width", 22))
    col = tuple(stripe["color"])
    x0 = HELMET_STRIPE_CENTER - w // 2
    x1 = HELMET_STRIPE_CENTER + (w - w // 2)
    y0, y1 = HELMET_STRIPE_Y
    for y in range(y0, y1):
        for x in range(x0, x1):
            if not (0 <= x < img.width) or any(k[0] <= x < k[2] and k[1] <= y < k[3] for k in keep):
                continue
            px[x, y] = col + (255,)
    return img


def apply_logo(data: bytes, logo: Image.Image, name: str, colors, mapper=None, original: bytes | None = None,
               helmet_logo: Image.Image | None = None, splayer_logo_box=None,
               away_pants_white_box=None, is_away: bool = False, uniform: dict | None = None,
               base_colors=None) -> bytes:
    """A uniform file with the new logo on its helmets, logo and flip chip.
    With the base file (original) and the colour mapper, helmets are
    recoloured from the original pixels, keeping the flag and maker marks."""
    # Which granular uniform parts are requested (opt-in; absent fields keep
    # the old behaviour for that texture).
    u = uniform or {}
    paint_body = bool(u.get("jersey_color") or u.get("pants_color"))
    paint_nums = bool(u.get("number_color"))
    paint_shell = bool(u.get("helmet_color"))
    helmet_stripe = u.get("helmet_stripe") if u else None

    orig_tex = {}
    if original is not None and (mapper is not None or paint_body or paint_nums or paint_shell):
        for oc in T.parse_chunks(original, allow_trailing=True):
            if oc.kind == "TXTR" and oc.compressed:
                od, _ = T.decode_chunk(original, oc)
                try:
                    oi = T.parse_texture(od, oc)
                except Exception:
                    continue
                if oi.format_code != 0x0B:
                    continue
                want = (oi.name.startswith("helmet")
                        or (paint_body and oi.name == "splayer")
                        or (paint_nums and oi.name in NUMBER_NAMES))
                if want:
                    orig_tex[oi.name] = _decode_p8(od, oc, oi)
    logo_boxes = _norm_boxes(splayer_logo_box)
    pants_boxes = _norm_boxes(away_pants_white_box) if is_away else []
    chunks = T.parse_chunks(data, allow_trailing=True)
    out = bytearray()
    for c in chunks:
        raw = data[c.offset:c.end_offset]
        if c.kind == "TXTR" and c.compressed:
            dec, _ = T.decode_chunk(data, c)
            try:
                info = T.parse_texture(dec, c)
            except Exception:
                info = None
            nm = info.name if info else ""
            touch_splayer = bool(info and info.format_code == 0x0B and nm == "splayer"
                                 and (logo_boxes or pants_boxes or paint_body))
            touch_number = bool(info and info.format_code == 0x0B and paint_nums and nm in NUMBER_NAMES)
            if info and info.format_code == 0x0B and (nm in ("helmet00", "helmet02", "logo", "flipchip", "chiclet") or touch_splayer or touch_number):
                dec = bytearray(dec)
                if touch_splayer:
                    if paint_body:
                        src = orig_tex.get("splayer")
                        new = paint_splayer(src if src is not None else _decode_p8(dec, c, info),
                                            u, base_colors or colors, is_away)
                    else:
                        new = _decode_p8(dec, c, info)
                    if pants_boxes:
                        new = whiten_boxes(new, pants_boxes)
                    if logo_boxes:
                        new = blank_logo_boxes(new, logo_boxes)
                elif touch_number:
                    src = orig_tex.get(nm)
                    new = paint_number(src if src is not None else _decode_p8(dec, c, info), u)
                elif nm.startswith("helmet"):
                    src = orig_tex.get(nm)
                    if src is None:
                        src = _decode_p8(dec, c, info)
                    elif paint_shell:
                        src = paint_helmet_shell(src, u["helmet_color"])
                    elif mapper is not None:
                        src = recolor_image(src, mapper, HELMET_KEEP)
                    new = helmet(src, helmet_logo or logo, name, (u.get("helmet_color") or colors[0]))
                    if helmet_stripe and helmet_stripe.get("color"):
                        new = draw_helmet_stripe(new, helmet_stripe)
                elif nm == "chiclet":
                    new = Image.new("RGBA", (info.width, info.height), tuple(colors[0]) + (255,))
                else:
                    new = Image.new("RGBA", (info.width, info.height), (0, 0, 0, 0))
                    lg, at = _fit(logo, (2, 2, info.width - 2, info.height - 2))
                    new.alpha_composite(lg, at)
                _encode_p8(dec, c, info, new)
                comp, _ = T.compress_vc_lz(bytes(dec))
                scratch = (T.minimum_vc_lz_overlap_scratch(comp, len(comp), len(dec)) + 15) & ~15
                comp = comp + bytes(-len(comp) % 16)
                raw = struct.pack("<4s7I", c.kind.encode(), len(comp), c.system_bytes, c.video_bytes,
                                  c.compression_magic, scratch + 16, 0, 0) + comp
        out += raw
    out += data[chunks[-1].end_offset:]
    return bytes(out)


def text_logo(text: str, primary, secondary, size: int = 256) -> Image.Image:
    """A simple logo when the player has none: the initials on a ring."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((6, 6, size - 6, size - 6), fill=tuple(secondary) + (255,))
    d.ellipse((22, 22, size - 22, size - 22), fill=tuple(primary) + (255,))
    t = text.upper()[:3]
    f = _font(int(size * (0.5 if len(t) < 3 else 0.36)))
    l, tp, r, b = d.textbbox((0, 0), t, font=f)
    d.text(((size - (r - l)) // 2 - l, (size - (b - tp)) // 2 - tp), t, font=f, fill=tuple(secondary) + (255,))
    return img


# logos.cdf / flipchip.cdf: the front end's per-team logos (Team Rosters
# backdrop "logo_<code>_<style>", 256x256; the roster card's chip
# "<code>_flipchip_00_<side><style>", 64x64), each in a fixed-size slot of an
# uncompressed P8 TXTR. The game finds them by name CRC in a sorted index
# (logos.iff / flipchip.iff); the PC port serves an added team's slots from
# mods\teams\<folder>\cache\<name>.bin (src/nfl2k5_team_files.c).
CACHE_FILES = (("logos.cdf", "logos.iff"), ("flipchip.cdf", "flipchip.iff"))


def outlined(logo: Image.Image, width: int = 6, color=(255, 255, 255)) -> Image.Image:
    """The logo with an outline around its shape, so it reads on any background."""
    from PIL import ImageFilter
    pad = width * 2
    src = Image.new("RGBA", (logo.width + 2 * pad, logo.height + 2 * pad), (0, 0, 0, 0))
    src.alpha_composite(logo.convert("RGBA"), (pad, pad))
    a = src.getchannel("A").point(lambda v: 255 if v > 96 else 0).filter(ImageFilter.MaxFilter(2 * width + 1))
    out = Image.new("RGBA", src.size, tuple(color) + (0,))
    out.putalpha(a)
    out.alpha_composite(src)
    return out


def cache_art(arc, base: str, code: str, logo: Image.Image) -> dict[str, bytes]:
    """{slot name: slot bytes}: every logos.cdf / flipchip.cdf picture of the
    base team, with the new team's logo and under its code."""
    out = {}
    art = outlined(logo)
    for cdf, iff in CACHE_FILES:
        ix = arc.read(iff)
        count, slot = struct.unpack_from("<II", ix, 0x60)
        data = arc.read(cdf)
        for i in range(count):
            s = data[i * slot:(i + 1) * slot]
            if s[:4] != b"TXTR":
                continue
            c = T.parse_chunks(s, allow_trailing=True)[0]
            if c.compressed:
                continue
            body = bytearray(s[c.body_offset:c.end_offset])
            try:
                info = T.parse_texture(bytes(body), c)
            except Exception:
                continue
            n = info.name
            if not (n.startswith("logo_" + base + "_") or n.startswith(base + "_flipchip_")) or info.format_code != 0x0B:
                continue
            new_name = n.replace(base, code, 1)
            img = Image.new("RGBA", (info.width, info.height), (0, 0, 0, 0))
            m = max(1, info.width // 32)
            lg, at = _fit(art, (m, m, info.width - m, info.height - m))
            img.alpha_composite(lg, at)
            _encode_p8(body, c, info, img)
            body[info.name_offset:info.name_offset + 2 * len(new_name)] = new_name.encode("utf-16le")
            out[new_name] = bytes(s[:c.body_offset] + body + s[c.end_offset:])
    return out


# Player photos: portrait.cdf slots (128x128 P8) named by the player record's
# photo number. An added team's photos get numbers from 40000 by team code
# (photo_base) and are served from cache\<number>.bin like the logos.
CODE_CHARS = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def photo_base(code: str) -> int:
    """40000 + 100 per team code (Z1 = 40000 ... Q? after Z?): 100 photos a team."""
    code = code.upper()
    return 40000 + ("ZQ".index(code[0]) * len(CODE_CHARS) + CODE_CHARS.index(code[1])) * 100


def portrait_slots(arc, photos: dict[int, Image.Image]) -> dict[str, bytes]:
    """{"<number>": slot bytes} for {photo number: image}; the image is cropped
    to a square from the top (a head shot) and scaled to 128x128."""
    ix = arc.read("Portrait.iff")
    count, slot = struct.unpack_from("<II", ix, 0x60)
    data = arc.read("portrait.cdf")
    tpl = data[:slot]                                   # any portrait: the layout to fill
    c = T.parse_chunks(tpl, allow_trailing=True)[0]
    base_body = tpl[c.body_offset:c.end_offset]
    info = T.parse_texture(base_body, c)
    room = info.descriptor_offset - info.name_offset
    out = {}
    for number, img in photos.items():
        img = img.convert("RGBA")
        side = min(img.width, img.height)
        img = img.crop(((img.width - side) // 2, 0, (img.width - side) // 2 + side, side)).resize((info.width, info.height), Image.LANCZOS)
        body = bytearray(base_body)
        _encode_p8(body, c, info, img)
        name = str(number).encode("utf-16le") + b"\0\0"
        body[info.name_offset:info.name_offset + room] = name + bytes(room - len(name))
        out[str(number)] = bytes(tpl[:c.body_offset] + body + tpl[c.end_offset:])
    return out

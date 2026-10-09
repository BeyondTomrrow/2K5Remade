"""A new team's logo on its uniforms' textures (P8: 256-colour, swizzled, mips).

In every NFL 2K5 uniform file the helmet textures ("helmet00", "helmet02",
256x256) share one layout: the logo twice on the shell (the upper one mirrored,
for the other side of the helmet) and the team name on the bumper plate. The
128x128 "logo" and 64x64 "flipchip" are the logo alone; "chiclet" is a colour
chip. apply_logo() repaints those from the player's logo image.
"""
from __future__ import annotations

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


def apply_logo(data: bytes, logo: Image.Image, name: str, colors, mapper=None, original: bytes | None = None) -> bytes:
    """A uniform file with the new logo on its helmets, logo and flip chip.
    With the base file (original) and the colour mapper, helmets are
    recoloured from the original pixels, keeping the flag and maker marks."""
    orig_tex = {}
    if original is not None and mapper is not None:
        for oc in T.parse_chunks(original, allow_trailing=True):
            if oc.kind == "TXTR" and oc.compressed:
                od, _ = T.decode_chunk(original, oc)
                try:
                    oi = T.parse_texture(od, oc)
                except Exception:
                    continue
                if oi.name.startswith("helmet") and oi.format_code == 0x0B:
                    orig_tex[oi.name] = _decode_p8(od, oc, oi)
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
            if info and info.format_code == 0x0B and info.name in ("helmet00", "helmet02", "logo", "flipchip", "chiclet"):
                dec = bytearray(dec)
                if info.name.startswith("helmet"):
                    src = orig_tex.get(info.name)
                    src = recolor_image(src, mapper, HELMET_KEEP) if src is not None else _decode_p8(dec, c, info)
                    new = helmet(src, logo, name, colors[0])
                elif info.name == "chiclet":
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

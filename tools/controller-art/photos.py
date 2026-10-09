"""Controller Setup pictures from photos (2026-10-09).

    python tools/controller-art/photos.py

tools/controller-art/photos/<kind>.jpg (white background) -> assets/controls/
pad-<kind>.png (1024x1024, transparent, placed where the game's Duke picture
sits) plus pad-<kind>.json (button positions) and pad-<kind>-hl-<button>.png
highlight overlays. Button shapes are hand-placed in the photo's pixels below.
"""
import json
import os
from collections import deque

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "assets", "controls")

# Shapes in photo pixels: ("c", cx, cy, r) circle, ("e", x0, y0, x1, y1) ellipse,
# ("r", x0, y0, x1, y1, radius) rounded rectangle, ("p", [(x, y)...]) polygon.
PADS = {
    "xbox": ("xbox-series.jpg", {
        "a": ("c", 465, 268, 17), "b": ("c", 506, 231, 17), "x": ("c", 426, 230, 17), "y": ("c", 466, 193, 17),
        "lstick": ("c", 171, 230, 37), "rstick": ("c", 393, 320, 37), "dpad": ("c", 242, 323, 44),
        "back": ("c", 276, 230, 13), "start": ("c", 360, 230, 13),
        "lb": ("p", [(126, 158), (170, 130), (240, 118), (262, 124), (258, 140), (190, 146), (136, 170)]),
        "rb": ("p", [(514, 158), (470, 130), (400, 118), (378, 124), (382, 140), (450, 146), (504, 170)]),
        "lt": ("p", [(150, 132), (178, 112), (240, 104), (250, 118), (186, 126)]),
        "rt": ("p", [(490, 132), (462, 112), (400, 104), (390, 118), (454, 126)]),
    }),
    "dualsense": ("dualsense.jpg", {
        "a": ("c", 886, 494, 34), "b": ("c", 960, 416, 34), "x": ("c", 810, 416, 34), "y": ("c", 886, 340, 34),
        "lstick": ("c", 406, 553, 62), "rstick": ("c", 730, 553, 62), "dpad": ("c", 254, 416, 88),
        "back": ("e", 316, 284, 346, 330), "start": ("e", 792, 284, 822, 330),
        "lb": ("r", 190, 246, 330, 272, 12), "rb": ("r", 814, 246, 954, 272, 12),
        "lt": ("r", 186, 224, 322, 248, 12), "rt": ("r", 818, 224, 954, 248, 12),
    }),
    "ds4": ("ds4.jpg", {
        "a": ("c", 1634, 892, 62), "b": ("c", 1781, 747, 62), "x": ("c", 1489, 747, 62), "y": ("c", 1634, 600, 62),
        "lstick": ("c", 685, 1035, 176), "rstick": ("c", 1327, 1035, 176), "dpad": ("c", 385, 745, 212),
        "back": ("e", 569, 495, 634, 598), "start": ("e", 1389, 495, 1453, 598),
        "lb": ("r", 271, 381, 518, 427, 18), "rb": ("r", 1480, 381, 1727, 427, 18),
        "lt": ("r", 277, 342, 511, 381, 18), "rt": ("r", 1487, 342, 1720, 381, 18),
    }),
}


def cut_out(im):
    """Alpha from the white background: flood fill from the border through
    near-white pixels; inside that region alpha follows the darkness (the
    soft shadow under the pad stays as a faint shadow)."""
    im = im.convert("RGB")
    w, h = im.size
    px = im.load()
    bg = bytearray(w * h)
    q = deque()
    for x in range(w):
        q.append((x, 0)); q.append((x, h - 1))
    for y in range(h):
        q.append((0, y)); q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        i = y * w + x
        if bg[i]:
            continue
        if min(px[x, y]) < 236:
            continue
        bg[i] = 1
        if x > 0: q.append((x - 1, y))
        if x < w - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < h - 1: q.append((x, y + 1))
    a = Image.new("L", (w, h), 255)
    ap = a.load()
    for y in range(h):
        for x in range(w):
            if bg[y * w + x]:
                ap[x, y] = 0
    # the soft shadow round a white pad: near-white pixels next to the
    # background become a dark, faint shadow instead of a white halo
    near = Image.new("L", (w, h), 0)
    near.putdata([255 * b for b in bg])
    near = near.filter(ImageFilter.MaxFilter(25))
    np_ = near.load()
    shadow = {}
    for y in range(h):
        for x in range(w):
            if not bg[y * w + x] and np_[x, y]:
                r, g, b = px[x, y]
                if min(r, g, b) >= 214 and max(r, g, b) - min(r, g, b) < 12:
                    shadow[(x, y)] = 255 - min(r, g, b)
    # soften the cut edge by one pixel
    a = a.filter(ImageFilter.GaussianBlur(0.8))
    out = im.convert("RGBA")
    out.putalpha(a)
    op = out.load()
    for (x, y), d in shadow.items():
        op[x, y] = (0, 0, 0, min(255, d * 4))
    return out


def shape_mask(size, s):
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    if s[0] == "c":
        _, cx, cy, r = s
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    elif s[0] == "e":
        d.ellipse(s[1:5], fill=255)
    elif s[0] == "r":
        d.rounded_rectangle(s[1:5], radius=s[5], fill=255)
    else:
        d.polygon(s[1], fill=255)
    return m


def main():
    os.makedirs(OUT, exist_ok=True)
    for kind, (photo, shapes) in PADS.items():
        src = Image.open(os.path.join(HERE, "photos", photo))
        art = cut_out(src)
        x0, y0, x1, y1 = art.getchannel("A").point(lambda v: 255 if v > 40 else 0).getbbox()
        bx0, by0, bx1, by1 = 8, 33 * 4, 1016, 224 * 4          # the Duke's place in its texture
        s = min((bx1 - bx0) / (x1 - x0), (by1 - by0) / (y1 - y0))
        nw, nh = round((x1 - x0) * s), round((y1 - y0) * s)
        ox, oy = bx0 + ((bx1 - bx0) - nw) // 2, by0 + ((by1 - by0) - nh) // 2
        canvas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
        canvas.alpha_composite(art.crop((x0, y0, x1, y1)).resize((nw, nh), Image.LANCZOS), (ox, oy))
        canvas.save(os.path.join(OUT, "pad-%s.png" % kind))
        info = {"hl": {}}
        for name, sh in shapes.items():
            m = shape_mask(src.size, sh).crop((x0, y0, x1, y1)).resize((nw, nh), Image.LANCZOS)
            full = Image.new("L", (1024, 1024), 0)
            full.paste(m, (ox, oy))
            bb = full.getbbox()
            info[name] = [(bb[0] + bb[2]) // 2, (bb[1] + bb[3]) // 2, max(bb[2] - bb[0], bb[3] - bb[1]) // 2]
            grown = full.filter(ImageFilter.MaxFilter(13))
            glow = grown.filter(ImageFilter.GaussianBlur(14))
            ring = ImageChops.subtract(grown, full.filter(ImageFilter.MaxFilter(5)))
            outside = ImageChops.subtract(glow, full)
            alpha = ImageChops.lighter(Image.eval(outside, lambda v: min(255, v * 2)), ring)
            alpha = ImageChops.lighter(alpha, Image.eval(full, lambda v: v * 22 // 100))
            ov = Image.new("RGBA", (1024, 1024), (255, 214, 60, 0))
            ov.putalpha(alpha)
            hb = ov.getchannel("A").getbbox()
            ov.crop(hb).save(os.path.join(OUT, "pad-%s-hl-%s.png" % (kind, name)))
            info["hl"][name] = [hb[0], hb[1]]
        with open(os.path.join(OUT, "pad-%s.json" % kind), "w") as f:
            json.dump(info, f, indent=1)
        print("pad", kind, (nw, nh), "at", (ox, oy))


if __name__ == "__main__":
    main()

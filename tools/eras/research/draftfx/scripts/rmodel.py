"""Render exported glTF models front + 3/4 views. usage: rmodel.py out.png gltf [gltf...] (optionally gltf:label)"""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, math
sys.path.insert(0, __import__("os").path.join(ROOT, r"tools\eras\research\office\scripts"))
import render as R, numpy as np
from PIL import Image, ImageDraw
out = sys.argv[1]; tiles = []
for arg in sys.argv[2:]:
    p, _, label = arg.partition('|')
    mats, tris = R.load_gltf(p, {})
    allp = np.concatenate([t[1] for t in tris])
    lo, hi = allp.min(0), allp.max(0); c = (lo + hi) / 2; ext = (hi - lo)
    h = max(ext[1], ext[0], ext[2]) * 1.15
    dist = h / 2 / math.tan(math.radians(23.2)) * 1.05
    ims = []
    for ang in (0, 35, 180):
        a = math.radians(ang)
        eye = (c[0] + dist * math.sin(a), c[1], c[2] + dist * math.cos(a))
        ims.append(R.render(mats, tris, eye, tuple(c), W=240, H=320, light=1.4))
    tile = Image.new('RGB', (720, 340), (30, 30, 30)); d = ImageDraw.Draw(tile)
    for i, im in enumerate(ims): tile.paste(im, (i * 240, 20))
    d.text((4, 4), (label or p.replace(chr(92), '/').split('/')[-1]) + f"  bbox {np.round(ext).astype(int).tolist()}", fill=(255, 255, 0))
    tiles.append(tile); print(p, lo.round(1), hi.round(1))
sheet = Image.new('RGB', (720, 340 * len(tiles)))
for i, t in enumerate(tiles): sheet.paste(t, (0, 340 * i))
sheet.save(out)

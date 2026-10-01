"""Compare the NBC SNF render with the reference, pixel by pixel.

    python tools/nbc-snf/diff.py [render.png] [out.png]

Prints the mean absolute error (0-255, all channels) for each region of the
bug and writes a sheet: reference / render / difference x4, enlarged.
"""
import sys

import numpy as np
from PIL import Image

ROOT = __file__.replace('\\', '/').rsplit('/tools/', 1)[0]
REF = ROOT + '/mods/presentations/NBC_SNF/reference/reference.png'
render = sys.argv[1] if len(sys.argv) > 1 else ROOT + '/logs/nbc-render.png'
out = sys.argv[2] if len(sys.argv) > 2 else ROOT + '/logs/nbc-diff.png'

ref = np.asarray(Image.open(REF).convert('RGB'), dtype=np.float32)
ren = np.asarray(Image.open(render).convert('RGB'), dtype=np.float32)
d = np.abs(ref - ren).mean(axis=2)

REGIONS = {
    'whole bug':      (413, 90, 989, 184),
    'away logo':      (415, 113, 478, 165),
    'away abbr+rec':  (478, 118, 560, 160),
    'away score':     (568, 115, 650, 162),
    'away panel':     (476, 113, 652, 165),
    'pod':            (652, 92, 749, 182),
    'clock text':     (660, 126, 742, 148),
    'quarter':        (680, 152, 721, 172),
    'home score':     (750, 115, 830, 162),
    'home abbr+rec':  (840, 118, 912, 160),
    'home panel':     (749, 113, 932, 165),
    'home logo':      (930, 113, 986, 165),
    'rail+timeouts':  (415, 163, 986, 171),
}
for name, (x0, y0, x1, y1) in REGIONS.items():
    r = d[y0:y1, x0:x1]
    print('%-15s mean %5.2f   >24: %5.1f%%   max %3d' % (name, r.mean(), (r > 24).mean() * 100, r.max()))

x0, y0, x1, y1 = REGIONS['whole bug']
k = 3
a = Image.fromarray(ref[y0:y1, x0:x1].astype(np.uint8)).resize(((x1 - x0) * k, (y1 - y0) * k), Image.NEAREST)
b = Image.fromarray(ren[y0:y1, x0:x1].astype(np.uint8)).resize(((x1 - x0) * k, (y1 - y0) * k), Image.NEAREST)
c = Image.fromarray(np.clip(d[y0:y1, x0:x1] * 4, 0, 255).astype(np.uint8)).convert('RGB').resize(((x1 - x0) * k, (y1 - y0) * k), Image.NEAREST)
sheet = Image.new('RGB', (a.width, a.height * 3 + 20), (128, 0, 128))
sheet.paste(a, (0, 0)); sheet.paste(b, (0, a.height + 10)); sheet.paste(c, (0, a.height * 2 + 20))
sheet.save(out)
print('sheet', out)

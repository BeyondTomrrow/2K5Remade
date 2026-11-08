"""xemu screenshot without screendump: pmemsave of the NV2A scanout buffer
(PCRTC_START read over gdb; 0x03710000 for NFL 2K5) -> PNG. xemu_fb.py <png> [addr] [w] [h]"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from xemu_mon import mon
png = sys.argv[1]; addr = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x03710000
w = int(sys.argv[3]) if len(sys.argv) > 3 else 768; h = int(sys.argv[4]) if len(sys.argv) > 4 else 480
raw = png[:-4] + '.fb'
try: os.remove(raw)
except FileNotFoundError: pass
mon(['pmemsave 0x%X %d "%s"' % (addr, w * h * 4, raw.replace(chr(92), '/'))], wait=2.5)
import numpy as np
from PIL import Image
d = np.fromfile(raw, dtype=np.uint8)
if d.size < w * h * 4: print('short read', d.size); sys.exit(1)
d = d[:w * h * 4].reshape(h, w, 4)[:, :, [2, 1, 0]]
Image.fromarray(d[:, :720]).save(png); os.remove(raw); print('saved', png)

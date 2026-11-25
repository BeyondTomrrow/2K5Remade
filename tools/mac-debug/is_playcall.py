"""Exit 0 if a PNG (from snap.sh) shows the play-call layout: the blue
PAGE/formation strip, black above it, and black left of the huddle camera."""
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); w, h = struct.unpack('>II', d[16:24]); raw = b''; p = 8
while p < len(d):
    L = struct.unpack('>I', d[p:p + 4])[0]; t = d[p + 4:p + 8]
    if t == b'IDAT': raw += d[p + 8:p + 8 + L]
    p += 12 + L
raw = zlib.decompress(raw); row = w * 3 + 1
def px(x, y): i = int(y) * row + 1 + int(x) * 3; return tuple(raw[i:i + 3])
blue = px(w * 0.3, h * 0.175)
ok = blue[2] > blue[0] + 40 and sum(px(w * 0.5, h * 0.155)) < 30 \
     and all(sum(px(w * 0.1, h * f)) < 30 for f in (0.6, 0.75, 0.9))
sys.exit(0 if ok else 1)

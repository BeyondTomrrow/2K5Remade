"""The San Francisco Claude logo: an orange spark on a cream disc with a dark ring.

    python tools/team-creator/examples/claude_logo.py out.png
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 1024
CLAY = (217, 119, 87)
DARK = (31, 30, 29)
CREAM = (245, 240, 232)

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
c = S / 2
d.ellipse((24, 24, S - 24, S - 24), fill=DARK + (255,))
d.ellipse((70, 70, S - 70, S - 70), fill=CREAM + (255,))

# the spark: twelve tapered rays with rounded tips, alternating long and short
rays = 12
for i in range(rays):
    a = i * 2 * math.pi / rays - math.pi / 2
    length = 340 if i % 2 == 0 else 255
    w0, w1 = (58, 30) if i % 2 == 0 else (46, 24)
    nx, ny = -math.sin(a), math.cos(a)
    tx, ty = c + length * math.cos(a), c + length * math.sin(a)
    pts = [(c + nx * w0, c + ny * w0), (tx + nx * w1, ty + ny * w1),
           (tx - nx * w1, ty - ny * w1), (c - nx * w0, c - ny * w0)]
    d.polygon(pts, fill=CLAY + (255,))
    d.ellipse((tx - w1, ty - w1, tx + w1, ty + w1), fill=CLAY + (255,))
d.ellipse((c - 95, c - 95, c + 95, c + 95), fill=CLAY + (255,))
img = img.filter(ImageFilter.SMOOTH_MORE)
img.save(sys.argv[1] if len(sys.argv) > 1 else "claude_logo.png")
print("saved")

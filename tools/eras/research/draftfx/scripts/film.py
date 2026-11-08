"""Film strip of SMCD clips: stick figures (front + side) over time and the root trajectory (top view)."""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, struct, dataclasses, math
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools"))
from pathlib import Path
from PIL import Image, ImageDraw
from mod_editor.core import nfl2k5_animation as A
INV = Path(sys.argv[1]); OUT = sys.argv[2]; specs = sys.argv[3:]   # identity:family
src = A.AnimationSource(Path(__import__("os").path.join(ROOT, r"original/disc/vc_53450030/0")), INV)
rows = []
for spec in specs:
    ident, fam = spec.rsplit(':', 1)
    clip = src.load(ident); clip = dataclasses.replace(clip, family=fam)
    sk = src.skeleton(clip); r = clip.roots[0]
    traj = [tuple(v*0.125 for v in struct.unpack_from('<3h', clip.body, r.trajectory + r.stride*f)) for f in range(r.frames)]
    rows.append((clip, sk, r, traj))
N = 7; CW = 110; H = 250
img = Image.new('RGB', (CW*N*2 + 260, H*len(rows)), (25, 25, 28)); d = ImageDraw.Draw(img)
for ri, (clip, sk, r, traj) in enumerate(rows):
    y0 = ri*H
    d.text((4, y0+2), f"{clip.name} ({clip.identity}) {r.frames}f {r.duration:.1f}s  family={clip.family} (forced)", fill=(255,255,0))
    for k in range(N):
        t = r.duration*k/(N-1)*0.999
        for pi, plane in enumerate(('front', 'side')):
            segs = A.project_pose(clip, t, sk, plane=plane, loop=False)
            ox = (k*2+pi)*CW + CW/2; oy = y0 + 150
            for (a, b, name) in segs:
                d.line([(ox+a[0]*0.75, oy-a[1]*0.75), (ox+b[0]*0.75, oy-b[1]*0.75)], fill=(120,200,255) if pi==0 else (255,170,90), width=2)
            if pi == 0: d.text((ox-30, y0+18), f"t={t:.1f}s", fill=(200,200,200))
    # trajectory top view (x, z)
    xs = [p[0] for p in traj]; zs = [p[2] for p in traj]
    span = max(max(xs)-min(xs), max(zs)-min(zs), 50)
    bx = CW*N*2 + 20; by = y0 + 30; sc = 190/span
    d.rectangle([bx, by, bx+200, by+190], outline=(80,80,80))
    pts = [(bx+5+(x-min(xs))*sc, by+5+(z-min(zs))*sc) for x, z in zip(xs, zs)]
    d.line(pts, fill=(120,255,120), width=2); d.ellipse([pts[0][0]-3, pts[0][1]-3, pts[0][0]+3, pts[0][1]+3], fill=(255,80,80))
    dist = sum(math.dist(traj[i], traj[i+1]) for i in range(len(traj)-1))
    d.text((bx, by+192), f"root path {dist:.0f} cm (start red)", fill=(200,200,200))
img.save(OUT)

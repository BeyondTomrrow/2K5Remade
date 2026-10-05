"""Cut the NBC SNF scorebug's fixed artwork out of the reference image.

Input:  mods/presentations/NBC_SNF/reference/reference.png (1400x268 still;
        the bug occupies x 415..985, y 93..181)
Output: mods/presentations/NBC_SNF/assets/plates/*.png, at reference pixels
        (the page scales them; it never redraws them):

  pod.png          clock pod (ring, glass, peacock, divider lines) with the
                   clock and quarter text removed
  rail.png         timeout rail along the bottom edge, bars removed
  away_mask.png,   how strongly the team colour shows at each pixel of each
  home_mask.png    panel (0 = the panel's dark tone, 255 = full team colour),
                   text removed; the page multiplies this with the team colour
  logo_LAR_away.png, logo_CIN_home.png
                   the reference's own logo windows (exact for those teams)

Run with any Python 3 that has Pillow and NumPy:
    python tools/nbc-snf/build_plates.py
"""
import json
import os

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PKG = os.path.join(ROOT, 'mods', 'presentations', 'NBC_SNF')
OUT = os.path.join(PKG, 'assets', 'plates')
ref = np.asarray(Image.open(os.path.join(PKG, 'reference', 'reference.png')).convert('RGB'), dtype=np.float32)

# Bug box (reference pixels): everything below is relative to this origin.
OX, OY, BW, BH = 413, 90, 576, 94
BG = np.array([33, 33, 33], np.float32)


def box(x0, y0, x1, y1):
    return ref[y0:y1, x0:x1].copy()


def save_rgba(arr_rgb, alpha, name):
    a = np.clip(alpha, 0, 1)
    img = np.dstack([np.clip(arr_rgb, 0, 255), a * 255]).astype(np.uint8)
    Image.fromarray(img, 'RGBA').save(os.path.join(OUT, name))


def lum(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def fill_vertical(img, mask):
    """Replace masked pixels column by column, interpolating between the
    nearest unmasked pixels above and below."""
    out = img.copy()
    H, W = mask.shape
    for x in range(W):
        col = mask[:, x]
        y = 0
        while y < H:
            if not col[y]:
                y += 1
                continue
            y0 = y
            while y < H and col[y]:
                y += 1
            a, b = y0 - 1, y
            for yy in range(y0, y):
                if a >= 0 and b < H:
                    t = (yy - a) / (b - a)
                    out[yy, x] = out[a, x] * (1 - t) + out[b, x] * t
                elif a >= 0:
                    out[yy, x] = out[a, x]
                elif b < H:
                    out[yy, x] = out[b, x]
    return out


def fill_horizontal(img, mask):
    return np.transpose(fill_vertical(np.transpose(img, (1, 0, 2)), mask.T), (1, 0, 2))


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def smooth_fill(img, known, knot=8, ydeg=3):
    """Least-squares smooth surface through the known pixels: piecewise
    linear across x (a knot every `knot` px), polynomial in y. Used where
    text has to be removed from a smooth gradient (pod glass, team panels):
    it continues the gradient instead of smearing the text's edges."""
    H, W = known.shape
    knots = np.arange(0, W + knot, knot, dtype=np.float32)
    xs = np.arange(W, dtype=np.float32)
    hat = np.clip(1 - np.abs(xs[:, None] - knots[None, :]) / knot, 0, 1)      # W x K
    ys = (np.arange(H, dtype=np.float32) / max(1, H - 1)) * 2 - 1
    poly = np.stack([ys ** k for k in range(ydeg + 1)], axis=1)               # H x P
    basis = (poly[:, None, :, None] * hat[None, :, None, :]).reshape(H, W, -1)
    A = basis[known]
    # Smoothness: neighbouring knots of the same y term are tied together,
    # so columns with no known pixels follow their neighbours instead of
    # being left unconstrained.
    K, P = len(knots), ydeg + 1
    reg = []
    for p in range(P):
        for k in range(1, K - 1):
            row = np.zeros(P * K, np.float32)
            row[p * K + k - 1], row[p * K + k], row[p * K + k + 1] = 1, -2, 1
            reg.append(row)
    lam = 4.0
    A2 = np.vstack([A, lam * np.array(reg)])
    out = img.copy()
    for c in range(img.shape[2]):
        b2 = np.concatenate([img[..., c][known], np.zeros(len(reg), np.float32)])
        coef, *_ = np.linalg.lstsq(A2, b2, rcond=None)
        fit = basis @ coef
        out[..., c] = np.where(known, img[..., c], fit)
    return out


os.makedirs(OUT, exist_ok=True)
meta = {}

# ---- clock pod ---------------------------------------------------------
# Ellipse fitted to the ring: x 653..747, y 93..180.
PX0, PY0, PX1, PY1 = 650, 90, 751, 184
pod = box(PX0, PY0, PX1, PY1)
cx, cy, rx, ry = 700.0 - PX0, 136.5 - PY0, 47.5, 44.5
yy, xx = np.mgrid[0:pod.shape[0], 0:pod.shape[1]].astype(np.float32)
d = np.sqrt(((xx + 0.5 - cx) / rx) ** 2 + ((yy + 0.5 - cy) / ry) ** 2)
alpha = np.clip((1.0 - d) * min(rx, ry) + 0.5, 0, 1)        # 1 px soft edge
# Clock text 15:00 (x 664..738, y 129..143) and quarter 2ND (x 684..716,
# y 155..169, underline included) with their shadows.
#
# The glass between the two divider lines (rows 125..149) and below the
# lower one (rows 151..173) is a smooth gradient; text is removed by fitting
# that gradient to the glass pixels around it.
# Divider lines are rows 124 and 150; glass rows 125-127 / 146-149 and
# 151-153 / 171-174 stay as they are and anchor the fit.
for (y0, y1), (tx0, tx1, ty0, ty1) in [((125, 150), (658, 745, 127, 148)),
                                         ((151, 175), (680, 721, 154, 171))]:
    band = pod[y0 - PY0:y1 - PY0]
    inside = d[y0 - PY0:y1 - PY0] < 0.86                     # glass, not ring
    text = np.zeros(band.shape[:2], bool)
    text[ty0 - y0:ty1 - y0, tx0 - PX0:tx1 - PX0] = True
    # Glass visible between the glyphs: dark pixels at least 2 px from any
    # glyph pixel (the glyphs and their anti-aliased edges are bright).
    glyph = lum(band) > 75
    gap = text & ~dilate(glyph, 2)
    known = inside & (~text | gap)
    filled = smooth_fill(band, known, knot=10, ydeg=2)
    pod[y0 - PY0:y1 - PY0] = np.where((inside & text)[..., None], filled, band)
save_rgba(pod, alpha, 'pod.png')
meta['pod'] = {'x': PX0 - OX, 'y': PY0 - OY, 'w': PX1 - PX0, 'h': PY1 - PY0}

# ---- timeout rail ------------------------------------------------------
# Rows 163..170 under both panels; the bars (white or grey) are removed.
RX0, RY0, RX1, RY1 = 415, 163, 986, 171
rail = box(RX0, RY0, RX1, RY1)
bars = np.zeros(rail.shape[:2], bool)
for x0, x1 in [(462, 556), (843, 937)]:
    bars[165 - RY0:169 - RY0, x0 - RX0:x1 - RX0] = True
rail = fill_horizontal(rail, dilate(bars, 1))
# Only the bottom rows are rail; the rows above blend into the panel.
ra = np.ones(rail.shape[:2], np.float32)
ra[0], ra[1] = 0.35, 0.7
save_rgba(rail, ra, 'rail.png')
meta['rail'] = {'x': RX0 - OX, 'y': RY0 - OY, 'w': RX1 - RX0, 'h': RY1 - RY0}

# ---- team colour masks -------------------------------------------------
# A panel pixel is modelled as dark + a * (team - dark). The reference's
# dark and full team tones were measured from the panels.
TEAM = {
    'away': {'x0': 476, 'x1': 652, 'dark': [26, 30, 46], 'team': [31, 79, 174], 'divider': 564},   # LAR
    'home': {'x0': 749, 'x1': 932, 'dark': [42, 33, 39], 'team': [218, 84, 27], 'divider': 832},   # CIN
}
PY_TOP, PY_BOT = 113, 165          # rows 113-114 are the lighter top rim
for side, t in TEAM.items():
    seg = box(t['x0'], PY_TOP, t['x1'], PY_BOT)
    # Text with its shadow, and the divider line, are not panel colour.
    white = seg.min(axis=2) > 150
    m = dilate(white, 5)
    dv = t['divider'] - t['x0']
    m[:, dv - 1:dv + 3] = True
    seg_f = smooth_fill(seg, ~m, knot=8, ydeg=3)
    D = np.array(t['dark'], np.float32)
    T = np.array(t['team'], np.float32)
    v = T - D
    a = np.clip(((seg_f - D) * v).sum(axis=2) / (v * v).sum(), 0, 1)
    model = D + a[..., None] * v
    err = np.abs(model - seg_f).mean()
    # Alpha mask (white, opacity = a) for CSS mask-image.
    m8 = (a * 255 + 0.5).astype(np.uint8)
    Image.fromarray(np.dstack([np.full_like(m8, 255)] * 3 + [m8]), 'RGBA').save(os.path.join(OUT, side + '_mask.png'))
    meta[side + '_mask'] = {'x': t['x0'] - OX, 'y': PY_TOP - OY, 'w': t['x1'] - t['x0'], 'h': PY_BOT - PY_TOP,
                            'dark': t['dark'], 'team': t['team'], 'mean_abs_error': round(float(err), 2)}

# ---- logo windows for the reference teams ------------------------------
for name, (x0, x1) in {'logo_LAR_away': (415, 478), 'logo_CIN_home': (930, 986)}.items():
    win = box(x0, 113, x1, 165)
    a = np.ones(win.shape[:2], np.float32)
    # outer column next to the background is half covered
    if x0 == 415:
        a[:, 0] = 0.6
    else:
        a[:, -1] = 0.6
    save_rgba(win, a, name + '.png')
    meta[name] = {'x': x0 - OX, 'y': 113 - OY, 'w': x1 - x0, 'h': 52}

with open(os.path.join(OUT, 'plates.json'), 'w') as f:
    json.dump(meta, f, indent=2)
print(json.dumps(meta, indent=2))

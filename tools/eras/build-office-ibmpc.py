"""The 1980s office desk: an IBM PC 5150, a multi-line desk phone and the period wall pieces.

    python -I tools/eras/build-office-ibmpc.py            (also run by build-office-pack.py)
    python -I tools/eras/build-office-ibmpc.py --layout   print the desk layout check only

A copy of the Coach's Desk scene with props reshaped, written as a fifth record of
mods/eras/office/files/erasoffice.iff under the scene name "coach_d80s" (same length as
"coach_desk"). The 1980s office look uses it (mods/eras/offices/1980s/era.ini [room]); the
retail scene, Franchise's fr.iff and the other looks are untouched.

Every change is a same-topology edit (RETEX option E): vertex counts, triangles and every other
byte of the scene stay as they are; positions, texture coordinates, vertex colours and submesh
material indices change.

* monitor_group1 (the flat panel, 376 vertices) becomes the IBM PC: the panel is a deep putty
  CRT (IBM 5151) sitting on a wide, low system unit (the stand's base plate), and the stand's
  neck becomes the keyboard's coiled cable. The whole computer is placed and turned on the desk
  so that it clears the day planner, the legal pad and the notebook (2026-10-16: the first
  version's system unit sat on the day planner and its keyboard on the legal pad), keeping the
  screen where the Standings camera stop frames it.
* teamwallpaper (the screen quad: the made green-phosphor terminal) moves into the CRT's opening.
* mousepad becomes the keyboard: a separate, thick beige IBM Model F, sloped, in front of the unit.
* phone_group1 (the cordless phone and its base) becomes a beige multi-line desk phone: a wedge
  body, a keypad with a row of line buttons, the handset lying in its cradle, a coiled cord.
* wall_pic01's picture (the large frame) becomes a framed 1980 schedule poster, wall_pennant a
  felt pennant.
* notebook_logo (the notebook sticker quad, hidden in the 1980s since the sticker shares the
  screen texture) becomes an amber glass ashtray on the right of the desk.
* room_walls' vertex colours are lifted so the made wood paneling (office_atlas.png) reads.

The reshaped props take the monitor's material (vcSwShader1); its texture is the 1024x1024 atlas
written here as mods/eras/offices/1980s/computer_monitor.png (used only while the 1980s office
shows): crops of the user's photo (mods/eras/offices/1980s/reference/ibm_pc_5150.jpg: monitor
bezel, drive bay, keyboard, perspective-corrected) and made art (putty surfaces, the phone, the
cords, the poster, the pennant, the ashtray; shapes and Windows fonts only, no retail pixels).

Needs 2K Football Mod Tools in .tmp-2k-football-mod-tools (scene decoding and its VC-LZ
re-encoder); without them the 1980s look keeps the retail-shaped desk.
"""
import math, os, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MODTOOLS = os.path.join(ROOT, '.tmp-2k-football-mod-tools')
PACK = os.path.join(ROOT, 'mods', 'eras', 'office', 'files', 'erasoffice.iff')
ERA = os.path.join(ROOT, 'mods', 'eras', 'offices', '1980s')
PHOTO = os.path.join(ERA, 'reference', 'ibm_pc_5150.jpg')
FONTS = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
NEW_NAME = 'coach_d80s'
MAT_PROP = 'vcSwShader1'

# ---- the prop atlas (u0, v0, u1, v1 in 0..1, v down the image) ------------------------------
R_MON_FRONT = (0.00, 0.00, 0.50, 0.40)
R_MON_SIDE = (0.50, 0.00, 0.75, 0.20)
R_UNIT_SIDE = (0.75, 0.00, 1.00, 0.20)
R_KEY_SIDE = (0.50, 0.20, 0.75, 0.30)
R_CABLE = (0.75, 0.20, 1.00, 0.25)
R_CORD = (0.75, 0.25, 1.00, 0.30)
R_PHONE_SIDE = (0.50, 0.30, 0.75, 0.40)
R_HSET_SIDE = (0.75, 0.30, 1.00, 0.40)
R_UNIT_FRONT = (0.00, 0.40, 0.50, 0.55)
R_PHONE_TOP = (0.50, 0.40, 0.75, 0.60)
R_PHONE_KEYS = (0.75, 0.40, 1.00, 0.60)
R_KEYS = (0.00, 0.55, 0.50, 0.75)
R_HSET_TOP = (0.50, 0.60, 0.75, 0.70)
R_ASHTRAY = (0.75, 0.60, 1.00, 0.85)
R_DARK = (0.50, 0.70, 0.75, 0.75)
R_PENNANT = (0.00, 0.75, 0.20, 1.00)
R_POSTER = (0.20, 0.75, 0.45, 1.00)

# ---- the desk layout (desk-local frame from the retail screen quad: u right, v up, w toward the
# chair; centimetre-like scene units). Checked by --layout against the day planner, the legal
# pad, the notebook, the lamp, the mug and the desk's outline.
PC_AT = (52.0, -14.0)          # front-centre of the system unit on the desk (u, w)
PC_YAW = 14.0                  # degrees; the computer turns toward the chair (front -> -u)
UNIT_W, UNIT_H, UNIT_D = 350.0, 98.0, 285.0
CRT_SCALE = 0.76               # the flat panel's front ring -> the CRT's face (268 x 200)
CRT_SETBACK = 18.0             # the CRT's face sits a little behind the unit's front
CRT_DEPTH = 255.0
KEYB = (-140.0, 140.0, 34.0, 136.0, 11.0, 30.0)   # x0, x1, z0, z1, front height, back height
# the keyboard's coiled cable lies on the desk from the keyboard's back-left corner to under the
# system unit's front-left (prop-local x, y, z)
CABLE_A = (KEYB[0] + 26.0, 3.6, KEYB[2] + 3.0)
CABLE_B = (-UNIT_W / 2 + 34.0, 3.6, -12.0)
CABLE_R = 3.8
PHONE_AT = (-464.0, 300.0)
PHONE_YAW = 10.0               # turned a little toward the lamp, to clear it and the mug
PHONE_W, PHONE_D, PHONE_HF, PHONE_HB = 118.0, 132.0, 22.0, 52.0
ASHTRAY_AT = (268.0, 72.0)
ASHTRAY_SIZE = 78.0
WALL_LIFT = 1.9                # room_walls vertex colour multiplier (the paneling is dark wood)
LAMP_BRASS = (1.02, 0.80, 0.44)  # new_lamp_group vertex colour multiplier: a brass desk lamp


def records(d):
    o = 0
    while o + 0x20 <= len(d):
        tag = d[o:o + 4]
        if not all(65 <= c <= 90 for c in tag):
            break
        size = struct.unpack_from('<I', d, o + 4)[0]
        yield o, tag.decode(), d[o:o + 0x20 + size]
        o += 0x20 + size
        o = (o + 15) & ~15


def font(name, size):
    for n in (name, 'arial.ttf'):
        try:
            return ImageFont.truetype(os.path.join(FONTS, n), size)
        except OSError:
            pass
    return ImageFont.load_default()


# ---- the atlas -----------------------------------------------------------------------------

def quad_crop(photo, quad, size):
    """quad = TL, TR, BR, BL in photo pixels -> a size[0] x size[1] frontal image."""
    tl, tr, br, bl = quad
    return photo.transform(size, Image.QUAD, data=(*tl, *bl, *br, *tr), resample=Image.BICUBIC)


def plastic(size, seed, base=(205, 201, 186), shade=1.0, grain=3.0):
    w, h = size
    rng = np.random.default_rng(seed)
    b = np.array(base, np.float32) * shade
    n = rng.normal(0, grain, (h, w, 1)).astype(np.float32)
    im = np.clip(b + n, 0, 255).astype(np.uint8)
    return Image.fromarray(im, 'RGB').filter(ImageFilter.GaussianBlur(0.6))


def coil(size, base, dark, turns):
    """A coiled cord seen from the side: bright and dark bands across its length (u)."""
    w, h = size
    im = Image.new('RGB', (w, h), base)
    d = ImageDraw.Draw(im)
    step = w / turns
    for k in range(turns):
        x = k * step
        d.rectangle([x + step * 0.55, 0, x + step * 0.95, h], fill=dark)
        d.line([(x + step * 0.2, 0), (x + step * 0.2, h)], fill=tuple(min(255, c + 30) for c in base), width=max(1, int(step * 0.12)))
    return im


def phone_top(size):
    """The multi-line phone's sloped top: almond plastic, a row of six clear line buttons with
    lit lamps along the front, a hold button, the number card."""
    w, h = size
    im = plastic(size, 21, (214, 204, 178), 1.0, 2.0)
    d = ImageDraw.Draw(im)
    # line buttons along the front edge (bottom of the region = front of the phone)
    y0, y1 = int(h * 0.80), int(h * 0.93)
    for k in range(6):
        x0 = int(w * (0.08 + k * 0.145))
        lit = k in (0, 2)
        d.rectangle([x0, y0, x0 + int(w * 0.11), y1], fill=(250, 246, 230) if not lit else (255, 214, 120),
                    outline=(120, 112, 96), width=2)
    d.rectangle([int(w * 0.08), int(h * 0.70), int(w * 0.19), int(h * 0.77)], fill=(190, 40, 34), outline=(110, 30, 20), width=2)
    d.rectangle([int(w * 0.30), int(h * 0.70), int(w * 0.92), int(h * 0.77)], fill=(246, 242, 228), outline=(140, 130, 110), width=2)
    d.text((int(w * 0.33), int(h * 0.705)), 'EXT 1980', font=font('arial.ttf', max(8, int(h * 0.05))), fill=(60, 50, 40))
    # the handset cradle along the back: a darker recess
    d.rounded_rectangle([int(w * 0.03), int(h * 0.03), int(w * 0.97), int(h * 0.33)], radius=int(h * 0.05), fill=(186, 176, 152))
    # the keypad, right of centre
    kp = phone_keys((int(w * 0.42), int(h * 0.34)))
    im.paste(kp, (int(w * 0.52), int(h * 0.35)))
    d.rectangle([int(w * 0.52), int(h * 0.35), int(w * 0.94), int(h * 0.69)], outline=(150, 140, 116), width=2)
    # a speaker grille, left of the keypad
    for k in range(7):
        y = int(h * (0.40 + k * 0.04))
        d.line([(int(w * 0.10), y), (int(w * 0.42), y)], fill=(120, 112, 96), width=max(2, h // 120))
    return im


def phone_keys(size):
    """The keypad panel: 12 square push buttons with digits and letters."""
    w, h = size
    im = plastic(size, 22, (206, 196, 170), 1.0, 2.0)
    d = ImageDraw.Draw(im)
    f = font('arialbd.ttf', max(10, int(h * 0.12)))
    fs = font('arial.ttf', max(6, int(h * 0.045)))
    keys = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#']
    letters = ['', 'ABC', 'DEF', 'GHI', 'JKL', 'MNO', 'PRS', 'TUV', 'WXY', '', 'OPER', '']
    for i, (k, l) in enumerate(zip(keys, letters)):
        cx = int(w * (0.22 + (i % 3) * 0.28))
        cy = int(h * (0.16 + (i // 3) * 0.225))
        r = int(min(w, h) * 0.105)
        d.rounded_rectangle([cx - r, cy - r, cx + r, cy + r], radius=r // 4, fill=(244, 240, 228), outline=(130, 120, 100), width=2)
        tw = d.textlength(k, font=f)
        d.text((cx - tw / 2, cy - r * 0.85), k, font=f, fill=(40, 34, 28))
        if l:
            tw = d.textlength(l, font=fs)
            d.text((cx - tw / 2, cy + r * 0.35), l, font=fs, fill=(90, 80, 70))
    return im


def handset_top(size):
    w, h = size
    im = plastic(size, 23, (216, 206, 180), 1.0, 2.0)
    d = ImageDraw.Draw(im)
    for cx in (0.13, 0.87):                       # earpiece and mouthpiece cups (along u)
        x = int(w * cx)
        r = int(h * 0.34)
        d.ellipse([x - r, h // 2 - r, x + r, h // 2 + r], fill=(200, 190, 164), outline=(160, 150, 126), width=2)
        for a in range(8):
            px = x + int(r * 0.5 * math.cos(a * math.pi / 4))
            py = h // 2 + int(r * 0.5 * math.sin(a * math.pi / 4))
            d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(110, 100, 86))
    return im


def ashtray(size):
    """An amber glass ashtray from above: square, four rests, a stubbed cigarette."""
    w, h = size
    im = Image.new('RGB', (w, h), (120, 70, 24))
    d = ImageDraw.Draw(im)
    for k in range(w // 2):
        t = k / (w / 2)
        c = (int(196 - 70 * t), int(120 - 50 * t), int(40 - 20 * t))
        d.rectangle([k, k, w - 1 - k, h - 1 - k], outline=c)
    m = int(w * 0.22)
    d.rounded_rectangle([m, m, w - m, h - m], radius=int(w * 0.08), fill=(96, 52, 18))
    for (cx, cy) in ((0.5, 0.04), (0.5, 0.96), (0.04, 0.5), (0.96, 0.5)):
        x, y = int(w * cx), int(h * cy)
        d.ellipse([x - w // 18, y - h // 18, x + w // 18, y + h // 18], fill=(70, 38, 12))
    d.ellipse([int(w * 0.36), int(h * 0.44), int(w * 0.52), int(h * 0.60)], fill=(120, 116, 110))   # ash
    d.line([(int(w * 0.46), int(h * 0.52)), (int(w * 0.88), int(h * 0.30))], fill=(240, 236, 226), width=max(3, w // 22))
    d.line([(int(w * 0.75), int(h * 0.37)), (int(w * 0.88), int(h * 0.30))], fill=(196, 140, 70), width=max(3, w // 22))
    return im


def pennant(size):
    """A felt pennant hanging point down (drawn at the pennant's own 1:3 shape): burgundy felt,
    a cream sleeve at the top, gold lettering down its length."""
    W, H = 240, 720
    im = Image.new('RGB', (W, H), (122, 26, 32))
    d = ImageDraw.Draw(im)
    rng = np.random.default_rng(31)
    for _ in range(2500):                             # felt nap
        x, y = int(rng.integers(0, W)), int(rng.integers(0, H))
        c = int(rng.integers(-14, 14))
        d.point((x, y), fill=(122 + c, 26 + c // 2, 32 + c // 2))
    d.rectangle([0, 0, W, int(H * 0.09)], fill=(236, 226, 196))
    txt = Image.new('RGBA', (900, 260), (0, 0, 0, 0))
    td = ImageDraw.Draw(txt)
    td.text((10, 0), 'CHAMPIONS', font=font('impact.ttf', 200), fill=(234, 188, 62))
    txt = txt.crop(txt.getbbox()).rotate(-90, expand=True)
    k = min(W * 0.62 / txt.width, H * 0.66 / txt.height)
    tw, th = int(txt.width * k), int(txt.height * k)
    txt = txt.resize((tw, th), Image.LANCZOS)
    im.paste(txt, ((W - tw) // 2, int(H * 0.13)), txt)
    return im.resize(size, Image.LANCZOS)


def poster(size):
    """A framed 1980 schedule poster (period type, harvest gold and brown)."""
    W, H = 600, 960                                 # drawn at the picture's own aspect
    im = Image.new('RGB', (W, H), (238, 226, 192))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 190], fill=(120, 58, 22))
    d.rectangle([0, 190, W, 214], fill=(214, 156, 40))
    fb = font('impact.ttf', 120)
    tw = d.textlength('1980', font=fb)
    d.text(((W - tw) / 2, 4), '1980', font=fb, fill=(246, 214, 120))
    f2 = font('impact.ttf', 52)
    tw = d.textlength('SCHEDULE', font=f2)
    d.text(((W - tw) / 2, 136), 'SCHEDULE', font=f2, fill=(246, 236, 210))
    games = [('SEPT 7', 'AT DALLAS'), ('SEPT 14', 'DENVER'), ('SEPT 21', 'AT MIAMI'), ('SEPT 28', 'CHICAGO'),
             ('OCT 5', 'AT HOUSTON'), ('OCT 12', 'MINNESOTA'), ('OCT 19', 'AT ATLANTA'), ('OCT 26', 'SEATTLE'),
             ('NOV 2', 'AT BUFFALO'), ('NOV 9', 'DETROIT'), ('NOV 16', 'AT ST. LOUIS'), ('NOV 23', 'CLEVELAND'),
             ('NOV 30', 'AT OAKLAND'), ('DEC 7', 'NEW ENGLAND'), ('DEC 14', 'AT SAN DIEGO'), ('DEC 21', 'PITTSBURGH')]
    f = font('arialbd.ttf', 33)
    fs = font('arial.ttf', 30)
    y = 232
    for k, (dt, opp) in enumerate(games):
        if k % 2:
            d.rectangle([24, y - 4, W - 24, y + 40], fill=(226, 208, 164))
        d.text((40, y), dt, font=f, fill=(110, 50, 18))
        d.text((230, y), opp, font=fs, fill=(50, 34, 20))
        y += 44
    d.rectangle([0, H - 40, W, H], fill=(120, 58, 22))
    d.text((40, H - 36), 'ALL GAMES 1:00 PM  -  RADIO 1070 AM', font=font('arial.ttf', 24), fill=(246, 236, 210))
    # age: a faint yellowing toward the edges
    a = np.asarray(im).astype(np.float32)
    yy, xx = np.mgrid[0:H, 0:W]
    edge = np.maximum(np.abs(xx / W - 0.5), np.abs(yy / H - 0.5)) * 2
    a *= (1 - 0.18 * edge ** 3)[:, :, None]
    a[:, :, 2] *= 0.92
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize(size, Image.LANCZOS)


def build_atlas(photo):
    S = 1024
    at = Image.new('RGB', (S, S), (196, 190, 172))

    def box(r):
        return int(r[0] * S), int(r[1] * S), int(r[2] * S), int(r[3] * S)

    def put(img, r):
        x0, y0, x1, y1 = box(r)
        at.paste(img.resize((x1 - x0, y1 - y0), Image.LANCZOS), (x0, y0))

    def size(r):
        x0, y0, x1, y1 = box(r)
        return x1 - x0, y1 - y0

    # the computer, from the user's photo (frontal crops) and made putty
    put(quad_crop(photo, ((205, 26), (684, 30), (695, 366), (236, 436)), size(R_MON_FRONT)), R_MON_FRONT)
    put(plastic(size(R_MON_SIDE), 1, shade=0.97), R_MON_SIDE)
    put(quad_crop(photo, ((152, 490), (790, 388), (796, 556), (160, 690)), size(R_UNIT_FRONT)), R_UNIT_FRONT)
    put(plastic(size(R_UNIT_SIDE), 2, shade=0.95), R_UNIT_SIDE)
    put(quad_crop(photo, ((214, 706), (806, 566), (1014, 690), (328, 934)), size(R_KEYS)), R_KEYS)
    put(plastic(size(R_KEY_SIDE), 3, shade=0.92), R_KEY_SIDE)
    put(coil(size(R_CABLE), (54, 54, 52), (18, 18, 18), 26), R_CABLE)
    # the phone
    put(phone_top(size(R_PHONE_TOP)), R_PHONE_TOP)
    put(phone_keys(size(R_PHONE_KEYS)), R_PHONE_KEYS)
    put(plastic(size(R_PHONE_SIDE), 24, (196, 186, 160), 0.95), R_PHONE_SIDE)
    put(handset_top(size(R_HSET_TOP)), R_HSET_TOP)
    put(plastic(size(R_HSET_SIDE), 25, (204, 194, 168), 0.95), R_HSET_SIDE)
    put(coil(size(R_CORD), (210, 198, 170), (150, 140, 116), 30), R_CORD)
    # the wall and the desk
    put(ashtray(size(R_ASHTRAY)), R_ASHTRAY)
    put(pennant(size(R_PENNANT)), R_PENNANT)
    put(poster(size(R_POSTER)), R_POSTER)
    d = ImageDraw.Draw(at)
    d.rectangle(box(R_DARK), fill=(52, 54, 52))
    return at


# ---- geometry helpers ----------------------------------------------------------------------

def frame_from_screen(P):
    """Local axes of the desk from the retail screen quad: u right, v up, w toward the viewer."""
    c = P.mean(0)
    xs = P[:, [0, 2]]
    i0 = int(np.argmin(xs[:, 0])); i1 = int(np.argmax(xs[:, 0]))
    u = np.array([xs[i1, 0] - xs[i0, 0], 0.0, xs[i1, 1] - xs[i0, 1]])
    u /= np.linalg.norm(u)
    v = np.array([0.0, 1.0, 0.0])
    w = np.cross(u, v)
    return c, u, v, w


def to_local(P, fr):
    c, u, v, w = fr
    return np.stack([(P - c) @ u, (P - c) @ v, (P - c) @ w], 1)


def to_world(L, fr):
    c, u, v, w = fr
    return c + L[:, :1] * u + L[:, 1:2] * v + L[:, 2:3] * w


def place(Q, at, yaw_deg, desk):
    """Prop-local (x right, y up from the desk, z toward the front) -> desk-local (u, v, w),
    turned by yaw (positive turns the front toward -u)."""
    t = math.radians(yaw_deg)
    c, s = math.cos(t), math.sin(t)
    out = np.empty_like(Q)
    out[:, 0] = at[0] + Q[:, 0] * c - Q[:, 2] * s
    out[:, 1] = desk + Q[:, 1]
    out[:, 2] = at[1] + Q[:, 0] * s + Q[:, 2] * c
    return out


def components(n, tris, P):
    """Connected parts of a mesh (shared vertex or the same position)."""
    par = list(range(n))

    def f(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for a, b, c in tris:
        par[f(a)] = f(b)
        par[f(b)] = f(c)
    seen = {}
    for i in range(n):
        k = tuple(np.round(P[i], 1))
        if k in seen:
            par[f(i)] = f(seen[k])
        else:
            seen[k] = i
    groups = {}
    for i in range(n):
        groups.setdefault(f(i), []).append(i)
    return sorted(groups.values(), key=len)


def pca_unit(Q):
    """A part's coordinates in its own principal axes (longest first, a proper rotation), each
    scaled to 0..1."""
    c = Q.mean(0)
    _, _, vt = np.linalg.svd(Q - c, full_matrices=False)
    if np.linalg.det(vt) < 0:
        vt[2] = -vt[2]
    L = (Q - c) @ vt.T
    lo, hi = L.min(0), L.max(0)
    return (L - lo) / np.maximum(hi - lo, 1e-6)


def yaw_frame(Q):
    """Footprint coordinates (a, b) of a part turned about the vertical so that its minimum-area
    bounding rectangle is axis-aligned (a proper turn)."""
    P = Q[:, [0, 2]]
    best = None
    for deg in np.arange(0.0, 90.0, 0.5):
        t = math.radians(deg)
        a = P[:, 0] * math.cos(t) + P[:, 1] * math.sin(t)
        b = -P[:, 0] * math.sin(t) + P[:, 1] * math.cos(t)
        area = np.ptp(a) * np.ptp(b)
        if best is None or area < best[0]:
            best = (area, a, b)
    return best[1], best[2]


def box_map(Q, src_lo, src_hi, dst_lo, dst_hi):
    f = (Q - src_lo) / np.maximum(src_hi - src_lo, 1e-6)
    return dst_lo + f * (dst_hi - dst_lo)


def tube_map(Q, a, b, radius):
    """A part (any shape) -> a thin tube from a to b: its longest axis runs along the tube."""
    lo, hi = Q.min(0), Q.max(0)
    order = np.argsort(-(hi - lo))                    # longest, middle, shortest source axis
    f = (Q - lo) / np.maximum(hi - lo, 1e-6) - 0.5
    a, b = np.asarray(a, float), np.asarray(b, float)
    ax = b - a
    ax /= np.linalg.norm(ax)
    side = np.cross(ax, [0.0, 1.0, 0.0])
    if np.linalg.norm(side) < 1e-6:
        side = np.array([1.0, 0.0, 0.0])
    side /= np.linalg.norm(side)
    up = np.cross(side, ax)
    # keep the source's handedness so the faces keep facing out
    src = np.eye(3)[order]
    if np.linalg.det(src) * np.linalg.det(np.stack([ax, side, up])) < 0:
        up = -up
    L = np.linalg.norm(b - a)
    out = (a + (b - a) * 0.5)[None, :] + f[:, order[0]:order[0] + 1] * L * ax \
        + f[:, order[1]:order[1] + 1] * 2 * radius * side + f[:, order[2]:order[2] + 1] * 2 * radius * up
    return out


# ---- the computer --------------------------------------------------------------------------

def morph_monitor(L, neck_set=frozenset()):
    """The flat panel + stand (desk-local) -> the IBM PC in prop-local coordinates (x right,
    y up from the desk, z toward the front, origin at the unit's front-centre). Returns new
    positions and a part label per vertex ('crt', 'unit', 'neck')."""
    out = L.copy()
    part = []
    desk = L[:, 1].min()
    hw = UNIT_W / 2
    for i, (u, v, w) in enumerate(L):
        if i in neck_set:
            out[i] = (u, v, w)                        # replaced below by the cable
            part.append('neck')
        elif v < -170:                                # the base plate -> the system unit
            nx = u / 102.0 * hw
            nz = -UNIT_D + (w + 139.0) / 185.0 * UNIT_D
            ny = 0.0 if v < -195 else UNIT_H
            out[i] = (nx, ny, nz)
            part.append('unit')
        else:                                         # the panel -> the CRT housing
            s = CRT_SCALE
            nx = (u - 4.0) * s
            ny = UNIT_H + (v + 139.0) * s
            if w >= -8.0:
                nz = -CRT_SETBACK
            else:
                cy = UNIT_H + 132.0 * s
                nx *= 0.82
                ny = cy + (ny - cy) * 0.80
                nz = -CRT_SETBACK - CRT_DEPTH
            out[i] = (nx, ny, nz)
            part.append('crt')
    return out, part


def morph_screen(n, mon_new, part):
    crt = mon_new[np.array(part) == 'crt']
    front = crt[crt[:, 2] > -CRT_SETBACK - 1]
    x0, x1 = front[:, 0].min(), front[:, 0].max()
    y0, y1 = front[:, 1].min(), front[:, 1].max()
    return x0, x1, y0, y1


def morph_keyboard(L):
    """The mouse pad (a flat rounded slab) -> the keyboard, prop-local."""
    out = L.copy()
    lo, hi = L.min(0), L.max(0)
    top = L[:, 1] > (lo[1] + hi[1]) / 2
    x0, x1, z0, z1, hf, hb = KEYB
    for i, (u, v, w) in enumerate(L):
        fu = (u - lo[0]) / (hi[0] - lo[0])
        fw = (w - lo[2]) / (hi[2] - lo[2])
        nx = x0 + fu * (x1 - x0)
        nz = z0 + fw * (z1 - z0)
        ny = (hf + (1.0 - fw) * (hb - hf)) if top[i] else 0.3
        out[i] = (nx, ny, nz)
    return out


def face_uvs(W, tris, n_vert, regions, only=None):
    """Per-vertex UV from the final faces: each vertex takes the class of its first triangle and
    is mapped planar into that class's atlas region."""
    cls = [None] * n_vert
    used = sorted({x for t in tris for x in t})
    centre = (W[used].max(0) + W[used].min(0)) / 2
    for a, b, c in tris:
        nrm = np.cross(W[b] - W[a], W[c] - W[a])
        if np.linalg.norm(nrm) < 1e-6:
            continue
        k = int(np.argmax(np.abs(nrm)))
        m = (W[a] + W[b] + W[c]) / 3
        key = (k, 1 if m[k] >= centre[k] else -1)
        for x in (a, b, c):
            if cls[x] is None:
                cls[x] = key
    uv = np.zeros((n_vert, 2))
    shade = np.ones(n_vert)
    groups = {}                                       # one planar map per (region, axes, flip)
    for i in used:
        region, axes, s, flip = regions(cls[i])
        groups.setdefault((region, axes, flip), []).append(i)
        shade[i] = s
    for (region, axes, flip), idx in groups.items():
        P = W[idx]
        a0, a1 = axes
        mn, mx = P[:, [a0, a1]].min(0), P[:, [a0, a1]].max(0)
        span = np.maximum(mx - mn, 1e-6)
        f = (P[:, [a0, a1]] - mn) / span
        if flip[0]:
            f[:, 0] = 1 - f[:, 0]
        if flip[1]:
            f[:, 1] = 1 - f[:, 1]
        r = region
        uv[idx, 0] = r[0] + 0.003 + f[:, 0] * (r[2] - r[0] - 0.006)
        uv[idx, 1] = r[1] + 0.003 + f[:, 1] * (r[3] - r[1] - 0.006)
    return uv, shade, used


def side_regions(top, side, front=None, front_axes=(0, 1)):
    """Region picker for a box-like part in prop-local axes (0 x, 1 y up, 2 z front)."""
    def regions(k):
        ax, sg = k if k else (1, 1)
        if ax == 1 and sg > 0:
            return top, (0, 2), 1.0, (False, False)
        if ax == 2 and sg > 0 and front is not None:
            return front, front_axes, 0.95, (False, True)
        if ax == 1:
            return side, (0, 2), 0.45, (False, False)
        if ax == 0:
            return side, (2, 1), 0.80, (sg < 0, True)
        return side, (0, 1), 0.70 if sg < 0 else 0.9, (True, True)
    return regions


# ---- the build -----------------------------------------------------------------------------

def gltf_tris(M, src, key):
    """Triangles per node and submesh, as source vertex indices (from the exporter's glTF)."""
    import json, pathlib, tempfile
    tmp = tempfile.mkdtemp(prefix='erasdesk')
    M.export_model(src, key, pathlib.Path(tmp) / 'd.gltf')
    g = json.load(open(os.path.join(tmp, 'd.gltf')))
    binp = open(os.path.join(tmp, 'd.bin'), 'rb').read()

    def acc(i):
        a = g['accessors'][i]; bv = g['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        dt = {5126: 'f', 5123: 'H', 5125: 'I', 5121: 'B', 5122: 'h'}[a['componentType']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        st = bv.get('byteStride', 0) or n * struct.calcsize(dt)
        return np.array([struct.unpack_from('<' + dt * n, binp, off + k * st) for k in range(a['count'])])

    mats = [m['name'] for m in g['materials']]
    out = {}
    for n in g['nodes']:
        if 'mesh' not in n:
            continue
        lst = out.setdefault(n['name'], [])
        for p in g['meshes'][n['mesh']]['primitives']:
            idx = acc(p['indices']).ravel()
            src_i = acc(p['attributes']['_NFL_VERTEX_INDEX']).ravel().round().astype(int)
            if p.get('mode', 4) == 5:
                tt = [(idx[k], idx[k + 1], idx[k + 2]) if k % 2 == 0 else (idx[k + 1], idx[k], idx[k + 2]) for k in range(len(idx) - 2)]
            else:
                tt = [tuple(idx[k:k + 3]) for k in range(0, len(idx) - 2, 3)]
            lst.append((mats[p['material']], [tuple(int(src_i[x]) for x in t) for t in tt if len(set(t)) == 3]))
    return out


def build_scene(M, src, key, decoded, scene, rename=True, report=None):
    out = bytearray(decoded)
    shapes = {s['name']: s for s in scene['shapes']}
    mats = [m['name'] for m in scene['materials']]
    mi = mats.index(MAT_PROP)
    tri = gltf_tris(M, src, key)

    def positions(name):
        s = shapes[name]
        lanes = M._shape_lanes(scene, s, decoded)
        return s, lanes, np.array(M.read_positions(decoded, s, lanes))

    def all_tris(name):
        return [t for _m, tt in tri[name] for t in tt]

    sw, sw_l, sw_p = positions('teamwallpaper')
    fr = frame_from_screen(sw_p)
    mon, mon_l, mon_p = positions('monitor_group1')
    Lm = to_local(mon_p, fr)
    desk = Lm[:, 1].min()

    def write(shape, lanes, Wlocal, uv=None, shade=None, prop_mat=True):
        Wp = to_world(Wlocal, fr)
        centre = (Wp.max(0) + Wp.min(0)) / 2
        half = float(np.abs(Wp - centre).max()) * 1.002
        struct.pack_into('<f', out, lanes.record_offset + 0x10, half)
        struct.pack_into('<3f', out, lanes.record_offset + 0x20, *centre)
        base = M._stream_base({}, shape, lanes.position_stream)
        for i, p in enumerate(Wp):
            q = [M.encode_normshort(max(-1.0, min(1.0, (p[a] - centre[a]) / half))) for a in range(3)]
            struct.pack_into('<3h', out, base + i * lanes.position_stride + lanes.position_offset, *q)
        if uv is not None:
            struct.pack_into('<4f', out, lanes.record_offset + 0x30, 0.5, 0.5, 0.5, 0.5)
            ub = M._stream_base({}, shape, lanes.texcoord[0])
            for i, (a, b) in enumerate(uv):
                qa = M.encode_normshort(max(-1.0, min(1.0, (a - 0.5) / 0.5)))
                qb = M.encode_normshort(max(-1.0, min(1.0, (b - 0.5) / 0.5)))
                struct.pack_into('<2h', out, ub + i * lanes.texcoord[2] + lanes.texcoord[1], qa, qb)
        if shade is not None and lanes.colour is not None:
            cb = M._stream_base({}, shape, lanes.colour[0])
            for i, s in enumerate(shade):
                c = int(max(0, min(255, 200 * s)))
                struct.pack_into('<I', out, cb + i * lanes.colour[2] + lanes.colour[1], M.rgba_to_d3dcolor(c, c, c, 255))
        if prop_mat:
            for j in range(int(shape['submesh_count'])):
                struct.pack_into('<H', out, int(shape['submesh_offset']) + j * 0x80, mi)

    # ---- the computer (prop-local), then placed and turned on the desk
    t_mon = all_tris('monitor_group1')
    # the monitor's three parts: the stand's neck (the smallest), the base plate, the panel
    neck_set = frozenset(components(len(Lm), t_mon, Lm)[0])
    new_m, part = morph_monitor(Lm, neck_set)
    pa = np.array(part)
    kb, kb_l, kb_p = positions('mousepad')
    new_k = morph_keyboard(to_local(kb_p, fr))
    x0, x1, z0, z1, hf, hb = KEYB
    # the keyboard's coiled cable: from the keyboard's back-left corner to the unit's left side
    neck = np.nonzero(pa == 'neck')[0]
    cab_a, cab_b = np.array(CABLE_A), np.array(CABLE_B)
    new_m[neck] = tube_map(Lm[neck], cab_a, cab_b, CABLE_R)
    uv_m = np.zeros((len(new_m), 2)); sh_m = np.ones(len(new_m))
    for pname, reg in (('crt', side_regions(R_MON_SIDE, R_MON_SIDE, R_MON_FRONT)),
                       ('unit', side_regions(R_UNIT_SIDE, R_UNIT_SIDE, R_UNIT_FRONT)),
                       ('neck', lambda k: (R_CABLE, (0, 2) if (k or (1, 1))[0] == 1 else (2, 1), 0.9, (False, False)))):
        sel = set(np.nonzero(pa == pname)[0].tolist())
        tt = [t for t in t_mon if t[0] in sel]
        if not tt:
            continue
        uv, sh, used = face_uvs(new_m, tt, len(new_m), reg)
        uv_m[used] = uv[used]; sh_m[used] = sh[used]
    # the cable runs along x/z: map its length onto the coil bands
    if len(neck):
        q = new_m[neck]
        a, b = cab_a, cab_b
        tlen = ((q - a) @ (b - a)) / np.dot(b - a, b - a)
        uv_m[neck, 0] = R_CABLE[0] + 0.003 + np.clip(tlen, 0, 1) * (R_CABLE[2] - R_CABLE[0] - 0.006)
        uv_m[neck, 1] = (R_CABLE[1] + R_CABLE[3]) / 2 + (q[:, 1] - q[:, 1].mean()) / 20.0 * (R_CABLE[3] - R_CABLE[1])
    t_kb = all_tris('mousepad')
    uv_k, sh_k, _ = face_uvs(new_k, t_kb, len(new_k), side_regions(R_KEYS, R_KEY_SIDE))
    # the screen quad into the CRT's opening (the photo bezel's opening: 13.5%..77.5% across,
    # 19%..84% down)
    sx0, sx1, sy0, sy1 = morph_screen(None, new_m, part)
    Ls = to_local(sw_p, fr)
    new_s = np.zeros_like(Ls)
    for i, (u, v, w) in enumerate(Ls):
        fu = 0.135 if u < 0 else 0.775
        fv = 0.19 if v > 0 else 0.84
        new_s[i] = (sx0 + fu * (sx1 - sx0), sy1 - fv * (sy1 - sy0), -CRT_SETBACK + 0.7)
    write(mon, mon_l, place(new_m, PC_AT, PC_YAW, desk), uv_m, sh_m)
    write(sw, sw_l, place(new_s, PC_AT, PC_YAW, desk), prop_mat=False)
    write(kb, kb_l, place(new_k, PC_AT, PC_YAW, desk), uv_k, sh_k)

    # the monitor's soft shadow quad: under the system unit, a little larger
    ms, ms_l, ms_p = positions('monitor_shadow')
    Lsh = to_local(ms_p, fr)
    lo, hi = Lsh.min(0), Lsh.max(0)
    new_sh = np.zeros_like(Lsh)
    for i, (u, v, w) in enumerate(Lsh):
        fu = (u - lo[0]) / (hi[0] - lo[0]); fw = (w - lo[2]) / (hi[2] - lo[2])
        new_sh[i] = (-UNIT_W / 2 - 14 + fu * (UNIT_W + 28), 0.5, -UNIT_D - 14 + fw * (UNIT_D + 40))
    write(ms, ms_l, place(new_sh, PC_AT, PC_YAW, desk), prop_mat=False)

    # ---- the phone
    ph, ph_l, ph_p = positions('phone_group1')
    Lp = to_local(ph_p, fr)
    t_ph = all_tris('phone_group1')
    comps = components(len(Lp), t_ph, Lp)
    body = comps[-1]                                  # the base (largest footprint, 84 vertices)
    handset = comps[-2]                               # the cordless handset (96)
    small = [c for c in comps[:-2]]
    # by size: the keypad plate (24 vertices), the antenna (18), three 4-vertex buttons
    keypad = max(small, key=len)
    rest = [c for c in small if c is not keypad]
    antenna = max(rest, key=len)
    buttons = [c for c in rest if c is not antenna]
    new_p = Lp.copy()
    W2, D2 = PHONE_W / 2, PHONE_D / 2

    def top_y(z):                                     # the sloped top: low at the front (+z)
        f = (z + D2) / PHONE_D
        return PHONE_HB + f * (PHONE_HF - PHONE_HB)
    # the base: un-turn it (its footprint's minimum-area rectangle), then wrap it onto the wedge:
    # each vertex goes, along its direction from the base's centre, to the wedge's surface (the
    # base is a closed, roughly box-like slab, so the wrap keeps every face facing out)
    q = Lp[body]
    a_, b_ = yaw_frame(q)
    na = (a_ - (a_.min() + a_.max()) / 2) / (np.ptp(a_) / 2)
    nb = (b_ - (b_.min() + b_.max()) / 2) / (np.ptp(b_) / 2)
    nh = (q[:, 1] - (q[:, 1].min() + q[:, 1].max()) / 2) / (np.ptp(q[:, 1]) / 2)
    for k, i in enumerate(body):
        t = 1.0 / max(abs(na[k]), abs(nb[k]), abs(nh[k]), 1e-6)
        x, z, hh = na[k] * t * W2, nb[k] * t * D2, nh[k] * t
        new_p[i] = (x, (hh + 1) / 2 * top_y(z) + (0.2 if hh < -0.99 else 0.0), z)
    # the keypad plate becomes the phone's whole face plate on the slope (keypad, line
    # buttons, cradle drawn on it); own axes e1, e2 in the plate, e3 its normal
    f = pca_unit(Lp[keypad])
    for k, i in enumerate(keypad):
        x = -W2 * 0.97 + f[k, 0] * PHONE_W * 0.97
        z = -D2 * 0.97 + (1 - f[k, 1]) * PHONE_D * 0.97
        new_p[i] = (x, top_y(z) + 0.8 + f[k, 2] * 1.5, z)
    # the handset lying across the back of the top, in its cradle (length e1 -> x, width e2 ->
    # z flipped, thickness e3 -> y: a proper turn, so the faces keep facing out)
    f = pca_unit(Lp[handset])
    for k, i in enumerate(handset):
        x = -W2 * 0.94 + f[k, 0] * PHONE_W * 0.94
        z = -D2 * 0.84 + (1 - f[k, 1]) * PHONE_D * 0.30
        new_p[i] = (x, top_y(z) + 2.0 + f[k, 2] * 20.0, z)
    # the coiled cord: from the handset's left end down the phone's left side
    new_p[antenna] = tube_map(Lp[antenna], (-W2 * 0.97, top_y(-D2 * 0.70) + 8, -D2 * 0.70), (-W2 + 2, 10.0, -D2 * 0.10), 3.5)
    for c in buttons:                                 # the cordless buttons fold into the body
        new_p[c] = (0.0, PHONE_HF * 0.5, 0.0)
    uv_p = np.zeros((len(new_p), 2)); sh_p = np.ones(len(new_p))
    for grp, reg in ((body, side_regions(R_PHONE_TOP, R_PHONE_SIDE)),
                     (keypad, lambda k: (R_PHONE_TOP, (0, 2), 1.0, (False, False))),   # a single sheet
                     (handset, side_regions(R_HSET_TOP, R_HSET_SIDE))):
        sel = set(grp)
        tt = [t for t in t_ph if t[0] in sel]
        uv, sh, used = face_uvs(new_p, tt, len(new_p), reg)
        uv_p[used] = uv[used]; sh_p[used] = sh[used]
    qa = new_p[antenna]
    tl = (qa[:, 0] - qa[:, 0].min()) / max(np.ptp(qa[:, 0]), 1e-6)
    uv_p[antenna, 0] = R_CORD[0] + 0.003 + tl * (R_CORD[2] - R_CORD[0] - 0.006)
    uv_p[antenna, 1] = (R_CORD[1] + R_CORD[3]) / 2
    for c in buttons:
        uv_p[c] = ((R_PHONE_SIDE[0] + R_PHONE_SIDE[2]) / 2, (R_PHONE_SIDE[1] + R_PHONE_SIDE[3]) / 2)
    write(ph, ph_l, place(new_p, PHONE_AT, PHONE_YAW, desk), uv_p, sh_p)

    # ---- the ashtray (the notebook sticker quad), flat on the desk
    nl, nl_l, nl_p = positions('notebook_logo')
    Ln = to_local(nl_p, fr)
    lo, hi = Ln.min(0), Ln.max(0)
    h = ASHTRAY_SIZE / 2
    new_n = np.zeros_like(Ln)
    uv_n = np.zeros((len(Ln), 2))
    for i, (u, v, w) in enumerate(Ln):
        fu = (u - lo[0]) / (hi[0] - lo[0]); fw = (w - lo[2]) / (hi[2] - lo[2])
        new_n[i] = (-h + fu * 2 * h, 1.0, -h + fw * 2 * h)
        uv_n[i] = (R_ASHTRAY[0] + 0.004 + fu * (R_ASHTRAY[2] - R_ASHTRAY[0] - 0.008),
                   R_ASHTRAY[1] + 0.004 + fw * (R_ASHTRAY[3] - R_ASHTRAY[1] - 0.008))
    write(nl, nl_l, place(new_n, ASHTRAY_AT, 20.0, desk), uv_n, np.full(len(Ln), 1.0))

    # ---- the wall: the large picture becomes the schedule poster, the pennant a felt pennant
    def wall_uv(name, region, verts=None, submesh=None):
        s, l, P = positions(name)
        idx = list(range(len(P))) if verts is None else verts
        Q = P[idx]
        ext = Q.max(0) - Q.min(0)
        ah, av = (0 if ext[0] >= ext[2] else 2), 1      # along the wall, up
        mn, mx = Q.min(0), Q.max(0)
        uv = np.zeros((len(P), 2))
        u_raw = struct.unpack_from('<4f', decoded, l.record_offset + 0x30)
        ub = M._stream_base({}, s, l.texcoord[0])
        for i in range(len(P)):                        # keep the other vertices' UVs as they are
            qa, qb = struct.unpack_from('<2h', decoded, ub + i * l.texcoord[2] + l.texcoord[1])
            uv[i] = (M.normshort(qa) * u_raw[0] + u_raw[2], M.normshort(qb) * u_raw[1] + u_raw[3])
        # the camera looks at the wall from +z (world): u grows with world x
        for i in idx:
            fx = (P[i, ah] - mn[ah]) / max(mx[ah] - mn[ah], 1e-6)
            fy = (mx[av] - P[i, av]) / max(mx[av] - mn[av], 1e-6)
            uv[i] = (region[0] + 0.004 + fx * (region[2] - region[0] - 0.008),
                     region[1] + 0.004 + fy * (region[3] - region[1] - 0.008))
        # write UVs (the shape's own constant is replaced: all UVs in 0..1 are representable,
        # the frame cells of the atlas included)
        struct.pack_into('<4f', out, l.record_offset + 0x30, 0.5, 0.5, 0.5, 0.5)
        for i, (a, b) in enumerate(uv):
            qa = M.encode_normshort(max(-1.0, min(1.0, (a - 0.5) / 0.5)))
            qb = M.encode_normshort(max(-1.0, min(1.0, (b - 0.5) / 0.5)))
            struct.pack_into('<2h', out, ub + i * l.texcoord[2] + l.texcoord[1], qa, qb)
        subs = range(int(s['submesh_count'])) if submesh is None else [submesh]
        for j in subs:
            struct.pack_into('<H', out, int(s['submesh_offset']) + j * 0x80, mi)
        return uv

    # wall_pic01: submesh 1 (wallphoto) is the picture, vertices 16..19; the frame keeps coach01
    pic_mats = [m for m, _ in tri['wall_pic01']]
    pic_sub = pic_mats.index('wallphoto')
    pic_verts = sorted({x for t in tri['wall_pic01'][pic_sub][1] for x in t})
    other = sorted({x for m, tt in tri['wall_pic01'] if m != 'wallphoto' for t in tt for x in t})
    if set(pic_verts) & set(other):
        print('build-office-ibmpc: wall_pic01 picture shares vertices with its frame; poster skipped')
    else:
        wall_uv('wall_pic01', R_POSTER, pic_verts, pic_sub)
    wall_uv('wall_pennant', R_PENNANT)

    # ---- vertex colours: lift the walls so the paneling reads; the desk lamp turns brass (its
    # arm is untextured, so only its vertex colours can change it)
    def tint_colours(name, mul):
        sh = shapes[name]
        ln = M._shape_lanes(scene, sh, decoded)
        if ln.colour is None:
            return
        cb = M._stream_base({}, sh, ln.colour[0])
        for i in range(ln.vertex_count):
            o = cb + i * ln.colour[2] + ln.colour[1]
            c = struct.unpack_from('<I', decoded, o)[0]
            a, r, g, b = c >> 24, (c >> 16) & 255, (c >> 8) & 255, c & 255
            struct.pack_into('<I', out, o, M.rgba_to_d3dcolor(r * mul[0], g * mul[1], b * mul[2], a))
    tint_colours('room_walls', (WALL_LIFT, WALL_LIFT, WALL_LIFT))
    tint_colours('new_lamp_group', LAMP_BRASS)

    if report is not None:
        report['phone_dbg'] = (new_p, uv_p, body, keypad, handset, t_ph)
        report['frame'] = fr
        report['desk'] = desk
        report['parts'] = {
            'unit': place(new_m[pa == 'unit'], PC_AT, PC_YAW, desk),
            'crt': place(new_m[pa == 'crt'], PC_AT, PC_YAW, desk),
            'cable': place(new_m[pa == 'neck'], PC_AT, PC_YAW, desk),
            'keyboard': place(new_k, PC_AT, PC_YAW, desk),
            'phone': place(new_p, PHONE_AT, PHONE_YAW, desk),
            'ashtray': place(new_n, ASHTRAY_AT, 20.0, desk),
        }
        report['fixed'] = {n: to_local(positions(n)[2], fr) for n in
                           ('dayplanner_group', 'pad_group', 'notebook', 'new_lamp_group', 'mug_group', 'blotter')}
        lamp = report['fixed']['new_lamp_group']      # the lamp's foot only (its arm is overhead)
        report['fixed']['new_lamp_group'] = lamp[lamp[:, 1] < desk + 40]
        report['desk_tris'] = (to_local(positions('desktop001')[2], fr), all_tris('desktop001'))

    if rename:
        old = 'coach_desk'.encode('utf-16le')
        at = out.find(old)
        assert at >= 0
        out[at:at + len(old)] = NEW_NAME.encode('utf-16le')
    return bytes(out)


def footprint_hull(P):
    """2-D convex hull (u, w) of a point set."""
    pts = sorted(set((round(float(a), 2), round(float(b), 2)) for a, b in P[:, [0, 2]]))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def hull_gap(A, B):
    """Separation of two convex polygons (> 0: apart by that much along the best axis)."""
    best = -1e9
    for poly in (A, B):
        for i in range(len(poly)):
            p, q = np.array(poly[i]), np.array(poly[(i + 1) % len(poly)])
            n = np.array([q[1] - p[1], p[0] - q[0]])
            n /= max(np.linalg.norm(n), 1e-9)
            a = [np.dot(n, x) for x in A]
            b = [np.dot(n, x) for x in B]
            best = max(best, max(min(b) - max(a), min(a) - max(b)))
    return best


def layout_check(rep):
    """Every placed part against every fixed desk prop it could touch, and the desk's outline."""
    ok = True
    parts = rep['parts']
    fixed = rep['fixed']
    lines = []
    for pn, P in parts.items():
        if pn == 'crt':
            continue
        A = footprint_hull(P)
        for fn, F in fixed.items():
            if fn == 'blotter':
                continue
            g = hull_gap(A, footprint_hull(F))
            if g < 4.0:
                ok = False
            lines.append('  %-9s vs %-16s gap %6.1f%s' % (pn, fn, g, '' if g >= 4.0 else '  OVERLAP'))
    for a in ('unit', 'keyboard', 'phone', 'ashtray'):
        for b in ('unit', 'keyboard', 'phone', 'ashtray'):
            if a < b:
                g = hull_gap(footprint_hull(parts[a]), footprint_hull(parts[b]))
                if g < 2.0:
                    ok = False
                lines.append('  %-9s vs %-16s gap %6.1f%s' % (a, b, g, '' if g >= 2.0 else '  OVERLAP'))
    # desk outline: every footprint corner over a desk-top triangle
    D, T = rep['desk_tris']
    top = [t for t in T if min(D[t[0], 1], D[t[1], 1], D[t[2], 1]) > rep['desk'] - 3]

    def on_desk(p):
        for a, b, c in top:
            A, B, C = D[a][[0, 2]], D[b][[0, 2]], D[c][[0, 2]]
            v0, v1, v2 = C - A, B - A, p - A
            den = v0[0] * v1[1] - v1[0] * v0[1]
            if abs(den) < 1e-9:
                continue
            s = (v2[0] * v1[1] - v1[0] * v2[1]) / den
            t = (v0[0] * v2[1] - v2[0] * v0[1]) / den
            if s >= -1e-6 and t >= -1e-6 and s + t <= 1 + 1e-6:
                return True
        return False
    for pn, P in parts.items():
        if pn == 'crt':
            continue
        off = [c for c in footprint_hull(P) if not on_desk(np.array(c))]
        if off:
            ok = False
        lines.append('  %-9s on the desk: %s' % (pn, 'yes' if not off else 'NO, %d corners off' % len(off)))
    return ok, lines


def main():
    if not os.path.isdir(MODTOOLS):
        print('build-office-ibmpc: 2K Football Mod Tools not found (%s); the 1980s office keeps the retail desk' % MODTOOLS)
        return 1
    if not os.path.exists(PHOTO):
        print('build-office-ibmpc: %s missing' % PHOTO)
        return 1
    sys.path.insert(0, MODTOOLS)
    from mod_editor.core import nfl2k5_models as M

    pack = open(PACK, 'rb').read()
    recs = list(records(pack))
    _, tag0, desk_rec = recs[0]                       # build-office-pack.py writes coach_desk first
    assert tag0 == 'SCNE'
    src = M.ModelSpanSource({'o23c4': desk_rec})
    resource, decoded, scene = src.parse('o23c4')
    assert scene['name'] == 'coach_desk'
    rep = {}
    out = build_scene(M, src, 'o23c4', decoded, scene, report=rep)
    ok, lines = layout_check(rep)
    print('build-office-ibmpc: desk layout %s' % ('clear' if ok else 'HAS OVERLAPS'))
    print('\n'.join(lines))
    if '--layout' in sys.argv:
        return 0 if ok else 1

    fill = M._tools_module('nfl_vc_lz_fill')
    rebuilt, info = fill.rebuild_fixed_span_filled(desk_rec, out, encoder='auto')
    print('build-office-ibmpc: coach_d80s compressed into %d of %d bytes' % (info.filled_bytes, info.stored_size))

    # pack: the four original records, then coach_d80s
    body = b''.join(r + b'\0' * (-len(r) & 15) for r in [r for o, t, r in recs][:4])
    body += rebuilt + b'\0' * (-len(rebuilt) & 15)
    open(PACK, 'wb').write(body)
    os.makedirs(ERA, exist_ok=True)
    build_atlas(Image.open(PHOTO).convert('RGB')).save(os.path.join(ERA, 'computer_monitor.png'))
    print('build-office-ibmpc: wrote %s (5 records) and %s' % (PACK, os.path.join(ERA, 'computer_monitor.png')))
    return 0


if __name__ == '__main__':
    sys.exit(main())

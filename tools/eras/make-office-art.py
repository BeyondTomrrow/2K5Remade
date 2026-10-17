"""Made screen images for the My NFL Eras office looks (docs/eras/ERAS-OFFICE-DESIGN.md 2.2).

    python -I tools/eras/make-office-art.py            write them into mods/eras/offices/<era>/

Everything here is drawn from scratch (shapes, gradients and text in Windows' own fonts); no
retail pixels are read or written. The images replace, only while the Eras office shows, the
textures named in mods/eras/offices/office-textures.ini:

  1980s/skin_team_logo.png     the coach_desk monitor wallpaper: a green-phosphor terminal
                               (the same texture is the notebook sticker)
  1980s/office_atlas.png       the Coach's Desk atlas redrawn for the 1980s: dark walnut wall
                               paneling, walnut shelf and door trim, book cloth, an oxblood day
                               planner, a harvest-gold PLAYBOOK binder, brass door knob
  1980s/carpet.png             rust and brown shag (tileable)
  1980s/desk_metal.png         honey-oak desk veneer (the glass desk top's texture)
  1980s/wood_panel.png         a walnut raised-panel door (door, end table, floor lamp)
  1980s/coffee_table_wood.png  walnut (shelf, coffee table)
  1990s/computer_screen.png    the front_office computer: a teal mid-90s desktop with a league window
  1990s/wall_tv_picture.png    the wall TV: a 4:3 broadcast still of a game
  2010s/skin_team_logo.png     the monitor wallpaper: a dark flat dashboard
  2020s/computer_screen.png    the front_office computer: a dark analytics dashboard
  2020s/wall_tv_picture.png    the wall TV: a widescreen field view with a modern score strip

A user's own image of the same name in the same folder is simply put there instead
(mods/eras/offices/README.txt). Images are drawn at 4x and reduced, so edges stay smooth.
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.join(ROOT, 'mods', 'eras', 'offices')
FONTS = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')


def font(name, size):
    for n in (name, 'arial.ttf'):
        try:
            return ImageFont.truetype(os.path.join(FONTS, n), size)
        except OSError:
            pass
    return ImageFont.load_default()


def vgrad(w, h, top, bottom):
    im = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return im


def save(im, era, name, size):
    os.makedirs(os.path.join(OUT, era), exist_ok=True)
    p = os.path.join(OUT, era, name)
    im.resize(size, Image.LANCZOS).save(p)
    print('wrote', os.path.relpath(p, ROOT), size)


# ---- 1980s: green-phosphor terminal ------------------------------------------------------
def terminal_1980s():
    S = 2048
    im = vgrad(S, S, (6, 22, 10), (2, 10, 4))
    d = ImageDraw.Draw(im)
    f = font('lucon.ttf', 132)
    fb = font('lucon.ttf', 150)
    lines = [
        ('NFL LEAGUE OFFICE', fb),
        ('1980 SEASON', f),
        ('-' * 18, f),
        ('> STANDINGS', f),
        ('> SCHEDULE', f),
        ('> NEXT GAME', f),
        ('', f),
        ('READY_', f),
    ]
    glow = Image.new('RGB', (S, S))
    gd = ImageDraw.Draw(glow)
    y = 260
    for text, ff in lines:
        gd.text((230, y), text, font=ff, fill=(90, 255, 120))
        y += 175 if ff is f else 210
    blur = glow.filter(ImageFilter.GaussianBlur(22)).convert('L').point(lambda v: min(255, v))
    im = Image.composite(Image.new('RGB', (S, S), (40, 200, 70)), im, blur.point(lambda v: v * 2 // 3))
    im.paste(Image.new('RGB', (S, S), (190, 255, 200)), (0, 0), glow.convert('L').point(lambda v: min(255, v * 4 // 3)))
    d = ImageDraw.Draw(im)
    for y in range(0, S, 8):                     # scan lines
        d.line([(0, y), (S, y)], fill=(0, 0, 0), width=2)
    # vignette, as a curved tube darkens toward its corners
    vig = Image.new('L', (S, S), 0)
    vd = ImageDraw.Draw(vig)
    for i in range(60):
        r = i / 60
        vd.rounded_rectangle([int(S * r * 0.12), int(S * r * 0.12), S - int(S * r * 0.12), S - int(S * r * 0.12)],
                             radius=int(S * 0.2), fill=int(255 * r))
    vig = vig.filter(ImageFilter.GaussianBlur(60))
    im = Image.composite(im, Image.new('RGB', (S, S), (0, 0, 0)), vig)
    return im


# ---- 1990s: teal desktop ---------------------------------------------------------------
def desktop_1990s():
    S = 2048
    vis = int(S * 0.80)                          # the monitor shows rows 0..0.80 (5:4)
    im = Image.new('RGB', (S, S), (0, 128, 128))
    d = ImageDraw.Draw(im)
    ft = font('tahoma.ttf', 46)
    ftb = font('tahomabd.ttf', 50)
    fsm = font('tahoma.ttf', 40)
    # icons down the left
    icons = [('My Computer', (200, 200, 200)), ('League Files', (230, 200, 90)), ('Schedule', (240, 240, 240)),
             ('Scouting', (120, 160, 230)), ('Recycle Bin', (170, 170, 170))]
    for i, (name, col) in enumerate(icons):
        x, y = 70, 60 + i * 250
        d.rectangle([x + 40, y, x + 150, y + 100], fill=col, outline=(0, 0, 0), width=4)
        d.rectangle([x + 55, y + 15, x + 135, y + 70], fill=(30, 60, 120) if i == 0 else tuple(max(0, c - 40) for c in col))
        tw = d.textlength(name, font=fsm)
        d.text((x + 95 - tw / 2, y + 115), name, font=fsm, fill=(255, 255, 255))
    # the league window
    wx0, wy0, wx1, wy1 = 420, 120, 1930, vis - 170
    d.rectangle([wx0 + 14, wy0 + 14, wx1 + 14, wy1 + 14], fill=(0, 70, 70))
    d.rectangle([wx0, wy0, wx1, wy1], fill=(192, 192, 192), outline=(255, 255, 255), width=6)
    d.line([(wx1, wy0), (wx1, wy1), (wx0, wy1)], fill=(64, 64, 64), width=6)
    bar = vgrad(wx1 - wx0 - 20, 80, (0, 0, 128), (16, 132, 208))
    im.paste(bar, (wx0 + 10, wy0 + 10))
    d.text((wx0 + 30, wy0 + 18), 'League Office - 1990s Standings', font=ftb, fill=(255, 255, 255))
    for k in range(3):
        bx = wx1 - 70 - k * 70
        d.rectangle([bx, wy0 + 20, bx + 56, wy0 + 76], fill=(192, 192, 192), outline=(255, 255, 255), width=3)
    d.text((wx0 + 30, wy0 + 110), 'File   Edit   View   Teams   Help', font=ft, fill=(0, 0, 0))
    tx0, ty0 = wx0 + 30, wy0 + 180
    d.rectangle([tx0, ty0, wx1 - 30, wy1 - 40], fill=(255, 255, 255), outline=(128, 128, 128), width=4)
    hdr = ['TEAM', 'W', 'L', 'T', 'PCT', 'PF', 'PA']
    cols = [tx0 + 30, tx0 + 640, tx0 + 760, tx0 + 880, tx0 + 1000, tx0 + 1170, tx0 + 1320]
    d.rectangle([tx0 + 4, ty0 + 4, wx1 - 34, ty0 + 74], fill=(220, 220, 220))
    for c, h in zip(cols, hdr):
        d.text((c, ty0 + 14), h, font=ftb, fill=(0, 0, 0))
    rnd = random.Random(1990)
    names = ['East Division', 'Team A', 'Team B', 'Team C', 'Team D', 'Central Division', 'Team E', 'Team F', 'Team G']
    y = ty0 + 90
    for n in names:
        if 'Division' in n:
            d.text((cols[0], y), n, font=ftb, fill=(0, 0, 128))
        else:
            w = rnd.randint(4, 12)
            l = 16 - w
            vals = [n, str(w), str(l), '0', '%.3f' % (w / 16), str(rnd.randint(250, 420)), str(rnd.randint(250, 420))]
            for c, v in zip(cols, vals):
                d.text((c, y), v, font=ft, fill=(0, 0, 0))
        y += 66
    # taskbar at the bottom of the visible area
    d.rectangle([0, vis - 100, S, vis], fill=(192, 192, 192))
    d.line([(0, vis - 98), (S, vis - 98)], fill=(255, 255, 255), width=4)
    d.rectangle([10, vis - 88, 230, vis - 14], fill=(192, 192, 192), outline=(255, 255, 255), width=4)
    d.line([(230, vis - 88), (230, vis - 14), (10, vis - 14)], fill=(64, 64, 64), width=4)
    for i, col in enumerate([(220, 40, 30), (40, 170, 40), (30, 80, 220), (240, 200, 30)]):
        d.rectangle([28 + (i % 2) * 26, vis - 76 + (i // 2) * 26, 50 + (i % 2) * 26, vis - 56 + (i // 2) * 26], fill=col)
    d.text((95, vis - 80), 'Start', font=ftb, fill=(0, 0, 0))
    d.rectangle([S - 260, vis - 88, S - 10, vis - 14], fill=(192, 192, 192), outline=(128, 128, 128), width=3)
    d.text((S - 225, vis - 80), '7:30 PM', font=ft, fill=(0, 0, 0))
    d.rectangle([0, vis, S, S], fill=(0, 0, 0))
    return im


# ---- a football field, for the wall TV ---------------------------------------------------
def field(w, h, grass=(46, 118, 46), stripes=True, hd=False):
    im = Image.new('RGB', (w, h), grass)
    d = ImageDraw.Draw(im)
    horizon = int(h * 0.18)
    sky = vgrad(w, horizon, (18, 20, 34), (60, 64, 80)) if not hd else vgrad(w, horizon, (20, 26, 40), (70, 80, 100))
    im.paste(sky, (0, 0))
    # stands: rows of faint dots
    rnd = random.Random(7)
    for y in range(int(horizon * 0.35), horizon, 6):
        for x in range(0, w, 7):
            v = rnd.randint(40, 120)
            d.point((x + rnd.randint(0, 3), y), fill=(v, v - 10, v - 20))
    # perspective field: the sidelines converge toward the stands
    def proj(x, y):  # x across the field (-1..1 sideline to sideline), y 0 (far) .. 1 (near)
        yy = horizon + (h - horizon) * (y ** 1.35)
        half = w * (0.30 + 0.62 * y)
        return w / 2 + x * half, yy
    for k in range(12):
        y0, y1 = k / 12, (k + 1) / 12
        if stripes and k % 2 == 0:
            p = [proj(-1.4, y0), proj(1.4, y0), proj(1.4, y1), proj(-1.4, y1)]
            d.polygon(p, fill=tuple(min(255, c + 12) for c in grass))
    for k in range(13):                          # yard lines across the field, near to far
        y = k / 12
        d.line([proj(-1.4, y), proj(1.4, y)], fill=(235, 240, 235), width=max(2, int(h * 0.004 * (0.4 + y))))
    for x in (-1.0, 1.0):                         # sidelines
        d.line([proj(x, 0), proj(x, 1)], fill=(235, 240, 235), width=int(h * 0.006))
    for k in range(12 * 5):                       # hash marks
        y = k / 60
        for x in (-0.25, 0.25):
            a, b = proj(x - 0.03, y), proj(x + 0.03, y)
            d.line([a, b], fill=(225, 230, 225), width=2)
    # two lines of players: small dark and light blobs at the line of scrimmage
    ly = 0.55
    for i in range(11):
        for team, off, col in ((0, -0.02, (240, 240, 240)), (1, 0.03, (150, 30, 30))):
            x = -0.55 + i * 0.11 + rnd.uniform(-0.01, 0.01)
            px, py = proj(x, ly + off * (1 if team else 1))
            r = h * 0.012
            d.ellipse([px - r, py - r * 2.4, px + r, py], fill=col)
            d.ellipse([px - r * 0.8, py - r * 3.3, px + r * 0.8, py - r * 1.9], fill=tuple(max(0, c - 60) for c in col))
    return im


def tv_1990s():
    S = 2048
    im = Image.new('RGB', (S, S), (0, 0, 0))
    y0, y1 = int(S * 0.05), int(S * 0.81)
    pic = field(S, y1 - y0)
    d = ImageDraw.Draw(pic)
    f = font('impact.ttf', 96)
    fs = font('arialbd.ttf', 70)
    # a 90s score box in the upper left
    d.rectangle([90, 90, 900, 330], fill=(10, 20, 90))
    d.rectangle([90, 90, 900, 150], fill=(200, 30, 30))
    d.text((115, 92), 'SUNDAY FOOTBALL', font=fs, fill=(255, 255, 255))
    d.text((115, 160), 'HOME  14', font=f, fill=(255, 230, 80))
    d.text((520, 160), 'AWAY  10', font=f, fill=(255, 255, 255))
    d.text((115, 255), '3RD  8:42   2ND & 7', font=fs, fill=(220, 220, 220))
    for y in range(0, pic.height, 6):              # tube scan lines, faint
        d.line([(0, y), (S, y)], fill=(0, 0, 0), width=1)
    pic = pic.filter(ImageFilter.GaussianBlur(1.5))
    im.paste(pic, (0, y0))
    return im


def wallpaper_2010s():
    S = 2048
    im = vgrad(S, S, (16, 22, 32), (6, 8, 12))
    d = ImageDraw.Draw(im)
    f = font('segoeuil.ttf', 150)
    fs = font('segoeui.ttf', 70)
    for i in range(14):                           # soft diagonal light bars
        x = -600 + i * 260
        d.polygon([(x, S), (x + 160, S), (x + 1300, 0), (x + 1140, 0)], fill=(20 + i % 3 * 3, 30 + i % 3 * 3, 44 + i % 3 * 4))
    im = im.filter(ImageFilter.GaussianBlur(30))
    d = ImageDraw.Draw(im)
    d.text((200, 820), 'LEAGUE OFFICE', font=f, fill=(230, 236, 245))
    d.rectangle([200, 1010, 1100, 1022], fill=(0, 150, 255))
    d.text((200, 1060), '2010s  |  Game day ready', font=fs, fill=(150, 170, 195))
    return im


def dashboard_2020s():
    S = 2048
    vis = int(S * 0.80)
    im = vgrad(S, S, (14, 17, 24), (10, 12, 17))
    d = ImageDraw.Draw(im)
    fb = font('segoeuib.ttf', 70)
    f = font('segoeui.ttf', 52)
    fs = font('segoeui.ttf', 40)
    d.rectangle([0, 0, 170, vis], fill=(20, 24, 33))
    for i in range(6):
        d.rounded_rectangle([50, 80 + i * 170, 120, 150 + i * 170], radius=18, fill=(40, 48, 64) if i else (0, 132, 255))
    d.text((230, 60), 'League Office', font=fb, fill=(240, 244, 250))
    d.text((230, 150), 'Season dashboard', font=fs, fill=(130, 140, 160))
    cards = [('Record', '11-3'), ('Point diff.', '+96'), ('Win prob.', '78%'), ('Cap room', '$14.2M')]
    for i, (k, v) in enumerate(cards):
        x = 230 + i * 450
        d.rounded_rectangle([x, 240, x + 410, 470], radius=26, fill=(24, 29, 40))
        d.text((x + 34, 270), k, font=fs, fill=(130, 140, 160))
        d.text((x + 34, 340), v, font=font('segoeuib.ttf', 96), fill=(240, 244, 250))
    # chart card
    cx0, cy0, cx1, cy1 = 230, 520, 1330, vis - 70
    d.rounded_rectangle([cx0, cy0, cx1, cy1], radius=26, fill=(24, 29, 40))
    d.text((cx0 + 34, cy0 + 26), 'Expected points added, by week', font=fs, fill=(130, 140, 160))
    rnd = random.Random(2025)
    pts = []
    for k in range(17):
        x = cx0 + 60 + k * (cx1 - cx0 - 120) / 16
        y = cy1 - 90 - (0.45 + 0.3 * math.sin(k / 3.0) + rnd.uniform(-0.08, 0.08)) * (cy1 - cy0 - 260)
        pts.append((x, y))
    for k in range(5):
        y = cy0 + 120 + k * (cy1 - cy0 - 200) / 4
        d.line([(cx0 + 40, y), (cx1 - 40, y)], fill=(36, 42, 56), width=3)
    poly = pts + [(pts[-1][0], cy1 - 60), (pts[0][0], cy1 - 60)]
    shade = Image.new('L', (S, S), 0)
    ImageDraw.Draw(shade).polygon(poly, fill=70)
    im = Image.composite(Image.new('RGB', (S, S), (0, 132, 255)), im, shade)
    d = ImageDraw.Draw(im)
    d.line(pts, fill=(0, 150, 255), width=10, joint='curve')
    for p in pts:
        d.ellipse([p[0] - 10, p[1] - 10, p[0] + 10, p[1] + 10], fill=(240, 244, 250))
    # list card
    lx0 = 1380
    d.rounded_rectangle([lx0, cy0, S - 60, cy1], radius=26, fill=(24, 29, 40))
    d.text((lx0 + 34, cy0 + 26), 'Next opponents', font=fs, fill=(130, 140, 160))
    for k in range(6):
        y = cy0 + 110 + k * 100
        d.ellipse([lx0 + 34, y, lx0 + 94, y + 60], fill=[(200, 60, 60), (60, 120, 220), (230, 170, 40), (60, 170, 110), (150, 90, 200), (200, 200, 210)][k])
        d.text((lx0 + 120, y + 4), 'Week %d' % (15 + k), font=f, fill=(225, 230, 240))
    d.rectangle([0, vis, S, S], fill=(0, 0, 0))
    return im


def tv_2020s():
    S = 2048
    im = Image.new('RGB', (S, S), (0, 0, 0))
    y0, y1 = int(S * 0.05), int(S * 0.81)
    pic = field(S, y1 - y0, grass=(40, 128, 52), hd=True)
    d = ImageDraw.Draw(pic)
    fb = font('bahnschrift.ttf', 78)
    # modern score strip along the bottom
    h = pic.height
    d.rounded_rectangle([140, h - 230, S - 140, h - 110], radius=20, fill=(12, 14, 20))
    d.rectangle([140, h - 230, 560, h - 110], fill=(170, 20, 30))
    d.rectangle([980, h - 230, 1400, h - 110], fill=(20, 60, 160))
    d.text((180, h - 215), 'HOME   21', font=fb, fill=(255, 255, 255))
    d.text((1020, h - 215), 'AWAY   17', font=fb, fill=(255, 255, 255))
    d.text((620, h - 215), '4TH 2:14', font=fb, fill=(235, 235, 235))
    d.text((1460, h - 215), '3RD & 4', font=fb, fill=(255, 210, 60))
    im.paste(pic, (0, y0))
    return im


# ---- 1980s room surfaces: paneling, shag, veneer, walnut -----------------------------------
# All tileable: noise is built from integer-frequency waves (np.fft on a periodic grid), so a
# texture repeats without a seam when the room's UVs wrap it.

def pnoise(w, h, seed, fmax=8, power=1.6):
    """Periodic smooth noise in 0..1, w x h, frequencies up to fmax cycles per tile."""
    import numpy as np
    rng = np.random.default_rng(seed)
    F = np.zeros((h, w), complex)
    for fy in range(-fmax, fmax + 1):
        for fx in range(-fmax, fmax + 1):
            if fx == 0 and fy == 0:
                continue
            a = rng.normal() / (fx * fx + fy * fy) ** (power / 2)
            F[fy % h, fx % w] = a * np.exp(2j * math.pi * rng.random())
    n = np.real(np.fft.ifft2(F))
    n -= n.min()
    return n / max(n.max(), 1e-9)


def wood(w, h, dark, light, seed, rings=9, warp=0.9, streak=0.35, horizontal=False):
    """Tileable wood grain running along y (or x): long, gently waving growth lines, pores and
    fine straight fibres (a sawn board, not marble)."""
    import numpy as np
    if horizontal:
        return wood(h, w, dark, light, seed, rings, warp, streak).transpose(Image.Transpose.ROTATE_90)
    rng = np.random.default_rng(seed)
    x = np.arange(w)[None, :] / w
    warp_n = pnoise(w, h, seed + 1, fmax=2, power=2.0) - 0.5
    wob = pnoise(w, h, seed + 3, fmax=5, power=1.5) - 0.5
    t = (x * rings + warp_n * warp + 0.08 * wob) % 1.0
    band = np.clip(np.abs(t - 0.5) * 2, 0, 1) ** 3.0  # thin dark late-wood lines
    pores = pnoise(w, h, seed + 2, fmax=64, power=0.5)
    fibre = np.repeat(rng.normal(0, 1, (1, w)), h, 0)
    v = 1.0 - 0.6 * band + streak * (pores - 0.5) + 0.10 * fibre + 0.25 * warp_n
    v = (v - v.min()) / (v.max() - v.min())
    d, l = np.array(dark, float), np.array(light, float)
    rgb = d[None, None, :] + (l - d)[None, None, :] * v[:, :, None]
    return Image.fromarray(np.clip(rgb, 0, 255).astype('uint8'), 'RGB')


def paneling_1980s(w, h, planks=8):
    """Dark walnut wall paneling: random-width boards with V grooves, the grain running up."""
    import numpy as np
    im = wood(w, h, (52, 30, 16), (118, 74, 40), 801, rings=14, warp=1.2, streak=0.5)
    a = np.asarray(im).astype(float)
    rng = np.random.default_rng(80)
    edges = np.cumsum(rng.uniform(0.7, 1.3, planks))
    edges = (edges / edges[-1] * w).astype(int)
    tone = np.ones(w)
    x0 = 0
    for k, x1 in enumerate(edges):
        tone[x0:x1] = rng.uniform(0.85, 1.12)
        x0 = x1
    a *= tone[None, :, None]
    for x in edges:                                   # the grooves: a dark V with a lit edge
        for dx, f in ((-2, 0.55), (-1, 0.25), (0, 0.18), (1, 0.4), (2, 1.25)):
            a[:, (x + dx) % w] *= f
    return Image.fromarray(np.clip(a, 0, 255).astype('uint8'), 'RGB')


def shag_1980s(S=256):
    """Rust and brown shag: clumped strands in three tones (tileable)."""
    import numpy as np
    rng = np.random.default_rng(1980)
    base = pnoise(S, S, 11, fmax=24, power=0.9)
    clump = pnoise(S, S, 12, fmax=6, power=1.4)
    strands = rng.random((S, S))
    v = 0.45 * base + 0.25 * clump + 0.30 * strands
    pal = np.array([(58, 26, 12), (104, 46, 20), (150, 72, 30), (176, 104, 46)], float)
    idx = np.clip(v * 3.0, 0, 2.999)
    i0 = idx.astype(int)
    f = (idx - i0)[:, :, None]
    rgb = pal[i0] * (1 - f) + pal[i0 + 1] * f
    im = Image.fromarray(np.clip(rgb, 0, 255).astype('uint8'), 'RGB')
    big = Image.new('RGB', (S * 3, S * 3))
    for i in range(3):
        for j in range(3):
            big.paste(im, (i * S, j * S))
    big = big.filter(ImageFilter.GaussianBlur(0.7))
    return big.crop((S, S, 2 * S, 2 * S))


def veneer_1980s(S=512):
    """Honey-oak desk veneer (lighter than the 1970s walnut den), grain along u."""
    return wood(S, S, (120, 76, 36), (196, 142, 82), 811, rings=6, warp=1.4, streak=0.3)


def walnut_1980s(S=512):
    return wood(S, S, (48, 28, 14), (112, 70, 38), 821, rings=7, warp=1.0, streak=0.4)


def door_1980s(S=512):
    """vcHwShader27 (the door, end table and floor lamp): a walnut raised-panel door."""
    import numpy as np
    im = walnut_1980s(S)
    m = int(S * 0.06)
    a = np.asarray(im).astype(float)
    a[:m, :] *= 0.8
    a[-m:, :] *= 0.8
    a[:, :m] *= 0.8
    a[:, -m:] *= 0.8
    a[m:m + 6, m:S - m] *= 1.15
    a[m:S - m, m:m + 6] *= 1.1
    a[S - m - 6:S - m, m:S - m] *= 0.55
    a[m:S - m, S - m - 6:S - m] *= 0.6
    return Image.fromarray(np.clip(a, 0, 255).astype('uint8'), 'RGB')


def office_atlas_1980s():
    """coach01 (the Coach's Desk atlas: walls, shelf, book covers and pages, picture frames, door
    frame, light switch, day planner, notebook, the mug's coffee and rim) redrawn from scratch for
    the 1980s office, cell for cell at the places the scene's texture coordinates use (cells in
    256-pixel units, measured from the scene's UVs; build/eras/office/office_uv_boxes.json)."""
    import numpy as np
    K = 4                                             # drawn at 1024
    S = 256 * K
    im = Image.new('RGB', (S, S), (70, 50, 34))
    d = ImageDraw.Draw(im)

    def R(x0, y0, x1, y1):
        return [x0 * K, y0 * K, x1 * K - 1, y1 * K - 1]

    def paste(img, x0, y0, x1, y1):
        im.paste(img.resize(((x1 - x0) * K, (y1 - y0) * K), Image.LANCZOS), (x0 * K, y0 * K))

    # walls (room_walls): dark walnut paneling, the cell stretched over each wall
    paste(paneling_1980s(640, 760, planks=9), 0, 160, 80, 256)
    # shelf (top-left wood cell): walnut
    paste(walnut_1980s(256), 0, 0, 64, 64)
    # book pages and covers: cream page edges; covers in period cloth colours
    d.rectangle(R(64, 0, 96, 32), fill=(226, 214, 188))
    pg = Image.new('RGB', (32 * K, 32 * K), (220, 208, 182))
    pd = ImageDraw.Draw(pg)
    for y in range(0, 32 * K, 5):
        pd.line([(0, y), (32 * K, y)], fill=(186, 172, 146), width=1)
    im.paste(pg, (64 * K, 32 * K))
    # mug: coffee (brown ellipse cell) and the rim ring
    cof = Image.new('RGB', (32 * K, 64 * K), (52, 30, 16))
    cd = ImageDraw.Draw(cof)
    for r in range(16 * K, 0, -1):
        t = r / (16 * K)
        cd.ellipse([16 * K - r, 32 * K - r * 2, 16 * K + r, 32 * K + r * 2],
                   fill=(int(150 - 90 * t), int(96 - 62 * t), int(56 - 40 * t)))
    im.paste(cof, (96 * K, 0))
    d.rectangle(R(128, 0, 160, 32), fill=(20, 16, 12))
    d.ellipse(R(130, 2, 158, 30), outline=(232, 226, 210), width=4 * K)
    # unused by the Coach's Desk (front office only): neutral beige plastic
    d.rectangle(R(160, 0, 192, 32), fill=(206, 196, 170))
    d.rectangle(R(128, 32, 192, 96), fill=(200, 192, 172))
    # door knob: brass
    kn = Image.new('RGB', (64 * K, 64 * K))
    kd = ImageDraw.Draw(kn)
    for r in range(32 * K, 0, -1):
        t = r / (32 * K)
        kd.ellipse([20 * K - r, 22 * K - r, 20 * K + r * 1.6, 22 * K + r * 1.6],
                   fill=(int(250 - 120 * t), int(220 - 120 * t), int(130 - 90 * t)))
    im.paste(kn, (192 * K, 0))
    # small cells (shelf trim, legal pad parts, book cloth, picture frames)
    cells = {(0, 64): (92, 58, 30), (8, 64): (236, 232, 210), (16, 64): (210, 200, 160),
             (24, 64): (96, 60, 32), (24, 72): (120, 40, 30), (32, 64): (176, 138, 64),
             (40, 64): (64, 40, 22), (0, 72): (196, 160, 70), (8, 72): (40, 32, 26),
             (16, 72): (62, 92, 60), (32, 72): (36, 26, 18), (40, 72): (52, 70, 46),
             (48, 64): (92, 58, 30), (56, 64): (92, 58, 30)}
    for (x, y), c in cells.items():
        h = 8
        d.rectangle(R(x, y, x + 8, y + h), fill=c)
    # gilt frame cell gets a bevel
    d.rectangle(R(32, 64, 40, 72), outline=(222, 186, 98), width=K)
    # door frame trim: walnut, darker at the edges
    paste(wood(64, 64, (40, 22, 12), (100, 62, 34), 831, rings=3), 0, 80, 16, 96)
    # light switch: an almond plate with a toggle
    d.rectangle(R(16, 80, 48, 96), fill=(214, 202, 176))
    d.rectangle(R(29, 84, 35, 92), fill=(232, 224, 204), outline=(150, 140, 120), width=2)
    # the brown band (shelf back / spare): walnut
    paste(walnut_1980s(256).crop((0, 0, 256, 96)), 48, 64, 128, 96)
    # front-office-only cells: plain almond and brown
    d.rectangle(R(0, 96, 128, 160), fill=(200, 190, 166))
    d.rectangle(R(128, 96, 192, 160), fill=(150, 74, 36))
    d.rectangle(R(80, 160, 112, 256), fill=(196, 186, 162))
    # notebook: a harvest-gold vinyl three-ring binder, "PLAYBOOK" up the spine
    nb = Image.new('RGB', (32 * K, 96 * K), (196, 146, 40))
    nd = ImageDraw.Draw(nb)
    nd.rectangle([0, 0, 32 * K - 1, 96 * K - 1], outline=(150, 104, 24), width=2 * K)
    nd.line([(24 * K, 0), (24 * K, 96 * K)], fill=(160, 112, 28), width=K)
    txt = Image.new('RGBA', (96 * K, 32 * K), (0, 0, 0, 0))
    td = ImageDraw.Draw(txt)
    td.text((10 * K, 6 * K), 'PLAYBOOK', font=font('arialbd.ttf', 15 * K), fill=(80, 46, 12))
    nb.paste(txt.rotate(90, expand=True), (2 * K, 0), txt.rotate(90, expand=True))
    im.paste(nb, (128 * K, 160 * K))
    d.rectangle(R(112, 160, 128, 240), fill=(234, 228, 210))   # pages and rings
    for y in range(160, 236, 12):
        d.ellipse(R(116, y + 4, 124, y + 8), fill=(170, 170, 160))
    cov = pnoise(16 * K, 16 * K, 851, fmax=12, power=0.8)       # the binder's cover (its whole face)
    cov = np.stack([176 + 30 * cov, 128 + 22 * cov, 30 + 14 * cov], 2)
    im.paste(Image.fromarray(np.clip(cov, 0, 255).astype('uint8'), 'RGB'), (112 * K, 240 * K))
    # day planner: oxblood leather, gold "Day Planner 1980" up the cover, gold seal, cream pages
    lea = pnoise(48 * K, 192 * K, 841, fmax=40, power=0.7)
    lea = np.stack([84 + 30 * lea, 26 + 12 * lea, 20 + 10 * lea], 2)
    dp = Image.fromarray(np.clip(lea, 0, 255).astype('uint8'), 'RGB')
    txt = Image.new('RGBA', (150 * K, 40 * K), (0, 0, 0, 0))
    td = ImageDraw.Draw(txt)
    td.text((4 * K, 6 * K), 'Day Planner', font=font('georgiai.ttf', 22 * K), fill=(214, 176, 90))
    txt = txt.crop(txt.getbbox()).rotate(90, expand=True)
    dp.paste(txt, ((48 * K - txt.width) // 2, 48 * K + (144 * K - txt.height) // 2), txt)
    im.paste(dp, (208 * K, 64 * K))
    d.rectangle(R(160, 160, 208, 208), fill=(236, 228, 206))
    seal = Image.new('RGB', (48 * K, 48 * K), (84, 26, 20))
    sd = ImageDraw.Draw(seal)
    sd.ellipse([6 * K, 6 * K, 42 * K, 42 * K], outline=(214, 176, 90), width=2 * K)
    sd.text((12 * K, 16 * K), '1980', font=font('georgiab.ttf', 11 * K), fill=(214, 176, 90))
    im.paste(seal, (160 * K, 208 * K))
    d.rectangle(R(192, 80, 208, 160), fill=(236, 228, 206))
    d.rectangle(R(192, 64, 208, 80), fill=(44, 26, 14))         # the binder's spine edge
    return im


def main():
    save(terminal_1980s(), '1980s', 'skin_team_logo.png', (512, 512))
    save(office_atlas_1980s(), '1980s', 'office_atlas.png', (1024, 1024))
    save(shag_1980s(256), '1980s', 'carpet.png', (256, 256))
    save(veneer_1980s(512), '1980s', 'desk_metal.png', (512, 512))
    save(door_1980s(512), '1980s', 'wood_panel.png', (512, 512))
    save(walnut_1980s(512), '1980s', 'coffee_table_wood.png', (512, 512))
    save(desktop_1990s(), '1990s', 'computer_screen.png', (1024, 1024))
    save(tv_1990s(), '1990s', 'wall_tv_picture.png', (1024, 1024))
    save(wallpaper_2010s(), '2010s', 'skin_team_logo.png', (512, 512))
    save(dashboard_2020s(), '2020s', 'computer_screen.png', (1024, 1024))
    save(tv_2020s(), '2020s', 'wall_tv_picture.png', (1024, 1024))


if __name__ == '__main__':
    main()

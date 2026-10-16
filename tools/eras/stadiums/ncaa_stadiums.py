"""NCAA Football 07 stadiums that were NFL home stadiums, as My NFL Eras stadium packs (same route as the Coliseum).

    python tools/eras/stadiums/ncaa_stadiums.py list
    python tools/eras/stadiums/ncaa_stadiums.py build   [slug ...] [--only dd nd]   # bundles -> data/eras/stadiums/<slug>/build
    python tools/eras/stadiums/ncaa_stadiums.py preview [slug ...]                  # -> data/eras/stadiums/<slug>/preview
    python tools/eras/stadiums/ncaa_stadiums.py pack    [slug ...]                  # -> data/eras/stadiums/<slug>/pack/<Folder>
    python tools/eras/stadiums/ncaa_stadiums.py install [slug ...]                  # -> mods/eras/stadiums/<Folder>
    python tools/eras/stadiums/ncaa_stadiums.py textures [slug ...]                 # before/after sheets of the art pass

The conversion is coliseum_build.py's (docs/eras/research/COLISEUM-IMPORT.md), driven by a per-venue table:

* base bundles: the retail s24 (QUALCOMM STADIUM) bundles, for every venue (their field, cityscape, detail layer,
  intro cameras, sideline props and the neutral-art routines are the ones the Coliseum work fitted); the stadium
  ROW the pack clones (climate, indoor/grass words) is chosen per venue: an open-air grass venue of a similar
  climate, a dome (Louisiana Super Dome) for the Alamodome;
* team-neutral art (coliseum_neutral.py): end zones with the NFL team's city of that era, no midfield logo, plain
  goal-post pads, neutral fan banners, a backdrop in the venue's style; on the NCAA model itself, per texture:
  ``fan``   college flags / tarps / rail banners -> the neutral fan-banner materials,
  ``board`` scoreboards with college / EA logos -> a plain lamp scoreboard (alpha kept),
  ``flat``  walls and signs with college lettering -> their own median colour,
  ``rect``  part of a texture (fractions x0, y0, x1, y1) -> the median colour around it,
  ``drop``  decals (college logos, lettering, jerseys) -> not converted;
* NCAA ground inside the retail field's outside-grass rectangle is lifted 3 cm (or dropped under the playing
  field) so it does not fight the retail grass; the retail sideline props are squeezed inside the venue's wall.

Only the user's own discs are read; outputs stay in the gitignored data/eras/stadiums and mods/eras/stadiums.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import shutil
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
for p in (HERE, ROOT / "tools" / "team-creator" / "vendor", ROOT / "tools" / "team-creator"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

import coliseum_build as CB  # noqa: E402
import coliseum_neutral as CN  # noqa: E402
import ncaa_source as N  # noqa: E402

DATA = ROOT / "data" / "eras" / "stadiums"
MODS = ROOT / "mods" / "eras" / "stadiums"
RAW = N.NCAA_WORK / "stadiums_raw"
VARIANTS = CB.VARIANTS
BASE = "s24"

FLAT_CITY = dict(ridge=False, foothills=False, palms=0, conifers=0, tall=0.15, tall_max=12)


def _sky(label, **kw):
    d = dict(FLAT_CITY, seed=abs(hash(label)) % 10000, label=label)
    d.update(kw)
    return d


# The venues. entries: (day, night) STADIUMS.DAT entries (STAD.SRES * 12 + 0 / + 4). capacity: the NFL era's.
STADIUMS = [
    dict(slug="orange_bowl", folder="Miami_Orange_Bowl", code="s61", name="MIAMI ORANGE BOWL",
         display="Miami Orange Bowl", location="Miami, FL", capacity=75000, entries=(0x2F4, 0x2F8),
         ncaa="Miami Stadium (STAD.SRES 63, MIAMFL)", clone="Pro Player Stadium", host="Electra Coliseum",
         indoor=0, grass=1, endzone="MIAMI", tag="MIAMI", crowd="ORANGE BOWL",
         sky=_sky("Miami: flat city with palms", palms=16, seed=61),
         nfl="Dolphins 1966-1986",
         art={"RAIL": "fan", "RAIL~00": "fan", "RAIL~01": "fan", "MIAMI_WALL04_bm": "flat", "SB01": "board",
              "SB02": "board", "RAIL~07": "flat", "RAIL~08": "flat", "MASTER_Cannon_C": "drop"}),
    dict(slug="cotton_bowl", folder="Cotton_Bowl", code="s62", name="COTTON BOWL",
         display="Cotton Bowl", location="Dallas, TX", capacity=75504, entries=(0x864, 0x868),
         ncaa="Cotton Bowl, neutral-site dressing (STAD.SRES 179, NEUCOT)", clone="Arrowhead Stadium",
         host="Funk Field", indoor=0, grass=1, endzone="DALLAS", tag="DALLAS", crowd="COTTON BOWL",
         sky=_sky("Dallas: flat city", tall=0.2, seed=62), nfl="Cowboys 1960 - Oct 1971",
         art={"COTTON_upper~00": "flat", "COTTON_upper~01": "flat", "COTTON_sb2": "board", "RAIL~00": "fan",
              "RAIL": "fan"}),
    dict(slug="franklin_field", folder="Franklin_Field", code="s63", name="FRANKLIN FIELD",
         display="Franklin Field", location="Philadelphia, PA", capacity=60546, entries=(0x468, 0x46C),
         ncaa="Franklin Field (STAD.SRES 94, PENNSY)", clone="Lincoln Financial Field", host="Dream Superdome",
         indoor=0, grass=1, endzone="PHILADELPHIA", tag="PHILLY", crowd="FRANKLIN FIELD",
         sky=_sky("Philadelphia: flat city", tall=0.22, seed=63), nfl="Eagles 1958-1970",
         art={"PENNSY_bmp_FILE": "drop", "PENNSY_TEAMTEXT": "drop", "PENNSY_SCOREBOA": "board", "RAIL~01": "fan",
              "RAIL": "fan", "RAIL~00": "fan", "CROWDONHILL_PRO": ("rect", [(0.5, 0.0, 1.0, 0.5)])}),
    dict(slug="yale_bowl", folder="Yale_Bowl", code="s64", name="YALE BOWL",
         display="Yale Bowl", location="New Haven, CT", capacity=70869, entries=(0x6B4, 0x6B8),
         ncaa="Yale Bowl (STAD.SRES 143, YALEXX; EA's generic small-bowl model)", clone="Giants Stadium",
         host="Alien Arena", indoor=0, grass=1, endzone="NEW YORK", tag="NEW YORK", crowd="YALE BOWL",
         sky=_sky("New Haven: low town, wooded hills", foothills=True, tall=0.05, seed=64), nfl="Giants 1973-1974",
         art={"YALEXX_SCOREBOA": "board", "YALEXX_SCORE~00": "board", "RAIL~00": "fan", "RAIL~01": "fan",
              "RAIL~02": "fan"}),
    dict(slug="harvard_stadium", folder="Harvard_Stadium", code="s65", name="HARVARD STADIUM",
         display="Harvard Stadium", location="Boston, MA", capacity=40000, entries=(0x204, 0x208),
         ncaa="Harvard Stadium (STAD.SRES 43, HARVAR; EA's generic small-bowl model)", clone="Gillette Stadium",
         host="Atomic Dome", indoor=0, grass=1, endzone="BOSTON", tag="BOSTON", crowd="HARVARD STADIUM",
         sky=_sky("Boston: flat city", tall=0.2, seed=65), nfl="Patriots 1970",
         art={"YALEXX_SCOREBOA": "board", "YALEXX_SCORE~00": "board", "RAIL~00": "fan", "RAIL": "fan",
              "RAIL~01": "fan"}),
    dict(slug="alumni_stadium", folder="Alumni_Stadium", code="s66", name="ALUMNI STADIUM",
         display="Alumni Stadium", location="Chestnut Hill, MA", capacity=26000, entries=(0xCC, 0xD0),
         ncaa="Alumni Stadium (STAD.SRES 17, BOSTON)", clone="Gillette Stadium", host="Metro Field",
         indoor=0, grass=1, endzone="BOSTON", tag="BOSTON", crowd="ALUMNI STADIUM",
         sky=_sky("Chestnut Hill: low town, trees", foothills=True, tall=0.05, seed=66), nfl="Patriots 1969",
         art={"file4": "fan", "file5": "fan", "RAIL~11": "fan", "RAIL~12": "fan", "RAIL~00": "fan",
              "file2": "drop", "file3": "drop", "COLLEGE_bmp_FIL": "drop", "BOSTON_bmp_FILE": "drop",
              "ALUMNISTADIUM_b": "drop", "SHIRT_bmp_FILE": "drop", "RAIL~02": "drop", "RAIL~06": "drop",
              "RAIL~07": "drop", "RAIL~08": "drop", "RAIL~09": "drop", "RAIL~10": "drop",
              "TUNNELHANG_bmp_": "flat"}),
    dict(slug="nippert_stadium", folder="Nippert_Stadium", code="s67", name="NIPPERT STADIUM",
         display="Nippert Stadium", location="Cincinnati, OH", capacity=28000, entries=(0x114, 0x118),
         ncaa="Nippert Stadium (STAD.SRES 23, CINCIN)", clone="Paul Brown Stadium", host="Bat Field",
         indoor=0, grass=1, endzone="CINCINNATI", tag="CINCY", crowd="NIPPERT",
         sky=_sky("Cincinnati: river hills", foothills=True, tall=0.12, seed=67), nfl="Bengals 1968-1969",
         art={"RAIL~00": "fan", "RAIL~01": "fan", "RAIL~02": "fan", "file14": "board", "file15": "board",
              "file18": "board"}),
    dict(slug="rice_stadium", folder="Rice_Stadium", code="s68", name="RICE STADIUM",
         display="Rice Stadium", location="Houston, TX", capacity=70000, entries=(0x4A4, 0x4A8),
         ncaa="Rice Stadium (STAD.SRES 99, RICEXX)", clone="Tampa Bay Stadium", host="Axe Superdome",
         indoor=0, grass=1, endzone="HOUSTON", tag="HOUSTON", crowd="RICE STADIUM",
         sky=_sky("Houston: flat city", tall=0.15, palms=3, seed=68), nfl="Oilers 1965-1967",
         art={"file13": "board", "file35": "board", "RAIL~01": "fan", "RAIL~02": "fan", "RAIL~03": "fan"}),
    dict(slug="liberty_bowl", folder="Liberty_Bowl_Memorial_Stadium", code="s69",
         name="LIBERTY BOWL MEMORIAL STADIUM", display="Liberty Bowl Memorial Stadium", location="Memphis, TN",
         capacity=62380, entries=(0x768, 0x76C), ncaa="Liberty Bowl Memorial Stadium, bowl dressing (STAD.SRES 158, LIBERT)",
         clone="Titans Coliseum", host="Angry Dog Park", indoor=0, grass=1, endzone="TENNESSEE", tag="TENNESSEE",
         crowd="MEMPHIS", sky=_sky("Memphis: flat city", tall=0.1, seed=69), nfl="Oilers 1997",
         art={"MEMPHI_edgewall": "flat", "MEMPHI_sb2": "board", "MEMPHI_sb5": "board", "RAIL": "fan",
              "RAIL~00": "fan", "RAIL~01": "fan"}),
    dict(slug="vanderbilt_stadium", folder="Vanderbilt_Stadium", code="s70", name="VANDERBILT STADIUM",
         display="Vanderbilt Stadium", location="Nashville, TN", capacity=41000, entries=(0x63C, 0x640),
         ncaa="Vanderbilt Stadium (STAD.SRES 133, VANDER)", clone="Titans Coliseum", host="Express Dome",
         indoor=0, grass=1, endzone="TENNESSEE", tag="TENNESSEE", crowd="NASHVILLE",
         sky=_sky("Nashville: low hills", foothills=True, tall=0.12, seed=70), nfl="Oilers 1998",
         art={"RAIL~00": "fan", "RAIL": "fan", "VANDY_SIGN_V_1": "drop", "VANDY_SEC_1": "drop",
              "VANDY_SB_NORTH_": ("board", 0.0, 0.42), "VANDY_SB_SOUTH_": "board", "VANDY_SB_SOU~00": "board",
              "VANDY_SB_SOU~01": "board", "VANDY_JUMBOTRON": "flat", "VANDY_TUNNEL_BA": "flat"}),
    dict(slug="clemson_memorial", folder="Clemson_Memorial_Stadium", code="s71", name="CLEMSON MEMORIAL STADIUM",
         display="Memorial Stadium", location="Clemson, SC", capacity=81473, entries=(0x120, 0x124),
         ncaa="Clemson Memorial Stadium (STAD.SRES 24, CLEMSO)", clone="B of A Stadium", host="Cat Park",
         indoor=0, grass=1, endzone="CAROLINA", tag="CAROLINA", crowd="CLEMSON",
         sky=_sky("Clemson: Blue Ridge, small town", ridge=True, foothills=True, tall=0.03, seed=71),
         nfl="Panthers 1995",
         art={"SCOREBOARD01_bm": "board", "RAIL~00": "flat", "RAIL": "drop", "HOWARDSROCK_bmp": "drop",
              "RAIL~01": "fan", "RAIL~02": "fan", "RAIL~03": "fan",
              "CROWDONHILL_PRO": ("rect", [(0.5, 0.0, 1.0, 0.5)])}),
    dict(slug="husky_stadium", folder="Husky_Stadium", code="s72", name="HUSKY STADIUM",
         display="Husky Stadium", location="Seattle, WA", capacity=72500, entries=(0x66C, 0x670),
         ncaa="Husky Stadium (STAD.SRES 137, WASHIN)", clone="Qwest Field", host="Cobra Field",
         indoor=0, grass=1, endzone="SEATTLE", tag="SEATTLE", crowd="HUSKY STADIUM",
         sky=_sky("Seattle: Cascades and evergreens", ridge=True, foothills=False, conifers=20, tall=0.1, seed=72,
                  ridge_colour=(138, 150, 170)),
         nfl="Seahawks 2000-2001", shrink=128 * 128,
         art={"WASHIN_FENCE03_": "flat", "WASHIN_BUILD~01": "flat", "WASHIN_SCORE~01": "board",
              "WASHIN_SCOREBOA": "board", "WASHIN_SCORE~03": "board", "RAIL~01": "fan", "RAIL~02": "fan",
              "WASHIN_LIGHTS04": "fan", "WASHIN_LIGHTS03": "fan"}),
    dict(slug="memorial_champaign", folder="Memorial_Stadium_Champaign", code="s73", name="MEMORIAL STADIUM CHAMPAIGN",
         display="Memorial Stadium", location="Champaign, IL", capacity=70904, entries=(0x240, 0x244),
         ncaa="Memorial Stadium, Champaign (STAD.SRES 48, ILLINO)", clone="Chicago Field", host="Royal Arena",
         indoor=0, grass=1, endzone="CHICAGO", tag="CHICAGO", crowd="CHAMPAIGN",
         sky=_sky("Champaign: prairie town", tall=0.04, seed=73), nfl="Bears 2002",
         art={"ILLINO_BLDG_05_": "flat", "ILLINO_SB_1": "board", "RAIL~00": "fan", "RAIL~01": "fan",
              "RAIL~02": "fan"}),
    dict(slug="tiger_stadium", folder="Tiger_Stadium_Baton_Rouge", code="s74", name="TIGER STADIUM BATON ROUGE",
         display="Tiger Stadium", location="Baton Rouge, LA", capacity=91600, entries=(0x2C4, 0x2C8),
         ncaa="Tiger Stadium, Baton Rouge (STAD.SRES 59, LSUXXX)", clone="Tampa Bay Stadium", host="Dragon Dome",
         indoor=0, grass=1, endzone="NEW ORLEANS", tag="N.O.", crowd="BATON ROUGE",
         sky=_sky("Baton Rouge: flat town, oaks", tall=0.06, palms=2, seed=74), nfl="Saints 2005 (4 home games)",
         art={"PLAYERTUNNELTOP": "flat", "PLAYERTUNNELBAC": "flat", "TUNNELSIGN_bmp_": "flat",
              "SCOREBOARD_bmp_": "board", "RAIL~06": "fan", "RAIL~07": "fan", "RAIL~08": "fan"}),
    dict(slug="alamodome", folder="Alamodome", code="s75", name="ALAMODOME",
         display="Alamodome", location="San Antonio, TX", capacity=65000, entries=(0x6C0, 0x6C0),
         ncaa="Alamodome (STAD.SRES 144, ALAMOX; one model, day)", clone="Louisiana Super Dome", host="Gold Coast Dome",
         indoor=1, grass=0, endzone="NEW ORLEANS", tag="N.O.", crowd="ALAMODOME",
         sky=_sky("indoor: backdrop unseen", tall=0.15, seed=75), nfl="Saints 2005 (3 home games)",
         art={"ALAMOX_BED03": "flat", "ALAMOX_MED03": "flat", "ALAMOX_MED024": ("rect", [(0.0, 0.0, 1.0, 0.12)]),
              "ALAMOX_TED06": ("rect", [(0.0, 0.0, 1.0, 0.22)]), "ALAMOX_SB03": "board", "ALAMOX_SB09": "board",
              "ALAMOX_SB012": "board", "RAIL~00": "fan", "RAIL": "fan", "RAIL~01": "fan"}),
    dict(slug="ladd_peebles", folder="Ladd_Peebles_Stadium", code="s76", name="LADD-PEEBLES STADIUM",
         display="Ladd-Peebles Stadium", location="Mobile, AL", capacity=40646, entries=(0x78C, 0x790),
         ncaa="GMAC Bowl stadium (STAD.SRES 161, MOBILE)", clone="Tampa Bay Stadium", host="Scimitar Stadium",
         indoor=0, grass=1, endzone="MOBILE", tag="MOBILE", crowd="SENIOR BOWL",
         sky=_sky("Mobile: flat town with palms", tall=0.06, palms=8, seed=76), nfl="Senior Bowl (future)",
         art={"MOBILE_ealogo2": "drop", "MOBILE_sb2": "board", "RAIL": "fan", "MOBILE_wall2": "flat",
              "MOBILE_wall5": "flat", "MOBILE_wall8": "flat", "MOBILE_wall11": "flat", "MOBILE_wall13": "flat"}),
]
BY_SLUG = {s["slug"]: s for s in STADIUMS}

# NCAA parts never converted (as coliseum_build): helper meshes, runtime scoreboard digits, field light rigs
SKIP_PREFIXES = ("SB_", "CLK")
SKIP_SUBSTRINGS = ("LIGHTRIG",)
SKIP_MATERIALS = {"lambert1", "lambert3"}
CROWD = "STDCROWD_CROWD_"


# --- the per-texture art pass ---------------------------------------------------------------------------------

def _rule(st, tex):
    if tex is None:
        return None
    for k, v in st["art"].items():
        if tex == k or (len(k) >= 8 and tex.startswith(k)):
            return k, v
    return None


def _median_rgb(a, mask=None):
    px = a[..., :3][(a[..., 3] > 127) if mask is None else (mask & (a[..., 3] > 127))]
    return np.median(px, axis=0) if len(px) else np.array((120, 120, 120), np.float64)


def flat_art(a):
    out = a.copy()
    rng = np.random.default_rng(5)
    med = _median_rgb(a)
    noise = rng.normal(0, 3.0, a.shape[:2] + (1,))
    out[..., :3] = np.clip(med[None, None, :] + noise, 0, 255).astype(np.uint8)
    return out


def rect_art(a, rects):
    out = a.copy()
    h, w = a.shape[:2]
    rng = np.random.default_rng(6)
    for x0, y0, x1, y1 in rects:
        X0, Y0, X1, Y1 = int(x0 * w), int(y0 * h), max(int(x0 * w) + 1, int(math.ceil(x1 * w))), \
            max(int(y0 * h) + 1, int(math.ceil(y1 * h)))
        ring = np.zeros((h, w), bool)
        ring[max(0, Y0 - 3):min(h, Y1 + 3), max(0, X0 - 3):min(w, X1 + 3)] = True
        ring[Y0:Y1, X0:X1] = False
        inner = np.zeros((h, w), bool)
        inner[Y0:Y1, X0:X1] = True
        med = _median_rgb(a, ring) if ring.any() else _median_rgb(a, inner)
        n = rng.normal(0, 3.0, (Y1 - Y0, X1 - X0, 1))
        out[Y0:Y1, X0:X1, :3] = np.clip(med[None, None, :] + n, 0, 255).astype(np.uint8)
    return out


def board_art(a, y0f=0.0, y1f=1.0):
    """A plain lamp scoreboard over rows y0f..y1f of the texture (alpha kept): dark panel, HOME / VISITORS,
    QTR, amber lamp zeros. No college marks, no EA SPORTS panels."""
    from PIL import Image, ImageDraw
    h, w = a.shape[:2]
    Y0, Y1 = int(y0f * h), max(int(y0f * h) + 2, int(y1f * h))
    bh = Y1 - Y0
    scale = 4
    img = Image.new("RGB", (w * scale, bh * scale), (26, 34, 30))
    d = ImageDraw.Draw(img)
    W, H = img.size
    m = max(2, W // 64)
    d.rectangle((0, 0, W - 1, H - 1), outline=(150, 150, 140), width=m)
    size = max(8, int(H * 0.2))
    while size > 8 and CN._font("impact.ttf", size).getlength("VISITORS") > W * 0.36:
        size -= 2
    title = CN._font("impact.ttf", size)
    lamp = (255, 190, 70)
    for text, cx in (("HOME", 0.22), ("VISITORS", 0.78)):
        tw = d.textlength(text, font=title)
        d.text((W * cx - tw / 2, H * 0.08), text, font=title, fill=(235, 235, 225))
    # lamp digits "00" under each name, "QTR 1" in the middle
    def lamps(cx, cy, cell):
        for k in range(2):
            ox = cx + (k - 1) * cell * 4.2 + cell * 0.6
            for (gx, gy) in [(x, y) for y in range(5) for x in range(3) if not (x == 1 and 0 < y < 4)]:
                r = cell * 0.36
                px, py = ox + gx * cell, cy + gy * cell
                d.ellipse((px - r, py - r, px + r, py + r), fill=lamp)
    cell = max(2.0, min(W * 0.03, H * 0.09))
    lamps(W * 0.22, H * 0.45, cell)
    lamps(W * 0.78, H * 0.45, cell)
    small = CN._font("impact.ttf", max(6, int(H * 0.12)))
    for text, cy in (("QTR", 0.42), ("1", 0.58)):
        tw = d.textlength(text, font=small)
        d.text((W * 0.5 - tw / 2, H * cy), text, font=small, fill=lamp if text == "1" else (220, 220, 210))
    size = max(6, int(H * 0.1))
    while size > 6 and CN._font("impact.ttf", size).getlength("BALL ON") > W * 0.17:
        size -= 1
    small2 = CN._font("impact.ttf", size)
    for text, cx in (("DOWN", 0.3), ("TO GO", 0.5), ("BALL ON", 0.7)):
        tw = d.textlength(text, font=small2)
        d.text((W * cx - tw / 2, H * 0.84), text, font=small2, fill=(220, 220, 210))
    panel = np.asarray(img.resize((w, bh), Image.LANCZOS), np.uint8)
    out = a.copy()
    out[Y0:Y1, :, :3] = panel
    return out


def _thumb(im):
    from PIL import Image
    a = np.asarray(im.convert("RGBA").resize((16, 16), Image.BOX), np.float64)
    return a


def match_rules(st, day_src, src):
    """{texture of src: rule} by the day model's names; a night model numbers its textures differently
    (RAIL~07 by day can be RAIL~08 by night), so its textures inherit the rule of the day texture with the same
    size and nearly the same pixels."""
    day = {}
    for t, im in day_src.textures.items():
        r = _rule(st, t)
        if r is not None:
            day[t] = (im.size, _thumb(im), r)
    def related(a, b):                  # same texture under its night name (SB01 -> MIAMI_SB01_bmp_)
        if "~" in a or "~" in b:
            return a == b and not a.startswith("RAIL")
        a, b = a.upper().rstrip("_"), b.upper().rstrip("_")
        return a == b or (len(a) >= 4 and a in b) or (len(b) >= 4 and b in a)

    out = {}
    for t, im in src.textures.items():
        best = None
        for dt, (size, th, r) in day.items():
            if size != im.size:
                continue
            d = float(np.abs(_thumb(im) - th).mean())
            if best is None or d < best[0]:
                best = (d, r, dt)
        if best is not None and (best[0] < 10.0 or (best[0] < 45.0 and related(best[2], t))):
            out[t] = best[1]
    return out


def neutralise_textures(st, src, log, rules=None):
    """Repaint the textures the venue's art table names (flat / rect / board), in place. Returns
    {texture: action} for the 'fan' and 'drop' ones (classify() routes them) and the list of what happened.
    ``rules``: {texture: (key, action)} (match_rules) instead of matching the art table by name."""
    from PIL import Image
    route, done = {}, []
    for tname in list(src.textures):
        r = rules.get(tname) if rules is not None else _rule(st, tname)
        if r is None:
            continue
        key, act = r
        kind = act[0] if isinstance(act, tuple) else act
        if kind in ("fan", "drop"):
            route[tname] = kind
            done.append((tname, kind))
            continue
        a = np.asarray(src.textures[tname].convert("RGBA")).copy()
        if kind == "flat":
            a = flat_art(a)
        elif kind == "rect":
            a = rect_art(a, act[1])
        elif kind == "board":
            a = board_art(a, *(act[1:] if isinstance(act, tuple) else ()))
        src.textures[tname] = Image.fromarray(a, "RGBA")
        done.append((tname, kind))
    for tname, im in src.textures.items():      # baked shadow overlays (NCAA blends them; here they would be opaque)
        if tname in route:
            continue
        mats = {p.material.upper() for p in src.parts if p.texture == tname}
        a = np.asarray(im.convert("RGBA"), np.float64)
        if any("SHADOW" in m for m in mats) and (a[..., :3].std() < 4.0 or a[..., :3].mean() < 10.0):
            route[tname] = "shadow"
            done.append((tname, "shadow"))
    matched = {(rules.get(t) if rules is not None else _rule(st, t))[0] for t, k in done if k != "shadow"}
    missing = sorted(set(st["art"]) - matched)
    if missing and rules is None:
        log(f"  art rules with no texture in {src.name}: {missing}")
    if rules is not None:
        log(f"  {src.name}: {len(done)} textures matched to the day model's art rules: "
            + ", ".join(f"{t}={k}" for t, k in done))
    return route, done


# --- conversion (coliseum_build.convert_source, generalised) -------------------------------------------------------

def classify(st, part, counters, route):
    m, tex = part.material, part.texture
    if part.shader == "Flat" or m in SKIP_MATERIALS or m.startswith(SKIP_PREFIXES) or any(s in m for s in SKIP_SUBSTRINGS):
        return None
    if tex is not None and route.get(tex) in ("drop", "shadow"):
        return None
    if m.startswith(CROWD):
        return "crowd", "crowd", lambda u, v: (v, u)
    if m == "JUMBOTRON":
        return "jumbo_tron", "jumbo", lambda u, v: (0.625 * u, 0.875 * v)
    if m in CB.AD_BANNERS:
        cu, cv = CB.CORP_CELLS[counters["corp"] % len(CB.CORP_CELLS)]
        counters["corp"] += 1
        return "banner_corp", "banner", lambda u, v, cu=cu, cv=cv: (cu + 0.5 * CB._clamp01(u), cv + 0.25 * CB._clamp01(v))
    if tex is not None and route.get(tex) == "fan":
        name, su, sv = CB.FAN_BANNERS[counters["fan"] % len(CB.FAN_BANNERS)]
        half = 0.5 * (counters["fan"] // len(CB.FAN_BANNERS) % 2)
        counters["fan"] += 1
        return name, "banner", lambda u, v, su=su, sv=sv, half=half: (su * u, half + sv * CB._clamp01(v))
    if tex is None:
        return None
    up = tex.upper()
    lit = "LIGHT" in up and not any(s in up for s in ("POST", "POL", "POS", "STRUCT", "TOWER", "RIG"))
    key = ("LIGHT_" if lit else "") + CB.short_name(tex)
    return key, ("lit" if lit else "static"), None


FIELD_HALF = (2700.0, 5800.0)          # cm: the retail playing field + border (A_grass_color / overlays) + margin
GRASS_HALF = (7750.0, 11450.0)         # cm: the retail Outside_grass rectangle


def convert_source(st, src, tint, route, stats):
    tris, mats, counters = [], {}, collections.Counter()
    for part in src.parts:
        c = classify(st, part, counters, route)
        if c is None:
            continue
        key, kind, uvf = c
        mats.setdefault(key, (part.texture, kind))
        P = [CB.ncaa_to_2k5(p) for p in part.positions]
        zbias = part.texture is not None and part.texture.upper().startswith("ZBIAS")
        if zbias and part.normals:
            P = [(x - 3.0 * n[0], y + 3.0 * n[2], z + 3.0 * n[1]) for (x, y, z), n in zip(P, part.normals)]
        for f in part.faces:
            tp = [P[i] for i in f]
            if kind == "static" and max(p[1] for p in tp) < 12.0:          # flat ground at the field's level
                cx = sum(p[0] for p in tp) / 3.0
                cz = sum(p[2] for p in tp) / 3.0
                if abs(cx) < FIELD_HALF[0] and abs(cz) < FIELD_HALF[1]:
                    stats["ground_dropped"] += 1
                    continue
                if abs(cx) < GRASS_HALF[0] + 500 and abs(cz) < GRASS_HALF[1] + 500:
                    tp = [(x, y + 3.0, z) for x, y, z in tp]
                    stats["ground_lifted"] += 1
            uv = [part.uvs[i] for i in f]
            if uvf is not None:
                uv = [uvf(u, v) for u, v in uv]
            if kind in ("static", "lit"):
                du = math.floor(min(u for u, _ in uv))
                dv = math.floor(min(v for _, v in uv))
                uv = [(u - du, v - dv) for u, v in uv]
            elif kind == "banner" and key != "banner_corp":
                du = math.floor(min(u for u, _ in uv))
                uv = [(u - du, v) for u, v in uv]
            col = []
            for i in f:
                r, g, b, a = part.colours[i]
                if kind == "crowd":
                    r = g = b = 0.86
                elif kind == "jumbo":
                    r, g, b = 0.8, 0.9, 1.0
                col.append(tuple(int(max(0, min(255, round(255 * ch * t)))) for ch, t in zip((r, g, b), tint)) + (255,))
            tris.append(CB.Tri(key, tp, uv, col))
    return tris, mats


def venue_geometry(src):
    """The wall in front of the side stands (cm across) and the bowl's footprint (cityscape lawn ellipse)."""
    xs, ys = [], []
    near = []
    for part in src.parts:
        m = part.material
        if part.shader == "Flat" or m.startswith(SKIP_PREFIXES) or any(s in m for s in SKIP_SUBSTRINGS):
            continue
        for x, y, z in part.positions:
            xs.append(x)
            ys.append(y)
            if 0.8 <= z <= 6.0 and abs(y) < 40.0 and abs(x) > 27.5:
                near.append(abs(x))
    near.sort()
    wall = near[max(0, len(near) // 50)] * CB.YD if near else 6000.0      # 2nd percentile: ignore stray posts
    rx = max(abs(min(xs)), abs(max(xs))) * CB.YD + 1500.0
    rz = (max(ys) - min(ys)) / 2 * CB.YD + 1500.0
    cz = (max(ys) + min(ys)) / 2 * CB.YD
    return wall, (rx, rz, cz)


def configure(st, src, report):
    """Point coliseum_build / coliseum_neutral's module settings at this venue."""
    wall, ellipse = venue_geometry(src)
    CB.WALL_X = max(3300.0, min(5110.0, wall - 60.0))
    CB.PROP_START = min(3000.0, CB.WALL_X - 600.0)
    CB.GRASS_ELLIPSE = ellipse
    CB.SHAPE_PREFIX = st["code"]
    CB.EA_BOARD = None                  # the Coliseum's USC scoreboard fix; here the art table handles boards
    CB.BIG_TEXTURES = set()
    CN.ENDZONE_TEXT = st["endzone"]
    t, c = st["tag"], st["crowd"]
    crowd = c if len(c) <= 9 else c.split()[0]
    CN.BANNERS = {
        "banner_home_team": (1, 2, [(f"LET'S GO\n{t}!", (20, 30, 90)), ("DE-FENSE!", (150, 20, 20))]),
        "banner_away_team": (1, 2, [(f"WELCOME\nTO {t}", (20, 70, 30)), ("BEAT 'EM!", (20, 20, 20))]),
        "banner_home_player": (2, 2, [("TOUCH-\nDOWN!", (150, 20, 20)), ("WE LOVE\nFOOTBALL", (20, 30, 90)),
                                      ("#1\nFANS", (20, 20, 20)), (f"{crowd}\nCROWD", (20, 70, 30))]),
    }
    CN.SKYLINE = st["sky"]
    report.update(wall_cm=round(wall), WALL_X=CB.WALL_X, PROP_START=CB.PROP_START,
                  lawn_ellipse=[round(v) for v in ellipse])


# --- commands ------------------------------------------------------------------------------------------------------

def _entry(e):
    return RAW / f"{e:08X}.tmd"


def build(st, only=None, log=print):
    import game_assets as G
    archive = G.Archive(CB.DISC)
    out = DATA / st["slug"] / "build"
    out.mkdir(parents=True, exist_ok=True)
    day_e, night_e = st["entries"]
    sources = {"day": N.load(_entry(day_e))}
    sources["night"] = sources["day"] if night_e == day_e else N.load(_entry(night_e))
    report = dict(code=st["code"], base=BASE, name=st["name"], ncaa=st["ncaa"],
                  source=dict(day=str(_entry(day_e)), night=str(_entry(night_e))), bundles={})
    routes = {}
    night_rules = None if sources["night"] is sources["day"] else match_rules(st, sources["day"], sources["night"])
    routes["day"] = neutralise_textures(st, sources["day"], log)
    routes["night"] = routes["day"] if night_rules is None else         neutralise_textures(st, sources["night"], log, rules=night_rules)
    report["art"] = {k: v[1] for k, v in routes.items()}
    if st.get("shrink"):                 # size budget: halve the big textures (retail bundles reach 1.65 MB)
        from PIL import Image
        n = 0
        for src in {id(x): x for x in sources.values()}.values():
            for t, im in list(src.textures.items()):
                w, h = im.size
                if w * h >= st["shrink"]:                  # to 64 on the long side (the converter caps at 128)
                    k = 64.0 / max(w, h)
                    src.textures[t] = im.resize((max(8, int(w * k)), max(8, int(h * k))), Image.LANCZOS)
                    n += 1
        report["shrunk_textures"] = n
        log(f"  size budget: {n} textures of >= {st['shrink']} texels cut to 64 px")
    configure(st, sources["day"], report)
    dry = CN.retail_dry(archive, BASE)
    for v in only or VARIANTS:
        t, w = v[0], v[1]
        key = "night" if t == "n" else "day"
        src = sources[key]
        tint = tuple(x * y for x, y in zip(CB.TOD_TINT[t], CB.WEATHER_TINT[w]))
        stats = collections.Counter()
        tris, mats = convert_source(st, src, tint, routes[key][0], stats)
        retail = archive.read(f"{BASE}{v}.iff")
        rep = dict(ground=dict(stats))
        data = CB.build_bundle(retail, tris, mats, src.textures, rep, variant=v, dry=dry)
        path = out / f"{st['code']}{v}.iff"
        path.write_bytes(data)
        rep.update(file=path.name, size=len(data), retail_size=len(retail), sha256=hashlib.sha256(data).hexdigest())
        report["bundles"][v] = rep
        log(f"  {path.name}: {len(data)} bytes (retail {len(retail)}), {rep['new_shapes']} shapes, "
            f"{rep['new_vertices']} vertices, {rep['triangles']} triangles, ground {dict(stats)}")
    old = out / "build_report.json"
    if only and old.is_file():                       # keep the other variants' entries
        prev = json.loads(old.read_text(encoding="utf-8"))
        prev["bundles"].update(report["bundles"])
        report["bundles"] = prev["bundles"]
    old.write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")


PREVIEW_VIEWS = {"dd": ["blimp", "endzone", "sideline", "high_sideline", "midfield", "flyover"],
                 "nd": ["blimp", "endzone"], "ds": ["midfield"]}


def preview(st, log=print):
    import render_preview as R
    from PIL import Image
    out = DATA / st["slug"] / "preview"
    out.mkdir(parents=True, exist_ok=True)
    for v, views in PREVIEW_VIEWS.items():
        bundle = DATA / st["slug"] / "build" / f"{st['code']}{v}.iff"
        if not bundle.is_file():
            continue
        groups = R.load_groups(bundle)
        for view in views:
            img = R.render(groups, R.VIEWS[view], 960, 540, sky=(40, 45, 60) if v[0] == "n" else (150, 185, 225))
            p = out / f"{st['code']}{v}_{view}.png"
            Image.fromarray(img, "RGB").save(p)
            log(f"  {p.relative_to(ROOT)}")


def pack_json(st):
    return {"name": st["name"], "display": st["display"], "location": st["location"], "code": st["code"],
            "clone": st["clone"], "host": st["host"], "capacity": st["capacity"], "indoor": st["indoor"],
            "grass": st["grass"], "nfl": st["nfl"],
            "source": f"NCAA Football 07 {st['ncaa']}; tools/eras/stadiums/ncaa_stadiums.py"}


def pack(st, log=print):
    p = DATA / st["slug"] / "pack" / st["folder"]
    (p / "files").mkdir(parents=True, exist_ok=True)
    text = json.dumps(pack_json(st), indent=2)
    if len(text.encode()) >= 4000:
        raise SystemExit(f"{st['slug']}: stadium.json over the reader's 4 KB buffer")
    (p / "stadium.json").write_text(text, encoding="utf-8")
    for v in VARIANTS:
        shutil.copy2(DATA / st["slug"] / "build" / f"{st['code']}{v}.iff", p / "files" / f"{st['code']}{v}.iff")
    log(f"  pack: {p.relative_to(ROOT)}")


def install(st, log=print):
    src = DATA / st["slug"] / "pack" / st["folder"]
    dst = MODS / st["folder"]
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    log(f"  installed: {dst.relative_to(ROOT)}")


def textures(st, log=print):
    """Before / after sheet of every texture the art table touches (data/eras/stadiums/<slug>/preview/art.png)."""
    from PIL import Image, ImageDraw
    src0 = N.load(_entry(st["entries"][0]))
    src1 = N.load(_entry(st["entries"][0]))
    route, done = neutralise_textures(st, src1, log)
    cw = 132
    sheet = Image.new("RGB", (cw * 2 + 8, max(1, len(done)) * (cw + 14)), (50, 50, 50))
    dr = ImageDraw.Draw(sheet)
    for i, (t, kind) in enumerate(done):
        y = i * (cw + 14)
        dr.text((2, y), f"{t[:26]}  {kind}", fill=(255, 255, 255))
        for j, im in enumerate((src0.textures[t], src1.textures[t])):
            if j == 1 and kind in ("fan", "drop", "shadow"):
                continue
            th = im.copy()
            th.thumbnail((cw - 4, cw - 4))
            bg = Image.new("RGB", th.size, (255, 0, 255))
            bg.paste(th, mask=th.split()[3])
            sheet.paste(bg, (2 + j * (cw + 4), y + 12))
    out = DATA / st["slug"] / "preview"
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / "art.png")
    log(f"  {out / 'art.png'}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["list", "build", "preview", "pack", "install", "textures", "all"])
    ap.add_argument("slugs", nargs="*")
    ap.add_argument("--only", nargs="*", default=None)
    a = ap.parse_args(argv)
    todo = [BY_SLUG[s] for s in a.slugs] if a.slugs else STADIUMS
    if a.cmd == "list":
        for s in STADIUMS:
            print(f"{s['code']} {s['slug']:20s} {s['name']:30s} host {s['host']:18s} clone {s['clone']:24s} {s['nfl']}")
        return
    for st in todo:
        print(f"== {st['code']} {st['name']}", flush=True)
        steps = ["build", "preview", "pack", "install"] if a.cmd == "all" else [a.cmd]
        for step in steps:
            try:
                if step == "build":
                    build(st, a.only, log=lambda s: print(s, flush=True))
                else:
                    globals()[step](st, log=lambda s: print(s, flush=True))
            except Exception as e:                      # one venue failing does not stop the others
                import traceback
                traceback.print_exc()
                print(f"  FAILED {step}: {e!r}", flush=True)
                break


if __name__ == "__main__":
    main()

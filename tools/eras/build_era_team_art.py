"""My NFL Eras (M9): the Eras-only texture replacements for a season's team art, built from the
player's own disc (and the user's supplied Bengals logo). Written to

    mods\eras\teams\<season>\art\<HASH>_<what>.png      (gitignored: disc pixels)

and used only while an Eras game is launched or an Eras depth chart is open
(src/nfl2k5_texture_packs.cpp nfl2k5_texpack_eras_teams, src/nfl2k5_eras.c
nfl2k5_eras_team_art_update). HASH is the texture's content hash as the game uploads it
(build/eras/office/office_inventory.py content_hash; the values are in build/eras/logos/hashes.json,
checked in game with one texture-dump run).

    python -I tools/eras/build_era_team_art.py <extracted dir> [--season 1980] [--check]

<extracted dir> holds logos\logo_<code>_<style>.png (256), mini\logo_s<code>_<style>.png (64) and
hashes.json, as written by build/eras/logos/cdf.py and hashes.py (docs/eras/research/ERA-LOGOS.md).

What is replaced (docs/eras/research/ERA-LOGOS.md section 3):
  * the roster / depth chart / lineup card chip <code>_flipchip_00_h<S> (one picture per club, 2004
    art) of the clubs whose chip is not their 1980 look -> the club's 1980 logo, fitted to the chip;
  * the modern style logos of PHI style 5 and CHI style 1 (logo_ and logo_s) -> the same club's
    period logo from another of its styles (PHI 6, CHI 0);
  * the Bengals' logo_06_3 / logo_s06_3 and their chip -> the user's 1980 image
    (mods\eras\teams\1980\bengals\logos\scorebug.png, or logo.png).
"""
import argparse, json, os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# 1980: eras id -> (disc code, the logo style that is the 1980 logo). From ERA-LOGOS.md.
LOGO_1980 = {
    'dolphins': ('14', 3), 'patriots': ('16', 6), 'jets': ('19', 3), 'bengals': ('06', None),
    'oilers_titans': ('28', 2), 'broncos': ('08', 1), 'seahawks': ('26', 2), 'giants': ('18', 3),
    'eagles': ('21', 6), 'buccaneers': ('27', 3), 'falcons': ('01', 6), 'rams': ('23', 4),
    # "close" 2003/2002 chips (ERA-LOGOS.md): the 1980 logo instead, so every chip is the 1980 look
    'lions': ('09', 3), 'chargers': ('24', 4),
}
# style logos that are modern art on the disc: (member) -> (source member)
LOGO_SWAPS = {'logo_21_5': 'logo_21_6', 'logo_s21_5': 'logo_s21_6',
              'logo_05_1': 'logo_05_0', 'logo_s05_1': 'logo_s05_0'}
USER_LOGO = {'bengals': ['logo_06_3', 'logo_s06_3']}


def fit(img, size, margin=0.06):
    """The logo's opaque part, centred on a transparent square of `size`."""
    img = img.convert('RGBA')
    bb = img.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()
    if bb:
        img = img.crop(bb)
    s = max(img.size)
    S = int(round(s * (1 + 2 * margin)))
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.paste(img, ((S - img.width) // 2, (S - img.height) // 2), img)
    return out.resize((size, size), Image.LANCZOS)


def user_logo(season, tid):
    d = os.path.join(ROOT, 'mods', 'eras', 'teams', str(season), tid)
    for p in (os.path.join(d, 'logo.png'), os.path.join(d, 'logos', 'scorebug.png')):
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src')
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--check', action='store_true', help='list what would be written and what is missing')
    a = ap.parse_args()
    if a.season != 1980:
        sys.exit('only 1980 has a team art table (docs/eras/research/ERA-LOGOS.md)')
    hashes = json.load(open(os.path.join(a.src, 'hashes.json'), encoding='utf-8'))
    out_dir = os.path.join(ROOT, 'mods', 'eras', 'teams', str(a.season), 'art')
    plan, missing = [], []

    def chip_hash(code):
        hs = sorted({v[0] for k, v in hashes.items() if k.startswith(code + '_flipchip_')})
        assert len(hs) == 1, (code, hs)   # one picture per club (ERA-LOGOS.md)
        return hs[0]

    for tid, (code, style) in LOGO_1980.items():
        if style is None:
            src = user_logo(a.season, tid)
        else:
            src = os.path.join(a.src, 'logos', 'logo_%s_%d.png' % (code, style))
        if not src or not os.path.exists(src):
            missing.append('%s chip: %s' % (tid, src)); continue
        plan.append((chip_hash(code), '%s_chip' % tid, src, 128, 0.04))
    for member, source in LOGO_SWAPS.items():
        size = hashes[member][1]
        src = os.path.join(a.src, 'logos' if member.startswith('logo_') and not member.startswith('logo_s') else 'mini', source + '.png')
        if not os.path.exists(src):
            missing.append('%s: %s' % (member, src)); continue
        plan.append((hashes[member][0], member, src, size, None))
    for tid, members in USER_LOGO.items():
        src = user_logo(a.season, tid)
        for member in members:
            if not src:
                missing.append('%s: no user logo for %s' % (member, tid)); continue
            plan.append((hashes[member][0], member, src, hashes[member][1] * 2, 0.04))

    for h, what, src, size, margin in plan:
        print('%s %-16s <- %s (%d px)' % (h, what, os.path.relpath(src, ROOT) if src.startswith(ROOT) else src, size))
    for m in missing:
        print('MISSING', m)
    if a.check:
        return
    os.makedirs(out_dir, exist_ok=True)
    for h, what, src, size, margin in plan:
        img = Image.open(src).convert('RGBA')
        img = fit(img, size, margin) if margin is not None else img.resize((size, size), Image.LANCZOS)
        img.save(os.path.join(out_dir, '%s_%s.png' % (h, what)))
    print('wrote %d textures to %s' % (len(plan), os.path.relpath(out_dir, ROOT)))


if __name__ == '__main__':
    main()

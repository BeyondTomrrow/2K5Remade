"""Extract each 1980 Eras team's era-correct logo from the player's own disc into
mods\eras\teams\1980\<id>\ (logos\scorebug.png, logos\mini.png) and write team.json for the
presentation (scorebug) lookup.

    python tools/eras/extract_era_logos.py <dir with extracted logos\ and mini\ PNGs>

The PNG folders come from build/eras/logos/cdf.py (logos.cdf -> logos\logo_<code>_<style>.png,
mini.cdf -> mini\logo_s<code>_<style>.png). Code/style per team: docs/eras/research/ERA-LOGOS.md.
A user-supplied logos\scorebug.png (or logo.png) in a team folder is kept, never overwritten.
Colours: the teams' 1980 colours (needs_check where noted). Disc pixels are gitignored.
"""
import csv, json, os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# id: (disc code, logo style, primary, secondary)
LOOK = {
 'colts': ('11', 5, '#003A70', '#FFFFFF'), 'bills': ('03', 4, '#00338D', '#C60C30'),
 'dolphins': ('14', 3, '#008E97', '#F26A24'), 'patriots': ('16', 6, '#C8102E', '#002244'),
 'jets': ('19', 3, '#0C371D', '#FFFFFF'), 'bengals': ('06', 3, '#FB4F14', '#000000'),
 'browns': ('30', 1, '#311D00', '#FF3C00'), 'oilers_titans': ('28', 2, '#4B92DB', '#C8102E'),
 'steelers': ('22', 1, '#101820', '#FFB612'), 'broncos': ('08', 1, '#FB4F14', '#002244'),
 'chiefs': ('13', 3, '#E31837', '#FFB81C'), 'raiders': ('20', 0, '#000000', '#A5ACAF'),
 'chargers': ('24', 4, '#002244', '#FFC20E'), 'seahawks': ('26', 2, '#002244', '#69BE28'),
 'cowboys': ('07', 3, '#003594', '#869397'), 'giants': ('18', 3, '#0B2265', '#A71930'),
 'eagles': ('21', 6, '#004C54', '#A5ACAF'), 'cardinals': ('00', 3, '#97233F', '#FFFFFF'),
 'washington': ('29', 2, '#5A1414', '#FFB612'), 'bears': ('05', 0, '#0B162A', '#C83803'),
 'lions': ('09', 3, '#0076B6', '#B0B7BC'), 'packers': ('10', 2, '#203731', '#FFB612'),
 'vikings': ('15', 2, '#4F2683', '#FFC62F'), 'buccaneers': ('27', 3, '#F26522', '#C8102E'),
 'falcons': ('01', 6, '#A71930', '#000000'), 'rams': ('23', 4, '#002244', '#FFD100'),
 'saints': ('17', 5, '#9F8958', '#000000'), '49ers': ('25', 3, '#AA0000', '#B3995D'),
}

def square(img, size=512):
    img = img.convert('RGBA'); bb = img.getbbox()
    if bb: img = img.crop(bb)
    s = max(img.size); pad = int(s * 0.04); S = s + 2 * pad
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0)); out.paste(img, ((S - img.width) // 2, (S - img.height) // 2), img)
    return out.resize((size, size), Image.LANCZOS)

def main(src):
    teams = {r['id']: r for r in csv.DictReader(open(os.path.join(ROOT, 'data', 'eras', 'runtime', '1980', 'teams.csv'), encoding='utf-8'))}
    for tid, (code, style, pri, sec) in LOOK.items():
        t = teams[tid]
        d = os.path.join(ROOT, 'mods', 'eras', 'teams', '1980', tid); os.makedirs(os.path.join(d, 'logos'), exist_ok=True)
        user = [p for p in (os.path.join(d, 'logos', 'scorebug.png'), os.path.join(d, 'logo.png')) if os.path.exists(p)]
        note = ''
        if os.path.join(d, 'logo.png') in user and not os.path.exists(os.path.join(d, 'logos', 'scorebug.png')):
            square(Image.open(os.path.join(d, 'logo.png'))).save(os.path.join(d, 'logos', 'scorebug.png')); note = 'user logo.png'
        elif not user:
            p = os.path.join(src, 'logos', 'logo_%s_%d.png' % (code, style))
            if os.path.exists(p): square(Image.open(p)).save(os.path.join(d, 'logos', 'scorebug.png')); note = 'disc logo_%s_%d' % (code, style)
            else: note = 'MISSING'
        else: note = 'user scorebug.png kept'
        m = os.path.join(src, 'mini', 'logo_s%s_%d.png' % (code, style))
        if os.path.exists(m): square(Image.open(m), 128).save(os.path.join(d, 'logos', 'mini.png'))
        j = {'abbreviation': t['abbr'], 'city': t['city'], 'name': t['name'], 'season': 1980, 'eras_id': tid,
             'colors': {'primary': pri, 'secondary': sec, 'text': '#FFFFFF'},
             'logos': {'scorebug': 'logos/scorebug.png', 'mini': 'logos/mini.png'},
             'source': note}
        json.dump(j, open(os.path.join(d, 'team.json'), 'w', encoding='utf-8'), indent=2)
        print('%-14s %-4s %s' % (tid, t['abbr'], note))

if __name__ == '__main__':
    main(sys.argv[1])

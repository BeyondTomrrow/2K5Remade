ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, os, json, glob
from PIL import Image, ImageDraw, ImageFont
codes = [('colts','11','BAL'),('bills','03','BUF'),('dolphins','14','MIA'),('patriots','16','NE'),('jets','19','NYJ'),('bengals','06','CIN'),('browns','30','CLE'),('oilers','28','HOU'),('steelers','22','PIT'),('broncos','08','DEN'),('chiefs','13','KC'),('raiders','20','OAK'),('chargers','24','SD'),('seahawks','26','SEA'),('cowboys','07','DAL'),('giants','18','NYG'),('eagles','21','PHI'),('cardinals','00','STL'),('redskins','29','WAS'),('bears','05','CHI'),('lions','09','DET'),('packers','10','GB'),('vikings','15','MIN'),('buccaneers','27','TB'),('falcons','01','ATL'),('rams','23','LA'),('saints','17','NO'),('49ers','25','SF')]
hist = json.load(open(__import__("os").path.join(ROOT, r'data\eras\historic_teams_2k5.json')))['teams']
hv = {}
for e in hist: hv.setdefault((e['asset_code'], e['variant']), []).append(str(e['season']))
mode = sys.argv[1]   # 'mini' or 'logos' (helm_h) or 'logo' (logo_)
sel = sys.argv[2].split(',') if len(sys.argv) > 2 and sys.argv[2] else None
pat = {'mini': 'mini/mini_a%s_%d.png', 'helm': 'logos/helm_h%s_%d.png', 'logo': 'logos/logo_%s_%d.png', 'slogo':'mini/logo_s%s_%d.png', 'unif':'logos/unif_h%s_%d.png', 'flip':'flipchip/%s_flipchip_00_h%d.png', 'unifa':'logos/unif_a%s_%d.png'}[mode]
C = int(os.environ.get("CS","96")); L = 90
rows = [c for c in codes if not sel or c[2] in sel]
img = Image.new('RGB', (L + 14 * C, len(rows) * (C + 18)), (60, 60, 60))
d = ImageDraw.Draw(img)
try: f = ImageFont.truetype('arial.ttf', 12)
except: f = ImageFont.load_default()
for r, (n, code, ab) in enumerate(rows):
    y = r * (C + 18)
    d.text((4, y + 30), '%s %s' % (ab, code), fill=(255, 255, 255), font=f)
    for v in range(14):
        p = pat % (code, v)
        if not os.path.exists(p): continue
        t = Image.open(p).convert('RGBA'); t.thumbnail((C, C))
        bg = Image.new('RGBA', (C, C), (60, 60, 60, 255)); bg.alpha_composite(t, ((C - t.width)//2, (C - t.height)//2))
        img.paste(bg.convert('RGB'), (L + v * C, y))
        lab = 'v%d %s' % (v, '/'.join(hv.get((code, v), [])))
        d.text((L + v * C + 2, y + C + 2), lab, fill=(255, 255, 0) if (code, v) in hv else (220, 220, 220), font=f)
img.save(sys.argv[3] if len(sys.argv) > 3 else mode + '_sheet.png')

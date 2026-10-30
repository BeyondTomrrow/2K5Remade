ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import os
from PIL import Image, ImageDraw, ImageFont
# abbr, team, code, style S, years of S (disc table), current template style, era logo source (code,style), status
T = [
('BAL','Colts','11',5,'1968-70',5,('11',5),'OK'),
('BUF','Bills','03',4,'1976-83',2,('03',4),'FIX: style 4 (now red 1987-02 helmet)'),
('MIA','Dolphins','14',3,'1979-80',3,('14',3),'OK'),
('NE','Patriots','16',6,'1975-81',5,('16',6),'closer: style 6 (now 5, 1984-87)'),
('NYJ','Jets','19',3,'1978-85',4,('19',3),'FIX: style 3 (now 1965-68 white)'),
('CIN','Bengals','06',3,'1968-78*',1,None,'FIX: style 3 (now stripes); LOGO GAP'),
('CLE','Browns','30',1,'1983-84',1,('30',1),'OK'),
('HOU','Oilers','28',2,'1974-75',2,('28',2),'OK (chip/scorebug Titans now)'),
('PIT','Steelers','22',1,'1968-76',1,('22',1),'OK'),
('DEN','Broncos','08',1,'1985-96',2,('08',1),'OK (1 or 2)'),
('KC','Chiefs','13',3,'1970-71',4,('13',3),'OK (3 or 4)'),
('OAK','Raiders','20',0,'current',0,('20',0),'OK'),
('SD','Chargers','24',4,'1978-79',4,('24',4),'OK'),
('SEA','Seahawks','26',2,'1976-82',1,('26',2),'closer: style 2 (now 1983-02)'),
('DAL','Cowboys','07',3,'1977-80',3,('07',3),'OK'),
('NYG','Giants','18',3,'1976-80',2,('18',3),'closer: style 3 (now 1981-99)'),
('PHI','Eagles','21',5,'1979-80',5,('21',6),'OK; logo from style 6'),
('STL','Cardinals','00',3,'1971-72',3,('00',3),'OK'),
('WAS','Redskins','29',2,'1982',2,('29',2),'OK (mask grey, 1980 gold)'),
('CHI','Bears','05',1,'1974-82',1,('05',0),'OK; logo "C" from style 0'),
('DET','Lions','09',3,'1980-93',3,('09',3),'OK'),
('GB','Packers','10',2,'1983-84',3,('10',2),'closer: style 2 (green mask)'),
('MIN','Vikings','15',2,'1980-84',3,('15',2),'closer: style 2 (now 1972-73)'),
('TB','Buccaneers','27',3,'1976-91',3,('27',3),'OK'),
('ATL','Falcons','01',6,'1978-79',6,('01',6),'OK'),
('LA','Rams','23',4,'1978-79',4,('23',4),'OK'),
('NO','Saints','17',5,'1976-82',4,('17',5),'closer: style 5 (now 1991-92)'),
('SF','49ers','25',3,'1964-95',3,('25',3),'OK'),
]
C = 92; COLS = 6; TXT = 250; BW = TXT + COLS * C + 16; BH = C + 30
f = ImageFont.truetype('arialbd.ttf', 15); fs = ImageFont.truetype('arial.ttf', 12); ft = ImageFont.truetype('arialbd.ttf', 22)
rows = (len(T) + 1) // 2
img = Image.new('RGB', (2 * BW, 70 + rows * BH + 40), (34, 36, 40))
d = ImageDraw.Draw(img)
d.text((12, 10), 'My NFL Eras 1980: era-correct team art on the NFL 2K5 disc (recommended uniform style per team)', fill=(255, 255, 255), font=ft)
heads = ['helmet', 'mini helmet', 'era logo', 'small logo', 'uniform', 'card chip now']
for k in range(2):
    for j, h in enumerate(heads):
        d.text((k * BW + TXT + j * C + 4, 44), h, fill=(200, 200, 200), font=fs)
def tile(p, ok=True):
    t = Image.new('RGBA', (C - 4, C - 4), (58, 60, 66, 255))
    if p and os.path.exists(p):
        a = Image.open(p).convert('RGBA'); a.thumbnail((C - 8, C - 8))
        t.alpha_composite(a, ((C - 4 - a.width) // 2, (C - 4 - a.height) // 2))
    else:
        dd = ImageDraw.Draw(t); dd.text((10, 30), 'not on disc', fill=(255, 120, 120), font=fs)
    if not ok:
        dd = ImageDraw.Draw(t); dd.rectangle((0, 0, C - 5, C - 5), outline=(220, 60, 60), width=3)
    return t.convert('RGB')
for i, (ab, nm, code, S, yrs, cur, lg, st) in enumerate(T):
    k, r = i // rows, i % rows
    x, y = k * BW, 66 + r * BH
    col = (120, 220, 120) if st.startswith('OK') else (255, 200, 80) if st.startswith('closer') else (255, 110, 110)
    d.text((x + 10, y + 6), '%s %s  (code %s)' % (ab, nm, code), fill=(255, 255, 255), font=f)
    d.text((x + 10, y + 28), 'style %d  disc years %s' % (S, yrs), fill=(210, 210, 210), font=fs)
    d.text((x + 10, y + 44), 'template now: style %d' % cur, fill=(210, 210, 210), font=fs)
    d.text((x + 10, y + 62), st, fill=col, font=fs)
    ps = ['logos/helm_h%s_%d.png' % (code, S), 'mini/mini_a%s_%d.png' % (code, S),
          ('logos/logo_%s_%d.png' % lg) if lg else None, ('mini/logo_s%s_%d.png' % lg) if lg else 'mini/logo_s%s_%d.png' % (code, S),
          'logos/unif_h%s_%d.png' % (code, S), 'flipchip/%s_flipchip_00_h%d.png' % (code, S)]
    oks = [True, True, lg is not None, code != '06', True, ab not in ('MIA','NE','NYJ','CIN','HOU','DEN','SEA','NYG','PHI','TB','ATL','LA')]
    for j, p in enumerate(ps):
        img.paste(tile(p, oks[j]), (x + TXT + j * C, y))
d.text((12, img.height - 30), 'Source: retail disc (logos.cdf helm_h/logo_/unif_h, mini.cdf mini_a/logo_s, flipchip.cdf), style years from the main roster uniform table (+0x15A). '
       '* CIN style 3 is labelled 1968-78 but has the 1980 black facemask. Red frame = not era-correct. Retail art: do not commit or share.', fill=(170, 170, 170), font=fs)
os.makedirs(__import__("os").path.join(ROOT, r'logs\eras'), exist_ok=True)
img.save(__import__("os").path.join(ROOT, r'logs\eras\era-logos-sheet.png'))
print(img.size)

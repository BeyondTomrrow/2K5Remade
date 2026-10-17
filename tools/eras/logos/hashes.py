ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, struct, importlib.util, json; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__))); sys.path.insert(0, __import__("os").path.join(ROOT, r'tools\team-creator\vendor'))
from arc import get
import nfl_txtr as T
spec = importlib.util.spec_from_file_location('oi', __import__("os").path.join(ROOT, r'tools\eras\research\office\office_inventory.py')); oi = importlib.util.module_from_spec(spec)
try: spec.loader.exec_module(oi)
except SystemExit: pass
codes = '11 03 14 16 19 06 30 28 22 08 13 20 24 26 07 18 21 00 29 05 09 10 15 27 01 23 17 25'.split()
out = {}
for base in ('flipchip', 'logos', 'mini'):
    ix = get(base+'.iff'); count, slot = struct.unpack_from('<II', ix, 0x60); data = get(base+'.cdf')
    for i in range(count):
        s = data[i*slot:(i+1)*slot]
        if s[:4] != b'TXTR': continue
        c = T.parse_chunks(s, allow_trailing=True)[0]; dec,_ = T.decode_chunk(s, c); t = T.parse_texture(dec, c)
        nm = t.name
        if base == 'logos' and not nm.startswith('logo_'): continue
        if base == 'mini' and not nm.startswith('logo_s'): continue
        code = nm.split('_')[0] if base == 'flipchip' else (nm[6:8] if base=='mini' else nm.split('_')[1])
        if code not in codes: continue
        video = dec[c.system_bytes:c.system_bytes+c.video_bytes]
        out[nm] = (oi.content_hash(video, t.pixel_offset, t.palette_offset, t.width, t.height, t.format_code), t.width, t.height, t.format_code)
json.dump(out, open('hashes.json','w'), indent=0)
from collections import defaultdict
inv = defaultdict(list)
for k,(h,*_) in out.items(): inv[h].append(k)
for code in codes:
    fl = sorted({out[k][0] for k in out if k.startswith(code+'_flipchip')})
    print(code, 'flipchip hashes', fl)
cross = [v for v in inv.values() if len({(x.split('_')[0] if 'flipchip' in x else x) for x in v}) > 1 and len({x[:2] if 'flipchip' in x else x.split('_')[1][-2:] for x in v})>1]
print('cross-team shared hashes:', cross[:5])
for k in ['logo_21_5','logo_05_1','logo_06_3','logo_21_6','logo_05_0']: print(k, out.get(k))

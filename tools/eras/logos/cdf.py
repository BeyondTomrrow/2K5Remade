ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, os, struct, pathlib; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__))); sys.path.insert(0, __import__("os").path.join(ROOT, r'tools\team-creator\vendor'))
from arc import get, has
import nfl_txtr as T
base, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
ix = get(base+'.iff'); count, slot = struct.unpack_from('<II', ix, 0x60)
data = get(base+'.cdf') if has(base+'.cdf') else None
print(base, 'count', count, 'slot', slot, 'cdf', len(data) if data else None)
names=[]
for i in range(count):
    s = data[i*slot:(i+1)*slot]
    if s[:4] != b'TXTR': names.append((i,'?',0,0)); continue
    try:
        c = T.parse_chunks(s, allow_trailing=True)[0]
        out,_ = T.decode_chunk(s,c); t = T.parse_texture(out,c); rgba = T.texture_to_rgba(out,c,t)
        T.write_png(pathlib.Path(outdir)/(T.safe_texture_name(t.name,'s%d'%i)+'.png'), t.width,t.height,rgba)
        names.append((i,t.name,t.width,t.height))
    except Exception as e: names.append((i,'ERR '+str(e)[:60],0,0))
open(os.path.join(outdir,'_names.txt'),'w').write('\n'.join('%d %s %dx%d'%n for n in names))
print(' '.join(n[1] for n in names))

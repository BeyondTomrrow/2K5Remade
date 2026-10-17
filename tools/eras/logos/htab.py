ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, struct, json; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__))); sys.path.insert(0, __import__("os").path.join(ROOT, r'tools\team-creator\vendor'))
from arc import get, has
import nfl_txtr as T
hist = json.load(open(__import__("os").path.join(ROOT, r'data\eras\historic_teams_2k5.json')))['teams']
def rel(d,a): return a + struct.unpack_from('<i', d, a)[0] - 1
want = sys.argv[1].split(',')
for e in hist:
    if e['id'] not in want: continue
    n = 'h-%s-%d-%s-%d.iff' % (e['asset_code'], e['season'], e['franchise'], e['variant'])
    if not has(n): print(e['id'], 'missing', n); continue
    raw = get(n); c = [c for c in T.parse_chunks(raw, allow_trailing=True) if c.kind=='ROST'][0]
    body,_ = T.decode_chunk(raw, c)
    table = rel(body, 0x5C); at = table
    pairs = [struct.unpack_from('<HH', body, at+0x15A+4*k) for k in range(14)]
    print(e['id'], n, 'style', body[at+0x192], ' '.join('%d:%d-%d'%(k+1,a,b) for k,(a,b) in enumerate(pairs) if a or b))

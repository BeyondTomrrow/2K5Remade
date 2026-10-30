import sys, re, json, unicodedata
wiki, draft = sys.argv[1], sys.argv[2]
t = open(wiki, encoding='utf-8').read()
rows = {}
for m in re.finditer(r'\{\{NFLDraft-row(.*?)\}\}\s*(?:<ref|\n)', t, re.S):
    body = re.sub(r'\[\[[^\]|]*\|([^\]]*)\]\]', lambda mm: mm.group(1), m.group(1))
    f = {}
    for kv in re.finditer(r'\|\s*([a-z ]+?)\s*=\s*([^|]*)', body):
        f[kv.group(1).strip()] = kv.group(2).strip()
    if 'picknum' in f and f['picknum'].isdigit():
        rows[int(f['picknum'])] = f
def norm(s):
    s = unicodedata.normalize('NFKD', s).encode('ascii','ignore').decode().lower()
    s = re.sub(r'\b(jr|sr|ii|iii|iv)\b\.?', '', s)
    return re.sub(r'[^a-z]', '', s)
d = json.load(open(draft, encoding='utf-8'))
mism = []
for p in d['picks']:
    w = rows.get(p['pick'])
    if not w: mism.append((p['pick'], 'missing in wiki')); continue
    wn = (w.get('first','') + ' ' + w.get('last','')).strip()
    if norm(wn) != norm(p['player']) and norm(w.get('last','')) not in norm(p['player']):
        mism.append((p['pick'], p['player'], wn, p['team'], w.get('team')))
    p['wiki_position'] = w.get('position')
    p['wiki_team'] = w.get('team')
    if norm(wn) != norm(p['player']): p['wiki_name'] = wn
print('wiki rows', len(rows), 'mismatches', len(mism))
for x in mism: print(x)
d['source']['cross_check'] = {"url": "https://en.wikipedia.org/wiki/1981_NFL_draft", "fetched": "2026-10-16",
  "method": "wikitext NFLDraft-row templates compared pick-by-pick (normalized name)", "wiki_rows": len(rows), "name_mismatches": len(mism),
  "mismatch_detail": [list(map(str,x)) for x in mism],
  "notes": "wiki_position/wiki_team copied from Wikipedia (finer positions than PFR's DB/LB/DL). Wikipedia lists ATL rd7 and SD rd9 as forfeited (1980 supplemental draft); PFR shows 27 picks in rounds 3,5,7,9."}
json.dump(d, open(draft,'w',encoding='utf-8'), indent=1, ensure_ascii=False)
from collections import Counter
print(Counter(p.get('wiki_position') for p in d['picks']).most_common())

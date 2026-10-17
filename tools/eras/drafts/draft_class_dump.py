ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json, importlib.util
sys.path.insert(0, __import__("os").path.join(ROOT, r'build\eras'))
spec = importlib.util.spec_from_file_location('ecr', __import__("os").path.join(ROOT, r'tools\eras\extract_community_rosters.py'))
ecr = importlib.util.module_from_spec(spec); spec.loader.exec_module(ecr)
rr = ecr.rr
out = {}
for stem, season in [(a, int(b)) for a, b in (x.split(':') for x in sys.argv[2:])]:
    save = ecr.read_save(ecr.find_save(stem)); payload = save['payload']
    doc = rr.RosterDocument(payload, base=rr.find_block_base(payload), reference_year=season)
    dc = doc.group_players('draft_class')
    rows = []
    for p in dc:
        v = p.record.values
        rows.append(dict(first=p.first, last=p.last, pool=p.pool,
            position=ecr.POS[v['position']] if v['position'] < len(ecr.POS) else v['position'],
            college=p.college or None, years_pro=v['years_pro'], birth_year_raw=ecr.raw_birth_year(v),
            ratings=[v[n] for n in ecr.RATING_ORDER]))
    out[season] = rows
    print(stem, season, len(rows), [ (r['first'], r['last'], r['position'], r['college']) for r in rows[:15]])
json.dump(out, open(sys.argv[1], 'w', encoding='utf-8'), ensure_ascii=False)

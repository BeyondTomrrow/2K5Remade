"""My NFL Eras: a season's head coaches from PFR's coaches page (Wayback copy), as
data/eras/league/head_coaches_<season>.json (the schema the 1980 file uses; read by
make_runtime_package.py write_coaches -> coaches.csv).

    python -I tools/eras/head_coaches.py --season 1981 [--fetch]

Reads build/eras/league/pfr_cache/coaches_<season>.html (/years/<season>/coaches.htm). With
--fetch, the newest Wayback capture that holds the coaches table is downloaded. Several rows for
one team (an in-season change) get first_game / last_game in PFR table order (the order coached).
Data only: the page is parsed as text, never executed. Run with python -I.
"""
import argparse, gzip, hashlib, html, json, os, re, sys, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import league_history as lh  # noqa: E402  (FRANCHISE codes)

ROOT = lh.ROOT
CACHE = os.path.join(ROOT, 'build', 'eras', 'league', 'pfr_cache')
OUT = os.path.join(ROOT, 'data', 'eras', 'league')
PAGE = 'https://www.pro-football-reference.com/years/%d/coaches.htm'
UA = {'User-Agent': 'nfl2k5-eras-research/1.0', 'Accept-Encoding': 'gzip'}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180) as r:
        b = r.read()
    return gzip.decompress(b) if b[:2] == b'\x1f\x8b' else b


def fetch(season, path):
    cdx = ('https://web.archive.org/cdx/search/cdx?url=pro-football-reference.com/years/%d/coaches.htm'
           '&output=json&filter=statuscode:200&limit=-25' % season)
    for ts in reversed([r[1] for r in json.loads(get(cdx))[1:]]):
        b = get('https://web.archive.org/web/%sid_/%s' % (ts, PAGE % season))
        if b'id="coaches"' in b:
            open(path, 'wb').write(b)
            return ts
    raise SystemExit('no usable Wayback capture of %s' % (PAGE % season))


def strip(x):
    return html.unescape(re.sub(r'<[^>]+>', '', x)).replace('\xa0', ' ').strip()


def parse(path, season):
    s = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'<table[^>]*id="coaches"', s)
    t = s[m.start():s.find('</table>', m.start())]
    snap = re.search(r'web\.archive\.org/web/(\d{14})', s)
    rows = []
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
        c = dict(re.findall(r'data-stat="([^"]+)"[^>]*>(.*?)</t[hd]>', row, re.S))
        if 'coach' not in c or not c.get('g', '').strip().isdigit():
            continue
        code = re.search(r'/teams/([a-z]{3})/%d\.htm' % season, c['team'])
        if not code:
            continue
        name = strip(c['coach'])
        first, _, last = name.partition(' ')
        rows.append({'team': lh.FRANCHISE[code.group(1)], 'pfr_team': strip(c['team']), 'coach': name,
                     'first': first, 'last': last, 'games': int(strip(c['g'])), 'wins': int(strip(c['wins']) or 0),
                     'losses': int(strip(c['losses']) or 0), 'ties': int(strip(c['ties']) or 0)})
    # several rows per team: PFR lists them in the order coached
    nxt = {}
    for r in rows:
        r['first_game'] = nxt.get(r['team'], 0) + 1
        r['last_game'] = r['first_game'] + r['games'] - 1
        nxt[r['team']] = r['last_game']
    return rows, snap


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1981)
    ap.add_argument('--fetch', action='store_true')
    a = ap.parse_args()
    path = os.path.join(CACHE, 'coaches_%d.html' % a.season)
    ts = None
    if a.fetch and not os.path.exists(path):
        ts = fetch(a.season, path)
    rows, snap = parse(path, a.season)
    games = max(r['last_game'] for r in rows)
    teams = sorted({r['team'] for r in rows})
    bad = [t for t in teams if max(r['last_game'] for r in rows if r['team'] == t) != games]
    assert not bad, ('a team does not cover every game', bad)
    idx_path = os.path.join(CACHE, 'index.json')
    idx = json.load(open(idx_path)) if os.path.exists(idx_path) else {}
    key = 'coaches_%d' % a.season
    snapshot = ts or idx.get(key, {}).get('snapshot') or (snap.group(1) if snap else 'unknown')
    url = 'https://web.archive.org/web/%s/%s' % (snapshot, PAGE % a.season)
    idx[key] = {'snapshot': snapshot, 'url': url}
    json.dump(idx, open(idx_path, 'w', encoding='utf-8'), indent=1)
    out = {
        'schema': 'nfl2k5_eras_head_coaches/v1', 'season': a.season, 'source': url,
        'fetched': '2026-10-17', 'snapshot_sha256': hashlib.sha256(open(path, 'rb').read()).hexdigest(),
        'notes': ['first_game/last_game: the team\'s regular-season game numbers (1-%d) coached, in PFR table order (the order coached).' % games,
                  'Names split at the first space into first/last.'],
        'coaches': rows,
    }
    json.dump(out, open(os.path.join(OUT, 'head_coaches_%d.json' % a.season), 'w', encoding='utf-8'), indent=1)
    print('%d coaches, %d teams, %d games: %s' % (len(rows), len(teams), games, ', '.join('%s %s' % (r['team'], r['coach']) for r in rows if r['first_game'] > 1) or 'no in-season changes'))


if __name__ == '__main__':
    main()

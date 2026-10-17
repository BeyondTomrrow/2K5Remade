"""My NFL Eras: each team's real starters for a season (games started, kicker, punter), from PFR.

    python -I tools/eras/league_starters.py [--season 1980] [--fetch]

Same method as league_schedule.py and lineage_check.py: Pro-Football-Reference answers scripts
with a Cloudflare challenge (HTTP 403), which is not bypassed; its pages are read from Wayback
Machine copies cached in build/eras/league/pfr_cache/ (index: starters_sources.json). --fetch
downloads the copies that are missing (nothing else is fetched): for every team

    roster_<code>_<season>.html        /teams/<code>/<season>_roster.htm  (primary: the full roster
                                       table with G and GS, and PFR's "Starters" table)
    roster_<code>_<season>_<snap>.html the same page, an older Wayback capture (second copy, parsed
                                       separately; GS must agree player for player)
    team_<code>_<season>.html          /teams/<code>/<season>.htm (kicking: FGA; punting: punts)

A capture is picked from the Wayback CDX listing (status 200, newest first) and kept only if it
holds the tables it is used for (many recent captures are Cloudflare challenge pages). The four
roster pages lineage_check.py already cached are reused as the primary copies.

Writes data/eras/league/starters_<season>.json: per team the source URLs and sha256 of every
copy used, the roster (PFR player id, name, position, number, G, GS), PFR's starters table
(position, player), kicking (FGA, XPA) and punting (punts), and the cross-checks:
    second_copy: GS of every player in both copies, agreements and differences;
    starters_have_gs: each starters-table player is on the roster table with GS > 0;
    team_page_roster: each kicker/punter on the team page is on the roster table.
make_runtime_package.py reads this file for the default depth chart (PROVENANCE "DEPTH CHART").
Data only: the cached pages are parsed as text, never executed. Run with python -I.
"""
import argparse, gzip, hashlib, json, os, re, sys, time
import urllib.error, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import league_schedule as ls  # noqa: E402  (shared Wayback cache helpers)

ROOT = ls.ROOT
CACHE = ls.CACHE
INDEX = os.path.join(CACHE, 'starters_sources.json')
LINEAGE_INDEX = os.path.join(CACHE, 'lineage_sources.json')
CDX = 'https://web.archive.org/cdx/search/cdx?url=pro-football-reference.com/%s&output=json&filter=statuscode:200'
UA = {'User-Agent': 'nfl2k5-eras-research/1.0', 'Accept-Encoding': 'gzip'}
MAX_TRIES = 10                     # captures tried per page, newest first
TEAM_BEFORE = '20250301'           # team pages captured since 2025 hold no kicking/punting tables
SECOND_MIN_GAP_DAYS = 365          # the second roster copy is at least a year older than the primary


def http(url, timeout=180, tries=5):
    for n in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                body, final = r.read(), r.geturl()
            if body[:2] == b'\x1f\x8b':
                body = gzip.decompress(body)
            return body, final
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            if isinstance(e, urllib.error.HTTPError) and e.code in (403, 404):
                raise
            print('  retry %d %s: %s' % (n + 1, url, e))
            time.sleep(10 * (n + 1))
    raise SystemExit('could not fetch %s' % url)


def captures(path):
    body, _ = http(CDX % path, timeout=120)
    rows = json.loads(body.decode('utf-8'))[1:]
    return [r[1] for r in rows]            # timestamps, oldest first


def table(s, tid):
    m = re.search(r'<table[^>]*id="%s"' % tid, s)
    if not m:
        return None
    return s[m.start():s.find('</table>', m.start())]


def rows_of(t):
    out = []
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
        cells = dict(re.findall(r'<t[hd][^>]*data-stat="([^"]+)"[^>]*>(.*?)</t[hd]>', row, re.S))
        link = re.search(r'<a href="/players/[A-Z]/([^"]+?)\.htm">(.*?)</a>', cells.get('player', ''))
        if link:
            out.append((link.group(1), ls.strip(link.group(2)), cells))
    return out


def num(x):
    x = ls.strip(x or '')
    return int(x) if re.fullmatch(r'\d+', x) else 0


def parse_roster(path):
    s = open(path, encoding='utf-8', errors='replace').read()
    t = table(s, 'roster')
    if t is None or 'data-stat="gs"' not in t:
        return None
    roster = [{'pfr_id': pid, 'name': name, 'pos': ls.strip(c.get('pos', '')),
               'number': ls.strip(c.get('uniform_number', '')), 'g': num(c.get('g')), 'gs': num(c.get('gs'))}
              for pid, name, c in rows_of(t)]
    st = table(s, 'starters')
    starters = [{'pos': ls.strip(c.get('pos', '')), 'pfr_id': pid, 'name': name}
                for pid, name, c in rows_of(st)] if st else None
    return {'roster': roster, 'starters': starters}


def parse_team(path):
    s = open(path, encoding='utf-8', errors='replace').read()
    k, p = table(s, 'kicking'), table(s, 'punting')
    if k is None or p is None:
        return None
    return {'kicking': [{'pfr_id': pid, 'name': n, 'fga': num(c.get('fga')), 'xpa': num(c.get('xpa'))}
                        for pid, n, c in rows_of(k)],
            'punting': [{'pfr_id': pid, 'name': n, 'punts': num(c.get('punt'))} for pid, n, c in rows_of(p)]}


def ok_roster(path, need_starters=True):
    r = parse_roster(path)
    return bool(r and len(r['roster']) >= 40 and (r['starters'] or not need_starters))


def ok_team(path):
    t = parse_team(path)
    return bool(t and t['punting'])


def fetch_capture(page, snap, path):
    body, final = http(ls.WB % (snap + 'id_', ls.PFR + page))
    m = re.search(r'/web/(\d{14})id_/', final)
    open(path, 'wb').write(body)
    return m.group(1) if m else snap


def record(idx, fname, snap, page, role):
    data = open(os.path.join(CACHE, fname), 'rb').read()
    idx[fname] = {'url': ls.WB % (snap, ls.PFR + page), 'snapshot': snap, 'role': role,
                  'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def pick(page, fname, good, idx, role, before=None, skip=()):
    """Newest status-200 capture (older than `before` if given) whose page passes `good`."""
    path = os.path.join(CACHE, fname)
    snaps = captures(page.lstrip('/'))
    if before:
        snaps = [x for x in snaps if x < before]
    snaps = [x for x in snaps if x not in skip]
    for snap in list(reversed(snaps))[:MAX_TRIES]:
        try:
            got = fetch_capture(page, snap, path)
        except urllib.error.HTTPError as e:
            print('  %s %s: HTTP %d' % (fname, snap, e.code))
            continue
        if good(path):
            record(idx, fname, got, page, role)
            print('fetched %s (capture %s)' % (fname, got), flush=True)
            return got
        print('  %s capture %s lacks the tables, trying an older one' % (fname, snap))
        time.sleep(2)
    if os.path.exists(path):
        os.remove(path)
    print('BLOCKED: no usable capture of %s' % page, flush=True)
    return None


def season_codes(season):
    d = json.load(open(os.path.join(ls.LEAGUE, 'nfl_seasons_1970_1980.json'), encoding='utf-8'))
    s = [x for x in d['seasons'] if x['season'] == season][0]
    return [(t['id'], t['pfr_code']) for c in s['conferences'] for dv in c['divisions'] for t in dv['teams']]


def years_back(snap, days):
    y, rest = int(snap[:4]), snap[4:]
    return '%04d%s' % (y - max(1, days // 365), rest)


def fetch_team(season, code, idx, lin, save):
    rp = '/teams/%s/%d_roster.htm' % (code, season)
    tp = '/teams/%s/%d.htm' % (code, season)
    rf = 'roster_%s_%d.html' % (code, season)
    if rf not in idx:
        if rf in lin and os.path.exists(os.path.join(CACHE, rf)) and ok_roster(os.path.join(CACHE, rf)):
            idx[rf] = dict(lin[rf], role='roster')
            print('reused %s (lineage_check copy %s)' % (rf, lin[rf]['snapshot']), flush=True)
        else:
            pick(rp, rf, ok_roster, idx, 'roster')
        save()
    if rf in idx and not any(v.get('role') == 'roster_second' and k.startswith('roster_%s_%d_' % (code, season))
                             for k, v in list(idx.items())):
        before = years_back(idx[rf]['snapshot'], SECOND_MIN_GAP_DAYS)
        tmp = 'roster_%s_%d_second.html' % (code, season)
        good2 = lambda p: ok_roster(p, need_starters=False)
        got = pick(rp, tmp, good2, idx, 'roster_second', before=before)
        if not got:     # no older copy with GS: any other capture than the primary's
            got = pick(rp, tmp, good2, idx, 'roster_second', skip=(idx[rf]['snapshot'],))
        if got:
            final = 'roster_%s_%d_%s.html' % (code, season, got)
            os.replace(os.path.join(CACHE, tmp), os.path.join(CACHE, final))
            idx[final] = idx.pop(tmp)
        save()
    tf = 'team_%s_%d.html' % (code, season)
    if tf not in idx:
        pick(tp, tf, ok_team, idx, 'team', before=TEAM_BEFORE)
        save()


def fetch(season, workers=8):
    """Teams are fetched in parallel (a few at a time); the index is saved after every page."""
    import threading
    from concurrent.futures import ThreadPoolExecutor
    os.makedirs(CACHE, exist_ok=True)
    idx = ls.load_index(INDEX)
    lin = ls.load_index(LINEAGE_INDEX)
    lock = threading.Lock()

    def save():
        with lock:
            json.dump(dict(sorted(idx.items())), open(INDEX, 'w', encoding='utf-8'), indent=1)
    with ThreadPoolExecutor(workers) as ex:
        futs = {ex.submit(fetch_team, season, code, idx, lin, save): code for _, code in season_codes(season)}
        for f, code in futs.items():
            try:
                f.result()
            except BaseException as e:     # recorded as blocked by build(); a re-run tries again
                print('BLOCKED: %s: %s' % (code, e), flush=True)


def build(season):
    idx = ls.load_index(INDEX)
    teams, blocked = {}, []
    for fid, code in season_codes(season):
        rf = 'roster_%s_%d.html' % (code, season)
        tf = 'team_%s_%d.html' % (code, season)
        second = sorted(k for k, v in idx.items() if v.get('role') == 'roster_second'
                        and k.startswith('roster_%s_%d_' % (code, season)))
        if rf not in idx or tf not in idx:
            blocked.append({'team': fid, 'missing': [f for f in (rf, tf) if f not in idx]})
            continue
        r = parse_roster(os.path.join(CACHE, rf))
        k = parse_team(os.path.join(CACHE, tf))
        assert r and r['starters'] and k, fid
        on = {p['pfr_id']: p for p in r['roster']}
        checks = {}
        if second:
            r2 = parse_roster(os.path.join(CACHE, second[0]))
            gs2 = {p['pfr_id']: p['gs'] for p in r2['roster']}
            both = [p for p in r['roster'] if p['pfr_id'] in gs2]
            diff = [{'pfr_id': p['pfr_id'], 'name': p['name'], 'primary': p['gs'], 'second': gs2[p['pfr_id']]}
                    for p in both if p['gs'] != gs2[p['pfr_id']]]
            checks['second_copy'] = {'file': second[0], 'url': idx[second[0]]['url'], 'sha256': idx[second[0]]['sha256'],
                                     'players_in_both': len(both), 'gs_agree': len(both) - len(diff),
                                     'gs_differ': diff,
                                     'only_in_primary': [p['name'] for p in r['roster'] if p['pfr_id'] not in gs2],
                                     'only_in_second': [p['name'] for p in r2['roster'] if p['pfr_id'] not in on]}
        else:
            checks['second_copy'] = None
        checks['starters_have_gs'] = [s['name'] for s in r['starters'] if on.get(s['pfr_id'], {}).get('gs', 0) <= 0]
        checks['team_page_not_on_roster'] = [x['name'] for x in k['kicking'] + k['punting'] if x['pfr_id'] not in on]
        teams[fid] = {
            'pfr_code': code,
            'sources': {'roster': idx[rf]['url'], 'roster_sha256': idx[rf]['sha256'],
                        'team': idx[tf]['url'], 'team_sha256': idx[tf]['sha256']},
            'roster': r['roster'], 'starters': r['starters'],
            'kicking': k['kicking'], 'punting': k['punting'], 'checks': checks,
        }
        c2 = checks['second_copy']
        print('%-14s %-3s roster %2d, starters %2d, K %s, P %s; second copy GS %s' % (
            fid, code, len(r['roster']), len(r['starters']),
            '/'.join('%s %d' % (x['name'], x['fga']) for x in k['kicking'] if x['fga']) or '-',
            '/'.join('%s %d' % (x['name'], x['punts']) for x in k['punting'] if x['punts']) or '-',
            ('%d/%d agree' % (c2['gs_agree'], c2['players_in_both'])) if c2 else 'none'))
    out = os.path.join(ls.LEAGUE, 'starters_%d.json' % season)
    json.dump({
        'schema': 'nfl2k5_eras_starters/v1',
        'generated_by': 'tools/eras/league_starters.py',
        'season': season,
        'source_note': ('Pro-Football-Reference team roster pages (/teams/<code>/%d_roster.htm: roster table '
                        'G and GS, Starters table) and team season pages (/teams/<code>/%d.htm: kicking FGA, '
                        'punting punts), read from Wayback Machine copies; a second, older Wayback copy of '
                        'each roster page is parsed separately as a cross-check.' % (season, season)),
        'teams': teams, 'blocked': blocked,
    }, open(out, 'w', encoding='utf-8'), indent=1)
    print('wrote %s: %d teams, %d blocked' % (os.path.relpath(out, ROOT), len(teams), len(blocked)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--fetch', action='store_true')
    a = ap.parse_args()
    if a.fetch:
        fetch(a.season)
    build(a.season)


if __name__ == '__main__':
    main()

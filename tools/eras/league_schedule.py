"""My NFL Eras: a season's historical regular-season schedule, from cached source pages.

    python -I tools/eras/league_schedule.py [--season 1980] [--fetch] [--check]

Same method as league_history.py: Pro-Football-Reference (PFR) answers scripts with a
Cloudflare challenge, which is not bypassed; its pages are read from Wayback Machine copies
cached in build/eras/league/pfr_cache/. --fetch downloads the copies listed in SOURCES that
are missing from the cache (nothing else is fetched); the cache index is
pfr_cache/schedule_sources.json ({file: {url, snapshot, sha256, bytes}}).

Sources for 1980:
    games_1980.html            PFR /years/1980/games.htm, Wayback 2025-01-23 (primary)
    games_1980_20190818.html   the same page, Wayback 2019-08-18 (second copy, parsed separately)
    nfl_past_standings_2011.pdf  NFL Record & Fact Book 2012, "Past Standings" (static.nfl.com,
                               Wayback 2013-09-25); pdftotext -layout makes the .txt beside it
    years_1980.html            PFR /years/1980/ standings (already cached by league_history.py)

Writes:
    data/eras/league/nfl_schedule_<season>.json   week, date, away, home, real score, sources
    data/eras/runtime/<season>/schedule.csv       game_id,week,date,away,home,historical
                                                  (no scores: real results never reach play)

Checks (always run while building; --check re-runs them on the written files):
    every team plays the season's game count; games = teams x games / 2; no team twice in a
    week; every team id is in the season's list (nfl_seasons_1970_1980.json); the two PFR
    copies agree game for game; the home team read from the box-score link agrees with PFR's
    location mark; and each team's W-L-T recomputed from the real scores equals its record in
    nfl_seasons_1970_1980.json (verified there against the NFL book). PF/PA recomputed from the
    scores must also equal the NFL book's Pts/OP and PFR's own standings table.

Per-game cross-check (home/away is the one field the totals above cannot confirm): each
team's Wikipedia season article ("1980 <city> <name> season", one revision each, cached in
build/eras/league/wiki_cache/<season>/ with its revision id). Every schedule row the parser
finds must agree with the schedule on date, opponent, home/away, result and score; rows it
cannot find are listed, not counted as agreement. Wikipedia is not independent of PFR (many
of these articles cite PFR box scores), so it is a second reading, not a second source.

Team ids are the stable franchise ids of franchise_events_1970_2025.json. Data only: the
cached pages are parsed as text, never executed. Run with python -I.
"""
import argparse, csv, datetime as dt, gzip, hashlib, html, json, os, re, shutil, subprocess, time
import urllib.error, urllib.parse, urllib.request
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CACHE = os.path.join(ROOT, 'build', 'eras', 'league', 'pfr_cache')
WIKI_CACHE = os.path.join(ROOT, 'build', 'eras', 'league', 'wiki_cache')
WIKI_TITLE = {'cardinals': '%d St. Louis Cardinals (NFL) season'}   # default '%d <city> <name> season'
LEAGUE = os.path.join(ROOT, 'data', 'eras', 'league')
RUNTIME = os.path.join(ROOT, 'data', 'eras', 'runtime')
INDEX = os.path.join(CACHE, 'schedule_sources.json')
PFR = 'https://www.pro-football-reference.com'
WB = 'https://web.archive.org/web/%s/%s'

# Every page a season's schedule is built from. role: primary (the data), second (a separate
# Wayback copy, must agree game for game), nfl_book (official PF/PA), standings (PFR table).
SOURCES = {
    1980: [
        {'file': 'games_1980.html', 'role': 'primary', 'snapshot': '20250123165834',
         'page': PFR + '/years/1980/games.htm'},
        {'file': 'games_1980_20190818.html', 'role': 'second', 'snapshot': '20190818122956',
         'page': PFR + '/years/1980/games.htm'},
        {'file': 'nfl_past_standings_2011.pdf', 'role': 'nfl_book', 'snapshot': '20130925182142',
         'page': 'http://static.nfl.com/static/content/public/image/history/pdfs/History/Past_Standings_2011.pdf',
         'title': '2012 NFL Record & Fact Book, "Past Standings" (1980: p. 387)'},
        {'file': 'years_1980.html', 'role': 'standings', 'snapshot': '20241219080443',
         'page': PFR + '/years/1980/', 'note': 'cached by league_history.py'},
    ],
}


def strip(x):
    return html.unescape(re.sub(r'<[^>]+>', '', x)).replace('\xa0', ' ').strip()


def load_index(index=INDEX):
    return json.load(open(index, encoding='utf-8')) if os.path.exists(index) else {}


def wayback(src, idx=None):
    """The capture actually cached (the index), else the one SOURCES asks for."""
    snap = (idx or {}).get(src['file'], {}).get('snapshot', src['snapshot'])
    return WB % (snap, src['page'])


def fetch(season):
    fetch_sources(SOURCES[season], INDEX)


def fetch_sources(sources, index):
    """Download the Wayback copies that are not cached yet (raw id_ captures).

    Wayback may answer a capture time with a redirect to the nearest capture it holds; the
    capture actually served is what the index records. Also used by lineage_check.py.
    """
    os.makedirs(CACHE, exist_ok=True)
    idx = load_index(index)
    for src in sources:
        path = os.path.join(CACHE, src['file'])
        snap = idx.get(src['file'], {}).get('snapshot', src['snapshot'])
        if not os.path.exists(path):
            url = WB % (src['snapshot'] + 'id_', src['page'])
            req = urllib.request.Request(url, headers={'User-Agent': 'nfl2k5-eras-research/1.0',
                                                       'Accept-Encoding': 'gzip'})
            with urllib.request.urlopen(req, timeout=180) as r:
                body, final = r.read(), r.geturl()
            if body[:2] == b'\x1f\x8b':
                body = gzip.decompress(body)
            m = re.search(r'/web/(\d{14})id_/', final)
            snap = m.group(1) if m else src['snapshot']
            if snap != src['snapshot']:
                print('%s: Wayback served capture %s for %s' % (src['file'], snap, src['snapshot']))
            open(path, 'wb').write(body)
            print('fetched', src['file'], len(body), 'bytes')
        data = open(path, 'rb').read()
        idx[src['file']] = {'url': WB % (snap, src['page']), 'snapshot': snap, 'role': src['role'],
                            'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
    json.dump(idx, open(index, 'w', encoding='utf-8'), indent=1)


def wiki_title(season, t):
    return (WIKI_TITLE[t['id']] if t['id'] in WIKI_TITLE else '%d ' + t['city'] + ' ' + t['name'] + ' season') % season


def fetch_wiki(season, teams):
    """One revision of each team's season article (MediaWiki API, wikitext), paced and retried."""
    d = os.path.join(WIKI_CACHE, str(season))
    os.makedirs(d, exist_ok=True)
    ip = os.path.join(d, 'index.json')
    idx = load_index(ip)
    for t in teams:
        p = os.path.join(d, t['id'] + '.json')
        if not os.path.exists(p):
            url = 'https://en.wikipedia.org/w/api.php?' + urllib.parse.urlencode(
                {'action': 'query', 'prop': 'revisions', 'titles': wiki_title(season, t), 'rvprop': 'ids|content',
                 'rvslots': 'main', 'format': 'json', 'redirects': '1'})
            req = urllib.request.Request(url, headers={'User-Agent': 'nfl2k5-eras-research/1.0'})
            for k in range(6):
                try:
                    body = urllib.request.urlopen(req, timeout=60).read()
                    break
                except urllib.error.HTTPError as e:
                    if e.code != 429:
                        raise
                    time.sleep(15 * (k + 1))
            else:
                raise SystemExit('Wikipedia keeps answering 429; try again later')
            open(p, 'wb').write(body)
            print('fetched', os.path.relpath(p, ROOT))
            time.sleep(5)
        page = wiki_page(p)
        idx[t['id']] = {'title': page['title'], 'revid': page['revid'],
                        'url': 'https://en.wikipedia.org/w/index.php?title=%s&oldid=%d'
                               % (urllib.parse.quote(page['title'].replace(' ', '_')), page['revid']),
                        'sha256': hashlib.sha256(open(p, 'rb').read()).hexdigest()}
    json.dump(idx, open(ip, 'w', encoding='utf-8'), indent=1)


def wiki_page(path):
    w = json.load(open(path, encoding='utf-8'))
    pages = list(w['query']['pages'].values())
    assert len(pages) == 1 and 'revisions' in pages[0], path
    rev = pages[0]['revisions'][0]
    return {'title': pages[0]['title'], 'revid': rev['revid'], 'text': rev['slots']['main']['*']}


MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')
WIKI_DATE = re.compile(r'^(?:\{\{dow tooltip\|)?(?:[A-Z][a-z]+\.?,?\s+)?(%s)[a-z]*\.?\s+(\d{1,2})\b' % '|'.join(MONTHS))
WIKI_RESULT = re.compile(r'^([WLT])\s+(\d+)\s*[–—\-]\s*(\d+)')


def wiki_clean(x):
    x = re.sub(r'\{\{[Ss]mall\|[^}]*\}\}|<[^>]+>', '', x)
    x = re.sub(r'\[\[[^|\]]*\|([^\]]*)\]\]', r'\1', x)
    x = re.sub(r'\[\[([^\]]*)\]\]', r'\1', x)
    return x.replace("'''", '').replace("''", '').strip()


def wiki_cells(row):
    out = []
    for ln in row.split('\n'):
        ln = ln.lstrip()
        if ln[:1] in ('|', '!'):
            for c in re.split(r'\s*\|\|\s*|\s*!!\s*', ln[1:]):
                head = c.split('|', 1)
                if len(head) == 2 and 'style=' in head[0] and '[[' not in head[0]:
                    c = head[1]  # 'style="..." | text'
                out.append(c.strip())
    return out


def wiki_rows(text, teams):
    """{(month, day): [(opponent id, away?, W/L/T, sorted scores)]} from the regular-season table.

    A row is a date cell followed by the opponent cell ("at X" = away) and the result cell. The
    score order varies between articles, so scores are compared as a pair.
    """
    names = {t['city'] + ' ' + t['name']: t['id'] for t in teams}
    m = re.search(r'==\s*Regular season\s*==', text)
    rows = defaultdict(list)
    for row in re.split(r'\n\s*\|-', text[m.end() if m else 0:]):
        c = wiki_cells(row)
        for i, x in enumerate(c):
            dm = WIKI_DATE.match(wiki_clean(x))
            if not dm or i + 2 >= len(c):
                continue
            opp = wiki_clean(c[i + 1])
            res = WIKI_RESULT.match(wiki_clean(c[i + 2]))
            if res:
                away = bool(re.match(r'(?:at|@)\s+', opp))
                name = re.sub(r'^(?:at|@)\s+', '', opp).replace(' (NFL)', '').strip()
                rows[(MONTHS.index(dm.group(1)) + 1, int(dm.group(2)))].append(
                    (names.get(name, '?' + name), away, res.group(1), sorted((int(res.group(2)), int(res.group(3))))))
            break
    return rows


def wiki_check(season, teams, games):
    """Every parsed Wikipedia schedule row must agree with the schedule; returns the coverage."""
    d = os.path.join(WIKI_CACHE, str(season))
    idx = load_index(os.path.join(d, 'index.json'))
    if not idx or any(t['id'] not in idx for t in teams):
        return None
    found, missing, bad = 0, [], []
    for t in teams:
        rows = wiki_rows(wiki_page(os.path.join(d, t['id'] + '.json'))['text'], teams)
        for g in games:
            if t['id'] not in (g['home'], g['away']):
                continue
            home = g['home'] == t['id']
            mine, theirs = (g['home_pts'], g['away_pts']) if home else (g['away_pts'], g['home_pts'])
            want = (g['away'] if home else g['home'], not home,
                    'W' if mine > theirs else 'L' if mine < theirs else 'T', sorted((mine, theirs)))
            got = rows.get((int(g['date'][5:7]), int(g['date'][8:10])), [])
            if not got:
                missing.append('%s: %s' % (t['id'], g['game_id']))
            elif want in got:
                found += 1
            else:
                bad.append((t['id'], g['game_id'], want, got))
    assert not bad, ('Wikipedia schedule rows disagree', bad)
    return {'team_games': 2 * len(games), 'agree': found, 'disagree': 0, 'not_found': missing,
            'articles': {t['id']: idx[t['id']]['url'] for t in teams}}


def book_text(season):
    """pdftotext -layout output of the NFL book PDF (made once, kept beside the PDF)."""
    src = [s for s in SOURCES[season] if s['role'] == 'nfl_book'][0]
    pdf = os.path.join(CACHE, src['file'])
    txt = pdf[:-4] + '.txt'
    if not os.path.exists(txt):
        exe = shutil.which('pdftotext')
        if not exe or not os.path.exists(pdf):
            return None, src
        subprocess.run([exe, '-layout', pdf, txt], check=True)
    return open(txt, encoding='utf-8', errors='replace').read(), src


# ---------------------------------------------------------------------------
# parsing

def season_teams(season):
    d = json.load(open(os.path.join(LEAGUE, 'nfl_seasons_1970_1980.json'), encoding='utf-8'))
    s = [x for x in d['seasons'] if x['season'] == season]
    if not s:
        raise SystemExit('season %d is not in nfl_seasons_1970_1980.json' % season)
    s = s[0]
    teams = [dict(t, conference=c['name'], division=dv['name'])
             for c in s['conferences'] for dv in c['divisions'] for t in dv['teams']]
    return s, teams


def pfr_games(path, season):
    """Regular-season rows of PFR's games table: [{week, date, day, time, home, away, pts}]."""
    s = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'<table[^>]*id="games"', s)
    assert m, path
    t = s[m.start():s.find('</table>', m.start())]
    out = []
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
        cells = dict((k, v) for k, v in re.findall(r'<t[hd][^>]*data-stat="([^"]+)"[^>]*>(.*?)</t[hd]>', row, re.S))
        week = strip(cells.get('week_num', ''))
        if not week.isdigit():  # header rows, the playoff separator and the playoff rounds
            continue
        date = re.search(r'data-stat="game_date"[^>]*csk="(\d{4}-\d\d-\d\d)"', row)
        win = re.search(r'/teams/([a-z]{3})/%d\.htm' % season, cells['winner'])
        lose = re.search(r'/teams/([a-z]{3})/%d\.htm' % season, cells['loser'])
        box = re.search(r'/boxscores/(\d{8})0([a-z]{3})\.htm', cells.get('boxscore_word', ''))
        pw, pl = int(strip(cells['pts_win'])), int(strip(cells['pts_lose']))
        loc = strip(cells.get('game_location', ''))
        w, l = win.group(1), lose.group(1)
        assert box and box.group(2) in (w, l), (path, row[:200])
        home = box.group(2)
        # PFR's own location mark must agree with the box-score link: '@' = winner was away
        assert loc in ('', '@'), (path, week, w, l, loc)
        assert (home == l) == (loc == '@'), (path, week, w, l, loc, home)
        assert box.group(1) == date.group(1).replace('-', ''), (path, week, w, l)
        away = l if home == w else w
        out.append({'week': int(week), 'date': date.group(1), 'day': strip(cells.get('game_day_of_week', '')),
                    'time': strip(cells.get('gametime', '')), 'home': home, 'away': away,
                    'home_pts': pw if home == w else pl, 'away_pts': pl if home == w else pw,
                    'boxscore': PFR + '/boxscores/%s0%s.htm' % (box.group(1), home)})
    return out


def pfr_standings_points(path):
    """{pfr_code: (PF, PA)} from PFR's season standings tables."""
    s = open(path, encoding='utf-8', errors='replace').read()
    out = {}
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', s, re.S):
        code = re.search(r'data-stat="team"[^>]*>.*?/teams/([a-z]{3})/\d{4}\.htm', row, re.S)
        pf = re.search(r'data-stat="points"\s*>(\d+)<', row)
        pa = re.search(r'data-stat="points_opp"\s*>(\d+)<', row)
        if code and pf and pa:
            out[code.group(1)] = (int(pf.group(1)), int(pa.group(1)))
    return out


BOOK_ROW = re.compile(r"(?<![A-Za-z.'])([A-Z][A-Za-z.']*(?: [A-Za-z.']+)*)[*#]*\s+(\d+) (\d+) (\d+) (\.\d{3}|1\.000) (\d+) (\d+)")


def book_standings(text, season, teams):
    """{team id: (w, l, t, PF, PA)} for one season from the NFL book's Past Standings text.

    Each page holds two seasons side by side (pdftotext -layout keeps the columns): the page
    whose header line names `season` in its right half, rows starting at or right of it.
    """
    lines = text.splitlines()
    head = [i for i, ln in enumerate(lines) if re.fullmatch(r'\s*%d\s+%d\s*' % (season + 1, season), ln)]
    assert len(head) == 1, ('NFL book: no unique page header for', season)
    col = lines[head[0]].index(str(season)) - 3
    name_of = {}
    for t in teams:
        key = ('N.Y. ' + t['name']) if t['city'] == 'New York' else t['city']
        assert key not in name_of, key
        name_of[key] = t['id']
    out = {}
    for ln in lines[head[0] + 1:]:
        if ln.lstrip().startswith('*Wild Card'):
            break
        for m in BOOK_ROW.finditer(ln):
            if m.start() < col:
                continue
            fid = name_of[m.group(1)]
            assert fid not in out, m.group(0)
            out[fid] = tuple(int(m.group(k)) for k in (2, 3, 4, 6, 7))
    assert len(out) == len(teams), ('NFL book rows found', len(out))
    return out


# ---------------------------------------------------------------------------
# build and checks

def game_id(season, g):
    return '%dw%02d-%s-%s' % (season, g['week'], g['away'], g['home'])


def check_games(season, info, teams, games, book, pfr_pts):
    """The D2 assertions; returns a summary of what was checked."""
    ids = {t['id'] for t in teams}
    per_team = Counter()
    weeks = defaultdict(list)
    rec = {i: [0, 0, 0, 0, 0] for i in ids}  # w l t pf pa
    for g in games:
        assert g['home'] in ids and g['away'] in ids, g
        assert g['home'] != g['away'], g
        for side, opp in (('home', 'away'), ('away', 'home')):
            fid = g[side]
            per_team[fid] += 1
            weeks[g['week']].append(fid)
            r = rec[fid]
            r[3] += g[side + '_pts']
            r[4] += g[opp + '_pts']
            r[0 if g[side + '_pts'] > g[opp + '_pts'] else 1 if g[side + '_pts'] < g[opp + '_pts'] else 2] += 1
    n = info['games']
    assert len(ids) == info['team_count'], (len(ids), info['team_count'])
    assert all(per_team[i] == n for i in ids), {i: per_team[i] for i in ids if per_team[i] != n}
    assert len(games) == len(ids) * n // 2, len(games)
    for w, lst in weeks.items():
        dup = [i for i, c in Counter(lst).items() if c > 1]
        assert not dup, ('team twice in week', w, dup)
        days = sorted(g['date'] for g in games if g['week'] == w)
        span = (dt.date.fromisoformat(days[-1]) - dt.date.fromisoformat(days[0])).days
        assert span <= 6, ('week spans more than 7 days', w, days[0], days[-1])
    assert len({game_id(season, g) for g in games}) == len(games)
    bad = [(t['id'], t['record'], rec[t['id']][:3]) for t in teams
           if [t['record']['w'], t['record']['l'], t['record']['t']] != rec[t['id']][:3]]
    assert not bad, ('W-L-T recomputed from the scores differs from nfl_seasons', bad)
    summary = {'games': len(games), 'teams': len(ids), 'games_per_team': n,
               'dates': [min(g['date'] for g in games), max(g['date'] for g in games)],
               'weeks': sorted(weeks), 'byes_per_week': {w: len(ids) - len(v) for w, v in sorted(weeks.items())},
               'ties': sum(1 for g in games if g['home_pts'] == g['away_pts']),
               'wlt_matches_nfl_seasons_json': True}
    if book is not None:
        bad = [(i, book[i], rec[i]) for i in ids if list(book[i]) != rec[i]]
        assert not bad, ('W-L-T or PF/PA differs from the NFL book', bad)
        summary['wlt_pf_pa_match_nfl_book'] = True
    if pfr_pts is not None:
        code = {t['id']: t['pfr_code'] for t in teams}
        bad = [(i, pfr_pts.get(code[i]), rec[i][3:]) for i in ids if pfr_pts.get(code[i]) != tuple(rec[i][3:])]
        assert not bad, ('PF/PA differs from the PFR standings table', bad)
        summary['pf_pa_match_pfr_standings'] = True
    return summary, rec


def build(season):
    info, teams = season_teams(season)
    fid = {t['pfr_code']: t['id'] for t in teams}
    srcs = {s['role']: s for s in SOURCES[season]}
    idx = load_index()
    for s in SOURCES[season]:
        if not os.path.exists(os.path.join(CACHE, s['file'])):
            raise SystemExit('missing %s in %s (run with --fetch)' % (s['file'], CACHE))
    raw = pfr_games(os.path.join(CACHE, srcs['primary']['file']), season)
    second = pfr_games(os.path.join(CACHE, srcs['second']['file']), season)
    key = lambda g: (g['week'], g['date'], g['home'], g['away'], g['home_pts'], g['away_pts'])
    assert sorted(map(key, raw)) == sorted(map(key, second)), 'the two PFR copies disagree'
    for g in raw:
        assert g['home'] in fid and g['away'] in fid, ('PFR team code not in the season list', g)
    games = []
    for g in raw:  # PFR order: week, then date and time as PFR lists them
        h, a = fid[g['home']], fid[g['away']]
        games.append({'week': g['week'], 'date': g['date'], 'day': g['day'], 'pfr_time': g['time'],
                      'away': a, 'home': h, 'away_pts': g['away_pts'], 'home_pts': g['home_pts'],
                      'boxscore': g['boxscore']})
    for g in games:
        g['game_id'] = game_id(season, g)
    text, _ = book_text(season)
    book = book_standings(text, season, teams) if text else None
    pfr_pts = pfr_standings_points(os.path.join(CACHE, srcs['standings']['file']))
    summary, _ = check_games(season, info, teams, games, book, pfr_pts)
    summary['second_pfr_copy_matches'] = True
    summary['home_from_boxscore_link_matches_pfr_location'] = True
    wk = wiki_check(season, teams, games)
    if wk:
        summary['wikipedia_cross_check'] = {k: wk[k] for k in ('team_games', 'agree', 'disagree', 'not_found')}
    sources = []
    for s in SOURCES[season]:
        e = {'role': s['role'], 'page': s['page'], 'wayback': wayback(s, idx), 'cache_file': s['file']}
        if s['file'] in idx:
            e['sha256'] = idx[s['file']]['sha256']
        if 'title' in s:
            e['title'] = s['title']
        sources.append(e)
    doc = {
        'schema': 'nfl2k5_eras_schedule/v1',
        'generated_by': 'tools/eras/league_schedule.py',
        'season': season,
        'historical': True,
        'conventions': {
            'teams': 'stable franchise ids (franchise_events_1970_2025.json); names as in nfl_seasons_1970_1980.json',
            'week': "PFR's week number",
            'date': 'game date as PFR lists it (csk of the date cell)',
            'pfr_time': 'kick-off time as PFR prints it; the time zone is not stated on the page',
            'home': 'the team whose code ends the PFR box-score id (/boxscores/YYYYMMDD0<home>.htm); '
                    "agrees with PFR's '@' location mark for every game",
            'order': "PFR's order: by week, then as PFR lists the week's games",
            'game_id': '<season>w<week>-<away id>-<home id>',
            'scores': 'real results, for reference and checks only; the runtime schedule.csv carries no scores',
        },
        'checks': summary,
        'sources': sources,
        'wikipedia_articles': wk['articles'] if wk else None,
        'games': [{k: g[k] for k in ('game_id', 'week', 'date', 'day', 'pfr_time', 'away', 'home',
                                     'away_pts', 'home_pts', 'boxscore')} for g in games],
    }
    os.makedirs(LEAGUE, exist_ok=True)
    out = os.path.join(LEAGUE, 'nfl_schedule_%d.json' % season)
    json.dump(doc, open(out, 'w', encoding='utf-8'), indent=1)
    rt = os.path.join(RUNTIME, str(season))
    os.makedirs(rt, exist_ok=True)
    with open(os.path.join(rt, 'schedule.csv'), 'w', encoding='ascii', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['game_id', 'week', 'date', 'away', 'home', 'historical'])
        for g in games:
            w.writerow([g['game_id'], g['week'], g['date'], g['away'], g['home'], 'true'])
    print('wrote', os.path.relpath(out, ROOT), 'and', os.path.relpath(os.path.join(rt, 'schedule.csv'), ROOT),
          '(%d games)' % len(games))
    return doc


def check(season):
    """Re-run every assertion on the written files (and the cached sources when present)."""
    info, teams = season_teams(season)
    doc = json.load(open(os.path.join(LEAGUE, 'nfl_schedule_%d.json' % season), encoding='utf-8'))
    assert doc['season'] == season and doc['historical'] is True
    games = doc['games']
    for g in games:
        assert g['game_id'] == game_id(season, g), g
    text, _ = book_text(season)
    book = book_standings(text, season, teams) if text else None
    std = [s for s in SOURCES[season] if s['role'] == 'standings'][0]
    sp = os.path.join(CACHE, std['file'])
    pfr_pts = pfr_standings_points(sp) if os.path.exists(sp) else None
    summary, rec = check_games(season, info, teams, games, book, pfr_pts)
    second = [s for s in SOURCES[season] if s['role'] == 'second'][0]
    sp = os.path.join(CACHE, second['file'])
    if os.path.exists(sp):
        fid = {t['pfr_code']: t['id'] for t in teams}
        a = sorted((g['week'], g['date'], fid[g['home']], fid[g['away']], g['home_pts'], g['away_pts'])
                   for g in pfr_games(sp, season))
        b = sorted((g['week'], g['date'], g['home'], g['away'], g['home_pts'], g['away_pts']) for g in games)
        assert a == b, 'second PFR copy disagrees with the written schedule'
        summary['second_pfr_copy_matches'] = True
    wk = wiki_check(season, teams, games)
    if wk:
        summary['wikipedia_cross_check'] = {k: wk[k] for k in ('team_games', 'agree', 'disagree', 'not_found')}
    rows = list(csv.reader(open(os.path.join(RUNTIME, str(season), 'schedule.csv'), encoding='ascii')))
    assert rows[0] == ['game_id', 'week', 'date', 'away', 'home', 'historical'], rows[0]
    assert [r[:5] for r in rows[1:]] == [[g['game_id'], str(g['week']), g['date'], g['away'], g['home']]
                                         for g in games], 'schedule.csv differs from the JSON'
    assert all(r[5] == 'true' for r in rows[1:])
    assert all(len(r) == 6 for r in rows), 'schedule.csv carries extra fields (no scores allowed)'
    print('T0.2 %d schedule: OK' % season)
    for k, v in summary.items():
        if k == 'wikipedia_cross_check':
            print('  %s: %d of %d team-games agree, %d disagree, %d rows not found: %s'
                  % (k, v['agree'], v['team_games'], v['disagree'], len(v['not_found']), '; '.join(v['not_found'])))
        else:
            print('  %s: %s' % (k, v))
    print('  team    W-L-T    PF-PA (recomputed from the real scores)')
    for t in teams:
        r = rec[t['id']]
        print('  %-14s %d-%d-%d  %d-%d' % (t['id'], r[0], r[1], r[2], r[3], r[4]))
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--fetch', action='store_true', help='download missing Wayback copies into the cache')
    ap.add_argument('--check', action='store_true', help='only check the written files')
    a = ap.parse_args()
    if a.season not in SOURCES:
        raise SystemExit('no schedule sources are listed for %d' % a.season)
    if a.fetch:
        fetch(a.season)
        fetch_wiki(a.season, season_teams(a.season)[1])
    if not a.check:
        build(a.season)
    check(a.season)


if __name__ == '__main__':
    main()

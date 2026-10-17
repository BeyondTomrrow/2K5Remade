"""Build the My NFL Eras league-history data from cached Pro-Football-Reference pages.

    python -I tools/eras/league_history.py [cache_dir]

cache_dir (default build/eras/league/pfr_cache) holds Wayback Machine copies of
pro-football-reference.com pages (PFR itself sits behind a Cloudflare challenge):
    years_<season>.html   https://www.pro-football-reference.com/years/<season>/
    teams_<code>.html     https://www.pro-football-reference.com/teams/<code>/
    index.json            {page_name: {"snapshot": ts, "url": wayback_url}}

Writes data/eras/league/nfl_seasons_1970_1980.json and
data/eras/league/franchise_events_1970_2025.json. Every structural fact (team, name,
conference, division, record, playoff game) comes from the PFR pages; the narrative
sources for events are listed in EVENT_NOTES below and were read by hand.
"""
import html, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CACHE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'eras', 'league', 'pfr_cache')
OUT = os.path.join(ROOT, 'data', 'eras', 'league')
PFR = 'https://www.pro-football-reference.com'

# PFR franchise code -> stable My NFL Eras franchise id.
FRANCHISE = {
    'crd': 'cardinals', 'atl': 'falcons', 'rav': 'ravens', 'buf': 'bills', 'car': 'panthers',
    'chi': 'bears', 'cin': 'bengals', 'cle': 'browns', 'dal': 'cowboys', 'den': 'broncos',
    'det': 'lions', 'gnb': 'packers', 'htx': 'texans', 'clt': 'colts', 'jax': 'jaguars',
    'kan': 'chiefs', 'rai': 'raiders', 'sdg': 'chargers', 'ram': 'rams', 'mia': 'dolphins',
    'min': 'vikings', 'nwe': 'patriots', 'nor': 'saints', 'nyg': 'giants', 'nyj': 'jets',
    'phi': 'eagles', 'pit': 'steelers', 'sfo': '49ers', 'sea': 'seahawks', 'tam': 'buccaneers',
    'oti': 'oilers_titans', 'was': 'washington',
}

# Display abbreviations. A project convention for menus and scorebugs, NOT a historical
# claim: keyed by (franchise_id, city); first match wins.
ABBR = {
    ('cardinals', 'St. Louis'): 'STL', ('cardinals', 'Phoenix'): 'PHO', ('cardinals', 'Arizona'): 'ARI',
    ('falcons', None): 'ATL', ('ravens', None): 'BAL', ('bills', None): 'BUF', ('panthers', None): 'CAR',
    ('bears', None): 'CHI', ('bengals', None): 'CIN', ('browns', None): 'CLE', ('cowboys', None): 'DAL',
    ('broncos', None): 'DEN', ('lions', None): 'DET', ('packers', None): 'GB', ('texans', None): 'HOU',
    ('colts', 'Baltimore'): 'BAL', ('colts', 'Indianapolis'): 'IND', ('jaguars', None): 'JAX',
    ('chiefs', None): 'KC', ('raiders', 'Oakland'): 'OAK', ('raiders', 'Los Angeles'): 'LARD',
    ('raiders', 'Las Vegas'): 'LV', ('chargers', 'San Diego'): 'SD', ('chargers', 'Los Angeles'): 'LAC',
    ('rams', 'Los Angeles', 'pre1995'): 'LA', ('rams', 'St. Louis'): 'STL', ('rams', 'Los Angeles'): 'LAR',
    ('dolphins', None): 'MIA', ('vikings', None): 'MIN', ('patriots', 'Boston'): 'BOS',
    ('patriots', 'New England'): 'NE', ('saints', None): 'NO', ('giants', None): 'NYG', ('jets', None): 'NYJ',
    ('eagles', None): 'PHI', ('steelers', None): 'PIT', ('49ers', None): 'SF', ('seahawks', None): 'SEA',
    ('buccaneers', None): 'TB', ('oilers_titans', 'Houston'): 'HOU', ('oilers_titans', 'Tennessee'): 'TEN',
    ('washington', None): 'WAS',
}

def abbr(fid, city, season):
    if fid == 'rams' and city == 'Los Angeles' and season < 1995:
        return ABBR[('rams', 'Los Angeles', 'pre1995')]
    return ABBR.get((fid, city)) or ABBR.get((fid, None))

def strip(x):
    return html.unescape(re.sub(r'<[^>]+>', '', x)).replace('\xa0', ' ').strip()

def split_name(full):
    """'St. Louis Cardinals' -> ('St. Louis', 'Cardinals'); 'Washington Football Team' special."""
    if full == 'Washington Football Team':
        return 'Washington', 'Football Team'
    city, _, nick = full.rpartition(' ')
    return city, nick

def load_index():
    p = os.path.join(CACHE, 'index.json')
    return json.load(open(p)) if os.path.exists(p) else {}

INDEX = load_index()

def page_sources(name, canonical):
    src = [canonical]
    if name in INDEX:
        src.append(INDEX[name]['url'].replace('id_/', '/'))
    return src

def table(s, tid):
    m = re.search(r'<table[^>]*id="%s"' % tid, s)
    if not m:
        return None
    return s[m.start():s.find('</table>', m.start())]

def standings(season):
    """[(conference/league table id, division, team dict)] from a PFR season page."""
    s = open(os.path.join(CACHE, 'years_%d.html' % season), encoding='utf-8').read()
    full = s
    afl = os.path.join(CACHE, 'years_%d_AFL.html' % season)
    if os.path.exists(afl):  # 1969: the AFL has its own PFR page
        full = s + open(afl, encoding='utf-8').read()
    out = []
    for tid in ('AFC', 'NFC', 'NFL', 'AFL'):
        t = table(full, tid)
        if not t:
            continue
        div = None
        for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
            if 'data-stat="onecell"' in row:
                div = strip(row)
                continue
            m = re.search(r'scope="row"[^>]*data-stat="team"[^>]*>(.*?)</th>', row, re.S)
            if not m:
                continue
            cell = m.group(1)
            code = re.search(r'/teams/([a-z]{3})/%d\.htm' % season, cell)
            name = strip(cell)
            mark = ''
            while name and name[-1] in '*+':
                mark = name[-1] + mark
                name = name[:-1].strip()
            vals = dict(re.findall(r'data-stat="(wins|losses|ties)"\s*>(\d*)<', row))
            out.append((tid, div, {
                'pfr_code': code.group(1), 'full_name': name,
                'w': int(vals.get('wins') or 0), 'l': int(vals.get('losses') or 0),
                't': int(vals.get('ties') or 0), 'mark': mark}))
    return out, s

def playoffs(s):
    t = table(s, 'playoff_results')
    games = []
    if not t:
        return games
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
        cells = dict((k, strip(v)) for k, v in re.findall(r'<t[hd][^>]*data-stat="([^"]+)"[^>]*>(.*?)</t[hd]>', row, re.S))
        if not cells.get('game_date') or cells.get('week_num') in (None, '', 'Week'):
            continue
        codes = re.findall(r'/teams/([a-z]{3})/\d{4}\.htm', row)
        games.append({'round': cells['week_num'], 'date': cells['game_date'],
                      'winner': cells.get('winner'), 'loser': cells.get('loser'),
                      'winner_id': FRANCHISE.get(codes[0]) if codes else None,
                      'loser_id': FRANCHISE.get(codes[1]) if len(codes) > 1 else None,
                      'pts_win': int(cells['pts_win']), 'pts_lose': int(cells['pts_lose']),
                      'site': {'@': 'loser_home', 'N': 'neutral', '': 'winner_home'}.get(cells.get('game_location', ''), cells.get('game_location'))})
    return games

ROUND_NAMES = {'WildCard': 'Wild Card playoffs', 'Division': 'Divisional playoffs',
               'ConfChamp': 'Conference championships', 'SuperBowl': 'Super Bowl'}

ROMAN = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI', 'XII', 'XIII', 'XIV', 'XV']
# Super Bowl sites as listed by ESPN's winners table (dates/scores match PFR's playoff table).
SB_SITE = {5: 'Orange Bowl, Miami', 6: 'Tulane Stadium, New Orleans', 7: 'Los Angeles Memorial Coliseum',
           8: 'Rice Stadium, Houston', 9: 'Tulane Stadium, New Orleans', 10: 'Orange Bowl, Miami',
           11: 'Rose Bowl, Pasadena, Calif.', 12: 'Superdome, New Orleans', 13: 'Orange Bowl, Miami',
           14: 'Rose Bowl, Pasadena, Calif.', 15: 'Superdome, New Orleans'}
SRC_ESPN_SB = 'https://www.espn.com/nfl/superbowl/history/winners'
SRC_HOF_WILDCARD = 'https://www.profootballhof.com/news/wild-card-golden-nuggets'
SRC_WIKI_PLAYOFFS = 'https://en.wikipedia.org/wiki/NFL_playoffs'
# The NFL's official past standings: 2012 NFL Record & Fact Book, "Past Standings" (1970-1980 on
# pp. 388-393), archived copy of the PDF on static.nfl.com.
SRC_NFL_PAST = ('https://web.archive.org/web/20130925182142/http://static.nfl.com/static/content/'
                'public/image/history/pdfs/History/Past_Standings_2011.pdf')

# Divisions where the NFL's official standings break a tie differently from the order PFR lists
# (checked for every division 1970-1980; these are the only four). Only tied, non-playoff teams
# change places; build_seasons() asserts that the teams and records are unchanged.
OFFICIAL_ORDER = {
    (1974, 'AFC East'): (['mia', 'buf', 'nyj', 'nwe', 'clt'],
                         'N.Y. Jets finished ahead of New England on better conference record (5-6 to 4-7); PFR lists New England first.'),
    (1976, 'NFC West'): (['ram', 'sfo', 'nor', 'atl', 'sea'],
                         'New Orleans finished ahead of Atlanta on head-to-head point differential (27 points); PFR lists Atlanta first.'),
    (1977, 'AFC East'): (['clt', 'mia', 'nwe', 'nyj', 'buf'],
                         'N.Y. Jets finished ahead of Buffalo on head-to-head point differential (1 point); PFR lists Buffalo first.'),
    (1977, 'AFC Central'): (['pit', 'oti', 'cin', 'cle'],
                            'Houston finished ahead of Cincinnati on head-to-head point differential (2 points); PFR lists Cincinnati first.'),
}

def apply_official_order(season, divs):
    """Reorder tied teams to the NFL's official standings; returns True if a division changed."""
    changed = False
    for d in divs:
        ov = OFFICIAL_ORDER.get((season, d['name']))
        if not ov:
            continue
        order, note = ov
        by = {t['pfr_code']: t for t in d['teams']}
        assert sorted(by) == sorted(order), (season, d['name'])
        new = [by[c] for c in order]
        for a, b in zip(d['teams'], new):  # only teams with identical records may swap
            assert a['pfr_code'] == b['pfr_code'] or a['record'] == b['record'], (season, d['name'])
            assert a['pfr_code'] == b['pfr_code'] or not (a['finish'] or b['finish']), (season, d['name'])
        d['teams'] = new
        d['order_note'] = note + ' Order follows the NFL Record & Fact Book.'
        d['order_sources'] = [SRC_NFL_PAST, PFR + '/years/%d/' % season]
        changed = True
    return changed

def playoff_format(season):
    if season <= 1977:
        f = {
            'teams_per_conference': 4, 'division_winners_per_conference': 3, 'wild_cards_per_conference': 1,
            'byes_per_conference': 0,
            'rounds': ['Divisional playoffs', 'Conference championships', 'Super Bowl'],
            'qualification': 'Three division winners plus the non-division winner with the best record in each conference (Pro Football Hall of Fame).',
            'home_field': ('Predetermined rotation among divisions, regardless of record (Pro Football Hall of Fame: the rotation was dropped in 1975).'
                           if season <= 1974 else
                           'Seeded by record; the rotation used 1970-1974 was dropped in 1975 (Pro Football Hall of Fame).'),
            'same_division_rule': 'A wild card could not meet its own division winner in the divisional round (NFL Record & Fact Book: from 1970 to 1989 two teams from the same division could not meet before the conference championship game; consistent with every PFR divisional matchup 1970-1977).',
        }
    else:
        f = {
            'teams_per_conference': 5, 'division_winners_per_conference': 3, 'wild_cards_per_conference': 2,
            'byes_per_conference': 3,
            'rounds': ['Wild Card playoffs', 'Divisional playoffs', 'Conference championships', 'Super Bowl'],
            'qualification': 'Three division winners plus two wild cards per conference; the two wild cards play each other in the first round (Pro Football Hall of Fame).',
            'home_field': ('Seeded by record; the higher-seeded wild card hosts the Wild Card game (Wikipedia cross-check). '
                           'PFR game sites agree wherever the two wild cards had different records. The two equal-record cases were '
                           'settled by tiebreakers, per the NFL Record & Fact Book: 1978 NFC, Atlanta was first wild card over '
                           'Philadelphia (both 9-7) on record against common opponents (5-2 to 5-3); 1980 AFC, Oakland was first '
                           'wild card over Houston (both 11-5) on conference record (9-3 to 7-5). The general tiebreak procedure '
                           'was not researched.'),
            'same_division_rule': 'Teams from the same division could not meet in the divisional round (NFL Record & Fact Book: from 1970 to 1989 two teams from the same division could not meet before the conference championship game; consistent with every PFR divisional matchup 1978-1980).',
        }
    f['sources'] = [SRC_HOF_WILDCARD, SRC_NFL_PAST, SRC_WIKI_PLAYOFFS]
    return f

def build_seasons():
    seasons = []
    for y in range(1970, 1981):
        rows, s = standings(y)
        confs = []
        games_played = set()
        for conf in ('AFC', 'NFC'):
            divs = []
            for tid, div, t in rows:
                if tid != conf:
                    continue
                if not divs or divs[-1]['name'] != div:
                    divs.append({'name': div, 'teams': []})
                fid = FRANCHISE[t['pfr_code']]
                city, nick = split_name(t['full_name'])
                games_played.add(t['w'] + t['l'] + t['t'])
                divs[-1]['teams'].append({
                    'id': fid, 'city': city, 'name': nick, 'abbreviation': abbr(fid, city, y),
                    'pfr_code': t['pfr_code'],
                    'record': {'w': t['w'], 'l': t['l'], 't': t['t']},
                    'finish': {'*': 'division_winner', '+': 'wild_card', '': None}[t['mark']],
                })
            confs.append({'name': conf, 'divisions': divs})
        reordered = any([apply_official_order(y, c['divisions']) for c in confs])
        assert len(games_played) == 1, (y, games_played)
        po = playoffs(s)
        sb = [g for g in po if g['round'] == 'SuperBowl'][0]
        n = y - 1965
        teams = sum(len(d['teams']) for c in confs for d in c['divisions'])
        fmt = playoff_format(y)
        marked = sum(1 for c in confs for d in c['divisions'] for t in d['teams'] if t['finish'])
        assert marked == 2 * fmt['teams_per_conference'], (y, marked)
        for c in confs:  # each conference: the expected division winners and wild cards
            fin = [t['finish'] for d in c['divisions'] for t in d['teams']]
            assert fin.count('division_winner') == fmt['division_winners_per_conference'], (y, c['name'])
            assert fin.count('wild_card') == fmt['wild_cards_per_conference'], (y, c['name'])
        rounds = [ROUND_NAMES[g['round']] for g in po]
        assert sorted(set(rounds), key=fmt['rounds'].index) == fmt['rounds'], (y, rounds)
        seasons.append({
            'season': y,
            'team_count': teams,
            'games': games_played.pop(),
            'playoff_teams': 2 * fmt['teams_per_conference'],
            'playoff_format': fmt,
            'championship': {
                'name': 'Super Bowl ' + ROMAN[n], 'number': n, 'date': sb['date'],
                'site': SB_SITE[n], 'winner': sb['winner_id'], 'loser': sb['loser_id'],
                'winner_name': sb['winner'], 'loser_name': sb['loser'],
                'score': '%d-%d' % (sb['pts_win'], sb['pts_lose']),
                'sources': [PFR + '/years/%d/' % y, SRC_ESPN_SB],
            },
            'conference_championships': ['AFC Championship Game', 'NFC Championship Game'],
            'conferences': confs,
            'postseason': [dict(g, round=ROUND_NAMES.get(g['round'], g['round'])) for g in po],
            'sources': page_sources('years_%d' % y, PFR + '/years/%d/' % y) +
                       ([SRC_NFL_PAST] if reordered else []) +
                       ['https://en.wikipedia.org/wiki/%d_NFL_season' % y],
        })
    return seasons

# ---------------------------------------------------------------------------
# Franchise lineage 1970-2025

def franchise_names():
    """{code: {season: full name}} from the PFR franchise pages (cross-check)."""
    out = {}
    for code in FRANCHISE:
        p = os.path.join(CACHE, 'teams_%s.html' % code)
        s = open(p, encoding='utf-8').read()
        names = {}
        for row in re.findall(r'<tr[^>]*>(.*?)</tr>', s, re.S):
            y = re.search(r'data-stat="year_id"[^>]*>(.*?)</th>', row, re.S)
            n = re.search(r'data-stat="team"[^>]*>(.*?)</td>', row, re.S)
            if y and n and strip(y.group(1)).isdigit():
                names[int(strip(y.group(1)))] = strip(n.group(1)).rstrip('*').strip()
        out[code] = names
    return out

SUMMARY = {}

def alignment(first=1969, last=2025):
    """{season: {code: identity}} from the PFR season pages."""
    al = {}
    for y in range(first, last + 1):
        rows, _ = standings(y)
        cur = {}
        SUMMARY[y] = {'teams': len(rows), 'games': max(t['w'] + t['l'] + t['t'] for _, _, t in rows),
                      'playoff_teams': sum(1 for _, _, t in rows if t['mark']),
                      'divisions': sorted(set(d for _, d, _ in rows))}
        for tid, div, t in rows:
            city, nick = split_name(t['full_name'])
            conf = tid if tid in ('AFC', 'NFC') else None
            league = 'NFL' if tid in ('AFC', 'NFC', 'NFL') else tid
            cur[t['pfr_code']] = {'city': city, 'name': nick, 'league': league, 'conference': conf,
                                  'division': div}
        al[y] = cur
    return al

# Narrative sources and classification for each name/city change and each
# first/last season. Keys: (franchise_id, season). Read by hand; see LEAGUE-HISTORY.md.
S_HOF = 'https://www.profootballhof.com/teams/%s/team-history/'
EVENT_NOTES = {
    ('patriots', 1971): ('rename', 'Boston Patriots became the New England Patriots and moved to a new stadium in Foxboro, about 25 miles south of Boston.',
                         [S_HOF % 'new-england-patriots']),
    ('seahawks', 1976): ('expansion', 'NFL franchise awarded June 4, 1974; first season 1976 in the NFC West (one season only).',
                         [S_HOF % 'seattle-seahawks', 'https://www.buccaneers.com/news/series-history-buccaneers-seahawks-18129237', 'https://en.wikipedia.org/wiki/1976_NFL_season']),
    ('buccaneers', 1976): ('expansion', 'Awarded the NFL\'s 27th franchise in 1974; first season 1976 in the AFC West (one season only).',
                           [S_HOF % 'tampa-bay-buccaneers', 'https://www.buccaneers.com/news/series-history-buccaneers-seahawks-18129237', 'https://en.wikipedia.org/wiki/1976_NFL_season']),
    ('seahawks', 1977): ('realignment', 'Swapped with Tampa Bay after one season, as set in the 1976 expansion plan, so both expansion teams played each other twice and every other club once in their first two seasons (reason: Wikipedia cross-check).',
                         [S_HOF % 'seattle-seahawks', 'https://www.buccaneers.com/news/series-history-buccaneers-seahawks-18129237', 'https://en.wikipedia.org/wiki/History_of_the_Seattle_Seahawks']),
    ('buccaneers', 1977): ('realignment', 'Swapped with Seattle after one season (see seahawks 1977).',
                           ['https://www.buccaneers.com/news/series-history-buccaneers-seahawks-18129237', 'https://en.wikipedia.org/wiki/History_of_the_Tampa_Bay_Buccaneers']),
    ('raiders', 1982): ('relocation', 'Moved from the Oakland Coliseum to the Los Angeles Coliseum in 1982.',
                        [S_HOF % 'las-vegas-raiders']),
    ('colts', 1984): ('relocation', 'Robert Irsay moved the Colts to Indianapolis on March 28, 1984 (the HOF page gives the date; the first Indianapolis season, 1984, is from PFR).',
                      [S_HOF % 'indianapolis-colts']),
    ('cardinals', 1988): ('relocation', 'William Bidwill moved the franchise from St. Louis to Phoenix in 1988.',
                          [S_HOF % 'arizona-cardinals']),
    ('cardinals', 1994): ('rename', 'Renamed the Arizona Cardinals in March 1994 (same home, new geographic name).',
                          [S_HOF % 'arizona-cardinals']),
    ('panthers', 1995): ('expansion', 'Awarded an NFL expansion franchise on October 26, 1993 (HOF); first season 1995, in the NFC West (PFR).',
                         [S_HOF % 'carolina-panthers']),
    ('jaguars', 1995): ('expansion', 'Awarded the 30th NFL franchise on November 30, 1993 (HOF); first season 1995, in the AFC Central (PFR).',
                        [S_HOF % 'jacksonville-jaguars']),
    ('rams', 1995): ('relocation', 'Moved from Los Angeles to St. Louis in 1995.',
                     [S_HOF % 'los-angeles-rams']),
    ('raiders', 1995): ('relocation', "Returned to Oakland for 1995 after 13 seasons in Los Angeles (1982-1994: PFR standings; Wikipedia says they played in Los Angeles from 1982 to 1994, and Davis signed a letter of intent to return to Oakland on June 23, 1995). The HOF page says '12 seasons', which does not match those dates.",
                        [S_HOF % 'las-vegas-raiders', 'https://en.wikipedia.org/wiki/History_of_the_Los_Angeles_Raiders']),
    ('browns', 1996): ('suspension', 'Art Modell announced he would move his Cleveland franchise to Baltimore to begin play in 1996, and agreed to give up the Browns name, colors and team history to the new owner of the suspended Cleveland franchise. PFR lists no Browns seasons 1996-1998.',
                       [S_HOF % 'cleveland-browns', 'https://en.wikipedia.org/wiki/Cleveland_Browns_relocation_controversy']),
    ('ravens', 1996): ('new_franchise', "Pro football returned to Baltimore in 1996 as \"the new Baltimore Ravens\" (HOF), Modell's organisation from Cleveland; PFR's Ravens history starts in 1996 and the Browns' history stays with Cleveland.",
                       [S_HOF % 'baltimore-ravens', S_HOF % 'cleveland-browns']),
    ('oilers_titans', 1997): ('relocation', 'Houston Oilers relocated to Tennessee as the Tennessee Oilers. Home games: Memphis (Liberty Bowl Memorial Stadium) in 1997, Nashville (Vanderbilt Stadium) in 1998 (venues: Wikipedia cross-check).',
                              [S_HOF % 'tennessee-titans', 'https://en.wikipedia.org/wiki/History_of_the_Tennessee_Titans']),
    ('oilers_titans', 1999): ('rename', 'Retired the Oilers nickname and became the Tennessee Titans in 1999.',
                              [S_HOF % 'tennessee-titans']),
    ('browns', 1999): ('reactivation', 'The suspended Cleveland franchise resumed play in 1999 with the Browns name and history; the NFL had guaranteed football would return to Cleveland no later than 1999 (HOF).',
                       [S_HOF % 'cleveland-browns', 'https://en.wikipedia.org/wiki/1999_Cleveland_Browns_season']),
    ('texans', 2002): ('expansion', 'Owners voted 29-0 on October 6, 1999 to award the expansion franchise to Bob McNair (HOF); first season 2002, in the new AFC South.',
                       [S_HOF % 'houston-texans', 'https://www.espn.com/nfl/news/2001/0522/1202816.html']),
    ('rams', 2016): ('relocation', 'Owners approved the move from St. Louis to Los Angeles on January 12, 2016, 30-2 (Fortune, citing NFL.com); the HOF page gives 2016 as the move year.',
                     [S_HOF % 'los-angeles-rams', 'https://fortune.com/2016/01/12/nfl-approves-st-louis-rams-move-to-los-angeles']),
    ('chargers', 2017): ('relocation', 'Dean Spanos announced in January 2017 that the team would relocate from San Diego to Los Angeles "beginning with the 2017 NFL season", with the StubHub Center in Carson as a temporary home (NFL.com). Conflict: the HOF team page says 2016; PFR lists 2016 as San Diego and 2017 as Los Angeles.',
                         ['https://www.nfl.com/news/chargers-announce-decision-to-relocate-to-los-angeles-0ap3000000773179', S_HOF % 'los-angeles-chargers']),
    ('raiders', 2020): ('relocation', 'Owners approved the move to Las Vegas 31-1 on March 27, 2017 (AP via Boston.com); PFR lists Oakland through 2019 and Las Vegas from 2020, and the HOF page dates the move to 2020.',
                        [S_HOF % 'las-vegas-raiders', 'https://www.boston.com/sports/nfl/2017/03/27/raiders-move-to-las-vegas/']),
    ('washington', 2020): ('rename', 'After dropping the Redskins name the team played as the Washington Football Team (Boston Globe, July 23, 2020: the name for the 2020 season); NFL.com says it spent two seasons under that name.',
                           [S_HOF % 'washington-commanders', 'https://www.bostonglobe.com/2020/07/23/sports/this-nfl-season-call-it-washington-football-team',
                            'https://www.nfl.com/news/washington-commanders-new-team-name']),
    ('washington', 2022): ('rename', 'Renamed the Washington Commanders, unveiled February 2, 2022.',
                           ['https://www.nfl.com/news/washington-commanders-new-team-name', S_HOF % 'washington-commanders']),
}
SRC_2002 = ['https://www.espn.com/nfl/news/2001/0522/1202816.html',
            'https://www.seahawks.com/news/on-this-date-nfl-owners-vote-for-realignment']
SRC_1970 = [S_HOF % 'indianapolis-colts', 'https://en.wikipedia.org/wiki/1970_NFL_season']

def identity(fid, y, a):
    return {'city': a['city'], 'name': a['name'], 'abbreviation': abbr(fid, a['city'], y),
            'league': a['league'], 'conference': a['conference'], 'division': a['division']}

def build_events():
    al = alignment()
    names = franchise_names()
    mismatches = []
    franchises = []
    for code, fid in sorted(FRANCHISE.items(), key=lambda kv: kv[1]):
        events, runs = [], []
        prev = None  # (season, identity)
        seasons = [y for y in range(1969, 2026) if code in al[y]]
        for y in range(1969, 2026):
            a = al[y].get(code)
            if a and y in names[code] and names[code][y] != '%s %s' % (a['city'], a['name']):
                mismatches.append((fid, y, names[code][y], a))
            if y == 1969:
                prev = (y, identity(fid, y, a)) if a else None
                continue
            cur = identity(fid, y, a) if a else None
            src_pages = [PFR + '/years/%d/' % (y - 1), PFR + '/years/%d/' % y, PFR + '/teams/%s/' % code]
            if cur:
                if not runs or any(runs[-1][k] != cur[k] for k in ('city', 'name', 'conference', 'division')) or runs[-1]['last'] != y - 1:
                    runs.append(dict(cur, first=y, last=y))
                else:
                    runs[-1]['last'] = y
            p = prev[1] if prev else None
            if cur and not p:
                if prev is None and not any(s < y for s in seasons):
                    kind, note, src = EVENT_NOTES.get((fid, y), ('expansion', 'First season.', []))
                else:
                    kind, note, src = EVENT_NOTES[(fid, y)]
                events.append({'season': y, 'kind': kind, 'from': None, 'to': cur, 'note': note,
                               'sources': src + src_pages[1:]})
            elif p and not cur:
                kind, note, src = EVENT_NOTES[(fid, y)]
                events.append({'season': y, 'kind': kind, 'from': p, 'to': None, 'note': note,
                               'sources': src + src_pages})
            elif p and cur:
                if (p['city'], p['name']) != (cur['city'], cur['name']):
                    kind, note, src = EVENT_NOTES[(fid, y)]
                    events.append({'season': y, 'kind': kind, 'from': p, 'to': cur, 'note': note,
                                   'sources': src + src_pages})
                if (p['conference'], p['division']) != (cur['conference'], cur['division']):
                    if y == 1970:
                        kind, note, src = ('merger_realignment',
                                           'AFL-NFL merger in effect: placed in the %s (1969: %s %s division).' % (
                                               cur['division'], p['league'], p['division']) +
                                           (' One of the three NFL clubs moved into the AFC.' if p['league'] == 'NFL' and cur['conference'] == 'AFC' else ''),
                                           SRC_1970)
                    elif y == 2002:
                        kind, note, src = ('realignment', '2002 realignment into eight four-team divisions, approved by the owners May 22, 2001.' +
                                           (' Seattle was the only club to change conferences.' if fid == 'seahawks' else ''), SRC_2002)
                    else:
                        kind, note, src = EVENT_NOTES[(fid, y)]
                    events.append({'season': y, 'kind': kind, 'from': p, 'to': cur, 'note': note,
                                   'sources': src + src_pages})
            if cur:
                prev = (y, cur)
            elif p:
                prev = (y, None)
        franchises.append({
            'franchise_id': fid, 'pfr_code': code,
            'pfr_page': PFR + '/teams/%s/' % code,
            'identities': [{'first_season': r['first'], 'last_season': r['last'], 'city': r['city'], 'name': r['name'],
                            'abbreviation': r['abbreviation'], 'conference': r['conference'], 'division': r['division']}
                           for r in runs],
            'events': events,
        })
    counts = {y: len(al[y]) for y in range(1970, 2026)}
    return franchises, counts, mismatches, al

LEAGUE_EVENTS = [
    {'season': 1970, 'kind': 'merger', 'note': 'AFL-NFL merger in effect: 26 teams in the AFC and NFC, three divisions each; Baltimore, Cleveland and Pittsburgh joined the 10 former AFL teams in the AFC.',
     'sources': [PFR + '/years/1970/', S_HOF % 'indianapolis-colts', 'https://en.wikipedia.org/wiki/1970_NFL_season']},
    {'season': 1975, 'kind': 'playoff_format', 'note': 'Playoff home field by record replaces the 1970-1974 divisional rotation.',
     'sources': [SRC_HOF_WILDCARD]},
    {'season': 1976, 'kind': 'expansion', 'note': 'Seattle and Tampa Bay join: 28 teams.', 'sources': [PFR + '/years/1976/']},
    {'season': 1978, 'kind': 'schedule_and_playoffs', 'note': 'Regular season 14 -> 16 games; second wild card per conference (10 playoff teams) with a Wild Card round.',
     'sources': [PFR + '/years/1978/', SRC_HOF_WILDCARD]},
    {'season': 1982, 'kind': 'strike', 'note': 'Players\' strike: 9-game regular season (PFR records); 16-team playoff tournament, top eight per conference regardless of division (Wikipedia cross-check).',
     'sources': [PFR + '/years/1982/', SRC_WIKI_PLAYOFFS]},
    {'season': 1987, 'kind': 'strike', 'note': 'Players\' strike: 15-game regular season (PFR records; one week cancelled, three weeks played by replacement players - replacement detail: Wikipedia cross-check).',
     'sources': [PFR + '/years/1987/', 'https://en.wikipedia.org/wiki/1987_NFL_season']},
    {'season': 1990, 'kind': 'playoff_format', 'note': 'Third wild card per conference: 12 playoff teams.',
     'sources': [SRC_HOF_WILDCARD]},
    {'season': 1995, 'kind': 'expansion', 'note': 'Carolina and Jacksonville join: 30 teams.', 'sources': [PFR + '/years/1995/']},
    {'season': 1996, 'kind': 'suspension', 'note': 'Cleveland franchise suspended 1996-1998; Baltimore Ravens begin. Still 30 teams.', 'sources': [PFR + '/years/1996/', S_HOF % 'cleveland-browns']},
    {'season': 1999, 'kind': 'reactivation', 'note': 'Cleveland Browns resume play: 31 teams.', 'sources': [PFR + '/years/1999/']},
    {'season': 2002, 'kind': 'expansion_and_realignment', 'note': 'Houston Texans join (32 teams); eight four-team divisions (AFC/NFC East, North, South, West); playoffs: four division winners and two wild cards per conference.',
     'sources': SRC_2002 + [PFR + '/years/2002/', SRC_WIKI_PLAYOFFS]},
    {'season': 2020, 'kind': 'playoff_format', 'note': 'Playoffs expand to 14 teams (seven per conference, one bye each); approved March 31, 2020.',
     'sources': ['https://www.chicagobears.com/news/nfl-owners-vote-to-expand-playoffs-to-14-teams', SRC_WIKI_PLAYOFFS]},
    {'season': 2021, 'kind': 'schedule', 'note': 'Regular season 16 -> 17 games (approved March 30, 2021).',
     'sources': ['https://www.fox13news.com/news/nfl-approves-17-game-regular-season-beginning-in-2021', PFR + '/years/2021/']},
]

def main():
    os.makedirs(OUT, exist_ok=True)
    seasons = build_seasons()
    json.dump({
        'schema': 'nfl2k5_eras_league_seasons/v1',
        'generated_by': 'tools/eras/league_history.py',
        'scope': 'NFL regular seasons 1970-1980: teams, conferences, divisions, records, postseason',
        'conventions': {
            'id': 'stable franchise id (see franchise_events_1970_2025.json)',
            'city/name': 'as Pro-Football-Reference lists the team that season',
            'abbreviation': 'project display convention, not a historical claim',
            'team order': ('final standings order within the division: the order PFR lists, except four ties the NFL\'s '
                           'official standings break differently (1974 AFC East, 1976 NFC West, 1977 AFC East, '
                           '1977 AFC Central), which follow the NFL Record & Fact Book (see order_note on those divisions)'),
            'finish': 'division_winner / wild_card / null, from PFR standings marks',
            'season': 'the season year; the Super Bowl is played the following January',
        },
        'seasons': seasons,
    }, open(os.path.join(OUT, 'nfl_seasons_1970_1980.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)

    franchises, counts, mismatches, al = build_events()
    json.dump({
        'schema': 'nfl2k5_eras_franchise_events/v1',
        'generated_by': 'tools/eras/league_history.py',
        'scope': 'Franchise lineage events for the 32 current NFL franchises, seasons 1970-2025 (1969 used as the baseline)',
        'kinds': {
            'merger_realignment': '1970: placed into the AFC or NFC and a new division by the AFL-NFL merger',
            'expansion': 'first season of a new franchise',
            'new_franchise': 'first season of a franchise that is legally new but took over another club\'s organisation (Ravens 1996)',
            'relocation': 'home city changed (name city changes with it)',
            'rename': 'name changed without a move (nickname, or geographic name for the same home)',
            'realignment': 'conference and/or division changed',
            'suspension': 'franchise did not play from this season',
            'reactivation': 'suspended franchise resumed play this season',
        },
        'conventions': {
            'season': 'the first season the change applies to',
            'from/to': 'identity for the season before / the season of the event (null when there was none)',
            'identities': 'consecutive seasons with the same city, name, conference and division (from PFR season standings)',
            'abbreviation': 'project display convention, not a historical claim',
        },
        'season_summary': {str(y): SUMMARY[y] for y in range(1970, 2026)},
        'season_summary_note': 'teams, games (most played by any team), playoff_teams (teams PFR marks as division winner or wild card) and division names, computed from the PFR season standings',
        'league_events': LEAGUE_EVENTS,
        'pfr_snapshots': {
            PFR + '/' + k.replace('years_1969_AFL', 'years/1969_AFL').replace('years_', 'years/').replace('teams_', 'teams/') + '/':
                v['url'].replace('id_/', '/') for k, v in sorted(INDEX.items())},
        'pfr_snapshots_note': 'PFR answers scripts with a Cloudflare challenge; every PFR page above was read from the Wayback Machine copy given here',
        'franchises': franchises,
    }, open(os.path.join(OUT, 'franchise_events_1970_2025.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)

    print('seasons:', [(s['season'], s['team_count'], s['games'], s['playoff_teams'], s['championship']['name']) for s in seasons])
    for y in range(1969, 2026):
        print('summary', y, SUMMARY[y])
    print('name mismatches (season page vs franchise page):', mismatches)
    for f in franchises:
        for e in f['events']:
            fr = e['from'] and '%s %s (%s)' % (e['from']['city'], e['from']['name'], e['from']['division'])
            to = e['to'] and '%s %s (%s)' % (e['to']['city'], e['to']['name'], e['to']['division'])
            print('%-14s %d %-18s %s -> %s' % (f['franchise_id'], e['season'], e['kind'], fr, to))

if __name__ == '__main__':
    main()

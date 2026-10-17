"""My NFL Eras (M6 rosters): each team's real <season> roster, with a rating source for every player.

    python -I tools/eras/roster_membership.py [--season 1980]

Membership comes from Pro-Football-Reference's <season> roster pages (the Wayback copies
tools/eras/league_starters.py cached in build/eras/league/pfr_cache/, sha256-checked against
data/eras/league/starters_<season>.json). Ratings and attributes are never invented: every
player's record comes from a community roster save (data/eras/community_rosters/<year>.json),
the <season> save first (on any team or in its free-agent pool), then the nearest season
(<season>+1, <season>-1, +2, -2, ...). Writes data/eras/league/rosters_<season>.json, which
make_runtime_package.py turns into rosters/<id>.csv.

Identity across saves (match_source, name_kind; every non-exact or weak match is listed in
PROVENANCE): the normalized full name (case, accents, punctuation and spaces ignored), a
nickname, an initial or a one/two-letter spelling difference; a position group compatible with
the PFR position; and the college (PFR's against the save's) as the main identity check. The
saves' colleges are often wrong, so a disagreeing college is accepted only with more evidence
(same jersey or the franchise's own club slot, or a last name almost nobody else in that save
has), and never more than NEAR seasons away. Where several people are left in one save the
strongest match wins (college, exact name, jersey, club slot); otherwise the save is skipped
for him (ambiguous, recorded) and the next season is tried. One save record serves one real
player: when two claim it (two real Mark Bells in 1980) the better match keeps it.

Selection (M8: real players only. ROSTER_TARGET = 45, the 1980 NFL active roster limit; the
staging accepts GAME_MIN..SLOTS = 42..53 players, the game's own limits):
    a player traded during the season belongs to the team where he had more GS, then more G;
    A. every player with a <season> start (GS > 0), the kicker with the most FGA and the punter
       with the most punts, within the position caps (most GS first). These are always kept, so
       a team can go past 45 with players who started that season;
    B. position minimums (MIN_POS), filled by most G, then GS (real players only: a minimum no
       real rated player can fill is recorded as a shortfall, not filled);
    C. the rest by most G, then GS, within the position caps (MAX_POS), up to ROSTER_TARGET;
    D. players over a cap, by most G, up to ROSTER_TARGET;
    E. FILLERS only as a last resort: one per HARD_MIN position (K) that no real rated player
       holds, and up to GAME_MIN rows for a team with fewer rated real players (none in 1980).
       A team with no real rated P gets none: the game punts with its K (M8 test m8b). Fillers are players of the
       franchise's own club slot in the <season> community save who are on no <season> PFR
       roster (so no real player is duplicated), preferring players present in the next save.
       Fillers are flagged in the flags file and in PROVENANCE.
The position used for caps and in the CSV (game_position) is PFR's specific <season> position
(LCB, FS, NT, ROLB, LT ...) when it is in the same group as the save's code, otherwise the
save's code (the ratings fit that group); the PFR position is recorded beside it.
Data only: cached pages are parsed as text, nothing is fetched. Run with python -I.
"""
import argparse, glob, hashlib, json, os, re, sys, unicodedata
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import league_starters as lst  # noqa: E402
import league_schedule as ls  # noqa: E402

ROOT = ls.ROOT
LEAGUE = ls.LEAGUE
CACHE = ls.CACHE
COMM = os.path.join(ROOT, 'data', 'eras', 'community_rosters')
SLOTS = 53              # most rows a team slot takes (the disc templates hold 53 players)
ROSTER_TARGET = 45      # the 1980 NFL active roster limit
GAME_MIN = 42           # the game's minimum ("You must keep 42 players on your team ...")
NEAR = 4                # saves more than NEAR seasons away need an agreeing college

POS = ('QB', 'K', 'P', 'WR', 'CB', 'FS', 'SS', 'HB', 'FB', 'TE', 'OLB', 'ILB', 'C', 'G', 'T', 'DT', 'DE')
GAME_GROUP = {'QB': 'QB', 'K': 'K', 'P': 'P', 'WR': 'WR', 'CB': 'DB', 'FS': 'DB', 'SS': 'DB', 'HB': 'RB',
              'FB': 'RB', 'TE': 'TE', 'OLB': 'LB', 'ILB': 'LB', 'C': 'OL', 'G': 'OL', 'T': 'OL', 'DT': 'DL',
              'DE': 'DL'}
PFR_GROUP = {}
for g, codes in {'QB': 'QB', 'K': 'K', 'P': 'P', 'RB': 'RB HB FB', 'WR': 'WR', 'TE': 'TE',
                 'OL': 'T LT RT G LG RG C OL OT OG LS', 'DL': 'DE LDE RDE DT LDT RDT NT DL',
                 'LB': 'LB OLB ILB MLB LLB RLB LOLB ROLB LILB RILB',
                 'DB': 'DB CB LCB RCB S FS SS'}.items():
    for c in codes.split():
        PFR_GROUP[c] = g
NEIGHBOURS = {frozenset(p) for p in (('RB', 'WR'), ('WR', 'TE'), ('TE', 'OL'), ('RB', 'TE'), ('DL', 'LB'),
                                     ('LB', 'DB'), ('K', 'P'))}
# per game position: minimum wanted, cap, typical count (fillers aim at TYPICAL)
MIN_POS = {'QB': 2, 'K': 1, 'P': 1, 'HB': 2, 'FB': 1, 'WR': 4, 'TE': 2, 'T': 3, 'G': 3, 'C': 1,
           'DE': 3, 'DT': 2, 'OLB': 2, 'ILB': 2, 'CB': 3, 'FS': 1, 'SS': 1}
MAX_POS = {'QB': 3, 'K': 2, 'P': 2, 'HB': 6, 'FB': 4, 'WR': 7, 'TE': 5, 'T': 6, 'G': 6, 'C': 3,
           'DE': 6, 'DT': 6, 'OLB': 6, 'ILB': 6, 'CB': 7, 'FS': 4, 'SS': 4}
# a position no real rated player holds gets one flagged filler only if it is listed here (M8)
HARD_MIN = {'K': 1}     # not P: with no P on the roster the game punts with the K (M8 test m8b)
TYPICAL_POS = {'QB': 3, 'K': 1, 'P': 1, 'HB': 4, 'FB': 2, 'WR': 5, 'TE': 3, 'T': 4, 'G': 4, 'C': 2,
               'DE': 4, 'DT': 3, 'OLB': 4, 'ILB': 4, 'CB': 5, 'FS': 2, 'SS': 2}
NICKNAMES = [set(g.split()) for g in (
    'robert bob bobby rob', 'william bill billy will willie', 'james jim jimmy jimmie', 'michael mike',
    'thomas tom tommy', 'richard dick rick ricky rich richie', 'joseph joe joey', 'david dave',
    'edward ed eddie', 'kenneth ken kenny', 'anthony tony', 'charles chuck charlie', 'lawrence larry',
    'steven stephen steve', 'daniel dan danny', 'gerald jerry', 'ronald ron ronnie', 'donald don donnie',
    'patrick pat', 'arthur art', 'frederick fred freddie', 'theodore ted', 'herman herm',
    'matthew matt', 'douglas doug', 'gregory greg', 'raymond ray', 'johnny john jon', 'leonard len lenny',
    'samuel sam', 'timothy tim', 'andrew andy', 'benjamin ben', 'nicholas nick', 'albert al',
    'alan al', 'alfred al', 'eugene gene', 'harold hal', 'walter walt', 'stanley stan', 'vincent vince',
    'jeffrey jeff', 'gary garry', 'terrence terry terence', 'clarence clancy', 'delvin del',
    'frank franklin', 'louis lou', 'philip phil', 'curtis curt', 'kevin kev', 'randall randy',
    'reginald reggie', 'calvin cal', 'nathaniel nate nat', 'jerome jerry', 'oliver ollie',
    'zachary zach', 'wesley wes', 'willard will willie', 'clifford cliff', 'russell russ', 'gilbert gil', 'abraham abe')]


def norm(s):
    s = unicodedata.normalize('NFKD', s or '').encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z]', '', s)


COLLEGE_ALIAS = {'lsu': 'louisianastate', 'smu': 'southernmethodist', 'tcu': 'texaschristian',
                 'usc': 'southerncalifornia', 'byu': 'brighamyoung', 'unlv': 'nevadalasvegas',
                 'utep': 'texaselpaso', 'olemiss': 'mississippi', 'pitt': 'pittsburgh', 'miamifl': 'miami',
                 'miamifla': 'miami', 'mdeasternshore': 'marylandeasternshore',
                 'texasaandmcom': 'texasaandmcommerce', 'easttexasstate': 'texasaandmcommerce',
                 'texasaandi': 'texasaandmkingsville', 'southwesttexasstate': 'texasstate'}


def norm_college(s):
    if not s or s == 'None':
        return ''
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().lower()
    s = re.sub(r'\bst\.?(?=[\s,)-]|$)', 'state', s).replace('&', 'and')
    s = re.sub(r'\b(university|univ|of|the|college)\b', '', s)
    s = re.sub(r'[^a-z]', '', s)
    return COLLEGE_ALIAS.get(s, s)


def edit_distance(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def college_agrees(a, b):
    """True/False when both are known, None when either is missing. PFR lists every college a
    player attended, comma-separated; any one agreeing is enough."""
    xs = [norm_college(x) for x in (a or '').split(',')]
    xs = [x for x in xs if x]
    b = norm_college(b)
    if not xs or not b:
        return None
    return any(x == b or (min(len(x), len(b)) >= 4 and (x in b or b in x)) or edit_distance(x, b) <= 2
               for x in xs)


def split_name(name):
    parts = name.split(' ', 1)
    return (parts[0], parts[1]) if len(parts) == 2 else ('', parts[0])


def name_kind(pfr_name, first, last):
    """How a save's (first, last) names the PFR player: 'exact' (normalized full names equal, so
    "Lee Roy|Selmon" = "Lee Roy Selmon"), 'nickname' (same last name, first names in one NICKNAMES
    set), 'initial' (same last name, the save's first name an initial of PFR's: "A." = "A.J."),
    'spelling' (full names within one letter, at least 9 letters; two letters from 12) or None."""
    fp, fc, nl, nf = norm(pfr_name), norm(first + last), norm(last), norm(first)
    if fp == fc:
        return 'exact'
    if nl and fp.endswith(nl) and len(fp) > len(nl):
        pre = fp[:-len(nl)]
        if any(pre in g and nf in g for g in NICKNAMES):
            return 'nickname'
        if nf and len(nf) <= 2 and pre.startswith(nf) and re.fullmatch(r'([A-Za-z]\.\s?)+', first.strip()):
            return 'initial'
    if min(len(fp), len(fc)) >= 9 and edit_distance(fp, fc) <= (2 if min(len(fp), len(fc)) >= 12 else 1):
        return 'spelling'
    return None


def group_ok(pfr_pos, game_pos):
    g = PFR_GROUP.get((pfr_pos or '').split('/')[0])
    if g is None:
        return True
    h = GAME_GROUP[game_pos]
    return g == h or frozenset((g, h)) in NEIGHBOURS


# PFR's specific 1980 positions -> game codes (generic DB, LB, RB, OL, DL, S keep the save's code)
PFR_SPECIFIC = {'LCB': 'CB', 'RCB': 'CB', 'CB': 'CB', 'FS': 'FS', 'SS': 'SS', 'LOLB': 'OLB', 'ROLB': 'OLB',
                'OLB': 'OLB', 'LLB': 'OLB', 'RLB': 'OLB', 'LILB': 'ILB', 'RILB': 'ILB', 'ILB': 'ILB', 'MLB': 'ILB',
                'LDE': 'DE', 'RDE': 'DE', 'DE': 'DE', 'NT': 'DT', 'LDT': 'DT', 'RDT': 'DT', 'DT': 'DT',
                'LT': 'T', 'RT': 'T', 'T': 'T', 'LG': 'G', 'RG': 'G', 'G': 'G', 'C': 'C', 'FB': 'FB', 'HB': 'HB'}


def game_position(pfr_pos, save_pos, starter_pos=None):
    """The game position of a real player: PFR's specific <season> position when it is in the same
    group as the save's (a 1980 FS the save codes CB plays FS; the ratings are the record's, and
    the groups share the same rating profile), otherwise the save's (a different group would need
    other ratings: Ray Hamilton, 1980 NT, stays the G the saves make him). The Starters table's
    position comes first (it tells FB from RB), then the roster table's."""
    p = None
    if starter_pos:     # PFR's Starters table: its RB is the halfback beside the FB
        p = dict(PFR_SPECIFIC, RB='HB').get(starter_pos.split('/')[0])
    p = p or PFR_SPECIFIC.get((pfr_pos or '').split('/')[0])
    if p and GAME_GROUP[p] == GAME_GROUP[save_pos]:
        return p
    return save_pos


def num(x):
    return int(x) if re.fullmatch(r'\d+', x or '') else None


def parse_pfr_details(path):
    """Height, weight, college, experience, birth date per PFR id, from the roster table."""
    s = open(path, encoding='utf-8', errors='replace').read()
    t = lst.table(s, 'roster')
    out = {}
    for pid, name, c in lst.rows_of(t):
        c = {k: ls.strip(v) for k, v in c.items()}
        ht = re.fullmatch(r'(\d)-(\d{1,2})', c.get('height', ''))
        exp = c.get('experience', '')
        out[pid] = {'height_in': int(ht.group(1)) * 12 + int(ht.group(2)) if ht else None,
                    'weight_lb': num(c.get('weight', '')), 'college': c.get('college_id') or None,
                    'experience': 0 if exp == 'Rook' else num(exp), 'birth_date': c.get('birth_date_mod') or None,
                    'age': num(c.get('age', ''))}
    return out


def community_seasons(season):
    have = sorted(int(os.path.basename(p)[:4]) for p in glob.glob(os.path.join(COMM, '[0-9][0-9][0-9][0-9].json')))
    order = [season] if season in have else []
    for d in range(1, 60):
        for y in (season + d, season - d):
            if y in have:
                order.append(y)
    return order


class Save:
    def __init__(self, year):
        self.year = year
        self.path = os.path.join(COMM, '%d.json' % year)
        d = json.load(open(self.path, encoding='utf-8'))
        self.source = d['source']
        self.abbr_of_slot = {t['index']: t['abbreviation'] for t in d['teams']}
        self.players = [p for p in d['players'] if p['group'] in ('team', 'free_agent')]
        self.by_last = defaultdict(list)
        for p in self.players:
            self.by_last[norm(p['last'])].append(p)
            w = p['last'].split()
            if len(w) > 1:
                self.by_last[norm(w[-1])].append(p)
        self.by_first = defaultdict(list)
        for p in self.players:
            self.by_first[norm(p['first'])].append(p)
        self.by_full = defaultdict(list)
        for p in self.players:
            self.by_full[norm(p['first'] + p['last'])].append(p)
        self.names = set(self.by_full)


def person(p):
    return (norm(p['first'] + p['last']), p['position_code'], norm_college(p['college']), p['height_in'])


def match_source(save, pfr, det, franchise_slot, season, extra_groups=(), exclude=()):
    """The record of this PFR player in one save: (player, info) or (None, reason).
    Name (name_kind), position group (group_ok; extra_groups adds K/P for a player with FGA or
    punts that season, e.g. WR/P Pat McInally), college (college_agrees), jersey and club slot:
        strong   colleges agree, or one is unknown and the name is exact or a nickname;
        weak     colleges differ (the saves' colleges have errors: Vern Den Herder is "Temple" in
                 1980): exact name or nickname, the same position group (not a neighbour), and the
                 same jersey or the franchise's own club slot, or an exact name whose last name
                 belongs to at most two people in that save (Lyle Alzado);
        posdiff  position group incompatible: exact name, colleges agree and the same jersey
                 (Ray Hamilton, NT in 1980, is a G in the saves); flagged, his GS do not count for
                 the depth chart at the save's position;
        alias    another first name (Dirt / Dennis Winston): same last name, colleges agree, same
                 jersey and the franchise's own club slot in the <season> save.
    Initials and spellings need agreeing colleges. A save more than NEAR seasons away from <season>
    is only used with an agreeing college (a 1993 Mike Williams is not the 1980 one)."""
    words = pfr['name'].split()
    keys = {norm(pfr['name'])} | {norm(w) for w in words}
    cands = {}
    for k in keys:
        for p in save.by_full.get(k, []) + save.by_last.get(k, []) + save.by_first.get(k, []):
            if p['record_index'] not in exclude:
                cands[id(p)] = p
    pg = PFR_GROUP.get((pfr['pos'] or '').split('/')[0])
    pl = norm(words[-1]) if words else ''
    hits = []
    for p in cands.values():
        kind = name_kind(pfr['name'], p['first'], p['last'])
        gpos = POS[p['position_code']]
        ggrp = GAME_GROUP[gpos]
        col = college_agrees(det.get('college'), p['college'])
        same_no = pfr['number'] != '' and str(p['jersey']) == pfr['number']
        own = p['team'] == franchise_slot
        g_ok = group_ok(pfr['pos'], gpos) or ggrp in extra_groups
        same_group = pg is None or pg == ggrp or ggrp in extra_groups
        if not kind:
            if (norm(p['last']) == pl and col is True and same_no and own and save.year == season
                    and same_group):
                kind, strength = 'alias', 'alias'
            else:
                continue
        elif not g_ok:
            if kind == 'exact' and col is True and same_no:
                strength = 'posdiff'
            else:
                continue
        elif col is True:
            strength = 'strong'
        elif col is None and kind in ('exact', 'nickname'):
            strength = 'strong'
        elif col is False and kind in ('exact', 'nickname') and same_group and (same_no or own):
            strength = 'weak'
        elif col is False and kind == 'exact' and same_group and len(
                {person(q) for q in save.by_last.get(norm(p['last']), [])}) <= 2:
            strength = 'weak'
        else:
            continue
        if abs(save.year - season) > NEAR and col is not True:
            continue        # far from the season only an agreeing college identifies him
        if abs(save.year - season) > NEAR and strength == 'posdiff':
            continue
        hits.append((p, kind, col, same_no, strength))
    if not hits:
        return None, 'no match'
    # one record per person (the same record can sit on two club rosters; duplicates share a bio)
    people = {}
    for h in hits:
        people.setdefault(person(h[0]), []).append(h)
    if len(people) > 1:
        for test in (lambda h: h[4] == 'strong', lambda h: h[4] in ('strong', 'weak'), lambda h: h[1] == 'exact', lambda h: h[2] is True,
                     lambda h: h[3], lambda h: h[0]['team'] == franchise_slot):
            keep = {k: v for k, v in people.items() if any(test(h) for h in v)}
            if len(keep) == 1:
                people = keep
                break
            if keep:
                people = keep
    if len(people) > 1:
        return None, 'ambiguous (%s)' % '; '.join('%s %s %s %s' % (v[0][0]['first'], v[0][0]['last'],
                                                                    POS[v[0][0]['position_code']], v[0][0]['college'])
                                                   for v in people.values())
    group = list(people.values())[0]
    # the record on a club roster first (team over free agent), the franchise's slot first
    group.sort(key=lambda h: (h[0]['group'] != 'team', h[0]['team'] != franchise_slot, h[0]['record_index']))
    p, kind, col, same_no, strength = group[0]
    return p, {'kind': kind, 'college': {True: 'agrees', False: 'differs', None: 'unknown'}[col],
               'same_jersey': same_no, 'strength': strength}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1980)
    a = ap.parse_args()
    build(a.season)


def build(season):
    st_doc = json.load(open(os.path.join(LEAGUE, 'starters_%d.json' % season), encoding='utf-8'))
    teams = st_doc['teams']
    import make_runtime_package as mp
    save_abbr = mp.SAVE_ABBR_2004
    order = community_seasons(season)
    saves = {}

    def save(y):
        if y not in saves:
            saves[y] = Save(y)
        return saves[y]
    base = save(season)
    slot_of_abbr = {v: k for k, v in base.abbr_of_slot.items() if k < 32}

    # PFR details, sha-checked against the starters file's sources
    details = {}
    for fid, t in teams.items():
        path = os.path.join(CACHE, 'roster_%s_%d.html' % (t['pfr_code'], season))
        h = hashlib.sha256(open(path, 'rb').read()).hexdigest()
        assert h == t['sources']['roster_sha256'], (fid, 'cached roster page differs from starters file')
        details.update(parse_pfr_details(path))

    # traded players: one team each
    on_teams = defaultdict(list)
    for fid, t in teams.items():
        for p in t['roster']:
            on_teams[p['pfr_id']].append((fid, p))
    home_of, traded = {}, []
    for pid, lst_ in on_teams.items():
        best = max(lst_, key=lambda x: (x[1]['gs'], x[1]['g'], -sorted(teams).index(x[0])))
        home_of[pid] = best[0]
        if len(lst_) > 1:
            traded.append({'pfr_id': pid, 'name': best[1]['name'], 'team': best[0],
                           'also': ['%s (%d G, %d GS)' % (f, p['g'], p['gs']) for f, p in lst_ if f != best[0]],
                           'here': '%d G, %d GS' % (best[1]['g'], best[1]['gs'])})

    # stage 1: a rating source for every real player (traded players with their main team)
    entries = {}            # pfr_id -> entry
    team_entries = defaultdict(list)
    left_off = defaultdict(list)
    excl = defaultdict(set)         # pfr_id -> {(season, record_index)} lost to a better claim
    slot_of = {fid: slot_of_abbr[save_abbr[fid]] for fid in teams}

    def find_source(fid, p, det, extra):
        tried = []
        for y in order:
            ex = {r for (yy, r) in excl[p['pfr_id']] if yy == y}
            m, info = match_source(save(y), p, det, slot_of[fid], season, extra, ex)
            if m:
                return (y, m, info), tried
            if info != 'no match':
                tried.append('%d: %s' % (y, info))
        return None, tried

    def set_source(e, src, tried):
        e.pop('source', None)
        e.pop('reason', None)
        if tried:
            e['ambiguous_saves'] = tried
        if not src:
            e['reason'] = 'no rating source in any community save'
            return
        y, m, info = src
        e['source'] = {'season': y, 'record_index': m['record_index'], 'first': m['first'], 'last': m['last'],
                       'position_code': m['position_code'], 'position': POS[m['position_code']],
                       'jersey': m['jersey'], 'college': m['college'], 'group': m['group'],
                       'club': save(y).abbr_of_slot.get(m['team']) if m['team'] is not None else None,
                       'club_slot': m['team'], 'match': info, 'flags': m['flags'],
                       'height_in': m['height_in'], 'weight_lb': m['weight_lb'], 'years_pro': m['years_pro']}
        e['position'] = game_position(e['pfr_pos'], POS[m['position_code']], e.get('pfr_starter_pos'))
        e['position_code'] = POS.index(e['position'])

    def score(e):
        s_, m = e['source'], e['source']['match']
        return ({'strong': 3, 'alias': 1, 'weak': 1, 'posdiff': 0}[m['strength']], m['college'] == 'agrees',
                m['same_jersey'], s_['club_slot'] == slot_of[e['team']], m['kind'] == 'exact',
                -abs(s_['season'] - season))
    for fid in sorted(teams):
        t = teams[fid]
        fga = {k['pfr_id']: k['fga'] for k in t['kicking']}
        punts = {k['pfr_id']: k['punts'] for k in t['punting']}
        starters_ids = {x['pfr_id'] for x in t['starters']}
        starter_pos = {x['pfr_id']: x['pos'] for x in t['starters']}
        for p in t['roster']:
            if home_of[p['pfr_id']] != fid:
                left_off[fid].append({'name': p['name'], 'pfr_pos': p['pos'], 'g': p['g'], 'gs': p['gs'],
                                      'reason': 'traded: counted with %s (more GS/G there)' % home_of[p['pfr_id']]})
                continue
            det = details.get(p['pfr_id'], {})
            e = {'pfr_id': p['pfr_id'], 'team': fid, 'name': p['name'], 'pfr_pos': p['pos'], 'number': p['number'],
                 'g': p['g'], 'gs': p['gs'], 'fga': fga.get(p['pfr_id'], 0), 'punts': punts.get(p['pfr_id'], 0),
                 'pfr_starter': p['pfr_id'] in starters_ids, 'pfr_starter_pos': starter_pos.get(p['pfr_id']),
                 'pfr': det,
                 '_extra': tuple(g for g, v in (('K', fga.get(p['pfr_id'], 0)), ('P', punts.get(p['pfr_id'], 0))) if v > 0),
                 '_p': p}
            set_source(e, *find_source(fid, p, det, e['_extra']))
            entries[p['pfr_id']] = e
            team_entries[fid].append(e)
    # stage 2: one record, one real player. Two PFR players naming the same record (two real
    # Mark Bells in 1980): the better match keeps it, the other looks again without it.
    conflicts = []
    while True:
        claims = defaultdict(list)
        for e in entries.values():
            if 'source' in e:
                claims[(e['source']['season'], e['source']['record_index'])].append(e)
        clash = [(k, v) for k, v in claims.items() if len(v) > 1]
        if not clash:
            break
        for k, v in clash:
            v.sort(key=score, reverse=True)
            for loser in v[1:]:
                excl[loser['pfr_id']].add(k)
                conflicts.append('%d save record %d (%s %s): kept by %s (%s); %s (%s) looked again' % (
                    k[0], k[1], v[0]['source']['first'], v[0]['source']['last'], v[0]['name'], v[0]['team'],
                    loser['name'], loser['team']))
                set_source(loser, *find_source(loser['team'], loser['_p'], loser['pfr'], loser['_extra']))
    used_records = set()

    out_teams = {}
    tally = Counter()
    for fid in sorted(teams):
        t = teams[fid]
        slot = slot_of[fid]
        cands = [e for e in team_entries[fid] if 'source' in e]
        no_source = [e for e in team_entries[fid] if 'source' not in e]

        def key_gs(e):
            return (-e['gs'], -e['g'], e['name'])

        def key_g(e):
            return (-e['g'], -e['gs'], e['name'])
        chosen, count = [], Counter()

        def take(e, why):
            e['picked'] = why
            chosen.append(e)
            count[e['position']] += 1
        top_k = max((e for e in cands if e['fga'] > 0), key=lambda e: e['fga'], default=None)
        top_p = max((e for e in cands if e['punts'] > 0), key=lambda e: e['punts'], default=None)
        for e, why in ((top_k, 'kicker (most FGA: %d)' % (top_k or {}).get('fga', 0)),
                       (top_p, 'punter (most punts: %d)' % (top_p or {}).get('punts', 0))):
            if e and e not in chosen:
                take(e, why)
        for e in sorted(cands, key=key_gs):
            if e in chosen or e['gs'] <= 0:
                continue
            if count[e['position']] >= MAX_POS[e['position']]:
                continue
            take(e, 'started (%d GS)' % e['gs'])
        for pos, need in MIN_POS.items():
            for e in sorted(cands, key=key_g):
                if count[pos] >= need or len(chosen) >= SLOTS:
                    break
                if e not in chosen and e['position'] == pos:
                    take(e, 'position minimum %s (%d G)' % (pos, e['g']))
        for e in sorted(cands, key=key_g):
            if len(chosen) >= ROSTER_TARGET:
                break
            if e in chosen or count[e['position']] >= MAX_POS[e['position']]:
                continue
            take(e, 'games played (%d G)' % e['g'])
        # a real player over a position cap still beats a filler
        for e in sorted(cands, key=key_g):
            if len(chosen) >= max(ROSTER_TARGET, GAME_MIN):
                break
            if e not in chosen:
                take(e, 'games played (%d G; over the %s cap)' % (e['g'], e['position']))
        for e in sorted(cands, key=key_g):
            if e not in chosen:
                capped = count[e['position']] >= MAX_POS[e['position']]
                left_off[fid].append({'name': e['name'], 'pfr_pos': e['pfr_pos'], 'g': e['g'], 'gs': e['gs'],
                                      'reason': ('position cap %s %d' % (e['position'], MAX_POS[e['position']])
                                                 if capped else '%d-man roster full (fewer games than those kept)'
                                                 % ROSTER_TARGET)})
        assert len(chosen) <= SLOTS, fid
        out_teams[fid] = {'pfr_code': t['pfr_code'], 'community_slot': slot, 'sources': t['sources'],
                          'shortfalls': {p_: '%d of %d' % (count[p_], n_) for p_, n_ in MIN_POS.items()
                                         if count[p_] < n_},
                          'players': chosen, 'no_source': no_source, 'left_off': left_off[fid],
                          'starters': t['starters'], '_count': count}
        used_records |= {(e['source']['season'], e['source']['record_index']) for e in cands}
    # stage 3: fillers, from the franchise's own club slot of the <season> save: on no PFR roster
    # (no name_kind match with any PFR <season> player, so not "Lyle Alzedo" beside the real
    # Alzado), not a record any real player uses
    pfr_by_last = defaultdict(list)
    for t in teams.values():
        for p in t['roster']:
            pfr_by_last[norm(p['name'].split()[-1])].append(p['name'])

    def real_name(first, last):
        cands_ = pfr_by_last.get(norm(last.split()[-1]), []) + pfr_by_last.get(norm(last), [])
        return any(name_kind(n, first, last) for n in cands_)
    nxt = save(order[1]) if len(order) > 1 else None
    for fid in sorted(teams):
        o = out_teams[fid]
        chosen, count, slot = o['players'], o.pop('_count'), o['community_slot']
        fillers = []
        pool = [p for p in base.players if p['team'] == slot and not real_name(p['first'], p['last'])
                and (season, p['record_index']) not in used_records]
        pool.sort(key=lambda p: p['depth_slot'])
        def pick_filler(want_pos=None):
            have = count.copy()
            for f in fillers:
                have[POS[f['position_code']]] += 1

            def fkey(p):
                pos = POS[p['position_code']]
                active = bool(nxt and norm(p['first'] + p['last']) in nxt.names)
                return (have[pos] >= MIN_POS[pos], not active, -(TYPICAL_POS[pos] - have[pos]),
                        have[pos] >= MAX_POS[pos], p['depth_slot'])
            cand = [p for p in pool if want_pos is None or POS[p['position_code']] == want_pos]
            if not cand:
                return None
            p = min(cand, key=fkey)
            pool.remove(p)
            used_records.add((season, p['record_index']))
            f = dict(p, active_next=bool(nxt and norm(p['first'] + p['last']) in nxt.names))
            fillers.append(f)
            return f
        why = {}
        # last resort 1: a position the game cannot do without that no real rated player holds
        for pos, need in HARD_MIN.items():
            while count[pos] + sum(1 for f in fillers if POS[f['position_code']] == pos) < need:
                f = pick_filler(pos)
                if not f:
                    break
                why[f['record_index']] = ('filler: no real rated %d %s on the team; one %s kept from the community '
                                          'club slot (on no PFR %d roster)' % (season, pos, pos, season))
        # last resort 2: the game's 42-player minimum
        while len(chosen) + len(fillers) < GAME_MIN and pool:
            f = pick_filler()
            why[f['record_index']] = ('filler: %d rated real players, below the game minimum of %d; kept from the '
                                      'community club slot (on no PFR %d roster)' % (len(chosen), GAME_MIN, season))
        assert GAME_MIN <= len(chosen) + len(fillers) <= SLOTS, (fid, len(chosen), len(fillers))
        o['fillers'] = [{'source': {'season': season, 'record_index': f['record_index'], 'first': f['first'],
                                    'last': f['last'], 'position_code': f['position_code'],
                                    'position': POS[f['position_code']], 'jersey': f['jersey'],
                                    'college': f['college'], 'club': base.abbr_of_slot[slot], 'club_slot': slot,
                                    'flags': f['flags'], 'height_in': f['height_in'], 'weight_lb': f['weight_lb'],
                                    'years_pro': f['years_pro']},
                         'position': POS[f['position_code']], 'position_code': f['position_code'],
                         'active_next_save': f['active_next'],
                         'picked': why[f['record_index']]}
                        for f in fillers]
        tally['chosen'] += len(chosen)
        tally['fillers'] += len(fillers)
        tally['no_source'] += len(o['no_source'])
        print('%-14s PFR %2d, kept %2d, fillers %d (%s), no source %2d, left off %2d; short %s; sources %s' % (
            fid, len(teams[fid]['roster']), len(chosen), len(fillers),
            ' '.join(POS[f['position_code']] for f in fillers) or '-', len(o['no_source']), len(o['left_off']),
            o['shortfalls'] or '-',
            dict(sorted(Counter(c['source']['season'] for c in chosen).items()))))
    for e in entries.values():
        e.pop('_p', None)
        e.pop('_extra', None)
    for c in conflicts:
        print('conflict:', c)
    out = os.path.join(LEAGUE, 'rosters_%d.json' % season)
    json.dump({'schema': 'nfl2k5_eras_rosters/v1', 'generated_by': 'tools/eras/roster_membership.py',
               'season': season, 'slots': SLOTS, 'roster_target': ROSTER_TARGET,
               'game_min': GAME_MIN, 'season_order': order,
               'saves': {str(y): {'file': 'data/eras/community_rosters/%d.json' % y,
                                  'sha256': hashlib.sha256(open(s.path, 'rb').read()).hexdigest(),
                                  'payload': s.source['payload'], 'payload_sha256': s.source['sha256']}
                         for y, s in sorted(saves.items())},
               'starters_file_sha256': hashlib.sha256(open(os.path.join(LEAGUE, 'starters_%d.json' % season), 'rb').read()).hexdigest(),
               'rules': {'min': MIN_POS, 'max': MAX_POS, 'typical': TYPICAL_POS, 'hard_min': HARD_MIN},
               'traded': sorted(traded, key=lambda x: x['name']), 'record_conflicts': conflicts,
               'teams': out_teams},
              open(out, 'w', encoding='utf-8'), indent=1)
    print('wrote %s: %s' % (os.path.relpath(out, ROOT), dict(tally)))


if __name__ == '__main__':
    main()

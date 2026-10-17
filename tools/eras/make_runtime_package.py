"""My NFL Eras (M0 task D1): the runtime package one season's league needs in game.

    python -I tools/eras/make_runtime_package.py [--season 1980] [--check] [--out DIR]

Reads (nothing is fetched):
    data/eras/league/nfl_seasons_1970_1980.json      teams, conferences, divisions, abbreviations
    data/eras/league/franchise_events_1970_2025.json identity per season (the template city rule)
    data/eras/historic_teams_2k5.json                disc historic teams (templates, entries 0-74)
    the season's community roster save payload       through the mod tools' RosterDocument (the
                                                     extracted JSON lacks skin, face and hand)
    data/eras/community_rosters/<season>.json        the extractor's per-player flags (joined by
                                                     slot and list position, names cross-checked)
    data/eras/league/lineage_check_<season>.json     D3 result for the renamed slots (optional)
    data/eras/league/nfl_schedule_<season>.json      schedule provenance (optional; D2)
    data/eras/league/starters_<season>.json          verified starters, GS, FGA, punts per team
                                                     (optional; tools/eras/league_starters.py)
    data/eras/league/rosters_<season>.json           verified membership: each team's real players
                                                     (PFR) and the save record that rates each one
                                                     (optional; tools/eras/roster_membership.py).
                                                     When it exists the rosters are built from it
                                                     (MEMBERSHIP MODE below); without it, from the
                                                     season's save slots as before

Writes data/eras/runtime/<season>/:
    teams.csv          id,city,name,abbr,conference,division,template_entry,template_id,
                       roster_file,community_slot,lineage_status,stadium,stadium_match,stadium_historical,
                       style (the season's disc uniform style from data/eras/league/team_looks.json,
                       else the template's own; written to team +0x192 after staging, M9)
    rosters/<id>.csv   the roster.csv format nfl2k5_team_roster reads (src/nfl2k5_team_files.c):
                       first,last,pos,number,height,weight,years_pro,skin,face,hand,<28 ratings>,photo
    rosters/<id>.flags.txt   what is odd about each row (kept as stored, never "fixed")
    depth/<id>.csv     the default depth chart: the save's stored order, with the verified
                       starters (most games started; K most FGA, P most punts) moved first
    PROVENANCE.txt     where every part came from, and what is unverified

Roster rows are in the save's team-list order, and the game-side writer ranks depth within
each position by row order. The depth fields stored in the records are compared with that
order (depth_key); rows where they disagree are kept in list order and named in the flags
file (PROVENANCE.txt gives the counts). Ratings above 99 are written as 99, the
value nfl2k5_team_roster clamps them to anyway, and listed in the flags file. Heights and
weights are kept as stored. Birth dates are not written (the save's birth-year convention is
mixed or unclear). Photo is 39999 ("no photo").

MEMBERSHIP MODE (rosters_<season>.json present): each team's rows are its real players (PFR
<season> roster, chosen by roster_membership.py: up to 45, the 1980 active roster limit, more
only for players who started that season), 42..53 rows (the game's minimum and the templates'
player count; src/nfl2k5_teams.c NFL2K5_ROSTER_MIN), plus a flagged filler only where the
membership file names one. A team with no real rated P has none: the game punts with the K. Each row is read from its save record (that season's payload, through
RosterDocument, sha256-checked): names, position, height, weight, skin, face, hand and the 28
ratings are the record's. The jersey is the PFR <season> number, years_pro the PFR experience
("Rook" = 0) where PFR has them. A name the save misspells (a "spelling" or "initial" match) is
written as PFR spells it. Rows are ordered by position code, then by the depth order. Depth:
per position code most <season> GS first (K: most FGA, P: most punts); GS count only where the
PFR position group fits the game position (a save that lists a 1980 NT as a G does not make him
the starting G); then most games, then GS. Paired positions: the two leaders head the left and
right chains, sides from PFR's starters table (LT/RT, LCB/RCB ...) where it gives them.

--check (T0.1) only checks the written files. --out writes (and checks) another folder instead
of data/eras/runtime/<season> (also ERAS_RUNTIME_OUT). Data only; run with python -I.
"""
import argparse, csv, hashlib, json, os, re, sys, textwrap, unicodedata
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, '.tmp-2k-football-mod-tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # roster_membership (membership mode)
LEAGUE = os.path.join(ROOT, 'data', 'eras', 'league')
SOURCE_LABEL = 'community PS2 roster (unverified)'
NO_PHOTO = 39999
RATING_MAX = 99                     # nfl2k5_team_roster clamps every rating to 0..99
NAME_MAX = 31                       # M2's per-side name arena: 32 UTF-16 chars with the terminator
ROWS_MIN, ROWS_MAX = 42, 53         # M8: what the staging takes (game minimum .. template player count)
# M8: per position code, what the staging requires (src/nfl2k5_teams.c k_stage_min): base offense,
# base defense, K and P. Checked by T0.1 so a package the game would refuse never ships.
STAGE_MIN = {'QB': 1, 'K': 1, 'P': 0, 'WR': 2, 'CB': 2, 'FS': 1, 'SS': 0, 'HB': 1, 'FB': 0, 'TE': 1,
             'OLB': 2, 'ILB': 1, 'C': 1, 'G': 2, 'T': 2, 'DT': 1, 'DE': 2}
HEADER = ['first', 'last', 'pos', 'number', 'height', 'weight', 'years_pro', 'skin', 'face', 'hand']
TEAMS_HEADER = ['id', 'city', 'name', 'abbr', 'conference', 'division', 'template_entry', 'template_id',
                'roster_file', 'community_slot', 'lineage_status', 'stadium', 'stadium_match', 'stadium_historical',
                'style']

# Club slots of the community saves carry the retail 2004 names (COMMUNITY-ROSTERS.md); each
# franchise id's slot is found by its 2004 abbreviation (franchise lineage).
SAVE_ABBR_2004 = {
    '49ers': 'SF', 'bears': 'CHI', 'bengals': 'CIN', 'bills': 'BUF', 'broncos': 'DEN', 'browns': 'CLE',
    'buccaneers': 'TB', 'cardinals': 'ARZ', 'chargers': 'SD', 'chiefs': 'KC', 'colts': 'IND',
    'cowboys': 'DAL', 'dolphins': 'MIA', 'eagles': 'PHI', 'falcons': 'ATL', 'giants': 'NYG',
    'jaguars': 'JAX', 'jets': 'NYJ', 'lions': 'DET', 'packers': 'GB', 'panthers': 'CAR', 'patriots': 'NE',
    'raiders': 'OAK', 'rams': 'STL', 'ravens': 'BAL', 'washington': 'WAS', 'saints': 'NO',
    'seahawks': 'SEA', 'steelers': 'PIT', 'texans': 'HOU', 'oilers_titans': 'TEN', 'vikings': 'MIN',
}

# Historic-team labels (the game's nicknames) -> league franchise ids (LEAGUE-HISTORY.md,
# "Matching the historic teams on the disc"). DAL_1962 "texans" is left out: its identity was
# not checked (it may be the AFL Dallas Texans, i.e. the chiefs), and it is not the Houston Texans.
HISTORIC_ALIAS = {'oilers': 'oilers_titans', 'titans': 'oilers_titans', 'redskins': 'washington'}
HISTORIC_EXCLUDE = {'DAL_1962'}

# Retail left/right depth chains (the mod tools' nfl2k5_espn25_more_moments.PAIRED); other
# positions keep one chain ordered by rank.
PAIRED = {'T', 'G', 'DE', 'DT', 'OLB', 'ILB', 'CB', 'WR'}

# PHASE2-PLAN.md section 2.4, the template table (checked by --check for 1980).
PLAN_TEMPLATES_1980 = {
    'bills': 'BUF_1990', 'patriots': 'NE_1985', 'dolphins': 'MIA_1984', 'colts': 'BAL_1970',
    'jets': 'NYJ_1968', 'browns': 'CLE_1986', 'oilers_titans': 'HOU_1979', 'steelers': 'PIT_1978',
    'bengals': 'CIN_1981', 'chargers': 'SD_1980', 'raiders': 'OAK_1976', 'chiefs': 'KC_1969',
    'broncos': 'DEN_1977', 'seahawks': 'SEA_1984', 'eagles': 'PHI_1980', 'cowboys': 'DAL_1977',
    'washington': 'WAS_1982', 'cardinals': 'STL_1975', 'giants': 'NYG_1986', 'vikings': 'MIN_1973',
    'lions': 'DET_1991', 'bears': 'CHI_1985', 'buccaneers': 'TB_1979', 'packers': 'GB_1966',
    'falcons': 'ATL_1980', 'rams': 'LA_1979', '49ers': 'SF_1981', 'saints': 'NO_1991',
}


def jload(*parts):
    return json.load(open(os.path.join(ROOT, *parts), encoding='utf-8'))


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


OUT_OVERRIDE = os.environ.get('ERAS_RUNTIME_OUT') or None


def runtime_dir(season):
    if OUT_OVERRIDE:
        return os.path.abspath(OUT_OVERRIDE)
    return os.path.join(ROOT, 'data', 'eras', 'runtime', str(season))


def membership_path(season):
    return os.path.join(LEAGUE, 'rosters_%d.json' % season)


def season_league(season):
    d = jload('data', 'eras', 'league', 'nfl_seasons_1970_1980.json')
    s = [x for x in d['seasons'] if x['season'] == season]
    if not s:
        raise SystemExit('season %d is not in nfl_seasons_1970_1980.json' % season)
    return s[0]


def identity(events, fid, season):
    f = [x for x in events['franchises'] if x['franchise_id'] == fid][0]
    ids = [i for i in f['identities'] if i['first_season'] <= season <= i['last_season']]
    assert len(ids) == 1, (fid, season)
    return ids[0]


def pick_template(historic, fid, city, season):
    """The template rule (PHASE2-PLAN.md 2.4): same franchise, the season's city first, then the
    nearest season, the earlier one on a tie. Returns (entry, rule note)."""
    cands = [t for t in historic['teams'] if t['id'] not in HISTORIC_EXCLUDE
             and HISTORIC_ALIAS.get(t['franchise'], t['franchise']) == fid]
    if not cands:
        return None, 'no disc historic team for this franchise'
    same_city = [t for t in cands if t['display'] == city]
    pool = same_city or cands
    best = min(pool, key=lambda t: (abs(t['season'] - season), t['season'] > season))
    note = 'city %s' % ('matches' if same_city else 'differs (no entry in %s)' % city)
    nearer = [t for t in cands if t not in pool and abs(t['season'] - season) < abs(best['season'] - season)]
    if nearer:
        note += '; %s is nearer but %s' % (', '.join(t['id'] for t in nearer), nearer[0]['display'])
    return best, note


def home_stadium(stadiums, fid, season):
    """The franchise's home stadium spell for the season (data/eras/league/stadiums_1966_2025.json,
    docs/eras/research/STADIUMS.md): its in-game venue name (resolved by name at run time), exact or
    standin, and the historical building's name."""
    spells = [s for s in stadiums['franchises'][fid]
              if s['first'] <= season <= (s['last'] if s['last'] is not None else 9999)]
    assert len(spells) == 1, (fid, season, [s['stadium'] for s in spells])
    s = spells[0]
    return s['ingame'], s['ingame_match'], s['stadium']


def team_style(looks, fid, season, tpl):
    """M9: the disc uniform style (team +0x192) the franchise wears in the season
    (data/eras/league/team_looks.json, docs/eras/research/ERA-LOGOS.md); a season with no entry keeps
    the template's own style."""
    spans = [s for s in looks['teams'].get(fid, []) if s['from_season'] <= season <= s['to_season']]
    assert len(spans) <= 1, (fid, season, spans)
    style = spans[0]['style'] if spans else tpl['variant']
    assert 0 <= style <= 14, (fid, season, style)
    return style


def depth_key(pos, v):
    if pos in PAIRED:
        return (min(v['depth_rank'], v['depth_side']), v['depth_rank'] > v['depth_side'])
    return (v['depth_rank'], v['depth_side'])


def build(season):
    from mod_editor.core import nfl2k5_roster_records as rr
    league = season_league(season)
    events = jload('data', 'eras', 'league', 'franchise_events_1970_2025.json')
    historic = jload('data', 'eras', 'historic_teams_2k5.json')
    stadiums = jload('data', 'eras', 'league', 'stadiums_1966_2025.json')
    looks = jload('data', 'eras', 'league', 'team_looks.json')
    assert len(historic['teams']) == 75 and [t['entry'] for t in historic['teams']] == list(range(75))
    comm = jload('data', 'eras', 'community_rosters', '%d.json' % season)
    payload_path = os.path.join(ROOT, comm['source']['payload'])
    payload = open(payload_path, 'rb').read()
    digest = hashlib.sha256(payload).hexdigest()
    assert digest == comm['source']['sha256'], 'the roster payload differs from the one extracted'
    doc = rr.RosterDocument(payload, base=rr.find_block_base(payload), source=comm['source']['payload'],
                            reference_year=season)
    positions = rr.POSITIONS
    assert len(positions) == 17
    lin_path = os.path.join(LEAGUE, 'lineage_check_%d.json' % season)
    lineage = {s['slot']: s for s in jload(lin_path)['slots']} if os.path.exists(lin_path) else {}

    slot_of = {}
    for t in doc.teams[:32]:
        assert t.abbreviation not in slot_of, t.abbreviation
        slot_of[t.abbreviation] = t.index
    comm_rows = defaultdict(list)
    for p in comm['players']:
        if p['team'] is not None:
            comm_rows[p['team']].append(p)

    st_path = os.path.join(LEAGUE, 'starters_%d.json' % season)
    starters_doc = jload(st_path) if os.path.exists(st_path) else None
    starters = starters_doc['teams'] if starters_doc else None
    all_pfr = defaultdict(list)          # normalized name -> [(team, PFR roster entry)], for mismatches
    for tid, t in (starters or {}).items():
        for p in t['roster']:
            all_pfr[norm_name(p['name'])].append((tid, p))

    out_dir = runtime_dir(season)
    os.makedirs(os.path.join(out_dir, 'rosters'), exist_ok=True)
    mem = jload(membership_path(season)) if os.path.exists(membership_path(season)) else None
    docs = SaveDocs(rr, mem, season, doc, digest) if mem else None
    teams, report = [], []
    for conf in league['conferences']:
        for div in conf['divisions']:
            # alphabetical within the division: teams.csv must not carry the real final order
            for lt in sorted(div['teams'], key=lambda t: (t['city'], t['name'])):
                fid = lt['id']
                ident = identity(events, fid, season)
                assert (ident['city'], ident['name']) == (lt['city'], lt['name']), (fid, ident)
                tpl, tnote = pick_template(historic, fid, lt['city'], season)
                assert tpl is not None, (fid, tnote)
                slot = slot_of[SAVE_ABBR_2004[fid]]
                st = doc.teams[slot]
                if (st.city, st.nickname) == (lt['city'], lt['name']):
                    status = 'direct'
                elif slot in lineage and lineage[slot]['franchise'] == fid:
                    status = lineage[slot]['lineage_status']
                else:
                    status = 'inferred'
                if mem:
                    rows, flags, ents = membership_rows(rr, docs, mem['teams'][fid], season)
                    MEMBERSHIP_FLAGS[fid] = flags
                    plan = write_depth_membership(out_dir, fid, rows, ents, positions, starters[fid], season)
                    depth_bad, groups, seed_changes = [], len({r[2] for r in rows}), []
                else:
                    rows, flags, depth_bad, groups = roster_rows(rr, doc, slot, comm_rows[slot], positions)
                    write_roster(out_dir, fid, rows, flags, season, comm['source'], st)
                    seed_changes, plan = write_depth_seed(out_dir, fid, doc, slot, rows, positions,
                                                          starters.get(fid) if starters else None, all_pfr)
                venue, venue_match, venue_hist = home_stadium(stadiums, fid, season)
                teams.append({'id': fid, 'city': lt['city'], 'name': lt['name'], 'abbr': lt['abbreviation'],
                              'conference': conf['name'], 'division': div['name'],
                              'template_entry': tpl['entry'], 'template_id': tpl['id'],
                              'roster_file': 'rosters/%s.csv' % fid, 'community_slot': slot,
                              'lineage_status': status, 'stadium': venue, 'stadium_match': venue_match,
                              'stadium_historical': venue_hist,
                              'style': team_style(looks, fid, season, tpl)})
                report.append({'id': fid, 'slot': slot, 'save_name': '%s %s (%s)' % (st.city, st.nickname, st.abbreviation),
                               'template': tpl, 'template_note': tnote, 'gap': tpl['season'] - season,
                               'flags': len(flags), 'depth_bad': depth_bad, 'groups': groups,
                               'seed_changes': seed_changes, 'starters': plan,
                               'clamped': sum(1 for f in flags if 'written as 99' in f[1])})
    with open(os.path.join(out_dir, 'teams.csv'), 'w', encoding='ascii', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(TEAMS_HEADER)
        for t in teams:
            w.writerow([t[k] for k in TEAMS_HEADER])
    coaches = write_coaches(out_dir, season, teams)
    write_provenance(out_dir, season, league, comm, digest, payload_path, teams, report, lineage, coaches,
                     starters_doc, mem, docs)
    unused = sorted(set(range(32)) - {t['community_slot'] for t in teams})
    print('wrote %s: %d teams, %d roster files; unused save slots %s' % (
        os.path.relpath(out_dir, ROOT), len(teams), len(teams),
        ', '.join('%d %s' % (i, doc.teams[i].abbreviation) for i in unused)))


def roster_rows(rr, doc, slot, comm_players, positions):
    """53 roster.csv rows in the save's list order, plus [(row, flag)] and depth-order misfits."""
    players = doc.team_players(slot)
    comm_players = sorted(comm_players, key=lambda p: p['depth_slot'])
    assert len(players) == len(comm_players) == 53, (slot, len(players), len(comm_players))
    rows, flags = [], []
    last_key = {}
    depth_bad = []
    for i, (pl, cp) in enumerate(zip(players, comm_players)):
        r, v = pl.record, pl.record.values
        assert (pl.first, pl.last) == (cp['first'], cp['last']) and cp['depth_slot'] == i, (slot, i)
        pos = v['position']
        assert 0 <= pos <= 16, (slot, i, pos)
        pname = positions[pos]
        for part in (pl.first, pl.last):
            # the game-side reader splits on commas only and widens each byte to UTF-16
            assert part and not re.search(r'[,"\r\n]', part) and len(part) <= NAME_MAX, (slot, i, part)
            part.encode('latin-1')
        mine = list(cp['flags'])
        ratings = []
        for rn in rr.RATING_BYTE_ORDER:
            val = v[rn]
            if val > RATING_MAX:
                mine.append('%s stored as %d, written as 99 (the roster writer clamps to 0..99)' % (rn, val))
                val = RATING_MAX
            ratings.append(val)
        if v['jersey'] > 99:
            mine.append('jersey %d above 99' % v['jersey'])
        if r.skin > 21:
            mine.append('skin %d outside the 22 named skins' % r.skin)
        if v['face'] > 14:
            mine.append('face %d outside the 15 named faces' % v['face'])
        k = depth_key(pname, v)
        if pname in last_key and k < last_key[pname]:
            depth_bad.append(pname)
            mine.append('list order disagrees with the stored depth fields (rank %d, side %d)'
                        % (v['depth_rank'], v['depth_side']))
        last_key[pname] = k
        rows.append([pl.first, pl.last, pos, v['jersey'], v['height'], r.weight, v['years_pro'],
                     r.skin, v['face'], v['hand'], *ratings, NO_PHOTO])
        label = '%s %s (%s #%d)' % (pl.first, pl.last, pname, v['jersey'])
        flags.extend((i, text, label) for text in mine)
    return rows, flags, depth_bad, len(last_key)


def write_roster(out_dir, fid, rows, flags, season, src, st):
    from mod_editor.core import nfl2k5_roster_records as rr
    path = os.path.join(out_dir, 'rosters', '%s.csv' % fid)
    with open(path, 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(HEADER + list(rr.RATING_BYTE_ORDER) + ['photo'])
        w.writerows(rows)
    with open(os.path.join(out_dir, 'rosters', '%s.flags.txt' % fid), 'w', encoding='latin-1', newline='\n') as f:
        f.write('%s.csv: %s, save %s slot %d (%s %s, %s), sha256 %s\n' % (
            fid, SOURCE_LABEL, src['stem'], st.index, st.city, st.nickname, st.abbreviation, src['sha256']))
        f.write('Rows are numbered from 0 in file order (the header is not a row). Flagged values are written '
                'as stored, except ratings above 99 (written as 99).\n')
        if not flags:
            f.write('no flags\n')
        for row, text, name in flags:
            f.write('row %d: %s: %s\n' % (row, name, text))


DEPTH_HEADER = ['row', 'first', 'last', 'pos', 'number', 'rank', 'side']
SIDE_FOR_RANK = {0: 2, 1: 0, 2: 1}


def norm_name(name):
    """Names compared without case, accents, punctuation or spaces (lineage_check.py's rule)."""
    s = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z]', '', s)


def edit_distance(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def match_pfr(first, last, roster, number=None):
    """The PFR roster entry for a community player, and how it was matched: 'exact' (normalized
    full name), 'spelling' (normalized full names within edit distance 2, last names within 1,
    and only one such entry), 'nickname' (same last name, first names in one NICKNAMES set, only
    one such entry) or 'initial' (same last name, first initial and jersey number, only one such
    entry). (None, None) when no entry qualifies; nothing else is guessed."""
    full = norm_name(first + last)
    ex = [p for p in roster if norm_name(p['name']) == full]
    if len(ex) == 1:
        return ex[0], 'exact'
    nl = norm_name(last)

    def pfr_last(p):
        return norm_name(p['name'].split(' ', 1)[1] if ' ' in p['name'] else p['name'])
    sp = [p for p in roster if edit_distance(norm_name(p['name']), full) <= 2 and edit_distance(pfr_last(p), nl) <= 1]
    if len(sp) == 1:
        return sp[0], 'spelling'
    nf = norm_name(first)
    nick = [p for p in roster if pfr_last(p) == nl and any(
        nf in g and norm_name(p['name'].split(' ', 1)[0]) in g for g in NICKNAMES)]
    if len(nick) == 1:
        return nick[0], 'nickname'
    ini = [p for p in roster if pfr_last(p) == nl and norm_name(p['name'])[:1] == nf[:1]
           and number is not None and p['number'] == str(number)]
    if len(ini) == 1:
        return ini[0], 'initial'
    return None, None


# Common English short forms of first names (match_pfr 'nickname': same last name, first names in one set).
NICKNAMES = [set(g.split()) for g in (
    'robert bob bobby rob', 'william bill billy will willie', 'james jim jimmy jimmie', 'michael mike',
    'thomas tom tommy', 'richard dick rick ricky rich richie', 'joseph joe joey', 'david dave',
    'edward ed eddie', 'kenneth ken kenny', 'anthony tony', 'charles chuck charlie', 'lawrence larry',
    'steven stephen steve', 'daniel dan danny', 'gerald jerry', 'ronald ron ronnie', 'donald don donnie',
    'patrick pat', 'arthur art', 'frederick fred freddie', 'theodore ted', 'herman herm',
    'matthew matt', 'douglas doug', 'gregory greg', 'raymond ray', 'johnny john jon', 'leonard len lenny',
    'samuel sam', 'timothy tim', 'andrew andy', 'benjamin ben', 'nicholas nick', 'albert al',
    'alan al', 'alfred al', 'eugene gene', 'harold hal', 'walter walt', 'stanley stan', 'vincent vince',
    'jeffrey jeff', 'gary garry', 'terrence terry terence', 'clarence clancy', 'delvin del')]


def starters_plan(fid, rows, groups, L, R, positions, st, all_pfr):
    """Apply the verified starters (data/eras/league/starters_<season>.json) to the community
    order: per position code the player with the most <season> games started (K: most FGA,
    P: most punts) goes first, the rest keep their order; for the paired positions the two
    leaders head the left and right chains. Changes L and R in place; returns the report."""
    roster = st['roster']
    fga = {k['pfr_id']: k['fga'] for k in st['kicking']}
    punts = {p['pfr_id']: p['punts'] for p in st['punting']}
    match, how = {}, {}
    for i, r in enumerate(rows):
        p, m = match_pfr(r[0], r[1], roster, r[3])
        if p:
            match[i], how[i] = p, m

    def name(i):
        return '%s %s' % (rows[i][0], rows[i][1])

    def key(i):
        p = match.get(i)
        if not p:
            return 0
        pc = positions[rows[i][2]]
        return fga.get(p['pfr_id'], 0) if pc == 'K' else punts.get(p['pfr_id'], 0) if pc == 'P' else p['gs']

    def unit(pc):
        return 'FGA' if pc == 'K' else 'punts' if pc == 'P' else 'GS'

    def desc(i):
        p = match.get(i)
        return '%s (%s)' % (name(i), ('%d %s' % (key(i), unit(positions[rows[i][2]]))) if p else 'not on the PFR roster')
    changes = []
    for pos, idx in sorted(groups.items()):
        pc = positions[pos]
        left = L[pos]
        order = {i: n for n, i in enumerate(left)}
        lead = sorted((i for i in idx if key(i) > 0), key=lambda i: (-key(i), order[i]))
        if pc in PAIRED and len(idx) > 1:
            right = R[pos]
            want = lead[:2]
            if len(want) == 2:
                a, b = want
                keep_ab = (left[0] == a) + (right[0] == b)
                keep_ba = (left[0] == b) + (right[0] == a)
                nl, nr = (a, b) if keep_ab >= keep_ba else (b, a)
            elif len(want) == 1:
                nl, nr = (left[0], want[0]) if right[0] == want[0] else (want[0], right[0] if right[0] != want[0] else None)
            else:
                continue
            for chain, new, label in ((left, nl, ' left'), (right, nr, ' right')):
                if new is None or chain[0] == new:
                    continue
                old = chain[0]
                chain.remove(new)
                chain.insert(0, new)
                changes.append('%s%s: %s first, was %s' % (pc, label, desc(new), desc(old)))
        else:
            if not lead or left[0] == lead[0]:
                continue
            old = left[0]
            left.remove(lead[0])
            left.insert(0, lead[0])
            changes.append('%s: %s first, was %s' % (pc, desc(lead[0]), desc(old)))
    # roster membership
    matched_ids = {p['pfr_id'] for p in match.values()}
    not_on = []
    for i, r in enumerate(rows):
        if i in match:
            continue
        full = norm_name(r[0] + r[1])
        elsewhere = ['%s (%d GS)' % (t, p['gs']) for t, p in all_pfr.get(full, []) if t != fid]
        not_on.append('%s %s%s' % (name(i), positions[r[2]], (' [1980: %s]' % ', '.join(elsewhere)) if elsewhere else ''))
    fuzzy = ['%s = PFR %s (%s)' % (name(i), match[i]['name'], how[i]) for i in sorted(match) if how[i] != 'exact']
    missing = []
    for s in st['starters']:
        if s['pfr_id'] not in matched_ids:
            missing.append('%s %s (%d GS)' % (s['pos'], s['name'], next((p['gs'] for p in roster if p['pfr_id'] == s['pfr_id']), 0)))
    for lst, pc, u in ((st['kicking'], 'K', 'fga'), (st['punting'], 'P', 'punts')):
        top = max(lst, key=lambda x: x[u], default=None)
        if top and top[u] > 0 and top['pfr_id'] not in matched_ids:
            missing.append('%s %s (%d %s)' % (pc, top['name'], top[u], unit(pc)))
    return {'changes': changes, 'not_on_pfr': not_on, 'fuzzy': fuzzy, 'missing_starters': missing,
            'matched': len(match), 'sources': st['sources'], 'checks': st['checks']}


def write_depth_seed(out_dir, fid, doc, slot, rows, positions, starters=None, all_pfr=None):
    """depth/<id>.csv from the stored depth fields (PROVENANCE 'DEPTH CHART SEED'), then the
    verified starters when starters_<season>.json has the team. Returns the starters that differ
    from list order (text) and the starters report (None without the file)."""
    players = doc.team_players(slot)
    groups = defaultdict(list)
    for i, pl in enumerate(players):
        groups[pl.record.values['position']].append(i)
    rank, side = {}, {}
    changes = []
    L, R = {}, {}

    def name(i):
        return '%s %s' % (rows[i][0], rows[i][1])
    for pos, idx in sorted(groups.items()):
        v = [players[i].record.values for i in idx]
        left = sorted(range(len(idx)), key=lambda k: (v[k]['depth_rank'], k))
        L[pos] = [idx[k] for k in left]
        paired = positions[pos] in PAIRED
        if paired:
            right = sorted(range(len(idx)), key=lambda k: (v[k]['depth_side'], k))
            R[pos] = [idx[k] for k in right]
        # list order (the roster writer): rank = order in the group, side = SIDE_FOR_RANK
        if left[0] != 0:
            changes.append('%s%s: %s (stored) instead of %s (list order)' % (
                positions[pos], ' left' if paired else '', name(idx[left[0]]), name(idx[0])))
        if paired and len(idx) > 1:
            r0 = R[pos][0]
            if r0 != idx[1]:
                changes.append('%s right: %s (stored) instead of %s (list order)' % (positions[pos], name(r0), name(idx[1])))
    plan = starters_plan(fid, rows, groups, L, R, positions, starters, all_pfr) if starters else None
    for pos, idx in groups.items():
        for n, i in enumerate(L[pos]):
            rank[i] = min(n, 7)
        if pos in R:
            for n, i in enumerate(R[pos]):
                side[i] = min(n, 7)
        else:
            for i in idx:
                side[i] = min(SIDE_FOR_RANK.get(rank[i], rank[i]), 7)
    os.makedirs(os.path.join(out_dir, 'depth'), exist_ok=True)
    with open(os.path.join(out_dir, 'depth', '%s.csv' % fid), 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(DEPTH_HEADER)
        for i, r in enumerate(rows):
            w.writerow([i, r[0], r[1], r[2], r[3], rank[i], side[i]])
    return changes, plan


# ---------------------------------------------------------------------------
# membership mode (data/eras/league/rosters_<season>.json, tools/eras/roster_membership.py)

class SaveDocs:
    """RosterDocument per community save season, each payload sha256-checked against the
    membership file; record(season, index) is the primary-pool player record."""

    def __init__(self, rr, mem, season, base_doc, base_digest):
        self.rr, self.mem, self.docs, self.by_index = rr, mem, {season: base_doc}, {}
        assert mem['saves'][str(season)]['payload_sha256'] == base_digest
        self.used = Counter()

    def doc(self, y):
        if y not in self.docs:
            info = self.mem['saves'][str(y)]
            path = os.path.join(ROOT, info['payload'])
            payload = open(path, 'rb').read()
            assert hashlib.sha256(payload).hexdigest() == info['payload_sha256'], (y, 'payload changed')
            assert sha256(os.path.join(ROOT, info['file'])) == info['sha256'], (y, 'community json changed')
            self.docs[y] = self.rr.RosterDocument(payload, base=self.rr.find_block_base(payload),
                                                  source=info['payload'], reference_year=y)
        return self.docs[y]

    def record(self, y, idx):
        if y not in self.by_index:
            self.by_index[y] = {p.index: p for p in self.doc(y).players if p.pool == 'primary'}
        self.used[y] += 1
        return self.by_index[y][idx]


def positions_of(rr):
    return rr.POSITIONS


def safe_name(part):
    return bool(part) and not re.search(r'[,"\r\n]', part) and len(part) <= NAME_MAX and _latin1(part)


def _latin1(x):
    try:
        x.encode('latin-1')
        return True
    except UnicodeEncodeError:
        return False


def written_name(e):
    """(first, last, note): the save's names, or PFR's spelling where the save misspells him."""
    src = e['source']
    first, last = src['first'], src['last']
    m = src.get('match') or {}
    if 'pfr_id' in e and m.get('kind') in ('spelling', 'initial'):
        w = e['name'].split(' ', 1)
        if len(w) == 2 and safe_name(w[0]) and safe_name(w[1]):
            return w[0], w[1], 'name written as PFR spells it (save: %s %s)' % (first, last)
    return first, last, None


def entry_key(e, positions_name):
    """The depth key of one membership entry at its game position: (key, games, starts)."""
    if 'pfr_id' not in e:
        return (0, 0, 0)
    import roster_membership as rm
    pc = positions_name
    if pc == 'K':
        return (e['fga'], e['g'], e['gs'])
    if pc == 'P':
        return (e['punts'], e['g'], e['gs'])
    gs = e['gs'] if rm.group_ok(e['pfr_pos'], pc) else 0
    return (gs, e['g'], e['gs'])


def membership_rows(rr, docs, tm, season):
    """42..53 rows (unsorted), [(entry index, flag)], and the entries, in the same order."""
    ents = list(tm['players']) + list(tm['fillers'])
    assert ROWS_MIN <= len(ents) <= ROWS_MAX, len(ents)
    rows, flags = [], []
    for i, e in enumerate(ents):
        src = e['source']
        pl = docs.record(src['season'], src['record_index'])
        r, v = pl.record, pl.record.values
        assert (pl.first, pl.last) == (src['first'], src['last']) and v['position'] == src['position_code'], (src, pl.first, pl.last)
        assert rr.POSITIONS[e['position_code']] == e['position']
        first, last, note = written_name(e)
        assert safe_name(first) and safe_name(last), (first, last)
        mine = []
        real = 'pfr_id' in e
        if real:
            if src['season'] != season:
                mine.append('ratings from the %d save (%s); not in the %d save' % (
                    src['season'], src['club'] or 'free agent', season))
            elif src['club_slot'] != tm['community_slot']:
                mine.append('moved: the %d save has him %s' % (season, ('in its %s slot' % src['club']) if src['club'] else 'as a free agent'))
            m = src['match']
            if m['kind'] != 'exact':
                mine.append('matched to PFR %s by %s' % (e['name'], m['kind']))
            if m['college'] == 'differs':
                mine.append("college differs: PFR %s, save %s (identity from %s)" % (
                    e['pfr'].get('college'), src['college'], 'jersey/club slot' if m['strength'] == 'weak' else m['strength']))
            if m['strength'] == 'posdiff':
                mine.append('position: the save has him at %s, PFR %d at %s; his GS do not count at %s'
                            % (src['position'], season, e['pfr_pos'] or '?', src['position']))
            if m['strength'] == 'alias':
                mine.append('other first name: PFR %s, save %s %s (same last name, college, jersey and club)' % (
                    e['name'], src['first'], src['last']))
        else:
            mine.append('FILLER: on no PFR %d roster; kept from the %d save\'s %s slot (%s)%s' % (
                season, season, src['club'], e.get('picked', 'filler'),
                '' if e.get('active_next_save') else ' (not in the next save either)'))
        if note:
            mine.append(note)
        mine.extend('save flag: %s' % f for f in src.get('flags', []))
        ratings = []
        for rn in rr.RATING_BYTE_ORDER:
            val = v[rn]
            if val > RATING_MAX:
                mine.append('%s stored as %d, written as 99 (the roster writer clamps to 0..99)' % (rn, val))
                val = RATING_MAX
            ratings.append(val)
        number = v['jersey']
        if real and re.fullmatch(r'\d{1,2}', e['number'] or ''):
            if int(e['number']) != number:
                mine.append('jersey %d (PFR %d) instead of the save\'s %d' % (int(e['number']), season, number))
            number = int(e['number'])
        elif number > 99:
            mine.append('jersey %d above 99' % number)
        years = v['years_pro']
        exp = (e.get('pfr') or {}).get('experience') if real else None
        if exp is not None:
            years = exp
        elif src['season'] != season:
            years = max(0, years - (src['season'] - season))
        years = min(years, 31)
        if r.skin > 21:
            mine.append('skin %d outside the 22 named skins' % r.skin)
        if v['face'] > 14:
            mine.append('face %d outside the 15 named faces' % v['face'])
        if e['position_code'] != v['position']:
            mine.append('position %s (PFR %d %s); the save codes him %s' % (
                positions_of(rr)[e['position_code']], season, e['pfr_pos'], positions_of(rr)[v['position']]))
        rows.append([first, last, e['position_code'], number, v['height'], r.weight, years,
                     r.skin, v['face'], v['hand'], *ratings, NO_PHOTO])
        flags.extend((i, t) for t in mine)
    return rows, flags, ents


def order_membership(rows, ents, positions, starters):
    """Row order and depth (rank, side) per row: see the module docstring, MEMBERSHIP MODE."""
    side_of = {}
    for s_ in starters['starters']:
        pos = s_['pos'].upper()
        if pos[:1] in 'LR' and len(pos) > 1:
            side_of[s_['pfr_id']] = pos[0]
    groups = defaultdict(list)
    for i, r in enumerate(rows):
        groups[r[2]].append(i)
    rank, side, chains = {}, {}, {}
    for pos, idx in groups.items():
        pc = positions[pos]
        order = sorted(idx, key=lambda i: tuple(-x for x in entry_key(ents[i], pc)) + (rows[i][1], rows[i][0]))
        left = list(order)
        if pc in PAIRED and len(order) > 1:
            a, b = order[0], order[1]
            if side_of.get(ents[a].get('pfr_id')) == 'R' and side_of.get(ents[b].get('pfr_id')) == 'L':
                a, b = b, a
            left = [a, b] + order[2:]
            right = [b, a] + order[2:]
            for n, i in enumerate(right):
                side[i] = min(n, 7)
        for n, i in enumerate(left):
            rank[i] = min(n, 7)
            if not (pc in PAIRED and len(order) > 1):
                side[i] = min(SIDE_FOR_RANK.get(rank[i], rank[i]), 7)
        chains[pos] = left
    row_order = [i for pos in sorted(chains) for i in chains[pos]]
    return row_order, rank, side


def write_depth_membership(out_dir, fid, rows, ents, positions, starters, season):
    from mod_editor.core import nfl2k5_roster_records as rr
    order, rank, side = order_membership(rows, ents, positions, starters)
    new_index = {old: n for n, old in enumerate(order)}
    out_rows = [rows[i] for i in order]
    names = Counter((r[0], r[1]) for r in out_rows)
    dup = [n for n, c in names.items() if c > 1]
    assert not dup, (fid, 'two rows with one name: the depth file matches rows by name', dup)
    with open(os.path.join(out_dir, 'rosters', '%s.csv' % fid), 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(HEADER + list(rr.RATING_BYTE_ORDER) + ['photo'])
        w.writerows(out_rows)
    os.makedirs(os.path.join(out_dir, 'depth'), exist_ok=True)
    with open(os.path.join(out_dir, 'depth', '%s.csv' % fid), 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(DEPTH_HEADER)
        for n, i in enumerate(order):
            r = rows[i]
            w.writerow([n, r[0], r[1], r[2], r[3], rank[i], side[i]])
    flags = MEMBERSHIP_FLAGS.pop(fid)
    with open(os.path.join(out_dir, 'rosters', '%s.flags.txt' % fid), 'w', encoding='latin-1', newline='\n') as f:
        f.write('%s.csv: real %d roster (PFR, %s) rated from community PS2 saves (unverified ratings); '
                'data/eras/league/rosters_%d.json\n' % (fid, season, starters['sources']['roster'], season))
        f.write('Rows are numbered from 0 in file order (the header is not a row). Ratings above 99 are written '
                'as 99; everything else as the save record has it, except the jersey and years_pro (PFR).\n')
        by_row = defaultdict(list)
        for i, t in flags:
            by_row[new_index[i]].append(t)
        if not by_row:
            f.write('no flags\n')
        for n in sorted(by_row):
            r = out_rows[n]
            label = '%s %s (%s #%d)' % (r[0], r[1], positions[r[2]], r[3])
            for t in by_row[n]:
                f.write(('row %d: %s: %s\n' % (n, label, t)).encode('latin-1', 'replace').decode('latin-1'))
    # starters report (PROVENANCE): heads of each chain, real starters absent
    present = {e.get('pfr_id') for e in ents}
    heads = {}
    for i in order:
        pc = positions[rows[i][2]]
        if rank[i] == 0:
            heads.setdefault(pc, []).append('%s %s' % (rows[i][0], rows[i][1]))
        elif pc in PAIRED and side[i] == 0:
            heads.setdefault(pc, []).append('%s %s' % (rows[i][0], rows[i][1]))
    missing = [s_ for s_ in starters['starters'] if s_['pfr_id'] not in present]
    return {'heads': heads, 'missing': missing, 'order': order, 'rank': rank, 'side': side}


MEMBERSHIP_FLAGS = {}


def write_coaches(out_dir, season, teams):
    p = os.path.join(LEAGUE, 'head_coaches_%d.json' % season)
    if not os.path.exists(p):
        return None
    d = json.load(open(p, encoding='utf-8'))
    ids = {t['id'] for t in teams}
    rows = sorted(d['coaches'], key=lambda c: (c['team'], c['first_game']))
    assert {c['team'] for c in rows} == ids, 'head coaches: teams differ from teams.csv'
    for fid in ids:
        mine = [c for c in rows if c['team'] == fid]
        assert mine[0]['first_game'] == 1 and mine[-1]['last_game'] == 16, fid
        assert all(a['last_game'] + 1 == b['first_game'] for a, b in zip(mine, mine[1:])), fid
        for c in mine:
            assert c['first'] and c['last'] and not any(ch in c['first'] + c['last'] for ch in ',"\r\n'), c
    with open(os.path.join(out_dir, 'coaches.csv'), 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(['id', 'first', 'last', 'first_game', 'last_game'])
        for c in rows:
            w.writerow([c['team'], c['first'], c['last'], c['first_game'], c['last_game']])
    return d


def write_provenance(out_dir, season, league, comm, digest, payload_path, teams, report, lineage, coaches=None,
                     starters_doc=None, mem=None, docs=None):
    sched_path = os.path.join(LEAGUE, 'nfl_schedule_%d.json' % season)
    L = []
    add = L.append
    add('My NFL Eras runtime package, season %d' % season)
    add('Generated by tools/eras/make_runtime_package.py --season %d (teams, rosters, this file) and' % season)
    add('tools/eras/league_schedule.py --season %d (schedule.csv). Regenerate rather than edit, except' % season)
    add('teams.csv template_entry/template_id, which the user may change per franchise.')
    add('')
    add('LEAGUE DATA (verified)')
    add('  Teams, conferences, divisions, city and name: data/eras/league/nfl_seasons_1970_1980.json,')
    add('  verified against two sets of PFR Wayback snapshots and the NFL Record & Fact Book (2012),')
    add('  "Past Standings" (docs/eras/research/LEAGUE-HISTORY.md). Sources for %d:' % season)
    for u in league['sources']:
        add('    ' + u)
    add('    https://web.archive.org/web/20130925182142/http://static.nfl.com/static/content/public/image/history/pdfs/History/Past_Standings_2011.pdf')
    add('  City per season (template rule): data/eras/league/franchise_events_1970_2025.json identities.')
    add('  Abbreviations are a project display convention, not history.')
    add('  teams.csv lists each division alphabetically, not in the real final order.')
    add('')
    add('SCHEDULE')
    if os.path.exists(sched_path):
        s = json.load(open(sched_path, encoding='utf-8'))
        add('  schedule.csv: the historical %d regular season (historical=true), %d games,' % (season, len(s['games'])))
        add('  from data/eras/league/nfl_schedule_%d.json (docs/eras/research/SCHEDULE-%d.md). No scores' % (season, season))
        add('  are in schedule.csv. Checks passed: ' + ', '.join(k for k, v in s['checks'].items() if v is True) + '.')
        wk = s['checks'].get('wikipedia_cross_check')
        if wk:
            add('  Wikipedia team-season articles (a second reading, not independent of PFR): %d of %d'
                % (wk['agree'], wk['team_games']))
            add('  team-games agree on date, opponent, home/away, result and score; %d disagree.' % wk['disagree'])
        add('  Sources:')
        for e in s['sources']:
            add('    %s: %s' % (e['role'], e['wayback']))
    else:
        add('  not built (run tools/eras/league_schedule.py)')
    add('')
    if not mem:
        add('ROSTERS: %s' % SOURCE_LABEL.upper())
        src = comm['source']
        add('  Every name, number, rating and body in rosters/*.csv is the save author\'s. None of it has been')
        add('  checked against history. Show it in game as "unverified community roster".')
        add('  Save: stem %s, directory %s, %s' % (src['stem'], src['save_directory'], src['integrity']))
        add('  Payload: %s' % src['payload'])
        add('  sha256 %s' % digest)
        add('  Parser: 2K Football Mod Tools RosterDocument (.tmp-2k-football-mod-tools/mod_editor/core/'
            'nfl2k5_roster_records.py),')
        add('  positions as stored (0 QB .. 16 DE), ratings in RATING_BYTE_ORDER (record bytes +0x36..+0x51).')
        add('  Flags per row (duplicates, cloned bodies, implausible bodies, shared names) are the extractor\'s')
        add('  (data/eras/community_rosters/%d.json, docs/eras/research/COMMUNITY-ROSTERS.md); they are listed' % season)
        add('  in rosters/<id>.flags.txt and nothing was "fixed". Ratings above 99 are written as 99 (the game-')
        add('  side writer clamps to 0..99 anyway). Birth dates are not written: this save\'s birth-year')
        add('  convention is "%s", so ages are unreliable. photo = %d (no photo).' % (
            comm['quality']['birth_convention']['estimate'], NO_PHOTO))
        bad = sum(len(r['depth_bad']) for r in report)
        groups = sum(r['groups'] for r in report)
        bad_groups = sum(len(set(r['depth_bad'])) for r in report)
        add('  Row order: the save\'s team-list order; the game-side writer ranks depth within each position')
        add('  by row order. The depth fields stored in the records (rank/side; left and right chains for T, G,')
        add('  DE, DT, OLB, ILB, CB, WR) follow the same order in %d of %d position groups. In the other %d,'
            % (groups - bad_groups, groups, bad_groups))
        add('  %d rows in %d slots carry stored values that would rank them above rows listed before them (the'
            % (bad, sum(1 for r in report if r['depth_bad'])))
        add('  stored values there are not a consistent chain, e.g. a group ranked 1, 2, 0). List order is kept;')
        add('  each such row is named in its flags file. How the game itself orders them is not tested (T2.1).')
        add('  Slot lineage (lineage_status in teams.csv): direct = the save slot carries this franchise\'s own')
        add('  %d city and nickname; checked:<url> = D3 found the slot\'s names on the franchise\'s sourced' % season)
        add('  %d roster (tools/eras/lineage_check.py); inferred = assumed from franchise lineage only.' % season)
        for r in report:
            t = [x for x in teams if x['id'] == r['id']][0]
            if t['lineage_status'] != 'direct':
                ln = lineage.get(r['slot'])
                extra = (' (%d of %d names on the PFR roster)' % (ln['matched'], ln['compared'])) if ln else ''
                add('    %-14s slot %2d %s: %s%s' % (r['id'], r['slot'], r['save_name'], t['lineage_status'], extra))
        add('')
    if mem:
        provenance_membership(add, season, mem, docs, teams, report, comm, digest)
    add('TEMPLATES (disc historic teams, data/eras/historic_teams_2k5.json)')
    add('  Rule (PHASE2-PLAN.md 2.4): entries of the same franchise (oilers/titans -> oilers_titans,')
    add('  redskins -> washington; DAL_1962 "texans" excluded, identity unchecked); prefer the franchise\'s')
    add('  %d city; then the nearest season, the earlier on a tie. The template gives uniform variant,' % season)
    add('  helmet/uniform asset code and the 53 player records: the roster writer overwrites the first rows')
    add('  and the staging removes the rest (M8: a team keeps only its CSV rows, 42..53); city, name and')
    add('  abbreviation come from the league data. No claim that a template uniform looks like the %d one.' % season)
    for r in report:
        tpl = r['template']
        add('    %-14s %-9s entry %2d  variant %d  gap %+d  (%s)' % (
            r['id'], tpl['id'], tpl['entry'], tpl['variant'], r['gap'], r['template_note']))
    add('')
    add('HOME STADIUMS (teams.csv stadium, stadium_match, stadium_historical)')
    add('  data/eras/league/stadiums_1966_2025.json (docs/eras/research/STADIUMS.md): the home stadium of each')
    add('  franchise in %d and the retail NFL 2K5 venue an Eras home game is played in. exact = the same' % season)
    add('  building is in the game (as it looked in 2004); standin = the closest retail venue by roof,')
    add('  surface and setting. The game resolves the venue by its name at run time (src/nfl2k5_eras_launch.c).')
    for t in teams:
        add('    %-14s %-42s -> %s (%s)' % (t['id'], t['stadium_historical'][:42], t['stadium'], t['stadium_match']))
    add('')
    if not mem:
        add('DEPTH CHART SEED (community roster, unverified)')
        add('  depth/<id>.csv: the default depth chart of each team, from the depth fields stored in the')
        add("  community save's player records (rank = bits 10-12, side = bits 13-15 of +0x28; for T, G, DE,")
        add('  DT, OLB, ILB, CB and WR rank is the left chain and side the right chain). Within each position')
        add('  code the players are put in the order of their stored rank (ties: list order), the paired')
        add("  positions' right chain in the order of their stored side, and each order is renumbered 0, 1, 2")
        add("  .. (capped at 7, the game's field width). Nothing is added or invented: a stored order is only")
        add("  renumbered. Other positions get side from rank as the game's auto depth chart does (0->2,")
        add('  1->0, 2->1). The game applies depth/<id>.csv when it stages the team (src/nfl2k5_eras_depth.c);')
        add("  a league's own edits (Depth Chart in the office) go to <league>\\depth\\<id>.csv, never here.")
        nch = sum(len(r['seed_changes']) for r in report)
        add('  Starters that differ from list order (the M2 rule before this seed): %d' % nch)
        for r in report:
            for c in r['seed_changes']:
                add('    %-14s %s' % (r['id'], c))
        add('')
        if starters_doc:
            st_rel = 'data/eras/league/starters_%d.json' % season
            add('DEPTH CHART STARTERS (verified, %s)' % st_rel)
            add('  The seed order above is then corrected with the real %d starters: per position code the' % season)
            add('  player with the most %d games started (PFR roster table, GS) goes first, and everyone else' % season)
            add('  keeps the seed order. K goes by most field goals attempted and P by most punts (PFR team')
            add('  page kicking and punting tables). For T, G, DE, DT, OLB, ILB, CB and WR the two GS leaders head')
            add('  the left and right chains (whichever assignment keeps more of the seed\'s two starters). Ties')
            add('  keep the seed order; a position where no player of the community team has a %d start keeps' % season)
            add('  the seed order. Players are matched to PFR by name (case, accents, punctuation and spaces')
            add('  ignored; "spelling" = within two letters; "nickname" = same last name, first names a common')
            add('  short form of each other (Bob/Robert); "initial" = same last name, first initial and jersey')
            add('  number; each must be unique on that roster, and all three kinds are listed per team). Nobody is moved')
            add('  between teams: community players not on the team\'s PFR %d roster are listed (with any other' % season)
            add('  %d team whose PFR roster has the name), and real starters the community team lacks are listed;' % season)
            add('  the rosters stay as the save has them. Built by tools/eras/league_starters.py from Wayback copies')
            add('  of PFR; a second Wayback copy of each roster page (older where one has GS) is parsed separately')
            add('  and its GS compared.')
            tot = sum(len(r['starters']['changes']) for r in report if r['starters'])
            add('  Starters changed from the seed order: %d (in %d teams); community players not on the PFR roster: %d;'
                % (tot, sum(1 for r in report if r['starters'] and r['starters']['changes']),
                   sum(len(r['starters']['not_on_pfr']) for r in report if r['starters'])))
            add('  real starters (starters table, K, P) missing from the community team: %d.'
                % sum(len(r['starters']['missing_starters']) for r in report if r['starters']))
            for b in starters_doc.get('blocked', []):
                add('  BLOCKED %s: %s not cached; its seed order is kept' % (b['team'], ', '.join(b['missing'])))
            for r in report:
                p = r['starters']
                if not p:
                    continue
                add('  %s' % r['id'])
                add('    source: %s' % p['sources']['roster'])
                add('            %s' % p['sources']['team'])
                c2 = p['checks'].get('second_copy')
                if c2:
                    add('    second copy: %s: GS agree for %d of %d players%s' % (
                        c2['url'], c2['gs_agree'], c2['players_in_both'],
                        ('; differ: ' + ', '.join('%s %d/%d' % (d['name'], d['primary'], d['second']) for d in c2['gs_differ']))
                        if c2['gs_differ'] else ''))
                else:
                    add('    second copy: none cached')
                add('    names on the PFR roster: %d of 53' % p['matched'])
                if not p['changes']:
                    add('    changes from the seed order: none')
                for c in p['changes']:
                    add('    change: %s' % c)
                for label, items in (('not on the PFR %d roster' % season, p['not_on_pfr']),
                                     ('real starters missing from the community team', p['missing_starters']),
                                     ('name matches that are not exact', p['fuzzy'])):
                    if items:
                        for n, ln in enumerate(textwrap.wrap('%s (%d): %s' % (label, len(items), '; '.join(items)),
                                                             width=96, break_on_hyphens=False)):
                            add(('    ' if n == 0 else '      ') + ln)
            add('')
    if mem:
        provenance_depth_membership(add, season, report, starters_doc)
    if coaches:
        add('HEAD COACHES (verified)')
        add("  coaches.csv: id,first,last,first_game,last_game (the team's game numbers 1-16 the coach")
        add('  coached). From data/eras/league/head_coaches_%d.json, parsed from' % season)
        add('    %s' % coaches['source'])
        add('  (fetched %s, page sha256 %s).' % (coaches['fetched'], coaches['snapshot_sha256']))
        add("  In game the staged team gets its own coach record: a copy of the template coach's (so its")
        add("  other fields stay the template's) with this name; the career numbers and biography lines")
        add('  are cleared (not sourced). Retail coach records are never written.')
        for c in coaches['coaches']:
            add('    %-14s %-18s games %2d-%2d (%d-%d-%d)' % (c['team'], c['coach'], c['first_game'], c['last_game'],
                                                          c['wins'], c['losses'], c['ties']))
        add('')
    add('INPUT FILES (sha256)')
    for rel in ('data/eras/league/nfl_seasons_1970_1980.json', 'data/eras/league/franchise_events_1970_2025.json',
                'data/eras/historic_teams_2k5.json', 'data/eras/community_rosters/%d.json' % season,
                'data/eras/league/lineage_check_%d.json' % season, 'data/eras/league/nfl_schedule_%d.json' % season,
                'data/eras/league/stadiums_1966_2025.json', 'data/eras/league/head_coaches_%d.json' % season,
                'data/eras/league/starters_%d.json' % season, 'data/eras/league/rosters_%d.json' % season):
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            add('  %s  %s' % (sha256(p), rel))
    if mem:
        for y in sorted(int(k) for k in mem['saves']):
            info = mem['saves'][str(y)]
            if docs and docs.used.get(y):
                add('  %s  %s (%d rows rated from it)' % (info['sha256'], info['file'], docs.used[y]))
    with open(os.path.join(out_dir, 'PROVENANCE.txt'), 'w', encoding='ascii', newline='\n') as f:
        f.write(ascii_text('\n'.join(L) + '\n'))


def wrap_add(add, label, items, indent='    '):
    if not items:
        return
    for n, ln in enumerate(textwrap.wrap('%s (%d): %s' % (label, len(items), '; '.join(items)),
                                         width=96, break_on_hyphens=False)):
        add((indent if n == 0 else indent + '  ') + ln)


def ascii_text(x):
    return unicodedata.normalize('NFKD', str(x)).encode('ascii', 'replace').decode('ascii')


def provenance_membership(add, season, mem, docs, teams, report, comm, digest):
    tot = Counter()
    for fid, tm in mem['teams'].items():
        for e in tm['players']:
            src = e['source']
            tot['real'] += 1
            if src['season'] != season:
                tot['other_season'] += 1
            elif src['club_slot'] != tm['community_slot']:
                tot['moved'] += 1
            else:
                tot['stayed'] += 1
        tot['fillers'] += len(tm['fillers'])
        tot['no_source'] += len(tm['no_source'])
    add('ROSTERS: REAL %d MEMBERSHIP (verified, PFR) + COMMUNITY PS2 ROSTER (UNVERIFIED) RATINGS' % season)
    add('  Who is on each team is the real %d roster: Pro-Football-Reference /teams/<code>/%d_roster.htm' % (season, season))
    add('  (Wayback copies, the same pages as DEPTH CHART STARTERS below), chosen and rated by')
    add('  tools/eras/roster_membership.py into data/eras/league/rosters_%d.json. Names, position, height,' % season)
    add("  weight, skin, face, hand and all 28 ratings are a community save record's, never invented:")
    add("  that player's record in the %d save (on any club slot or among its free agents) if it has him," % season)
    add('  else the nearest season that does (%s, ...). The ratings are the save authors\'' % ', '.join(str(y) for y in mem['season_order'][1:6]))
    add('  and unverified: show them in game as "unverified community roster" ratings.')
    add('  Jersey = the PFR %d number; years_pro = PFR experience ("Rook" = 0). Birth dates are not written.' % season)
    add('  photo = %d (no photo). Ratings above 99 are written as 99.' % NO_PHOTO)
    add('  Selection rule (M8, real players only; %d real players at most, the 1980 NFL active roster' % mem.get('roster_target', 45))
    add('  limit, plus a last-resort filler where one is listed;')
    add('  the staging takes %d..%d rows, the game\'s minimum and the templates\' player count): a traded'
        % (mem.get('game_min', ROWS_MIN), mem.get('slots', ROWS_MAX)))
    add('  player belongs to the team where he had more GS, then G; every player with a %d start plus' % season)
    add('  the kicker with the most FGA and the punter with the most punts (always kept, so a team may')
    add('  pass %d with starters); then position minimums (by games, real players only); then the most'
        % mem.get('roster_target', 45))
    add('  games played within position caps, up to %d; then players over a cap, up to %d. Minimums %s;'
        % (mem.get('roster_target', 45), mem.get('roster_target', 45),
           ' '.join('%s %d' % kv for kv in mem['rules']['min'].items())))
    add('  caps %s. A minimum no real rated player can fill is a shortfall, listed per team, not' % ' '.join(
        '%s %d' % kv for kv in mem['rules']['max'].items()))
    add('  filled. FILLERS only as a last resort: one per position in %s that no real rated player' % ' '.join(
        '%s %d' % kv for kv in mem['rules'].get('hard_min', {}).items()))
    add('  holds, and up to %d rows for a team'
        % mem.get('game_min', ROWS_MIN))
    add('  with fewer rated real players (none in %d). A filler is not a %d player of that team: a' % (season, season))
    add('  player of the franchise\'s own slot in the %d save who is on no PFR %d roster (no name match' % (season, season))
    add('  with any real player, so no one is duplicated). Every filler is flagged "FILLER" in its flags file.')
    add('  Identity across saves: name (exact, nickname, initial, one/two-letter spelling), position')
    add("  group, college (PFR's against the save's), jersey and club slot; see roster_membership.py.")
    add('  The saves\' colleges are often wrong; a match whose college differs is listed per team.')
    add('  Totals: %d real %d players (%d stayed on their save slot, %d moved from another slot or the' % (
        tot['real'], season, tot['stayed'], tot['moved']))
    add('  free agents, %d rated from another season\'s save), %d fillers, %d real %d players with no' % (
        tot['other_season'], tot['fillers'], tot['no_source'], season))
    add('  rating source in any save (left off, listed per team).')
    src = comm['source']
    add('  %d save: stem %s, directory %s, %s; payload %s sha256 %s' % (season, src['stem'], src['save_directory'],
                                                                       src['integrity'], src['payload'], digest))
    add('  Parser: 2K Football Mod Tools RosterDocument (.tmp-2k-football-mod-tools/mod_editor/core/'
        'nfl2k5_roster_records.py),')
    add('  positions as stored (0 QB .. 16 DE), ratings in RATING_BYTE_ORDER (record bytes +0x36..+0x51).')
    add('  Other saves used (payload sha256-checked when read):')
    for y in sorted(int(k) for k in mem['saves']):
        if docs.used.get(y) and y != season:
            info = mem['saves'][str(y)]
            add('    %d: %s (%d rows), payload sha256 %s' % (y, info['payload'], docs.used[y], info['payload_sha256']))
    if mem.get('record_conflicts'):
        wrap_add(add, 'one save record claimed by two real players', mem['record_conflicts'], '  ')
    if mem.get('traded'):
        wrap_add(add, 'traded during %d (kept with one team)' % season,
                 ['%s %s (%s; also %s)' % (t['name'], t['team'], t['here'], ', '.join(t['also'])) for t in mem['traded']], '  ')
    add("  Save slot lineage (teams.csv lineage_status; community_slot = the franchise's slot in the %d" % season)
    add('  save, whose players count as "stayed"): direct, or checked by tools/eras/lineage_check.py:')
    for r in report:
        t = [x for x in teams if x['id'] == r['id']][0]
        if t['lineage_status'] != 'direct':
            add('    %-14s slot %2d %s: %s' % (r['id'], r['slot'], r['save_name'], t['lineage_status']))
    for fid in [t['id'] for t in teams]:
        tm = mem['teams'][fid]
        add('  %s' % fid)
        add('    membership: %s' % tm['sources']['roster'])
        by = Counter(e['source']['season'] for e in tm['players'])
        add('    %d rows: %d real players, rated from: %s; fillers %d' % (
            len(tm['players']) + len(tm['fillers']), len(tm['players']),
            ', '.join('%d save %d' % (y, n) for y, n in sorted(by.items())), len(tm['fillers'])))
        if tm.get('shortfalls'):
            add('    position minimums not met (no more real rated players): %s' % ', '.join(
                '%s %s' % kv for kv in sorted(tm['shortfalls'].items())))
        moved = ['%s (%s)' % (e['name'], e['source']['club'] or 'FA') for e in tm['players']
                 if e['source']['season'] == season and e['source']['club_slot'] != tm['community_slot']]
        other = ['%s (%d %s)' % (e['name'], e['source']['season'], e['source']['club'] or 'FA') for e in tm['players']
                 if e['source']['season'] != season]
        wrap_add(add, 'moved in from another %d save slot' % season, moved)
        wrap_add(add, 'rated from another season', other)
        wrap_add(add, 'FILLERS', ['%s %s %s (%s)' % (f['source']['first'], f['source']['last'], f['source']['position'],
                                                   f.get('picked', 'filler'))
                                  for f in tm['fillers']])
        wrap_add(add, 'left off: no rating source', ['%s %s (%d G, %d GS)' % (e['name'], e['pfr_pos'] or '-', e['g'], e['gs'])
                                                    for e in tm['no_source']])
        wrap_add(add, 'left off: other reasons', ['%s %s (%d G, %d GS): %s' % (e['name'], e['pfr_pos'] or '-', e['g'], e['gs'], e['reason'])
                                                 for e in tm['left_off']])
        odd = []
        for e in tm['players']:
            m = e['source']['match']
            if m['kind'] != 'exact' or m['strength'] != 'strong':
                odd.append('%s = %d %s %s (%s, %s%s)' % (e['name'], e['source']['season'], e['source']['first'],
                                                       e['source']['last'], m['kind'], m['strength'],
                                                       ', college %s' % m['college'] if m['college'] != 'agrees' else ''))
        wrap_add(add, 'matches that are not exact or not strong', odd)
    add('')


def provenance_depth_membership(add, season, report, starters_doc):
    add('DEPTH CHART (verified starters, data/eras/league/starters_%d.json)' % season)
    add('  depth/<id>.csv and the roster row order: per position code the player with the most %d games' % season)
    add('  started (PFR roster table GS) first; K by most field goals attempted, P by most punts (PFR team')
    add('  page). GS count only where the PFR position group fits the game position. Then most games,')
    add('  then GS; fillers last. For T, G, DE, DT, OLB, ILB, CB and WR the two leaders head the left (rank)')
    add('  and right (side) chains, left/right from PFR\'s starters table (LT/RT, LCB/RCB ...) where it')
    add('  names a side. Other positions get side from rank as the game\'s auto depth chart does (0->2,')
    add('  1->0, 2->1); everything is capped at 7. The game applies depth/<id>.csv when it stages the team')
    add('  (src/nfl2k5_eras_depth.c).')
    for b in starters_doc.get('blocked', []):
        add('  BLOCKED %s: %s not cached' % (b['team'], ', '.join(b['missing'])))
    tot = sum(len(r['starters']['missing']) for r in report)
    add('  Real starters (PFR starters table) absent from their team (no rating source): %d' % tot)
    for r in report:
        p = r['starters']
        add('  %s' % r['id'])
        add('    source: %s' % starters_doc['teams'][r['id']]['sources']['roster'])
        add('            %s' % starters_doc['teams'][r['id']]['sources']['team'])
        c2 = starters_doc['teams'][r['id']]['checks'].get('second_copy')
        add('    second copy: %s' % (('%s: GS agree for %d of %d players' % (c2['url'], c2['gs_agree'], c2['players_in_both']))
                                     if c2 else 'none cached'))
        heads = ['%s %s' % (pc, ' / '.join(n)) for pc, n in sorted(p['heads'].items(), key=lambda kv: POS_ORDER.index(kv[0]))]
        wrap_add(add, 'first on the depth chart', heads)
        wrap_add(add, 'real starters absent', ['%s %s' % (m['pos'], m['name']) for m in p['missing']])
    add('')


POS_ORDER = ('QB', 'HB', 'FB', 'WR', 'TE', 'T', 'G', 'C', 'DE', 'DT', 'OLB', 'ILB', 'CB', 'FS', 'SS', 'K', 'P')


# ---------------------------------------------------------------------------
# T0.1

def check(season):
    out_dir = runtime_dir(season)
    league = season_league(season)
    historic = jload('data', 'eras', 'historic_teams_2k5.json')
    stadiums = jload('data', 'eras', 'league', 'stadiums_1966_2025.json')
    looks = jload('data', 'eras', 'league', 'team_looks.json')
    from mod_editor.core import nfl2k5_roster_records as rr
    header = HEADER + list(rr.RATING_BYTE_ORDER) + ['photo']
    rows = list(csv.reader(open(os.path.join(out_dir, 'teams.csv'), encoding='ascii')))
    assert rows[0] == TEAMS_HEADER, rows[0]
    teams = [dict(zip(TEAMS_HEADER, r)) for r in rows[1:]]
    assert all(len(r) == len(TEAMS_HEADER) for r in rows)
    want = {t['id']: (c['name'], d['name']) for c in league['conferences'] for d in c['divisions'] for t in d['teams']}
    assert len(teams) == league['team_count'] == len(want), len(teams)
    assert {t['id'] for t in teams} == set(want)
    for t in teams:
        assert (t['conference'], t['division']) == want[t['id']], t
    sizes = {}
    for c in ('AFC', 'NFC'):
        sizes[c] = [sum(1 for t in teams if t['division'] == '%s %s' % (c, d)) for d in ('East', 'Central', 'West')]
    if season == 1980:
        assert sizes == {'AFC': [5, 4, 5], 'NFC': [5, 5, 4]}, sizes
    assert len({t['community_slot'] for t in teams}) == len(teams)
    lin_ok = 0
    for t in teams:
        e = int(t['template_entry'])
        assert 0 <= e <= 74 and historic['teams'][e]['id'] == t['template_id'], t
        assert t['lineage_status'] in ('direct', 'inferred') or t['lineage_status'].startswith('checked:https://'), t
        lin_ok += t['lineage_status'] != 'inferred'
        assert t['stadium'] and t['stadium_match'] in ('exact', 'standin') and t['stadium_historical'], t
        assert (t['stadium'], t['stadium_match'], t['stadium_historical']) == home_stadium(stadiums, t['id'], season), t
        assert int(t['style']) == team_style(looks, t['id'], season, historic['teams'][e]), t
        for field in TEAMS_HEADER:
            assert not re.search(r'[,"\r\n]', t[field]), (t['id'], field)
    if season == 1980:
        bad = {t['id']: t['template_id'] for t in teams if PLAN_TEMPLATES_1980[t['id']] != t['template_id']}
        assert not bad, ('templates differ from PHASE2-PLAN 2.4', bad)
    files = 0
    totals = Counter()
    row_counts = {}
    mem_mode = os.path.exists(membership_path(season))
    for t in teams:
        p = os.path.join(out_dir, t['roster_file'])
        lines = open(p, encoding='latin-1').read().split('\n')
        assert lines[-1] == '' and lines[0] == ','.join(header), p
        body = lines[1:-1]
        if mem_mode:
            assert ROWS_MIN <= len(body) <= ROWS_MAX, (p, len(body), 'a team takes %d..%d rows' % (ROWS_MIN, ROWS_MAX))
        else:
            assert len(body) == 53, (p, len(body))
        row_counts[t['id']] = len(body)
        pos_seen = Counter()
        for ln in body:
            v = ln.split(',')
            assert len(v) == len(header) == 39, (p, ln)
            assert v[0] and v[1] and len(v[0]) <= NAME_MAX and len(v[1]) <= NAME_MAX and '"' not in ln, (p, ln)
            n = [int(x) for x in v[2:]]
            pos, ratings = n[0], n[8:36]
            assert 0 <= pos <= 16, (p, ln)
            assert all(0 <= r <= 99 for r in ratings), (p, ln)
            assert 0 <= n[1] <= 127 and 0 <= n[2] <= 255 and 150 <= n[3] <= 405 and 0 <= n[4] <= 31, (p, ln)
            assert 0 <= n[5] <= 31 and 0 <= n[6] <= 15 and n[7] in (0, 1) and n[36] == NO_PHOTO, (p, ln)
            pos_seen[rr.POSITIONS[pos]] += 1
        for need in (('QB', 'K') if mem_mode else ('QB', 'K', 'P')):
            assert pos_seen[need] >= 1, (p, need)
        for need, n_ in STAGE_MIN.items():
            assert pos_seen[need] >= n_, (p, need, pos_seen[need], 'below the staging minimum %d' % n_)
        assert os.path.exists(p[:-4] + '.flags.txt'), p
        files += 1
        totals.update(pos_seen)
    assert files == len(teams)
    # depth/<id>.csv: one row per roster row, one starter per chain, and the starters rule
    st_path = os.path.join(LEAGUE, 'starters_%d.json' % season)
    st = jload(st_path)['teams'] if os.path.exists(st_path) else {}
    mem_doc = jload(membership_path(season)) if os.path.exists(membership_path(season)) else None
    MEM_SEEN.clear()
    MEM_CHECK.clear()
    del MEM_FILLERS[:]
    depth_ok = rule_ok = 0
    for t in teams:
        p = os.path.join(out_dir, 'depth', '%s.csv' % t['id'])
        if not os.path.exists(p):
            continue
        roster = [ln.split(',') for ln in open(os.path.join(out_dir, t['roster_file']), encoding='latin-1').read().split('\n')[1:-1]]
        lines = open(p, encoding='latin-1').read().split('\n')
        assert lines[0] == ','.join(DEPTH_HEADER) and lines[-1] == '' and len(lines) == len(roster) + 2, p
        d = [ln.split(',') for ln in lines[1:-1]]
        by_pos = defaultdict(list)
        for i, (v, r) in enumerate(zip(d, roster)):
            assert int(v[0]) == i and v[1:5] == r[0:4] and 0 <= int(v[5]) <= 7 and 0 <= int(v[6]) <= 7, (p, v)
            by_pos[int(v[3])].append((i, int(v[5]), int(v[6])))
        for pos, g in by_pos.items():
            assert sum(1 for _, rk, _ in g if rk == 0) == 1, (p, pos)
            if rr.POSITIONS[pos] in PAIRED and len(g) > 1:
                assert sum(1 for _, _, sd in g if sd == 0) == 1, (p, pos)
        depth_ok += 1
        if mem_doc:
            rule_ok += check_membership_team(t, d, by_pos, mem_doc, season)
            continue
        if t['id'] in st:
            s = st[t['id']]
            fga = {k['pfr_id']: k['fga'] for k in s['kicking']}
            punts = {k['pfr_id']: k['punts'] for k in s['punting']}
            for pos, g in by_pos.items():
                pc = rr.POSITIONS[pos]

                def key(i):
                    m, _ = match_pfr(d[i][1], d[i][2], s['roster'], d[i][4])
                    return 0 if not m else fga.get(m['pfr_id'], 0) if pc == 'K' else \
                        punts.get(m['pfr_id'], 0) if pc == 'P' else m['gs']
                keys = sorted((key(i) for i, _, _ in g), reverse=True)
                heads = [key(i) for i, rk, _ in g if rk == 0]
                if pc in PAIRED and len(g) > 1:
                    heads += [key(i) for i, _, sd in g if sd == 0]
                    assert sorted(heads, reverse=True) == keys[:2] or (keys[1] == 0 and keys[0] in heads), (p, pc, heads, keys)
                else:
                    assert heads[0] == keys[0], (p, pc, heads, keys)
            rule_ok += 1
    prov = os.path.join(out_dir, 'PROVENANCE.txt')
    assert os.path.exists(prov), prov
    ptxt = open(prov, encoding='ascii').read()
    assert 'community PS2 roster (unverified)'.upper() in ptxt and 'unverified community roster' in ptxt
    sched = os.path.join(out_dir, 'schedule.csv')
    sched_note = 'absent'
    if os.path.exists(sched):
        srows = list(csv.reader(open(sched, encoding='ascii')))
        ids = {t['id'] for t in teams}
        assert all(r[3] in ids and r[4] in ids for r in srows[1:]), 'schedule.csv names a team not in teams.csv'
        sched_note = '%d games, every team id in teams.csv, historical=%s' % (
            len(srows) - 1, ','.join(sorted({r[5] for r in srows[1:]})))
    print('T0.1 %d runtime package: OK' % season)
    print('  teams: %d; division sizes AFC %s, NFC %s' % (len(teams), sizes['AFC'], sizes['NFC']))
    print('  roster files: %d, %d..%d rows (%s), 39 fields; positions 0..16, ratings 0..99; staging position'
          ' minimums met' % (files, min(row_counts.values()), max(row_counts.values()),
                             ' '.join('%d x %d' % (c, n) for n, c in sorted(Counter(row_counts.values()).items()))))
    print('  template_entry 0..74 and template_id agree with historic_teams_2k5.json%s' % (
        '; all 28 equal PHASE2-PLAN 2.4' if season == 1980 else ''))
    print('  lineage: %s' % dict(Counter(t['lineage_status'].split(':')[0] for t in teams)))
    print('  home stadiums: %s' % dict(Counter(t['stadium_match'] for t in teams)))
    print('  PROVENANCE.txt present; schedule.csv: %s' % sched_note)
    print('  depth files: %d (rows = roster rows, one starter per chain); verified starters rule holds for %d'
          % (depth_ok, rule_ok))
    print('  position totals: %s' % dict(sorted(totals.items())))
    if mem_doc:
        print('  membership: every row is a rosters_%d.json entry of its team; no name twice in a team; no real'
              % season)
        print('  player twice in the league; real starters on their team where rated: %d of %d'
              % (MEM_CHECK['starters_present'], MEM_CHECK['starters']))
        print('  fillers: %d, each the last resort the membership file states (%s)'
              % (MEM_CHECK['fillers'], ', '.join(MEM_FILLERS) or 'none'))


MEM_CHECK = Counter()


def check_membership_team(t, d, by_pos, mem, season):
    """Membership mode: rows = the team's membership entries (names as written_name gives them,
    position, jersey), no name twice, and the depth rule (entry_key) holds at every chain head."""
    from mod_editor.core import nfl2k5_roster_records as rr
    tm = mem['teams'][t['id']]
    ents = list(tm['players']) + list(tm['fillers'])
    want = Counter()
    by_name = {}
    for e in ents:
        f, l, _ = written_name(e)
        want[(f, l, str(e['position_code']))] += 1
        by_name[(f, l)] = e
    got = Counter((v[1], v[2], v[3]) for v in d)
    assert got == want, (t['id'], 'roster rows differ from the membership file', got - want, want - got)
    assert len({(v[1], v[2]) for v in d}) == len(d), (t['id'], 'a name appears twice')
    for v in d:
        e = by_name[(v[1], v[2])]
        if 'pfr_id' in e and re.fullmatch(r'\d{1,2}', e['number'] or ''):
            assert int(v[4]) == int(e['number']), (t['id'], v, 'jersey is not the PFR one')
    for pos, g in by_pos.items():
        pc = rr.POSITIONS[pos]
        keys = sorted((entry_key(by_name[(d[i][1], d[i][2])], pc) for i, _, _ in g), reverse=True)
        heads = [entry_key(by_name[(d[i][1], d[i][2])], pc) for i, rk, _ in g if rk == 0]
        if pc in PAIRED and len(g) > 1:
            heads += [entry_key(by_name[(d[i][1], d[i][2])], pc) for i, _, sd in g if sd == 0]
            assert sorted(heads, reverse=True) == keys[:2], (t['id'], pc, heads, keys)
        else:
            assert heads[0] == keys[0], (t['id'], pc, heads, keys)
        # list order follows the depth order (the M2 writer ranks by row order)
        ranks = [rk for _, rk, _ in sorted(g)]
        assert ranks == sorted(ranks), (t['id'], pc, 'row order is not the depth order', ranks)
    hard = mem['rules'].get('hard_min', {})
    real_pos = Counter(e['position'] for e in tm['players'])
    for f in tm['fillers']:
        pos = f['position']
        last_resort = (pos in hard and real_pos[pos] < hard[pos]) or len(tm['players']) < mem.get('game_min', ROWS_MIN)
        assert last_resort, (t['id'], 'a filler that is not a last resort', f['source']['first'], f['source']['last'], pos)
        MEM_CHECK['fillers'] += 1
        MEM_FILLERS.append('%s %s %s %s' % (t['id'], pos, f['source']['first'], f['source']['last']))
    if 'roster_target' in mem:
        keep = sum(1 for e in tm['players'] if e.get('picked', '').startswith(('started', 'kicker', 'punter')))
        assert len(tm['players']) <= max(mem['roster_target'], keep), (t['id'], 'more real players than the target')
    present = {e.get('pfr_id') for e in ents}
    MEM_CHECK['starters'] += len(tm['starters'])
    MEM_CHECK['starters_present'] += sum(1 for s_ in tm['starters'] if s_['pfr_id'] in present)
    ids = [e['pfr_id'] for e in tm['players']]
    MEM_SEEN.update(ids)
    assert all(MEM_SEEN[i] == 1 for i in ids), (t['id'], 'a real player is on two teams')
    return 1


MEM_SEEN = Counter()
MEM_FILLERS = []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--check', action='store_true', help='only check the written package (T0.1)')
    ap.add_argument('--out', help='write/check this folder instead of data/eras/runtime/<season>')
    a = ap.parse_args()
    if a.out:
        global OUT_OVERRIDE
        OUT_OVERRIDE = a.out
    if not a.check:
        build(a.season)
    check(a.season)


if __name__ == '__main__':
    main()

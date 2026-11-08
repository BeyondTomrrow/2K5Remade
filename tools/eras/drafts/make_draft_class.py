"""The My NFL Eras draft class of one year: every real pick with its game ratings.

    python -I tools/eras/drafts/make_draft_class.py [year]        (default 1981)
    python -I tools/eras/drafts/make_draft_class.py 1981 --check  (re-read and summarise the written files)

Inputs (docs/eras/research/DRAFT-CLASSES.md):
  data/eras/league/drafts/<year>.json                 every real pick (PFR, cross-checked with Wikipedia)
  data/eras/league/drafts/<year>_ratings_match.json   each pick matched to the community PS2 rosters
  data/eras/community_rosters/<season>.json           the record ratings (28), jersey, height, weight
  data/eras/ps2_saves/... (through rosters_<season-1>.json 'saves')  skin / face / hand of the record
  data/eras/runtime/<season-1>/rosters/*.csv          position medians for the picks with no record

Outputs:
  data/eras/league/drafts/<year>_class.json           the class with sources and flags (documentation)
  mods/eras/draft/class_<year>.csv                    what the game reads (src/nfl2k5_eras_draft.c)

The method is the one DRAFT-CLASSES.md section 3 proposes and the user's decisions of 2026-10-16
(PHASE2-PLAN.md "Draft decisions"):
  * rookie ratings = the pick's record in the draft season's save (rookie_rated); else the first
    season he is rated (next_season / rated_later), kept unchanged and flagged;
  * potential = his real peak season's 28 ratings (the highest overall_estimate in the career window);
    a pick rated once has potential = his one record (single_observation);
  * the picks never rated in any save (133 of 332 in 1981) get LOW ESTIMATED ratings, flagged
    `estimated`: the position's mean rookie profile among rated picks of rounds 7-12 (the camp-body
    end of the class), scaled to a target overall by round and real career (never played: 46-52 by
    round; played but AV <= 2: 55; AV 3-9: 60; AV 10-29: 64; AV 30+: 68). Their potential is the same
    profile at the AV band's peak (64 / 70 / 74), or the rookie profile when he never played. Their
    body (height, weight, skin, face, hand) is the position's median / mode in the season's package,
    flagged `estimated_body`; their jersey is 0 (the game assigns a free number of the position's
    range when he joins a roster, logged).
  * The 2K5 position of a rated pick is his record's; an unrated pick's is mapped from PFR / Wikipedia.
Nothing else is invented. Sleepers (hidden star potential, Alternate History only) are chosen by the
game per league at draft time, not here. Source label of every rating: community PS2 roster
(unverified). Data only; run with python -I.
"""
import csv, hashlib, json, os, re, statistics, sys
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, '.tmp-2k-football-mod-tools'))
LEAGUE = os.path.join(ROOT, 'data', 'eras', 'league')

POSITIONS = ("QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE")
RATINGS = ("speed", "agility", "pass_arm_strength", "stamina", "kick_power", "durability", "strength",
           "jumping", "coverage", "run_route", "tackle", "break_tackle", "pass_accuracy",
           "pass_read_coverage", "catch", "run_blocking", "pass_blocking", "hold_onto_ball", "pass_rush",
           "run_coverage", "kick_accuracy", "kicking_style", "leadership", "power_run_style", "composure",
           "scramble", "consistency", "aggressiveness")
STYLE = ("power_run_style", "scramble", "kicking_style")
# The Mod Tools' position-weighted estimate (mod_editor/core/nfl2k5_roster_records.py OVERALL_WEIGHTS):
# not the game's overall, but the number every community-roster figure in DRAFT-CLASSES.md uses.
_SKILL = {
    "QB": {"pass_accuracy": 28, "pass_read_coverage": 22, "pass_arm_strength": 18, "composure": 10, "scramble": 6, "speed": 6, "agility": 5, "consistency": 5},
    "HB": {"speed": 20, "agility": 18, "break_tackle": 18, "strength": 10, "catch": 8, "hold_onto_ball": 8, "run_blocking": 6, "jumping": 5, "stamina": 5},
    "FB": {"run_blocking": 24, "strength": 18, "break_tackle": 16, "speed": 12, "catch": 10, "agility": 10, "hold_onto_ball": 10},
    "WR": {"catch": 28, "run_route": 22, "speed": 20, "agility": 12, "jumping": 10, "hold_onto_ball": 8},
    "TE": {"catch": 24, "run_blocking": 18, "run_route": 16, "speed": 12, "pass_blocking": 10, "strength": 10, "agility": 10},
    "C": {"run_blocking": 38, "pass_blocking": 38, "strength": 16, "agility": 8},
    "DT": {"pass_rush": 28, "tackle": 22, "strength": 22, "run_coverage": 16, "agility": 6, "speed": 6},
    "DE": {"pass_rush": 32, "tackle": 20, "strength": 18, "run_coverage": 14, "speed": 10, "agility": 6},
    "ILB": {"tackle": 26, "run_coverage": 20, "coverage": 16, "pass_rush": 14, "speed": 12, "strength": 12},
    "CB": {"coverage": 30, "speed": 22, "agility": 16, "tackle": 14, "jumping": 10, "catch": 8},
    "SS": {"coverage": 22, "tackle": 24, "speed": 20, "agility": 14, "jumping": 10, "strength": 10},
    "K": {"kick_power": 50, "kick_accuracy": 50},
}
WEIGHTS = {**_SKILL, "G": _SKILL["C"], "T": _SKILL["C"], "OLB": _SKILL["ILB"], "FS": _SKILL["CB"], "P": _SKILL["K"]}

# PFR franchise codes of the 1980s -> Eras team ids (tools/eras/draftshow/make_draft_picks.py)
TEAM = {
    'ATL': 'falcons', 'BAL': 'colts', 'BUF': 'bills', 'CHI': 'bears', 'CIN': 'bengals', 'CLE': 'browns',
    'DAL': 'cowboys', 'DEN': 'broncos', 'DET': 'lions', 'GNB': 'packers', 'HOU': 'oilers_titans',
    'KAN': 'chiefs', 'MIA': 'dolphins', 'MIN': 'vikings', 'NOR': 'saints', 'NWE': 'patriots',
    'NYG': 'giants', 'NYJ': 'jets', 'OAK': 'raiders', 'PHI': 'eagles', 'PIT': 'steelers', 'RAM': 'rams',
    'SDG': 'chargers', 'SEA': 'seahawks', 'SFO': '49ers', 'STL': 'cardinals', 'TAM': 'buccaneers',
    'WAS': 'washington',
}

# Estimated-pick targets (overall estimate) by real career; never played: by round group.
NEVER_PLAYED = {1: 52, 2: 52, 3: 52, 4: 50, 5: 50, 6: 50, 7: 48, 8: 48, 9: 48, 10: 46, 11: 46, 12: 46}
AV_BANDS = ((2, 55, 55), (9, 60, 64), (29, 64, 70), (10 ** 6, 68, 74))     # (max AV, rookie, peak)


def overall(pos, r):
    w = WEIGHTS.get(pos)
    if not w:
        named = [r[k] for k in RATINGS if k not in STYLE]
        return max(0, min(99, round(sum(named) / len(named))))
    return max(0, min(99, round(sum(r[k] * v for k, v in w.items()) / sum(w.values()))))


def pos_from_pfr(pfr, wiki):
    """2K5 position of a pick with no record: PFR's coarse code refined by Wikipedia's."""
    w = (wiki or '').lower()
    if pfr in ('QB', 'K', 'P', 'WR', 'TE', 'C', 'G', 'T', 'DT', 'DE'):
        return pfr
    if pfr == 'NT':
        return 'DT'
    if pfr == 'RB':
        return 'FB' if 'fullback' in w else 'HB'
    if pfr in ('DB', 'CB', 'S'):
        if 'strong' in w:
            return 'SS'
        if 'safety' in w:
            return 'FS'
        return 'CB'
    if pfr == 'LB':
        return 'ILB' if ('inside' in w or 'middle' in w) else 'OLB'
    if pfr in ('OL',):
        return 'G'
    if pfr in ('DL',):
        return 'DE'
    return {'OT': 'T', 'OG': 'G'}.get(pfr, 'HB')


def load_json(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return json.load(f)


class Saves:
    """Community roster JSON per season (ratings, body) and, where the payload is there, the save's
    own record for skin / face / hand (through the Mod Tools' RosterDocument)."""

    def __init__(self, mem):
        self.mem = mem
        self.json = {}
        self.docs = {}
        self.rr = None
        try:
            from mod_editor.core import nfl2k5_roster_records as rr
            self.rr = rr
        except Exception as e:                      # the Mod Tools are not in this checkout
            print('note: RosterDocument unavailable (%s): skin / face / hand fall back to the position mode' % e)

    def record(self, season, index):
        if season not in self.json:
            d = load_json('data/eras/community_rosters/%d.json' % season)
            self.json[season] = {p['record_index']: p for p in d['players']}
        return self.json[season].get(index)

    def body(self, season, index):
        """(skin, face, hand) of the save record, or None."""
        if not self.rr:
            return None
        if season not in self.docs:
            info = (self.mem.get('saves') or {}).get(str(season))
            self.docs[season] = None
            if info and os.path.exists(os.path.join(ROOT, info['payload'])):
                payload = open(os.path.join(ROOT, info['payload']), 'rb').read()
                if hashlib.sha256(payload).hexdigest() == info['payload_sha256']:
                    doc = self.rr.RosterDocument(payload, base=self.rr.find_block_base(payload),
                                                 source=info['payload'], reference_year=season)
                    self.docs[season] = {p.index: p for p in doc.players if p.pool == 'primary'}
        docs = self.docs[season]
        if not docs or index not in docs:
            return None
        rec = docs[index].record
        return rec.skin, rec.values['face'], rec.values['hand']


def package_medians(season):
    """Per 2K5 position: median height / weight and modal skin / face / hand of the season's package."""
    out = {}
    rows = defaultdict(list)
    folder = os.path.join(ROOT, 'data', 'eras', 'runtime', str(season), 'rosters')
    if os.path.isdir(folder):
        for name in sorted(os.listdir(folder)):
            if not name.endswith('.csv'):
                continue
            with open(os.path.join(folder, name), encoding='latin-1') as f:
                for r in csv.DictReader(f):
                    rows[int(r['pos'])].append(r)
    for code, name in enumerate(POSITIONS):
        g = rows.get(code)
        if g:
            out[name] = dict(height=int(statistics.median(int(x['height']) for x in g)),
                             weight=int(statistics.median(int(x['weight']) for x in g)),
                             skin=int(Counter(x['skin'] for x in g).most_common(1)[0][0]),
                             face=int(Counter(x['face'] for x in g).most_common(1)[0][0]),
                             hand=int(Counter(x['hand'] for x in g).most_common(1)[0][0]), source='package %d' % season)
        else:   # no package in this checkout: the 1980 package's values, recorded here once
            fallback = {"QB": (75, 207), "K": (71, 181), "P": (73, 195), "WR": (72, 188), "CB": (72, 187), "FS": (73, 191),
                        "SS": (73, 193), "HB": (72, 205), "FB": (72, 219), "TE": (76, 230), "OLB": (74, 225), "ILB": (74, 229),
                        "C": (75, 251), "G": (75, 259), "T": (77, 264), "DT": (75, 257), "DE": (76, 254)}[name]
            out[name] = dict(height=fallback[0], weight=fallback[1], skin=0, face=1, hand=1, source='built-in 1980 medians')
    return out


def scaled(profile, pos, target):
    """The profile's non-style ratings scaled so the overall lands on target (clamped 20..99)."""
    base = overall(pos, profile)
    k = target / base if base else 1.0
    out = {}
    for name in RATINGS:
        if name in STYLE:
            out[name] = profile[name]
        else:
            out[name] = max(20, min(99, round(profile[name] * k)))
    # one more pass to land on the target exactly where the clamp moved it
    for _ in range(4):
        got = overall(pos, out)
        if got == target:
            break
        d = 1 if got < target else -1
        for name, w in sorted((WEIGHTS.get(pos) or {}).items(), key=lambda kv: -kv[1]):
            out[name] = max(20, min(99, out[name] + d))
    return out


def build(year):
    season = year - 1
    draft = load_json('data/eras/league/drafts/%d.json' % year)
    match = load_json('data/eras/league/drafts/%d_ratings_match.json' % year)
    mem_path = os.path.join(LEAGUE, 'rosters_%d.json' % season)
    mem = json.load(open(mem_path, encoding='utf-8')) if os.path.exists(mem_path) else {}
    saves = Saves(mem)
    medians = package_medians(season)
    by_pick = {p['pick']: p for p in match['picks']}
    players = []
    rated_profiles = defaultdict(list)       # pos -> rookie profiles of rated late-round picks
    counts = Counter()

    def rec_ratings(rec):
        return dict(zip(RATINGS, rec['ratings']))

    # pass 1: the rated picks
    for p in draft['picks']:
        m = by_pick[p['pick']]
        e = dict(pick=p['pick'], round=p['round'], team=TEAM[p['team']], pfr_team=p['team'], player=p['player'],
                 pfr_pos=p['pos'], wiki_position=p.get('wiki_position'), college=p['college'] or '', pfr_id=p.get('pfr_id'),
                 games=p.get('games') or 0, career_av=p.get('career_av') or 0, last_season=p.get('last_season'),
                 status=m['status'], flags=[])
        first_season = None
        if m['status'] == 'rookie_rated':
            first_season = year
            e['rookie_record'] = m['rookie']['record_index']
        elif m['status'] in ('first_rated_second_season', 'rated_later'):
            first_season = min(m['seasons_rated'])
            e['rookie_record'] = m['season_checks'][str(first_season)]['record_index']
            e['flags'].append('rookie_from_next_season' if first_season == year + 1 else 'rookie_from_%d' % first_season)
        if first_season is not None:
            rec = saves.record(first_season, e['rookie_record'])
            e['rookie_season'] = first_season
            e['pos'] = rec['position'] if rec['position'] in POSITIONS else pos_from_pfr(p['pos'], p.get('wiki_position'))
            e['ratings'] = rec_ratings(rec)
            e['ovr'] = overall(e['pos'], e['ratings'])
            e['height'] = rec['height_in']
            e['weight'] = rec['weight_lb']
            e['number'] = rec['jersey'] if 0 < rec['jersey'] <= 99 else 0
            body = saves.body(first_season, e['rookie_record'])
            if body:
                e['skin'], e['face'], e['hand'] = body
                e['body_source'] = 'save record %d of %d' % (e['rookie_record'], first_season)
            else:
                md = medians[e['pos']]
                e['skin'], e['face'], e['hand'] = md['skin'], md['face'], md['hand']
                e['body_source'] = 'position mode (%s)' % md['source']
                e['flags'].append('estimated_look')
            pk = m['peak']
            e['peak_season'] = pk['season']
            e['peak_ratings'] = pk['ratings']
            e['peak_ovr'] = overall(e['pos'], pk['ratings'])
            e['years_to_peak'] = max(0, pk['season'] - year)
            if len(m['seasons_rated']) == 1:
                e['flags'].append('single_observation')
            e['source'] = 'rookie_rated' if m['status'] == 'rookie_rated' else ('next_season' if first_season == year + 1 else 'rated_later')
            if p['round'] >= 7:
                rated_profiles[e['pos']].append(e['ratings'])
            counts[e['source']] += 1
        players.append(e)

    # the camp-body profile per position: mean of the late-round rated rookies, else all rated rookies
    def profile_for(pos):
        pool = rated_profiles.get(pos) or [x['ratings'] for x in players if x.get('ratings') and x['pos'] == pos]
        if not pool:
            grp = {'FS': 'CB', 'SS': 'CB', 'FB': 'HB', 'OLB': 'ILB', 'ILB': 'OLB', 'G': 'T', 'T': 'G', 'C': 'G', 'P': 'K', 'K': 'P', 'DT': 'DE', 'DE': 'DT'}
            pool = [x['ratings'] for x in players if x.get('ratings') and x['pos'] == grp.get(pos)]
        if not pool:
            pool = [x['ratings'] for x in players if x.get('ratings')]
        prof = {}
        for name in RATINGS:
            vals = [r[name] for r in pool]
            prof[name] = int(statistics.median(vals)) if name in STYLE else round(statistics.mean(vals))
        return prof, len(pool)

    # pass 2: the never-rated picks, estimated and flagged
    for e, p in zip(players, draft['picks']):
        if e.get('ratings'):
            continue
        e['pos'] = pos_from_pfr(p['pos'], p.get('wiki_position'))
        prof, n = profile_for(e['pos'])
        if e['games'] <= 0:
            target, peak_t, band = NEVER_PLAYED[e['round']], None, 'never played (%s)' % ('round %d' % e['round'])
        else:
            for av_max, r_t, p_t in AV_BANDS:
                if e['career_av'] <= av_max:
                    target, peak_t, band = r_t, p_t, 'played, career AV %d' % e['career_av']
                    break
        e['ratings'] = scaled(prof, e['pos'], target)
        e['ovr'] = overall(e['pos'], e['ratings'])
        e['peak_ratings'] = scaled(prof, e['pos'], peak_t) if peak_t else dict(e['ratings'])
        e['peak_ovr'] = overall(e['pos'], e['peak_ratings'])
        e['peak_season'] = None
        e['years_to_peak'] = 3 if peak_t else 0
        md = medians[e['pos']]
        e['height'], e['weight'], e['skin'], e['face'], e['hand'] = md['height'], md['weight'], md['skin'], md['face'], md['hand']
        e['body_source'] = 'position median / mode (%s)' % md['source']
        e['number'] = 0
        e['source'] = 'estimated'
        e['estimate'] = dict(band=band, target=target, peak_target=peak_t, profile='mean rookie profile of %d rated %s picks (rounds 7-12 where possible)' % (n, e['pos']))
        e['flags'] += ['estimated', 'estimated_body']
        counts['estimated'] += 1

    # names
    for e in players:
        w = e['player'].split(' ', 1)
        e['first'], e['last'] = (w[0], w[1]) if len(w) == 2 else (e['player'], '')
        for k in ('first', 'last'):
            e[k] = re.sub(r'[,"\r\n]', '', e[k])[:31]
    return draft, players, counts, medians


def write(year, draft, players, counts, medians):
    out_json = os.path.join(LEAGUE, 'drafts', '%d_class.json' % year)
    out_csv = os.path.join(ROOT, 'mods', 'eras', 'draft', 'class_%d.csv' % year)
    doc = dict(schema='nfl2k5_eras_draft_class/v1', draft_year=year, season=year - 1, source_label='community PS2 roster (unverified)',
               picks_source=draft.get('source'), method=__doc__.strip().split('\n'), counts=dict(counts),
               medians=medians, players=players)
    with open(out_json, 'w', encoding='utf-8') as f:
        json.dump(doc, f, indent=1)
    os.makedirs(os.path.dirname(out_csv), exist_ok=True)
    head = ['pick', 'round', 'team', 'pfr_team', 'first', 'last', 'pos', 'pos_code', 'college', 'height', 'weight', 'skin', 'face', 'hand',
            'number', 'source', 'ovr', 'peak_ovr', 'peak_season', 'years_to_peak', 'games', 'career_av', 'flags']
    head += ['r_' + k for k in RATINGS] + ['p_' + k for k in RATINGS]
    with open(out_csv, 'w', encoding='latin-1', newline='', errors='replace') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE, escapechar='\\')
        w.writerow(head)
        for e in players:
            row = [e['pick'], e['round'], e['team'], e['pfr_team'], e['first'], e['last'], e['pos'], POSITIONS.index(e['pos']),
                   e['college'].replace(',', ''), e['height'], e['weight'], e['skin'], e['face'], e['hand'], e['number'], e['source'],
                   e['ovr'], e['peak_ovr'], e['peak_season'] or '', e['years_to_peak'], e['games'], e['career_av'], ' '.join(e['flags'])]
            row += [min(99, e['ratings'][k]) for k in RATINGS] + [min(99, e['peak_ratings'][k]) for k in RATINGS]
            w.writerow(row)
    return out_json, out_csv


def summary(year, players, counts):
    print('%d draft class: %d picks; sources: %s' % (year, len(players), dict(counts)))
    by_round = defaultdict(list)
    for e in players:
        by_round[e['round']].append(e)
    for r in sorted(by_round):
        g = by_round[r]
        est = [x for x in g if x['source'] == 'estimated']
        print('  round %2d: %2d picks, mean ovr %.1f (rated %.1f, estimated %s), mean peak %.1f' % (
            r, len(g), statistics.mean(x['ovr'] for x in g),
            statistics.mean([x['ovr'] for x in g if x['source'] != 'estimated'] or [0]),
            ('%.1f' % statistics.mean(x['ovr'] for x in est)) if est else '-', statistics.mean(x['peak_ovr'] for x in g)))
    print('  positions:', dict(Counter(e['pos'] for e in players)))
    print('  looks from save records: %d, estimated looks: %d' % (
        sum(1 for e in players if e.get('body_source', '').startswith('save')), sum(1 for e in players if 'estimated_look' in e['flags'] or 'estimated_body' in e['flags'])))
    for e in players[:5]:
        print('  #%d %s %s %s %s ovr %d peak %d (%s) %s' % (e['pick'], e['team'], e['player'], e['pos'], e['college'], e['ovr'], e['peak_ovr'], e['peak_season'], e['source']))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    year = int(args[0]) if args else 1981
    if '--check' in sys.argv:
        doc = load_json('data/eras/league/drafts/%d_class.json' % year)
        rows = list(csv.DictReader(open(os.path.join(ROOT, 'mods', 'eras', 'draft', 'class_%d.csv' % year), encoding='latin-1')))
        assert len(rows) == len(doc['players']) == doc_count(year), 'row counts differ'
        summary(year, doc['players'], Counter(doc['counts']))
        return
    draft, players, counts, medians = build(year)
    oj, oc = write(year, draft, players, counts, medians)
    summary(year, players, counts)
    print('wrote %s and %s' % (oj, oc))


def doc_count(year):
    return load_json('data/eras/league/drafts/%d.json' % year)['pick_count']


if __name__ == '__main__':
    main()

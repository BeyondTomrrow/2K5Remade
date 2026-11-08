"""My NFL Eras (M0 task D3): do the renamed slots of a community roster save hold the right franchise?

    python -I tools/eras/lineage_check.py [--season 1980] [--fetch]

The community saves keep the retail 2004 names on every club slot (COMMUNITY-ROSTERS.md), so
four 1980 franchises sit in slots named for their later identity: ARZ (St. Louis Cardinals),
IND (Baltimore Colts), STL (Los Angeles Rams) and TEN (Houston Oilers). For each, every
player name in the slot is compared with the franchise's sourced roster for the season: PFR
/teams/<code>/<season>_roster.htm, read from a Wayback copy cached in
build/eras/league/pfr_cache/ (index: lineage_sources.json; --fetch downloads missing copies,
nothing else). As a control, the same names are compared with the other three rosters.

Result per slot: checked:<wayback url> when at least MIN_MATCHES names are on the franchise's
own roster and the slot matches no other of the four rosters as well; otherwise inferred.
Written to data/eras/league/lineage_check_<season>.json, which make_runtime_package.py reads
for teams.csv lineage_status.

The slot players come from data/eras/community_rosters/<season>.json (decoded from the save by
extract_community_rosters.py with the mod tools' RosterDocument). Only names are compared; the
ratings and bios stay "community PS2 roster (unverified)". Data only; run with python -I.
"""
import argparse, json, os, re, sys, unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import league_schedule as ls  # noqa: E402  (shared Wayback cache helpers)

ROOT = ls.ROOT
CACHE = ls.CACHE
INDEX = os.path.join(CACHE, 'lineage_sources.json')
MIN_MATCHES = 5

# season -> [(save slot, save abbreviation, franchise id, PFR code, capture)]
SLOTS = {
    1980: [
        (7, 'ARZ', 'cardinals', 'crd', '20220514152119'),
        (10, 'IND', 'colts', 'clt', '20250126220236'),
        (23, 'STL', 'rams', 'ram', '20241207021645'),
        (30, 'TEN', 'oilers_titans', 'oti', '20240526054753'),
    ],
}


def sources(season):
    return [{'file': 'roster_%s_%d.html' % (code, season), 'role': 'roster', 'snapshot': snap,
             'page': ls.PFR + '/teams/%s/%d_roster.htm' % (code, season)}
            for _, _, _, code, snap in SLOTS[season]]


def norm(name):
    """Compare names without case, accents, punctuation or spaces ("J. T. Thomas" = "J.T. Thomas")."""
    s = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z]', '', s)


def pfr_roster(path):
    """[{name, pos, number}] from PFR's full roster table (id="roster")."""
    s = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'<table[^>]*id="roster"', s)
    assert m, path
    t = s[m.start():s.find('</table>', m.start())]
    out = []
    for row in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S):
        cells = dict(re.findall(r'<t[hd][^>]*data-stat="([^"]+)"[^>]*>(.*?)</t[hd]>', row, re.S))
        link = re.search(r'<a href="/players/[^"]+">(.*?)</a>', cells.get('player', ''))
        if not link:
            continue
        out.append({'name': ls.strip(link.group(1)), 'pos': ls.strip(cells.get('pos', '')),
                    'number': ls.strip(cells.get('uniform_number', ''))})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--fetch', action='store_true')
    a = ap.parse_args()
    season = a.season
    if season not in SLOTS:
        raise SystemExit('no lineage slots are listed for %d' % season)
    if a.fetch:
        ls.fetch_sources(sources(season), INDEX)
    idx = ls.load_index(INDEX)
    comm = json.load(open(os.path.join(ROOT, 'data', 'eras', 'community_rosters', '%d.json' % season), encoding='utf-8'))
    teams = {t['index']: t for t in comm['teams']}
    rosters = {}
    for src, (_, _, fid, code, _) in zip(sources(season), SLOTS[season]):
        p = os.path.join(CACHE, src['file'])
        if not os.path.exists(p):
            raise SystemExit('missing %s (run with --fetch)' % src['file'])
        rosters[fid] = {'url': ls.wayback(src, idx), 'players': pfr_roster(p), 'file': src['file'],
                        'sha256': idx.get(src['file'], {}).get('sha256')}
    results = []
    for slot, abbr, fid, code, _ in SLOTS[season]:
        t = teams[slot]
        assert t['abbreviation'] == abbr, (slot, t['abbreviation'], abbr)
        slot_players = sorted((p for p in comm['players'] if p['team'] == slot), key=lambda p: p['depth_slot'])
        assert len(slot_players) == t['player_count'] == 53, (slot, len(slot_players))
        own = {norm(r['name']): r for r in rosters[fid]['players']}
        matched, missing = [], []
        for p in slot_players:
            full = '%s %s' % (p['first'], p['last'])
            r = own.get(norm(full))
            if r:
                matched.append({'save': full, 'save_position': p['position'], 'save_jersey': p['jersey'],
                                'pfr': r['name'], 'pfr_position': r['pos'], 'pfr_number': r['number']})
            else:
                missing.append(full)
        control = {}
        for other, ro in rosters.items():
            if other != fid:
                names = {norm(r['name']) for r in ro['players']}
                control[other] = sum(norm('%s %s' % (p['first'], p['last'])) in names for p in slot_players)
        ok = len(matched) >= MIN_MATCHES and all(len(matched) > c for c in control.values())
        status = ('checked:' + rosters[fid]['url']) if ok else 'inferred'
        results.append({
            'slot': slot, 'save_name': '%s %s (%s)' % (t['city'], t['nickname'], abbr), 'franchise': fid,
            'source': rosters[fid]['url'], 'source_file': rosters[fid]['file'], 'source_sha256': rosters[fid]['sha256'],
            'pfr_roster_size': len(rosters[fid]['players']),
            'compared': len(slot_players), 'matched': len(matched),
            'matched_same_number': sum(1 for m in matched if str(m['save_jersey']) == m['pfr_number']),
            'control_matches_other_rosters': control,
            'lineage_status': status,
            'matched_players': matched, 'not_on_pfr_roster': missing,
        })
        print('%-3s slot %2d -> %-14s %2d/%d names on the PFR %d roster (%d same number); control %s: %s'
              % (abbr, slot, fid, len(matched), len(slot_players), season,
                 results[-1]['matched_same_number'], control, 'checked' if ok else 'inferred'))
    out = os.path.join(ROOT, 'data', 'eras', 'league', 'lineage_check_%d.json' % season)
    json.dump({
        'schema': 'nfl2k5_eras_lineage_check/v1',
        'generated_by': 'tools/eras/lineage_check.py',
        'season': season,
        'save': comm['source']['stem'], 'save_sha256': comm['source']['sha256'],
        'rule': ('every slot name is compared (case, accents, punctuation and spaces ignored) with the '
                 "franchise's PFR roster for the season; checked when at least %d match and no other of the "
                 'four rosters matches as many' % MIN_MATCHES),
        'note': 'Names only. A match says the slot holds this franchise; it does not verify ratings, bios '
                'or that the roster is complete (community PS2 roster, unverified).',
        'slots': results,
    }, open(out, 'w', encoding='utf-8'), indent=1)
    print('wrote', os.path.relpath(out, ROOT))


if __name__ == '__main__':
    main()

"""My NFL Eras draft show: each 1981 first-round pick's skin-tone group -> mods/eras/draft/looks_1981.csv

    python -I tools/eras/draftshow/make_draft_looks.py

Columns: pick, player, skin_group (light / medium / dark: the group of coach heads the runtime
picks the player's head from; empty = any), community_skin, source.

The 2K5 roster record carries a skin value 0..21 (Flying Finn's map: bit 0 at +0x18 bit 7, bits
1-4 at +0x19; tone = value & 7, 0 lightest .. 5 darkest, variants at +8 / +16: retail 2004 puts
Favre, Manning, Brady at 16, Urlacher, Bruschi at 0, Ray Lewis and McNabb at 3, Vick at 4, Moss at
5). The community 1981-1983 rosters' records (data/eras/ps2_saves/ros<season>, named per pick by
data/eras/league/drafts/1981_ratings_match.json) were read for that value (the community_skin
column) and it is not set there: George Rogers, Hugh Green and Dennis Smith sit at 0 with Brian
Holloway and Keith Van Horne at 1 next to Lawrence Taylor, so it steers nothing. skin_group is
therefore the editor's own table below (from public photographs of these players; correct it in the
csv if one is wrong), and the hash of the name picks within the group.
"""
import csv, json, os, sys
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
MODTOOLS = os.path.join(ROOT, '.tmp-2k-football-mod-tools')

# the editor's table: 1981 first round, light (white players) / dark; medium is left to the hash
GROUP = {
    1: 'dark', 2: 'dark', 3: 'dark', 4: 'dark', 5: 'dark', 6: 'light', 7: 'dark', 8: 'dark', 9: 'dark', 10: 'dark',
    11: 'light', 12: 'dark', 13: 'dark', 14: 'dark', 15: 'dark', 16: 'dark', 17: 'dark', 18: 'dark', 19: 'light',
    20: 'light', 21: 'dark', 22: 'dark', 23: 'light', 24: 'dark', 25: 'dark', 26: 'dark', 27: 'dark', 28: 'dark',
}


def load_doc(rr, season):
    stem = os.path.join(ROOT, 'data', 'eras', 'ps2_saves', 'ros%d' % season)
    if not os.path.isdir(stem):
        return None
    found = [d for d in os.listdir(stem) if os.path.isfile(os.path.join(stem, d, d))]
    if len(found) != 1:
        return None
    payload = open(os.path.join(stem, found[0], found[0]), 'rb').read()
    return rr.RosterDocument(payload, base=rr.find_block_base(payload), source='ros%d' % season, reference_year=season)


def community_skin(rr, docs, p):
    """(value, source) of the pick's community roster record, or ('', '')."""
    rk = p.get('rookie') or {}
    cands = ([(1981, rk['record_index'])] if rk.get('record_index') is not None else []) + \
            [(s, None) for s in p.get('seasons_rated') or []]
    first, last = p['player'].split()[0].lower(), p['player'].split()[-1].lower()
    for season, idx in cands:
        if season not in docs:
            docs[season] = load_doc(rr, season)
        doc = docs[season]
        if doc is None:
            continue
        prim = [q for q in doc.players if q.pool == 'primary']
        rec = prim[idx] if idx is not None and idx < len(prim) and (prim[idx].first + ' ' + prim[idx].last).lower() == p['player'].lower() else None
        if rec is None:
            rec = next((q for q in prim if q.last.lower() == last and q.first.lower()[:3] == first[:3]), None)
        if rec is not None:
            return rec.record.skin, 'ros%d #%d' % (season, rec.index)
    return '', ''


def main():
    rr = None
    if os.path.isdir(MODTOOLS):
        sys.path.insert(0, MODTOOLS)
        from mod_editor.core import nfl2k5_roster_records as rr
    match = json.load(open(os.path.join(ROOT, 'data', 'eras', 'league', 'drafts', '1981_ratings_match.json')))
    docs, rows = {}, []
    for p in match['picks']:
        if p['round'] != 1:
            continue
        skin, src = community_skin(rr, docs, p) if rr else ('', '')
        rows.append((p['pick'], p['player'], GROUP.get(p['pick'], ''), skin, src))
        print('%2d %-20s %-6s community %-3s %s' % rows[-1])
    out = os.path.join(ROOT, 'mods', 'eras', 'draft', 'looks_1981.csv')
    with open(out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(('pick', 'player', 'skin_group', 'community_skin', 'source'))
        w.writerows(rows)
    print('wrote', out)


if __name__ == '__main__':
    main()

"""My NFL Eras (M14): the player progression table between two seasons, for the Alternate History
rollover (src/nfl2k5_eras_rollover.c).

    python -I tools/eras/make_progression.py [--from 1980] [--to 1981]

For every player of the <from> package (data/eras/league/rosters_<from>.json: PFR id, name, game
position) the table says what the real <to> season holds for him:

    retired   his PFR id is on no team's <to> roster page (data/eras/league/starters_<to>.json,
              every PFR roster of the season, incl. players left off the 45-man packages)
    rated     he is on a <to> roster and the <to> package (rosters_<to>.json) rated him from the
              <to> community save: his 28 <to> ratings are copied from data/eras/runtime/<to>/rosters/
              <team>.csv (the row the package wrote for him)
    unrated   he played in <to> but the <to> package has no record of him for that season (left off
              the 45, or rated from another save's record): he keeps his ratings

No number is invented: the ratings are the community roster's for <to>, the retirements are
PFR's rosters. Writes data/eras/runtime/<to>/progression.csv (first,last,pos_code,team_<from>,
pfr_id,status,team_<to>, 28 ratings) and appends a paragraph to PROVENANCE.txt. Run with python -I.
"""
import argparse, csv, json, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
LEAGUE = os.path.join(ROOT, 'data', 'eras', 'league')
RUNTIME = os.path.join(ROOT, 'data', 'eras', 'runtime')


def jload(p):
    return json.load(open(p, encoding='utf-8'))


def roster_rows(season, team):
    p = os.path.join(RUNTIME, str(season), 'rosters', team + '.csv')
    rows = list(csv.reader(open(p, encoding='latin-1')))
    hdr = rows[0]
    i0 = hdr.index('hand') + 1
    ratings = hdr[i0:hdr.index('photo')]
    assert len(ratings) == 28, ratings
    return hdr, ratings, [dict(zip(hdr, r)) for r in rows[1:]]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--from', dest='a', type=int, default=1980)
    ap.add_argument('--to', dest='b', type=int, default=1981)
    x = ap.parse_args()
    a, b = x.a, x.b
    mem_a = jload(os.path.join(LEAGUE, 'rosters_%d.json' % a))
    mem_b = jload(os.path.join(LEAGUE, 'rosters_%d.json' % b))
    st_b = jload(os.path.join(LEAGUE, 'starters_%d.json' % b))
    on_b = {}
    for fid, t in st_b['teams'].items():
        for p in t['roster']:
            on_b.setdefault(p['pfr_id'], []).append(fid)
    # <to> package: pfr id -> (team, name, position code, rated from the <to> save?)
    rated_b = {}
    for fid, t in mem_b['teams'].items():
        for p in t['players']:
            src = p.get('source')
            rated_b[p['pfr_id']] = (fid, p['source']['first'] if src else None, p['source']['last'] if src else None,
                                    p['position_code'], bool(src) and src['season'] == b)
    rows_b = {fid: roster_rows(b, fid) for fid in mem_b['teams']}
    out, counts = [], {'retired': 0, 'rated': 0, 'unrated': 0}
    ratings_hdr = None
    for fid, t in sorted(mem_a['teams'].items()):
        for p in t['players']:
            src = p.get('source')
            if not src:
                continue
            first, last, pc = src['first'], src['last'], p['position_code']
            pid = p['pfr_id']
            status, team_b, vals = 'unrated', '', [''] * 28
            if pid not in on_b:
                status = 'retired'
            elif pid in rated_b and rated_b[pid][4]:
                tb, f2, l2, pc2, _ = rated_b[pid]
                hdr, ratings_hdr, rows = rows_b[tb]
                match = [r for r in rows if r['first'] == f2 and r['last'] == l2 and int(r['pos']) == pc2]
                if len(match) == 1:
                    status, team_b = 'rated', tb
                    vals = [match[0][k] for k in ratings_hdr]
                else:
                    team_b = tb
            else:
                team_b = on_b[pid][0]
            counts[status] += 1
            out.append([first, last, pc, fid, pid, status, team_b] + vals)
    if ratings_hdr is None:
        ratings_hdr = roster_rows(b, sorted(mem_b['teams'])[0])[1]
    out_dir = os.path.join(RUNTIME, str(b))
    with open(os.path.join(out_dir, 'progression.csv'), 'w', encoding='latin-1', newline='') as f:
        w = csv.writer(f, lineterminator='\n', quoting=csv.QUOTE_NONE)
        w.writerow(['first', 'last', 'pos_code', 'team_%d' % a, 'pfr_id', 'status', 'team_%d' % b] + list(ratings_hdr))
        for r in out:
            w.writerow(r)
    with open(os.path.join(out_dir, 'PROVENANCE.txt'), 'a', encoding='utf-8') as f:
        f.write('\nPROGRESSION (progression.csv, M14; tools/eras/make_progression.py --from %d --to %d)\n' % (a, b))
        f.write('  For every rated player of the %d package (rosters_%d.json, %d players): retired = his PFR id is on no\n' % (a, a, len(out)))
        f.write('  %d roster page (starters_%d.json); rated = the %d package rated him from the %d community save, and his 28\n' % (b, b, b, b))
        f.write('  %d ratings are that row of rosters/<team>.csv; unrated = he played in %d but has no %d record here (he keeps\n' % (b, b, b))
        f.write('  his ratings). Counts: %s. Used by the Alternate History rollover (src/nfl2k5_eras_rollover.c); nothing invented.\n' % counts)
    print('progression %d -> %d: %s -> %s' % (a, b, counts, os.path.relpath(os.path.join(out_dir, 'progression.csv'), ROOT)))


if __name__ == '__main__':
    main()

"""My NFL Eras (M14, TEST ONLY): mark a league's regular season complete with plausible results.

    python -I tools/eras/seed_results.py --league <folder of league.ini> [--season 1980] [--keep-played]
                                         [--through-week N]

Appends to the league's results.csv one `committed` row per unplayed regular-season game, with the
REAL score of that game from data/eras/league/nfl_schedule_<season>.json (PFR), so the standings
are the real ones (ties, tiebreak cases and all) without playing 224 games. Rows already in
results.csv are kept. No box score is written for a seeded game (the Awards screen counts only the
games that have one). This is a test tool for sandbox leagues: it is not part of the game, and it
never touches the real saves.
"""
import argparse, csv, json, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--league', required=True)
    ap.add_argument('--season', type=int, default=1980)
    ap.add_argument('--through-week', type=int, default=16)
    a = ap.parse_args()
    sched = json.load(open(os.path.join(ROOT, 'data', 'eras', 'league', 'nfl_schedule_%d.json' % a.season), encoding='utf-8'))
    res = os.path.join(a.league, 'results.csv')
    have = set()
    if os.path.exists(res):
        for r in csv.DictReader(open(res, encoding='ascii')):
            have.add(r['game_id'])
    else:
        open(res, 'w', encoding='ascii', newline='').write('game_id,home_pts,away_pts,final_period,end_state,status\r\n')
    n = 0
    with open(res, 'a', encoding='ascii', newline='') as f:
        for g in sched['games']:
            if g['week'] > a.through_week or g['game_id'] in have:
                continue
            hp, ap_ = g['home_pts'], g['away_pts']
            f.write('%s,%d,%d,%d,0x00,committed\r\n' % (g['game_id'], hp, ap_, 5 if g.get('overtime') else 4))
            n += 1
    with open(os.path.join(a.league, 'eras.log'), 'a', encoding='utf-8') as f:
        f.write('TEST seed_results.py: %d regular-season results seeded with the real %d scores (through week %d); no box scores\n'
                % (n, a.season, a.through_week))
    print('seeded %d results into %s (%d were already there)' % (n, res, len(have)))


if __name__ == '__main__':
    main()

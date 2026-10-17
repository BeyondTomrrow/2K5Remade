"""The draft show's pick list: data/eras/league/drafts/<year>.json -> mods/eras/draft/picks_<year>.csv.

    python -I tools/eras/draftshow/make_draft_picks.py [year]      (default 1981)

One row per pick: round, pick (overall), Eras team id (mods/eras/teams/<season>/<id>, the season
before the draft), PFR team code, player, position, college. Facts only (Pro-Football-Reference,
cross-checked with Wikipedia in the source file). The game reads it (src/nfl2k5_eras_draftshow.c)
and so can the overlay page.
"""
import csv, json, os, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
# PFR franchise codes of the 1980s -> Eras team ids
TEAM = {
    'ATL': 'falcons', 'BAL': 'colts', 'BUF': 'bills', 'CHI': 'bears', 'CIN': 'bengals', 'CLE': 'browns',
    'DAL': 'cowboys', 'DEN': 'broncos', 'DET': 'lions', 'GNB': 'packers', 'HOU': 'oilers_titans',
    'KAN': 'chiefs', 'MIA': 'dolphins', 'MIN': 'vikings', 'NOR': 'saints', 'NWE': 'patriots',
    'NYG': 'giants', 'NYJ': 'jets', 'OAK': 'raiders', 'PHI': 'eagles', 'PIT': 'steelers', 'RAM': 'rams',
    'SDG': 'chargers', 'SEA': 'seahawks', 'SFO': '49ers', 'STL': 'cardinals', 'TAM': 'buccaneers',
    'WAS': 'washington',
}


def main():
    year = int(sys.argv[1]) if len(sys.argv) > 1 else 1981
    src = os.path.join(ROOT, 'data', 'eras', 'league', 'drafts', '%d.json' % year)
    d = json.load(open(src, encoding='utf-8'))
    out = os.path.join(ROOT, 'mods', 'eras', 'draft', 'picks_%d.csv' % year)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    n = 0
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['round', 'pick', 'team', 'pfr_team', 'player', 'pos', 'college'])
        for p in d['picks']:
            team = TEAM.get(p['team'])
            if not team:
                sys.exit('no Eras team for %s' % p['team'])
            w.writerow([p['round'], p['pick'], team, p['team'], p['player'], p['pos'], p['college']])
            n += 1
    print('wrote %s (%d picks, season %d teams)' % (out, n, year - 1))


if __name__ == '__main__':
    main()

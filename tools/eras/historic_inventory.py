"""My NFL Eras, phase 1: the game's 75 historic teams, decoded.

    python tools/eras/historic_inventory.py [logs/historic_inventory.jsonl]

Input: the in-game dump (NFL2K5_HISTORIC_INVENTORY=1, src/nfl2k5_teams.c),
taken with the game's own historic loader. Output:
    data/eras/historic_teams_2k5.json   every team and player, decoded
    docs/eras/HISTORIC-TEAMS-INVENTORY.md  the summary table
Field meanings: 2K Football Mod Tools' nfl2k5_roster_records.

A player is "named" when the game gives a real person's name; the retail
historic files mostly carry placeholders ("49ers Center"): the team name as
the first name and a position name as the last.
"""
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.tmp-2k-football-mod-tools'))
from mod_editor.core import nfl2k5_roster_records as R  # noqa: E402

POS = R.position_names('retail')
LONG = {R.position_long_name(c).lower() for c in range(len(POS))} | {
    'center', 'cornerback', 'free safety', 'strong safety', 'guard', 'tackle', 'defensive end',
    'defensive tackle', 'linebacker', 'outside linebacker', 'inside linebacker', 'middle linebacker',
    'quarterback', 'halfback', 'running back', 'fullback', 'tight end', 'wide receiver', 'kicker',
    'punter', 'offensive tackle', 'offensive guard', 'safety'}


def placeholder(first, last, nickname):
    return last.lower() in LONG or (nickname and first.lower() == nickname.lower())


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'logs' / 'historic_inventory.jsonl'
    teams = []
    for line in src.read_text(encoding='utf-8').splitlines():
        t = json.loads(line)
        if not t.get('loaded'):
            teams.append(dict(entry=t['entry'], loaded=False)); continue
        city_full = t['city']
        nickname = t['franchise']
        players = []
        for p in t['players']:
            v = R.decode_record(bytes.fromhex(p['rec']))
            raw_year = v['birth_year_low'] | (v['birth_year_high'] << 3)
            pos = v['position']
            players.append(dict(
                first=p['first'], last=p['last'],
                position=POS[pos] if pos < len(POS) else f'#{pos}',
                jersey=v['jersey'], height_in=v['height'], weight_lb=v['weight_raw'] + 150,
                years_pro=v['years_pro'],
                birth=f"{R.decode_birth_year(raw_year, t['year'])}-{v['birth_month']:02d}-{v['birth_day']:02d}",
                ratings={k: v[k] for k in R.RATING_BYTE_ORDER},
                named=not placeholder(p['first'], p['last'], nickname)))
        abbr = t['abbreviation'] or ''
        teams.append(dict(
            id=f"{abbr[:-2] if abbr[-2:].isdigit() else abbr}_{t['year']}",
            entry=t['entry'], season=t['year'], variant=t['variant'], franchise=nickname,
            display=city_full, abbreviation=abbr, asset_code=t['code'], scheme=t['scheme'],
            players=players, loaded=True,
            named_players=sum(p['named'] for p in players),
            positions=dict(Counter(p['position'] for p in players))))
    out = ROOT / 'data' / 'eras'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'historic_teams_2k5.json').write_text(json.dumps(dict(
        schema='nfl2k5_eras_historic_teams/v1',
        source='in-game dump with the retail historic loader (src/nfl2k5_teams.c historic_inventory)',
        teams=teams), indent=1), encoding='utf-8')
    rows = sorted((t for t in teams if t['loaded']), key=lambda t: (t['season'], t['id']))
    md = ['# NFL 2K5 historic teams: inventory', '',
          f'{len(rows)} of {len(teams)} entries load with the game\'s own historic loader '
          '(Create a Team "Load Historic Team" / Quick Game Team Select). Source: an in-game dump, '
          '`NFL2K5_HISTORIC_INVENTORY=1`; decoded by `tools/eras/historic_inventory.py` into '
          '`data/eras/historic_teams_2k5.json`.', '',
          '"Named" counts players whose name is a real person\'s; the rest are placeholders the retail '
          'files carry ("49ers Center"). Ratings, numbers, heights and weights are present for every '
          'player but are the game\'s own values, not verified history.', '',
          '| ID | Season | Team | Players | Named | Uniform variant | Exhibition | Franchise |',
          '|---|---|---|---|---|---|---|---|']
    for t in rows:
        md.append(f"| {t['id']} | {t['season']} | {t['display']} | {len(t['players'])} | "
                  f"{t['named_players']} | {t['variant']} | yes (spare slot) | no (not in the 32-team league) |")
    seasons = Counter(t['season'] for t in rows)
    md += ['', '## Seasons covered', '', ', '.join(f'{y} ({n})' for y, n in sorted(seasons.items())), '']
    named = sum(t['named_players'] for t in rows); total = sum(len(t['players']) for t in rows)
    md += [f'Players: {total}; named: {named}; placeholders: {total - named}.', '']
    (ROOT / 'docs' / 'eras').mkdir(parents=True, exist_ok=True)
    (ROOT / 'docs' / 'eras' / 'HISTORIC-TEAMS-INVENTORY.md').write_text('\n'.join(md), encoding='utf-8')
    print(f'{len(rows)} teams, {total} players, {named} named; seasons {min(seasons)}..{max(seasons)}')


if __name__ == '__main__':
    main()

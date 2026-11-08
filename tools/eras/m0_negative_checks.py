"""My NFL Eras M0: negative checks. Each tampering below must make a T0.1/T0.2 check fail.

    python -I tools/eras/m0_negative_checks.py

The schedule cases change an in-memory copy of nfl_schedule_1980.json; the package cases
change a copy of data/eras/runtime/1980 in a temporary folder (deleted afterwards). Nothing
under data/ is modified. Exit status 1 if any tampering goes unnoticed.
"""
import copy, json, os, shutil, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import league_schedule as ls  # noqa: E402
import make_runtime_package as mp  # noqa: E402

MISSED, CAUGHT = [], []


def expect_fail(label, fn):
    try:
        fn()
    except AssertionError as e:
        print('fails as it should:', label, '->', str(e)[:110])
        CAUGHT.append(label)
        return
    print('!! NOT CAUGHT:', label)
    MISSED.append(label)


# ---- schedule (T0.2)
info, teams = ls.season_teams(1980)
doc = json.load(open(os.path.join(ls.LEAGUE, 'nfl_schedule_1980.json'), encoding='utf-8'))
games = doc['games']
text, _ = ls.book_text(1980)
book = ls.book_standings(text, 1980, teams)
pts = ls.pfr_standings_points(os.path.join(ls.CACHE, 'years_1980.html'))
ls.check_games(1980, info, teams, games, book, pts)  # untouched: passes

g = copy.deepcopy(games)
g[10]['home_pts'] += 1
expect_fail('one score +1', lambda: ls.check_games(1980, info, teams, g, book, pts))

g2 = copy.deepcopy(games)
g2[10]['home_pts'], g2[10]['away_pts'] = g2[10]['away_pts'], g2[10]['home_pts']
expect_fail('one result reversed (records only, no PF/PA sources)',
            lambda: ls.check_games(1980, info, teams, g2, None, None))

g3 = copy.deepcopy(games)[:-1]
expect_fail('one game dropped', lambda: ls.check_games(1980, info, teams, g3, None, None))

# home and away swapped with their scores: every total stays the same, only the per-game
# Wikipedia reading can see it
g4 = copy.deepcopy(games)
x = g4[0]
x['home'], x['away'], x['home_pts'], x['away_pts'] = x['away'], x['home'], x['away_pts'], x['home_pts']
x['game_id'] = ls.game_id(1980, x)
ls.check_games(1980, info, teams, g4, book, pts)
expect_fail('home/away swapped (Wikipedia check)', lambda: ls.wiki_check(1980, teams, g4))

g5 = copy.deepcopy(games)
g5[20]['week'] = 1 if g5[20]['week'] != 1 else 2
expect_fail('a game moved to another week', lambda: ls.check_games(1980, info, teams, g5, book, pts))

# ---- runtime package (T0.1)
tmp = tempfile.mkdtemp()
src = mp.runtime_dir(1980)
real_dir = mp.runtime_dir


def tamper(fn):
    d = os.path.join(tmp, 'pkg')
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(src, d)
    fn(d)
    mp.runtime_dir = lambda season: d
    try:
        mp.check(1980)
    finally:
        mp.runtime_dir = real_dir


def edit(path, f):
    s = open(path, encoding='latin-1').read()
    t = f(s)
    assert t != s, ('tamper did not change', path)
    open(path, 'w', encoding='latin-1', newline='').write(t)


colts = lambda d: os.path.join(d, 'rosters', 'colts.csv')
teams_csv = lambda d: os.path.join(d, 'teams.csv')
# the first roster row of colts.csv (whoever it is): its first 11 fields, i.e. through the first rating
_row = open(colts(src), encoding='latin-1').read().split('\n')[1].split(',')
first = ','.join(_row[:11]) + ','
first_100 = ','.join(_row[:10] + ['100']) + ','
name_pos = ','.join(_row[:3]) + ','
name_pos17 = ','.join(_row[:2] + ['17']) + ','
try:
    expect_fail('rating 100', lambda: tamper(lambda d: edit(colts(d), lambda s: s.replace(first, first_100, 1))))
    expect_fail('one row dropped', lambda: tamper(lambda d: edit(colts(d), lambda s: '\n'.join(s.split('\n')[:-2]) + '\n')))

    # M8: the smallest team one row below the game's 42-player minimum, roster and depth file
    # dropped together (so only the row-count range can see it)
    def below_min(d):
        sizes = {f: len(open(os.path.join(d, 'rosters', f), encoding='latin-1').read().split('\n')) - 2
                 for f in os.listdir(os.path.join(d, 'rosters')) if f.endswith('.csv')}
        f = min(sizes, key=sizes.get)
        for p in (os.path.join(d, 'rosters', f), os.path.join(d, 'depth', f)):
            n = sizes[f] - (mp.ROWS_MIN - 1)
            edit(p, lambda s: '\n'.join(s.split('\n')[:-1 - n]) + '\n')
    expect_fail('a team below 42 rows', lambda: tamper(below_min))

    # M8: a roster left with one T (the others recoded G, row count unchanged): the staging
    # minimum T 2 must catch it
    def one_tackle(d):
        lines = open(colts(d), encoding='latin-1').read().split('\n')
        ts = [n for n, ln in enumerate(lines[1:-1], 1) if ln.split(',')[2] == '14']
        for n in ts[1:]:
            v = lines[n].split(',')
            v[2] = '13'
            lines[n] = ','.join(v)
        open(colts(d), 'w', encoding='latin-1', newline='').write('\n'.join(lines))
    expect_fail('a team with one T', lambda: tamper(one_tackle))
    expect_fail('position 17', lambda: tamper(lambda d: edit(colts(d), lambda s: s.replace(name_pos, name_pos17, 1))))
    expect_fail('template_entry 75', lambda: tamper(lambda d: edit(teams_csv(d), lambda s: s.replace(
        'AFC East,24,BAL_1970', 'AFC East,75,BAL_1970'))))
    expect_fail('team moved to another division', lambda: tamper(lambda d: edit(teams_csv(d), lambda s: s.replace(
        'colts,Baltimore,Colts,BAL,AFC,AFC East', 'colts,Baltimore,Colts,BAL,AFC,AFC West'))))
    def bills_style(s):   # M9: the Bills back in the template's red 1987-2002 helmet (style 2)
        lines = s.split('\n')
        n = next(i for i, l in enumerate(lines) if l.startswith('bills,'))
        lines[n] = lines[n].rsplit(',', 1)[0] + ',2'
        return '\n'.join(lines)
    expect_fail('Bills style 2 instead of team_looks.json 4', lambda: tamper(lambda d: edit(teams_csv(d), bills_style)))
    expect_fail('PROVENANCE.txt missing', lambda: tamper(lambda d: os.remove(os.path.join(d, 'PROVENANCE.txt'))))
    if os.path.exists(mp.membership_path(1980)):
        # membership mode: a player who is not on the team's real roster (renamed in both the
        # roster and the depth file, so only the membership check can see it)
        renamed = lambda s: s.replace(','.join(_row[:2]) + ',', 'Zed,Nobody,', 1)
        expect_fail('player not in the membership file', lambda: tamper(lambda d: (
            edit(colts(d), renamed), edit(os.path.join(d, 'depth', 'colts.csv'), renamed))))
        # the starter of a position moved down the depth chart
        def demote(d):
            p = os.path.join(d, 'depth', 'colts.csv')
            lines = open(p, encoding='latin-1').read().split('\n')
            qbs = [n for n, ln in enumerate(lines[1:-1], 1) if ln.split(',')[3] == '0']
            a, b = qbs[0], qbs[1]
            va, vb = lines[a].split(','), lines[b].split(',')
            va[5], vb[5] = vb[5], va[5]
            lines[a], lines[b] = ','.join(va), ','.join(vb)
            open(p, 'w', encoding='latin-1', newline='').write('\n'.join(lines))
        expect_fail('QB starter swapped with his backup', lambda: tamper(demote))
    tamper(lambda d: None)
    print('untouched copy passes')
finally:
    shutil.rmtree(tmp)
print('negative checks: %d caught, %d missed' % (len(CAUGHT), len(MISSED)))
sys.exit(1 if MISSED else 0)

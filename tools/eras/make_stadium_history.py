"""NFL home stadiums 1966-2025 per franchise, and the NFL 2K5 stadium each maps to.

    python tools/eras/make_stadium_history.py

Writes data/eras/league/stadiums_1966_2025.json and docs/eras/research/STADIUMS.md.

Sources (fetched 2026-10-16): Wikipedia "List of former National Football League
stadiums" and "List of current National Football League stadiums" (seasons, temporary
venues); Wikipedia "Texas Stadium" / WTTW (Bears' last Wrigley game 1970-12-13, first
Soldier Field game 1971-09-19, Texas Stadium's first game 1971-10-24); Foxboro Stadium /
History of the New England Patriots (Alumni Stadium 1969, Harvard Stadium 1970, Schaefer
Stadium 1971); Britannica Shea Stadium / Giants Stadium (Jets at Shea 1964-1983, Giants at
Shea 1975, Giants Stadium from 1976-10-10). Rows marked check=true come from general
knowledge not yet confirmed against a fetched source.

In-game mapping: retail NFL 2K5 ships the 2004 venues (names as the Create-a-Team picker
shows them), no classic stadiums. 'exact' = the same building is in the game (its 2004
look; renovations differ). 'standin' = the closest retail venue by roof type, surface and
setting, chosen so a dome never stands in for an open-air park.
"""
import json
from pathlib import Path

ROOT = Path(r'E:\NFL2K5-PC')

# (franchise, stadium, city, first, last, roof, surface_era, ingame, match, note, check)
# last = None means still in use in 2025. 'roof': open / dome / retractable / skylight.
R = [
 ('bills', 'War Memorial Stadium', 'Buffalo, NY', 1960, 1972, 'open', 'grass->turf', 'RALPH WILSON STADIUM', 'standin', 'the "Rockpile"', False),
 ('bills', 'Rich Stadium / Ralph Wilson Stadium / New Era Field / Bills Stadium / Highmark Stadium (old)', 'Orchard Park, NY', 1973, 2025, 'open', 'turf', 'RALPH WILSON STADIUM', 'exact', 'replaced by the new Highmark Stadium in 2026', False),
 ('dolphins', 'Miami Orange Bowl', 'Miami, FL', 1966, 1986, 'open', 'turf->grass', 'PRO PLAYER STADIUM', 'standin', '', False),
 ('dolphins', 'Joe Robbie Stadium / Pro Player / Dolphin / Sun Life / Hard Rock Stadium', 'Miami Gardens, FL', 1987, None, 'open', 'grass', 'PRO PLAYER STADIUM', 'exact', '', False),
 ('patriots', 'Fenway Park', 'Boston, MA', 1963, 1968, 'open', 'grass', 'GILLETTE STADIUM', 'standin', '', True),
 ('patriots', 'Alumni Stadium (Boston College)', 'Chestnut Hill, MA', 1969, 1969, 'open', 'grass', 'GILLETTE STADIUM', 'standin', '', False),
 ('patriots', 'Harvard Stadium', 'Boston, MA', 1970, 1970, 'open', 'grass', 'GILLETTE STADIUM', 'standin', 'stopgap while Schaefer Stadium was built', False),
 ('patriots', 'Schaefer / Sullivan / Foxboro Stadium', 'Foxborough, MA', 1971, 2001, 'open', 'turf->grass', 'GILLETTE STADIUM', 'standin', 'renamed Sullivan 1983, Foxboro 1990; Gillette was built next door', False),
 ('patriots', 'Gillette Stadium', 'Foxborough, MA', 2002, None, 'open', 'grass->turf', 'GILLETTE STADIUM', 'exact', '', False),
 ('jets', 'Shea Stadium', 'Flushing, NY', 1964, 1983, 'open', 'grass', 'JETS STADIUM', 'standin', '', False),
 ('jets', 'Giants Stadium', 'East Rutherford, NJ', 1984, 2009, 'open', 'turf', 'JETS STADIUM', 'exact', 'the game ships the building twice (Giants / Jets dressing)', False),
 ('jets', 'MetLife Stadium', 'East Rutherford, NJ', 2010, None, 'open', 'turf', 'JETS STADIUM', 'standin', '', False),
 ('colts', 'Memorial Stadium', 'Baltimore, MD', 1953, 1983, 'open', 'grass', 'M&T BANK STADIUM', 'standin', 'open-air Baltimore stand-in', False),
 ('colts', 'Hoosier Dome / RCA Dome', 'Indianapolis, IN', 1984, 2007, 'dome', 'turf', 'RCA DOME', 'exact', '', False),
 ('colts', 'Lucas Oil Stadium', 'Indianapolis, IN', 2008, None, 'retractable', 'turf', 'RCA DOME', 'standin', '', False),
 ('browns', 'Cleveland Municipal Stadium', 'Cleveland, OH', 1946, 1995, 'open', 'grass', 'CLEVELAND STADIUM', 'standin', 'same lakefront site as the 1999 stadium in the game', False),
 ('browns', 'Cleveland Browns Stadium / FirstEnergy / Huntington Bank Field', 'Cleveland, OH', 1999, None, 'open', 'grass', 'CLEVELAND STADIUM', 'exact', 'franchise inactive 1996-1998', False),
 ('ravens', 'Memorial Stadium', 'Baltimore, MD', 1996, 1997, 'open', 'grass', 'M&T BANK STADIUM', 'standin', '', False),
 ('ravens', 'PSINet / Ravens / M&T Bank Stadium', 'Baltimore, MD', 1998, None, 'open', 'turf->grass', 'M&T BANK STADIUM', 'exact', '', False),
 ('steelers', 'Pitt Stadium', 'Pittsburgh, PA', 1958, 1969, 'open', 'grass->turf', 'HEINZ FIELD', 'standin', '', False),
 ('steelers', 'Three Rivers Stadium', 'Pittsburgh, PA', 1970, 2000, 'open', 'turf', 'HEINZ FIELD', 'standin', 'same riverfront site', False),
 ('steelers', 'Heinz Field / Acrisure Stadium', 'Pittsburgh, PA', 2001, None, 'open', 'grass', 'HEINZ FIELD', 'exact', '', False),
 ('bengals', 'Nippert Stadium', 'Cincinnati, OH', 1968, 1969, 'open', 'grass', 'PAUL BROWN STADIUM', 'standin', 'expansion team 1968', True),
 ('bengals', 'Riverfront Stadium / Cinergy Field', 'Cincinnati, OH', 1970, 1999, 'open', 'turf', 'PAUL BROWN STADIUM', 'standin', '', False),
 ('bengals', 'Paul Brown Stadium / Paycor Stadium', 'Cincinnati, OH', 2000, None, 'open', 'grass->turf', 'PAUL BROWN STADIUM', 'exact', '', False),
 ('oilers_titans', 'Rice Stadium', 'Houston, TX', 1965, 1967, 'open', 'grass', 'RELIANT STADIUM', 'standin', '', True),
 ('oilers_titans', 'Astrodome', 'Houston, TX', 1968, 1996, 'dome', 'turf', 'RELIANT STADIUM', 'standin', 'list gives 1970-1996 from the merger; the Oilers moved in 1968', True),
 ('oilers_titans', 'Liberty Bowl Memorial Stadium', 'Memphis, TN', 1997, 1997, 'open', 'grass', 'TITANS COLISEUM', 'standin', 'temporary (Tennessee Oilers)', False),
 ('oilers_titans', 'Vanderbilt Stadium', 'Nashville, TN', 1998, 1998, 'open', 'grass', 'TITANS COLISEUM', 'standin', 'temporary', False),
 ('oilers_titans', 'Adelphia Coliseum / LP Field / Nissan Stadium', 'Nashville, TN', 1999, None, 'open', 'grass->turf', 'TITANS COLISEUM', 'exact', '', False),
 ('texans', 'Reliant Stadium / NRG Stadium', 'Houston, TX', 2002, None, 'retractable', 'grass->turf', 'RELIANT STADIUM', 'exact', 'expansion team 2002', False),
 ('jaguars', 'Jacksonville Municipal / Alltel / EverBank Stadium', 'Jacksonville, FL', 1995, None, 'open', 'grass', 'ALLTEL STADIUM', 'exact', 'expansion team 1995', False),
 ('broncos', 'Bears Stadium / Mile High Stadium', 'Denver, CO', 1960, 2000, 'open', 'grass', 'INVESCO FIELD', 'standin', 'Invesco was built next to it', False),
 ('broncos', 'Invesco Field at Mile High / Empower Field', 'Denver, CO', 2001, None, 'open', 'grass', 'INVESCO FIELD', 'exact', '', False),
 ('chiefs', 'Kansas City Municipal Stadium', 'Kansas City, MO', 1963, 1971, 'open', 'grass', 'ARROWHEAD STADIUM', 'standin', '', False),
 ('chiefs', 'Arrowhead Stadium', 'Kansas City, MO', 1972, None, 'open', 'turf->grass', 'ARROWHEAD STADIUM', 'exact', '', False),
 ('raiders', 'Oakland-Alameda County Coliseum', 'Oakland, CA', 1966, 1981, 'open', 'grass', 'NETWORK ASSOCIATES', 'exact', '', False),
 ('raiders', 'Los Angeles Memorial Coliseum', 'Los Angeles, CA', 1982, 1994, 'open', 'grass', 'QUALCOMM STADIUM', 'standin', 'large open bowl stand-in', False),
 ('raiders', 'Oakland-Alameda County Coliseum (Network Associates, McAfee, ...)', 'Oakland, CA', 1995, 2019, 'open', 'grass', 'NETWORK ASSOCIATES', 'exact', 'Mt. Davis added 1996 (the game shows it)', False),
 ('raiders', 'Allegiant Stadium', 'Paradise, NV', 2020, None, 'skylight', 'grass', 'RELIANT STADIUM', 'standin', 'closed-roof stand-in (the mod tools have an Allegiant model)', False),
 ('chargers', 'San Diego Stadium / Jack Murphy / Qualcomm Stadium', 'San Diego, CA', 1967, 2016, 'open', 'grass', 'QUALCOMM STADIUM', 'exact', '', False),
 ('chargers', 'StubHub Center / Dignity Health Sports Park', 'Carson, CA', 2017, 2019, 'open', 'grass', 'QUALCOMM STADIUM', 'standin', 'temporary, 27,000 seats', False),
 ('chargers', 'SoFi Stadium', 'Inglewood, CA', 2020, None, 'skylight', 'turf', 'RELIANT STADIUM', 'standin', 'closed-roof stand-in', False),
 ('seahawks', 'Kingdome', 'Seattle, WA', 1976, 1999, 'dome', 'turf', 'EDWARD JONES DOME', 'standin', 'expansion team 1976', False),
 ('seahawks', 'Husky Stadium', 'Seattle, WA', 2000, 2001, 'open', 'turf', 'QWEST FIELD', 'standin', 'temporary', False),
 ('seahawks', 'Seahawks Stadium / Qwest Field / CenturyLink / Lumen Field', 'Seattle, WA', 2002, None, 'open', 'turf', 'QWEST FIELD', 'exact', '', False),
 ('cowboys', 'Cotton Bowl', 'Dallas, TX', 1960, 1971, 'open', 'grass', 'TEXAS STADIUM', 'standin', 'first 1971 home games here; Texas Stadium opened 1971-10-24', False),
 ('cowboys', 'Texas Stadium', 'Irving, TX', 1971, 2008, 'open roof hole', 'turf', 'TEXAS STADIUM', 'exact', 'from 1971-10-24', False),
 ('cowboys', 'Cowboys Stadium / AT&T Stadium', 'Arlington, TX', 2009, None, 'retractable', 'turf', 'RELIANT STADIUM', 'standin', 'retractable-roof stand-in', False),
 ('giants', 'Yankee Stadium', 'Bronx, NY', 1956, 1973, 'open', 'grass', 'GIANTS STADIUM', 'standin', 'left mid-1973', False),
 ('giants', 'Yale Bowl', 'New Haven, CT', 1973, 1974, 'open', 'grass', 'GIANTS STADIUM', 'standin', 'temporary: last 1973 games and 1974', False),
 ('giants', 'Shea Stadium', 'Flushing, NY', 1975, 1975, 'open', 'grass', 'JETS STADIUM', 'standin', 'temporary', False),
 ('giants', 'Giants Stadium', 'East Rutherford, NJ', 1976, 2009, 'open', 'turf', 'GIANTS STADIUM', 'exact', 'from 1976-10-10 (week 5)', False),
 ('giants', 'MetLife Stadium', 'East Rutherford, NJ', 2010, None, 'open', 'turf', 'GIANTS STADIUM', 'standin', '', False),
 ('eagles', 'Franklin Field', 'Philadelphia, PA', 1958, 1970, 'open', 'grass->turf', 'LINCOLN FINANCIAL FIELD', 'standin', '', False),
 ('eagles', 'Veterans Stadium', 'Philadelphia, PA', 1971, 2002, 'open', 'turf', 'LINCOLN FINANCIAL FIELD', 'standin', '', False),
 ('eagles', 'Lincoln Financial Field', 'Philadelphia, PA', 2003, None, 'open', 'grass', 'LINCOLN FINANCIAL FIELD', 'exact', '', False),
 ('washington', 'D.C. Stadium / RFK Stadium', 'Washington, DC', 1961, 1996, 'open', 'grass->turf->grass', 'WASHINGTON FIELD', 'standin', '', False),
 ('washington', 'Jack Kent Cooke / FedExField / Northwest Stadium', 'Landover, MD', 1997, None, 'open', 'grass', 'WASHINGTON FIELD', 'exact', '', False),
 ('cardinals', 'Busch Memorial Stadium', 'St. Louis, MO', 1966, 1987, 'open', 'grass->turf', 'ARROWHEAD STADIUM', 'standin', 'open-air Midwest stand-in', False),
 ('cardinals', 'Sun Devil Stadium', 'Tempe, AZ', 1988, 2005, 'open', 'grass', 'ARIZONA STADIUM', 'exact', '', False),
 ('cardinals', 'University of Phoenix / State Farm Stadium', 'Glendale, AZ', 2006, None, 'retractable', 'grass', 'RELIANT STADIUM', 'standin', 'retractable-roof stand-in', False),
 ('bears', 'Wrigley Field', 'Chicago, IL', 1921, 1970, 'open', 'grass', 'CHICAGO FIELD', 'standin', 'last game 1970-12-13', False),
 ('bears', 'Soldier Field', 'Chicago, IL', 1971, 2001, 'open', 'turf->grass', 'CHICAGO FIELD', 'standin', 'pre-2003 bowl; the game shows the renovated one', False),
 ('bears', 'Memorial Stadium (Champaign)', 'Champaign, IL', 2002, 2002, 'open', 'grass', 'CHICAGO FIELD', 'standin', 'temporary', False),
 ('bears', 'Soldier Field (renovated)', 'Chicago, IL', 2003, None, 'open', 'grass', 'CHICAGO FIELD', 'exact', '', False),
 ('lions', 'Tiger Stadium', 'Detroit, MI', 1938, 1974, 'open', 'grass', 'CLEVELAND STADIUM', 'standin', 'open-air Great Lakes stand-in (Ford Field is a dome)', False),
 ('lions', 'Pontiac Silverdome', 'Pontiac, MI', 1975, 2001, 'dome', 'turf', 'FORD FIELD', 'standin', '', False),
 ('lions', 'Ford Field', 'Detroit, MI', 2002, None, 'dome', 'turf', 'FORD FIELD', 'exact', '', False),
 ('packers', 'Lambeau Field (City Stadium)', 'Green Bay, WI', 1957, None, 'open', 'grass', 'LAMBEAU FIELD', 'exact', 'plus 1-3 games a year at Milwaukee County Stadium through 1994', False),
 ('vikings', 'Metropolitan Stadium', 'Bloomington, MN', 1961, 1981, 'open', 'grass', 'LAMBEAU FIELD', 'standin', 'open-air northern stand-in', False),
 ('vikings', 'Hubert H. Humphrey Metrodome', 'Minneapolis, MN', 1982, 2013, 'dome', 'turf', 'H. H. H. METRODOME', 'exact', '', False),
 ('vikings', 'TCF Bank Stadium', 'Minneapolis, MN', 2014, 2015, 'open', 'turf', 'LAMBEAU FIELD', 'standin', 'temporary', False),
 ('vikings', 'U.S. Bank Stadium', 'Minneapolis, MN', 2016, None, 'skylight', 'turf', 'H. H. H. METRODOME', 'standin', '', False),
 ('buccaneers', 'Tampa Stadium', 'Tampa, FL', 1976, 1997, 'open', 'grass', 'TAMPA BAY STADIUM', 'standin', 'expansion team 1976; Raymond James was built next door', False),
 ('buccaneers', 'Raymond James Stadium', 'Tampa, FL', 1998, None, 'open', 'grass', 'TAMPA BAY STADIUM', 'exact', '', False),
 ('falcons', 'Atlanta Stadium / Atlanta-Fulton County Stadium', 'Atlanta, GA', 1966, 1991, 'open', 'grass', 'ARROWHEAD STADIUM', 'standin', 'open-air stand-in', False),
 ('falcons', 'Georgia Dome', 'Atlanta, GA', 1992, 2016, 'dome', 'turf', 'GEORGIA DOME', 'exact', '', False),
 ('falcons', 'Mercedes-Benz Stadium', 'Atlanta, GA', 2017, None, 'retractable', 'turf', 'GEORGIA DOME', 'standin', 'the mod tools have a Mercedes-Benz model', False),
 ('panthers', 'Memorial Stadium (Clemson)', 'Clemson, SC', 1995, 1995, 'open', 'grass', 'B OF A STADIUM', 'standin', 'temporary, first season', False),
 ('panthers', 'Ericsson / Bank of America Stadium', 'Charlotte, NC', 1996, None, 'open', 'grass->turf', 'B OF A STADIUM', 'exact', '', False),
 ('saints', 'Tulane Stadium', 'New Orleans, LA', 1967, 1974, 'open', 'grass->turf', 'LOUISIANA SUPER DOME', 'standin', 'expansion team 1967; open-air', False),
 ('saints', 'Louisiana Superdome / Caesars Superdome', 'New Orleans, LA', 1975, None, 'dome', 'turf', 'LOUISIANA SUPER DOME', 'exact', '2005: displaced by Hurricane Katrina', False),
 ('saints', 'Alamodome / Tiger Stadium (LSU) / Giants Stadium', 'San Antonio / Baton Rouge / East Rutherford', 2005, 2005, 'mixed', 'mixed', 'LOUISIANA SUPER DOME', 'standin', '2005 only: home games split after Katrina (one at Giants Stadium)', True),
 ('rams', 'Los Angeles Memorial Coliseum', 'Los Angeles, CA', 1946, 1979, 'open', 'grass', 'QUALCOMM STADIUM', 'standin', '', False),
 ('rams', 'Anaheim Stadium', 'Anaheim, CA', 1980, 1994, 'open', 'grass', 'QUALCOMM STADIUM', 'standin', '', False),
 ('rams', 'Busch Memorial Stadium', 'St. Louis, MO', 1995, 1995, 'open', 'turf', 'EDWARD JONES DOME', 'standin', 'part of 1995 while the dome was finished', False),
 ('rams', 'Trans World Dome / Edward Jones Dome', 'St. Louis, MO', 1995, 2015, 'dome', 'turf', 'EDWARD JONES DOME', 'exact', '', False),
 ('rams', 'Los Angeles Memorial Coliseum', 'Los Angeles, CA', 2016, 2019, 'open', 'grass', 'QUALCOMM STADIUM', 'standin', 'temporary', False),
 ('rams', 'SoFi Stadium', 'Inglewood, CA', 2020, None, 'skylight', 'turf', 'RELIANT STADIUM', 'standin', 'closed-roof stand-in', False),
 ('49ers', 'Kezar Stadium', 'San Francisco, CA', 1946, 1970, 'open', 'grass', 'SAN FRANCISCO PARK', 'standin', '', False),
 ('49ers', 'Candlestick Park / 3Com Park / Monster Park', 'San Francisco, CA', 1971, 2013, 'open', 'grass', 'SAN FRANCISCO PARK', 'exact', '', False),
 ('49ers', "Levi's Stadium", 'Santa Clara, CA', 2014, None, 'open', 'grass', 'SAN FRANCISCO PARK', 'standin', '', False),
]

def main():
    fr = {}
    for (f, name, city, a, b, roof, surf, game, match, note, check) in R:
        fr.setdefault(f, []).append(dict(stadium=name, city=city, first=a, last=b, roof=roof, surface=surf,
                                         ingame=game, ingame_match=match, note=note, needs_check=check))
    for f, rows in fr.items():
        rows.sort(key=lambda r: (r['first'], r['last'] or 9999))
        for i, r in enumerate(rows):
            nxt = rows[i + 1]['stadium'] if i + 1 < len(rows) else None
            r['moved_to'] = nxt
    out = dict(schema='nfl2k5_eras_stadiums/v1', seasons='1966-2025',
               sources=['https://en.wikipedia.org/wiki/List_of_former_National_Football_League_stadiums',
                        'https://en.wikipedia.org/wiki/List_of_current_National_Football_League_stadiums',
                        'https://en.wikipedia.org/wiki/Texas_Stadium',
                        'https://interactive.wttw.com/node/441',
                        'https://en.wikipedia.org/wiki/Foxboro_Stadium',
                        'https://en.wikipedia.org/wiki/History_of_the_New_England_Patriots',
                        'https://www.britannica.com/place/Shea-Stadium',
                        'https://en.wikipedia.org/wiki/Giants_Stadium'],
               franchises=fr)
    p = ROOT / 'data' / 'eras' / 'league' / 'stadiums_1966_2025.json'
    p.write_text(json.dumps(out, indent=1), encoding='utf-8')
    md = ['# NFL home stadiums, 1966-2025', '',
          'Per franchise: every primary home stadium, the seasons used, where the team went next, and the',
          'NFL 2K5 venue an Eras game uses (`exact` = the same building is in the game, as it looked in 2004;',
          '`standin` = the closest retail venue by roof, surface and setting). Data: `data/eras/league/stadiums_1966_2025.json`',
          '(`tools/eras/make_stadium_history.py`). Rows marked * still need a check against a fetched source.', '']
    for f in sorted(fr):
        md += [f'## {f}', '', '| Seasons | Stadium | City | Then moved to | In game |', '|---|---|---|---|---|']
        for r in fr[f]:
            yrs = f"{r['first']}-{r['last'] if r['last'] else 'now'}"
            md.append(f"| {yrs}{' *' if r['needs_check'] else ''} | {r['stadium']} | {r['city']} | {r['moved_to'] or '-'} | "
                      f"{r['ingame']} ({r['ingame_match']}){': ' + r['note'] if r['note'] else ''} |")
        md.append('')
    (ROOT / 'docs' / 'eras' / 'research' / 'STADIUMS.md').write_text('\n'.join(md), encoding='utf-8')
    print(len(R), 'stadium spells,', len(fr), 'franchises')

if __name__ == '__main__':
    main()

# M6 results: default depth charts from the real 1980 starters

2026-10-16. Data work only. `src/` was not touched, nothing was built, the game was not run, and
nothing was committed.

## What was wrong

`data/eras/runtime/1980/depth/*.csv` (the M6 seed, `docs/eras/results/M6-depth.md`) copied the
community save's stored depth fields. On 6 teams those put obvious backups first, 67 starters in
total. Some examples: Barisich over Montana, Humm over Stabler, Francis over Ray Guy, Manucci over
Ferguson and Dean over Simms.

The community roster is also not a 1980 roster. Across the 28 teams, only 956 of 1,484 names
(64%) are on that team's PFR 1980 roster. It looks like a late-1970s roster:
* Ken Stabler is a Raider, but he started all 16 games for Houston in 1980.
* Dan Pastorini is an Oiler, but he started 5 games for the Raiders in 1980.
* Roger Staubach is the Cowboys' starter, but he had retired after 1979.

## What was done

1. **New verified input: `data/eras/league/starters_1980.json`.** It is built by the new
   `build/eras/league_starters.py` and holds, for all 28 teams:
   * the PFR roster table (player id, name, position, number, G and GS);
   * PFR's Starters table;
   * team-page kicking (FGA) and punting (punts);
   * the source URL and sha256 of every page copy used.

   PFR answers scripts with HTTP 403, so every page was read from a Wayback copy. The copies are
   cached in `build/eras/league/pfr_cache/`, with the index in `starters_sources.json`. `--fetch`
   downloads only what is missing.
2. **Depth rule in `build/eras/make_runtime_package.py`.** The tool reads the file above when it
   exists. It starts from the seed order (the save's stored depth, unchanged), then, for each
   position code:
   * the player with the most 1980 GS goes first;
   * for K, the player with the most FGA goes first, and for P, the player with the most punts;
   * for the paired positions (T, G, DE, DT, OLB, ILB, CB, WR), the two GS leaders head the left
     and right chains, assigned to keep more of the seed's two starters in place;
   * everyone else keeps the seed order;
   * ties keep the seed order;
   * a position where no community player has a 1980 start keeps the seed order.
3. **Matching community players to PFR.** Players are matched by name, ignoring case, accents,
   punctuation and spaces. Three fallbacks are allowed, and each must be unique on that roster:
   * "spelling": within two letters (Johhny/Johnny Perkins);
   * "nickname": a common short form (Bob/Robert Pratt);
   * "initial": same last name, first initial and jersey number (Dennis "Dirt" Winston).

   Every match that is not exact is listed. **Nobody was moved between teams.**
   * Community players not on their team's PFR 1980 roster are flagged, with the other 1980 team
     whose PFR roster has the name, where there is one.
   * Real 1980 starters that the community team lacks are flagged.
4. **PROVENANCE.txt** has a new section, "DEPTH CHART STARTERS (verified, ...)". For each team it
   records the sources, the second-copy check, every change from the seed order, the players not
   on the PFR roster, the missing real starters and the name matches that are not exact.
   `starters_1980.json` is added to INPUT FILES.
5. **T0.1 (`--check`)** now also checks each `depth/<id>.csv`:
   * one row per roster row, with the same names, position and number;
   * rank and side are each in 0..7;
   * each position has exactly one rank 0, and each paired position exactly one side 0;
   * the starters rule holds again, recomputed from `starters_1980.json`.

   Result: `T0.1 1980 runtime package: OK ... depth files: 28 ...; verified starters rule holds
   for 28`.

## Checks

* **Files that changed.** I compared the package with a copy taken before the run (`diff -r`).
  * Changed: only `PROVENANCE.txt` and the 28 `depth/*.csv`.
  * Byte-identical: `teams.csv`, `schedule.csv`, `coaches.csv`, all `rosters/*.csv` and all
    `rosters/*.flags.txt`.
  * **No roster fix was made.**
  * A second regeneration produced the same bytes.
* **Second copy.** For each team, a second Wayback capture of the roster page was parsed
  separately. Where one with GS exists, it is at least a year older.
  * 26 teams: GS agree for every player in both copies, 1,380 of 1,380, with 0 differences.
  * Seahawks and Saints: no other capture with GS exists.
* **Wikipedia (a second reading, not independent: the articles cite PFR).**
  * 1980 Seattle Seahawks season: the 22 starters shown in bold match PFR's Starters table by
    jersey number, 22 of 22.
  * 1980 Oakland Raiders season: its depth-chart template gives QB Plunkett, K Chris Bahr and
    P Ray Guy, which agrees with the PFR data.
  * The other 26 articles have no starters list.
* **Parser check.** PFR player ids can contain a dot (J.C. Wilson is `WilsJ.20`). The first
  parse dropped those players, which was caught and fixed. Every roster table now parses all of
  its player rows, and only the "Team Total" row is skipped.

## Results

**185 chain starters changed** from the seed order, across all 28 teams. The five examples from the
seed report:

| team | seed starter | now | 1980 PFR |
|---|---|---|---|
| 49ers QB | Larry Barisich (not on the PFR roster) | **Steve DeBerg** | DeBerg 9 GS, Montana 7 GS |
| Raiders QB | David Humm (not on the PFR roster) | **Jim Plunkett** | Plunkett 11 GS. Stabler started for Houston |
| Raiders P | Dave Francis (not on the PFR roster) | **Ray Guy** | 71 punts |
| Bills QB | Dan Manucci (0 GS) | **Joe Ferguson** | 16 GS |
| Giants QB | Randy Dean (not on the PFR roster) | **Phil Simms** | 13 GS |

Note on the 49ers: by the rule, DeBerg starts, not Montana. PFR counts 9 starts for DeBerg and 7
for Montana in 1980.

Other QB, K and P changes:
* Broncos QB Craig Morton (9 GS) over Craig Penrose. Matt Robinson (7 GS) is on the community
  Jets.
* Cowboys QB Danny White (16 GS) over Roger Staubach (retired).
* Dolphins K Uwe von Schamann over Mike Owens.
* Vikings K Rick Danmeier over Mike Mitchell.

**Still not right, because the real player is not on the community team** (flagged; the seed
order is kept there):
* QB:
  * Oilers (Ken Stabler, 16 GS; he is on the community Raiders);
  * Dolphins (David Woodley, 11 GS);
  * Lions (Gary Danielson, 16 GS).
* K:
  * Bengals (Ian Sunter);
  * Chiefs (Nick Lowery);
  * Raiders (Chris Bahr; he is on the community Bengals);
  * Cardinals (Neil O'Donoghue);
  * Lions (Eddie Murray);
  * Packers (Tom Birney);
  * Buccaneers (Garo Yepremian);
  * Saints (Benny Ricardo).
* P:
  * Colts (Mike Bragg);
  * Bills (Greg Cater);
  * Patriots (Mike Hubach);
  * Chargers (Rick Partridge);
  * Cardinals (Larry Swider);
  * Washington (Mike Connell);
  * Lions (Tom Skladany);
  * 49ers (Jim Miller).

Fixing these needs roster changes, so it is left for the roster work. The full list is under
each team below and in PROVENANCE.

**Roster-membership mismatches:**
* 528 community players are not on their team's PFR 1980 roster.
  * 128 of them are on another team's 1980 PFR roster (for example Stabler on the Oilers,
    Pastorini on the Raiders, Joe DeLamielleure on the Browns and Bob Chandler on the Raiders).
  * The rest are on no 1980 PFR roster. Most are players who had retired or left the league by
    1980.
* 103 real 1980 starters (from PFR's Starters table, plus the top K and P) are missing from their
  community team.

Limits of the rule:
* GS is the player's season total, not GS at the game position. A player the save lists at a
  different position carries his GS with him.
* Where the real starter is missing, the GS leader can be a part-timer (Giants HB Eddie Hicks,
  1 GS).

| team | starters changed | names on PFR roster | community players not on PFR roster | real starters missing | second copy GS agree |
|---|---|---|---|---|---|
| colts | 9 | 33 of 53 | 20 | 5 | 50 of 50 |
| bills | 7 | 33 of 53 | 20 | 4 | 50 of 50 |
| dolphins | 8 | 35 of 53 | 18 | 2 | 53 of 53 |
| patriots | 5 | 36 of 53 | 17 | 5 | 48 of 48 |
| jets | 5 | 39 of 53 | 14 | 2 | 53 of 53 |
| bengals | 7 | 34 of 53 | 19 | 3 | 52 of 52 |
| browns | 3 | 31 of 53 | 22 | 4 | 49 of 49 |
| oilers_titans | 6 | 38 of 53 | 15 | 2 | 53 of 53 |
| steelers | 8 | 38 of 53 | 15 | 2 | 49 of 49 |
| broncos | 6 | 34 of 53 | 19 | 1 | 53 of 53 |
| chiefs | 5 | 36 of 53 | 17 | 4 | 56 of 56 |
| raiders | 6 | 30 of 53 | 23 | 6 | 52 of 52 |
| chargers | 6 | 39 of 53 | 14 | 3 | 52 of 52 |
| seahawks | 6 | 34 of 53 | 19 | 3 | none (Wikipedia 22/22) |
| cowboys | 4 | 36 of 53 | 17 | 2 | 48 of 48 |
| giants | 9 | 26 of 53 | 27 | 6 | 68 of 68 |
| eagles | 9 | 35 of 53 | 18 | 1 | 52 of 52 |
| cardinals | 9 | 31 of 53 | 22 | 5 | 58 of 58 |
| washington | 10 | 33 of 53 | 20 | 4 | 51 of 51 |
| bears | 7 | 36 of 53 | 17 | 1 | 48 of 48 |
| lions | 4 | 30 of 53 | 23 | 8 | 60 of 60 |
| packers | 8 | 36 of 53 | 17 | 5 | 61 of 61 |
| vikings | 7 | 33 of 53 | 20 | 1 | 51 of 51 |
| buccaneers | 7 | 33 of 53 | 20 | 3 | 50 of 50 |
| falcons | 5 | 35 of 53 | 18 | 4 | 50 of 50 |
| rams | 3 | 33 of 53 | 20 | 4 | 53 of 53 |
| saints | 7 | 38 of 53 | 15 | 5 | none |
| 49ers | 9 | 31 of 53 | 22 | 8 | 60 of 60 |
| **total** | **185** | **956 of 1,484** | **528** | **103** | |

How to rebuild:

    python -I build/eras/league_starters.py [--fetch]    # writes data/eras/league/starters_1980.json
    python -I build/eras/make_runtime_package.py         # regenerates the package, then runs T0.1

Blocked: none. Only the Seahawks and Saints lack a second roster copy (see Checks).

## Per team

### colts

Sources: roster <https://web.archive.org/web/20250126220236/https://www.pro-football-reference.com/teams/clt/1980_roster.htm>, team page <https://web.archive.org/web/20241216131336/https://www.pro-football-reference.com/teams/clt/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20221002034806/https://www.pro-football-reference.com/teams/clt/1980_roster.htm>: GS agree for 50 of 50 players.

Changes from the community order (9):

* WR right: Mike Siani (1 GS) first, was Randy Burke (0 GS)
* CB left: Bruce Laird (15 GS) first, was Norm Thompson (not on the PFR roster)
* HB: Nesby Glasgow (16 GS) first, was Joe Washington (11 GS)
* OLB right: Mike Woods (12 GS) first, was Steve Zabel (not on the PFR roster)
* ILB left: Sanders Shiver (13 GS) first, was Stan White (not on the PFR roster)
* C: Ken Mendenhall (16 GS) first, was Dwight Stephenson (not on the PFR roster)
* G left: Ken Huff (16 GS) first, was Ron Baker (not on the PFR roster)
* DT left: Herb Orvis (16 GS) first, was George Gross (not on the PFR roster)
* DE right: Mike Ozdowski (10 GS) first, was Ron Fernandes (not on the PFR roster)

Community players not on the PFR 1980 roster (20): Norm Thompson CB; Doug Nettles CB [1980: giants (0 GS)]; Mike Jones CB; Ron Fernandes DE; George Gross DT; Roosevelt Leaks FB [1980: bills (0 GS)]; Don Hardeman FB; Ron Baker G [1980: eagles (1 GS)]; Stan White ILB [1980: lions (16 GS)]; Bob Haslett K; Toni Linhart K; Steve Zabel OLB; Bucky Dilts P; Clinton Oldham P; Jerry Golsteyn QB; Johnny Johnson SS; Don Hall SS; David Taylor T; Glenn Doughty WR; Dwight Stephenson C [1980: dolphins (0 GS)]

Real 1980 starters missing from the community team (5): FB Cleveland Franklin (13 GS); WR Ray Butler (16 GS); LDT Mike Barnes (14 GS); LCB Derrick Hatchett (16 GS); P Mike Bragg (82 punts)

Name matches that are not exact (1): Bob Pratt = PFR Robert Pratt (nickname)

### bills

Sources: roster <https://web.archive.org/web/20241212013129/https://www.pro-football-reference.com/teams/buf/1980_roster.htm>, team page <https://web.archive.org/web/20241226155153/https://www.pro-football-reference.com/teams/buf/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20230624230750/https://www.pro-football-reference.com/teams/buf/1980_roster.htm>: GS agree for 50 of 50 players.

Changes from the community order (7):

* QB: Joe Ferguson (16 GS) first, was Dan Manucci (0 GS)
* CB right: Steve Freeman (16 GS) first, was Mario Clark (15 GS)
* FB: Curtis Brown (16 GS) first, was Fernando Moody (not on the PFR roster)
* OLB left: Jim Haslett (16 GS) first, was Dan Jilek (not on the PFR roster)
* C: Will Grant (16 GS) first, was Willie Parker (not on the PFR roster)
* DE left: Ben Williams (16 GS) first, was Mike Kadish (0 GS)
* DE right: Sherman White (13 GS) first, was Scott Hutchinson (3 GS)

Community players not on the PFR 1980 roster (20): Fernando Moody FB; Joe DeLamielleure G [1980: browns (13 GS)]; Ed Fulton G; Mike Collier HB; Steve Powell HB; Dennis Brooks HB; Tom Dempsey K; Dan Jilek OLB; Rusty Jackson P; Curtis Bryan P; Bill Munson QB; Tony Greene SS; O. J. Newhouse TE; Ron Howard TE; Joseph Shipp TE; Tom Wilkins TE; Leonard Willis WR; Bob Chandler WR [1980: raiders (16 GS)]; Danny Kalu WR; Willie Parker C [1980: lions (1 GS)]

Real 1980 starters missing from the community team (4): RB Joe Cribbs (16 GS); RG Conrad Dobler (16 GS); FS Bill Simpson (11 GS); P Greg Cater (73 punts)

### dolphins

Sources: roster <https://web.archive.org/web/20241203111303/https://www.pro-football-reference.com/teams/mia/1980_roster.htm>, team page <https://web.archive.org/web/20241203212714/https://www.pro-football-reference.com/teams/mia/1980.htm>. Names on the PFR roster: 35 of 53.

Second copy: <https://web.archive.org/web/20230406171955/https://www.pro-football-reference.com/teams/mia/1980_roster.htm>: GS agree for 53 of 53 players.

Changes from the community order (8):

* K: Uwe von Schamann (23 FGA) first, was Mike Owens (not on the PFR roster)
* WR left: Nat Moore (16 GS) first, was Duriel Harris (3 GS)
* CB right: Glenn Blackwood (15 GS) first, was Norris Thomas (not on the PFR roster)
* HB: Del Williams (10 GS) first, was Larry Csonka (not on the PFR roster)
* TE: Ronnie Lee (13 GS) first, was Bruce Hardy (12 GS)
* ILB left: A. Duhe (15 GS) first, was Steve Towle (0 GS)
* G right: Ed Newman (16 GS) first, was Jim Langer (not on the PFR roster)
* T right: Eric Laakso (16 GS) first, was Cleveland Green (3 GS)

Community players not on the PFR 1980 roster (18): Norris Thomas CB [1980: buccaneers (1 GS)]; Charlie Babb CB; Mike Kozlowski CB; Neal Colzie FS [1980: buccaneers (1 GS)]; Cal Bell FS; Jim Langer G [1980: vikings (0 GS)]; Larry Csonka HB; Gary Davis HB [1980: buccaneers (2 GS)]; Norm Bulaich HB; Bob Matheson ILB; Mel Land ILB [1980: 49ers (0 GS)]; Mike Owens K; Mike Nugent K; Lem Wood P; Guy Benjamin QB [1980: saints (0 GS)]; Whitney Willis SS; Steve Vaughn SS; Ozzie Quillan SS

Real 1980 starters missing from the community team (2): QB David Woodley (11 GS); LCB Don McNeal (13 GS)

Name matches that are not exact (2): Del Williams = PFR Delvin Williams (nickname); A. Duhe = PFR A.J. Duhe (spelling)

### patriots

Sources: roster <https://web.archive.org/web/20240530184408/https://www.pro-football-reference.com/teams/nwe/1980_roster.htm>, team page <https://web.archive.org/web/20241204002532/https://www.pro-football-reference.com/teams/nwe/1980.htm>. Names on the PFR roster: 36 of 53.

Second copy: <https://web.archive.org/web/20230319112010/https://www.pro-football-reference.com/teams/nwe/1980_roster.htm>: GS agree for 48 of 48 players.

Changes from the community order (5):

* WR right: Stanley Morgan (16 GS) first, was Don Westbrook (0 GS)
* CB left: Rick Sanford (10 GS) first, was Prentice McCray (0 GS)
* HB: Don Calhoun (16 GS) first, was Horace Ivory (0 GS)
* OLB left: Mike Hawkins (16 GS) first, was Sam Hunt (not on the PFR roster)
* ILB left: John Zamberlin (13 GS) first, was Steve King (0 GS)

Community players not on the PFR 1980 roster (17): Doug Beaudion CB; Dick Conn CB; Mark Washington CB; Mark Buben DE; Mike Barnes DT [1980: colts (14 GS)]; Sam Cunningham FB; Terry Falcon G [1980: giants (3 GS)]; Ken Rafferty G; Ray Costict ILB; Clarence Lindstrom K; Sam Hunt OLB; Eddie Hare P; Herbert Ehrmann P; Tom Owen QB; George Mike-Mayer SS; Al Chandler TE; Ray Jarvis WR

Real 1980 starters missing from the community team (5): RB Vagas Ferguson (16 GS); RG Sam Adams (14 GS); RCB Mike Haynes (12 GS); SS Roland James (10 GS); P Mike Hubach (63 punts)

Name matches that are not exact (1): Gary Puetz = PFR Garry Puetz (spelling)

### jets

Sources: roster <https://web.archive.org/web/20231224203914/https://www.pro-football-reference.com/teams/nyj/1980_roster.htm>, team page <https://web.archive.org/web/20241210201057/https://www.pro-football-reference.com/teams/nyj/1980.htm>. Names on the PFR roster: 39 of 53.

Second copy: <https://web.archive.org/web/20220123024956/https://www.pro-football-reference.com/teams/nyj/1980_roster.htm>: GS agree for 53 of 53 players.

Changes from the community order (5):

* CB left: Donald Dykes (16 GS) first, was Burgess Owens (not on the PFR roster)
* HB: Scott Dierking (13 GS) first, was Clark Gaines (5 GS)
* OLB right: Ron Crosby (16 GS) first, was Mike McKibben (1 GS)
* G left: Dan Alexander (16 GS) first, was Randy Rasmussen (8 GS)
* DT right: Abdul Salaam (16 GS) first, was Bob Winkel (0 GS)

Community players not on the PFR 1980 roster (14): Burgess Owens CB [1980: raiders (16 GS)]; Johnny Lynn CB; Ed Taylor CB [1980: dolphins (3 GS)]; Joe Pellegrini DT; Jim Hampton G; Dave Jacobs K; Richie Szaro K; John Hennessy OLB; Bob Martin OLB; Matt Robinson QB [1980: broncos (7 GS)]; James Turner SS; Bob Raba TE [1980: colts (0 GS)]; Roger Farmer WR; Ed McGlasson C [1980: rams (0 GS)]

Real 1980 starters missing from the community team (2): WR Lam Jones (13 GS); FS Darrol Ray (16 GS)

### bengals

Sources: roster <https://web.archive.org/web/20250819211321/https://www.pro-football-reference.com/teams/cin/1980_roster.htm>, team page <https://web.archive.org/web/20241202013045/https://www.pro-football-reference.com/teams/cin/1980.htm>. Names on the PFR roster: 34 of 53.

Second copy: <https://web.archive.org/web/20230731072952/https://www.pro-football-reference.com/teams/cin/1980_roster.htm>: GS agree for 52 of 52 players.

Changes from the community order (7):

* WR right: Don Bass (12 GS) first, was Steve Kreider (0 GS)
* CB right: Louis Breeden (5 GS) first, was Jim Browner (0 GS)
* HB: Charles Alexander (16 GS) first, was Archie Griffin (7 GS)
* OLB right: Glenn Cameron (14 GS) first, was Tom Dinkel (12 GS)
* C: Blair Bush (13 GS) first, was Bob Johnson (not on the PFR roster)
* G left: Glenn Bujnoch (16 GS) first, was Vern Holland (not on the PFR roster)
* G right: Dave Lapham (1 GS) first, was Barney Cotton (not on the PFR roster)

Community players not on the PFR 1980 roster (19): Vaughn Lusby CB [1980: bears (0 GS)]; Scott Burk CB; Cliff Cornelius CB; Leonard Mitchell DE; Scott Perry FS [1980: chargers (0 GS), 49ers (0 GS)]; Paul McCartney FS; Vern Holland G [1980: giants (5 GS), lions (0 GS)]; Barney Cotton G [1980: cardinals (9 GS)]; Mark Donahue G; Bruce Vaughn ILB; Howie Kurnick ILB; Chris Bahr K [1980: raiders (0 GS)]; Tom Ruud OLB; Mike Langer P; Archie Williams SS; Lee Roy Mendenhall SS; Rick Walker TE [1980: washington (4 GS)]; Billy Brooks WR; Bob Johnson C

Real 1980 starters missing from the community team (3): LT Anthony Munoz (16 GS); SS Greg Bright (16 GS); K Ian Sunter (20 FGA)

### browns

Sources: roster <https://web.archive.org/web/20241215043057/https://www.pro-football-reference.com/teams/cle/1980_roster.htm>, team page <https://web.archive.org/web/20241226165353/https://www.pro-football-reference.com/teams/cle/1980.htm>. Names on the PFR roster: 31 of 53.

Second copy: <https://web.archive.org/web/20230407035503/https://www.pro-football-reference.com/teams/cle/1980_roster.htm>: GS agree for 49 of 49 players.

Changes from the community order (3):

* HB: Greg Pruitt (9 GS) first, was Calvin Hill (0 GS)
* ILB right: Clay Matthews (13 GS) first, was Gerald Irons (not on the PFR roster)
* DT right: Henry Bradley (16 GS) first, was Mickey Sims (not on the PFR roster)

Community players not on the PFR 1980 roster (22): Ricky Jones CB [1980: colts (0 GS)]; Jack Gregory DE; Mike St. Clair DE [1980: bengals (0 GS)]; Mickey Sims DT; Rich Dimler DT [1980: packers (0 GS)]; Randy Rich FS; George Buehler G; Pat Moriarty HB; Ron Burton HB; Steve Taylor HB; Gerald Irons ILB; Dave Graf ILB; Wade Melville K; Willie Donovan P; John Smith QB [1980: patriots (0 GS)]; Tom Burke QB; Mark Miller QB; Ernie Johnson SS; Ron Weaver SS; Randy Bergey SS; Mark Preston SS; Matt Miller T

Real 1980 starters missing from the community team (4): RG Joe DeLamielleure (13 GS); LDE Marshall Harris (15 GS); LILB Robert Jackson (14 GS); LCB Ron Bolton (16 GS)

Name matches that are not exact (1): Lyle Alzedo = PFR Lyle Alzado (spelling)

### oilers_titans

Sources: roster <https://web.archive.org/web/20240526054753/https://www.pro-football-reference.com/teams/oti/1980_roster.htm>, team page <https://web.archive.org/web/20241224190150/https://www.pro-football-reference.com/teams/oti/1980.htm>. Names on the PFR roster: 38 of 53.

Second copy: <https://web.archive.org/web/20230407053051/https://www.pro-football-reference.com/teams/oti/1980_roster.htm>: GS agree for 53 of 53 players.

Changes from the community order (6):

* CB left: J.C. Wilson (16 GS) first, was Charles Jefferson (not on the PFR roster)
* FB: Tim Wilson (16 GS) first, was Rob Carpenter (1 GS)
* TE: Mike Barber (16 GS) first, was Rich Caster (0 GS)
* ILB left: Gregg Bingham (16 GS) first, was Jimbo Elrod (not on the PFR roster)
* G right: David Carter (4 GS) first, was Conway Hayman (3 GS)
* DT right: Ken Kennard (11 GS) first, was Mike Stensrud (0 GS)

Community players not on the PFR 1980 roster (15): Charles Jefferson CB; Willie Alexander CB; Bill Currier CB [1980: patriots (0 GS)]; Jim Young DE; Steve Baumgartner DE; George Reihner G; Tom Randall G; Kenny King HB [1980: raiders (13 GS)]; Rich Ellender HB; Jimbo Elrod ILB; Mike Murphy OLB; Dan Pastorini QB [1980: raiders (5 GS)]; Wes Phillips T; Conrad Rucker TE [1980: buccaneers (0 GS), rams (0 GS)]; Eddie Foster WR

Real 1980 starters missing from the community team (2): QB Ken Stabler (16 GS); LG Bob Young (12 GS)

### steelers

Sources: roster <https://web.archive.org/web/20260425095904/https://www.pro-football-reference.com/teams/pit/1980_roster.htm>, team page <https://web.archive.org/web/20241203235246/https://www.pro-football-reference.com/teams/pit/1980.htm>. Names on the PFR roster: 38 of 53.

Second copy: <https://web.archive.org/web/20241215154456/https://www.pro-football-reference.com/teams/pit/1980_roster.htm>: GS agree for 49 of 49 players.

Changes from the community order (8):

* WR right: Jim Smith (10 GS) first, was John Stallworth (2 GS)
* CB right: Ron Johnson (16 GS) first, was Mike Wagner (13 GS)
* HB: Franco Harris (13 GS) first, was Rocky Bleier (6 GS)
* TE: Randy Grossman (15 GS) first, was Bennie Cunningham (1 GS)
* T left: Larry Brown (16 GS) first, was Jon Kolb (7 GS)
* T right: Ted Petersen (9 GS) first, was Sam Davis (not on the PFR roster)
* DT right: Steve Furness (16 GS) first, was John Banaszak (11 GS)
* DE right: L. C. Greenwood (15 GS) first, was Greg Grupp (not on the PFR roster)

Community players not on the PFR 1980 roster (15): Greg Grupp DE; Anthony Anderson FB [1980: falcons (0 GS)]; Harry Howell FS; Larry Slater FS; Gerry Mullins G; Thom Dornbrook G [1980: dolphins (0 GS)]; Rick Moser HB [1980: dolphins (0 GS)]; Tom Graves ILB; Doug Buffone OLB; Charley Austin P; Lenny Kruczek QB; Damien Staubach SS; Charles Schroy SS; Sam Davis T; Darryl Dieken T

Real 1980 starters missing from the community team (2): LG Ray Pinney (16 GS); RG Tyrone McGriff (10 GS)

Name matches that are not exact (1): Dennis Winston = PFR Dirt Winston (initial)

### broncos

Sources: roster <https://web.archive.org/web/20241203230530/https://www.pro-football-reference.com/teams/den/1980_roster.htm>, team page <https://web.archive.org/web/20241203035506/https://www.pro-football-reference.com/teams/den/1980.htm>. Names on the PFR roster: 34 of 53.

Second copy: <https://web.archive.org/web/20220525105031/https://www.pro-football-reference.com/teams/den/1980_roster.htm>: GS agree for 53 of 53 players.

Changes from the community order (6):

* QB: Craig Morton (9 GS) first, was Craig Penrose (not on the PFR roster)
* WR right: Haven Moses (14 GS) first, was Steve Watson (1 GS)
* CB left: Billy Thompson (16 GS) first, was Charlie West (not on the PFR roster)
* HB: Jim Jensen (13 GS) first, was Otis Armstrong (6 GS)
* TE: Steve Foley (16 GS) first, was Riley Odoms (15 GS)
* ILB right: Randy Gradishar (16 GS) first, was Bob Swenson (not on the PFR roster)

Community players not on the PFR 1980 roster (19): Charlie West CB; Chris Pane CB; Butch Atkinson CB; Kit Lathrop DE [1980: packers (0 GS)]; Bruce Radford DT [1980: buccaneers (0 GS)]; John Grant DT; Larry Canada HB; Zachary Dixon HB [1980: colts (0 GS), eagles (0 GS)]; Ken Brown HB [1980: packers (0 GS)]; Bob Swenson ILB; Jim Turner K; Petey McKenzie K; Norris Weese QB; Craig Penrose QB; Rod Payton SS; Mike Sheppard SS; Jack Dolbin WR; Mike Shumann WR [1980: buccaneers (0 GS)]; Vince Kinney WR

Real 1980 starters missing from the community team (1): RCB Aaron Kyle (9 GS)

Name matches that are not exact (1): Billy Thompson = PFR Bill Thompson (spelling)

### chiefs

Sources: roster <https://web.archive.org/web/20241203110535/https://www.pro-football-reference.com/teams/kan/1980_roster.htm>, team page <https://web.archive.org/web/20241219052914/https://www.pro-football-reference.com/teams/kan/1980.htm>. Names on the PFR roster: 36 of 53.

Second copy: <https://web.archive.org/web/20230103171232/https://www.pro-football-reference.com/teams/kan/1980_roster.htm>: GS agree for 56 of 56 players.

Changes from the community order (5):

* CB right: Herb Christopher (16 GS) first, was Mike Williams (11 GS)
* HB: Ted McKnight (16 GS) first, was Horace Belton (4 GS)
* G right: Tom Condon (16 GS) first, was Charlie Getty (6 GS)
* T right: Rod Walters (1 GS) first, was Larry Brown (not on the PFR roster)
* DE right: Dave Lindstrom (14 GS) first, was Sylvester Hicks (0 GS)

Community players not on the PFR 1980 roster (17): Gerald Jackson CB; Curtis Anderson DE; Don Greene FB; Jim Harrison FB; Horace Perkins FS; Tim Collier FS [1980: cardinals (5 GS)]; Wilbert Haslip HB; Dave Rozumek ILB; Jan Stenerud K [1980: packers (0 GS)]; Dana Joiner K; Toni Baker P; Mike Livingston QB; Larry Brown T [1980: steelers (16 GS)]; Steve Kreider T [1980: bengals (0 GS)]; Johnnie Dirden WR; Steve Gaunty WR; Barry Dirden WR

Real 1980 starters missing from the community team (4): WR J.T. Smith (16 GS); RT Jim Rourke (9 GS); RCB Eric Harris (13 GS); K Nick Lowery (26 FGA)

### raiders

Sources: roster <https://web.archive.org/web/20250221225348/https://www.pro-football-reference.com/teams/rai/1980_roster.htm>, team page <https://web.archive.org/web/20241207211031/https://www.pro-football-reference.com/teams/rai/1980.htm>. Names on the PFR roster: 30 of 53.

Second copy: <https://web.archive.org/web/20230406015642/https://www.pro-football-reference.com/teams/rai/1980_roster.htm>: GS agree for 52 of 52 players.

Changes from the community order (6):

* QB: Jim Plunkett (11 GS) first, was David Humm (not on the PFR roster)
* P: Ray Guy (71 punts) first, was Dave Francis (not on the PFR roster)
* WR right: Rich Martini (1 GS) first, was Morris Bradshaw (0 GS)
* HB: Mark van Eeghen (16 GS) first, was Clarence Hawkins (not on the PFR roster)
* TE: Ray Chester (10 GS) first, was Dave Casper (6 GS)
* DE left: John Matuszak (16 GS) first, was Pat Toomay (not on the PFR roster)

Community players not on the PFR 1980 roster (23): Henry Williams CB; Charlie Phillips CB; Rufus Bess CB [1980: bills (1 GS)]; Pat Toomay DE; John Alexander DE; Carl Laidlaw FB; Brian Benirschke FS; John Vella G [1980: vikings (0 GS)]; Dan Medlin G; Booker Russell HB [1980: chargers (0 GS)]; Terry Robiskie HB [1980: dolphins (5 GS)]; Clarence Hawkins HB; John Huddleston ILB; Jim Breech K [1980: bengals (0 GS)]; Bob Barefield K; Phil Villapiano OLB [1980: bills (0 GS)]; Monte Johnson OLB; Dave Francis P; Ken Stabler QB [1980: oilers_titans (16 GS)]; David Humm QB [1980: bills (0 GS)]; Jack Tatum SS [1980: oilers_titans (0 GS)]; Larry Brunson WR [1980: broncos (0 GS)]; Joe Stewart WR

Real 1980 starters missing from the community team (6): RB Kenny King (13 GS); WR Bob Chandler (16 GS); LILB Matt Millen (16 GS); RILB Randy McClanahan (8 GS); FS Burgess Owens (16 GS); K Chris Bahr (37 FGA)

Name matches that are not exact (2): Art Whittington = PFR Arthur Whittington (nickname); Ray Chester = PFR Raymond Chester (nickname)

### chargers

Sources: roster <https://web.archive.org/web/20241203202040/https://www.pro-football-reference.com/teams/sdg/1980_roster.htm>, team page <https://web.archive.org/web/20241204132122/https://www.pro-football-reference.com/teams/sdg/1980.htm>. Names on the PFR roster: 39 of 53.

Second copy: <https://web.archive.org/web/20230422121031/https://www.pro-football-reference.com/teams/sdg/1980_roster.htm>: GS agree for 52 of 52 players.

Changes from the community order (6):

* CB right: Willie Buchanon (16 GS) first, was Jerome Dove (2 GS)
* SS: Pete Shaw (12 GS) first, was Mike Fuller (5 GS)
* HB: Clarence Williams (9 GS) first, was Lydell Mitchell (not on the PFR roster)
* C: Bob Rush (1 GS) first, was Ralph Perretta (0 GS)
* G right: Don Macek (16 GS) first, was Ed White (16 GS)
* T left: Dan Audick (9 GS) first, was Russ Washington (6 GS)

Community players not on the PFR 1980 roster (14): Tom Suggs FS; Lydell Mitchell HB [1980: rams (0 GS)]; Bo Matthews HB [1980: giants (7 GS)]; Don Goode ILB [1980: browns (3 GS)]; Dan Stieve K; Roy Gerela K; Jeff West P; Al McGlasson P; James Harris QB; Cliff Olander QB; Steve Taylor SS; Bob Klein TE; Larry Burton WR; Artie Owens WR [1980: bills (0 GS), saints (0 GS)]

Real 1980 starters missing from the community team (3): FB John Cappelletti (8 GS); LDT Louie Kelcher (14 GS); P Rick Partridge (60 punts)

### seahawks

Sources: roster <https://web.archive.org/web/20220514155906/https://www.pro-football-reference.com/teams/sea/1980_roster.htm>, team page <https://web.archive.org/web/20241204030509/https://www.pro-football-reference.com/teams/sea/1980.htm>. Names on the PFR roster: 34 of 53.

Second copy: none (no other Wayback capture of the roster page has GS).

Changes from the community order (6):

* CB left: Kerry Justin (11 GS) first, was Cornell Webster (5 GS)
* HB: Dan Doornink (14 GS) first, was Sherman Smith (2 GS)
* OLB right: Michael Jackson (15 GS) first, was Sammy Green (not on the PFR roster)
* G right: Tom Lynch (12 GS) first, was Ron Coder (not on the PFR roster)
* T right: Louis Bullard (13 GS) first, was Nick Bebout (not on the PFR roster)
* DT left: Robert Hardy (16 GS) first, was Bill Cooke (3 GS)

Community players not on the PFR 1980 roster (19): Mike O'Brien CB; Carl Eller DE; Dennis Boyd DE; Ernie Price DE; Tony Benjamin FB; Ron Coder G [1980: cardinals (2 GS)]; Tony Green HB; David Sims HB; Pete Cronan ILB; Charles McShane ILB; Tom McCauley K; Sammy Green OLB [1980: oilers_titans (0 GS)]; Larry Polowski OLB; Steve Myer QB; Autry Beamon SS [1980: browns (0 GS)]; Nick Bebout T [1980: vikings (0 GS)]; Brian Peets TE; Duke Ferguson WR; Gerald Pane C

Real 1980 starters missing from the community team (3): FB Jim Jodat (13 GS); TE John Sawyer (16 GS); LDE Jacob Green (13 GS)

Name matches that are not exact (1): Effren Herrera = PFR Efren Herrera (spelling)

### cowboys

Sources: roster <https://web.archive.org/web/20241204162904/https://www.pro-football-reference.com/teams/dal/1980_roster.htm>, team page <https://web.archive.org/web/20241203141044/https://www.pro-football-reference.com/teams/dal/1980.htm>. Names on the PFR roster: 36 of 53.

Second copy: <https://web.archive.org/web/20220518230517/https://www.pro-football-reference.com/teams/dal/1980_roster.htm>: GS agree for 48 of 48 players.

Changes from the community order (4):

* QB: Danny White (16 GS) first, was Roger Staubach (not on the PFR roster)
* CB right: Aaron Mitchell (15 GS) first, was Benny Barnes (6 GS)
* OLB right: D. D. Lewis (16 GS) first, was Tom Henderson (not on the PFR roster)
* T left: Jim Cooper (15 GS) first, was Rayfield Wright (not on the PFR roster)

Community players not on the PFR 1980 roster (17): Aaron Kyle CB [1980: broncos (9 GS)]; Dave Stalls DE [1980: buccaneers (3 GS)]; Larry Dyson DE; Dan Merrill FS; Burton Lawless G [1980: lions (5 GS)]; Scott Laidlaw HB [1980: giants (1 GS)]; Alois Blackwell HB; Bob Hicks K; Tom Henderson OLB; Reggie Septien P; Roger Staubach QB; Cliff Harris SS; Bobby Wright SS; Rob Bunz SS; Willie Solomon SS; Rayfield Wright T; Wade Manning WR

Real 1980 starters missing from the community team (2): LDE Too Tall Jones (16 GS); SS Charlie Waters (16 GS)

### giants

Sources: roster <https://web.archive.org/web/20230409201538/https://www.pro-football-reference.com/teams/nyg/1980_roster.htm>, team page <https://web.archive.org/web/20241210173901/https://www.pro-football-reference.com/teams/nyg/1980.htm>. Names on the PFR roster: 26 of 53.

Second copy: <https://web.archive.org/web/20220212212738/https://www.pro-football-reference.com/teams/nyg/1980_roster.htm>: GS agree for 68 of 68 players.

Changes from the community order (9):

* QB: Phil Simms (13 GS) first, was Randy Dean (not on the PFR roster)
* WR left: Johhny Perkins (5 GS) first, was Tom Mullady (3 GS)
* HB: Eddie Hicks (1 GS) first, was Bob Torrey (not on the PFR roster)
* TE: Gary Shirk (14 GS) first, was Al Dixon (not on the PFR roster)
* OLB left: Frank Marion (6 GS) first, was Randy Coffield (not on the PFR roster)
* ILB left: Brad Van Pelt (14 GS) first, was Brian Kelley (1 GS)
* G right: Roy Simmons (16 GS) first, was Brad Benson (14 GS)
* T left: Gordon King (9 GS) first, was Gus Coppens (not on the PFR roster)
* DT right: Phil Tabor (6 GS) first, was John Mendenhall (not on the PFR roster)

Community players not on the PFR 1980 roster (27): Odis McKinney CB [1980: raiders (0 GS)]; Ray Rhodes CB [1980: 49ers (7 GS)]; Ernie Jones CB; Mack Mitchell DE; John Mendenhall DT [1980: lions (12 GS)]; Ken Johnson DT [1980: bills (0 GS)]; Calvin Miller DT [1980: falcons (0 GS)]; Kevin Lloyd DT; Alan Caldwell FS; Tom Easterling FS; Doug Horn G; Jim Wilson G; Doug Kotar HB; Bob Hammond HB; Bob Torrey HB [1980: eagles (0 GS)]; Randy Coffield OLB; Joe Pisarcik QB [1980: eagles (0 GS)]; Randy Dean QB; Ray Oldham SS [1980: lions (12 GS)]; Tom Neville T; Gus Coppens T; Loaird McCreary TE; Cleveland Jackson TE; Al Dixon TE [1980: chiefs (0 GS)]; Emery Moorehead WR [1980: broncos (0 GS)]; Dwight Scales WR; Keith Eck C

Real 1980 starters missing from the community team (6): FB Bo Matthews (7 GS); WR Danny Pittman (6 GS); NT Curtis McGriff (11 GS); LCB Mark Haynes (11 GS); RCB Mike Dennis (10 GS); FS Gary Woolford (8 GS)

Name matches that are not exact (1): Johhny Perkins = PFR Johnny Perkins (spelling)

### eagles

Sources: roster <https://web.archive.org/web/20250812192457/https://www.pro-football-reference.com/teams/phi/1980_roster.htm>, team page <https://web.archive.org/web/20241202104044/https://www.pro-football-reference.com/teams/phi/1980.htm>. Names on the PFR roster: 35 of 53.

Second copy: <https://web.archive.org/web/20240211173253/https://www.pro-football-reference.com/teams/phi/1980_roster.htm>: GS agree for 52 of 52 players.

Changes from the community order (9):

* WR right: Charlie Smith (16 GS) first, was Wally Henry (0 GS)
* FS: Herman Edwards (16 GS) first, was Bob Howard (not on the PFR roster)
* HB: Leroy Harris (15 GS) first, was Wilbert Montgomery (12 GS)
* TE: Keith Krepfle (11 GS) first, was John Spagnola (5 GS)
* ILB right: Jerry Robinson (16 GS) first, was Reggie Wilkes (0 GS)
* G right: Petey Perot (15 GS) first, was John Condon (not on the PFR roster)
* T right: Stan Walters (16 GS) first, was Rufus Mayes (not on the PFR roster)
* DT left: Charles Johnson (16 GS) first, was Manny Sistrunk (not on the PFR roster)
* DE left: Dennis Harrison (15 GS) first, was Claude Humphrey (1 GS)

Community players not on the PFR 1980 roster (18): Jimmy Carr CB; Deac Sanders CB; Lem Burnham DE; Manny Sistrunk DT; Bob Howard FS; Al Latimer FS; Wesley Herkenhoff FS; John Condon G; Terry Tautolo ILB [1980: 49ers (4 GS)]; Jack Arneson K; Steven Scott P; John Walton QB; Bruce Horton SS; Craig Shaw SS; Rufus Mayes T; Wade Key T; Larry Jones WR; Jerrold McRae WR

Real 1980 starters missing from the community team (1): LCB Roynell Young (16 GS)

Name matches that are not exact (2): Charles Johnson = PFR Charlie Johnson (spelling); Herman Edwards = PFR Herm Edwards (spelling)

### cardinals

Sources: roster <https://web.archive.org/web/20220514152119/https://www.pro-football-reference.com/teams/crd/1980_roster.htm>, team page <https://web.archive.org/web/20241226125325/https://www.pro-football-reference.com/teams/crd/1980.htm>. Names on the PFR roster: 31 of 53.

Second copy: <https://web.archive.org/web/20230213132436/https://www.pro-football-reference.com/teams/crd/1980_roster.htm>: GS agree for 58 of 58 players.

Changes from the community order (9):

* WR left: Mel Gray (16 GS) first, was Roy Green (6 GS)
* CB left: Carl Allen (10 GS) first, was Perry Smith (not on the PFR roster)
* CB right: Lee Nelson (10 GS) first, was Ken Greene (8 GS)
* FB: Wayne Morris (16 GS) first, was Theotis Brown (0 GS)
* OLB left: Steve Neils (14 GS) first, was Kurt Allerman (not on the PFR roster)
* OLB right: Tim Kearney (12 GS) first, was John Barefield (0 GS)
* ILB right: Eric Williams (12 GS) first, was Calvin Favron (7 GS)
* C: Tom Brahaney (16 GS) first, was Tom Banks (6 GS)
* T right: George Collins (9 GS) first, was Brad Oates (5 GS)

Community players not on the PFR 1980 roster (22): Perry Smith CB [1980: broncos (1 GS)]; Mike Robinson DE; John Zook DE; Bob Rozier DE; Jim Ramey DE; Charlie Davis DT; Dave Simons DT; Bob Young DT [1980: oilers_titans (12 GS)]; Steve Henry FS [1980: giants (0 GS)]; Terry Stieve G; Will Harrell HB; Thomas Lott HB; Don Lathrop K; Kurt Allerman OLB [1980: packers (11 GS)]; Sean Clancy OLB; Vern Den Herron P; Steve Pisarkiewicz QB [1980: packers (0 GS)]; Lenvil Berns SS; Chris Dieterich T [1980: lions (3 GS)]; Richard Osborne TE; Bill Murrell TE; Jimmy Childs WR

Real 1980 starters missing from the community team (5): TE Doug Marsh (16 GS); LG Barney Cotton (9 GS); NT Rush Brown (10 GS); K Neil O'Donoghue (15 FGA); P Larry Swider (99 punts)

### washington

Sources: roster <https://web.archive.org/web/20251231101738/https://www.pro-football-reference.com/teams/was/1980_roster.htm>, team page <https://web.archive.org/web/20241204044741/https://www.pro-football-reference.com/teams/was/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20241206105843/https://www.pro-football-reference.com/teams/was/1980_roster.htm>: GS agree for 51 of 51 players.

Changes from the community order (10):

* WR left: Ricky Thompson (15 GS) first, was Chris DeFrance (not on the PFR roster)
* CB right: Tony Peters (11 GS) first, was Joe Lavender (16 GS)
* FS: Lemar Parrish (15 GS) first, was Don VanDuyne (not on the PFR roster)
* SS: Mark Murphy (16 GS) first, was Tim Smith (not on the PFR roster)
* HB: Clarence Harmon (14 GS) first, was Benny Malone (not on the PFR roster)
* TE: Don Warren (12 GS) first, was Grady Richardson (0 GS)
* ILB left: Monte Coleman (10 GS) first, was Rich Milot (5 GS)
* T left: Jeff Williams (15 GS) first, was Greg Dubinetz (not on the PFR roster)
* DT left: Coy Bacon (16 GS) first, was Paul Smith (7 GS)
* DT right: Dave Butz (16 GS) first, was Diron Talbert (5 GS)

Community players not on the PFR 1980 roster (20): Don Harris CB [1980: giants (4 GS)]; Ted Fritsch FB; Ricky Williams FB; John Riggins FB; Don VanDuyne FS; Keith Baker FS; Jerome Perkins FS; Lynn Boden G; Benny Malone HB; Don Hover ILB; Joe Lemek K; Mike Bragg P [1980: colts (0 GS)]; Steve Wilson P [1980: cowboys (11 GS), buccaneers (14 GS)]; Kim McQuilken QB; Tim Smith SS [1980: oilers_titans (0 GS)]; Greg Dubinetz T; Jean Fugett TE; Danny Buggs WR; Chris DeFrance WR; Bob McDaniel C

Real 1980 starters missing from the community team (4): RB Wilbur Jackson (14 GS); WR Art Monk (11 GS); C Bob Kuziel (13 GS); P Mike Connell (85 punts)

### bears

Sources: roster <https://web.archive.org/web/20260114155518/https://www.pro-football-reference.com/teams/chi/1980_roster.htm>, team page <https://web.archive.org/web/20241226081159/https://www.pro-football-reference.com/teams/chi/1980.htm>. Names on the PFR roster: 36 of 53.

Second copy: <https://web.archive.org/web/20230406201838/https://www.pro-football-reference.com/teams/chi/1980_roster.htm>: GS agree for 48 of 48 players.

Changes from the community order (7):

* WR left: James Scott (12 GS) first, was Rickey Watts (2 GS)
* CB left: Allan Ellis (16 GS) first, was Virgil Livers (not on the PFR roster)
* TE: Mike Cobb (3 GS) first, was Greg Latta (not on the PFR roster)
* OLB left: Jerry Muckensturm (15 GS) first, was Bruce Herron (0 GS)
* ILB right: Gary Campbell (16 GS) first, was Lee Kunz (2 GS)
* DT right: Jim Osborne (16 GS) first, was Ron Rydalch (0 GS)
* DE right: Mike Hartenstine (16 GS) first, was Tommy Hart (not on the PFR roster)

Community players not on the PFR 1980 roster (17): Virgil Livers CB; Tommy Hart DE [1980: saints (12 GS)]; Jerry Meyers DE [1980: chiefs (0 GS)]; Pat Jones FB; Lonnie Perrin HB; Jack Deloplaine HB; Mark Merrill ILB; Keyaron Burnham K; Doug Buffone OLB; Brian McConnell OLB; Bob Avellini QB; Mike Spivey SS [1980: raiders (0 GS), saints (0 GS)]; Greg Latta TE; Steve Schubert WR; Golden Richards WR; Harry Washington WR; Tony Ardizzone C

Real 1980 starters missing from the community team (1): FB Roland Harper (12 GS)

### lions

Sources: roster <https://web.archive.org/web/20241204144657/https://www.pro-football-reference.com/teams/det/1980_roster.htm>, team page <https://web.archive.org/web/20241225104635/https://www.pro-football-reference.com/teams/det/1980.htm>. Names on the PFR roster: 30 of 53.

Second copy: <https://web.archive.org/web/20230503152445/https://www.pro-football-reference.com/teams/det/1980_roster.htm>: GS agree for 60 of 60 players.

Changes from the community order (4):

* WR left: Leonard Thompson (12 GS) first, was Gene Washington (not on the PFR roster)
* ILB left: Charlie Weaver (16 GS) first, was Ed O.Neil (not on the PFR roster)
* C: Amos Fowler (12 GS) first, was Larry Tearry (not on the PFR roster)
* DE right: Dave Pureifory (16 GS) first, was Dave Gallagher (not on the PFR roster)

Community players not on the PFR 1980 roster (23): Jimmy Stewart CB; Ken Ellis CB; Don Patterson CB [1980: giants (1 GS)]; Dave Gallagher DE; Ken Sanders DE [1980: vikings (0 GS)]; Cleveland Elam DT; Doug English DT; Bob Fernandes FB; Lawrence Gaines FB; Doug Jones FS; Dave Parkin FS; Nate Allen FS; Ed O.Neil ILB [1980: packers (11 GS)]; Jon Brooks ILB [1980: cardinals (0 GS), falcons (0 GS)]; Benny Ricardo K [1980: saints (0 GS)]; Dave Washington OLB [1980: saints (0 GS)]; Larry Swider P [1980: cardinals (0 GS)]; Joe Reed QB; Scott Hunter QB; Don Morrison T; Gene Washington WR; Larry Tearry C; Jamal Franks C

Real 1980 starters missing from the community team (8): QB Gary Danielson (16 GS); RB Billy Sims (16 GS); RDT John Mendenhall (12 GS); RLB Stan White (16 GS); LCB Wayne Smith (8 GS); FS Ray Oldham (12 GS); K Eddie Murray (42 FGA); P Tom Skladany (72 punts)

Name matches that are not exact (1): Bill Gay = PFR William Gay (nickname)

### packers

Sources: roster <https://web.archive.org/web/20241206142514/https://www.pro-football-reference.com/teams/gnb/1980_roster.htm>, team page <https://web.archive.org/web/20241204045018/https://www.pro-football-reference.com/teams/gnb/1980.htm>. Names on the PFR roster: 36 of 53.

Second copy: <https://web.archive.org/web/20220803230058/https://www.pro-football-reference.com/teams/gnb/1980_roster.htm>: GS agree for 61 of 61 players.

Changes from the community order (8):

* HB: Steve Atkins (3 GS) first, was Terdell Middleton (0 GS)
* ILB left: Mike Douglass (16 GS) first, was Gary Weaver (not on the PFR roster)
* C: Larry McCarren (16 GS) first, was Mike Wellman (0 GS)
* G right: Derrell Gofourth (16 GS) first, was Melvin Jackson (0 GS)
* T left: Tim Stokes (15 GS) first, was Mark Koncar (1 GS)
* DT left: Mike McCoy (16 GS) first, was Carl Barzilauskas (not on the PFR roster)
* DT right: Charles Johnson (15 GS) first, was Terry Jones (1 GS)
* DE right: Ezra Johnson (15 GS) first, was Bob Barber (not on the PFR roster)

Community players not on the PFR 1980 roster (17): Howard Sampson CB; Bob Barber DE; Earl Edwards DE; Carl Barzilauskas DT; Steve Wagner FS [1980: eagles (0 GS)]; Nate Simpson HB; Eric Torkelson HB; Sammy Johnson HB; Ricky Patton HB [1980: 49ers (0 GS)]; Walt Landers HB; Gary Weaver ILB; Dave Simmons ILB [1980: lions (0 GS)]; Rich Wingo ILB; Walter Tullis WR; Steve Odom WR; Brian Armstrong WR; Tom Brahaney C [1980: cardinals (16 GS)]

Real 1980 starters missing from the community team (5): RB Eddie Lee Ivery (16 GS); FB Gerry Ellis (13 GS); LILB Ed O'Neil (11 GS); RILB Kurt Allerman (11 GS); K Tom Birney (12 FGA)

Name matches that are not exact (1): Derrell Gofourth = PFR Derrel Gofourth (spelling)

### vikings

Sources: roster <https://web.archive.org/web/20250814161114/https://www.pro-football-reference.com/teams/min/1980_roster.htm>, team page <https://web.archive.org/web/20241203025307/https://www.pro-football-reference.com/teams/min/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20231008202829/https://www.pro-football-reference.com/teams/min/1980_roster.htm>: GS agree for 51 of 51 players.

Changes from the community order (7):

* K: Rick Danmeier (26 FGA) first, was Mike Mitchell (not on the PFR roster)
* WR right: Sammy White (16 GS) first, was Robert Steele (not on the PFR roster)
* CB left: John Turner (16 GS) first, was Bobby Bryant (12 GS)
* CB right: Tom Hannon (16 GS) first, was Nate Wright (4 GS)
* SS: Kurt Knoff (16 GS) first, was Paul Krause (not on the PFR roster)
* HB: Rickey Young (16 GS) first, was Chuck Foreman (not on the PFR roster)
* DE left: Randy Holloway (16 GS) first, was Jim Marshall (not on the PFR roster)

Community players not on the PFR 1980 roster (20): Jim Marshall DE; Joshua Niehaus DT; Tim Baylor FS; Phil Wise FS; Steve Smith FS; Butch Davis FS; Bob Baker FS; Chuck Foreman HB [1980: patriots (0 GS)]; Brent McClanahan HB; Jimmy Edwards HB; Mike Mitchell K; Wally Hilgenberg OLB; Mike Shaw QB; Paul Krause SS; Steve Kunz SS; Frank Myers T; Charlie Goodrum T; Robert Steele WR; Douglas Cunningham WR; Norm Thomas C

Real 1980 starters missing from the community team (1): TE Bob Tucker (13 GS)

### buccaneers

Sources: roster <https://web.archive.org/web/20241227224125/https://www.pro-football-reference.com/teams/tam/1980_roster.htm>, team page <https://web.archive.org/web/20241207022309/https://www.pro-football-reference.com/teams/tam/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20241208080044/https://www.pro-football-reference.com/teams/tam/1980_roster.htm>: GS agree for 50 of 50 players.

Changes from the community order (7):

* WR left: Gordon Jones (14 GS) first, was Larry Mucker (0 GS)
* WR right: Isaac Hagins (14 GS) first, was Morris Owens (not on the PFR roster)
* ILB right: David Logan (14 GS) first, was Rik Bonness (not on the PFR roster)
* C: Steven Wilson (14 GS) first, was Darrell Austin (2 GS)
* G left: Charley Hannah (16 GS) first, was Greg Horton (not on the PFR roster)
* DT left: Dewey Selmon (15 GS) first, was Randy Crowder (2 GS)
* DT right: Gene Sanders (3 GS) first, was Wally Chambers (not on the PFR roster)

Community players not on the PFR 1980 roster (20): Billy Cesare CB; Jeris White CB [1980: washington (1 GS)]; Wally Chambers DT; Lonnie Livers FS; Ray Smith FS; Greg Horton G [1980: rams (0 GS)]; Cleveland Hartenstine HB; Rik Bonness ILB; Dana Nafziger ILB; Neil O'Donoghue K [1980: cardinals (0 GS)]; Steve Baschnagel K; Terry Ramey P; Mike Rae QB; Charlie Brown QB; Jerry Mayberry SS; Tim Jones SS; Willie LeCount SS; Darryl Carlton T; Morris Owens WR; George Ragsdale WR

Real 1980 starters missing from the community team (3): RG Ray Snell (11 GS); RCB Mike Washington (16 GS); K Garo Yepremian (23 FGA)

Name matches that are not exact (1): Steven Wilson = PFR Steve Wilson (spelling)

### falcons

Sources: roster <https://web.archive.org/web/20241203133458/https://www.pro-football-reference.com/teams/atl/1980_roster.htm>, team page <https://web.archive.org/web/20241227101737/https://www.pro-football-reference.com/teams/atl/1980.htm>. Names on the PFR roster: 35 of 53.

Second copy: <https://web.archive.org/web/20220212165921/https://www.pro-football-reference.com/teams/atl/1980_roster.htm>: GS agree for 50 of 50 players.

Changes from the community order (5):

* WR right: Wallace Francis (16 GS) first, was Alfred Jackson (0 GS)
* OLB right: Robert Pennywell (2 GS) first, was Greg Brezina (not on the PFR roster)
* C: Joel Williams (16 GS) first, was Paul Ryczek (not on the PFR roster)
* G left: Dave Scott (16 GS) first, was Pat Howell (0 GS)
* DT left: Mike Zele (3 GS) first, was Edgar Fields (1 GS)

Community players not on the PFR 1980 roster (18): Tom Moriarty CB [1980: steelers (0 GS)]; Jim Mitchell DE; Mike Lewis DT [1980: packers (0 GS)]; Haskel Stanback FB; Ray Easterling FS; Bubba Bean HB; Brian Cabral ILB [1980: packers (0 GS)]; Mike Scott K; Greg Brezina OLB; Ron McCartney OLB; Mike Scales P; June Jones QB; Norris Johnson SS; Dave O'Sullivan SS; Brian Keating TE; Dennis Pearson WR; Billy Ryckman WR; Paul Ryczek C

Real 1980 starters missing from the community team (4): TE Junior Miller (14 GS); LOLB Al Richardson (16 GS); RILB Buddy Curry (16 GS); RCB Kenny Johnson (16 GS)

### rams

Sources: roster <https://web.archive.org/web/20241207021645/https://www.pro-football-reference.com/teams/ram/1980_roster.htm>, team page <https://web.archive.org/web/20241211131937/https://www.pro-football-reference.com/teams/ram/1980.htm>. Names on the PFR roster: 33 of 53.

Second copy: <https://web.archive.org/web/20230409163018/https://www.pro-football-reference.com/teams/ram/1980_roster.htm>: GS agree for 53 of 53 players.

Changes from the community order (3):

* WR right: Preston Dennard (16 GS) first, was Drew Hill (0 GS)
* HB: Elvis Peacock (9 GS) first, was Wendell Tyler (4 GS)
* DT right: Mike Fanning (3 GS) first, was Jerry Wilkinson (not on the PFR roster)

Community players not on the PFR 1980 roster (20): Jackie Wallace CB; Dwayne O'Steen CB [1980: raiders (6 GS)]; Sidney Justin CB; Ricky Odom CB; Jerry Wilkinson DT [1980: browns (0 GS), 49ers (0 GS)]; Tim Pollard FB; Bruce Hicks G; Lawrence McCutcheon HB [1980: broncos (0 GS), seahawks (1 GS)]; Jim Jodat HB [1980: seahawks (13 GS)]; Ken Clark P; Dave Elmendorf SS; Bill Dunstan SS; Eddie Brown SS; John Williams T; Gordon Gravelle T; Charle Young TE [1980: 49ers (15 GS)]; Woodrow Lyons TE; Ron Jessie WR [1980: bills (3 GS)]; Ron Smith WR [1980: chargers (0 GS)]; Dan Ryczek C

Real 1980 starters missing from the community team (4): TE Victor Hicks (14 GS); RG Doug Smith (8 GS); RDT Cody Jones (15 GS); SS Johnnie Johnson (16 GS)

### saints

Sources: roster <https://web.archive.org/web/20230828001432/https://www.pro-football-reference.com/teams/nor/1980_roster.htm>, team page <https://web.archive.org/web/20241203191825/https://www.pro-football-reference.com/teams/nor/1980.htm>. Names on the PFR roster: 38 of 53.

Second copy: none (no other Wayback capture of the roster page has GS).

Changes from the community order (7):

* CB right: Clarence Chapman (10 GS) first, was David Gray (not on the PFR roster)
* FB: Jack Holmes (7 GS) first, was Tony Galbreath (4 GS)
* OLB left: Ken Bordelon (14 GS) first, was Jim Kovach (2 GS)
* G left: Emanuel Zanders (16 GS) first, was Conrad Dobler (not on the PFR roster)
* T right: Robert Woods (6 GS) first, was Dave Lafary (4 GS)
* DT left: Derland Moore (15 GS) first, was Elex Price (1 GS)
* DT right: Mike Fultz (8 GS) first, was Barry Bennett (6 GS)

Community players not on the PFR 1980 roster (15): David Gray CB; Eric Felton CB [1980: giants (3 GS)]; Leroy Land CB; Kim Jones FB; Ralph McGill FS; Conrad Dobler G [1980: bills (16 GS)]; Jim Merlo ILB; Garo Yepremian K [1980: buccaneers (0 GS)]; John Jordan K; Pat Hughes OLB; Rick Partridge P [1980: chargers (0 GS)]; Don Jauron P; Clarence Smith SS; Roger Finnie T; John Watson T

Real 1980 starters missing from the community team (5): RB Jimmy Rogers (9 GS); RT Stan Brock (12 GS); RDE Tommy Hart (12 GS); RCB Dave Waymer (10 GS); K Benny Ricardo (17 FGA)

### 49ers

Sources: roster <https://web.archive.org/web/20241204142349/https://www.pro-football-reference.com/teams/sfo/1980_roster.htm>, team page <https://web.archive.org/web/20241210042409/https://www.pro-football-reference.com/teams/sfo/1980.htm>. Names on the PFR roster: 31 of 53.

Second copy: <https://web.archive.org/web/20230405184244/https://www.pro-football-reference.com/teams/sfo/1980_roster.htm>: GS agree for 60 of 60 players.

Changes from the community order (9):

* QB: Steve DeBerg (9 GS) first, was Larry Barisich (not on the PFR roster)
* CB left: Charles Cornelius (6 GS) first, was Tony Dungy (not on the PFR roster)
* HB: Lenvil Elliott (3 GS) first, was Phil Francis (1 GS)
* FB: Paul Hofer (6 GS) first, was Wilbur Jackson (not on the PFR roster)
* TE: Eason Ramson (3 GS) first, was Ken MacAfee (not on the PFR roster)
* OLB left: Willie Harper (12 GS) first, was Russell Morton (not on the PFR roster)
* G left: John Ayers (16 GS) first, was Walt Downing (0 GS)
* DT right: Charles Johnson (14 GS) first, was Jimmy Webb (12 GS)
* DE left: Dwaine Board (3 GS) first, was Cedrick Hardman (not on the PFR roster)

Community players not on the PFR 1980 roster (22): Tony Dungy CB; John Bristor CB; Cedrick Hardman DE [1980: raiders (0 GS)]; Al Cowlings DE; Ruben Vaughn DT; Wilbur Jackson FB [1980: washington (14 GS)]; Tim Gray FS; O. J. Simpson HB; Ron Burton HB; Gordy Ceresino ILB; Jeff McIntyre ILB [1980: cardinals (2 GS)]; Bob Nelson ILB [1980: raiders (8 GS)]; Ron Shumon ILB; Mike Hennigan OLB; Russell Morton OLB; Dan Melville P; Larry Barisich QB; Ken MacAfee TE; Paul Seal TE; Eric Bjornson TE; Terry LeCount WR [1980: vikings (0 GS)]; Mike Shumann WR [1980: buccaneers (0 GS)]

Real 1980 starters missing from the community team (8): FB Earl Cooper (16 GS); TE Charle Young (15 GS); LT Ron Singleton (15 GS); RDE Lawrence Pillers (10 GS); RLB Bobby Leopold (15 GS); LCB Ray Rhodes (7 GS); SS Ricky Churchman (15 GS); P Jim Miller (77 punts)

Name matches that are not exact (1): Thomas Seabron = PFR Tom Seabron (nickname)

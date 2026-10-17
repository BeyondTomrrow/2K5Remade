# M6 results: the real 1980 rosters

2026-10-16. Data work only. `src/` was not touched, nothing was built, the game was not run, and
nothing was committed.

## What was wrong

The "1980" community roster is really a late-1970s roster (`M6-starters.md`):
* only 956 of its 1,484 players were on their team's real 1980 roster;
* 528 were on the wrong team or on no 1980 team (Stabler a Raider, Pastorini an Oiler, Staubach
  still the Cowboys' starter);
* 103 real 1980 starters were missing, among them QBs Stabler (HOU), Woodley (MIA) and
  Danielson (DET), plus 8 kickers and 8 punters.

## What was done

1. **New step `build/eras/roster_membership.py`** writes `data/eras/league/rosters_1980.json`.
   * **Membership** comes from PFR's 1980 roster pages. These are the Wayback copies that
     `league_starters.py` cached; each one is sha256-checked against `starters_1980.json`.
     Height, weight, college, experience and birth date are now also parsed from the same tables.
   * **Ratings source** for every real player:
     * first, his record in the 1980 save, on any club slot or among its free agents;
     * otherwise, the nearest season that has him: 1981, 1979, 1982, 1978, then outward.

     Players are matched by name (exact, nickname, initial, or a spelling difference of one or two
     letters), position group, college, jersey and club slot. The saves' colleges are often
     wrong (Vern Den Herder is "Temple", Vince Evans "Arkansas"). So when the colleges disagree,
     a match is accepted only with more evidence: the same jersey, the franchise's own slot, or a
     last name almost nobody else in that save has. Saves more than 4 seasons away need an
     agreeing college; this keeps out a 1993 Mike Williams and a 2004 Michael Smith.
   * **One record per player.** A save record serves only one real player. Where two claimed the
     same record (two real Mark Bells, two Mark Murphys), the better match kept it and the other
     looked again.
   * **No derivation from stats.** No existing tool or doc defines one, so none was used. A player
     with no rating source in any save is left off and listed.
2. **Selection rule** (53 rows per team; the template's player count, which the M2 staging
   requires):
   * A player traded during 1980 belongs to the team where he had more GS, then more G. There
     were 44 such players, and each appears once in the league.
   * Picked in this order:
     1. every player with a 1980 start, plus the kicker with the most FGA and the punter with the
        most punts;
     2. position minimums (QB 2, K 1, P 1, HB 2, FB 1, WR 4, TE 2, T 3, G 3, C 1, DE 3, DT 2,
        OLB 2, ILB 2, CB 3, FS 1, SS 1), filled by games played;
     3. the rest by games played, within position caps (QB 3, K 2, P 2, HB 6, FB 4, WR 7, TE 5,
        OL and DL 6 each, LB 6, CB 7, S 4);
     4. players over a cap;
     5. fillers.
   * **Fillers** fill the slots of a team with fewer than 53 rated real players. They are not 1980
     players of that team: they come from the franchise's own slot in the 1980 save, and are
     players who are on no PFR 1980 roster. They are chosen by position need, preferring players
     who are still in the 1981 save. Each one is flagged `FILLER` in its flags file and in
     PROVENANCE.
3. **Position.** A real player gets PFR's specific 1980 position (Starters table first, then the
   roster table: LCB, FS, NT, ROLB, LT, FB ...) when it is in the same group as the save's code.
   His ratings stay the record's. 185 players changed code this way, for example a 1980 FS the
   save codes as a CB. Across groups the save's code is kept and flagged; Ray Hamilton, the 1980
   NT, stays a G.
4. **`build/eras/make_runtime_package.py`, membership mode.** The tool uses this mode when
   `rosters_1980.json` exists.
   * Each row is read from its save record (that season's payload through RosterDocument, with
     the payload sha256-checked). Names, position, height, weight, skin, face, hand and the 28
     ratings are the record's.
   * The jersey is the PFR 1980 number, and years_pro is PFR's experience ("Rook" = 0).
   * A name the save misspells is written as PFR spells it: Johhny/Johnny Perkins,
     Effren/Efren Herrera, Skledany/Skladany, A./A.J. Duhe.
   * Rows are sorted by position, then by depth order, so the M2 writer's list order and the
     depth file agree.
   * **Depth** follows the real 1980 games started. K goes by most FGA and P by most punts. GS
     count only where the PFR position group fits the game position. Paired chains take their
     sides from PFR's Starters table (LT/RT, LCB/RCB).
   * **New `--out DIR`** (also `ERAS_RUNTIME_OUT`) writes and checks another folder. The new
     package was built in `data/eras/runtime/1980.staging`.
5. **PROVENANCE.txt.** The ROSTERS section now records:
   * the membership source, the selection rule and the totals;
   * every other save used, with its sha256 and row count;
   * the record conflicts and the traded players.

   It also gives, per team:
   * the roster page;
   * the players by rating season;
   * the players moved in, those rated from another season, and the fillers;
   * the players left off and why;
   * every match that is not exact or not strong.

   DEPTH CHART lists each team's chain heads and the real starters still absent. The coaches,
   teams and schedule are unchanged (byte-identical).
6. **Swap.** The old package was kept as **`data/eras/runtime/1980.prev-20261016-132047`**, and
   the staging folder was renamed to `data/eras/runtime/1980`. The two renames ran back to back.

## Checks

* **T0.1** (`python -I build/eras/make_runtime_package.py --check`): **OK**. In membership mode it
  now also checks:
  * every row is one of the team's `rosters_1980.json` entries, with the name, position and PFR
    jersey;
  * no name appears twice in a team (the depth file matches rows by name);
  * no real player is on two teams;
  * the depth rule holds at every chain head, and the row order follows the depth order.

  Output: `28 x 53 rows, 39 fields; positions 0..16, ratings 0..99; QB/K/P in every roster ...
  depth files: 28 ... verified starters rule holds for 28 ... real starters on their team where
  rated: 609 of 616`.
* **M0 tamper tests** (`python -I build/eras/m0_negative_checks.py`): **13 caught, 0 missed**.
  * The roster cases used to look for a hard-coded Norm Thompson row, and he is now a Colts
    filler. They now use whichever player is in colts.csv's first row.
  * Two new membership cases were added: a row renamed in both the roster and the depth file, and
    the QB starter swapped with his backup.
* **Staging constraints:**
  * the roster.csv format (39 fields, latin-1, no commas or quotes);
  * 53 rows per team;
  * position codes 0..16;
  * names at most 31 characters (the M2 arena);
  * QB, K and P present on every team;
  * no duplicate name within a team.

  All hold. M2/M4's 0-mismatch identity check compares the box capture with these CSV rows in
  game, so it needs a game run, which was not done here. The offline preconditions it relies on
  (unique names, the jersey and position in the file) are checked.
* A second regeneration produced the same bytes.

## Totals

| | before (community save) | now |
|---|---|---|
| rows that are real 1980 players of that team | 956 of 1,484 | **1,321 of 1,484** |
| ... the save's own slot player, unchanged team | | 935 |
| ... moved from another slot or the free agents of the 1980 save | | **133** |
| ... added from another season's save | | **253** (1981: 182, 1982: 34, 1978: 12, 1979: 11, 1983: 5, 1984: 3, one each from 1976, 1977, 1985, 1986, 1987, 1990) |
| fillers (not 1980 players of the team, flagged) | 528 wrong-team or retired | **163** |
| real 1980 players with no rating source in any save (left off) | | 117 (7 of them Starters-table starters) |
| PFR Starters-table starters on their team | 529 of 616 | **609 of 616** |
| ... first on the depth chart at their position | | 598 of 616 |
| real 1980 starters missing (starters table + top K + top P) | 103 (87 + 8 K + 8 P) | **10** (7 + 2 K + 1 P; see below) |

Match quality of the 1,321:
* 1,021 exact name with agreeing or unknown college;
* 261 exact name with a disagreeing college, accepted on jersey, own slot or a rare name;
* 12 + 6 nicknames;
* 8 spellings;
* 1 initial;
* 1 alias (Dirt/Dennis Winston);
* 11 exact names whose save position is in another group.

All of these are listed per team below.

## Key starters before and after

First on the depth chart (WR: left and right chains). **Bold** = changed. "(filler)" = not a 1980
player of the team.

| team | QB before | QB now | HB before | HB now | FB before | FB now | WR before | WR now | K before | K now | P before | P now |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| colts | Bert Jones | Bert Jones | Nesby Glasgow | **Joe Washington** | Roosevelt Leaks | **Cleveland Franklin** | Roger Carr, Mike Siani | **Ray Butler, Roger Carr** | Steve Mike-Mayer | Steve Mike-Mayer | Bucky Dilts | **Mike Bragg** |
| bills | Joe Ferguson | Joe Ferguson | Terry Miller | **Joe Cribbs** | Curtis Brown | Curtis Brown | Jerry Butler, Frank Lewis | Jerry Butler, Frank Lewis | Nick Mike-Mayer | Nick Mike-Mayer | Curtis Bryan | **Greg Cater** |
| dolphins | Bob Griese | **David Woodley** | Del Williams | Del Williams | Steve Howell | Steve Howell | Nat Moore, Jimmy Cefalo | Nat Moore, Jimmy Cefalo | Uwe von Schamann | Uwe von Schamann | George Roberts | George Roberts |
| patriots | Steve Grogan | Steve Grogan | Don Calhoun | **Vagas Ferguson** | Mosi Tatupu | **Don Calhoun** | Harold Jackson, Stanley Morgan | **Stanley Morgan, Harold Jackson** | Clarence Lindstrom | **John Smith** | Eddie Hare | Eddie Hare (filler) |
| jets | Richard Todd | Richard Todd | Scott Dierking | Scott Dierking | Woody Bennett | **Kevin Long** | Wesley Walker, Derrick Gaffney | **Lam Jones, Wesley Walker** | Pat Leahy | Pat Leahy | Chuck Ramsey | Chuck Ramsey |
| bengals | Ken Anderson | Ken Anderson | Charles Alexander | Charles Alexander | Pete Johnson | Pete Johnson | Isaac Curtis, Don Bass | Isaac Curtis, Don Bass | Chris Bahr | **Jim Breech** | Pat McInally | Pat McInally |
| browns | Brian Sipe | Brian Sipe | Greg Pruitt | Greg Pruitt | Mike Pruitt | Mike Pruitt | Dave Logan, Reggie Rucker | Dave Logan, Reggie Rucker | Don Cockroft | Don Cockroft | Johnny Evans | Johnny Evans |
| oilers_titans | Dan Pastorini | **Ken Stabler** | Earl Campbell | Earl Campbell | Tim Wilson | Tim Wilson | Mike Renfro, Billy Johnson | Mike Renfro, Billy Johnson | Toni Fritsch | Toni Fritsch | Cliff Parsley | Cliff Parsley |
| steelers | Terry Bradshaw | Terry Bradshaw | Franco Harris | **Rocky Bleier** | Anthony Anderson | **Franco Harris** | Lynn Swann, Jim Smith | Lynn Swann, Jim Smith | Matt Bahr | Matt Bahr | Craig Colquitt | Craig Colquitt |
| broncos | Craig Morton | Craig Morton | Jim Jensen | **Dave Preston** | Dave Preston | **Jim Jensen** | Rick Upchurch, Haven Moses | Rick Upchurch, Haven Moses | Fred Steinfort | Fred Steinfort | Luke Prestridge | Luke Prestridge |
| chiefs | Steve Fuller | Steve Fuller | Ted McKnight | **Tony Reed** | Don Greene | **Ted McKnight** | Henry Marshall, Stan Rome | **Henry Marshall, J.T. Smith** | Jan Stenerud | **Nick Lowery** | Bob Grupp | Bob Grupp |
| raiders | Jim Plunkett | Jim Plunkett | Mark van Eeghen | **Kenny King** | Carl Laidlaw | **Mark van Eeghen** | Cliff Branch, Rich Martini | **Bob Chandler, Cliff Branch** | Bob Barefield | **Chris Bahr** | Ray Guy | Ray Guy |
| chargers | Dan Fouts | Dan Fouts | Clarence Williams | Clarence Williams | Hank Bauer | **John Cappelletti** | Charlie Joiner, John Jefferson | **John Jefferson, Charlie Joiner** | Rolf Benirschke | Rolf Benirschke | Jeff West | **Rick Partridge** |
| seahawks | Jim Zorn | Jim Zorn | Dan Doornink | Dan Doornink | Tony Benjamin | **Jim Jodat** | Steve Largent, Sam McCullum | Steve Largent, Sam McCullum | Effren Herrera | **Efren Herrera** | Herman Weaver | Herman Weaver |
| cowboys | Danny White | Danny White | Tony Dorsett | Tony Dorsett | Robert Newhouse | Robert Newhouse | Drew Pearson, Tony Hill | Drew Pearson, Tony Hill | Rafael Septien | Rafael Septien | Reggie Septien | Reggie Septien (filler) |
| giants | Phil Simms | Phil Simms | Eddie Hicks | **Billy Taylor** | Billy Taylor | **Bo Matthews** | Johhny Perkins, Earnest Gray | **Earnest Gray, Danny Pittman** | Joe Danelo | Joe Danelo | Dave Jennings | Dave Jennings |
| eagles | Ron Jaworski | Ron Jaworski | Leroy Harris | **Wilbert Montgomery** |  | **Leroy Harris** | Harold Carmichael, Charlie Smith | Harold Carmichael, Charlie Smith | Tony Franklin | Tony Franklin | Max Runager | Max Runager |
| cardinals | Jim Hart | Jim Hart | Ottis Anderson | Ottis Anderson | Wayne Morris | Wayne Morris | Mel Gray, Pat Tilley | Mel Gray, Pat Tilley | Don Lathrop | **Neil O'Donoghue** | Vern Den Herron | **Larry Swider** |
| washington | Joe Theismann | Joe Theismann | Clarence Harmon | **Wilbur Jackson** | Ricky Williams | **Clarence Harmon** | Ricky Thompson, John McDaniel | **Ricky Thompson, Art Monk** | Mark Moseley | Mark Moseley | Mike Bragg | **Mike Connell** |
| bears | Vince Evans | Vince Evans | Walter Payton | Walter Payton | Pat Jones | **Roland Harper** | James Scott, Brian Baschnagel | **Brian Baschnagel, James Scott** | Bob Thomas | Bob Thomas | Bob Parsons | Bob Parsons |
| lions | Jeff Komlo | **Gary Danielson** | Dexter Bussey | **Billy Sims** | Bob Fernandes | **Dexter Bussey** | Leonard Thompson, Freddie Scott | **Freddie Scott, Leonard Thompson** | Benny Ricardo | **Eddie Murray** | Larry Swider | **Tom Skladany** |
| packers | Lynn Dickey | Lynn Dickey | Steve Atkins | **Eddie Lee Ivery** |  | **Gerry Ellis** | James Lofton, Aundra Thompson | James Lofton, Aundra Thompson | Chester Marcol | **Jan Stenerud** | David Beverly | David Beverly |
| vikings | Tommy Kramer | Tommy Kramer | Rickey Young | Rickey Young |  | **Ted Brown** | Ahmad Rashad, Sammy White | Ahmad Rashad, Sammy White | Rick Danmeier | Rick Danmeier | Greg Coleman | Greg Coleman |
| buccaneers | Doug Williams | Doug Williams | Jerry Eckwood | Jerry Eckwood | Ricky Bell | Ricky Bell | Gordon Jones, Isaac Hagins | **Isaac Hagins, Gordon Jones** | Neil O'Donoghue | **Garo Yepremian** | Tom Blanchard | Tom Blanchard |
| falcons | Steve Bartkowski | Steve Bartkowski | William Andrews | **Lynn Cain** | James Mayberry | **William Andrews** | Alfred Jenkins, Wallace Francis | **Wallace Francis, Alfred Jenkins** | Tim Mazzetti | Tim Mazzetti | John James | John James |
| rams | Vince Ferragamo | Vince Ferragamo | Elvis Peacock | Elvis Peacock | Cullen Bryant | Cullen Bryant | Billy Waddy, Preston Dennard | **Preston Dennard, Billy Waddy** | Frank Corral | Frank Corral | Ken Clark | Ken Clark (filler) |
| saints | Archie Manning | Archie Manning | Chuck Muncie | **Jimmy Rogers** | Jack Holmes | Jack Holmes | Wes Chandler, Ike Harris | Wes Chandler, Ike Harris | Garo Yepremian | **Benny Ricardo** | Russell Erxleben | Russell Erxleben |
| 49ers | Steve DeBerg | Steve DeBerg | Lenvil Elliott | **Paul Hofer** | Paul Hofer | **Earl Cooper** | Freddie Solomon, Dwight Clark | Freddie Solomon, Dwight Clark | Ray Wersching | Ray Wersching | Dan Melville | **Jim Miller** |

The fixes asked for:
* Oilers QB Ken Stabler (16 GS; his record moved over from the 1980 save's Raiders);
* Dolphins QB David Woodley (rated from the 1981 save);
* Lions QB Gary Danielson.

Pastorini is now a Raider, but Plunkett starts there (11 GS against 5). Roger Staubach (retired)
is off the Cowboys, and Bob Griese (3 GS) is Woodley's backup. These kickers and punters now start for their real teams:
* K: Chris Bahr (Raiders), Nick Lowery (Chiefs), Neil O'Donoghue (Cardinals), Eddie Murray
  (Lions), Garo Yepremian (Buccaneers), Benny Ricardo (Saints), Jan Stenerud (Packers) and Jim
  Breech (Bengals);
* P: Mike Bragg (Colts), Greg Cater (Bills), Rick Partridge (Chargers), Larry Swider (Cardinals),
  Mike Connell (Washington), Tom Skladany (Lions) and Jim Miller (49ers).

## Still missing or flagged

**Real 1980 starters with no rating source in any community save** (left off; the next real
player starts):
* SS Ricky Churchman (49ers);
* SS Greg Bright (Bengals);
* LILB Robert Jackson (Browns; the save's Robert Jackson is a Duke guard, a different player);
* TE Mike Williams (Chiefs; the only Mike Williams TE in the saves is a 1993 Tennessee player);
* FS Gary Woolford (Giants);
* TE Victor Hicks (Rams);
* C Bob Kuziel (Washington).

**Kickers and punters:**
* No rating source: Bengals K Ian Sunter (20 FGA; Jim Breech kicks), Packers K Tom Birney (12 FGA;
  Jan Stenerud kicks) and Patriots P Mike Hubach (63 punts).
* The Patriots' P slot is a filler, Eddie Hare, the save's Patriots punter.

**Real punters coded at another position:**
* Cowboys: Danny White punted 71 times, and he is the QB.
* Rams: Frank Corral punted 76 times, and he is the K.

A row has only one position, so a filler P holds the P slot on these two teams (Cowboys "Reggie
Septien", Rams Ken Clark).

**Real starters on the team but not first at their position** (18 in all; the 7 missing ones above
plus these 11):
* The save puts them in another position group, so their GS do not count there. Their ratings are
  for that position:
  * Colts FS Nesby Glasgow (HB in the save);
  * Broncos FS Steve Foley (TE);
  * Saints RCB Dave Waymer (HB);
  * Patriots NT Ray Hamilton (G);
  * Bears TE Robin Earl (HB);
  * Dolphins WR Bruce Hardy (TE).
* They are second behind a player with more 1980 GS at that position: Jets TE Mickey Shuler and LG
  Randy Rasmussen, Washington RDT Perry Brooks, Rams RG Doug Smith and Saints FB Wayne Wilson.

**Fillers.** There are 163 fillers. For example, the Cowboys have 9, including Cliff Harris and
Rayfield Wright (both retired), and Washington has 7, including John Riggins (sat out 1980). Every
filler has 0 GS, so each one is last at his position, except where no real player holds it:
* Bengals SS (Greg Bright missing);
* the second DT chain head of the 3-4 Browns;
* the three P slots above.

All of them are listed per team below.

Other limits:
* GS is the player's season total, not his GS at the game position.
* Ratings from another season are that season's ratings (182 of them from 1981).
* The college check depends on the saves' colleges, which are often wrong. Every accepted match
  whose college differs is listed.

## How to rebuild

    python -I build/eras/league_starters.py              # starters_1980.json (PFR, cached)
    python -I build/eras/roster_membership.py            # rosters_1980.json (membership + rating sources)
    python -I build/eras/make_runtime_package.py [--out DIR]   # the package, then T0.1
    python -I build/eras/m0_negative_checks.py           # tamper tests (ERAS_RUNTIME_OUT=DIR for another folder)

Files changed:
* `build/eras/roster_membership.py` (new);
* `build/eras/make_runtime_package.py` (membership mode, `--out`, T0.1 membership checks);
* `build/eras/m0_negative_checks.py`;
* `data/eras/league/rosters_1980.json` (new);
* `data/eras/runtime/1980/` (rosters, flags, depth and PROVENANCE regenerated; teams, schedule
  and coaches byte-identical);
* `data/eras/runtime/1980.prev-20261016-132047/` (the old package, kept).

Blocked: nothing.

## Per team: every flagged player

### colts

Roster page: <https://web.archive.org/web/20250126220236/https://www.pro-football-reference.com/teams/clt/1980_roster.htm>. 46 real players (rated from: 1978 save 1, 1980 save 36, 1981 save 8, 1982 save 1), 7 fillers, 4 real players left off.

* **Missing, no rating source in any save (2):** Steve Heimkreiter LB (15 G, 0 GS); Ron LaPointe TE (2 G, 0 GS)
* **Left off for other reasons (2):** Zachary Dixon RB (1 G, 0 GS): traded: counted with eagles (more GS/G there); Jim Krahl DT (2 G, 0 GS): traded: counted with 49ers (more GS/G there)
* **Fillers (not 1980 players of this team) (7):** Mike Jones CB; Ron Fernandes DE (not in the 1981 save); Steve Zabel OLB (not in the 1981 save); Jerry Golsteyn QB (not in the 1981 save); Norm Thompson CB (not in the 1981 save); George Gross DT (not in the 1981 save); Don Hardeman FB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (4):** Mike Bragg (WAS); Mike Barnes (NE); Ricky Jones (CLE); Bob Raba (NYJ)
* **Rated from another season (10):** Derrick Hatchett (1981, IND); Ray Butler (1981, IND); Cleveland Franklin (1982, IND); George Kunz (1978, BAL); Curtis Dickey (1981, IND); Marvin Sims (1981, IND); Ed Smith (1981, IND); Kim Anderson (1981, IND); Chris Foote (1981, IND); Ray Donaldson (1981, IND)
* **Identity flags (12):** Fred Cook (college PFR Southern Miss / save Penn State); Nesby Glasgow (save position HB, PFR FS: his GS do not count there); Ray Butler (college PFR USC / save Miami (Fla.)); Robert Pratt (nickname match to save "Bob Pratt"); Bruce Laird (college PFR American Int. / save Temple); Larry Braziel (college PFR USC / save Princeton); Mike Barnes (college PFR Miami (FL) / save Arkansas); Sanders Shiver (college PFR Carson-Newman / save Massachusetts); Greg Fields (college PFR Hartnell,Grambling St. / save Boston College); Brian DeRoo (college PFR Redlands / save Florida); Mack Alston (college PFR MD-Eastern Shore / save Utah); Chris Foote (college PFR USC / save North Carolina)
* **Position from PFR (save code in the same group) (8):** Ed Simonini ILB (save OLB); Bruce Laird SS (save CB); Larry Braziel CB (save FS); Sanders Shiver OLB (save ILB); Cleveland Franklin FB (save HB); Barry Krauss OLB (save ILB); Chris Foote T (save C); Bob Van Duyne T (save G)

### bills

Roster page: <https://web.archive.org/web/20241212013129/https://www.pro-football-reference.com/teams/buf/1980_roster.htm>. 49 real players (rated from: 1980 save 41, 1981 save 6, 1982 save 2), 4 fillers, 1 real players left off.

* **Left off for other reasons (1):** Roland Solomon DB (1 G, 0 GS): traded: counted with cowboys (more GS/G there)
* **Fillers (not 1980 players of this team) (4):** Mike Collier HB; Tony Greene SS; Joseph Shipp TE; Fernando Moody FB
* **Moved in (1980 save had him elsewhere) (8):** Conrad Dobler (NO); Ron Jessie (STL); Rufus Bess (OAK); David Humm (OAK); Duke Fergerson (SEA); Phil Villapiano (OAK); Roosevelt Leaks (IND); Artie Owens (SD)
* **Rated from another season (8):** Greg Cater (1981, BUF); Joe Cribbs (1981, BUF); Bill Simpson (1982, BUF); Mark Brammer (1981, BUF); Jim Ritcher (1981, BUF); Ervin Parker (1981, BUF); Darrell Irvin (1982, BUF); Rod Kush (1981, BUF)
* **Identity flags (10):** Charles Romes (college PFR North Carolina Central / save North Carolina State); Jim Haslett (college PFR Indiana (PA) / save Temple); Frank Lewis (college PFR Grambling St. / save Bowling Green); Rufus Bess (college PFR South Carolina St. / save Ohio State); Roland Hooks (college PFR North Carolina St. / save Penn State); Duke Fergerson (spelling match to save "Duke Ferguson"); Dee Hardison (save position DE, PFR T: his GS do not count there); Ervin Parker (college PFR South Carolina St. / save South Dakota); Lou Piccone (college PFR West Liberty St. / save Arkansas); Doug Greene (college PFR Texas A&M-Kingsville / save Michigan)
* **Position from PFR (save code in the same group) (11):** Isiah Robertson OLB (save ILB); Jim Haslett ILB (save OLB); Lucius Sanford OLB (save ILB); Shane Nelson ILB (save OLB); Steve Freeman SS (save CB); Bill Simpson FS (save CB); Jon Borchardt T (save G); Jim Ritcher G (save T); Mike Kadish DT (save DE); Ken Johnson DE (save DT); Tim Vogler G (save T)

### dolphins

Roster page: <https://web.archive.org/web/20241203111303/https://www.pro-football-reference.com/teams/mia/1980_roster.htm>. 51 real players (rated from: 1978 save 1, 1980 save 42, 1981 save 5, 1982 save 3), 2 fillers, 2 real players left off.

* **Missing, no rating source in any save (1):** Jeff Allen DB (16 G, 0 GS)
* **Left off for other reasons (1):** Rod Walters G (1 G, 0 GS): traded: counted with chiefs (more GS/G there)
* **Fillers (not 1980 players of this team) (2):** Mike Kozlowski CB; Cal Bell FS (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (7):** Terry Robiskie (OAK); Ed Taylor (NYJ); Woody Bennett (NYJ); Dwight Stephenson (IND); Doug Beaudoin (NE); Rick Moser (PIT); Thom Dornbrook (PIT)
* **Rated from another season (9):** Don McNeal (1981, MIA); David Woodley (1981, MIA); Elmer Bailey (1981, MIA); Bill Barnett (1982, MIA); Joe Rose (1981, MIA); Nick Giaquinto (1982, WAS); Steve Shull (1981, MIA); Don Testerman (1978, SEA); William Cesare (1982, DET)
* **Identity flags (10):** Doug Betters (college PFR Montana,Nevada / save Texas); Vern Den Herder (college PFR Central College (IA) / save Temple); A.J. Duhe (initial match to save "A. Duhe"); Delvin Williams (nickname match to save "Del Williams"); Woody Bennett (college PFR Miami (FL) / save Washington State); Earnest Rhone (college PFR Henderson St. / save Bethune-Cookman); Dwight Stephenson (college PFR Alabama / save Southern Mississippi); Doug Beaudoin (spelling match to save "Doug Beaudion"); Rick Moser (college PFR Rhode Island / save Notre Dame); William Cesare (nickname match to save "Billy Cesare")
* **Position from PFR (save code in the same group) (6):** Bob Kuechenberg G (save T); Glenn Blackwood SS (save CB); Don Bessillieu FS (save CB); Ralph Ortega ILB (save OLB); Terry Robiskie FB (save HB); Bill Barnett DE (save DT)

### patriots

Roster page: <https://web.archive.org/web/20240530184408/https://www.pro-football-reference.com/teams/nwe/1980_roster.htm>. 45 real players (rated from: 1979 save 1, 1980 save 36, 1981 save 4, 1982 save 1, 1983 save 2, 1984 save 1), 8 fillers, 3 real players left off.

* **Missing, no rating source in any save (2):** Mike Hubach P (16 G, 0 GS); Doug McDougald DE (8 G, 0 GS)
* **Left off for other reasons (1):** Prentice McCray DB (3 G, 0 GS): traded: counted with lions (more GS/G there)
* **Fillers (not 1980 players of this team) (8):** Eddie Hare P (not in the 1981 save); Mark Buben DE; Sam Cunningham FB; Dick Conn CB (not in the 1981 save); Mark Washington CB (not in the 1981 save); Sam Hunt OLB (not in the 1981 save); Tom Owen QB (not in the 1981 save); Al Chandler TE (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (2):** Bill Currier (TEN); Chuck Foreman (MIN)
* **Rated from another season (9):** John Smith (1979, NE); Vagas Ferguson (1983, CLE); Sam Adams (1981, NO); Mike Haynes (1983, OAK); Roland James (1981, NE); Bob Golic (1981, NE); Steve McMichael (1981, CHI); Larry McGrew (1984, NE); Preston Brown (1982, NE)
* **Identity flags (9):** John Smith (college PFR King Alfred's (England) / save Texas A&M); Shelby Jordan (college PFR Washington (MO) / save Tennessee); Ray Hamilton (save position G, PFR NT: his GS do not count there); John Zamberlin (college PFR Pacific Lutheran / save Jackson State); Mel Lunsford (college PFR Central State (OH) / save Pittsburgh); Carlos Pennywell (college PFR Grambling St. / save Michigan State); Garry Puetz (nickname match to save "Gary Puetz", college PFR Valparaiso / save Sacramento State); Mosi Tatupu (college PFR USC / save Texas); Larry McGrew (college PFR USC / save Oklahoma)
* **Position from PFR (save code in the same group) (10):** Don Calhoun FB (save HB); Dwight Wheeler T (save C); Raymond Clayborn CB (save FS); Tim Fox FS (save SS); Sam Adams G (save C); Rick Sanford SS (save CB); Roland James SS (save CB); Richard Bishop DE (save DT); Bob Cryder G (save T); Garry Puetz T (save C)

### jets

Roster page: <https://web.archive.org/web/20231224203914/https://www.pro-football-reference.com/teams/nyj/1980_roster.htm>. 46 real players (rated from: 1980 save 37, 1981 save 8, 1984 save 1), 7 fillers, 7 real players left off.

* **Missing, no rating source in any save (5):** Bobby Batton RB (8 G, 0 GS); Steve Carpenter DB (3 G, 0 GS); Kenny Lewis RB (7 G, 0 GS); Wes Roberts DE (6 G, 0 GS); John Sullivan LB (16 G, 0 GS)
* **Left off for other reasons (2):** Woody Bennett RB (10 G, 0 GS): traded: counted with dolphins (more GS/G there); Lawrence Pillers DE (3 G, 1 GS): traded: counted with 49ers (more GS/G there)
* **Fillers (not 1980 players of this team) (7):** Johnny Lynn CB; John Hennessy OLB (not in the 1981 save); James Turner SS (not in the 1981 save); Joe Pellegrini DT (not in the 1981 save); Dave Jacobs K (not in the 1981 save); Bob Martin OLB (not in the 1981 save); Jim Hampton G (not in the 1981 save)
* **Rated from another season (9):** Darrol Ray (1981, NYJ); Lam Jones (1981, NYJ); Guy Bingham (1981, NYJ); Jesse Johnson (1981, NYJ); Lance Mehl (1981, NYJ); Jerry Holmes (1981, NYJ); Chris Godfrey (1984, NYG); Gerald Carter (1981, TB); Saladin Martin (1981, SF)
* **Identity flags (9):** Donald Dykes (college PFR Southeastern Louisiana / save Texas Southern); Mark Gastineau (college PFR Eastern Arizona,Arizona St.,East Central (OK) / save Penn State); Marvin Powell (college PFR USC / save Maryland); Joe Fields (college PFR Rutgers-Camden,Widener / save USC); Randy Rasmussen (college PFR Nebraska-Kearney / save Minnesota); Bobby Jones (college PFR Youngstown St.,Millikin / save Kentucky); Bruce Harper (college PFR Kutztown (PA) / save Syracuse); Paul Darby (college PFR Texas St. / save Ohio State); Chris Godfrey (save position G, PFR DE: his GS do not count there)
* **Position from PFR (save code in the same group) (8):** Darrol Ray FS (save CB); Marty Lyons DT (save DE); Stan Blinka ILB (save OLB); Bobby Jackson CB (save SS); Joe Klecko DE (save DT); Chris Ward T (save G); Greg Buttle OLB (save ILB); Kevin Long FB (save HB)

### bengals

Roster page: <https://web.archive.org/web/20250819211321/https://www.pro-football-reference.com/teams/cin/1980_roster.htm>. 44 real players (rated from: 1980 save 36, 1981 save 6, 1982 save 2), 9 fillers, 8 real players left off.

* **Missing, no rating source in any save (7):** Alton Alexis WR (1 G, 0 GS); Greg Bright SS (16 G, 16 GS, **1980 starter**); William Glass G (15 G, 0 GS); Jo Heath DB (10 G, 0 GS); Andrew Melontree LB (16 G, 0 GS); Ian Sunter K (10 G, 0 GS); Sandro Vitiello K (2 G, 0 GS)
* **Left off for other reasons (1):** Shafer Suggs DB (4 G, 0 GS): traded: counted with jets (more GS/G there)
* **Fillers (not 1980 players of this team) (9):** Mark Donahue G; Archie Williams SS (not in the 1981 save); Scott Burk CB; Billy Brooks WR; Tom Ruud OLB; Bob Johnson C; Leonard Mitchell DE; Lee Roy Mendenhall SS (not in the 1981 save); Cliff Cornelius CB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (2):** Jim Breech (OAK); Mike St. Clair (CLE)
* **Rated from another season (8):** Anthony Munoz (1981, CIN); Bryan Hicks (1981, CIN); Blake Moore (1981, CIN); Ron Simpkins (1982, CIN); M.L. Harris (1981, CIN); Cleo Montgomery (1982, OAK); Rick Razzano (1981, CIN); Rod Horn (1981, CIN)
* **Identity flags (10):** Anthony Munoz (college PFR USC / save Augustana (SD)); Dan Ross (college PFR Northeastern / save Arkansas); Eddie Edwards (college PFR Miami (FL) / save Illinois); Ken Anderson (college PFR Augustana (IL) / save Wyoming); Louis Breeden (college PFR North Carolina Central / save Southern Mississippi); Blake Moore (college PFR Wooster / save West Virginia); Mike White (college PFR Albany State (GA) / save Michigan); Deacon Turner (college PFR San Diego St. / save Michigan); Steve Kreider (college PFR Lehigh / save Oklahoma); Mike St. Clair (college PFR Iowa Central,Grambling St. / save Miami (Fla.))
* **Position from PFR (save code in the same group) (8):** Eddie Edwards DE (save DT); Max Montoya G (save T); Glenn Cameron ILB (save OLB); Ray Griffin CB (save FS); Dick Jauron FS (save SS); Bryan Hicks FS (save CB); Dave Lapham T (save G); Mike White DE (save DT)

### browns

Roster page: <https://web.archive.org/web/20241215043057/https://www.pro-football-reference.com/teams/cle/1980_roster.htm>. 45 real players (rated from: 1980 save 35, 1981 save 5, 1982 save 2, 1983 save 2, 1990 save 1), 8 fillers, 4 real players left off.

* **Missing, no rating source in any save (4):** Bill Cowher LB (16 G, 2 GS); Ron Crews NT (16 G, 0 GS); Robert Jackson LILB (14 G, 14 GS, **1980 starter**); John Mohring LB (14 G, 0 GS)
* **Fillers (not 1980 players of this team) (8):** Mickey Sims DT; Randy Rich FS; George Buehler G; Gerald Irons ILB; Dave Graf ILB; Ernie Johnson SS; Matt Miller T; Lyle Alzedo DE
* **Moved in (1980 save had him elsewhere) (5):** Ron Bolton (free agent); Joe DeLamielleure (BUF); Don Goode (SD); Autry Beamon (SEA); Jerry Wilkinson (STL)
* **Rated from another season (10):** Lyle Alzado (1983, OAK); Marshall Harris (1982, CLE); Charles White (1981, CLE); Robert Jackson (1983, CLE); Paul McDonald (1981, CLE); McDonald Oden (1981, CLE); Elvis Franks (1981, CLE); Judson Flint (1981, CLE); Cliff Odom (1982, IND); Joel Patten (1990, SD)
* **Identity flags (13):** Don Cockroft (college PFR Adams St. / save Ohio State); Johnny Evans (college PFR North Carolina St. / save Ohio State); Lyle Alzado (college PFR Kilgore College,Yankton / save Youngstown State); Reggie Rucker (college PFR Boston Univ. / save Western Illinois); Ron Bolton (college PFR Norfolk St. / save Eastern Washington); Henry Sheppard (college PFR SMU / save Oklahoma); Clay Matthews (college PFR USC / save Oregon State); Charles White (college PFR USC / save Boston College); Paul McDonald (college PFR USC / save Louisiana Tech); Ricky Feacher (college PFR Miss. Valley St. / save Syracuse); Cleo Miller (college PFR Ark-Pine Bluff / save Auburn); Dino Hall (college PFR Rowan / save Virginia Tech); Autry Beamon (college PFR Texas A&M-Commerce / save Northwestern)
* **Position from PFR (save code in the same group) (7):** Cody Risien T (save G); Henry Sheppard G (save T); Clinton Burrell CB (save SS); Clay Matthews OLB (save ILB); Jerry Sherk DE (save DT); Jerry Wilkinson DE (save DT); Joel Patten T (save G)

### oilers_titans

Roster page: <https://web.archive.org/web/20240526054753/https://www.pro-football-reference.com/teams/oti/1980_roster.htm>. 50 real players (rated from: 1979 save 1, 1980 save 43, 1981 save 5, 1982 save 1), 3 fillers, 3 real players left off.

* **Missing, no rating source in any save (1):** Craig Bradshaw QB (2 G, 0 GS)
* **Left off for other reasons (2):** Chester Marcol K (1 G, 0 GS): traded: counted with packers (more GS/G there); Guido Merkens FS (3 G, 0 GS): traded: counted with saints (more GS/G there)
* **Fillers (not 1980 players of this team) (3):** Charles Jefferson CB (not in the 1981 save); Willie Alexander CB (not in the 1981 save); George Reihner G (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (6):** Ken Stabler (OAK); Dave Casper (OAK); Jack Tatum (OAK); Thomas Henderson (DAL); Sammy Green (SEA); Charles Davis (ARZ)
* **Rated from another season (7):** Bob Young (1979, STL); Angelo Fields (1981, TEN); Adger Armstrong (1981, TEN); Carl Roaches (1981, TEN); John Corker (1981, TEN); Tim Smith (1982, TEN); Greg Davidson (1981, TEN)
* **Identity flags (14):** Carl Mauck (college PFR Southern Illinois / save Texas Southern); Mike Barber (college PFR Louisiana Tech / save Louisiana State); Mike Reinfeldt (college PFR Wisconsin?Milwaukee / save Mississippi); Ted Washington (college PFR Miss. Valley St. / save Florida); Ken Kennard (college PFR Angelo State (TX) / save Maryland); Billy Johnson (college PFR Widener / save Missouri); Jesse Baker (college PFR Jacksonville St. / save Western Kentucky); Ted Thompson (college PFR SMU / save Oklahoma State); Rob Carpenter (college PFR Miami (OH) / save Arizona State); Gifford Nielsen (college PFR BYU / save Jackson State); Mike Stensrud (college PFR Iowa St. / save Bethune-Cookman); Carter Hartwig (college PFR USC / save Iowa State); Thomas Henderson (nickname match to save "Tom Henderson"); Charles Davis (nickname match to save "Charlie Davis")
* **Position from PFR (save code in the same group) (6):** Mike Reinfeldt FS (save SS); Vernon Perry SS (save FS); Bob Young G (save C); Daryl Hunt ILB (save OLB); Conway Hayman T (save G); Mike Stensrud DE (save DT)

### steelers

Roster page: <https://web.archive.org/web/20260425095904/https://www.pro-football-reference.com/teams/pit/1980_roster.htm>. 49 real players (rated from: 1976 save 1, 1980 save 39, 1981 save 9), 4 fillers, 0 real players left off.

* **Fillers (not 1980 players of this team) (4):** Doug Buffone OLB; Greg Grupp DE (not in the 1981 save); Tom Graves ILB (not in the 1981 save); Damien Staubach SS (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (1):** Tom Moriarty (ATL)
* **Rated from another season (10):** Ray Pinney (1981, PIT); Tyrone McGriff (1981, PIT); Russell Davis (1981, PIT); Cliff Stoudt (1981, PIT); Craig Wolfley (1981, PIT); Frank Pollard (1981, PIT); Calvin Sweeney (1981, PIT); Tunch Ilkin (1981, PIT); Marvin Cobb (1976, CIN); Mark Malone (1981, PIT)
* **Identity flags (11):** Donnie Shell (college PFR South Carolina St. / save Florida State); Larry Brown (college PFR Kansas / save Colorado); Steve Furness (college PFR Rhode Island / save New Hampshire); L.C. Greenwood (college PFR Ark-Pine Bluff / save BYU); Lynn Swann (college PFR USC / save Colorado); Dirt Winston (alias match to save "Dennis Winston"); Dwight White (college PFR Texas A&M-Commerce / save Arizona); Gary Dunn (college PFR Miami (FL) / save South Carolina); Calvin Sweeney (college PFR UC Riverside,USC / save Brown); Marvin Cobb (college PFR USC / save Purdue); Tom Moriarty (college PFR Bowling Green / save Houston)
* **Position from PFR (save code in the same group) (5):** Ray Pinney G (save T); Mike Wagner FS (save CB); Franco Harris FB (save HB); John Banaszak DE (save DT); Tunch Ilkin G (save T)

### broncos

Roster page: <https://web.archive.org/web/20241203230530/https://www.pro-football-reference.com/teams/den/1980_roster.htm>. 47 real players (rated from: 1980 save 39, 1981 save 6, 1982 save 2), 6 fillers, 6 real players left off.

* **Missing, no rating source in any save (4):** Jeff Knapple QB (2 G, 0 GS); Ben Norman RB (3 G, 0 GS); Laval Short NT (15 G, 0 GS); Art Smith LB (2 G, 0 GS)
* **Left off for other reasons (2):** Bill Larson TE (2 G, 0 GS): traded: counted with packers (more GS/G there); Lawrence McCutcheon RB (6 G, 0 GS): traded: counted with seahawks (more GS/G there)
* **Fillers (not 1980 players of this team) (6):** John Grant DT; Norris Weese QB; Chris Pane CB; Larry Canada HB; Bob Swenson ILB; Jim Turner K
* **Moved in (1980 save had him elsewhere) (5):** Aaron Kyle (DAL); Matt Robinson (NYJ); Perry Smith (ARZ); Emery Moorehead (NYG); Larry Brunson (OAK)
* **Rated from another season (8):** Maurice Harvey (1981, GB); Rulon Jones (1981, DEN); Greg Boyd (1982, DEN); Keith Bishop (1982, DEN); Mike Harden (1981, DEN); Greg Bracelin (1981, OAK); Arland Thompson (1981, GB); James Wright (1981, DEN)
* **Identity flags (7):** Barney Chavous (college PFR South Carolina St. / save Illinois); Bill Thompson (nickname match to save "Billy Thompson", college PFR MD-Eastern Shore / save Florida); Rubin Carter (college PFR Miami (FL) / save Louisiana Tech); Steve Foley (save position TE, PFR FS/RCB: his GS do not count there); Paul Howard (college PFR BYU / save USC); Joe Rizzo (college PFR Merchant Marine / save Louisiana Tech); Don Latimer (college PFR Miami (FL) / save Cincinnati)
* **Position from PFR (save code in the same group) (4):** Bill Thompson SS (save CB); Jim Jensen FB (save HB); Dave Preston HB (save FB); Glenn Hyde G (save C)

### chiefs

Roster page: <https://web.archive.org/web/20241203110535/https://www.pro-football-reference.com/teams/kan/1980_roster.htm>. 49 real players (rated from: 1980 save 38, 1981 save 10, 1986 save 1), 4 fillers, 7 real players left off.

* **Missing, no rating source in any save (6):** Tom Clements QB (1 G, 0 GS); Bubba Garcia WR (5 G, 0 GS); Kelly Kirchbaum LB (1 G, 0 GS); Clarence Sanders LB (1 G, 0 GS); Franky Smith T (4 G, 2 GS); Mike Williams TE (16 G, 11 GS, **1980 starter**)
* **Left off for other reasons (1):** Brad Oates T (1 G, 0 GS): traded: counted with cardinals (more GS/G there)
* **Fillers (not 1980 players of this team) (4):** Johnnie Dirden WR; Don Greene FB (not in the 1981 save); Horace Perkins FS (not in the 1981 save); Dave Rozumek ILB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (3):** Ed Beckman (free agent); Al Dixon (NYG); Jerry Meyers (CHI)
* **Rated from another season (11):** Nick Lowery (1981, KC); J.T. Smith (1981, KC); Eric Harris (1981, KC); Jim Rourke (1981, KC); James Hadnot (1981, KC); Brad Budde (1981, KC); Bill Kenney (1981, KC); Carlos Carson (1981, KC); Dino Mangiero (1981, KC); Paul Dombroski (1981, NE); Donovan Rose (1986, MIA)
* **Identity flags (8):** Gary Barbaro (college PFR Nicholls St. / save Colorado); Ted McKnight (college PFR Minnesota-Duluth / save Duke); Dave Lindstrom (college PFR Boston Univ. / save Morris Brown); James Hadnot (nickname match to save "Jim Hadnot"); Horace Belton (college PFR Southeastern Louisiana / save Hawaii); Brad Budde (college PFR USC / save North Texas); Paul Dombroski (college PFR Hawaii,Linfield / save Marshall); Arnold Morgado (college PFR Michigan St.,Hawaii / save South Florida)
* **Position from PFR (save code in the same group) (9):** Gary Barbaro FS (save SS); Gary Spani ILB (save OLB); Herb Christopher SS (save CB); Ted McKnight FB (save HB); Tom Howard OLB (save ILB); Cal Peterson OLB (save ILB); Charlie Getty T (save G); Rod Walters G (save T); Ken Kremer DE (save DT)

### raiders

Roster page: <https://web.archive.org/web/20250221225348/https://www.pro-football-reference.com/teams/rai/1980_roster.htm>. 47 real players (rated from: 1980 save 40, 1981 save 7), 6 fillers, 5 real players left off.

* **Missing, no rating source in any save (3):** Dwight Harrison CB (3 G, 0 GS); I.M. Hipp RB (1 G, 0 GS); Alva Liles DT (2 G, 0 GS)
* **Left off for other reasons (2):** Dave Casper TE (6 G, 6 GS): traded: counted with oilers_titans (more GS/G there); Greg Westbrooks LB (1 G, 0 GS): traded: counted with rams (more GS/G there)
* **Fillers (not 1980 players of this team) (6):** Carl Laidlaw FB (not in the 1981 save); Brian Benirschke FS (not in the 1981 save); Monte Johnson OLB (not in the 1981 save); Joe Stewart WR (not in the 1981 save); Charlie Phillips CB (not in the 1981 save); Dan Medlin G (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (11):** Chris Bahr (CIN); Bob Chandler (BUF); Kenny King (TEN); Bob Nelson (SF); Dwayne O'Steen (STL); Dan Pastorini (TEN); Cedrick Hardman (SF); Odis McKinney (NYG); Joe Campbell (NO); Mike Spivey (CHI); Keith Moody (free agent)
* **Rated from another season (7):** Burgess Owens (1981, OAK); Matt Millen (1981, OAK); Randy McClanahan (1981, OAK); Lindsey Mason (1981, OAK); Todd Christensen (1981, OAK); Mario Celotto (1981, STL); Marc Wilson (1981, OAK)
* **Identity flags (17):** Ray Guy (college PFR Southern Miss / save Florida State); Art Shell (college PFR MD-Eastern Shore / save Louisiana Tech); Bob Chandler (college PFR USC / save Virginia); Burgess Owens (college PFR Miami (FL) / save Louisiana State); Gene Upshaw (college PFR Texas A&M-Kingsville / save Florida); Mark van Eeghen (college PFR Colgate / save Michigan); Ted Hendricks (college PFR Miami (FL) / save Purdue); Raymond Chester (nickname match to save "Ray Chester"); Rod Martin (college PFR USC / save Washington); Randy McClanahan (college PFR Louisiana / save Fresno State); Dan Pastorini (college PFR Santa Clara / save Howard); Arthur Whittington (nickname match to save "Art Whittington", college PFR SMU / save Nevada); Derrick Jensen (college PFR Texas-Arlington / save Florida State); Rich Martini (college PFR UC Davis / save Virginia); Todd Christensen (college PFR BYU / save Syracuse); Mario Celotto (college PFR USC / save San Diego State); Marc Wilson (college PFR BYU / save UCLA)
* **Position from PFR (save code in the same group) (6):** Burgess Owens FS (save CB); Mark van Eeghen FB (save HB); Mike Davis SS (save CB); Jeff Barnes OLB (save ILB); Steve Sylvester G (save C); Bruce Davis G (save T)

### chargers

Roster page: <https://web.archive.org/web/20241203202040/https://www.pro-football-reference.com/teams/sdg/1980_roster.htm>. 47 real players (rated from: 1980 save 41, 1981 save 4, 1982 save 2), 6 fillers, 5 real players left off.

* **Missing, no rating source in any save (2):** LaRue Harrington RB (4 G, 0 GS); Carl McGee LB (6 G, 0 GS)
* **Left off for other reasons (3):** Bernard Jackson DB (4 G, 0 GS): traded: counted with broncos (more GS/G there); Scott Perry DB (4 G, 0 GS): traded: counted with 49ers (more GS/G there); Don Woods RB (2 G, 0 GS): traded: counted with 49ers (more GS/G there)
* **Fillers (not 1980 players of this team) (6):** Jeff West P; Tom Suggs FS (not in the 1981 save); James Harris QB (not in the 1981 save); Bob Klein TE (not in the 1981 save); Larry Burton WR (not in the 1981 save); Steve Taylor SS (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (4):** Rick Partridge (NO); Chuck Muncie (NO); Ron Smith (STL); Booker Russell (OAK)
* **Rated from another season (6):** Louie Kelcher (1981, SD); John Cappelletti (1981, SD); Jim Laslavic (1981, SD); Ed Luther (1981, SD); Chuck Loewen (1982, SD); Mike Wood (1982, IND)
* **Identity flags (15):** Rolf Benirschke (college PFR UC Davis / save Alabama); Charlie Joiner (college PFR Grambling St. / save California); Doug Wilkerson (college PFR North Carolina Central / save New Mexico State); Leroy Jones (college PFR Norfolk St. / save Louisiana Tech); Mike Williams (college PFR LSU / save New Mexico); Greg McCrary (college PFR Clark (GA) / save Missouri); Mike Thomas (college PFR Oklahoma,UNLV / save Northern Illinois); Wilbur Young (college PFR William Penn / save UCLA); Frank Duncan (college PFR San Francisco St. / save Penn State); John Floyd (college PFR La-Monroe / save Ohio State); Chuck Loewen (college PFR South Dakota St. / save Kent State); Hank Bauer (college PFR California Lutheran / save Oregon); Booker Russell (college PFR Texas St. / save Nevada); Cliff Thrift (college PFR East Central (OK) / save Nebraska); Hal Stringert (college PFR Hawaii,Willamette / save San Diego State)
* **Position from PFR (save code in the same group) (4):** Don Macek C (save G); John Cappelletti FB (save HB); Chuck Muncie FB (save HB); Charles DeJurnett DE (save DT)

### seahawks

Roster page: <https://web.archive.org/web/20220514155906/https://www.pro-football-reference.com/teams/sea/1980_roster.htm>. 42 real players (rated from: 1979 save 1, 1980 save 35, 1981 save 6), 11 fillers, 7 real players left off.

* **Missing, no rating source in any save (7):** Larry Brinson RB (7 G, 0 GS); Terry Dion DE (9 G, 2 GS); Andre Hines T (9 G, 0 GS); Vic Minor DB (16 G, 0 GS); Terry Rennaker LB (15 G, 0 GS); Tim Walker LB (16 G, 0 GS); Jim Walsh RB (4 G, 0 GS)
* **Fillers (not 1980 players of this team) (11):** Pete Cronan ILB; Dennis Boyd DE; Brian Peets TE; Larry Polowski OLB (not in the 1981 save); Tony Benjamin FB (not in the 1981 save); Charles McShane ILB (not in the 1981 save); Mike O'Brien CB (not in the 1981 save); Carl Eller DE (not in the 1981 save); Tom McCauley K (not in the 1981 save); Gerald Pane C (not in the 1981 save); Steve Myer QB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (2):** Jim Jodat (STL); Lawrence McCutcheon (STL)
* **Rated from another season (7):** John Sawyer (1981, SEA); Jacob Green (1981, SEA); Ron Essink (1981, SEA); Fred Anderson (1981, SEA); Mark Bell (1979, SEA); Will Lewis (1981, SEA); Dave Krieg (1981, SEA)
* **Identity flags (9):** Efren Herrera (spelling match to save "Effren Herrera"); Jim Zorn (college PFR Cerritos (CA),Cal Poly-Pomona / save New Mexico); Jim Jodat (college PFR Carthage / save Louisiana-Lafayette); Ron Essink (college PFR Grand Valley St. / save North Carolina State); Sherman Smith (college PFR Miami (OH) / save Tulsa); Fred Anderson (college PFR Oregon St.,Prairie View A&M / save Nebraska); Sam Adkins (college PFR Wichita St. / save Nebraska); Mark Bell (save position TE, PFR DE: his GS do not count there); Will Lewis (college PFR Millersville / save Georgia)
* **Position from PFR (save code in the same group) (5):** John Harris FS (save SS); Keith Butler OLB (save ILB); Keith Simpson SS (save FS); Terry Beeson ILB (save OLB); Jim Jodat FB (save HB)

### cowboys

Roster page: <https://web.archive.org/web/20241204162904/https://www.pro-football-reference.com/teams/dal/1980_roster.htm>. 44 real players (rated from: 1980 save 36, 1981 save 6, 1982 save 2), 9 fillers, 4 real players left off.

* **Missing, no rating source in any save (4):** Eric Hurt DB (4 G, 0 GS); Bill Roe LB (16 G, 0 GS); Roland Solomon DB (10 G, 0 GS); Norm Wells DT (3 G, 0 GS)
* **Fillers (not 1980 players of this team) (9):** Wade Manning WR; Reggie Septien P (not in the 1981 save); Larry Dyson DE (not in the 1981 save); Cliff Harris SS (not in the 1981 save); Rayfield Wright T (not in the 1981 save); Dan Merrill FS (not in the 1981 save); Alois Blackwell HB (not in the 1981 save); Bob Hicks K (not in the 1981 save); Bobby Wright SS (not in the 1981 save)
* **Rated from another season (8):** Charlie Waters (1981, DAL); Too Tall Jones (1981, DAL); Timmy Newsome (1982, DAL); Kurt Petersen (1981, DAL); Anthony Dickerson (1981, DAL); Dextor Clinkscale (1982, DAL); James Jones (1981, DAL); Gary Hogeboom (1981, DAL)
* **Identity flags (9):** Rafael Septien (college PFR Louisiana / save Tennessee); Dennis Thurman (college PFR USC / save Stanford); Harvey Martin (college PFR Texas A&M-Commerce / save Houston); Drew Pearson (college PFR Tulsa / save Tulane); Aaron Mitchell (college PFR College of the Canyons (CA),Morris Brown,UNLV / save Arizona State); Butch Johnson (college PFR UC Riverside / save Arizona State); Glenn Carano (college PFR UNLV / save Illinois); Dextor Clinkscale (college PFR South Carolina St. / save Rutgers); Doug Cosbie (college PFR Holy Cross,Santa Clara / save Ohio State)
* **Position from PFR (save code in the same group) (6):** Charlie Waters SS (save CB); Dennis Thurman FS (save CB); Larry Cole DT (save DE); Ron Springs FB (save HB); Bruce Thornton DE (save DT); John Dutton DT (save DE)

### giants

Roster page: <https://web.archive.org/web/20230409201538/https://www.pro-football-reference.com/teams/nyg/1980_roster.htm>. 48 real players (rated from: 1979 save 1, 1980 save 35, 1981 save 9, 1982 save 2, 1983 save 1), 5 fillers, 20 real players left off.

* **Missing, no rating source in any save (17):** Ben Apuna LB (10 G, 3 GS); Tony Blount DB (3 G, 0 GS); Alvin Garrett RB (15 G, 0 GS); Bud Hebert FS (10 G, 6 GS); Mike Hogan RB (6 G, 4 GS); Dennis Johnson TE (2 G, 0 GS); Nate Johnson WR (16 G, 0 GS); Chris Linnin DE (10 G, 2 GS); Dale Markham DT (1 G, 0 GS); Nate Rivers RB (3 G, 0 GS); Kelly Saalfeld C (7 G, 0 GS); George Small NT (7 G, 0 GS); Steve Tobin C (4 G, 0 GS); Kevin Turner LB (3 G, 1 GS); Whip Walton LB (2 G, 0 GS); Gary Woolford FS (12 G, 8 GS, **1980 starter**); Kervin Wyatt LB (4 G, 3 GS)
* **Left off for other reasons (3):** Jim Culbreath RB (1 G, 0 GS): traded: counted with eagles (more GS/G there); Jerome King DB (1 G, 0 GS): traded: counted with falcons (more GS/G there); Ralph Perretta C (5 G, 0 GS): traded: counted with chargers (more GS/G there)
* **Fillers (not 1980 players of this team) (5):** Loaird McCreary TE (not in the 1981 save); Dwight Scales WR; Alan Caldwell FS (not in the 1981 save); Doug Horn G (not in the 1981 save); Randy Coffield OLB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (9):** Bo Matthews (SD); Vern Holland (CIN); Don Harris (WAS); Terry Falcon (NE); Eric Felton (NO); Scott Laidlaw (DAL); Don Patterson (DET); Steve Henry (ARZ); Doug Nettles (IND)
* **Rated from another season (13):** Mark Haynes (1981, NYG); Curtis McGriff (1981, NYG); Mike Dennis (1981, NYG); Mike Whittington (1981, NYG); Danny Pittman (1982, NYG); Joe McLaughlin (1981, NYG); Larry Heater (1982, NYG); Leon Perry (1981, NYG); Mike Friede (1981, NYG); Scott Brunner (1981, NYG); Phil Cancik (1981, KC); Myron Lapka (1983, STL); Art Best (1979, free agent)
* **Identity flags (9):** Dave Jennings (college PFR St. Lawrence / save Oklahoma State); Gary Jeter (college PFR USC / save Wake Forest); Gary Shirk (college PFR Morehead St. / save Georgia Tech); Phil Simms (college PFR Morehead St. / save Colorado); Harry Carson (college PFR South Carolina St. / save Kentucky); Johnny Perkins (spelling match to save "Johhny Perkins"); Jeff Weston (save position DE, PFR T: his GS do not count there); Steve Henry (college PFR Emporia St. / save Michigan); Myron Lapka (college PFR USC / save Duke)
* **Position from PFR (save code in the same group) (12):** Beasley Reece SS (save CB); Brad Benson T (save G); Brad Van Pelt OLB (save ILB); Curtis McGriff DT (save DE); Billy Taylor HB (save FB); Bo Matthews FB (save HB); Mike Whittington OLB (save ILB); Frank Marion ILB (save OLB); Joe McLaughlin ILB (save OLB); Vern Holland T (save G); Leon Perry FB (save HB); Myron Lapka DE (save DT)

### eagles

Roster page: <https://web.archive.org/web/20250812192457/https://www.pro-football-reference.com/teams/phi/1980_roster.htm>. 48 real players (rated from: 1978 save 2, 1980 save 40, 1981 save 5, 1982 save 1), 5 fillers, 4 real players left off.

* **Missing, no rating source in any save (2):** Ken Dunek TE (2 G, 0 GS); Zac Henderson DB (12 G, 0 GS)
* **Left off for other reasons (2):** Lewis Gilbert TE (3 G, 0 GS): traded: counted with 49ers (more GS/G there); Mike Hogan RB (7 G, 0 GS): traded: counted with giants (more GS/G there)
* **Fillers (not 1980 players of this team) (5):** Jimmy Carr CB (not in the 1981 save); Deac Sanders CB (not in the 1981 save); Manny Sistrunk DT (not in the 1981 save); John Condon G (not in the 1981 save); John Walton QB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (5):** Ron Baker (IND); Joe Pisarcik (NYG); Zachary Dixon (DEN); Steve Wagner (GB); Bob Torrey (NYG)
* **Rated from another season (8):** Roynell Young (1981, PHI); Perry Harrington (1982, PHI); Rodney Parker (1981, PHI); Steve Kenney (1981, PHI); Ray Phillips (1981, PHI); Thomas Brown (1981, CLE); Luther Blue (1978, DET); Jim Culbreath (1978, GB)
* **Identity flags (7):** Carl Hairston (college PFR MD-Eastern Shore / save Southern Mississippi); Charlie Johnson (nickname match to save "Charles Johnson", college PFR Colorado / save Maryland); Charlie Smith (college PFR Grambling St. / save Oklahoma State); Herm Edwards (nickname match to save "Herman Edwards"); Woody Peoples (college PFR Grambling St. / save Vanderbilt); Petey Perot (college PFR Northwestern St. (LA) / save UCLA); Bob Torrey (college PFR Penn St. / save Wisconsin)
* **Position from PFR (save code in the same group) (5):** Brenard Wilson FS (save SS); Herm Edwards CB (save FS); Jerry Robinson OLB (save ILB); Leroy Harris FB (save HB); Steve Kenney T (save G)

### cardinals

Roster page: <https://web.archive.org/web/20220514152119/https://www.pro-football-reference.com/teams/crd/1980_roster.htm>. 49 real players (rated from: 1980 save 39, 1981 save 8, 1982 save 2), 4 fillers, 9 real players left off.

* **Missing, no rating source in any save (6):** Kirby Criswell LB (4 G, 0 GS); Mike Dawson DT (4 G, 4 GS); Mark Goodspeed T (3 G, 0 GS); Jeff Lee WR (4 G, 0 GS); Oudious Lee NT (1 G, 0 GS); Mike Loyd QB (5 G, 1 GS)
* **Left off for other reasons (3):** Jon Brooks LB (1 G, 0 GS): traded: counted with falcons (more GS/G there); Tom Seabron LB (2 G, 0 GS): traded: counted with 49ers (more GS/G there); Gerard Williams DB (4 G, 0 GS): traded: counted with 49ers (more GS/G there)
* **Fillers (not 1980 players of this team) (4):** Terry Stieve G; Mike Robinson DE; Dave Simons DT (not in the 1981 save); Thomas Lott HB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (7):** Neil O'Donoghue (TB); Larry Swider (DET); Barney Cotton (CIN); Tim Collier (KC); Ron Coder (SEA); Jeff McIntyre (SF); Mark Bell (SEA)
* **Rated from another season (10):** Doug Marsh (1981, ARZ); Rush Brown (1981, ARZ); Bill Acker (1982, KC); Curtis Greer (1981, ARZ); Charles Baker (1982, ARZ); Rusty Lisch (1981, ARZ); Randy Love (1981, ARZ); Chris Combs (1981, ARZ); Stafford Mays (1981, ARZ); Randy Clark (1981, ARZ)
* **Identity flags (15):** Ottis Anderson (college PFR Miami (FL) / save Georgia); Wayne Morris (college PFR SMU / save Texas Christian); Jim Hart (college PFR Southern Illinois / save Florida State); Tim Kearney (college PFR Northern Michigan / save North Carolina); Eric Williams (college PFR USC / save Houston); Carl Allen (college PFR Southern Miss / save Washington); Calvin Favron (college PFR Southeastern Louisiana / save Tulane); Roy Green (college PFR Henderson St. / save Vanderbilt); Tim Collier (college PFR Texas A&M-Commerce / save Sam Houston State); Brad Oates (college PFR Duke,BYU / save Kentucky); Dave Stief (college PFR Portland St. / save Penn State); Charles Baker (nickname match to save "Charlie Baker"); Willard Harrell (nickname match to save "Will Harrell", college PFR Pacific / save Florida); John Barefield (college PFR Texas A&M-Kingsville / save Florida State); Steve Little (save position WR, PFR K: his GS do not count there)
* **Position from PFR (save code in the same group) (6):** Roger Wehrli CB (save FS); Mark Arneson OLB (save ILB); Tim Kearney ILB (save OLB); Lee Nelson SS (save CB); Ken Stone FS (save SS); Keith Wortman T (save G)

### washington

Roster page: <https://web.archive.org/web/20251231101738/https://www.pro-football-reference.com/teams/was/1980_roster.htm>. 46 real players (rated from: 1977 save 1, 1978 save 2, 1979 save 1, 1980 save 37, 1981 save 5), 7 fillers, 5 real players left off.

* **Missing, no rating source in any save (5):** Gary Anderson G (5 G, 1 GS); Rickey Claitt RB (15 G, 1 GS); Fred Dean LT (12 G, 8 GS); Bob Kuziel C (15 G, 13 GS, **1980 starter**); Zion McKinney WR (10 G, 0 GS)
* **Fillers (not 1980 players of this team) (7):** John Riggins FB; Don Hover ILB (not in the 1981 save); Don VanDuyne FS (not in the 1981 save); Lynn Boden G (not in the 1981 save); Kim McQuilken QB (not in the 1981 save); Greg Dubinetz T (not in the 1981 save); Danny Buggs WR (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (4):** Wilbur Jackson (SF); Rick Walker (CIN); Jeris White (TB); Bobby Hammond (NYG)
* **Rated from another season (9):** Mike Connell (1981, WAS); Art Monk (1981, WAS); Dan Peiffer (1977, CHI); Dan Nugent (1978, WAS); Mike Kruczek (1978, PIT); Kenny Harrison (1979, free agent); Jerry Scanlan (1981, WAS); Jeff Bostic (1981, WAS); Mike Nelms (1981, WAS)
* **Identity flags (14):** Mark Moseley (college PFR Texas A&M,S.F. Austin / save Army); Karl Lorch (college PFR USC / save Tennessee); Mark Murphy (college PFR Colgate / save New Mexico); Jeff Williams (college PFR Rhode Island / save Connecticut); Lemar Parrish (college PFR Lincoln (MO) / save Missouri); Terry Hermeling (college PFR Nevada / save USC); Monte Coleman (college PFR Central Arkansas / save Penn State); John McDaniel (college PFR Lincoln (MO) / save Arkansas); Ken Houston (college PFR Prairie View A&M / save Richmond); Dan Peiffer (college PFR SE Missouri St. / save Texas); Bobby Hammond (nickname match to save "Bob Hammond"); Buddy Hardeman (college PFR Iowa St. / save Florida); Ray Waddy (college PFR Texas A&M-Kingsville / save Purdue); Grady Richardson (college PFR Cal State-Fullerton / save Clemson)
* **Position from PFR (save code in the same group) (9):** Coy Bacon DE (save DT); Mark Murphy FS (save SS); Jeff Williams G (save T); Lemar Parrish CB (save FS); Clarence Harmon FB (save HB); Wilbur Jackson HB (save FB); Tony Peters SS (save CB); Terry Hermeling T (save G); Monte Coleman OLB (save ILB)

### bears

Roster page: <https://web.archive.org/web/20260114155518/https://www.pro-football-reference.com/teams/chi/1980_roster.htm>. 43 real players (rated from: 1978 save 1, 1979 save 2, 1980 save 37, 1981 save 2, 1982 save 1), 10 fillers, 5 real players left off.

* **Missing, no rating source in any save (5):** Dave Becker DB (11 G, 0 GS); Jonathan Hoke DB (11 G, 0 GS); Rocco Moore G (7 G, 0 GS); Paul Tabor C (16 G, 0 GS); Mike Ulmer DB (3 G, 0 GS)
* **Fillers (not 1980 players of this team) (10):** Mark Merrill ILB; Doug Buffone OLB; Bob Avellini QB; Greg Latta TE; Tony Ardizzone C; Lonnie Perrin HB; Brian McConnell OLB; Steve Schubert WR; Jack Deloplaine HB; Golden Richards WR
* **Moved in (1980 save had him elsewhere) (1):** Vaughn Lusby (CIN)
* **Rated from another season (6):** Roland Harper (1979, free agent); Bob Fisher (1981, CHI); Matt Suhey (1982, CHI); Otis Wilson (1981, CHI); John Skibinski (1978, CHI); Brad Shearer (1979, free agent)
* **Identity flags (2):** James Scott (college PFR Trinity Valley CC (TX) / save Wake Forest); Vince Evans (college PFR USC / save Arkansas)
* **Position from PFR (save code in the same group) (2):** Gary Campbell OLB (save ILB); Roland Harper FB (save HB)

### lions

Roster page: <https://web.archive.org/web/20241204144657/https://www.pro-football-reference.com/teams/det/1980_roster.htm>. 46 real players (rated from: 1978 save 1, 1980 save 38, 1981 save 6, 1982 save 1), 7 fillers, 14 real players left off.

* **Missing, no rating source in any save (7):** Tommie Ginn C (14 G, 5 GS); Wally Pesuit C (1 G, 0 GS); Ken Talton RB (2 G, 0 GS); Jesse Thompson WR (11 G, 2 GS); Tom Tuinei DT (12 G, 2 GS); Mike Whited T (16 G, 2 GS); Ray Williams RB (6 G, 1 GS)
* **Left off for other reasons (7):** Curley Culp DT (3 G, 2 GS): traded: counted with oilers_titans (more GS/G there); Mike Friede WR (4 G, 0 GS): traded: counted with giants (more GS/G there); Vern Holland T (2 G, 0 GS): traded: counted with giants (more GS/G there); Alva Liles DT (1 G, 0 GS): traded: counted with raiders (more GS/G there); Mike McCoy DT (4 G, 0 GS): traded: counted with giants (more GS/G there); John Mohring LB (1 G, 0 GS): traded: counted with browns (more GS/G there); Rod Walters G (2 G, 0 GS): traded: counted with chiefs (more GS/G there)
* **Fillers (not 1980 players of this team) (7):** Dave Gallagher DE (not in the 1981 save); Gene Washington WR (not in the 1981 save); Doug English DT; Bob Fernandes FB (not in the 1981 save); Doug Jones FS (not in the 1981 save); Don Morrison T (not in the 1981 save); Lawrence Gaines FB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (9):** Stan White (IND); Ray Oldham (NYG); John Mendenhall (NYG); Burton Lawless (DAL); Chris Dieterich (ARZ); Willie Parker (BUF); Derrel Luce (MIN); Prentice McCray (NE); Dave Simmons (GB)
* **Rated from another season (8):** Eddie Murray (1981, DET); Tom Skladany (1981, DET); Billy Sims (1982, DET); Gary Danielson (1981, DET); Wayne Smith (1981, DET); John Woodcock (1978, DET); Eric Hipple (1981, DET); Tom Turnure (1981, DET)
* **Identity flags (12):** Tom Skladany (spelling match to save "Tom Skledany"); Charlie Weaver (college PFR USC / save Texas); David Hill (college PFR Texas A&M-Kingsville / save Tulsa); Dexter Bussey (college PFR Oklahoma,Texas-Arlington / save Wyoming); Freddie Scott (college PFR Amherst / save Oklahoma State); James Hunter (college PFR Grambling St. / save Tennessee State); Russ Bolinger (college PFR UC Riverside,Long Beach St. / save Texas); Ray Oldham (college PFR Middle Tenn. St. / save Colorado); John Mendenhall (college PFR Grambling St. / save Washington State); William Gay (nickname match to save "Bill Gay", college PFR USC / save Princeton); Chris Dieterich (college PFR North Carolina St. / save Michigan); Garry Cobb (college PFR USC / save Virginia Union)
* **Position from PFR (save code in the same group) (7):** Charlie Weaver OLB (save ILB); Dexter Bussey FB (save HB); James Hunter CB (save FS); Ken Fantetti ILB (save OLB); Stan White OLB (save ILB); Ray Oldham FS (save SS); William Gay DT (save DE)

### packers

Roster page: <https://web.archive.org/web/20241206142514/https://www.pro-football-reference.com/teams/gnb/1980_roster.htm>. 53 real players (rated from: 1978 save 2, 1980 save 39, 1981 save 8, 1982 save 3, 1987 save 1), 0 fillers, 8 real players left off.

* **Missing, no rating source in any save (5):** Vickey Ray Anderson RB (7 G, 0 GS); Bruce Beekley LB (15 G, 3 GS); Tom Birney K (7 G, 0 GS); Ken Brown C (7 G, 0 GS); Mike Lewis NT (10 G, 0 GS)
* **Left off for other reasons (3):** Bobby Kimball WR (1 G, 0 GS): 53 slots full (fewer games than those kept); Mark Murphy DB (1 G, 0 GS): 53 slots full (fewer games than those kept); Steve Pisarkiewicz QB (1 G, 0 GS): position cap QB 3
* **Moved in (1980 save had him elsewhere) (5):** Jan Stenerud (KC); Kurt Allerman (ARZ); Ed O'Neil (DET); Brian Cabral (ATL); Rich Dimler (CLE)
* **Rated from another season (14):** Eddie Lee Ivery (1982, GB); Mike McCoy (1982, GB); Gerry Ellis (1981, GB); George Cumby (1981, GB); Mark Lee (1981, GB); Bill Troup (1978, BAL); Fred Nixon (1981, GB); Bill Larson (1978, PHI); Syd Kitson (1981, GB); Harlan Huckleby (1981, GB); Karl Swanke (1981, GB); Mike Jolly (1982, GB); Kit Lathrop (1981, free agent); Buddy Aydelette (1987, PIT)
* **Identity flags (11):** Derrel Gofourth (spelling match to save "Derrell Gofourth"); Johnnie Gray (college PFR Cal State-Fullerton / save Michigan); Aundra Thompson (college PFR Texas A&M-Commerce / save Alabama); Paul Rudzinski (college PFR Michigan St. / save Ohio State); Casey Merrill (college PFR UC Davis / save Oklahoma); Harlan Huckleby (spelling match to save "Harian Huckleby"); Wylie Turner (college PFR Angelo State (TX) / save Indiana); Kit Lathrop (college PFR Arizona St. / save Clemson); Melvin Jackson (college PFR USC / save Illinois State); Chester Marcol (college PFR Hillsdale / save Texas A&M); Rich Dimler (college PFR USC / save Missouri)
* **Position from PFR (save code in the same group) (6):** Eddie Lee Ivery HB (save FB); Johnnie Gray FS (save CB); Mike Douglass OLB (save ILB); Estus Hood CB (save FS); Gerry Ellis FB (save HB); Kurt Allerman ILB (save OLB)

### vikings

Roster page: <https://web.archive.org/web/20250814161114/https://www.pro-football-reference.com/teams/min/1980_roster.htm>. 46 real players (rated from: 1979 save 1, 1980 save 38, 1981 save 6, 1982 save 1), 7 fillers, 5 real players left off.

* **Missing, no rating source in any save (3):** Larry Brune DB (16 G, 0 GS); Melvin Mitchell G (6 G, 0 GS); Doug Paschal RB (16 G, 0 GS)
* **Left off for other reasons (2):** Marvin Cobb DB (2 G, 0 GS): traded: counted with steelers (more GS/G there); Derrel Luce LB (4 G, 0 GS): traded: counted with lions (more GS/G there)
* **Fillers (not 1980 players of this team) (7):** Wally Hilgenberg OLB (not in the 1981 save); Tim Baylor FS (not in the 1981 save); Brent McClanahan HB (not in the 1981 save); Mike Shaw QB (not in the 1981 save); Paul Krause SS (not in the 1981 save); Robert Steele WR (not in the 1981 save); Joshua Niehaus DT (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (6):** Terry LeCount (SF); Jim Langer (BAL); Bob Bruer (SF); John Vella (OAK); Ken Sanders (DET); Nick Bebout (SEA)
* **Rated from another season (8):** Bob Tucker (1979, MIN); Brent Boyd (1981, MIN); Dennis Johnson (1982, MIN); Doug Martin (1981, MIN); Eddie Payton (1981, MIN); Henry Johnson (1981, MIN); Joe Senser (1981, MIN); Willie Teal (1981, MIN)
* **Identity flags (12):** Rick Danmeier (college PFR Sioux Falls / save UCLA); John Turner (college PFR Miami (FL) / save South Carolina); Sammy White (college PFR Grambling St. / save Jackson State); Steve Riley (college PFR USC / save Stanford); Ted Brown (college PFR North Carolina St. / save Baylor); Doug Sutherland (college PFR Wisconsin?Superior / save Florida A&M); Ron Yary (college PFR Cerritos (CA),USC / save North Alabama); Bob Tucker (college PFR Bloomsburg / save Cincinnati); Dennis Johnson (save position ILB, PFR TE: his GS do not count there); Joe Senser (college PFR West Chester / save Nevada); Bob Bruer (college PFR Minnesota St. / save Washington); John Vella (college PFR USC / save Texas A&M)
* **Position from PFR (save code in the same group) (8):** Kurt Knoff FS (save SS); Ted Brown FB (save HB); Tom Hannon SS (save CB); Scott Studwell ILB (save OLB); Jim Hough G (save C); Dave Huffman T (save G); Jim Langer C (save G); John Vella T (save G)

### buccaneers

Roster page: <https://web.archive.org/web/20241227224125/https://www.pro-football-reference.com/teams/tam/1980_roster.htm>. 47 real players (rated from: 1980 save 40, 1981 save 5, 1982 save 1, 1985 save 1), 6 fillers, 3 real players left off.

* **Missing, no rating source in any save (1):** Greg Roberts RG (6 G, 6 GS)
* **Left off for other reasons (2):** Mike Calhoun DT (3 G, 0 GS): traded: counted with 49ers (more GS/G there); Tony Samuels TE (6 G, 3 GS): traded: counted with chiefs (more GS/G there)
* **Fillers (not 1980 players of this team) (6):** Rik Bonness ILB; Mike Rae QB; Wally Chambers DT; Dana Nafziger ILB; Morris Owens WR; George Ragsdale WR
* **Moved in (1980 save had him elsewhere) (8):** Garo Yepremian (NO); Mike Washington (free agent); Dave Stalls (DAL); Neal Colzie (MIA); Norris Thomas (MIA); Bruce Radford (DEN); Mike Shumann (DEN); Conrad Rucker (TEN)
* **Rated from another season (7):** Ray Snell (1981, TB); Jim Leonard (1981, TB); Gary Davis (1981, TB); Aaron Brown (1985, PHI); Andy Hawkins (1981, TB); Kevin House (1982, TB); Scot Brantley (1981, TB)
* **Identity flags (15):** Doug Williams (college PFR Grambling St. / save Arizona State); Mark Cotney (college PFR New Mex. Highlands,Cameron / save Alabama); Richard Wood (college PFR USC / save Montana State); Dave Lewis (college PFR USC / save Georgia Southern); Steve Wilson (nickname match to save "Steven Wilson"); Ricky Bell (college PFR USC / save Penn State); Gene Sanders (save position DT, PFR G: his GS do not count there); Gary Davis (college PFR Cal Poly-San Luis Obispo / save Michigan); Andy Hawkins (college PFR Texas A&M-Kingsville / save Oregon); Danny Reece (college PFR USC / save Ball State); Jim Obradovich (college PFR USC / save Oklahoma); Norris Thomas (college PFR Southern Miss / save Princeton); Kevin House (college PFR Southern Illinois / save Illinois State); Scot Brantley (spelling match to save "Scott Brantley"); Bruce Radford (college PFR Grambling St. / save Mississippi)
* **Position from PFR (save code in the same group) (6):** Charley Hannah T (save G); Mark Cotney SS (save FS); Cedric Brown FS (save SS); Jim Leonard C (save G); Darrell Austin T (save C); Bruce Radford DE (save DT)

### falcons

Roster page: <https://web.archive.org/web/20241203133458/https://www.pro-football-reference.com/teams/atl/1980_roster.htm>. 46 real players (rated from: 1978 save 1, 1979 save 1, 1980 save 37, 1981 save 7), 7 fillers, 4 real players left off.

* **Missing, no rating source in any save (4):** Chuck Correal C (16 G, 0 GS); Chuck Herman G (2 G, 0 GS); Mike Smith WR (5 G, 0 GS); Stan Sytsma LB (2 G, 0 GS)
* **Fillers (not 1980 players of this team) (7):** Jim Mitchell DE (not in the 1981 save); June Jones QB; Paul Ryczek C; Bubba Bean HB (not in the 1981 save); Ray Easterling FS (not in the 1981 save); Greg Brezina OLB (not in the 1981 save); Brian Keating TE (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (3):** Anthony Anderson (PIT); Jon Brooks (DET); Calvin Miller (NYG)
* **Rated from another season (9):** Al Richardson (1981, ATL); Buddy Curry (1981, ATL); Joel Williams (1979, ATL); Kenny Johnson (1981, ATL); Junior Miller (1981, ATL); Frank Reed (1978, ATL); Jim Laughlin (1981, ATL); Reggie Smith (1981, ATL); Earl Jones (1981, ATL)
* **Identity flags (12):** Lynn Cain (college PFR USC / save Oklahoma); Wallace Francis (college PFR Ark-Pine Bluff / save Georgia); Don Smith (college PFR Miami (FL) / save Georgia Tech); Robert Pennywell (college PFR Grambling St. / save Texas A&M); Mike Moroski (college PFR UC Davis / save Jackson State); Ray Strong (college PFR UNLV / save New Mexico); Reggie Smith (college PFR North Carolina Central / save Ohio State); Pat Howell (college PFR USC / save Texas); Earl Jones (college PFR Norfolk St. / save Virginia); Anthony Anderson (college PFR Temple / save Texas Christian); Dewey McClain (college PFR East Central (OK) / save Colorado); Rick Byas (college PFR Eastern Michigan,Wayne State (MI) / save Texas A&M)
* **Position from PFR (save code in the same group) (6):** Al Richardson OLB (save ILB); Tom Pridemore FS (save CB); William Andrews FB (save HB); Don Smith DT (save DE); Fulton Kuykendall ILB (save OLB); Frank Reed SS (save CB)

### rams

Roster page: <https://web.archive.org/web/20241207021645/https://www.pro-football-reference.com/teams/ram/1980_roster.htm>. 51 real players (rated from: 1980 save 36, 1981 save 13, 1982 save 1, 1984 save 1), 2 fillers, 2 real players left off.

* **Missing, no rating source in any save (1):** Victor Hicks TE (16 G, 14 GS, **1980 starter**)
* **Left off for other reasons (1):** Conrad Rucker TE (2 G, 0 GS): traded: counted with buccaneers (more GS/G there)
* **Fillers (not 1980 players of this team) (2):** Ken Clark P (not in the 1981 save); Tim Pollard FB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (3):** Greg Horton (TB); Lydell Mitchell (SD); Ed McGlasson (NYJ)
* **Rated from another season (15):** Johnnie Johnson (1981, STL); Cody Jones (1981, STL); Doug Smith (1981, STL); Jewerl Thomas (1981, STL); LeRoy Irvin (1981, STL); Irv Pankey (1981, STL); Walt Arnold (1981, STL); Bill Bain (1984, STL); Jeff Delaney (1981, DET); Lucious Smith (1981, STL); Mike Guman (1981, STL); Phil Murphy (1981, STL); Carl Ekern (1981, STL); Jeff Moore (1982, SF); Jeff Rutledge (1981, STL)
* **Identity flags (10):** Jim Youngblood (college PFR Tennessee Tech / save Colorado State); Larry Brooks (college PFR Virginia St. / save California); Dennis Harrah (college PFR Miami (FL) / save Nebraska); Terry Nelson (college PFR Ark-Pine Bluff / save Texas A&M); Pat Haden (college PFR USC / save Tennessee); Reggie Doss (college PFR Hampton / save New Mexico); Bill Bain (college PFR Colorado,USC / save Nebraska); Lucious Smith (college PFR San Diego St.,Cal State-Fullerton / save Southern Methodist); Phil Murphy (college PFR South Carolina St. / save Penn State); Bob Lee (college PFR Arizona St.,Pacific / save Virginia)
* **Position from PFR (save code in the same group) (3):** Johnnie Johnson SS (save CB); Cody Jones DT (save DE); Doug Smith G (save C)

### saints

Roster page: <https://web.archive.org/web/20230828001432/https://www.pro-football-reference.com/teams/nor/1980_roster.htm>. 47 real players (rated from: 1980 save 40, 1981 save 5, 1982 save 2), 6 fillers, 10 real players left off.

* **Missing, no rating source in any save (6):** Larry Collins RB (8 G, 0 GS); Larry Coombs C (1 G, 0 GS); Tom Donovan WR (5 G, 0 GS); Stan Holloway LB (11 G, 2 GS); James Marshall DB (16 G, 3 GS); Steve Parker DE (4 G, 1 GS)
* **Left off for other reasons (4):** Joe Campbell LB (5 G, 0 GS): traded: counted with raiders (more GS/G there); Chuck Muncie RB (4 G, 3 GS): traded: counted with chargers (more GS/G there); Artie Owens WR (3 G, 0 GS): traded: counted with bills (more GS/G there); Mike Spivey DB (2 G, 0 GS): traded: counted with raiders (more GS/G there)
* **Fillers (not 1980 players of this team) (6):** David Gray CB (not in the 1981 save); Leroy Land CB (not in the 1981 save); Jim Merlo ILB (not in the 1981 save); Ralph McGill FS (not in the 1981 save); John Jordan K (not in the 1981 save); Pat Hughes OLB (not in the 1981 save)
* **Moved in (1980 save had him elsewhere) (4):** Benny Ricardo (DET); Tommy Hart (CHI); Dave Washington (DET); Guy Benjamin (MIA)
* **Rated from another season (7):** Stan Brock (1981, NO); Dave Waymer (1981, NO); Jimmy Rogers (1982, NO); Jim Pietrzak (1981, NO); Chuck Evans (1981, NO); Guido Merkens (1982, NO); Gordon Banks (1981, NO)
* **Identity flags (8):** Elois Grooms (college PFR Tennessee Tech / save Wyoming); John Hill (college PFR Lehigh / save Jackson State); Fred Sturt (college PFR Bowling Green / save North Carolina); Dave Waymer (save position HB, PFR RCB: his GS do not count there); Clarence Chapman (college PFR East. Michigan / save Eastern Michigan); Wayne Wilson (college PFR Shepherd / save Shippensburg); Barry Bennett (college PFR Concordia-Moorhead (MN) / save Florida); Jim Pietrzak (college PFR East. Michigan / save Eastern Michigan)
* **Position from PFR (save code in the same group) (3):** Tom Myers FS (save CB); Ray Brown SS (save FS); Don Schwartz SS (save CB)

### 49ers

Roster page: <https://web.archive.org/web/20241204142349/https://www.pro-football-reference.com/teams/sfo/1980_roster.htm>. 50 real players (rated from: 1978 save 1, 1979 save 2, 1980 save 38, 1981 save 8, 1982 save 1), 3 fillers, 10 real players left off.

* **Missing, no rating source in any save (7):** Jerry Aldridge RB (1 G, 0 GS); Ken Bungarda T (15 G, 0 GS); Mike Calhoun DT (4 G, 0 GS); Ricky Churchman SS (16 G, 15 GS, **1980 starter**); Jim Robinson WR (5 G, 0 GS); Ken Times DT (3 G, 0 GS); George Visger DT (3 G, 0 GS)
* **Left off for other reasons (3):** Bob Bruer TE (1 G, 0 GS): traded: counted with vikings (more GS/G there); Thomas Henderson LB (1 G, 0 GS): traded: counted with oilers_titans (more GS/G there); Jerry Wilkinson DE (6 G, 0 GS): traded: counted with browns (more GS/G there)
* **Fillers (not 1980 players of this team) (3):** Paul Seal TE; Ron Burton HB; Eric Bjornson TE
* **Moved in (1980 save had him elsewhere) (9):** Charle Young (STL); Lawrence Pillers (NYJ); Ray Rhodes (NYG); Don Woods (SD); Terry Tautolo (PHI); Scott Perry (CIN); Ricky Patton (GB); Jim Krahl (IND); Mel Land (MIA)
* **Rated from another season (12):** Jim Miller (1981, SF); Earl Cooper (1981, SF); Bobby Leopold (1981, SF); Ron Singleton (1979, SF); Charles Johnson (1979, SF); Jim Stuckey (1981, SF); Keena Turner (1981, SF); Terry Anderson (1978, WAS); Herb Williams (1982, ARZ); Craig Puki (1981, SF); Lewis Gilbert (1981, STL); Ernie Hughes (1981, NYG)
* **Identity flags (10):** Charle Young (college PFR USC / save UCLA); Ron Singleton (college PFR Grambling St. / save Notre Dame); Charles Johnson (college PFR Grambling St. / save Colorado); Ted Vincent (college PFR Wichita St. / save Oklahoma State); Dan Bunz (college PFR UC Riverside,Long Beach St. / save Georgia Tech); Scott Hilton (college PFR Salem / save West Virginia); Melvin Morgan (college PFR Mississippi Gulf Coast CC,Miss. Valley St. / save Northern Colorado); Lenvil Elliott (college PFR Truman St. / save Idaho); Scott Perry (college PFR Williams / save California); Tom Seabron (nickname match to save "Thomas Seabron")
* **Position from PFR (save code in the same group) (9):** Dwight Hicks FS (save CB); Earl Cooper FB (save HB); Bobby Leopold OLB (save ILB); Jimmy Webb DE (save DT); Dan Bunz ILB (save OLB); Scott Hilton ILB (save OLB); Paul Hofer HB (save FB); Melvin Morgan CB (save SS); Ernie Hughes G (save C)

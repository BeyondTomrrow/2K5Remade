# NFL gameplay rules, 1966-2025

Readable companion to `data/eras/league/rules_1966_2025.json` (95 interval rows, one source URL
each). What NFL 2K5 can do with these is in `ERA-RULES.md`. Compiled 2026-10-16.

**Sources.** Most rows cite the Wikipedia season article's rule-changes section,
`https://en.wikipedia.org/wiki/<YEAR>_NFL_season#Rule_changes`. Its wikitext was fetched for all 60 seasons.
The kickoff history comes from NFL Football Operations, "The History of the Rules". The live page
no longer renders its text, so the citation is the
[Wayback snapshot of 2022-01-03](https://web.archive.org/web/20220103123107/https://operations.nfl.com/the-rules/evolution-of-the-nfl-rules/).
The other sources are:
* the [PFHOF 1974 page](https://www.profootballhof.com/football-history/football-history/1960-1979/1974);
* [Hogs Haven's roster-size history](https://www.hogshaven.com/2020/5/4/21245563/a-brief-history-of-roster-sizes-in-the-nfl);
* a [PFRA forum thread](https://profootballresearchers.com/forum/viewtopic.php?p=5640) for 1982;
* the Wikipedia articles [NFL playoffs](https://en.wikipedia.org/wiki/NFL_playoffs),
  [Two-minute warning](https://en.wikipedia.org/wiki/Two-minute_warning) and
  [Play clock](https://en.wikipedia.org/wiki/Play_clock).

Rows marked *medium* in the JSON rest on a single secondary source, or the sources disagree.

## Kicking game

| Rule | 1966-73 | 1974-93 | 1994-2010 | 2011-15 | 2016-23 | 2024 | 2025 |
|---|---|---|---|---|---|---|---|
| Kickoff spot | own 40 | own 35 | own 30 | own 35 | own 35 | 35, dynamic | 35, dynamic |
| Kickoff touchback | 20 | 20 | 20 | 20 | **25** | 30 in the air / 20 on a bounce | **35** / 20 |
| Kickoff fair catch | normal | normal | normal | normal | normal (2023: at or behind the 25 = ball at the 25) | not allowed | not allowed |
| Goal posts | goal line | **end line** | end line | end line | end line | end line | end line |
| PAT snap | 2 | 2 | 2 | 2 (2015: **15**) | 15 | 15 | 15 |
| Two-point try | no (AFL yes, to 1969) | no | **yes** (from 1994) | yes (2015: the defense can return it) | yes | yes | yes |

* **Dynamic kickoff (2024-):**
  * the coverage team lines up at the receiving 40;
  * the kick must land between the receiving 20 and the goal line;
  * a kick short of that zone or out of bounds = ball at the receiving 40;
  * in Q4 the trailing team may declare an old-style onside kick.
* **Kickoff out of bounds:**
  * before 1984: 5 yards and a re-kick;
  * 1984: the receiving team may take the spot after a second OOB kick;
  * 1987: ball 30 yards from the spot of the kick, or the OOB spot.
* **Missed field goals:**
  * before 1974: a touchback at the 20 when the miss went into the end zone;
  * 1974: the line of scrimmage or the 20, whichever is farther out;
  * 1994: the spot of the kick (*medium*, check the rulebook).
* **Scrimmage-kick and turnover touchbacks** stay at the 20 the whole time.
* **Fourth-down incompletion in the end zone:** a touchback until 1974; from 1975 the ball goes over
  at the previous spot.
* **Hash marks:**
  * 1966-71: 20 yards from each sideline;
  * from 1972: 70 ft 9 in from each sideline, 18 ft 6 in apart.

## Clock and overtime

* **Two-minute warning:** at 2:00 of Q2 and Q4 for the whole period, and in a regular-season OT
  period.
* **Play clock:**
  * to 1987: about 30 s, kept by an official (*medium*); visible stadium play clocks from 1976;
  * 1988: 45 s / 30 s;
  * 1990: 45 s / 25 s;
  * 1993-today: 40 s from the end of the play / 25 s after administrative stoppages.
* **Sacks** stopped the clock for 5 s from 1979 to 2013. **10-second runoff:** from 1980; it applies after the two-minute
  warning from 2017.
* **Regular-season overtime:**
  * 1966-73: none, ties stand;
  * **1974:** one 15-minute sudden-death period, a tie if nobody scores;
  * **2012:** modified (a first-possession FG does not end it);
  * **2017:** 10 minutes;
  * **2025:** both teams possess, still 10 minutes.
* **Postseason overtime:** unlimited 15-minute periods throughout:
  * sudden death to 2009;
  * modified from 2010;
  * both teams possess from 2022.
* **Ties in the standings:** ignored to 1971; from 1972 a half win and a half loss.

## Contact, blocking, quarterback

* **Receiver contact:**
  * 1966-73: anywhere until the pass;
  * 1974: one "chuck" after 3 yards ("Isaac Curtis rules");
  * 1977: one contact only;
  * **1978:** only within 5 yards (the "Mel Blount rule").
* **Pass blocking:** from **1978** extended arms and open hands are allowed. Head slap illegal from 1977.
* **Holding:** 15 -> 10 yards in **1974** (Wikipedia 1974 and PFHOF; Wikipedia 1981 also lists a hands/holding cut,
  marked *medium*).
* **"In the grasp":**
  * **1979:** dead when the QB is in the grasp and control of a tackler;
  * 1991: also requires the passer's safety to be in jeopardy.
* **"Stabler" fumble rule (1979):** on 4th down or after the two-minute warning, only the fumbler may advance; extended to PATs in 1988.
* **Stickum** banned 1981.

## Replay

* None through 1985 (tested in the 1978 preseason).
* **1986-1991:** booth official only.
* 1992-98: none.
* **1999+:** coach challenges plus booth review inside 2:00 and in OT:
  * a third challenge from 2004;
  * booth reviews all scores from 2011 and turnovers from 2012.
* Replay does not matter for a video game.

## Season shape

| Seasons | Games | Playoff teams | Active roster |
|---|---|---|---|
| 1966 | 14 | NFL 2 + AFL 2 (title games, Super Bowl) | 40 |
| 1967-68 | 14 | NFL 4 + AFL 2 | 40 (+ taxi squad) |
| 1969 | 14 | NFL 4 + AFL 4 | 40 |
| 1970-73 | 14 | 8 | 40 |
| 1974 | 14 | 8 | 47 |
| 1975-77 | 14 | 8 | 43 |
| 1978-81 | **16** | **10** | 45 |
| 1982 | **9** (strike) | **16** (by conference) | 45 + 4, then 49 |
| 1983 | 16 | 10 | 45 (*medium*) |
| 1984 | 16 | 10 | 49 |
| 1985-86 | 16 | 10 | 45 |
| 1987 | **15** (week 3 cancelled; weeks 4-6 replacement players) | 10 | 45, plus replacement rosters |
| 1988-89 | 16 | 10 | 45 |
| 1990-92 | 16 | **12** | 45 active + 2 inactive |
| 1993-2010 | 16 | 12 | 53 (45 active + emergency QB) |
| 2011-19 | 16 | 12 | 53 (46 active, *medium*) |
| 2020 | 16 | **14** | 53 (47-48 active, *medium*) |
| 2021-25 | **17** | 14 | 53 (47-48 active) |

## Open checks

* Pre-1988 play-clock length (30 s). Check a 1970s or 1980s rulebook or Record Manual.
* The 1983 roster limit: 45 or 49.
* The 1994 missed-FG spot. Check against the NFL rulebook archive.
* Post-2011 game-day actives. These are not in the fetched sources.

# My NFL Eras: the 1980 regular-season schedule

2026-10-15. M0 task D2 of `docs/eras/PHASE2-PLAN.md`. Status: **built from sources and checked**.
No game in the schedule was typed by hand or generated; the generated test fallback the plan
allows was not needed and was not produced.

* Script: `build/eras/league_schedule.py` (`python -I build/eras/league_schedule.py --season 1980`;
  `--fetch` downloads missing source copies, `--check` re-runs every check on the written files).
* Data: `data/eras/league/nfl_schedule_1980.json` (week, date, day, PFR kick-off time, away, home,
  real score, PFR box-score link, sources, check results).
* Runtime: `data/eras/runtime/1980/schedule.csv` (`game_id,week,date,away,home,historical`). It
  carries no scores, so real results cannot reach play.

## Sources

Pro-Football-Reference (PFR) answers scripts with a Cloudflare challenge, which was not bypassed.
As in `league_history.py`, its pages were read from Wayback Machine copies. Every file is cached
under `build/eras/league/`, with the capture actually served and a sha256 in the cache index
(`pfr_cache/schedule_sources.json`, `wiki_cache/1980/index.json`).

| role | page | capture | sha256 (first 12) |
|---|---|---|---|
| primary | PFR `/years/1980/games.htm` | [2025-01-23](https://web.archive.org/web/20250123165834/https://www.pro-football-reference.com/years/1980/games.htm) | `b9d60204cfc4` |
| second copy | the same page, parsed separately | [2019-08-18](https://web.archive.org/web/20190818122956/https://www.pro-football-reference.com/years/1980/games.htm) | `5947af018eeb` |
| official totals | 2012 NFL Record & Fact Book, "Past Standings" (1980 on p. 387), static.nfl.com PDF | [2013-09-25](https://web.archive.org/web/20130925182142/http://static.nfl.com/static/content/public/image/history/pdfs/History/Past_Standings_2011.pdf) | `d44093659ffa` |
| PFR standings | PFR `/years/1980/` (already cached by `league_history.py`) | [2024-12-19](https://web.archive.org/web/20241219080443/https://www.pro-football-reference.com/years/1980/) | `ba949014d551` |
| per-game reading | the 28 Wikipedia articles "1980 \<city\> \<name\> season" (Cardinals: "1980 St. Louis Cardinals (NFL) season"), one revision each (revids 1341198146-1379889101, permanent links in `nfl_schedule_1980.json` `wikipedia_articles`) | MediaWiki API, wikitext | in `wiki_cache/1980/index.json` |

The Wayback request for the 2025-12-06 capture of the games page was answered with the
2025-01-23 capture; the served capture is the one recorded. The NFL book PDF was turned into
text with `pdftotext -layout` (mingw64); the text file is cached beside the PDF.

## How the schedule is read

* Rows of PFR's `games` table whose week is a number (1-16). The playoff rows and header rows
  are skipped.
* **Home team:** the team whose PFR code ends the box-score id (`/boxscores/YYYYMMDD0<home>.htm`).
  For every game this agrees with PFR's own location mark (`@` = the winner was away), and the
  box-score date equals the date cell.
* Date: the date cell's sort key (`csk`, ISO). Week: PFR's week number. Kick-off time: as PFR
  prints it. The page does not state a time zone, so the time is kept only in the JSON.
* PFR team codes become the stable franchise ids of `franchise_events_1970_2025.json` through
  the 1980 team list in `nfl_seasons_1970_1980.json`. A code outside that list fails the build.
* `game_id` = `1980w<week>-<away id>-<home id>`, for example `1980w01-dolphins-bills`. It is unique
  because no team plays twice in a week.
* Order: by week, then in the order PFR lists the week's games.

## Checks (all pass)

Run on every build and again by `--check` (T0.2), on the written files:

| check | result |
|---|---|
| every team plays 16 games | yes, all 28 |
| 224 games (28 x 16 / 2) | 224 |
| no team twice in one week; each week's dates within 7 days | yes; 14 games in every week, no byes |
| every team id is in the 1980 list | yes |
| both PFR copies (2025 and 2019) give the same 224 games: week, date, home, away, both scores | identical |
| **each team's W-L-T recomputed from the real scores equals `nfl_seasons_1970_1980.json`** | 28 of 28 |
| W-L-T **and** PF/PA recomputed equal the NFL book's W-L-T and Pts/OP | 28 of 28 |
| PF/PA recomputed equal PFR's standings table | 28 of 28 |
| Wikipedia: each team-game (both teams' articles) agrees on date, opponent, home/away, result and score | 448 of 448; 0 disagree, 0 not found |

The record check is the plan's "second check": the records in `nfl_seasons_1970_1980.json` were
already verified against the NFL book. The NFL book check adds points: a wrong score would have to
cancel out exactly in both teams' PF and PA to pass. Home and away cannot be seen in any total,
so they are confirmed per game by two PFR readings (box-score id and location mark), two PFR
captures and both teams' Wikipedia articles.

`build/eras/m0_negative_checks.py` shows the checks can fail: one score changed by a point, one
result reversed, one game dropped, one game moved to another week, and one home/away swap (with its
scores, so every total is unchanged) are each caught. Log: `logs/eras-m0-negative.log`.

Recomputed 1980 standings (equal to the NFL book):

| team | W-L-T | PF-PA | | team | W-L-T | PF-PA |
|---|---|---|---|---|---|---|
| Buffalo Bills | 11-5-0 | 320-260 | | Philadelphia Eagles | 12-4-0 | 384-222 |
| New England Patriots | 10-6-0 | 441-325 | | Dallas Cowboys | 12-4-0 | 454-311 |
| Miami Dolphins | 8-8-0 | 266-305 | | Washington Redskins | 6-10-0 | 261-293 |
| Baltimore Colts | 7-9-0 | 355-387 | | St. Louis Cardinals | 5-11-0 | 299-350 |
| New York Jets | 4-12-0 | 302-395 | | New York Giants | 4-12-0 | 249-425 |
| Cleveland Browns | 11-5-0 | 357-310 | | Minnesota Vikings | 9-7-0 | 317-308 |
| Houston Oilers | 11-5-0 | 295-251 | | Detroit Lions | 9-7-0 | 334-272 |
| Pittsburgh Steelers | 9-7-0 | 352-313 | | Chicago Bears | 7-9-0 | 304-264 |
| Cincinnati Bengals | 6-10-0 | 244-312 | | Tampa Bay Buccaneers | 5-10-1 | 271-341 |
| San Diego Chargers | 11-5-0 | 418-327 | | Green Bay Packers | 5-10-1 | 231-371 |
| Oakland Raiders | 11-5-0 | 364-306 | | Atlanta Falcons | 12-4-0 | 405-272 |
| Kansas City Chiefs | 8-8-0 | 319-336 | | Los Angeles Rams | 11-5-0 | 424-289 |
| Denver Broncos | 8-8-0 | 310-323 | | San Francisco 49ers | 6-10-0 | 320-415 |
| Seattle Seahawks | 4-12-0 | 291-408 | | New Orleans Saints | 1-15-0 | 291-487 |

## What the schedule contains

* 16 weeks, 1980-09-07 to 1980-12-22; 14 games every week (28 teams, no byes).
* Days: 199 Sunday, 16 Monday (one every week), 5 Thursday (weeks 2, 12, 13 (two, Thanksgiving),
  14) and 4 Saturday (weeks 15 and 16).
* One tie: week 6, 1980-10-12, Green Bay Packers 14 at Tampa Bay Buccaneers 14. This is why both
  teams are 5-10-1.
* Real scores stay in the JSON. M4/M5 must never show them as an Eras result; they exist for
  checks and, later, for the user's option of replaying the real results (PHASE2-PLAN section 6).

## Runtime format

`schedule.csv`: one header line, then 224 rows, ASCII, no quoting:

```
game_id,week,date,away,home,historical
1980w01-falcons-vikings,1,1980-09-07,falcons,vikings,true
```

The sixth column, `historical`, is `true` for every row of this file. The plan asks for a
generated fallback to carry `historical=false`, so the column is there for both; a reader that
only needs the first five fields can ignore it. Team ids are the `id` column of `teams.csv`.

## Limits

* PFR is the only primary publisher of the per-game data. The second copy is the same publisher
  at another date, and Wikipedia's articles often cite PFR box scores, so neither is an
  independent source. The NFL book is independent but gives only season totals.
* Kick-off times are PFR's, with no stated time zone; they are not in `schedule.csv`.
* Order within a week is PFR's listing order, not a broadcast order.
* Overtime is not recorded (the PFR games table has no OT column); nothing in the slice needs it.
* Not covered: preseason, playoffs (they are in `nfl_seasons_1970_1980.json`), neutral sites (no
  1980 regular-season row carries PFR's neutral mark `N`; the parser fails on any mark it does not
  know).

# M14 results: season end (playoffs, Super Bowl, awards, draft hand-off, rollover to 1981)

2026-10-17. Every in-game test ran `NFL2K5.exe --retail` in background mode only (`RECOMP_BACKGROUND=1`,
`RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, press file, `NFL2K5_PRIVATE_PACKS=1`, its own exe copy
`build\Release\NFL2K5_season.exe`, sandbox root `logs\season\sbx-root` (junctions + a copied `saves\`, removed
with `cmd /c rmdir`), sandbox user folder `logs\season\home` (a copy of `logs\eras\office80-home`: a watch
league with 11 played games), through `tools/eras/season/season_test.ps1` and `nav_office.sh`). The build lock
`build\.eras-build.lock` was held for the build. The real `saves\UDATA` is untouched (nothing in these runs
writes outside the sandbox root and the sandbox user folder).

Contact sheet: **`logs/eras/season-end-sheet.png`**.

## 1. Data (offline tools; `data/eras/runtime/1981/` with PROVENANCE)

| item | tool | source | result |
|---|---|---|---|
| 1981 standings, playoff table, Super Bowl XVI | `tools/eras/league_history.py` (range extended to 1981; `ROMAN` XVI, `SB_SITE` 16) | PFR `/years/1981/` Wayback 2025-10-10 (already cached) | `nfl_seasons_1970_1980.json` now holds 1970-1981 (the file keeps its name) |
| 1981 schedule (224 games) | `tools/eras/league_schedule.py --season 1981 --fetch` (new `SOURCES[1981]`; the NFL-book parser learnt the left column of a page) | PFR `/years/1981/games.htm` Wayback 2026-09-26 (primary) and 2022-08-12 (second copy), the 2012 NFL Record & Fact Book "Past Standings" (1981 p. 386), PFR 1981 standings; 28 Wikipedia season articles | 224 games, 16 per team, dates 1981-09-05 .. 1981-12-21, 1 tie; W-L-T and PF/PA equal the NFL book and PFR for all 28 teams; the two PFR copies agree game for game; Wikipedia: 415 of 448 team-games agree, 0 disagree, 33 rows not found (the Steelers and 49ers articles' table formats) |
| 1981 head coaches | **new** `tools/eras/head_coaches.py --season 1981` | PFR `/years/1981/coaches.htm` Wayback 2025-12-31 | 28 coaches, no in-season change; `head_coaches_1981.json` |
| 1981 rosters (PFR membership, G / GS, K / P) | `tools/eras/league_starters.py --season 1981 --fetch` | PFR team roster pages (two Wayback copies each) and team pages | see section 1.1 |
| 1981 roster selection | `tools/eras/roster_membership.py --season 1981` | the 1981 PFR rosters, the community saves (1981 first) | see section 1.1 |
| 1981 uniform styles | `data/eras/league/team_looks.json` | the disc's uniform year labels (ERA-LOGOS.md) | 12 spans whose disc label covers 1981 extended to 1981 (bills, patriots, jets, chiefs, raiders, seahawks, bears, lions, vikings, buccaneers, saints, 49ers); the other 16 teams keep their template's style (no certain source) |
| 1981 stadiums | `stadiums_1966_2025.json` (season-keyed already) | STADIUMS.md | via `make_runtime_package.py` |
| postseason rounds, Super Bowl site and designated home | **new** `postseason.csv` (make_runtime_package.py `write_postseason`) | PFR playoff table dates; `SB_HOST` (Superdome -> saints' 1980 venue "LOUISIANA SUPER DOME", Silverdome -> lions' 1981 venue); designated home AFC in even-numbered Super Bowls, NFC in odd (Wikipedia Super Bowl XV / XVI infoboxes) | 1980: SB XV 1981-01-25 at LOUISIANA SUPER DOME, NFC home; 1981: SB XVI 1982-01-24, AFC home |
| player progression 1980 -> 1981 (Alternate History) | **new** `tools/eras/make_progression.py` | PFR 1981 rosters (retirements), the 1981 package's community ratings | `data/eras/runtime/1981/progression.csv`; see 1.2 |

### 1.1 1981 rosters

(filled below)

### 1.2 Progression

(filled below)

## 2. Playoffs (`src/nfl2k5_eras_playoffs.c`)

Format: `rules_1966_2025.json` playoff_teams 1978-1981 = 10, `nfl_seasons_1970_1980.json` playoff_format: three
division winners and two wild cards per conference; the wild cards meet at the better seed; the divisional round
pairs the wild-card winner with the 1 seed unless they share a division (then 3 at 1 and the wild-card winner at
2: the 1970-1989 rule that two teams of one division cannot meet before the conference final, NFL Record & Fact
Book via `league_history.py`); the higher seed hosts the conference championship; the Super Bowl at the
package's neutral site with the designated home conference.

Tiebreakers: the NFL procedure as applied in 1980, in the order the 1980 cases show (Wikipedia, *1980 NFL
season*, "Tiebreakers": Cleveland over Houston in the AFC Central on conference record 8-4 to 7-5; San Diego
over Oakland on net points in division games; San Diego the top AFC seed on conference record over Cleveland and
Buffalo; Cleveland the 2 seed on common games 5-2 to 5-3; Oakland the first wild card over Houston on conference
record 9-3 to 7-5; Kansas City over Denver head-to-head; Philadelphia over Dallas on net points in division
games; Atlanta the top NFC seed head-to-head over Philadelphia; Minnesota over Detroit on conference record;
Tampa Bay over Green Bay head-to-head), with the structure of the NFL's published procedure
(https://www.nfl.com/standings/tie-breaking-procedures: one club advances per step, the rest restart at step 1;
three or more wild-card clubs: the division procedure first leaves one club per division). The steps the NFL
added later (strength of victory / schedule, the scoring rankings) are not used.

* division: head-to-head, division record, conference record, common games, net points in division games, net
  points in all games, coin toss (a seeded, logged draw);
* wild card / seeding of the division winners: head-to-head (when every pair has met; three or more clubs: a
  sweep), conference record, common games (at least four), net points in conference games, net points in all
  games, coin toss.

A percentage counts a tie as half a win. When the regular season's games are all played the field is seeded
(`seeds.csv` with the deciding step of each seed, `eras.log`), the wild-card games are appended to the game list
(`playoffs.csv`: id `1980wc-<away>-<home>`, week 17; `dv` 18, `cc` 19, `sb` 20) and the office's next game is
the next playoff game; results commit as regular games; each later round is built when the one before is
complete. Historical mode does not force the real results: the field is whatever the league produced.

## 3. Screens (`src/nfl2k5_eras_sheets.c`)

The Standings machinery of M7 (the Franchise Schedule descriptor copy, the retail sheet engine and helmet
columns, pseudo grid rows 16..21) was generalised into pages. New office rows:

* **Playoff Picture / Bracket** (row "Playoff Picture" from week 13, "Playoff Bracket" once the regular season
  is complete). Page 0: tabs AFC / NFC; rows SEED, helmet, TEAM, W, L, T, CLINCHED (Home Field Adv. / Division
  Title / Playoff Berth from sufficient conditions without tiebreak look-ahead; else "Leads division" / "Wild
  card spot" / "In the hunt"); title "If the playoffs started today" or "Final seeds". Page 1 (LB/RB): tabs by
  round; each game as the retail away helmet (with its score once played, the winner highlighted), MATCHUP
  "(5) Raiders at (4) Oilers", DATE, RESULT, the retail home helmet and score.
* **Awards** (after the Super Bowl). Page 0: Most Valuable Player, Offensive Player of the Year, Defensive Player
  of the Year, Coach of the Year, each with the helmet, the player (position), the team and the stat line; the
  right-hand title says how many played games had a box score. Rule (named on the screen and in `awards.csv`):
  player points = pass yds / 25 + (rush + rec yds) / 10 + 6 per TD - 2 per interception thrown (K: 3 per FG +
  XP); defensive points = tackles + 4 per sack + 6 per INT + 3 per forced fumble; Coach of the Year = the best
  regular-season record (percentage, then point difference), the head coach from `coaches.csv`. Page 1: League
  Leaders, tabs PASSING YARDS, RUSHING YARDS, RECEIVING YARDS, TOUCHDOWNS, TACKLES, SACKS, INTERCEPTIONS, FIELD
  GOALS (top 16). Source: `box\<game_id>.json` of every played regular-season game (summed per player).
* The retail Playoff Picture `0x560DC0` and Awards `0x561750` are custom-drawn screens (FRANCHISE-SCREENS-REFERENCE);
  as with Standings (M7) the copies ride on the sheet engine instead.

## 4. Season end -> Draft -> Rollover (`src/nfl2k5_eras_rollover.c`)

(filled below)

## 5. Tests

(filled below)

## 6. Defects / skipped

(filled below)

# My NFL Eras: real draft classes, rookie ratings and hidden potential

2026-10-16. Research for the Eras draft (PHASE2-PLAN.md "Later": Draft, draft potential
Hidden/Scouted/Shown, Senior Bowl, draft show). Nothing here is wired into the game yet.

Data files:

* `data/eras/league/drafts/1981.json`: the complete 1981 NFL draft, 332 picks in 12 rounds,
  parsed from Pro-Football-Reference (PFR) and checked pick by pick against Wikipedia.
* `data/eras/league/drafts/1981_ratings_match.json`: every 1981 pick matched against the
  community PS2 rosters 1981-1995 (`data/eras/community_rosters/`, see COMMUNITY-ROSTERS.md):
  rookie record, peak season, overall by season and per-season match checks.

Source label for all ratings below: **community PS2 roster (unverified)**. The picks are real
(PFR + Wikipedia); the ratings are the save authors'.

## 1. Draft data sources

**Primary: PFR draft listing**, `https://www.pro-football-reference.com/years/<year>/draft.htm`.
The live site answers scripts with HTTP 403 (as noted in LEAGUE-HISTORY.md), which was not
bypassed. The Wayback Machine has recent copies of every year. The raw copy (`id_` URL,
`https://web.archive.org/web/<timestamp>id_/<live url>`) comes back gzip-compressed and holds
the full `table#drafts` (WebFetch can't read web.archive.org; curl can). Snapshots were found
with the CDX API
(`https://web.archive.org/cdx/search/cdx?url=www.pro-football-reference.com/years/<year>/draft.htm&filter=statuscode:200&fl=timestamp`).
The availability API (`archive.org/wayback/available`) returned 429 or empty results for most
years, so use CDX instead.

| Draft | Snapshot (fetched and parsed 2026-10-16) | Picks | Rounds short (forfeits) |
| --- | --- | --- | --- |
| 1981 | [20261012035621](https://web.archive.org/web/20261012035621/https://www.pro-football-reference.com/years/1981/draft.htm) | 332 | 3, 5, 7, 9 have 27 |
| 1982 | [20260729074457](https://web.archive.org/web/20260729074457/https://www.pro-football-reference.com/years/1982/draft.htm) | 334 | 1, 11 |
| 1983 | [20260916135732](https://web.archive.org/web/20260916135732/https://www.pro-football-reference.com/years/1983/draft.htm) | 335 | 9 |
| 1984 | [20261005045840](https://web.archive.org/web/20261005045840/https://www.pro-football-reference.com/years/1984/draft.htm) | 336 | none; the page also has `table#drafts_supp` (1984 supplemental draft of USFL/CFL players), not parsed |
| 1985 | [20260928164130](https://web.archive.org/web/20260928164130/https://www.pro-football-reference.com/years/1985/draft.htm) | 336 | none |
| 1986 | [20261005044311](https://web.archive.org/web/20261005044311/https://www.pro-football-reference.com/years/1986/draft.htm) | 333 | 1, 3, 8 |
| 1987 | [20260729074439](https://web.archive.org/web/20260729074439/https://www.pro-football-reference.com/years/1987/draft.htm) | 335 | 7 |
| 1988 | [20260928142057](https://web.archive.org/web/20260928142057/https://www.pro-football-reference.com/years/1988/draft.htm) | 333 | 1, 3, 4 |
| 1989 | [20260729074252](https://web.archive.org/web/20260729074252/https://www.pro-football-reference.com/years/1989/draft.htm) | 335 | 5 |

1982-1989 were fetched and parsed only to count picks; their JSON files are not written yet.
Each year has two or more other 2026 snapshots in CDX.

PFR columns kept per pick: round, pick, team (PFR code + `team_pfr` URL code), player, PFR
position (coarse: DB, LB, DL ...), age, college, `pfr_id`, HOF mark, and the career columns
(`last_season`, `ap1`, `pro_bowls`, `years_starter`, `career_av`, `draft_av`, `games`) as of
the snapshot date. 227 of the 332 picks have NFL games.

**Cross-check: Wikipedia** [1981 NFL draft](https://en.wikipedia.org/wiki/1981_NFL_draft),
read as wikitext (`index.php?title=1981_NFL_draft&action=raw`). All 332 `NFLDraft-row`
templates were compared with PFR by pick number:

* Team: every PFR club code maps to exactly one Wikipedia team name (28 clubs). Saints with 17
  picks agrees with the infobox ("most").
* Name: 328 agree. Three are spellings: #169 Kenny Neil / Neal, #250 Mike Barker / Barber,
  #297 Claude Mathews / Matthews. **#293 (MIN, round 11) disagrees:** PFR has Bill Stephanos
  (T, Boston College), Wikipedia has Brent Knourek (FB, Oregon State). Both are kept
  (`player` = PFR, `wiki_name`), unresolved. Neither has NFL games.
* Spot checks: #1 George Rogers NO, #2 Lawrence Taylor NYG, #8 Ronnie Lott SF agree in both.
* Forfeits: Wikipedia lists ATL round 7 and SD round 9 as forfeited for 1980 supplemental
  picks. PFR's four 27-pick rounds agree with the gaps in the pick numbers.
* Hall of Fame: PFR marks 7 picks. Wikipedia's infobox counts 8 because it adds Sam Mills,
  who went undrafted in 1981.
* `wiki_position` (finer than PFR: Cornerback, Safety, Offensive tackle ...) and `wiki_team`
  are stored on each pick.

**Other draft-pick data (not fetched):** nflverse `draft_picks` (every pick since 1980, taken
from PFR, with `pfr_player_id`, `cfb_player_id`, age, AV, games and stats). Docs:
https://nflreadr.nflverse.com/reference/load_draft_picks.html. File:
`https://github.com/nflverse/nflverse-data/releases/download/draft_picks/draft_picks.csv`
(also `.parquet`, `.rds`). It would make a good third check, or a bulk source for 1980-2025
without Wayback.

## 2. 1981 rookies in the community rosters

Matching (`1981_ratings_match.json` `method`):

* Name: first|last, with accents, punctuation and Jr/Sr/II/III removed. The Wikipedia spelling is
  tried as well. If nothing matches, a first-name variant (one first name a prefix of the other,
  e.g. Ken/Kenneth) is accepted only when both position and college match.
* Position groups: RB to HB/FB; LB to OLB/ILB/DE; DB/CB/S to CB/SS/FS; T/G/C to the OL
  positions; DE/DT/NT to DE/DT (DE also OLB); K and P to each other.
* College: aliases (LSU, SMU, BYU, USC, UCLA ...), "St." means State. A candidate is rejected
  when both position and college differ.
* Confidence: 3 = position and college agree, 2 = one agrees and the other is unknown,
  1 = position agrees but college differs. It drops by 1 when `years_pro` is more than 2 away
  from season - 1981 + 1. In the 1981 save LT has `years_pro` 1.
* Career span: seasons after PFR's `last_season` are not used. 59 picks have a same-name
  record outside their career, either a later player (Kevin Donnalley 1991-95, Johnny Ray Smith
  1985-95) or a retiree left in a save (LT 1994-95, Howie Long 1994-95). Picks with no NFL
  games are searched only in 1981 and 1982. 0 such picks matched.
* No pick had two equally good candidates in any season.

| Round | Picks | Rated in 1981 save | First rated 1982 | First rated 1983+ | Unmatched |
| --- | --- | --- | --- | --- | --- |
| 1 | 28 | 21 | 3 | 4 | 0 |
| 2 | 28 | 21 | 1 | 4 | 2 |
| 3 | 27 | 19 | 3 | 2 | 3 |
| 4 | 28 | 17 | 3 | 1 | 7 |
| 5 | 27 | 15 | 3 | 1 | 8 |
| 6 | 28 | 10 | 7 | 0 | 11 |
| 7 | 27 | 12 | 1 | 2 | 12 |
| 8 | 28 | 9 | 2 | 0 | 17 |
| 9 | 27 | 6 | 2 | 2 | 17 |
| 10 | 28 | 5 | 6 | 2 | 15 |
| 11 | 28 | 4 | 1 | 2 | 21 |
| 12 | 28 | 6 | 1 | 1 | 20 |
| **All** | **332** | **145** | **33** | **21** | **133** |

* 178 picks are rated in the 1981 or 1982 save, and 199 are rated in some season 1981-1995.
  Of the 227 picks who played, 199 are rated and 28 are not. 20 of those 28 have career AV
  of 2 or less; the exceptions include Joe Delaney (KC, #41, 1981 Pro Bowl, AV 20), who is
  missing from the 1981-82 saves. The 105 picks who never played are all unmatched.
* All 145 rookie records sit on a club roster. 115 of them are also in the 1982 save.
* Rookie confidence: 116 at 3, 9 at 2, 20 at 1. The confidence-1 cases are save errors on
  well-known players (Ronnie Lott "Ohio State", Neil Lomax "Florida State", Hoby Brenner
  "Idaho"). Position, team and years_pro identify them, and their later seasons have the
  right college.
* Examples. First rated 1982: Freeman McNeil (#3), Howard Richards (#26). First rated 1983:
  Keith Van Horne (#11, only 1983-84, although he started 1981-93), Keith Gary (#17, CFL
  first), Marion Barber Jr. (#30).

## 3. Hidden potential from real careers

`overall_estimate` is the extractor's position-weighted estimate, not the game's overall. The
league median per save stays between 67.3 and 72.2 over 1981-1995, so values are compared
across seasons without rescaling.

**Lawrence Taylor (#2, NYG):**

| Season | 81 | 82 | 83 | 84 | 85 | 86 | 87 | 88 | 89 | 90 | 91 | 92 | 93 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Pos | ILB | OLB | OLB | OLB | OLB | OLB | OLB | OLB | OLB | OLB | OLB | OLB | OLB |
| Overall est. | **77** | 89 | 89 | 94 | 89 | **96** | 93 | 92 | 89 | 96 | 86 | 86 | 95 |

Rookie (1981) against peak (1986, 5 years after the draft, his MVP season; 1990 ties at 96):
speed 56 to 92, agility 47 to 82, strength 79 to 100, tackle 86 to 100, pass rush 84 to 100,
run coverage 77 to 97, coverage 70 to 84, composure 85 to 100, consistency 71 to 100,
durability 56 to 100. In the 1981 save he is an ILB with a weak rookie profile, although that
season was his Defensive Rookie of the Year season. The save authors rate how a player is
seen at the time, not his later career. That is why the rookie and peak records differ, and
why the gap between them can serve as potential.

Others: George Rogers 68 to 73 (peak 1982), Ronnie Lott 82 to 87 (1993), Howie Long 76 to 86
(1983), Hugh Green 76 to 78 (1985).

Across the 145 rated rookies, peak minus rookie has a median of 4 and a mean of 4.8. Peak
season: 45 in the rookie year, 33 one year later, 67 two or more years later. Growth from
year 1 to year 2 has a median of +2 (115 players). Mean rookie and peak by round: R1 70.6 to
77.0, R2 71.0 to 77.2, R3 69.8 to 73.7, then roughly 66-71 to 68-75. Peak overall
correlates with PFR career AV (r = 0.62, n = 199): the median peak is 70 at AV 0-9, 73 at
AV 10-29, 75 at AV 30-49 and 78 at AV 60+.

### Proposed method (per drafted player, per draft year Y)

1. **Identity**: match as in section 2, within Y .. PFR `last_season`. Store the confidence
   and the reasons for each season.
2. **Rookie record**: take the year-Y record if there is one (status `rookie_rated`).
   Otherwise take the year-Y+1 record and keep it unchanged, flagged
   `rookie_from_next_season`. Taking off the observed +2 median would mean inventing
   per-rating values. Otherwise there is no rookie record (see 6).
3. **Peak target**: the season with the highest `overall_estimate` in Y .. min(last_season,
   Y+12). That record's 28 ratings become the target profile. Also store `years_to_peak`
   and `career_max_ratings`, the per-rating maximum, which is for reference only because it
   mixes seasons.
4. **Gaps**: the missing seasons (1975, 1996, 1999-2000, 2005, 2014) are skipped, never
   filled in. If the window crosses a gap, set `window_has_gap`; the peak is taken over the
   seasons that exist. The 1981 class window, 1981-1995, has no gap. Classes from 1985 on
   lose part of their window at 1996 and 1999-2000.
5. **Rated once** (27 of the 1981 picks): potential = that record, years_to_peak = 0, flag
   `single_observation`.
6. **Never rated**: no ratings are made up. Played but unrated players (28) and those who
   never played (105) keep their real name, college, position and pick, flagged
   `ratings_source: "generated"`. They get the Eras generator's rookie ratings, banded by the
   observed relation between round and rookie overall and between AV and peak above
   (unrated players who played: AV band; never played: the lowest band, camp body). An
   alternative is to leave them out of the class and let the game's own 380 prospects fill
   the gap.
7. **Storage**: a PC-side hidden development profile (PHASE1-ARCHITECTURE.md section 4) per
   player: `{rookie_record, target_record, years_to_peak, peak_season, sources, flags}`.
   "Hidden/Scouted/Shown" (PHASE2-PLAN) controls only what the UI reveals: Scouted could show
   the round-typical band, and the Senior Bowl reveal narrows it to the target.
8. **Use**: in Historical modes, each season loads the player's observed record for that
   season, when it exists, ahead of any progression. Potential matters only for Alternate
   History and for seasons with no observed record. There, each offseason moves every rating
   toward the target by (target - current) / remaining years until `years_to_peak`, then
   decays along the curve the data show after the peak (to be measured, section 5).

### What 2K5 itself has

* No potential field is documented in the player record (field table
  `.tmp-2k-football-mod-tools/mod_editor/core/nfl2k5_roster_records.py` lines 710-779).
  Unidentified bytes: +0x09, +0x0D..+0x0F, +0x30..+0x33, +0x52, +0x53 bits 1-7, and several
  high nibbles. None of them is known to be potential.
* Rookies: the game regenerates **380 prospects** into a fixed window of records (the 380
  after the last real player) and flags each one (+0x08 bit 4,
  `FUN_002BE6F0`; generator chain `0x2BE940 -> 0x2BE900 -> 0x2BD390/0x2BE6F0`, at franchise
  start and at preseason to regular season `0x2BF8B0`). Prospects can only be edited in
  place (`DRAFT_CLASS_WHY` in the same file;
  `.tmp-2k-football-mod-tools/ASTRA_MYCAREER_SUPERSIM_DRAFT_REPORT.md` section B). Every
  community save 1980-1995 carries 380 draft-class records. In the 1980 and 1981 saves they
  are nameless templates, so the saves contain no real class.
* Progression and ages run natively at season rollover (`0x2480B0 -> ... -> 0x247B40`, same
  report). Its formula is not documented. Franchise Options has an "Edit Rookies" Off/On row
  (FRANCHISE-OFFICE-MENUS.md), and ESPN.com has Rookie Report, Mel's Mock Draft and Draft
  Recap tables (FRANCHISE-SCREENS-REFERENCE.md rows 40 and 54).
* Since the Eras league lives PC-side, the Eras draft does not need the native class. A real
  class can be written into the 380-record window with in-place edits if the native draft
  screens are reused. Signed rookies go onto Eras rosters through the existing USER1/USER2
  loader.

## 4. Community draft-class files (reported only, nothing downloaded)

| Game | What | Link | Format / notes |
| --- | --- | --- | --- |
| Front Office Football 2007 | Historical rosters 1960-2008 and **draft files 1961-2008**, re-rated from PFR (nilodor, 2007; reupload March 2011) | https://forums.operationsports.com/fofc/showthread.php?p=1353070 (thread; 403 to WebFetch) | FOF "alternate player file" and per-year draft files; original host links are probably dead. This is the closest community equivalent of real 1980s classes with ratings |
| Front Office Football | 1980 player file: 491 real players plus 25 others | https://forums.operationsports.com/fofc/archive/index.php/t-39405.html | FOF player file, no draft history |
| Madden (Xbox 360) | Cursive's Classic Madden Rosters and Draft Classes, from **1987** onward | https://forums.operationsports.com/forums/madden-nfl-old-gen-rosters/587508-cursives-classic-madden-rosters-draft-classes-360-a.html | Madden 360 roster/draft saves; 403 to WebFetch, so the years and host are not confirmed |
| Madden 2005 (PS2/Xbox) | Draft classes exported from NCAA dynasties (camulos PS2, gr8fuldave Xbox) | https://forums.operationsports.com/forums/madden-nfl-last-gen-rosters/91347-madden-2005-draft-classes-ps2-camulos.html, https://forums.operationsports.com/forums/madden-nfl-old-gen-rosters/91630-madden-2005-draft-classes-xbox-gr8fuldave.html | Madden 2005 saves; 2004-2005 classes, not 1980s |
| Madden PC | Franchise Flashback 2002 roster mod with draft classes | https://forums.operationsports.com/forums/madden-nfl-football-pc/946768-franchise-flashback-2002-roster-mod-w-draft-classes.html | Madden PC files; early 2000s |
| Madden 04-06 | MaddenAmp roster and draft-class editor | https://github.com/keylimesoda/MaddenAmp | Tool, not data; it could read a Madden-format class if one turns up |
| Retro Madden | Franchise Football Community (Discord), "throwback rosters ... draft classes" | https://discordhunt.com/en/servers/franchise-football-community-512074651195801621 | Unverified; Discord membership needed |
| ESPN NFL 2K5 | "Draft classes" thread: forum answer that 2K5 can't load classes, only edit them in franchise | https://forums.operationsports.com/forums/espn-nfl-2k5-rosters/346879-draft-classes.html | No 2K5 historic draft-class files found |
| NCAA Football 06/14 | No 1980s classic draft classes found | | |

## 5. Plan

1. `build/eras/draft_classes.py`: CDX lookup, then the `id_` fetch with gzip handling, cached
   under `build/eras/league/pfr_cache` as LEAGUE-HISTORY does. It parses `table#drafts` (and
   `drafts_supp` for 1984), runs the Wikipedia wikitext cross-check, and writes
   `data/eras/league/drafts/<year>.json` for 1981-1989. The prototype scripts for this page
   (`parse_pfr.py`, `xcheck_wiki.py`, `match_ratings.py`, `compact_match.py`) are in this
   session's scratchpad and should be moved into `build/eras/`.
2. `build/eras/draft_ratings.py`: matching as in section 2 for each class, then the profile
   fields from section 3 (`rookie_record`, `target_record`, `years_to_peak`, flags), written
   to `data/eras/league/drafts/<year>_ratings_match.json`.
3. Measure the decline after the peak (overall by years after peak, by position) across the
   1981-1989 classes, for step 8 of the method.
4. Fit the generated-rookie bands for unrated picks (round to rookie overall, AV to peak) on
   the 1981-1989 matches, and document them.
5. Wire the Eras draft: class JSON to the Eras draft screen, profiles to the hidden store,
   signed rookies to rosters. Writing into the native 380-record window is optional.

Open questions:

* #293 MIN 1981: Stephanos (PFR) or Knourek (Wikipedia)? A third source such as nflverse or
  the newspaper draft list Wikipedia cites would settle it.
* Should the community saves' 1982+ records of players who are not in the 1981 save be used
  as rookie ratings? Keep the flag either way.
* Does 2K5's native progression use any hidden byte? Test by diffing a player before and
  after a native season rollover.
* Players whose saves are sparse (Keith Van Horne: 2 of 13 seasons) get a peak from very few
  observations. Should a minimum number of seasons be required before trusting the peak?
* Should picks who went to the CFL or USFL first (Keith Gary, David Overstreet) enter the Eras
  league in the year they actually arrived?

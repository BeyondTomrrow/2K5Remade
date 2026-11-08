# My NFL Eras: phase 1, the existing architecture

2026-10-15. What the project already has that My NFL Eras can build on, and where the
original game sets hard limits. "Verified" means seen in this project's code or in game;
"from the mod tools" means taken from 2K Football Mod Tools' documented research
(`.tmp-2k-football-mod-tools/mod_editor/core`), not re-checked here.

## 1. Historic teams on the disc (verified, in game)

`docs/eras/HISTORIC-TEAMS-INVENTORY.md`, data `data/eras/historic_teams_2k5.json`.

* 75 historic teams, seasons 1953-2004, every one loads with the game's own historic
  loader into a spare team slot (dumped in game, `NFL2K5_HISTORIC_INVENTORY=1`).
* Each has 53 players with numbers, positions, heights, weights, birth dates and the
  game's ratings, plus city, nickname, abbreviation and a uniform variant.
* **Only 682 of 3,975 players carry real names.** Pre-2000 teams are almost all
  placeholders ("49ers Center"). The ratings and physical data are the game's, not
  verified history. Historic rosters therefore have to be imported from sources;
  the disc gives team identities, uniforms and logos, not people.
* No coaches with real names were checked yet; team records point at coach records.

## 2. Teams the game can field

* Resident roster: 52 team records (the 32 clubs, two Create a Team slots USER1/USER2,
  all-star and alumni sides); a team record holds up to 65 players, 53 active
  (from the mod tools; `docs/XBOX-LIMITATION-AUDIT.md` items 3, 7, 8).
* Any one-team roster (historic or `mods\teams\<name>\team.rost`) can be loaded into
  USER1/USER2 and played in Quick Game, Practice and First Person Football
  (`src/nfl2k5_teams.c`, verified). That is the mechanism a PC-side league can use to
  stage any two teams for a game.
* A 155-record spare player pool backs the two slots (mod tools).

## 3. The retail Franchise mode (from the mod tools; not re-tested)

* League: the 32 resident clubs, iterated by 0x778C0/0x77950; fixed divisions and a
  fixed 17-week schedule. Created/historic slots are not franchise members.
* Length raised to 95 seasons (through 2099) in this project
  (`docs/XBOX-LIMITATION-AUDIT.md` item 2; a 31st season not yet played).
* Save `SAVEGAME.DAT`, 720,044 bytes: settings prefix, the roster arena (0x91040),
  the SEASON block (an image of RAM 0xE57776, schedule/standings/stats), and the
  FRONT-OFFICE block (draft order, transactions, trades, free agency, cap, IR)
  (`nfl2k5_franchise_save.py`). The save library keeps that layout
  (`src/nfl2k5_save_library.cpp`).

Consequence: a 26-team 1970 league, a 28-team 1980 league, AFL/NFL structures,
expansion and relocation cannot live inside the retail franchise's fixed 32-team
tables without rewriting its schedule, standings and draft code. The plan below keeps
the league on the PC side and uses the engine to play the games.

## 4. Proposed design (to build; nothing below exists yet)

* **League database** (PC side, versioned JSON + per-save state under
  `%LOCALAPPDATA%\NFL2K5-PC\eras\<save>`): franchises with stable lineage IDs
  (e.g. `colts` with identities `BAL_1970` -> `IND_1984`), seasons, divisions,
  schedules, players (identity separate from team-season assignment), contracts,
  draft classes, hidden development profiles, history timeline, rule sets.
* **Playing a game**: load the two franchises' current rosters into USER1/USER2
  (the existing loader, extended to write a roster from the database), start the game
  through the normal match flow, and read the final score and box score when it ends
  (a new hook at the game-over/stats screen) back into the database.
* **Menus**: built into the game's own frontend (the user's rule: native menus), as
  the video and controller menus already are (`src/nfl2k5_video_menu.c`,
  `src/nfl2k5_controller_setup.c`).
* **Rules**: season rule sets in data; each rule needs an engine hook that changes
  play (kickoff spot, overtime, hash marks, ...). Each one must be found in the game
  code and tested; a data field alone does not count.

## 5. Inputs that are missing

* `nfl_1970s_rules_research.json` / `.csv`: not found on E: or in the user profile.
* The 1968 Cowboys (Landry) and 2008 Alabama (Saban/Smart) playbook research: not found.
* Verified historical rosters for any season: none in the project yet.

## 6. Next steps

1. Find the game-over/final-score point and the box-score tables (needed to record a result).
2. A roster writer: database roster -> a spare slot (names, numbers, positions, ratings).
3. The first native My NFL Eras menu entry and era screen.
4. 1980 league data (28 teams, divisions, schedule) from verified sources, then one
   scheduled game played end to end with the result saved.

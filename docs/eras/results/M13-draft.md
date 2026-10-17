# M13 results: the My NFL Eras draft (the real 1981 class, the Big Board, the user's pick, rookies)

2026-10-17. The draft show of M11/M12 now announces the league's own picks. Every in-game test ran
`NFL2K5.exe --retail` in background mode only (`RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`,
`NFL2K5_NO_HOST_PAD=always`, press file, `NFL2K5_PRIVATE_PACKS=1`, its own exe copy, sandbox root
`logs\draftshow\sbx-root` (junctions + a copied `saves\`, removed with `cmd /c rmdir`), sandbox user
folders `logs\draftshow\home` (watch league) and `home-ah` (Seahawks league), through
`tools/eras/draftshow/draftshow_test.ps1`). The build lock `build\.eras-build.lock` was held. The real
`saves\UDATA` (507 files) was SHA-1 hashed before (`logs\draftshow\udata-before.sha1`) and after: see
"Checks" below.

Contact sheet: **`logs/eras/draft-sheet.png`** (`tools/eras/drafts/make_draft_sheet.py`).

## 1. The 1981 draft class (`tools/eras/drafts/make_draft_class.py`)

Inputs: `data/eras/league/drafts/1981.json` (every real pick, PFR + Wikipedia),
`1981_ratings_match.json` (the community-roster matches), `data/eras/community_rosters/<season>.json`
(the records: 28 ratings, jersey, height, weight) and the PS2 save payloads through the Mod Tools'
`RosterDocument` (skin / face / hand of the record). Outputs: `data/eras/league/drafts/1981_class.json`
(documentation: sources, flags, the estimate bands, the position medians) and **`mods/eras/draft/class_1981.csv`**
(what the game reads: 332 rows, 79 columns: pick, round, team, names, 2K5 position, college, body,
jersey, source, overall, peak overall / season, games, career AV, flags, 28 rookie ratings, 28 peak
ratings).

Method (DRAFT-CLASSES.md section 3 and the user's decisions of 2026-10-16):

| source | picks | rookie ratings | potential |
|---|---|---|---|
| `rookie_rated` | 145 | his record in the 1981 save | his real peak season's 28 ratings (highest overall estimate 1981-1993) |
| `next_season` | 33 | his 1982 record, unchanged (flag `rookie_from_next_season`) | same |
| `rated_later` | 21 | his first rated season's record (flag `rookie_from_<year>`) | same |
| `estimated` | **133** | the position's mean rookie profile of the rated round 7-12 picks, scaled to a target by round and real career: never played 52 / 50 / 48 / 46 (rounds 1-3 / 4-6 / 7-9 / 10-12); played, AV <= 2: 55; AV 3-9: 60; AV 10-29: 64; AV 30+: 68. Flags `estimated`, `estimated_body` (height / weight / skin / face / hand = the 1980 package's position median / mode); jersey 0 (assigned when he joins a roster) | the same profile at the band's peak (55 / 64 / 70 / 74), the rookie profile when he never played |

Overall = the Mod Tools' position-weighted estimate (`OVERALL_WEIGHTS`), the number every figure in
DRAFT-CLASSES.md uses; it is not the game's overall. Means by round: R1 70.2 (peak 75.4), R2 68.9, R3
68.0, R4 65.2 ... R12 53.5 (the estimated picks pull the late rounds down, as intended). Positions:
CB 55, HB 43, WR 41, ILB 30, DE 25, TE 24, T 23, G 21, OLB 20, DT 18, QB 16, C 6, K 5, P 2, FB 2, SS 1.
199 looks come from save records, 133 are the position mode. Source label of every rating: community
PS2 roster (unverified). `--check` re-reads the written files.

Per league: on the first open of a league's draft the class is copied to
`<league>\draft\1981\class.csv` and that copy is what the league reads from then on. In Alternate
History N **sleepers** (league.ini `sleepers`, default 5, Office Options "Sleepers" Off / 1-10) are
drawn there from the estimated / round 7+ pool (no K / P) with a seed from the league's creation
stamp: flag `sleeper`, potential = the rookie profile scaled to 88 overall, 3-5 years to the peak.
The seahawks league drew #151 Alvin Lewis (HB, 50 -> 88), #187 Eddie Johnson (ILB, 65 -> 88), #179
Mike Daum (T), #290 Pat Walker (WR), #329 Nate Lundy (WR) (`eras.log`). Nothing in the UI shows the
potential or the flag (the roster flags file strips the word too); it waits for development.

## 2. Order and picks (`src/nfl2k5_eras_draft.c`)

* **Historical** (league.ini `history` absent / `historical`, Office Options "History"): the real
  332 slots in pick order, team and number from the class (trades and forfeits already in it). A CPU
  team takes its real pick while he is available (the user may have taken him), else the best
  available by need and value (no randomness).
* **Alternate History**: every team picks every round, worst record first, the same order each
  round: fewer wins, then more losses, then worse point difference, then fewer points scored (the
  reverse of the standings order in `nfl2k5_eras.c`), then **the real 1981 order** as the last
  tiebreak, so an unplayed league drafts in the real order. Slots stop at the class size (332 of 336:
  the real forfeits). The seahawks league (11 games played) drew CHI ATL NYJ DET NYG SF CIN CLE PHI
  BUF HOU **SEA** KC WAS OAK SD DAL NO STL TB MIA DEN PIT NE LA BAL GB MIN. CPU value = overall x 4 +
  need x 8 (need = a 45-man roster's typical count at the position minus the team's, clamped -2..4,
  counted from the roster file plus the picks made) - 40 for a K / P before round 6 + 0..16
  (0..4 overall points) from a seed per league and slot.
* Every pick is appended to `<league>\draft\1981\picks.csv` (slot, round, pick, team, class pick,
  by = real / cpu / user, name, position, college, overall, source) and logged; the draft reopens
  from that file.

## 3. The screens (`src/nfl2k5_eras_draft_sheets.c`, served at 0xFEE90000)

Copies of Franchise's draft tables on the retail sheet engine, the way M7 built Standings
(FRANCHISE-SCREEN-COPY.md option (c)): one descriptor (a copy of the Franchise Schedule 0x522828
with an Eras event table and page table), two Eras sheets built from copies of the weekly sheet
definition with Eras tabs, page records and text columns (copies of the retail "AWAY" column with
our getters). Nothing reads or writes the Franchise season block, the coached team or the save; the
UI statics are saved at create and restored at destroy, as the Schedule copy does.

* **The Big Board / candidates** (page 0): tabs = position groups ALL, QB, RB, WR, TE, OL, DL, LB,
  DB, K/P (LT / RT); rows = the available prospects by overall (the rookie ratings; the hidden
  potential is never read), the top 64 of a group; columns #, NAME, POS, SCHOOL, OVR (`*` =
  estimated), MOCK (the round the real draft took him in: a fact, Mel's mock). The right-hand title
  says who is on the clock ("R1 #1: SAINTS") or "YOUR PICK: R1 #12". When the user's team is on the
  clock, A on a row arms the pick (title "A AGAIN: L. TAYLOR", toast with the full line) and A again
  on the same row makes it.
* **Draft Order / Recap** (page 1, LB / RB): tabs = rounds, rows = that round's slots: PICK, TEAM,
  NAME, POS, SCHOOL ("on the clock" at the next slot; empty rows until the pick is made). The
  screen opens on the recap once the draft is complete.
* **START** advances: CPU picks until the user's team is on the clock or the draft is complete (watch
  mode sims the whole draft); **Y** is "Skip to my pick" (the same, without the show); **B** leaves.
* The office row **"1981 Draft"** (row 7, before "Draft Show (preview)") opens it directly, since the
  1980 league has no season end yet. The row says "(Alt. History)" in that mode.
* A new backdrop is not drawn: the retail background keeps the last UI team's colours (Dallas
  watermark in the frames). Cosmetic, left.

## 4. The show (`src/nfl2k5_eras_draftshow.c`)

`nfl2k5_eras_draftshow_queue_add/clear/count`: the draft screen queues the picks the Draft Show
setting covers (First Round: round 1; Your Picks: the user's; Off: none) and pushes the show screen
(EM_DRAFT, `nfl2k5_eras_menu_header(2)`); the show plays the queue in order (real team, player,
position, college, round, pick, pick-in-round) with the full M11/M12 sequence, and when the queue is
done it pops itself (`sub_0006E400` 3.5 s after the summary); the draft screen's msg 3 rebuilds its
rows. START in the show still skips to the user's next pick or the end. The preview row (no queue)
plays `picks_1981.csv` as before.

## 5. Rookies into the league (`nfl2k5_eras_draft_write_rosters`)

When the draft completes (and again on reopening if the files are missing), for every team:
the 1980 roster rows (the package's file, never written) plus the team's rookies after the last
player of their position (jersey = his record's, or the first free number of the position's range
when it is 0 or taken, logged), kept within 53 by releasing the lowest-rated players who are not
starters (depth rank 0 / left starter) and not needed for the staging minimums (M8's
`k_stage_min`), each release in `draft\1981\releases.log` and `eras.log` -> `<league>\rosters\1981\<id>.csv`
(+ `.flags.txt` with each rookie's draft line), `<league>\depth\1981\<id>.csv` (veterans keep rank /
side, rookies rank after them; the special-teams row renumbered). The marker
`draft\1981\rosters_written` makes it idempotent. The staging reads these when
`NFL2K5_ERAS_ROSTER_SEASON=1981` (`nfl2k5_eras_roster_path` / `_depth_seed_path`, used by the
launch and the Depth Chart edit); the league season itself stays 1980 until the season-end flow
exists. Results: Historical league 28 x 53 (102 releases, e.g. Colts Ben Garry, Marvin Sims, George
Kunz, Kim Anderson, Curtis Dickey); seahawks league 12 rookies joined, Dave Krieg (QB, overall 53,
not the starter) released. The rule is blunt (it does not weigh age or 1980 snaps): a later pass
can refine it; every release is on record.

## 6. Tests

| # | run | what | result |
|---|---|---|---|
| 1 | m13a (`home`, watch, Historical, Draft Show First Round) | office -> 1981 Draft -> START | **pass**: 332 CPU picks at once, `picks.csv` 332 rows all `by = real`, slot pick == class pick for every row (round 1: Rogers, Taylor, McNeil, Easley, Junior ...), rosters written; the show played round 1's queue (picks 1-3 watched: Rogers, Taylor, McNeil, `m13a-show2.png`), START ended it, pop back to the recap (`m13a-recap1.png`, round 3 scrolled `m13a-recap3.png`), B back to the office |
| 2 | m13b/m13c (`home-ah`, Seahawks, Alternate History, Your Picks) | START, A A on L. Taylor, then Y / A A per round | **pass**: 11 CPU picks then "YOUR PICK: R1 #12"; the Big Board as the candidates screen (`m13b-cand.png`), armed (`m13b-armed.png`), the pick made and the show played it (`m13b-usershow.png`: Lawrence Taylor, Seattle Seahawks, No. 12), pop back, recap shows "12 Seahawks L. Taylor" with the Chiefs on the clock (`m13b-recap_user.png`); 11 more user picks, the draft completed on reopening (m13c) and 28 roster files were written (Seahawks 53 with 12 rookies) |
| 3 | m13d (`home`, `NFL2K5_ERAS_ROSTER_SEASON=1981`, `NFL2K5_QUARTER_SECONDS=60`) | Play Next Game (1980w01 Raiders at Chiefs, CPU vs CPU) with the rookie rosters | **pass**: both sides staged from `rosters\1981\<id>.csv` (53 rows written, depth `depth\1981\<id>.csv` 53 of 53 set); full game to 13-7 KC in the second overtime; **identity at the whistle: side 0 53 players, 53 staged rows, 0 name mismatches; side 1 53 / 53 / 0**; `[ERAS] final ... failures=0 warnings=none` (`m13d-rookies-game.stderr.log`) |
| 4 | m13e (`home-fr`, retail Franchise, Cowboys) | a new franchise: preseason, season and playoffs simulated, the off-season periods skipped through their prompts, "Take me to the Combine" | **pass**: the retail NFL Combine (`m13e-combine*.png`, the scouting-hours popup), The Combine menu, **The Big Board** (`m13e-bigboard2.png`: A. Massey HB Virginia ...), the skip-combine prompt, and the **Draft room 0x537E38 with the candidates screen** (`m13e-draftroom*.png`: Quarterbacks tab, SCOUTED / 40YARD / BENCH / JUMP, the round clock running) all open and run as before; no crash, the FSM stack balanced. The pick popup itself was not completed (the clock kept running; `m13e-frpick2.png`) |

Checks: the real `saves\UDATA` is unchanged (507 files, SHA-1 before = after, 0 differences;
`logs\draftshow\udata-before.sha1` / `udata-after.sha1`); the sandbox root's junctions were removed
with `cmd /c rmdir` (teardown) and the build lock released. The season package
`data/eras/runtime/1980` was not written (rosters go under the league folder only). No retail
pixels are in git: the frames stay under `logs\`.

Defects found and fixed during the tests: the screen's guest heap was too small (48 KB; the row
text buffers alone need 56 KB); the A activation's `ecx` is the tab value, not the FSM (the first
user pick pushed the show onto FSM 0); Alternate History built 336 slots for 332 players so the
draft could not complete.

## 7. Left / skipped

* **Scouting**: no scouting grade band is shown (the "GRADE if scouted" column): there is no
  scouting action yet, so the Big Board shows OVR (rookie ratings) and MOCK (the real round) only.
  The potential stays hidden everywhere.
* **Jerseys in the show** are baked per real 1981 first-round slot (`draftjerseys.bin`, the real
  pick's team kit with the pick number): in Alternate History the user's pick 12 held the real #12
  (Oilers) kit. The builder should bake one per team instead.
* The Alternate History CPU picks are value-first (Charlie Brown WR went #1 to the Bears): the
  need term is small on purpose; tune after real seasons exist.
* Release rule: rating-only among non-starters (Curtis Dickey, Dave Krieg went); no age or snaps.
* The office background behind the draft screens keeps the last UI team's colours.
* The draft is opened from the office row; the season-end flow (playoffs -> draft -> rollover to
  1981) is still to build, so `NFL2K5_ERAS_ROSTER_SEASON` is the only way to stage the 1981 rosters.
* Round-1 jerseys / heads / looks files cover 1981 only, as in M12.
* `NFL2K5_ERAS_HUB_TEST=1` (the hub as the Main Menu's first row) was added for the background
  tests: the Main Menu cursor cannot be seen from a script.

## Files

New: `tools/eras/drafts/{make_draft_class.py, nav_draft.sh, make_draft_sheet.py}`,
`data/eras/league/drafts/1981_class.json`, `mods/eras/draft/class_1981.csv`,
`src/nfl2k5_eras_draft.c/.h`, `src/nfl2k5_eras_draft_sheets.c/.h`, this file.
Changed: `src/nfl2k5_eras.c/.h` (history, sleepers, league seed, season-aware roster / depth paths),
`src/nfl2k5_eras_launch.c`, `src/nfl2k5_eras_depth.c` (those paths), `src/nfl2k5_eras_draftshow.c/.h`
(the queue, pop), `src/nfl2k5_eras_guest_calls.h` (sub_0006E400), `config/game.cmake`.
Uncommitted as before: `src/nfl2k5_eras_menu.c` hunks (the Draft row, History / Sleepers rows, the
lookup route, the hub test switch) and `config/game.cmake` (its single `add_executable` line already
carried other uncommitted sources, `nfl2k5_eras_draftshow.c` among them; the two new draft sources
are on that same line). Not in git: the installed pack's `generated\draft\class_1981.csv`
copy (the pack builder takes `mods\eras\draft`), the sandbox leagues under `logs\draftshow`.

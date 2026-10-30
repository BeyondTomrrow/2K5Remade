# M8 results: real 1980 roster sizes, no fillers

2026-10-16. Every in-game test ran in background mode:
* `RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, a press file and
  renderer frames, `NFL2K5_PRIVATE_PACKS=1`;
* `tools\eras\sched_test.ps1` with its own exe copy;
* an `-AppData` sandbox under `logs\eras\`;
* a sandbox `NFL2K5_ROOT` at `logs\eras\m8-root`. It held junctions to `original`, `data`, `mods`,
  `assets`, `cache`, `config`, `dependencies`, `external`, `font`, `Music` and `tools`, plus a
  copied `saves\`.

At the end, the junctions were removed with `cmd /c rmdir` only. The copied `saves\` is still
there. The real `saves\UDATA` was MD5-hashed before and after: 507 files, 0 differences. The
partition images' times are unchanged (13:13). Nothing was committed.

Contact sheet: **`logs/eras/rosters45-sheet.png`** (15 labelled frames).

## Result

| | before | now |
|---|---|---|
| rows per team | 53 (all 28) | **42-45**: 1 x 42 (Seahawks), 1 x 43 (Bears), 2 x 44 (Bengals, Cowboys), 24 x 45 |
| fillers (not 1980 players of the team) | **163** | **0** |
| real 1980 players | 1,321 | 1,253 (68 left off by the 45-man rule; none of them started a 1980 game) |

**The minimum that works is 42**, the game's own minimum. The fewest real rated players on any
team is 42 (Seahawks). That team played a full Eras game and a Quick Game, and its Depth Chart
opened and saved. A test team cut to 41 (Bengals without P and C, see below) also played a full
game, but the staging refuses fewer than 42.

**No position needs a filler.** The three P fillers from a first pass were removed after test
m8b showed that a team with no P punts with its K.

## 1. What the game allows

* **Player count: team `+0x11C` (`T_ACTIVE`, u8).** The team record has 65 player pointers. The
  game's own limits, from the XBE strings and code:
  * Create a Team: `sub_0016D6F0` compares the count with 42 and shows "You must have a larger
    player roster to save your team." below it.
  * `sub_002BF990` returns `count >= 42`, a "team can play" predicate (callers `sub_002480B0`
    and `sub_0024BF80`).
  * Franchise strings: "You must keep 42 healthy players on the team or you will not be able to
    take the field", "The %s %s must have at least 42 players", and "... have a maximum of 54
    players to enter the regular season". `sub_0036ECE0` and `sub_0036F830` compare with 54.
* **Roster loader / match copy.** The match copies `count x 4` bytes of pointers
  (MATCH-LAUNCH 1.4). The slot clear `sub_000C2300` frees `count` records. Both depend only on
  the count.
* **Removing players.** The game has its own remove-from-team, `sub_000C3A90` (eax = team,
  ecx = index). It:
  * clears the player's `+0x24` bits 13-15;
  * decrements `+0x11C` and closes up the pointer list;
  * sets a special-teams role (`+0x194..+0x199`) that pointed at the removed player to 255;
  * moves the roles above that index down by one.

  Then `sub_000BFF90` (`+8 &= 0x7B`) puts the record back in the spare pool, as the slot clear
  does for every player.
* **Depth chart.** The depth chart is per player (`+0x28`). `nfl2k5_eras_depth.c` works from the
  count, so it needed no change.
* **Per position, the game needs very little** (test m8b below). With no P, the K punts. With no
  C or no SS, the CPU still lines up and plays a full game. So an injury that empties a position
  cannot break a game: m8b played with three positions empty from the start.

## 2. Staging (`src/nfl2k5_teams.c`, `src/nfl2k5_teams.h`)

`nfl2k5_teams_stage`:
1. Counts the CSV's data rows. A count outside **42 (`NFL2K5_ROSTER_MIN`) .. the template's
   count (53)** is refused: `NFL2K5_STAGE_ROSTER_CSV`, slot released.
2. Writes the rows over the template's first N players.
3. Removes the rest, last first, with `sub_000C3A90` + `sub_000BFF90`.
4. Checks that the count equals N.
5. Checks the per-position staging minimum (`k_stage_min`, so a base offense, base defense and
   kicker can line up): QB 1, HB 1, WR 2, TE 1, T 2, G 2, C 1, DE 2, DT 1, OLB 2, ILB 1, CB 2,
   FS 1, K 1. P, SS and FB are 0.
6. Runs the special-teams fix as before. Roles that the removal set to 255 are picked again.

New log line:
`stage side 0: 42 CSV rows written over the template's first 42 of 53 players; the other 11
removed (game remove-from-team, records back to the pool)`.

Other changes:
* New `nfl2k5_teams_verify_csv(side, csv)` reads the CSV again and compares every slot player
  (first and last name, position, jersey) and the count.
* **Test switches** (test only, off by default):
  * `NFL2K5_ERAS_STAGE_ALL=1`: at the first Team Select step, stages all 28 franchises into
    USER1 and then USER2, verifies each one and releases it.
  * `NFL2K5_ERAS_TEST_DROP=P,C`: after staging, removes every player at the listed positions
    (experiments only; the identity rows then no longer line up).
  * `NFL2K5_ERAS_TEST_NEXT=<team id>` (`src/nfl2k5_eras.c`): watch mode's next game becomes that
    team's first unplayed game, still CPU vs CPU.

No gen patches. Retail, Franchise and SOFTDRINK code paths are unchanged: the staging runs only
for Eras and the test triggers. `tools\build.ps1 -Game` builds clean.

## 3. Data (`build/eras/roster_membership.py`, `build/eras/make_runtime_package.py`)

Selection rule, real players only:
* `ROSTER_TARGET = 45` (the 1980 active limit), `GAME_MIN = 42`, `SLOTS = 53`.
* The order of picks:
  1. **always kept:** every player with a 1980 start, the K with the most FGA and the P with the
     most punts. A team may go past 45 this way, but none did;
  2. position minimums from real players. If no real rated player can fill one, it is recorded
     as a **shortfall**, not filled;
  3. the rest by games played within the position caps, up to 45;
  4. players over a cap, up to 45.
* **Fillers only as a last resort:**
  * a K, if no real rated K exists (`HARD_MIN = {K: 1}`; never needed);
  * rows up to 42, for a team with fewer rated players (never needed).

T0.1 (`--check`) now checks:
* 42..53 rows in membership mode;
* the staging position minimums (`STAGE_MIN`, mirrors `k_stage_min`);
* depth file rows equal roster rows;
* every filler is a stated last resort.

Output: `roster files: 28, 42..45 rows (1 x 42 1 x 43 2 x 44 24 x 45) ... staging position
minimums met ... real starters on their team where rated: 609 of 616 ... fillers: 0`.

M0 tamper tests: **15 caught, 0 missed.** The two new cases are "a team below 42 rows" (the
smallest team cut to 41 in both roster and depth file) and "a team with one T" (the other Ts
recoded G).

Files:
* `PROVENANCE.txt` regenerated. It has the new selection rule and, per team, the rows, the
  shortfalls and the players left off ("45-man roster full").
* `teams.csv`, `coaches.csv` and `schedule.csv` are byte-identical (schedule.csv is copied in,
  as it comes from `league_schedule.py`).
* The old package is kept as **`data/eras/runtime/1980.prev-20261016-144948`**.
* An intermediate package from the first pass (with 3 P fillers) was moved out of `data\` to the
  session scratchpad.

**Shortfalls (position minimums no real rated player can meet; not filled, the game copes):**

| team | shortfall |
|---|---|
| Bengals | G 2 of 3, DT 1 of 2, SS 0 of 1 (Greg Bright has no rating source) |
| Cowboys | P 0 of 1 (Danny White, the QB, punted in 1980; in game the K punts), WR 3 of 4 |
| Patriots | P 0 of 1 (Mike Hubach has no rating source), DT 1 of 2 |
| Rams | P 0 of 1 (Frank Corral was both K and P in 1980, so the K punting is historically right) |
| Bears, Buccaneers, Chargers, Colts, Saints, Seahawks | G 2 of 3 (Saints also CB 2 of 3) |
| Browns, Chiefs | DT 1 of 2 |
| Falcons, Jets | DE 2 of 3 |
| Lions | WR 3 of 4, DE 2 of 3 |
| Giants | TE 1 of 2 |
| Oilers | CB 2 of 3 |

**Fillers removed (163):** every filler listed per team in M6-rosters.md, for example Cliff Harris,
Rayfield Wright, Reggie Septien (Cowboys), John Riggins (Washington), Eddie Hare (Patriots) and
Ken Clark (Rams). The roster files and flags files now contain no FILLER rows.

## 4. In-game tests

| run | what | result |
|---|---|---|
| m8a (Quick Game, `NFL2K5_ERAS_STAGE_TEST=seahawks,bears`) | Seahawks 42 vs Bears 43, full game, CPU vs CPU | **pass**. 10-7 Bears in OT (period 8). Identity at the whistle: `42 players, 42 staged rows, 0 name mismatches` / `43 ... 0`; offline `m2_identity_check`: OK. Kicking: Herrera (K) 2 FGA, XP, 2 kickoffs; Weaver (P) 1 punt 35 yds; Thomas (K) FG 45, XP, 3 kickoffs; Parsons (P) 1 punt 42 yds. Lineup Special Teams shows the real K/P (`m8a-lineup-st.png`). No CRASH/STALL/WALK |
| m8a, `NFL2K5_ERAS_STAGE_ALL=1` | all 28 teams staged and verified on both sides | **pass**: `stage all: 28 franchises x 2 sides: 56 staged and verified with 0 mismatches, 0 failed; players 42..46 (fewest: seahawks)` |
| m8b (experiment, `NFL2K5_ERAS_TEST_DROP=P,C`) | Bengals (no SS; with P and C dropped, 41 players) vs Cowboys (no P, no C, 42) | **full game, no crash**: 13-7 Cowboys. The K **Rafael Septien punted** (1 punt, 44 yds) as well as kicking off and the XP; Breech kicked for CIN. The identity mismatches (38) come from the test's middle-row removal, as expected |
| m8c, `NFL2K5_ERAS_STAGE_ALL=1` on the **final** package | all 28 teams again | **pass**: 56 of 56 with 0 mismatches, players 42..45 |
| m8c, office, Depth Chart | Seahawks (42) through the picker into the game's own Depth Chart | **pass**. Offense, 4-3 defense and Special Teams pages are complete (`m8c-dc-*.png`); back to the picker and the office. The league file was saved (the known M6 side-renormalisation counts as a change; the order is unchanged) |
| **m8c, Eras game 1** | 1980w01 Chargers at **Seahawks (42)** | **pass**. Final 10-7 SD. Identity `42 ... 0 mismatches` / `45 ... 0`; offline OK. K: Herrera XP and 2 kickoffs; Benirschke FG 33, XP and 3 kickoffs. No punt in this game. End Of Game -> Exit: `committed ... results row written`, `return: to the office`, `1 of 224 games played`, `packages loaded again (ok)`. No CRASH/STALL/WALK |
| **m8d, Eras game 2** (same league, new process, Continue) | 1980w01 **Cowboys (44, no P)** at Washington | **pass**. Final 7-0 DAL. Identity `45 ... 0` / `44 ... 0`; offline OK. K: Septien XP and 2 kickoffs; Moseley 1 kickoff. No punt in this game (60 s quarters). Committed, back in the office, `2 of 224 games played`, `results.csv` has both rows |

**Injuries.** No injury showed in these games, and one cannot be forced without editing data.
Test m8b covers the worst case an injury could cause, a position with nobody left, at three
positions at once (P, C, SS). The game neither crashed nor stalled.

**Not tested in game:** a punt in an Eras office game (both office games had none; punts by real
P in m8a and by the K with no P in m8b); a Bengals Eras game (the no-SS case ran in m8b's Quick
Game); a user-team (non-watch) game; Franchise or Retail regressions. Those code paths were not
touched.

## 5. Coordinator's stadium question (Bears at Seahawks, `m8a-g2.png`)

* m8a was a **Quick Game** started by the M2 test trigger (`NFL2K5_ERAS_STAGE_TEST`), not an
  office launch. That trigger only stages the rosters. The era stadium is applied only by
  `nfl2k5_eras_launch` (step 6). m8a's log has no launch or stadium line, and the frame is the
  Seahawks template's own home stadium, Qwest Field. That is expected for the test trigger.
* In the real office launch of the same home team (m8c), the launch line reads `launch
  1980w01-chargers-seahawks ... stadium EDWARD JONES DOME (record 811CDD70 id 23)`.
* The frames show the dome: the roof, and the Rams end zone of that stand-in
  (`m8c-g1-dome.png`, `m8c-g1-stadium-grid.png`).
* **The stadium is applied correctly; nothing to fix.** If the M2 test trigger should also use
  era stadiums, that is a small follow-up (test-only path).

## Files

Changed:
* `src/nfl2k5_teams.c`, `src/nfl2k5_teams.h`;
* `src/nfl2k5_eras.c` (the test switch);
* `build/eras/roster_membership.py`, `build/eras/make_runtime_package.py`,
  `build/eras/m0_negative_checks.py`;
* `data/eras/league/rosters_1980.json`;
* `data/eras/runtime/1980/` (rosters, flags, depth, PROVENANCE regenerated).

Kept:
* `data/eras/runtime/1980.prev-20261016-144948/`.

Logs:
* `logs/eras/m8a*`, `m8b*`, `m8c*`, `m8d*`;
* `logs/eras/rosters45-sheet.png`.

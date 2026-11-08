# M6 results: Depth Chart, hidden game rows, 1980 head coaches

2026-10-16. Built in the order the user approved (Depth Chart first; Roster, Trades and the rest
come later). Every in-game test ran on `NFL2K5.exe --retail` in background mode
(`RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, press file,
renderer frames). The runs used `tools\eras-capture-test.ps1` with the sandbox `-AppData logs\eras\m6-home`,
`NFL2K5_PRIVATE_PACKS=1`, `NFL2K5_WALK_DIAG=1` and 10 s quarters. The real user folder was not
used. Nothing was committed.

Contact sheet: **`logs/eras/depth-sheet.png`** (18 labelled frames).

**Status:**

| part | status |
|---|---|
| 1 Depth Chart | **works.** It uses the game's own Depth Chart screen. The edit is saved per league, applied at launch, and the edited starter played. |
| 2 Rematch / VIP / Options | **works.** These rows are hidden in Eras games only, and retail is unchanged afterwards. |
| 3 1980 head coaches | **works.** Coach Match Up shows the real coach names. The portraits are still the template coaches'. |

## 1. Depth Chart

### It is the game's own screen

The office has a new row, **Depth Chart**. Its camera stop is the Gameplan / notebook stop (the
contract stop in the front-office looks), because Franchise reaches its Depth Chart through
Gameplan.

The row opens a small Eras picker:
* Team (with "(LT / RT)");
* Edit Depth Chart;
* Starting QB;
* Reset;
* the "unverified community roster" note.

The picker chooses which team to edit:
* With a Your Team set, the team is fixed to that team.
* In watch mode, LT and RT step through all 28 teams, and so does A on the Team row. The default
  is the home team of the next fixture.

The picker is the only screen I built myself. **Edit Depth Chart** opens the game's own screens:
* `0x532D14` Depth Chart (Offense / Defense / 3-4 Defense / Special Teams);
* `0x533088` dc_overview;
* `0x533230` dc_playercard.

These are the screens that Roster Manager -> Depth Chart and Franchise's Gameplan -> Depth
Chart both push. Both paths reach `0x532D14`, so it is one UI.

How the screens are driven:
* The retail entry points `sub_00243E80` (Roster Manager) and `sub_00243F10` (Franchise) each
  set the screen's team in `[0xC1749C]` and push `0x532D14`. Eras does the same.
* `[0xC174B4]` is set to 0. When it is non-zero, the screen's msg 2 (`sub_002440F0`) re-sorts
  Franchise's user team.
* The previous values of `[0xC1749C]`, `[0xC174B4]` and `[0xC174B8]` are saved and put back
  afterwards.

No Franchise function or save is touched.

### How an edit flows

1. **Stage.** `nfl2k5_eras_depth_edit` stages the team into USER1, or USER2 if USER1 is not free,
   with the M2 `nfl2k5_teams_stage`. A Create-a-Team team in a slot is refused, as in M4. Editing
   is also refused while a league game is launched, and a launch is refused while an editor is
   open.
2. **Apply the depth chart.** The team's depth chart is written into the players' rank and side
   fields (`+0x28`, bits 10-12 and 13-15):
   * the league's own file `<league>\depth\<id>.csv` if it exists;
   * otherwise the package seed `data\eras\runtime\1980\depth\<id>.csv`.
3. **Edit.** The user edits in the native screens. Back out of them to return to the picker.
4. **Read back.** At the picker's msg 3, `nfl2k5_eras_depth_edit_end` reads the fields back.
   * If any rank, side or special-teams role (`+0x194..+0x199`) changed, it writes
     `<league>\depth\<id>.csv`.
   * It then releases the slot and repoints any selected-team word that pointed at the slot. That
     repointing is the M4 code, factored out as `nfl2k5_eras_repoint_released`.
   * The FSM-pop hook does the same if the office, hub or Main Menu comes back on top first.
5. **Launch.** `nfl2k5_eras_launch` calls `nfl2k5_eras_depth_apply` for both sides right after
   staging.

What is written where:
* `data\eras\runtime` is never written at run time.
* **Reset** moves the league file aside to `depth\<id>.csv.old<n>`. It never deletes it.

The league file format is the seed format plus one extra line:

```
row,first,last,pos,number,rank,side
special,<holder>,<kr1>,<kr2>,<k>,<ls>,<pr>
```

Rows are matched by row index plus name and position. If the order changed, they are matched by
name, number and position.

### Seed from the community roster's depth chart

`build/eras/make_runtime_package.py` now also writes `depth/<id>.csv` for all 28 teams. It uses
the depth fields stored in the community save's player records:
* the rank chain, and for the paired positions T, G, DE, DT, OLB, ILB, CB and WR also the side
  chain;
* each chain is ordered by its stored value, with ties broken by list order, and renumbered
  0, 1, 2 .. (capped at 7).

Nothing is invented. A stored order is only renumbered.

`PROVENANCE.txt` has a new "DEPTH CHART SEED" section. It lists every starter that differs from
the list-order rule M2 used: **67 starters in 6 teams**.

**Note for the user: the stored depth puts some obvious backups first.** These are the community
save's own depth charts, and they are applied as asked. The user can change any of them in the
Depth Chart. Examples:
* 49ers: QB L. Barisich over J. Montana;
* Raiders: QB D. Humm over K. Stabler, and P D. Francis over R. Guy;
* Bills: QB D. Manucci over J. Ferguson;
* Giants: QB R. Dean over P. Simms.

Other files:
* The regeneration changed only `PROVENANCE.txt`, and added `depth/` and `coaches.csv`.
* `teams.csv`, `schedule.csv` and the rosters are byte-identical (checked with `diff -r` against a
  backup).
* T0.1 (`--check`) still passes.
* The stadiums JSON hash in PROVENANCE changed because another agent edited that file. The
  generated `teams.csv` is still identical.

### Tests (run m6a, then m6b in a new process)

| test | result | evidence (`logs/eras/`) |
|---|---|---|
| Office row, 8 rows fit | pass | `m6a-office.png` |
| Picker: watch mode, default team, LT / RT | pass | `m6a-picker.png` (Vikings, the seed QB T. Kramer), `m6a-picker-rt.png`; log `depth chart picker: buccaneers` / `vikings` |
| Native screen on the staged 1980 team | pass | `m6a-dc1.png`, `m6a-dc-off.png` (Kramer, Dils, Shaw ...), `m6a-dc-card.png`; log `staged into USER1 (slot 811D5188, 53 players); the game's Depth Chart 00532D14 opens` |
| Edit and save | pass | QB swap `m6a-dc-swap.png`. Log: `5 players and 0 special-teams roles changed; saved to ...\league1\depth\vikings.csv: ... QB Kramer 0/2->1/2; QB Dils 1/0->0/0 ...`; `after the edit: starters QB Dils #12`; `[TEAMS] released side 0 ... players 0`. Picker shows "Starting QB: S. Dils" (`m6a-picker2.png`) |
| **Regression: one full Eras game** | **pass** | Falcons at Vikings. Launch log: `depth chart vikings (side 0): the league's ...vikings.csv, 53 of 53 players set`, `starters QB Dils #12`; the Falcons from the seed. Final: `[ERAS] final game=1980w01-falcons-vikings home=6 away=0 ... period=33` and **`home QB #12 Steve Dils 5/10 107 yds TD=1 INT=2`**. In-game frame `m6a-eog0.png`: "PASSING TODAY 1. S. Dils (MIN)". End Of Game -> Exit: `match exit: committed`, `return: to the office`, `clean-up (office on top) ... 1 of 224 games played`, `office: packages loaded again after the match (ok)`. `results.csv` has the row; the office shows the next fixture (`m6a-office3.png`) |
| Persistence across a restart | pass | m6b: Continue -> Depth Chart -> Vikings, "Starting QB: S. Dils" (`m6b-pickv.png`) |
| Reset | pass | `depth chart vikings: the league's depth chart moved aside to ...vikings.csv.old1`; picker back to T. Kramer (`m6b-reset.png`) |
| Retail Depth Chart unchanged | pass | Features -> Roster Manager -> Depth Charts: the retail team list and the retail 49ers chart (`m6b-rm-dc.png`, `m6b-rm-off.png`) |

There were no `[CRASH]` lines in either run, and no `STALL` or `[WALK]` lines in m6a.

Franchise was not run in these tests. Its Depth Chart path (`sub_00243F10`) and Franchise's data
are not touched by this code: only the three screen globals are written while an Eras editor is
open, and they are restored afterwards.

## 2. Rematch, VIP and Options hidden in Eras games

`nfl2k5_eras_menu_game_rows(1)` runs at launch and `(0)` at clean-up. It swaps the row-update
function (`+0x2C`) of these retail rows for an Eras one that sets the row's hide flag:
* End Of Game `0x4E935C` row 0 **Rematch** (retail update `sub_0006EB30`);
* End Of Game row 5 **Save VIP and Exit** (`sub_0006EB80`);
* the pause menu's Quit sub-menu `0x4E8D70` row 3 **Rematch** (`sub_0006EB30`);
* Coach Match Up `0x585474` row 4 **Options** (Team Options; `sub_00367240`);
* Coach Match Up row 5 **VIP** (`sub_00367260`).

A row is swapped only if it still holds the retail function, and it is restored after the game.
Retail games therefore see the retail rows. No gen patch was needed.

Evidence:
* The log shows `game menus: 5 rows hidden for the Eras game ...` and later `5 rows back to retail`.
* Coach Match Up has no Options or VIP row (`m6a-cmu.png`, `m6b-cmu.png`).
* End Of Game shows Replay / GameCast Stats / Crib Tally / Exit (`m6a-eog1.png`). **A on End Of
  Game is now Replay, not Rematch.**
* Retail Quick Game Coach Match Up afterwards shows Options and VIP again (`m6a-retail-cmu.png`).

Not hidden:
* The in-game pause menu's **Options** (`0x5038C8`) is still shown. It also holds Choose Side and
  Camera, which a user-controlled game needs. Changes made there are rolled back by the M4
  settings snapshot after the game, as before.
* **Simulate To End** in the Quit sub-menu was left as is (not checked).

The pause-menu Quit -> Rematch row is swapped according to the log, but no frame of it was taken.

## 3. 1980 head coaches on Coach Match Up

Source: the Pro-Football-Reference 1980 coaches page. The live site returned 403, so the Wayback
copy was used:
`https://web.archive.org/web/20241204112639/https://www.pro-football-reference.com/years/1980/coaches.htm`.
* It has 29 rows, including the Saints change: Dick Nolan coached games 1-12 and Dick Stanfel
  games 13-16.
* The parsed data is in `data/eras/league/head_coaches_1980.json`, with the URL, fetch date and
  page sha256.
* The generator writes `data/eras/runtime/1980/coaches.csv` and a "HEAD COACHES" section in
  `PROVENANCE.txt`.

How it works:
* Team `+0x14C` points at the coach record. Coach Match Up's msg 1 (`sub_00366E20`) reads it.
* A staged team's pointer is the template's coach, which is a retail record, for example Mike
  Mularkey for the Bills template.
* `nfl2k5_eras_coach_apply` gives each side **its own 0xA8-byte copy** of that record:
  * the name is the coach for the team's game number in the schedule;
  * the biography lines are empty;
  * the career numbers are 0, because they were not sourced.
* The copy keeps the template's other fields, including the play-calling tendencies.
* The slot's pointer is restored before the slot is released.
* Retail coach records are never written.

Evidence:
* m6b: `coach bills (side 0, game 1): Chuck Knox (the template's was Mike Mularkey, record 811CFE08; ours 01B86260)`
  and `coach dolphins ... Don Shula (the template's was Dave Wannstedt ...)`.
* Coach Match Up shows **D. Shula** vs **C. Knox** (`m6b-cmu.png`).
* In m6a, before a fix, the check refused every record (`m6a-cmu.png` still shows the template
  coaches). The cause was an address check that rejected guest addresses at `0x811Dxxxx`. It was
  fixed and rebuilt before m6b.

## Defects / open items

* **Coach portraits** are the template coach's face (Wannstedt's photo above "D. Shula"). The
  record shows 0-0-0 and 0 years, because the career numbers are cleared.
* **Seeded depth charts put some backups first** (see section 1). This is the data's own order,
  applied as asked.
* **Unedited fields can still count as an edit.** The native screen's compactor renormalises the
  side value of single-player positions (K, P, a lone TE). So opening and closing the screen can
  count as a change and write the league file. It is harmless: the order is unchanged.
* **Special-teams roles:** the staging fix (`special_teams` in `src/nfl2k5_teams.c`) still picks
  returners by list order, before the depth file is applied. A league file's `special` line
  overrides it. The seed has no `special` line.
* **Picker:** stepping teams uses LT / RT, or A on the Team row. Left / Right do nothing, because
  link lists have no value rows.
* **Long overtime:** with 10 s quarters, the 0-0 regression game went to period 33 (about 35
  minutes) before it ended 6-0.
* **Not run:**
  * a user-team league (picker locked to Your Team);
  * a Create-a-Team refusal;
  * the Defense and Special Teams pages;
  * a front-office (1990s / 2020s) look;
  * Franchise itself.

## Files

New:
* `src/nfl2k5_eras_depth.c`, `src/nfl2k5_eras_depth.h`
* `data/eras/league/head_coaches_1980.json`
* `data/eras/runtime/1980/depth/*.csv`, `coaches.csv` (generated)

Changed:
* `src/nfl2k5_eras_menu.c`: the office row, the picker, and the hidden-row hook.
* `src/nfl2k5_eras_launch.c`: depth and coach at launch, the coach restore, the menu rows on and
  off, and `nfl2k5_eras_repoint_released`.
* `config/game.cmake`
* `build/eras/make_runtime_package.py`: the depth seed, coaches and provenance.
* `data/eras/runtime/1980/PROVENANCE.txt` (regenerated)

No gen patches.

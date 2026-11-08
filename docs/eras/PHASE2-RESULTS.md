# My NFL Eras phase 2: slice results

2026-10-16. This summarises `docs/eras/results/M0.md` .. `M4.md` and the reviews of each
milestone. The plan is `docs/eras/PHASE2-PLAN.md`.

All in-game tests ran on `NFL2K5.exe --retail` in background mode, with these settings:
* `RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`, `NFL2K5_RENDER_SCALE=1`, `NFL2K5_NO_HOST_PAD=always`;
* sandbox user folders (`-AppData`);
* short quarters.

Evidence paths are under `logs/eras/` unless stated otherwise. Nothing was committed to git.

## Status at a glance

| milestone | status | in one line |
|---|---|---|
| M0 1980 runtime package | **pass** (offline) | 28 teams, 53-man rosters, the real 1980 schedule, lineage checked |
| M1 result capture | **pass** | one capture at the final whistle; a quit is "abandoned"; a rematch is ignored |
| M2 team staging | **pass** | 1980 teams staged into USER1/USER2 and released afterwards |
| M3 menus and league save | **partial** | the plain-menu shell works; the Franchise-office hub is **not built** |
| M4 launch / commit / standings | **pass** | Play Next Game -> game -> back to the League screen with standings updated (M4b) |
| M5 slice acceptance | T5.1-T5.5 pass; T5.6 with a test switch only; T5.7 not run | |

The slice works end to end on the clean game. It is **not complete as the plan now defines
it**, because M3 requires the office hub (ERAS-OFFICE-DESIGN.md section 3, tests T3.0-T3.12).
The implementer proposed splitting the office into a separate M3-office milestone. That
proposal is marked **PROPOSED, NOT RATIFIED** in PHASE2-PLAN.md, and the user or the
orchestrator still has to accept it.

## What is playable now

On `NFL2K5.exe --retail` (the clean launch; SOFTDRINK and other packs were not tested):

1. **Main Menu -> My NFL Eras.** It is the 4th row, after The Crib; the menu now has 8 rows.
   Features, Options, Xbox Live and Extras still open their own screens (M3 round 2).
2. **New League** creates a 1980 league in `%LOCALAPPDATA%\NFL2K5-PC\eras\league1\`.
   **Continue** reloads it after a restart.
3. **League screen:**
   * Week / next-fixture title;
   * Your Team: choose one team or watch mode; locked after the first game;
   * Schedule;
   * NFC and AFC standings (W-L-T, PF, PA; the order is labelled unofficial);
   * the "unverified community roster" label.
4. **Play Next Game.** It stages both 1980 teams (M0 rosters, names and numbers) into the
   USER1/USER2 slots and picks the era home stadium and uniforms. It then goes to Coach
   Match Up and plays a normal game:
   * CPU vs CPU, or your team under your control.
5. **At the final whistle** the score, team box and player boxes are captured once.
   * Exit returns to the **League screen** (M4b).
   * The result is committed and the standings are updated.
   * Both spare slots are emptied and the game settings are restored.
6. **Quitting mid-game** leaves the fixture unplayed. A crash or exit during a game is
   detected on the next Continue, and the fixture also stays unplayed.

At 10 games played, a sandbox league (`logs/eras/m4r-home`) has 10 rows in `results.csv`, and its
standings match.

## What passed, with evidence

### M0 (offline, no build, no game)
* **T0.1**, the runtime package: `logs/eras-m0-T0.1.log` "T0.1 1980 runtime package: OK".
  * 28 teams, divisions AFC 5/4/5 and NFC 5/5/4.
  * 28 rosters of 53 rows by 39 fields.
  * The templates equal plan section 2.4.
* **T0.2**, the schedule: `logs/eras-m0-T0.2.log`.
  * 224 games, 16 per team.
  * Each team's W-L-T recomputed from the real scores equals `nfl_seasons_1970_1980.json`
    for all 28 teams.
  * Cross-checked against the NFL Record & Fact Book, PFR standings, a 2019 PFR capture,
    and Wikipedia (448/448 team-games agree).
* **D3, lineage:** `logs/eras-m0-D3.log`.
  * ARZ/IND/STL/TEN match their 1980 franchise on 31-38 of 53 names.
  * They match the other rosters on 0-1 names.
* **Negative checks:** `logs/eras-m0-negative.log`, 11 deliberate tamperings caught, 0 missed.
* **Reproducibility:** two regenerations gave identical sha256 values.

### M1 (in game)
* **T1.1 normal game:** `t11.stderr.log`.
  * One final and one match exit per game.
  * The GameCast Stats frames (`t11-gc*.png`, `t11-g4*.png`) match `eras-capture-1.json` and
    `eras-capture-4.json` for team totals, QB, RB and WR lines.
* **T1.2 overtime:** a field goal in the 3rd OT period (period 7), one final, and the 3
  points in the OT slot (`t11-gc2.png`).
* **T1.3 quit:** "match exit: abandoned" and no final (`t11-quit1.png`, `t11-quit2.png`).
* **T1.4 rematch:** "final ignored (not armed)", and the first game's file hash is unchanged.

### M2 (in game)
* **T2.1 staging:** Colts vs Patriots with the M0 names and numbers on Team Select, Coach
  Match Up and the lineups (`t21-*.png`). The M0 kicker and punter did the kicking (box JSON).
* **T2.2 identity:** 53/53 players per side and 0 name mismatches at the whistle, in game
  and offline (`t22-offline.log`).
* **T2.3 release:** both slots go back to empty created slots (ident 90/91, 0 players).
  Quick Game Team Select then shows retail teams (`t21-t23ts.png`).

### M3 (in game, plain-menu shell only)
* **T3.1:** Eras sits after The Crib in an 8-row Main Menu (log
  `"My NFL Eras" in the Main Menu after The Crib (8 rows, flags 02)`).
  * Features, Extras and Xbox Live each open their own screen (`m3k-features.png`,
    `m3k-extras.png`, `m3k-xboxlive.png`).
  * The descriptor dump matches the pre-insert dump (`m3k-desc-515660.bin`).
* **T3.2-T3.4, confirm, bad team, no folder:** pass on the 01:48 binary (m3h/m3i/m3j; see M3.md).
* **Label rows:** they give feedback ("label row ... pressed (no action)").

### M4 / M5 (in game)
* **T4.1 launch:** Falcons at Vikings, staged, armed, one final, 53/53 identity, one results row,
  a box JSON (`m4a-cmu.png`, `m4a-pregame*.png`, `m4a-eog1.png`).
* **T4.2 return and standings:**
  * Exit returns to the League screen (`m4a-ret1.png`).
  * Standings are updated (`m4a-nfc*.png`, `m4a-afc.png`).
  * Game Options are unchanged (`m4a-gameopt0/1.png`).
  * Quick Game Team Select shows retail teams (`m4a-qts2.png`).
* **T4.3 settings snapshot:** a 7-minute quarter setting was used in the Eras game and kept
  afterwards (`m4b-*.png`).
* **T5.1 back to back:** pairs in m4a, m4c and m4f, plus 4 games in one process in m4r1
  (`m4r1-e1ret.png` .. `m4r1-e4-ret.png`).
* **T5.2 restart / Continue:** m4b.
  * Also the crash path in m4f: "was not cleaned up in the last session ... the fixture
    stays unplayed".
* **T5.3 quit = abandoned:** the fixture stays unplayed and `results.csv` is unchanged (`m4b-q6.png`).
* **T5.4 user-controlled:** the Falcons were under user control (`m4e-cmu.png`, `m4e-k1/k2.png`).
  Your Team locks after the first game (`m4e-ytlock.png`).
* **T5.5 overtime:** Giants at Cardinals ended in period 11, `overtime: true`, with one row (`m4f-otnfc.png`).
* **Review fix, dangling selection (m4r1):**
  * Setup: the 1998 Vikings in USER1 for a Quick Game, then an Eras game.
  * The selected-team word is repointed to a club
    (`[00E5FE68] was 811D5188 ... -> 811D1308`).
  * Quick Game Team Select is healthy afterwards (`m4r1-qts.png`, `m4r1-qts-user1.png`).
* **Settings rollback audit (m4r1):** over 4 games, only the Start Game weather/time
  outputs and the launch's own uniform slots differed. No user option or Crib counter changed.

## Failed, partial or not run

* **M3 office hub (T3.0-T3.12): not built.** This is the main gap. M4's return and clean-up
  target the plain League screen (`nfl2k5_eras_menu_header(1)`).
* **Halftime hang: root cause not proven.** In m4c the game thread spun in `sub_000561F0` at
  the halftime show once (the 3rd Eras game in that process; about 1 in 12 Eras halftimes
  overall).
  * It did not recur in 4 back-to-back Eras games or in 7 retail control halftimes with
    `NFL2K5_WALK_DIAG=1`.
  * m4c was the only run with mass `ERROR_SHARING_VIOLATION` on
    `saves\Cache\tr000000.iff`: 82,774 lines from line 6374. Disc-cache failures also
    appear from boot.
  * Whether another process actually held the files was not checked; no process list was
    taken. An environmental cause is likely but unproven.
* **T5.6 Create-a-Team refusal:** tested only with a test switch (`NFL2K5_ERAS_TEST_USER_TEAM=1`).
  * The shipped text was re-shot (`m4r4-refused.png`).
  * It was never tested with a real created team, because that would need a write to the
    user's shared roster save.
* **Not run:**
  * T5.7 (a mod-pack run);
  * the M1 interp hooks under a pack;
  * the M3 pack-ignores-env-override path;
  * `tools/drive-menu.ps1 -Retail` (parse-checked only);
  * pause-menu Rematch during an Eras game.
* **M3 review minors** that were never re-run on the final M3 binary:
  * T3.3 Your Team change and the full T3.4 path, which should include Your Team and
    Schedule, and Options -> back without entering Game Options;
  * the msg 1 / msg 5 wording in M3.md.

M4's later runs did exercise Your Team (T5.4) and the League screens on the 06:02 binary.

## Known defects

* **Halftime hang** (above). The league survives it: the fixture stays unplayed on the next Continue.
* **Rematch, VIP and Options are not hidden** in Eras games.
  * A rematch after the final is ignored, so the first result stands.
  * A **pause-menu Rematch** keeps the capture armed, so the restarted game would be
    recorded for the fixture. Not tested.
* **Coach Match Up shows the templates' coaches** (for example J. Mora Jr. for the 1980
  Falcons). Playbook labels and some art also come from the templates.
* **Options changed in Coach Match Up during an Eras game are reverted afterwards**
  (snapshot restore, by design).
* **The phone icon** is drawn beside "My NFL Eras" when the Main Menu is scrolled to
  Extras while a Crib call is pending (cosmetic).
* **Fixed in code after the M3 review (code read only; whether they were re-tested is in M3.md):**
  * New League over an existing league now asks "Replace league? Press A again". The old
    folder is moved aside, never deleted.
  * An unknown `user_team` in `league.ini` is refused with a log line.
  * The existence check no longer creates the `eras\league1\` folder (`league_path_peek`).
* **Stale name pointers:** players released into the spare pool keep pointers into the
  per-side name arena, which the next staging rewrites. This is latent and was not seen.
* **Data:**
  * Unverified community rosters, for example Stabler still a Raider and Pastorini an Oiler.
  * No birth dates, so in-game ages mean nothing.
  * 59 ratings clamped to 99.
  * Depth follows list order: in 49 position groups this differs from the roster's
    stored depth, for example SF QB.
  * Some stadiums are stand-ins (Lambeau for Metropolitan Stadium).
* **Small code nits:**
  * `NFL2K5_WALK_DIAG=0` also turns the diagnostic on (`src/main.c:3292` checks only
    that the variable exists).
  * The walk-diagnostic counter is not re-entrancy safe if `sub_00056100` can re-enter
    `sub_000561F0` (not checked).

## Changes outside the plan

* `NFL2K5_NO_HOST_PAD=always` (`src/nfl2k5_input_hle.c`). It was added because a real
  controller reached a hidden test game. The default is unchanged.
* The special-teams role fix in staging (`NFL2K5_ERAS_ST_FIX`, on by default). The
  templates' K/P indices pointed at the wrong players.
* Gen patches:
  * `ERAS_GAME_FINAL` (recomp_0004.c);
  * `ERAS_MATCH_EXIT` and `ERAS_RETURN` (recomp_0002.c / sub_0034C1F0);
  * `WALK_DIAG_ENTER/STEP` (recomp_0002.c; off unless the env var is set).
* The tested build tree also contains earlier uncommitted work (oldschool/NCAA gen patches,
  SNF hooks). The evidence comes from that combined tree.

## Next steps

1. **Decide M3-office:** ratify the split and schedule it, or build the office inside M3.
   Then build it:
   * the pack builder;
   * `erasoffice.iff` / `erasskin.iff`;
   * `src/nfl2k5_eras_office.c`;
   * tests T3.0-T3.12.
   
   Then move M4's return and clean-up target from the League screen to the office screen.
2. **Halftime hang:**
   * Keep `NFL2K5_WALK_DIAG=1` on every Eras soak run.
   * Log the process list at launch.
   * Fix the `=0` check.
   * Run a longer soak (10+ Eras games in one process).
3. **Hide or neutralise Rematch** (End Of Game and pause menu) and VIP save in Eras games.
   Alternatively, test that a pause-menu Rematch is captured correctly.
4. **Test T5.6 with a real Create-a-Team team.** This needs the user's OK to use a save
   copy that holds one.
5. **Run the pack path (T5.7)** under SOFTDRINK once a pack run is allowed.
6. **Finish the M3 minors:** re-run T3.3/T3.4 on the current binary, including the New
   League confirm and the unknown-`user_team` refusal.
7. **Data follow-ups:** era coaches, depth from the stored depth fields, and the remaining
   seasons beyond 1980.

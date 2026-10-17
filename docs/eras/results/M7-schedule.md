# M7 results: Franchise's own Schedule screen for My NFL Eras, and Standings

2026-10-16. Built from `docs/eras/research/FRANCHISE-SCREEN-COPY.md`, approach (c). All in-game tests
ran on `NFL2K5.exe --retail` (an exe copy, `build\Release\NFL2K5_sched.exe`) in background mode
(`RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, a press file,
renderer frames), `NFL2K5_PRIVATE_PACKS=1`, and `-AppData` sandboxes under `logs\eras\`
(`m7-home`, `m7c-home`, `m7d-home`). Driver: `tools\eras\sched_test.ps1`. Nothing was committed.

Contact sheet: **`logs/eras/schedule-sheet.png`**. Each row puts an Eras screen on the left and the
matching Franchise screen on the right.

## Status

| part | status |
|---|---|
| Schedule (Team and Weekly pages) | **works.** It is the retail screen: the retail handler, sheet engine, pages, columns, getters, helmets and page draw, filled with Eras data |
| Results after a played Eras game | **works.** The game shows as WON/LOST with `BUF: 3 / MIA: 0`, the winner's score is highlighted, and records are updated (1-0 / 0-1) |
| Standings | **works, built on the retail sheet engine** (see below). It is not the retail Team Standings layout |
| No leak into Franchise | **pass.** Byte-identical block after 22 cycles (empty block) and after 21 cycles (a live Franchise block); `[0xAA26B0]` stays 0 |
| Franchise regression | **pass.** New franchise in a sandbox root: Schedule (both pages), a played and processed week 1, Team Standings, then Franchise again after the Eras cycles |
| Gen patches | none |

## What was built

* **`src/nfl2k5_eras_sheets.c/.h`** (new). The served functions are at `0xFEE80000..`, routed by
  `nfl2k5_eras_menu_lookup`.
  * **Schedule descriptor.** A copy of `0x522828` (handler `0xF3DA0`, flags `0x95`) with its own
    event table:
    * msgs 1, 2, 3, 4, 5 and 6 are Eras code. msg 5 calls the retail `sub_003275C0` (page
      leave) and then restores the block. msg 6 calls the retail `sub_003276C0` (LB/RB page
      switch with its sound), which dispatches to the Eras page frame.
    * msg 8 is the retail `sub_00327690`.
  * **Page table.** `{Eras init, retail leave sub_0024C440, Eras frame, retail draw sub_0024C480}`.
    Page 0 is Team and page 1 is Weekly, in the retail order.
  * **Page inits.** Ports of `sub_0024B740` modes 1 and 0. They use the Eras team instead of
    `sub_0013EC80` (`[0xE3C0A0]` is never read).
  * **Page frame.** The safe part of `sub_0024D1B0`:
    * help slots: 0xF ("for Weekly / Team Schedule"); 9, 0xB and 0xD are off;
    * the input filter at `+0x590`;
    * A, up/down, LT/RT tabs and Back;
    * the team colours (`sub_00327980`) and the sheet update (`sub_00174C70`).

    **START, Y, SportsCenter, `sub_0024CD20`, `sub_002486F0` and `sub_0024BF80` are never
    called.**
  * **Sheet definitions.** Copies of `0x537958` (team) and `0x537858` (weekly). Only the A
    activation (`+0xC4` / `+0xDC`) points elsewhere, at an Eras no-op. `sub_000C67E0` is never
    reached.
  * **Season block swap.** At msg 1 and msg 3, `0xE576A0..0xE5FC00` (0x8560 bytes) and
    `[0xA84494]` are snapshotted. The block is then filled in Franchise layout:
    * mode 2, stage 8, 28 teams, 16 weeks, current week = the next fixture's week, year field
      -24;
    * the grid `{type, home, away, month, day, 0, 13, 0}`, both flags, type 7 terminators;
    * played games as type 3 with the home score in byte 0 and the away score in byte 5;
    * divisions at `0xE576D4`, the user mark at `0xE5775C`, the 10 ones at `0xE5A240`.

    Both are restored byte for byte at msg 5 and msg 2. The restore is hash-checked (FNV over
    the block and `[0xA84494]`) and logged to `eras.log`. The UI statics `0xC95DE8..0xC95E20` are
    saved at msg 1 and put back at msg 2.
  * **28 Eras team records.**
    * Each is a copy of the club that owns the historic template's asset code. The code is read
      from the historic entry `[root+0x5C] + 16*entry + 4`.
    * `+0x138` / `+0x104` / `+0x108` point at the 1980 city, nickname and abbreviation.
    * `+0x192` is the template's helmet variant, so throwback mini helmets show where `mini.iff`
      has them. Seen: the Buccaneers creamsicle, the 1979 LA Rams and the red Falcons. The others
      fall back to `_0`.
    * The records are rebuilt at every swap. Arena records are never touched.
  * **`mini.iff`.** It is queued under ctx `FRMINI` in an Eras slot (`sub_000441B0` as
    `sub_00372500` does) and unloaded at msg 5 and msg 2. If `[0xAA240C]` is set (Coach's Desk
    packages loaded), or `mini_a<code>_0` is already found, the resident copy is used and never
    unloaded.
* **`src/nfl2k5_eras_menu.c`**
  * The office's **Schedule** row pushes the copy. `NFL2K5_ERAS_SCHEDULE=list` keeps the old list.
  * **AFC / NFC Standings** push the Eras Standings. `NFL2K5_ERAS_STANDINGS=list` keeps the old
    lists.
  * The install calls `nfl2k5_eras_sheets_install()`. If the install fails, both rows use the
    lists.
* `config/game.cmake`: adds the new source file.
* **`tools/eras/sched_test.ps1`** (new, from `office80_test.ps1`).
  * It runs its own exe copy, press file and frames, and stops only its own PID.
  * It sets **`NFL2K5_ROOT` to a sandbox root** (default `logs\eras\m7-root`). That root holds
    junctions to `original`, `data`, `mods`, `Music`, `font` ... and a **copied `saves\`**. Game saves
    live in `<root>\saves`, not under `LOCALAPPDATA`.
* Test switches (off by default):
  * `NFL2K5_ERAS_SHEET_DUMP=<prefix>` writes `<prefix>-pre.bin` at the first swap and
    `<prefix>-post.bin` after every restore. Each file is the block plus `[A84494]` and `[AA26B0]`.
  * `NFL2K5_ERAS_SHEET_MODE=1` uses mode 1 (the Tournament mapping) if mode 2 ever misbehaves.
    It was not needed.

## Standings: why the sheet engine and not the retail screen

The retail Team Standings (`0x54CEA0`) is a custom-drawn screen, not a sheet. Its draw
`sub_003554B0` uses a fixed 2 x 2 layout of 4-team division boxes per conference (the
`0xAE92C0..` / `0xAE93C0..` tables). In 1980 each conference has three divisions of 5 / 4 / 5
teams, so the five-team divisions do not fit.

Following the brief, Standings uses the same descriptor copy and the same retail sheet engine:

* an Eras sheet: a copy of the weekly sheet with Eras tab value, count, next and previous;
* an Eras page with row count and seven Eras text columns (TEAM "City / Nickname", W, L, T, PCT,
  PF, PA);
* the **retail helmet column `0x536E00`**, unchanged. It draws the away helmet of (tab value,
  row), so the standings order is written into the unused grid rows 16..21 of the filled block.
  Those are type 0 cells, so they show no score and do not count toward W/L/T.

Page 0 has six division tabs (LT/RT; AFC opens on the user's division, else AFC East). Page 1 has
the conference tabs (LB/RB). The page draw uses the retail pieces:

* the `sub_0024C480`-style record line;
* the title and right-hand text through `sub_003273C0`;
* the banner `STANDINGS`;
* the sheet draw `sub_00176380`.

Teams are ordered by percentage (a tie counts as half), then W, L, point difference, points scored
and name. It looks like the Franchise Schedule, not like the Franchise Team Standings (sheet,
rows 5 and 6).

## Tests

| test | result | evidence (`logs/eras/`) |
|---|---|---|
| Schedule opens from the office, Team page | pass: Bills (watch mode: the next fixture's home team), OPPONENT/SCORE, "At New Orleans Saints", helmets, `SEP 7, 1980`, "SEASON", "BUF Record: (0-0-0)", "Press LB RB for Weekly Schedule" | `m7a-sched1.png` |
| Weekly page, LT/RT weeks, cursor | pass: week 1 has 14 games; the already-played Falcons at Vikings shows 0 / 6; week 2 is `SEP 14, 1980` | `m7a-week1.png`, `m7a-week2d.png` |
| After a played Eras game | pass: `final game=1980w01-dolphins-bills home=3 away=0` -> committed -> office. Weekly: Miami 0 (0-1), Buffalo 3 (1-0). Team: WON, BUF: 3 / MIA: 0, BUF Record (1-0-0) | `m7b-after-week.png`, `m7b-tp-bills.png` |
| Standings update | pass: AFC East Bills 1-0 1.000 3-0, Dolphins 0-1 0-3; NFC Central Vikings 1-0 6-0 | `m7b-after-stand.png`, `m7a-stand-nfcc.png`, `m7a-conf2d.png` |
| START / Y / R3 / A on the Eras Schedule | pass: no dialog, no push (`[FSMOP]` unchanged); A logs "no action" | `m7b-keys.png`, `m7b.stderr.log` |
| **No-leak, empty block** (no Franchise in RAM) | **pass**: 22 open/close cycles with page switches and LT/RT; `m7-cycles-pre.bin` = `m7-cycles-post.bin` (SHA-256 `7166fc51...`); 22 restores, all `hash F22A99C1 = before` | `m7a-block-*.bin`, `m7-home\...\eras.log` |
| **No-leak, live Franchise block** (new franchise, then quit to the Main Menu: mode 0, stage 7, 1,095 non-zero bytes) | **pass**: 20 Schedule cycles plus a Standings cycle (21 restores), `m7d-block-pre.bin` = `m7d-block-post.bin` (SHA-256 `78684a9c...`), every restore `8CE2B569 = before`. In m7c (after the played franchise week) 3 restores `1032E5FC = before` | `m7d-block-*.bin`, `m7d-home\...\eras.log`, `m7c.stderr.log` |
| `[0xAA26B0]` | **0** in every swap and restore line, and in the dump tails | logs above |
| **Franchise regression** (m7c, sandbox root) | **pass**: new franchise (Cowboys, preseason off) -> desk -> Schedule Team and Weekly retail. Week 1 played (Cowboys at Vikings, CPU) and the week processed: Seattle 35-28 ... Dallas 0-0 tie (0-0-1). ESPN.com -> Team Standings NFC with week 1 records. Quit without saving | `m7c-fr-team.png`, `m7c-fr-week.png`, `m7c-e1.png`, `m7c-fr-stand.png` |
| Franchise after the Eras cycles (m7d) | pass: a new franchise's desk Schedule is retail (PRE-SEASON, Jul 29 2004) | `m7d-d-week.png` |
| Crashes | none (`[CRASH]` 0 in m7a-m7d) | |

## Defects / open items

* **Two early runs (m7a, m7b) were not in the sandbox root.** A PowerShell variable clash
  (`$Root` is `$root`) made `NFL2K5_ROOT` the real root. Those runs played an Eras game only;
  nothing saves in that path.
  * Checked: `saves\UDATA` has nothing newer than 12:00.
  * `saves\Partition*.img`, `Soundtracks\ST.DB` and `Cache` were rewritten at 13:13, which is
    what every boot does (other agents' copies also boot from the real root).
  * The script was fixed (`-SandboxRoot`), and m7c / m7d write only to `logs\eras\m7-root\saves`.
* **Standings is not the retail Team Standings look.** It uses the sheet engine (above), with no
  clinch marks.
* **A on a game does nothing.** "Play this game" (plan phase 2) was not built.
* **Watch mode team.** The Team page and the "Record" line follow the next fixture's home team.
  LT/RT cycles all 28 teams.
* **LT/RT icons:** the real-root profile shows "LT/RT" icons (m7a, m7b). The sandbox profile shows
  "L/R" as retail, so this is a profile setting, not the copy.
* **`mini.iff` load is asynchronous.** The probe right after the queue reads 0 and the helmets
  appear a frame later. The "already resident" branch (Franchise packages loaded) was not reached
  in a test: Eras is only reachable from the Main Menu.
* **Playoff rows 17..21** are not used (regular season only, plan R7).
* **Housekeeping.** `logs\eras\m7-root` holds directory junctions. Delete them with `rmdir`, never
  with a recursive delete. The private pack folder `original\disc\VC_53450030.NFL2K5_sched` was
  removed after the runs.

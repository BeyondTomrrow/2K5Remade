# M16: "the disc is dirty or damaged", My NFL Eras moved to Game Modes, an Exit row

2026-10-21. The user's report: "how do I load eras up I tried and it said the disc is either dirty or
damaged also the eras needs to be in the game modes menu not the main menu and can you add a exit to
the menu".

Contact sheet: **`logs/eras/menus-fix-sheet.png`**.

## 1. The disc error: root cause

The user's launch: `build\Release\NFL2K5.exe` with no arguments; no `mods\active-pack.ini`
(so the clean game, `[MODPACK] active=Original ESPN NFL 2K5`), `mods\features.ini my-nfl-eras=1`,
the pack at `mods\packs\MyNFLEras` (`[PLUGIN] my-nfl-eras 0.3.0: on`). Their league
(`%LOCALAPPDATA%\NFL2K5-PC\eras\league1`, user team none) was created at 21:18 and 21:29 and loaded
five times between 21:18 and 22:00. Each time, Continue / New League led to the error.

**The archive's extra pack G was cut short because drive E: was full.**

* At every boot `nfl2k5_archive_extend` (src/nfl2k5_team_files.c) rewrote
  `original\disc\VC_53450030\G` (330 MB: the added teams' files, then My NFL Eras' 153 stadium files,
  then its 3 office packages, listed last). It also appended their entries to the game's archive
  index. Nothing checked the writes.
* The user's session log says `252 added-team files in an extra archive pack (329250 KB)`, which is
  337,152,000 bytes. The `G` on disk (written 22:00:34, the user's boot) is **296,019,968 bytes**. Every
  test copy of the same file set is exactly 337,152,000 bytes. Searching `G` for the files' contents:
  144 of the 153 stadium files are there, while the Yale Bowl's 9 files and all three office packages
  (`erasoffice.iff`, `erasdraft.iff`, `erasskin.iff`) are not.
* Continue / New League push the office, which loads `erasoffice.iff` by name. The index entry
  pointed past the end of `G`, the read failed, and the game showed its own "the disc is dirty or
  damaged" screen. The Draft, the draft show and a game at the Yale Bowl would have failed the same
  way.
* E: was at 99 % (6 GB free) when I started and reached 0 bytes twice during this work. The
  sandboxes' `saves\` copies and frame dumps were part of that (see 5).

The pack itself was **not stale**. `make_pack.py --install` checked all 538 local files against the
development folders: none differ, and one tool-only file (`draft\audio\clip_timing.json`) was
missing. The gate was right too: Eras was on and its files were listed. This was not SOFTDRINK.

### The fix (src/nfl2k5_team_files.c, plugin API, Eras hub)

* **Checked writes.** Every `fwrite`, the final extend and the `fclose` are checked, and so is the
  file size afterwards. A file that is not wholly in `G` gets **no index entry**. A missing file
  then means "not in the archive" (the office falls back cleanly), never a read past the end.
* **Room first.** Before writing, the free space plus the old `G` (truncating frees it) must cover
  the new pack plus 32 MB. If it does not, only the added teams' own files are written, or nothing.
* **Not rewritten when unchanged.** `G.list` beside `G` records what it holds (block, size, mtime,
  name, path). When it matches, the 330 MB is not rewritten at every boot (`the extra pack already
  holds these 252 files; not rewritten`). `NFL2K5_REWRITE_PACK=1` forces a rewrite.
* **Eras says why instead of failing.** `NfHostServices.archive_problem` is a new member at the end
  of the plugin API's services. `nfl2k5_eras_limited()` returns it when any of the module's files
  were left out. The hub then hides Continue, and New League reads e.g. `Disk full: free 32 MB on
  E:, restart` and refuses. A toast says the same at boot. Continue / New League are also refused
  for any LIMITED reason (before, New League only relabelled).
* Test switch: `NFL2K5_TEST_FREE_MB=<n>` pretends the drive has n MB free.
* `tools/eras/make_pack.py --install`: a kept `generated\` file is now replaced when the development
  folder holds a newer, different copy, so hand-copied draft/office files cannot go stale. Re-run
  (adds `clip_timing.json`; nothing else differed).

## 2. My NFL Eras in Game Modes

`NFL2K5_ERAS_MENU` now defaults to `modes`: "My NFL Eras" is the 7th and last row of Game Modes
(after Tournament). It fits the navigation layout and does not scroll. The Main Menu row is gone,
which also ends the Crib phone icon sitting beside it. `NFL2K5_ERAS_MENU=main` brings back the old
placement. The pack gate is unchanged: no pack, `--vanilla`, features.ini off or a mod pack means no
row. B from the hub returns to Game Modes (FSM pop). The test switches `NFL2K5_ERAS_HUB_TEST` /
`_DRAFTSHOW_TEST` still put their rows at the top of the Main Menu.

## 3. Exit

The new file `src/nfl2k5_exit_menu.c`: "Exit" is the Main Menu's last (8th) row. The menu scrolls,
as it did with the Eras row in M3, and Quick Game stays the first row and the default. It opens
"Exit NFL 2K5?" with **Yes / No**: a copy of the game's Features link list (the same kind of screen
as the Eras hub), with two type-9 rows served at `0xFEC0F000..` (routed in `recomp_lookup_manual`).
No and B pop back to the Main Menu. Yes posts `WM_CLOSE` to the game window, the same path as the
title-bar close (`[WINDOW] WM_CLOSE received ... the game is closing`, then the window thread's
`ExitProcess`). The game's own OK message box (the "Successfully loaded ..." dialogs) was not used;
it has no Yes/No form that was found.

* The Main Menu and the template are found by their row labels (Features, Options). An XBE whose
  layout differs gets no Exit row (`[EXITMENU] off: ...`), never a broken one.
* SOFTDRINK: the menu is the same descriptor (`[EXITMENU] "Exit" is the Main Menu's last row (8 rows)`
  with v0.6 and v0.6.1). The interpreter's `native_for` now also tries `recomp_lookup_manual` for
  `>= 0xFE000000` targets (after the kernel thunks). That way the menu functions served from C
  (Exit, Video Settings, Pads) are reachable from interpreted pack code (NATIVE-MENU 2.2 hazard).
* `NFL2K5_NO_EXIT_ROW=1` turns the row off (used once as a control).

## 4. Tests

Background mode (RECOMP_BACKGROUND, RECOMP_AUDIO_MUTE, NFL2K5_NO_HOST_PAD=always, press file),
NFL2K5_PRIVATE_PACKS=1, the exe copy `NFL2K5_menus.exe`, sandbox root `E:\eras-sbx\m16-root` (junctions
plus a sparse copy of `saves\`; `tools/eras/m16_test.ps1`) and sandbox user folders (`-UserLeague`
copies the user's own league1). The build lock was held. The real `saves\` was listed before and after
(2987 files, size and mtime): **unchanged**. Nothing under `saves\UDATA` is newer than the start.

| test | result |
|---|---|
| user config (no args, clean game, Eras on), user's league: Game Modes -> My NFL Eras -> Continue -> office -> Play | **pass**: row 7 of Game Modes; hub; office (1980 week 1, IBM PC); `launch 1980w01-falcons-vikings`; Coach Match Up -> pregame. No disc error |
| `--retail`, same league: hub, B back to Game Modes, Continue, Schedule, Depth Chart, Office settings, 1981 Draft (The Big Board), Play | **pass**: every screen opened; Vikings vs Falcons kickoff on the field |
| simulated full drive (`NFL2K5_TEST_FREE_MB=0`, rewrite forced) | **pass**: `writing 96 of 252 files`, `156 ... left out`; hub: Continue hidden, the New League row gives the reason (sheet panel 8 shows the first, longer text that clipped; shortened afterwards to `Disk full: free N MB on E:, restart`); A on it: `hub row 1 refused`; no office push, no disc error |
| unchanged pack | **pass**: `the extra pack already holds these 252 files; not rewritten` |
| Exit, clean game | **pass**: Quick Game first; Up wraps to Exit; prompt; No: `back to the Main Menu`; Yes: `Yes: closing the game`, WM_CLOSE / WM_DESTROY, the process ended |
| SOFTDRINK v0.6.1 (sandbox `mods\active-pack.ini` only) | **blocked**: `[ERAS] off: SOFTDRINK_2K28_v0.6.1-v0.6.1 is on`, no Eras row, the Exit row installed (`8 rows`). The game then ends during boot with **0xC0000409** (fail-fast) before the menus, every run (background, foreground, without PRIVATE_PACKS, without NFL2K5_NO_PACK_DISPATCH, with the Exit row switched off) **and with the Oct 17 exe (`NFL2K5_season.exe`), which has none of this work**. So it is pre-existing and not from this change. v0.6 crashes in the same harness with the known divide by zero in `sub_003CAD50+0xDF` (as in M10). Exit under SOFTDRINK was therefore not clicked in game |

## 5. Disk space (housekeeping)

* `saves\Partition*.img` are sparse (5.4 GB apparent, 0.5 GB used). `Copy-Item` filled them in and
  filled E:, so `m16_test.ps1` copies them with Git's `cp --sparse=always`. It keeps only the newest 5
  frame BMPs (one 1 MB frame a second filled the drive a second time).
* Removed the `G` files (only `G`; the 0..F hard links stay) of 18 finished test copies' private
  archive folders from Oct 16-17 (`VC_53450030.NFL2K5`, `_coliseum`, `_draftshow`, `_season`,
  `_sched`, `_m10*`). Each is a cache that is rewritten whenever that exe name boots. Also removed my
  sandbox, frames and my own `G`. The music and saves agents' folders were left alone.
* The user's shared `G` is still the short one from Oct 20. Their next boot rewrites it (there is no
  `G.list`), now with the space check.

## How to launch Eras now

Run `E:\NFL2K5-PC\build\Release\NFL2K5.exe` as before (no arguments, or `--retail`). Keep a few hundred
MB free on E: (the game needs about 360 MB for its added-files pack at the first boot after a change).
Main Menu -> **Game Modes -> My NFL Eras** (last row) -> Continue League (or New League) -> the office
-> Play. If the drive is too full, the hub's New League row says how much to free, with no disc
error. **Exit** is the last row of the Main Menu (press Up from Quick Game).

## Files

`src/nfl2k5_team_files.c` (archive extension), `src/nfl2k5_plugin_api.h` / `src/nfl2k5_plugin_host.c`
(`archive_problem`), `src/nfl2k5_eras_module.c`, `src/nfl2k5_eras_menu.c` (Game Modes default, hub
refusals), `src/nfl2k5_exit_menu.c` (new), `src/recomp_manual.c` (lookup), `src/main.c` (install),
`src/interp/x86_interp.c` (`native_for`), `config/game.cmake`, `tools/eras/make_pack.py`,
`tools/eras/m16_test.ps1` (new).

# SOFTDRINK 2K28 v0.6.1: boot crash fixed (2026-10-21)

Frames: `logs\softdrink-061-sheet.png`. It shows the v0.6.1 Main Menu, the Exit row, the Exit
prompt, Game Modes without My NFL Eras, The Crib, a Quick Game kickoff and play, the Main Menu
after quitting a game, and a run without `NFL2K5_PRIVATE_PACKS`. The single frames are in
`logs\sd061\`.

## 1. The 0xC0000409 at boot (affects every user)

**Cause.** The crash was not a stack-cookie failure. It was our own `abort()`, which raises a
fail-fast with code 0xC0000409, subcode 7 `FAST_FAIL_FATAL_APP_EXIT`. I found it by running the
game under cdb (`cdbX64.exe`, from the WinDbg app). I ignored first-chance exceptions (the MMIO
handlers raise many access violations, and there are single steps), and the debugger stopped on
the fail-fast with this stack:

```
ucrtbase!abort
NFL2K5!nfl2k5_io_worker+0x35b   [src/recomp_manual.c, the "Invalid state ... target" default case]
NFL2K5!sub_000170B4             (guest thread start, worker 0x0004D810)
```

`recomp_lookup_manual()` serves a hand-written C version of the retail I/O worker at
**0x0004D810**. That version dispatches through the retail jump table at 0x0004D8C0.
**SOFTDRINK v0.6.1 rewrote that function.** It is now a compact loop: state 1 sleeps 1 ms
through `KeDelayExecutionThread`, and states 2-12 call through a table at 0x0004D86C. The
retail table bytes are gone too. Our C version still ran, because manual overrides have no
interpreter gate. It read the pack's bytes as jump targets, found none it knew, and called
`abort()`. The `[WORKER]` message was lost because stderr was not flushed at the abort.

Among all installed packs, only v0.6.1 changes any of the 29 manual-override entry points. I
checked v0.4, v0.5, v0.6, iso, 2K28 and Widescreen.

**Fix.**
* `src/interp/interp_setup.c`: new `nfl2k5_interp_code_changed(va)`. It keeps a copy of the
  converted executable's image and compares the pack's bytes at `va`. It uses the converted
  function's range when one starts at `va`, otherwise the first 32 bytes. Results are cached
  in a lock-free table.
* `src/recomp_manual.c`: `recomp_lookup_manual()` is now a wrapper around the old switch
  (`lookup_manual_any`). When the interpreter is active and the pack changed that retail
  address, it returns NULL. The call then reaches the pack's own code through the interpreter,
  from icall dispatch, thread start or `native_for`. The `0xFE......` menu functions are not
  affected. Log line:
  `[INTERP] the pack changed 0x0004D810, which has a hand-written native version: interpreting the pack's code`.

## 2. "Disc is dirty or damaged" with NFL2K5_PRIVATE_PACKS (test runs only), and the v0.6 divide by zero

With the abort fixed, sandbox test runs showed the SEGA disc error. `NFL2K5_PRIVATE_PACKS=1`
maps `D:\VC_53450030` to `<disc>\VC_53450030.<exe>\`. A mod pack's overlay
(`external/xboxrecomp/src/kernel/kernel_overlay.c`, `ovl_find`) recognises its changed banks
by the path's tail `vc_53450030\<n>`, so it never matched them under that folder. The game
then read the retail banks with the pack's index.

**Fix** (`src/nfl2k5_team_files.c`): the private banks now sit one level down, in
`<disc>\VC_53450030.<exe>\VC_53450030\`, and the overlay matches again.

**This also explains the v0.6 divide by zero in `sub_003CAD50+0xDF`** seen in M10/M16 and in
the music agent's sandbox runs, which all used PRIVATE_PACKS. With this build, v0.6 in the
same harness boots to the Main Menu, gets through Play Now and the pregame to kickoff, and
plays several downs with no crash. The older Oct 9-10 v0.5 crashes at the same address ran
without PRIVATE_PACKS and on older builds. I did not re-run v0.5.

Every test-copy folder made before this change (`VC_53450030.NFL2K5_*`, flat layout) is now
stale. Its `G` file can be deleted; the next boot of that exe name rebuilds the cache in the
new place.

## 3. Base files

I hashed every `original\disc` file the v0.6.1 manifest lists (default/update/dashupdate.xbe
and `vc_53450030\0..F`) against its "before" sha256. **All 19 match retail.** The restored
`0` is clean. Free space on E: was 69-72 GB during the work.

## 4. Tests

All runs were in background mode (RECOMP_BACKGROUND, muted, NFL2K5_NO_HOST_PAD=always, press
file), with the exe copy `NFL2K5_sd.exe` and sandbox root `E:\eras-sbx\sd061-root`. The
sandbox used junctions and a sparse copy of `saves\`, and its own `mods\active-pack.ini` set to
`SOFTDRINK_2K28_v0.6.1-v0.6.1`. The real `mods\active-pack.ini` was not touched.

| run | result |
|---|---|
| v0.6.1, PRIVATE_PACKS, before the fix, under cdb | fail-fast in `nfl2k5_io_worker` (above) |
| v0.6.1, worker fix only | boots, then the SEGA disc error (the overlay miss, section 2) |
| v0.6.1, both fixes, PRIVATE_PACKS | **pass.** `[ERAS] off: ... v0.6.1 is on`. `[EXITMENU] ... 8 rows`. Title, then the settings/VIP load dialogs, then the Main Menu. Exit row; prompt; **No** returns to the Main Menu. Game Modes has no Eras row. The Crib: profile, walk-around, its own menu, "Exit your Crib?" OK. Play Now: Team Select, Coach Matchup, pregame, kickoff, about 4 minutes of play. Pause, Quit, Yes: back at the Main Menu. Exit, **Yes**: `Yes: closing the game`, WM_CLOSE / WM_DESTROY, process ended. No `[CRASH]` |
| v0.6.1, both fixes, **no** PRIVATE_PACKS (the user's setup; sandbox `original\disc` made of hard links so `G` was written in the sandbox) | **pass**: boot, Team Select, kickoff, plays |
| v0.6, both fixes, PRIVATE_PACKS | **pass**: Main Menu, pregame, kickoff, several downs |

The real `saves\` was not written by these runs. The only writes since 21:40 are 21:51-22:02,
from the separately launched `NFL2K5.exe` (pid 35420, started 21:51). The sandbox was removed:
junctions with `cmd /c rmdir` first, with a check that no reparse points were left.

## 5. The pack itself

Nothing found broken in v0.6.1 itself. Its rewritten worker is equivalent to retail
(0 -> state 1; state 1 waits, now with a 1 ms sleep instead of `sub_0004B910`; states 2-12
call and complete; 13 ends). It only failed because our C replacement ignored the change. A
Crib phone icon appears beside "The Crib" on the Main Menu after a game. That is the game's
own notice, not the old Eras row.

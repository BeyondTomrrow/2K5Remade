# Old-school equipment removed from the active build (2026-10-22)

The user reported "players are missing face masks again" on 2026-10-22, after
the retail archive `original\disc\vc_53450030\0` had already been restored to
clean retail on 10-21. The cause was that the parked old-school equipment
feature (leather helmet / no face mask / 1950s body, 2026-10-14/15) was still
**compiled into the game** and its hooks ran on every player draw. "Parked" had
only meant a git branch (`parked/oldschool-1953`) and a `parked\` folder; the
working tree still carried the live code. This note records what was removed,
where it went, and what was verified.

Rule going forward: parking a feature means removing it from the build (cmake,
gen patches, hooks, data folders), not just branching it.

## Why face masks vanished for modern players

The feature kept its choices in spare roster-record bits: `+0x53` bit 5
(OLD_BODY), bit 6 (LEATHER), bit 7 (NO_MASK). The user's roster save
`Roster1sd28` (UDATA `376C2FA85485`, a 2026 roster written with 2K Football
Mod Tools) uses `+0x53` bits 1-7 for its own purposes (star tag, rank/side
locks, "Rules v2 tier, physical footer +0x53 bits 6..7"). 160 of its 2,547
records have one or more of bits 5-7 set (e.g. Budda Baker 0xD9, Lamar Jackson
0x9D, Ja'Marr Chase 0xDD, Josh Allen 0x49). With the old-school draw hook
compiled in, every such player was dressed with a leather shell and/or no face
mask. Retail code ignores those bits, so with the hook gone the records draw
normally; the save itself was not and must not be modified.

Scan (read-only, `logs\facemask-scan53.txt`): 111 saves parsed (56 ROS roster
saves, 55 FXG franchise saves), 0 errors. Records with `+0x53 & 0xE0`: 160,
all in `376C2FA85485` (Roster1sd28); every other roster and franchise save has 0.

## What was removed from the active build

Gen patches (`tools/apply-gen-patches.py`) and their applied blocks in
`src/recomp/gen` (the patcher skips IDs already present, so the applied lines
and their `/* NFL2K5-GENPATCH:<ID> */` markers were stripped by hand):

| ID | function | file |
|---|---|---|
| OLDSCHOOL_DRAW | sub_0008FAD0 | recomp_0004.c |
| OLDSCHOOL_RESTORE / _2 | sub_0008FD90 / sub_0008FDB0 | recomp_0004.c |
| NCAA_CAPTURE / _2 / _3 | sub_00022C00 / DA0 / DDD | recomp_0000.c |
| OLDSCHOOL_HISTORIC | sub_002D17B0 | recomp_0020.c |
| OLDSCHOOL_CLONE / _2 | sub_000C1030 / sub_000C14C7 | recomp_0005.c |
| OLDSCHOOL_ROWS | sub_00174140 | recomp_0011.c |
| OLDSCHOOL_CAMERA | sub_003433C0 | recomp_0022.c |
| OLDSCHOOL_MENU_HELMET_NAME/NEXT/PREV, _MASK_NAME/NEXT/PREV | sub_00345A10..B10 | recomp_0023.c |

`tools/gen-reg-locals.py` fails the build when a gen file still carries a
marker for an ID the patcher no longer lists, which is how the markers were
caught.

C sources and hooks:

- `config/game.cmake`: `src/nfl2k5_oldschool.c` and `src/nfl2k5_ncaa_body.c`
  removed from the executable.
- `src/nfl2k5_team_files.c`: the `mods\oldschool\files\*` include in the extra
  archive pack (every boot) and the `"oldschool": true` team dressing removed.
- `src/nfl2k5_teams.c`: `nfl2k5_oldschool_historic()` call on Load Historic Team.
- `src/recomp_manual.c`: the `0xFED00000` "Pads" row lookup
  (`nfl2k5_oldschool_lookup`).
- `src/nfl2k5_input_hle.c`: the `g_nfl2k5_pad0_rx` right-stick export used by
  the Pads preview spin (the last unresolved extern).
- `external/xboxrecomp` (vendored, gitignored): `nv2a_gpu_d3d11.inc.c` no longer
  includes `src/nfl2k5_ncaa_render.inc.c`, and the `ncaa_mesh` /
  `ncaa_pre_draw` / `ncaa_player_draw` / `ncaa_texture_srv` blocks are gone;
  `nv2a_pb_exec.c` is back to upstream (the `fetch_attr` / `g_os_delta`
  wrapper was only for the old-school body). Nothing else used the NCAA body
  renderer, so nothing of it is kept.

The new `build\Release\NFL2K5.exe` (56,816,128 bytes, 2026-10-22 12:59)
contains no `OLDSCHOOL`, `oldschool`, `L"Leather"`, `nfl2k5_ncaa` or
`NCAA_MESH` strings.

## Where it went (nothing deleted)

`parked\oldschool-1953\` (git-ignored; code also on branch `parked/oldschool-1953`):

- `code\nfl2k5_oldschool.c`, `nfl2k5_ncaa_body.c/.h`, `nfl2k5_ncaa_render.inc.c`,
  `nfl2k5_ncaa_routes.inc`, `test-oldschool.ps1`, `lions1953.py` (the 10-15
  versions of the first two that were already there moved to
  `code\2026-10-15-copies\`).
- `data\mods_oldschool\` <- `mods\oldschool\` (historic.txt, files\oshibody.iff
  etc., v7 deltas, ncaa meshes).
- `data\team_Lions1953\` <- `mods\teams\Lions1953\` (team.json `"oldschool": true`).

None of the removed hunks had ever been committed to master: after the removal
`git diff` of `src/nfl2k5_team_files.c`, `src/nfl2k5_teams.c`,
`recomp_0020.c`, `recomp_0022.c` and `recomp_0023.c` is empty, and the
remaining uncommitted hunks in `game.cmake`, `recomp_manual.c`,
`nfl2k5_input_hle.c`, `apply-gen-patches.py` and the other gen files belong to
other in-progress work, so this change commits only this document.

## Verification (sandboxed, background mode)

Harness: a copy of `tools/eras/m16_test.ps1` (sandbox root `E:\eras-sbx\mask-root`
of junctions + a sparse copy of `saves\`, `-AppData` sandbox, exe copy
`NFL2K5_mask.exe`, `RECOMP_BACKGROUND=1`, `NFL2K5_NO_HOST_PAD=always`, press
file, `NFL2K5_PRIVATE_PACKS=1`). The real `saves\` tree was fingerprinted
before and after (2,987 files, names/sizes/mtimes): unchanged.

Frames in `logs\facemask\`, sheet at `logs\facemask-sheet.png`:

- `--retail`, Quick Game TB at DAL (disc roster): kickoff, plays, post-play
  close-ups (`retail2-f08-zoom.png`: Cowboys #40 and Buccaneers #58 with face
  masks; `retail2-f21-zoom.png` huddle, `retail2-f15-zoom.png`).
- SOFTDRINK v0.6.1 (`SOFTDRINK_2K28_v0.6.1-v0.6.1`), attract-demo game SEA at
  IND: `sd-g38-zoom.png` (Seahawks #14 and Colts), `sd-g35-zoom.png` (#91),
  `sd-g11-zoom.png` (QB). All masked.
- The roster with the bits: `Roster1sd28` loaded in the sandbox through
  Options > Load/Save > Load (`ros2-z6.png` "Successfully loaded Roster
  Roster1sd28"), then Quick Game CLE at DAL, CPU vs CPU (2026 coaches Monken /
  Schottenheimer confirm the roster). Cowboys #4, #56 (`ros2-p11-zoom.png`),
  Browns #10, #55, #74, #11 (`ros2-f31-zoom.png`, `ros2-f29-zoom.png`,
  `ros2-f23-zoom.png`), kickoff, celebration: every helmet has its mask. DAL's
  flagged records include Javonte Williams #33 (0x9C, the old NO_MASK bit 7 set),
  seen masked from behind in `ros2-f10.png`.
- Edit Player (Features > Player Create > new player > Equipment page):
  Helmet cycles Standard / Revolution only, Face Mask Type 1 .. Type 27 only
  (`edit5-eq-rows.png`; no "Leather", no "None"). The roster-save prompt was
  answered No.
- My NFL Eras (sandbox `features.ini` my-nfl-eras=1, `NFL2K5_ERAS_HUB_TEST=1`):
  hub > New League (1980) > the 3D office loads (`eras-h3.png`), Draft Show
  (Preview) runs with the commissioner and pick 3 Freeman McNeil on stage
  (`eras-d3.png`, log "draftshow pick 3: chest-height hold"). No crash in any
  run's log.

Caveats: the background harness presses buttons blind; a few runs drifted
(an over-pressed A started a Quick Game, one Exit-row press closed a sandbox
run) and were simply relaunched. Each run writes only to the sandbox copy of
`saves\` (E:\eras-sbx\mask-root, removed afterwards).

Build lock `build\.eras-build.lock` was held for the three rebuilds (first two
failed: gen-patch markers still present, then the `g_nfl2k5_pad0_rx` extern) and
released afterwards. Disk: E: fell from 69 GB to 25 GB free during the session,
caused by another workstream's 36 GB `E:\MLB26-PC-Prototype-Data\extracted\romfs.bin`
(14:12), not these tests.

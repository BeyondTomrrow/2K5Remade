# M10: My NFL Eras as a separately installed feature pack

2026-10-16. Implements docs/eras/MOD-PACK-DESIGN.md, migration steps 0-8 (step 9, the DLL, skipped
as designed). The Eras code stays in `NFL2K5.exe` behind one versioned hook table; the table is
filled only when a valid `mods\packs\MyNFLEras\pack.json` is installed on the clean game.

## What was built

| step | what | where |
|---|---|---|
| 0 | the module and its host code under version control; the build/eras scripts moved to tools/eras (paths fixed) | commit "step 0" |
| 1 | one path resolver: every Eras file by its pack-relative name (`league\1980\teams.csv`, `offices\<era>\era.ini`, `office\files\*.iff`, `teams\1980\<id>\team.json`, `stadiums\<F>\stadium.json`, `presentations.ini`) | `nfl2k5_eras_path`, later `nfl2k5_pack_path` |
| 2 | `src/nfl2k5_plugin_api.h`, `src/nfl2k5_plugin_host.c`, `src/nfl2k5_eras_module.c`: the hook table and the `nfl2k5_hook_*` shims; gen patches `HOOK_GAME_FINAL/MATCH_EXIT/RETURN` replace `ERAS_*` (apply-gen-patches.py retires the old blocks); interp hooks, FSM pop, the 0xFEE VA range, menu install and the archive extension go through it; WALK_DIAG moved to `src/nfl2k5_walk_diag.c` | |
| 3 | `NfHostServices`: the module reaches teams, stadiums, presentation, texture packs, stats, toasts and the user folder only through `g_eras_svc->`; the pack roots are a host service (none while no module is on); `src/nfl2k5_eras_guest_calls.h` (63 guest functions) made and checked by `tools/eras/check_guest_calls.py` (build.ps1 warns when stale) | |
| 4 | the gate (pack.json via `src/nfl2k5_json.c`, id/kind/manifest_version/data_schema/min_game_version/module, features.ini, `--vanilla`, `NFL2K5_ERAS=0/1`, base check, season-package check), LIMITED mode, `--vanilla`, the XBE-pack list skips `kind: feature` | |
| 5 | the pack layout, `tools/eras/make_pack.py` (--install / --dist / --check-zip), `tools/eras/m10_test.ps1` | |
| 6 | `tools/eras/build_assets.py` (NFL2K5-ErasBuilder), `tools/build-eras-builder.ps1`, `tools/eras/eras_settings.py` (no hard-coded paths; `--ncaa-dir`), hub row "Build My Assets" + `svc->run_builder` | |
| 7 | `tools/eras/pack-release.ps1` (release split + checker) | |
| 8 | Features > Mod Packs lists "My NFL Eras (feature pack)" On/Off (`mods\features.ini`, applies at the next start); the installer ships the builder and the 2K Football Mod Tools licence | |

### Every gate site of the design (section 1.3)

| # | now |
|---|---|
| G1 menu install | `nfl2k5_hook_menus_ready()` (main.c) |
| G2/G3 interp hooks, capture init | registered by the module's `on_boot` through `svc->interp_add_hook`; none when off |
| G4 0xFEE00000 VA range | `nfl2k5_hook_lookup_va` (recomp_manual.c), NULL when off |
| G5-G7 gen patches | `HOOK_GAME_FINAL / HOOK_MATCH_EXIT / HOOK_RETURN` shims (one load and a branch when off) |
| G8 WALK_DIAG | `src/nfl2k5_walk_diag.c` (not Eras) |
| G9 FSM pop | `nfl2k5_hook_fsm_pop` (main.c) |
| G10 archive extension (the 211 MB leak) | `nfl2k5_hook_archive_extend(&sink)`; the module lists its files; nothing when off (tested: 96 files without the pack, under `--vanilla`, under SOFTDRINK and with features.ini off; 252 with the pack) |
| G11 stadium-pack rows | host service `stadium_borrow`, `stadiums\*\stadium.json` from the pack roots |
| G12 test hooks | `nfl2k5_pack_path("league\\1980")` (empty when off) |
| G13 diagnostics | left |
| G14 texture packs | pack roots (offices, office-textures.ini, team art) |
| G15/G16 presentation | team.json / logos / presentations.ini from the pack roots; erateams.local mapped only when a root has teams\ |
| G17 module paths | resolver |
| G18 game.cmake | unchanged (built in) |

The draft show (the other agent's M11, `src/nfl2k5_eras_draftshow.*`) lives inside the module
(its menu and office hooks run only when the module is on), so it has the same gate.

### Pack layout (mods\packs\MyNFLEras)

```
pack.json  README.txt  CREDITS.txt  (MISSING\SUPPLY.txt in the download)
league\1980\  teams.csv schedule.csv coaches.csv PROVENANCE.txt   [+ rosters\ depth\ supplied]
teams\1980\<id>\team.json        offices\ (era.ini, office-textures.ini, the drawn PNGs)
stadiums\<F>\stadium.json        presentations.ini      recipes\ (build.json, era_logos_1980.json,
generated\  (built on the player's PC)                            team_art_1980.json, ncaa_stadiums.json)
  office\files\erasoffice.iff erasskin.iff   teams\1980\<id>\logos\  teams\1980\art\
  stadiums\<F>\files\   offices\1980s\computer_monitor.png   user\   draft\   eraselect\   build.json
```

Lookup order: `generated\`, then the pack, then (only with `NFL2K5_ERAS=1` or `dev.txt`) the
development folders (`data\eras\runtime`, `mods\eras`). On this PC `make_pack.py --install` made
the full pack from the development folders (stadium bundles hard-linked); the development
folders are untouched.

## Tests

All in-game runs: background mode (RECOMP_BACKGROUND, RECOMP_AUDIO_MUTE, NFL2K5_NO_HOST_PAD=always,
press file), sandbox root `E:\eras-sbx\root` (junctions + its own saves copy + its own
mods\packs), sandbox user folder per run, NFL2K5_PRIVATE_PACKS=1, the shared build lock.
College Football 27 was running throughout and was never touched. The real `saves\` was hashed
before and after: unchanged (2978 files).

| test | result |
|---|---|
| step 0: clean tree builds; `make_runtime_package.py --season 1980 --out` reproduces `data/eras/runtime/1980` | **pass**: byte for byte except the two script-path lines in PROVENANCE.txt (now tools/eras); `league_schedule.py --check` ok; `m0_negative_checks.py`: 16 caught, 0 missed |
| every commit builds | **pass** (worktree build of each code step 0, 1, 2, 3, 4, 6, 8; 5 and 7 are scripts only) with two worktree-only stubs, see below |
| step 2: hook table, no gate | **pass**: `retired ERAS_* / applied HOOK_*`; boot `[PLUGIN] my-nfl-eras 0.3.0: on`; archive `My NFL Eras stadiums: 153 files`, `office: 3 files`; New League -> the 1980s 3D office with the IBM PC (m10-s2-office2.png) |
| step 3: guest-call list | **pass**: 63 functions, list OK |
| step 4 matrix (worktree build) and again with the full main build | **pass**: no pack: `[ERAS] off: no ...pack.json`, 96 archive files, no row; pack: roots generated + pack, 252 files, row; min_game_version 9.9.0: `limited: ... needs NFL2K5-PC 9.9.0 or newer`, 96 files, row with the reason; download pack (no rosters): `limited: rosters missing ... (see MISSING\SUPPLY.txt)`; `--vanilla`: off, 96 files; features.ini `my-nfl-eras=0`: off, 96 files, the XBE list shows 5 packs (no feature pack) |
| (1) without the pack: no row, no Eras archive files, Quick Game fine | **pass**: log above; Quick Game Panthers at Cowboys started and played to the kickoff (m10-qg-t4.png) |
| (2) with the pack: one full Eras game from the office, committed, back to the office | **pass** (main build): New League -> office -> Play: Falcons at Vikings (CBS package, logos from `generated\teams`), CPU vs CPU to the final (Falcons 20, Vikings 7, m10-game-mid2.png); `results.csv` row and box score at the whistle; End Of Game (Rematch / Save VIP hidden) -> Exit: `return: to the office`, `committed 1980w01-falcons-vikings`, slots released, settings restored, next `1980w01-dolphins-bills` (m10-game-back2.png) |
| (3) SOFTDRINK boot: Eras off, archive without Eras files | **pass** for the gate: `[ERAS] off: SOFTDRINK_2K28_v0.6-v0.6 is on (feature not supported on that base)`, 96 archive files. The run then crashed in the boot menus: divide by zero in `sub_003CAD50+0xDF` -- the same crash is in logs/sd2, sdz, sdm1B, sdm4, snf-pregame-hw.stderr.log from 2026-10-09/10, before this work (pre-existing; not Eras) |
| (4) `--retail` with the pack: Eras on | **pass**: every pack run above used `--retail` |
| (5) make_pack.py zip has no retail pixels | **pass**: `MyNFLEras-0.3.0.zip`, 78 files, 11 images, all drawn office art, none byte- or pixel-equal to any of 814 disc-derived / user files of this PC, no .iff/.cdf/.tmd/.glb/.max/.psu/.mp4/.mp3/.jpg; negative check: a zip with a disc logo and erasskin.iff added fails (7 findings) |
| (6) the builder rebuilds office / logos / badges into a fresh folder from the disc | **pass**: `python -I tools/eras/build_assets.py --out <fresh>`: 79 of 79 files byte-identical to today's (both office packages incl. the IBM desk, the monitor atlas, 56 logos, 20 card textures). `build\tools\NFL2K5-ErasBuilder.exe --out <fresh>`: 77 of 78 identical; erasoffice.iff is the base four records (identical) without the optional IBM desk (see below) |

## Commits

On master (each ends with the Co-Authored-By line):
step 0 .. step 8 and this document; see `git log --oneline f9a28e1..`.

Only Eras files and hunks are committed. Shared files were split hunk by hunk: committed are the
Eras parts of `src/main.c`, `src/recomp_manual.c`, `src/nfl2k5_teams.c`, `src/nfl2k5_team_files.c`,
`src/nfl2k5_presentation.cpp`, `src/nfl2k5_texture_packs.cpp`, `src/nfl2k5_controller_art.cpp`,
`src/presentation/*`, `config/game.cmake`, `tools/apply-gen-patches.py`, `tools/build.ps1`,
`src/nfl2k5_mod_packs.c`, `installer/NFL2K5-PC.iss` and the three gen files with the HOOK_*
patches. Left uncommitted (the user's): oldschool / NCAA body, SNF presentation, input, music,
save library, video menu, interpreter, SOFTDRINK patches, build.ps1 -StageOnly, PROJECT_STATUS.md;
and the draft-show agent's files and hunks (it commits its own).

## Deviations from the design, and what was skipped

* **Greyed Main Menu row**: implemented as LIMITED mode instead -- the row opens the hub, whose
  New League row carries the reason (greyed), Continue hidden; no capture hooks, no archive
  files. The row-update grey bit on the Main Menu itself was not tried (no test of it).
* **Pack source in git**: no tracked `packs/MyNFLEras/` + junction. The sources stay where they
  are (`data/eras/runtime`, `mods/eras`, tracked where redistributable); `make_pack.py` assembles
  the pack (local install and download). The development layout stays as a fallback root
  (`NFL2K5_ERAS=1` or `mods\packs\MyNFLEras\dev.txt`), as asked ("keep a dev fallback").
* **generated\office\files\**: the office packages keep the `files\` level (same rel name in
  every root).
* **Exe builder**: the optional `office_1980s` step (the IBM desk) needs 2K Football Mod Tools'
  `mod_editor` package (`.tmp-2k-football-mod-tools`, not in git); the PyInstaller exe does not
  bundle it and skips the step (the 1980s office then keeps the retail desk). The Python entry
  point runs it. The stadiums step is implemented (per-venue resume, `--ncaa-dir`) but was **not
  run** (about 100 minutes); LA Coliseum is built by `coliseum_build.py`, not by the builder yet.
  No Python STADIUMS.DAT reader (the player needs an extracted `stadiums_raw`).
* **Feature Packs toggle** (step 8): the features.ini switch was tested at boot; the menu rows
  themselves were not clicked in game.
* **Toast under SOFTDRINK**: shown at the menu install (not seen: that run crashed first, see (3)).
* **Step 9 (DLL)**: skipped, as designed.
* **Worktree build stubs (not committed)**: master already did not link on its own --
  `main.c` calls `nfl2k5_test_interp_flags` and `nfl2k5_local_music_shutdown`, defined only in the
  user's uncommitted `x86_interp.c` / `nfl2k5_local_music.cpp`, and the shared
  `external/xboxrecomp` includes the user's uncommitted `src/nfl2k5_ncaa_render.inc.c`. The
  verification worktree (`E:\NFL2K5-erawt`) had an untracked stub for these; everything else
  built from the commits alone.

## Redistribution (section 8) in the download

Shipped: league facts, team.json identities, drawn office art and era.ini, stadium.json,
presentations.ini (names only), recipes (hashes and numbers only), pack.json, README, CREDITS
(sources, MIT notice, disclaimer). Left out and listed in MISSING\SUPPLY.txt: the 1980 rosters
and depth charts (community ratings), the IBM PC photo and its crops, the 1980 Bengals logo
image, the ElevenLabs narration, all disc/NCAA-derived files (office packages, logos, card art,
stadium bundles), the draft show's files and audio. With the download alone the pack runs
LIMITED until the rosters are supplied.

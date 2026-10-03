# Mod pack support: handoff (2026-10-03)

The goal is **easiest for the player**. They drop a 2K5 Mod Studio `.2k5patch` into `mods\`,
turn it on in an in-game **Extras** screen (Features menu), restart, and it plays. There are
no toolchains, ISO steps or emulators.

The user agreed this design:
- **Mod Packs page:** one row per pack, On/Off. Changing a pack applies at restart or at
  the main menu.
- **Tweaks page:** our own built-in features (My Career, Passing Power, …) as
  toggles or sliders, gated so the vanilla game is untouched when they are off, with
  separate saves (`saves\extras\…`).
- **A pack's data** (rosters, stadiums, graphics) is served by a **disc overlay**; the
  retail files are never modified.
- **A pack's code** (its patched `default.xbe`) is recompiled once into a native build
  of that pack ("code pack"), loaded when the pack is on.

## Test pack: SOFTDRINK 2K28 v0.2

- **Source:** github.com/cruuz/softdrink-2k28 release v0.2.
- **File:** `mods\packs\SOFTDRINK-2K28-v0.2.2k5patch`, 715,083,579 bytes, SHA-256
  `81436ab1…756e1` (verified).
- **Scale:** a full overhaul with about 105 recipe options and about 40 gameplay code
  features: My Career, Weekly Prep, abilities, momentum, AI tuning, modern stadiums and
  more. `default.xbe` gets 1.5 MB of rewritten bytes and grows by 352 KB of new code in new
  sections `.AS0000`–`.AS0005` (VA 0x014BA000+) plus an enlarged `.XTLID`.
- **Memory:** the pack's own notes (`assets/documents/frozen-recipe.json`, candidate I)
  say the final build "plays at 64 MB (the guard) and 128 MB (K128 room)". So
  128 MB is optional headroom; we run at 64 MB.
- **Base files:** the retail disc files in `original\disc` match the pack's `before` hashes.

## The .2k5patch format 3 ("source-copy-v1"): decoded

The pack is a zip containing `manifest.json`, `operations/*.bin` and `assets/…`.

`manifest.files[]` has one entry per disc file: `path`, `before{size,sha256}`,
`after{size,sha256}`, and a `mode`:

| Mode | Meaning |
|---|---|
| `copy` with equal hashes | Unchanged; the retail file serves it |
| `runs` | Changed; built from literal runs and copy records (below) |

How a `runs` file is built:
1. **Literal runs** (`payload.member`, e.g. `operations/0001.bin`), repeated: `u64` dest
   offset, `u32` length, 32-byte sha256 before, 32-byte sha256 after, then the bytes.
   The header is 76 bytes.
2. **Copy records** (`copies.member` from the same retail file, and `cross_copies[]` from
   the retail file named by `source_path`): 84 bytes each, `u64` dest, `u64` src, `u32`
   length, then two sha256.
3. **Everything else** is the retail file at the same offset, truncated or zero-extended
   to `after.size`.

`tools/pack-install.py` implements this, and all 14 changed SOFTDRINK files verify
bit-exact (`--verify`). The installed Mod Studio (rc107-era) can't read format 3; we
don't need it.

## Built so far

- **`tools/pack-install.py <pack> [--verify]`** writes `mods\packs\<Name>-<Version>\`:
  `overlay.bin` (the index), `lit\<n>.bin` (literal bytes), the patched `default.xbe`,
  and `pack.json`. It takes about 50 s and 750 MB.
- **`external/xboxrecomp/src/kernel/kernel_overlay.c`**, hooked into
  `kernel_file.c` (open/attach, read, close, the size queries). A changed disc file is
  served as retail bytes, plus extents (literal or copy), plus zeros past the retail end.
  Overlay files are opened buffered. It loads with `NFL2K5_PACK=<installed pack folder>`
  (`src/main.c`, after `xbox_path_init`).
- **`tools/analyze.ps1 -Xbe -Analysis -Gen`** converts any XBE into its own folders.
  SOFTDRINK: `-Xbe mods\packs\softdrink_default.xbe -Analysis analysis-softdrink
  -Gen build\gen-softdrink`.
- **`tools/build.ps1 -Game -Pack softdrink`** builds `build\gen-softdrink` into
  `build\Release-softdrink\NFL2K5.exe`:
  - `apply-gen-patches.py --gen … --lenient` and `gen-reg-locals.py --gen … --out …`
  - CMake `NFL2K5_GEN_DIR` / `NFL2K5_LOCALS_DIR`

## Next steps

1. Finish the SOFTDRINK conversion and build. Then run with
   `NFL2K5_PACK=mods\packs\SOFTDRINK_2K28-2K28` and fix what breaks.
2. Check our hooks against the pack. Our gen patches (scorebug, lineup, frame interval)
   and the manual overrides in `src/recomp_manual.c` assume retail functions;
   SOFTDRINK rewrites some of the same ones (`scorebug_runtime`, …). Find out which
   anchors failed (`--lenient` output) and which manual overrides would hide the
   pack's changed functions.
3. The portrait extractor (`src/presentation/portraits.cpp`) reads `vc_53450030` pack 3
   straight from the disc. With a pack on, it should read through the overlay.
4. Build the Extras screen. Reuse the native screen technique in
   `src/nfl2k5_video_menu.c`. Pack On/Off restarts into the pack's exe with
   `NFL2K5_PACK`.
5. 128 MB memory support (optional for SOFTDRINK): `XBOX_CONTIG_SIZE`,
   `MmQueryStatistics` total pages, the 64 MB walk bounds in `nv2a_pb_scan.c`.
6. Distribution: ship code packs keyed by the patched XBE's sha256, so players never
   compile anything.

## Progress log (2026-10-03, in-game pack selector)

- **Features > Mod Packs is implemented.** `src/nfl2k5_video_menu.c` adds a native
  `Mod Packs` link to the existing Features screen. The page lists every installed
  pack that has `pack.json`, `default.xbe`, and `native\NFL2K5.exe`, with an On/Off
  value beside its display name.
- **Selection persists and restarts safely.** `src/nfl2k5_mod_packs.c` stores the
  selected folder in `mods\active-pack.ini`, validates the folder name, and writes
  the file atomically. Turning a pack on restarts into its native executable;
  turning it off restarts into the retail executable. Launching the normal retail
  shortcut also dispatches to the selected pack before Xbox state is initialized.
- **Pack builds install their native executable.** `tools/build.ps1 -Game -Pack
  <name>` matches the installed pack by the patched XBE SHA-256 and copies the EXE
  and PDB into the pack's `native` folder. Players do not need a compiler or a
  separate launcher.
- **SOFTDRINK 2K28 verified.** Both retail and SOFTDRINK builds pass the native
  toolchain check. The live game log reports one playable pack and an eight-row
  Features menu. Direct launch tests passed in both directions: retail dispatched
  to `SOFTDRINK_2K28-2K28\native\NFL2K5.exe`, and the pack executable returned to
  `build\Release\NFL2K5.exe` when disabled.

## Progress log (2026-10-03, later)

- **SOFTDRINK conversion:** `analysis-softdrink` → `build\gen-softdrink`. All 33,935
  functions translated, 0 failed (retail 33,223), 491 of them in the new `.AS*` sections.
  There are 547 unresolved jump targets (retail 430); watch stderr for
  `[UNRESOLVED]` / `[FLAGS] unresolved branch` and add seeds to
  `analysis-softdrink\seed_functions.json` if needed.
- **Hooks against the pack:** all functional gen patches applied. Only the
  diagnostics DIAG_FSMPUSH and DIAG_GAMECAST_STATVALUE miss. SOFTDRINK changed the
  ESPN scorebug functions `sub_000FC200` / `sub_000FCE70`, where our HTML-scorebug hooks
  live, so test those with a presentation package. None of the 29 overrides in
  `src/recomp_manual.c` overlap the pack's changes.
- **Pack startup:** the pack's entry point (0x014EAD50) is its 128 MB setup: it calls the
  detector `sub_014EAE05`, which needs kernel 0x1213, `PFB_CFG0` = 128 MB, an empty PTE
  and a kernel code signature. `main.c` still starts at the game's 0x16BD1, which the
  pack's entry also ends at. The entry-point check in `main.c` applies to retail only.
- **`cr3`:** the pack's page-table code does `mov cr3`. `recomp_types.h` now defines a
  dummy `cr3`.
- **Flags returned across calls (a converter improvement):** mod hooks replace an
  inline `cmp` with `call cave`, and the caller's `jcc` reads the flags the cave set.
  - `tools/recomp/flag_return_scan.py` lists functions whose every `ret` follows a
    cmp/test (16 in SOFTDRINK, 1 in retail).
  - With `RECOMP_FLAG_RETURN_FUNCS=<list>`, `lifter.py` makes those functions store
    their compare in `g_rf_*` at return, and the branch after a direct call to one
    reads it (`__retflags` in `_make_condition`).
  - SOFTDRINK's C was regenerated with it. Retail is not regenerated yet; its one
    function would also benefit.
- **Pack builds:** `tools/build.ps1 -Game -Pack softdrink` compiles `main.c` with the
  pack XBE's SHA-256 (`NFL2K5_XBE_SHA256`, from `analysis-<pack>\input-sha256.json`).
  At boot the exe finds `mods\packs\*\default.xbe` with that hash, loads that XBE, and
  sets `NFL2K5_PACK`, so the overlay turns on.
- **pushal/popal/pushfd/popfd:** the converter used to skip these (`RECOMP_TODO`).
  Mod caves wrap every hook in them: 837 sites in SOFTDRINK, 51 in retail.
  - First SOFTDRINK boot: the cave at 0x014E8555 clobbered `edi`, the boot routine
    (0x74892, `sete` on `edi`) reported failure, and the game rebooted
    (`HalReturnToFirmware`, launch data type 1).
  - Now implemented in `lifter.py`, after `_lift_pop`. `pushfd`/`popfd` carry the
    direction flag. `translator.py` declares `ebp` for functions that use them.
  - Retail C still has the old skips (51 sites); regenerate it to pick this up after
    testing.
- **Detours** (found with the ABI checker; `NFL2K5_CMAKE_EXTRA=-DNFL2K5_ABI_CHECK=ON`
  plus `tools/build.ps1 -Game -Pack softdrink`, which logs `[ABI] sub_X: ebx esi edi esp`):
  - **Shape:** SOFTDRINK replaces a function's first bytes with `jmp cave`. The cave
    re-runs the overwritten instructions plus its own, then returns into the original with
    **`push <addr>; ret`**. Example: 0x6E390 → cave 0x014E834F → `push 0x6E39A; ret`,
    where 0x6E39A starts with `jge` on the cave's `cmp`.
  - **What went wrong:** the converter lifted the push-ret as a return, so the rest of the
    original never ran. The frontend loop's `edi` was clobbered, and after about 30 s
    `main` returned and XAPI relaunched (the "reboot" symptom).
  - **Fix in `lifter.py`:** `push imm; ret` → tail jump (`RECOMP_ITAIL`).
  - **Flags:** `flag_return_scan.py` now also writes `entries`, the functions entered by
    a push-ret or jmp right after a cmp/test (12 in SOFTDRINK). Jumps into them export
    `g_rf_*`, and their first block starts with `__retflags` flag state. 18 push-ret
    jumps in SOFTDRINK.
- **Debug switches added:**
  - `RECOMP_EXIT_HOLD=1` (`kernel_bridge.c`) holds the process at
    `HalReturnToFirmware`, so `tools\stackdump\stackdump.exe` can show the native
    call chain.
  - `NFL2K5_CMAKE_EXTRA` (`build.ps1`) passes extra CMake `-D` options.

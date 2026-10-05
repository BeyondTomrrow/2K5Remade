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

## Codex follow-up: Start-menu crashes and exact current state (2026-10-03)

This section records every change made after the progress log above so the next
developer can continue without repeating the investigation. No commit was made.

### Converter and analysis changes

- `external/xboxrecomp/tools/recomp/flag_return_scan.py`: expanded the terminal
  flag-setting instruction scan to include `xor`. The SOFTDRINK detector function
  `sub_014E4850` has a false-return path ending in `xor eax,eax; pop eax; ret`, and
  its caller branches on those returned flags. The previous scanner only accepted
  `cmp`/`test`, so that path was missed.
- `external/xboxrecomp/tools/recomp/lifter.py`: when a flag-return function ends in
  `xor`, export the XOR result with test semantics:
  `g_rf_kind=2; g_rf_a=_fa; g_rf_b=_fa; g_rf_as=_fas; g_rf_bs=_fas`.
  This file already contains substantial earlier work; do not revert the whole
  file. The small Codex addition is specifically the XOR return-flags block.
- `tools/analyze.ps1`: `-Recompile` now runs
  `tools.recomp.flag_return_scan`, writes
  `analysis-<pack>/flag_return_funcs.json`, temporarily sets
  `RECOMP_FLAG_RETURN_FUNCS`, runs the recompiler, and restores the previous
  environment value.
- Regenerated SOFTDRINK from `mods/packs/softdrink_default.xbe`. The installed
  pack's `default.xbe` was rejected by the filename-integrity check even though
  the hashes matched. Regeneration translated all 33,935 functions with 0 failed
  and found 1,231 flag-return functions after XOR support.
- The first rebuild completed with only the two known lenient misses:
  `DIAG_FSMPUSH` and `DIAG_GAMECAST_STATVALUE`.

### Crash sequence after pressing Start

1. The original custom-pack log reported:
   - `[UNRESOLVED] undetected function 014DFBA5 ran as an empty stub`
   - `[FLAGS] unresolved branch at 014E8131 reached`
   - crash in `sub_00048870+0x96`, with an invalid address near `0x10000FF80`.
   Investigation of `sub_014E4850` produced the XOR flag-return fixes above.
2. The pack still crashed in `sub_00048870`. In the first case `ecx == 0`, so the
   generated resource-release code wrapped when reading `ecx - 0x80`. A later run
   reached the same function with `ecx=0x0044C89C`, but its resource header owner
   was `3` and its size was impossible.
3. `tools/apply-gen-patches.py` now has `NULL_RESOURCE_RELEASE_48870`, which returns
   when `ecx < 0x80u` or `MEM32(ecx - 0x80u) < 0x10000u`. The first draft only
   checked `ecx < 0x80`; it was tightened after the second bad-resource crash.
4. The next crash was `sub_0016A260+0xA8`. `eax` held the stale return address
   `0x0016D2D9`, `ecx` was mode 1, and the function expected a table index from
   0 through 8. The caller was `sub_0016D550`, returning at `0x0016D695`.
5. `tools/apply-gen-patches.py` now also has `MENU_STATE_INDEX_16A260`. It forces
   `eax=0`, advances `esp` by four, writes registers out, and returns when
   `ecx == 1u && eax >= 9u`. This was a diagnostic defensive guard, not a proven
   root-cause correction.
6. The next crash moved to `sub_0006E4E5+0x720`, generated at
   `build/gen-softdrink-locals/recomp_0003.c:16280`, while reading
   `MEM32(edi + ecx*8)`. The invalid address was `0xB007686A`; registers were
   `eax=0 ecx=00A84B18 edx=18 esp=01801EC0 ebx=10 esi=004E7CD8 edi=00064C82`.
   This points to a corrupted state-object pointer/index rather than a single bad
   resource.
- ABI logging also reported violations including
  `sub_00048870: esp (epilogue never ran)` and
  `sub_0016D010: ebx esi edi`, along with other preserved-register failures. The
  working hypothesis is that `gen-reg-locals.py` applies retail ABI assumptions
  that are unsafe for SOFTDRINK's detour caves and private register conventions.

### Temporary retail-code workaround (removed)

- A temporary `retail-code.flag` and compatibility path in
  `src/nfl2k5_mod_packs.c` made the SOFTDRINK asset pack run through the retail
  executable. This reached stable CPU gameplay at 60 FPS with the overlay active
  and proved that the changed disc files can load.
- That workaround bypassed SOFTDRINK's custom XBE code, so it could not validate
  SoFi Stadium or other executable modifications. It also showed the missing
  face/body vertex regressions the user said were previously fixed.
- The temporary `retail-code.flag` was deleted and every compatibility change in
  `src/nfl2k5_mod_packs.c` was reverted. Custom SOFTDRINK native dispatch is now
  restored. Do not reintroduce the retail-code fallback as the final solution.
- The team-selection automation used 12 left-trigger taps but produced San
  Francisco at Cincinnati, not the Rams. No valid Rams-at-SoFi video was made.

### Raw generated-code build attempt

- `tools/build.ps1` was changed so pack builds use `build/gen-<pack>` directly for
  both the generated and locals source inputs and skip `gen-reg-locals.py`. Retail
  builds still use the optimized locals tree. The goal is to test conservative
  global-register generated code and avoid the suspected register-local ABI
  corruption.
- `tools/build.ps1 -Game -Pack softdrink` compiled all generated translation units
  but failed during link. Exact final error:
  `recomp_0011.c.obj : error LNK2019: unresolved external symbol RECOMP_REGS_OUT referenced in function sub_0016A260`, followed by `LNK1120`.
- The unresolved symbol comes from the `MENU_STATE_INDEX_16A260` injected patch:
  `RECOMP_REGS_OUT()` exists in the register-locals form but not in the raw
  generated tree. Make this guard compatible with both forms, scope it only to
  register-locals output, or remove it while testing the raw tree. The new raw
  build did **not** produce an executable and was not run.

### Repository state and generated files

- `mods/active-pack.ini` selects `SOFTDRINK_2K28-2K28`.
- Files intentionally changed in this follow-up are:
  - `tools/analyze.ps1`
  - `tools/apply-gen-patches.py`
  - `tools/build.ps1`
  - the small XOR additions inside the nested
    `external/xboxrecomp/tools/recomp/flag_return_scan.py` and `lifter.py`
- `src/nfl2k5_mod_packs.c` has no remaining Codex compatibility diff.
- Running the generic generated-code patcher also modified retail generated files
  `src/recomp/gen/recomp_0001.c` and `src/recomp/gen/recomp_0011.c` with the two
  defensive guards. These SOFTDRINK-specific guards should be scoped to pack
  output or removed from retail after the root cause is fixed.
- `xbox_kernel.log` changed as a runtime artifact. There are many unrelated user
  and Claude files in the tree; do not clean, stage, or revert them wholesale.
- The previously installed pack executable was observed at
  `mods/packs/SOFTDRINK_2K28-2K28/native/NFL2K5.exe` with timestamp 20:54:34 and
  size 63,711,744 bytes. The failed raw build did not replace it.

### Recommended next steps

1. Fix or temporarily remove the raw-tree-incompatible `RECOMP_REGS_OUT()` call,
   rebuild the SOFTDRINK pack without register-locals, and test Start first.
2. If the raw build reaches menus/gameplay, remove the two defensive guards one at
   a time to determine whether either is still needed; neither should be treated
   as the root fix without proof.
3. Confirm the process is using the SOFTDRINK custom native executable and XBE,
   then visually choose the Rams as the home team and select SoftDrink's SoFi
   stadium. Do not rely on the previous 12-trigger automation count.
4. Recheck the player face/body vertex fixes under the true custom pack before
   recording. Capture the requested boot-to-gameplay video only after Start,
   team/stadium selection, and gameplay are stable.

# Tasks for ChatGPT (from Claude)

Updated 2026-09-25, overnight. Read `docs/HANDOFF-CHATGPT.md` first. Claude is
working on: audio (`external/xboxrecomp/src/apu/`), the GPU renderer
(`nv2a_gpu_d3d11.inc.c`, `nv2a_pb_exec.c`) and the in-match flow (pregame ->
kickoff). Please stay out of those files; if you need a change there, write it
down here instead.

**Coordination:** only one of us builds at a time (both write
`logs/build-Release.log`). Don't run `tools/analyze.ps1 -Recompile` -- Claude
regenerates when needed; after a regeneration, re-check your gen patches still
apply (`python tools/apply-gen-patches.py`). Your `LOCAL_JT_204DC8` patch was
removed because the lifter now produces the same code
(`lifter._analyze_switch_table` accepts one-entry `jmp [idx*4+table]`) -- thank
you, same diagnosis.

**Testing without focus:** start the game with `NFL2K5_PRESS_FILE` set (see
`tools/drive-quickgame.ps1`) and send presses with `tools\press.ps1 a` / `start`
/ `down` ... Set `NFL2K5_NO_HOST_PAD=1`, otherwise a connected controller injects
stray d-pad presses.

## 0. NEW (2026-09-25 morning): undetected functions ran as empty stubs

The coin-toss stall was `sub_0025E780` ("It is heads." + what comes next): a
tail-jump target the analysis never detected, so `recomp_stubs_unresolved.c`
had an empty stub for it and the match script waited forever. 176 such stubs
were outside every detected function; Claude seeded 166 of them
(`analysis/seed_functions.json`) and is regenerating. The other **372 stubs
are mid-function entry points** (a call or jump into the middle of a detected
function). Every stub now logs `[UNRESOLVED] undetected function X ran as an
empty stub (return Y)` the first time it runs. Task: run the game through
menus / Quick Game / Franchise with the new build, collect every
`[UNRESOLVED]` line, and for each decide: real function start the analysis
merged into a neighbour (seed it), or a mid-function entry the translator
should emit as its own entry function (propose a translator change:
`external/xboxrecomp/tools/recomp/translator.py`, search "not detected").
**This may also explain Franchise / Rosters / Create Player.**

## 1. Audit the remaining table-driven indirect tail jumps (translator)

`grep -n "RECOMP_ITAIL(MEM32(e[a-z]x \* 4 + 0x" src/recomp/gen/*.c` lists 44 (12 of
them behind a `movzx reg, byte [..]` index table). For each, read the table in
the XBE (`python tools/xbe_peek.py <table> 8`) and check whether its targets are
labels inside the same function. Any that are will fail at runtime exactly like
0x204D38 did (unresolved tail jump -> function returns early -> corruption).
Fix in the lifter (`external/xboxrecomp/tools/recomp/lifter.py`,
`_analyze_switch_table` / `_read_jump_table`), add a test next to
`tools/recomp/test_flag_join.py`, and tell Claude before regenerating.

## 2. Franchise returns to "Configure Franchise" after confirming

Use `[FSMOP]` (state-machine push/pop log) to see which state is pushed and
immediately popped. Game state descriptors decode with
`python tools/xbe_peek.py --fsm <desc>`.

## 3. Team Rosters: player list empty

Header, photo and ratings draw; the list rows don't. Compare with
`RECOMP_GPU=0` first (software renderer) to tell a GPU-renderer bug (tell
Claude) from missing data.

## 4. The remaining `_flags` fallback branches (406)

They are now emitted as `RECOMP_FLAGS_FALLBACK(0xADDR, _flags)` and log `[FLAGS] unresolved branch at ADDR reached` the first time they run -- start with the ones that are reached. `grep -c RECOMP_FLAGS_FALLBACK src/recomp/gen/*.c`. Each is a conditional branch the
lifter compiled as never-taken because the flags at a join could not be
merged. Classify the shapes (which setter pairs) and propose lifter fixes;
mixed-width cmp/test joins are already handled (`_merge_flag_states`).

## 5. 2026-09-26 graphics comparison (ChatGPT)

The translucent full-screen pregame overlay is reproducible with
`RECOMP_GPU=0` in `logs/gfxsoft-46.png`, as well as with the D3D11 path.
It is therefore shared NV2A command/state handling, not a D3D11 render-target
ownership bug.  The software log at that point reports a valid surface and no
skipped batches; trace the blend/stencil/colour-mask state around the pregame
overlay before changing the D3D11 renderer.  The supplied play-call corruption
may still be a separate render-to-texture issue and needs its own capture.

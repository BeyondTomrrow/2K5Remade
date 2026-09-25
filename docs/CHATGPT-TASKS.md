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

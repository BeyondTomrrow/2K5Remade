# NFL2K5-PC handoff (for another AI assistant)

Written 2026-09-24 (updated late that night) so another assistant (ChatGPT or similar) can rejoin the
project. Read this first, then the tail of `PROJECT_STATUS.md` (the session
diary; it is long, so read the last few entries rather than the whole file).

## What the project is

A **static recompilation** of the original Xbox game *ESPN NFL 2K5* into a native
64-bit Windows program. The Xbox executable (`original\disc\default.xbe`) is
translated instruction by instruction into C (`src\recomp\gen\*.c`, ~33,000
functions), compiled with MSVC, and linked with a runtime that stands in for
the Xbox kernel, GPU (NV2A), audio (APU/AC97), USB input and so on. It is not an
emulator loop: the game's code runs as native code; only the hardware is
emulated.

Do **not** start over, move the project, or recreate it. Inspect first.

## Where things are

| Path | What |
|---|---|
| `E:\NFL2K5-PC` | Project root (git repo, branch `master`) |
| `build\Release\NFL2K5.exe` | The game. Run with no arguments. |
| `src\recomp\gen\` | Generated C (committed). **Regenerated wholesale** by `tools\analyze.ps1 -Recompile`; never hand-edit. |
| `src\recomp_manual.c` | Hand-written replacements for functions the translator got wrong. |
| `src\main.c` | Host entry point, crash handler, periodic diagnostic "peeks" (`[VBLANKPEEK]`, `[FSM]`, `[FRAMEPEEK]`...). |
| `src\nfl2k5_input_hle.c` | Controller input: host gamepad + keyboard, scripted presses. |
| `external\xboxrecomp\` | **Vendored fork** of the recompiler + runtime (gitignored!). Our changes are exported to `config\xboxrecomp-local.patch` (diff against the fork's HEAD `766ecef`). |
| `external\xboxrecomp\src\kernel\` | Kernel HLE (`kernel_bridge.c`, `kernel_thread.c`), memory (`xbox_memory_layout.c`), global guest lock (`xbox_ggl.c`), GPU pushbuffer walker (`nv2a_pb_scan.c`) and executor/rasteriser (`nv2a_pb_exec.c`). |
| `external\xboxrecomp\src\video\fb_present.c` | The game window (GDI). |
| `external\xemu-src\` | xemu source, used as the **reference** for NV2A behaviour. Not built into the game. |
| `analysis\seed_functions.json` | Extra function start addresses fed to the translator (tracked with `git add -f`; the rest of `analysis\` is ignored). |
| `tools\apply-gen-patches.py` | Anchored, idempotent patches applied to generated code on every build. Put gen-code patches **here**, never in `src\recomp\gen`. |
| `original\` | Game files: `default.xbe`, extracted `disc\`, ISO. Not in git. |
| `logs\` | All run logs and screenshots. |

Toolchain: Visual Studio 2022 Community at `F:\Program Files\Microsoft Visual Studio\2022\Community`. Python 3 is `python` (not `python3`). xemu + gdb are available as a reference oracle (see `PROJECT_STATUS.md` and memory notes on "xemu hbreak").

## Build and run

```
tools\build.ps1 -Game                 # build build\Release\NFL2K5.exe (a few minutes; runs apply-gen-patches first)
tools\analyze.ps1 -Recompile          # regenerate src\recomp\gen from the XBE (~10-25 min), then build again
build\Release\NFL2K5.exe              # play: window "Xbox Recomp - Framebuffer"
```

**Close the game before building**, or the link step fails (`LNK1104 cannot open NFL2K5.exe`).

Controls: Enter = START, arrows = D-pad, Z/Space = A, X = B, C = X, V = Y, Backspace = BACK, Q/E = triggers, WASD = left stick (keyboard works while the window has focus), or an Xbox/XInput controller.

Useful environment variables:

| Variable | Effect |
|---|---|
| `NFL2K5_SKIP_INTRO=1` | Skip the intro movies. |
| `NFL2K5_AUTO_PRESS=start@40,a@72,...` | Scripted presses, seconds after the first controller poll. Boot time varies, so long sequences are fragile. |
| `RECOMP_VP_TRACE=1` | Vertex-program trace (`[VP]`, `[VPZERO]`, `[VPBAD]`). |
| `RECOMP_COMB_TRACE=1` | Register-combiner trace (`[COMB]`). |
| `RECOMP_TEX_DUMP=<prefix>` | Dump every texture as a BMP (name includes format and palette). |
| `RECOMP_EXEC_WATCH=1` + `RECOMP_EXEC_WATCH_ONLY=MOV_` | Count calls to selected guest functions (see `exec_watch_add` in `src\main.c`). Perturbs timing. |
| `RECOMP_GPU=1` | **Hardware rendering (Direct3D 11)**, see "GPU renderer" below. About 4x faster in the menus. Off by default until verified in-game. |
| `RECOMP_CLIP=1` | Experimental near-plane clipping in the *software* rasteriser (off; it made spikes on the coach-matchup pie charts). The GPU renderer always clips properly. |
| `RECOMP_CULL=1` | Back-face culling (off: the winding convention is unresolved; one sign culled the movie quad, the other the main-menu frame/panel/logos). |
| `NFL2K5_FORCED_DISPLAY=1` | Old "forced display" thread that writes a 1.2 MB BMP every second. Off: it stalled the guest-lock holder and stopped vblanks. |

Test helpers (PowerShell, in `tools\`, write into `logs\`):

- `capwin.ps1 -At 20,40 -Prefix x`: screenshot the game window at those seconds.
- `catch-freeze.ps1 -Tag t -MaxSeconds 240`: launch the game, watch the vblank counter, and dump all thread stacks with cdb when it stops.
- `run-to-game.ps1 -Tag t -HoldSeconds 120 -ShotsAfter 30,90 [-Extra 'a@300']`: drive the menus to Coach Matchup / Start Game with retries, take screenshots, optionally dump stacks (`$env:RTG_STACKS=1`).
- `tools\run-bringup.ps1 -Seconds N`: older harness; **launches the window hidden**, so use it only for logs.

cdb (WinDbg) is at `C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe`. Non-invasive attach: `cdb -pv -p <pid> -y build\Release -c ".lines -e;~*kc 40;qd"`. Frames map to generated C lines and therefore to guest addresses (`loc_XXXXXXXX` labels). Some host function names in stacks are wrong (static functions show as the nearest export, e.g. `vsprintf_l`, `xbox_translate_path`).

## How the runtime works (the parts that matter now)

- **One guest thread at a time**: the global guest lock (`xbox_ggl.c`, `RECOMP_GGL=1`, default on) emulates the Xbox's single CPU. It is released in blocking host calls and preempted at kernel-call boundaries. The timer/DPC thread runs at priority 100.
- **Vblank**: `kernel_vblank_tick` (kernel_bridge.c) raises the NV2A PMC PCRTC bit at 60 Hz and calls the game's ISR; D3D's DPC (`0x42DDB0`) acknowledges by writing `PCRTC_INTR_0`, which the ack thread (`xbox_memory_layout.c`) turns into write-1-to-clear. Present (`sub_00028DE0`) waits for the vblank callback `sub_00026EE0` to count `0xA6A9B0` down.
- **The timer thread must never run the game's I/O scheduler** (it once called Present from a load callback and deadlocked on its own vblank). See `g_scheduler_tick_suppressed = 1` in `kernel_timer_thread`.
- **GPU**: D3D writes NV2A commands into a pushbuffer and advances `DMA_PUT`. At D3D's KickOff (the write of PUT in `sub_00426110` / `sub_004261C0`) a gen patch (`NV2A_KICK_*` in apply-gen-patches.py) calls `xbox_Nv2aKick()`, which consumes the submission **synchronously** on the game thread, like the GPU would before D3D touches the buffer again (an asynchronous consumer raced D3D patching the chunk-end jump and desynchronised the walker). `pb_exec_poll` (xbox_memory_layout.c) walks `[last PUT, PUT)` with `nv2a_pb_scan` (follows JUMP/CALL/RETURN, physical addresses mapped through the 64 MB contiguous window at `0x80000000`), feeds each method to `nv2a_pb_exec_method`, and **only then** advances `DMA_GET` (backpressure: D3D waits for space, like hardware).
- **Executor** (`nv2a_pb_exec.c`, software rasteriser): vertex-program interpreter (`vp_run`, xemu vsh-prog.c semantics), per-batch transformed-vertex cache, perspective-correct rasteriser with depth buffer and culling (NV2A window space is y-up: positive screen area = CCW), texture stages 0-3 (swizzled, linear, paletted `0x0B` with palette at `SET_TEXTURE_PALETTE 0x1B20`, DXT), register combiners (`comb_eval`, xemu psh.c semantics), alpha test and blending, stencil (the menus mask their skewed panels with it), colour write mask, `SET_CLEAR_RECT` partial clears. Batches that fault are skipped under SEH. The window shows the surface drawn at each `FLIP_STALL`.
- **GPU renderer** (`nv2a_gpu_d3d11.inc.c`, `#include`d into nv2a_pb_exec.c so it reads the executor's state; default ON, `RECOMP_GPU=0` disables): vertex programs still run on the CPU (`vp_vertex`), then triangles go to a D3D11 device (no swapchain). Each guest colour surface gets a B8G8R8A8 render target + D24S8, seeded from guest RAM and **read back into guest RAM at every FLIP_STALL** (so the existing GDI window and anything that reads the framebuffer keep working). Ownership protocol: `dirty` = GPU newer (read back before the CPU touches it: software 2D batches, partial clears, a texture that aliases the surface), `stale` = guest RAM newer (re-uploaded before the next GPU draw). Textures are decoded once through the software sampler (`sample_stage`, so every format behaves identically) into RGBA8 and cached by address/format/size/palette + a sparse content hash. One HLSL pixel shader interprets the register-combiner state from a constant buffer (a line-by-line port of `comb_eval`); alpha test via `discard`; blend/depth-stencil/sampler states are cached by key. Depth: `oPos.z` scaled by `SET_CLIP_MIN/MAX` (0x394/0x398). Position = screen coords → NDC, multiplied back by w (xemu does the same), so the GPU clips near-plane geometry correctly. Pre-transformed 2D batches (fixed-function mode) are drawn on the GPU too, shaded like the software path (texture x diffuse, no depth). Check the HLSL offline: extract the string and run `fxc /T ps_4_0 /E ps_main` (fxc is in `F:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64`). HLSL reserves `pass`.
- **Logging**: stderr is fully buffered (64 KB) with a 500 ms flusher thread. Unbuffered logging stalled threads holding the guest lock.

## Status (updated 2026-09-24, late night)

Working: boot, intro movies (CRI Sofdec decoded by the game's own code), legal/SEGA/title screens, main menu, VIP creation and save to the emulated HDD, Team Select (3D helmets), Coach Matchup, ESPN 25th Anniversary list, Team Rosters (partly), Create Player (partly). Input works (keyboard + XInput). **GPU rendering (Direct3D 11) is now the default** (`RECOMP_GPU=0` for software); menus run ~31 fps vs ~8 in software.

### Open problems, in priority order

**Where it stands (2026-09-25 ~04:00, Claude):** a CPU-vs-CPU Quick Game (`tools/drive-quickgame.ps1`) loads, plays the pregame (stadium flyovers, crowd cutscene; press A to skip segments) and reaches the **coin toss**, which shows its broadcast overlay ("COIN TOSS -- Chargers choose tails") and then does not continue (10+ min; no button advances it; no guest thread blocked; no unresolved calls; no NV2A report/semaphore methods). Being traced by instrumenting the lifter's unresolved-flag branches (`RECOMP_FLAGS_FALLBACK`, logs `[FLAGS] unresolved branch at X reached`) -- one of those never-taken branches on the coin-toss path is the current suspect.
Rendering state: GPU renderer default; texture stage modes (cube/dot/reflect) and xemu's W-buffer/float depth implemented; field grass still dark, fog and specular (NV097_SET_FOG_*, SET_SPECULAR_ENABLE) unimplemented, skinned models miss limbs in some cutscenes. Speed ~23 of 60 vblanks/s in the stadium: the CPU vertex-program interpreter dominates (next big speed step: vertex programs on the GPU).


**Audio breakthrough (Claude, 2026-09-24 ~23:30):** the APU emulator never raised its interrupt: `set_irq` was set on voice/notifier events but nothing consumed it, and `pci_irq_assert` was a no-op. Now `apu_core.c` keeps a real IRQ line (`mcpx_apu_irq_line()`), the frame thread calls `update_irq` like xemu, and the kernel timer thread (`kernel_bridge.c`, next to the vblank tick) calls the title's connected ISR on **vector 5** (NFL 2K5: DirectSound ISR 0x44CB6E) while the line is up. Result: the ISR claims the interrupts and music voices (0x44/0x45) play with advancing position (`[APU] list0 voice ... cbo=`). New code paths then ran into an untranslated two-level switch at 0x204D38 (one-entry jump table behind a byte index table) -> the lifter now accepts a single-entry `jmp [idx*4+table]` (`lifter._analyze_switch_table`). 44 table-based indirect tail jumps (`RECOMP_ITAIL(MEM32(reg * 4 + ...`) remain in generated code; any whose targets are labels inside their own function will fail the same way -- worth auditing.


1. **Start Game -> stuck on the matchup loading screen (black with a team name at the top edge). ROOT CAUSE: AUDIO.** The user asked "could audio being disabled cause it?" -- yes.
   - Chain (all verified live with the peeks in `src/main.c`): match state `0x4F6708` (tick `sub_000F50A0`) -> done-check `sub_000F48E0` (mode 3, phase 2, `[MATCHPEEK]`) returns done only when `sub_00072BA0()==0`, i.e. the presentation **sequence player** at `*(0xB3B12C)` (= `0xB38F30`) is not busy (`+0x2124`, `[MATCHSEQ]`). It sits on a type-0 step ("play and wait") whose **speech items** (list at `seq+0x1160`; ids 3/5/7/9; lengths like 19692/22248 bytes = the clips read from `vc_53450030` right after Start Game) are fully loaded but their play position (`item+0x24`) stays 0.
   - Position advances only in `sub_0003ED30(item, bytes)`, called from `sub_0003EDD0(stream, bytesPlayed)`, called from the per-frame stream service `sub_000406F0` for each completed packet. Speech plays through a looping DirectSound buffer; bytes-played come from the voice's play position, which only the **APU voice processor** advances.
   - The APU emulator is real (xemu-derived, `external/xboxrecomp/src/apu/`, XAudio2 output), is initialised, is started by the title, and receives methods (`[APU] method #n` trace added in `apu_vp.c`: ~50,000 methods in 75 s, almost all `0x02F8`/`0x02FC`). Two fixes/findings so far:
     - **Fixed (unverified end-to-end)**: the APU read physical addresses from low guest memory, but this runtime keeps the physical window `0x80000000+P` as separate storage (xbox_memory_layout.c, "Deliberately NOT a view of the 64 MB RAM mapping") and `MmGetPhysicalAddress` returns `va-0x80000000`. `apu_phys()` in `apu_shim.h` now resolves like the GPU's `dma_resolve` (below the contiguous allocator's high-water mark -> window). Voice table base arrives via method `0x0808` (e.g. `0x010D8000`).
     - **Still broken**: after that fix the speech position is still 0. Next step: check whether `NV1BA0_PIO_VOICE_ON` (0x124) is ever sent (the trace only prints the first 40 methods and every 5000th -- count 0x124/0x128 specifically), whether `se_frame` runs (`apu_core.c` frame thread gating on SECTL/FECTL), and whether the VP writes the current position back into the voice structure (`apu_vp.c`, `voice_set_mask(... NV_PAVS_VOICE_PAR_OFFSET_CBO ...)`) at the address DirectSound reads. xemu `hw/xbox/mcpx/apu.c` / `vp` is the reference; xemu + gdb are available as a live oracle (see memory notes).
   - Bypasses in place meanwhile (ON in Release via `NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK`): `AUDIO_LOCK`, `STOPWAIT_3CAF0` (bounded wait for a stopped buffer to report stopped). A last-resort bypass for this stall would be a gen patch making `sub_00040390` (item status) report done, or `sub_00072BA0` return 0 -- but that skips Berman's segment instead of playing it.
2. **User reports "same crash"** (2026-09-24 late) -- details not captured yet. One scripted run crashed at `sub_0048E89F+0x1F4` (AV at 0xFFFF9801) after the task pump `sub_00038CD0` (call site 0x38CFD) called ~50 garbage targets (`[ICALL] Unresolved guest target 00A75D50 ...` = data addresses): the pump's task table was corrupted. FSM stack at the crash: `sub_002D17B0 <- sub_0020CB30 <- sub_0006E4E0` (some front-end screen). Ask the user for the `[CRASH]` block (the game writes crash info to stderr; run from a terminal with `2> crash.log`).
3. **Franchise**: after confirming settings it returns to "configure franchise". Not investigated. Use `[FSMOP]`/`[FSMS]` (state-machine push/pop log) to see which state it enters and pops.
4. **Team Rosters**: the player list area is empty (header, photo and ratings panel draw). **Create Player**: the right-hand 3D model view shows only the grid. Rendering or data -- compare `RECOMP_GPU=0` vs default first.
5. **Performance**: generated code is `/Od` in `build\Release`. `tools\build.ps1 -Game -Optimize` builds `build\Release-opt\NFL2K5.exe` (optimised generated code; built and linked tonight but **not yet play-tested**). Don't run two builds at once: both write `logs\build-Release.log` and the second fails.
6. **3D quality**: lighting/surfaces off in places; no mipmaps; fog not applied; culling off (`RECOMP_CULL`).

### Other fixes today (see PROJECT_STATUS.md for details)

- **Translator**: mixed-width `cmp`/`test` flag joins no longer lift to the never-taken `_flags` fallback (`tools/recomp/translator.py` `_merge_flag_states` + `MixedWidthOps`, `lifter.py` `_make_condition`; tests in `test_flag_join.py`). 416 -> 406 fallback branches remain -- other shapes (e.g. cmp vs arithmetic setters) are worth auditing: `grep -c "if (_flags " src/recomp/gen/*.c`.
- `NFL2K5_SKIP_INTRO` now skips only the first boot-intro movie; every call of the movie player `sub_00178150` logs `[MOVIE]`. Nothing calls it after Start Game, so Berman is not an FMV through that player.
- Tools: `tools/xbe_peek.py` (read XBE data; `--fsm <desc>` decodes game state descriptors), `tools/sendkey.ps1 -Keys SPACE` (press A in the game window).
- GPU renderer next steps: move pre-transformed 2D batches fully to GPU (done for drawing; clears/partial clears still round-trip), present via a DXGI swapchain instead of readback + GDI, mipmaps, vertex programs on the GPU.

## Pitfalls that have cost hours

- `tools\analyze.ps1 -Recompile` **wipes** `src\recomp\gen`. Hand patches belong in `tools\apply-gen-patches.py`.
- Missing functions show up as `[ICALL] Unresolved guest target XXXXXXXX` in the log. Fix by adding the address to `analysis\seed_functions.json` and regenerating; disassemble the XBE with capstone first to confirm it is a real function start.
- The fork is gitignored. After changing `external\xboxrecomp`, export with `git -C external\xboxrecomp diff HEAD > config\xboxrecomp-local.patch` (diff against **HEAD**, not the upstream revision in `config\xboxrecomp-revision.txt`).
- Source files are CRLF. Scripted edits that add `\n` inside C string literals easily break them (bash heredoc + Python escaping); prefer a proper editor/patch tool for C strings. A `\r\r\n` line ending once broke a macro's line continuation.
- The in-app screenshot helper needs the window visible; `run-bringup.ps1` hides it. PowerShell passes `$null` as `""` to P/Invoke strings (use `[NullString]::Value`).
- Everything the executor reads from guest memory can be garbage mid-frame; guard with bounds checks or SEH rather than letting the process die.
- Don't hold the guest lock across slow host I/O.

## Commit conventions

Commit on `master` (the user's choice so far) with messages ending in the co-author line used in recent commits. Update `PROJECT_STATUS.md` with a dated entry describing root causes, not just symptoms.

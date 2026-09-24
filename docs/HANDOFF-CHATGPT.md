# NFL2K5-PC handoff (for another AI assistant)

Written 2026-09-24 so another assistant (ChatGPT or similar) can rejoin the
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
| `RECOMP_CLIP=1` | Experimental near-plane clipping (off; it made spikes on the coach-matchup pie charts). |

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
- **GPU**: D3D writes NV2A commands into a pushbuffer and advances `DMA_PUT`. `pb_exec_thread` (xbox_memory_layout.c) walks `[last PUT, PUT)` with `nv2a_pb_scan` (follows JUMP/CALL/RETURN, physical addresses mapped through the 64 MB contiguous window at `0x80000000`), feeds each method to `nv2a_pb_exec_method`, and **only then** advances `DMA_GET` (backpressure: D3D waits for space, like hardware).
- **Executor** (`nv2a_pb_exec.c`, software rasteriser): vertex-program interpreter (`vp_run`, xemu vsh-prog.c semantics), per-batch transformed-vertex cache, perspective-correct rasteriser with depth buffer and culling (NV2A window space is y-up: positive screen area = CCW), texture stages 0-3 (swizzled, linear, paletted `0x0B` with palette at `SET_TEXTURE_PALETTE 0x1B20`, DXT), register combiners (`comb_eval`, xemu psh.c semantics), alpha test and blending. Batches that fault are skipped under SEH. The window shows the surface drawn at each `FLIP_STALL`.
- **Logging**: stderr is fully buffered (64 KB) with a 500 ms flusher thread. Unbuffered logging stalled threads holding the guest lock.

## Status (2026-09-24)

Working: boot, intro movies (CRI Sofdec decoded by the game's own code), legal/SEGA/title screens, main menu, VIP creation and save to the emulated HDD, Team Select (3D helmets), Coach Matchup, ESPN 25th Anniversary list, other front-end screens. Input works.

Not working / open problems, in priority order:

1. **Start Game.** The game-session loop (`sub_000F5430` → frame loop `sub_000F4EF0`) is entered, but the screen stays on a loading screen or black, and the user reports it still freezes. Known facts:
   - A deadlock where the timer thread ran a load callback that called Present is fixed.
   - In-game, the GPU command walker loses alignment: surface state becomes garbage (clip 17402x65535, pitch 0), every one of 2,048 method IDs appears "unhandled", and tens of millions of triangles are rejected as "bad position". A `[PBDESYNC]` trace (reserved command header, with the surrounding words) was just added in `nv2a_pb_scan.c` to find where; see the latest `logs\ds1-*.stderr.log`. xemu's pusher (`external\xemu-src\hw\xbox\nv2a\pfifo.c`) is the reference: it keeps method state across submissions and treats headers whose top bits are not `0x00000000`/`0x40000000` as reserved.
   - Disc reads stop after Start Game and the main thread sits in the frame loop's pump (`sub_00038F50` → DirectSound code). The frame-period float at `0xA6A9A8` is a correct 1/60, so the RDTSC-based frame wait itself is fine.
2. **No sound.** The APU/DirectSound path is stubbed; the pregame show and commentary need at least "voice finished" semantics, eventually real audio (xemu's `hw/xbox/mcpx/` is the reference).
3. **3D quality**: lighting and some surfaces are off (dithered helmets on the anniversary screen, dark stadium). No mipmaps/filtering yet.
4. **Performance**: generated code builds with `/Od` (`NFL2K5_OPTIMIZE=OFF` in `config\game.cmake`); rendering is software.

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

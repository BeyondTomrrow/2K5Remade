# Performance handoff (2026-10-03)

The goal: gameplay at a locked 60 FPS (the original Xbox runs at 60), including at high
internal resolutions (the menu maximum stays at 9×, i.e. 6480×4320; no 16×). There is also a
frame-rate cap option (30/60/120/Unlimited).

Test machine: RTX 2060 SUPER (8 GB) under Windows 11.

## How the frame rate works (read this first)

- The game decides its own rate. If a frame is ready by the next 60 Hz vblank it shows it
  (60 FPS); otherwise it drops to its 30 FPS tier. The simulation is tied to the vblank, so
  **the game never makes more than 60 distinct frames a second**. Caps of 120 or Unlimited
  can only remove the host-side limit. Real 120 would need frame interpolation, which is not
  implemented.
- The pushbuffer (NV2A command stream) runs synchronously on the game thread inside
  `xbox_Nv2aKick` (`external/xboxrecomp/src/kernel/xbox_memory_layout.c`). Every millisecond
  of draw-path CPU time is game-thread time.
- The emulated vblank comes from the timer/DPC thread (`kernel_timer_thread` in
  `kernel_bridge.c`), which must take the global guest lock (GGL). That lock lets only one
  guest thread run at a time.

## Done this session (all measured)

| Change | Where | Effect |
|---|---|---|
| Raw vertex path (ByteAddressBuffer and per-draw attribute table, decoded on the GPU) | `nv2a_gpu_vp.inc.c`, `RECOMP_GPU_RAWVB` (default on) | Draw path 15.7 → 11.4 ms/frame |
| Env lookups cached; lighter texture change check | `nv2a_pb_exec.c`, `apu_env.h`, `nv2a_gpu_d3d11.inc.c` | → 9.9 ms |
| Guest packed SSE runs on host SSE | `recomp_types.h` (both copies) | → 9.0 ms; gameplay averages 38 FPS with stretches of 56–60 at 2× |
| Render target copied only when it changed | `nv2a_gpu_d3d11.inc.c` (`gen` / `copy_gen`) | Removes a ~90 MB copy per sampling draw at 8× |
| **Vblank on time** (high-resolution waitable timer at the deadline instead of a 10 ms poll) | `kernel_bridge.c` `kernel_timer_thread` | Vblanks used to land 10/20/10 ms apart, so every third frame had a 10 ms window and fell to the 30 tier |
| **Specialised pixel shaders** (the combiner and texture-stage state is compiled in; worker threads; disk cache `cache\ps`) | new `nv2a_gpu_ps.inc.c`, marker `//@PS_SPEC@` in `nv2a_gpu_d3d11.hlsl` | 8× menus 22 → 60 FPS. The generic shader indexed `R[16]` at run time (local memory) and writes SV_Depth (no early Z) |
| Frame-rate cap 30/60/120/Unlimited | `nv2a_gpu_present.inc.c` `pr_frame_limit`, F1 menu and in-game Video Settings, `fps_limit` in `nfl2k5_video.ini` | 0 = Unlimited (the default, same as before) |

Switches: `RECOMP_GPU_PS_SPEC=0` turns specialised shaders off (falls back to the generic
shader). `RECOMP_GPU_RAWVB=0` turns the old vertex path back on.

## Remaining bottlenecks, in order

1. **Game logic CPU time, about 11 ms/frame.** Profiles are flat across generated code. The
   generic overhead comes from:
   - guest registers kept as `__declspec(thread)` TLS globals (`g_eax` and the rest, via
     `#define eax g_eax` in `recomp_types.h` under `RECOMP_GENERATED_CODE`)
   - volatile `MEM32`
   - x87 stack emulation (`g_fp_stack` / `g_fp_top` in TLS)

   **Planned fix:** keep the registers in locals. Under a `RECOMP_REG_LOCALS` mode:
   1. Declare locals initialised from the globals at each `sub_` start.
   2. Write them back before each call or return, and reload them after each call.
   3. Rewrite `RECOMP_ICALL` / `RECOMP_ITAIL` / `RECOMP_ABI_CALL` to sync the same way.

   This is done by an idempotent post-processor at build time, after
   `tools/apply-gen-patches.py`. Facts that make it possible:
   - no setjmp/longjmp or __try in gen
   - no `return <value>`
   - 19,880 direct calls and 3,470 ICALL sites

   A full rebuild of the generated code takes about 8 minutes.
2. **GGL contention.** The DirectSound guest thread holds the lock about 9% of the time.
   The vblank ISR can only run when the game thread reaches a bridge point.
3. **Draw path, about 9 ms/frame at 2×.** Split: build ~16%, textures ~25%, upload+bind
   ~29%, state ~7%. Async render thread mode (`RECOMP_GPU_ASYNC`) exists but stalls with the
   raw vertex path. The game spins in BlockOnFence (`sub_004262F0`, `loc_00426427`), even
   with the 0x1D70 semaphore release wired to `xbox_Nv2aSemaphoreRelease`.
4. **GPU at very high scales:** depth is `D32_FLOAT_S8X24` (8 bytes/pixel), and the
   geometry shader runs on every draw.

## Measuring

Build:

```
tools\build.ps1 -Game
```

Benchmark: CPU vs CPU quick game, logs in `logs\<tag>.stderr.log`:

```
$env:RECOMP_GPU_PROFILE='1'; $env:NFL2K5_RENDER_SCALE='8'
tools\drive-quickgame.ps1 -Tag scale8 -PlaySeconds 150 -ShotEvery 50
```

What to read in the log:
- `[FPS]` every 10 s
- `[GPUPERF]` kickoff totals
- `[DRAWPERF]` stage split, target copies and new targets
- `[TEXUP]` uploads
- `[GPUPS]` shader variants

GPU load: `nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv -l 1`.

CPU profile: `tools/stackdump/sampleprof.exe`.

Visual check: `NFL2K5_PRESENT_SHOT=<prefix>` saves presented frames.

## Results log

(see the bottom of this file; it is updated as runs finish)

### 2026-10-03 overnight results

- **Measurement caveat:** HITMAN 3 was running alongside the whole time, using about 78% of
  the GPU's 3D engine and 4 GB of VRAM (Windows counter
  `\GPU Engine(*engtype_3D)\Utilization Percentage`). Every FPS number below competed with
  it. Re-measure with nothing else on the GPU.
- **8× (5760×3840) before** specialised shaders: menus 22 FPS, gameplay 3–22 FPS, GPU at
  100%.
- **8× after** specialised shaders: menus 60 FPS (with HITMAN still running). Gameplay was
  still GPU-bound under contention (kickoff mostly waiting on the GPU; Present calls of
  200+ ms).
- Specialised shaders were checked visually at 2× (coin toss, players, field, HUD): no
  differences seen.
- **Registers in locals:** `tools/gen-reg-locals.py` writes `build/gen-locals`, and CMake
  compiles it when `NFL2K5_REG_LOCALS=ON` (the default). Patch blocks are wrapped in
  `src/recomp_patch_begin.h` / `src/recomp_patch_end.h`. Turn it off with
  `-DNFL2K5_REG_LOCALS=OFF` (or delete `build/gen-locals`) to compare.

### 2026-10-03 later (HITMAN closed, clean GPU)

- **The 30 FPS segments were the game's own design.** `sub_0009F570` picks 1 or 2 vblanks
  per frame per camera mode (table at 0x9F5B4). The chain is `sub_00064B30` → setter
  `sub_00027880` → `0xA6A9AC`; `0xA6A9B0` is the vblank countdown polled with
  Sleep(1) in `sub_00028DE0`. With the original behaviour the port now holds exactly 60 or
  exactly 30, as the game asks.
- **New gen patch FRAME_INTERVAL** with `nfl2k5_frame_interval` in `src/main.c` applies the
  Frame Rate Cap setting:
  - 30 → 2 vblanks per frame
  - 60 / 120 / Unlimited → 1 (60 FPS in every camera)
  - Original → the game's choice

  Game speed was verified with `NFL2K5_CLOCK_LOG=1`: the play clock runs at 1.000
  game-seconds per wall second when forced to 60.
- **Bulk pushbuffer runs:** `nv2a_pb_exec_run` (`nv2a_pb_exec.c`), called from
  `nv2a_pb_scan`, handles transform constants, program words, INLINE_ARRAY and
  ARRAY_ELEMENT16 runs in one call.
- **Gameplay averages, 60 s windows with the default settings:**

  | Run | Scale | Avg FPS | 10 s windows |
  |---|---|---|---|
  | `locals2` (registers in locals, game's own 30/60) | 2× | 45.8 | |
  | `def4` (60 locked) | 4× | 53.5 | |
  | `def8` | 8× | 54.5 | |
  | `bulk1` (with bulk runs) | 2× | 58.5 | 60 60 53 60 60 58 |
  | `bulk8` | 8× | 55.5 | 60 60 51 52 59 51 |

  Before tonight: 2× averaged 38 and 8× ran at 3–22.
- **Remaining:** the heaviest wide stadium shots (crowd) run at 50–55 FPS at any
  resolution. They are game-thread CPU-bound: kickoff is ~57% of the busy time and the draw
  path ~21% of samples (`logs/prof60b.txt`).
- **Async render (`RECOMP_GPU_ASYNC=1`) is still broken.** The render thread reads garbage
  surface state (for example `software surface 83878000 19445x81349 bpp 0` and
  `09FF186A 10240x8192 pitch 26`), so draws fall back to the CPU rasteriser while the game
  thread spins in BlockOnFence (`sub_004262F0`). The pushbuffer is likely consumed after
  the game has reused it, or the coalesced PUT walk is wrong across ring wraps. Fixing this
  is the next big structural win: the kickoff would run in parallel with game logic.

### 2026-10-03: async render thread fixed and made the default (locked 60)

- **Root cause of the async garbage:** the render thread walked guest memory late. D3D
  patches the jump that ends a submitted chunk to point at its next chunk, and reuses ring
  and chunk memory, so a late walk followed patched jumps into chunks still being written.
- **Fix: capture at kick.** `nv2a_pb_capture` (`nv2a_pb_scan.c`) runs on the game thread
  inside `xbox_Nv2aKick`. It copies the submitted segment as headers + data, following
  jumps, calls and returns exactly as the synchronous walk reads them. GET is set to PUT
  at once, because the commands are already copied.
- The render thread (`pb_async_thread`, `xbox_memory_layout.c`) executes the copies
  (`nv2a_pb_exec_linear`) from a queue of up to 48. Fence and counter mirrors are
  published only after a copy has been executed, so D3D's resource waits stay honest.
- `RECOMP_GPU_ASYNC` now defaults to 1 (`src/main.c`); set `RECOMP_GPU_ASYNC=0` to go back.
- **Results:**

  | Run | Scale | Avg FPS | 10 s windows | Desyncs |
  |---|---|---|---|---|
  | `async7` | 2× | 59.2 | 60 60 60 60 55 60 | 0 |
  | `async8x` | 8× | 60.0 | 60 60 60 60 60 60 | — |

  The heavy wide stadium shots, which the game used to cap at 30, also hold 60. The only
  "lost stream" is one capture during D3D init (`03FCC000 → 010A0000`, 0 words), the
  same garbage walk sync mode also does there; it is harmless.
- **Files changed in `external/xboxrecomp`** (left uncommitted there by convention):
  - `src/kernel/nv2a_pb_scan.c` (bulk runs, live-PUT stop, capture, linear execution)
  - `src/kernel/nv2a_pb_exec.c` (`nv2a_pb_exec_run`)
  - `src/kernel/xbox_memory_layout.c` (async queue)
  - `src/kernel/kernel_bridge.c` (exact vblank timer)
  - `src/kernel/nv2a_gpu_d3d11.inc.c`, `nv2a_gpu_ps.inc.c` (new), `nv2a_gpu_d3d11.hlsl`,
    `nv2a_gpu_d3d11_hlsl.inc` (specialised pixel shaders, target-copy generation)
  - `src/kernel/nv2a_gpu_present.inc.c` (frame-rate cap)
  - `templates/runtime/recomp_types.h` (register-locals macros)

# Known issues and debugging tools

Status as of 2026-11-24, after the native macOS (Apple Silicon) port and the
SDL3 GPU renderer work. See `docs/MACOS.md` for building and running on macOS.

## Outstanding issues

### Rendering (SDL3 GPU renderer, `RECOMP_SDLGPU=1`)

- **Render-to-texture costs a GPU sync per frame.** Several screens (the
  attract-mode game, parts of live play) draw into a surface and then sample
  it, which downloads that surface to guest RAM every frame and waits for the
  GPU to finish. Heavy scenes run ~30-45 fps at the default 3x internal
  resolution on Apple silicon; light scenes hold 60. A GPU-side copy for
  render-to-texture (no round trip through guest RAM) is the fix.
- **Window clips: inclusive only.** NV2A window-clip rectangles are applied by
  replaying a draw once per rectangle with its own scissor. Exclusive clips
  (`SET_WINDOW_CLIP_TYPE` = 1) fall back to the surface clip, as the D3D11
  path does; overlapping inclusive rectangles would double-blend.
- **Texture-stage modes not implemented:** BUMPENVMAP / BUMPENVMAP_LUM are
  sampled as plain 2D; the dot-product HILO mappings are unimplemented (as in
  xemu). Not observed in NFL 2K5 so far.
- **Cube maps:** one mip level only, and the texture-cache key hashes face 0's
  bytes -- a title rewriting only faces 1-5 in place would not be re-uploaded.
- **Small visual glitches still seen, cause unknown:** a white streak at the
  left edge of the main-menu background; white/black octagonal rings at the
  studio anchor's wrists in the pregame show (seen before the cube-map /
  dot-product fix, not rechecked since); occasional dark block with light
  streaks in the top-left corner of a gameplay frame.
- **Points and lines** in 3D batches stay on the CPU path.
- **Metal fast math:** SDL compiles MSL with Metal's default fast-math, so
  `isnan` checks in the generated vertex-program epilogue may be folded away.
  No visible effect found.
- **Metal shader cache crash (seen once):** after many runs killed during
  shader compilation, the game aborted inside Apple's shader cache
  (`fscache_insert_and_retain`). Moving
  `$(getconf DARWIN_USER_CACHE_DIR)/com.apple.metal/<id>` aside fixed it.
  Likely cache corruption from the killed processes, not a game bug.

### Software renderer

- Much slower than the GPU path, and draws some things wrong that the GPU
  path gets right (e.g. a red grid over the end zone during a kick return).
  It remains the reference for combiner math, not for texture-shader modes:
  it samples dot-product stages as plain 2D textures.

### Platform / build

- **Linux and Windows were not rebuilt or retested** after these changes.
  The renderer fixes (lighting, window clips, scaling, `sg_grow`) are shared
  with the Linux Vulkan path; `xf.frag` and the SDL3 sources changed.
- **arm64 MMIO emulation** (`platform/mmio_decode_arm64.h`) handles ordinary
  loads/stores, pairs and SIMD loads/stores. Exclusives, LSE atomics and
  LD1/ST1 against the trapped APU / AC97 pages log
  `[APUMMIO] arm64 decode failed` and crash. Not hit in testing.
- The xboxrecomp submodule commit for this work is local (branch
  `macos-port`); it has to be pushed to a remote the main repo can reach
  before anyone else can check this out.
- Build with `tools/gen-reg-locals.py` first (guest registers in locals).
  Without it macOS pays for slow thread-local access on every guest register.

### Testing coverage

- Verified: boot, menus, team select, pregame, coin toss, kickoff/return,
  offensive and defensive play calling, live plays in the first quarter, audio,
  at 1x-3x internal resolution. Long sessions, other modes (Franchise, Crib,
  online) and mod packs were not tried on macOS.

## Debugging and test tools

### Renderer environment variables (SDL3 GPU path)

| Variable | Effect |
|---|---|
| `RECOMP_SDLGPU=1` | Use the GPU renderer (Metal on macOS, Vulkan on Linux) |
| `RECOMP_SDLGPU_SCALE=<n>` | Internal resolution multiplier (default: F1 > Video setting) |
| `RECOMP_SDLGPU_FPS=1` | Once a second: flips/s, downloads, uploads, passes, draws, time waiting on the GPU |
| `RECOMP_SDLGPU_DUMP=<file.bmp>` + `RECOMP_SDLGPU_DUMP_AT=<flip>` | Write the GPU's frame, read back from the texture, at a flip; touching `<file.bmp>.req` dumps the next flip on demand |
| `RECOMP_FB_DUMP=<file.bmp>` | Guest-RAM frame at window frame 60, or on demand via `<file.bmp>.req` |
| `RECOMP_SDLGPU_TOGGLE_DIR=<dir>` | Live A/B switches: create `novp`, `nospec`, `no3d`, `nomip`, `nofloor` in `<dir>`; write `x y` to `<dir>/watch` for a one-frame pixel trace |
| `RECOMP_SDLGPU_NOSPEC=1` | Generic (uber) fragment shader instead of per-combiner variants |
| `RECOMP_SDLGPU_NOVP=1` | Vertex programs on the CPU |
| `RECOMP_SDLGPU_NO3D=1` | 3D batches on the software rasterizer |
| `RECOMP_SDLGPU_MSLDUMP=1` | Print every generated Metal shader |
| `RECOMP_SDLGPU_MODELOG=1` | Log each new texture-stage / combiner combination |
| `RECOMP_SDLGPU_COLHIST=1` | Histogram of vertex diffuse brightness (CPU vertex-program batches) |
| `RECOMP_SDLGPU_PROBE=x,y` | Every 10 s, trace the batches covering a pixel (slow: downloads per batch) |

### `tools/mac-debug/` -- scripted runs without screen-recording permission

All scripts share `env.sh` (`NFL2K5_DEBUG_DIR`, default `$TMPDIR/nfl2k5-debug`).

| Tool | Use |
|---|---|
| `run-live.sh [VAR=val...]` | Start the game with the press file, on-demand dumps and toggles wired up |
| `press.sh <wait> <buttons...>` | Queue controller presses (`start a b x y up down left right lb rb lt rt l3 r3 lsl lsr lsu lsd`) |
| `snap.sh <name>` | Dump the next GPU frame to `<name>.png` |
| `ab.sh <name> <toggle>` | Snapshot, flip a toggle, snapshot again, side-by-side PNG |
| `watch.sh <x> <y>` | One-frame trace of every batch that changes a guest pixel (GPU and software) |
| `to-playcall.sh` | Drive boot -> Quick Game -> an offensive play call, snapshot the play cards |
| `is_playcall.py` | Detect the play-call layout in a snapshot |
| `bmp2png.py`, `sheet.py`, `crop.py` | Convert dumps, make contact sheets, side-by-side crops |
| `busy.py`, `callers.py` | Summarise `sample <pid>` output: busy (non-waiting) time per thread, and who calls a function |
| `mmio_decode_arm64_test.c` | Unit test for the arm64 MMIO load/store emulator (build line in the file) |

Typical session:

    tools/mac-debug/run-live.sh &            # leave the window open
    tools/mac-debug/to-playcall.sh
    tools/mac-debug/ab.sh coin no3d          # GPU vs software on the same frame
    tools/mac-debug/watch.sh 590 100         # which batches draw that pixel

Profiling: `sample <pid> 6 -file out.txt`, then
`python3 tools/mac-debug/busy.py out.txt`.

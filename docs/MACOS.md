# Native macOS build (experimental, Apple Silicon)

Builds on the Linux port (docs/LINUX.md) with Clang and SDL3. Boots to the
front end with sound, drawn by the software renderer.

## Build

    brew install cmake ninja sdl3 openssl@3 glslang spirv-cross
    git submodule update --init --recursive
    mkdir -p external/xemu-src/util/miniz
    for f in miniz.c miniz.h; do
      curl -fL -o external/xemu-src/util/miniz/$f \
        https://raw.githubusercontent.com/xemu-project/xemu/master/util/miniz/$f
    done
    python3 tools/gen-reg-locals.py        # guest registers in locals (faster)
    cmake -B build-mac-game -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFL2K5_BUILD_GAME=ON \
      -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
    cmake --build build-mac-game

## Game files

Extract your own disc into `original/` (needs `default.xbe` and
`vc_53450030`). A full Redump ISO extracts with xdvdfs
(`cargo install xdvdfs-cli`):

    xdvdfs unpack "ESPN NFL 2K5 (USA).iso" original/

## Run

    tools/run-mac.sh

## What is different on macOS

- **Main thread.** Cocoa only allows windows and events on the main thread,
  so `main()` runs the game on a 16 MB-stack pthread and the main thread runs
  the SDL window loop (`xbox_FramebufferMainThreadRun`, fb_present.c).
- **Trapped MMIO on arm64.** The APU registers and the AC'97 reset trap are
  serviced by emulating the faulting AArch64 load/store
  (`platform/mmio_decode_arm64.h`) instead of the x86-64 decoder. The AC'97
  trap emulates the store rather than single-stepping it. Darwin raises SIGBUS
  for most protection faults, so it is handled alongside SIGSEGV.
- **16 KB pages.** Traps cover a whole host page. The APU DSP windows open
  only the host pages wholly inside them; the rest is shadowed by the APU
  handler, so GPRST/EPRST stay trapped.
- **Checked guest copies** use `mach_vm_read_overwrite` and `mach_vm_region`
  (platform/guest_memory.c) instead of `process_vm_readv` and `/proc/self/maps`.

## GPU renderer (Metal)

    RECOMP_SDLGPU=1 tools/run-mac.sh

SDL's Metal backend takes MSL, not SPIR-V. The renderer keeps its GLSL as the
only source: the SPIR-V it already makes (build-time shaders and the
glslang output for each vertex program) is cross-compiled to MSL with
SPIRV-Cross at run time (`sdlgpu/sdlgpu_msl.inc.c`). Vertex programs convert on
the compile worker, so the draw thread only creates them. Without
spirv-cross installed the build still works and the renderer reports itself
unavailable.

It renders at the internal resolution from F1 > Video (default 3x; override
with `RECOMP_SDLGPU_SCALE=<n>`). Guest RAM stays 1x: copies to and from it
go through a 1x staging texture with a GPU blit.

Performance notes (Apple silicon):

- 3D batches use fragment shaders specialised per combiner setup
  (`sdlgpu_fscache.inc.c`), compiled in the background; the generic
  interpreter shader draws until a variant is ready. In the attract-mode
  game at 3x this took GPU time from ~84 ms to ~10 ms a frame.
  `RECOMP_SDLGPU_NOSPEC=1` turns it off.
- Build with the register-locals copy of the generated code (the Windows
  build always does): `python3 tools/gen-reg-locals.py` before configuring.
  macOS thread-locals are slow; keeping guest registers in locals avoids them.
- `RECOMP_SDLGPU_FPS=1` prints flips/s, GPU downloads and uploads, passes,
  draws, `rtt` copies, and time spent waiting on the GPU, once a second.
- Render-to-texture stays on the GPU: when the title samples a surface it
  just drew (screen effects, replays), the renderer blits the surface into a
  1x R8G8B8A8 texture (format convert + downscale in one `SDL_BlitGPUTexture`)
  and binds that, instead of downloading to guest RAM and re-uploading -- a
  full GPU sync every frame. A version counter refreshes the copy only when
  the surface changed. `RECOMP_SDLGPU_NO_GPURTT=1` forces the old
  download path (also used automatically for sub-rectangle or cube samples).
  On the attract-mode game this took downloads from ~30 to 0 a second and
  about 10 -> 56 flips/s.

Rendering fixes in this renderer (not macOS-specific, apply on Linux too):
player/field lighting uses the dot-product texture modes and real cube maps
(as the D3D11 path); NV2A window clips are honoured (the play-call cards);
unindexed batches no longer fall back to software by mistake.

A/B while the game runs: set `RECOMP_SDLGPU_TOGGLE_DIR=<dir>` and create files
there named `novp`, `nospec`, `no3d`, `nomip` or `nofloor` to switch those
paths off live; write `x y` to `<dir>/watch` to log every batch that touches
that pixel in the next frame (GPU and software). `NFL2K5_PRESS_FILE` also
accepts `lsl`/`lsr`/`lsu`/`lsd` (left stick).

Diagnostics: `RECOMP_SDLGPU_MSLDUMP=1` prints every generated MSL shader;
`RECOMP_SDLGPU_DUMP=frame.bmp` (with `RECOMP_SDLGPU_DUMP_AT=<flip>`, default
600) writes the GPU's frame read straight back from the texture;
`RECOMP_SDLGPU_DUMP_SEQ=<n>` writes `n` consecutive flips from there
(`frame.bmp.<flip>.bmp`), for frame-by-frame evidence.

## Intro movies

The boot intro movies are decoded by the title's own Sofdec MPEG decoder,
running on the recompiled CPU. That decoder cannot keep up: the movie player
steps through its frame buffers faster than the decoder fills them, so most
presented frames are still-black buffers and the intro flashes. Windows does
not have this problem (playback goes through Media Foundation), so there the
skip stays opt-in.

On macOS and Linux the boot intro is skipped by default (the movie player
`sub_00178150` returns "player skipped" and the intro loop ends), which takes
boot from the splash screens to the title screen in about ten seconds.
`NFL2K5_SKIP_INTRO=0` plays the movies anyway.

## Not on macOS yet
- Everything in docs/LINUX.md's "Not on Linux yet" list.

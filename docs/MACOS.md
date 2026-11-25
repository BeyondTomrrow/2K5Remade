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
  draws, and time spent waiting on the GPU, once a second.

Diagnostics: `RECOMP_SDLGPU_MSLDUMP=1` prints every generated MSL shader;
`RECOMP_SDLGPU_DUMP=frame.bmp` (with `RECOMP_SDLGPU_DUMP_AT=<flip>`, default
600) writes the GPU's frame read straight back from the texture.

## Not on macOS yet
- Everything in docs/LINUX.md's "Not on Linux yet" list.

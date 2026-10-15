# Native Linux build (experimental)

The game builds and links natively on Linux with GCC and SDL3. It has not
been played end to end yet; expect the same rough edges as early Windows
builds, plus the Linux gaps listed below.

## Build

Arch / CachyOS:

    sudo pacman -S --needed base-devel git cmake ninja python sdl3 openssl libepoxy
    git clone --recursive https://github.com/BeyondTomrrow/2K5Remade.git
    cd 2K5Remade

miniz comes from xemu's source tree (external/ is gitignored):

    mkdir -p external/xemu-src/util/miniz
    for f in miniz.c miniz.h; do
      curl -fL -o external/xemu-src/util/miniz/$f \
        https://raw.githubusercontent.com/xemu-project/xemu/master/util/miniz/$f
    done

Then:

    cmake -B build-linux-game -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFL2K5_BUILD_GAME=ON
    cmake --build build-linux-game

The generated code is about 220 MB of C; expect several minutes. The game
target builds at -O2 by default (`NFL2K5_LINUX_OPT`; 0 for debugging, 1 if
an -O2 build misbehaves). The largest generated files need about 2 GB of RAM
each to compile, so if a parallel build runs out of memory, limit the jobs:
`cmake --build build-linux-game -j6`.

The four `NFL2K5_FORCE_UNBLOCK_*` options that `tools/build.ps1` enables for
Windows (AUDIO_LOCK, 33660_DRAIN, NETPOLL, STATE9_READY) default to ON on Linux.
Without them the game sits on a black screen (the main thread waits forever
in XNetStartup's network poll). A build directory configured before they
defaulted on keeps them OFF in its cache; reconfigure it with

    cmake -B build-linux-game -DNFL2K5_FORCE_UNBLOCK_AUDIO_LOCK=ON \
      -DNFL2K5_FORCE_UNBLOCK_33660_DRAIN=ON -DNFL2K5_FORCE_UNBLOCK_NETPOLL=ON \
      -DNFL2K5_FORCE_UNBLOCK_STATE9_READY=ON

## Game files

Put your extracted disc in `original/` (it must contain `default.xbe` and
`vc_53450030`). Same layout as the Windows build.

## Run

    tools/run-linux.sh

Logs go to the terminal. `SDL_VIDEO_DRIVER=wayland` or `=x11` forces a
video backend if the default misbehaves.

## Not on Linux yet

- Broadcast presentation packages (FOX/CBS/NBC scorebugs, player cards,
  broadcast music). The renderer is Direct2D/Media Foundation/XAudio2;
  Linux keeps the original ESPN presentation. `src/platform/posix/presentation_stub.cpp`
- Mod pack disc overlays (`xboxrecomp/src/kernel/kernel_overlay.c`)
- DDS replacement textures (PNG/JPEG/BMP/TGA work)
- Interpreter fault report on crash (Windows VEH diagnostic)
- `analysis/` is not needed to build; without it the mod-pack
  interpreter ranges are empty, which retail does not use

## How the port is structured

- `src/platform/posix/windows.h` is only on the Linux include path. It routes
  `#include <windows.h>` to xboxrecomp's portable NT types and Win32 compat.
- `src/platform/posix/win32_game_compat.*` adds the Win32 calls the game
  uses that xboxrecomp does not (directory search, attributes, copy/move,
  secure CRT, a few intrinsics). Paths are accepted with backslashes and
  converted, so game code builds paths the same way on both platforms.
- Windows-specific code paths are `#ifdef _WIN32`; Windows builds are
  unchanged.

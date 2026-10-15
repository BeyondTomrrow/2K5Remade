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

The generated code is about 220 MB of C; expect several minutes.

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

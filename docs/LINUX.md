# Native Linux build (experimental)

The release is an x86-64 archive, `NFL2K5-linux-x86_64-<version>.tar.gz`, with
its own `README.txt` for players. This page is about how the port stands and
how it is built. `linux/README.md` has the release build steps.

## What the release is

- Full renderer: the same gx layer as Windows and Android, on SDL_GPU with
  Vulkan. Shaders are compiled at run time by glslang. No OpenGL or Direct3D.
- SDL3 ships in `lib/` (RUNPATH `$ORIGIN/lib`). libstdc++ and libgcc are linked
  statically and OpenSSL is replaced by `android/openssl_shim`, so the program
  needs only glibc 2.35 or newer, plus the system's Vulkan loader and whatever
  video and audio stack SDL finds at run time.
- `nfl2k5.sh` starts the game and keeps `logs/session.log`. Running `./NFL2K5`
  directly also works: it raises its own stack limit to 16 MB and restarts
  itself, since the Windows build links that size and Linux defaults to 8 MB.
- Saves are in `saves/` in the game folder, as on Windows. Settings, logs and
  caches are in `~/.local/share/NFL2K5-PC`, or in the game folder when
  `portable.txt` is there.

## What has been tested

Only in a QEMU TCG (no hardware acceleration) x86-64 VM on Ubuntu 22.04, with
Xvfb, openbox and Mesa lavapipe. That is about 0.1-1 frame a second, so nothing
here says anything about speed.

Seen working, on the real Vulkan path: legal screen, boot intro, title, Main
Menu, VIP creation, saving and reloading after a restart, Quick Game team
select, a CPU vs CPU match through pregame, coin toss, kickoff and return to the
first down (a test build skipped most draws between screenshots to get that far
in the VM; retail and SOFTDRINK both reached the coin toss in full); F1 menu
pages and keys; Exit (menu and window close); `.2k5patch` install in the game
(SOFTDRINK v0.7, output identical to the PC install); playing with the pack
(`1127 of 33530 converted functions differ`, disc overlay active); pack switch
prompt and restart; Music folder conversion through ffmpeg, non-ASCII file name
included; pad unplug and replug through a uinput device; game audio reaching
SDL (disk driver, menus and in a match).

Also seen: the release archive, unpacked fresh with an empty home folder, boots
to the title on Wayland (weston's headless backend), and with no Vulkan driver
it shows an error box instead of a black window.

Not tested: any real GPU or driver, real speed, real pads and rumble, PipeWire,
PulseAudio or ALSA output, a real Wayland desktop, Steam Deck, any distribution
but Ubuntu 22.04. One open question: after the kickoff a close-up showed a player
on a flat olive background. That frame came from the draw-skipping test build,
so it may be an artifact of the test; compare it with the PC build.

## Not on Linux

- Broadcast presentation packages (FOX/CBS/NBC scorebugs, player cards,
  broadcast music). The renderer is Direct2D/Media Foundation/XAudio2 with an
  HTML host; `src/platform/posix/presentation_stub.cpp` and the stub
  `PresentationHost` keep the original ESPN presentation. The stub also means
  moment audio clips stay silent.
- Sandbox: its only UI is HTML (`src/nfl2k5_sandbox_ui.cpp`, same host). The
  archive leaves out `mods/sandbox`, so the Game Modes row does not appear.
- Tools that are Windows programs: Team Builder, `nfl2k5-save-converter`, the
  disc-image half of the mod pack installer. `.2k5patch` files install in the
  game itself.
- FSR 3 and frame generation (Direct3D 12 only), HDR output (written for the
  Windows path, never run on Linux); the F1 and in-game video menus leave those
  rows out on Linux and the Graphics API row with them.
- DDS replacement textures (PNG, JPEG, BMP and TGA work).
- Interpreter fault report on crash (Windows VEH diagnostic). The general crash
  report does work and goes to the session log.
- Music folder conversion runs an `ffmpeg` program from PATH (wmav2 encoder)
  instead of Media Foundation. Android links FFmpeg in; the Linux release does
  not.

## Building from source without the release wrapper

The plain configuration below builds the older software path, not the release:
it draws 2D on the GPU at most and leaves 3D to the software rasterizer. For
anything but porting work, use `linux/README.md`.

    cmake -B build-linux-game -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFL2K5_BUILD_GAME=ON
    cmake --build build-linux-game
    tools/run-linux.sh

It needs SDL3, OpenSSL, libepoxy and glslang from the distribution, and miniz
from xemu's source tree in `external/xemu-src/util/miniz` (`external/` is
gitignored). The generated code is about 220 MB of C; the largest files need
about 2 GB of RAM each to compile, so limit the jobs on a small machine. The
game target builds at -O2 (`NFL2K5_LINUX_OPT`; 0 for debugging).

The four `NFL2K5_FORCE_UNBLOCK_*` options that `tools/build.ps1` enables for
Windows (AUDIO_LOCK, 33660_DRAIN, NETPOLL, STATE9_READY) default to ON on Linux.
Without them the game sits on a black screen.

## How the port is structured

- `src/platform/posix/windows.h` is only on the Linux include path. It routes
  `#include <windows.h>` to xboxrecomp's portable NT types and Win32 compat.
- `src/platform/posix/win32_game_compat.*` adds the Win32 calls the game
  uses that xboxrecomp does not (directory search, attributes, copy/move,
  secure CRT, a few intrinsics). Paths are accepted with backslashes and
  converted, so game code builds paths the same way on both platforms; `fopen`
  is wrapped for the same reason.
- Windows-specific code paths are `#ifdef _WIN32`; Windows builds are
  unchanged.

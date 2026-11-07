# Linux x86-64 release build

The release binary is cross-compiled so that it needs nothing newer than
Ubuntu 22.04 (glibc 2.35). `docs/LINUX.md` covers playing and the feature list.

Inputs:

- **Sysroot** (`LINUX_X64_SYSROOT`): `libc6 libc6-dev linux-libc-dev libgcc-s1
  libgcc-11-dev libstdc++-11-dev libstdc++6 libcrypt-dev libcrypt1` from Ubuntu
  22.04 unpacked into one folder.
- **SDL3** (`NFL2K5_SDL3_PREFIX`): the same SDL tree as the Android build,
  `android/patches/sdl-vulkan-pipeline-cache.patch` applied, built on Ubuntu
  22.04 itself, because SDL's Wayland backend needs `wayland-scanner` at build
  time. X11, Wayland, ALSA, PulseAudio, PipeWire and udev support are on and
  all dlopen'd, so the game starts on a machine that lacks any of them. The
  flags are in `linux/build-sdl.sh`.
- **glslang** (`GLSLANG_SRC`) and the mingw-w64 DirectX headers
  (`XBOXRECOMP_DX_HEADERS`, DXVK's `include/native/directx`).
- `external/xemu-src/util/miniz` and `analysis/` as for any other game build.
  Without `analysis/disasm/functions.json` the mod pack interpreter has no
  function ranges and packs will not run.

Build and pack (the generated code first: `python tools/gen-reg-locals.py`):

    cmake -S linux -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=linux/x86_64-toolchain.cmake \
      -DGLSLANG_SRC=... -DXBOXRECOMP_DX_HEADERS=... -DNFL2K5_SDL3_PREFIX=...
    ninja -C build-linux NFL2K5
    python linux/package.py --binary build-linux/nfl2k5/NFL2K5 \
      --sdl3 $SDL3/lib/libSDL3.so.0 --version X.Y.Z --out dist/NFL2K5-linux-x86_64-X.Y.Z.tar.gz

The toolchain file is for Windows with the Android NDK's clang. On a Linux
host, drop `-DCMAKE_TOOLCHAIN_FILE` and build on Ubuntu 22.04 or in a container
of it with the same other options.

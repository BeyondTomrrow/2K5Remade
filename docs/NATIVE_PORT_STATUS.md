# ESPN NFL 2K5 Native PC Recompilation — Status Report

Updated: 2026-09-08

## Goal

Create a standalone Windows executable that runs the user-provided, legally extracted Xbox version of ESPN NFL 2K5 without requiring Xemu at runtime. The first playable milestone is a native EXE that reaches the title’s menu flow; later work covers complete game logic, controller support, sound, and a production renderer.

## What exists now

- Project workspace: `E:\NFL2K5-PC`
- Original game input is kept separate in `original\default.xbe`. No copyrighted game files were downloaded or added by the project work.
- Native build output: `build\Release\NFL2K5.exe`
- The EXE is a native x64 Windows program. It maps the title’s Xbox memory layout, loads the XBE into that layout, dispatches recompiled game functions, and bridges Xbox kernel calls to Windows compatibility code.
- `tools\build.ps1 -Game` builds the EXE successfully.
- `tools\run-bringup.ps1 -ValidateOnly` passes memory-map, guest `memmove`, and contiguous-memory allocator checks.

## Recompilation and startup work completed

- Generated and compiled a C representation of the Xbox executable’s discovered code.
- Added hand-written implementations for a small number of missed function boundaries and indirect-call targets needed by startup, including the XBE `memmove`, I/O worker paths, boot registration, timer setup, and the title’s known frame callback.
- Replaced unsupported Xbox kernel and hardware behavior with Windows-side bridges or controlled compatibility behavior where needed for startup.
- Added diagnostics for guest indirect calls, worker threads, allocator activity, frame callbacks, GPU pushbuffers, crashes, and sampled game state.
- Added a native state sampler that records selected Xbox-memory globals to `logs\native-sample.txt` so the recompiled program can be compared directly with a live Xemu execution.

## Rendering work completed

- Replaced the previous GDI framebuffer display path with a Direct3D 11 swap-chain presenter in `external\xboxrecomp\src\video\fb_present.c`.
- The native EXE can create a 640×480 Direct3D 11 framebuffer window when the game reaches its display-mode setup.
- The Xbox NV2A command processor is receiving and decoding display, surface, texture, transform, clear, and flip setup commands from the game.
- The current frame remains blank because the game has not yet reached the frontend geometry submission commands (`SET_BEGIN_END` / `INLINE_ARRAY`) that contain menu triangles and textures.

## Evidence from the working Xemu reference

Xemu is currently useful only as a reference. It is not part of the native EXE runtime.

- Xemu runs the retail title through its demo/game state.
- A local debugger connection and live RAM capture tools were added under `tools\`.
- The working game’s scheduler has nine active callbacks:
  `0003E910`, `003CD120`, `0003A1C0`, `0003A310`, `00051F00`, `00039380`, `00042BD0`, `00045F20`, and `00041810`.
- At the comparable native startup point, only `00041810` is registered.
- This is the present primary blocker: the native startup handoff reaches framebuffer and GPU initialization but does not complete the delayed registrations that drive frontend updates and rendering.

## Experiments and conclusions

- A temporary attempt to force the missing callbacks at the earliest boot location was rejected. It ran callbacks before their required title state was initialized and prevented normal display startup.
- The test was removed from the early boot path. The controlled recovery code remains opt-in at the existing frame callback only, so it cannot silently alter ordinary runs.
- A longer native trace confirmed the EXE is alive and executing startup/frame code rather than simply exiting. It produces Xbox GPU state traffic, but no menu geometry yet.

## Current state

The project has a compiling, native Windows EXE and a Direct3D 11 presentation path. It is an engineering bring-up build, not yet a bootable or playable game. It does not display NFL 2K5 menus yet.

## Next work, in order

1. Trace the real title event/worker completion that registers the remaining eight scheduler callbacks.
2. Make the native compatibility layer deliver that event in the same order as the working Xbox/Xemu execution.
3. Confirm the native pushbuffer begins emitting frontend `SET_BEGIN_END` and `INLINE_ARRAY` commands.
4. Extend NV2A/D3D11 translation for the methods and vertex/texture formats used by the title’s menus.
5. Verify visible menus, then connect controller input and continue through title-specific rendering and audio paths.

## Important files

- `docs\PROGRESS.md` — chronological development notes.
- `logs\native-sample.txt` — latest native guest-state sample.
- `logs\startup-*.stdout.log` and `logs\startup-*.stderr.log` — native runtime traces.
- `tools\start-xemu-gdb.ps1`, `tools\capture-xemu-menu.ps1` — local Xemu reference-capture tools.
- `src\main.c` — native startup and state sampler.
- `src\recomp_manual.c` — reviewed hand-written recompilation gap handlers.
- `external\xboxrecomp\src\video\fb_present.c` — Direct3D 11 framebuffer presenter.

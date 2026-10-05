# ESPN NFL 2K5 PC Recompilation — Review Package

## Purpose

This package is for technical reviewers helping diagnose the current native Windows recompilation build. It contains project source, build scripts, compatibility code, renderer code, configuration, and diagnostic documentation. It intentionally excludes the retail game executable, ISO/XISO images, extracted game archives, saves, logs containing game data, and compiled game binaries.

The reviewer must use their own legally extracted retail Xbox game data. Do not add game files to an issue, pull request, archive, or fork.

## Current build

The native target is `build\\Release\\NFL2K5.exe`. The project uses the original Xbox executable only as local, user-supplied input and renders through the D3D11 hardware path by default (`RECOMP_GPU=1`). `RECOMP_GPU=0` selects the slower software rasterizer for comparison and diagnosis.

The build has reached the title flow, menus, 3D stadium scenes, pregame, and playable in-game states. This is still an experimental compatibility project, not a release-quality port.

## Reviewer setup

1. Install Visual Studio 2022 with the Desktop development with C++ workload and a Windows SDK.
2. Place a personally extracted retail game disc tree outside the review archive.
3. Point the local development setup at that data as described in the project documentation.
4. Build with:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\\tools\\build.ps1 -Game
   ```

5. Run `build\\Release\\NFL2K5.exe` with `RECOMP_GPU=1` (the default). Test `RECOMP_GPU=0` only to distinguish D3D11-specific behavior from shared emulation/translation behavior.

## Priority issues

### P0 — Audio corruption across multiple mix categories

**User reproduction:** In a running game, lower the audio categories and bring them back up one at a time. Enabling **Crowd Noise**, **Sound Effects**, **Music**, or **Player Voice** produces loud, distorted bursts described as car-horn-like noise, while random sound effects can appear out of sequence. The problem is not limited to one volume slider or one scene.

**Expected:** Each category should play its intended audio continuously at the selected volume. Changing a slider must never change sample interpretation, playback position, or cause a burst of unrelated audio.

**Observed impact:** The game becomes unpleasant or unsafe to listen to at ordinary volume. This blocks a public playable build.

**Important history:** A recent ADPCM alignment change reduced a previously measured broadband burst in one native capture, but this report shows that the overall audio problem remains unresolved across several categories. Treat that change as incomplete mitigation, not a fix.

**Likely review areas:**

- `external\\xboxrecomp\\src\\apu\\` — voice/ring-buffer addressing, ADPCM block alignment, loop/stop behavior, volume/pan, and stream position reporting.
- XAudio2 queue ownership and underflow/overwrite behavior.
- APU IRQ / voice-state timing relative to title task scheduling.
- Comparison captures from the same menu or gameplay sequence in Xemu and the native build.

**Requested evidence:** Per-voice logs for format, source address, ring size, read/write cursors, ADPCM alignment, queue depth, decoded frame count, and stop/loop transitions. Avoid per-sample logging because it changes timing.

### P1 — Remaining field, sky, and geometry artifacts

The major discontinuous `DRAW_ARRAYS` strip issue was fixed, and recent captures show a continuous field in several scenes. The user still reports visible field glitches and sky/background coverage that appears to stop at scene edges in other gameplay cameras.

**Expected:** Field tiles, sidelines, sky/background geometry, crowd, player models, and stadium surfaces should cover their intended view without seams, wedges, missing strips, or abrupt edge cutoffs.

**Likely review areas:**

- NV2A primitive segmentation and strip restart behavior.
- Vertex-program outputs, clipping, viewport transform, and fog/texture-stage behavior.
- Render-to-texture and surface/flip ownership.
- Texture addressing, mip selection, palette formats, and depth/stencil state.

### P2 — Performance consistency

The current D3D11 path is substantially faster than the software renderer, but live gameplay still commonly runs near 28–33 FPS at the 2x Enhanced Sharp preset. Menus are near 60 FPS. The original simulation must remain 60 Hz; increasing guest vblank to 120/165 Hz is not a valid performance fix because it changes gameplay timing.

**Expected:** Stable 60 Hz game simulation and improved present performance. High-refresh monitors should eventually use a presentation-only approach that does not accelerate the Xbox simulation.

**Likely review areas:**

- Synchronous pushbuffer execution and D3D11 submission frequency.
- Small-batch draw overhead.
- Safe immutable pushbuffer queuing/batching.
- GPU texture cache churn and readback frequency.

### P2 — Rendering features not fully implemented

Known incomplete NV2A/D3D11 behavior includes mipmapping/filtering quality, fog, exact partial depth/stencil clears, and potentially unimplemented texture/combiners in less common gameplay paths. These can affect visual fidelity even when geometry is correct.

### P2 — Game-flow and content checks still needed

Previously reported functionality needing continued regression testing:

- Franchise can return to Configure Franchise after settings confirmation.
- Team Rosters can show an empty player list.
- Create Player can show no player model.
- Some presentation sequences and player-stat producers remain incomplete.

## Known-good reference points

- Native x64 Release build completes.
- User-supplied XBE loading, guest memory, scheduler, file access, APU startup, and D3D11 initialization work.
- Intro movie, legal/title flow, menus, VIP flow, team selection, and 3D scenes have been observed.
- `RECOMP_GPU=1` is the normal D3D11 hardware renderer; it is not the software path.
- Field bridge-triangle artifacts from discontinuous `DRAW_ARRAYS` were fixed in the documented test route.
- FOX and other presentation packages are optional overlays and should be disabled while isolating core renderer/audio bugs.

## Recommended triage order

1. Reproduce the multi-category audio issue with a short native capture and matching Xemu reference capture.
2. Identify whether affected categories share a voice format, DMA ring, decoder state, or mixer route.
3. Fix audio correctness before increasing volume defaults or adding more presentation music.
4. Capture an affected field/sky camera with `RECOMP_GPU=1` and the same route using `RECOMP_GPU=0`.
5. Compare primitive batches, vertex-program IDs, render targets, texture states, and depth values around the first visual divergence.
6. Profile gameplay submission cost only after rendering is correct; preserve the 60 Hz guest clock.

## Useful controls

| Setting | Use |
| --- | --- |
| `RECOMP_GPU=1` | Default D3D11 hardware renderer. |
| `RECOMP_GPU=0` | Software reference renderer for differential testing. |
| `RECOMP_GPU_PROFILE=1` | Five-second low-overhead GPU/pushbuffer profile. |
| `RECOMP_GPU_DEBUG=1` | D3D11 debug layer diagnostics. |
| `RECOMP_GPU_SHOW=1..6` | Inspect selected texture/depth/alpha stages. |
| `RECOMP_SHOT_PREFIX=<path>` | Timed renderer screenshots. |
| `RECOMP_TEX_DUMP=<prefix>` | Texture decode inspection. |
| `RECOMP_VP_TRACE=1` | Vertex-program diagnostics; use only for targeted reproduction. |

## Reporting template

Please include:

- Build commit/hash and whether local changes were present.
- GPU model, Windows version, display resolution, and driver version.
- `RECOMP_GPU` setting and video preset/internal resolution.
- Exact game route, teams/stadium, and game clock when the issue occurs.
- A short capture or screenshot from native plus the matching Xemu state when possible.
- Relevant stderr/log excerpt with timestamps.
- For audio: which individual category slider triggered it, whether the issue starts immediately, and whether muting the category stops it.

## Scope boundaries

Do not commit or redistribute retail game data. Do not paper over faults by muting audio categories, disabling D3D11, forcing software rendering, faking title state, or changing guest timing. Fixes should preserve guest-context execution and Xbox-accurate 60 Hz simulation.

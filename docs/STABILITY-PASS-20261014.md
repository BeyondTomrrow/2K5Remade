# Stability pass — 2026-10-14

This pass keeps the retail game and SOFTDRINK as separate configurations. It
does not replace saves, packs, presentation assets, or the active-pack choice.

## Verification correction (afternoon)

Earlier process-survival results are not evidence of correct gameplay or a
fixed SOFTDRINK crash. The timed drivers did not confirm CPU-vs-CPU selection,
and a staged pack executable can dispatch to the installed main executable.
The old `a0.stderr.log` is from October 2 and cannot establish the cause of
the current Situation Mode defect. Retract the earlier claim that its
unhandled-method/pixel counters explain today's black environment.

Direct current-game observations and renderer-owned captures reproduce visible
players against an absent black field/stadium in SOFTDRINK. The user reports
Regular is unaffected. The symptom persists with raw vertex decoding disabled,
with GPU vertex programs disabled while retaining D3D11 rasterization, and
with depth testing and pixel-shader shading/alpha rejection bypassed.
Diagnostic captures are `logs/situation-color-007.bmp` and
`logs/situation-color-010.bmp`. Normal rendering was restored afterward.

The poison-target suppression was removed: returning from an invalid call
without repairing its state/stack is not a verified crash fix. No complete
halftime, Situation Mode, or audio fix is claimed.

## Changes made

### Follow-up: SOFTDRINK mode helper flag return

- Found a definite translation defect in `sub_014DB710`: several comparison
  paths merge before returning, and the generated function did not export its
  final flags. Animation detours branch on those flags immediately after CALL.
- Added the optional `SOFTDRINK_MODE_FLAG_RETURN` generated-code patch in
  `tools/apply-gen-patches.py`. Retail has no such helper and skips this patch.
- Extended the actual native/interpreter validation in
  `src/interp/x86_interp.c` with four helper cases, checking ZF, EAX preservation,
  and stack cleanup. Before the patch all four cases failed (validation exit 9);
  afterward all passed (exit 0). Evidence: `logs/validate-mode-before.*.log`
  and `logs/validate-mode-after.*.log`. SOFTDRINK build succeeded in
  `logs/build-mode-flag-fix.log`; retail's existing eight-case validation also
  passed in `logs/validate-flags-regular.*.log`.
- This is NOT a verified Situation Mode/crash fix. The subsequent normal
  hardware-rendering run reached the attract demo with visible field/stadium,
  then crashed. No manual Situation Mode launch was completed in that run.
  Retained evidence: `logs/situation-mode-return.stderr.log`, its stdout log,
  and numbered `situation-mode-return-*.bmp` captures. Earlier black-stadium
  captures and logs remain intact. No software-rendering fallback was used.

- `src/nfl2k5_local_music.cpp`
  - Fixed the UTF-8-to-UTF-16 helper allocating one wchar too few while asking
    Windows to write the terminating wchar. This was a startup heap-overwrite
    risk in both builds.
  - Added a publication gate so the game-thread “Now Playing” hook cannot read
    the playlist vector while the background scanner is still appending albums.
    The scanner publishes the library only after its structure is complete.
- `tools/build.ps1`
  - Fixed the generated-source check for pack builds. `Get-Content -Raw` was
    being passed a wildcard and failed before CMake under the bundled PowerShell
    runtime. The script now enumerates `recomp_*.c` and reads each file.
- Local external runtime test tree (ignored by this repository):
  - Removed the XAudio2 path that injected two synthetic silent buffers whenever
    the queue reached one buffer. That could produce audible gaps while the
    mixer was still submitting valid audio.
  - Added queue-low and `SubmitSourceBuffer` failure counters to the existing
    `[XA2]` telemetry. This change is present in the local external runtime used
    by the builds, but that vendored tree is intentionally excluded by `.gitignore`.
- `src/main.c`
  - Corrected the contiguous-memory validation to test live block reuse and
    invalidation instead of comparing the allocator's intentional high-water
    boundary as if it were live usage.
  - Made `--validate` terminate after its checks, before normal game teardown;
    normal launches still use the existing long-running path.
- `src/nfl2k5_local_music.cpp`
  - Added an explicit worker shutdown/join path before guest memory teardown.
- `src/nfl2k5_presentation.cpp`
  - Invalidates the detached presentation-theme decode generation before a
    quarter/halftime voice is destroyed, preventing an old decode from
    resurrecting audio after a state transition.
- `src/interp/x86_interp.c`
  - Rejects the Xbox uninitialized-memory fill patterns as indirect-call or
    jump targets. A SOFTDRINK run reproduced `0x86868686` as the target of the
    crash; the interpreter now returns to the caller and records the fault
    instead of executing that poison address.
- `src/main.c`
  - Added the diagnostic-only `NFL2K5_QUARTER_SECONDS` override for short
    quarter reproduction. Normal launches are unchanged unless the variable
    is explicitly set.

## Build and runtime evidence

| Check | Regular NFL 2K5 | SOFTDRINK v0.6 |
|---|---|---|
| Native build | PASS — `tools/build.ps1 -Game` | PASS — `tools/build.ps1 -Game -Pack softdrink-v06 -StageOnly` |
| Built-in validation | PASS — memmove and contiguous allocation/reuse checks pass; process exits cleanly after diagnostics | PASS — SOFTDRINK v0.6 reaches XBE/128 MiB setup, both allocator checks, presentation enumeration and clean diagnostic exit |
| GPU startup | PASS to recompiled entry in 20-second bounded run; no crash observed | PASS to pack boot/XBE initialization in direct validation run; no crash observed |
| Audio telemetry | XAudio2 initialized; the earlier 90-second run recorded `dropped=0`, `underruns=0`; a fresh 120-second run recorded `dropped=0`, `underruns=5`, queue 3 — underruns remain open | XAudio2 path initialized in the long probe; no drop/underrun failure was reported |
| Bounded gameplay probe | PASS — hardware-rendered CPU-vs-CPU run stayed alive for 90 seconds with no crash marker | PASS after interpreter guard — hardware-rendered run stayed alive for 600 seconds; the earlier 80-second `0x86868686` crash did not recur |
| Short-clock transition probe | PASS to a zero game clock and presentation host restart without a crash; full halftime identity still needs explicit period telemetry | PASS to a zero game clock and repeated NBC presentation host transitions without a crash; full halftime identity still needs explicit period telemetry |
| Halftime through postgame | NOT TESTED — requires interactive game/emulator control | NOT TESTED — requires interactive game/emulator control |
| Jukebox/custom-song playback | NOT TESTED in a live match/menu session | NOT TESTED in a live match/menu session |
| PS2 save conversion | NOT TESTED with a verified supported sample | NOT TESTED with a verified supported sample |
| FOX package in gameplay | NOT TESTED | NOT TESTED |
| Full frame-pacing matrix | NOT TESTED in this pass; existing benchmark data remains in `docs/BENCHMARKS.md` | NOT TESTED |

The earlier regular bounded audio log is `logs/startup-20261014-023055.stderr.log`.
The clean regular validation log is `logs/validate-20261014-105821.stderr.log`.
The latest SOFTDRINK v0.6 validation logs are
`logs/stability-softdrink-v06-validate-20261014.stdout.log` and
`logs/stability-softdrink-v06-validate-20261014.stderr.log`.
The longer SOFTDRINK gameplay probe is `logs/q1m-softdrink-20261014-long.stderr.log`;
the regular hardware probe is `logs/regular-probe-20261014.stderr.log`. The
short-clock transition capture is `logs/q1m-clock-20261014.stderr.log`.

## Remaining highest priorities

1. Capture a real shortened-quarter game in both configurations and capture the
   second-quarter-to-halftime transition with XA2, GPU, and presentation logs.
2. Verify whether the two observed XA2 underruns reproduce after removing the
   synthetic silence injection; compare a fresh 60-second gameplay capture.
3. Exercise a custom soundtrack selection and L3/R3 skip in both builds.
4. Run PS2 conversion against known-good `.psu`, `.max`, or `.ps2` samples and
   preserve the converter’s per-stage diagnostics.

No halftime, full-game, FOX, save-conversion, or SOFTDRINK gameplay fix is
claimed here without the corresponding live evidence.

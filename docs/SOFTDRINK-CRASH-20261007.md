# SoftDrink v0.4 crash investigation, 2026-10-07

User reports: Play Now goes black after Berman, MyCareer goes black on selection, demo/situation cycle through stadium views, and faces have missing triangles. None of those are yet verified fixed by this investigation.

## Captured failure

`logs/crash-20261007-missing-014DD6E6.log` records a guest access violation in `sub_0024E3E0` at generated `recomp_0016.c:73156`. EDX/EAX were `86868686`; reading EDX+4 faulted. Immediately beforehand, `sub_014DD6E6` ran as an empty unresolved stub, reached by the real detour at `0024FFCB`.

Disassembling the pack XBE proves `014DD6E6` is executable code. It adjusts EAX, preserves flags/registers around `014DD43A`, restores EDI/ESI, then jumps to `0024FFD0`. The stub only advanced ESP by four, omitting the real work and stack restoration.

The process continued after the exception because main.c unconditionally kept it alive for a retired synthetic-display diagnostic. A responding Windows process and continuing audio/vblank logs did not mean the guest was alive.

## Changes

- `tools/pack-seeds.py`: added nine verified injected code entries omitted from conversion: `014DD695`, `014DD69B`, `014DD6E6`, `014DD78D`, `014DEE44`, `014E4E21`, `014E82D6`, `014E837B`, `014E9850`. The last three include MyCareer state transitions and game setup. `014DC5C1` was not blindly seeded: its bytes do not establish a valid helper entry.
- `src/main.c`: after reporting a guest exception, exit with failure unless the explicit `NFL2K5_FORCED_DISPLAY` diagnostic is enabled.
- Regenerating `analysis-softdrink-v04` / `build/gen-softdrink-v04` from the pack's own XBE, SHA256 `5a9dc534b50c7f13b5124bfff9e39c99f96f62be4cc1de33309895c330c75f29`.

## Validation constraints

Use Direct3D 11 hardware rendering and 128 MB. Do not switch to software rendering or a retail-XBE fallback. The Windows capture tool returned foreground Chrome/other desktop content despite selecting the NFL2K5 window; those captures are not gameplay evidence. Use the existing `NFL2K5_PRESENT_SHOT` renderer capture to verify actual game frames without confusing foreground windows.

Prior save enumeration and diagnostic patches remain in the working tree. They are separate from the missing-function fix above and must not be described as resolving Play Now or MyCareer without testing.

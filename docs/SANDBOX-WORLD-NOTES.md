# Sandbox world controls: traced integration

Implementation date: 2026-10-22. These notes describe source investigation and
implementation, **not a gameplay PASS**. No live game or build was run by this
implementation agent.

## Ownership

`src/nfl2k5_sandbox_world.c/.h` owns only optional host state, one scratch guest
allocation, the selected live viewport camera while enabled, and the per-call
simulation delta argument. All mutation APIs must run on the guest thread.
`get_state` copies a lock-protected host snapshot and never reads guest RAM.

Enter validates the existing match clock, period and frontend state. A clock
identity change disables the controls. Leave restores only the still-owned
camera in the same live match. It does not write released memory, save files,
rosters, settings, or static engine timing globals.

## Simulation boundary

The retail `sub_000650A0` calls `sub_00064CD0` before processing Start/pause
input. In `sub_00064CD0`, `ebx+0x104` is copied through x87 into local stack
`esp+0xC`. The local becomes `edi` (unscaled frame time); multiplying it by the
result of `sub_0007A6E0` produces `esi` (scaled game time). Both feed the match
subsystem update calls, including actor update, game clocks and camera work.
The existing native pause path already sets this local to zero.

Generated patch, before this exact anchor in `sub_00064CD0`:

```c
PUSH32(esp, 0x00064CF7u); RECOMP_ABI_CALL(0x00064BE0u, sub_00064BE0); /* call 0x00064BE0 */
```

Insert:

```c
{ extern float nfl2k5_sandbox_world_frame_delta(float);
  MEMF(esp + 0xC) = nfl2k5_sandbox_world_frame_delta(MEMF(esp + 0xC)); }
```

Instruction address for an interpreter pre-instruction hook is `0x00064CF2`.
Do not replace the entire main loop or host clock. No thread suspension, wait,
or change to renderer/APU timing is involved. A requested step supplies one
`1/60` second simulation update and then returns to zero delta. Native pause
can still override that update, so stepping while the native pause menu owns
the simulation needs a separate live test.

## Actual world camera

`sub_00064F80` iterates the game's viewport cameras through `sub_0005E5F0`.
At `loc_00064FA8`, the result is assigned to ECX and passed to
`sub_0002AC80`, which activates it before `sub_00064020` draws the world.
This is distinct from the many HUD, menu, portrait and draft-camera activation
calls. Hook only this viewport call, not all calls to `sub_0002AC80`.

Generated patch, before this exact anchor in `sub_00064F80`:

```c
PUSH32(esp, 0x00064FAFu); RECOMP_ABI_CALL(0x0002AC80u, sub_0002AC80); /* call 0x0002AC80 */
```

Insert:

```c
{ extern void nfl2k5_sandbox_world_camera(uint32_t);
  RECOMP_REGS_OUT(); nfl2k5_sandbox_world_camera(ecx); RECOMP_REGS_IN(); }
```

Interpreter pre-call instruction address is `0x00064FAA`. ECX must already
contain EAX's selected viewport camera at that point.

The camera object's 0x2B0-byte structure has its view basis in the matrix at
`+0x40`, eye at `+0x80`, and reciprocal half-FOV tangent at `+0x270`. These
fields are established by the real camera builder `sub_0002BA10`, which receives
ECX=object, EDX=eye, and stack arguments forward/up/right. It builds the view
and dependent transforms; `sub_0002B510` rebuilds projection/frustum after FOV
changes. The implementation invokes these native routines with every guest
register, returned flag descriptor, SSE register and x87 stack preserved.
The native calls use a private 8 KiB scratch allocation (pose at the beginning,
stack near the end), never the caller's stack. Register-local generated builds
must still synchronize their globals around the hook as shown above.

The first valid camera is captured; a second split-screen camera is left alone.
Each override snapshots the actual native output. Repeated frozen frames that
still contain our own camera do not overwrite the restoration snapshot. A
failed override restores the input camera and reports failure. Frame/input
callbacks only move host camera state; the actual override runs at draw time.

## Current limits and required tests

- These addresses describe the clean retail guest ABI. SOFTDRINK rewriting
  the update, camera or camera-builder functions requires compatibility
  checking and matching interpreter hooks; native compilation alone cannot
  establish compatibility.
- The current helper does not invent player actors from roster records. No
  player movement, spawn, animation-on-field or position-lock API is exposed
  until the live actor map is verified.
- Enter from an actual Situation Mode match; observe the same clock and
  camera identity, paused players/clock, live controller movement, unchanged
  renderer/audio responsiveness, slow motion, one-frame step, FOV and exit.
- Re-enter and restore a scene camera. Reject NaN/infinite/out-of-bounds
  coordinates, invalid FOV, and a mismatched/closed match.
- Test native pause, cutscenes, replay, split screen, match exit and repeated
  entry/exit. Compare Regular and SOFTDRINK separately.
- Screenshots/video helpers, package visibility and input routing are owned
  by the Sandbox core and renderer integration, outside this file's scope.

No all-game physics, ragdolls, unlimited actors, arbitrary-animation browser,
outcome locks, or global state restoration is claimed here.

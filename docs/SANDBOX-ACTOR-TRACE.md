# Sandbox live player trace

October 23, 2026. Source inspection plus the existing read-only capture
`logs/sandbox-overnight-actors-a.json`. No actor transforms were changed during
this research. The capture is an attract/demo scene (period zero), so it is
identity evidence, not a Sandbox gameplay test.

## Confirmed identity chain

- `sub_00061730` copies the two match team headers to `B30864` and `B30A58`,
  rewrites their player-pointer tables to copied compact player records at
  `B30C4C` and `B321A0`, and tags player byte `+34` as home=1/away=2.
  Player records have stride `54` hex. Team `+11C` is a **byte** count.
- `sub_00075B30` builds primary actor nodes at `B3EF60`, node stride `50`
  hex. Root `E60268` and node `+30` form the list. Node `+4` points into a
  per-player `15C0`-byte pose payload at `B3F640`.
- Primary node `+3C` is the exact compact match roster record. The existing
  capture contains 22 direct node-to-roster matches. The descriptor at roster
  `+30` contains a backlink at `+10` to the same primary actor node.
- Node `+38` is the live side object, `E5FC20` for home and `E5FC60` for away.
  `sub_0017B010` independently selects these side objects using the compact
  roster's byte `+34`.
- `sub_000918B0` consumes the node `+3C` compact roster record and reads the
  height byte at `+2B`; it is not a transform object.

## Position and skeleton

`sub_00075D90` reads the translation vectors of matrix 4 (`payload+130`),
matrix 8 (`payload+230`), and matrix 12 (`payload+330`). In the capture, the
first two have Y approximately 5 cm, and matrix 12 has Y approximately 167 cm.
Matrix 12 is useful for a camera focus point.

These are **bone matrices, not current/previous/next actor positions**.
Changing only `payload+330` would change a single bone and corrupt the pose.
`sub_0007A530` copies and translates 25 matrices of stride `40` hex, confirming
that the leading pose data is a skeletal matrix palette.

Node `+18` points at an object whose `+30/+34/+38` form another position vector.
`sub_00076670` uses its X/Z positions for nearest-player distance comparisons.
The complete ownership/update semantics of this object are not established
well enough to expose a teleport control yet.

## Inactive versus hidden

Primary node `+48 != 0` is skipped by native actor rendering **and update**
loops. `sub_00076510(ECX=node)` sets it to 1, while `sub_000764B0` clears it
across the list. It is not established as a safe permanent visibility flag.
Do not leave it changed merely to hide an actor: that also removes the actor
from other native loops and could interfere with slot ownership.

## Secondary list has a different layout

`sub_00074D40` constructs `E60274` nodes at `B3C210` with stride `38` hex,
payload `+4` at `B3C3A0` with stride `640` hex, and a next pointer at `+30`.
Reading a primary node's `+38/+3C/+48` fields from these nodes reads adjacent
storage. The new player module excludes this list.

## Implemented access

`src/nfl2k5_sandbox_actors.c/.h` adds guest-thread-only read access:

- `nfl2k5_sandbox_actors_list(expected_clock, out, capacity)`
- `nfl2k5_sandbox_actor_get(expected_clock, roster_record, out)`

It requires the current gameplay stage, a nonzero matching clock, period
1..8, exact roster identity, native side tags and context, the descriptor
backlink, an active primary node, and finite bounded matrix coordinates.
List traversal is capped at 64 with cycle detection. Output owns its strings
and values. It performs no guest writes or original-game function calls.

Root integration/build/gameplay verification are separate work. No PASS for
live editor focus, grab, move, rotation, spawning or hiding is claimed here.

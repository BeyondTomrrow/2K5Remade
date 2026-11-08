# Sandbox props and team loading: source trace

Investigated October 23, 2026. This is read-only source evidence, not an
implemented feature or gameplay test. No game memory, assets, renderer sources,
or running process was changed by this investigation.

## Rendering paths actually present

The presentation HTML surface is unsuitable for solid world props. In
`external/xboxrecomp/src/kernel/nv2a_gpu_present.inc.c`, `pr_draw_hud` composites
the HUD after the game picture; the presenter binds `s_pr.nodepth`. An HTML
cone projected into screen space cannot be occluded by a player or a stand.
Screen-space aim guides are possible, but must not be described as solid,
depth-tested props.

The real guest API `sub_004251A0`, in `src/recomp/gen/recomp_0029.c`, begins a
primitive with NV097 method `0x17FC`, copies interleaved vertex attributes into
nonincrementing inline-array packets at `0x1818`, and ends the primitive. Its
four stack arguments are primitive, vertex count, vertex-data pointer, and
vertex stride. It obtains the active D3D device from `[0x004409A8]`, flushes
pending state through `sub_004308C0`, and uses the device declaration at
`+0xBC8/+0xBCC/+0xBD0/+0xBD4/+0xC54` to rearrange the attributes.

`sub_00033CE0`, in `recomp_0001.c`, demonstrates a real call:

    sub_004251A0(4, 2, stack_vertices, 0x10)

The function initializes 0x100 bytes of stack data to zero and submits the two
vertices through the current pipeline. This is evidence of the ABI, not a
ready-to-use debug draw function: the current declaration, shader, textures,
and material still determine what the vertices mean.

`sub_00031A40`, also in `recomp_0001.c`, is a lower-level primitive packet
builder. ECX carries primitive, EDX points at an output pushbuffer (zero means
size-only calculation), and the three stack arguments describe optional index
data, start, and count. It emits BEGIN, DRAW_ARRAYS/ARRAY_ELEMENT, and END, but
does not establish a shader or unlit material. Calling either helper alone
with an arbitrary current player shader is unsafe.

## Smallest plausible host extension

The existing GPU backend already owns the correct color/depth pair:

- `GpuSurface` contains `tex/rtv` and `ds/dsv`, plus original width, height,
  widescreen `ox`, and internal render scale.
- `gpu_bind_surface()` selects the guest render target.
- `gpu_draw_batch_unlocked_body()` binds `surf->rtv` with `surf->dsv` and
  already handles triangle and line lists. It consumes `GpuVertex` values.
- `gpu_vertex()` converts guest post-transform screen coordinates to host
  clip space while retaining raw screen position, `oPos.w`, and diffuse color.
- `gpu_depth_state()` mirrors the guest depth convention.
- All immediate-context work must stay under the existing `gpu_lock()`.
- A custom draw must call `gpu_state_invalidate()` afterward. The backend
  caches bound shaders, buffers, textures, viewports, and render targets in
  `s_bound`; changing D3D11 state without invalidation damages later draws.

A narrowly scoped, optional world-mesh callback or submission API here could
draw bounded cones, boxes, spheres, lines, and selection handles without
rewriting the renderer. The title hook must identify a real world viewport
and its matching surface, not simply the last surface at present time (that
can be a portrait, replay, menu, or render-to-texture view). Geometry snapshots
must be copied under a lock; renderer callbacks should never read mutable
guest state or execute guest code.

Depth must be verified before calling this a working prop renderer. The game
does not universally use ordinary D3D normalized Z. `gpu_zmax()` accounts for
16/24-bit integer versus floating depth, and `GpuConsts.depthp` carries
w-buffer mode and clip range. `ps_main` in `nv2a_gpu_d3d11.hlsl` uses either
perspective-correct `oPos.w` or normalized Z, applies clip behavior, floors
integer depth, and outputs `SV_Depth`. NFL 2K5 redraws stadium geometry in a
different clip range, as documented directly above the `depthp` assignment.
Using a new shader with conventional Z would therefore need explicit
comparison against player and stadium occlusion. It is not sufficient to
bind the existing DSV and assume correctness.

The known world camera hook is before `sub_0002AC80` in `sub_00064F80` at
guest instruction `0x00064FAA`. It identifies the actual selected viewport.
The following `sub_00064020` saves/restores a 0x2B0 camera and makes native
render calls, but is not a general game-world-end boundary by itself.

## Native team reload evidence

`src/nfl2k5_teams.c` already supports the game's resident, historic, and custom
teams. Roster root `[0x00B72918]` supplies resident table `+0x1C` and count
`+0x18`; entries are 500 bytes. Team player count at `+0x11C` is a byte.
Historic count/table are `+0x58/+0x5C`.

Historic loading uses the original resource queue/wait/lookup/import sequence:
`sub_00043F50`, `sub_000432D0`, `sub_000449E0`, `sub_000C2300`,
`sub_000C1030`, then `sub_000432F0`. Custom `team.rost` loading reuses the
import function and optional team identity/roster hooks. Only the two spare
USER slots are borrowed; an occupied created-team slot is preserved.

`sub_00061730`, in `recomp_0002.c`, is the match roster copy operation:

- Inputs ECX and EDX are the two source team records.
- It copies 0x1F4 bytes into `0x00B30864` and `0x00B30A58`.
- `sub_000C3C60` copies their player records into `0x00B30C4C` and
  `0x00B321A0` respectively.
- Header player pointers are then rebuilt to those copied player records,
  stride 0x54, with side byte `+0x34` set to 1 or 2.
- `sub_000617E0` obtains selected teams through `sub_00077B40` and
  `sub_00077B00` and then calls `sub_00061730`.

These copied roster records are distinct from live actor/animation objects.
Changing a source team pointer or invoking this copy during an existing game
does not prove that player model assets, skeletons, uniforms, AI formations,
and actor references are rebuilt. Continue to use the queued native quit/load
path for team changes until a complete asset/actor reload is traced and
tested. The current Sandbox implementation already provides that safe path.

## Test requirements for a world prop extension

1. One stationary untextured box on the field, no collision claim.
2. Free-camera orbit with a player and stadium passing both in front and
   behind the box; confirm occlusion in both directions.
3. Internal resolutions 1x and the user's configured higher setting, plus
   widescreen, photo mode, replay, and portrait transitions.
4. Creation, duplicate, transform, delete, undo/redo, save/load, and match
   exit/re-entry with an explicit bounded object count.
5. Regular and SOFTDRINK separately. Optional rendering must be absent in
   normal gameplay and must restore/invalidate all backend state.

No runtime test or implemented native prop renderer is claimed by this note.

## Follow-up: scoped actor rendering without touching simulation

### Actual draw queue and per-player boundary

`sub_00075D90` is a viewport render preparation loop, reached from
`sub_00064E80 -> sub_0011A8F0`. It walks `E60268` using ESI as node and skips
nodes with nonzero `+0x48`. It is **not** the final player draw call.

At `loc_00075E74`, it prepares a six-dword packet on its stack:

| Packet offset | Proven source |
|---|---|
| +0x00 | `[node+0x3C]`, copied roster record |
| +0x04 | `[node+0x04]`, or the native adjusted palette from `sub_0007A530` |
| +0x08 / +0x0C | Initial zero fields |
| +0x10 | Packed render flags assembled by this function |
| +0x14 | Animation-derived scalar |

When `sub_0007DA50(node)` allows it, `sub_00091800(ECX=packet)` copies these
24 bytes into a bounded native queue (`B652CC`, stride 0x15E0). It returns a
place for native supplementary data. `sub_00090030` matches those packets
to persistent renderer records at `[B652D0]`, stride 0x54. A renderer record
stores the packet at `+0x2C`. `sub_0008F800` explicitly copies packet+0 into
renderer-record+0, confirming the roster identity relation.

`sub_000913B0`, later in the same `sub_00064E80` viewport, prepares/culls
those renderer records and calls `sub_0008FD90` twice with two lists.
`sub_0008FD90` is the useful per-player rendering boundary:

```text
loc_0008FDB0:
    ESI = list[index]                 // renderer record
    EAX = [ESI+0x2C]                 // queued packet
    test [EAX+0x10], 0x04000000
    EBX = [EAX+0x04]                 // palette
    ... body/head/material handling ...
loc_0008FFB6:                        // all iteration branches converge here
    ... next index ...
    [ESI+0x2C] = 0
```

Do not insert a condition between the original `test` and branch unless its
guest returned flags are preserved. A generated hook should either come
before the `test`, or save/re-publish the descriptor when synchronizing guest
registers. A hook changing local EBX must update generated register locals,
not only global `g_ebx`.

### Minimal safe candidate

A render-only hide command can skip the per-player iteration to
`loc_0008FFB6` after confirming the packet's roster matches a still-owned
live node. It must not change node+0x48: that flag is also read throughout
simulation, team traversal, and animation code. This would hide this player
mesh pass only; it is not deletion or simulation removal.

A render-only translate/rotate/scale candidate can avoid live palette writes:

1. Validate packet+0 roster against the live actor map and current match.
2. For the ordinary body branch only (`packet[0x13] & 1 == 0`), copy the
   first 25 native matrices to a **private, aligned guest allocation owned by
   that actor/session** and transform that copy.
3. Replace local EBX with the private copy for this draw iteration.
4. At `loc_0008FFA4`, ECX is `[rendererRecord+0x20]` and EDX=EBX;
   `sub_00021900` simply writes `[object+0x14]=EDX`. The subsequent
   `sub_00021860` draws the object through `sub_000243D0`.
5. Save/restore the renderer object's original `+0x14` pointer by the common
   `loc_0008FFB6` boundary. Do not mutate the original node palette, geometry
   matrix, copied roster, physics position, or AI data.

The shape header `[object+8]` has physical and derived bone counts at
`+0x50/+0x52` (16-bit). `sub_00022C00` loops the physical count at stride0x40
and expands derived matrices into its own render scratch. Reject unexpected
counts rather than reading beyond a 25-matrix copy. `sub_000243D0` invokes
that conversion before submitting render commands.

The alternate path (`packet[0x13] & 1 != 0`) is **not yet covered by this
candidate**. It draws one object using EBX+0x640 and constructs another
palette on the stack through `sub_000DE980`; do not assume all0x15C0 bytes
are world matrices and transform them indiscriminately. `sub_000DE980` uses
signed-byte bone remaps from B652A0/B759B0 and calls the native palette builder
`sub_000233C0`. Additional tracing/tests are required to cover that path.

### Matrix convention, proven from math

`sub_00021520` transforms a local point by the palette as follows:

```text
world.x = x*M[0] + y*M[4] + z*M[8]  + M[12]
world.y = x*M[1] + y*M[5] + z*M[9]  + M[13]
world.z = x*M[2] + y*M[6] + z*M[10] + M[14]
```

Thus translations occupy byte offsets0x30/0x34/0x38 and each 0x10-byte row
is a world basis vector. A world-space Y rotation/scale applies to all three
basis rows and to each translation around a chosen world pivot; it must
preserve homogeneous elements. For a convention of positive yaw taking
world+X toward world+Z:

```text
x' = cos(yaw)*x - sin(yaw)*z
z' = sin(yaw)*x + cos(yaw)*z
```

For a bone translation, subtract pivot, scale/rotate, then add pivot and
requested offset. Uniform scale is easier to bound correctly than nonuniform
scale because normals and all limbs retain proportional dimensions.

This is source evidence for an implementation candidate, not gameplay
verification. Culling occurs earlier (`sub_00090190`); a large visual move can
therefore retain the original culling bounds until a matching earlier hook is
implemented. Shadows/nameplates are separate passes and will retain original
positions if only this final mesh draw is transformed. Label controls as
visual posing, keep motion bounded, and test player/helmet/uniform alignment
before exposing them as working editor tools.

Isolated arithmetic check: 1,000 deterministic random affine matrices and
points compared (A) editing the matrix and then transforming the point with
(B) transforming the point first and applying the world pivot/scale/yaw/offset
afterward. PASS; maximum double precision difference 2.84217e-13. This checked
only the formulas above and did not read or write guest state.

An optional implementation candidate now exists in the new
`src/nfl2k5_sandbox_pose.c/.h` files. It uses per-ID private palettes and
restores the renderer-object palette pointer at an explicit end hook. It
requires root integration and compilation; this investigation has not built
or run it. Offsets are bounded to five metres per axis, yaw to180 degrees,
and uniform scale to0.25..3. It uses the editor's Y=0 pivot plane rather than
claiming to query terrain collision. Unsupported palette/shape paths return
the original palette untouched. Its included C selftest has not yet run.

# Sandbox animation sampling trace

Read-only source and file inspection, 2026-10-23. No game was controlled, no
process memory was written, and no build was run for this investigation.

## Result

An initial animation picker can use the eight existing baked player clips in
`mods/eras/draft/files/draftposes.bin`. Sampling those floats and constructing a
private 25-bone palette does not require changing a live animation controller,
loading the Eras draft scene, or invoking a native clip lookup. This is the
smallest route compatible with the Sandbox draw substitution already present
in `src/nfl2k5_sandbox_pose.c`.

The draft show's `actor_clip`, `sample`, `actor_tick` and `actor_write` are
**static host C helpers**, not native game animation entry points. Their
algorithms are reusable, but the helpers themselves depend on draft-show
globals and stage coordinates. Do not invoke the draft show to animate a field
player and do not write to its palettes or the live player's original palette.

This investigation establishes an implementation route, not a witnessed
animation test. No Sandbox animation has been verified in game by this trace.

## Existing asset and ownership

`src/nfl2k5_eras_draftshow.c:257` loads `draftposes.bin` through
`nfl2k5_eras_path("draft\\files\\draftposes.bin", ...)`. It allocates each clip's
samples with host `malloc`; these are host arrays, not relocated guest clip
headers. The scene loader and its `ERASDRAFT` resource context are not needed to
read this file.

The builder is `tools/eras/draftshow/build-draftshow-pack.py:243`. It samples the
player's disc using the existing Mod Tools animation decoder and writes this
gitignored, disc-derived output. No need to rebake it during a game or on the
render thread. Missing output should disable the picker with an explanation;
it should not activate Eras or silently install an animation package.

File format (little endian):

| Field | Layout |
|---|---|
| Magic | 8 bytes, `DSPOSE01` |
| Clip count | `uint32_t` |
| Per-clip header | `char name[32]`, `uint32_t frames`, `float fps`, `uint32_t reserved` |
| Per frame | 25 quaternions `[w,x,y,z]`, then root trajectory `[x,y,z]` in centimetres |
| Frame size | 103 floats / 412 bytes |

Read-only inspection of the existing file found 329,552 bytes, eight clips,
zero trailing bytes, finite samples throughout, and quaternion norm-squared
within approximately 0.9998115..1.0001379. These are data checks only.

| Picker key | Source resource | Description | Frames | Seconds `(frames-1)/fps` |
|---|---|---|---:|---:|
| `idle` | `archive:2514/3` | Anthem standing idle | 106 | 7.0000 |
| `loop` | `archive:2530/1` | Coin toss standing idle | 99 | 6.4833 |
| `walk` | `archive:2761/1` | Captains walking out | 104 | 6.8500 |
| `shake` | `archive:2763/7` | First person handshake | 61 | 3.9833 |
| `shake2` | `archive:2531/1` | Coin toss handshake | 104 | 6.8500 |
| `raise` | `archive:2723/2` | Helmet raise | 68 | 4.4167 |
| `trophy` | `archive:2725/3` | MVP trophy presentation | 137 | 9.0333 |
| `walk2` | `archive:3106/101` | Walk to huddle talking | 120 | 7.9000 |

Clip resources were identified by the builder; no animation-name search of the
running game is involved. The baked file contains movement samples, not native
gameplay events, attached props, ball ownership, audio cues, or controller
state. A helmet-raise animation alone does not spawn a helmet in the hands.

## Host sampling

`src/nfl2k5_eras_draftshow.c:660` implements the existing sampler:

1. Clamp `time * fps` to `[0, frames - 1]`.
2. Select adjacent frames and the fractional position between them.
3. For each joint, use the quaternion dot product to choose the shortest
   hemisphere, linearly interpolate all four components, then normalize.
4. Interpolate the three trajectory components linearly.

`actor_clip` at line 1114 is a host `strcmp` lookup followed by setting the
actor's clip index, bounds, speed, looping flag and time. `actor_tick` advances
time and applies an optional 0.3 second normalized quaternion crossfade.

For a new bounded loader, additionally reject non-finite/implausible frame
rates, duplicate or unterminated keys, excessive counts, truncated input,
non-finite samples and near-zero quaternions. Keep the previous valid cache
until a complete replacement has passed validation. Do not reproduce the
draft loader's partial-allocation failure path.

## Bone order and remaps

The baked file already contains **25 logical player joints in final order**.
The builder calls `sample_pose(..., loop=False)` with family `player`, so the
retail channel map, mirrored mapping and derived wrist completion have already
been applied. Do not apply those remaps or complete the wrists a second time.

The expected player joint names and parents are checked by
`.tmp-2k-football-mod-tools/mod_editor/core/nfl2k5_animation.py:30`:

| Index | Name | Parent |
|---:|---|---:|
| 0 | root | -1 |
| 1 | lfemur | 0 |
| 2 | ltibia | 1 |
| 3 | lfoot | 2 |
| 4 | ltoes | 3 |
| 5 | rfemur | 0 |
| 6 | rtibia | 5 |
| 7 | rfoot | 6 |
| 8 | rtoes | 7 |
| 9 | waist | 0 |
| 10 | thorax | 9 |
| 11 | neck | 10 |
| 12 | head | 11 |
| 13 | lcollar | 10 |
| 14 | lhumerus | 13 |
| 15 | lelbow | 14 |
| 16 | lwrist | 15 |
| 17 | lhand | 16 |
| 18 | rcollar | 10 |
| 19 | rhumerus | 18 |
| 20 | relbow | 19 |
| 21 | rwrist | 20 |
| 22 | rhand | 21 |
| 23 | lshoulderpad | 10 |
| 24 | rshoulderpad | 10 |

The Mod Tools reference player skeleton comes from outer 3 / chunk 113,
`LO_res`. The commissioner also uses that player skeleton. A referee can have
25 joints too, with a different order: a count of 25 alone does not prove
compatibility.

For a selected Sandbox player, obtain the actual body shape from the validated
draw object (`object+8`), not a cached Eras commissioner scene. Shape `+0x50`
is a 16-bit transform count; `+0x64` points at the transform table. Each
transform is `0x70` bytes:

| Offset | Meaning used here |
|---|---|
| `+0x40` | Absolute bind position used for inverse-bind translation |
| `+0x50` | Three local bind-position floats |
| `+0x60` | Joint name pointer |
| `+0x64` | Signed parent index (`-1` for root) |

Require the 25 expected names/parents, finite local translations, bounded
pointer/string reads and parents strictly earlier than their children. Read
the selected shape's bind translations; a stock JSON skeleton does not prove
that a modified SOFTDRINK body has the same proportions or bind data.

## Constructing the private palette

`actor_write` at `src/nfl2k5_eras_draftshow.c:801` converts sampled local
quaternions into column-vector 3x3 matrices for its intermediate host math:

```
world_rotation[root] = sampled_local_rotation[root]
world_position[root] = bind_local[root]
world_rotation[i] = world_rotation[parent] * sampled_local_rotation[i]
world_position[i] = world_position[parent] +
                    world_rotation[parent] * bind_local[i]
```

It then applies its external world placement and transposes the rotation into
the game's row-vector 4x4 affine layout. Translation is floats 12,13,14;
floats 3,7,11 are zero and float 15 is one. There are 25 matrices, `0x40`
bytes each, `0x640` bytes total.

The renderer expects **current joint matrices**, not matrices that already
include inverse bind. Its skinning path applies
`T(-bind_absolute[i]) * current[i]`. Applying inverse bind twice is incorrect.

Draft-specific constants are not field coordinates. Draftshow uses
`SCALE=6`, `STAGE_Y=474`, and a root-height baseline of 107 cm. The trophy clip
starts with root Y 183.125 cm because its source actor stood on a platform;
the draft show subtracts the clip's first Y before applying its stage baseline.
Do not copy these constants or the trajectory's absolute first-frame height
into a field player.

For an initial in-place pose preview, explicitly anchor the private output at
the current player placement and use only bounded relative trajectory/root
changes, or omit root motion as a stated preview limitation. Determining an
exact external root from an already posed live palette needs care: its root
matrix contains the native controller's pose, position and scale. Treating it
as an unposed bind transform would add rotation/translation twice. Validate
root placement and feet in game before enabling continuous walk displacement.

The native player pipeline additionally performs body-proportion processing
in `sub_00092140` before hierarchy expansion and `sub_00093850` afterward.
The draft-show host math does not reproduce those live-player passes. This
means the eight baked clips provide a safe isolated **visual preview route**,
but do not prove a fully native-equivalent animation for every body/LOD.

## Native ABIs recovered from generated code

All addresses below refer to the current generated source. They are Xbox
register/guest-stack ABIs, **not ordinary host C functions**. Generated function
headers that say cdecl/zero parameters do not describe the real inputs.

| Function | Inputs / return cleanup | Evidence / concern |
|---|---|---|
| `sub_000DF700` | ECX=relocated clip root header; EDX=logical joint enable **bitmask**; guest stack args `(float time, map_ptr, output_quat_ptr)`; `ret 12` | `src/recomp/gen/recomp_0006.c:213546`; root+24 packed samples, u16 root+2 frames, byte+C rate, float+10 multiplier. Full 25-joint mask `0x01ffffff`. Negative mapping entries leave output slots untouched. |
| `sub_003CA3D0` | ECX=quaternion array; EDX=joint **count**; stack arg `(output_matrix_ptr)`; `ret 4` | `src/recomp/gen/recomp_0026.c:66841`; input stride0x10, output stride0x40, scalar-first quaternions. Different EDX meaning from DF700. |
| `sub_000233C0` | ECX=shape; EDX=output current palette; stack args `(local_matrices, external_root_matrix)`; `ret 8` | `src/recomp/gen/recomp_0000.c:69730`; adds bind local translations and multiplies local matrix by parent/output or external root. |
| `sub_00021900` | ECX=render object; EDX=current palette; no stack args | Stores palette in object+14. Existing Sandbox hook substitutes this pointer temporarily. |
| `sub_00021860` | ECX=render object; no stack args | Consumes object+8 shape and object+14 palette through the native draw path. |

DF700 reads loader-relocated guest pointers. An offset in a raw SMCD disk chunk
is not a valid native root pointer. Its fallback map is `0x4F24A0`; the retail
player map is at `0x51CD70`. The mirroring flag chooses the other byte of each
map pair and changes quaternion signs. A direct native caller would need a
zero/identity-initialized full output, logical-family completion, owned
relocated clip memory, a valid guest stack and preservation of the recomp
register/FPU state. The baked route avoids all of that.

Do not call `sub_0028B140` on a live controller as a sampler: it writes its
controller's last sample time at `+8` and follows native resource/global state.
Do not call `sub_00093800` on a live player just to pose a preview: it uses
shared player scratch globals, runs proportion passes and builds two palettes.
`sub_00022C00` also owns renderer scratch/cache state; the existing draw path
should call it, not an independent preview thread.

## Integration boundary and tests still needed

Keep a host-owned clip cache and per-player clip/time state keyed by the
existing bounded Sandbox ID plus stable identity hash. Store only a clip key,
playback settings and times in scenes; never persist shape/palette/controller
pointers. Clear state when the match clock or roster identity changes.

At the supported ordinary-body draw hook, validate the current model, read its
bind table, sample into a host scratch array, form a complete finite private
palette, and hand that palette to the existing paired begin/end substitution.
The original actor palette and simulation controller remain untouched. Do not
allocate, load files or decode source archive data per draw. End the override
on the same draw boundary and restore object+14 only if it still points at the
Sandbox replacement.

Unsupported high-body/remapped paths must retain their native palettes. The
existing pose hook excludes packet flag `0x01000000`; those paths can use a
second palette at `+0x640` and additional remaps. This trace does not authorize
reusing a single 25-bone palette there. Culling, shadows, ball attachments and
nameplates still use the original simulation placement.

Before a feature can be reported as tested, cover finite sample/normalization
and parent-hierarchy unit checks, native-vs-host quaternion/matrix conventions,
start/middle/end of all eight clips, play/stop/scrub/loop, at least two body
sizes in both Regular and SOFTDRINK, front/back camera views, head/helmet/body
alignment, player leaving the field, match teardown, native gameplay resuming,
and original palettes restored after every draw. Current status: source trace
and baked-file validation completed; all Sandbox animation gameplay checks
are **NOT TESTED**.

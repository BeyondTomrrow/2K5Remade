# Player pose / skin continuation, 2026-10-14

Integration entry point: `work/export_player_rig.py`. NumPy plus sibling
`skel_seam_fit.py`, `skel_probe.py`, `skel_rotation_experiment.py`; no main-module
imports. All research source changes are confined to work/.

## Recommended experimental default

Column vectors; local rotation **Rz @ Ry @ Rx**, raw angles times 2*pi/65536;
world = parent_world @ local. Source player is treated as Y-up; do not apply
the stadium Z-up conversion. A common orthogonal world-root conversion cannot
change any seam distances and therefore cannot resolve an Euler order.

Two source04 cross-node, cross-bone rigid hip boundary correspondences support
ZYX. Each uses Clst weight4096, so partial-cluster assumptions do not affect
these comparisons. They were selected from the UNION of near matches under
all six orders at distance <0.00015, not from the winning order alone.

| Pair | Node/vertex/cluster/bone A | Node/vertex/cluster/bone B |
|---|---|---|
| 1 | 55 / 255 / 9 / 2 | 206 / 83 / 23 / 7 |
| 2 | 55 / 282 / 23 / 7 | 206 / 60 / 9 / 2 |

| Column-vector rotation product | RMS distance, fixed two pairs |
|---|---:|
| Rx Ry Rz | 0.006650781224 |
| Rx Rz Ry | 0.006385766630 |
| Ry Rx Rz | 0.001033973661 |
| Ry Rz Rx | 0.000283554508 |
| Rz Rx Ry | 0.005650078414 |
| **Rz Ry Rx** | **0.000026681623** |

ZYX is ~249x better than XYZ and ~10.6x better than the next order on these
two pairs. This is strong *local* geometric support, not a general proof from
independent rigs: file05 contributes three neck seams that agree under every
order (the relevant local rotation is about one axis). Files06,07,11,12,21
contribute no rigid cross-part near pairs at this threshold. The broad whole
boundary nearest-distance score does not consistently pick ZYX; it includes
unmatched boundaries, clothes/variant overlaps, and parts that cannot touch.
Do not label its RMS as measured error on known seam correspondences.

## Exact mesh attachment and draw-batch correction

Node+4: u32 count and count u32 palette pointers. Each pointed block has u32
entry_count and <i16 slot,u16 cluster> entries. **These blocks update resident
slots**. Copy the prior palette and apply each batch's updates; do not treat
each list as a complete replacement.

Node+0 draw table: u32 draw_count, then draw pointers. Draw headers are
<u16 triangle_count,u16 batch_id,u32 zero,u32 zero,u32 zero>, followed at +16
by triangle_count+2 u16 strip indices. The batch ID chooses the corresponding
cumulative palette snapshot. Resolve each referenced vertex's scalar/3 slot
using that snapshot. File04 node55 uses all seven batch IDs 0..6. In its second
draw, vertex233 uses slot6, which is omitted by batch1's updates and must be
retained from batch0.

All 26 fixtures resolve this way: 78,036 referenced vertices, zero vertices
assigned different clusters by different draws. The new loader asserts draw
headers, palette/cluster/vertex ranges, exact divisibility of selectors, and
retains actual triangle winding. The earlier skel_geom_bridge.py identifies
multiple-palette ambiguity but does not contain this final draw resolution;
use skel_seam_fit.load for integration.

## Partial-cluster candidates

For each Clst row, w=raw_u16/4096; parent cluster points to the final row for
the source bone's skeleton parent. Source04 has 1,929 partially weighted
vertices out of 16,344. Tested at fixed ZYX:

* linear_world: linearly interpolate the full parent and child world matrices.
* linear_pivot: interpolate their 3x3 blocks, retain the full child joint origin.
* euler_trs: scale local Euler angles AND local translation by w.
* euler_pivot: scale local Euler angles; retain full child local translation.
* slerp_trs: SLERP identity to local rotation by w; scale local translation by w.
* slerp_pivot: same SLERP, retain full child local translation.

Boundary metric: for every unique source-node/cluster/local-position boundary
sample, find its exact nearest point on a DIFFERENT source node AND cluster.
Boundary edges occur in one nondegenerate triangle. This is a broad candidate
ranking, not a guaranteed seam correspondence set. NumPy block search is
exact and deterministic, with bounded working memory; SciPy is not required.

| Model at ZYX | File04 RMS (6216 samples) | File05 RMS (2795) | File11 RMS (2295) |
|---|---:|---:|---:|
| linear_world | 0.1196608914 | 0.1270456467 | 0.1591960939 |
| linear_pivot | 0.1105769311 | 0.1128030157 | 0.1244159335 |
| euler_trs | 0.1190437888 | 0.1266374068 | 0.1589661525 |
| euler_pivot | 0.0928847037 | 0.1049026182 | 0.1199523149 |
| slerp_trs | 0.1190360842 | 0.1265779122 | 0.1589687532 |
| **slerp_pivot** | **0.0927543023** | **0.1046573248** | **0.1198428810** |

SLERP at the full child origin is the best of these six at ZYX on all three
samples. Linear parentworld/childworld blending remains technically viable
as a candidate, but is substantially worse here. No tested partial model has
created a new near-exact cross-cluster seam beyond the rigid matches. Treat
the partial-frame formula as unresolved; further geometric or runtime evidence
is required. Cluster helpers with one-hot weights are appropriate for preserving
a provisional frame without claiming that conventional parent/child LBS is the
game's deformation algorithm.

## Delivered preview and distinction from exact game skinning

outputs/player_00000004_experimental_zyx_slerp_pivot.glb has 75 source joints,
137 active mesh parts, 16,344 vertices, 15,873 nondegenerate triangles, and 1,929
vertices with two nonzero skin weights. All active source variants are retained;
overlapping parts are not proof of a transform failure.

The exporter first places vertices in provisional world coordinates using the
cluster frame, then writes a conventional glTF skin with child weight w and
parent weight 1-w. Rigid rows weight the source joint alone. Inverse-bind
matrices correspond to the source75 joint world matrices. The default rest
pose is preserved, but animation deformation is an EXPERIMENTAL LBS hypothesis;
the existing file does not use cluster helpers and contains no animation.

The source now also retains the raw Skel header, raw Clst rows and draw-batch
audit in glTF extras for subsequent exports. The original GLB already being
rendered was not overwritten when those metadata additions were made.

Independent serialized-GLB readback checks indices, references, finite values,
normalized weights and the full skin rest equation. Maximum bind-rest error
after float32 inverse-bind serialization is 6.528911528134581e-8 source units.
This validates the exported skin mechanics, not the unproved game pose.

## Reproduce

python work/report_player_seams.py
python -m unittest discover -s work -p test_player_seams.py -v
python work/export_player_rig.py
python work/check_player_glb.py outputs/player_00000004_experimental_zyx_slerp_pivot.glb

report_player_seams.py saves full six-order and partial candidate statistics,
per-pair provenance/coordinates, thresholds, RMS/median/p90/near-match counts,
and all26 draw attachment audits to work/player_seam_statistics.json.
The original five skeleton tests and three new real-fixture tests pass.

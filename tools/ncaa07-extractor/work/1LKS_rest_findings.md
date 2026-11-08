# 1LKS rest transforms: actual binary findings

2026-10-14. Read-only analysis of all 11 extracted `.1lk` files and the player
Skel in `outputs/plydata_extracted/00000004.tmd`. No main files or assets edited.
Reproduce with `python work/ilk_rest_probe.py`. This probe imports the existing
read-only skeleton decoder in `work/skel_probe.py`; it does not alter it.

## Strongest result

The primary body rig `00000011.1lk` and primary hand rigs `00000019.1lk` and
`0000001a.1lk` reproduce the player Skel rest translations exactly and its raw
rotations at 1/16 precision:

```
LKS.translation[i] == Skel.translation[name(i)]
LKS.rotation_i16[i][axis] == trunc(Skel.rotation_i16[name(i)][axis] / 16)
```

ZERO mismatches across 192 translation components and 192 rotation components
(64 node records across three rigs; 62 unique names, because wrists overlap).
This is an exact numerical correspondence, not an anatomical plausibility test.

Truncation is toward zero, not an arithmetic shift: Skel main -300 becomes
LKS -18, whereas -300 >> 4 would be -19; Skel ankle -7 becomes 0, not -1.
Thus `LKS_raw * 16` is a coarse approximation to Skel raw, not a lossless inverse.

If Skel full-turn65536 is correct, LKS full-turn4096 follows. The match alone
does not establish either absolute angular unit or Euler multiplication order.
It establishes a shared component representation with a factor-of-16 precision
relationship, independent of the chosen angle-to-matrix conversion.

## Complete layout, all 11 files

All numeric values are little endian, with offsets relative to file start.

| Byte offset | Layout | Evidence |
|---|---|---|
| 0x000 | ASCII `1LKS` | every file |
| 0x004 | u16, observed 0 | retained as unknown/version |
| 0x006 | u16 active node count N | 8, 11, 17, 29, or 30 |
| 0x008 | u32 node table pointer | equals 0x590 + 20*N |
| 0x00c | u32 name table pointer | always 0x590 |
| 0x010 | 64 records, `<3fHH`, 16 bytes each | translation, parent index, zero flag |
| 0x410 | 64 records, `<3h`, 6 bytes each | coarse rest rotation components |
| 0x590 | N records, `<I16s`, 20 bytes each | node pointer and NUL-padded name |
| node pointer | `<BBHI` then link_count u32 words | link count byte, depth candidate, node index, parent pointer, links |

Only the first N transform slots are active. All unused translation and rotation
slots are byte-zero in every file. This fixed capacity explains the formerly
unidentified 1,408-byte region after the header:

```
64 * (3 float32 + parent_u16 + flag_u16) + 64 * (3 i16)
= 1024 + 384 = 1408 bytes
```

The translation-table parent index agrees with the parent pointer in the node
graph for all 210 named nodes. Rig roots have parent_u16 65535; every observed
translation-record flag_u16 is zero. This differs from the model Skel's root
encoding and flag use; do not copy a Skel decoder's sentinel rules blindly.
An animation hand-rig root may retain its model-local wrist offset even though
the forearm parent is not present in that smaller rig.

Node indices match name indices, but nodes are not always physically stored in
name order. Sort physical node offsets when checking record extents. Each
node's `8 + 4*link_count` bytes reaches the next physical node, and the final
node reaches EOF. The probe accounts for the entire file with header, fixed
transform arrays, names, and node/link records. The outgoing link words need
further semantic work; leaf links can refer onward in traversal, so they must
not all be labeled direct children.

There is no remaining unidentified matrix/quaternion block in this layout.
The floats found here are translations; interpreting four consecutive floats
as quaternions incorrectly consumes the parent/flag word.

## Exact rest examples

| Bone | Skel raw | 1LK raw |
|---|---|---|
| main | (-300, 0, 0) | (-18, 0, 0) |
| rhip | (-1075, -1339, -691) | (-67, -83, -43) |
| rknee | (4052, 0, 0) | (253, 0, 0) |
| rankle | (20700, 440, -7) | (1293, 27, 0) |
| rclavicle | (-1132, -544, 18297) | (-70, -34, 1143) |
| rpinkyBase | (-3042, 19, -2235) | (-190, 1, -139) |
| lthumbBase | (8320, 1105, -3627) | (520, 69, -226) |

The full per-node values and source offsets are printed by the probe.
`00000011.1lk` SHA-256:
`d9c87bcdb6f39b3765bac95b5b952335a68b5eec7c60d8a4e1583ba65560937b`.

## Alternate rigs matter

The earlier finding that `00000011` and `00000012` have identical decoded names
and node links does NOT mean they have identical transforms.

| 1LK | Nodes | Names in player Skel | Exact translation triples | Exact trunc(Skel/16) triples |
|---|---:|---:|---:|---:|
| 00000011 | 30 | 30 | 30 | 30 |
| 00000012 | 30 | 30 | 2 | 7 |
| 00000013 | 11 | 0 | 0 | 0 |
| 00000014 | 29 | 29 | 1 | 6 |
| 00000015 | 8 | 3 | 1 | 0 |
| 00000019 | 17 | 17 | 17 | 17 |
| 0000001a | 17 | 17 | 17 | 17 |
| 0000001b | 17 | 17 | 11 | 8 |
| 0000001c | 17 | 17 | 11 | 8 |
| 00000024 | 17 | 17 | 0 | 0 |
| 00000025 | 17 | 17 | 0 | 0 |

In 00000012, main raw is (-8,0,0), rknee (250,0,0), rankle (1217,0,0),
versus (-18,0,0), (253,0,0), (1293,27,0) in 00000011. Its rhip translation
(-0.13,-0.148,0.1183) differs from the primary player's approximately
(-0.097103,-0.096667,0.118272). These are different rig/rest data, not numerical
noise. Select the rig by real resource linkage or fixture matching, not names
alone. The audit's hand pose-distance rows use player Skel rotations in each
rig's name order; they intentionally do not select among same-name variants.

## Raw hand animation corroboration

`0000001e.dat` sample 0, decoded as a u16 header followed by 17 signed-i16 triples,
is very close to player Skel in RIGHT-hand node order. Excluding the animated
wrist, 25 of 48 finger components match exactly; total absolute difference is
486, maximum individual difference 74.

| Right-hand bone | Skel rest raw | Animation sample 0 raw |
|---|---|---|
| rpinkyBase | (-3042,19,-2235) | (-3038,21,-2235) |
| rpinkyLow | (203,123,4747) | (213,130,4776) |
| rpinkyMid | (190,8,1920) | (191,7,1931) |
| rpinkyHi | (386,35,4080) | (386,35,4080) |

This strongly corroborates high-precision animation triples using the same raw
rotation scale and name order as Skel, and supports absolute local rotations
for this sample over a zero-centered rest-delta interpretation. It is not an
exact rest fixture and does not prove a resource-ID mapping. A pose-distance
comparison favors right order for 1d/1e/26, left order for 1f; confidence is much
stronger for the near-rest 1e sample than the more posed banks.

## What this resolves and what it cannot resolve

Resolved: local translations and parent indices in 1LK, full rotation-table
location and extent, name correspondence, exact coarse/fine rest relationship,
and strong hand-animation corroboration of the fine raw component scale.

Unresolved: absolute angle units, rotation order, intrinsic versus extrinsic
convention, handedness, and bind-pose composition. Both formats store component
triples, not an independently known rotation matrix. Applying the same arbitrary
Euler convention to both necessarily preserves their numerical agreement, so
that agreement cannot select one convention without independent evidence.
Full-turn65536 for Skel/raw animation and full-turn4096 for 1LK remain supported
working hypotheses, not newly verified units.

The body A coefficients do not supply an exact rest fixture either: the closest
one-frame A array to the 30-node Skel raw pose in the scanned corpus had L1
component difference 80,820, max 7,043, in `0000023d.dat` at block offset 1,852
(and matching values in `00000346.dat`). Do not equate body A with rest rotations
or subtract the 1LK rest pose without solving the frame reconstruction.

Next evidence needed: the parallel geometric seam analysis or runtime rotation
conversion code can choose angular scale/order. The now-verified 1LK table is
useful for confirming the correct rest rig, naming tracks, and rejecting a
false quaternion interpretation, but supplies no independent matrix constraint.

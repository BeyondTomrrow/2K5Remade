# Player Skel: binary evidence and independent decoder

Inspected actual extracted bytes on 2026-10-14 with Python, without importing or changing the main application. All new source and findings are under work/. No source fixtures were written. Primary fixture: outputs/plydata_extracted/00000003.tmd (201,888 bytes), SHA-256 690263124a570e7d5dbdb6ac11b80bd09ccfa231af8ed77df04f6640b852aa20.

## Result and confidence

Names, parent indices, and local translations are supported by all 26 fixtures, including two distinct skeletons (75 and 32 bones). Rotation table bytes and strides are established; the exact angular encoding and composition convention are not established. The final float triplets are almost certainly scale, but all are near one, so their function cannot be distinguished experimentally using these fixtures alone. Treat these as stored rest/bind-transform components; runtime bind-pose semantics and inverse bind matrices remain unverified.

A separate Geom -> palette -> Clst -> Skel mapping is verified structurally against 78,036 vertex selector values in all 26 files. This is the useful route for assigning local mesh vertices to bones. Some nodes contain multiple draw palettes and require batch assignment before their selectors resolve uniquely.

## Exact binary layout

The ASCII Skel at file offset 0x20 is a TMdl child-table tag, NOT the start of its payload. The descriptor is (offset=0x80, size=0xED4, count=1); actual payload is [0x80,0xF54).

All numeric fields below are little-endian. Offsets in the target address the TMdl/file beginning, not the Skel beginning. Since every extracted TMdl starts at file offset zero, these fixtures cannot distinguish model-relative from absolute file-relative addressing for an embedded model.

| Skel-relative offset | Type | Target value | Interpretation |
|---|---|---|---|
| +0x00 | u32 | 75 | bone count N |
| +0x04 | u32 | 0x98 | name table pointer |
| +0x08 | u32 | 0x550 | translation/parent/flag table pointer |
| +0x0C | u32 | 0xA00 | packed rotation triplet table pointer |
| +0x10 | u32 | 0xBD0 | probable scale triplet table pointer |
| +0x14 | u32 | 0 | reserved/unknown |

| Absolute byte range (end exclusive) | Length | Content |
|---|---|---|
| 0x80..0x98 | 24 | six-u32 header |
| 0x98..0x548 | 1200 | N fixed 16-byte NUL-padded names |
| 0x548..0x550 | 8 | zero alignment padding |
| 0x550..0xA00 | 1200 | N records, each <3fHH> |
| 0xA00..0xBC2 | 450 | N raw signed-i16 triplets, <3h> |
| 0xBC2..0xBD0 | 14 | zero alignment padding |
| 0xBD0..0xF54 | 900 | N near-unit float triplets, <3f> |

For bone i, record at positions_pointer + 16*i:
- +0x00/+0x04/+0x08: float32 x/y/z parent-local translation.
- +0x0C: u16 parent index.
- +0x0E: separate u16 flag, observed 0 or 1.

Reading the last four bytes as one parent u32 is incorrect: main would falsely have parent 65536. Root IDEAL_OUT stores parent 0, not 0xFFFF; the decoder exposes root parent=null while preserving parent_raw=0. Treating this self-link as the root sentinel is an interpretation, made explicit rather than discarded.

All other parents are smaller than their child indices. Names and links form anatomically coherent leg, torso, arm, finger, and eye branches. The translations are local: rknee and rankle have y offsets -0.4778000116 and -0.4361999929; rwrist has (0,0.1657391042,0). They cannot be global joint positions. Root translation is (0,1.1593527794,-0.1137267426), so accumulating only translations also omits the rotations needed to orient feet and limbs.

## Cross-file proof

All ten 75-bone fixtures have byte-identical Skel payloads:
00000003 through 0000000b inclusive, plus 00000021.

Raw Skel SHA-256:
7ff5846187a8c4b1e9280287d1e31fc825c269a0c21f99938a254cce5b861b13

The remaining sixteen fixtures are one 32-bone skeleton. Six put Skel at 0x80 and ten at 0x90, depending on model Info length. Every table pointer shifts by exactly 16; after replacing the four table pointers by Skel-relative offsets, these sixteen payloads are byte-identical. This relocation is direct evidence against treating pointers as Skel-relative.

Normalized Skel hashes (only the four header pointers are rebased):
- 75 bones: a2434c2b43e430ed96ab8088f3575de7aa1bdc336ff16ecf34519d1faf18b500
- 32 bones: 308e10df907942563070e0ddd2639fc8f6e157b3f4bf268cefa3eaea96955c1d

Thirty names occur in both distinct rigs. Their parent NAMES and raw rotations agree exactly. Their local translations differ by at most 6.938893903907228e-18. Numeric indices change (rclavicle 21 -> 18, rwrist 25 -> 22, lclavicle 48 -> 25); this independently supports the parent-index interpretation rather than relying on duplicate files.

Flag=1 names in the 75-bone rig: main, up_torso, neckhi, eye_knuckle, rwrist, lwrist. In the 32-bone rig: main, up_torso, rwrist, lwrist. neckhi therefore changes flag while keeping the same parent and transform. It is neither part of the parent index nor a reliable root/leaf marker; actual flag meaning is unresolved.

Maximum absolute deviation of any scale-candidate component from 1, across all files: 1.1920928955078125e-7.

| Fixture | Info name | N | Skel offset | Names, positions, rotations, scales pointers |
|---|---|---:|---|---|
| 00000003.tmd | fm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000004.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000005.tmd | pm1200ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000006.tmd | pm600ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000007.tmd | pm300ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000008.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000009.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 0000000a.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 0000000b.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |
| 00000011.tmd | h2400ps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 00000012.tmd | h1200ps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 00000013.tmd | h600ps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 00000014.tmd | h300ps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 00000015.tmd | 2400pm40s_50sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000016.tmd | 1200pm40s_50sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000017.tmd | 600pm40s_50sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000018.tmd | 300pm40s_50sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000019.tmd | 2400pm60sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 0000001a.tmd | 1200pm60sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 0000001b.tmd | 600pm60sps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 0000001c.tmd | 300pm60sps2.ea3 | 32 | 0x80 | 0x98, 0x2a0, 0x4a0, 0x560 |
| 0000001d.tmd | 2400pm60s_70sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 0000001e.tmd | 1200pm60s_70sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 0000001f.tmd | 600pm60s_70sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000020.tmd | 300pm60s_70sps2.ea3 | 32 | 0x90 | 0xa8, 0x2b0, 0x4b0, 0x570 |
| 00000021.tmd | pm2400ps2.ea3 | 75 | 0x80 | 0x98, 0x550, 0xa00, 0xbd0 |

## Full target hierarchy and stored transforms

Tiny float noise below 1e-12 is displayed as zero here; the decoder retains exact unpacked float values. Parent numbers are indices in this same table. All scale candidates are (1,1,1) within the error bound above. Rotations are RAW signed integers, not asserted radians or quaternions.

| Index | Name | Parent | Flag | Local translation x,y,z | Raw rotation x,y,z |
|---:|---|---|---:|---|---|
| 0 | IDEAL_OUT | root (raw 0) | 0 | 0, 1.15935278, -0.113726743 | 0, 0, 0 |
| 1 | main | 0 IDEAL_OUT | 1 | 0, 0, 0 | -300, 0, 0 |
| 2 | rhip | 1 main | 0 | -0.0971029177, -0.0966670886, 0.118272059 | -1075, -1339, -691 |
| 3 | rknee | 2 rhip | 0 | 0, -0.477800012, 0 | 4052, 0, 0 |
| 4 | rankle | 3 rknee | 0 | 0, -0.436199993, 0 | 20700, 440, -7 |
| 5 | rball | 4 rankle | 0 | 0, 0.159999937, 0 | -5069, 0, 0 |
| 6 | rtoe | 5 rball | 0 | 0, 0.0504999943, 0 | 0, 0, 0 |
| 7 | lhip | 1 main | 0 | 0.0971029177, -0.0966670886, 0.118272059 | -1075, 1339, 691 |
| 8 | lknee | 7 lhip | 0 | 0, -0.477799952, 0 | 4052, 0, 0 |
| 9 | lankle | 8 lknee | 0 | 0, -0.436199993, 0 | 20700, -440, 7 |
| 10 | lball | 9 lankle | 0 | 0, 0.159999996, 0 | -5069, 0, 0 |
| 11 | ltoe | 10 lball | 0 | 0, 0.0505000018, 0 | 0, 0, 0 |
| 12 | low_torso | 1 main | 0 | 0, 0, 0.037556529 | -1627, 0, 0 |
| 13 | mid_torso | 12 low_torso | 0 | 0, 0.15874812, 0 | 2587, 0, 0 |
| 14 | up_torso | 13 mid_torso | 1 | 0, 0.154543892, 0 | 0, 0, 0 |
| 15 | necklo | 14 up_torso | 0 | 0, 0.205547914, 0.0725362226 | 5860, 0, 0 |
| 16 | neckhi | 15 necklo | 1 | 0, 0.122599207, 0 | -9056, 0, 0 |
| 17 | headend | 16 neckhi | 0 | 0, 0.0925996006, 0 | 0, 0, 0 |
| 18 | eye_knuckle | 16 neckhi | 1 | 0, 0, 0 | 2536, 0, 0 |
| 19 | reye | 18 eye_knuckle | 0 | -0.0399999991, 0.089309819, 0.0544965491 | 0, 0, 0 |
| 20 | leye | 18 eye_knuckle | 0 | 0.0399999991, 0.089309819, 0.0544965491 | 0, 0, 0 |
| 21 | rclavicle | 14 up_torso | 0 | -0.0285999998, 0.148078084, 0.125540003 | -1132, -544, 18297 |
| 22 | rshoulder | 21 rclavicle | 0 | 0, 0.239999995, 0 | 864, 1043, 3185 |
| 23 | relbow | 22 rshoulder | 0 | 0, 0.298999995, 0 | 5195, 0, 0 |
| 24 | rforearm | 23 relbow | 0 | 0, 0.131260872, 0 | 0, 0, 0 |
| 25 | rwrist | 24 rforearm | 1 | 0, 0.165739104, 0 | 0, 0, 0 |
| 26 | rpinkyBase | 25 rwrist | 0 | 0.00759122008, 0.0228000525, -0.0371553153 | -3042, 19, -2235 |
| 27 | rpinkyLow | 26 rpinkyBase | 0 | 0, 0.0426393114, 0 | 203, 123, 4747 |
| 28 | rpinkyMid | 27 rpinkyLow | 0 | 0, 0.039790839, 0 | 190, 8, 1920 |
| 29 | rpinkyHi | 28 rpinkyMid | 0 | 0, 0.0268110912, 0 | 386, 35, 4080 |
| 30 | rpinkyEnd | 29 rpinkyHi | 0 | 0, 0.0232949425, 0 | 0, 0, 0 |
| 31 | RFOOTBALL | 25 rwrist | 0 | 0, 0, 0 | 0, 0, 0 |
| 32 | rringLow | 25 rwrist | 0 | 0.0222043339, 0.0723359957, -0.0232452415 | -2011, 279, 1452 |
| 33 | rringMid | 32 rringLow | 0 | 0, 0.0451097563, 0 | 330, 118, 4640 |
| 34 | rringHi | 33 rringMid | 0 | 0, 0.0354194455, 0 | 112, 23, 2044 |
| 35 | rringEnd | 34 rringHi | 0 | 0, 0.0283890404, 0 | 0, 0, 0 |
| 36 | rmiddleLow | 25 rwrist | 0 | 0.021171933, 0.0779139251, 0.0021185053 | -539, 89, 2205 |
| 37 | rmiddleMid | 36 rmiddleLow | 0 | 0, 0.0491335839, 0 | -569, 188, 3143 |
| 38 | rmiddleHi | 37 rmiddleMid | 0 | 0, 0.0357111506, 0 | -489, 124, 2552 |
| 39 | rmiddleEnd | 38 rmiddleHi | 0 | 0, 0.027262086, 0 | 0, 0, 0 |
| 40 | rindexLow | 25 rwrist | 0 | 0.0158047117, 0.0758574978, 0.0321655869 | -337, 85, 2881 |
| 41 | rindexMid | 40 rindexLow | 0 | 0, 0.0507462509, 0 | -725, 124, 2119 |
| 42 | rindexHi | 41 rindexMid | 0 | 0, 0.0304305851, 0 | -1037, 249, 2996 |
| 43 | rindexEnd | 42 rindexHi | 0 | 0, 0.027849745, 0 | 0, 0, 0 |
| 44 | rthumbBase | 25 rwrist | 0 | -0.000862496614, 0.013815349, 0.0354419127 | 8320, -1105, 3627 |
| 45 | rthumbLow | 44 rthumbBase | 0 | 0, 0.0454629436, 0 | -4871, 683, 1657 |
| 46 | rthumbHi | 45 rthumbLow | 0 | 0, 0.0368887261, 0 | -3472, 320, 1146 |
| 47 | rthumbEnd | 46 rthumbHi | 0 | 0, 0.0305329617, 0 | 0, 0, 0 |
| 48 | lclavicle | 14 up_torso | 0 | 0.0285967682, 0.148076609, 0.125541165 | -1132, 544, -18297 |
| 49 | lshoulder | 48 lclavicle | 0 | 0, 0.239999995, 0 | 864, -1043, -3185 |
| 50 | lelbow | 49 lshoulder | 0 | 0, 0.298999995, 0 | 5195, 0, 0 |
| 51 | lforearm | 50 lelbow | 0 | 0, 0.131260887, 0 | 0, 0, 0 |
| 52 | lwrist | 51 lforearm | 1 | 0, 0.165739119, 0 | 0, 0, 0 |
| 53 | lpinkyBase | 52 lwrist | 0 | -0.00759000005, 0.0228000004, -0.0371600017 | -3042, -19, 2235 |
| 54 | lpinkyLow | 53 lpinkyBase | 0 | 0, 0.0426393114, 0 | 203, -123, -4747 |
| 55 | lpinkyMid | 54 lpinkyLow | 0 | 0, 0.039790839, 0 | 190, -8, -1920 |
| 56 | lpinkyHi | 55 lpinkyMid | 0 | 0, 0.0268110912, 0 | 386, -35, -4080 |
| 57 | lpinkyEnd | 56 lpinkyHi | 0 | 0, 0.0232949425, 0 | 0, 0, 0 |
| 58 | LFOOTBALL | 52 lwrist | 0 | 0, 0, 0 | 0, 0, 0 |
| 59 | lringLow | 52 lwrist | 0 | -0.0221999995, 0.0723399967, -0.0232500006 | -2011, -279, -1452 |
| 60 | lringMid | 59 lringLow | 0 | 0, 0.0451097563, 0 | 330, -118, -4640 |
| 61 | lringHi | 60 lringMid | 0 | 0, 0.0354194455, 0 | 112, -23, -2044 |
| 62 | lringEnd | 61 lringHi | 0 | 0, 0.0283890404, 0 | 0, 0, 0 |
| 63 | lmiddleLow | 52 lwrist | 0 | -0.0211699996, 0.0779099986, 0.00212000008 | -539, -89, -2205 |
| 64 | lmiddleMid | 63 lmiddleLow | 0 | 0, 0.0491335839, 0 | -569, -188, -3143 |
| 65 | lmiddleHi | 64 lmiddleMid | 0 | 0, 0.0357111506, 0 | -489, -124, -2552 |
| 66 | lmiddleEnd | 65 lmiddleHi | 0 | 0, 0.027262086, 0 | 0, 0, 0 |
| 67 | lindexLow | 52 lwrist | 0 | -0.0157999992, 0.0758600011, 0.0321700014 | -337, -85, -2881 |
| 68 | lindexMid | 67 lindexLow | 0 | 0, 0.0507462509, 0 | -725, -124, -2119 |
| 69 | lindexHi | 68 lindexMid | 0 | 0, 0.0304305851, 0 | -1037, -249, -2996 |
| 70 | lindexEnd | 69 lindexHi | 0 | 0, 0.027849745, 0 | 0, 0, 0 |
| 71 | lthumbBase | 52 lwrist | 0 | 0.000859999971, 0.01382, 0.0354400016 | 8320, 1105, -3627 |
| 72 | lthumbLow | 71 lthumbBase | 0 | 0, 0.0454629436, 0 | -4871, -683, -1657 |
| 73 | lthumbHi | 72 lthumbLow | 0 | 0, 0.0368887261, 0 | -3472, -320, -1146 |
| 74 | lthumbEnd | 73 lthumbHi | 0 | 0, 0.0305329617, 0 | 0, 0, 0 |

## Rotation experiments and limits on global placement

The strongest working hypothesis is Euler components with one full turn = 65536, i.e. radians = raw * 2*pi/65536. For example main=(-300,0,0), rknee=(4052,0,0), rankle=(20700,440,-7), and rclavicle=(-1132,-544,18297). This gives anatomically plausible feet, head and arms under a right-handed, column-vector parent-world * T * R * S interpretation. This is supporting plausibility, not proof of encoding, axes, units, handedness, multiplication order, or runtime use.

The independent experiment compares all six Euler multiplication orders and two alternatives. Selected coordinates for the primary fixture:

| Hypothesis | Right ankle y | Right ball y | Right toe y | Headend y | Right wrist y,z |
|---|---:|---:|---:|---:|---|
| Euler turn65536 XYZ (Rx*Ry*Rz) | 0.17167409 | 0.07226054 | 0.06299444 | 1.86055631 | 1.32906682, 0.21588857 |
| Euler turn65536 ZYX (Rz*Ry*Rx) | 0.17191380 | 0.07338239 | 0.06442760 | 1.86055631 | 1.31349561, 0.11532875 |
| Quaternion xyz=raw/32767, positive reconstructed w | 0.15900479 | 0.16543549 | 0.18278947 | 1.87889559 | 1.82081833, 0.11773342 |
| Euler radians=raw/16384, XYZ | 0.15892676 | 0.18183262 | 0.20393605 | 1.87908185 | 1.87522108, 0.13923234 |

The Euler full-turn alternatives put the forefoot below the ankle; the sampled alternatives put it above. All six Euler orders remain plausible and preserve bilateral symmetry; XYZ vs ZYX changes wrist depth by 0.10055982 units. These comparisons do not exhaust possible encodings and cannot choose an authoritative bind matrix. Do not silently use XYZ as established fact or infer source meters solely from human-looking proportions.

## Geometry-to-bone bridge: actionable mapping

The user supplied the Geom <np,nc,sp,sc>, 20-byte node and 12-byte stream layouts. These were used to inspect actual bytes, without modifying the main geometry decoder.

For every active node, node+4 points to a descriptor:
1. u32 palette_count.
2. palette_count u32 pointers.
3. At each pointed-to palette: u32 entry_count, followed by entry_count records <i16 slot,u16 Clst_index>.

Node+14 is the u16 base stream index. At stream base+2, descriptor <u32 ptr,u16 vertex_count,u16 stride,u32 semantic> has stride=4 and semantic=0. Its payload at ptr+12 contains float32 selectors, each EXACTLY divisible by 3. selector/3 is a SIGNED palette slot (often -32), not a bone index. Resolve slot in the applicable draw palette to obtain Clst index.

Clst payload begins <u32 rows_ptr,u32 row_count>. The rows pointer is Clst+8 in all 26 fixtures; length is exactly 8+4*count. Each row is <u8 bone_index,u8 predecessor_cluster,u16 weight_raw>. Across every row in all fixtures:
- bone_index addresses Skel.
- predecessor_cluster is 255 for root; otherwise it is the FINAL Clst row for that bone's skeleton parent.
- weight_raw lies in [0,4096], often in approximate tenths of 4096. A Q12 interpolation weight is strongly suggested, but the runtime blending formula is unverified.
- Multiple rows can reference the same bone with different weights; Clst index is not interchangeable with bone index.

Verified primary-fixture result:

| Node | Base stream | Vertices | Selector -> palette -> cluster -> bone |
|---:|---:|---:|---|
| 0 | 0 | 11 | all -96 -> slot -32 -> Clst[6] -> bone 16 neckhi |
| 72 | 75 | 450 | 441 vertices as above; 9 use -93 -> slot -31 -> Clst[4] -> bone 14 up_torso |
| 144 | 150 | 11 | all neckhi |
| 216 | 225 | 16 | all neckhi |
| 288 | 300 | 61 | all neckhi |

All seven primary Clst rows have weight 4096 and are: (bone,parent_cluster,weight)
(0,255,4096), (1,0,4096), (12,1,4096), (13,2,4096), (14,3,4096), (15,4,4096), (16,5,4096).
Thus this fixture has no partial cluster weights, and its vertices can be assigned to the two named joints without resolving interpolation semantics.

File04 has 137 active nodes; node55 (base stream88) has SEVEN draw palettes. All other active nodes have one palette. Node55's palette descriptor is at 0xCAD74; it begins u32 7 followed by seven palette pointers. The seven palettes can reuse the same signed slot for different clusters. Resolve its draw-batch boundaries before assigning vertices; taking only the first palette is wrong. The bridge explicitly reports ambiguous vertices instead of selecting an arbitrary cluster.

Across all 26 files, 78,036 selectors matched signed palette slots; all Clst predecessor links matched the skeleton-parent rule. This establishes the addressing relationship, not the interpolation matrix or per-batch vertex assignment.

In the primary fixture, node+10 is 0xFFFF on every active node: it cannot directly supply the head bone. The full body fixture needs the palette/cluster chain as well.

## Remaining streams: sparse morph evidence, not confirmed weights

The apparent 3072/3084 strides are 0x0C00/0x0C0C, suggesting a packed format field rather than ordinary byte strides. Primary stream19 has count2 and ptr=0x7B00; at ptr+12 its records are:
- 00000000 0100 0300 0000 0000: vertex 0, signed deltas (1,3,0), trailing zero.
- 09000000 0100 0000 0000 0000: vertex 9, signed deltas (1,0,0), trailing zero.

These fit 12-byte sparse <u32 vertex,i16 dx,i16 dy,i16 dz,u16 reserved> records. Many other streams have count0, semantic=0xFFFFFFFF, while nonempty counts are within the base mesh vertex count. Several paired sparse streams are identical. Combined with EyeL/EyeR/Face/Chinstrap/Eyelash Swap names and local head coordinates, sparse morph targets are a stronger explanation than skin weights. This remains a hypothesis until the affected base vertices, delta units, and target controls are independently matched.

The base+3 stride0 streams examined have only zeros in the first 116 payload bytes. Stride0 and semantic0 alone are not evidence of skin weights. Do not use count*3072 to bound the sparse streams.

## Reproduce / verify

From the workspace:
- python work/skel_probe.py
- python work/skel_rotation_experiment.py
- python work/skel_geom_bridge.py
- python -m unittest discover -s work -p test_skel_probe.py -v

The decoder emits JSON to stdout, including full bones, file and payload hashes, offsets, padding, groups and checks. It has no export/write path and imports no main application modules. It is intentionally strict to observed fixtures, not a general claim about every EA model. The tests pin the real target hash and expected fields, both normalized rig hashes, relocation behavior, geometry selector totals, seven-palette node55, and the unresolved rotation-order discrepancy. Negative controls alter copies in memory to check rejection of truncation, invalid parents and out-of-range pointers.

## Next evidence needed for correct global placement

Use the proven selector/palette/cluster assignment for single-palette meshes, then compare attachment seams between independent local parts under each Euler convention. For partial Clst rows, determine whether the Q12 value interpolates parent/child rigid transforms, transformed vertices, or another frame representation. For multiple palettes, parse the draw/index batch descriptors and retain palette identity per draw. A runtime transform routine, decoded animation rest frame, or quantitatively matched seams is needed before calling the resulting world and inverse-bind matrices verified.


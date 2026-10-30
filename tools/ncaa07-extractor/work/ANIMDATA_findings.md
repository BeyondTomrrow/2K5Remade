# ANIMDATA: binary evidence and decoder roadmap

Inspected the actual payloads in `outputs/animdata_extracted` on 2026-10-14.
All offsets below are byte offsets; integers are little endian. Main files and
extracted assets were not edited. No external format documentation was used.

## Results ready for integration

* 1,070 DATs plus `0000008f.app` contain a recognizable embedded container.
  Together they contain 1,766 `MCFB` blocks, each with 90 scalar channels.
* Seventeen metadata VAPs contain 1,510 records. Every record's payload index
  and extra-block count resolve consistently against 17 payload groups.
* Body rig names match 30 of the player's 75 named bones exactly. Separate
  17-name hand rigs bring the union to 62 bones, because each wrist overlaps.
* Four early DAT banks contain 815 directly readable samples, each comprising
  17 triples of signed 16-bit integers. Those values are decoded, but conversion
  to joint rotations is still a hypothesis.
* The compressed body codec is structurally separated into sample references,
  control data, four coefficient arrays, mirrored channel indices, and packets.
  This is not yet a certified animation-to-glTF decoder.

## Reproduce

Run from the workspace:

```powershell
python work/anim_decode_candidate.py audit
python work/anim_decode_candidate.py hand 0000001e.dat 0
python work/anim_decode_candidate.py body 00000468.dat 0 0
python work/anim_probe.py
```

These commands read assets and print results; they do not overwrite assets.
`audit` also prints all 11 rig name tables and their exact player-name matches.
The body command deliberately labels unpacked bit values as speculative and
does not emit angles, quaternions, or animation files.

## The extensions do not identify the internal codec

The extracted directory contains 1,094 DATs, 18 VAPs, 11 1LKs, and other support
files. `00000001.vap` is an early animation bank with a 32-byte header, resource
directory, sample table, and shared coefficient section. Its layout differs
from the other 17 VAPs. Conversely, the APP contains the same embedded body
container as the later DATs. Extension-only classification misses both facts.

### Embedded container

For the later DAT/APP payloads, `u32(file,0)+4` locates bytes `42 46 4d 45`
(`BFME`). A zero first word puts that marker at +4, with no preceding track
prefix; 53 DATs have this form. 1,017 DATs and the APP have a preceding region.
Do not confuse marker bytes with a reversed integer FourCC spelling.

| Relative to BFME | Decoded field | Observed evidence |
|---|---|---|
| +0x00 | marker bytes | `BFME` |
| +0x04 | u32 | always 8 |
| +0x08 | u32 blocks per segment | always 2 |
| +0x0c | u32 segment count | 1 through 22 in this corpus |
| +0x10 | descriptor array | segment count times two 16-byte descriptors |

Each descriptor is `{u32 type, char tag[4], u32 byte_size, u32 file_offset}`.
Tags alternate `MCFB`, `MFFB`. All descriptor byte ranges are in bounds.
Empty MFFB descriptors have zero offset and size; do not read file offset zero
as a real MFFB block. Type codes vary; they are not enough to infer one codec.

Example `0000002f.dat`:

* first u32 = 896; BFME at 900 / 0x384;
* three segments, six descriptors;
* MCFB blocks at 1,012 (size 9,540), 10,972 (13,384), 24,776 (12,634);
* their logical sample counts are 113, 150, 150.

### MCFB block: offsets relative to its descriptor's file_offset

| Offset | Field | Evidence |
|---|---|---|
| +0x00 | packed identifier | high 16 bits advance across segments, reset per group |
| +0x04 | packed count/format | low 16 bits equal logical sample count; high half is 9 |
| +0x08 | resource directory offset | 32 |
| +0x0c | control-section offset | immediately after sample-reference table |
| +0x10 | sample-reference table offset | table has count times four bytes |
| +0x14 | shared coefficient section offset | six u32 fields below |
| +0x18/+0x1c | reserved/unknown | retained as raw words |

All 1,766 sample tables resolve inside their MCFB blocks. The table is a list
of block-relative packet offsets, not necessarily a monotonic physical stream.
`0000017f.dat`, descriptor index 28, reuses packet offsets and revisits earlier
ones (strides include 0 and -62). Size a physical packet using the next greater
distinct offset, not the next logical sample's offset.

The shared section contains six u32s: `90, A, B, C, D, E`.

* A/B/C/D are four arrays, each with 90 useful signed i16 values. A/B/C occupy
  192 bytes each (96 values including padding); D occupies 180 bytes.
* E is an array of 90 u16 descriptors (180 bytes), followed by alignment bytes
  before packet data.
* All 1,766 blocks have pointer differences 192, 192, 192, 180.
* In every block, `E[i] & 255` is a permutation of 0..89. It is the same
  left/right channel permutation, independently of the high bytes.
* The high descriptor byte is a bit-width candidate. Nonconstant blocks have
  nonzero widths; the zero-width blocks have one logical sample.

Define `m[i] = E[i] & 255` and `s[i] = +1` for i mod 3 = 0, otherwise -1.
For every useful element in all 1,766 blocks, using signed-16 wraparound:

```
C[i] == s[i] * A[m[i]]
D[i] == s[i] * B[m[i]]
```

Both identities passed with ZERO mismatches. This establishes stored mirror
coefficients, and rules out interpreting A and C as ordinary minimum/maximum
arrays. A/B as base plus span, with C/D the mirrored equivalent, is a useful
candidate; the reconstruction equation is not proven yet.

Frame-boundary evidence for the bit codec: `00000468.dat`, first MCFB, has
151 logical samples, each physical packet 66 bytes. Descriptor widths total
447 bits / 56 bytes, leaving ten bytes before the hypothesized packed channels.
An LSB-first unpack starting at byte 10 gives mean normalized adjacent-sample
change 0.0459, versus 0.1853 for an MSB-first unpack at the same boundary.
This supports that candidate on this block only. Other blocks leave 16 or 22
bytes, and control data can affect framing. Continuity is supporting evidence,
not proof of an angle decoder.

## Metadata VAP to payload linkage

For these 17 VAPs: u32 version at +0 is 1, u32 record count at +4; absolute
record offsets begin at +8. First record begins at `8+4*count`.
The u32 at each record's +0 packs:

* low 16 bits: sequential metadata record index, verified for all 1,510;
* high 16 bits: zero-based index of the payload in the associated group.

Record +4 plus one equals the selected payload's segment count, verified for
all 1,510 records. Multiple metadata records can select the same payload.
Record +0x18 points to a 64-byte tag/parameter record; use that pointer rather
than assuming this tag begins at record+72. Four-character tags are stored in
reverse-looking byte order (e.g. bytes `SSAP`, `ENON`); preserve raw spelling.

The u32 at tag+8 frequently matches a payload sample count, but 140 of 1,510
records disagree with the final MCFB's sample count. It must not be treated as
an unconditional sample-count field. E.g. metadata `00000028.vap` record 6
contains 241 there, while selected `00000032.dat` has 237 samples per segment.

| VAP | Metadata records | Payloads | Inclusive payload range |
|---|---:|---:|---|
| 00000028 | 92 | 28 | 0000002f–0000004a |
| 00000029 | 75 | 18 | 0000004b–0000005c |
| 0000002a | 115 | 65 | 0000005d–0000009d, includes 0000008f.app |
| 0000002b | 63 | 23 | 0000009e–000000b4 |
| 0000002c | 96 | 30 | 000000b5–000000d2 |
| 0000002d | 169 | 72 | 000000d3–0000011a |
| 0000002e | 65 | 41 | 0000011b–00000143 |
| 00000144 | 76 | 47 | 00000145–00000173 |
| 00000174 | 25 | 13 | 00000175–00000181 |
| 00000183 | 264 | 264 | 00000184–0000028b |
| 0000028c | 264 | 264 | 0000028d–00000394 |
| 00000395 | 114 | 114 | 00000396–00000407 |
| 00000408 | 12 | 12 | 00000409–00000414 |
| 00000415 | 16 | 16 | 00000416–00000425 |
| 00000426 | 53 | 53 | 00000427–0000045b |
| 0000045c | 10 | 10 | 0000045d–00000466 |
| 00000467 | 1 | 1 | 00000468 |

Group inference uses resets of the first MCFB's identifier and validates the
result against all metadata references. It is corpus-established, not a claim
that arbitrary archives can omit an explicit higher-level directory.

## Relationship to the 75 player bones

Source: `outputs/plydata_extracted/00000004.tmd`, Skel at file offset 128,
bone count 75, 16-byte names at Skel+24. The exact name-to-index mapping is
printed by the audit, independent of the main inspector module.

`00000011.1lk` and `00000012.1lk` contain identical decoded 30-name body tables
and node links, but different rest transforms (see `1LKS_rest_findings.md`).
The names table consists of 20-byte records: `{u32 node_offset,char name[16]}`.
u16 count at file+6 is 30; u32 file+12 is 1,424, the name table base; u32 +8
is 2,024, the node-region base. Node+2 is a u16 matching the name index.
Node+4 points to another named node, consistent with an anatomical parent;
the root has a zero pointer. The candidate reader emits these parent indices.
This is animation-rig hierarchy evidence, not a decoded 75-bone model hierarchy.

Thirty body names in track-candidate order:

```
main rhip rknee rankle rball lhip lknee lankle lball
low_torso mid_torso up_torso necklo neckhi
rclavicle rshoulder relbow rforearm rwrist RFOOTBALL
lclavicle lshoulder lelbow lforearm lwrist LFOOTBALL
headend eye_knuckle reye leye
```

The mirrored channel permutation swaps rhip/lhip, the corresponding leg and
arm chains, RFOOTBALL/LFOOTBALL, and reye/leye. It preserves the central body
chain. This supports THREE channels per body-rig node; 90 is not a bone count.
The metadata VAP with 75 records is likewise not a 75-bone track table.

Hand name sets:

* left: `00000019`, `0000001b`, `00000025` 1LKs;
* right: `0000001a`, `0000001c`, `00000024` 1LKs;
* each includes wrist plus pinkyBase/Low/Mid/Hi, ringLow/Mid/Hi,
  middleLow/Mid/Hi, indexLow/Mid/Hi, thumbBase/Low/Hi, with l/r prefixes.

Body plus the two hands = 62 unique model bone names. The 13 absent names are
`IDEAL_OUT`, `rtoe`, `ltoe`, and ten finger `End` bones. Their absence from
these name tables does not prove they are static or safe to omit; their bind
and inheritance behavior must come from the model/skeleton work.

Other 1LKs contain an 11-name crowd rig, a 29-name body variant and an 8-name
rig. Do not apply a player mapping blindly to every early bank.

## Directly decoded 17-triple samples

These are early resource banks, not BFME-wrapped late payloads:

| DAT | Logical samples | Offset of first sample | Bytes per sample | Header u16 |
|---|---:|---:|---:|---:|
| 0000001d | 14 | 0xc0 | 104 | 0x0111 |
| 0000001e | 403 | 0xb80 | 104 | 0x0011 |
| 0000001f | 393 | 0xb00 | 104 | 0x0011 |
| 00000026 | 5 | 0xa0 | 104 | 0x0011 |

For these banks, low u16(file+4) is the sample count, u32(file+0x10) points
to an absolute sample-offset table. Every adjacent offset differs by 104,
and the final sample ends exactly at EOF. Decode as `<H51h`, then group the
51 signed values into 17 consecutive triples. Example `0000001e.dat` sample 0:
`(-466,0,-75), (-3038,21,-2235), (213,130,4776), ...`.

The matching 17-node hand rigs make rotation triples a strong candidate.
The correspondence of a specific bank to a particular left/right 1LK, rotation
units (e.g. integer turns), Euler order, coordinate basis, and rest-pose
composition are not fixture-proven. The decoder outputs integers, not guessed
quaternions. Header bit 0x0100 in 1d also remains unexplained.

## Feasible decoder roadmap

1. Integrate verified structure first: BFME descriptor reader, MCFB relative
   pointers and sample references, VAP payload selection, 1LK exact names.
   Keep control words, type codes, and MFFB bytes available for follow-up.
2. Use the four raw banks and one-frame body blocks as minimal pose fixtures.
   Match the skeleton agent's local transforms; test integer-angle scale and
   axis/order with left/right symmetry and known joint constraints. Preserve
   all 75 skeleton bones and map animation names explicitly.
3. Solve compressed body packets: use the high descriptor byte as the first
   width hypothesis, LSB-first packing, and A/B base/span candidates. Explain
   10/16/22-byte prefixes, root channels, quantization rounding, and control
   sections. Reuse offsets correctly. Require agreement across several type
   codes and blocks; continuity alone is insufficient.
4. Only then emit animation channels into the geometry GLB using the verified
   hierarchy and bind transforms. Confirm frame timing (30 Hz is a candidate
   suggested by prefix floats, not established here), segment semantics,
   mirroring and loop/transition selection from VAP control records. MFFB and
the 15-descriptor prefix region remain to be semantically decoded.

Useful early-bank follow-up: they share the 32-byte resource header and a
directory at +32, but some use an uncompressed high-half format 8 and others
format 9/13. Do not run the four raw-bank reader on them without validating
their own sample layouts. The initial VAP has 14,775 logical sample references
by its packed count, unlike the 17 metadata VAPs.

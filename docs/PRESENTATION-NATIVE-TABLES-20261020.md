> **2026-10-20 user correction:** the new eleven-player grid and generic navy table do not match CBS. They are rejected visual prototypes, disabled in the CBS manifest and removed from the shipped page. Native data/ownership experiments below remain engineering evidence, not approved CBS graphics. Do not re-enable them merely because tests/builds pass. Use measured CBS broadcast treatments instead.

# Accepted native team tables — October 20, 2026

Scope: replace the original broadcast **table artwork**, using its exact live
title and cells. This is not a new statistics calculator. Read-only inspection
used the user's existing XBE and extracted disc resources; no original resource
was changed, copied into a release, or downloaded. This agent did not build the
game, launch it, control it, or attach to Xemu. Native integration and runtime
verification belong to the coordinating session.

## What counts as a displayed table

`sub_0008ACF0` is a request/acceptance boundary, not a draw guarantee. Existing
`[CARD]` / `[POPUPOP]` lines can describe rejected or unready requests. The
implementation binds only an actual active slot at `00B61B50` or `00B62A70`,
with its real slot number, active word, no pending resource load (`+F08 == 0`),
readiness `+1D8 == 5`, first-draw latch `+F1C == 1`, loaded resource at `+1DC`
and a nonzero scene. Callback, clock object, both loaded rosters, package, page
generation, title, subjects and every cell remain part of the ownership key.

`sub_0008A170` calls both the generic subtype-table draw and the scene draw.
Catalog `+24` governs only the first of those paths. The scene path must be
audited separately before claiming a zero `+24` means an invisible layout.

| Layout | Actual descriptor | Proven data/draw path | Replacement eligibility |
|---|---|---|---|
| 0 | Zero descriptor | Serialized table requests exist; real resource/scene draw not established | Keep original; reject zero resource |
| 3 | `1-t_few-stats`, `00AAD2C4` | Generic subtype table enabled by descriptor `+24` | Exact subtype 1 team table, supported cells |
| 4 | `1-t_many-stats`, `00AAD330` | Generic subtype table enabled by descriptor `+24` | Same; total cells at most 24 |
| 27 | `halftime_team_stats`, `00AADCE4` | Independent scene text callbacks; see proof below | Exact two-subject, three-column, five-row scene only |

The catalog base is `00AAD180`, stride `6C`, registered by `sub_001C1530`
through `sub_001BC880` and stored at `00BE5214`. Production code verifies the
catalog identity, entry names/groups and expected draw path. It does not enable
the whole catalog or suppress every statistics layout.

## Exact native data model

`sub_0008A2E0` adds a **type 2 subject**. Its value is a loaded roster pointer,
not an arbitrary numeric team ID: `sub_00061C50` returns home `00B30864`, and
`sub_00061C60` returns away `00B30A58`. The live canonical roster comparison is
against `k_team_obj[away/home] + 1C`. `sub_001C04A0` returns a type 2 subject's
stored value directly; its player/type 1 path is different and is not accepted
by this team-table producer.

| Popup offset | Meaning |
|---|---|
| `+10 + i*8`, `+14 + i*8` | Subject type and roster pointer |
| `+40` | Subject count: supported one or two |
| `+48` | Actual catalog layout |
| `+4C` | Literal native UTF-16 title |
| `+CC` | Second caption/message; must be empty for this schema |
| `+228` | Subtype 1 |
| `+22C` | **Column count**, including the label column |
| `+230` | **Row count** |
| `+234 + i*88` | Row-major cell record |
| Cell `+0`, `+4`, `+8` | Type, style, then inline text (type 0) or text pointer (type 1) |
| `+EF4` | Populated cell count; must equal columns times rows |
| `+F14` | Native close callback |

The legacy variable names for rows and columns are reversed. The actual index
in `sub_0025F8B0` is `columns * row + column`; `sub_0025F1B0` establishes
`columns == subjectCount + 1`. `sub_001BEE40` decodes subtype 1 cells using the
type 0 / type 1 rules above. Native formatter `sub_0025EE30` passes the actual
roster into `sub_000CB2D0`; host code must not reformat these cells from fresh
Gamecast totals or infer their period scope.

Both native subject order and all cell strings, including blanks, are preserved.
The only added header labels are the current `GameState` abbreviations for the
rosters actually verified against each subject. Unknown subjects, duplicate
subjects, ambiguous same-roster sides, malformed/unreadable text, a meaningful
uncarried second caption, wrong dimensions or unsupported cells retain original
artwork. Every string is bounded by the existing 64-character guest text reader.
No truncated subset is accepted.

## Layout 27: independently proven scene path

Correction to the initial source-audit assumption: layout 27's `+24 == 0`
disables its generic subtype draw, **not its scene text**. `sub_0008A170` still
calls `sub_001BCE50`, whose MRKS text dispatcher reaches the registered cell
callbacks. This is why layout 27 is now supported without changing its native
draw flag.

Read-only decoding of the owned `halftime.iff` found MRKS chunk 7 at `0x61660`,
decoded size 17,536 bytes. Its wrapper is at decoded `0x20`, scene descriptor
at `0x1300`, and text-record array at `0x50`. Loaded resource `B+1DC` addresses
that wrapper: `resource+0` is scene, `+4` is text count **32**, and `+8` is the
text array. Text records are `0x90` bytes, with their callback hash at `+80`.

The 32 records are exactly paired copies of 15 grid cells plus the native title:

| Row | Column 0 | Column 1 | Column 2 |
|---:|---|---|---|
| 4 | `7F905A43` | `C72C3D26` | `D59992C8` |
| 3 | `30D1CC84` | `886DABE1` | `9AD8040F` |
| 2 | `29CAFDC5` | `91769AA0` | `83C3354E` |
| 1 | `02E7AE06` | `BA5BC963` | `A8EE668D` |
| 0 | `1BFC9F47` | `A340F822` | `B1F557CC` |

The title hash is `AE7481A6`. Native callback registry `0050D268` maps every
grid hash to `sub_001C0CD0`, parameter `(column << 16) | row`; the title maps
to `sub_001C0AF0`. The grid callback checks subtype/dimensions and reads the
same exact row-major cells described above; the title callback reads `B+4C`.
No additional text hashes occur in this actual resource. Dynamic team logos
use the native subject path (`0050D650`, `sub_001C0F20`), not a third statistics
scope. Decorative scene art remains an adapted CBS skin, not a claim of exact
CBS production meshes/materials.

The replacement therefore requires this exact 32-record hash sequence, real
scene/resource, catalog callback registry, two verified team subjects and a
3-by-5 table. A different/modded scene or extra caption falls back to native.
`Through Regulation`, if the native title says that, stays **Through Regulation**
after a period/phase change or overtime begins. It is never relabelled FINAL.

## Implemented interface and ownership

New isolated module: `src/presentation/native_table.inc.cpp`. This keeps native
table ownership and exact data copying separate from football logic and the
existing general statistics formatter. Parent integration supplies:

- `Pkg.native_tables_ack`, selected/host package registry and page generation.
- Pending-event fields `table_token`, `table_generation`, `table_package`.
- Actual popup frame, HUD-context, narrow draw-filter, slot-close, page reset,
  page-message and queued-event validation calls.

Events are `NATIVE_TABLE_SHOW` / `NATIVE_TABLE_HIDE`, with this shared payload:

```json
{
  "source": "native",
  "nativeVisible": true,
  "nativeTableToken": "17",
  "slot": 0,
  "layout": 27,
  "title": "<literal native title>",
  "headers": ["", "<verified first subject abbreviation>", "<verified second subject abbreviation>"],
  "rows": [["<native label>", "<native value>", "<native value>"]]
}
```

`nativeTableToken` is a canonical **string**, not a JS floating-point identity.
Browser acknowledgment is `NFL2K5_NATIVE_TABLE_SHOW:<token>` only after paint;
release is `NFL2K5_NATIVE_TABLE_HIDE:<token>`. Host parsing rejects zero,
overflow, leading zeroes, suffixes, stale package/generation/token and late SHOW
while native closing. Page handlers touch only synchronized host copies: zero
guest reads or guest calls.

Native artwork remains until the exact page has acknowledged paint. Matching
alpha/text draw seams then suppress only that popup's artwork; original scene
updates, input, duration and close callback continue unchanged. A first accepted
slot owns the single plate instead of alternating ownership between two slots.
Payload/slot/resource/match reuse and pause/play-selection revoke ownership.
Native period changes and a legitimate final state do not invalidate an already
accepted exact table. Closing keeps painted native art suppressed until native
cleanup; a late browser HIDE cannot resurrect the closing ESPN artwork.

Hot paths check package capability before `read_state`, and check active owner,
context and acknowledgment before draw-time `read_state`. Accepted-instance
diagnostics print `[NATIVE-TABLE] accepted` with slot/layout/dimensions/title.
When both presentation tracing and `NFL2K5_REC` are enabled, actual acceptance
also requests one bounded eight-second recording. No test table is injected.

## Verification and remaining native proof

`python tools/presentation-audit/verify-native-table-owner.py` compiles the
**actual included module verbatim** against mock RAM and host dependencies.
Latest result: **1,066 assertions pass**. This includes both slots, catalog and
scene guards, all 24 supported cells, inline/pointer text and blanks, reversed
team order, malformed/unknown subjects, real close versus reuse, stale queue
and browser tokens, match/page changes, pause/play selection, unchanged titles
across period/phase/final changes, zero guest reads in page handlers and idle
fast paths, and one guarded eight-second recording per actual accepted bind.

The verifier separately decodes the user's owned halftime resource and checks
the XBE callback registry; it does not merely mirror the production constants.
Included module SHA-256:
`f708e45e46e2b439f2a3555e932cd5523ac1bc6eca7e47dfc3f12ea67dabddbf`.
Harness outputs are under
`outputs/presentation-audit/native-table-owner-test/`. No original asset bytes
are copied to that output.

The CBS browser component has separately passed 87 real Edge assertions,
according to its implementing agent. That does not establish a native table
handoff. Next verification must capture a naturally accepted layout 3/4 table
and the real layout 27 halftime instance, their paint ACKs, suppression, native
close callbacks, and composed D3D11 frames. Also exercise missing page/assets,
unsupported layout 0 and a page reset to retain original fallback. Do not call
quarter/halftime/final table replacement complete from request logs or tests.

## Other remaining brand-only candidates: 24, 30 and 31

These are separate from statistics tables. They need their own narrowly typed
accepted-instance owner and asset/render verification, not `native_tables_ack`.

- Layout **24**, `fullscreen_SurroundSound`, catalog `00AADBA0`: not found among
  the 22 valid MRKS resources in the owned fixed-slot `overlay.cdf` scan. Catalog
  naming alone does not prove this layout is loadable or displayed. Keep native
  until actual resource/callers are established.
- Layout **30**, `gamesound_digital`, `00AADE28`: its real constructor is the
  post-matchup callback `sub_00318510`. It first calls `sub_00128C70`; only the
  eligible branch builds layout 30 on the stack and calls `sub_0008ACF0`, return
  `00318539`. The preceding layout 13 matchup is built by `sub_00318540` with
  actual away/home subjects and its own close callback; do not conflate them.
- Layout **31**, `gamesound_prologicII`, `00AADE94`: separate resource. A specific
  accepted native caller/lifetime still needs confirmation.

Owned `overlay.cdf` is fixed-slot `0x1C580`, not a contiguous no-padding chunk
stream. Its Pro Logic II MRKS begins at `0x55080`, Digital at `0x137C80`;
both decode to 115,968 bytes and have **zero dynamic text records**, seven
embedded textures and materials named `a_espn_logo_and_glow`, `b_espn_glow`,
`c_glow_moving`, `d_gamesound`, `e_gamesound_glow` and directional arc layers.
This proves ESPN branding is part of the native scene, but does not prove that
all remaining image/geometry content is interchangeable network branding.
Audio-format artwork or wording must be inspected and preserved if present.
Do not silently erase a Digital/Pro Logic informational graphic by treating its
whole scene as an empty CBS-logo bumper. These findings are source audit only;
no 24/30/31 draw suppression was implemented here.

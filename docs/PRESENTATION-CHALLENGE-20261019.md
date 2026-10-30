# Native challenge/review popup: source and ownership audit

Date: 2026-10-19. Read-only production audit; no game, build, debugger, Xemu,
regeneration, or production edit was performed for this document.

## Result

The screenshot's original **CHALLENGE / The booth is reviewing the play /
Spot Of Ball** graphic is a normal native popup with **layout 14 and subtype
2**, constructed in `sub_00117310`. The ruling is a second popup constructed
in `sub_00117430`, with a different completion callback. Both use the reusable
request object `0x00BA4E78`; the request itself is **not** the accepted drawable
instance. Layout 14's resource is named `coin_toss`, so resource/layout names
alone do not distinguish challenge graphics from other uses.

The precise accepted-instance seam is the return from each constructor's
`sub_0008ACF0` call. Its returned `EAX` is an actual slot index 0 or 1 on
success, or `0xFFFFFFFF` on rejection. Bind to
`0x00B61B50 + slot * 0xF20`, validate its copied callback and payload, and issue
a new match/package/generation-scoped token. A show-request entry log is not
acceptance evidence. Existing native popup alpha and text seams can suppress
this exact acknowledged slot while all native timing and callbacks continue.

## Primary evidence

Generated source was checked against the locally owned, read-only retail XBE
using Capstone. Bounded disassembly, descriptor and string evidence is saved
under `outputs/presentation-audit/challenge-20261019/owned-xbe-disassembly.txt`.

| Source | Relevant functions / addresses |
|---|---|
| `src/recomp/gen/recomp_0008.c` | `1172F0`, `117300`, `117310`, `117420`, `117430`, `117580`, `117590`, `1175B0`, `1175C0`, `1175E0` |
| `src/recomp/gen/recomp_0004.c` | show `8ACF0`, reset `8A270`, callback setter `8AC30`, draw `8A170`, timed update `8A840`, close `8A340`, close-all `8A3F0`, teardown `8A810` |
| `src/recomp/gen/recomp_0013.c` | subtype reset `1BDD20`, row insertion `1BE1F0`, subtype selection/focus `1BE6B0`, subtype-2 draw `1BDDA0`, generic draw router `1BD5A0` |
| Existing generated-patch source | `tools/apply-gen-patches.py`: `SNF_POPUP_FRAME`, `SNF_POPUP_TABLE_ALPHA`, `SNF_POPUP_LAYOUT_ALPHA`, independent layout/art/texture/text alpha hooks |

## Review announcement

`sub_00117310` (`117310..11741E`) accepts optional challenging team in `ECX`
and reason index in `EDX` (saved in `EDI`). It produces:

1. Optional team: formats `%s is challenging` (`E71E40`) using the team's
   `+138` text into UTF-16 scratch `BA5DA0`. Without a team it formats
   `The booth is reviewing the play` (`E71E68`).
2. Calls native close-all `8A3F0`; then writes `BA5D98 = 1`.
3. Resets request `BA4E78` through `8A270`.
4. Sets timeout to **6.0 float** at request `+8`, title `Challenge`
   (`E71EA8`) at `+4C`, and formatted message at `+CC`.
5. Writes layout **14** at `1173C8`, request `+48 = E`.
6. Calls `1BDD20(request+228)` to initialize subtype **2**. Calls
   `1BE6B0(..., EDX=0)` to clear selection/focus-related field `+684`.
7. Looks up `A97ED0[reason]`, inserts one subtype-2 row through `1BE1F0`.
8. Sets callback `1172F0` through `8AC30` (return `117411`).
9. Calls show at **117416**, returning at **11741B**.

Exact function-scoped generated patch anchor for a future post-acceptance
observer (place **after the call line**, before any epilogue):

```c
PUSH32(esp, 0x0011741Bu); RECOMP_ABI_CALL(0x0008ACF0u, sub_0008ACF0); /* call 0x0008ACF0 */
```

At this point `EAX` still contains the native return slot. The observer must
not change registers, guest flags, guest memory, return value, or control flow.

The source reason table contains **12** challenge reasons; values after index
11 are neighboring ordinal strings and are not reasons:

| Index | UTF-16 pointer | Text |
|---:|---:|---|
| 0 | E71C30 | Nothing |
| 1 | E71C40 | Spot Of Ball |
| 2 | E71C5C | Catch Made Out Of Bounds |
| 3 | E71C90 | Catch Made In Bounds |
| 4 | E71CBC | Fumble Before Down |
| 5 | E71CE4 | Possession Before Fumble |
| 6 | E71D18 | Pass Behind LOS |
| 7 | E71D38 | Illegal Forward Pass Beyond LOS |
| 8 | E71D78 | No Step Out Of Bounds |
| 9 | E71DA8 | Fumble Before Arm Moved Forward |
| 10 | E71DE8 | Ball Passed Backwards |
| 11 | E71E14 | Ground Caused Fumble |

For rendering, prefer the copied inline row text in the accepted native slot,
not a second guess from another game's mutable review state.

## Ruling announcement

`sub_00117430` (`117430..11757F`) takes optional challenging team in `ECX`
and boolean in `EDX`:

- nonzero: `The ruling on the field is overturned.` (`E71EC0`);
- zero: `The ruling on the field stands.` (`E71F10`).

It constructs the same layout/title/timeout, sets `BA5D9C = 1`, and installs
callback **117300**. Its single subtype-2 row is the formatted timeout-charge
sentence `%s is %scharged a timeout.` (`E71F60`), with optional `not `
(`E71F50`), or an empty string for a booth review. It does **not** preserve the
earlier review reason. Do not show stale `Spot Of Ball` under a ruling just
because the request object address was reused.

The exact accepted-return anchor is inside `sub_00117430`, after:

```c
PUSH32(esp, 0x00117578u); RECOMP_ABI_CALL(0x0008ACF0u, sub_0008ACF0); /* call 0x0008ACF0 */
```

Call instruction is `117573`, native return is `117578`. Its `EAX` has the
same slot-or-failure semantics.

## Actual slot payload

`B` below is the actual accepted slot, either `B61B50` or `B62A70`.
The request is copied by `8ACF0` for a full `F20` bytes before activation.

| Field | Meaning for this popup |
|---|---|
| `B+00` | slot index, 0 or 1 |
| `B+04` | active flag, 1 while owned native instance exists |
| `B+08` | mutable float timeout; initialized 6, decremented natively |
| `B+0C` | native closing flag |
| `B+48` | layout 14 |
| `B+4C` | inline UTF-16 title, `Challenge` |
| `B+CC` | inline UTF-16 message (review or ruling) |
| `B+228` | subtype 2 |
| `B+22C` | subtype-selected row; initialized 0 |
| `B+230` | row count, 1 |
| `B+234` | first row ID/value, initialized 0 |
| `B+238` | first row's first inline UTF-16 text, up to 64 characters |
| `B+2B8` | first cell style/value, 1 in these constructors |
| `B+458` | first row's cell count, 1 |
| `B+684` | selection/focus-related field, initialized 0 |
| `B+F00` | native elapsed/fade time; not an identity field |
| `B+F08` | native draw-disable flag |
| `B+F14` | `1172F0` review or `117300` ruling completion callback |
| `B+F1C` | first-draw latch |

Subtype-2 row stride is **228** hex; cells within a row use **88** hex. This
differs from subtype-1 generic stats cells. `1BE1F0` receives `request+228`,
uses row index `+8`, starts its row at subtype `+C`, copies text into row `+4`,
writes row `+84`, sets row cell count `+224 = 1`, then increments row count.

Layout descriptor is `AAD180 + 14*6C = AAD768`:

- resource UTF-16 `E8635C`: **coin_toss**;
- family UTF-16 `E86370`: **GAMEDATA**;
- generic stats mode `+0C = 0`;
- dynamic callback arrays `+2C/+30/+34/+38` are the shared material, text,
  art and texture arrays `50D650/50D268/50D898/50D908`;
- show-initializer callback array `+3C = 0`;
- default timeout `+40 = 15.0`, overridden by these requests' **6.0**.

## Acceptance and paint are separate boundaries

`8ACF0` logs `DIAG_POPUPSHOW` at **entry**. It can later reject a specifically
requested busy slot or find no usable slot and return `EAX = -1` at `8ADB2`.
Do not create a replacement owner from that entry log.

On success it copies the request into a slot, sets slot `+4 = 1`, starts the
layout via `1BD410`, then returns the chosen slot:

- explicit-slot path: activate `8AD55`, layout-start returns `8AD7A`;
- auto/reused-slot path: activate `8AE12`, layout-start returns `8AE27`.

The constructor-specific post-return hooks above therefore provide a precise
source, native result, and copied instance. Require all of:

- returned index 0 or 1, actual `B+0` and request `BA4E78+0` equal that index;
- `B+4 == 1`, `B+0C == 0` at a new binding;
- layout 14, subtype 2, one row/one cell;
- first subtype-2 cell style `B+2B8 == 1`;
- exact expected completion callback;
- well-formed bounded copied title/message/row strings;
- current valid match/teams/package/generation.

The existing actual-slot frame hook `nfl2k5_presentation_native_popup_frame`
is at draw function `8A170` **entry**. It is an accepted instance, but its
layout may not yet be paint-ready: the function can exit for `+F08`, resource
readiness (`1BCE30`) or first-frame policy (`1BC7C0`, `+F1C`). A stricter
native-visible event can be deferred until the actual first-draw latch store
`B+F1C = 1` at **8A1C0**, before call `1BCB40` at `8A1CA`. Whichever boundary
is used, actual browser paint still needs its own token ACK.

## Draw-only suppression and lifecycle to retain

The current `SNF_POPUP_TABLE_ALPHA` hook changes only outgoing alpha for
`8A170 -> 1BD5A0`, return `8A235`. The router sees subtype 2 and calls
**1BDDA0** at `1BD5F5`, return `1BD5FA`. This renderer uses its incoming
float alpha at `[ebp+18]` to generate header/background/text ARGB (e.g.
`1BDDB8`, `1BDDF0`, `1BE07B`); alpha zero hides its output without deleting
rows or skipping renderer behavior.

The remaining original scene is drawn through `1BCE50`, return `8A269`.
Existing layout-effective alpha, dynamic art/texture alpha, and scoped scene
text hooks cover independently colored nodes. Extend their exact owner
predicate to the acknowledged challenge token instead of globally removing
the coin-toss resource or skipping the full native draw/update routine.

Native update `8A840` decrements the timeout at `8A900..8A909`, calls close
`8A340` at `8A92B` when expired, and continues subtype/layout updates. Closing
animation completion is polled at `8A958..8A96A`. Close `8A340`:

1. marks closing and advances the original close animation;
2. calls layout release `1BCDE0`;
3. clears actual active flag at **8A3CD**;
4. calls stored completion callback at **8A3E1**.

`1172F0` clears **BA5D98** and `117300` clears **BA5D9C**. Script handlers
`1175B0 -> 117420` and `1175E0 -> 117580` invoke close-all while those flags
remain set. These callbacks and flags are gameplay/presentation sequencing,
not decoration. They must run normally. `8A810` also has a teardown path
that clears active slots without these callbacks; owner validation must
notice inactive/reused slots even if a normal callback was not observed.

Recommend sending browser HIDE when native close starts, retaining native
draw suppression through its close animation, then releasing suppression
after inactive/reuse. A replacement token cannot carry across a later ruling
in the same slot, page reload, match, team change, package switch, or host
generation. Never compare mutable timeout/elapsed values as stable identity.
Page ACK handling should validate cached host-owned metadata and must not
read guest memory on the browser thread.

## Existing runtime corroboration

`outputs/cbs-2026/native-review-replay02/stderr.log`:

- line 5930: script opcode **152**, type 0, start **117590**;
- line 5931: booth review request `BA4E78`, title Challenge, timeout 6,
  return **11741B**;
- lines 5995 / 6004: actual **slot 0**, active 1, callback **1172F0**, native
  close begins then completes; line 6005 calls **1172F0**;
- line 6067: opcode **153**, start **1175B0**, review close handler;
- line 6175: opcode **154**, start **1175C0**, ruling constructor wrapper;
- line 6176: overturned request, return **117578**;
- lines 6216 / 6220: actual **slot 0**, callback **117300**, close sequence;
  line 6221 calls **117300**;
- line 6248: opcode **155**, start **1175E0**, ruling close handler.

Actual captured images were visually inspected:

- `frames/composed-134.bmp`: referee close-up, original Challenge title,
  booth message, **Spot Of Ball**, NFL logo at left;
- `frames/composed-143.bmp` (PSHOT tick `780573453`): original ruling graphic
  saying **The ruling on the field is overturned.**, with no stale reason
  text underneath.

This corroborates the source binding and independent review/ruling instances;
it does not prove a new CBS replacement is installed. No new native run was
made during this audit. A CBS challenge restyle can safely use these live
native strings, but no claim is made that its visual treatment has been
verified against a dedicated CBS challenge reference.

## Standalone native owner verification

`tools/presentation-audit/verify-review-owner.py` extracts the actual
`NativeReviewOwner` block and helpers from production C++, compiling them into
a small standalone MSVC executable under
`outputs/presentation-audit/review-owner-test`. It does not build or run the
game. Guest RAM, package selection, `read_state`, recording and host time are
fixtures; the owner functions, game/event declarations, `rd32`, and JSON helper
are actual source. Every browser page callback asserts **zero guest reads and
zero state reads**.

After the root agent added its first-draw and subtype-2 cell-style guards,
the verifier passed **1,035 assertions** covering both slots and stages,
rejected/native-mismatched requests, copied payloads, canonical tokens and
wrap, stale owner/package/page/match data, ruling empty-row clearing, lack of
suppression before paint ACK and before the native first-draw latch, live
HIDE revocation, suppression retained through native closing, cleanup, and
page-reset fallback. It also checks actual post-accept generated hooks,
request slot assignment before copy, both-slot close glue, and original
readiness-before-latch order. Source fingerprint for this run:
`28edba9ed6d8a36dbca27516a2ba3eb62a82eb186b0eb9cc2c55a62d6acb21c6`.
This is ownership-unit evidence; a live CBS card capture is separate runtime
verification.

## Verified native CBS replacement

Two independent human-controller match runs reached an ordinary accepted
coach challenge. CPU-only matches correctly disable that menu entry; no
eligibility flag, game outcome or review result was forced. In review06,
normal Team Select assigned port0 to Dallas, ordinary A presses completed
plays, and Start / Down3 / A opened Challenge. Ball Spot Error's confirmation
defaults to Cancel; Down / A selects Call Challenge. The title supplied:

- token1: `Dallas is challenging` / `Spot Of Ball`, callback1172F0;
- token2: `The ruling on the field stands.` / `Dallas is charged a timeout.`,
  callback117300.

Review06 painted acknowledgments arrived266ms and234ms after acceptance.
Both native close callbacks ran and both owners released. Inspected actual
video frames show the CBS navy/dot card above the live Eyebar with no original
NFL/ESPN review panel beneath it. Proof:
`outputs/cbs-2026/review-native-proof-20261019/review.png`, `ruling.png`;
`outputs/cbs-2026/CBS-native-challenge-proof-20261019.mp4`. The silent MP4 joins
the two actual captured native excerpts; the intervening review-camera gap is
omitted. This is a CBS design-system adaptation, not a claim of a measured
CBS-specific challenge reference.

The test exposed a related presentation bug: the actual in-game pause and
challenge menus do not set AC7444. Added `native_pause_active()` using their
active inclusive FSM descriptors, retaining the existing navigation latch.
The common HTML context, replay guard and in-match aspect policy now use it.
No gameplay pause code or guest state was changed. The replay verifier extracts
the actual helper and in_match function: **1,713 assertions pass**, including
nested menus, stale/reused slots, malformed indices, replay cancellation and
zero guest reads from browser callbacks. Review owner:1,035 assertions;
real Edge review component:92 assertions.

Final Release build `native-build-cbs-review-pause-20261019.log` passed.
Fresh review07 reproduced both cards through the fully automatic ordinary-input
driver: acceptance ticks835548140/835582265, paint ticks835548421/835582500
(281ms/235ms), callbacks1172F0/117300 and owner cleanup. Actual composed030
shows the confirmation menu with CBS Eyebar/corner hidden;031 and038 show the
CBS review/ruling cards. All use visible1920x1080 D3D11/internal2x, private
settings/saves, no audio-quality claim. Both games are stopped. Final exported
proof and hashes are recorded in `native-verification.json` beside the PNGs.

## Ordinary-button reproduction and correction of pause detection

The initial `drive-review.py` attempt used `AC7444` as a pause flag. That was
incorrect. Its writes belong to a separate frontend update/init lifecycle
(`270600`, `270B00`, `278310`); it is not written by the native pause entry.
Do not classify a real paused title as unpaused because this word remains 0.

The authoritative pause screen is the current FSM descriptor:

```text
top = MEM32(A84C18)                        // FSM A84B18 +100, inclusive top index
descriptor = MEM32(A84B18 + top*8)
descriptor == 4E9078                       // Paused
descriptor == 52DC20                       // Challenge Menu (reason selection)
data = MEM32(A84C24)                       // current FSM data at +10C
```

Check the top index bounds before reading its descriptor. The native pause
initializer `6EF60` calls `64BC0` (increments shared freeze-reference count
`A83A14`) and `9FC40`; the exit path `6E800` calls `64BD0` and `9FCD0`.
That count can have other users and is not a replacement for exact top-menu
identity.

Independent source/XBE audit of Start handling (`650A0`) confirms a CPU-only
game may pause from neutral port 0. It requires `A83A18 == 3`,
`128670(port)` and `131A10(port)` to allow the port, then tests
`70A10(port,0) & 0x10`. The successful branch consumes Start, records the
pausing port through `F3590`, and pushes **4E9078** at `65105`. There is no
`E5FE88` assignment check in this Start branch. The later disconnected-pad
path is separate. `128670` returns 1 in ordinary offline mode; `131A10`
returns 1 while `BB83E8 == 0`, otherwise uses `BB8458[port]`.

Press-file input is added to port 0 after physical-controller input in
`src/nfl2k5_input_hle.c`; `NO_HOST_PAD` does not remove it. Each queued press
has a 250 ms delay, 200 ms hold, then release. Wait for actual menu identity
and release rather than repeatedly toggling Start. A failed pause attempt
can be diagnosed from `A83A18`, port-permission state, and port-0 Start word
`B37A78`, without changing guest RAM.

**Coach challenge availability is separate from pausing.** `14D190` obtains
the pausing port (`6EEB0`, `B35058`), looks up side assignment through
`771F0` (`E5FE88 + port*1C`), and returns 0 for neutral side 0. `14D1C0`
likewise returns zero remaining coach challenges for a neutral port. Thus a
CPU-only user can open Pause while its coach-challenge row is disabled or
shows No Challenges. The legitimate route is to assign port 0 to a side
through normal Team Select or **Pause > Options > Choose Side** before
requesting a challenge. The latter's static menu link is
`5036F4 -> 51BB70`; this audit did not execute that path. A private test using
normal Team Select with one human side is also valid; never write eligibility
or side assignments directly into RAM.

The Pause list's static rows start at `4E8DA0`, stride `34`, terminated by
type 3. Its first logical visible entries are Resume, Replay, Timeout and
one Challenge variant. `Challenge : 2` (`4E8E3C`) and `Challenge : 1`
(`4E8E70`) both call **14CF60**; No Challenges calls no-op **14CF50**.
`6F070` dynamically shows/hides/disables these rows, so Down three is valid
only after confirming the actual visible list and eligibility, not as a
universal blind recipe. In the runtime menu data, rows start at `data+10`,
stride `2C`; original template pointer is at each row `+4`, hidden flag at
`+8`, disabled flag at `+C`. The template's `+4` is its UTF-16 label.

`14CF60` rejects booth-only periods, a previously viewed replay, or an
undealt-with penalty with a real dialog. Otherwise it pushes reason menu
**52DC20**, whose first static row `52DA80` is **Ball Spot Error**, callback
**14D040**. Other visible reasons include Stepped Out of Bounds, Catch
In/Out of Bounds, Incomplete Pass NOT Fumble, Legal/Illegal Forward Pass,
Hit while Arm was Moving Forward, and Ball Passed Backwards. The selected
reason invokes a confirmation dialog; `14D040` calls **A1DB0** only if its
native dialog returns choice ID **0xB**, then returns to game descriptor
4E7EC0. Observe that screen/result, then the actual accepted review/ruling
events. A menu attempt is not a successful review.

The native challenge predicate `1B11E0` also requires the game's review
option/policy, eligible last-play flag, a permitted flow, positive remaining
challenge count and positive timeouts. `1B11A0` reserves the last two minutes
of Q2/Q4 and overtime for booth review. These rules remain intact. A test
must wait for a genuinely eligible completed play and cannot force a review
merely to exercise a new broadcast card.

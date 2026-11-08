# Original ESPN period-board ownership audit — 2026-10-18

Read-only audit of the existing source, the user's local XBE and prior native D3D11 captures. No build, new game launch, debugger attachment, generated-code change or game-data change was made for this audit.

## Confirmed identity of the overlapping board

The large original end-period graphic is **popup layout 7, `endofqrt`**, not the normal `score_bug` scene, not the following quarter-stat table, and not layout28.

| Identity | Evidence |
|---|---|
| Popup layout table | `sub_001C1530` passes `ECX=AAD180`, `EDX=32` to `sub_001BC880`; the latter stores the base/count at `BE5214/BE5218`. |
| Descriptor size | Layout consumers multiply the popup's `+48` selector by `0x6C`. |
| Layout7 descriptor | `AAD180 + 7*6C = AAD474`. Its first pointer is `E86270`, the literal UTF-16 `endofqrt`; the second is `E861A8`, `OVERLAY`. |
| Requested popup | `B73C44`, reused by the presentation script. This is a request buffer, not an enduring display instance. |
| Actual displayed slot in these captures | Slot1, `B62A70 = B61B50 + F20`. `sub_0008ACF0` copies all `F20` bytes from the request into the selected slot, sets slot `+4` active, then starts its layout. |
| Normal scorebug | Separate `A95528` scene and `sub_000FCE70` path; hiding that scene cannot remove this board. |
| Layout28 | Descriptor `AADD50`, UTF-16 `halftime_player_transition` at `E86594`; unrelated to the Q1 full board. |

Source locations: `recomp_0013.c:106626` (`1C1530`), `:86220` (`1BC880`), `recomp_0004.c:8091` (`8ACF0`). Source line numbers may shift after normal generator patch application; guest addresses are the stable identifiers.

## Native evidence and exact order

`outputs/cbs-2026/native-review-clean01/stderr.log` records:

```text
2947: composed-165.bmp t=721859218
2966: [CARD] ctx=00B73C44 kind=7
2967: [POPUPOP] show obj=00B73C44 slot=1 t=6 'NFL 2K3' '' ret=000DBC5F
2968: composed-169.bmp t=721863218
2983: composed-172.bmp t=721866218
3018: [CARD] ctx=00B73C44 kind=0
3019: [POPUPOP] show obj=00B73C44 slot=1 t=10 'After the 1st Quarter' '' ret=000DBC5F
3020: [TEST] period 1 -> 2 (t=721867343)
3026: composed-174.bmp t=721868218
```

I inspected composed165 and172 directly.165 shows the CBS END1STQTR Eyebar alone.172 shows the original large animated Seahawks/Cowboys/ESPN end-period artwork behind that Eyebar. The request's stale `NFL 2K3` title is not the board's visual identity and is not a suitable ownership key.

`native-review-period01` repeats kind7 at log2958, followed by the kind0 `After the 1st Quarter` request at3018 and period1→2 at3019. `native-review-hd02` repeats kind7 at3060, the quarter-stat title at3116 and period change at3117. These are repeated prior-run observations, not newly launched tests.

The kind28 request in clean01 is at4300, around composed365 during Q2/halftime. It must not be conflated with the earlier frame172 Q1 overlap.

## Script command and native lifecycle

The original command table at `A8F7B8` has12-byte `{type,start,poll}` records. Confirmed from the XBE:

| Opcode | Handler | Behavior |
|---|---|---|
|130|`000DCFB0`|Resets request `B73C44` through `8A270`, requests slot1, enables the request flag.|
|141|`000DD2D0`|Stores byte `ECX` as `MEM32(B73C8C)`, i.e. request `+48`, the selected layout. Layout7 uses the same mechanism as other numbered layouts.|
|133|`002604D0`|Gets the request through `DCFA0`, probes its layout, optionally builds special layouts17–23, then tail-calls `DD290`.|
|134|`000DD420`|Calls `8A430`, the close-all-popup path.|
|132|`000DD430`|Takes a slot byte and calls `8A340(slot)`.|

`DD290` calls `8ACF0(B73C44)` if request flag `B73C40&1` is set. The observed `ret=DBC5F` is the interpreter's normal no-argument command dispatch return. The exact on-disc script byte offset/sequence name issuing this particular layout7 request has **not** been captured, so it is not invented here.

`8ACF0` starts the copied layout through `1BD410(slot+48)`. Readiness and native animation data continue through `1BCE30`, `1BCB40`, `1BCB60` and the regular popup loop. The actual draw function is `8A170`, called with the active slot in `ESI`.

`8A170` preserves the first-draw latch at `+F1C`, tests readiness, obtains table bounds, then draws the native table through `1BD5A0` and scene/layout through `1BCE50`. Optional per-layout callbacks are called through `1BCB00` when slot `+F0C` permits it. They must continue even when replacement graphics own the pixels.

`8A340` performs the native closing sequence: marks slot `+C`, waits for the layout's close timing, releases it through `1BCDE0`, clears `+4`, then invokes completion callback `+F14`. Suppressing or returning early from this routine would change title progression.

## Which quarter the original board displays

The original formatted period text is **`sub_001C07B0`**, in text callback record `50D430`:

```text
50D430 = { 1A443B2D, 001C07B0, 00000000 }
1C07B1: mov eax, [E602B8]    ; title flow state
1C07B9: mov ecx, [E602C4]    ; current title period
```

Flow0A or19 produces the original FINAL text. Otherwise the current-period switch chooses1st,2nd/HALF (based on clock),3rd,4th,OT or numbered OT. This source was checked against the local XBE with Capstone, not only inferred from a generated body that can include fallthrough routines.

For a replacement, snapshot the title period **when the actual kind7 display instance binds**, matching the original formatter's source at that moment. Keep that snapshot with its replacement token instead of rereading a later Q2/Q4 value after the title changes periods. In clean01, kind7 and frame172 both precede the logged1→2 change. If binding first occurs after a transition in another capture, verify the native text/flow rather than automatically subtracting one.

Do **not** use popup `+F18` as an ending-period field. `1C0A90` uses `+F18` to index `AAD168` into stat-scope labels (`Season`, `Last Season`, `Regular Season`, `Postseason`, `Career`). It is not a quarter ordinal.

## Resources and nodes established by the descriptor

Layout7 reuses the shared callback tables:

| Descriptor member | Value | Role established by consumers |
|---|---|---|
|`+2C`|`50D650`|Named material/texture substitutions, including `endQTR_textures`.|
|`+30`|`50D268`|Hashed text callbacks, including period formatter `1C07B0` at record `50D430`.|
|`+34`|`50D898`|Dynamic node art callback table, used in `1BCE50` around `1BD1A0..1BD230`.|
|`+38`|`50D908`|Dynamic texture callback table, used around `1BD263..1BD2F9`.|
|`+3C`|0|No separate callback array on this descriptor.|

The material entry at `50D7D0` is `{E85F54,1C0E80,E85F54}`. `E85F54` is UTF-16 `endQTR_textures`; `1C0E80` resolves a `TXTR` resource through `449E0` and assigns it to the selected material. Neighboring shared named entries include `score_buga`, `z_ESPN_bug`, `nflshield1/2` and ESPN logo names. These are shared table entries; this audit does **not** claim each one is present in the loaded endofqrt scene.

The literal scene-node name corresponding to text hash `1A443B2D` and the runtime scene's individual mesh/node addresses were not dumped. An exact node inventory requires a read-only snapshot of this active slot and its loaded layout/scene. The callback hash, layout name, descriptor and actual popup slot are already enough to identify a conservative draw-only ownership boundary.

## Safe suppression boundary

The existing `SNF_POPUP_*` generated patches already call `nfl2k5_presentation_pregame_draw_replaced(ctx)` immediately before **draw-alpha arguments**, including:

- `8A235` table draw argument, native `1BD5A0`.
- `8A269` layout argument, native `1BCE50`.
- `1BCEC4` effective-alpha argument after optional layout fade callbacks.
- `1BD230` and `1BD2F9` dynamic art/texture node-alpha arguments.

An isolated layout7 owner can reuse those hooks; it does not need to skip native script/update/close code or globally hide every popup during flow15.

Required ownership guards: actual slot only (`B61B50/B62A70`, observed slot1 here), active `+4`, exact layout `+48==7`, no pending/native-close mismatch, current match clock-object/rosters/package/generation, unique per-instance token, and replacement-page paint acknowledgement. Revoke on slot close/reuse/layout change, package/page failure or changed match identity. Keep the native board visible until the corresponding replacement reports it has painted; preserve Original ESPN and packages that do not opt into this replacement capability.

Review requirement: a closing owner followed by a native slot with `+C==0` is a **new instance**, even if the selector/title/match pointers match. A Q3 layout7 can reuse Q1's slot and stale `NFL 2K3` title. Do not let its new draw inherit the old token/acknowledgement/quarter merely because a slower host sampler missed the inactive gap. Reject the closing→open mismatch in the draw predicate and rebind a fresh instance from the guest frame path.

The following kind0 `After the1stQuarter` statistical board is a **separate** ownership task. Hiding it automatically when layout7 is replaced would remove information without a matching CBS table. The original player celebration/replay camera and sequence completion must likewise remain controlled by the game while only their rendered presentation layer changes.

## Remaining verification

No new runtime replacement has been tested by this audit. Next: bind the exact active layout7 slot, serialize its native lifetime/period with a replacement token, render/ack the CBS equivalent, then verify an isolated D3D11 short-quarter run shows no original-board overlap and still advances to the next period. Repeat with package off to confirm Original ESPN remains intact. Full halftime/quarter-stat/replay ownership remains separate; this finding does not establish that every original broadcast call is mapped.

## Follow-up: remaining independent text alpha

The parent subsequently produced `native-review-lifecycle01`. I inspected composed225/227: the large ESPN meshes are gone while CBS END1STQTR is painted, but a small original white `0:00` remains near screen239/179 in the320×240 captures. This is evidence of incomplete native draw suppression, not a data-clock problem.

Source/XBE inspection establishes an independent text draw inside `1BCE50`:

```text
1BD16C: call [eax+4]          ; original text formatter fills BE5228
1BD16F: mov edx, BE5228
1BD174: mov ecx, edi          ; actual scene text node
1BD176: call 1513D0           ; no layout-alpha argument
1BD17B: ... next text node
```

`1513D0` calls `1513B0`, which prepares native text state through `150BF0`. That function calls `150AB0`; the latter reads **float text-node+5C**, multiplies by255, clamps/converts it and packs the high ARGB byte with RGB values from node+60/+64/+68. `150C08` calls `46B50` to store that packed color in the outgoing font-state object's `+20` (`AA2890`). The clock's numeric-font branch also reads `AA2890` at`1512EE` and forwards it as the packed color argument to `F2080`. Mesh alpha and the prior table/layout/dynamic-image hooks do not change this text node's alpha.

A narrow candidate patch can wrap **only** the `1BD176→1513D0` call: if the existing exact popup ownership predicate approves the saved popup context (`MEM32(esp+14)` at this call boundary), save the text node's `+5C` bits, temporarily set that outgoing draw alpha to0, execute the original call, then restore the bits. The original formatter callback has already run; normal text-state setup, layout/node iteration and native update/close/completion paths still run. Capture the node address and saved bits in C locals across the call. Do not permanently change the node, zero its scale/string, skip the formatter or globally hide all font draws.

This is a source-verified candidate, not an already tested fix. An alternative scoped font-color interception would require carrying the exact popup ownership into the deeper renderer; a global `AA2890` hide would affect unrelated UI and is not recommended. The next native capture must show the residual clock disappearing only after the matching CBS paint acknowledgement, with ESPN fallback still visible when no package owns it.

### Implemented patch anchors, pending the parent's native test

The parent selected the narrower **outgoing packed-color** approach rather than temporarily altering a text node. Added only these three anchored entries to `tools/apply-gen-patches.py`:

| Patch | Exact boundary | Host interface |
|---|---|---|
|`PRESENTATION_POPUP_TEXT_BEGIN`|Before `1BD176→1513D0`, generated return anchor `1BD17B`.|`nfl2k5_presentation_native_text_begin(MEM32(esp+0x14u))`; the saved popup context is unaffected by a pushed return because the hook precedes it.|
|`PRESENTATION_POPUP_TEXT_END`|After `loc_001BD17B`.|`nfl2k5_presentation_native_text_end()` balances that one original text draw.|
|`PRESENTATION_POPUP_TEXT_COLOR`|Before `150C08→46B50`, generated return anchor `150C0D` in `150BF0`.|`edx = nfl2k5_presentation_native_text_color(edx)` filters only the packed outgoing ARGB register.|

The parent owns the nesting-safe thread-local scope and exact acknowledged-owner predicate in C++. Color outside that scope is unchanged; within an approved scope only the high alpha byte is removed, preserving RGB. The formatter at`1BD16C`, original `1513D0` call, native node fields and update/close paths are preserved. No persistent guest-node or general font-global hide is used.

The numeric-font path reads the filtered outgoing color at`1512EE` and passes it to`F2080`. The ordinary font path `F1D50` copies the prepared0x80-byte font state into`B9CCC0`; `F0140` reads its packed color from`B9CCE0` (`+20`). The generic branch `46CB0→465D0` likewise reads font-state`+20`.

Checked independent shadow/effect behavior: `150BF0` first calls `46920`, which clears the0x80-byte state and explicitly sets effect mode`+28` to0 at`46991`. No later call in`150BF0` re-enables it (`46A10` sets`+68`, not`+28`). `F1D50` explicitly clears`+28` again. In`465D0`, the independent effect colors`+2C/+40` are used only for effect modes1/2; those extra draws are skipped in this prepared text path. This supports using the single outgoing-color filter for ordinary, numeric and generic popup text without leaving a separately colored shadow.

Validation performed here: Python AST parse passed; all three anchors occur exactly once in their named current generated functions; the saved-context stack location, text-call instruction, packed EDX color and color setter were decoded from the user's XBE. No generated files were edited, no build was run and no game was launched by this subtask. Runtime removal of the residual clock remains the parent's next test.

## Explicit caption extension audit — October 20, 2026

The following branch map was independently decoded from the owned XBE at
`1C07B0..1C08B3`; the literals were read as UTF-16 rather than inferred from
the API's rounded clock. This subtask made no native source, build or game
change.

| Native branch | Literal and address | Browser presentation kind / caption |
|---|---|---|
| `E602B8 == 0x0A` or `0x19`, before the quarter switch | `Final` at `E85A28` | Explicit `final` → `FINAL`, including a final decided in overtime. |
| Quarter 1 | `1st` at `E85A34` | `quarter_end` → `END 1ST QTR`. |
| Quarter 2 and raw clock-object `+10` float exactly equals `0.0f` | `HALF` at `E85A3C` | Explicit `halftime` → `HALFTIME`. |
| Quarter 2 otherwise | `2nd` at `E85A48` | `quarter_end` → `END 2ND QTR`; a displayed zero does not choose HALF. |
| Quarter 3 | `3rd` at `E85A50` | `quarter_end` → `END 3RD QTR`. |
| Quarter 4, without either Final flow | `4th` at `E85A58` | `quarter_end` or `regulation_end` → `END 4TH QTR`; never FINAL. |
| Quarter 5 | `OT` at `E85A60` | `overtime` → `END OT`. |
| Quarter greater than 5 | `OT%d` at `E85A68`, integer argument `quarter - 4` | `overtime` → literal `END OT2`, `END OT3`, etc. |

The Q2 comparison is `fcomp [4E4180]` (`0.0f`), followed by `fnstsw ax`,
`test ah,44`, and `jp 1C0825`. Only the equality branch copies HALF; a
nonzero, negative or unordered raw float takes the `2nd` branch. Sampling
the rounded `gameClock` field is therefore insufficient. The actual native
flow/quarter/clock branch or the already formatted string must be captured
in guest context for the accepted layout-7 owner.

The backward-compatible event extension is:

```text
PERIOD_BOARD_SHOW
  source: "native", nativeVisible: true
  nativePeriodToken: canonical positive uint32 string
  quarter: native quarter snapshot
  nativeCaption: exact literal above
  presentationKind: quarter_end | halftime | regulation_end | final | overtime
```

Only legacy Q1/Q3 events may omit both new fields. Partial, mismatched or
unsupported fields leave the native graphic as fallback. The browser never
chooses Final from Q4, HALF from an API clock, or a statistical scope from
either. The native owner keeps its caption and quarter until its actual
close/reuse; a later quarter delta does not reinterpret it. Through
Regulation is a separate literal table title and is not a layout-7 Final
caption.

`CBSPeriodBoard.presentation()` exposes the validated frozen object,
including `displayCaption`; `displayCaption()` is a convenience accessor.
The existing `activeQuarter()` API remains for visibility and compatibility.
The parent-owned scorebug renderer must consume the explicit caption before
new native producers are enabled. Acknowledgment now checks the exact
painted caption, loaded CBS brand and score font, plus two paint frames.
An END substring cannot acknowledge a HALF or Final caption. If the parent
renderer has not yet painted the expected string, normal state updates can
retry the bounded paint acknowledgment without a timer or guest call.

These are CBS-family caption adaptations. The source audit establishes
native semantic accuracy, not a frame-measured CBS halftime or overtime
animation. `verify-period-captions.js` uses a small renderer adapter in real
Edge; the existing full-page period verifier remains the separate legacy
Q1/Q3 integration check. Native source integration and visible D3D11 proof
remain the parent's work.

After the parent integrated the explicit-caption API into `scorebug.js`, the
full-page `verify-period-board.js` was extended and passed **110 real Edge
assertions**. This test loads the actual shipped HTML, CSS, fonts, period
component and scorebug renderer, rather than the small adapter. It confirms
HALFTIME, END 4TH QTR, FINAL, END OT and END OT2 acknowledgment, frozen
captions across later period changes, real native-close lifetime, and Final
styling before/after the API game-status delta. Timeouts hide for explicit
Final, live clocks remain hidden, and all tested captions fit their actual
center bounds. Standalone sample screenshots are under
`outputs/cbs-2026/period-captions-page-20261020`.

The focused semantic verifier separately passed **91 assertions**. The CSS
audit found no tested clipping when both `final` and `period-ending` classes
are present. The later period-ending rule uses 38px CBS Heavy; after the
native owner closes, normal final styling returns to 30px. That size
difference is reported, not presented as a measured CBS source requirement.
Neither browser run establishes accepted native period instances or a
successful D3D11 capture; those remain separate runtime verification.

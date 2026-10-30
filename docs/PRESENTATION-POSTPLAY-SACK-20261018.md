# Native post-play / sack presentation audit — 2026-10-18

This is a read-only source and existing-log audit. No generated or production code was changed, no game or debugger was started, and no build was run. The original `original/default.xbe` was opened read-only and its instructions/data were compared with the generated C. The task is to recover the game's own presentation cues; an observed increase in sacks is not permission to cut a camera or start a replay.

## Findings that can be used now

There **is a precise original sack decision and presentation notification**. It has not yet been connected to the HTML broadcast event API. The existing popup-title recognizer is not that decision, and the existing live-stat candidate is not that presentation notification.

| Point | Native address / source | Verified behavior |
|---|---|---|
| Actor forwarding | `sub_000A1820`, call at `0x000A187B`, return `0x000A1880`; `src/recomp/gen/recomp_0004.c` | Saves input ECX in ESI and input EDX in EDI, then forwards those two actors to `sub_000B96B0`. |
| Sack decision | `sub_000B96B0`, `0x000B96B0..0x000B97AF`; `recomp_0005.c` | The conditional branch at `0x000B9740` sets `[[0x00E602D4]+0x94] = 1`. It then calls `sub_000A1A00` with the original ECX/EDX actors; return address `0x000B9759`. |
| Sack notification adapter | `sub_000A1A00`, `0x000A1A00..0x000A1A13`; `recomp_0004.c` | In game modes >=4 calls `sub_001BBC60`; then tail-calls `sub_00189010`. |
| Original presentation event | `sub_001BBC60`, `0x001BBC60..0x001BBCCA`; `recomp_0013.c` | Queues native event **0x15** through `sub_000A7B40`, call at `0x001BBCBD`, return `0x001BBCC2`. The function retains the two input actors and pushes both, together with the scalar returned by `sub_001E8DE0`. The EDX subtype is derived from its own contact-list scan, not possession. |
| Separate native side effect | `sub_00189010` → `sub_002075A0`; `recomp_0012.c`, `recomp_0015.c` | The latter sets `0x00BF16F4 = 1`. This write is a real side effect; its downstream meaning has not been established by this audit. |
| Original text finalizer | `sub_000FEA90`, `0x000FEB0A..0x000FEB25`; `recomp_0007.c` | Tests the same result `+0x94`, then formats the XBE UTF-16 literal **Sack** (`0x00E6C898`) into `0x00BA303C`. |
| Text-finalizer caller | `sub_000A0390`, call at `0x000A03FD`, return `0x000A0402`; `recomp_0004.c` | Calls the text finalizer after `sub_000B7230`, `sub_00189080`, `sub_001CEEE0`, `sub_001E8480`, `sub_001CEEE0`; next calls `sub_000B7330`. This is a later display preparation point, not the sack decision itself. |

The instruction bytes for the decision and original event path were directly disassembled from the owned XBE. The generated implementations agree with them; these conclusions do not depend on similarly named helper functions or a scoreboard heuristic.

## The original sack decision's conditions

`sub_000B96B0` first requires flow state `[0xE602B8] == 0x0E`. It saves original ECX as EBX and EDX as ESI, and writes ESI to result `+0xA8`. The branch that writes result `+0x94 = 1` also requires:

- `[0xE602B4]` is 3 or 4;
- `[[0xE602EC]+0x1A4] == EBX`;
- the result of `sub_000B58C0` compared against zero takes the native positive branch;
- `[0xE602BC]` is 4 or 5;
- `[[0xE602EC]+0x1B0] == 0`;
- `[[0xE602EC]+0x130] == 0`.

Those are the actual gates. Their existence does not prove independent English names for every field. In particular, do not copy this logic into a host-side polling detector; observing the decision call in the established guest context avoids sampling these fields during a partial update.

`[0xE602D4]` is initialized to the result object at `0x00B28260` by match setup (`recomp_0002.c`, store at its `MEM32(0xE602D4) = 0xB28260`). Retain the pointer as the owner instead of assuming one permanent physical location in a general API.

## What the two actors do and do not prove

The exact two pointers are preserved by `A1820 → B96B0 → A1A00 → 1BBC60`. The first is compared against `[E602EC]+0x1A4` during the sack decision. That is strong evidence for the tackled passer/ball carrier. The second is the other actor forwarded by the contact path and stored at result `+0xA8`. It is a **candidate** sack defender, not yet a certified credited defender or sole sack winner.

Missing verification:

1. A live trace with both actors resolved to roster records and both team rosters, correlated to the game's visible sack chip and Gamecast credit.
2. Whether the second actor can be null, an assist, an indirect collision participant, or a different contact actor on shared sacks.
3. The original presentation event's eventual chosen graphic subject, which can differ from the first collision actor.

The generic native event helper `sub_000A7B90` can convert actor pointers through `actor+0x3C`. That is useful corroborating context, **not** sufficient by itself to publish every `+0x3C` as a validated current roster record. Use the existing identity reader and roster-membership checks once the exact relevant actor is known.

Do not assign the possession team's QB as the player celebrating, or infer the defensive side merely by flipping whichever possession was read later. A turnover, penalty or asynchronous presentation can make that wrong.

## Current broadcast bridge: a real gap

`src/nfl2k5_presentation.cpp::nfl2k5_presentation_popup` currently recognizes `SACK` only by an uppercase substring in a **generic popup title**, queues `{"name":"sack", team, ""}`, and requests commentary. Its team comes from `s_prev.poss`, so it is not a verified defender side; there is no player payload in that event.

`html_event_name` **does** map `sack → SACK`. The statement in older `docs/PRESENTATION-SYSTEM.md` that SACK is only defined and never produced is stale with respect to this substring producer. However, the original `FEA90` result-string write described above does **not** call `nfl2k5_presentation_popup` or `sub_0008ACF0`. Thus seeing the original on-screen word Sack does not prove an HTML SACK event was emitted.

The scope-0 native sampler separately notices a sack-stat increase and assigns a defensive `PLAYER_STAT` candidate priority 7. That proves data changed, and can supply a truthful statistical insert. It does not identify the original ESPN display start, celebration start, automatic replay start, or return transition.

Existing runtime counterexample: `outputs/cbs-2026/native-review-extended01/stderr.log:4488` shows kind 2, blank-title native slot-1 card; `:4556` later delivers real `PLAYER_STAT` for La'Roi Glover (1 tackle, 1 sack, 1 tackle for loss) after the native popup closes. No SACK event was found in that log. The proximity supports doing a focused capture; it does not certify that kind 2 universally means sack or prove the displayed card's identity from the stat delta.

## Recover the exact card rather than synthesize its subject

The game already has a shared graphic construction pipeline:

1. Script interpreter `sub_000DBC10`; opcode table at `0x00A8F7B8`, 12 bytes per entry. Existing `DIAG_SCRIPTOP` records opcode, native handler and context.
2. Script opcode **133** invokes `sub_002604D0`. It obtains the shared configuration at `sub_000DCFA0`, runs kind-specific completion where applicable, then tail-calls `sub_000DD290`.
3. `sub_000DD290` tests bit 0 of `0x00B73C40` and tail-calls `sub_0008ACF0(ECX=0x00B73C44)` when enabled. `B73C44` is a configuration object, not a permanent active popup slot.
4. `sub_0008ACF0` copies/shows into the real slots `0x00B61B50` and `0x00B62A70`, stride `0xF20`.
5. Native close `sub_0008A340` preserves the fade and eventually executes the slot callback at `+0xF14`. Hiding a draw must not skip that close callback.

For a verified single-player fact the existing bridge already has useful validated fields:

| Offset | Meaning / guard |
|---|---|
| `+0x00` | native slot ID |
| `+0x04` | active flag |
| `+0x08` | native timeout |
| `+0x0C` | closing flag |
| `+0x10` | subject type; **1 player**, **2 team** in the stat construction branches |
| `+0x14` | subject value; use as player only after its subject type/family and roster membership are validated |
| `+0x40` | subject count / class used by layout selection |
| `+0x48` | native layout kind, not `Nfl2k5StatKind` |
| `+0x4C` | inline UTF-16 subject/title |
| `+0x228` | native table/prose subtype |
| `+0x22C`, `+0x230` | table dimensions, with axis convention defined by `sub_0025F8B0`; for prose subtype 3 these instead contain character count / inline text |
| `+0x234 + i*0x88` | cell descriptor; type 0 inline UTF-16 at `+8`, type 1 pointer at `+8` |
| `+0xEF4` | cell count (at most 24 initialized by `sub_001BEF40`) |
| `+0xF14` | native end callback |
| `+0xF18` | caption/scope selector used by some single-player layouts |

`sub_001C16B0` specifically maps the single-player table case to layout 1. Do not assume a blank-title layout 2 has this same schema.

The script's stat constructors explicitly retain subject distinctions. Opcode **144** / `sub_0025F1B0` loops subjects, reads subject type, then calls `sub_0025ED80` for a player or `sub_0025EE30` for a team before formatting a native cell. Opcode **145** / `sub_0025F460` does the same for a selected subject and a native caption. `sub_0025ED80` takes a selector and native scope mapping before calling `sub_000CB240`. These are much better future hook points for recovering original requested stats/scope than guessing from the visible number or the active QB.

Owned-XBE corroboration: static current-match player descriptors at `0x00AE4580`, `0x00AE45A0`, `0x00AE45C0` are respectively Sack, Sacks, Sacks Today; each has formatter `0x00329400`, selector `0xB8`, scope 0. Selector B8 preserves half-sack units via the already documented `2F * 0.5`. The unrelated leaderboard category at `0x00AC6D00` says Sacks Today and uses `25B8C0/25B8D0/25B910`; that ranks/formats top players and is **not** proof of the post-sack chip trigger.

The current `nfl2k5_presentation_playercard` acknowledgement path intentionally accepts only the exact Consecutive Incompletions title. `nfl2k5_presentation_player_fact` can copy roster identity, native cells or native prose for supported single-player layouts. Neither makes every original ESPN card replaceable yet. Extend with a measured family and preserve native fallback until the replacement acknowledges its exact owner.

## Celebration is a separate native lifecycle

The source contains real animation-resource paths:

- `sub_001B6800`, called at `0x00155253` (return `155258`), conditionally selects a **POSTPLAY** resource (`0x00E846F8`, table `0x00AACA38`) and invokes `sub_002C93F0` with the selected resource and callback `0x001B6590`.
- `sub_001B6B50` selects user celebrations from the 12-byte rows at `0x0050CFC8`; records resource/selection globals `BE50C4/BE50CC`; requests the **CELEBRATE** resource (`0x00E8470C`) through `sub_001685B0` at `0x001B6C31`. Its caller `sub_002DE9C0` checks the current actor handler before attempting it.
- `sub_001B6C70` polls that selected CELEBRATE resource via `sub_001685E0` at `0x001B6C7F` (return `0x001B6C84`) and returns an availability result.

The XBE has `POSTPLAY`/`Postplay.iff` and `CELEBRATE`/`Celebrate.iff` names. **These names alone do not prove which is used for an automatic sack celebration**. The already modified user/team touchdown celebration menu path is especially unsafe to rebrand as the automatic sack-camera cue. Inspect exact native calls/loaded scene identity during the real sack before deciding where the network wipe belongs.

## The ordering still needs a focused trace

What is proven is the local control-flow order:

`A1820 → B96B0 sack decision → A1A00 → 1BBC60 event 0x15`, with a separate later result-text preparation in `A0390 → FEA90`.

It is **not** yet proven that every sack always orders a card, celebration, replay and return identically. Presentation can choose or skip a replay and can choose a historical card. The generic card handler must not become an unconditional replay trigger, nor should a fixed host timer switch cameras after a sack.

Recommended next instrumented run, without forcing football state:

1. Add a bounded guest-context observer immediately after the original sack branch call at **`0x000B9759`**, gated by the exact result `+0x94` and matching live owner. Copy actor IDs/pointers, flow, period, clock object, result pointer and relevant result serial, then return without altering registers, x87, memory or original control flow. One record per real decision; do not continuously log every update.
2. Correlate the original event enqueue at **`0x001BBCBD`/`0x001BBCC2`** with that decision. Verify the event's actor parameter order and subtype in the central native event record. Do not prematurely expose a defender identity.
3. Capture script command starts, the actual source configuration and final active slot show/close, including native layout, subject type/value, cells/prose, caption, timeout and callback. Copy data to owned host structures while in valid guest context.
4. Record original active scene/camera lifecycle separately, including whether the post-play scene is a celebration. Pair the automatic replay lifecycle observer with those scene changes. The replay audit is separate from this document.
5. Compare at least a solo sack and shared-sack/penalty-cancelled case with native Gamecast credit and composed game captures. Treat unverified player, scope or timing as unavailable; retain ESPN fallback for that family.
6. Only then emit an actual named SACK result event and a separately owned NATIVE_GRAPHIC_SHOW/HIDE/replay/celebration lifecycle. The selected CBS/NBC/FOX/modern ESPN package can translate those cues while the original camera, scene, script polling and callbacks continue.

This gives a reusable presentation seam for all networks while retaining the game's football and cinematic decisions. It does not require inventing sack outcomes, stat totals or camera choices.

## 2026-10-19 verification: credited sacks, native subject selection and real captures

This addition checks the actual generated implementations against fresh Capstone disassembly of the read-only owned XBE. The bounded instruction transcript is `outputs/presentation-audit/sack-credit-20261019/owned-xbe-disassembly.txt`. No production/generated source, native build or running game was changed for this audit. Earlier statements above about missing runtime actor/credit evidence are now narrowed by the solo and shared examples below; uncertainty about universal camera/card ordering remains.

### Two different native record systems must stay distinct

The presentation queue uses **64-byte records** at `0x00B667E0 + ringIndex*0x40`, with a512-record ring. `sub_000A7AD0` computes the candidate pointer in EDI from `B6E7E4`, converts its optional actor arguments through `actor+0x3C`, and calls `sub_000A7A40`. Only its nonzero return means the record was accepted. `A7A40` rejects a missing current context (`B6FF68`) or a full ring before writing anything.

`sub_000A7690` writes the accepted presentation record:

| Field | Verified meaning |
|---|---|
|`+0x00`|Native presentation event ID; `0x15` for this sack notification.|
|`+0x04`|Copied bits from `B6FF7C`, used as a **float native time**, not an integer serial.|
|`+0x10..+0x1F`|Copied vector.|
|`+0x20`|First actor's resolved player record, or zero.|
|`+0x24`|Second actor's resolved player record, or zero.|
|`+0x28/+0x29`|Additional byte identity fields derived through each player's native structure. They are not full player pointers.|
|`+0x30`|Subtype supplied by the caller.|
|`+0x34`|Float scalar supplied by the caller.|

**Correction:** the sack01 executable printed record `+4` as `nativeSerial=1142135100`. Interpreting those same bits as a float gives **590.394287**. Current source correctly calls it `nativeTime`. Neither that float nor the ring pointer is a unique play ID. `A7A40` separately advances `B6E7E4` modulo512 and increments `B6E7E8`; the latter is an accepted-queue counter, with its own context/reset lifetime, not proof of an NFL play number. A host sequence scoped to an explicit current match is the simplest safe broadcast event token.

The Gamecast history scanned by the sack accumulator is a **different20-byte record ring**. `sub_000CCF80(index)` returns `0x00E53874 + (index % 0x300)*0x14`. Its low-nibble type6 is not presentation event6 and is not presentation event `0x15`. Do not use the presentation-ring field offsets to interpret this history.

### Family7 is the native sack-credit accumulator

The24-byte family table begins at `0x0050EB80`. Family7 is its row at `0x0050EC28`:

```text
28 00 00 00  02 00 00 00  08 00 00 00
A0 BC 1E 00  30 CE BE 00  10 D0 BE 00
```

The callback at row`+0x0C` is **`sub_001EBCA0`**, with cache/count fields `BECE30/BED010`. `sub_001ECA20` computes `family*24`, passes the native player subject mode2, and calls `[familyRow+0x0C]`. Thus this is a real table-driven statistics consumer, not a function guessed from its address.

For family7, `sub_001EC830`'s jump-table entry7 is `1EC87D`: it stores the accumulator pointer at stat-root`+0x28`. `sub_001EC960` entry7 is `1EC9AD`: it stores the associated native float time at stat-root`+0x24`. The native base selector `0x2F` getter, `sub_0024EF90`, checks stat-root`+0x24`, loads stat-root`+0x28`, and returns its byte`+4`. The derived selector `0xB8` multiplies that raw count by0.5. This independently connects the family7 history arithmetic to the scope0 Gamecast sack values already exposed by the live collector.

`1EBCA0..1EBD9F` accepts a subject in ECX, an8-byte output accumulator in EDX, and four stack arguments controlling native subject mode, accumulation/reset and the half-open history index range. It clears the8-byte accumulator when reset is requested. In player mode2 it derives the native side from `player+0x34`; other native modes represent a side. It resolves compact history IDs with `sub_000BBA60` and applies the original opposing-side condition through `sub_000BCC90`/`sub_000BBE30`. The side formula and player-ID resolution should be preserved rather than replaced by a later possession sample.

The credit-bearing history fields and instructions are exact:

- `1EBD3D..1EBD43`: require `(history.word0 &0xF)==6`.
- `1EBD20..1EBD2A`: resolve `history.byte[+6]` through `BBA60`; retain the resolved pointer in EDI.
- `1EBD4D..1EBD55`: for a player subject, resolve `history.byte[+0xD]` through `BBA60` and compare with that subject.
- If `+0xD` matches and the `+6` player is null, `1EBD77..1EBD7C` adds **2** to output byte`+4`: one full sack in half units.
- If `+0xD` matches and the `+6` player exists, `1EBD70..1EBD7C` adds **1**: half a sack.
- If `+0xD` does not match but the nonnull `+6` player matches, `1EBD67` adds **1**: the other half sack.
- If neither matches, the player receives no credit from that record. In native team mode the same accepted sack record adds2 half units.
- `1EBD7F` also subtracts the signed yard byte at history`+4` from output word`+6`. The routine does not halve that yard subtraction for shared participants.

`BBA60` treats compact ID0 as null. Even nonzero IDs decode against the native base returned by `61B60` (`B30C4C`); odd IDs decode against `61B70` (`B321A0`), with record stride`0x54` and index`(ID-2)>>1`. Its inverse `BBAA0` checks the player's native side byte and generates the same parity/index encoding. These static roster bases explain the original layout; the host must still validate each resolved record against the current roster membership so mods and a new match cannot inherit stale identity.

In concise pseudocode, **after** the original side and low-type6 gates:

```text
second = decode(history[6])
first  = decode(history[0xD])
if nativeTeamSubject:
    halfUnits +=2
else if first == player:
    halfUnits += second ?1 :2
else if second && second == player:
    halfUnits +=1
sacksDisplayed = halfUnits *0.5
```

Here `first` and `second` name the two history-credit slots only. They do not identify the order of collision contacts or assert that the presentation event's second actor is always the first credit slot.

### Original script subject selection: opcode24 and selector73(decimal)

The owned XBE registry entry `A8F7B8 +24*12` is `{type2, start55450, poll0}`. The generated `sub_00055450` agrees with its native bytes:

1. Save zero-extended CL as the selector in ESI.
2. Call `67970` to obtain the current original script/event context.
3. Call `A89F0(ECX=context,EDX=selector)`.
4. At **`55462`**, store the returned player pointer into **`B25E24`**.

The existing `PRES_SELECTED_PLAYER` patch observes that store afterward. This is selection for the original graphic/transition subject; it does not itself change Gamecast credit or prove a new sack.

**Selector73 means decimal73 (`0x49`), not hex`0x73`.** `A91B0 +73*4` contains `A8FF2`, which calls `D2E00` and returns its result. Hex`0x73` is a different switch entry and routes to `DB0E0`; mixing those bases would trace the wrong subject selector.

The `D2E00` path is a general defensive/post-play actor selector, with these source-backed priorities:

1. Retain the candidate from `D2D20` (event`0x1D`'s player`+0x20`).
2. Return a nonzero `D2C40` result first. That helper prefers event5's player`+0x24`, then event`0x50`'s player`+0x24`, then event`0x51`'s player`+0x20`.
3. Otherwise call `D2CA0`. It requires that an event **`0x15`** exists. If an event5 also exists, it returns **event5`+0x24`**, not event15`+0x24`. If no event5 exists, it looks up event`0x50` and returns **that event's`+0x24`**. If the required combination is absent, it returns zero.
4. If still unresolved and the retained event`0x1D` actor exists, scan events`0x55`, then`0x0D`, returning a`+0x20` actor whose native side differs from that retained actor.
5. Finally try event6's player`+0x24`; otherwise return zero.

`A7CD0` performs a bounded lookup within the original context's valid queue interval; `A8650` finds the later matching instance through the queue iteration helpers. `D2CA0` therefore does **not** mean “read the second actor directly from the sack event.” Event15 acts as a gate while event5/50 can supply the selected defender. Independently, `D2C40` can satisfy the broader selector without event15. Consequently selector73 alone is neither a sack detector nor a guarantee that the selected player owns every sack credit.

### Runtime correlation: sack01 solo sack and title-selected timeout

`outputs/cbs-2026/native-review-sack01/stderr.log` supplies the previously missing real actor/credit evidence:

| Log line / host tick | What actually happened |
|---|---|
|9819 /762522296|Accepted event`0x15`, record`B6B220`; first player`B32C20` Brad Johnson, away; second player`B30F40` La'Roi Glover, home; subtype0. The stale selected subject was `B30CA0` Pete Hunter.|
|9821 /762522296|Exact `B96B0` sack decision, result`B28260+94=1`, flow14, Q2 clock75.524s; actorA`B3F280` resolves Brad Johnson(QB); actorB`B3F550` resolves La'Roi Glover(DT). Same clock`B28680` and rosters`B30A58/B30864`.|
|9860 /762523156|Same-match scope0 Gamecast sample: Glover's **known** sack total changes0.0→1.0,860ms after the accepted event.|
|9886 /762523890|The title's presentation flow reports a genuine **TIMEOUT**, away team.|
|9893–9894 /762524137|Existing live-stat policy sends `PLAYER_STAT`, home, source game, scope this_game: L.Glover,1 tackle,1 sack,1 tackle for loss.|

`frames/composed-224.bmp` at762523281 shows the real completed sack and3rd&13. `composed-225.bmp` at762526281 shows the CBS Glover stat insert over the close-up. These images were inspected in this audit. They corroborate the player/stat payload, not a native sack-graphic replacement ACK.

There is **no automatic-replay FSM/READY trace after this sack** in the remaining captured sack01 log, although those observers produced multiple complete replay cycles earlier in this same run. The original title chose its timeout/presentation path. A replacement package must not force a replay, an NFL/CBS wipe or a celebration camera just because event15 or a sack total was observed.

### Runtime shared-sack counterexample: actorB is insufficient

`outputs/cbs-2026/native-review-replay01/stderr.log` contains a real shared-credit sequence:

| Log line / tick | Evidence |
|---|---|
|7153 /774254375|Accepted event15: Quincy Carter(`B31624`,home) and Wendell Bryant(`B3253C`,away).|
|7157 /774254375|Matching genuine sack branch, Q2 clock117.273s, result`+94=1`, same current-match owner.|
|7192 /774254828|Bert Berry(`B322F0`,away) scope0 **known** total0.0→0.5,453ms later. Berry was not event15's actorB.|
|7197 /774255265|Wendell Bryant scope0 **known** total0.0→0.5,890ms later.|
|7202 /774255759|The existing separate `PLAYER_STAT` insert truthfully carries Bryant's sack total0.5.|

This is empirical evidence that attributing the whole sack solely to event15 actorB is wrong, and that the two sampled co-credits can arrive437ms apart. The family7 `+1/+1` history arithmetic agrees with the observed halves. The capture does not contain a direct dump of those history bytes, so the exact `+6` versus`+0xD` ordering of Berry/Bryant in this play is **not** claimed.

The same run also confirms that the original script can select a sack defender: line5374 at774141406 records selector**73** choosing Willie Blade after the earlier real sack/event15 and his known0→1 scope0 credit at line5325. That observation supports the recovered selector path, while the shared example proves why the chosen subject must remain separate from the complete credit list.

### Minimal safe producer: genuine result, confirmed credit, separate presentation ownership

The smallest sound implementation can reuse the accepted-event hook, native roster identity reader and existing scope0 collector. It does not need to rewrite football logic or force a camera:

1. **Create one pending host record only after event15 was accepted.** The existing anchor is the exact `A7AD0→A7A40` call with return`A7B0F`; require EAX!=0 and the copied EDI record's event ID15. Copy the record now, while in guest context. Preserve the float timestamp as a timestamp. Assign a nonzero host sequence; do not use record`+4`, the rotating queue address or stale `B25E24` as its ID.
2. **Bind it to the current match, not just a player pointer.** Retain the live owner's clock object, both roster pointers, both team abbreviations and the host match epoch. Retain native period/clock and the accepted event's timestamp/actor candidates. Increment that epoch only from a real match-lifecycle change; a duplicate browser `GAME_STARTED` during replay is not a new match. Reject old pending results on actual match reset, roster/clock owner replacement, unload/final teardown or package ownership reset.
3. **Require real scope0 credit for attributed players.** A candidate needs a known earlier same-owner baseline, a strictly positive finite sack delta in half-unit steps, and a sample timestamp after this accepted result. A previously unavailable counter becoming positive is useful for the existing generic stat card, but is not enough to prove new credit for this event. ActorA/B are candidates, not a substitute for this confirmation.
4. **Preserve shared credit.** Keep an array of credited defenders with their0.5/1.0 play credit and their actual current Gamecast totals. Do not emit a solo1.0 sack for the first0.5 arrival, round halves away, or discard Berry because he was absent from actorB. For complete attribution, correlate the matching fresh low-type6 history record and its decoded `+0xD/+6` participants with those scope0 changes; the native history identifies who should arrive and whether each receives one or two half units. Require unique current-roster membership and defensive-side agreement for every published identity.
5. **Do not turn temporal proximity into a fake exact join.** These logs prove totals/actors but do not yet expose a direct presentation-event15→20-byte-history-index key. Until that association is copied at its genuine producer or independently proved, a complete typed credit list must remain unavailable when two pending sack results or stale samples overlap. Publish the genuine result without an invented player, or retain original fallback; do not silently assign any nearby positive counter. A later `PLAYER_STAT` may still use truthful current totals separately.
6. **Bound pending work without blocking.** Allow at least the collector's actual full-roster refresh cycle plus a measured margin, then expire unsupported attribution. Observed legitimate solo-credit latency in replay01 reaches3,140ms, so a750ms/one-frame credit deadline would lose real sacks. A new accepted play/result boundary, roster epoch change or stale snapshot should invalidate the old join rather than letting it decorate a later play. Expiry yields missing attribution, never invented success.
7. **Emit at most one real result token, keeping credit and subject distinct.** A future `SACK` payload can carry `source:native`, the host result token, current-match ID, defensive side, optional tackled passer, and the verified credited-player array with fractional values. The original selector73 subject can be retained separately for a native graphic owner if it matches that result; it must not overwrite the credit list. Package-specific sacks/team chips can react to that result, while actual popup SHOW/HIDE, celebration and automatic-replay events continue to govern their own scenes and wipes.

No SACK producer implementation is asserted complete by this audit. The proven solo/shared captures close the identity/half-unit question. The remaining narrow dependency for an **exactly attributed** real-time producer is the native event-to-history record association (or an equivalent unambiguous native play generation), plus a live capture of the resulting typed payload. Neither uncertainty justifies forcing the replay path chosen by the game.

Read-only validation rerun: `python tools/cbs-scorebug/verify-live-selectors.py --self-test --xbe` passed all32 field fingerprints,7 corrupted-source rejection cases, the owned-XBE hash/base/derived tables, the half-sack factor and all17 native position names. This is source/data verification, not a new game execution test.

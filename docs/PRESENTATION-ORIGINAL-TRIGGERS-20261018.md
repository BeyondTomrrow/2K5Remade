# Original broadcast triggers and replacement coverage — 2026-10-18

**We do not yet have every original broadcast call connected to the modern packages.** We have a working common presentation layer, several verified native graphic lifecycles, and newly recovered quarter/replay/sack paths. A working scorebug or a compiled script handler is not proof that every ESPN graphic has a safe replacement.

This document aggregates the read-only source/XBE/runtime-log audits listed below. The new CBS quarter-board owner has passed the browser/native unit checks reported by the parent task; its new in-game overlap test is pending at the time of this entry. Do not turn that into a runtime-success claim until composed game captures confirm it.

## What the command inventory establishes

`outputs/presentation-audit/command-registry-20261018.json` records the original table at `A8F7B8`, dispatched by `DBC10`:

-  256 slots,255 populated,33 operand types.
-  250 distinct nonzero start handlers and46 distinct nonzero poll handlers.
-  Every nonzero handler has a recompiled entry; no missing-handler flags were found.
-  This inventory run had no input logs (`log_files: []`); its observed-start/poll counts are zero and are not a coverage measurement.

These255 commands include waits, script control, actors, cameras, replay configuration, sound and graphic construction. **They are not255 understood graphics or255 implemented modern animations.** The older `ESPN-GRAPHICS-TRIGGER-MAP.md` contains observed command/card summaries from its own logs, but most semantic opcode labels there remain unclassified. Use the new focused audits to correct stale or ambiguous associations.

## Producer, observation and production status

| Cue/family | Exact source versus heuristic | Native evidence | Current replacement status |
|---|---|---|---|
|Live scorebug state|Scores/teams/clocks/down/possession read from guest state; `FCE70` reports original scorebug update/eligibility.|Repeated real D3D11 games.|Live CBS/NBC/FOX state bindings exist; ordinary bug suppression is a separate scene path. Does not hide other ESPN cards.|
|Generic card/layout show|Script130 resets request,141 selects layout,133→`2604D0`→`DD290`→`8ACF0` copies it into a real active slot.|Actual `B61B50/B62A70` shows and closes in prior logs.|Exact instances/cells can be copied; replacement needs supported family plus paint acknowledgement. Not every blank-title card is classified.|
|Native card close|`8A340` marks closing, completes native fade/release, then runs callback `+F14`.|Repeated native show/close traces.|Existing draw-only hooks preserve updates and callback progression. A host timer or early return is not an equivalent close.|
|Q1/Q3 end board|Layout7 `endofqrt`, descriptor`AAD474`; period text formatter`1C07B0` reads actual flow/period.|clean01 frame172 shows original ESPN board overlapping CBS; kind7/slot1 show precedes it.|New isolated CBS Q1/Q3 owner/paint-ack integration under test. Q2/final original board stays as fallback.|
|Following quarter statistics|Separate kind0 request titled `After the1stQuarter`/`After the3rdQuarter`.|clean01 kind0 follows layout7 and precedes period increment.|Not automatically covered by layout7 ownership. Needs faithful CBS table/data and its own acknowledgement.|
|Period flow/final|`B8A00` selects actual halftime/end-period flow09/15; actual gameover is flow0A.|Short-quarter native run reached halftime/OT and a real GAME_ENDED.|CBS ordinary END label/final detection integrated. Older period-increment events are separate; they are not the original board-show instant.|
|Player/team live statistics|Scope0 native accessors and roster identity; changed counters create optional stat-insert candidates.|Native QB/RB/WR/DEF/team/drive examples already captured.|Data-backed inserts work. A stat delta is not the original ESPN card trigger, celebration or replay request. Unsupported values stay unavailable.|
|Original sack decision|`B96B0` branch`B9740` sets result`+94`; calls`A1A00`→`1BBC60`, original event0x15.|Owned-XBE/source control flow verified; actor/visible-chip correlation still needed.|No exact sack decision hook implemented yet. Current popup-title SACK recognizer is heuristic and lacks certified credited-defender payload.|
|Sack player chip|Shared native layout/subject/cell construction; player/team subject type must be validated.|An actual blank-title kind2 precedes a later Glover sack-stat insert in extended01.|Proximity does not prove kind2 always means sack. Must capture exact subject/layout/cells and Gamecast credit before replacement.|
|Post-play celebration|POSTPLAY/CELEBRATE resource selection exists in`1B6800`/`1B6B50`.|Source/XBE verified; automatic sack-specific scene not identified.|No forced camera/celebration. User touchdown-celebration selection is not an automatic sack cue.|
|Automatic replay entrance/playback/return|Real screen descriptor`4E3F38`; six state transitions in`AFA078`, separate cover/completion thresholds.|Eight actual descriptor pushes in followup01; internal phases need frame correlation.|New replay observations are diagnostic only. No production replay lifecycle replacement/ESPN-wipe suppression yet.|
|Replay title heuristic|Generic popup title containing REPLAY queues replay_begin.|Existing bridge code, not reliable automatic-replay lifecycle.|Do not use as proof the automatic wipe started. replay_end alias previously had no native producer.|
|Replay pause diversion/reset|Distinct immediate cleanup→Paused descriptor`4E9078`, or reset path.|Source/XBE verified.|Must cancel replacement cleanly, not play an ordinary broadcast return.|

## Why the quarter graphic overlapped

The ordinary ESPN bug is scene`A95528`. The large end-period board is a **different popup**, layout7, normally copied from request`B73C44` to active slot1`B62A70`. Hiding scene`A95528` therefore leaves the large board drawing.

The prior clean01 sequence proves the distinction: kind7/slot1/t6 → visible ESPN end-period artwork in composed172 → separate kind0 quarter-stat request → period1→2. Layout28 is `halftime_player_transition`, not the Q1 board. The stale request title `NFL2K3` is not a safe visual identity key.

The new replacement uses layout7's real active lifetime and a fresh token. It leaves native timing/update/close callbacks running, hides only outgoing draw alpha **after** CBS acknowledges painting the matching replacement, and keeps Original ESPN/unsupported packages intact. Ending period is captured when the actual instance binds, rather than rereading a later next-quarter value. Q1/Q3 are the measured CBS END-label variants; other periods retain original artwork until their replacements are supported.

## Sack → player → replay → return

The desired broadcast sequence should follow the game's actual decisions:

```text
Native sack decision / original event0x15
        ↓
Native post-play scene and selected graphic subject
        ↓
Optional native celebration / player chip
        ↓
Actual automatic-replay entrance, if the game chooses one
        ↓
Replay camera/playback
        ↓
Actual return transition and live-data restoration
        ↓
Selected network's return/reveal treatment
```

A sack does not guarantee the same chip, celebration or replay every time. The second collision actor is a candidate defender, not yet a proven sole credited sacker. Shared sacks, penalties, skips and pause diversion need truthful identities and cancellation. We must not assign the active QB as a celebrating defender or make a host timer choose a camera.

The recovered replay phases provide the better seam:0→1 starts original entrance;1→2 switches replay actor/camera data at its cover threshold;2→3 finishes entrance;3→4 requests return;4→5 restores live data at cover;5→0 completes return and requests screen pop. These are separate from the raw replay-screen request and from a suspend callback. Diagnostic observations must first be correlated with composed frames before becoming production broadcast cues.

## One reusable seam for every network

```text
NFL2K5 football / actors / native scripts and camera decisions
        ↓
BroadcastState + typed native cue + copied identity/stat payload
        ↓
Per-instance native graphic ownership and show/close tokens
        ↓
Presentation manager
        ↓
CBS / NBC / FOX / modern ESPN editable motion and graphics
        ↓
Paint acknowledgement → suppress only the matching original draw
```

Football facts, cinematic decisions and native callback completion remain with NFL2K5. Each package chooses its own measured artwork, timing, layers and wipe treatment. Reuse the same exact cue/ownership/data contract across networks; do not put CBS-specific names into gameplay logic. Fallback remains visible if a package does not implement or acknowledge that family.

Next integration work is exact native subject/cell capture for the post-sack chip, bounded sack-decision observation, frame-correlated replay phase traces, then separately acknowledged original-wipe rendering ownership. These are the foundations for replacing the wider2004 ESPN presentation accurately; the current evidence does not justify claiming complete replacement.

## Detailed evidence

- `PRESENTATION-PERIOD-BOARD-20261018.md`: exact layout7 resources, script commands, quarter formatter, prior composed evidence and safe draw hooks.
- `PRESENTATION-POSTPLAY-SACK-20261018.md`: original sack branch/event, actor limitations, native chip schema and celebration-resource boundaries.
- `PRESENTATION-REPLAY-LIFECYCLE-20261018.md`: automatic replay descriptor, six phases, thresholds, natural/button exit and pause diversion; diagnostic patch sites.
- `outputs/presentation-audit/command-registry-20261018.json`: static opcode/handler inventory, not runtime semantic coverage.

This aggregation changed documentation only. The parent task owns implementation, build and new native visual verification; append that evidence after the actual run.

## Native verification and text-path correction — 2026-10-18

The parent integration now has actual runtime evidence, separate from the
read-only audit above:

- `native-review-lifecycle01`: native Q1 token1 and Q3 token2 both bound layout7
  in slotB62A70, received CBS paint acknowledgements and released at native
  close/reuse. This first implementation removed the large ESPN board but
  composed225/227 still showed a small original 0:00 scene-text node. That
  incomplete result was not accepted as the finished replacement.
- Added three generator-maintained draw hooks: text scope around
  BCE50→1513D0 (return1BD17B), then outgoing ARGB alpha filtering at
  150BF0→46B50 (return150C0D). Scope is nestable and thread-local; no node
  alpha, formatted text, native animation or completion state is overwritten.
- `native-review-lifecycle2x`: visible D3D11 window1920x1080, internal scale2
  confirmed by native BENCH/PRESENT output. Q1 layout7 token1 bound at
 746254437, painted at746254453, and released at746260593. Composed179/180/181
  show the CBS END1stQTR Eyebar with no original large ESPN board and no
  floating 0:00. Composed183 retains the period label during the game's own
  black camera transition. The subsequent original cameras and Q2 progression
  continue. The bounded test reached Q3 before it stopped; it did not capture
  the Q3 board after the final text change. Q3 ownership/reuse was verified in
  the earlier native run and both quarters share the tested text boundary.
- The two captured screenshots converted without image edits and provenance
  hashes are in `outputs/presentation-audit/native-proof-20261018/`.
- Release build `outputs/presentation-audit/native-build-text-scope-20261018.log`
  passed. Actual extracted C++ owner/text-scope tests passed368 assertions,
  including concurrent thread isolation. Browser period tests passed81 and
  existing CBS player/team lifecycle tests passed403 assertions/28cases.

The following kind0 quarter-stat table and kind28 halftime transition remain
separate, unsupported native artwork. The original automatic replay art is
also still visible; replay observations are diagnostic, not a safe replacement
claim. See the replay and sack audits for their exact remaining boundaries.

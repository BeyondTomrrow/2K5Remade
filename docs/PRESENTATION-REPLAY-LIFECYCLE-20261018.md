# Original replay lifecycle audit — 2026-10-18

Scope: owned retail `original/default.xbe`, current generated C, existing
presentation hooks, and already-recorded private CBS review logs. No game,
debugger, build or regeneration was run for this audit. The XBE was read only.
Addresses below are guest virtual addresses, not host function pointers.

## Result

The original automatic-replay screen has a real native state machine. It can
provide separate entrance, playback, return and completion observations. The
current presentation API does **not** yet receive this lifecycle: its
`replay_begin` producer matches popup title text containing `REPLAY`, while
`replay_end` exists as an enum/name adapter without a native producer.

This is a useful integration route, not proof that every original presentation
call has been mapped or that the existing modern wipe is correctly timed in a
running game. Names and integer-state changes need frame correlation before
being advertised as visually verified network transitions.

## Automatic replay screen and real runtime occurrence

Retail descriptor `0x004E3F38` has this event table at `0x004E3F10`:

| Message | Record | Handler / meaning established by code |
| --- | --- | --- |
| 3 | `0x004E3DF0` | type 1, `sub_000119B0` |
| 5 | `0x004E3EC8` | type 1, `sub_000119D0` |
| 6 | `0x004E3E38` | type 1, `sub_00013610` |
| 7 | `0x004E3E80` | type 3 command record; not a C callback |

`sub_000656E0` consumes request `[0x00B347E0]`. Its retail switch at
`0x0006578C` maps request 1 to `0x000656EF`, which sets `[B347E4]=1`
and pushes descriptor `4E3F38` through `sub_0006E390`. Request 2 maps to
`0x00065713`, clears `[B347E4]`, and pops the screen through `sub_0006E400`.
Requests 5 and 7 also pop but are generic request cases, not proof of a replay.

`outputs/cbs-2026/native-review-followup01/stderr.log` contains eight real
`[FSMOP] push ... desc=004E3F38` entries (lines 3019, 4034, 7125, 8028, 9256,
14058, 14445 and 15580 in the inspected file). This establishes that this
descriptor is used during a real CPU game. Those logs do not contain the
internal replay phases, wipe markers or reliable pop diagnostics; they do not
establish precise on-screen timings. The first inspected push occurred while
the match was Q1/PAT following Dallas's first touchdown.

Sources: `src/recomp/gen/recomp_0000.c` (`119B0`, `119D0`, `13610`),
`recomp_0002.c` (`656E0`, `657B0`), and read-only XBE descriptor decoding
using `tools/xbe_peek.py` with an explicit `original/default.xbe` path.

## Raw replay phases, including two different completion thresholds

`[0x00AFA078]` is the replay transition state. The transition record copied
into `0x00AFA07C` is 0x30 bytes. Its `+0x10` threshold is tested by
`sub_00057C20`, `+0x14` by `sub_00057C70`, `+0x18` is signed playback speed,
and `+0x1C` is current transition time. `sub_00057CB0` advances that time
and the attached animation. The threshold helpers invert their result for
negative playback speed. Do not assume fixed real-time millisecond constants.

| State transition | Exact guest store | Code consequences |
| --- | --- | --- |
| 0 → 1 | `116FB`, `sub_000116F0` | Starts only from zero. Obtains the entry transition with `54AC0(ecx=0)`, copies/initializes `AFA07C`, starts it with `57BF0`, then calls `55030` for its ambient script. State is written **before** this initialization. |
| 1 → 2 | `13669`, `sub_00013610` | After transition threshold `+10`, switches into replay data/camera: `5F460`, `5E5C0`, saves the live block via `5E820`/`110B0`, calls `60090`, sets `[AFA010]=1`, then `55070`. |
| 2 → 3 | `1372D`, same function | After transition threshold `+14`, calls `550F0` (releases transition ownership), prepares replay timing and enters ordinary playback. |
| 3 → 4 | `12FDE`, `sub_00012FD0` | Accepts an exit request **only** from state 3; other states return zero. Selects the return transition using `54AC0(ecx=1)`, initializes it, calls `54FC0` for the exit ambient script, returns one. |
| 4 → 5 | `13786`, `sub_00013610` | After threshold `+10`, restores saved live actor data (`60640`, `5E5C0`, `5E830` with `AF9BB0`), calls `54FF0`/`5B040`, clears `[AFA010]`. |
| 5 → 0 | `1380D`, same function | After threshold `+14`, calls `55010` transition cleanup, clears `[AF9B94]`, requests screen pop with `657B0(ecx=2)`. The actual pop is later in `656E0`. |

The threshold-10 actor-data switches are strong evidence for the cover/camera
handoff point. The threshold-14 transitions are separate animation-completion
boundaries. Capturing real frames alongside these observations is still
needed to establish exact visual cover and reveal instants.

`sub_000119B0` saves the current `27870` mode in `AFA0CC`, sets mode 2 through
`27880`, and tail-calls `116F0`. `119D0` restores that saved mode. Message 5
also runs when `6E390` suspends a current screen to push another, and when
`6E2E0` replaces a descriptor. **119D0 alone is not replay completion.**

## Request, natural end, button skip and menu diversion

* `sub_00011F50` calls readiness `11830`; on success it requests replay-screen
  push with `657B0(1)`. On failure it invokes `55120` cleanup and returns 2.
  This request is distinct from an actually pushed replay descriptor.
* The active-playback updater `sub_00013370` advances recorded playback. Once
  its playback-end condition is met, call `1342C → 12FD0` requests return.
* The screen updater `13610` polls the native input path at `13856 → 70A10`
  across up to eight controllers. Mask `0x110`, connected/ready checks
  `54F80` and `72190`, replay states 2/3 and `AFA118` affect admission. The
  normal accepted skip path reaches `13922 → 12FD0`, preserving its state-3
  guard and native cleanup. A button is not itself a completed replay.
* `sub_00013340` is another guarded exit wrapper; it calls `12FD0`, then
  `55150`/`5ABB0(4)` only on success. It is used by the surrounding
  presentation manager at `72705`.
* There is a separate **fast diversion to the pause menu** in `13610`:
  state 4 is written at `13964`, state 5 at `139C2`, restored data/cleanup
  run immediately, state zero at `13A24`, and `6E2E0` replaces the current
  descriptor with `4E9078`. Its retail descriptor title at `E62AEC` is
  `Paused`. Do not emit a normal broadcast return animation for this path.
* `sub_00013230` resets replay state to zero at `13309` as part of broader
  resource initialization/reset. That is a reset, not a replay-return event.
* `sub_00013040` sets state 3 directly after preparing replay mode. It is
  another mode/entry path, so a global watcher of all writes to state 3 would
  not by itself establish automatic entrance. Keep the automatic descriptor
  and lifecycle identity with each observation.

The exact full manual-replay/highlight lifecycle was not audited here. The
neighboring highlight callback `1124E0` is distinct and must not inherit all
automatic-replay events merely because it also calls the running update.

## Original wipe and player-name resources

`54AC0` selects and initializes a real 3D transition resource through
`sub_0008B3B0`. Its defaults are `logo_wipe` (`E60E78`) and, in a special
mode, `logo_coin_wipe` (`E60E58`). `8B3B0` builds the animation record using
the MRKS resource, first/last animation times, callback fields, and native
resource ownership. `550F0`/`55010` release ownership through `8B4F0`.

Entry `55030` dispatches the active transition's ambient name at
`[[B25DD0]+28]` via `38650 → 89DA0`, then `A27D0`. Return `54FC0` dispatches
`[[B25DD0]+2C]`, with fallback `E60E8C` (`Ambient: Replay Wipe Out`), then
`A27E0`. This is independent of English popup-title parsing.

The retail data table also contains these named resources (names identify
resources; they do not alone prove a particular visible direction):

| Data row | Ambient / scene |
| --- | --- |
| `A86960` | `Ambient: Replay ESPN Wipe Out`, `replay_wipe`, `cameraShape2` |
| `A86988` | same ambient, `fullscreen_WipeRedFlashy` |
| `A869B0` | same ambient, `fullscreen_WipeRedName`; callbacks `8B1E0`, `8B110` |
| `A869D8` | `Ambient: Replay Player Name`, `Ambient: Replay Wipe Out`, `logo_coin_wipe`; descriptor field `4EEA70`, value 4 |
| `A86A00` | `Ambient: Replay Wipe`, `Ambient: Replay Wipe Out`, `fullscreen_WipeElectricity` |
| `A86A28` | same ambient pair, `fullscreen_PlayerOfTheGame_transition` |

Do not disable these scripts or replay-screen handlers to remove ESPN art:
they own timing, state transitions and restoration. A future replacement
should suppress only owned draw output after a replacement acknowledges the
specific transition, leaving original updates/completion active. The exact
native-draw interception and player-name payload are **not** proven here.

## Small observation hooks for the next run

Use `tools/apply-gen-patches.py`; do not hand-edit generated C. The read-only
observer callback should log raw phase, guest PC, state/descriptor identity,
transition pointer, midpoint/end/speed/time when initialized, `AFA010`,
`AFA118`, and request `B347E0`. It must not call guest code, alter state or
delay the game thread. A bounded ring or one log per actual transition avoids
frame-timing noise. State 1 is a request before transition initialization.

Initial observations should not immediately emit broad `REPLAY_STARTED` or
`REPLAY_ENDED` aliases. First correlate natural completion, button skip,
pause diversion, match exit and screen suspension with actual composed D3D11
captures. Afterwards typed entrance/playback/return/completed events can
drive CBS/NBC/FOX separately. Ordinary completion and pause cancellation
must remain distinct.

No 119D0 completion hook is recommended. A descriptor-qualified **actual
pop** can be a cancellation fallback if its old descriptor is captured before
`6E400` and confirmed removed afterwards; a generic message-5 notification
is insufficient.

Seven diagnostic-only entries were added to `tools/apply-gen-patches.py` with
callback `nfl2k5_presentation_replay_observe(unsigned phase, uint32_t guest_pc)`.
They observe raw phases 1/2/3/4/5/0 at the six normal store sites above, plus
menu cancellation phase 7 at `13A24`. The normal state-5 hook intentionally uses the first
function-scoped matching store at `13786`; the later fast-diversion store is
not emitted as a normal return. No generated C was edited or patched by this
audit, and no runtime event aliases were added.

Validation: the Python file parses; every function/anchor exists in the
current generated tree; first-match blocks were checked; all seven PCs were
decoded from the owned retail XBE and are original `mov [AFA078], ...`
instructions. The inserted observer blocks contain no guest memory writes,
guest function calls or early returns. Build/link support and runtime capture
are separate integration work by the parent task, not a result of this audit.

## Runtime verification — private lifecycle01 capture, 2026-10-18

The parent task subsequently built and recorded
`outputs/cbs-2026/native-review-lifecycle01/`. This follow-up only inspected
the existing `stderr.log` and composed BMPs; it did not launch/control a
game, build, attach a debugger or modify production code. Only the first
three complete normal replay cycles were analyzed. The long later overtime
portion is outside this verification.

### Observed phase cadence and actual screen removal

Every inspected cycle reports **1 → 2 → 3 → 4 → 5 → 0** at the expected
guest PCs, followed by the new native `[FSMOP] pop` diagnostic returning
the top-level machine to `004E7EC0`. No phase-7 menu diversion occurred in
these three cycles. Times below are observed host milliseconds from
`GetTickCount64`; they are measurements, not hardcoded animation durations.

| Cycle | Phase 1 tick | Relative phase 2 | Relative phase 3 | Relative phase 4 | Relative phase 5 | Relative phase 0 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 736730015 | 1500 ms | 1656 ms | 6250 ms | 7750 ms | 7922 ms |
| 2 | 736928531 | 1484 ms | 1656 ms | 6406 ms | 7906 ms | 8078 ms |
| 3 | 737043218 | 1500 ms | 1828 ms | 6907 ms | 8422 ms | 8797 ms |

Cycle 1's preceding live-state log shows **Q2, 0:00**, with configured
20-second periods; its completion reports flow 9 (halftime). Cycle 2 is
**Q3, 0:00**, completing to flow 21/0x15 (period break). Cycle 3 is an
ordinary **Q4, 5.5 seconds remaining** replay, completing to flow 11/0x0B
(post-play). Thus the replay lifecycle is not confined to quarter breaks,
and a replay completion must not itself imply live football resumes or a
quarter graphic should play.

The entrance reaches its data-switch midpoint after approximately 1.5 s in
these samples. Its remainder lasts 156/172/328 ms; return reaches its
midpoint after 1500/1500/1515 ms and its remainder lasts 172/172/375 ms.
Playback holds differ (4594/4750/5079 ms). These measurements support the
two-boundary interpretation, but three samples do not establish universal
durations or a frame-accurate replacement timeline.

### Visual correlation from composed D3D11 captures

The `[PSHOT]` markers identify **approximately one capture per second**.
This sampling is too sparse to certify an exact animation start, coverage
frame or final reveal frame. It does, however, show actual original ESPN
wipe art during the mapped native transitions:

| Exact captured frame | Logged capture tick | Most recent phase | Visible content |
| --- | --- | --- | --- |
| `composed-301.bmp` | 736730937 | 1 | Bengals post-play close-up remains visible; this early entrance sample does not yet show a wipe. |
| `composed-302.bmp` | 736731937 | 3 | Replayed field action is visible, following the entrance camera/data switch. |
| `composed-307.bmp` | 736736937 | 4 | Stadium/crowd shot remains visible early in return. |
| `composed-308.bmp` | 736737937 | 0 marker has the same tick | Large translucent ESPN wordmark with gold/gray rays still fills the captured picture. The same-millisecond log ordering cannot determine whether the captured render frame preceded the completion callback. |
| `composed-309.bmp` | 736738937 | 0 | Referee/field shot after return; the full-screen wordmark has cleared. |
| `composed-499.bmp` | 736928968 | 1 | Crowd close-up early in entrance. |
| `composed-500.bmp` | 736929968 | 1 | Large translucent ESPN/gold wipe appears over the crowd, 47 ms before logged entrance midpoint. |
| `composed-501.bmp` | 736930968 | 3 | Replay camera follows the Dallas pass play. |
| `composed-507.bmp` | 736936984 | 0 | Referee shot with a remaining colored horizontal transition band. |
| `composed-508.bmp` | 736937984 | 0 | Referee shot after that band has cleared. |
| `composed-614.bmp` | 737044000 | 1 | Dallas bench close-up with a white/yellow horizontal bar already crossing the picture. |
| `composed-615.bmp` | 737045000 | 2 | Replay field camera is visible behind remaining horizontal white/yellow transition bars. |
| `composed-616.bmp` | 737046000 | 3 | Replayed field action, without those bars. |
| `composed-621.bmp` | 737051000 | 4 | Replay action remains visible during the early return interval. |
| `composed-622.bmp` | 737052000 | 5 | Live stadium view behind a tall red/white/black ESPN-style transition panel; capture is 15 ms before phase 0. |
| `composed-623.bmp` | 737053000 | 0 | Stadium/team-huddle view with the tall panel cleared. |

The original wipe remains rendered in this diagnostic build; this pass did
not replace or alpha-suppress it. The modern CBS Eyebar can also be seen in
several replay samples. Correct replacement therefore needs lifecycle
ownership for both original wipe art and competing modern inserts, rather
than a popup-title callback or a fixed delay after the play.

For review, bounded contact sheets and the exact log-to-frame ledger were
saved to `outputs/cbs-2026/replay-lifecycle-audit-20261018/`:
`cycle-1-contact.jpg`, `cycle-2-contact.jpg`, `cycle-3-contact.jpg` and
`alignment.json`. All contact cells are existing composed game captures,
with labels added for analysis; none synthesizes a new gameplay frame.

### Diagnostic-field corrections before the next run

**The zero timing words in lifecycle01 are not guest transition zeros.**
The inspected observer takes `rd32(AFA07C)` as a pointer, rejects the
`81xxxxxx` contiguous-memory alias, and prints zero fallback fields.
Furthermore, the source proves that `AFA07C` **itself** is the copied
transition record: its first word is the MRKS resource pointer, not a
pointer to another timing record. `8B3B0` builds the original at `A86900`;
entry/return copy 0x30 bytes to `AFA07C`, and `57CB0`/`57C20`/`57C70` are
called with `ecx=AFA07C`.

The next observer should read midpoint/end/speed/current directly at
`AFA08C`, `AFA090`, `AFA094`, `AFA098`, and log the MRKS alias separately.
Accepting a valid alias through `guest_va_ok` is necessary for actual
resource reads, but dereferencing resource+0x10 would still not read these
timing fields. Phase 1 precedes initialization and must continue to label
its timing values unavailable. The logged `clock=00B28680` is a raw guest
pointer (`[E6028C]`), not game-clock seconds; the nearby `[PRES]` state logs
were used for clock context above.

At the phase-2 callback `13669`, `replayActors=0` is expected: that hook is
immediately after the raw state store and **before** the same block writes
`AFA010=1`. Likewise phase 5 precedes restoration and can still log
`replayActors=1`. Phase 3 then reports 1 and phase 0 reports 0. This is not
evidence of a worker race or failed replay switch.

Next verification: read the correct timing-record fields, capture a short
native transition at higher frequency, and correlate the native midpoint
and completion with rendered coverage before enabling replacement events.
Natural completion, button skip, pause diversion and match exit still need
separate acceptance coverage; the first three normal cycles establish
none of the exceptional paths by themselves.

## Narrow original-wipe draw seam — read-only follow-up

This follow-up inspected current generated C and decoded the owned retail
instructions. No game/debugger attachment, production edit, build or native
draw suppression was performed. Runtime acceptance of a replacement remains
outstanding.

### Resource is not the scene

The pointer chain is now established directly by code:

1. `8B3B0` loads an MRKS resource through `449E0` with type `0x534B524D`,
   saves that resource at `A86900`, then calls `151530(resource)`.
2. `151530` returns **`[resource]`** (or zero for a null resource).
   `8B3B0` stores that returned scene pointer at `A86904`.
3. The 0x30-byte record is copied to `AFA07C`. Thus `[AFA07C]` is the
   MRKS wrapper, while `[AFA080]` is the actual scene used for rendering.
4. `57CE0` receives **the record address** in ECX. Its `+4` field goes to
   the scene preparation and render routines. The wrapper address itself
   must not be passed as a guessed scene.

| Transition-record offset | Native initialization/use |
| --- | --- |
| `+00` | MRKS resource pointer, from `449E0` |
| `+04` | `151530` result, the scene/animation object |
| `+08` | Named camera pointer from the selected table row |
| `+0C` | Animation start time from `2EC20` |
| `+10` | Midpoint threshold from the selected row (1.5 for six rows) |
| `+14` | Animation end time from `2EC70` |
| `+18` | Speed, initialized to 1 |
| `+1C` | Current animation time |
| `+20` | Per-draw callback copied from table `+10` |
| `+24` | Initialization callback copied from table `+14`, run by `57BF0` |
| `+28` | Entry ambient command name, table `+20` |
| `+2C` | Exit ambient command name, table `+24` |

### Strongest caller-qualified seam

The replay descriptor's message-7 record `4E3E80` has type 3 and callback
**`11C80` at record +0x0C**. This is the render-side route; its message-6
update handler is separately `13610`.

`11C80` reaches `119E0`, which saves/sets the camera matrix and switches on
`AFA078`. The retail table at `11A60` decodes as:

* state 1 → `11A48`;
* state 2 → `11A48`;
* state 3 → `11A5B` (skip transition draw);
* state 4 → `11A48`;
* state 5 → `11A48`.

At `11A48`, ECX is set to **AFA07C**. Guest call **`11A4D → 57CE0`**
draws this original replay transition; continuation **`11A52`** restores
the saved matrix with `2AC80` before returning. The generated exact call
anchor is:

```
PUSH32(esp, 0x00011A52u); RECOMP_ABI_CALL(0x00057CE0u, sub_00057CE0); /* call 0x00057CE0 */
```

This **caller-qualified** boundary is narrower than disabling `57CE0`
globally. There is another real caller `FB41D`, which renders a different
record at **BA2BF4**, controlled by BA2BF0. An unconditional generic change
would affect that independent transition.

### What the draw wrapper includes

`57CE0` has three distinct visual portions, so hiding just one mesh call
would leave original artwork behind:

1. **Background/full-picture primitive**: `57D1B..57D7D` configures the
   material at B289E0 through `2D2A0`, emits four `(-1/+1,-1/+1,-2)`
   vertices through `2CB50`, and flushes with `2CA00 → 2C980`. The latter
   writes an inline-vertex command with method `0x1818` and a terminating
   `0x17FC=0` to the guest pushbuffer. This is actual draw emission, not
   merely metadata.
2. **Animated wipe scene**: `57DBC → 2DE30` prepares the scene's camera
   and lights; `57DCD → 2DE60` iterates scene `+0x24` objects from its
   `+0x28` array in 0x60-byte steps. Each reaches `21860 → 243D0` for mesh
   drawing. `2DE30` itself calls `2DD30` to set up lights and `2DCA0` to
   resolve the selected camera into the temporary view matrix.
3. **Optional player/team name artwork**: `57DD9` calls record `+0x20`
   when nonzero. In the seven retail wipe rows, its only nonzero target
   is `8B1E0` (used by `logo_wipe` and `fullscreen_WipeRedName`). It
   updates text and draws auxiliary team-chip material; hiding only
   `2DE60` would not suppress this branch.

`8B1E0` gets the selected player from `55470 → [B25E24]`. It iterates the
MRKS wrapper's text records (`151580` count, `151570` record address),
uses label CRCs `6AD72F8F` and `9765E63B` to select the player's `+0x10`
and `+0x14` name strings, and passes those through `1513D0` into the text
layout/cache machinery. It separately iterates auxiliary records via
`151620`/`151610`, derives their transform with `8AFD0`, and calls
`1C12E0` for team-chip geometry. That latter routine performs material/
color/texture setup and mesh drawing (`1C14F6 → 21860`).

These are strong source connections between the original wipe, player
name and team-chip art. The inspected code does not independently establish
which text CRC is first name versus last name, nor which sack or other
football event selected B25E24; that belongs to the separate post-play
command audit. It does not justify inventing a sack celebration camera.

### Correction: actual native wipe table row boundaries

The earlier table in this document listed addresses of **ambient-name
fields**, not row starts. `8B300` proves lookup rows begin at A86940 and
advance by **0x28** through A86A30. The following supersedes those earlier
row-address labels:

| Actual row start | Scene name | Per-draw callback | Initialization callback | Entry / exit ambient |
| --- | --- | --- | --- | --- |
| `A86940` | `logo_wipe` | `8B1E0` | 0 | Replay ESPN Wipe Out / same |
| `A86968` | `replay_wipe` | 0 | 0 | Replay ESPN Wipe Out / same |
| `A86990` | `fullscreen_WipeRedFlashy` | 0 | 0 | Replay ESPN Wipe Out / same |
| `A869B8` | `fullscreen_WipeRedName` | `8B1E0` | `8B110` | Replay Player Name / Replay Wipe Out |
| `A869E0` | `logo_coin_wipe` | 0 | 0 | Replay Wipe / Replay Wipe Out |
| `A86A08` | `fullscreen_WipeElectricity` | 0 | 0 | Replay Wipe / Replay Wipe Out |
| `A86A30` | `fullscreen_PlayerOfTheGame_transition` | 0 | 0 | none / none |

Every ambient name above has the `Ambient: ` prefix in the XBE. A869E0 also
contains descriptor 4EEA70 and value 4 at row +18/+1C. These are preserved
data, not permission to execute a transition from an arbitrary host thread.

### Recommended guarded experiment and remaining limits

The smallest candidate experiment is to guard only the **11A4D draw call**
for this exact replay record and an acknowledged replacement owner, then
continue through **11A52**. Such a guard belongs in
`tools/apply-gen-patches.py`, with a bridge-side read-only ownership query;
it must never alter AFA078 or fake completion. It is a recommendation for a
controlled experiment, **not an implemented or runtime-proven fix**.

The ownership predicate should require the current match, active replay
generation/direction, a canonical native token, successful replacement
paint acknowledgement, and the same record/scene/resource identity.
Accept valid contiguous aliases; do not hardcode one observed `81xxxxxx`
resource address. Without this exact proof, draw the original. Release the
replacement on matching close, pause diversion, match exit, page failure
or owner change. A late acknowledgement must not affect a later transition.

This leaves **57CB0** advancement, **57C20/57C70** boundary checks,
**57BF0** initialization, entry/exit ambient scripts, all actor snapshot/
restore calls, the replay update, and eventual screen pop untouched. The
caller also still restores its outer matrix. The optional 8B1E0 callback
contains text-cache updates as well as drawing, so a first guarded trial
must specifically verify that temporarily omitting that visual wrapper
does not create stale cached labels when the original fallback returns.
Do not describe the entire transitive draw helper tree as side-effect-free.

The evidence does **not** yet prove:

* that this seam removes every independently queued ESPN ambient/UI draw;
* that no resource variant uses another callback or deferred draw path;
* the exact first/last frame at which a replacement can safely release
  cover relative to asynchronously composed native frames;
* that menu/manual-highlight transitions should share this replay token;
* the behavior of fallback after a failed or canceled modern wipe.

Next read-only/controlled verification should count this exact caller and
capture its record/resource/scene identities alongside a short high-rate
native recording. A later acknowledged replacement experiment should
compare native phase cadence and actor restoration against the baseline,
verify that original art disappears only inside the owned interval, and
exercise early skip, normal end, pause and page-failure fallback. No global
pushbuffer suppression, generic `2DE60` bypass, scene-pointer guessing or
guest-timer-thread execution is warranted by this audit.

## Post-initialization event boundary and caption side effects

This section is another source/XBE-only follow-up. No native hook, event,
draw suppression or game control was implemented here.

### Exact initialization-complete anchors

The raw phase-1 and phase-4 stores happen **before** the new transition is
copied. They should remain diagnostic observations. An actionable typed
transition event can instead be emitted immediately after `57BF0` returns:

| Direction | Function | Exact function-scoped anchor; insert **after this call line** |
| --- | --- | --- |
| Entrance, copied resource branch | `sub_000116F0` | `PUSH32(esp, 0x0001173Bu); RECOMP_ABI_CALL(0x00057BF0u, sub_00057BF0); /* call 0x00057BF0 */` |
| Entrance, initialized default/empty branch | same | `PUSH32(esp, 0x00011754u); RECOMP_ABI_CALL(0x00057BF0u, sub_00057BF0); /* call 0x00057BF0 */` |
| Accepted return, both branches | `sub_00012FD0` | `PUSH32(esp, 0x00013027u); RECOMP_ABI_CALL(0x00057BF0u, sub_00057BF0); /* call 0x00057BF0 */` |

The second entrance anchor must be the **call line, not the `11754`
label**. A nonzero replay state at the initial `116F0` test jumps directly
to `loc_00011754`; that is a resume/re-entry path that has not initialized
a new record. Inserting between the preceding call and the label records
only the newly initialized default branch and is skipped by that jump.

The copied entrance branch goes through `1173B → 55030`; the default
entrance branch through `11754 → 55030`. The common return continuation
`13027` calls `54FC0`, then `12E80`, and returns 1. Therefore these hooks
also precede the direction's ambient-script dispatch without bypassing it.

At each proposed anchor:

* the full record at AFA07C has either been copied or initialized by 57BC0;
* 57BF0 has set current time `+1C` from start time `+0C`;
* the underlying animation has received that start time through 2F010
  when a scene exists;
* the initialization callback at `+24`, including 8B110 for the red-name
  variant, has returned;
* midpoint, end, speed and current can be read at the **record's own**
  +10/+14/+18/+1C, not through its first resource word.

Initialization does not guarantee an available resource: 57BC0 explicitly
initializes a valid empty record with a null scene. The event bridge should
distinguish `initialized` from `hasNativeScene`, check finite float fields
and the observed raw direction (1 or 4), and accept valid alias pointers
for a non-null resource/scene. Do not declare an asset loaded merely
because the initializer returned.

### Timing metadata versus a host deadline

Useful typed payload facts are: match identity, replay generation/token,
direction, record address, MRKS identity, scene identity, camera identity,
animation start/current/midpoint/end values, signed speed, and observation
tick. This gives a presentation package a measured native handoff target
without leaking arbitrary guest pointers into executable page behavior.
The host can keep pointer identity private and expose an opaque token.

For a nonzero speed, `(midpoint-current)/speed` is a remaining interval in
the animation's native time domain. It is **not automatically a Windows
wall-clock deadline**: 57CB0 advances with the FSM delta, including native
pause and pacing. The native capture's roughly 1.5 s lead-in does not
justify assuming every match or game-speed setting uses that wall time.

A separate `desiredVisualStart` may be derived from that native interval
and the inspected CBS/FOX/NBC animation's own coverage point, but it must
not replace authoritative native camera boundaries. Without ongoing
native-time synchronization, the conservative behavior is to hold the
replacement covered until the actual handoff and then reveal. That can
extend a hold beyond the reference; it is compatibility scaffolding, not
pixel/frame-identical broadcast motion.

The existing raw phase-2 hook occurs before actor switching. If a later
typed event is named `REPLAY_CAMERA_READY`, observe it only after the
same block's switch work and `[AFA010]=1` write have finished (the
`136DF` continuation after `55070` is a candidate). Similarly, a typed
`LIVE_CAMERA_RESTORED` should follow the normal return block's restoration
and `[AFA010]=0` write, rather than the earlier phase-5 store. Even then,
the new camera's **composed frame** can lag a guest callback: do not
release a cover solely from a host notification if the old guest frame
is still queued for presentation.

### What omitting the name callback actually omits

The per-draw `8B1E0` callback is not the 57BF0 initialization callback.
Its visible call graph is:

```
8B1E0
  55470 -> [B25E24]                         selected player
  151580 / 151570                         enumerate text records
  1513D0                                 immediate text rendering
    1513B0 -> 150E30 / 150BF0              font transform/style state
    150CC0 -> 30AE0 / 30F20 / 150C80       bounded text copy/case
    150F20                                layout and font draw calls
  151620 / 151610                         enumerate auxiliary records
  8AFD0                                  transform assembly
  1C12E0                                 team-chip material and mesh draws
```

More specifically, `30AE0` is a bounded UTF-16 copy into AA28F0 (limit
0x3FF); `30F20` uppercases ASCII a–z in that scratch string. `1513B0`
copies/configures the text transform at AA2830 and font style around
AA2870. `150F20` computes clipping/alignment/repetition, can truncate the
scratch string, and calls ordinary font/numeric draw paths including
46CB0, F2080 and F1D50. These are real immediate draws, not merely a
player-name notification. `1C12E0` changes original team-chip materials,
colors and textures as well as rendering them.

No direct write to AFA078, AFA010, the midpoint/end/current values,
pending screen-pop request B347E0, or actor snapshot/restore block occurs
in 8B1E0. Its return value is ignored by 57CE0. No replay cleanup call is
located in this callback; 550F0/55010 and the separate update FSM retain
that responsibility. Thus omitting the 11A4D visual wrapper does not
omit those **established** replay completion/cleanup operations.

It does omit shared font scratch/style updates, caption draws and
team-chip material changes. The inspected native path prepares them
again on every visible invocation; no per-replay completion latch or
mandatory one-time caption formatter was identified. However, this is
not an exhaustive proof that every transitive font/mesh resource helper
is free of cache uploads or lazy initialization. A first fallback frame
could incur work otherwise warmed while the original was hidden. Keep
that distinction explicit rather than claiming the complete helper tree
has no side effects or that cache behavior is runtime-proven.

### Minimal safe ownership/skip/cancel recommendation

1. Observe raw diagnostic stores as before; mint typed entrance/return
   ownership only at the initialization-complete anchors above. Pair it
   with the current match and automatic replay descriptor 4E3F38, not a
   title string or any generic transition.
2. Start with original draws enabled. Bind a page's paint acknowledgement
   to the current token, direction and private record/resource/scene
   identity. Reject an old token, changed scene, unrelated record,
   canceled owner, invalid/paused/menu context or failed page.
3. Only an acknowledged owner may guard the exact 11A4D draw call; keep
   11A52 matrix restoration and every independent native update/init/
   ambient/actor-restore/completion call. Do not globally disable 57CE0,
   2DE60, font draws or MRKS resources.
4. Phase 7 is cancellation into the pause menu. Actual removal of the
   replay screen, match exit/change, invalid native state or page failure
   must release the owner and re-enable original fallback. Message 5
   alone remains a suspension signal, not a successful return.
5. A skip accepted by 12FD0 creates a **return** owner using the initialized
   return record. An input bit before native acceptance, a refused call
   outside state 3, or the fast pause diversion must not synthesize a
   normal return animation. A late entrance acknowledgement must never
   suppress the return art under a different token.

These are source-supported recommendations only. Native acceptance still
needs an acknowledged draw trial with high-rate composed frames, retained
phase cadence, correct original fallback labels and resource cleanup, and
separate normal-end, skip, pause, match-exit and page-failure cases.

## Hook definitions added after the read-only audit

At the parent task's explicit implementation request, four entries were
added to `tools/apply-gen-patches.py`:

* `PRES_REPLAY_READY_ENTRY_COPY`: after the 57BF0 call returning to 1173B,
  calls `nfl2k5_presentation_replay_ready(0u, 0x0001173Bu)`.
* `PRES_REPLAY_READY_ENTRY_DEFAULT`: after the 57BF0 call returning to
  11754, calls the same helper with direction 0 and PC 11754. It remains
  **before** the shared label, so the resume/re-entry jump skips it.
* `PRES_REPLAY_READY_RETURN`: after the 57BF0 call returning to 13027,
  calls that helper with direction 1 and PC 13027.
* `PRES_SACK_OBSERVE_DECISION`: after the exact B9754 → A1A00 call,
  calls `nfl2k5_presentation_sack_observe(ebx, esi, 0x000B9754u)`.
  This is before shared label B9759, which multiple rejected/non-sack
  branches jump to directly. It preserves all original sack handling.

The sack helper observes the two original actor registers saved by
`sub_000B96B0` (`ebx=entry ecx`, `esi=entry edx`). No actor identity is
invented in the patch, and the existing title call runs first. Source
event validation and publication remain responsibilities of the parent
bridge implementation.

Validation without applying patches: Python AST syntax passed; all four
exact anchors occur once in their intended function; each injected block
lies after the call but before its continuation label; original retail
Capstone decoding confirms calls 11736/1174F/13022 → 57BF0 and B9754 →
A1A00 with continuations 1173B/11754/13027/B9759. The inserted blocks
contain no guest memory writes, guest calls, skipped original calls or
early returns. No generated file, bridge API implementation or header was
changed, and no build/game/debugger operation was performed by this task.

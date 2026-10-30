# Native presentation follow-up audit — October 19, 2026

## ESPN SKY CAM: independent replay-angle overlay

Read-only source/XBE/log inspection; no native build, game control, debugger,
production edit or draw-suppression trial was performed for this section.
The runtime evidence belongs to the existing `native-review-replay04` capture.

### Identity and observed lifecycle

The retail XBE contains UTF-16LE **`skycam`** at guest `0x00E86400`.
Its resource catalog entry is **`0x00AAD984`**, calculated from catalog base
`0x00AAD180 + 19 * 0x6C`. Entry `+0` points to `skycam`; `+4` points to
UTF-16 `OVERLAY` at `0x00E861A8`. This is layout **19 / `0x13`**.
`sub_001C1530` initializes the 32-entry catalog through `sub_001BC880`,
which stores its base at `0x00BE5214`.

Existing log `outputs/cbs-2026/native-review-replay04/stderr.log` proves:

| Line | Observation |
|---:|---|
| 4965 | Automatic replay enters raw phase 3 at host tick 795173046. |
| 4971 | Script opcode 133 starts `sub_002604D0`. |
| 4972 | `[CARD] ctx=00B73C44 kind=19`. |
| 4974 | `[POPUPOP] show obj=00B73C44 slot=1 t=-1 '' ''`. |
| 5057 | Replay OUT reaches its camera handoff at tick 795178921. |
| 5058 | Slot 1 closes, caller continuation `0008A46A`. |

`nfl2k5_playercard_probe` in `src/main.c` prints `kind` from **`ctx+0x48`**,
the same layout index used by the generic overlay catalog. It is not a
separate football/stat-card enum. Thus this capture actually queued the
catalog's `skycam` layout during replay. The existing
`replay-001-contact.png` visibly shows the ESPN SKY CAM mark at the upper
left during two replay angles, separately from the CBS transition artwork.

`sub_0008ACF0` copies the source object into the accepted popup slot. Slot 1
is `0x00B61B50 + 0xF20 = 0x00B62A70`; the reusable builder object
`0x00B73C44` must not be treated as the persistent draw owner.
`sub_001BCC30` resolves `catalog[layout]` via `sub_000449E0` with type
`0x534B524D` (MRKS), storing the resource at layout-object `+0x194`, hence
popup `+0x1DC`. `sub_001BC3F0` obtains its scene through `sub_00151530`.

### Candidate draw-only replacement seam

Use the existing **exact active popup** ownership/paint-acknowledgement
mechanism for verified layout 19, rather than disabling the replay renderer
or globally suppressing scenes/fonts. Bind the accepted slot, current match,
package and page generation, layout, and loaded resource/scene identity;
retain original art until the replacement has visibly painted. A CBS corner
mark's lifetime is a separate package concern, not proof that the native
SKY CAM popup has an acknowledged replacement.

The existing outgoing-alpha/text hooks already cover the generic path:

| Exact call site / continuation | Draw seam |
|---|---|
| `sub_0008A170`: call `001BD5A0` → `0008A235` | Table alpha at `ESP+16`, before the guest return address is pushed. |
| `sub_0008A170`: call `001BCE50` → `0008A269` | Layout alpha at `ESP+8`, before the guest return address is pushed. |
| `sub_001BCE50`: call `001BC200` → `001BCEC4` | Effective layout alpha, preventing a layout fade callback from restoring visibility. |
| `sub_001BCE50`: indirect calls → `001BD230` / `001BD2F9` | Dynamic art/texture outgoing alpha, retaining callbacks and node animation. |
| `sub_001BCE50`: call `001513D0` → `001BD17B` | Scoped scene-text color filtering through the existing `00150BF0` → `00046B50` seam. |

Keep the popup update pass, first-draw latch, resource loading, scene/node
updates, camera setup/restoration, close callbacks and native replay actor
switches intact. Do not close the popup early, shorten its timeout, edit its
stored alpha, or clear its resource pointer. Release the replacement on its
actual slot close/reuse, invalid match, package/page change or lost paint
ownership; original art remains the fallback. The brief wipe owner ends on
entry to replay phase 3, so that owner alone cannot cover this later popup.

### Distinction and remaining proof

`sub_00011C20` separately draws scene `MEM32(0x00AFA108)` from replay callback
`sub_00011C80`, gated by raw replay phases 3/4 and native eligibility. It sets
camera `0x00AF95B0`, calls `sub_0002DE60` at `00011C64` → `00011C69`, then
restores camera state. This audit found no explicit writer/load linking
`AFA108` to the `skycam` resource. Its reads, blanket replay-state reset and
scene/node update path are not sufficient evidence for that link. Do **not**
label `11C20` the SKY CAM draw or suppress it on that assumption.

The catalog, queued popup and visible mark are established; exact per-frame
resource identity and a successful guarded replacement/fallback run remain
unverified. No claim is made that suppressing layout 19 removes every replay
annotation, or that a CBS equivalent of a SKY CAM label has been reference-
measured. This is an independent overlay gap, not a failure of the completed
six-direction replay-wipe handshakes in the same capture.

## Ordinary quarter-stat table: source identity and a request/acceptance caveat

The ordinary table following the large end-period board is **not layout 7**.
Existing captures show a request with layout 0, title **After the 1st Quarter**
or **After the 3rd Quarter**, two team subjects, subtype 1, three columns and
six rows. This is a generic serialized native stats table, not an `endofqrt`
scene and not the similarly named halftime transition.

There is an important proof limit: existing `[CARD]`, `[SNF-CARD]` and
`[POPUPOP] show` logs run at **8ACF0 entry** before automatic layout selection,
busy-slot rejection, copying and activation. They prove a request's contents,
not that it became a displayed instance. `8ACF0` returns `EAX=-1` if the
requested slot is already active. Do not suppress original art, emit a
native-visible replacement, or acquire ownership from this entry log alone.

In particular, replay02's quarter-table request follows an endofqrt close
sequence closely. `frames/composed-123.bmp` after that request shows live
gameplay and the CBS Eyebar, without the table. Earlier clean01 captures
174/175 still show prior endofqrt art. Those observations do not establish
a rendered quarter-stat instance; a new post-acceptance/actual-slot capture
is needed before replacing it.

### Concrete prior-run serialized data

`outputs/cbs-2026/native-review-replay02/stderr.log`, lines 5594–5629:

```text
script: 11, 130, 135, 142, 142, 143, 143, 143, 143, 139, 143, 143, 139, 140, 133
ctx=B73C44 kind=0 subtype=1 firstSubjectType=2 columns=3 rows=6 cells=18
title='After the 1st Quarter' requestedSlot=1 timeout=10
```

The 18 row-major cells are:

| Label | First native subject | Second native subject |
|---|---:|---:|
| First Downs | 2 | 0 |
| Passing Yards | 50 | 0 |
| Rushing Yards | 4 | 5 |
| Total Yards | 54 | 5 |
| Possessions | 1 | 1 |
| Time of Poss. | 1:11 | 3:49 |

The next script command changes period 1 to 2 at tick `780512250`; subsequent
QUARTER_ENDED/STARTED events describe that game-state change. They do not
prove that this independent popup was accepted. The native title has already
formatted the table values; preserve its labels, subject order and strings
instead of rebuilding a period scope from guessed globals. A title saying
After the 3rd Quarter also does not prove that its values are Q3-only deltas.

Other existing request evidence:

- clean01 line 3019: After the 1st Quarter, slot 1, timeout 10; period change
  at line 3020.
- clean01 line 5133: After the 3rd Quarter, slot 1, timeout 10; period change
  at line 5139.
- period01 has **Through Regulation** before overtime; that title must not
  become FINAL merely because the fourth-quarter clock expired.

### Source and exact serializer

| Function | Behavior established by generated C and/or owned XBE bytes |
|---|---|
| `DCFB0` / opcode 130 | Resets reusable request B73C44 through 8A270; requests slot 1. |
| `DD200` / opcode 135 | Sets request title. |
| `DCFD0` / opcode 142 | Adds a player/team subject to the request. |
| `DD060` / opcode 143 | Adds the requested native stat row. |
| `DD1A0` / opcode 139 | Adds the script-supplied row. |
| `DD2B0` / opcode 140 | Sets native request timeout. |
| `2604D0 -> DD290` / opcode 133 | Performs layout-specific preparation, then requests show through 8ACF0. |
| `25F1B0` | Adds a label/value row for all current subjects; columns = subject count + 1, rows increments. |
| `25F8A0` | Initializes table dimensions and cell count. |
| `25F8B0` | Selects row-major cell index `columns*row+column`, stride 88 hex. |
| `1BEE40` | Reads a cell descriptor, supporting type 0 inline UTF-16 or type 1 UTF-16 pointer. |
| `1BEF40` | Initializes the table's 24 cell descriptors. |

Source files are `src/recomp/gen/recomp_0006.c`, `recomp_0017.c` and
`recomp_0013.c`. Guest addresses are stable identifiers; generated line
numbers can move after normal anchored patch application.

For subtype 1, actual-slot fields are:

```text
B+40 subject count
B+10/+14, B+18/+1C, ... pairs of subject type/value (1 player, 2 team)
B+228 = 1
B+22C column count
B+230 row count
B+234 + i*88 cell descriptor
    descriptor+0 type: 0 inline text, 1 text pointer
    descriptor+4 style/value
    descriptor+8 inline UTF-16 text or pointer
B+EF4 cell count (bounded by 24)
```

`25F1B0` selects player formatting `25ED80` for type 1 or team formatting
`25EE30` for type 2, then appends the actual formatted text. Preserve this
binding; a roster object carried in `+14` is not itself a QB identity just
because a generic diagnostic names the field `player`.

### Why layout 0 is not a universal quarter-stat identifier

Catalog descriptor 0 at **AAD180** is zero-filled in the original image.
When a request has layout 0, `1C16B0` can choose another layout:

- no subjects -> layout 10;
- one player subject with subtype 1 -> layout 1;
- other one-subject table -> layout 3 for at most three rows, otherwise 4;
- multiple subjects can retain layout 0.

Many non-period requests initially have kind 0. A conservative period-table
owner therefore needs the successful returned slot, exact copied layout,
team-subject pair/order, subtype/dimensions/cells/title, current match and
page/package generation. A source-specific show caller or a pending script
snapshot can be correlated with the accepted slot; the request buffer itself
must not become the owner.

The exact script archive byte offset/sequence name supplying the After
Quarter title was not captured in this audit. It is not invented here.

### Draw-only seam, native timing, and cleanup

`8A170` draws the accepted slot in ESI. It checks native resource readiness
and first-draw eligibility, sets `+F1C`, obtains bounds, draws the table
through **1BD5A0** (call 8A230, return 8A235), then draws the layout through
**1BCE50** (return 8A269), retaining optional native callbacks.

For subtype 1, `1BD5A0` examines descriptor `+0C` via `1BC8D0`: zero selects
the generic table renderer **1BF020**, nonzero selects scene-local **1BF620**.
Layout 0 uses the generic path. `1BF020` reads `+22C/+230`, consumes its
incoming float alpha at `[ebp+18]`, and multiplies both prepared text alpha
(e.g. 1BF105 -> 1BF12F/46B50) and cell quad alpha (1BF3DE -> 1BEBA0). The
existing `SNF_POPUP_TABLE_ALPHA` hook before return 8A235 can therefore hide
only this owned table by changing its outgoing alpha argument to zero.
Do not skip the whole renderer or change stored rows/alpha/timers.

The original request timeout of 10 is decremented by the native popup loop
`8A840`. `8A340` marks closing, runs the close animation, releases the
layout, clears active `+4`, and calls optional `+F14`. Keep all of that code.
Hide the replacement when native close starts, retain acknowledged native
suppression through the original close animation, and release on actual
inactive/reuse or owner invalidation. Independent scene/art/texture/text
alpha hooks are listed in the SKY CAM section above. A successful request
alone is insufficient to activate them.

The bounded independently decoded draw transcript is
`outputs/presentation-audit/period-stats-20261019/owned-xbe-disassembly.txt`.

## Halftime and final popup families: shared scenes are not game events

The same catalog/slot mechanism is used by these layouts:

| Layout | Descriptor | Original resource | Family |
|---:|---:|---|---|
| 7 | AAD474 | endofqrt | OVERLAY |
| 9 | AAD54C | halftime_team_matchup | HALFTIME |
| 15 | AAD7D4 | halftime_gametrack | HALFTIME |
| 27 | AADCE4 | halftime_team_stats | HALFTIME |
| 28 | AADD50 | halftime_player_transition | HALFTIME |
| 29 | AADDBC | halftime_player_ticker | HALFTIME |
| 30 | AADE28 | gamesound_digital | OVERLAY |

`native-review-clean01` has Q2 endofqrt at line 4077, then layout 15 in slot
0 (4172), layout 27 Team Comparison in slot 1 (4174), native closes (4256,
4299), layouts 28/29 (4301/4321), layout 9 (4373), layout 30 (4396), then
Q2 -> Q3 at line 4428. Layouts 15/27/28/29 are also reused **after the actual
GAME_ENDED event** at line 5806 (Team Comparison again at 6046). Their
`halftime_` asset names do not classify the actual presentation as halftime.

`1C0A90`, one shared text callback, calls `DAD10` to read **B72E84**. While
that flag is set it formats FINAL when the title period is at least 4 and
HALFTIME otherwise. With the flag clear it instead indexes **AAD168** using
popup `+F18`, yielding Season/Last Season/Regular Season/Postseason/Career
scope text. Thus **+F18 is not an ending-quarter number**. Other text
callbacks and scene-specific art populate these families independently.

`DA5D0` sets B72E84 during its scene/load sequence and `DA444` / `DAAE0`
clear it on other transitions. This is additional native context, not a
standalone universal halftime/final event. A future owner must snapshot the
actual scene/period/flow and payload of the accepted instance rather than
guessing from one flag or a resource filename. Layout 15 also has a
descriptor `+3C` initializer callback array `50D86C`; it must continue.

The full loaded node inventory, per-scene dynamic cell mapping, exact
presentation-script selection and accepted-instance payloads for every
halftime/final scene have not yet been captured. This audit identifies their
family and conservative draw seams, not a completed replacement schema.

## What inspected CBS material actually supports

The independent reference check used existing local source clips, saved
frame measurements and package files, without a new download or game run.
The following limits matter before calling a replacement source-accurate:

- Regular-game Q1 source `4gxq8Na22nA`, roughly 1321.5–1325 seconds, proves
  CBS SPORTS / END 1ST QTR Eyebar behavior. HD evidence is
  `outputs/cbs-2026/reference/regular-20261018/end-q1-1323.2-hd.png`.
  The following **THE REMATCH** screen is a matchup promo, not a universal
  end-quarter bumper or a statistical board.
- Thanksgiving source `PLR04ljZ5DI`, 1434.6–1436.5, shows a holiday
  pumpkin/leaves matchup board; it is not evidence for a regular-game board.
- Halftime source `goBwy8oRNBk` includes the red/blue eye exit and a NE 7 /
  DEN 7 HALFTIME scoreline. The inspected end of that clip has **no
  statistical rows**. A prior description calling it a stats table should
  not be used to invent rows/layout.
- Source `kapv4nxG0eI`, roughly 854–858 seconds, proves the Eyebar FINAL
  state, not a complete postgame stats scene.
- Thanksgiving `PLR04ljZ5DI`, 1198.80–1203.13, does show a compact
  RUSHING/PASSING/TOTAL YARDS team table. Existing `stats-team.js/css`
  reconstructs that insert. It is suitable evidence for that specific
  compact graphic, not every ordinary quarter or halftime board.
- Its later TOTAL YARDS / 1ST QTR vs SINCE graphic is a scope-comparison
  insert; it must not be silently substituted for a period-boundary board.
- The 307.8–323.05 WALKOFF WINS / WEEK 12 GAMES comparison is a full-screen
  feature. `stats-large` can render comparisons but lacks that original
  stadium plate, depth and player cutouts; reusing it for quarter stats
  would be an adaptation, not a measured replica.

Current CBS `stats-team.js` accepts `TEAM_STAT` with `scope: this_game` and
yardage/defense/possession categories. It has no native quarter/half table
payload interface. The current Gamecast collector uses scope 0; native
accessor quarter scopes 1–4 and half scopes 6–7 must be verified per stat
descriptor before exposing them. Existing `period-board.js` owns only the
verified Q1/Q3 END Eyebar token; it has no statistical rows.

Available CBS package assets include team logos, branding SVGs and substitute
fonts. No independently measured ordinary quarter-stat, halftime-stat or
final-stat board asset/template was found in this reference pass. Unsupported
motion remains explicitly reference-needed. Preserve original fallback until
both a real accepted native instance and a verified replacement are present;
do not erase original ESPN art merely because another package is selected.

## Recommended next bounded measurement

1. Observe the actual return slot/result of the B73C44 script show, retaining
   a pre-call title/subject/table snapshot only for correlation.
2. For a real successful quarter/halftime/final instance, capture actual
   slot/layout/resource, native title/cells, first-draw latch, closing state,
   current period/flow, and final callback. A failed request remains a failed
   request.
3. Capture the same type of statistical screen in an unedited CBS source
   broadcast. Use that exact geometry/motion as the replacement specification.
4. Add typed events and per-instance paint ownership using only the narrow
   existing draw-alpha/text seams. Keep all original sequence/update/close
   behavior, and test fallback/reuse independently.

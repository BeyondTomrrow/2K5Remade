# ESPN broadcast replacement inventory — October 20, 2026

Read-only source audit for the request to replace the coin toss and the other
ESPN broadcast graphics. No game launch, build, original-data change or production
edit was performed by this audit. Current runtime baseline is the verified CBS
0.5.4 entry in PROJECT_STATUS.md. New CBS pregame files are being implemented
concurrently; their existence alone is not an in-game verification claim.

## What can be reused immediately

The existing native coin-toss bridge already publishes the actual title,
message, selected choice, confirm-button label and native close. NBC consumes
COIN_TOSS/COIN_TOSS_HIDE and PLAYER_FACT/PLAYER_FACT_HIDE in
pregame-inserts.js. CBS 0.5.4 does not declare replaces[pregame_cards], so its
original coin-toss panel remaining visible is expected. Adding a CBS consumer
and enabling that capability after paint/fallback verification is the shortest
path. Do not calculate the coin outcome or send HTML commands into football.

Layout 14 is named coin_toss but is also used by challenge, injury and penalty
constructors. A global layout-14 hide would erase unrelated messages and the
accept/decline control. Use exact accepted-instance ownership, as the review
owner already does, and preserve the original choices and callbacks.

## Complete native layout catalog

Decoded directly from the user-owned XBE, table AAD180, stride 0x6C, **32 entries**.
sub_001C1530 registers exactly 0x20 entries through sub_001BC880. These are layout
templates, not 32 unique football events. Several are reused across periods,
pregame and postgame. Status below is the CBS 0.5.4 baseline, before this session's
new pregame work; source identity is not proof an instance was accepted/painted.

| ID | Descriptor | Resource / group | CBS baseline and required next binding |
|---:|---|---|---|
| 0 | AAD180 | Zero-filled generic table descriptor | Unowned native tables, including requested After the 1st/3rd Quarter tables. Preserve dimensions/cells and the successful result. |
| 1 | AAD1EC | 1-p_flip-chip_stats-few / OVERLAY | Only supported consecutive-incompletions QB owner is acknowledged by CBS. Arbitrary career/season/prose facts need the exact PLAYER_FACT consumer; current live-stat deltas are a separate policy. |
| 2 | AAD258 | 1-pfullsc_details / OVERLAY | Full-screen player detail/chip, unowned. Blank title does not identify a sack. |
| 3 | AAD2C4 | 1-t_few-stats / OVERLAY | Drive Summary data is serialized, but that does not establish native artwork ownership. Other team tables remain unowned. |
| 4 | AAD330 | 1-t_many-stats / OVERLAY | Unowned larger team-stat scene/table. |
| 5 | AAD39C | 2-p_same-t_stats_few / OVERLAY | Unowned exact two-player subject/table instance. Browser comparison support alone is insufficient. |
| 6 | AAD408 | 2-p_oppo-t_stats_few / OVERLAY | Unowned exact opposing-player subject/table instance. |
| 7 | AAD474 | endofqrt / OVERLAY | Q1/Q3 END Eyebar ownership verified. Q2/final and subsequent tables remain separate. |
| 8 | AAD4E0 | splitscreen_intro / OVERLAY | Unowned split-screen broadcast introduction. |
| 9 | AAD54C | halftime_team_matchup / HALFTIME | Unowned accepted scene/payload. Reused after actual game end; asset name alone is not halftime. |
| 10 | AAD5B8 | voice_of / OVERLAY | Unowned commentator/reporter identity; prior request title Suzy Kolber. Do not replace the announcer's name with invented CBS talent. |
| 11 | AAD624 | weather_3row / OVERLAY | Unowned exact weather cells/scene; no verified native weather-event schema in current HTML API. |
| 12 | AAD690 | 1pfullsc_statsfew / OVERLAY | Unowned full-screen Performance/Career/Today Stats instances. |
| 13 | AAD6FC | big_team_matchup / PREGAME | Unowned exact native matchup. NBC has a separate stage-gated introduction, not blanket ownership of this layout. |
| 14 | AAD768 | coin_toss / GAMEDATA | Challenge/ruling acknowledged and verified. Toss, injury, penalty choice/result remain distinct constructors; detailed payload below. |
| 15 | AAD7D4 | halftime_gametrack / HALFTIME | Unowned; descriptor +3C initializer array 50D86C invokes DADE0. Keep initialization. |
| 16 | AAD840 | playclock_warn / OVERLAY | Unowned standalone warning. A live play-clock number/low-clock glow is not its lifecycle. |
| 17 | AAD8AC | divStandings / OVERLAY | Unowned actual season/division data. Do not synthesize current-NFL standings. |
| 18 | AAD918 | playoffRace / OVERLAY | Unowned actual league/playoff data. |
| 19 | AAD984 | skycam / OVERLAY | Independently observed during replay04, separate from the now-owned replay wipe. Accepted slot/resource and painted replacement still needed. |
| 20 | AAD9F0 | fullscreen_InjuriesV1 / OVERLAY | Unowned full-screen injury variant, different from layout-14 injury notice. |
| 21 | AADA5C | fullscreen_InjuriesV2 / OVERLAY | Unowned second injury scene/subject layout. |
| 22 | AADAC8 | fullscreen_PlayerOfTheGame / OVERLAY | Unowned exact selected-player/stat instance; captured request titles also Career Stats/Last Season. Name alone does not establish game-end context. |
| 23 | AADB34 | fullscreen_Possessions / OVERLAY | Unowned native drive/possession chart; not equivalent to one DRIVE_SUMMARY insert. |
| 24 | AADBA0 | fullscreen_SurroundSound / OVERLAY | Unowned audio-format promotional card. Do not replace facts about the audio output with a guessed format. |
| 25 | AADC0C | fullscreen_TwoPlayerDuo / OVERLAY | Unowned two-player graphic; preserve actual selected subjects. |
| 26 | AADC78 | fullscreen_PassChart_Stats / OVERLAY | Unowned pass-location chart/stat graphic. Requires actual plotted data as well as summary cells. |
| 27 | AADCE4 | halftime_team_stats / HALFTIME | Unowned accepted team comparison scene, also reused postgame. |
| 28 | AADD50 | halftime_player_transition / HALFTIME | Unowned independent player-transition scene. |
| 29 | AADDBC | halftime_player_ticker / HALFTIME | Unowned independent player ticker. |
| 30 | AADE28 | gamesound_digital / OVERLAY | Unowned format graphic, also used by constructor 318510. |
| 31 | AADE94 | gamesound_prologicII / OVERLAY | Unowned second audio-format graphic. |

The common callback lists in this catalog describe dynamic photos, logos,
numbers and text. For example 50D650 includes player1photo→1C0D90 and
team1logo→1C0F20; 50D898 includes player1position→1C1230,
player1number→1C1270 and player1teamlogo→1C12B0. 50D908 includes team1/team2
and NFL/ESPN scene callbacks. They explain why a single scene alpha is not
enough; they are not a reason to skip those callbacks or clear their resources.

## Shared accepted-instance and draw seams

All catalog requests eventually pass through 8ACF0. Entry diagnostics
[CARD]/[POPUPOP] prove requests only. Busy-slot failure returns EAX=-1 through
8ADB2. Source request buffers may be reused or have their layout auto-selected.

Two successful branches provide a possible complete acceptance observer:

| Site | Accepted result | Generator-maintained hook location |
|---|---|---|
| Explicit requested slot | 8AD7A sets EAX=MEM32(EDI), then restores callee-saved registers | After that exact EAX assignment, only in sub_0008ACF0. |
| Auto/reused closing slot | 8AE27 restores EDI, sets EAX=ESI, restores other registers | After that exact EAX assignment, only in sub_0008ACF0. |

The first preceding 1BD410 return has already run native layout setup. Actual
slots are B61B50 and B62A70, stride F20. Bind returned slot +0/+4, exact
layout +48, subtype/payload, callback +F14, current clock/rosters and page/package
generation. Exact constructor-return hooks are even narrower for notices below.
Do not infer acceptance from a nearby script opcode or title request.

The existing 8A170 frame observer sees the real slot. Keep its resource-ready
checks and first-draw latch +F1C. Narrow outgoing-art hooks already exist for:

- 8A170→1BD5A0, return 8A235: subtype table/choices alpha at pre-call ESP+16.
- 8A170→1BCE50, return 8A269: layout alpha at pre-call ESP+8.
- 1BCE50→1BC200, return 1BCEC4: effective layout alpha.
- 1BCE50 dynamic art/texture callbacks, returns 1BD230/1BD2F9.
- 1BCE50→1513D0, return 1BD17B: nested thread-local scene-text scope;
  150BF0→46B50 changes outgoing ARGB alpha within the scope only.

Native 8A840 update, 8A340 close/fade/resource release, optional callbacks and
8A810 teardown must remain. A page acquires suppression only after matching
paint and releases on inactive/reuse/page/match invalidation; closing ownership
retains suppression through the original exit. UI callbacks use copied host
flags only, never guest memory or guest calls.

## Layout-14 notices and interactive choices: exact payload

Let B be the accepted popup, P=B+228, and row R=B+234+i*228 (all offsets hex).
This is subtype **2**, not subtype-1's 24-cell table.

| Field | Meaning established by native code |
|---|---|
| B+4C | Inline UTF-16 heading, capacity 64 characters. |
| B+CC | Inline UTF-16 main message; inspected notice builders copy at most 64 characters. |
| B+228 | Subtype 2, set by 1BDD20. |
| B+22C | Native selected row index. |
| B+230 | Row count. Two row records physically fit before the controls; do not accept arbitrary counts. |
| R+0 | Row's guest completion callback, installed only by native confirmation/automatic choice. |
| R+4+j*88 | Inline UTF-16 cell text, max 64 characters, for each cell j. |
| R+84+j*88 | Cell text alignment/style. Most inspected notices pass 1; offsetting penalties use 2 for the team-name cell and 1 for the following penalty-name cell. |
| R+224 | Row cell count. At most four cell records fit before this count. |
| B+684 | Input team selector set by 1BE6B0; 1/2 match the owning away/home port side, 0 has no human selector. |
| B+688 | Default/automatic row index set by 1BE6C0; it also initializes B+22C. |
| B+68C | Native accumulated input delay; manual confirmation requires over 1.0s in this loop. |
| B+690 | Native measured-column/layout-ready latch; preserve it. |
| B+F14 | Whole-popup close callback; may legitimately change on interactive confirmation. |

1BE1F0 appends an initial cell and callback to a row. 1BE240 appends further
cells to that row. 1BE290 is the native update/selection owner: it enumerates
eight ports with 771F0, checks the side against B+684, reads native input
70A10, changes B+22C on D-pad bits 01000000/02000000, and confirms on bit 100
after the native delay. When no assigned human port exists, it selects the
default B+688 callback instead. BE5438 is also a native programmatic-confirm
latch. Never send these callbacks from the page or replace the input loop.

### Constructors safe to distinguish by their accepted result

| Family | Builder / request | After-8ACF0 return | Exact subtype-2 content/control |
|---|---|---|---|
| Coin result/message | 25DFE0 (overlapping alias 25E080), C38268 | 25E0B0 | Native title/message, one cell, finite 5s; callback supplied by caller. |
| Coin Heads/Tails, Kick/Receive, North/South | 25E0C0, C38268 | 25E1AC | Two rows, one cell each; FLT_MAX, exact native side/default/row callbacks. |
| Review | 117310, BA4E78 | 11741B | Already owned by CBS review capability; close callback 1172F0. |
| Ruling | 117430, BA4E78 | 117578 | Already owned; close callback 117300. Blank reason must replace previous review text. |
| Injury notice | 117780, BA5EA0 | 1178F9 | Heading Injured Player; message is the actual team, position and jersey (`%s %s #%d`); the one row is the actual player's first and last name (`%s %s`). No injury diagnosis is supplied here. 8s; input selector 0, row callback 0, whole close callback 117770 clears BA6DC0. |
| Penalty decision | 1180D0, BA7048 | 1182DF | Two rows **with three cells each**. Accept/Decline labels plus the two actual native consequence strings; FLT_MAX; side selector 1/2 and native preferred/default row. |
| Penalty result | 118310, BA7048 | 1185CE | Two rows, one cell each: actual accept/decline/enforced sentence and field/down consequence; 3s; selector 0, row callbacks 0, whole close callback 1180C0 clears BA7FF4. |
| Offsetting penalties | 1185F0, BA7048 | 11871E | Heading Double Penalty, message Penalties are offset. Replay down.; two rows of two cells, each team's actual name and penalty name; styles 2 then 1; 3s, row callbacks 0, close callback 1180C0. |

Penalty decision row callbacks are **117C10** (Accept) and **117C30**
(Decline). They tail-call A1CF0/A1D40 using BA7FE8/EC/F0, implementing real
football decisions. Its whole close callback initially differs from those
row callbacks; 1BE290 installs the chosen row callback with 8AC30 only when
the native decision completes. A replacement must allow this legitimate
closing transition while preserving the initially bound row identity.

The additional penalty cells come from native formatters **117C50** and
**117F30**. They are required information, not decoration. The existing
native_coin_choices parser extracts only each row's first label, selected
flag and an optional confirm label; using it unchanged for interactive
penalties or offsetting penalties would silently omit field/down outcomes.
Use an extended exact-row/cell serializer, or retain the native fallback.
For one-cell injury/result notices its address arithmetic is reusable, but
do not show a button solely because a string exists: honor the exact input
selector, native timeout and row count.

Concrete coin constructor callers and literal identities are established:
25E1C0/25E290 announce defending north/south; 25E360 chooses the goal with
actual wind text; 25E580/25E660 announce kick/receive and heads/tails;
25E780 chooses Kick/Receive; 25E830/25E8F0 announce chosen face;
25E9B0/25EA20 choose Heads/Tails. All retain original continuation callbacks.

## Independent graphics outside the popup catalog

| Family | Existing native seam | Current CBS coverage / caution |
|---|---|---|
| Main live bug and its text | FC200/FCE70, scene A95528; independent text colors A95894…A95C08 | CBS live bug exists and original bug hiding is implemented. This does not affect unrelated ESPN popups. |
| Automatic replay entrance/return | Screen 4E3F38, phases AFA078, exact 119E0→57CE0 draw and 57CB0 private transition deltas | Verified CBS paint/cover/handoff protocol, 6/6 transitions in final replay04. This is not SKY CAM or every replay annotation. |
| After-kickoff offense/defense lineup | FFBA0 reports 11 actual records at saved SP+1C, position codes SP+48; FFD60 displays original ticker | NBC declares lineup and consumes data; CBS does not. Current generic hide is package/host gated rather than per-paint, so do not enable it for CBS before a real consumer and stronger ownership. |
| Pregame team introduction | Actual playing-scene descriptor/filename observer, fresh phase-zero stage and featured-player snapshot | NBC has stage-gated own intro; CBS lacks a matching native intro owner. Loaded-only scenes or GAME_STARTED alone do not prove the camera is ready. |
| Native player/career facts | Exact player identity + caption selector F18 + table/prose payload | NBC has consumer. CBS PLAYER_STAT live inserts are not substitutes for original arbitrary career/season facts. |
| Drive Summary | Title/cells currently copied from generic popup request | CBS renders actual published plays/yards/TOP. No blanket original layout-3 replacement is justified by this heuristic/request alone. |
| Sack/celebration/player chip | Actual event 15 and selected-player observations mapped separately | Shared-sack credit disproves choosing just contact actor B. Exact event-to-credit-history join remains open. Keep original camera and chip choices. |
| Halftime/postgame show and statistical scenes | Native script construction and catalog 9/15/27/28/29 | Preserve real loaded scene, period/flow and selected subjects. Asset filename and B72E84 alone do not identify halftime/final or stat scope. |
| Pause, play selection, challenge confirmation | Actual FSM 4E9078, nested challenge 52DC20 and other native menus | These are interactive game UI, not passive broadcast inserts. Keep visible/input-capable; hide CBS graphics while native menus own the screen. |

The same ESPN wordmark can also exist in stadium art, uniforms, prerecorded
game footage or menus. Replacing broadcast graphics does not justify replacing
every matching texture or globally suppressing text/geometry. Those require
separate explicit scope and evidence.

## Order that produces useful verified progress

1. Finish CBS native toss and selected PLAYER_FACT consumers using the existing
   exact message/choice/cell tokens. Verify a human-controlled Heads/Tails and
   Kick/Receive/goal selection as well as CPU announcement flow and failed-page
   fallback. Share no input or saves with unrelated development runs.
2. Add narrow accepted-result observers for injury and penalty notices above;
   serialize **all** outcome cells before enabling CBS artwork ownership.
   Test actual native Accept and Decline, native close callbacks and reuse.
3. Bind layout-19 SKY CAM to its actual accepted slot/resource and a painted
   CBS camera annotation, preserving replay actor/camera state.
4. Capture accepted quarter/halftime/postgame tables and measured CBS variants;
   retain native titles, subject order, scopes and strings. Do not call a
   requested kind-0 table displayed or relabel Through Regulation as FINAL.
5. Expand exact single-player, two-player, weather, lineups and remaining full
   screen/chart families one supported schema at a time. Independently verify
   source motion/art; a generic CBS adaptation should be labeled as such.

Completion means native captures show the intended CBS artwork while the
original choices, timeout/update/close and football sequence still function.
The 255 populated script commands remain a title-wide command inventory,
**not 255 identified ESPN graphics** and not a replacement-coverage percentage.

## Evidence inspected

- PROJECT_STATUS.md through the verified CBS 0.5.4 challenge entry.
- docs/PRESENTATION-ORIGINAL-TRIGGERS-20261018.md,
  PRESENTATION-PERIOD-STATS-20261019.md and PRESENTATION-CHALLENGE-20261019.md.
- docs/SNF-NATIVE-INTEGRATION-20261011.md and SNF-PREGAME-20261010.md; stale
  historical readiness statements are superseded by later status entries.
- src/nfl2k5_presentation.cpp, src/main.c (read-only), tools/apply-gen-patches.py.
- Owned XBE catalog/callback/string records and generated functions 8ACF0,
  1C1530/1C16B0, 1BDD20/1BDDA0/1BE1F0/1BE240/1BE290/1BE6B0/1BE6C0,
  117780/1180D0/118310/1185F0 and 25DFE0/25E0C0 family.
- CBS/NBC manifests and existing component/event consumers.

## Isolated injury/penalty owner implementation — source verification only

Added src/presentation/native_notice.inc.cpp as a deliberately separate included
module: the shared layout-14 notice owner can be reviewed independently of coin,
review and football code. It binds only actual active slots with native layout
readiness 5, first-draw latch, resource/scene and the supported callback/row
identities above. It emits BROADCAST_NOTICE/BROADCAST_NOTICE_HIDE with the exact
heading, message and every cell; selected-row updates retain the instance token.
It does not call native choice callbacks, change timers or write guest state.
The parent integration owns package registration, shared draw glue and the CBS
consumer. Unsupported callbacks/layouts remain original artwork.

tools/presentation-audit/verify-notice-owner.py compiled that production module
verbatim with mocked RAM/page dependencies and passed **674 assertions**. Covered
both slots, injury/result/offsetting/interactive families, all extra outcome
strings including valid empty cells, native selection updates, delayed/stale ACKs,
the legitimate selected callback change during native closing, slot reuse,
resource/clock/roster/page/phase invalidation, and zero guest reads on browser
message callbacks. Artifacts are under outputs/presentation-audit/notice-owner-test.
The owner source SHA-256 at this check was
ac80f6a4908704c71b0ff37c963975af0db1d32fa9a44b977fcce802f2278d03.

This standalone verification does **not** prove a real injury or penalty was
painted in-game. The production build, ordinary native decision controls,
composed captures and failure fallback still require the parent integration's
runtime test. It is not counted as complete CBS graphic coverage.

## Subsequent exact team-table implementation — source verification only

The catalog's coverage column above records the initial CBS 0.5.4 baseline, not
the integration status of work completed later in this session. An isolated
`src/presentation/native_table.inc.cpp` now supports actual accepted subtype-1
team tables in layouts 3 and 4, plus the independently verified scene-driven
3-column/5-row layout 27. It copies literal native title and every exact cell,
resolves only verified loaded-roster team subjects, and requires paint before
replacing the instance's draw. Layout 0 remains unsupported: request logs and a
serialized cell array do not prove its zero descriptor produced visible art.

Correction to the initial layout-27 assumption: its zero catalog `+24` disables
the generic subtype-table draw, but `1BCE50` independently paints its scene text.
Read-only decoding of the owned halftime resource proves 32 paired text records:
15 exact cells through callback `1C0CD0`, plus the literal title through `1C0AF0`.
The owner validates that actual loaded scene before suppressing any art.

`tools/presentation-audit/verify-native-table-owner.py` compiles the production
module verbatim and passes **1,066 assertions**. It also independently checks
the owned scene/XBE callback mapping and zero browser guest reads. No native
game build or runtime table handoff was performed by this agent. Full data
layout, interface, evidence and remaining runtime checks:
`docs/PRESENTATION-NATIVE-TABLES-20261020.md`.

That follow-up also audits the separate brand/audio-format scenes 24/30/31.
Gamesound Digital/Pro Logic II contain real ESPN scene materials and no dynamic
text, but their remaining image/geometry meaning is not yet verified. They are
not safe to replace with a generic CBS logo merely because there are no cells.

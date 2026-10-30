> **2026-10-20 user correction:** the new eleven-player grid and generic navy table do not match CBS. They are rejected visual prototypes, disabled in the CBS manifest and removed from the shipped page. Native data/ownership experiments below remain engineering evidence, not approved CBS graphics. Do not re-enable them merely because tests/builds pass. Use measured CBS broadcast treatments instead.

# Native starting-lineup ticker — October 20, 2026

Read-only audit of production source, generated guest functions and the
owned retail XBE. No game launch, native build, generated-code edit or
draw-suppression trial was performed for this audit. At the audit's start,
CBS had no LINEUP handler or lineup replacement capability; the isolated
component added below still requires native integration and verification.
Existing SNF timed slides are not native paint ownership.

## The real object is a ticker, not an 8ACF0 popup

`000FFE90`, called by match initialization `648D9 -> 648DE`, loads resource
**start_line_p_ticker** (UTF-16 **E6CBE4**) as type **534B524D / MRKS** through
`449E0`. The result is stored at **BA3538**. Its first dword is the loaded
scene, by `151530`. It allocates a generic ticker through `177430` and stores
that pointer at **BA3534**. `177540` binds:

| Ticker field | Native value / role |
|---|---|
| +12C | Current text-ring read index; mutable |
| +130 | Text-ring producer index; mutable |
| +134 | Ring entry flags; not a football phase |
| +138 | Native configured timing value, initialized 11.0 |
| +13C | Native configured secondary value, initialized 1.0 |
| +140 | Native mutable time/advance field |
| +150 | Completion callback **000FFE70** |
| +154 | Post-draw callback, initialized 0 |
| +158 | Resource **MEM32(BA3538)** |
| +15C | Optional mesh camera, initialized 0 (default BDE990) |
| +160 | Text camera **BA3550** |
| +164/+168 | Intrusive queue links, maintained natively |

Do not infer a 28.6-second native duration from the existing host's eleven
2.6-second slides. The native timing, ring indexes, resource animation and
callbacks remain authoritative.

## Native lifetime

| Address / global | Behavior |
|---|---|
| FFDD0 | Accepts the selected team's roster pointer in ECX. If a sequence is active, queues BA353C/BA3540; otherwise sets BA3210 and BA3544 and tail-calls FF980. |
| FF980 | Populates team-specific colors/logo in the loaded ticker scene; preserves scene animation setup. |
| BA3544 | Roster selected for the current lineup; bind this actual roster to a side, rather than re-deriving side later from changed possession. |
| BA3210 | Sequence enabled/active flag. |
| BA3214 | Native currently-shown flag. |
| BA3218 | Text populated flag. |
| FFA80 | Native eligibility checks, including competing popups/presentation/field conditions. |
| FFD60 | Sets BA3214; populates with FFBA0 if needed; sets BA3218; tail-calls 1775D0 with BA3534. |
| 1775D0 | Inserts that ticker into the active intrusive queue whose head is BDE77C and sentinel is BDE618. This is not just a draw call. |
| FFE00 | Original per-frame visibility/queue controller; shows/hides when native eligibility changes and starts queued next lineup after completion. |
| FFDA0 | Calls 177640 to remove the ticker from active queue; clears BA3214. |
| FFE70 | Completion callback: calls FFDA0, clears BA3210 and BA3218. |
| FFDC0 | Match cleanup: tail-calls 1774D0 to release/reset the allocated ticker. |

`1776A0` is the generic ticker draw dispatcher. It respects original match,
pause/menu and presentation conditions, selects the current active queue
head, and chooses scene or generic text rendering based on native mode and
whether head+158 holds a resource. `176DC0` is a tail-call to `110F40`, the
native update path. Do not remove this update or the generic ticker manager.

The existing `LINEUP_NATIVE_HIDE` at FFD8B returns before the tail-call to
1775D0 when a package merely declares `replaces: ["lineup"]` and the HTML
host exists. This is a blind one-shot enqueue bypass. A late browser ACK
cannot restore its native queue membership. CBS must not enable this legacy
branch for its new implementation.

## Existing live player payload

The existing hook at **FFD54**, at the completed end of FFBA0, calls
`nfl2k5_lineup_ticker(esp)`. Its stack contains eleven selected roster records
at **SP+1C+i*4** and eleven native position codes at **SP+48+i*4**. FFBA0 has
already formatted native names into **BA3274+i*40**.

Current host extraction exposes these actual fields in each player JSON:

```text
position, number, name, first, last, college, photoId, portrait
```

Position strings come from **4F2688[code]**. Jersey number is
`(record+20 >> 3) & 7F`. First/last names come from the live record's relative
or fixed-up pointers at +10/+14. Photo ID is the uint16 at +6. College is
resolved from the live record, and may be unavailable. Preserve names and
player order. For a full-name graphic prefer explicit first + last when
both resolve, then the native formatted name. Missing college/portrait is
not grounds for inventing either.

The current host infers offense by finding a QB, then resolves the event's
team at later delivery from current possession. The actual native OFFENSE /
DEFENSE heading is selected in FFBA0 by comparing **BA3544** with the roster
at **MEM32(E60280)+1C**. An owned replacement should capture that selection
at the native source and bind **BA3544** to one of the two match rosters.
Do not label a new possession's roster using a stale queued event.

The existing plain `LINEUP` event is a useful data source, but it has no
source token, generation, resource identity, actual-show observation or
paint ACK. It is therefore insufficient on its own to suppress native art.

## Narrow output-only seams

Scene ticker draw **1770C0** receives the current ticker in **EAX** and saves
it in **ESI**. It saves the current camera, sets the mesh camera, iterates
scene meshes, changes to the text camera, draws the current native text
ring entries, then restores the original camera and calls optional +154.

The relevant calls are:

| Caller | Exact output call / return | Context |
|---|---|---|
| 1770C0 | **21860 -> 177128** | Draw one scene mesh, ECX=node; owning ticker remains ESI. |
| 1770C0 | **1513D0 -> 1771A3** | Draw current scene text, ECX=text node, EDX=current string; ticker remains ESI. |
| 1770C0 | 2AC80 -> 1771C0 | Restore original camera; always retain. |
| 1770C0 | indirect callback -> 1771CC | Optional ticker+154; always retain. |

The scene meshes include the ESPN ticker artwork, independently of the
generic popup text-alpha scopes. The ticker is not an active B61B50/B62A70
popup. The popup draw predicate must not be broadened to every ticker.

A safe new owner can observe the actual **1770C0 entry EAX** and bind only
when it equals **MEM32(BA3534)**, the exact BA3538 resource and loaded scene
match, all three native sequence/show/population flags are set, the ticker
is actually active, and copied player/team identity is complete. It must
match package/page generation, clock object and both roster pointers.

For draw-only output suppression, a narrowly anchored guard before those
two output calls can bypass **only** the owned call, before pushing its
guest return address, then continue at its normal return label. This must
retain mesh/text iteration, camera save/change/restore and +154 callback.
Before adopting a call bypass, verify in the actual output helper that it
does not own independent simulation/update behavior. Alternatively add
scoped zero-alpha handling to each output helper with the exact ticker
owner. Do not early-return 1770C0, skip 1775D0, remove queue membership,
shorten duration, replace completion callbacks, or globally hide 21860 /
1513D0 calls. Generic non-scene ticker branches remain original fallback.

Additional helper inspection: `21860` checks native node validity and
visibility, can call `215A0` for culling (its temporary camera+238 adjustment
is restored), then submits geometry through `243D0`. The latter changes
render/material/pushbuffer caches and emits graphics commands. No football,
ticker completion or player-selection callback was found in this path.
The omission must still be scoped to this exact owned ticker's two calls;
these helpers are used elsewhere throughout the game.

## Proposed copied owner and browser handshake

1. At completed FFBA0 capture the actual eleven selected players, their
   stable record pointers/order, actual selected roster BA3544 and unit,
   resource/ticker identity, valid match/package/generation. Mark pending;
   this preparation is not proof of visible native art.
2. At actual 1770C0 draw confirm the prepared identity, actual active queue
   instance and BA3210/14/18 flags. Assign a fresh canonical uint32 token and
   publish an owned native LINEUP event with that token. Keep original
   output until browser paint ACK. An unbound pending event cannot hide art.
3. CBS loads its real fonts, selected team branding and any optional player
   portrait, presents the live roster, then ACKs after full entrance and
   two visible paint frames. The page never touches guest memory.
4. Every draw predicate rechecks actual ticker/resource/scene/rosters and
   context. The browser ACK is a copied host flag only.
5. Native FFDA0/FFE70/FFDC0 and actual queue/flags/resource changes revoke
   ownership and send native HIDE. Pause/play selection/replay/context/page
   changes revoke paint and leave native lifecycle intact. A still-valid
   ticker can repaint after temporary eligibility loss, without fabricated
   native completion.

Approved protocol for the new CBS component:
`LINEUP` with `source:"native"`, `nativeVisible:true`, canonical string
`nativeLineupToken`, captured `unit`, actual `team` and players; native
`LINEUP_HIDE` with the exact token. Page ACK/HIDE:
`NFL2K5_LINEUP_SHOW/HIDE:<token>`. Browser previews send no native ACK.
Manifest capability should be separate `lineup_cards_ack:true`; CBS must
not set the legacy blind `replaces_lineup` path.

## Editable CBS unit component

`mods/presentations/CBS_2026/lineup.js` and `lineup.css` now provide that
approved browser-side protocol and an editable whole-unit roster. They were
created as isolated files; this work does not add native ownership, renderer
hooks, shared HTML/router wiring or a manifest capability by itself.

The grid displays all supplied starters in native order, with actual full
first/last names (or the native formatted fallback), positions, numbers,
college text and optional portraits. It does not cycle through players on
an independent timer. The source native LINEUP/HIDE events own its lifetime.
Its navy/silver plates, dot treatment, official CBS mark and existing fonts
follow this package's design system; this is an explicitly labelled
adaptation, not a measured CBS lineup reconstruction.

Integration API is `CBSLineup.updateState(state)`, `handleEvent(event)`,
`snapshot()` and `cancel()`. The shared page must include the new CSS and JS,
call the state handler and route events. Untokened legacy LINEUP events are
consumed without painting. Original generic HTML lineup behavior in other
packages is not changed.

Paint ACK requires the actual required branding image, fonts, full readable
names, a complete entrance and two visible animation frames. Failed optional
portrait/team logos are omitted with an actual abbreviation fallback; failed
required branding, fonts, unfittable text or an asset deadline preserve
native output. Names are never truncated or converted into executable HTML.
Old asynchronous image work cannot overwrite a newer token's shared header.

Pause, play selection or replay context/wipe hides and revokes the painted
owner while retaining the exact pending instance until native HIDE. A still
valid instance can repaint after those temporary conditions. Changed match,
invalid/inactive/final state, native game end or exact HIDE clears ownership.
The active visible roster is monitored for lost paint and newly starting
replay wipes without waiting for a later state message. No guest memory or
callbacks are accessed from JavaScript.

## Visual evidence boundary and next verification

Existing SNF slides provide an editable roster-rendering example but no
measured CBS lineup reference. Reusing the established CBS font, navy/silver
plates and dot pattern would be a labelled CBS design-system adaptation,
not proof of a pixel-matched 2026 lineup. Do not claim exact CBS source
fidelity until a suitable broadcast sequence has been measured.

Native verification needs actual ticker pointer/resource/scene/unit/roster,
queue head, callback and flags at show, first draw, pause/hide/resume and
completion; compare original vs replacement on screen. Exercise fallback
for missing host/capability, fonts/image failure, stale token/generation,
changed possession while a queued event waits, and the next defense lineup.

The old `s_lineup_until` predicts 11*2600ms and asks main.c to close player
cards early. That is a presentation policy with native side effects, not
proof that a lineup painted. Do not activate it for CBS; preserve the actual
title's player-card sequencing until replacement ownership is independently
defined and verified.

## Completed isolated browser check

`tools/cbs-scorebug/verify-lineup.js` completed **92 assertions PASS in real
Chromium/Edge** using the shipped CSS, fonts, official branding and component.
Checks include all eleven copied full names/numbers/positions, native hold
beyond 30 seconds without an independent expiry/player cycle, paint ACK,
temporary context/wipe deferral and repaint, native/final cancellation,
canonical/stale tokens, async-close safety, optional-asset omission, required
font/logo/asset-timeout fallback, literal native strings, unfittable-name
fallback, preview silence and explicit reduced-motion policy.

Browser-only fixture images were rendered and visually inspected at
`outputs/cbs-2026/lineup-browser-20261020/unit-offense.png` and
`unit-defense.png`. The offense grid displays all eleven supplied fixture
players above the normal Eyebar without overlap. These are not in-game
captures or a measured CBS television lineup comparison. Native output
replacement is still awaiting the primary task's ownership/draw integration.

## Implemented native owner include and isolated check

`src/presentation/native_lineup.inc.cpp` now implements the audited native
owner as an isolated include. It prepares from the existing FFBA0 collector
through `native_lineup_snapshot(sp, players_json, count)`, requiring eleven
distinct selected live records belonging to the selected roster. It captures
unit/side from the actual native selected/offense roster comparison, not the
presence of a QB or possession at later browser delivery. Player membership
is validated once; draw queries do not repeatedly scan the roster or names.

Preparation alone never publishes native visibility or hides art. A matching
actual 1770C0 frame acquires a new token only for BA3534, BA3538/loaded scene,
all three native flags, actual queue head, source callback/camera bindings,
current match lifecycle, clock, rosters, package and page generation. Tokens
are delivered through the shared pending event transport and acknowledged
only through copied host flags. No guest code is called from the browser.

`native_lineup_hide(0)` handles a temporary native hide: it releases the
visible token while retaining the actual prepared seed so that normal native
re-show (which does not necessarily repopulate FFBA0) can acquire a fresh
token. `native_lineup_hide(1)` handles FFE70 completion/FFDC0 cleanup and
discards the seed. The HUD path also observes real queue/flag/context loss.
An invalid new source discards the old unit rather than displaying stale
players. A source serial guards copied work against a concurrent newer seed.

The include additionally provides `native_lineup_frame`,
`native_lineup_draw_replaced`, `native_lineup_hud_context`,
`native_lineup_event_current`, `native_lineup_page_message` and
`native_lineup_reset`. The primary task must supply the shared capability,
pending fields, actual gen hooks, host registration and callback glue.
`lineup_cards_ack` remains separate from legacy `replaces_lineup`.

`tools/presentation-audit/verify-lineup-owner.py` completed **1284 actual
native lineup-owner assertions PASS**, compiling the unmodified include and
the real pause/replay/final predicates with fixture guest memory and external
transport/package inputs. Source SHA-256:
`f601f794d3f089804753fc9ee69bcb0fa79a23d29294bd55bfb669ceb2e20b58`.
Coverage includes all four team/unit combinations, preparation vs actual
render vs delivery vs paint, eleven-record membership, stale source/token /
resource / match rejection, native hide/resume/final cleanup, lost context,
mutable native ring fields, zero guest reads in page callbacks and hot-path
early gates. It performs no Release build, game launch, main CPP/header or
generated-code mutation. In-game replacement remains to be demonstrated by
the primary native integration run.

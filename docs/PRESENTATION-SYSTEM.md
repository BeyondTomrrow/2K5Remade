# Broadcast presentation system

Status: HTML presentation host introduced 2026-09-30. The JSON presentation
renderer (CBS, FOX and the imported packages) is unchanged and still the path
for every `presentation.json` package.

## 1. What existed before this work (inventory, 2026-09-30)

| Item | Where | What it is |
|---|---|---|
| JSON presentation renderer | `src/nfl2k5_presentation.cpp` | Reads the game state from guest memory, detects events, draws `presentation.json` element lists (box/image/text/timeouts) with Direct2D into a CPU image, handed to the presenter's HUD layer (`xbox_PresentSetHudCallback`). Also: theme music, pregame open video, ESPN-bug hiding, menu selection (Coach Match Up > Presentation). |
| Broadcast state/event API (C) | `src/nfl2k5_broadcast.h` | `Nfl2k5BroadcastState` (teams, scores, timeouts, quarter, clocks, down, distance, ball, possession, flag) and `Nfl2k5BroadcastEvent` plus player-stat samples. Produced by the renderer above; consumed by it too. |
| **NFL on CBS** | `mods/presentations/NFL on CBS/presentation.json` | The original package (2026-09-26): 23 elements, 11 animations, CBS open video, theme music. |
| **NFL on FOX** | `mods/presentations/NFL on FOX/` | Newest package (2026-09-27..30): 19 elements, possession-side down/distance banner, live play clock, QB stat insert, private fonts, `REFERENCE.md` measurement ledger from a 1280x720 still. Same JSON format. |
| CBS_Style_Football_Scorebug | `mods/presentations/CBS_Style_Football_Scorebug/` | Imported/normalised JSON package (13 elements). |
| Classic Broadcast Scorebug v27 | `mods/presentations/Classic_Broadcast_Scorebug_v3_Custom_Network_Logo/` | Imported JSON package (82 elements, 11 animations, custom network logo). |
| Scorebug Studio | `tools/scorebug-studio/index.html` (+ `tools/open-scorebug-studio.ps1`) | Canvas preview of a CBS-style three-panel bug with form controls; the whole bug can be dragged; exports `presentation.json`. No per-element selection or resize, no reference image. |
| Installer for JSON packages | `Install Scorebug JSON.cmd`, `tools/install-scorebug-json.ps1` | Copies a `presentation.json` (and nearby media) into `mods/presentations/<name>/`. |

Not found anywhere in the project: an NBC / Sunday Night Football package,
HTML/CSS/JS scorebugs, or scorebug reference screenshots other than the FOX
ledger's description of its still. (The three images in the project root are
installer/box art.)

## 2. Architecture

```
NFL2K5.exe (football facts)
   reads guest state, detects events           src/nfl2k5_presentation.cpp
        |  Presentation API v1 (JSON messages)
        v
PresentationHost (engine)                       src/presentation/presentation_host.h
   WebView2 implementation                      src/presentation/webview2_host.cpp
        |  runs the package page off screen, captures it with alpha
        v
PresentationSurface (BGRA, premultiplied, CPU)
        |
        v
HUD layer of the presenter (one textured quad, premultiplied blend)
        |
        v
whichever backend presents the game (D3D11 today; DX12/Vulkan later)
```

Rules:
- The game reports what happened (`TOUCHDOWN`, team `home`). It never
  names a network or an animation. A package decides what that looks like.
- No graphics backend knows about HTML or scorebugs. The host produces
  pixels; the presenter composites an image, as it already does for the JSON
  renderer and the pregame video.
- JSON is metadata (`mod.json`). The look is HTML/CSS; behaviour is JS.

### Canvas and resolution

A package draws on a logical canvas, normally 1920x1080 CSS pixels. The host
rasterises it at about the size it is shown (raster scale = shown height /
1080, clamped to 0.5..2, so 1440p is 1.33 and 4K is 2) and the canvas is
placed on the largest 16:9 rectangle inside the game picture. At 4:3, 21:9 or
32:9 the broadcast canvas keeps its proportions and stays in the 16:9 safe
area; it is never stretched.

Planned (not built yet): `canvas.fit: "full_viewport"` for a package that
wants the whole ultrawide width, where the canvas width follows the aspect
(1080 tall, 2520 wide at 21:9).

## 3. Package layout

```
mods/presentations/
    _runtime/nfl2k5-presentation.js     shared Presentation API runtime
    HTML_Test/
        mod.json
        scorebug.html
        scorebug.css
        scorebug.js
        assets/...
```

Packages live in `mods/presentations/` (the folder the game, the installer and
Scorebug Studio already use) next to the JSON packages. A folder with
`mod.json` declaring `"type": "html"` is an HTML package; a folder with
`presentation.json` is a JSON package. Folders whose names start with `_` are
shared support files, not packages.

### mod.json

```json
{
  "schema": 1,
  "id": "html_test",
  "name": "HTML Test (development)",
  "type": "html",
  "version": "0.1.0",
  "author": "...",
  "description": "...",
  "enabled": true,
  "entry": "scorebug.html",
  "canvas": { "width": 1920, "height": 1080, "fit": "broadcast_safe_16x9" },
  "dependencies": [],
  "assets": {},
  "compatibility": { "game": "NFL2K5-PC", "presentation_api": 1 }
}
```

Inside the game the page is served from `https://presentation.local/<package>/`,
so relative paths work (`assets/logo.png`, `../_runtime/...`). Team assets are
served from `https://teams.local/<ABBR>/...` (the `mods/teams` folder).

**My NFL Eras games** (docs/eras/results/M9-era-looks.md): each side's `abbreviation`, `city`,
`name`, colours and `logo` come from `mods/eras/teams/<season>/<eras id>/team.json`, keyed by the
staged Eras team (BAL is the Baltimore Colts, HOU the Houston Oilers), and `logo` is served from
`https://erateams.local/<season>/<eras id>/logos/scorebug.png`. `state.league` is `"eras"` and
`season`, `decade` and `week` are set. A package that keeps its own per-abbreviation art (which is
2004 art) should use `team.logo` when `state.league === "eras"`. `mods/eras/presentations.ini` can
pick a package per season, range or decade for Eras games only; the player's own selection is put
back after the game and never saved over.

## 4. Presentation API v1

Pages include the runtime and register handlers:

```html
<script src="../_runtime/nfl2k5-presentation.js"></script>
<script>
  NFL2K5.onState(function (state, previous) { /* update elements */ });
  NFL2K5.onEvent(function (event) { /* play an animation */ });
</script>
```

`NFL2K5.host` is `"game"` in NFL2K5.exe, `"editor"` in Scorebug Studio.
`NFL2K5.log(text)` writes to the game's stderr log (`[HTMLPRES] page: ...`).

### State (sent on every change, at least twice a second)

| Field | Type | Meaning |
|---|---|---|
| `apiVersion` | number | 1 |
| `valid` | bool | a match is loaded and its state could be read |
| `gameStatus` | string | `inactive`, `pregame`, `in_progress`, `final` |
| `phase` | number | the title's play phase (0 none, 1 free kick, 2 kickoff, 3 PAT, 4 scrimmage) |
| `quarter` | number | 0 pregame, 1-4, 5+ overtime |
| `gameClock` | number | seconds left in the period |
| `periodLength` | number | seconds per period |
| `playClock` | number | seconds, -1 when there is no live play clock |
| `down`, `distance` | number / null | 0 and null outside scrimmage |
| `goalToGo` | bool | |
| `downDistanceText` | string | `"3RD & 7"`, `"1ST & GOAL"`, `"KICKOFF"`, `"PAT"` |
| `ballPosition` | object | `{ yardLine, side, text }`; `side` is null (drive direction not mapped yet) |
| `redZone` | null | not reported yet; needs the drive direction. Never guessed. |
| `possession` | string / null | `"away"`, `"home"` |
| `league` | string / null | `"eras"` in a My NFL Eras game, else null |
| `season` | number / null | the Eras season (e.g. 1980), else null |
| `decade` | string / null | `"1980s"` in an Eras game, else null |
| `week` | number / null | the Eras fixture's week, else null |
| `away`, `home` | object | `abbreviation`, `city`, `name`, `logo` (URL), `record`, `score`, `timeouts`, `primaryColor`, `secondaryColor` |
| `context` | object | `scorebugVisible` (live play camera: show the bug), `playSelection`, `paused`, `nativeScorebug`, `flag`, `periodEnding`, `cornerClaimed` |

`context.cornerClaimed` is true while a game HUD element uses the top-right corner (the MyCareer stat bar, `src/nfl2k5_mycareer_hud.c`). A package with a corner bug should clear it while this is true and bring it back when it turns false (NBC_SNF in `updateWatermark`, CBS_2026 for its `#network` bug).

`context.periodEnding` is true only while the title has entered its actual end-of-period/halftime flow at a zero clock. A last play, pending PAT or untimed down at 0:00 does not imply this state. The field is independent of the later `QUARTER_ENDED` event (currently sent when the period increments), and visibility/menu/pause rules still apply. It is not a command to play a generic quarter bumper.

### Events

Each event is `{ name, team, time, ... }`, with `team` being `"away"`, `"home"` or
null.

Reported now:

| Event | Extra fields | Source |
|---|---|---|
| `GAME_STARTED` | | first state with a quarter |
| `SCORE_CHANGED` | `points`, `score` | state |
| `POSSESSION_CHANGED` | | state |
| `DOWN_CHANGED` | `down` | state |
| `QUARTER_ENDED` | `quarter` | state |
| `QUARTER_STARTED` | `quarter` | state |
| `HALFTIME` | | state |
| `OVERTIME` | | state |
| `TOUCHDOWN`, `FIELD_GOAL`, `EXTRA_POINT`, `TWO_POINT_CONVERSION`, `SAFETY` | | scoring change |
| `TIMEOUT` | | timeout count |
| `DRIVE_SUMMARY` | `drive` | the game's Drive Summary popup (exact plays, yards, time) |
| `FIRST_DOWN` | | down reset on the same possession |
| `TWO_MINUTE_WARNING` | | clock |
| `GAME_ENDED` | | clock and period |
| `PENALTY`, `INTERCEPTION`, `FUMBLE`, `TURNOVER`, `INJURY`, `REPLAY_STARTED` | | the title's own popup text |

`TOUCHDOWN` and `FIELD_GOAL` carry `drive`: `{ plays, yards, timeOfPossession (seconds), source }`. `source` is `"tracked"` (measured from the live state during the drive) or `"game"` (from the game's Drive Summary, sent later as `DRIVE_SUMMARY`; prefer it when it arrives).
Defined but not produced yet (no verified source in the title): `DRIVE_STARTED`, `PLAY_STARTED`,
`PLAY_ENDED`, `SACK`, `REPLAY_ENDED`, `PLAYER_STAT` (the FOX QB card still uses
the JSON path).

### Players and portraits (planned)

Player graphics (introductions, stat cards) will receive a player object from
the game: id, name, position, number, team, rookie/Pro Bowl/MVP flags. The
portrait is requested from a portrait service by player id, so it follows the
player's *current* team (trades, free agency, draft classes) rather than being
an image stored in a network package.

## 5. Testing switches

| Variable | Effect |
|---|---|
| `NFL2K5_PRES_LOG=1` | logs state and every HTML event sent |
| `NFL2K5_PRES_TEST=1` | fixed sample state (KC at NE) for layout work |
| `NFL2K5_PRES_EVENT=touchdown,first_down,interception,penalty` | cycles those events every 5 s (HTML packages) |
| `NFL2K5_HTML_DEVTOOLS=1` | allows the WebView2 developer tools |

## 6. Building

The host builds against the WebView2 SDK in `dependencies/webview2/sdk`
(`tools/get-webview2-sdk.ps1` fetches it) and the Windows SDK's C++/WinRT
headers. At run time it needs the WebView2 runtime, which Windows 10/11
include. If it is missing, the host logs an error and the game runs without
the HTML overlay.

## Native period-board ownership (2026-10-18)

HTML packages may declare `"replaces": ["period_boards"]` and
`"period_boards_ack": true`. This first supported family is the actual native
Q1/Q3 popup layout 7 (`endofqrt`), not every quarter statistic, halftime scene,
final board or the ordinary scorebug. Original ESPN and packages without both
capabilities retain their original draw path.

The native popup frame hook binds the active instance, its clock/both rosters,
package/page generation, and ending quarter. It sends:

```json
{"name":"PERIOD_BOARD_SHOW","source":"native","nativeVisible":true,"nativePeriodToken":"1","quarter":1}
```

`nativePeriodToken` is a canonical positive uint32 **string**, unique to that
popup instance. State changes may already advance `state.quarter`; a package
must retain this event's ending-quarter value until the matching close. The
native event is independent of `QUARTER_ENDED`, which is a later state delta.

A page that actually paints its replacement logs
`NFL2K5_PERIOD_BOARD_SHOW:<token>` through `NFL2K5.log`. CBS waits for loaded
fonts, two animation frames and visible geometry. The host rejects stale
package/generation/token/slot/match identities. Until that acknowledgement,
the original board remains visible. A page must not acknowledge sample data,
a hidden graphic, failed fonts or an unsupported period.

Native close sends `PERIOD_BOARD_HIDE` with the same source/token/quarter and
`nativeVisible:false`. The page releases only the matching instance, logging
`NFL2K5_PERIOD_BOARD_HIDE:<token>`. Duplicate closes are harmless. The native
owner preserves acknowledged suppression through its original closing
animation, then releases on inactivity, slot reuse or changed identity.

The host changes only outgoing artwork/font alpha at the existing draw
boundaries. Text formatting, scene/node state, camera selection, native
animation updates, timeout and close callback all remain with the game. The
browser updates synchronized host flags only; it never reads or executes
anything in guest memory.

## Native automatic replay ownership (2026-10-19)

HTML packages can explicitly opt in with `replay_wipes_ack:true`. The actual
automatic replay screen binds each initialized entrance/return record to a
canonical uint32 string `replayToken`, package/page generation, clock, both
rosters and exact native resource/scene/callback/timing identity. Its events
are `NATIVE_REPLAY_PREPARE`, `NATIVE_REPLAY_START`,
`NATIVE_REPLAY_HANDOFF` and `NATIVE_REPLAY_CANCEL`, with
`source:"native_automatic_replay"` and `direction:"in"|"out"`.

The page acknowledges `NFL2K5_REPLAY_READY:<token>`, START, COVER and DONE
through `NFL2K5.log`. Readiness alone cannot hide original art. START and
COVER follow two painted animation frames; COVER must actually conceal the
viewport. Only the title's original midpoint/camera/actor calls generate
HANDOFF. DONE follows the painted final transparent frame. Page callbacks
read/write no guest memory and execute no guest code.

The guest's four private transition-delta calls adapt visual time to the
package's timeline. Original transition values, football clocks, midpoint/end
comparisons, camera/actor changes, callbacks and cleanup remain owned by the
title. Only the exact replay-screen57CE0 draw call is omitted while a painted
replacement owns it. Missing/unsupported/page-failed/canceled contexts keep
the original path; initial readiness and total ownership have bounded
watchdogs. See `PRESENTATION-REPLAY-OWNERSHIP-20261019.md` for timing,
original-call anchors, reset behavior and actual pixel-coverage evidence.

State includes `context.replayActive` only when the actual descriptor is in
the active FSM stack, and `context.animationsEnabled` from the game's explicit
Presentation setting. Actual successful match initialization/exit observations
keep the host warm through stationary cameras without leaving it active on
retained front-end match globals. A quiet game clock is not a match exit.

See `PRESENTATION-ORIGINAL-TRIGGERS-20261018.md` for exact confirmed sources,
heuristic producers and remaining coverage gaps.

## Native challenge/review ownership (2026-10-19)

HTML packages may opt in with `review_cards_ack:true`. The host observes the
accepted result of the two actual challenge/ruling constructors, never the
pre-acceptance popup request. `REVIEW_SHOW` / `REVIEW_HIDE` carry `source:"native"`,
`nativeReviewToken` (canonical uint32 string), `nativeVisible`, `reviewStage`
(`review` or `ruling`), and the copied native `heading`, `message`, `reason`.
Do not infer a verdict or preserve a prior reason when the ruling reason is empty.

The page sends `NFL2K5_REVIEW_SHOW:<token>` only after fonts/assets and actual
paint, and `NFL2K5_REVIEW_HIDE:<token>` when removed. Ownership binds the exact
accepted slot, layout14/subtype2, callback, copied strings, clock, both rosters,
package and page generation. Native first-draw readiness is retained. Original
draw alpha changes only after a matching painted acknowledgment; timeout,
close animation, callbacks1172F0/117300 and football decisions remain native.
UI acknowledgments use synchronized copied host state and never access guest RAM.

CBS defers the card during a controlled replay and releases it on pause,
play selection, changed match, page reset or native close. A closing native
instance retains acknowledged suppression until its original cleanup. Native
pause detection includes the active inclusive FSM stack's4E9078/52DC20 menus;
AC7444 alone is only the separate navigation-pause latch. Stale slots above
the current top do not count. Details and real native captures are in
`PRESENTATION-CHALLENGE-20261019.md`.

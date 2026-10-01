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
| `away`, `home` | object | `abbreviation`, `city`, `name`, `logo` (URL), `record`, `score`, `timeouts`, `primaryColor`, `secondaryColor` |
| `context` | object | `scorebugVisible` (live play camera: show the bug), `playSelection`, `paused`, `nativeScorebug`, `flag` |

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

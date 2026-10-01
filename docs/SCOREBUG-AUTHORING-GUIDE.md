# How to make a scorebug for NFL2K5-PC (guide for ChatGPT and other authors)

This explains how to build a broadcast scorebug (or any broadcast graphic) as
an **HTML presentation package**. The game supplies live football data and
events; your package decides everything about how it looks and moves.

Technical reference (all fields and events): `docs/PRESENTATION-SYSTEM.md`.
Working example to copy: `mods/presentations/HTML_Test/`.

---

## 1. Rules from the project owner (follow these exactly)

1. **Match the reference, do not reinterpret it.** When given a TV screenshot,
   reproduce it: panel sizes, logo size and position, abbreviation and score
   positions, font size and weight, separator lines, timeout bars, clock,
   quarter, down and distance, borders, gradients, spacing, padding,
   alignment, shadows, opacity. Not "inspired by", not "similar to".
   - If the reference has a thin separator line, draw it.
   - If a timeout bar runs to the end of a panel, make it run to the end.
   - If there is no gap beside a logo, do not add one.
   - Both teams follow identical sizing and alignment rules unless the
     reference shows otherwise.
   - Do not "improve" the design.
2. **Never invent a network package** (NBC, FOX, ESPN, ...) without a reference
   image from the owner.
3. **Never hardcode game data.** Team names, scores, clock and so on come from
   the game. Sample data is only for previewing in the editor.
4. **No network-specific logic in the game.** Everything visual lives in the
   package's HTML/CSS/JS.
5. **Do not touch** the existing CBS and FOX packages (`NFL on CBS`,
   `NFL on FOX`); they use the older JSON format and still work.

---

## 2. What to deliver

A folder in `mods/presentations/` (the folder name is the package id):

```
mods/presentations/My_Scorebug/
    mod.json          metadata only (name, entry page, canvas)
    scorebug.html     structure
    scorebug.css      appearance, layout, animations
    scorebug.js       binds live data, reacts to events
    layout.css        empty; the editor writes visual adjustments here
    assets/           images (network logo, textures, backgrounds)
    fonts/            font files, if the design needs them
```

### mod.json

```json
{
  "schema": 1,
  "id": "my_scorebug",
  "name": "My Scorebug",
  "type": "html",
  "version": "1.0.0",
  "author": "",
  "description": "",
  "enabled": true,
  "entry": "scorebug.html",
  "canvas": { "width": 1920, "height": 1080, "fit": "broadcast_safe_16x9" },
  "dependencies": [],
  "assets": {},
  "compatibility": { "game": "NFL2K5-PC", "presentation_api": 1 }
}
```

JSON is metadata only. Do not describe the graphic in JSON.

### scorebug.html skeleton

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<link rel="stylesheet" href="scorebug.css">
<link rel="stylesheet" href="layout.css">   <!-- must come after scorebug.css -->
</head>
<body>
<div id="canvas">
  <div id="bug" data-element="bug">
    <img class="logo" data-element="away-logo" alt="">
    <span data-element="away-abbr"></span>
    <span data-element="away-score"></span>
    <!-- ... -->
  </div>
</div>
<script src="../_runtime/nfl2k5-presentation.js"></script>  <!-- required, before your script -->
<script src="scorebug.js"></script>
</body>
</html>
```

---

## 3. The canvas

- Design on a **1920x1080** canvas. The game scales it to any resolution
  (720p to 4K) and keeps it in the 16:9 broadcast area on 4:3, 21:9 and 32:9
  screens. Never use viewport units or window sizes; use px on the 1920x1080
  canvas.
- The page background **must be transparent**:
  `html, body { margin: 0; background: transparent; overflow: hidden; }`.
  Anything you do not draw shows the game.
- Make `#canvas` exactly `width: 1920px; height: 1080px; position: relative`
  and place elements with `position: absolute; left/top` in canvas pixels.
  This makes reference matching and editor dragging exact.
- To match a 1280x720 screenshot, multiply every measurement by 1.5.
- Give every element the user may want to adjust a **`data-element="name"`**
  attribute (unique names like `away-score`, `clock`, `timeouts-home`). The
  editor lists, selects and saves by these names.

---

## 4. Live data (state)

```js
NFL2K5.onState(function (state, previous) { /* update the DOM */ });
```

Called on every change (and at least twice a second). Main fields:

| Field | Example | Notes |
|---|---|---|
| `away`, `home` | object | `abbreviation` ("DAL"), `city`, `name`, `logo` (image URL), `record`, `score`, `timeouts` (0-3), `primaryColor`, `secondaryColor` |
| `quarter` | 2 | 0 pregame, 1-4, 5+ overtime |
| `gameClock` | 425.3 | seconds left; format it yourself (`7:06`), round up |
| `playClock` | 17 | -1 when there is none: hide your play-clock element |
| `down`, `distance` | 3, 7 | down 0 / distance null outside scrimmage |
| `downDistanceText` | "3RD & 7" | also "1ST & GOAL", "KICKOFF", "PAT", "" |
| `possession` | "home" | "away", "home" or null |
| `gameStatus` | "in_progress" | "inactive", "pregame", "in_progress", "final" |
| `context.scorebugVisible` | true | **show the bug only when true** (false during play selection, pause, cutscenes) |
| `context.playSelection` | false | play-call screen is up |
| `context.flag` | false | a penalty flag is down |
| `redZone`, `ballPosition.side` | null | not available yet; do not guess them |

Rules:
- Use `state.away.logo` / `state.home.logo` for team logos. Never bundle team
  logos inside the package; logos come from `mods/teams`.
- Use team colours from the state when the design is team-coloured.
- Handle missing values (empty abbreviation, null distance) without showing
  "undefined" or "null".

---

## 5. Events (for animations)

```js
NFL2K5.onEvent(function (event) {
  // event = { name: "TOUCHDOWN", team: "home", time: 1234.5, ... }
});
```

| Event | When | Extra fields |
|---|---|---|
| `GAME_STARTED` | first live state of a match | |
| `TOUCHDOWN`, `FIELD_GOAL`, `EXTRA_POINT`, `TWO_POINT_CONVERSION`, `SAFETY` | scoring play | `team` = scoring team |
| `SCORE_CHANGED` | any score change | `points`, `score` |
| `FIRST_DOWN` | new first down, same possession | |
| `DOWN_CHANGED` | down changes | `down` |
| `POSSESSION_CHANGED` | possession changes | `team` = new offense |
| `INTERCEPTION`, `FUMBLE`, `TURNOVER` | turnovers | |
| `PENALTY` | flag | |
| `TIMEOUT` | timeout used | `team` |
| `TWO_MINUTE_WARNING` | 2:00 in Q2/Q4 | |
| `QUARTER_ENDED`, `QUARTER_STARTED` | period change | `quarter` |
| `HALFTIME`, `OVERTIME`, `GAME_ENDED` | | |
| `INJURY`, `REPLAY_STARTED` | | |

Defined but not sent yet: `DRIVE_STARTED`, `PLAY_STARTED`, `PLAY_ENDED`,
`SACK`, `REPLAY_ENDED`. Handle them if useful; they will start arriving later.
Ignore event names you do not know.

---

## 6. Animations and transitions

There is no timeline editor. Animations are written in CSS (keyframes and
transitions) and started from JavaScript when an event or state change
arrives. Anything Chromium/Edge can animate works: transforms, opacity,
clip-path, masks, gradients, filters, SVG, canvas, WebGL, video.

### Show / hide transition (required)

```css
#bug { transition: transform 0.35s ease, opacity 0.35s ease; }
#bug.hidden { opacity: 0; transform: translateY(40px); }
```
```js
NFL2K5.onState(function (s) {
  document.getElementById('bug').classList.toggle('hidden', !(s.context && s.context.scorebugVisible));
});
```

### Event animation with a queue

Events can arrive close together (`SCORE_CHANGED` + `TOUCHDOWN`, then
`EXTRA_POINT`). Queue them so one finishes before the next starts, and keep
the live data updating underneath:

```js
var queue = [], playing = false;
var DURATION = { TOUCHDOWN: 4000, FIELD_GOAL: 3000, INTERCEPTION: 3000, PENALTY: 3000, FIRST_DOWN: 1800 };

NFL2K5.onEvent(function (e) {
  if (!DURATION[e.name]) return;           // only events this design animates
  queue.push(e);
  if (!playing) next();
});

function next() {
  var e = queue.shift();
  if (!e) { playing = false; return; }
  playing = true;
  var el = document.getElementById('banner');
  el.className = '';                        // restart even if the same class repeats
  void el.offsetWidth;
  el.className = 'show ' + e.name.toLowerCase() + ' ' + (e.team || '');
  setTimeout(next, DURATION[e.name]);
}
```
```css
#banner { opacity: 0; }
#banner.show { animation: banner-in-out 4s ease forwards; }
@keyframes banner-in-out {
  0%   { opacity: 0; transform: scaleX(0); }
  10%  { opacity: 1; transform: scaleX(1); }
  85%  { opacity: 1; }
  100% { opacity: 0; }
}
```

### Value-change transitions

Compare with `previous` in `onState` (for example score went up, clock
stopped) and add a class for a short effect such as a score flash or a
possession marker slide. Restart the animation by removing the class, reading
`offsetWidth`, then adding it again.

### Animation rules

- Animate `transform` and `opacity` where possible (smooth at 4K).
- Animations must end in a correct state even if another event interrupts.
- Do not hide live score or clock changes behind long animations.
- Do not mark animated properties `!important` in CSS; the editor's
  `layout.css` uses `!important` for the properties the user adjusts, which
  would freeze those properties if they are also animated.

---

## 7. Images and fonts

- **Package images** (network logo, panel textures): put them in `assets/` and
  use relative paths: `<img src="assets/network.png">` or
  `background-image: url("assets/panel.png")`.
- **Team logos:** only from `state.away.logo` / `state.home.logo`.
- **Fonts:** put the files in `fonts/` and load them with `@font-face`:
  ```css
  @font-face { font-family: "BugFont"; src: url("fonts/BugFont-Bold.ttf"); font-weight: 700; }
  ```
  Only use fonts the owner supplied or that are licensed for this use.
- No internet access: do not link CDNs, web fonts or remote images. Everything
  must be inside the package (or `mods/teams` for logos).
- Players will later have portraits served by player id that follow the
  player's current team. Do not bundle player photos in a scorebug package.

---

## 8. Presentation Studio (the editor)

Start it with:

```
powershell -ExecutionPolicy Bypass -File tools\presentation-studio\serve.ps1
```

or double-click `Presentation Studio.cmd` in the project folder. It opens
`http://127.0.0.1:8735/` in the browser (keep its window open while editing).
If it is already running, starting it again just opens the page.

**Image import options in the editor:**

| Option | Where | What it does |
|---|---|---|
| **Reference image** | right panel, "Reference image" | Loads a TV screenshot over or under the canvas: show/hide, opacity 0-100%, lock, move (drag), scale (slider or mouse wheel). A full-frame screenshot fills the 1920x1080 canvas automatically. Not saved into the package; it is for comparison only. |
| **Replace image / background asset** | right panel, with an element selected | Copies the chosen image into the package's `assets/` folder and applies it to the selected element (an `<img>` gets the new picture, other elements get it as background). Saved in `layout.css`. |
| **Game screenshot background** | left panel, "Background" | Shows a game screenshot behind the canvas, to judge how the bug looks over gameplay. Not saved into the package. |
| **Import HTML…** | top bar | Turns a folder or loose files (HTML, CSS, JS, images, fonts) into a new package. Adds the runtime script and `layout.css` link to the entry page and can make the background transparent. |

**Other editing features:**
- Click an element to select it. A box with 8 handles appears: drag inside to
  move, drag an edge or corner to resize, Shift keeps proportions, arrow keys
  nudge 1 px (Shift: 10 px), Ctrl+Z undoes.
- Properties panel: X, Y, width, height, font size/family/weight, colour,
  alignment, padding, letter spacing, gap, z-index, opacity, background,
  border, radius, shadow, visibility, display.
- **Sample game data** (left panel) drives the page like the game does.
  **Send event** buttons trigger each event, so animations can be previewed
  without the game.
- **Code view** edits the package's HTML/CSS/JS files directly; saving reloads
  the preview.
- **Save layout** writes the visual edits to `layout.css`.

---

## 9. Testing in the game

1. Start the game, go to **Coach Match Up > Presentation**, choose the
   package, start a match.
2. Optional switches (set before starting `build\Release\NFL2K5.exe`):
   - `NFL2K5_PRES_LOG=1`: logs every state and event sent to the page, and
     `NFL2K5.log("text")` lines from the page (`[HTMLPRES] page: text`).
   - `NFL2K5_PRES_EVENT=TOUCHDOWN,FIRST_DOWN,INTERCEPTION,PENALTY,QUARTER_STARTED`:
     sends those events every 5 seconds during a match to test animations.
   - `NFL2K5_HTML_DEVTOOLS=1`: allows the browser developer tools.
3. Check:
   - the background is transparent;
   - every value updates (score, clock, quarter, down and distance, timeouts,
     possession);
   - the bug hides on the play-call screen and in pause menus;
   - each animation plays once and ends cleanly;
   - it looks right at 1080p, 1440p and on an ultrawide screen.

---

## 10. Common mistake: a standalone demo page

Do not deliver a demo page: a dark full-window background, the bug centred
with `min-height: 100vh` / `place-items: center`, demo buttons ("Swap Teams",
"Away TD"), and hardcoded sample teams and scores. It imports fine but shows
fixed values and covers the game. The game page must be the transparent
1920x1080 canvas described above, with every value coming from
`NFL2K5.onState`. Presentation Studio warns "not connected to game data" when
a page never calls `NFL2K5.onState`.

Also make sure no two elements overlap by accident (for example score text
running under a centre clock pod); check every value with two-digit scores.

## 11. Checklist before handing over a package

- [ ] Folder in `mods/presentations/`, `mod.json` with `"type": "html"`
- [ ] Runtime script included before the package's own script
- [ ] `layout.css` linked after the main CSS, file present
- [ ] Transparent page background, 1920x1080 canvas, absolute positioning
- [ ] `data-element` names on every adjustable element
- [ ] All data from `NFL2K5.onState`, nothing hardcoded; no demo background or demo buttons
- [ ] Two-digit scores fit and nothing overlaps by accident
- [ ] Hides when `context.scorebugVisible` is false
- [ ] Animations queued, interruption-safe, ending in a correct state
- [ ] No remote resources; images in `assets/`, fonts in `fonts/`
- [ ] Measurements match the reference screenshot (checked with the
      reference image at reduced opacity in Presentation Studio)

# Sandbox browser contract

The browser is a separate transparent `PresentationHost`. It does not change,
replace, or load assets from the chosen ESPN/FOX/CBS/NBC package.

`nfl2k5_sandbox_ui_tick()` and `nfl2k5_sandbox_ui_surface()` run on the presenter
thread. The returned surface contains premultiplied BGRA at 1920×1080. The core
supplies `nfl2k5_sandbox_json()`, `nfl2k5_sandbox_active()`, and
`nfl2k5_sandbox_ui_visible()`; `ui_visible` remains true for the closed-browser
help/reticle and false during photo mode. Shutdown calls
`nfl2k5_sandbox_ui_shutdown()` before renderer shutdown.

The presenter also calls `nfl2k5_sandbox_ui_viewport(x, y, width, height)`
with the actual game-picture bounds before `ui_tick()`. This includes
letterboxing. Native mouse-to-gamepad translation must return early while
`nfl2k5_sandbox_ui_pointer_active()` is true, preventing one click from also
becoming the game's A button.

State fields:

- `active`, `menuOpen`, `photoMode`, `paused`, `speed` (percent, 0–100),
  `hideHints`, `reticle`. `speedPercent` can explicitly override `speed`.
- `grabMode: true` and `selectedPlayer` (name) replace closed-browser camera
  hints with **PHOTO POSING** controls: left stick moves the visual pose,
  triggers change height, right stick turns, A/Cross drops and B/Circle cancels.
  Menu/Options opens the browser and commits the pose. This is a render-only
  photo tool; it does not spawn a player or move the simulation/collision body.
- `generation`: the core's current scene generation. Every page command
  carries it; the core refuses actions from an old scene.
- `style`: `xbox` or `playstation`; `stadium`/`location`, `message`.
- `tab`: zero-based Teams/Players/Tools/Scene; `selection`: row index.
- `rows`: the core's authoritative current category. Each row has `label`,
  `value`, `description`, `enabled`, optional `reason`/`note`, `command`, and
  optional integer `commandValue`. Numeric fields can supply `min/max/step`.
- `adjustable: true` on a row enables relative `−` / `+` controls. They send
  `adjust` with −1/+1, leaving five-second/one-yard increments with the core.
  This works for formatted values such as `1:25` and `25%`; the UI does not
  parse those displays as numbers. Existing explicit `min/max/step` rows
  retain their absolute-value behavior when `adjustable` is absent.
- Optional `details` is an object containing up to six short label/value pairs.
- `phase: "setup"` (or `screen: "stadiums"`) changes the browser to stadium
  selection. `stadiums`, `teams`, `players`, `tools`, and `scene` arrays are
  fallbacks when `rows` is absent.

Rows without a valid command are enabled only when the core explicitly sets
`enabled: true`; those use the generic `select` command. Unsupported rows are
disabled and display the core's reason. All labels use `textContent`.

Navigation commands are `{cmd: "navigate", value: ±N, generation: N}`, `tab` (relative change),
`adjust` (±1), `select`, and `back`. An explicit row command sends its bounded
integer value. Core validation and its bounded command queue remain the final
authority; the UI thread never reads or writes guest memory.

The Sandbox host implements its own bounded pointer bridge using the existing
page-event API; the shared offscreen WebView2 interface is unchanged. It reads
the cursor only while the exact game window is foreground and the browser is
open. Coordinates are mapped through the current game-picture rectangle.
Movement events are limited to 30 Hz; button edges are delivered immediately.
On opening, refocusing, or entering another scene, buttons must first be
released. The page rejects stale scene generations and duplicate/out-of-order
events, hit-tests its actual DOM, and uses the existing core command queue.
Rows and unchanged category buttons retain their DOM nodes across camera
snapshots. Display values update in place; event handlers resolve the newest
authoritative row so rapidly updating camera values do not leave stale action
arguments or repeatedly discard pointer targets.

Left-click selects a row/button; double-click activates a row; right-click
backs out; middle-click closes the browser. Page arrows browse long lists.
There is no mouse-wheel forwarding yet. The pointer is drawn by the HTML so it
remains visible when the game hides the operating-system cursor.

Keyboard/controller mappings remain owned by the existing controls system.
Q and C were not silently reassigned: the defaults already use Q for LT and
C for X. Menu/Options opens the browser, D-pad/arrows browse and adjust, and
the existing confirm/cancel mappings remain intact.

## In-game controls

Once the field scene is loaded, release the button used to start the game,
then press Menu (Xbox), Options (PlayStation), or the default Enter key once
to open/close the browser. Holding Enter is not necessary.

- LB/RB or L1/R1 switch categories while the browser is open.
- D-pad up/down selects an item; left/right changes an adjustable value.
- A/Cross selects; B/Circle goes back.
- With the default keyboard mapping, arrows browse/adjust, Z selects,
  X goes back, and Enter toggles the browser. User remapping still applies.
- Close the browser to use free camera controls when enabled: left stick
  moves, right stick looks, triggers descend/rise, and bumpers zoom.
- View/Share toggles pause; X/Square toggles pause and Y/Triangle steps one
  update while the free camera owns input outside the browser.

The roster list and each tool's enabled state come from the actual core
snapshot. Player tools remain unavailable until the core supplies their live
actions. The UI does not invent enabled spawn, pose, or prop controls.

Validation:

```
node --check mods/sandbox/ui/sandbox.js
node mods/sandbox/ui/verify.js
```

These tests exercise the real UI with a DOM double, including pointer hit
tests, scaling, disabled actions, and stale/duplicate events. They do not
confirm native HWND/focus polling, game rendering, WebView2 capture, controller
hardware, or gameplay behavior. Those require the root's integrated build and
live game test.

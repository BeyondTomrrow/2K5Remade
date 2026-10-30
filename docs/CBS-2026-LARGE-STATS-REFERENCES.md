# CBS large statistical graphics — measured reference

Date: 2026-10-17. Scope: the current compact-Eyebar-era CBS NFL package, researched and built as HTML/CSS/JavaScript without launching or building NFL2K5.exe.

## Verified full-screen comparison

Primary source: the official NFL upload [Epic Thanksgiving Game! Kansas City Chiefs vs. Dallas Cowboys FULL GAME | NFL 2025 Season Week 13](https://www.youtube.com/watch?v=PLR04ljZ5DI&t=307s). This is the **2025 Thanksgiving broadcast**, even though its official full-game upload is more recent. The scorebug and broadcast graphics are visually the current CBS package. This is stronger evidence than extrapolating a generic comparison graphic from the scorebug.

The observed graphic is **WALK-OFF WINS / WEEK 12 GAMES**. Patrick Mahomes and Dak Prescott occupy opposite edges. The central table compares their teams' previous-week games, using these actual reference values:

| Row | Chiefs column | Cowboys column |
| --- | --- | --- |
| Column heading | KC vs IND | DAL vs PHI |
| LARGEST DEFICIT | 11 PTS | 21 PTS, highlighted red |
| TIME LEADING | 0:00 | 0:00 |
| RESULT | W, 23-20 (OT) | W, 24-21 |

The bottom footnote reads “T-LARGEST COMEBACK WIN IN TEAM HISTORY,” with a small red marker. These are reference observations, **not runtime default statistics**. The implementation accepts event values and does not bake the source teams, players, values or labels into a texture.

Persistent local reference captures:

- [Full reference frame](../outputs/cbs-2026/reference/measurement/cbs-walkoff-comparison-full.png).
- [40-second contact sheet, 2 frames/second](../outputs/cbs-2026/reference/measurement/cbs-walkoff-comparison-contact.jpg).
- [Entrance contact sheet, 30 frames/second](../outputs/cbs-2026/reference/measurement/cbs-walkoff-comparison-entry-fine.jpg), local 12.73–14.13s.
- [Exit contact sheet, 30 frames/second](../outputs/cbs-2026/reference/measurement/cbs-walkoff-comparison-exit-fine.jpg), local 27.50–28.20s.
- [Bounded reference clip](../outputs/cbs-2026/reference/measurement/cbs-walkoff-comparison-295-335.mp4), source 295–335s, H.264 640×360. Local time +295 approximates source time; frame sampling/section boundaries limit precision to a few frames.

## Measured geometry

All positions below were observed at **640×360**, and are multiplied by three in the package's 1920×1080 design canvas.

| Element | Observed measurement | 1920×1080 implementation |
| --- | --- | --- |
| Central black panel | x134, y52, w372, h266 | x402, y156, w1116, h798 |
| Title | centered, near y54–80 | condensed display hierarchy, 84px nominal |
| Subtitle | centered, near y87–100 | 38px nominal, spaced capitals |
| Column headings | near y109 | two team/name or matchup headings with small real logo assets |
| Row centers | approximately y145, 203, 260 | approximately y145, 203, 260 after dividing canvas coordinates by3 |
| Row labels | center column, regular uppercase, two-line wrap | proportional Arial fallback; 43px nominal |
| Row values | left/right, tall condensed white | existing All Pro heavy asset, horizontally condensed |
| Left portrait | roughly x20–132, y92–360 | replaceable transparent portrait, bottom anchored |
| Right portrait | roughly x505–640, y92–360 | replaceable transparent portrait, bottom anchored |
| Team frame | red outer left; navy outer right; enormous oval center opening | SVG mask, ellipse center960,540; radii966,558 |
| Oval top intersections | approximately x240 and x400 | approximately x720 and x1200 |
| Team frame accents | thin gold left; silver right | editable SVG paths; actual logo image slots |
| Footnote | centered, near y304 | 34px nominal |

The source includes a **separate stadium camera plate**, original player cutouts and dimensional team-logo geometry. These assets are not included in the HTML package. Its center currently reveals the host game/preview background through a translucent black table. Missing portraits are omitted. No player photo, fake silhouette, stadium or statistics are generated to disguise unavailable assets.

## Observed motion

- Approximately **307.80s**: the first red team-logo plane enters from the left while the normal Eyebar is leaving.
- Approximately **308.0–308.3s**: oversized opposing dimensional team-logo surfaces fill the frame.
- Approximately **308.3–308.6s**: an oval opening exposes the stadium plate. The table reveals from its bottom toward the title.
- Approximately **308.6–309.03s**: the two portraits reveal last, briefly desaturated/flat before their full-color appearance settles.
- Approximately **309.03–322.55s**: the completed comparison holds. There is no independent number-counting animation or rolling statistics demonstrated.
- Approximately **322.55–323.1s**: the graphic uncovers from top to bottom, then the stadium plate dissolves into the live player camera. The Eyebar returns afterward.

The editable timing contract is **1250ms entrance**, **550ms exit**. These reflect sampled broadcast timing, not original production project keyframes. The split logo planes are CSS/image layers, not the unavailable original 3D meshes. Treat material/mesh fidelity and exact source font matching as remaining work; the implementation is not proof of pixel identity.

## Renderer contract

Files: [stats-large.js](../mods/presentations/CBS_2026/stats-large.js), [stats-large.css](../mods/presentations/CBS_2026/stats-large.css).

`window.CBSLargeStats.render(host, event, state)` returns whether it rendered a valid `PLAYER_COMPARISON`. It sets no timers, listeners or game state. The presentation manager owns cancellation, duration and `entering` / `live` / `leaving` classes on the `cbs-stat-comparison` host.

```js
{
  name: 'PLAYER_COMPARISON',
  title: 'WALK-OFF WINS',
  subtitle: 'WEEK 12 GAMES',
  players: [
    { team: 'away', label: 'KC vs IND', player: { name: 'P. Mahomes', portrait: 'assets/portraits/mahomes.png' } },
    { team: 'home', label: 'DAL vs PHI', player: { name: 'D. Prescott', portrait: 'assets/portraits/prescott.png' } }
  ],
  rows: [
    { label: 'LARGEST DEFICIT', values: ['11 PTS', '21 PTS'], highlightSide: 1 },
    { label: 'TIME LEADING', values: ['0:00', '0:00'] },
    { label: 'RESULT', values: ['W, 23-20 (OT)', 'W, 24-21'] }
  ],
  footnote: 'T-LARGEST COMEBACK WIN IN TEAM HISTORY'
}
```

This example reproduces the observed **reference**, not a claim that these players or values are loaded from the running NFL2K5 game. Portrait paths are replaceable slots and these example files need not exist. The renderer gets team colors/logos from `state.away` / `state.home` or the package's existing real team-logo assets. Column labels default to provided player names when an explicit matchup label is absent.

Three rows are the observed layout. One through six rows are supported for adaptation; the extra row counts are not individually verified CBS layouts. Numeric zero remains zero; missing values display an unavailable dash. Rendering uses DOM text nodes and rejects executable image protocols. No baked scores or player names, media playback, external scheduling library or native-game changes were added.

## Verification and limits

- JavaScript syntax check passed.
- Isolated renderer contract checks passed for zero statistics, missing values, missing portraits, text markup handling, executable asset protocol rejection, six rows, and rejection of seven rows.
- Visual verification of the **integrated** preview remains the parent task's responsibility. Do not claim in-game readiness from the renderer's existence.
- The early Dak Prescott and Andy Reid vertical cards are being handled separately by the small-insert agent. This full-screen table is a distinct verified layout.
- The game-wide storyboard scan found many player cards and normal play; it does not prove that every current CBS team-stat, drive-summary or comparison variation is implemented. Those should be added from further observed references rather than invented.

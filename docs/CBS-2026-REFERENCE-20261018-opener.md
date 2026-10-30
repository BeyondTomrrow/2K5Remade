# CBS compact team-table opener

October18,2026. This pass reconstructs the missing traveling-logo/contour
handoff preceding the compact CBS team table. It changes only
`mods/presentations/CBS_2026/stats-team.js` and `stats-team.css`.

## Inspected source

Official NFL full game, **Chiefs at Cowboys, Thanksgiving2025**:
https://www.youtube.com/watch?v=PLR04ljZ5DI&t=1198s

The existing bounded local source is
`outputs/cbs-2026/reference/extended-20261018/team-comparison.mp4`, covering
approximately1188–1215s. It was inspected at30fps through
`team-motion30.jpg`, with full frames `team-opener-start.png`,
`team-opener-sweep.png` and `team-opener-contours.png` in that folder.
The source feed is visibly CBS; its2025 game date has not been relabeled2026.

## Measured timeline

Start is1198.80s. Source sampling uncertainty is approximately33ms; positions
have approximately2–3 source-pixel uncertainty at640×360.

| Source time | Relative | Observed action |
|---|---:|---|
|1198.80 |0ms | Ordinary Eyebar team logos begin turning away |
|1198.87–1199.00 |70–200ms | Eyebar contracts horizontally into its center and clears |
|1199.07–1199.17 |270–370ms | Large cropped matchup-logo planes travel from the right across the old scorebug footprint |
|1199.20–1199.43 |400–630ms | Full-color planes resolve into pale logo contours, continuing left before disappearing |
|1199.47–1199.60 |670–800ms | Table identity rows reveal from left to right |
|1199.60–1199.87 |800–1070ms | RUSH, PASS and TOTAL columns reveal sequentially |
|1199.90–1200.03 |1100–1230ms | Raised YARDS tab appears last |
|1200.03–1203.10 |1230ms onward | Stable table hold |
|1203.13 |— | Camera cuts directly back to the ordinary Eyebar; no animated reverse exit is demonstrated |

At1199.10s, the colored-logo band spans approximately x182–461,y291–343 at
640×360: W279,H52. The left logo occupies approximately x182–322 and the
right x319–461. Their top/bottom edges are clipped by the horizontal band.
The implementation scales those measurements by3 to the1920×1080 canvas:
band x546,y873,W837,H156; each image plane is420px wide.

## Implementation and integration contract

- `CBSTeamStats.enterMs` is now1230; `exitMs` remains0 because the source exit
  is a camera cut. The controller owns event lifetime and cancellation.
- `CBSTeamStats.timing` exposes the200ms Eyebar exit,270ms logo start,670ms
  table start and560ms table reveal.
- The controller's temporary `#canvas.cbs-team-opening` class exposes the old
  Eyebar for its initial turn/contraction, overriding ordinary large-card
  suppression. Removing the class on cancellation or completed entrance
  restores the usual presentation ownership.
- Actual away/home logo image assets populate both traveling planes. The
  same validated asset/fallback selection used by the table supplies them.
- SVG filters derive silhouette edges and internal color-boundary contours
  from those actual images. No generic star, circle or hand-drawn team logo
  replaces a different team's artwork.
- The existing table/data validation remains intact: unknown fields remain
  unavailable, valid zeroes survive, and neither totals nor possession time
  are invented by the renderer.
- The stable table geometry, columns and typography remain as before. Its
 560ms identity/column/title reveal now starts after the measured670ms
  traveling-logo segment.
- Reduced motion omits the opener and uses the immediately readable table;
  the production controller retains responsibility for its reduced-motion
  timing override.

## Verification and limits

The standalone WebView2 renderer captured15 deterministic package frames
at2499–3730ms in the `stats-team-yardage` scene (event begins2500ms).
`opener-captures/contact.jpg` visibly verifies old-scorebug turn/contraction,
colored logos, asset-derived contours, sequential labels/numbers and the
final raised tab. JavaScript syntax checking passed. This was **not** a
native build or game run.

The reference logo surfaces are dimensional production artwork. Their
original mesh, material and depth animation are unavailable. This
implementation uses clipped image planes and image-derived contours;
horizontal positions and projection between the measured video frames are
interpolations. It does not claim an exact original3D mesh, production
keyframes or pixel identity. Exit animation was deliberately not invented.

Before editing, copies of both source files were preserved in
`outputs/cbs-2026/reference/extended-20261018/opener-backup/`.


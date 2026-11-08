# CBS 2026 HTML scorebug: reference and implementation

October 16, 2026. Package: `mods/presentations/CBS_2026/`. This adds a selectable HTML package alongside the existing JSON CBS presentation; it does not change the active game configuration.

## Sources inspected

1. [2026 live CBS crop, Cardinals/Giants](https://www.colorwaysports.com/images/posts/nfl-broadcasts-ranked-2026/cbs-scorebug-2026.jpg), published within [ColorWay's October 13, 2026 broadcast review](https://www.colorwaysports.com/stories/nfl-broadcasts-ranked-2026). Image inspected directly, not inferred from its description. The captured score/state is source evidence only, not a current fixture.
2. [NewscastStudio's September 2025 CBS Eyebar report](https://www.newscaststudio.com/2025/09/12/cbs-nfl-score-bug-update-2025/), including the [full-frame Steelers/Jets still](https://www.newscaststudio.com/wp-content/uploads/2025/09/cbs-sports-eyebar-2025.jpg) and [Lions/Packers penalty still](https://www.newscaststudio.com/wp-content/uploads/2025/09/cbs-sports-eyebar-2025-colors.jpg). The compact design was introduced in 2025 and appears in the inspected 2026 crop. The old full-width CBS bar is a different layout.
3. Embedded entry/live GIFs: [short entry](https://media0.giphy.com/media/vHihfmJf0n1OVtpcSW/giphy.gif), [longer entry/live sequence](https://media0.giphy.com/media/zehBOog4fK6DV0kpUo/giphy.gif). Frame samples show the brand/eye and team-logo strip expanding, holding, then resolving into the score layout. The second file was initially described as scoring, but direct inspection disproved that; no scoring-motion claim is based on it.
4. [Official CBS Sports](https://www.cbssports.com/) navigation SVG supplies the actual CBS eye and adjacent CBS SPORTS wordmark paths. Their equal rendered heights and CSS gap were retained in `assets/branding/cbs-sports.svg`; no lettering was AI-redrawn. Normalized source viewBox is 217.666667 by 24. Original geometry retained, white currentColor applied.

Reference images/GIFs and sampled comparison frames are saved under `outputs/cbs-2026/reference/`; they are not used as baked scorebug textures.

## Geometry

The 900 by 254 reference crop contains the bug at approximately x48-845, y111-224. Its left/center/right widths are approximately 255/286/255. The full-frame reference establishes bottom-center placement and a width close to 44 percent of the picture.

The implementation uses a centered 1920 by 1080 design: bug x540, y927, width840, height122; columns270/300/270. Logos occupy the outer team regions; scores sit inward. Records sit in the lower outer corners, three timeout marks below each score. The lower middle holds quarter, divider, game clock and red-outlined play clock. The upper middle displays down/distance on possession color, a yellow FLAG state, or the CBS SPORTS asset during kickoffs/punts. Restrained top/bottom edges follow the source; a subtle dotted panel texture was requested after the initial comparison. Extra viewport width pads the design symmetrically.

Typography is currently fitted Arial with separate ordinal sizing. Local supplied font candidates are included, but no original CBS broadcast font was verified; the preview must not be described as an exact typography match. Entry timings are reconstructed from measured GIF phases, not certified production keyframes. The initial generic scoring/timeout center messages and score bounce have been replaced by the reference-backed sequences and explicit gaps documented in `CBS-2026-MOTION.md`.

## Runtime and preview

`scorebug.html`, `scorebug.css` and `scorebug.js` use the existing Presentation API v1 runtime. State owns teams, scores, records, clocks, possession and timeouts. Events own entrance/penalty/temporary messages. Initial/inactive/menu/paused states hide the graphic and cancel pending timers. Zeroes remain valid; unknown or invalid scores/clocks/timeouts stay unavailable. Logo failures use the supplied team asset or abbreviation fallback.

`preview.html` and `preview.js` provide actual package rendering with editable sample teams/scores/clocks, scene controls, pause/menu and 16:9/ultrawide review. Default sample is Dallas 21-New England 14. `preview-scenes.js` enables deterministic capture only on explicit preview URLs. `preview-clock.js` reuses the existing standalone preview clock; ordinary game page timers remain native browser timers.

## Verification

Eleven checks through the actual browser controls passed: Dallas's 21-14 default, low-clock warning, kickoff branding, FLAG, FINAL, pause/menu hiding, a zero score, unknown score, invalid clock, actual team-logo change, and centered ultrawide padding. Browser/renderer captures inspect actual pixels in addition to these DOM checks.

The existing standalone WebView2 renderer is used for PNG/MP4 exports, via `tools/cbs-scorebug/render-preview.ps1`. No native build or game launch/control/attachment was performed; Claude's running game and the selected package were left alone. Native timing remains to be verified in a separately authorized game test.

### Visual refinement requested October 16

Added compact neutral-black shadows behind the live and entrance logos, darkened the team panels and possession band slightly, and added a restrained red glow around the play-clock border. Shadow spread stays close to the logo silhouette; no colored logo halo was added. Standalone review artifacts were refreshed; no game launch or native change.

Replaced the fine stripe overlay with the requested subtle dot texture across the team panels, upper center band and entrance strip. The darker colors, logo shadows and play-clock glow remain in place.

### Motion expansion, October 16

Added a package-local motion manager, measured entrance geometry, editable touchdown layers and +points return, FLAG reveal, and corrected FINAL state. Added gallery event controls, cancellation/late-event regression checks and an expanded standalone export. Full details and missing-reference limits: [CBS-2026-MOTION.md](CBS-2026-MOTION.md). No native code, audio, game build or running game was changed.


### HD calibration and native state follow-up, October 18

A bounded1080p official CBS Chiefs?Bills2025 excerpt supersedes the earlier inferred outer footprint: live Eyebar approximately x552?1368/y911?1029 on1920?1080. The internal840?122 grid/motion is uniformly scaled816/840 and anchored552/911. Attached compact player cards share this mapping. Separate tables retain their own measured positions. Local font specimens establish All-Pro Heavy as a closer fitted score-numeral/END-label face than Arial Bold; this does not identify the exact CBS font. The center CBS logo uses unchanged official paths with the observed gray/silver treatment. Source/frame details and limitations: CBS-REGULAR-PERIOD-REFERENCES-20261018.md and CBS-2026-FOLLOWUP-VISUAL-20261018.md.

Native FINAL uses the title's actual game-over flow, and the source-backed ordinary period label follows context.periodEnding. Stat inserts are cancelled/suppressed during that state. The final package is0.5.1. Native D3D11 and host/lifecycle tests are recorded in the follow-up document and PROJECT_STATUS.md; the earlier standalone-only verification describes the original stage.

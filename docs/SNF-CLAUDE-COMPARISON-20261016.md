# SNF_Claude scorebug: build notes and comparison

**NFL2K5-PC · October 16, 2026**

`mods/presentations/SNF_Claude/` is a second primetime presentation package, started as a copy of `NBC_SNF` (the earlier ChatGPT-built package described in [SNF-SCOREBUG-WRITEUP.md](SNF-SCOREBUG-WRITEUP.md)) and rebuilt around a different approach to the compact scorebug. Both packages target the same 2022 designer layout: team glass panels, circular clock pod, rail with timeout marks, possession tab and hanging play clock.

## What is different

| | NBC_SNF (ChatGPT) | SNF_Claude |
| --- | --- | --- |
| Pod, rim, rail | Raster crops cut from the reference image (`assets/plates/pod.png`, `rail.png`) | CSS: radial glass, silver rim, hairline dividers, grain overlay |
| Team panels | Reference-derived alpha masks with live team color applied through them | Translucent team glass (`color-mix` + `backdrop-filter`), grain, vapor-light glow, drifting sheen |
| Logo windows | Two hand-cropped plates for LAR/CIN, generic window for everyone else | Frosted glass, S-curve inner edge with silver stroke, ghosted mark behind the crisp mark, same treatment for all 32 teams |
| Network mark | Hand-painted peacock PNG keyed through SVG filters | Neutral original badge (`network-badge.svg`); no network trademark |
| Wordmark | Traced "Sunday Night Football" SVG | Neutral `primetime-wordmark.svg` |
| Scaling | Plates are 1x crops, soften at `--bug-scale` 1.45 and above | Fully vector, crisp at any scale |
| Insert/scene/transition code | shared | shared (`inserts.css` holds the insert styles that were in `scorebug.css`) |

The measured text grid (`SPEC` in `scorebug.js`: cap heights, anchors and per-glyph clock advances) is kept, so scores, clock, quarter, abbreviations and records land where the earlier measurements put them.

### The material system

The designer's own component board names five parts: glass, texture and contrast, the curve, vapor lights and motion theory. The compact bug implements four of them directly:

- **Glass:** panels and logo windows are translucent and blur what is behind them (`backdrop-filter: blur()`), with a top specular edge.
- **Texture and contrast:** a procedural SVG turbulence grain (`--noise`) is blended over every dark surface so the glass reads as material rather than flat fill.
- **The curve and the glass over the logo.** The curve was not eyeballed: a ridge detector pulled the thin bright line out of `reference/reference.png` row by row (36 points in the LAR window), and a cubic Bezier was least-squares fitted to them with a 0.24 px mean / 0.86 px max residual: `M52 0 C24.9 7.8 20.9 26 0 44` in window-local design px. The CIN window uses the same curve mirrored about its outer edge, which matches the nine clean detections there within 0.45 px. The CIN window is the reference's 56 px at x 517, the LAR window 63 px at x 2. The shape is steep near the top and settles to 45° before leaving the outer edge 44 px down; it is not a straight diagonal. the team mark sits whole in its window (about 1.4x the window height, bleeding off the outer edge). A frosted pane (`::before`, `clip-path: path()`, `backdrop-filter: blur() brightness()`) covers the outer region bounded by an S-curve, so that part of the logo goes soft and light while the inner part stays crisp. The silver curve is drawn as the pane's edge (`::after`). Because the pane blurs page content, this works in-game as well as in previews.
- **Vapor lights:** a soft team-tinted glow drifts along the lower edge of each panel in screen blend.

Motion theory is unchanged from the shared package code (staged reveals, scoring foldout, replay planes).

## Comparison

Both packages were rendered through the project's WebView2 renderer in the reference frame (1400 × 268, `?preview=reference`) and scored by `tools/nbc-snf/diff.py` against `reference/reference.png`. The sheet is `outputs/snf-claude-compare/comparison-sheet.png`.

| Region | NBC_SNF mean error | SNF_Claude mean error |
| --- | --- | --- |
| whole bug | 6.81 | ≈25 |
| away/home logo | 0.04 / 0.08 | ≈76 / ≈73 |
| pod | 9.12 | ≈36 |
| clock text | 20.60 | ≈28 |

The numbers are expected to favor NBC_SNF: its pod, rail and both logo windows are the reference's own pixels pasted back, so those regions score near zero by construction. SNF_Claude is not trying to reproduce those pixels. Its logo windows, pod and badge are deliberately different renderings, and its panels are darker, more translucent glass. Where the two packages share an approach (text placement), the scores are close.

Judged against the material board rather than the still, SNF_Claude is the one with glass, grain, curve and vapor light; NBC_SNF has none of those on the compact bug.

### Static checks

```
node tools/nbc-snf/verify-package.js mods/presentations/SNF_Claude
137/139 static lifecycle checks passed
```

The two failing checks are NBC_SNF-specific by design: one asserts the corner branding image is the peacock PNG, the other asserts the LAR/CIN reference crops load. Neither feature exists in SNF_Claude. `verify-package.js` now also reads `inserts.css` when a package has one; NBC_SNF still passes 139/139 with that change.

## Limits and honest notes

- **No network trademarks and no reference pixels.** The NBC peacock, the SNF wordmark and the reference-image crops are not reproduced. This is a deliberate boundary, not a technical gap; the package is a structural match with original artwork in those slots.
- **Blur needs a backdrop.** `backdrop-filter` blurs whatever the page composites over. In the standalone renderer that is only visible with `&background=assets/preview/field.jpg`. In the game, the page is handed to the HUD layer as an alpha image over the frame, so the translucency applies but the blur of the field underneath will not unless the host composites the page in-browser over the video. Translucent dark glass still reads correctly without the blur.
- **Fonts** are the user-supplied Ringside webfonts already in the project; the exact upright wide broadcast face is still not available, as before.
- **Not launched in game.** All checks here are the static harness and the standalone renderer. No build or game launch was performed.

## Files

| File | Change |
| --- | --- |
| `SNF_Claude/scorebug.css` | New: vector bug, material system |
| `SNF_Claude/inserts.css` | Insert styles moved out of the old stylesheet, peacock filter rules removed |
| `SNF_Claude/scorebug.html` | Badge in the pod, ghost logo images, `inserts.css` link, keyed-peacock filters removed |
| `SNF_Claude/scorebug.js` | Logo windows always use the supplied team mark (plus ghost); no per-matchup crops |
| `SNF_Claude/assets/branding/network-badge.svg`, `primetime-wordmark.svg` | Original neutral marks |
| `SNF_Claude/assets/preview/field.jpg` | Game frame for glass previews |
| `SNF_Claude/preview-scenes.js` | `background=` param accepts `transparent` or an asset path |
| `SNF_Claude/mod.json` | id `snf_claude`, version 0.5.0 |
| `tools/nbc-snf/verify-package.js` | Reads `inserts.css` when present |
| `outputs/snf-claude-compare/` | Renders, diff sheets, comparison sheet |

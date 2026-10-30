# CBS NFL current motion reference audit

Investigated October 16–17, 2026. This is an evidence log, not a declaration that every CBS treatment has been reproduced. The running game, native source, native build and game configuration were not touched for this work.

## Current broadcast evidence

| Reference | What was actually inspected | Useful evidence and limits |
| --- | --- | --- |
| [CBS 2025 Eyebar article](https://www.newscaststudio.com/2025/09/12/cbs-nfl-score-bug-update-2025/) | Two stills and two linked entry GIFs | Current compact pod geometry, team logos, eye/wordmark entrance. The second GIF is another entrance/live sequence, **not** a touchdown reference. |
| [Raiders/Patriots September 7, 2025](https://x.com/awfulannouncing/status/1964739217382355242) | Downloaded public 24s clip, sampled at 2fps and its final 1.5s at 10fps | Current touchdown and score-return sequence. The red vertical object at 22.5–24s is the physical sideline down marker, **not a replay wipe**. |
| [Official NFL Steelers/Jets Week 1 highlights](https://www.youtube.com/watch?v=kapv4nxG0eI) | Entire 858s video sampled at 1fps; first 120s at 2fps; FLAG span at 4fps | Current scorebug, touchdown, field-goal points, FLAG, low play clock and FINAL. The highlight edit did not provide a confirmed full-screen CBS replay wipe. Sampling alone cannot prove no subsecond wipe exists anywhere. |
| [CBS Thanksgiving KC/DAL intro](https://www.youtube.com/watch?v=KbYH8lAD6ro) | Entire 500s collector capture at 1fps | Current matchup/player lower thirds and a sponsor-specific Wayfair bumper around 338–342s. This sponsor treatment must not be used as a generic replay transition. |
| [CBS AFC Championship NE/DEN halftime, January 2026](https://www.youtube.com/watch?v=goBwy8oRNBk) | First/last 12s at 4fps; report exit at 30fps plus individual frames | Direct current-broadcast evidence for the halftime-report exit described below, and a separate halftime summary lower third. This is a collector's copy of broadcast footage, not a CBS-owned upload. |

### Halftime report exit: inspected source 10.81–11.43s

The full source begins with a Verizon phone/3D halftime-report introduction. Only its final horizontal-band/eye exit is implemented in `mods/presentations/CBS_2026/wipes.js` and `wipes.css`.

- Around 10.81s: a thin blue/red horizontal strip begins across the middle of the 640×360 reference.
- 10.90s: blue spans approximately y127–174; red spans y174–258. A fine white eye contour crosses the strip.
- 10.97s: blue spans approximately y113–204 with red below. The strips are moving separately, rather than fading uniformly.
- 11.03s: red is above blue; their visible bands occupy approximately y81–209 and y209–343.
- 11.10–11.17s: a white deconstructed eye aperture is centered over the red/blue plate. Faint `HALFTIME REPORT` lettering is visible. The plate is cropped by a large circular silhouette while the highlight becomes visible around its perimeter.
- 11.23s: the circle contracts further, a white upper segment becomes visible, and the eye aperture shrinks.
- 11.30–11.40s: the highlight is revealed, with a small white CBS eye approximately 74px square centered at (320,180).
- By 11.43s: the eye has cleared.

The approximately 620ms editable reconstruction uses layered CSS bands, a circular crop, the original CBS eye contours, and a small final official eye asset. It does not contain the original CBS 3D surface meshes, lighting, proprietary font or sponsor phone/hero introduction. Those differences require visual comparison and must not be described as a pixel-identical or complete halftime package.

The module responds only to explicit `CBS_REFERENCE_WIPE` and opt-in semantic `CBS_HALFTIME_REPORT_READY` events. Ordinary `HALFTIME`, `REPLAY_STARTED` and `REPLAY_ENDED` do not prove this treatment belongs there and are not mapped by this module. Invalid/paused/play-selection states suppress it. Cancellation clears timers and ignores stale callbacks.

### Current FLAG and FINAL evidence

The Steelers/Jets reference around 655s replaces the upper-center wordmark/down band with a bright yellow `FLAG` slab with black type. A narrow yellow line precedes expansion to the full center width. Four-frame-per-second sampling bounds that change to roughly 250ms; it does not establish an exact easing curve. The lower quarter/game-clock/play-clock row remains intact.

The same source around 854–858s shows CBS SPORTS in the upper center and `FINAL` in the lower center; quarter and clocks are removed. Updated team records remain at the lower outer corners and timeout marks disappear. The highlight edit does not establish the full entrance timing of this final state.

## Older primary designer material: useful but not proof of current usage

- [Two Fresh's 2021 rebrand on Behance](https://www.behance.net/gallery/163515651/CBS-REBRAND-2021?locale=zh_CN), with [its public Adobe video player](https://www-ccv.adobe.io/v1/player/ccv/UCowN7mBTXA/embed?api_key=behance1&bgcolor=%23191919): 100.97s reel retrieved and inspected. The NFL portion around 64–80s demonstrates team-colored deconstructed eyes, helmets, NFL Today and matchup/promo graphics. It does not by itself verify the 2025/2026 live replay wipe.
- [Two Fresh Super Bowl LV project](https://www.twofresh.tv/project/cbs--super-bowl): the current page's old Vimeo 515524999 media reports `clip not found`. The older `/cbs-super-bowl` URL resolves to the home page.
- [Two Fresh Super Bowl LVIII project](https://www.twofresh.tv/project/cbs-sports--super-bowl-58-): its 75.97s reel was retrieved. It is Vegas/neon/Super Bowl-specific, including a shield/eye transition. It must not be silently used as the current regular-season package.
- [Gretel CBS identity](https://gretelny.com/cbs) establishes the corporate deconstructed-eye motion vocabulary. It is not a reference for every current NFL event graphic.

Public research footage and contact sheets were kept in the Windows temporary `cbs-reference-audit-20261016` directory, not packaged as game overlays. A temporary official yt-dlp Python zipapp retrieved publicly playable material without cookies, authentication or DRM workarounds.

## Verification and next reference needs

`wipes.js` passed Node syntax checking and a focused VM check covering explicit-event routing, rejection of unverified replay/halftime mappings, paused suppression, timer cancellation and completion. These checks do not verify pixels. Root integration and rendered comparison are required for the CSS composition.

Still requiring a directly inspected current broadcast reference: generic replay-in/out wipe, ordinary end-of-quarter bumper, full halftime sequence, dedicated timeout motion, overtime sequence and full-screen final presentation. A 2026 full-game replay investigation is being handled separately; do not mark those components verified from the older reels above.

## October 17 follow-up: actual first-quarter boundary

Source: [official NFL Chiefs/Cowboys Thanksgiving 2025 full game](https://www.youtube.com/watch?v=PLR04ljZ5DI&t=1434s), title `Epic Thanksgiving Game! Kansas City Chiefs vs. Dallas Cowboys FULL GAME | NFL 2025 Season Week 13`, total duration 7,320 seconds. Public YouTube storyboard samples located the quarter boundary; only source 1410–1465s was retrieved at 640×360 for detailed inspection. This is the current compact CBS pod, not a legacy full-width scorebar.

The end of the first quarter includes an untimed down/penalty. Source around 1390–1434s retains `1st | 0:00`; the quarter presentation does not start merely when the clock initially reaches zero.

Measured at 10fps around source 1433.7–1437.3s:

- Approximately 1434.3s: the scorebug's upper center remains CBS SPORTS; the lower center changes to `END 1st QTR`. Team scores remain KC 14 / DAL 7.
- Approximately 1434.6s: a centered deconstructed-eye aperture begins opening over the referee camera. Fine white eye/circle contours accompany it.
- By approximately 1434.8s: the aperture reveals a full-screen two-tier matchup board. The upper half is Chiefs red with gold `CHIEFS`; the lower half is Cowboys navy with silver `COWBOYS`. A thin white horizontal rule separates the halves. Large team logos sit partly outside the left edge; smaller `KANSAS CITY` / `DALLAS` brush-label strips sit above the team names.
- This specific board contains falling autumn leaves, pumpkins, acorns and a pinecone. It is visibly **Thanksgiving-specific**, not evidence for a generic CBS regular-season quarter bumper.
- The board holds until approximately 1436.0s, then opens/retracts into a Dak Prescott close-up with fine circular/eye contours clearing by approximately 1436.5s. The sampled total treatment is about 1.9 seconds; its hold and exit timings are bounded by 100ms sampling, not frame-accurate.
- Around 1441s, the return camera carries a small two-row stacked KC/DAL score panel near the lower-left. Around 1448s, the normal centered pod is back with `2nd | 15:00`.

Reference-only temporary outputs:

- `cbs-q1-boundary-1410-1465.mp4` — 55s bounded clip, approximately 3.5MB.
- `cbs-q1-boundary-contact.jpg` — 2fps boundary scan.
- `cbs-q1-bumper-motion.jpg` — 10fps detailed contact sheet.
- `cbs-q1-bumper-full.png` — full matchup board reference frame.

All are under `%TEMP%/cbs-reference-audit-20261016/`; none are game overlays or shipped assets. No quarter-bumper implementation was added in this research follow-up. The generic quarter treatment, its context rules and complete return sequence still need a non-holiday current reference. Current replay-wipe verification from the same full-game source is handled by the separate motion-measurement task and should supersede the earlier broad replay-reference gap above once recorded in the main motion documentation.

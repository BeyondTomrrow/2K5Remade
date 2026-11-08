# CBS motion implementation and remaining reference gaps

October 16, 2026. Standalone HTML work only. The native game was not launched,
built, controlled or attached to; Claude's work remains untouched.

## What the package now runs

| Component | Implementation | Inspected evidence / limit |
|---|---|---|
| Eyebar return / kickoff entrance | Center eye, outward logo travel, horizontal wordmark reveal, oversized logo hold, contraction and live panel reveal; about 1.8 seconds | September 2025 PIT/NYJ GIF; source sampling is 60/70 ms, not original production keyframes |
| Touchdown | Plate contracts; scoring logo grows/turns; outlined dimensional title assembles across lower picture; optional real scorer/portrait slot; title contracts; plate returns; points display and settle | Current LV/NE September 7, 2025 broadcast clip, onset about 15.42 seconds, settled at 21.88 seconds; approximately 6.46 seconds |
| Numeric score update | Holds the preceding score briefly for event ordering, displays the actual positive delta, then the authoritative new total; only the scoring side changes size | `+6` in the touchdown clip; `+3` also observed in official 2025 Steelers/Jets highlights. No synthetic PAT is created from a combined seven-point update |
| FLAG | Yellow upper-center band reveals outward from its center | Official 2025 Steelers/Jets highlights around 655 seconds; reveal bounded to roughly 0.25 seconds at sampled resolution |
| FINAL | CBS SPORTS remains above FINAL; quarter, clocks and timeout ticks disappear | Official 2025 Steelers/Jets highlights around 854–856 seconds; this is a verified state, not evidence for a separate final bumper |
| Halftime-report exit segment | Editable horizontal red/blue bands, eye aperture and small settled CBS mark; about 620 ms | January 2026 NE/DEN halftime report, 10.81–11.43 seconds. This does not implement the complete sponsor/3D halftime open or prove a replay wipe |
| Replay in | Rotating NFL shield approaches through blue light and eye outlines, fills the picture and reveals replay; about 1.05 seconds | Official NFL Chiefs/Cowboys Thanksgiving 2025 full CBS broadcast, 94.42–95.43 seconds |
| Replay out | Oversized CBS SPORTS lettering contracts to the centered wordmark while eye outlines expand and clear; about 1.05 seconds | Same full CBS broadcast, 107.43–108.43 seconds. A separate sequence, not replay-in played backwards |
| Pause/menu/match change | Cancels motion and timers and clears overlays | Runtime correctness behavior, not a claimed CBS animation |
| Player inserts | Mirrored portrait/name/column strips and multiline variant; 440ms entrance, requested hold, 930ms staged exit | Actual home/away CBS inserts inspected at 43.63–50.93s and around 1156–1163.5s in the full Chiefs/Cowboys broadcast. Stat label combinations are data-driven adaptations |
| Full-screen comparison | Split-team opener, oval frame, table, optional portrait slots, 1.25s entrance and .55s exit | Actual WALK-OFF WINS treatment at 307.80–323.05s. Native comparison events and original source materials remain unavailable; see CBS-2026-STATS.md |
| Quarter / complete halftime / overtime sequences | Event interfaces and preview controls exist; unavailable sequences explicitly report `reference-needed` | A Thanksgiving-specific quarter bumper was found; it does not establish the ordinary regular-season quarter package. No generic wipe is substituted |
| Timeout / two-minute warning insert | Live fields remain functional; dedicated motion is unavailable | A bespoke CBS sequence has not been inspected |

The title is editable layered SVG text with translucent fills, repeated edge
contours and CSS transforms. It is **not** a prerecorded touchdown video. Team
art remains replaceable image assets. The exact CBS font and original 3D glass
materials were not supplied or verified; this reconstruction is not certified
pixel-identical or “100% complete.” The user-requested dotted panels remain.

## Sources

- [CBS's current Eyebar report and embedded entrance clips](https://www.newscaststudio.com/2025/09/12/cbs-nfl-score-bug-update-2025/).
- [Current scorebug and touchdown clip](https://awfulannouncing.com/cbs/new-scorebug-positive-reviews.html), embedded [original recording post](https://x.com/awfulannouncing/status/1964739217382355242).
- [Official NFL Steelers/Jets Week 1, 2025 highlights](https://www.youtube.com/watch?v=kapv4nxG0eI): numeric scoring, FLAG and FINAL checks.
- [January 2026 AFC halftime report](https://www.youtube.com/watch?v=goBwy8oRNBk): measured red/blue eye exit. Detailed audit: `CBS-2026-MOTION-REFERENCE-AUDIT-20261016.md`.
- [Official NFL Chiefs/Cowboys Thanksgiving 2025 full CBS game](https://www.youtube.com/watch?v=PLR04ljZ5DI): both replay directions at 94.42–95.43 and 107.43–108.43 seconds. The upload date/title alone was not treated as proof of the feed; the actual CBS graphics were inspected.
- [Primary designer's CBS project](https://www.twofresh.tv/project/cbs--super-bowl) and [2021 rebrand reel](https://www.behance.net/gallery/163515651/CBS-REBRAND-2021): historical design evidence, not proof of a 2026 replay wipe. The 2024 Super Bowl reel is event-specific and was not substituted for regular-season motion.

Reference files and measured timing tables are outside the distributable package
under `outputs/cbs-2026/reference/`. See
`outputs/cbs-2026/reference/measurement/MOTION-MEASUREMENTS.md` for observed
positions, clip timestamps and sampling uncertainty. Reference footage is not
loaded by the game page.

## Runtime and data ownership

`motion.js` owns cancellable timelines and score holds; `motion.css` owns their
rendering. `scorebug.js` supplies authoritative state and events. A match can
permit presentation while the ordinary Eyebar is hidden, but invalid state,
pause and play selection cancel everything. Full-screen extensions have a
separate `CBSWipe` interface. Its observed halftime-report exit runs only on an
explicit reference request or `CBS_HALFTIME_REPORT_READY`; it is not mapped to
ordinary replay or quarter events without evidence.

`CBSReplayWipe` now owns two separately measured replay directions, driven by
`REPLAY_STARTED` and `REPLAY_ENDED`. It uses editable CSS/SVG layers and existing
replaceable logo assets. Both sequences have generation-guarded completion,
cancel on invalid state/pause/menu, and respect reduced motion. Their original
3D meshes and materials are unavailable; these are motion reconstructions,
not pixel-identical original network renders.

State arrives before derived events. A short pending score hold allows a
touchdown event to claim that update. Each completion checks that it still owns
the same hold and stage, so a late touchdown cannot be cleared by the earlier
numeric-update timeout. Repeated touchdown events do not restart the sequence or
add points. Points always come from actual score deltas, not animation counters.

Player identity and portrait are optional `event.player` fields (`name` or
`displayName`, `position`, `portrait`). They are omitted when unavailable. The
current native Presentation API does not yet supply a verified touchdown scorer
or portrait; the code does not fabricate one. This slot needs a separate native
data integration after Claude's session.

Current native limitations also remain: replay-end has no confirmed producer;
period-end/halftime/period-start events arrive together on period increment,
rather than at the first 0:00 frame. A browser fixture does not prove native
camera or transition synchronization.

## Verification and review

- JavaScript syntax checks pass for motion, scorebug and preview scripts.
- Actual standalone WebView2 PNG frames inspect entry, title, return, +points,
  settled total, FLAG and FINAL.
- Actual browser controls confirm the touchdown event and its completion report;
  pause suppresses the graphic and a late touchdown event.
- Focused fake-clock regressions pass: late touchdown at 600 ms survives the old
  numeric completion, duplicate events do not restart, ordinary +1 settles,
  pause cancels future work, and only the scoring side receives points styling.
- `outputs/cbs-2026/CBS-2026-preview.mp4` is the expanded standalone motion
  showcase. It uses sample game state, not live game footage.
- `outputs/cbs-2026/CBS-2026-animation-package.mp4` is the 34.1-second captioned,
  silent reel including the reference halftime-report exit. Decoded frames were
  inspected and browser playback verified. `watch.html` provides a local player.
- The updated `CBS-2026-animation-package-v2.mp4` adds both replay directions.
  Its chaptered player is `outputs/cbs-2026/watch.html`. The reel uses sample
  data, contains no native gameplay footage, and has no audio.
- Focused replay checks pass: cancellation removes timers, an in-to-out restart
  survives the old completion, reduced motion skips the cover, pause cancels
  through the actual motion router, and unknown events leave motion alone.

Next: verify the ordinary quarter/halftime variants, improve source typography
and dimensional materials, and integrate native event timing after Claude's
session. A browser preview does not verify the native game event producers.

Player-stat expansion and the separate 69.1-second stats reel are documented
in [CBS-2026-STATS.md](CBS-2026-STATS.md). The original native player card remains
enabled until its queue-to-browser visibility handoff can be verified.

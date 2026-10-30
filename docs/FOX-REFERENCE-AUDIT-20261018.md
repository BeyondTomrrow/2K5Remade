# FOX reference audit — 2026-10-18

Research only. No FOX production assets, presentation definitions, native sources, game builds, or game processes were changed by this audit. CBS remains the implementation priority.

## Source identity and saved evidence

The user supplied `https://youtu.be/rb3v4bEhmLA`. YouTube's own oEmbed and player metadata identify it as **“GAME OF THE WEEK! Chicago Bears vs. Carolina Panthers FULL GAME | NFL 2026 Season Week 1”**, published by **NFL**, channel ID `UCDVYQ4Zhbm3S2dlz7P1GBDg`, upload date `2026-09-15`. The extracted duration is 8,730 seconds and the inspected stream is 640×360 at 30 fps. These are publisher metadata, not an independent claim about historical football results. [User's official NFL video](https://www.youtube.com/watch?v=rb3v4bEhmLA).

Evidence is saved under `outputs/fox-2026/reference/20261018/`:

- `youtube-oembed.json`, `player-response.json`, and `ytdlp-metadata.json` preserve source identification and extraction metadata. Signed media URLs expire; do not embed them into the package.
- `storyboard-160-M00.jpg` through `M34.jpg` are YouTube's sparse 160×90 storyboard cells at ten-second intervals, not a downloaded full game. `source-storyboards-0.jpg` through `17.jpg` are labeled contact sheets for finding candidate scenes. They establish scene inventory, not animation timing.
- Only short bounded video sections were saved: `source-0090-0165.mp4`, `source-0195-0265.mp4`, and accurately decoded sections `0148-0154`, `0580-0655`, `0740-0825`, `1255-1275`, `1995-2007`, `2018-2048`. No full-game video was downloaded.
- `source-2000s.png`, `source-4000s.png`, and `source-6000s.png` show ordinary live scorebugs and the real left/right quarterback text inserts.
- `entry-fine-contact.jpg`, `td-fine-contact.jpg`, and `wipe-fine-contact.jpg` show 100 ms samples. Underlying bounded clips retain the source's 30 fps for final frame refinement. `quarter-scan.jpg` records the actual inspected Q1→Q2 sequence.

The accurately decoded clips use input seek followed by decoding/re-encoding, rather than stream-copy seek. Earlier `0090-0165` and `0195-0265` are stream-copy inventory clips and should not be used as the authority for sub-frame absolute timing.

## Primary creator reference

Drive Studio's current home page labels a demonstration **“NFL ON FOX | SCOREBUG & INSERT 2025.”** Its actual image is WordPress media ID `22677`, slug `251118_fs_scorebug-2`, direct source `https://drivestudio.com/wp-content/uploads/2025/11/251118_FS_SCOREBUG-2.gif`. [Drive Studio](https://drivestudio.com/).

The saved `drive-2025-scorebug.gif` is 1500×500, 200 frames, 40 ms per frame: an eight-second creator demonstration. `drive-scorebug-contact.jpg`, `drive-gif-metadata.json`, and `drive-media-scorebug.json` document it. It shows an initial helmet/scorebug reveal, changing possession-side banners, and a large Patrick Mahomes QB insert. The GIF is edited demonstration material with cuts; eight seconds is **not** a claimed continuous game-event duration.

Drive's WordPress search returns a separate **2023** FOX project, but did not return a separate 2025 project page. Do not import its older design or motion as if it were the supplied 2026 broadcast. The inspected 2026 NFL video and the 2025 creator demonstration share the floating-logo/scores family, but individual sequences still need to match their own source. [Drive's earlier 2023 project](https://drivestudio.com/projects/fox-nfl-2023/) is an era distinction, not the target specification.

No exact typeface name or font download is supplied by the creator GIF's official media metadata. This audit does not identify a substitute as the broadcast's actual font. Official network marks are replaceable assets; do not redraw them with text or AI. [FOX's official NFL asset page](https://www.foxsports.com/nfl/logos).

## Measured live geometry

Coordinates below refer to the **640×360 source**, with roughly one-to-two-pixel measurement uncertainty from compression/antialiasing. Multiply by three for the existing 1920×1080 presentation canvas. Logo **visible ink** is distinguished from an inferred asset slot; different logos have different transparent padding.

| Element | Source evidence and approximate bounds | Implementation consequence |
|---|---|---|
| Floating base scorebug | Ordinary live frames around 580, 2000, 4000, 6000s; visible graphic roughly x188–456, y300–342 | Keep logos/scores floating over game. Do not add large team-colored backing panels. Do not infer exact asset-slot dimensions from Bears versus Panthers visible ink. |
| Clock / quarter | Center around x320; translucent dark plate approximately x297–342, y302–332 | Preserve a translucent plate; game remains visible through it. Clock is above quarter. |
| Timeout marks | Under each score, around y335–340 | Three short rectangular, square-corner marks. Distinguish used versus available states, retaining real counts. |
| Away possession banner | Around x202–296, y285–300, e.g. 4000s | Position over the possessing team's side. Play clock occupies the right end of the same compact banner. |
| Home possession banner | Around x344–438, y285–300, e.g. 2000s | Mirror the banner's placement, not the text. A center-only down marker is wrong for this source. |
| Right QB text insert | `source-2000s.png`, name approximately x568–623/y311–320; statistics x535–623/y323–332 | Anchor right to the broadcast-safe edge. White italic player name above compact completion/attempt, yards, TD line. No box or portrait in this small insert. |
| Left QB text insert | `source-6000s.png`, name approximately x19–91/y311–320; statistics x18–122/y323–332 | Anchor near left safe edge. Do not merely park it immediately next to the central scorebug. |
| Network corner mark | Approximately x556–613/y18–32 | Use an actual FOX/NFL image asset at its intended ratio; source logo is separate from the bottom scorebug. |

Visible source examples are `B. YOUNG` / `4/6 44 YDS 1 TD` on the right, and `C. WILLIAMS` / `16/24 230 YDS 1 TD` on the left. These are reference pixels, not values to bake into runtime textures. Native name/count/yards/TD/INT fields must populate the template. Empty or unavailable statistics must not be replaced with fabricated zeroes.

The existing `REFERENCE.md` measures a different supplied 1280×720 still (572×95 visible box, x349..920/y593..687). Those still measurements remain useful, but comparing different logo silhouettes alone does not prove a size regression. Match the same teams/assets before changing slot geometry.

## Verified motion inventory

### Normal return after player feature

`source-accurate-0148-0154.mp4` and `entry-fine-contact.jpg` show the Caleb Williams feature leaving around **150.5–150.7s**, while the compact live scorebug appears in the lower center. By approximately **150.8s** it is stable. This sampled return is a short opacity reveal, without a full-screen wipe or a traveling helmet. The 30 fps clip should determine final opacity keyframes; do not assume that every scorebug appearance must reuse the creator GIF's initial helmet opener.

### Initial helmet / scorebug opener

The primary Drive GIF clearly establishes a separate helmet-centered opener. Its early frames progress from the helmet into expanded team marks and scores, then contract into the live layout and reveal the possession-side banner. Use `drive-2025-scorebug.gif` and its exact 40 ms frame duration for this variant. Because the GIF is a 3:1 demonstration crop, derive the underlying normalized scoreboard transform from its stable graphic, not from an assumed 16:9 outer frame.

### Touchdown

`source-accurate-0740-0825.mp4`, `td-fine-contact.jpg`, and `touchdown-frames/frame-007.png` establish the real sequence:

| Approximate source time | Observed state |
|---|---|
| 751.9–752.0s | Old live scorebug clears; football camera remains visible. |
| 752.0–752.6s | Extruded/outlined TOUCHDOWN letters enter in a staged sweep; blue/black team layers and a star/chevron motif assemble. |
| 752.6–752.9s | Actual scorer cutout appears at lower left; the `CHUBA HUBBARD` tab resolves above the title. |
| 752.9–754.9s | Stable full-width lower-third. Large slanted outlined title spans about x100–571/y275–334; player cutout extends up near x40–165/y191. Team layers extend to the lower screen edges. |
| 755.0–755.3s | Name/title/portrait contract and clear; normal scorebug returns using its old score first. |
| 755.7–756.3s | Scoring side temporarily shows **+6**. |
| By ~756.5s | Updated numeric score replaces +6. |

These are 100 ms contact measurements, not final frame-accurate easing. The underlying 30 fps section allows precise tracking. The source does **not** show the existing JSON's small generic `TOUCHDOWN {team.abbr}` banner or a flashing gold score box. Preserve a scoring-side score hold until the presentation releases the new total; do not let the preceding live-state update skip the old-score→points→new-score sequence.

The actual scorer cutout/name is part of this source variant. If native events lack verified scorer identity or an appropriate portrait, keep a truthful supported variant/fallback; do not assign the current quarterback or an unrelated headshot merely to fill the slot.

### Replay return

`source-accurate-1255-1275.mp4`, `wipe-fine-contact.jpg`, and `wipe-fine/f011.png` show replay→live around **1269.9–1270.4s**:

- The FOX NFL shield begins extremely large, filling/cropping the frame, then recedes to the center.
- The live football camera changes underneath the transition; by the large readable shield the live scorebug is already returning behind it.
- Gold chevrons flank the shield, with thin vertical/wireframe graphic lines across the field. The shield has visible dimensional edging/shadow, not a flat text logo.
- At about 1270.0s the shield is roughly x193–451/y18–349; at 1270.3s it is roughly x252–392/y90–268. These are successive receding states, not a single static placement.
- By roughly 1270.4s the shield and chevrons have cleared.

This source establishes **replay return**, not an automatic end-of-quarter bumper. The nearby 2023.0s replay entry instead begins with a small centered helmet + gold chevrons before the camera switches to replay. These should be separate named transitions until their complete 30 fps movement has been measured.

### Actual inspected Q1 end / Q2 start

`source-accurate-2018-2048.mp4` and `quarter-scan.jpg` show the first quarter ending while Carolina is preparing fourth down. There is a short replay, then the live graphic leaves around 2033–2034s. At **2036–2037s** the camera cuts to a stadium aerial and a **1ST QUARTER STATS** panel; it holds to roughly **2043s**, then dissolves back to the field. By around **2045s** the normal scorebug shows Q2/15:00 and the delayed fourth-down play resumes.

The stats panel is roughly x18–219/y181–344 at 640×360: title, a two-team color/logo strip, and comparative RUSH YDS, PASS YDS, TAKEAWAYS, POINTS OFF, POSSESSION rows. See `quarter-frames/frame-020.png`. Those are **quarter-specific totals**, not automatically interchangeable with cumulative current-game totals. The current verified native collector is current-game scope; a quarter panel must wait for reliable period scope or complete validated deltas.

**No full-screen navy/gold FOX shield quarter bumper appears at this inspected Q1 boundary.** The master prompt's requested quarter sequence must be reconciled with the actual selected broadcast, rather than borrowing the replay shield and labeling it quarter-end. Q2 halftime, Q3 end, final, overtime, injury and replay-in have not yet had their complete motion measured; sparse storyboard sightings do not certify them.

## Existing package inventory and gaps

`mods/presentations/NFL on FOX/` currently has `presentation.json`, `REFERENCE.md`, 32 team PNGs plus logo README, music, and four local TTFs. It has **no HTML entry/JS/CSS package**, unlike the new CBS implementation. `existing-package-inventory.json` records its current structure without changing it.

The JSON has 19 scorebug elements and 12 named animation definitions: pregame, touchdown, field_goal, extra_point, two_point, safety, timeout, first_down, two_minute_warning, end_of_quarter, halftime, player_stat. Many use generic slide/wipe/fade text banners. The touchdown uses a gold blink over the score; halftime is a small `FOX NFL HALFTIME` text banner. There is no layered full-screen replay shield/chevron sequence, scorer cutout touchdown, quarter stat table, or actual source-driven initial helmet opener. Do not describe those existing definitions as verified FOX motion.

The current player-stat definition contains symmetric white name/line elements at local x=-224 and x=582 with five-second opacity/slide keyframes. Its approximate baseline is plausible, but the inspected 2026 frame places small QB statistics farther toward the left/right broadcast-safe edges. Verify with identical game framing before moving them.

`existing-font-identities.json` proves local font metadata only:

- `HumanPENarrow-Bold.ttf`: Human PE Narrow / Bold; `HumanPENarrow-Regular.ttf`: Human PE Narrow / Regular.
- `AllProSans-Bold.ttf`: All-Pro Sans / Bold; `AllProSans-Heavy.ttf`: All-Pro Sans Heavy / Regular (full face name All-Pro Sans Heavy).

This proves availability, not that either is the exact typeface in the 2026 source. Do not choose a heavier font purely because the old manifest uses weight900. Compare the reference's score numerals, angled counters and spacing against the actual local faces at matched size.

The existing team file names retain 2004-era aliases (`OAK`, `SD`, `STL`, `JAC`). A new HTML renderer must resolve both era aliases and current state codes safely, preserving the correct supplied team assets. Source logos remain replaceable image slots, with no AI redrawing.

## Safe implementation order after CBS

1. Preserve the existing JSON package. Add a selectable HTML FOX package with the same shared state/event contract and validated missing-data suppression; keep original ESPN and CBS intact.
2. Match static floating scorebug, transparent center plate, rectangular timeout marks, possession-side banner/play clock, and exact side-safe QB text using the saved 2026 frames.
3. Implement the two observed entrance variants separately: normal short return versus initial helmet opener. Reproduce the Drive GIF's actual sampled transforms, not an unrelated generic animation.
4. Implement score ownership/point changes, then the source touchdown lower-third when verified scorer data/portraits are available. Unknown portraits remain absent; native art must not be suppressed without a valid replacement.
5. Reconstruct the measured replay-return shield, chevrons and wireframe layers; verify the replay-in variant separately.
6. Use native quarter-specific data for the inspected Q1 stats panel, or leave that component unavailable until proven. Do not label cumulative totals `1ST QUARTER STATS`.
7. Inspect bounded Q2/Q3/final source windows before writing their timelines. No full-game download, game launch or source regeneration is necessary for this reference work.

All current broadcast sample identities/statistics in review fixtures must remain explicitly labeled sample data. This audit supplies measurements and constraints; it does not claim the new FOX package is already implemented or live-tested.


## Full-resolution follow-up

Obtained and inspected a bounded1920?1080 excerpt at5998?6004s (`qb-left-5998-6004-hd.mp4`, frame `qb-left-6000-hd.png`). It confirms the floating design, translucent center, square timeout marks and left-safe C.WILLIAMS insert at higher resolution. Use it for actual glyph/logo-ink refinement rather than enlarging the360p inventory images. The original font identity remains unverified. No FOX production changes were made during this CBS follow-up.

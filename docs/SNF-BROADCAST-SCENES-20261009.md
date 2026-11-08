# NBC SNF broadcast scenes — October 9, 2026

Package version 0.4.0 adds an opening matchup, a timeout score bumper, a persistent top-right NBC/SNF mark, a separate single-player profile and the compact context-card topic/footer variant. This is frontend presentation work. No NFL2K5 executable was built or launched; the existing collaborator hold remains in effect.

## Reference evidence

- [J. Collins / Compadre: Sunday Night Football identity, NBC client, 2022 launch](https://jcollins.design/Sunday-Night-Football). The creator identifies the lettering as custom and explains its peacock-beak counters. The directly viewed [logo sheet](https://freight.cargo.site/t/original/i/16f663a0dd9499c07d1225a20575ac659ab3193f12276aba461393b1c4cb20c3/SNF_Frames_LogoSet.jpg) establishes the square, horizontal SNF initials, the unboxed peacock/initials arrangement, and the longer marquee family. Generic italic SNF type was removed from the new scenes.
- The original [NFL-hosted NBC full wordmark SVG](https://static.www.nfl.com/league/apps/web/global/logos/schedule/primetime/SundayNightFootballLight.svg) contains the complete SUNDAY NIGHT / FOOTBALL lettering. Its 19 black letter paths are preserved in `sunday-night-football-wordmark.svg`, with white fill and a tight viewBox; the original peacock and NFL shield are excluded. It is the full wordmark, **not** the short corner logo.
- The short `snf-wordmark.svg` is a code-native reconstruction of the directly inspected creator sheet. It is not the designer's original vector. The user's Paint peacock is unchanged and remains a separate image, keyed at runtime.
- `nfl-shield.svg` preserves the 15 original shield paths from that same NFL-hosted primary SVG. The intro has the top peacock, stacked title, paired metallic flares and bottom NFL shield visible in the original identity family.
- The original [2022 Eagles insert GIF](https://mir-s3-cdn-cf.behance.net/project_modules/1400/bb5722154057369.6351b2465f02e.gif) was directly viewed. At 1280×720 its corner mark occupies approximately x1103–1237, y29–62: a 134×33 footprint with a 43px right inset. The corresponding 1920×1080 component is 201×50, 64px right and 44px top.
- The designer's [Super Bowl LVI large-score / halftime animation](https://cdn.myportfolio.com/c388ec05-fff3-4891-8755-5704b35ae0bb/40e57f5f-158b-44c8-8f29-d2fcaa889d3e_rw_600.gif?h=81b4c587c64fcd252a386f1eb80e8b49), from [Jacob LaPlante's original package](https://jacoblaplante.com/super-bowl-lvi-scorebug-gfx-package), was directly viewed. Two wide horizontal team rows show dominant team imagery and large right-side scores, with a pale status footer. The new timeout score display adapts that score-bumper structure with SNF branding in place of the Super Bowl trophy/mark.

The timeout-specific footer and animation timings have **not** been verified against a complete timeout-to-commercial broadcast recording. The matchup open uses the observed NBC wordmark/marquee vocabulary; its two-team arrangement and timing are a supplied-data recreation. Neither is claimed to reproduce NBC's original rendered 3D title video or Carrie Underwood opening. The old unverified circular intro rings were removed before recording.

The directly inspected [Mahomes context card](https://mir-s3-cdn-cf.behance.net/project_modules/1400_webp/fa93a7154057369.6351b2d036b0b.jpg) adds a team-color topic rail (PASSING VS BLITZ) and a pale footer-note tab to the compact portrait/stat insert. The package now supports those two distinct supplied-data elements through `topic` and `note`. They disappear when the next card lacks them; no season context or editorial note is invented.

## Behavior

| Component | Trigger and data | Lifecycle |
| --- | --- | --- |
| Corner watermark | Valid active match with both teams; no dependency on native scorebug visibility | Hidden on initial page, inactive game, pause, play-selection/menu, intro and replay wipe; restored after transition. Large features no longer draw a second competing corner mark. |
| Matchup open | `GAME_STARTED`, once per match, first quarter with elapsed clock <6 seconds; explicit `MATCHUP_INTRO` / `BROADCAST_INTRO` also supported | Six-second default; owned team names, records, logos, optional supplied stadium. A late/midgame page attachment cannot create a false opening. Cancels on menus, invalid states and other inserts. |
| Timeout score bumper | Existing `TIMEOUT` event, requesting side optional | Five-second default; both live scores, quarter/clock, remaining timeout marks and the requesting team. State refreshes update the values. The regular scorebug returns after the bumper; it cannot reappear over a menu. |
| Single-player profile | `PLAYER_PROFILE` or `PLAYER_BIO` with named `player` | Accepts all positions, supplied player render or portrait, team logo, position/number, height, weight, age, experience, college, hometown and supplied profile/stat fields. Unknown data is omitted, number zero is kept, -1 is hidden. Eight values per page, up to24 supplied values, with timed page changes. Pending swaps cannot mutate a replacement profile or reappear on a menu. |

## Single-player payload

```js
{
  name: 'PLAYER_PROFILE',
  team: 'home',
  duration: 8,
  caption: 'PLAYER PROFILE',
  player: {
    fullName: 'Player Name', position: 'WR', number: 0,
    portrait: 'https://portraits.local/123.png',
    // render: '...' may supply an actual game render instead of a portrait.
    height: '6 FT 0', weight: '200 LB', college: 'College',
    experience: '3RD YEAR',
    profile: [{label: 'HOMETOWN', value: 'Town'}],
    fields: [{label: 'RECEPTIONS', value: 0}]
  }
}
```

These are examples of the data contract, not a substitute roster. There is no ordinary native guest producer for PLAYER_PROFILE yet. The game's known typed QB sample path, lineup identities and ESPN card context do not establish all player biographies or historical datasets. The untouched compiled executable still has its previous native bridge. Existing GAME_STARTED and TIMEOUT producers are already present in source, but their precise appearance timing needs a permitted game test.

## Stat correctness fixes

Compact and large stat cards now preserve actual zero, display a dash for missing/non-finite numeric values, and derive rates only with complete inputs. Offensive linemen do not fall through to defensive stat columns. Supplied `player.fields` are authoritative in both layouts; large features also accept single-column custom `rows`. Supplied longest runs/receptions, forced fumbles, defensive touchdowns and kicker extras are no longer discarded.

## Validation and review

`node tools/nbc-snf/verify-package.js`: **70/70** package lifecycle and payload checks passed. These tests load the actual HTML/runtime/scripts with a virtual clock. They verify ownership, menu resets, unknown values, profiles across positions, live timeout updates, profile pagination, context-card replacement, team logo changes and missing-plate fallback; they do not establish pixel fidelity or native game-hook correctness.

The checks exposed stale player features surviving an automatic intro and invalid years-pro values becoming visible text. Both were fixed. Representative scenes were separately rendered and inspected through the existing offscreen WebView2 presentation renderer. No renderer rebuild was performed.

Use `mods/presentations/NBC_SNF/preview.html` for the complete scene selector. Recorded supplement clips are stored in `outputs/snf-previews/review`; these are rendered sample-data previews, not game footage. The new fixture uses the Rams as the home team for the opening, and original-game Brady/Brown portraits for profile examples. Those small original portraits visibly limit large-card image quality.

No protected build/analysis directories, mod packs, emulator tools, native memory/GPU files, video-menu or mod-pack sources were edited. No Discord changes were made.

## Latest scorebug reference corrections

The peacock inside the clock pod now uses the user's unchanged Paint PNG with a silver-gray silhouette (`#bfc8d1`). White is keyed out and the mask is restricted to SourceAlpha, so transparent filter margins cannot become an opaque gray rectangle. Its measured footprint is now 37×22 design pixels at pod-local x29, y7.5.

The existing timeout marks were painted underneath a later rail sibling. Their explicit origin/layer restores all three marks on each side, including the used gray mark. The rail has a light lower edge; quarter text is one pixel lower and its numeral one pixel shorter. The top logo-window cap has a silver bevel.

The old bright, flat team gradients have been replaced by the **existing** reference-derived `away_mask.png` and `home_mask.png` alpha plates, tinted with the current team's color over measured dark tones. This restores the uneven dark glass behind abbreviations/records while retaining color near the scores/logo windows. No new bitmap was generated or edited.

The existing `logo_LAR_away.png` and `logo_CIN_home.png` RGB planes were verified identical to their original reference crops. Those normal-scorebug windows use their original placements (LAR x2, 63×52; CIN x517, 56×52), without duplicate gloss or shadows. A scoped alpha filter avoids double blending the plates' already-composited edge. Other sides/teams and player/drive cards still use complete logos. Missing plate files fall back once to full logo art and cannot cause repeated failed loads on every state update.

`font-review.html` and `outputs/snf-previews/review/font-comparison.png` compare the same LAR 42 / CIN 27 / 15:00 / 2ND sample at 2× size. The reference file and prior package image are unchanged. The latest comparison includes the corrected gray logo, glass and crops.

### Typeface remains unresolved

The declared `fonts/nbc-snf.otf` and `.ttf` files are absent, and no verified original NBC font exists among the installed or repository families. Current typography therefore remains Arial Black. The measured cap heights/widths are close, but the clock `1` upper flag/stem weight, `0` counters, `2` curvature and abbreviation `R` junction still differ. Cipher, All-Pro and Bahnschrift specimens were inspected and do not establish an exact NBC match.

[Refinery's foundry](https://kimmydesign.com/typeface/refinery/) describes square forms and raised/underlined small caps; [Ringside's foundry](https://www.typography.com/fonts/ringside/how-to-use) documents width variants, tabular figures and a raised-colon alternate. These are research candidates only: neither primary source verifies which font/weight NBC used here. No font was bought, downloaded or relabeled as an exact match. A verified original font and another glyph comparison are still needed.

The 47-second `SNF-broadcast-additions.mp4` records the new motion families **before** these latest scorebug appearance corrections; use the current comparison PNG for the gray logo/glass/crops. Neither deliverable is game footage. The three new SVG branding sources are explicitly retained by Git; the existing user/reference PNG media remains local under the prior media ignore rule.

The follow-up `outputs/snf-previews/review-current/SNF-animations-current.mp4` is now the current animation review: 57.5 seconds, 720p/30fps, newly rendered from the corrected package. It contains intro, watermark, context stats, receiver stats, paginated profile, large feature, timeout and replay sequences. The sampled transition frames and exits were inspected; the assembled H.264 stream decodes without errors. A looping receiver GIF is supplied for automatic inline playback. Scene-specific temporary WebView2 profiles let independent renderer processes work without sharing a cache. No game/renderer rebuild or native analysis run was performed.

## Scorebug correction after the reference mismatch report — version 0.4.1

The user clarified that the scorebug must be compared with the original NBC photo, not with our animation preview. This pass corrects the actual HTML presentation. No build, native analysis or game launch was performed; the collaborator hold and protected-file list remain in force.

- Restored the **existing** `assets/plates/pod.png` at its original 101×94 placement (bug-local 237,0). It supplies the original rim, glass, silver peacock and rules. The earlier CSS ring and duplicate Paint-peacock rendering are hidden in the normal pod. The supplied Paint image remains unchanged and is still used in the other branding. FLAG hides the reference pod art and retains the gold state; scoring/compact transforms still apply to the same pod.
- Restored the **existing** `rail.png` at left 2/top 73/571×8, with live timeout marks above it. No raster image was generated or edited.
- Corrected home-divider placement, reduced the away divider's lower highlight, moved the home abbreviation to its reference bounds, and corrected the quarter numeral, ordinal spacing and underline. Quarter number and ordinal no longer touch.
- Replaced whole-string clock stretching with proportional per-character spacing and a raised two-dot colon. Preserved the live clock text, unknown-clock clearing, overtime and menu-return behavior. Away-record spacing is also corrected; the home-record experiment was reverted because it compared worse.
- Aligned clock/score shading with the actual glyph cap rather than the font line box. A lighter whole-score font test compared worse and was not adopted.

Read-only comparisons use the unchanged original `reference/reference.png` and the same LAR 42/CIN 27/15:00/2ND fixture. A neutral bright-pixel mask (minimum RGB >175, channel spread <45) measures overlap of glyph shapes, not overall package fidelity. Clock overlap improved from 0.459 before this pass to 0.820; away record from 0.303 to 0.637; home abbreviation from 0.688 to 0.834. The pod's logo/rule region now uses the same original artwork. These figures are diagnostic evidence, not a claim of an exact NBC match.

The foundry's [official Ringside character specimen](https://www.typography.com/fonts/ringside/characters) and [width/weight guide](https://www.typography.com/fonts/ringside/how-to-use) provide a much stronger font lead. In-memory glyph comparisons point to Extra Wide Ultra for LAR/CIN and a narrower Black variant for the large scores. This is inference from shapes and proportions; no primary source yet confirms NBC's exact family/weights. No fonts or foundry glyph assets were downloaded, copied into the package or purchased. A single replacement face for every role would not establish a correct match. The actual rendered clock 1 already lacks a bottom foot; its upper flag/stem is the mismatch, correcting an earlier research summary.

`node tools/nbc-snf/verify-package.js`: **75/75** checks pass. Five new behavior checks cover clock reconstruction, changes through 9:24/0:59/0:05/0:00/OT10:01, repeated updates/record changes, unknown clock clearing, and off-air/menu return without stale or duplicate glyphs/colon carriers. Reference, FLAG, away-possession, drive-foldout and scoring frames were rendered and visually inspected through the existing presentation renderer.

The updated `font-review.html` shows only the original reference and the actual current render at 2×. The current still is `outputs/snf-previews/review/scorebug-refinement-final.png`; the current comparison is `outputs/snf-previews/review/scorebug-reference-comparison.png`. The earlier 57.5-second reel predates these 0.4.1 scorebug corrections and should not be presented as the latest scorebug appearance. Native game timing and exact font fidelity remain unverified.

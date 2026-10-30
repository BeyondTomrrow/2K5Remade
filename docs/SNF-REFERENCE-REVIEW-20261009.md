# NBC SNF presentation review — October 9, 2026

Version 0.4.0 follow-up: see [broadcast-scenes additions](SNF-BROADCAST-SCENES-20261009.md) for the matchup open, timeout score bumper, persistent corner mark, player profiles, context card and expanded 69-check validation. The sections below preserve the earlier 0.3.0 review; its short timeout notice has been replaced by the new score bumper.

The presentation package has been revised and rendered for review. This is not a release build or proof of in-game trigger correctness. The user's hold on builds, native analysis executables and game launches remains in effect.

## Target and reference boundaries

The requested 2022 designer package remains the baseline. The latest user-supplied Brady image defines the compact stat insert: player/team on the left, stacked position/number, name and contextual caption above five columns, translucent black blending into the team color, and a curved silver endcap. It is a different graphic from the large Mahomes feature and the scorebug-attached player insert.

Primary sources reviewed:

- [Jacob LaPlante: 2022 SNF Insert and Scorebug System](https://www.behance.net/gallery/154057369/2022-SNF-Insert-and-Scorebug-System). Inspected images and animated GIFs for the circular clock pod, penalty state, down/distance, attached stats, scoring drive and larger player features.
- [2023 official NFL Cowboys–49ers footage](https://www.youtube.com/watch?v=x-fWSwgSluE). Directly viewed at 5:53: circular pod, team panels and possession strip; this footage places the play clock beside the quarter inside the pod.
- [2024 official Eagles–Rams footage](https://www.youtube.com/watch?v=0N5AZ5Fx44M). Directly viewed at 9:09: the older pod system remains visible.
- [2025 official NFL Packers–Steelers footage](https://www.youtube.com/watch?v=M8DrSiTBbd0). Directly viewed: first-quarter game clock 13:38, GB 0 / PIT 0, play clock 40 beside the quarter. The player's seek-time UI was inconsistent, so this observation uses the visible game clock rather than a claimed video timestamp.
- [January 2026 official NFL Chargers–Patriots footage](https://www.youtube.com/watch?v=r4w8LaKWuGM). Directly viewed at 1:34 earlier in this review: slimmer rounded team panels are visibly distinct from the original 2022 reference.
- [Onecrew: Super Bowl LX on NBC](https://www.behance.net/gallery/243831675/Super-Bowl-LX-on-NBC). Read the designer's February 2026 project and its distinctions between player interstitials, team text, matchup and other motion families. These are not interchangeable with a compact stat card.
- [Onecrew: Sunday Night Football, September 2026](https://www.behance.net/gallery/255604027/Sunday-Night-Football). Inspected the page, its video embed and a stadium/camera graphic. The reel was located, not fully reviewed frame by frame.

2025 NBC archived clips were located, but the Pickens clip explicitly displayed an expired-video copyright restriction. A Packers archived page advanced to unrelated 2026 material. Those attempts do not verify 2025 motion. No restriction was bypassed and no external broadcast media was downloaded into the package.

The original designer's [down/distance GIF](https://mir-s3-cdn-cf.behance.net/project_modules/1400/c51ca1154057369.690a1ac41df97.gif) places the play clock under the pod. That placement is retained here, rather than silently mixing the inline broadcast variant into the selected designer layout.

## What changed

### Presentation package

- Starts off air before receiving game data. Invalid game state, play selection, pause and match changes clear stale cards and scoring timers.
- Compact stats now follow the Brady reference's five-column QB structure: COMP, ATT, YDS, TD/INT, RATING. Receiver, running back, defense, kicker and supplied punter fields select their own layouts. Unknown unsupported values use a dash rather than invented numbers.
- The stat panel blends translucent dark glass into the supplied team's color. It has the curved silver endcap, left portrait/team treatment, stacked position/number and an independent caption. The portrait, rail, name and columns reveal in stages; the rail has a moving sheen.
- The user's original Paint peacock is retained as an unchanged PNG. An SVG compositing filter removes its white backdrop at render time, keeping the colored feathers. The first attempted matrix-only filter produced black; the final matrix-plus-composite was visually checked in WebView2.
- Large features use the supplied team color, portrait and staged table. They no longer invent “playoff magic,” career, season or two-season labels from current-game numbers. A two-column dataset must be explicitly supplied. Preview comparison numbers are marked examples.
- Added a distinct scorebug-attached player-stat foldout: stacked scores on the left, relocated pod, player portrait/name and a rail of stat columns on the right. It does not reuse the compact free-standing stat card.
- Added separate two-player comparison and small team-stat inserts. The comparison's curved center table grows from below, paired portraits and team-color rails rise, and table rows appear in sequence. The team table slides open at lower left with a heading, columns, row reveals and silver rail. Both consume supplied values, reject malformed replacements, reset at menus/transitions and cooperate with the existing cards. Neither currently has a mapped native guest producer.
- Scoring sequences queue briefly, preserve the score captured for each event, clear safely off air and restore the scorebug. Later extra-point data no longer contaminates the pending touchdown score tick.
- Added short timeout, two-minute warning, quarter, halftime, overtime and final notices.
- Added a replay transition using rendered chrome/glass curve components. It can begin while the native scorebug is hidden, and is canceled by an inactive game, pause or play-selection screen.
- Lineup player swaps now run normally in previews. A delayed lineup exit cannot reveal a scorebug on a menu.
- Removed the obsolete duplicate preview block from the live script. Sample events live in a separate preview file, and deterministic frame timing only activates with explicit preview parameters.
- Package version is now 0.3.0.

### Native stat bridge — source changes only

`src/nfl2k5_presentation.cpp` now copies the supplied player identity into the queued event instead of rereading the possession-side QB identity when the event is dequeued. Full name, number, portrait/photo ID and requested display time survive the queue. A proven QB-name change clears the previous QB identity. Unknown internal jersey numbers use -1 and serialize as null; actual zero remains valid. The header comments define -1 as the unknown photo/jersey sentinel without changing the C structure layout.

The opcode-133 card callback currently receives only a title and native kind. It carries no selected player pointer or historical dataset. Its native kind is not the public stat-family enum. The previous implementation claimed Receiving, Performance, Today Stats and career/last-season cards, then supplied the current quarterback's game statistics.

Those unidentified families now return to their native card. Only the specifically QB-related consecutive-incompletions title retains the existing live-QB path, after a valid sample exists. Typed events through `nfl2k5_broadcast_player_stat` can carry any supported stat family, but no ordinary non-QB guest producer was found in the current source. Mapping the real selected player and scope remains required before all ESPN cards can safely be replaced.

The final source-only mapping check found that `main.c` already probes the full card context, but forwards only title/kind into the presentation callback. Existing logs prove direct non-QB identities at `ctx+0x14` for some cards; other cards store an entire roster table there. This is useful evidence, not a universal extractor. Exact source/log locations and current-match versus season-scope evidence are recorded in `docs/SNF-NATIVE-PLAYER-MAPPING-NOTES-20261009.md`. No speculative memory hook was added.

These bridge changes were read and statically reviewed by a second agent. They have not been compiled or exercised in the running game. An already-built executable still contains its previous bridge behavior.

## Event coverage and limits

| Event/family | Package behavior | Remaining limit |
| --- | --- | --- |
| PLAYER_STAT | Timed compact insert, correct numeric family, player identity and caption | Non-QB guest extraction still needs mapping |
| PLAYER_STAT with placement=scorebug or scope=drive | Separate attached foldout | Native producer does not yet supply this placement/scope |
| PLAYER_FEATURE | Large portrait/table with supplied metadata | Historical datasets and selected player are not extracted by opcode 133 |
| LINEUP | Team ribbon, portrait, name, position, college and player swaps | Preview uses still portraits; no player introduction video assets |
| TOUCHDOWN / FIELD_GOAL | Existing scoring takeover, drive foldout, score tick, return | Exact broadcast 3D scoring render is not available |
| EXTRA_POINT / TWO_POINT_CONVERSION / SAFETY | Short scoring takeover | Rendered recreation, not a verified original scoring asset |
| PENALTY | Yellow flag treatment in the pod | Visual treatment follows inspected designer animation |
| TIMEOUT / TWO_MINUTE_WARNING / QUARTER_ENDED / HALFTIME / OVERTIME / GAME_ENDED | Short glass notice | Exact broadcast notice/wipe timing not yet verified |
| REPLAY_STARTED / REPLAY_ENDED | 1.1-second code-rendered transition | Exact replay wipe is unverified; fonts/3D camera assets are substitutes |
| Interception, fumble, sack, injury | Existing native events remain available | No invented standalone SNF graphic family was assigned |
| PLAYER_COMPARISON / TEAM_STATS | Separate supplied-data components with staged reveals | Package events are ready; no native guest producer or full dataset mapping yet |

The user asked for an exact match. This revision does not claim that standard fonts, portrait fallbacks, notice motion or the recreated replay wipe are NBC's original assets. In particular, the 2026 interstitial package has been studied separately and has not been silently merged into the 2022 target.

## Review outputs

Open `mods/presentations/NBC_SNF/preview.html` for the scene selector, Replay and Play all. It runs the actual package with sample data. It does not launch the game. The full sequence lasts about 70 seconds; Play all allows longer scoring and showcase scenes to finish before advancing.

The following recorded previews are in `outputs/snf-previews/review`:

| File | Duration | Content |
| --- | --- | --- |
| player-stat-motion.mp4 | 7.73 s | Compact QB insert entrance/hold/exit |
| player-feature-motion.mp4 | 8.73 s | Large feature with staged portrait/table |
| attached-player-motion.mp4 | 8.47 s | Scorebug-attached player foldout |
| touchdown-drive-motion.mp4 | 12.43 s | Scoring word, drive foldout, score tick, restoration |
| replay-wipe-motion.mp4 | 2.83 s | Replay transition recreation |
| player-comparison-motion.mp4 | 9.47 s | Paired portrait and curved comparison-table reveal |
| team-stats-motion.mp4 | 9.47 s | Compact team/pass-distribution table reveal |

`SNF-motion-review.mp4` combines these seven previews into a 59.13-second review reel.

These are actual rendered package motion frames at 1280×720/30 fps, with completed hold frames repeated between motion intervals. They are not game recordings. Brady, Dillon and Brown previews use copied original-game portraits, not generated modern-player stand-ins. The large portrait is visibly limited by the original image resolution.

The existing `tools/presentation-render/presentation-render.exe` rendered PNGs through its existing WebView2 host; it was not rebuilt. `tools/nbc-snf/render-review.ps1` reproduces the stat, feature, attached-stat, scoring and replay recordings. Other scenes can be watched in the gallery.

## Verification

- `node tools/nbc-snf/verify-package.js`: **29/29** checks pass, loading the actual HTML/CSS/runtime/scripts with virtual timers. Includes startup/off-air, invalid state, menu/paused state, queued scoring reset, stat expiration, lineup restoration, numeric families including zero stats, stale QB identity, jersey zero, malformed/off-air events, drive-foldout lifecycle, replay lifecycle with the native scorebug hidden, and comparison/team-table lifecycle and coexistence.
- `node --check` passed for the preview clock and scene scripts.
- `git diff --check` passed for the edited package, bridge/header and review tools.
- The seven clips and combined MP4 were inspected with ffprobe: all 1280×720, 30 fps, valid durations. Representative rendered entrance, resolve, score-tick and restored-scorebug frames were viewed.
- The gallery was rendered through the same existing host and visually inspected. Its local-file opening was rejected by the Codex browser's security policy. No server or browser workaround was used.

## Files changed or added in this work

- `mods/presentations/NBC_SNF/mod.json`
- `mods/presentations/NBC_SNF/scorebug.html`
- `mods/presentations/NBC_SNF/scorebug.css`
- `mods/presentations/NBC_SNF/scorebug.js`
- `mods/presentations/NBC_SNF/extra-inserts.js`
- `mods/presentations/NBC_SNF/extra-inserts.css`
- `mods/presentations/NBC_SNF/preview-clock.js`
- `mods/presentations/NBC_SNF/preview-scenes.js`
- `mods/presentations/NBC_SNF/preview.html`
- `mods/presentations/NBC_SNF/assets/preview/brady-2004.png`
- `mods/presentations/NBC_SNF/assets/preview/dillon-2004.png`
- `mods/presentations/NBC_SNF/assets/preview/brown-2004.png`
- `src/nfl2k5_presentation.cpp`
- `src/nfl2k5_broadcast.h` (sentinel comments only)
- `tools/nbc-snf/verify-package.js`
- `tools/nbc-snf/render-review.ps1`
- This review and the separate source inventory.
- `docs/SNF-NATIVE-PLAYER-MAPPING-NOTES-20261009.md`
- Generated PNG frames, screenshots, MP4s and renderer temp profiles under `outputs/snf-previews`.

The repository already contained other people's changes. This list is not a claim that the entire working-tree diff belongs to this task. No commit, push, release packaging, game build, native analysis executable or game launch was performed. The protected SoftDrink generation/analysis directories, mod packs, recomp tools, memory/kernel/GPU files, mod-pack source and video-menu source were left alone. Discord was skipped.

## Required next work after the shared-build hold is lifted

1. Compile the reviewed bridge changes in the collaborator's agreed build, then verify supplied player identity and duration in a real match.
2. Map each native ESPN card's selected player and stat scope; do not treat the title/kind as a player identity. Add typed receiver/RB/defense/kicker and historical producers before claiming those cards.
3. Verify lineup/stat/scoring/replay transitions through actual game recordings, including Play Now, paused/play-call screens and match exit. This review does not address SoftDrink crashes or Berman face vertices.
4. Obtain viewable original replay/scoring/notice motion references and usable fonts/3D renders before claiming exact broadcast fidelity. Connect the new comparison and team-stat inserts to their real game datasets and native triggers.

# SNF pregame graphics — October 10, 2026

Package draft: **0.4.2**. The user confirmed **one player per team: the best player on each loaded roster**. The current task replaces the opaque matchup composition and prepares the native CAREER fact and COIN TOSS banners pictured by the user. This document supersedes the old matchup-trigger description in `SNF-BROADCAST-SCENES-20261009.md`; it does not declare a tested game release.

## What is ready

- The matchup has no opaque full-screen backdrop. The live camera remains visible behind the players, large team logos and central SNF marquee. Only the team ribbons and venue footer have glass plates.
- There is one selected player on each half, ranked by supplied actual overall across all positions. Missing ratings do not produce a guessed selection or default QB. The native source ranks normalized values before converting the winner to the game's displayed integer overall.
- Supplied player renders take priority, then portraits. Existing original-game headshots are shown at a smaller size instead of being stretched into fabricated body renders. No game-render producer or new player photography was added.
- The title only opens on an explicit `context.pregameStage` of `warmup` or `panning`, with a valid fresh phase-zero pregame. A crowd-stage request waits. Coin toss, kickoff, a running clock, menus or other inserts cancel it. `GAME_STARTED` alone cannot open a title over kickoff.
- Separate SNF career-fact and coin-toss strips use the owning event's exact player, team and text. They do not substitute the current QB, calculate career totals, guess toss outcomes or intercept controller input.
- Both strips have a team-glass wipe, delayed logo/text reveal, chrome edge and sheen. Native hide or the supplied duration starts a 350ms exit, retaining the same text through the exit. Replacement cards, menus and football phase changes clear immediately and cancel pending work.
- The user-created peacock remains unchanged. The small coin-toss mark uses the existing white-key SVG filter, removing its white rectangle.
- A host acknowledgment is sent only for a valid native `replacementToken`, after two animation frames, while that exact card still owns the display. Reset sends the matching hide acknowledgment; stale acknowledgments cannot acquire a newer card.

## What is still pending

**No game build, launch or native analyzer was run.** The user's instruction to wait until Claude reports back remains in effect. A question asking whether that hold has ended was sent; no approval had arrived when this work was documented.

Two native integration points remain unresolved:

1. **Crowd → warmup/panning cue.** Existing traces record script opcode/type/handler but omit its operands and camera identity. The Jessie Bates capture confirms that particular CAREER fact can appear over warmups; it does not establish a general warmup boundary. No elapsed-time guess or kind-1 shortcut was added. The source currently publishes `coin_toss` and `kickoff` when proven, and `null` otherwise. Consequently the new matchup does not automatically open in a live game until the warmup cue is mapped.
2. **Original ESPN artwork suppression.** Acknowledgment/ownership code is prepared, but no draw hook has been wired. The internal `k_native_pregame_draw_hook_ready` guard stays false. Native replacement events, ownership tokens, the draw query and new guest rating capture remain disabled until integration is proven. A future build of this draft retains the original native banners instead of drawing two competing cards.

The original NBC video/3D title assets, exact broadcast fonts and full-body player images are not included. This is a working composition and motion review using the supplied references and existing assets; it is not an exact-match or in-game verification claim.

## Native source preparation

`src/main.c` forwards existing popup show/close hooks and contains an opt-in `NFL2K5_DIAG_SNF_PREGAME` trace. It logs the bounded next eight operand bytes, script cursor/op/type, frame/time, native mode words and camera pointers. It does not assign stages or mutate guest state.

`src/recomp_manual.c` contains the new guest-lane roster helper. It uses original `sub_00246D80` for normalized overall and `sub_00246D90` for the displayed winner's rating; it does not average attribute bytes. It preserves guest registers, x87 state/control, XMM values and lazy flags and checks the return ABI. It validates roster and rating dependencies before calling. Snapshot capture is gated and occurs on the existing popup path, never on the Windows presenter thread.

Roster ownership is away `E5FC60+1C → B30A58`, home `E5FC20+1C → B30864`; the count belongs to the pointed roster at `+11C`, not the team object. A missing winner or portrait on one side does not invent data for that side or discard a valid opposite-side winner. Winner identity accepts validated fixed-up or field-relative name fields.

`src/nfl2k5_presentation.cpp` and the new `src/nfl2k5_presentation.h` prepare:

- Owned per-side `featuredPlayers`, bound to clock object, loaded roster/count and team alias.
- COIN TOSS exact show/message/lifetime/hide forwarding. FLT_MAX interactive lifetime remains unknown; no fixed five-second timeout is invented. Result-only text that does not name a team cannot acquire a guessed team.
- A strict native kind-1 single-player CAREER table: slot/kind, selected record, dimensions/cells, exact strings, portrait/jersey and native timeout. Kind 22 CAREER STATS is a different graphic and is not misclassified as this fact strip.
- Positive replacement tokens, exact copied-slot ownership and per-host-generation acknowledgments. The draw query requires the current native popup to match the acknowledged title/message or selected player/cells, layout, match ownership and fresh pregame. It performs no popup writes.
- The false readiness guard described above, preserving native graphics until the drawing integration is complete.

`src/presentation/presentation_host.h` adds an optional bounded page-message callback. `webview2_host.cpp` accepts it only from the exact configured entry URL, on the host thread. Other pages/frames cannot acknowledge a native replacement. Existing packages need not provide this callback.

## Draw-path evidence for the next pass

- Popup show/copy: `sub_0008ACF0`. Popup update/timers: `sub_0008A840`. Close/selection callbacks: `sub_0008A340`. Do not deactivate slots or force timeout zero to hide artwork.
- `sub_0008A9A0` loops the two active slots, then calls `sub_0008A170`. A full skip looked promising but was rejected after inspecting its readiness callbacks, first-draw latch at `slot+F1C`, and dynamic `sub_001BCB00` callbacks.
- `sub_001BD5A0` dispatches popup subtype visuals, including interactive choice highlights. `sub_001BCE50` also changes node transforms, alpha/animation and dynamic layout callbacks. Skipping these two functions is not yet established as a complete safe replacement.
- If investigating their calls later, preserve native stack cleanup: at the A170 call sites there are respectively five and three argument words already pushed. Their native cleanup is ret20/ret12. No such skip was implemented here.
- `sub_001C1530` registers 32 layout entries from `AAD180`, stride `6C`. The layout-E descriptor list pointer is at `AAD7A4`. Its dynamic callbacks still need verification or a narrower visual-sink gate.

After the hold ends, correlate the new script/camera trace with marked last-crowd and first-warmup frames, then install and validate the draw-only replacement. Check that coin choices, callbacks and timers still work; record the actual loaded roster winners, crowd, warmup, fact, toss and kickoff. Use the hardware renderer as requested by the user.

## Frontend files changed

- `mods/presentations/NBC_SNF/broadcast-scenes.js` and `.css`: transparent player matchup, one highest-overall winner per side, strict stage/clock ownership and cancellation.
- New `pregame-inserts.js` and `.css`: fact/toss layouts, native ownership, entry/exit motion and acknowledgment.
- `scorebug.html` and `scorebug.js`: load the components and hide competing HTML cards/bug while the owned insert is active.
- `preview-scenes.js`, `preview-clock.js`, `preview.html`, new `pregame-review.html`: fixtures, review links and deterministic frame review. Explicit `preview=...&motion=full` shows complete motion even when this machine requests reduced motion; live game behavior still honors the operating-system preference.
- `mod.json`: draft version 0.4.2, declares `pregame_cards` capability for the guarded native integration.
- `tools/nbc-snf/verify-package.js`: **107/107** frontend lifecycle checks pass, including mixed-position highest rating, missing/cross-team data, intro timing/late-event rejection, exact fact/toss strings, 350ms exit ownership, menu/kickoff clearing and stale-token cancellation. These checks do not compile or execute native code.
- `tools/nbc-snf/render-review.ps1`: actual frame samples for the corrected pregame timings and a labeled presentation-preview export. The existing renderer executable was used, not rebuilt.

## Review artifacts

- Still review: `outputs/snf-previews/pregame-20261010/pregame-review.png`. The checkerboard is visible through the HTML camera area, demonstrating the composition's transparency. The standalone PNG renderer flattens alpha against its host background; the PNG itself is not claimed to have a transparent alpha channel.
- Matchup fixtures use historical Corey Dillon/Tom Brady headshots and illustrative ratings, clearly labeled as sample inputs. A real game must supply its own loaded roster data. The CAREER fixture uses the exact supplied Jessie Bates fact; no current league claim is made.
- Motion clips/reel are offscreen presentation previews, not game footage. Earlier 0.4.0/0.4.1 animation reels do not represent this new matchup.
- Final verified reel: `outputs/snf-previews/pregame-motion-20261010082326/revised2/SNF-pregame-animation-preview.mp4`, H.264, 1280×720, 30fps, 704 frames, 23.47 seconds. Full decode passed. Entrance/exit samples, native-message update fixture and no late intro at kickoff were inspected. Fresh initial frames removed the unrelated scorebug flash. An identical copy is in the chat workspace at `outputs/snf-animations/SNF-pregame-animation-preview-20261010.mp4` for inline playback.

Protected collaborator files/directories, SoftDrink build/generated outputs and unrelated working-tree edits were left alone. No source ZIP, release executable, installer, commit or upload was produced in this pass.

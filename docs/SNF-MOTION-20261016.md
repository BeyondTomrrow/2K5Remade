# SNF transition and period graphics pass — October 16, 2026

## Implemented

- `broadcast-transitions.js` owns replay-in and replay-out. It exposes covering, covered, revealing and hidden phases; coalesces duplicate events; cancels on resumed play, menus, invalid state or a changed match; and never controls a game camera or guest execution. Other SNF inserts release their ownership before its first paint and reject competing inserts until it clears.
- Replay-out reverses the sweeping planes without mirroring the supplied SNF mark. The existing peacock and full wordmark replace the old plain-font title. The cover includes the extra viewport width at ultrawide aspect ratios. Reduced-motion mode uses a short dissolve.
- The reference-derived two-row score display now supports end-of-quarter, HALFTIME, OVERTIME and FINAL. It coalesces the native `QUARTER_ENDED -> HALFTIME/OVERTIME -> QUARTER_STARTED` batch; the last event no longer immediately hides the break graphic. FINAL requires `GAME_ENDED`, and is never inferred from the fourth quarter.
- Summary scores refresh from state updates. The next period's reset clock is omitted from the ended-period graphic. A bounded hold is followed by a 700 ms owned exit, so the regular scorebug cannot overlap the outgoing rows. Resumed play/menu/invalid state cancels the queued or visible summary and all its timers.
- Browser fixtures now reproduce the native event batch and include a separate replay return. No gameplay, audio, native renderer, or generated recompilation code was changed.

## Verification

`node tools/nbc-snf/verify-package.js`: **139/139** static lifecycle checks passed. New checks cover native period event batching, live score updates, summary priority, exit ownership, competing inserts, replay phase/direction, duplicate notifications and interrupted playback.

The existing standalone WebView2 renderer captured actual package frames into `outputs/snf-previews/transitions-20261016/`: replay-in/out and halftime MP4s, quarter/halftime/overtime/final PNGs, and a 2560×1080 replay cover. These use illustrative sample data, not a live game capture. The source page can be reviewed at `mods/presentations/NBC_SNF/preview.html`.

Visual review caught the quarter footer showing only END. The supplied TD font has a 25-em space advance (25,000 units at 1,000 UPM), pushing the remaining words onto clipped lines. The summary title now uses the package's existing space-only face ahead of the original letter face and stays on one line. A new rendered quarter capture verifies the full END OF 1ST QUARTER label. Font files were not modified.

No game launch, native build, game controls, process attachment, configuration switch or native trigger test was performed. Claude's running game was left untouched.

## Reference limits

The score-summary geometry follows the previously inspected two-row halftime reference. Its regular-season identity and motion remain adaptations. The replay curve/light sweep is a controllable reconstruction; there is still no verified exact 2022 NBC replay-wipe motion/3D source in the inspected reference inventory. This pass does not certify a broadcast-identical transition or in-game timing. The next permitted game test should verify native break/replay event timing without changing football state.

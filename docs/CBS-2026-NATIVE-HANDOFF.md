# CBS native player-card handoff — October 17, 2026

The HTML graphics previously had preview events but no acknowledged native
replacement. This pass adds that boundary without advancing title scripts or
changing the football simulation. It does not complete every CBS graphic.

## Ownership

`mod.json` must declare both `replaces: ["player_cards"]` and
`player_cards_ack: true`. Packages without the new capability retain their
existing behavior. CBS's shared manifest now enables this capability after
the native bind, paint acknowledgement, artwork and close were captured.

The existing one-shot player-card probe examines a script/configuration
object. It is not the active popup. For the currently supported
`consecutive incompletions` request, the bridge copies a real QB sample and
validates its clock object, both rosters and team abbreviations. It returns
zero from the probe, preserving the game's original timeout and callbacks.

At the existing recurring popup-frame hook, the bridge can bind only a
matching, active, nonclosing slot at `0x00B61B50` or `0x00B62A70`. The title,
layout, match, package and WebView generation must still agree. An unsupported
card or failed validation keeps its original artwork.

Delivery includes a canonical positive uint32 token encoded as a string:

```js
{name: 'PLAYER_STAT', nativeCardToken: '123', nativeCardOwned: true,
 team: 'home', player: {/* copied live identity and statistics */}}
```

The page acknowledges through `NFL2K5.log` only inside the game, after the
440 ms entrance, font readiness and two animation frames. It checks current
ownership, permitted game context and a visible, nonzero detail plate first:

```text
NFL2K5_PLAYER_CARD_SHOW:123
NFL2K5_PLAYER_CARD_HIDE:123
```

The existing host validates the exact entry-page source. Its callback only
updates synchronized host flags; it never reads/writes guest RAM or invokes
guest code. The native draw hook independently rechecks the exact active slot
and match before suppressing its artwork. Native update and close callbacks
continue running.

Native-owned HTML cards have no separate six-second hold deadline. The real
close emits token-matched `PLAYER_STAT_HIDE`, which owns one 930 ms exit.
The bridge retains suppression through the native close animation, preventing
the original card from reappearing during the replacement's exit. Pause,
play selection, invalid state, package/match changes and host reset release
ownership. Duplicate closes and stale acknowledgements cannot affect a new
card.

## Verification

- Release native build passed with `tools/build.ps1 -Game`. No XBE regeneration
  was performed, and no direct generated-code, APU or gameplay edits were made.
- `node tools/cbs-scorebug/verify-player-ack.js`: **212 assertions, 17/17 cases**.
  Covers delayed fonts, separate paint frames, reduced motion, invalid/maximum
  tokens, stale generations, match/context changes, duplicate events and
  native-owned close timing. These are controller tests, not pixel proof.
- Pre-edit native files were copied to
  `outputs/cbs-2026/native-integration-before/` to preserve the already-dirty
  working tree.
- `tools/cbs-scorebug/native-review.ps1` creates an isolated runtime with its
  own saves, settings, input and logs. The composed D3D11 captures use
  `NFL2K5_PRESENT_SHOT`; guest-surface dumps alone omit the HTML overlay.
- Native Quick Game verified real live QB inserts and the supported native
  card replacement. `native-review-qb01/stderr.log` shows token 1 binding
  `0x00B62A70`, a copied **Q. Carter 0/4, 0 YDS, 1 INT** payload, page SHOW,
  native close, token-matched HIDE and slot release. Composed frames 797–804
  verify artwork and exit. No test events or made-up statistics were used.
- Saved captures: `outputs/cbs-2026/CBS-native-QB-Carter.bmp` (drive insert,
  0/2 and 1 INT) and `CBS-native-card-handoff.bmp` (native card, 0/4 and 1 INT).
  These are actual D3D11 output at the review's 640x480 window, captured at
  half size. They are functional verification, not a high-resolution visual
  fidelity claim.
- Fixed an isolated-review setup bug: WebView2 could not serve the shared
  `_runtime` script through a junction escaping its mapped root. The harness
  now copies that directory. Missing API initialization in that review setup
  was not a failure in the production CBS renderer.
- Final rebuild passed (`native-build-final-20261017.log`). A fresh
  `native-review-final01` used the shared manifest without capability
  overrides and reproduced the live CBS bug and automatic Q. Carter insert
  in a configured CPU-vs-CPU Quick Game. No crash reports were observed;
  the private processes were stopped after verification. A presentation-only
  patch against the pre-session dirty backup is saved beside the captures.

## Automatic drive-opening QB inserts

CBS also opts into `drive_qb_cards`. This separate presentation policy queues
one timed `PLAYER_STAT` for a new possession, after 1.2 seconds of continuous
eligible live-camera state. It requires the title's actual native scorebug,
scrimmage phase, no pause/play selection or active popup, and a current QB
sample with matching clock, both rosters and team identities. Unknown
possession restarts the eligibility delay.

The ledger survives browser-host recreation and resets with the QB cache's
match epoch. It uses the existing guest-side 500 ms stats collector; it does
not scrape text or change football state. These additional CBS inserts leave
original native cards intact. Other packages remain opted out by default.

Startup timing varies with profile loading. Fixed START/A/A/START delays can
overshoot or fail to leave the title screen. The corrected driver checks the
actual FSM stack: main menu `0x00515660`, Team Select `0x0052728C`, then Coach
Match Up `0x00585474`. On Team Select, the normal D-pad left moved port 0 from
home (1) to CPU (0); it remained zero after START and match loading. Only reads
of those guest state words and ordinary press-file input were used. A temporary
stick-input experiment was unnecessary and restored to the pre-session file.

## Remaining integration work

- The current adapter deliberately recognizes only the proven QB-specific
  request. Other cards need their real selected player, scope and layout;
  historical/career values must not be replaced with current-game totals.
- A request before the HTML host exists falls back to the original card.
- `REPLAY_ENDED` and `PLAY_STARTED` have no verified native producers.
- Ordinary quarter-end, halftime and return timing need distinct native
  lifecycle hooks. Current period-change events arrive together.
- Large comparison graphics accept explicit data, but no native producer has
  been verified. The vertical identity/history card is also unfinished.
- Native FINAL currently derives from clock/score/period; pending PATs and
  untimed downs require a stronger end-of-game predicate.
- Exact source fonts, full-length player images and original dimensional
  broadcast materials remain unavailable. Preview rendering is not a claim
  of complete or pixel-identical CBS coverage.

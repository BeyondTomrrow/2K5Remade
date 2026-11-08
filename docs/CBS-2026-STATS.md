# CBS player statistics and large comparison graphics

October 17, 2026. The initial standalone phase below used no native game.
The later native integration is now verified in a private CPU-vs-CPU Quick
Game; see [native handoff and evidence](CBS-2026-NATIVE-HANDOFF.md).
APU/audio, original game data and gameplay logic remain untouched.

## Inspected broadcast treatments

Source: [official NFL Chiefs/Cowboys full CBS broadcast](https://www.youtube.com/watch?v=PLR04ljZ5DI).
This is the 2025 Thanksgiving broadcast with the current compact Eyebar, not
a claim that the source game happened in 2026.

- Home player feature, 43.63–50.93s: portrait overlaps the home logo, raised
  name/number tab and team-colored multiline statistics extend to the right.
- Away player statistics, about 1156–1163.5s: mirrored left extension, portrait
  in the away logo slot, and four statistical columns. The observed columns
  are passing yards, rushing yards, total yards and touchdowns. The sample
  COM/ATT/YDS/TD/INT arrangement is an adaptation of that component; its exact
  label combination was not claimed to be independently filmed.
- Full-screen comparison, 307.80–323.05s: split team-logo opener, oval team
  frame, dark central table, title/subtitle, paired column headers, three
  comparison rows and optional full-length player image slots. Measured
  entrance is about 1,250ms and exit about 550ms.

Detailed measurements and saved reference locations:
[compact inserts](CBS-2026-PLAYER-STAT-REFERENCES.md),
[large comparison](CBS-2026-LARGE-STATS-REFERENCES.md).

## Implementation

- `stats.js` owns requested inserts, data validation, cancellation, duplicates
  and timers. `stats.css` renders the compact strips. Away and home layouts
  mirror their placement without reversing the text.
- `PLAYER_STAT` renders statistical columns. `PLAYER_FEATURE` renders the
  multiline variant. The supplied `event.player.kind` selects QB, RB, WR/TE,
  defense or kicker fields; irrelevant zero fields do not select the family.
- Abbreviated name, jersey, team color and portrait are event data. Zero
  counts and valid negative yardage survive validation. A missing portrait
  retains the team logo; no fake likeness is generated.
- Small motion uses a 440ms entrance, requested hold (six seconds by default),
  and 930ms exit. Exit removes the name tab first, then the body, then the
  portrait before restoring the team logo. The reference timing was sampled
  at 30fps; these are reconstructed layers, not original production meshes.
- `stats-large.js` and `stats-large.css` render `PLAYER_COMPARISON` with
  explicit players and row values. Three rows match the observed reference;
  support for one through six rows is an adaptation. Team colors, secondary
  accents, logos, labels, values and highlight side remain editable data.
- Pause, menus, invalid/mismatched games, play start, replay and touchdown
  cancel the previous insert. A new owner cannot be cleared by an old timer.
  The ordinary Eyebar is hidden during the full-screen comparison and restored
  afterwards. Extra ultrawide margins use the team background colors while
  the measured 16:9 geometry remains centered.
- The original source stadium plate, exact fonts, full-length player cutouts
  and 3D logo meshes are not supplied. Local 2004 headshots are used only in
  compact previews; they are not enlarged into the full-body comparison slots.
  This is not a claim of pixel-identical or complete CBS presentation.

## Game-data boundary

The current native serializer already supports `PLAYER_STAT` fields:

```text
event.team, event.displaySeconds
event.player.name, fullName, position, number, portrait, kind
completions, attempts, passingYards, passingTouchdowns, interceptions
carries, rushingYards, rushingTouchdowns
receptions, receivingYards, receivingTouchdowns
tackles, sacks, defensiveInterceptions
fieldGoalsMade, fieldGoalsAttempted, longestFieldGoal
```

Those fields are consumed directly. They are not scraped from rendered text,
filled from preview fixtures or synthesized as current game results.

The native replacement producer recognizes only `consecutive incompletions`
with a populated, match-owned QB cache. CBS now uses the explicit
`player_cards_ack` protocol: it preserves native timing and callbacks, binds
the actual active slot, and suppresses its artwork only after a visible
paint acknowledgement. Native Quick Game captured the full bind/SHOW/close/
HIDE lifecycle with real Q. Carter statistics. Unsupported cards keep their
original artwork. A separate `drive_qb_cards` policy supplies timed live QB
inserts on new possessions without replacing native cards.

`PLAYER_FEATURE` has a serializer mapping but no verified producing call.
There is no verified native `PLAYER_COMPARISON` or aggregate team-stat producer.
Large graphics accept explicit event payloads now; automatic native triggers
remain a separate integration task. Browser rendering is not an in-game test.

Example large-graphic contract (values must be supplied by the caller):

```js
{
  name: 'PLAYER_COMPARISON',
  title: 'QUARTERBACK COMPARISON', subtitle: 'THIS GAME', duration: 14.75,
  players: [
    { team: 'away', player: awayPlayer },
    { team: 'home', player: homePlayer }
  ],
  rows: [
    { label: 'COMP / ATT', values: [awayCompAtt, homeCompAtt] },
    { label: 'PASS YARDS', values: [awayYards, homeYards] },
    { label: 'TD / INT', values: [awayTdInt, homeTdInt] }
  ]
}
```

## Review tools

`preview.html` provides editable player identity and every supported stat
family, a separate multiline feature button, and full-screen comparison.
`stats-fixtures.js` is loaded only on preview pages, never the live entry.
Known sample names match the 2004 local preview rosters; unknown identities
are explicitly named Sample Player/Quarterback/etc. All numbers are sample
data, not historical or current NFL results.

The deterministic `stats-reel` scene exports a chaptered video covering the
five stat families, mirrored placement, multiline feature and large comparison.
The existing standalone WebView2 renderer is used. Reference footage is not
played over the game or included as baked graphical text.

Next: verify the native player-card visibility/acknowledgement handoff when
game testing is allowed; supply real full-length player images; add a native
comparison data/event producer; reconstruct further CBS layouts (including
the separately measured vertical identity/history card) from their own footage.

## Verified review

- Actual browser controls verified zero completions/attempts, negative passing
  yards, zero TD/INT, away-side placement, pause suppression, full-screen event
  rendering and same-player columns-to-multiline switching.
- Focused lifecycle regressions passed for all five families, duplicated and
  replaced events, old completion timers, stale portrait loads, pause/menu,
  replay/play cancellation, match changes, reduced motion and malformed data.
- Fixed two reproduced bugs: event type/caption missing from duplicate identity,
  and whitespace/nonprimitive counts being treated as valid zeroes. Numeric
  strings now normalize before formatting (for example `0/2`, not raw padding).
- Visual review fixed overlapping long completion totals, restored the live
  camera opening in the comparison, and added its own measured CBS wordmark.
  A 2560x1080 render verifies centered geometry and opaque side gutters.
- Final artifact: `outputs/cbs-2026/CBS-2026-player-stats-package.mp4`, **69.1s**,
  H.264, 1280x720, 30fps, silent. It includes the five families, away placement,
  multiline feature and complete comparison entrance/hold/exit. Decoded frames
  and contact sheets were inspected. `stats-watch.html` has chapter controls.
- Actual browser video playback verified 69.1s duration, decoded frames and a
  seekable range of 0–69.1s; chapter controls reach the comparison sequence.
- Those standalone checks were followed by a successful native build and
  actual CPU-vs-CPU verification; the supported native replacement is enabled.
  Exact evidence and remaining native producers are documented in the linked
  native handoff. Large-comparison preview footage remains sample data.

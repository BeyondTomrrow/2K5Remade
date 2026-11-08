# CBS ordinary-quarter evidence — October 18, 2026

Source: [official NFL Chiefs–Bills 2025 Week 9 full game](https://www.youtube.com/watch?v=4gxq8Na22nA), using the compact Eyebar family. This is a 2025 source, not a claim about a 2026 fixture. Private analysis evidence is in `outputs/cbs-2026/reference/regular-20261018/`.

Sparse storyboards locate candidates only. The accurately decoded bounded clip `q1-actual-1308-1342.mp4` and `q1-actual-contact.jpg` establish the actual Q1→Q2 sequence. The earlier 1365–1400 clip was already Q2, and must not be labelled a quarter-boundary capture.

- The live Eyebar shows 1st & GOAL, Q1, 0:01 until the period finishes.
- At approximately 1321.5s, the top center changes to the real CBS SPORTS mark, and the lower center reads **END 1st QTR**. Scores, records and the three timeout marks remain. `end-q1-1323.2.png` is the full-frame geometry reference: bug approximately x187–455, y304–344 at 640×360; center approximately x271–369. There is no giant full-screen logo in this part.
- Around 1325.5–1326.5s the edited full-game upload cuts to a custom Chiefs/Bills **THE REMATCH** promo, then a Mahomes close-up, aerial and a sponsored stadium shot. By approximately 1329.5–1331s it returns to the field and the live Eyebar, Q2/15:00.
- This establishes the ordinary Eyebar end label. It does **not** establish a reusable full-screen quarter bumper: the matchup-specific promo is not interchangeable with every quarter, and an edited NFL upload may remove commercial material.

## Implementation and timing

The native bridge now serializes `context.periodEnding`, based on the title's actual flow state at `E602B8`: `sub_000B8A00` chooses 0x09 at halftime or 0x15 at other period presentations. A clock of zero while flow remains live/post-play does not activate it. See the existing `docs/eras/research/RESULT-CAPTURE.md` and the generated guest body; no generated code was edited.

CBS uses this state for its observed Q1/Q3 END ordinal QTR center, hides the divider/game/play clock, and restores live clock layout as soon as the title leaves that condition. It does not add a wall-clock hold or play an invented wipe. Halftime, OT and final keep their separate existing paths; this source does not prove their complete graphics. The older period-increment events still arrive separately and are not the timing source for this label.

`period-sequence` is an explicitly labelled sample-data preview showing: live 0:02 → 0:00 while a play may remain pending → actual period presentation → next quarter. `period-end` is its static reference fixture. These browser fixtures are not native event evidence.

## Native evidence

The isolated `native-review-period01` used D3D11, ordinary CPU-versus-CPU controls, private saves/settings and the existing 20-second-quarter diagnostic option. That option deliberately shortens the test quarters; it does not inject presentation events or manufacture scores/statistics. Composed frame186 visibly shows CBS **END 1st QTR** in the game. The game subsequently progressed through Q2/halftime, Q3, Q4 and overtime. The read-only `period-flow-716995750.csv` records Q4 reaching 0:00 in flow0x12, then0x15, and progressing via the OT coin toss to period5; a tied Q4 was not incorrectly called FINAL.

The run subsequently emitted one real `GAME_ENDED` at host time717300.897 after Carolina took a3–0 OT lead. Composed frames794–799 show the original postgame player interview, with the CBS Eyebar hidden by native camera eligibility. This confirms delivery of the real final event, not a new claim that CBS FINAL artwork was visible during that interview. The private game was stopped by its recorded PID. No user's active package/settings/saves were changed.

## 1080p refinement

Bounded1080p clips `end-q1-1320-1325-hd.mp4` and `defense-1523-1541-hd.mp4` were then inspected. `end-q1-1323.2-hd.png` establishes the Eyebar's full-frame bounds at approximately x552–1368/y911–1029. Corrected the prior x540/y927/840px footprint using a uniform816/840 scale and the measured anchor. The compact player extensions use the same affine placement so their edge/bottom remain attached. Separately measured standalone receiving/team/comparison layouts are unchanged. The HD source also confirms the small ordinal suffix is uppercase, not lowercase.

The1529s graphic is Josh Allen **SACKED15TIMES THIS SEASON (14LAST SEASON)**. It is a historical QB feature, not a current-game defensive-player card. It does not justify relabelling live sacks as a season value or claiming a newly verified DEF skin.

Remaining fidelity limits include the original CBS font/3D materials, authoritative automatic replay completion, full ordinary halftime/final sequences and historical/season/period-scoped statistics. This is not a 100%-identical CBS completion claim.

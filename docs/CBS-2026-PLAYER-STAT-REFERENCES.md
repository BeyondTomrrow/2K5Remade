# CBS player inserts: measured references

Measured October 17, 2026 from the official NFL full game:
https://www.youtube.com/watch?v=PLR04ljZ5DI

The actual footage is **Chiefs at Cowboys, Thanksgiving 2025**. The current CBS Eyebar design matches the package being studied. Do not relabel this as a 2026 game merely because the package is named CBS 2026. Measurements below use the downloaded 640×360 reference, with approximately ±2 source-pixel spatial uncertainty. Fine motion sampled at30fps has approximately ±33ms timing uncertainty.

## Compact home-player insert

Reference:43.63–50.93s, Dak Prescott promotional passing fact. Screenshot:
`outputs/cbs-2026/reference/measurement/player-stat-041.png` (45s).

The insert grows from the **outer home edge of the Eyebar**. A portrait replaces the home-logo slot and overhangs the plate. The player's name sits in a separate raised rectangular header. The body is a flat navy team-color extension with white type, not a floating generic rounded rectangle.

At640×360 the live scorebug spans approximately x184–456,y304–343, W272 H39. Approximate insert bounds:

| Element | Reference pixels | Normalized to live plate W/H |
|---|---|---|
| Portrait | x422–482,y294–343 | x/W=.875–1.096; top/H=−.256; height/H=1.26 |
| Extended body | x455–610,y304–343 | right/W=1.566; width/W=.570; height/H=1 |
| Raised name header | x477–573,y286–305 | left/W=1.077; width/W=.353; top/H=−.462; height/H=.487 |
| Header text | approximately x479,y290 | White bold all caps; about10 source pixels high |
| Body text | approximately x490,y310 | White bold all caps; about8 pixels high,10–11px leading |

The portrait is a real supplied head-and-shoulders cutout. Its head projects above the scorebug; shoulders stop at the shared bottom edge. The live scoreboard's score/time/timeout data remain readable. Team logos return when the insert exits.

Actual header and content in this example:

```
D. PRESCOTT 4
309.4 PASS YDS/GAME
AT HOME THIS SEASON
(LEADS NFL)
```

These are **broadcast contextual season facts**, not current-match completion/attempt statistics. The template can be bound to live game data, but must not claim this footage proves a particular COM/ATT column design.

### Measured home entrance and exit

| Source time | Observed behavior |
|---|---|
|43.63s | Home team logo starts turning/deconstructing |
|43.67–43.83 | Team-color plate extends outward to the right |
|43.70–43.90 | Pale portrait silhouette builds in the outer logo slot |
|43.83–44.03 | Header and body text resolve horizontally/fade in |
|43.97–44.07 | Portrait resolves to photograph; all content settles |
|44.07–49.97 | Full hold, about5.9s |
|50.00–50.20 | Header letters disappear and header retracts down into plate |
|50.43–50.57 | Body clips from right toward the portrait |
|50.60–50.77 | Portrait returns to a pale silhouette |
|50.80–50.93 | Home team logo rotates back into the original slot |

Total demonstrated insert lifecycle is approximately7.3s. It is sequential: header leaves before body, then portrait, then logo restoration. A uniform opacity fade on the complete box is not the observed motion.

## Compact away-player live statistics

Reference:1156.2–1163.5s, Patrick Mahomes current-game yardage. Local bounded source covers1145–1170s:
`outputs/cbs-2026/reference/measurement/chiefs-compact.mp4`.
Screenshot: `away-compact-full.png` in the same folder.

**The away variant mirrors its placement to the left.** It does not place both teams' statistics on the right. Text remains normally oriented.

At640×360:

- Body x29–185,y304–343; it extends about.57 plate widths to the left.
- Portrait x157–215,y295–343, centered near the outer edge of the away panel.
- Name header x68–166,y285–305.
- Header: `P. MAHOMES 15`.
- Four aligned columns, small uppercase labels above larger values:

| PASS YDS | RUSH YDS | TOTAL | TD |
|---|---|---|---|
|41|16|57|1|

This demonstrates actual current-game statistical data. Required bindings for this exact treatment are player abbreviated name, jersey number, portrait, passing yards, rushing yards, total yards, touchdown count and team side/color. Total is41+16=57 in the shown example. Do not infer unavailable values as0.

This insert is already in place when replay-out reveals the live shot around1156.2s, so the clip does **not** establish its original entrance timing independently. It stays fully populated through1162.37s. Header removal begins1162.40–1162.60, body retracts1162.80–1162.97, portrait dissolves to silhouette1163.00–1163.23, and the away logo is restored by approximately1163.50. This repeats the home exit sequence with mirrored geometry.

## Left-side player identity/history card

The same full game also shows a larger left-side Prescott introduction31–41s:

- Intro begins approximately31s with a large Cowboys logo panel.
- Player collage/name card resolves about31.5s.
- Card turns through the Cowboys star around32.5–33s into a statistical back face.
- Card remains at approximately x22–221,y75–325.
- Heading hierarchy: small `DAK`, large condensed `PRESCOTT`, jersey4; `CAREER ON THANKSGIVING`; rows `RECORD4-3`, `PASS YDS/GM283.6`, `TD/INT13/5`; small `DALLAS COWBOYS` footer.
- Exit occurs around40.7–41.3s, before the compact insert arrives.

These are career/history facts. They must be supplied explicitly or omitted, not populated from one game's statistics. This is distinct from both the compact scorebug insert and the full-screen comparison being documented separately.

## Source and implementation limits

- Verified compact treatments use square-corner rectangular header/body plates and the team's dark broadcast color.
- The compact name and value face is a clean bold sans; the larger left-card surname is condensed. Exact licensed font identification is not established from this low-resolution footage.
- Do not introduce a fallback fake player head or convert a missing portrait into an invented likeness. A data-only fallback should retain the team logo and disclose the missing asset only in editor diagnostics.
- Current-game COM/ATT/YDS/TD/INT can use this data-driven component, but the two measured examples above demonstrate a contextual multiline fact and a four-column yardage line. No claim is made that the exact COM/ATT row layout was verified here.
- Original ESPN presentation, native football logic and running game were not changed for this analysis.

## Saved review artifacts

All under `outputs/cbs-2026/reference/measurement/`:

- `player-stat-contact.jpg`:25–49s overview.
- `compact-contact.jpg`:41–57s at5fps.
- `compact-motion30.jpg`:30fps home entrance/exit crops.
- `awaycompact-contact.jpg`:1145–1170s overview.
- `away-motion30.jpg`:30fps away reveal/exit crops.
- `player-stat-041.png`:home compact full frame45s.
- `away-compact-full.png`:away live-stat full frame1158s.


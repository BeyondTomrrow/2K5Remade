# MyCareer stat bar and grade (design note)

Our own layer over SOFTDRINK v0.9's MyCareer (`src/nfl2k5_mycareer_hud.c`, branch
`mycareer-hud`). The pack's files are not changed.

## Where it shows

Only where the pack itself would draw its "F. Last QB: CMP/ATT .." line: a MyCareer game, the
MyPlayer's team at the line before the snap (the pack hides it on the play-call screen, during
the play, in replays and when paused), and the pack's own "MyPlayer stat line" setting On
(that row stays the on/off switch). Our bar replaces that line (hook at 0x014EA522, continue at
0x014EA57C). It is hidden while Supersim runs, so it never sits on the Supersim panel.

Placement, measured in game: the HUD's 640 x 480 is drawn square (one unit as wide as tall),
centred, and clipped to x 0..640 also with the widescreen patch; y 448 is the bottom of the
window. The bar sits at the left, bottom edge at y 372, just above the bottom scorebug band
(the SNF scorebug starts at y 379, x 149), clear of the play art, which is only on the
play-call screen where the bar is hidden.

Look: MyCareer > Settings + Save > Settings > "Stat bar look: 2K5 / Modern" (our row, added to
the pack's list at run time; the pack's rows and its dispatcher are untouched). Stored in
`<root>/nfl2k5_mycareer_hud.ini` (`[hud]`, `style=1|2`), default 1. The same setting styles the
Supersim panel.

## Drawing, every renderer and platform

No host overlay and no WebView: everything is drawn by the game's own guest UI calls (its
immediate-mode primitives sub_0002D2A0 / 2CBE0 / 2CA70 / 2CA00, the path its rects take, and its
text sub_0006BC30 with its own fonts), so it goes through the NV2A command stream like the rest
of the HUD and comes out the same on D3D11, D3D12 and Vulkan, on Windows, Linux and Android.
Shapes (rounded panels, gradients, the hexagon, the grade letters) are triangle lists with a
one-unit alpha fringe, so their edges stay smooth at any render scale. The grade letters are
drawn as strokes (no font file); names, numbers and labels use the game's fonts F2 and F3.

## Stats (live, this match)

Read with the title's own current-match accessor `sub_000CB240` (player record, selector,
scope 0), the call the pack's line and Gamecast make. Selectors from
`data/eras/stat_selectors_2k5.json` (decoded from the XBE's tables 0xA8A510 / 0xA8B918).

| Group | Shown | Selectors |
|---|---|---|
| QB | CMP/ATT, YDS, TD, INT | 04, 23, 4C, 40, 16 (+ carries 03, rush yds 50, rush TD 3F, sacks taken 30 for the grade: getter 0x24E470 beside the passing getters, Gamecast "Sacks") |
| HB/FB | CAR, YDS, TD, REC | 03, 50, 3F+3E, 2E (+ rec yds 4F, fumbles 0F) |
| WR, TE | REC, YDS, TD, LNG | 2E, 4F, 3E, 1B (+ fumbles 0F) |
| DL, LB, DB | TKL, SCK, INT, PD | 32, B8 (half sacks x 0.5), 17, 22 (+ forced fumbles 11, TFL 34) |
| OL | PLAYS, TM YDS, SCK ALW | team record via `sub_000CB2D0`: rush att 03 + pass att 23 + sacks allowed; rush 50 + pass 4C yards; sacks allowed = the opponent team's B8 |
| K | FG, LNG, XP | C8/C7, 1C, 58/59 |
| P | PUNTS, AVG, IN20 | 2B, 4E / 2B, 2D |

Notes:
- "PD" is the game's selector 0x22, labelled "Tips" in Gamecast (passes tipped / defended).
- OL: the game has a PANCAKES column only in its history/scouting tables (`sub_0031CFB0`
  field 0x16), not in the live current-match selectors, so OL shows team plays instead.
- Stats are re-read every 200 ms while the bar shows; a change is one new play.

## The grade

A score from 0 to 100, shown as a letter. Every grade starts at C (66) and is pulled from it only
as the MyPlayer's involvement grows, so one early play does not swing it:

    score = (66 * k + E * n) / (k + n)

`E` is the position's performance estimate (clamped 20..100), `n` its involvement, `k` how many
plays of evidence weigh as much as the C prior. It is recomputed when the stats change (per play).

| Group | E | n | k |
|---|---|---|---|
| QB | 40 + 6.5 * a + 30 * (cmp% - 0.60), a = (pass yds + 20 TD - 45 INT + 0.8 rush yds + 20 rush TD) / (att + carries + sacks) | att + carries + sacks | 10 |
| RB | 40 + 7 * (rush + rec yds + 20 TD - 30 fumbles) / touches | touches | 8 |
| WR / TE | 66 + 25 * (P / X - 1), P = rec yds + 20 TD + 4 rec - 30 fumbles, X = 70 (WR) / 45 (TE) x game fraction | 2 rec + 6 x game fraction | 8 |
| DL / LB / DB | 66 + 25 * (P / X - 1), P = TKL + 4 SCK + 6 INT + 2 PD + 4 FF + 1.5 TFL, X = 4 / 7 / 5.5 x game fraction | TKL + 2 (SCK + INT + PD + FF) + 6 x game fraction | 6 |
| OL | 40 + 5.5 * team yards per play - 150 * sacks per play | plays / 3 | 8 |
| K | 30 + 62 * (FGM + XPM/2) / (FGA + XPA/2), + 3 for a 40+ long, + 6 for 50+ | FGA + XPA/2 | 3 |
| P | 66 + 2.5 * (average - 41) + 12 * inside-20 share | punts | 3 |

Game fraction = elapsed share of regulation from the quarter (0xE602C4), the clock object
(0xE6028C + 0x10) and the quarter length (0xE602B0); at least 0.05.

Letters: F < 48 <= D- < 52 <= D < 56 <= D+ < 60 <= C- < 64 <= C < 70 <= C+ < 74 <= B- < 78 <= B
< 82 <= B+ < 86 <= A- < 90 <= A < 96 <= A+. Colours: A green, B blue, C amber, D orange, F red.
When the letter changes the badge glows for half a second.

Rough anchors: a QB at 7 adjusted yards per play and 65% completions is a B+ by the half; an RB at
5 yards a touch is a B-; a linebacker with 7 tackles in a full game is a C, 10 tackles and a sack a
B+.

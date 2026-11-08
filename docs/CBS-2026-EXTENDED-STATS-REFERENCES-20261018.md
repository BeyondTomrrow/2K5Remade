# CBS extended statistics reference audit — October 18, 2026

The source is the [official NFL Chiefs–Cowboys Thanksgiving 2025 full game](https://www.youtube.com/watch?v=PLR04ljZ5DI).
It demonstrates the current compact CBS design used for this package; it is
not labelled a 2026 game. Source excerpts and measured frames are private
analysis artifacts under `outputs/cbs-2026/reference/extended-20261018/`.
Sparse storyboards across the game were followed by bounded clips and individual
frames. A sampled storyboard is not proof that every graphic in a game was seen.

## Verified graphics

Coordinates below refer to 640×360 source frames. Multiply by three for the
1920×1080 presentation canvas; keep the canvas centered at ultrawide sizes.

| Graphic | Source time | Content and geometry | Implementation consequence |
|---|---|---|---|
| Team yardage comparison | 1198.80–1203.13 | Table x175–463, y294–343; raised YARDS tab x299–341, y282–294. Three RUSH/PASS/TOTAL columns, two team rows. KC 64/41/105; Dallas 8/78/86. | Separate lower-third replaces the Eyebar. Authoritative team selectors supply values, never sums of player samples. |
| Current-drive extension | approximately 1066–1073.5 | Left body x30–185, y304–343; raised THIS DRIVE tab x68–166, y286–305. `5 RUSH, 1 PASS` and `47 YARDS, 3:17`. Time advances 3:13→3:19 during the hold. No portrait; team logo remains. | Team total TOP is not this-drive time. Native completed-drive summary supplies plays/yards/time, with an honest total-plays fallback when the rush/pass split is unavailable. |
| Kareem Hunt rushing | approximately 2668 | K. HUNT 29; **two columns**, RUSH 7 and RUSH YDS 43, attached away strip. | RB insert changed from generic four columns to the observed two-column treatment. |
| Javonte Williams rushing | approximately 5189 | J. WILLIAMS 33, RUSH 11 / RUSH YDS 44, home strip. | Confirms the mirrored home variant. An earlier storyboard identification as Kelce was incorrect. |
| George Pickens feature | approximately 2442 | REC YDS, LAST WEEK 146 / TODAY 22. | Mixed historical/current scope. Do not substitute a current-match value for LAST WEEK. This exact feature is not automatically generated. |
| Jake Ferguson feature | approximately 1673 | J. FERGUSON 87; 84.4% of targets **this season**, with minimum-target qualification. | Not evidence for a current-game TE receiving card. Season values are not fabricated from the current match. |
| CeeDee Lamb receiving table | approximately 6090 | Standalone x153–490, y309–343. Art/logo/portrait x153–233, portrait up to y294; position/jersey x233–261; first/last name x262–315; four black stat columns x315–490. 9 TARGETS / 7 REC / 112 YARDS / 1 TD. | New standalone WR component; separate full name/position/jersey, live receiving values and an unavailable target cell until an authoritative target selector is established. |
| Later team yardage | approximately 4763 | TOTAL YARDS; 1ST QTR 109/86 versus SINCE 152/200; Dallas outscoring KC 13–0 since the first quarter. | Period splits require separate scoped samples. This historical comparison is not relabelled as current totals. |

Full frames include `team-comparison-full.png`, `long-drive-full.png`,
`rb-hunt-full.png`, `rb-williams-full.png`, `wr-pickens-full.png`,
`te-ferguson-full.png`, `wr-lamb-table-full.png` and
`team-yards-later-full.png` where successfully captured.

## Measured motion and limits

Team comparison: Eyebar logos turn at 1198.80; the bug contracts around
1198.87–1199.00. Enlarged cropped team logos travel right to left at
1199.07–1199.17, become contours at 1199.20–1199.43, followed by row labels
at 1199.47–1199.60, RUSH/PASS/TOTAL cells at 1199.60–1199.87, and the YARDS
tab at 1199.90–1200.03. The source cuts to normal live presentation at
1203.13. The implemented **table portion** uses its measured 560 ms reveal
and a cut exit. The preceding wireframe-logo opener is not replaced with a
generic animation or claimed complete.

The drive graphic is already visible beneath the replay exit; its entrance
is not independently established by this excerpt. The tab retracts at
1072.4, then the body retracts approximately 1072.9–1073.5. The existing
compact lifecycle is reused, rather than claiming a newly measured entrance.
The exact Lamb-table entrance/exit remains unverified; it uses a cut.

No independent source frame has yet verified the exact current-game TE or
defensive-player skin. TE has a distinct native position and receiving data
binding. Defensive/team defensive values use verified native counters, but
their presentation reuses the measured CBS layout family. Original source
fonts and the wireframe opener remain fidelity limits. These are explicit
remaining tasks, not a claim of a complete or 100% identical CBS broadcast.

## Later October 18 verification

The full team opener is now reconstructed through its 1,230 ms sequence (real matchup image planes and image-derived contours, followed by the measured table reveal). The Lamb receiving table entrance was separately measured at 200 ms and implemented as that opacity transition, with a cut exit. See `CBS-2026-REFERENCE-20261018-opener.md` and `CBS-2026-FOLLOWUP-VISUAL-20261018.md`. Original dimensional meshes/materials and the exact font remain fidelity limits; the earlier unimplemented-opener/cut-entrance statements above describe the initial stage, not the current code.

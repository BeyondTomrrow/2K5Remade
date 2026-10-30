# SNF native card integration — October 11, 2026

This supersedes the integration status in SNF-PREGAME-20261010.md. The user lifted the earlier collaborator hold. These changes replace native broadcast inserts only for an HTML package declaring `pregame_cards`; the NBC SNF option declares that capability. ESPN menus and other presentation choices are separate.

## Implemented and observed

Six draw hooks now preserve native popup updates, selection callbacks, layout readiness and stack cleanup. They zero outgoing artwork alpha only after the configured page acknowledges an exact owned replacement. The readiness guard is enabled. No popup is disabled and no coin-toss result is calculated by the presentation.

Hardware-rendered SoftDrink v0.6 tests reached real player cards, the interactive coin toss, kickoff and progressing gameplay. Native picture captures, rather than desktop-window captures, are the evidence:

- `logs/snf-final-1011-186.bmp`: Chuba Hubbard, exact native rushing prose and mod portrait.
- `logs/snf-final-1011-192.bmp`: Tetairoa McMillan, exact native receiving prose and mod portrait.
- `logs/snf-final-1011-679.bmp`: South selected in the SNF coin-toss replacement; confirming advanced to kickoff.
- `logs/snf-timing-1011-201.bmp`: North selected with the actual configured A confirm prompt.
- `logs/snf-stat-1011-190.bmp`: Dexter Lawrence, the native Career caption and exact sacks prose.
- `logs/snf-stat-1011-201.bmp`: Jessie Bates, the native Career caption and exact tackles prose.

The latest native table bridge supports label/value pairs in each row as well as a label row followed by a value row. Kyle Pitts' captured native table has dimensions 2×4 and interleaved cells `Rec,88,Yds,928,Avg,10.5,TD,5`. Earlier code flattened this case into prose; the current code sends four exact stat columns. A new hardware capture of that table formatting is still pending.

Native captions and subject text are independent:

- Layout 1 callback `001C0A90` reads selector `ctx+F18` from the six-entry UTF16 pointer table at `AAD168`: empty, Season, Last Season, Regular Season, Postseason, Career.
- When `B72E84` is nonzero, signed `E602C4>=4` chooses FINAL at `E85B14`; otherwise HALFTIME at `E85B20`.
- Callback `001C0AF0` reads subject text from `ctx+4C`, such as Receiving. It is retained as a separate category strap.
- Prose subtype 3 uses inline UTF16 at `ctx+230`; `ctx+22C` is its character count.
- Table subtype 1 uses native column count at `+22C`, row count at `+230`, and cells at `+23C+i*88`.
- Cell type at `+234+i*88` selects inline UTF16 (0) or a pointer to UTF16 (1). Extraction and ownership checks share this resolver. Unsupported cell types and unproven 2×2 axis conventions retain the native graphic.
- Full popup copies retain the caption selector in the actual `B62A70` slot.

Portrait metadata and pixels now pass through the installed disc overlay. The stable archive entry ID locates dynamic offsets across all 36 archive volumes. The v0.6 runtime indexed 4,303 portraits. Pack-specific cache directories avoid reusing retail headshots for mod photo IDs.

Pregame activity keeps the page warm across camera gaps. Window size changes may still restart it at the new raster scale. Reduced-motion settings no longer leave stat columns or coin choices invisible.

## Validation and remaining limits

The v0.6 native build and the retail native build passed compilation and the development toolchain check. The latest package checks include scoring queues/cancellation and native close ownership. Static tests do not prove native trigger extraction or visual fidelity, and retail gameplay has not been re-tested in this pass.

The user supplied six original Ringside WOFF files in `E:/122`. They are copied without modification into the selected SNF package. Condensed Ultra now supplies the main scores, clock and quarter; Narrow Medium supplies record text. Team abbreviations use actual Wide Black Italic outlines with their slant removed, measured reference placement and a small outline stroke. Attached first names, surnames and captions are selected independently from source comparisons. The exact upright Wide/ExtraWide broadcast variant was not found, so the current package is not a verified 1:1 font match.

Actual hardware recordings are saved under the chat workspace's `outputs/snf-game-20261011/native-card-motion-000.mp4` and `-001.mp4`. They show Cam Skattebo and Jessie Bates, with the game's own prose, current mod portraits and moving warmup cameras. The Jessie Bates exit exposed a duplicate ESPN card when the HTML timer released its claim before native close; owned replacements now use the native close event instead of that entry-time timeout estimate. New hardware verification of this correction is pending.

The matching script trace and presented captures prove scene 179 (`eb004_%s.iff`) is crowd turnstiles, followed by scene 359 (`es448_%s.iff`) for the away-player bench warmup and scene 17 (`a331def_%s.iff`) for the home-player midfield stretch. The native mapper validates the first playing slot, ready state, instance descriptor and filename, binds prior crowd evidence to the current clock and both rosters, and rejects loaded-only and unknown scenes. The automatic transparent intro waits for native player cards to close and remains forbidden at coin toss/kickoff. Compilation and a hardware check of the new mapper are still pending.

The scoring animation now follows the designer's measured 9.13-second motion timeline after the demo lead-in: shaded logo, blue light, chrome, contraction into the clock pod, full-height scoring-drive carrier, intermediate score roll and exit. It uses actual team-logo contours, segmented team panels and separate letter phases. The original NBC extruded 3D artwork and a verified exact replay-wipe source are unavailable; those remaining differences are not certified as broadcast-identical.

No new installer, source upload or release publication has been produced from this integration work.

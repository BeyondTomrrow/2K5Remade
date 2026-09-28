# NFL on FOX reference ledger

The package treats supplied media as specifications. Measurements are in source pixels before normalization.

## Available FOX still

Source: `1-62810.jpg` (1280x720)

- Visible scorebug bounds: x=349..920, y=593..687 (572x95)
- Normalized center: x=0.5000, y=0.8896
- Normalized width: 0.446875
- Away logo region: about 349..496
- Away score region: about 497..590
- Center clock/quarter plate: about 591..687 (96 px)
- Home score region: about 688..783
- Home logo region: about 784..920
- The supplied still has floating logos and scores. It does not have large team or score background panels.
- Timeout indicators are three short light bars under each score.

The implementation preserves these measurements at 16:9. On ultrawide output the presenter anchors and scales against the game's 16:9 picture rectangle, leaving the design at broadcast-safe size instead of stretching it across the monitor.

## Runtime comparison

`logs/fox-layout-7-0.png` is the first post-change D3D11 capture at 3440x1440. It verifies the measured scorebug geometry, live team assets, live scores, live clock/quarter, timeout indicators, safe-area scaling, and layering behind the title's coin-toss overlay. `NFL2K5_PRES_TEST=1` intentionally kept the bug visible for that comparison; normal runs retain contextual visibility.

## Local typography and music

The supplied `Human PE Narrow` family is the active scorebug face. Its bold
cut is used for scores, clocks, down/distance, and timeout labels; its regular
cut is available for supporting text. The supplied All-Pro Sans Bold and Heavy
cuts are packaged for NFL/FOX title and player-stat layers. They are converted
to TTF under `fonts/` and loaded privately by the game, so the package does
not depend on fonts being installed in Windows.

`music/NFL on FOX Theme Song.mp3` is enabled as the package's pregame and
outro theme. The initial mix is deliberately moderate (0.35) and ducks beneath
commentary.

## Motion references currently absent

No FOX fade-in or FOX quarter-end video exists in the supplied attachment folders or the project tree as of 2026-09-27. The only attached broadcast motion file is the CBS Sports HQ clip, which is not used as a FOX reference. The engine now supports editable keyframes, easing, transforms, crop/wipes, polygons, opacity, anchors, and full-screen timelines so the FOX motion can be transcribed when its actual source clips are present. No invented motion is labelled frame-accurate.

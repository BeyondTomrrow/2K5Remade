# M3-office results: the Eras office hub (3D Franchise-office copy, era looks)

2026-10-16. Built from `ERAS-OFFICE-DESIGN.md` section 3, ratified by the user ("It needs a menu
like franchise with its office ... COPY it ... custom offices based on the 70s-2025"). Every
in-game test ran on `NFL2K5.exe --retail` in background mode (`RECOMP_BACKGROUND=1`,
`RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, press file, renderer frames) with
`tools\eras-capture-test.ps1` / `tools\eras\stadiums\coliseum_eras_test.ps1` and sandbox user
folders under `logs\eras\` (`o1-home` .. `o8-home`, `colo1-home`). The real user folder was not
used. Nothing was committed.

Contact sheet: **`logs/eras/office-sheet.png`** (era looks, camera stops, the IBM PC close-up,
every row's screen, the return after a game and after an abandon, Franchise isolation, the
Coliseum regression).

**Status: the office works end to end in all six era looks; T3.0-T3.12 pass (T3.3's lobby part
is not applicable: decision 1); the M4 regression passes after one fix (the office packages are
freed for the match).**

## Decisions taken by default (design section 6; the user was away)

1. Straight into the office, no lobby. "My NFL Eras" opens the plain hub (Continue / New
   League, as in M3), and Continue / New League open the office. The office replaces the League
   screen.
2. The 1980 slice uses the 1980s office (Auto = the season's decade).
3. Props an era did not have are hidden (1970s: monitor, wallpaper, monitor shadow, mouse pad,
   cordless phone; 2020s: the desk phone).
4. Made textures by default (colour grades of the game's own pixels at run time plus images drawn
   by `tools/eras/make-office-art.py`; no retail pixels are shipped).
5. The user's photos override from `mods\eras\offices\<season>\`, `<decade>\`, `default\`
   (none supplied yet; the mechanism is tested with a test-only file, T3.11).

## What was built

* **`tools/eras/build-office-pack.py`** (run on the player's machine): reads `fr.iff` and
  `fr31.iff` through the archive's own index and writes `mods/eras/office/files/erasoffice.iff`
  (byte copies of SCNE `coach_desk`, `front_office` and LAYT `coach_desk`, `front_office`) and
  `erasskin.iff` (`fr31.iff`). `--check` compares with the disc. `mods/eras/office/files/` is
  in `.gitignore`. It then runs:
* **`tools/eras/build-office-ibmpc.py`**: the 1980s computer (user request during the
  milestone). A fifth record, SCNE `coach_d80s`: a copy of `coach_desk` whose
  `monitor_group1` (flat panel, 376 vertices) is reshaped into a deep putty CRT on a wide system
  unit, `teamwallpaper` (the screen) moved into the CRT's opening, and `mousepad` reshaped into
  the keyboard (its submeshes take the monitor's material). Same vertex counts and triangles
  (RETEX option E), new positions, texture coordinates and per-face vertex shading, re-encoded
  with the Mod Tools' VC-LZ encoder (347,696 of 347,712 bytes). Its texture
  `mods/eras/offices/1980s/computer_monitor.png` is made from the user's photo
  `reference/ibm_pc_5150.jpg` (perspective-corrected crops of the bezel with badge and knobs,
  the drive bay with badge and grille, the keyboard) plus made putty surfaces. Needs
  `.tmp-2k-football-mod-tools`; without it the 1980s look falls back to the retail-shaped desk.
* **`src/nfl2k5_eras_office.c/.h`** (new): the room runtime, all Eras' own code and state.
  Load / unload of ctx `ERASOFFICE` / `ERASSKIN` (`sub_00043F50`, `sub_000432D0`,
  `sub_000432F0`), scene lookup by ctx (`sub_000449E0`), material / node / camera walks,
  snapshot and restore of node flags and material `+8` / `+0x30`, the era table plus
  `era.ini` parser ([room] [hide] [swap] [stops]), `move()` exactly as `sub_001427A0`, the
  msg-6 timer with the input lock (`sub_0014FC40` / `sub_0014FC30`), the msg-7 draw
  (`sub_0002AD80`, `sub_0013E710`, `sub_0002AC80`, `sub_0002DE60`), ambience, sounds, LT/RT
  browsing, the texture context, the reverse dolly out of the front office, and
  `NFL2K5_ERAS_OFFICE_HOLD=<s>` (test switch: stay at a camera stop before the push, for 1 fps
  captures). No Franchise function is called and no Franchise global is read or written.
* **`src/nfl2k5_eras_menu.c`**: screen 1 is now the office: a link list copied from the
  Features template with flags `0x10`, layout `L"front_office"` (from our package), event table
  `{1,2,3,4,5,6,7,10}`, seven type-9 rows: Play (`<AWAY> at <HOME>`), Schedule, AFC
  Standings, NFC Standings, Your Team, Office Look, Unverified rosters. Each row moves the
  camera, and on arrival opens its screen or launches. New child screen "Office" (Office Look:
  Auto / 1970s .. 2020s, saved as `league.ini` `office_look`). If the package is missing or does
  not load, the screen falls back to the plain M3 list (layout `navigation`).
* **`src/nfl2k5_eras_launch.c`**: unchanged steps. `nfl2k5_eras_menu_header(1)` now returns
  the office, so ERAS_RETURN, the FSM-pop clean-up and the msg-3 clean-up target it (log text
  updated). No gen-patch change.
* **`src/nfl2k5_eras.c/.h`**: `office_look` in `league.ini` (no format bump).
* **`src/nfl2k5_texture_packs.cpp`**: the era context (`nfl2k5_texpack_era`, `_gen`, `_era_on`):
  era folders by raw hash or friendly name (`mods/eras/offices/office-textures.ini`), and the
  `[grade]` lines of `era.ini` (sat / contrast / bright / mul / add / tint on the game's own
  pixels). Only while the office shows. `NFL2K5_ERAS_TEXLOG=1` logs each lookup.
* **`src/nfl2k5_controller_art.cpp`**: `wants()` returns 3 for paletted textures while the
  era context is set; `gen()` mixes in the texture-pack generation.
* **`src/nfl2k5_team_files.c`**: `mods\eras\office\files\*.iff` go last into pack G. Two
  changes for concurrent copies of the game (they share `original\disc`): if pack G cannot be
  written because another copy holds it, it is used when it already holds the same files at the
  same places; and `NFL2K5_PRIVATE_PACKS=1` (tests) gives this copy its own archive folder
  `vc_53450030.<exe name>\` (hard links to banks 0..F, its own G).
* **`external/xboxrecomp/src/kernel/nv2a_gpu_d3d11.inc.c`** (the vendored renderer, its own
  repository, uncommitted): `tex_override_wants()` returned `a && b && wants()`, i.e. 0/1, so
  the hook's bit 1 ("follows gen()") never reached `gpu_tex_hash` and no gen change ever
  re-uploaded a texture. It now returns the hook's value. Without this the era looks stuck on
  whichever look was uploaded first (o2 frames).
* **`mods/eras/offices/`**: `README.txt`, `office-textures.ini` (names and hashes only; plus
  the three erasskin hashes), `default\era.ini`, and per era `era.ini` and the made images
  (1980s terminal `skin_team_logo.png` and `computer_monitor.png`; 1990s `computer_screen.png`,
  `wall_tv_picture.png`; 2010s `skin_team_logo.png`; 2020s `computer_screen.png`,
  `wall_tv_picture.png`).
* **`tools/eras/make-office-art.py`**: the made screen images (shapes and Windows fonts only).
* `config/game.cmake`: `src/nfl2k5_eras_office.c`.

Build: `tools\build.ps1 -Game` exits 0 (last build 10:46). Gen patches: none added or changed.

## Era looks

| look | room | props | surfaces |
|---|---|---|---|
| 1970s | coach_desk | monitor, wallpaper, shadow, mouse pad, phone hidden; desk top takes the oak panel texture (wood desk) | walnut grade on the office atlas and wood, olive-gold carpet, brown vinyl couch, avocado cabinets, sepia wall photos; Standings and Office Look stop at the file cabinets |
| 1980s (Auto for 1980) | coach_d80s | IBM PC 5150 (CRT on system unit, keyboard); notebook sticker hidden | oak, berber carpet, burgundy couch, beige phone; green-phosphor terminal on the screen |
| 1990s | front_office | - | hunter-green walls, teal carpet, cherry wood, beige CRT bezel, made 90s desktop on the computer, made broadcast still on the wall TV |
| 2000s | coach_desk | - | as shipped |
| 2010s | coach_desk | notebook sticker hidden | charcoal walls, grey carpet, espresso wood, black monitor, made flat wallpaper |
| 2020s | front_office | desk phone hidden | white walls, white oak, light carpet, black monitor, made dashboard, made widescreen TV still |

## Tests

| test | result | evidence (`logs/eras/`) |
|---|---|---|
| T3.0 pack | **pass** | `build-office-pack.py --check`: both files match the disc record for record; the fifth record `coach_d80s` is reported as made. `git check-ignore`: `mods/eras/office/files/` ignored; no `.iff` in `git status` |
| T3.1 main menu row | **pass** (unchanged from M3) | `o2-main.png`, `o6-mainback.png`; the Franchise path through Game Modes still works (`o6-fr5..fr11`) |
| T3.2 pack G and ctx lookups | **pass** | `o1.stderr.log`: `[TEAMFILES] My NFL Eras office: 2 files`; `[ERAS] office load: ERASOFFICE coach_desk 81DD9880 (39 materials) front_office 81E65080 (42 materials), LAYT front_office 81F4F398; ERASSKIN logo ... wallphoto ...; 47 ms` |
| T3.3 room draws | **pass**; lobby **n/a** (decision 1) | `o1-office2.png` (first build): the room behind a readable title and list; the LAYT from our package draws the list. msg 7 on a `0x10` screen works (risk 1 retired), the LAYT resolves (risk 2 retired). 7 rows show (`nav_bar_c`, risk 11). The front-office entrance dolly plays on entry and on each switch into a front-office look |
| T3.4 camera | **pass** | 2000s look, `NFL2K5_ERAS_OFFICE_HOLD=2.5`: `o3-00-schedule-stop` (planner), `-afc-stop`, `-nfc-stop` (monitor), `-team-stop` (notebook), `-look-stop` (file cabinets), `o4-play-stop` (door); front office `o3-90-schedule-stop` (computer), `-team-stop` (contract). Each child opens on arrival (`menu push`), Back plays the return move (`office shown: ... time 6.00 -> 7.00`); input is locked while moving; `[FSMOP]` push/pop balance |
| T3.5 isolation | **pass** | Eras visited (o6), then a new Franchise (Cowboys): Coach's Desk retail (star logo, colour photos, silver monitor: no era grade) `o6-fr12.png`; Front Office retail (ESPN desktop) `o6-fr13.png`; no crash. Then Quit -> Main Menu -> My NFL Eras -> Continue: the Eras room shows the erasskin art (NFL shield, black-and-white photos), not the Cowboys' `o6-rev-office.png`. The packages decode to the same addresses each time |
| T3.6 flow | **pass** | Fresh profile o6: hub shows only New League (`o6-hub.png`), no `eras\` folder before; New League -> office (`o6-newoffice.png`); `league.ini`, `results.csv`, `eras.log`, `box\` created; AFC (`o6-afc`), NFC (`o6-nfc`), Schedule week 1 -> 2 (`o6-sched2`), Your Team -> Miami Dolphins (`o6-yt`, office row "Your Team: Dolphins" `o6-office-yt`), Office Look 1970s (`o6-look70`), back to the Main Menu (`o6-mainback`) |
| T3.7 persistence and input | **pass** | New process o7: `league loaded: ... user team dolphins ... next 1980w01-dolphins-bills`, `office create: look 1970s (setting 1970s)` (`o7-office.png`). Continue hidden in a fresh profile (o6). Button words: A `00000100`, LT **`00004000`**, RT **`00008000`** (`o3.stderr.log`): LT/RT browsing enabled |
| T3.8 Game Options untouched | **pass** | o6 with `NFL2K5_MEMDUMP=AA26B0:4`: after the whole Eras path the dirty flag is 0 (`o6-dirty-after-eras.bin`) |
| T3.9 props and cycling | **pass** | 1970s hides 5 of 5 nodes and swaps 1 of 1; 2020s hides 2 of 2; 1990s/2020s use front_office (`o3-look-*.png`). 40 RT switches in one process: working set 390 -> 391 (20) -> 393 MB (40); no crash |
| T3.10 hashes | **pass** | o2 with `NFL2K5_TEXDUMP=1`: the dump has `60BEEEB5`, `DD2E1927`, `E3D7F0CD`, `4C544C90` (and `67A3C2EF`, `9B096D0A`, `B3B6FB27`, `3B4C7AEE`). The front-office hashes were not in that dump (the front office was not shown in o2); they were confirmed by the era lookups in o3 (`[TEXPACK] era gen 3: 1F314622 ... 2B516A5F ... 9C802FB4 replaced`) |
| T3.11 user photos | **pass** (test file, removed) | A channel-swapped copy of the local `3B4C7AEE` dump as `1970s\3B4C7AEE.png` (test only): magenta photos in the 1970s look (`o6-office70.png`), black-and-white in the 1980s (`o6-office80.png`), and the Franchise desk afterwards shows its own photos. The file was deleted after the run |
| T3.12 embedded-texture bind | **pass** | `vcSwShader16 = vcHwShader27` gives a wooden desk top (`o3-look-1970s.png`) |
| M4 regression: full game | **pass (after the fix below)** | o5: office -> Play (door move) -> Coach Match Up -> Start -> CPU vs CPU, 10 s quarters -> `[ERAS] final game=1980w01-lions-rams home=6 away=0 ... period=25` -> `return: to the office 01890454` -> `committed ... results row written` -> both slots released -> `office: packages loaded again after the match (ok)` -> the office with "Play: Raiders at Chiefs" (`o5-quit2.png`); NFC standings Lions 0-1-0 0-6 (`o5-nfc.png`) |
| M4 regression: abandon | **pass** | o5: pause -> Quit -> Quit: `match exit: abandoned`, `return: to the office`, `abandoned ... the fixture stays unplayed`, settings restored, room reloaded (`o5-ab-office.png`); `results.csv` unchanged |
| M4 regression: back out of Coach Match Up | **pass** | o8 (with the IBM PC package): `ended with no game played`, clean-up, room reloaded (`o8-pc-backout.png`) |
| Coliseum regression (coordinator) | **pass** | colo1, own exe copy, fresh sandbox, Vikings' stadium set to LA MEMORIAL COLISEUM: `[TEAMS] Eras stadium pack LA_Coliseum: ... in row 811CE7F0 (LOCO ARENA)`, `launch ... stadium LA MEMORIAL COLISEUM (record 811CE7F0 id 96)`, the game played in the Coliseum with field, bowl and peristyle drawn (`colo1-pg1..3.png`, `colo1-play1.png`). `teams.csv` restored, SHA-256 `19FF397E...` |
| IBM PC 5150 (user request) | **built, seen in game** | `o8-office.png` (CRT on the system unit with two drives, badge and grille, keyboard in front), close-up at the monitor stop `o8-pc2-afc-stop.png` (screen opening with the green terminal, IBM badge, two knobs) |

No `[CRASH]` exception line in o1-o8 or colo1.

## Defects found and fixed during the milestone

1. **Pre-game stall with the office kept loaded under a match.** Design 1.7 inferred that the
   office could stay loaded under a match, as Franchise's desk does. In o4 the match stalled in
   the stadium fly-over (`[GGL] STALL ... owner sub_004262F0`, guest `0x0042D6AE`). The stadium
   agent's colv7 run on the same build stalled at the same place; that is the "black load
   screen" the coordinator reported. Fix: `nfl2k5_eras_office_free_for_match()` unloads both
   packages before `nfl2k5_eras_launch`; the office's msg 3 loads them again before the screen
   shows (`back_from_match`). A refused launch reloads at once. Proved by o5 (two launches, a
   final, an abandon), o8 and colo1. Loading from inside `nfl2k5_eras_return_desc` was tried and
   removed: it runs in generated code without the registers published.
2. **Shared pack G between concurrent copies of the game.** Every copy rewrites
   `original\disc\vc_53450030\G` at boot with the files present at that moment. While the stadium
   agent changed its bundles and both agents ran copies, each boot moved the other copy's files
   under it (o2: `cannot write the extra pack`; o3: my office lookups failed after the other copy
   rewrote G; pack sizes 122,942 / 122,992 / 123,070 / 123,428 KB across runs). This is the
   likely cause of the stadium agent's "no field or stadium drawn" runs. Fixes: reuse of an
   identical pack in use, and `NFL2K5_PRIVATE_PACKS=1` for test runs (o3 onward and colo1 used
   it). **Other agents' test scripts should set `NFL2K5_PRIVATE_PACKS=1` too.**
3. **Texture overrides never followed gen()** (renderer, above).
4. Title / row text clipped (`o1-office2.png`): rows are now short ("Play: Lions at Rams",
   abbreviations when long; "Your Team: Dolphins"), the title is "1980 Season - Week n".
5. The 1980s monitor-wallpaper texture is also the notebook sticker: the sticker is hidden in the
   1980s and 2010s looks.

## Known defects / open items

* **End Of Game -> A is Rematch.** Pressing A on End Of Game started a rematch (o5); quitting
  it committed the first result correctly and returned to the office. Known M4 limit, unchanged.
* **The IBM PC is a reshape, not a new model**: the keyboard is the mouse pad's mesh (no
  separate key caps, the keys are the photo), the CRT's back and the unit's sides are plain made
  putty, and the 1980s look has no mouse pad. The coiled cable is not modelled.
* **Grades are global per texture**: the office atlas grade also colours the books and the day
  planner; the 1990s/2020s wall-TV pictures were not seen up close (no camera stop frames the
  wall TV).
* **Not done (design polish, optional):** camera glances, the `sub_0006BC30` era strip, the
  LAYT `coach_desk` info panels, a help bar with an LT/RT hint (the Office Look child says
  "Browse with LT / RT in the office"), an Ambience setting, League History at the file
  cabinets, the wall-TV slideshow folder, pasting single photos into atlas cells
  (`atlas-cells.json`), and a guides export tool.
* **Not tested:** the front-office "Play" reverse dolly ending in a launch (only seen as a
  camera move in the code path; no 1990s/2020s launch was run); a full game with the 1980s
  IBM PC package (only the back-out path); halftime with the new launch order.
* Debug env switches left in (all off by default): `NFL2K5_ERAS_TEXLOG`,
  `NFL2K5_ERAS_OFFICE_HOLD`, `NFL2K5_PRIVATE_PACKS`.
* Private archive folders were left on disk: `original\disc\vc_53450030.NFL2K5\` and
  `vc_53450030.NFL2K5_coliseum\` (hard links to the banks plus a private G of about 120 MB
  each). They can be deleted when no copy of the game runs.

## Next steps

1. Review and commit (the external renderer fix needs its own commit in `external/xboxrecomp`).
2. Add `NFL2K5_PRIVATE_PACKS=1` to `tools\eras\stadiums\coliseum_eras_test.ps1` and
   `tools\bg-drive.ps1` so parallel test runs never share pack G.
3. Play a full game in a front-office look (reverse dolly) and in the 1980s look.
4. The user's photos per era (folders and names in `mods\eras\offices\README.txt`).
5. Real period props (rotary phone, typewriter, console TV) need mesh injection (RETEX F);
   the IBM PC shows that same-topology reshapes (RETEX E) of our own scene copy work and render.

## 2026-10-16 (later): the 1980s office, take 2 (user feedback)

User, verbatim: "Make the 80's look like an 80's office and the ibm is overlapping on the books".
Data-only change (no `src/` edit, no `tools\build.ps1`; another agent was building): the
`coach_d80s` record, the 1980s folder's images and `era.ini`. Tested with a copy of the current
exe. Nothing committed.

Contact sheet: **`logs/eras/office80-sheet.png`** (before / after: idle, the IBM close-up, every
camera stop, the return move, the 1970s and 2000s looks, Franchise; plus an offline preview of the
desk without the menu panel).

### What changed

* **`tools/eras/build-office-ibmpc.py`** (rewritten; still a same-topology edit of our own copy,
  RETEX option E, and still the fifth record `coach_d80s` of `erasoffice.iff`):
  * **IBM PC placement.** The first version scaled the stand's base plate to a 392 x 395 unit
    centred under the old monitor: its left-front corner sat on the day planner, its back-left
    corner hung off the desk, and the keyboard (the mouse pad mesh) lay over the legal pad and the
    day planner. Now the computer is built in its own frame (unit 350 x 98 x 285, CRT face
    268 x 200, 255 deep, set 18 back on the unit as on the 5150), then placed at desk (52, -14)
    and turned 14 degrees toward the chair. The CRT face stays where the Standings camera stop
    frames it (the close-up now shows the screen right of the menu panel instead of under it).
    A **layout check** runs on every build (`--layout` prints it alone): footprint hulls of the
    unit, keyboard, cable, phone and ashtray against the day planner, legal pad, notebook, lamp
    foot and mug, and every corner over a desk-top triangle. Result: all clear (smallest gaps:
    phone / lamp 14, phone / mug 16, ashtray / unit 23, keyboard / legal pad 56, keyboard / day
    planner 91, unit / day planner 134); every part on the desk.
  * **Keyboard**: a separate thick beige Model F (280 x 102, 11 high at the front, 30 at the
    back) in front of the unit, its top from the user's photo. **Coiled cable**: the stand's neck
    (24 vertices, found as the monitor mesh's smallest connected part, no longer by a position
    test) is a coil-textured tube lying on the desk from the keyboard's back-left corner to under
    the unit.
  * **Multi-line desk phone** (was the cordless phone): `phone_group1`'s base is un-turned (its
    footprint's minimum-area rectangle) and wrapped onto a 118 x 132 wedge (22 front, 52 back);
    the keypad plate (a single 4 x 3 sheet) becomes the whole sloped face plate (keypad, speaker
    grille, red hold button, six line buttons with two lit, extension card); the cordless handset
    lies in a cradle across the back (its principal axes, a proper turn so the faces stay
    outward); the antenna is a beige coiled cord; the cordless buttons fold into the body. At
    desk (-464, 300), turned 10 degrees.
  * **Wall**: `wall_pic01`'s picture (submesh 1, its own 4 vertices) is a framed **1980 schedule
    poster** (16 Sundays, Sept 7 to Dec 21, period type); `wall_pennant` is a burgundy felt
    **CHAMPIONS pennant** (drawn at its 1:3 shape).
  * **Ashtray**: `notebook_logo` (the hidden sticker quad) is an amber glass ashtray with a
    cigarette on the right of the desk (a flat decal: one quad).
  * The PC's soft shadow quad follows the unit; `room_walls` vertex colours x1.9 (the paneling is
    dark wood and the walls are lit at 0.2); `new_lamp_group` vertex colours x(1.02, 0.80, 0.44):
    a **brass desk lamp** (its arm is untextured, so only vertex colours can change it).
  * All reshaped props use the monitor's material and one 1024 x 1024 atlas,
    `mods/eras/offices/1980s/computer_monitor.png`: the photo crops plus made putty, phone,
    cords, poster, pennant and ashtray (shapes and Windows fonts).
  * Fixed in `face_uvs`: vertices are mapped per (atlas region, axes) over all the faces that use
    that region (before: per face class, so a sloped sheet split into two half-maps).
* **`tools/eras/make-office-art.py`**: new 1980s room surfaces (tileable, periodic noise):
  `office_atlas.png` (the Coach's Desk atlas redrawn cell for cell at the places the scene's UVs
  use: dark walnut wall paneling with V grooves, walnut shelf and door trim, cream book pages,
  book cloth cells, an oxblood "Day Planner" with a gold 1980 seal, a harvest-gold PLAYBOOK
  binder cover and label band, a brass door knob, an almond light switch, the mug's coffee and
  rim), `carpet.png` (rust and brown shag), `desk_metal.png` (honey-oak veneer desk),
  `wood_panel.png` (walnut raised-panel door, end table, floor lamp), `coffee_table_wood.png`
  (walnut shelf and coffee table). No retail pixels: every cell is drawn; only the cell positions
  were measured from the scene's texture coordinates.
* **`mods/eras/offices/1980s/era.ini`**: `[hide] nodes =` (empty: the sticker quad is the
  ashtray now); grades: sepia photos, chestnut leather couch, tan file cabinets, dark green
  blotter, brass desk-lamp textures and floor-lamp base, linen floor-lamp shade. The grades that
  the new images replace were removed (atlas, wood, carpet, desk, phone).
* **`tools/eras/office80_test.ps1`** (new): the background test driver for this work (own exe
  copy `NFL2K5_office80.exe`, `NFL2K5_PRIVATE_PACKS=1`, own press file and frames, sandbox
  `-AppData`, stops only its own PID, deletes the copy on stop).
* `mods/eras/offices/README.txt`: the 1980s images and `coach_d80s` in the limits paragraph.

`build-office-pack.py --check`: PASS (the four disc records match; `coach_d80s` reported as made).
`git check-ignore`: `mods/eras/office/files/` and `logs/` ignored.

### Tests

o80a and o80b: `NFL2K5_office80.exe --retail`, background, muted, `NFL2K5_NO_HOST_PAD=always`,
press file, `NFL2K5_PRIVATE_PACKS=1`, `NFL2K5_ERAS_OFFICE_HOLD=4`, sandbox
`logs\eras\office80-home` (copied from o8-home: 1980 league, Auto look = 1980s).

| check | result | evidence (`logs/eras/`) |
|---|---|---|
| 1980s idle | **pass**: paneled walls, chestnut couch, poster and pennant, oak desk, the IBM PC clear of the day planner, binder and legal pad, keyboard in front, cable into the unit | `o80b-idle.png` |
| camera stops | **pass**: Schedule (day planner, green blotter), AFC and NFC (the CRT close-up), Your Team (PLAYBOOK binder), Office Look (tan cabinets), Play (walnut door; then Coach Match Up, B: back to the office, room reloaded) | `o80b-sched-stop`, `-afc-stop`, `-nfc-stop`, `-team-stop`, `-look-stop`, `o80a-play-stop`, `o80a-play-next`, `o80a-backout` |
| children and return moves | **pass** | `o80b-sched-screen`, `-afc-back`, `-look-back`; camera-move frames `o80b-frames\f-091..106` |
| other looks | **pass, unchanged**: 1970s (`5 of 5 nodes hidden, 1 of 1 swaps`), 2000s retail; back to 1980s | `o80b-look70`, `-look00`, `-look80again` |
| T3.5 Franchise isolation | **pass**: after the Eras office, Game Modes -> Franchise -> Cowboys -> Coach's Desk is retail (silver flat monitor, star, colour photos, glass desk, cordless phone, silver lamp). Not saved; nothing under `original\disc\UDATA` / `TDATA` or the sandbox changed | `o80b-fr4.png` |
| crashes | none (`[CRASH]` count 0 in `o80b.stderr.log`) | |

The exe copy was deleted, and the private archive folder `original\disc\VC_53450030.NFL2K5_office80`
(hard links plus a 121 MB pack G) was removed after the run. No process named NFL2K5.exe was touched.

### Still off / limits

* **Compression headroom is 9 bytes.** `coach_d80s` must re-encode into the retail record's
  fixed span (`rebuild_fixed_span_filled`): 347,703 of 347,712 bytes. A further reshape of this
  record (more varied UVs or colours) may not fit; the next step would be letting the record grow
  in our own pack (the Mod Tools' fixed-span encoder is the limit, not the game), untested.
* The phone, the brass lamp, the mug and most of the binder and day planner sit under the menu
  panel in the idle view; they show in the camera moves and stops (and in the offline preview row
  of the sheet), not in the resting frame.
* **No wall TV in the Coach's Desk**, so a wood-cabinet CRT TV needs mesh injection (RETEX F).
  No chair either: the couch carries the period leather.
* The wall photos are the erasskin black-and-white action shots graded to sepia; faded colour
  photos need the user's own images (`1980s\skin_wall_photos.png`, or per hash).
* The desk lamp keeps its swing-arm shape (brass colour only); the ashtray is a flat decal; the
  pennant is generic ("CHAMPIONS"); the file cabinets are a grade of the green retail ones (tan
  grey, not a new design); the shelf trophies and the crystal trophy are unchanged.
* If `coach_d80s` is missing (no Mod Tools), the 1980s look falls back to `coach_desk` and the
  notebook sticker shows (with the terminal texture), since `[hide]` is now empty.
* The keyboard's top is the photo crop: its left end reads as a flat bevel, and there are no
  separate key caps.

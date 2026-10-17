# My NFL Eras: the office hub (design)

2026-10-15. Design only. **Nothing here has been built or seen in game.** It replaces the
plain-menu hub of `PHASE2-PLAN.md` M3 with a 3D office copied from Franchise mode, and gives
each era from the 1970s to 2025 its own look.

The user's request: a hub "like franchise with its office", built as a **copy**, not by
running Franchise directly, with custom offices for the 1970s through 2025, because "nobody
wants to just sit on a menu, they need eye candy". The standing rules also apply: build it
into the game's own menus, and the art must look polished and native. (Correction 2026-10-15:
there is no "user photos only" rule. The user once rejected crude hand-drawn controller art and
supplied photos for that one screen. Era art may be made by us -- recoloured or retextured retail
textures, period surfaces, made textures -- and the user's own photos are used wherever they supply
them.)

Sources (all read-only research, each re-checked by a second reader):

* `research/FRANCHISE-OFFICE-MENUS.md` (MENUS): the screen graph, the Coach's Desk and
  Front Office decoded, the clone recipe.
* `research/FRANCHISE-OFFICE-SCENE.md` (SCENE): the scene, camera curves, objects and
  materials, the copied package.
* `research/OFFICE-RETEXTURE.md` (RETEX): texture options A to G and the folder layout for
  photos.
* `PHASE2-PLAN.md` M3 and M4.

Labels, as in PHASE2-PLAN: **VERIFIED** (two independent static readings), **STRONG**,
**INFERRED**, **UNVERIFIED**, **RUNTIME** (needs an in-game test; none has been run).

### Corrections from the verification passes (these matter for the copy)

| report said | the check found | what the design does |
|---|---|---|
| The Front Office is "self-contained" | Its create callback `sub_0034DAA0` calls the desk's `sub_00142AF0`, which reads the coached-team pointer `[0xE3C0A0]+0x10C`. It also expects `office_photos` from the desk's team pack. | Every callback is Eras' own C. No Franchise function is called, including the material and camera walkers `sub_001426E0` / `sub_00142730`, which the copy re-implements in C. |
| `sub_0034DAA0` binds the same way as `sub_00142BB0` | It writes `+0x30` only and never clears the hidden bit `+8`. | The copy always writes both `+0x30` and `+8 &= ~1`. |
| Camera row `(a, b)` plays `a -> b` | `sub_001427A0` seeks to `a-1` and plays to `a+0.4`. `b` is only the return point: on return it plays `b-1 -> b`. | `move()` copies this exactly (section 1.4). |
| "1.4 s" camera move | 1.4 scene units at 1.5x speed, about 0.93 s if `dt` is in seconds | Speed is a constant in the copy and can be tuned. |
| The phone shares one untextured part with the floor lamp | That is true in `coach_desk` only. In `front_office`, `lambert1_DOUBLESIDED` is also on the desk top, keyboard, mouse, pen **and the room walls**. | The copy never hides by material in `front_office`. It hides by **node** (flag `0x10`). |
| Crib pack table has 27 entries | It has 22 | Not used either way; the Crib is not copied. |
| LT/RT = button bits `0x4000` / `0x8000` | The table is verified. That LT and RT sit on those bits is **inferred** (XDK order). | Era browsing on LT/RT is gated by an in-game test. The Settings row does the same job without it. |
| `ncaa_render.inc.c` cited as the mesh-injection precedent | The file does not exist in the tree or in git history | Mesh injection (RETEX option F) is out of M3 and stays INFERRED. |

---

## 1. How the Eras hub copies the Franchise office

### 1.1 What makes the Franchise desk an office (VERIFIED)

The Coach's Desk (`0x522190`) is an ordinary link-list screen (handler `0xF3E90`). Four
things turn it into an office:

1. **A 3D room behind the list.** SCNE `coach_desk` from `fr.iff` (archive entry 23) is drawn
   through its own scene camera `coach_cameraShape`.
2. **Camera moves on selection.** A row plays a sound, starts a camera segment and locks menu
   input (`[0xBD8050] = 0`). When the camera arrives, the screen pushes the stored destination.
   Coming back plays the return segment.
3. **Dressing.** A small per-team pack (`fr<code>.iff`: team logo, pdalogo, wallphoto,
   office_photos) is bound into material slots (`material+0x30`).
4. **Ambience.** The `coachambience` loop and named UI sounds.

The **Front Office** (`0x52533C`) repeats the same machinery with its own globals, drawing its
room in its own msg-7 event action, with flags `0x10` (Back pops, background mode 0). That
draw pattern is the one to copy. It needs no shared callback slot. The desk instead uses the
single global `[0xA84498]`, background nibble 7.

### 1.2 What "copy, not use" means here

| Franchise piece | Eras |
|---|---|
| `fr.iff` loaded under ctx `FRANCHISE1` | **`erasoffice.iff`**: a byte copy of the `coach_desk` and `front_office` SCNE chunks and their LAYT chunks, made from the player's own disc by an offline tool, served from the extra archive pack G, loaded under ctx `ERASOFFICE`, and always looked up with that ctx |
| `fr<team>.iff` per-team pack, ctx `FRANCHISE2` | **`erasskin.iff`**: a renamed byte copy of `fr31.iff` (NFL shield logo plus black-and-white action photo atlas, checked visually in `build/eras/office/historic_wallphotos.png`), ctx `ERASSKIN`. The user's photos replace its pixels (section 2.4). |
| Desk and Front Office event tables, row actions, globals | New descriptors and event tables in the Eras guest heap. The functions are C, served at `0xFEE0xxxx`, with state in a C struct. |
| Franchise child screens (Schedule, Standings, Gameplan, ...) | Not pushed. They all read or write Franchise data (MENUS section 6). Eras has its own child screens. |
| Quit `sub_000C8190` (roster teardown, season reset) | Back or Leave pops; no Franchise teardown |

Franchise is untouched: its files, code and globals are never written. The one shared fact
is that both packages contain scenes named `coach_desk` / `front_office`. They are never loaded
at the same time: Eras unloads at msg 2, and Franchise unloads when its desk is destroyed. A
test proves this (T3.5).

Engine primitives the copy calls (all verified in MENUS section 7.2; none reads Franchise
state):

* `sub_00043F50` / `sub_000432D0` / `sub_0014E4B0` / `sub_000432F0`: package load, wait,
  unload.
* `sub_000449E0`: resource lookup.
* `sub_0002F010` / `sub_0002F030`: scene time.
* `sub_00066B20`, `sub_0002AD80`, `sub_0013E710`, `sub_0002AC80`: camera. `sub_0013E710`
  sits in the Franchise address range, but its body only calls `sub_0002B860`,
  `sub_0002BA10`, `sub_000386F0` and `sub_00038760`. That was checked for this design in
  `build/gen-locals/recomp_0009.c`; it is stateless.
* `sub_0002DE60`: scene draw.
* `sub_0014FC40` / `sub_0014FC30`: input lock.
* `sub_00038650` + `sub_00089DA0`, `sub_00165BE0` / `sub_00165C10`: sounds and ambience.
* `sub_0014E540` / `sub_0014E520`: dialogs.
* `sub_00070A10`: pad buttons.
* FSM push, pop and replace.

### 1.3 Screens

```
Main Menu (0x515660)  "My NFL Eras" (inserted row; PHASE2 T3.1)
  -> ERAS_LOBBY   room screen: front_office, FOcamShape, entrance dolly 0 -> 1
       rows: Continue League (hidden without a save) | New League (1980) | Back (flag 0x10)
       Continue / New -> camera (3,5) zoom to the computer -> push ERAS_OFFICE
  -> ERAS_OFFICE  room screen: the era's room (coach_desk or front_office)
       rows (section 1.5) -> camera move -> push child
       LT/RT (after test T3.7) or the Office Look row: browse other eras' offices
  -> children (plain menus with the shared animated backdrop, flags 0x11/0x15):
       ERAS_SCHEDULE, ERAS_STANDINGS_AFC / _NFC, ERAS_YOURTEAM, ERAS_SETTINGS, (ERAS_HISTORY later)
```

The lobby and the office are two instances of one **room screen** implementation
(`src/nfl2k5_eras_office.c`). They differ only in their room, camera, rows and stops. They
share one load of `erasoffice.iff` / `erasskin.iff`, counted by a reference count. Both
scenes are in the same package. Only the top screen draws (msg 5 clears the covered screen's
scene pointer, as the desk does). Because the scene time lives in the scene (`[scene+4]`),
each screen sets its own time at msg 4.

The lobby is **eye candy, not a requirement**. If the user does not want it, Main Menu ->
ERAS_OFFICE works directly, with New and Continue shown as office rows when no league is
loaded. Section 6 lists this as a decision.

### 1.4 Room screen: descriptor, events, behaviour

**Descriptor** (0x2C bytes; Eras guest heap; field values from the Front Office):

| off | value |
|---|---|
| +0x00 | title buffer, UTF-16, 64 chars: "My NFL Eras", or "1980 Season  -  Week 3" on the office. Rewritten before msg 3. |
| +0x04 | own event table |
| +0x08 | `0x000F3E90` (link-list handler) |
| +0x0C | 0 in M3a. Later: a help bar with "Select", "Back" and, after T3.7, an LT/RT "Office" hint (help ids untested). |
| +0x10 | own type-9 rows + end row |
| +0x14 | 0 |
| +0x18 | `L"front_office"`: LAYT copied into `erasoffice.iff` (title_bar_wide + nav_bar_c). It must be loaded **before msg 3**, so the load happens at msg 1. |
| +0x1C | 0 (M3). Later, for LAYT `coach_desk`: Eras' own binding table for the info panels (MENUS 8.2). |
| +0x20, +0x24 | `0x02400044`, `0x018D0052` |
| +0x28 | **`0x10`**: Back pops, background nibble 0. Never `0x04`/`0x08`: those are background-mode bits, not scroll bits. |

**Event table** `{1, 2, 3, 4, 5, 6, 7, 10}`, type-1 records calling `0xFEE0S000 + slot*4`:

```
msg 1 create   refcount++ ; first: queue ERASOFFICE <- erasoffice.iff, ERASSKIN <- erasskin.iff,
               sub_0014E4B0(FSM) ("Loading, please wait"); camera object (0x2C0 bytes, 16-aligned, guest heap): sub_00066B20
               target = 0; pending = 0; ret_time = 1.0 for the lobby (entrance dolly) or 0 for the office
msg 3 refresh  rewrite the title and row label buffers (Play Next Game: <AWAY> at <HOME>, Continue: ...)
msg 4 shown    scene = sub_000449E0(L"ERASOFFICE", 'SCNE', room)    ; context lookup, never ctx 0
               apply the era look (2.3): node hides, material binds/swaps, texture context (2.4)
               sub_00165BE0(ambience, 20.0, 40.0); sub_0014FCD0(FSM, 0); move(ret_time, 0)
msg 5 covered  scene = 0
msg 6 frame    if scene && t < target: sub_0014FC40(); advance(dt * speed)   ; 1.5, or 1.0 for the 0 -> 1 dolly
               else sub_0014FC30(); if pending: set t = target; push pending (sub_0006E390)
               else if office && LT/RT edge && !busy: next/prev era look (2.5)
msg 7 draw     if scene: save camera; rec = camera record by name (walk [scene+0x3C] x 0x60 at [scene+0x40]);
               sub_0013E710(camobj, rec); sub_0002AC80(camobj); sub_0002DE60(scene); restore camera
msg 10 back    sound "Ambient: Statistics Out"
msg 2 destroy  sub_00165C10(); clear texture context; refcount--; at 0: unload ERASSKIN, ERASOFFICE; scene = 0

move(a, b): ret_time = b; target = a; if a == 0 { t = 0; return }
            t = a - 1.0; if b > 0: target = a + 0.4                           ; = sub_001427A0
row action: sound; move(a, b); pending = child descriptor; g_esp += 4
```

**Reverse moves (INFERRED).** The retail code only advances time. Because the copy owns msg 6,
it can also set the time backwards frame by frame with `sub_0002F010`. One use: "leave the
office" in `front_office` plays the 1 -> 0 dolly in reverse, out to the entrance. The curve
evaluator is presumably time-symmetric, but this is untested.

**Glances (INFERRED, optional, M3d).** The retail desk moves only when A is pressed. The copy
could play the first part of a row's segment while the cursor rests on that row (selected
row = FSM stack entry `+4`), without locking input. It goes behind an Eras setting until it
looks right.

### 1.5 Office rows and camera stops

Stops from SCENE 3.4. The keyed poses are taken from the producer's curve evaluator; the
second reader confirmed the time pairs in code but not the curve contents.

`coach_desk` (5 stops; idle eye (260, 250, 801)):

| row | stop (a, b) | what the camera shows | sound | opens (M3) |
|---|---|---|---|---|
| Play Next Game: `<AWAY> at <HOME>`, Week n | (13, 15) door | the office door | "Ambient: Front Office In" | M3: OK dialog "Coming in the next update" with **no** camera move. M4: the launch. |
| Schedule | (5, 7) day planner | the planner, top-down | "Ambient: Calendar In" | ERAS_SCHEDULE |
| Standings | (1, 3) monitor | the computer | "Statistics In" | ERAS_STANDINGS_AFC |
| Your Team | (9, 11) notebook | the notebook, top-down | "Gameplan In" | ERAS_YOURTEAM |
| League History | (17, 19) file cabinets: **unused by any retail row** | cabinets and plant | "Statistics In" | hidden (`+0x2C`) in M3 |
| Office Look / Settings | (1, 3) monitor | the computer | "Statistics In" | ERAS_SETTINGS |

`front_office` (two stops plus the entrance; idle eye (-600, 180.7, 825)):

| row | stop |
|---|---|
| Play Next Game | reverse dolly 1 -> 0 (INFERRED; fallback: no move) |
| Schedule, Standings, History, Settings | (3, 5) the computer |
| Your Team | (7, 9) the contract on the desk |

An era with the monitor hidden (the 1970s) remaps the monitor rows to the notebook (9, 11) or
the planner (5, 7). The stop table is per era (section 2.2). More stops need code-driven keys
written into the camera record before `sub_0013E710` (SCENE 6.4); that is after M3.

At most **6 visible rows**: the `nav_bar_c` limit is untested, and the desk shows up to 8.

### 1.6 Children (plain menus, built like `src/nfl2k5_video_menu.c`)

| screen | template / handler | rows |
|---|---|---|
| ERAS_SCHEDULE | settings list (`0xF3FC0`), own events, flags `0x15` | row 0: type-7 "Week" 1..16; rows 1-14: greyed "AWAY at HOME" with value "-" or "24-17". On a week change, the labels are rewritten and the screen replaces itself (`sub_0006E2E0`), unless T3.6 shows the labels refresh live. |
| ERAS_STANDINGS_AFC / _NFC | settings list, flags `0x15` | row 0 type 9 "Show NFC >" / "Show AFC >" (replace); 3 greyed division headers + 14 teams; value "W-L-T  PF-PA"; help line "Unofficial order (wins, losses, point difference); unverified community rosters" |
| ERAS_YOURTEAM | settings list | one type-7 row: None (watch) / the 28 franchises; locked after the first game |
| ERAS_SETTINGS | settings list | Office Look: Auto (season decade) / 1970s / 1980s / 1990s / 2000s / 2010s / 2020s; Camera Glances: Off/On; Office Ambience: On/Off. Saved in `league.ini`. |
| ERAS_HISTORY | later | needs a sourced history file (champions, records) from the verified league data |

These carry the shared animated backdrop (nibble 1/5). The zoom into the monitor or planner
hides the cut, as in Franchise (MENUS 8.6).

### 1.7 Return from a game (for M4)

Franchise keeps its desk on the FSM stack under the match, so `fr.iff` stays loaded through a
franchise game. msg 5 only clears the scene pointer, and only msg 2 or the Crib unloads
(INFERRED from the desk code). The Eras office should do the same: **do not free the office
packages for a match**. ERAS_RETURN (M4b) pops to **ERAS_OFFICE** instead of the League
screen. The office then gets msg 3 and msg 4 and plays its return move. A memory or crash
problem in T4.1 is the signal to free and reload instead. A reload must complete in msg 3,
before the handler resolves the LAYT.

---

## 2. One office per era

### 2.1 What can change, and how (honest limits)

| change | mechanism | status |
|---|---|---|
| Which room | era record: `coach_desk` or `front_office`, plus its camera name and stop table | VERIFIED code path (both are plain scene lookups by name) |
| Hide a whole prop | `node+0x0C \|= 0x10` (`sub_00021860` skips the node in `sub_0002DE60`) | VERIFIED static; RUNTIME |
| Hide or show a material | `material+8` bit 0 | VERIFIED (the desk does it); `front_office` walls share `lambert1_DOUBLESIDED`, so never there |
| Point a material at another texture | `material+0x30 = TXTR` from `ERASSKIN`, or another material's `+0x30` | VERIFIED for runtime materials; RUNTIME for materials with embedded textures (`carpet1`, `coach01`) |
| Replace pixels (the user's photos) | RETEX option A: era folders on the texture-pack hook, active only while the Eras office context is set | code paths VERIFIED; offline hashes INFERRED until one `texture_dump` run |
| Colour grade without new art | the same hook transforming the retail pixels it receives (`orig`): warmth, saturation, tint per era | INFERRED; a proposal (section 6) |
| Prop **shapes** (CRT depth, rotary phone, typewriter, console TV, flat wall TV) | RETEX E (same-topology morphs) or F (mesh injection) | **not in M3**. Textures cannot change a shape: the coach_desk monitor stays a 2004 flat-panel shape until E or F exists. |

Shared textures limit what can change independently:

* `coach01` is one atlas for the walls, books, shelf, door frame, day-planner cover and picture
  frames. The walls change by painting over the atlas guide.
* `teamlogo` is both the monitor wallpaper and the notebook sticker.
* `vcHwShader27` (light oak) covers the door, end table and floor lamp.

### 2.2 Era recipes

Each era ships with its own made textures in the era folder (2.4): retail textures recoloured
or retextured to the era (walnut paneling, shag, beige CRT faces, period screens), built to look
native, never crude placeholders. The user's own photos and images dropped into the same folder
replace them file by file (the user's files win). The colour notes are the art direction.

| era | room (default) | props (node hides) | screen / TV | surfaces (user photos -> friendly names) | colour direction |
|---|---|---|---|---|---|
| **1970s** | `coach_desk` | hide `monitor_group1`, `teamwallpaper`, `monitor_shadow`, `mousepad`; hide `phone_group1` until a period phone exists (F) | no computer; monitor rows remap to notebook / planner | walnut paneling -> `office_atlas` wall cells; shag -> `carpet`; `desk_metal` -> wood (or swap `vcSwShader16` to the wood panel); vinyl `couch_leather_*`; green `file_cabinet`; 1970s team photos -> `wall_photo_1..8`, `pennant`; era logo -> `team_logo` | walnut, harvest gold, avocado, burnt orange (the retail floor lamp is already orange) |
| **1980s** | `coach_desk` | none | monitor kept: beige face on `computer_monitor`, green-text terminal on the wallpaper (`team_logo` slot; it also changes the notebook sticker) | oak paneling; berber `carpet`; push-button `phone`; 1980s photos | oak, navy, burgundy, beige tech |
| **1990s** | `front_office` | none | wall TV: the user's 1990s broadcast still -> `wall_tv_picture`; computer: the user's period desktop screenshot -> `computer_screen` | `office_photos` / `diploma_*` / `photo_*` -> the user's 1990s photos; carpet; trims in the `front_office` atlas | cherry wood, hunter green, teal, beige CRT |
| **2000s** | `coach_desk` | none | retail (the room is the 2004 office) | the user's 2000s photos and logo | retail |
| **2010s** | `coach_desk` | none | thin-bezel look on `computer_monitor`; modern wallpaper | grey `carpet`, darker `office_atlas` walls, VoIP `phone`, the user's 2010s photos | charcoal, brushed steel, team colour accents |
| **2020s-2025** | `front_office` | hide `phone_group` (node) | wall TV slideshow of the user's 2020-2025 photos (`tv_slideshow\`); modern `computer_screen` | light oak or white trims, glass `desk_metal`, the user's photos | white oak, black, glass |

Which era a league shows: **Auto = the season's decade** (1980 -> 1980s), with the Office Look
setting overriding it. 1980 sits on the decade line; the user may prefer the 1970s den for it
(section 6).

### 2.3 The era record (C table plus per-era `era.ini`)

```
struct EraLook {
  const char *id;            // "1970s"
  const char *room;          // "coach_desk" | "front_office"
  const char *camera;        // "coach_cameraShape" | "FOcamShape"
  float stops[8][2];         // per office row: (a, b), or (0,0) = no move
  const char *hide_nodes[16];
  struct { const char *material, *from_material; } swaps[8];
  const char *ambience;      // "coachambience" by default
  float grade[3];            // optional warmth / saturation / tint
};
```

The defaults are built in. `mods\eras\offices\<era>\era.ini` overrides them:

```
[room]  scene = coach_desk
[hide]  nodes = monitor_group1, teamwallpaper, monitor_shadow, mousepad, phone_group1
[swap]  vcSwShader16 = vcHwShader27        ; metal desk top -> wood panel
[stops] standings = 9,11                   ; monitor gone: notebook
[grade] warmth = 0.15  saturation = 0.9
```

Applying an era at msg 4 or on an era change does four things:

1. Restore all nodes and materials to the load-time state, recorded at first load.
2. Apply the hides and swaps.
3. Bind `teamlogo`, `wallphoto` and `office_photos` to the `ERASSKIN` TXTRs (`+0x30`, `+8 &= ~1`).
4. Set the texture context (2.4).

All of these are game-thread writes into Eras' own copy, so they are exact in the captured
frame (RETEX constraint 2).

### 2.4 The user's photos: folder layout and mechanism

RETEX option A, scoped to the Eras office:

```
mods\eras\offices\
  README.txt                 how to add photos; the friendly names; image rules
  office-textures.ini        friendly name = content hash(es)   (from build/eras/office/office-textures.draft.ini; no pixels)
  atlas-cells.json           photo cell rectangles of wallphoto / office_photos / office_atlas (no pixels)
  default\                   used by every era without its own file
  1970s\ 1980s\ 1990s\ 2000s\ 2010s\ 2020s\
  1980\                      optional exact-season folder, beats 1980s\
    era.ini
    office_atlas.png  wood_panel.png  carpet.png  desk_metal.png  couch_leather_1..4.png
    phone.png  computer_monitor.png  computer_screen.png  wall_tv_picture.png  file_cabinet.png
    wall_photos.png            (full atlas)  OR  wall_photo_1..8.png + pennant.png (pasted into cells)
    office_photos.png          OR  photo_1..3.png, diploma_1..2.png
    team_logo.png              (else mods\teams\<ABBR>\logos\scorebug.png of the user's team)
    tv_slideshow\*.jpg         cycled on the wall TV / monitor
    <HASH>.png                 raw hash names also work (texture_dump workflow)
mods\eras\office\files\      erasoffice.iff, erasskin.iff (generated from the player's disc; gitignored)
```

* **Search order:** `<season>\`, `<decade>\`, `default\`, the global `mods\textures`, then
  retail.
* **Image rules:** any resolution (4x the original is a good target); same aspect as the
  original (square; 4:3 TV picture; 5:4 monitor screen); tileable for carpet, wood and
  leather; PNG with alpha for cut-outs.
* **Guides:** a later tool exports the retail textures from the player's own disc into
  `%LOCALAPPDATA%\NFL2K5-PC\eras\office-guides\` to paint over. The repository ships only names,
  hashes and cell rectangles. **No retail pixels are committed.**
* **Isolation:** the context is set only by the Eras room screens (msg 4) and cleared at
  msg 2 and on match launch. Franchise never sets it, so Franchise stays retail even with
  every era folder installed. The context can lag by about one frame at screen transitions
  (async render thread).
* **Hook change:** `wants()` in `src/nfl2k5_controller_art.cpp` must return 3 for the office
  shapes (P8 256x256, 128x256, 64x32, 32x32 as well as the 128/64 already covered), and
  `gen()` must mix in the era context generation. Those shapes then re-upload once at each
  context change: a short hitch at office entry or era switch.

### 2.5 Browsing eras live

The office shows the league's era by default. LT/RT, gated by T3.7, or the Office Look
setting changes `look` without touching the league. A change works like this:

1. Swap the scene pointer if the room differs (both rooms are in `erasoffice.iff`, so
   nothing reloads).
2. Re-apply the era record.
3. Bump the texture context generation.
4. Rewrite the title.
5. Play "Ambient: Menu Move Cursor".

No package reloads, unlike the desk's per-team re-skin. The paging title strip ("< 1980s
Office >") through `sub_0006BC30`, copying the argument pattern of `sub_00142A30` (msg 8), is
an M3d polish item, because its exact arguments are not decoded.

---

## 3. Revised M3

This replaces `PHASE2-PLAN.md` M3. The earlier M3's league-state and standings parts are kept;
its plain-menu hub and League screen become the office.

### M3: the Eras office (native menu shell with a 3D room) and league save (no launch)

**Goal.** "My NFL Eras" opens a 3D office copied from Franchise's: its own copy of the room,
its own code, its own camera moves. Inside are Schedule, Standings and Your Team, plus the
league save. Each era gets its own office look. No match launch yet (M4).

**Rules.**

* Test on `NFL2K5.exe --retail`.
* Call no Franchise function (list in ERAS-OFFICE-DESIGN 1.2). Never read `[0xE3C0A0]`.
  Never write the desk or Front Office globals (`0xAA2140..0xAA247C`, `0xCB8D10..0xCB8FD8`)
  or `[0xA84498]`.
* No retail pixels in git.
* Served C functions preserve EBX/ESI/EDI/EBP and return with `g_esp += 4` for type-1
  events and type-9 rows.

**New files.**

1. `tools/eras/build-office-pack.py` (offline, run on the player's machine):
   * Read archive entry 23 (`fr.iff`) and `fr31.iff` from `original/disc/vc_53450030` through
     the archive index. Reuse the chunk walker from `build/eras/office/iffls.py`.
   * Write `mods/eras/office/files/erasoffice.iff`: the SCNE `coach_desk` and `front_office`
     records and the LAYT `coach_desk` and `front_office` records, copied byte for byte with
     their 0x20-byte headers (payloads stay VC-LZ, magic `0xFEEDBEEF`), keeping the 16-byte
     alignment rule.
   * Write `mods/eras/office/files/erasskin.iff`: `fr31.iff` byte for byte.
   * `--check` lists both files' records and compares the payload hashes with the source.
   * Add `mods/eras/office/files/` to `.gitignore`.
2. `mods/eras/offices/README.txt`, `office-textures.ini` (from the draft ini; hashes and
   names only), and empty era folders `default`, `1970s`, `1980s`, `1990s`, `2000s`, `2010s`,
   `2020s`, each with its default `era.ini` (text).
3. `src/nfl2k5_eras_office.c/.h`: the room-screen runtime.
   * Package refcount: ctx `ERASOFFICE` / `ERASSKIN` through `sub_00043F50` with two 0x24-byte
     load slots in guest heap, `sub_0014E4B0`, unload `sub_000432F0`.
   * Scene lookup by ctx.
   * Material walk (`[scene+0x1C]` x 0x80 at `[scene+0x20]`, name at +0), node walk
     (`[scene+0x24]` x 0x60 at `[scene+0x28]`, flags +0x0C) and camera walk (`[scene+0x3C]`
     x 0x60 at `[scene+0x40]`). Read the node name offset in the decoded scene first; it
     is not in the research.
   * Load-time snapshot and restore of node flags and material `+8`/`+0x30`.
   * EraLook table plus `era.ini` parser.
   * `move()`, the msg 6 timer and input lock, the msg 7 draw.
   * `[ERAS] office ...` log lines under `NFL2K5_ERAS_LOG=1`.
4. `src/nfl2k5_eras_menu.c`:
   * Served range `0xFEE00000` (`+ screen*0x1000 + slot*4` for events,
     `+ screen*0x1000 + 0x800 + row*0x40 + slot*4` for rows), `nfl2k5_eras_menu_lookup()`
     with a TLS called-address dispatcher as `vm_row_fn`, about 256 KB of guest heap.
   * Screens: 0 LOBBY, 1 OFFICE, 2 SCHEDULE, 3 STANDINGS_AFC, 4 STANDINGS_NFC, 5 YOURTEAM,
     6 SETTINGS, 7 reserved HISTORY.
   * Room descriptors: 0x2C bytes, flags `0x10`, handler `0xF3E90`, layout
     `L"front_office"`, own event table `{1,2,3,4,5,6,7,10}`.
   * Children: settings-list descriptors with their own event tables, as in the earlier M3.
   * The main-menu insert "My NFL Eras" -> LOBBY, as in the earlier M3 (T3.1 fallback
     unchanged).
5. `src/nfl2k5_eras.c/.h`: league state, unchanged from the earlier M3.
   * Read `data\eras\runtime\1980\teams.csv` and `schedule.csv`.
   * `xbox_user_path("eras\\league1\\")`: `league.ini` (version, season, package, user team,
     `office_look`, `glances`, `ambience`, created), `results.csv`, `box\`, `eras.log`.
   * Next fixture, standings from `results.csv`, refusal of unknown versions.

**Changed files.**

* `src/nfl2k5_team_files.c` `nfl2k5_archive_extend`: also scan `mods\eras\office\files\*`,
  the same block as `mods\oldschool\files`.
* `src/recomp_manual.c` `recomp_lookup_manual`: `0xFEE00000` -> `nfl2k5_eras_menu_lookup`.
* `src/main.c`: `nfl2k5_eras_menu_install()` after `nfl2k5_video_menu_install()` (now line
  3814).
* `config/game.cmake`: the four new sources.
* `src/nfl2k5_texture_packs.cpp`:
  * `nfl2k5_texpack_context(const char *season_dir, const char *decade_dir)` / `_gen()`.
  * Era maps scanned from `mods\eras\offices\`, friendly names through
    `office-textures.ini`, searched before `mods\textures` only while the context is set.
  * Cache keyed by file path.
  * Optional pasting of single photos into atlas cells from `atlas-cells.json`.
* `src/nfl2k5_controller_art.cpp`: `wants()` returns 3 for the office P8 shapes, and `gen()`
  mixes in `nfl2k5_texpack_gen()`.
* `tools/drive-menu.ps1`: a `-Retail` switch, as in the earlier M3.

**Gen patches: none.** ARCHIVE_EXTRA_PACK already runs `nfl2k5_archive_extend`. Everything
else is served descriptors and C. If any test shows that the handler resolves the LAYT before
the msg-3 event action, or that msg 7 is not delivered to the event table of a `0x10` screen,
stop and report. Do not patch Franchise code to work around it.

**Steps and tests.** Each step must pass before the next. Use
`tools/drive-menu.ps1 -Retail` with `NFL2K5_ERAS_LOG=1`, screenshots at each check, and
`[FSMOP]` logs.

* **M3a: pack and plain flow.**
  * **T3.0 (offline):** `build-office-pack.py --check` passes. `git status` shows no `.iff`
    under `mods/eras`.
  * **T3.1:** main menu row, as in the earlier M3.
  * **T3.2:** the boot log shows the Eras files in pack G (`[TEAMFILES] N ... files`), and
    `[ERAS] office load` reports the ctx lookups non-zero: `coach_desk` and `front_office`
    SCNE, 39 and 42 materials.
* **M3b: rooms and camera.**
  * **T3.3 room draws:** the lobby shows `front_office` behind a readable title and list.
    The entrance dolly plays once. The idle frame matches
    `build/eras/office/previews/front_office_*` (local reference).
  * **T3.4 camera:** in the office (era 2000s, `coach_desk`), each row moves to its stop
    (screenshot at the stop), and input is ignored while it moves. The child opens on
    arrival. Back plays the return move to idle. `[FSMOP]` balances.
  * **T3.5 isolation:** visit Eras, leave, then start a new Franchise and open the Coach's
    Desk and Front Office. Pass: team logo and wall photos are retail, and there is no
    crash. Then the reverse order: Franchise desk, Quit, then Eras. Pass: the Eras room
    shows `erasskin` art (NFL shield), not the franchise team's.
* **M3c: league screens and save.**
  * **T3.6 flow:** Lobby -> New League -> Office -> Standings (AFC: 3 divisions, 14 teams at
    0-0-0; Show NFC) -> Back -> Schedule (Week 1 of the M0 schedule; change week) -> Back
    -> Your Team -> Back to the main menu. `league.ini` exists.
  * **T3.7 persistence and input:** pick a team, restart, Continue League. Same team, same
    next fixture. Continue is hidden in a fresh profile. Log the button word on LT and on
    RT. Enable LT/RT era browsing only if they read `0x4000` / `0x8000`.
  * **T3.8:** Game Options are untouched, as in the earlier M3.
* **M3d: era looks.**
  * **T3.9 props:** Office Look 1970s hides the monitor and phone completely (node flag
    `0x10`), and the monitor rows move to the notebook. 1990s and 2020s switch to
    `front_office`. Cycle the looks 20 times: no crash, and the working set is stable
    within 5 MB.
  * **T3.10 hashes:** one run with `texture_dump = 1` in the office. The dump contains
    `60BEEEB5`, `DD2E1927`, `E3D7F0CD` and `4C544C90`.
  * **T3.11 user photos:** put images in `mods\eras\offices\1970s\`: the user's photos if
    supplied, otherwise a channel-swapped dump as a test only, never shipped. They show
    only in the 1970s look, and Franchise stays retail.
  * **T3.12 embedded-texture bind:** `era.ini` swap `vcSwShader16 = vcHwShader27` shows a
    wood desk top. If it does not render, drop swaps and use A only.
  * Optional polish, each behind a setting and only after T3.9 passes: glances; the
    `sub_0006BC30` era strip; LAYT `coach_desk` info panels with an Eras binding table.

**Exit:** T3.0-T3.11 pass, or T3.1's fallback is chosen by the user. T3.12 and the polish are
recorded either way.

**M4 changes this implies:** the launch row is the office's "Play Next Game". Keep the
office packages loaded during the match (Franchise's desk stays loaded under its games;
INFERRED). If that fails, free them before the launch and reload synchronously in msg 3.
ERAS_RETURN returns ERAS_OFFICE. Clean-up (restore the snapshot, release the slots) moves
to the office's msg 3. Clear the texture context at launch.

---

## 4. Risks and unknowns

| # | risk / unknown | shows in | fallback |
|---|---|---|---|
| 1 | A `0x10` screen with its own msg-7 draw shows the room behind the list (the Front Office does this, but nothing built from C has) | T3.3 | nibble 7 + an Eras callback in `[0xA84498]`, set at msg 4 and cleared at msg 2 (the desk's way) |
| 2 | LAYT `front_office` must resolve at msg 3; it lives in our package | T3.3 | layout `navigation` with list mode 2 (MENUS hazard 2) |
| 3 | Byte-copied records in a new `.iff` load under a new ctx and decode into their own allocation | T3.2, T3.5 | if both copies alias, restore every edited slot at msg 2 (already done by the snapshot) |
| 4 | Two packages with the same scene names; a ctx-0 lookup by Franchise picks ours if both are loaded | T3.5 | never both loaded: Eras unloads at msg 2; Franchise is reachable only after the main menu |
| 5 | Front-end memory for `erasoffice.iff` (about 0.8 MB compressed; the decoded size is unknown) plus `erasskin.iff` (about 0.2 MB) | T3.2, T3.9 | drop `front_office` from the package, which drops the lobby room |
| 6 | Node flag `0x10` hides a whole prop | T3.9 | material hides, `coach_desk` only |
| 7 | `+0x30` binds over embedded textures | T3.12 | option A only |
| 8 | Offline content hashes equal the renderer's | T3.10 | name files by the hashes from the dump |
| 9 | `wants()` returning 3 for more shapes re-uploads more textures at every gen change (controller setup also bumps gen) | T3.9 timing | return 3 for those shapes only while the era context is set |
| 10 | LT/RT bits are inferred | T3.7 | the Office Look setting |
| 11 | `nav_bar_c` shows how many rows | T3.3 | keep 6 rows |
| 12 | Reverse dolly and glances (time set backwards, partial segments) | M3d | no move |
| 13 | Camera curve contents (stop poses) rest on one reader's evaluator | T3.4 | screenshots decide; re-key in code later |
| 14 | Served functions do not run under interpreted mod packs (PHASE2 hazard 8) | packs | `--retail` only; later extend `native_for` |
| 15 | Prop shapes cannot change by texture: a 1980s/1990s CRT, a rotary phone and a 2020s flat TV need RETEX E/F | user expectation | stated; E/F after M5 |
| 16 | `teamlogo` is both the monitor wallpaper and the notebook sticker; `coach01` covers walls and books | era looks | paint the atlas; accept the shared change |
| 17 | `front_office` is unfinished on the camera side (z > 0) | camera work | keep moves in the front half |
| 18 | Distribution: the `.iff` copies are retail data generated on the player's machine | release | the builder runs at install; nothing ships |

Not researched: whether the office ambience or sounds can be per era (the `AUSB` names are
known; era audio would come from the user's files through the local music system, which is
later work); flag bit `0x80`; the help-bar ids for an LT/RT hint.

## 5. After M3 (not committed)

* Era props with real shapes: RETEX E (CRT depth for the 1980s/1990s monitor), then F
  (rotary phone, typewriter, console TV, 2020s flat wall TV) from the user's models.
* Code-driven camera keys for more stops and a slow idle drift.
* Rebuilt or new rooms per era: the engine looks scenes up by name, so a new SCNE plugs in.
* League History at the file cabinets, from sourced data.

## 6. Decisions for the user

1. Lobby room (walk in through the front office) or straight into the office.
2. The 1980 slice's office: 1980s (decade rule) or the 1970s den.
3. The room per era (defaults in 2.2), and whether props that did not exist yet are hidden
   (default) or wait for reshaped period props.
4. (Settled: each era gets made textures by default; no "photos only" limit.)
5. Which of the user's photos go where (folders in 2.4); they override the made textures.

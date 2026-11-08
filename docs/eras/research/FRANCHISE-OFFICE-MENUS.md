# My NFL Eras: the Franchise office menus, screen by screen, and how to clone the hub

2026-10-15. Read-only research. The user wants My NFL Eras to have a hub like Franchise
mode's office, built as a **copy** (Eras' own screens, not the Franchise screens), with an
office for each era from the 1970s to 2025 ("nobody wants to just sit on a menu, they need
eye candy"). This note covers the **menus**: every Franchise front-end state (FSM descriptor)
from the Game Modes entry through the office hub and its sub-screens, how the office hub
differs from a plain list, which Franchise code a copy must not run, and what a copy needs
(descriptors, rows, event tables, served functions), in the way `NATIVE-MENU.md` section 2
describes.

Companion notes written by parallel tasks the same day. Not repeated here:

* **`FRANCHISE-OFFICE-SCENE.md`**: the 3D rooms (`coach_desk`, `front_office`,
  `draft_menu`), camera curves and every keyed camera pose, the objects and materials, preview
  renders, and the Crib comparison.
* **`OFFICE-RETEXTURE.md`**: how to dress each era (texture-pack folders gated by context,
  material edits, salted clones, prop morphs, mesh injection) and the photo folder layout.

Nothing was built or launched, and nothing in `src/`, `tools/`, `external/`, `config/` or
`saves/` was edited. Evidence levels:

* **verified (XBE)**: read from `original/default.xbe` with the scripts in
  `build/eras/office/` (capstone disassembly, section-mapped data).
* **verified (disc)**: decoded from `original/disc/vc_53450030/` (archive index
  `analysis/archive-index.csv`, VC-LZ, SCNE tables).
* **verified (code)**: read in this project's sources or in `build/gen-locals/*.c`.
* **inferred**: follows from the reading, not checked.
* **to test**: needs a game run.

---

## 0. Short version

* The Franchise hub is **Coach's Desk**, descriptor `0x00522190`: a normal link-list screen
  (handler `0x000F3E90`). It is also a 3D office. Layout `coach_desk`, flags `0x07`, 11
  type-9 (action) rows, an event table with 6 callbacks (`0x00521EE8`), no help bar, and no
  Back (it is the root of a franchise).
* Four things make it an office rather than a list (all verified (XBE)):
  1. **The background mode in the low 4 bits of the descriptor flags.** Mode 7 draws a
     one-slot "3D background" callback `[0xA84498]`, which the desk sets at msg 4
     (`sub_00069BB0(0x142820)`). The callback draws SCNE `coach_desk` from the scene camera
     `coach_cameraShape`.
  2. **Every row is an action that moves the camera first and opens its screen later.** The
     row action plays an "Ambient: ... In" sound, starts a 1.4-second camera segment on the
     scene's own animation curve, and stores the destination in `[0xAA2408]`. The msg-6
     per-frame callback plays the segment at 1.5x speed with menu input locked, then pushes
     the stored screen. When the desk is shown again, msg 4 plays the return segment.
  3. **Live dressing.** The per-team pack `fr<code>.iff` supplies the textures `wallphoto`
     (wall photo atlas) and `<code>_teamlogo_00_h0`, which are written into the scene's
     material slots (`material + 0x30`). With two or more user teams, LT/RT switch the team
     and the room re-skins live. Msg 8 then draws the team name with paging icons.
  4. **Info panels and ambience.** The `coach_desk` layout has two info panels
     (`coach_desk_info_a` in season, `_b` in the offseason) filled by text callbacks
     (week, record, next game, mini helmets). A `coachambience` sound loop plays underneath.
* **The Front Office (`0x0052533C`) is a second, self-contained copy of the same machinery.**
  It has its own scene (`front_office`), camera (`FOcamShape`) and globals, and it draws the
  room from its own **msg-7 event action** with background mode 0 (flags `0x10`). That is the
  simplest pattern for an Eras copy.
* Everything else in Franchise is ordinary list, settings, table or custom screens that read
  Franchise data (section 6). **None of those can be reused by Eras.** Only their look can be
  copied.
* Several desk callbacks run Franchise logic, not just drawing:
  * msg 4 runs **season-stage processing** (`sub_002486F0`, `sub_0024BF80`; the latter can
    push The Draft);
  * msg 6 **writes the coached team** (`sub_0013EC90`);
  * msgs 1 and 4 and the info-panel callbacks dereference the coached-team pointer
    `[0xE3C0A0]`;
  * Quit tears down the roster and season (`sub_000C0110`, `sub_000C7570`).

  So an Eras copy must **own every callback** and call only the engine primitives listed in
  section 7.2. It must never point at the desk's event table, row actions, binding table or
  globals.
* Clone recipe (section 8): one served descriptor (copied fields, own title, own event table,
  own type-9 rows at `0xFEE0xxxx`, layout `front_office` or `coach_desk`, flags `0x10`). Its
  msg 1 loads a copied office package and an era skin package under Eras context names, msg
  4 finds the scene, binds the era textures and plays the return move, msg 6 runs the camera
  timer and pending push plus LT/RT era switching, msg 7 draws the room, msg 8 draws the era
  strip, and msg 2 unloads. No generated-code patch is needed.
* Corrections to `NATIVE-MENU.md` (section 9):
  * A descriptor is **0x2C bytes**. What follows it belongs to other objects.
  * Flag bits 0..3 are the **background mode**. `0x04` is not "scroll".
  * The pad bit table is now decoded: `0x4000` = LT and `0x8000` = RT. The table is
    verified; the button order behind it is inferred.

---

## 1. The screen graph

Addresses are descriptors. `[link]` = type-0 row, `[act]` = type-9 row (target found in the
action's code), `(msg N)` = pushed by an event callback, `~>` = deferred push after a camera
move. All verified (XBE) unless marked.

```
Main Menu 0x515660
 +- [link] Game Modes 0x5015CC
     +- [link] Franchise -> Franchise Options 0x500DC8  (settings, START advances)
         +- (msg 11, sub_00148CC0: validate 0x148AB0, init 0x10EA10, league 0x13EE10)
            -> Customize League 0x52ABE0  (custom, spreadsheet; msg 1 sets [0xCBD534] = 0x589D80)
               +- (msg 11, sub_00355E30) -> Team Select 0x589D80  (table, pick user teams)
                   +- (msg 11, sub_00372B70: >= 1 user team, set coached team) -> 0x58095C
                       (table; help "to select player" / "& hold to speed up other teams
                        selection": the roster/fantasy draft step, inferred; its msg 1
                        stores completion sub_0013F1B0 at [0xCC38A8])
                       +- completion sub_0013F1B0 -> PUSH Coach's Desk 0x522190
 Load path: Main Menu > Options 0x503288 > Load / Save 0x508DF0 > Load 0x506EC0
            (msg 6 sub_0016DD30, I/O state 7)
            -> pop to root, replace top with Coach's Desk 0x522190 (the desk becomes depth 0)

Coach's Desk 0x522190  (link list, layout coach_desk, flags 0x07, 3D office)
 +- [act] Schedule / Playoff / Super Bowl / Off-Season Schedule (one visible per phase)
 |     ~> Schedule 0x522828 (custom calendar; A on a week -> sub_00135790:
 |        Weekly Preparation 0x51FEE4 / sim prompts / visual simulator 0x51C460 / game)
 +- [act] Front Office ~> The Front Office 0x52533C (link list, layout front_office, 3D office #2)
 |     +- [act] Franchise Status  ~> 0x585548 (custom, franchise_status)
 |     +- [act] Contracts         ~> Player Contracts 0x540650 (table; free agents, offers;
 |     |                             popups -> Sign Contract 0x557B2C / Offer Contract 0x557DBC)
 |     +- [act] Cap Management    ~> 0x584BA8 (table, franchise_cap_manager)
 |     +- [act] Team Needs        ~> 0x582EA0 (table, team_needs)
 |     +- [act] Trades            ~> Player Trade 0x55A0F0 (table)
 |     +- [act] Trading Block     ~> 0x55A544 (table; -> 0x55A31C, 0x55A0F0)
 |     +- [act] Player Progress   ~> 0x551800 (table)
 |     +- [act] Pro Bowl Votes    ~> 0x54AC78 (table)
 +- [act] Gameplan ~> Gameplan 0x503D6C (link list, navigation)
 |     +- [act] Depth Chart -> 0x532D14 (Offense / Defense / 3-4 / Special Teams -> table 0x533088)
 |     +- [link] Playbook Manager -> 0x519920 (msg 1 pushes 0x519AE0 or 0x519C30)
 |     +- [link] Coach's Card -> 0x52D9F8 (custom, coach_card)
 |     +- [link] Coach Gameplan -> 0x52BAE0 (settings, 7 sliders)
 +- [act] ESPN.com ~> ESPN.com 0x525664 (link list, league_news_main)
 |     +- E-mail Inbox 0x522A68 (custom; same calendar code as Schedule, inbox pages)
 |     +- Team Standings 0x54CEA0, Transactions 0x5623C0, Statistics 0x561910
 |     |     (Team Stats 0x54CACC, Player Stats 0x54A5A8, Season Stats 0x54A770,
 |     |      League Leaders 0x54A910, Rookie Watch 0x54AAD8)
 |     +- Player Awards 0x561750, Injury Report 0x52D478, Power Rankings 0x561448,
 |        Playoff Picture 0x560DC0, Rookie Report 0x53C7F4 / 0x53C9F4,
 |        Mel's Mock Draft 0x53CC1C, Draft Recap 0x56060C  (phase-gated by row updates)
 +- [act] The Crib ~> 0x524FA0 (custom 3D apartment; the desk unloads its packs first)
 +- [act] Features ~> 0x525930: VIP 0x574BF8, Celebrations 0x5755A4, Stadium Music 0x52F9FC
 +- [act] Options  ~> 0x503458: Franchise Options -> Franchise Settings 0x52BDD8, Game Options,
 |                   Load / Save 0x508F8C (Save Franchise -> 0x507EC8 ...), Difficulty,
 |                   Presentation, Penalties, Controller Setup
 +- [act] Quit -> sub_000C8190: Yes/No, roster/season teardown, Main Menu
 +- (msg 4, sub_0024BF80, draft stage with user drafting) -> The Draft 0x537E38 (custom 3D room)
 Back to the desk: after the draft, sub_003625F0 / sub_00363170 pop until 0x522190 (sub_0006E450)
```

Reproduce: `python build/eras/office/fsm_graph.py 5015CC 500DC8 522190 --depth 4`. This
crawls 158 reachable screens, including the shared Options and Features trees. The output is
in `build/eras/office/franchise_graph.txt`, and the curated 42 franchise screens are in
`franchise_screens.txt` / `.json`.

---

## 2. Entry and exit paths (verified (XBE))

| Step | Where | What happens |
|---|---|---|
| Game Modes row 0 "Franchise" | `0x5015CC` row 0, type 0 | push Franchise Options `0x500DC8` |
| Franchise Options | settings list, flags `0x55`, help id 9 " to Advance" | 13 rows: Weekly Preparation, First Person Football, Fantasy Draft Roster, Customize League, Preseason Games, Trade Deadline, Owner Firing, Edit Rookies (Off/On); Draft Players, Sign Draft Picks, Re-sign Players, Sign and Cut, Update Depth Chart (value lists) |
| START (msg 11) | `sub_00148CC0` | `sub_00148AB0` validates, `sub_0010EA10` + `sub_0013EE10` initialise the league (cap, orders, salaries, draft class: see the MyCareer design report in the Mod Tools), push Customize League `0x52ABE0` |
| Customize League | custom (`0xF3DA0`), spreadsheet, flags `0x55` | msg 1 `sub_00355DE0` stores Team Select `0x589D80` in `[0xCBD534]`; msg 11 `sub_00355E30` pushes `[0xCBD534]` |
| Team Select `0x589D80` | table, layout `options`, flags `0xD5` | msg 11 `sub_00372B70`: "You must select at least one team" (`sub_0014E520`) or set the coached team (`sub_0013EC90`) and push `0x58095C` |
| `0x58095C` | table, flags `0x45` | msg 1 `sub_0036FB50`: `[0xCC38A8] = sub_0013F1B0` (completion). Its twin `0x5806A8` stores `sub_0036F1C0` |
| `sub_0013F1B0` | completion | per-team setup loop, `[0xE60128] = 0`, `sub_0027EB80`, then **`sub_0006E390(FSM, 0x522190)`** at `0x13F2D9` |
| Load a franchise | Load `0x506EC0` msg 6 `sub_0016DD30` | I/O state `[0xBDBDA0]` 7: `sub_0006E480` (pop to root), `sub_0006E2E0(FSM, 0x522190)`; state 8 does the same with Tournament `0x52A880` |
| After the draft | `sub_003625F0`, `sub_00363170` | when `[0xE3C274]` reaches 0: `sub_002480B0`, then `sub_0006E450(FSM, 0x522190)` = pop until the desk is on top |
| After a game | game state `0x4E7EC0` ended -> pop | the desk stays under the game and is uncovered (msg 3, then 4). See the Mod Tools autosave and practice-exit reports |
| Quit row | `sub_000C8190` | Yes/No dialog `sub_0014E440(0xE3C040, "You will lose your current game if you q...", rows 0x504324)`; on Yes: wait for loads, `sub_000C0110` (roster teardown), `sub_000C7570(0)` (season reset), `[0xE5FFE4] = 0`, pop to root, replace with Main Menu `0x515660` |

`sub_0006E450(ecx = FSM, edx = descriptor)` pops until that descriptor is on top (verified
(XBE)). `NATIVE-MENU.md` did not list it.

---

## 3. Coach's Desk `0x00522190`, decoded (verified (XBE))

### 3.1 Descriptor

| Off | Value | Meaning |
|---|---|---|
| +0x00 | `0xE9A848` "Coach's Desk" | title |
| +0x04 | `0x521EE8` | event table |
| +0x08 | `0x000F3E90` | link-list handler |
| +0x0C | 0 | no help bar |
| +0x10 | `0x521F20` | 11 rows + end |
| +0x14 | 0 | no table |
| +0x18 | `0xE9A864` "coach_desk" | LAYT in `fr.iff` (only present while `fr.iff` is loaded) |
| +0x1C | `0xACF1A8` | layout bindings (info panels, 3.4) |
| +0x20, +0x24 | `0x02400044`, `0x018D0052` | the list frame (int16 pairs 68/576, 82/397; inferred from `sub_0027C080`) |
| +0x28 | `0x07` | background mode 7, no Back (`0x10` clear), no START advance |

### 3.2 Rows (`0x521F20`, 0x34 bytes each, all type 9)

| # | Label | Action | Row update (+0x2C) | Sound | Camera (a, b) | Opens (deferred) |
|---|---|---|---|---|---|---|
| 0 | Schedule | `0x142880` | `0x2C01C0`: visible only in phases 7, 8 | Ambient: Calendar In | (5, 7) | `0x522828` |
| 1 | Playoff Schedule | `0x142880` | `0x2C01F0`: phase 9 and week != 20 | Calendar In | (5, 7) | `0x522828` |
| 2 | Super Bowl Schedule | `0x142880` | `0x2C0220`: phase 9 and week == 20 | Calendar In | (5, 7) | `0x522828` |
| 3 | Off-Season Schedule | `0x142880` | `0x2C0250`: phase not 7, 8 or 9 | Calendar In | (5, 7) | `0x522828` |
| 4 | Front Office | `0x142910` | | Front Office In | (13, 15) | `0x52533C` |
| 5 | Gameplan | `0x142950` | | Gameplan In | (9, 11) | `0x503D6C` |
| 6 | ESPN.com | `0x142990` | | none | (1, 3) | `0x525664` |
| 7 | The Crib\|TM\| | `0x1428C0` | | Front Office In | (13, 15) | `0x524FA0`, only if `sub_001423B0(0) >= 0` |
| 8 | Features | `0x1429F0` | | Statistics In | (1, 3) | `0x525930` |
| 9 | Options | `0x1429B0` | | Statistics In | (1, 3) | `0x503458` |
| 10 | Quit | `0xC8190` | | | none | Yes/No, then Main Menu (section 2) |

The phase word is `[0xE576A4]` (`sub_000C4BB0`): 7 preseason, 8 regular season, 9 playoffs,
lower values the offseason stages (inferred from the row gates and the MyCareer report). The
row action pattern (`sub_00142910`, verified (XBE)):

```
push 0 x6; ecx = L"Ambient: Front Office In"; eax = sub_00038650()   ; sound by name
ecx = eax; edx = 0; sub_00089DA0()                                   ; play it
push 15.0; push 13.0; sub_001427A0()                                 ; camera segment (a = 13, b = 15)
[0xAA2408] = 0x52533C                                                ; pending screen
ret
```

What each stop frames, in the scene's own camera curve (`FRANCHISE-OFFICE-SCENE.md` 3.4):
1-3 the monitor, 5-7 the day planner, 9-11 the notebook, 13-15 the door. A fifth keyed stop,
17-19 (file cabinets), is not used by any retail row.

### 3.3 Event table `0x521EE8`

| Msg | Records | Function | Does | Franchise state? |
|---|---|---|---|---|
| 1 created | `0x521E58` type 3 | `sub_00142DA0` | camera object init `sub_00066B20(0xAA2150)`, zero `[0xAA2400..0xAA240C]`, then `sub_00142D00`: queue `fr.iff` (ctx `FRANCHISE1`, slot `0xAA2410`), `sub_00142BB0(0)` (team pack ctx `FRANCHISE2` "fr<code>.iff", slot `0xAA2434`, "Loading, please wait" `sub_0014E4B0`, bind `teamlogo`/`wallphoto`), `mini.iff` (ctx `FRMINI`, `sub_000441B0`, slot `0xAA2458`), LAYT `title_bar_news(_sub)` bindings, `[0xAA240C] = 1` | **reads** coached team `[0xE3C0A0]` (+0x10C asset code) |
| 4 shown | `0x521D38` type 1 | `sub_00142FB0` | `sub_00165BE0("coachambience", 20, 40)`; `[0xAA2140] = sub_000449E0(0, 'SCNE', "coach_desk")`; `sub_00069BB0(0x142820)`; `sub_00142D00`; `sub_0027D380`; `[0xAA2408] = 0`; `sub_0014FCD0(FSM, 0)` (list mode 0); return move `sub_001427A0([0xAA2404], 0)`; **`sub_002486F0`**, **`sub_0024BF80`**, **`sub_00110E60(7, 1)`**; re-skin if the coached team changed | **yes**: stage advance and weekly processing (`sub_002480B0`, `sub_00247D40`), auto offseason steps, may push The Draft |
| 5 covered | `0x521D80` type 3 | `sub_00142BA0` | `[0xAA2140] = 0`: the room is not drawn under sub-screens | no |
| 6 frame | `0x521DC8` type 1 | `sub_00142DD0` | camera timer and deferred push (4.3); info panel a/b by phase (`sub_00143FE0`); with 2+ user teams, LT/RT on any pad switch the coached team and re-skin | **writes** `[0xE3C0A0]` (`sub_0013EC90`) |
| 8 overlay | `0x521E10` type 3 | `sub_00142A30` | with 2+ user teams: font 3, coached team's name (team `+0x104`) at (320, 73) by `sub_0006BC30(edx = 1, ...)`, the paging title with icons | reads |
| 2 destroyed | `0x521EA0` type 3 | `sub_00142B80` | `sub_000F6510(1)`, `sub_00165C10` (ambience off), `sub_00142B40` (unload `FRANCHISE1`, `FRANCHISE2`, `FRMINI`), `sub_0027D3A0` | no |

There is no msg 3 (the link-list handler rebuilds the rows and loads the layout itself), no
msg 7 (mode 7 draws through the callback) and no msg 10 (no Back).

### 3.4 Layout and bindings

LAYT `coach_desk` (in `fr.iff`, verified (disc)) instances: `title_bar_sub` (template
`title_bar`), **`nav_bar_c` (template `navigation`)**, `coach_desk_info_a`,
`coach_desk_info_b`. The rows are drawn by `nav_bar_c` through the shared navigation text
binding (`0xAD0018`, `NATIVE-MENU.md` 1.7). The game's layout-name hash is
**CRC32 of the upper-cased UTF-16 name**, the same function as the archive file names
(`coach_desk_info_a` = `0x41DCCE88`, `coach_desk_info_b` = `0x6AF19D4B`; verified).

Binding table `0xACF1A8` = `{+0 text list 0xACF0F8, +4 0, +8 image list 0xACF0B8, +0x10 0}`,
copied into the layout instance by `sub_00143EA0` (+0x0C, +0x10, +0x14, +0x18).

| List | Entries `{instance hash, element hash, callback, arg}` |
|---|---|
| text `0xACF0F8` | info_a and info_b element `0xE98010FA` -> `sub_002C0290` (`"%s %s Week %d of %d"`); info_a `0x910DB446`, `0xBA20E785`, `0xA33BD6C4`, `0xEC7A4003` -> `sub_002C03C0` arg 0..3 (record `"%d-%d-%d"`, ...); the same four in info_b -> `sub_002C05E0` |
| image `0xACF0B8` | info_a `team1helmet1` / `team2helmet1`, info_b `team1helmet1` -> `sub_002C06D0` arg 0/1 (mini helmets `mini_a<code>_<n>` / `mini_h<code>_<n>` from `FRMINI`) |

Text callback ABI (`sub_00143450`, verified (XBE)): `ecx` = a layout pointer, `edx` = the
entry's arg, stack `[esp+4]` = the output UTF-16 buffer `0xBD6830`, `[esp+8]`, `[esp+0xC]` =
two context words (`0xAA2870` is one). The callee pops 12 (`ret 0xC`), and the element text is
then set from the buffer (`sub_00151440`). The list ends at the first entry whose callback is
0. A lookup that finds nothing writes "unknown label".

### 3.5 Desk globals (verified (XBE))

| Address | Meaning |
|---|---|
| `0xAA2140` | scene object (`coach_desk`), 0 while covered |
| `0xAA2150` | camera object (0x2B0 bytes up to `0xAA2400`) |
| `0xAA2400` | camera target time |
| `0xAA2404` | return move end time (b of the last selection) |
| `0xAA2408` | pending screen to push on arrival |
| `0xAA240C` | packages loaded |
| `0xAA2410`, `0xAA2434`, `0xAA2458` | load slots for `FRANCHISE1`, `FRANCHISE2`, `FRMINI` |
| `0xAA247C` | the team the room is dressed for |
| `0xA84498` | 3D background callback (set by `sub_00069BB0`) |
| `0xBD8050` | menu input enable (`sub_0014FC30` = 1, `sub_0014FC40` = 0) |

---

## 4. What makes the desk an office and not a list

### 4.1 Background mode = flags & 0xF (verified (XBE), new)

`sub_000F2810` (the first step of every generic handler's msg 7) jumps to `sub_0006C080`. That
reads the top descriptor's `+0x28`, sign-extends the low 4 bits, and dispatches through the
jump table at `0x6C05C` (`sub_0006BFD0`). Per-frame updates use the same nibble
(`sub_0006B7F0` -> `sub_0006B750`, table `0x6B7D0`).

| Nibble | Draw | Per-frame update | Seen on |
|---|---|---|---|
| 0 | nothing | nothing | The Front Office (`0x10`), The Crib, The Draft |
| 1, 3, 5 | `sub_0006B490` (shared animated backdrop) | `sub_0006B430` | most menus (`0x11`, `0x15`, `0x55`, `0x95`) |
| 2 | `sub_0006BEF0` | `sub_0006B550` | Main Menu (`0x02`) |
| 4 | `sub_0006C000` (a panel rectangle from `sub_000F31B0`) | none | |
| 6 | SCNE `Menubackground` / `BACKDROP1` (`sub_0006BF70`) | advances that scene | NFL Draft `0x5603E4` (`0xC6`) |
| 7 | the callback `[0xA84498]` (`jmp eax`, ecx = FSM) | none (the screen updates its own scene) | Coach's Desk (`0x07`) |

Other flag bits found: `0x10` Back pops (`sub_000F3970`), `0x40` START sends msg 11
(`sub_000F3970`, `sub_00150020`), `0x20` read only by Team Select code (`sub_002C1A50`).
`0x80` is common on Franchise sub-screens (`0x95`, `0xD5`) but no `test [desc+0x28], 0x80`
was found, so it remains unidentified. A full scan of `test byte [r+0x28], imm` found nothing
else for descriptors.

### 4.2 The room behind the list

The room is SCNE `coach_desk` in `fr.iff` (archive entry 23, 2,500,144 bytes; verified
(disc)). It has 39 materials, 52 nodes and 52 shapes, and exactly **one animation binding:
the camera** (scene header `+0x0C` count = 1). The only thing that moves is the camera;
there are no animated props. The callback `sub_00142820`:

```
if ([0xAA2140]) {
  sub_0002AD80(&tmp)                                     ; save the current camera (aligned stack frame)
  esi = scene; push L"coach_cameraShape"; sub_00142730() ; camera record by name: [scene+0x3C] x 0x60 at [scene+0x40]
  sub_0013E710(ecx = 0xAA2150, edx = record)             ; game camera from the scene camera at the scene time
  sub_0002AC80(ecx = 0xAA2150)                           ; make it current
  sub_0002DE60(ecx = scene)                              ; draw the scene
  sub_0002AC80(ecx = &tmp)                               ; restore
}
```

The 2D layout (title bar, list, info panels) is drawn afterwards by the same msg-7 pass
(`sub_000F2F70`, then `sub_00150260`).

### 4.3 Camera move on selection, deferred push (verified (XBE))

```
sub_001427A0(a, b)                       ; stdcall, ret 8
  [0xAA2404] = b; [0xAA2400] = a
  if !scene: return
  if a == 0: scene time = 0; return      ; sub_0002F010(ecx = scene, push t): [scene+4] = t, evaluate
  scene time = a - 1.0
  if b > 0: [0xAA2400] += 0.4            ; "in" moves hold 0.4 s at the stop before the push

sub_00142DD0 (msg 6)
  if [scene+4] < [0xAA2400]: sub_0014FC40(); sub_0002F030(ecx = scene, push dt * 1.5)
  else: sub_0014FC30()
        if [0xAA2408]: sub_0002F010(scene, [0xAA2400])
                       if pending == Crib: sub_0027D3A0(); sub_00142B40()   ; free memory first
                       sub_0006E390(FSM, [0xAA2408])
```

So **the camera moves only when a row is chosen**. Highlighting a row does nothing. "In" is
`a-1 -> a+0.4` (about 0.93 s at 1.5x), and then the screen opens. When the desk is shown
again, msg 4 plays `b-1 -> b` (about 0.67 s) back to the idle pose. While the camera moves,
menu input is off. On first entry `[0xAA2404]` is 0, so there is no intro move. The Front
Office instead starts with return time 1.0 and plays a 0 -> 1 dolly at 1x speed.

### 4.4 Dressing, team switching and the name strip

* `sub_00142BB0(wait)`: the team code is `[[0xE3C0A0] + 0x10C]`. It queues "fr<code>.iff"
  (85 packs `fr00..fr99.iff`, archive entries 24-108, each with 4 TXTRs: `<code>_teamlogo_00_h0`
  256x256, `pdalogo` 64x64, `wallphoto` 256x256 and `office_photos` 256x256, all P8, format
  word `0x08810B29`; verified (disc)). It hides the materials `teamlogo` and `wallphoto`
  (`+8 |= 1`, `+0x30 = 0`) and waits. It then binds `teamlogo` -> `sub_00142AF0()`
  (`"%s_teamlogo_00_h0"`) and `wallphoto` -> TXTR `wallphoto`, and stores the team in
  `[0xAA247C]`.
* Material walk `sub_001426E0`: `esi` = scene (register argument), push name, `ret 4`;
  materials `[scene+0x1C]` x 0x80 at `[scene+0x20]`, name pointer at `+0`.
* Which objects carry which material (verified (disc), `iffls.scene_shapes`): `wallphoto` is
  on `wall_pic01..08`, `wall_pennant`, `mug_group` and `mousepad`; `teamlogo` is on
  `teamwallpaper` (the monitor) and `notebook_logo`. The full material and object tables are
  in `FRANCHISE-OFFICE-SCENE.md` section 4.
* LT/RT: `sub_00070A10(ecx = pad, edx = 0)` returns the game's button word. Bit `0x4000` =
  LT goes to the previous user team, `0x8000` = RT to the next (table `0x4E95C0`, section
  9). The desk checks pads 0..7, and only when not animating and with
  `sub_0013EC40() >= 2` user teams.
* Msg 8 `sub_00142A30` draws the current team's name as a centred paging title
  (`sub_0006BC30`, the call Controller Setup uses for its |LT| / |RT| page title).

### 4.5 Sounds

The sounds are `"Ambient: Calendar In"`, `"Front Office In"`, `"Gameplan In"` and
`"Statistics In"` (strings at `0xE7C890..0xE7C91C`). They are played by
`sub_00038650(ecx = name, six zero pushes)`, then `sub_00089DA0(ecx = eax, edx = 0)`. The
Front Office plays `"Ambient: Statistics Out"` on Back. The loop is
`sub_00165BE0(ecx = L"coachambience", push 20.0, push 40.0)` (an `AUSB` resource), stopped
by `sub_00165C10`.

---

## 5. The Front Office `0x0052533C` (verified (XBE))

Title "The Front Office", link list, layout `front_office` (LAYT: `title_bar_wide`,
`nav_bar_c`), binding 0, flags `0x10` (Back pops, background mode 0), no help bar.

| Msg | Function | Does |
|---|---|---|
| 1 | `sub_0034DAA0` | `[0xCB8D10] = SCNE "front_office"`; `teamlogo` -> `sub_00142AF0()`; `office_photos` -> TXTR `office_photos`; camera init `sub_00066B20(0xCB8D20)`; time 0; target `[0xCB8FD0]` = 0; return `[0xCB8FD4]` = 1.0; pending `[0xCB8FD8]` = 0 |
| 4 | `sub_0034DBB0` | coachambience; `[0xCB8FD8] = 0`; `sub_0034D7A0([0xCB8FD4], 0)` (first time the 0 -> 1 dolly) |
| 6 | `sub_0034DB50` | the desk's timer: 1.5x except when the target is 1.0 (the dolly at 1x), input lock, then `jmp sub_0006E390` with the pending screen |
| **7** | **`sub_0034DA20`** | **draws the room** exactly as `sub_00142820`, with camera `FOcamShape` and camera object `0xCB8D20` |
| 10 | `sub_0034DA80` | "Ambient: Statistics Out" (the pop itself comes from flag `0x10`) |

| Row | Action | Camera (a, b) | Opens |
|---|---|---|---|
| Franchise Status | `0x34D920` | (3, 5) computer | `0x585548` |
| Contracts | `0x34D820` | (7, 9) contract on the desk | `0x540650` |
| Cap Management | `0x34D960` | (3, 5) | `0x584BA8` |
| Team Needs | `0x34D8E0` | (3, 5) | `0x582EA0` |
| Trades | `0x34D860`, update `0x2C2640` | (3, 5) | `0x55A0F0` |
| Trading Block | `0x34D8A0`, update `0x2C2640` | (3, 5) | `0x55A544` |
| Player Progress | `0x34D9A0`, update `0x2C26A0` | (7, 9) | `0x551800` |
| Pro Bowl Votes | `0x34D9E0`, update `0x2C2660` | (3, 5) | `0x54AC78` |

Its own code does not touch Franchise state except `sub_00142AF0` (coached-team code for the
logo name). Its children are all Franchise data screens. **This screen is the cleanest
template.** It needs no callback slot, it has its own globals, and it has a Back.

---

## 6. Every other Franchise screen (verified (XBE))

Handler: link `0xF3E90`, settings `0xF3FC0`, custom `0xF3DA0`, table `0xF40F0`. Event
callbacks are listed as `msg:function`. All of these read or write Franchise data (roster,
season, contracts, schedule), so **Eras cannot push any of them**.

| Descriptor | Title | Handler | Layout | Flags | Events | Rows |
|---|---|---|---|---|---|---|
| `0x522828` | (none) Schedule | custom | - | 0x95 | 1:327A00, 3:3275F0+327340, 4:327620, 5:3275C0, 6:3276C0, 8:327690 | calendar pages `[0xC95E04]` = `0xAE3750` (season) or `0xAE3770` (offseason); A on a week -> `sub_00135790` |
| `0x522A68` | (none) E-mail Inbox | custom | - | 0x95 | same, msg 3 `sub_00327390` (pages `0xAE3780`) | |
| `0x503D6C` | Gameplan | link | navigation | 0x11 | 10:14CD80 | Depth Chart (act `0x243F10` -> `0x532D14`), Playbook Manager -> `0x519920`, Coach's Card -> `0x52D9F8`, Coach Gameplan -> `0x52BAE0` |
| `0x525664` | ESPN.com | link | league_news_main | 0x11 | 4:2C26E0 (list mode 2) | 12 links, 8 phase-gated by row updates `0x2C26F0`..`0x2C28D0` |
| `0x525930` | Features | link | navigation | 0x11 | - | VIP, Celebrations (act `0x2C2930`), Stadium Music |
| `0x503458` | Options | link | navigation | 0x11 | - | Franchise Options, Game Options, Load / Save (update `0x14CCD0`), Difficulty, Presentation, Penalties, Controller Setup (update `0x14CCB0`); this project appends its own links here (`vm_add_link`) |
| `0x524FA0` | (none) The Crib | custom | - | 0x00 | 1:270490, 2:270600, 4:278310, 6:278390, 7:275B70 | 25-state machine; see `FRANCHISE-OFFICE-SCENE.md` 5 |
| `0x537E38` | The Draft | custom | - | 0x00 | 1:326AD0, 2:325DB0, 4:325E10, 5:325DF0, 6:326C50, 7:326250 | draft room SCNE `draft_menu` |
| `0x585548` | Franchise Status | custom | franchise_status | 0x95 | 1:367D50, 9:6C080 | |
| `0x540650` | Player Contracts | table | spreadsheet | 0x95 | 1:2B9960, 2:2B8060, 3:69E50, 4:2B80A0, 8:2B99B0+2B99F0, 18:320EC0 | per-player popup `0x2B83F0` (table `0x521340`, 10 actions) |
| `0x584BA8` | (none) Cap Management | table | franchise_cap_manager | 0x55 | 4:366160, 6:3662C0 | |
| `0x582EA0` | Team Needs | table | team_needs | 0x95 | 1:2B9960, 2:2B8060, 4:69E50, 18:320EC0 | |
| `0x55A0F0` | Player Trade | table | spreadsheet | 0x55 | 1:2B9960, 2:2B8060, 4:348110, 8:348680, 10:348190, 11:348150, 18:320F30 | |
| `0x55A544` / `0x55A31C` | Trading Block | table | spreadsheet | 0x15 | 4:348CD0+348D20, 6:349740, 8:3491A0, 12:3498D0 | |
| `0x551800` | Player Progress | table | spreadsheet | 0x95 | 4:365F70, 6:365CD0, 8:365CE0 | |
| `0x54AC78` | ProBowl Votes | table | spreadsheet | 0x15 | 1:14EDD0, 2:14EE10, 8:3215E0 | |
| `0x532D14` | Depth Chart | link | navigation | 0x11 | 2:2440F0, 4:243CF0 | Offense / Defense / 3-4 Defense / Special Teams -> table `0x533088` (dc_overview) |
| `0x52D9F8` | Coach's Card | custom | coach_card | 0x95 | 1:2C8120, 6:2C84D0, 9:6C080 | |
| `0x52BAE0` | Coaching Gameplan | settings | options | 0x95 | 3:69E50 | 7 kind-4 slider rows |
| `0x54CEA0` | Team Standings | custom | league_news_common | 0x15 | 1:3550F0+14EDD0, 2:14EE10, 5:355130, 6:3551A0, 8:3554B0+355CD0, 9:6C080 | |
| `0x561910` | Statistics | link | navigation | 0x11 | 4:C83C0 | Team Stats, Player Stats, Season Stats, League Leaders, Rookie Watch |
| `0x561750` | Awards | custom | spreadsheet | 0x05 | 14-17 page msgs | |
| `0x5623C0`, `0x52D478`, `0x561448`, `0x560DC0` | Transactions, Injuries, Power Rankings, Playoff Picture | table | league_news_common / power_rankings / spreadsheet | | | |
| `0x53C7F4`, `0x53C9F4`, `0x53CC1C`, `0x56060C` | Rookie Report x2, Mel's Mock Draft, Draft Recap | table | rookie_pane_* | | | |
| `0x52BDD8` | Franchise Settings | settings | options | 0x95 | 3:69E50 | 11 rows (FPF, Preseason Games, Trade Deadline, Weekly Preparation, 5 auto-steps, Edit Rookies, Sim. Quarter Length) |
| `0x508F8C` | Load Save (desk) | link | navigation | 0x11 | - | Save Franchise -> `0x507EC8`, Load / Save Playbook, Save VIP, Export Team, Delete |
| `0x507EC8` | Save Game Data | table | spreadsheet | 0x15 | 1:16BB20, 2:16B100, 4:F32C0+F32A0, 6:16DD30 | the save/load I/O state machine |
| `0x51FEE4` ... `0x52071C` | Weekly Preparation and its 13 pages | link / custom / table | weekly_prep_* | 0x15 / 0x55 | | pushed by `sub_00135790` when Weekly Preparation is on |
| `0x51C460` | (visual simulator) | table | visualsimulatormenu | 0x45 | | pushed by `sub_00135790` |

`sub_00135790` (the calendar's "play this week") uses the game's modal dialogs:
"Week %d - What do you want to do?", "Simulate ALL season games?", "Prepare for the game?"
(`sub_0014E440`, `sub_0014E540`). Its UX is worth copying for Eras. Its code is not.

---

## 7. What a copy may and may not call

### 7.1 Franchise code: do not run from Eras (verified (XBE))

| Function | Why |
|---|---|
| `sub_00142FB0` (desk msg 4) | season-stage advance `sub_002486F0` -> `sub_002480B0` / `sub_00247D40`; auto offseason steps `sub_0024BF80` (may push The Draft); `sub_00110E60(7, 1)` |
| `sub_00142DD0` (desk msg 6) | writes the coached team `[0xE3C0A0]` (`sub_0013EC90`); counts user teams in the league tables |
| `sub_00142DA0`, `sub_00142D00`, `sub_00142BB0`, `sub_00142AF0`, `sub_0034DAA0` | dereference `[0xE3C0A0]` for the asset code. Outside a franchise that pointer is not guaranteed (crash or wrong team; inferred) |
| `sub_00142A30` (desk msg 8) and binding callbacks `sub_002C0290`, `sub_002C03C0`, `sub_002C05E0`, `sub_002C06D0` | read Franchise season and team data through `[0xE3C0A0]` |
| row updates `sub_002C01C0`..`0x2C0250`, `0x2C2640`.., `0x2C26F0`.. | gate rows on the Franchise phase, meaningless for Eras |
| `sub_000C8190` (Quit) | `sub_000C0110` roster teardown, `sub_000C7570` season reset, `[0xE5FFE4] = 0` |
| `sub_00148CC0`, `sub_0010EA10`, `sub_0013EE10`, `sub_0013F1B0` | Franchise creation |
| `sub_00135790` and the calendar `0x327xxx` / `0x24Cxxx` | Franchise schedule, sims, weekly processing |
| `sub_0016E3F0`, `sub_0016DD30` | Franchise save/load (signed `Franchise1` container) |
| the desk/FO row actions `sub_00142880`.., `sub_0034D8xx`, and `sub_001427A0` / `sub_0034D7A0` | harmless code, but they write the desk's and Front Office's globals and push Franchise screens. A copy keeps its own state |

### 7.2 Engine primitives a copy can call (verified (XBE); this project already calls several)

| Purpose | Call |
|---|---|
| queue a package | `sub_00043F50(ecx = ctx wstr, edx = file wstr; stack slot, heap, 0, 0)`, `ret 0x10`; heap = `sub_00038FB0()` (`0xB04E24`). Used this way in `src/nfl2k5_teams.c` |
| wait / wait with "Loading, please wait" | `sub_000432D0()` / `sub_0014E4B0(ecx = FSM)` (completion test `sub_000432C0`) |
| unload a context | `sub_000432F0(ecx = ctx)` |
| find a resource | `sub_000449E0(ecx = ctx or 0, edx = fourcc 'SCNE' 0x454E4353 / 'TXTR' 0x52545854 / 'LAYT'; push name)`, `ret 4`, returns the object |
| scene time | `sub_0002F010(ecx = scene, push t)` set, `sub_0002F030(ecx = scene, push dt)` advance; current time at `scene + 4` |
| camera | `sub_00066B20(ecx = obj)` init; `sub_0002AD80(ecx = buf)` save current; `sub_0013E710(ecx = obj, edx = scene camera record)`; `sub_0002AC80(ecx = obj)` make current |
| draw a scene | `sub_0002DE60(ecx = scene)` |
| find a scene camera / material | `sub_00142730` / `sub_001426E0` (`esi` = scene, push name, `ret 4`), or walk `scene+0x3C/+0x40` (0x60) and `scene+0x1C/+0x20` (0x80) directly |
| 3D background slot | `sub_00069BB0(ecx = fn)` writes `[0xA84498]` (flag nibble 7) |
| input lock | `sub_0014FC40()` off, `sub_0014FC30()` on |
| list renderer mode | `sub_0014FCD0(ecx = FSM, edx = mode)`: runtime `+0xA7C`; 0 = rows drawn by the layout's nav bar, 2 = direct text (ESPN.com) |
| layout instance on/off | `sub_00143FE0(ecx = layout instance (runtime +0x5A0), edx = instance hash, push 0/1)`, `ret 4` |
| sounds | `sub_00038650(ecx = name; six zero pushes)` then `sub_00089DA0(ecx = eax, edx = 0)`; loop `sub_00165BE0(ecx = name, push 20.0, push 40.0)`, stop `sub_00165C10()` |
| text, paging title | `NATIVE-MENU.md` 1.7; `sub_0006BC30` as `sub_00142A30` calls it |
| dialogs | `sub_0014E540(ecx = FSM, edx = msg)` Yes/No -> 1 on Yes; `sub_0014E520(ecx = FSM, edx = msg)` OK; `sub_0014E440(ecx = 0xE3C040, edx = msg; stack choices {wstr, id}..., 0, FSM, 0, -1, 0)` returns the chosen id; Yes/No table `0x504324` = {"No" 3, "Yes" 2} |
| pad buttons | `sub_00070A10(ecx = pad, edx = 0)` (bits in section 9) |
| FSM | push `sub_0006E390`, pop `sub_0006E400`, pop until `sub_0006E450`, pop to root `sub_0006E480`, replace `sub_0006E2E0` |

---

## 8. Cloning the hub for Eras

### 8.1 Pattern

| | Front Office pattern (recommended) | Desk pattern |
|---|---|---|
| Flags | `0x10` (Back pops, mode 0) | `0x17` (Back, mode 7) |
| Room drawn by | own msg-7 event action (runs before the handler's background, layout and list pass, `sub_0006E4E0`) | own callback set with `sub_00069BB0` at msg 4; clear it at msg 2 |
| Shared state | none | `[0xA84498]` (only the desk sets it; the frontend reset `sub_0006B740` clears it: `xor ecx, ecx; ...; jmp sub_00069BB0`) |
| Back | flag `0x10`; optional msg-10 sound | same |

Keep the nibble exactly at 0 or 7. `0x04` or `0x08` would change the background mode
(section 9).

### 8.2 Data built at boot (all in the Eras guest heap, served functions at `0xFEE0xxxx`)

Use the screen numbering of `PHASE2-PLAN.md` M3: event actions at
`0xFEE00000 + screen*0x1000 + slot*4`, row functions at `+ 0x800 + row*0x40 + slot*4`. The
lookup goes in `recomp_lookup_manual`, as `0xFEC`/`0xFED` already do.

**Descriptor `ERAS_OFFICE` (0x2C bytes; copy `0x52533C`, then replace):**

| Off | Value |
|---|---|
| +0x00 | title buffer (UTF-16, 64 chars), e.g. "My NFL Eras" or "1978 Front Office". The handler copies the pointer on activation, so rewrite the contents before msg 3 |
| +0x04 | `ERAS_OFFICE_EVENTS` |
| +0x08 | `0x000F3E90` |
| +0x0C | 0, or a help bar such as `{9, "", "Select"}` plus an LT/RT "Change Era" entry (which help id draws which icon: **to test**, `NATIVE-MENU.md` open question 3) |
| +0x10 | `ERAS_OFFICE_ROWS` |
| +0x14 | 0 |
| +0x18 | `L"front_office"` (title + list), or `L"coach_desk"` (adds the two info panels). The LAYT must be **loaded before msg 3**, because the handler resolves `+0x18` at activation (`sub_000F37E0`). Load the package at msg 1 |
| +0x1C | 0 for `front_office`; for `coach_desk`, either 0 (static panels) or `ERAS_OFFICE_BINDING` (below) |
| +0x20, +0x24 | `0x02400044`, `0x018D0052` (copied) |
| +0x28 | `0x10` |

**Event table `ERAS_OFFICE_EVENTS`:** `{1, L1}, {2, L2}, {4, L4}, {5, L5}, {6, L6}, {7, L7},
{8, L8}, {10, L10}, {0, 0}`. Each `Lx` is one 0x24-byte record `{type 1, +4 =
0xFEE0S000 + slot*4}` followed by a zeroed record. Type 1 calls with `ecx` = FSM. The C entry
returns with `g_esp += 4`.

**Rows `ERAS_OFFICE_ROWS`:** type-9 records `{9, label, 0, ..., +0x28 = action, +0x2C =
update or 0}` plus an end row (type 3). Keep to **7 visible rows** until it is tested whether
the `nav_bar_c` panel shows 8 (the desk shows up to 8: one schedule row plus seven).

**Optional `ERAS_OFFICE_BINDING` (only with layout `coach_desk`):**
`{text list, 0, image list, 0, 0}` with entries `{0x41DCCE88 (info_a), element hash,
0xFEE0..., arg}` using the element hashes in 3.4. The callbacks write an Eras line into the
buffer at `[esp+4]` and pop 12. Examples: `0xE98010FA` -> "1978 Season  Week 3 of 16";
the four record slots -> record, next opponent, standings place. Hide `coach_desk_info_b`
with `sub_00143FE0(layout, 0x6AF19D4B, 0)` and show `_a` at msg 6, as the desk does. The
images (`team1helmet1/2`) need mini-helmet TXTRs that Eras teams do not have yet, so leave
the image list 0 at first.

### 8.3 Behaviour (C, served; the desk's and Front Office's flow with Eras state)

```
state: scene, camobj (0x2C0 bytes, 16-aligned, heap), target, ret_time, pending, era, busy

msg 1  create   camobj: sub_00066B20
                queue ctx L"ERASOFFICE" <- office package (8.5); ctx L"ERASSKIN" <- era skin package
                sub_0014E4B0(FSM)                       ; "Loading, please wait" until loaded
                target = 0; ret_time = 1.0 (entrance dolly) or 0; pending = 0
msg 4  shown    scene = sub_000449E0(L"ERASOFFICE", 'SCNE', era.room)   ; context lookup, never 0
                bind era textures and hides (8.5); sub_00165BE0(era.ambience, 20, 40)
                pending = 0; move(ret_time, 0)
msg 5  covered  scene = 0                               ; the room is not drawn under children
msg 6  frame    if scene && [scene+4] < target: sub_0014FC40(); sub_0002F030(scene, dt * (target == 1.0 ? 1 : 1.5))
                else: sub_0014FC30()
                      if pending: sub_0002F010(scene, target); p = pending; sub_0006E390(FSM, p)
                      else if LT/RT edge (sub_00070A10, 0x4000 / 0x8000) and !busy:
                          era +-= 1; unload L"ERASSKIN"; queue the next era's skin; sub_000432D0;
                          rebind; rewrite the title; sound "Ambient: Menu Move Cursor"
msg 7  draw     if scene: save camera; rec = camera record era.camera ("coach_cameraShape" / "FOcamShape");
                sub_0013E710(camobj, rec); sub_0002AC80(camobj); sub_0002DE60(scene); restore
msg 8  overlay  era strip: "< 1970s >" with sub_0006BC30 as sub_00142A30 calls it (font 3, (320, 73))
msg 10 back     sound "Ambient: Statistics Out"
msg 2  destroy  sub_00165C10(); unload L"ERASSKIN", L"ERASOFFICE"; scene = 0

move(a, b): ret_time = b; target = a; if a == 0 { time 0; return }
            sub_0002F010(scene, a - 1); if b > 0: target += 0.4

row action (ecx = FSM, edx = &item): sound "Ambient: ... In"; move(a, b); pending = Eras screen; g_esp += 4
```

**Per-highlight glances (optional, new):** the retail desk moves only on A. Because the copy
owns msg 6, it can read the selected row (FSM stack entry `+4`) and, when the cursor rests
on a row, play the first part of that row's "in" segment without locking input. The camera
would glance at the planner while Schedule is highlighted. **Inferred** to work. Tune it so
it does not fight the list's own animation.

### 8.4 Eras rows on the retail camera stops

Stops from `FRANCHISE-OFFICE-SCENE.md` 3.4 (verified (disc)). Destinations are the Eras
screens of `PHASE2-PLAN.md` M3/M4 and `NATIVE-MENU.md` 4.

| Row | Stop (a, b), `coach_desk` | Sound | Opens |
|---|---|---|---|
| Play Next Game | (13, 15) the door | Front Office In | the Eras launch path (`MATCH-LAUNCH.md`; free the office packages first, as the desk does for the Crib) |
| Schedule | (5, 7) day planner | Calendar In | Eras schedule (new custom screen) |
| Standings | (1, 3) monitor | Statistics In | AFC / NFC Standings (M3) |
| Team / Roster | (9, 11) notebook | Gameplan In | Your Team (M3) |
| League History | (17, 19) file cabinets (unused in retail) | Statistics In | later |
| Era and Settings | (1, 3) monitor | Statistics In | Association Setup (`NATIVE-MENU.md` 4.4) |
| Save / Quit to Main Menu | none | | Save; Quit = `sub_0014E540` "Leave My NFL Eras?" then `sub_0006E450(FSM, 0x515660)`, **without** `sub_000C0110` / `sub_000C7570` |

With `front_office` there are 2 stops: (3, 5) the computer and (7, 9) the contract. More
stops need code-driven keys or authored curves (`FRANCHISE-OFFICE-SCENE.md` 6.4).

### 8.5 One office per era

The user's rule is native look, using the user's own photos where they supply them, and no
crude placeholder art. Mechanisms, all from the companion notes:

* **Package (a copy, not Franchise's):** copy the `coach_desk` and `front_office` SCNE and
  LAYT chunks out of `fr.iff` into an Eras package served from pack G
  (`nfl2k5_archive_extend`), loaded under ctx `ERASOFFICE` and always looked up with that
  ctx. This is `FRANCHISE-OFFICE-SCENE.md` 6.2. It is a byte copy of 0x20-byte chunk headers
  and payloads (`iffls.py` lists them).
* **Per-era skin package** (ctx `ERASSKIN`): TXTRs named after the materials they replace
  (`wallphoto` and `office_photos` 256x256 P8 photo atlases, `teamlogo`, `carpet1`, `coach01`
  ...). Bind at msg 4 by walking the materials. `material + 0x30` = TXTR object,
  `+8 &= ~1`. Binding over a material that has an embedded texture is **to test**.
* **Era props:** hide what an era should not have (`node + 0x0C |= 0x10` per
  `FRANCHISE-OFFICE-SCENE.md` 2.3, to test), for example the monitor in a 1970s office. New
  shapes (rotary phone, CRT, flat screen) need `OFFICE-RETEXTURE.md` options E and F.
* **Room per era:** `coach_desk` for some eras, `front_office` for others (the room name and
  camera name are fields of the era record). Brand-new rooms per era are possible later: the
  engine only looks scenes up by name, so a rebuilt SCNE with its own camera curve plugs in
  by name.
* **Live switching:** LT/RT on the hub changes the era and re-skins in place, the same thing
  the desk does for user teams.

### 8.6 Children and return

* Push depth: Main Menu (0) -> Eras Office (1) -> Eras screen (2) -> ... (max 32).
* A push sends msg 5 to the office (it drops its scene pointer) and msgs 1 and 3 to the
  child. The child draws its own background (mode 1/5), so the zoom into the monitor or
  planner hides the change, as in Franchise.
* A pop sends msgs 5 and 2 to the child, then msg 3 to the office. The handler rebuilds the
  rows, reloads the layout and sends msg 4. The office finds the scene again and plays the
  return move.
* After an Eras game, the return should land on the office. That is a pop to it
  (`sub_0006E450(FSM, ERAS_OFFICE)`) where `PHASE2-PLAN.md` M4 pops to the Main Menu. **To
  test** together with the packages being freed for the match and reloaded after it (msg 1
  of a re-created office, or msg 4 if the office was kept).

### 8.7 Hazards checklist

1. **Never reuse** the desk's or Front Office's event tables, row actions, binding table or
   globals (sections 3.5 and 5). Copy only the data fields of the descriptor.
2. **Layout availability:** `front_office` and `coach_desk` exist only while their package is
   loaded. A descriptor naming a missing LAYT builds rows and accepts input but draws no
   list (the Mod Tools MyCareer mode-4 report saw exactly this with an unsatisfied `+0x18`).
   If the layout is unavailable, fall back to `navigation` with list mode 2.
3. **List mode:** rows come from `nav_bar_c` in mode 0. The desk sets 0 at msg 4; ESPN.com
   sets 2 for direct text.
4. **Context lookups:** use the Eras ctx in `sub_000449E0`. With ctx 0, the first loaded
   package with that name wins.
5. **Memory:** the desk frees its packages before the Crib. Free the Eras packages before a
   match (`MATCH-LAUNCH.md`) and in msg 2.
6. **Flags nibble:** exactly 0 (or 7 with the callback).
7. **Served functions** do not run in interpreted mod-pack code (`NATIVE-MENU.md` 2.2,
   `PHASE2-PLAN.md`): the office is `--retail` only until that is solved.
8. **Row indices** shift if rows are added (cursor restore per stack entry).
9. **The coached-team pointer** `[0xE3C0A0]` must not be read by any Eras path. Use the Eras
   league state instead.

### 8.8 Tests for later (none run)

1. Descriptor with flags `0x10` + msg-7 draw shows the room behind `front_office`'s list,
   with title and rows readable.
2. Package from pack G under ctx `ERASOFFICE`: `sub_000449E0(ctx, 'SCNE', "front_office")`
   returns the copy. Franchise afterwards still looks retail.
3. Row -> camera move -> push -> Back -> return move. The `[FSMOP]` log balances, and input
   stays locked only while the camera is moving.
4. LT/RT era switch rebinds `wallphoto` / `office_photos` live without a leak (load and
   unload 20 times).
5. `coach_desk` with `ERAS_OFFICE_BINDING`: info panel text appears.
6. 8 visible rows on `nav_bar_c`, or the list scrolls.
7. Quit leaves My NFL Eras without touching the roster (compare `0xB72918` roster tables
   before and after).

---

## 9. Corrections and additions to `NATIVE-MENU.md` (verified (XBE))

* **Descriptor size is 0x2C (44) bytes**, not 0x34. The words at `+0x2C`/`+0x30` belong to
  whatever follows. Front Office `+0x2C` = `0x525368` is ESPN.com's msg-4 action record
  `{1, 0x2C26E0}`, and Game Modes `+0x2C` = `0x5015F8` is an action list referenced from
  `0x5016B0`. This matches the Mod Tools MyCareer design ("descriptors of 44 bytes"). Copying
  0x34 bytes, as `vm_build_screen` does, is harmless.
* **Flags bits 0..3 = background mode** (section 4.1). `0x04` is not a scroll bit. On a `0x11`
  menu it turns mode 1 into mode 5, which draws and updates identically (`sub_0006B490`,
  `sub_0006B430`). The scrolling seen after `vm_add_link` comes from the list itself
  (first-visible logic `sub_002C8880`). `0x01` and `0x02` are also mode bits.
* `+0x20`/`+0x24` are int16 pairs read by the settings renderer (`sub_0027C080` reads
  `+0x24`/`+0x26`): the list frame, 68..576 by 82..397 (inferred).
* **Game button word** (`sub_00070A10`, built by `sub_000711D0`): the 12 analog inputs map
  through table `0x4E95C0` to bits `0x100 << i` (verified). If they come in XDK
  `bAnalogButtons` order (inferred), that gives A `0x100`, B `0x200`, X `0x400`, Y `0x800`,
  Black `0x1000`, White `0x2000`, **LT `0x4000`, RT `0x8000`**. The low byte holds the
  digital bits (D-pad `0x1..0x8`, START `0x10`, BACK `0x20`; inferred), and bits
  `0x1000000..0x8000000` mirror bits 0..3 (verified). The Back mask `0x220` = BACK | B and
  the desk's use of `0x4000`/`0x8000` for previous/next team under a paging title are
  consistent with this.
* `sub_0006E450` = pop until a descriptor is on top.
* List renderer mode `+0xA7C` via `sub_0014FCD0`: 0 = layout nav bar, 2 = direct text.
* Layout element and instance names hash with the archive CRC (CRC32 of upper-cased UTF-16).
* Layout binding table and text-callback ABI: section 3.4. Instance show/hide:
  `sub_00143FE0`.
* Dialogs: `sub_0014E440` / `sub_0014E520` / `sub_0014E540` (section 7.2).

---

## 10. Open questions (to test in game)

1. Whether `nav_bar_c` in `front_office` / `coach_desk` shows 8 rows or scrolls.
2. Help-bar ids for an LT/RT "Change Era" hint, and which icons they draw.
3. Whether binding a TXTR over a material with an embedded texture renders (`carpet1`,
   `coach01`), and whether node flag `0x10` hides a whole prop.
4. Exact `sub_0006BC30` arguments for the era strip (copy `sub_00142A30`'s call first).
5. Memory headroom for the office package plus an era skin package in the front end, and
   freeing both around a match.
6. Flag bit `0x80` on Franchise sub-screens.
7. Whether a re-created office after a match should replay the entrance dolly (Front Office
   style) or rest at idle (desk style). This is a design choice.

---

## 11. Scripts (`build/eras/office/`, written by this task; read-only)

* `fsm_graph.py [hex ...] [--depth N] [--json f]`: crawls descriptors (signature
  `+0x20 == 0x02400044`; 400 in the XBE). It prints fields, event tables, help bars and rows,
  and follows row targets, event pushes and descriptor immediates in row actions, row updates
  and event callbacks (one call level down). Outputs: `franchise_graph.txt` / `.json` (depth 4,
  158 screens) and `franchise_screens.txt` / `.json` (the 42 Franchise screens above).
* `xdis.py <hex> [n] [--all]`: capstone disassembly of the retail XBE with string,
  descriptor and float annotations.
* `xref.py <hex> ...`: every reference to a 32-bit value, with the enclosing generated
  function.
* `fnl.py sub_XXXXXXXX`: prints a function from `build/gen-locals`.
* `iffls.py <file.iff> | --entry <name.iff>`: lists archive resources by name CRC, with VC-LZ
  decode, `scene_tables()` (materials, nodes, cameras) and `scene_shapes()` (object ->
  materials).

Examples:

```
python build/eras/office/fsm_graph.py 522190 52533C --depth 0
python build/eras/office/xdis.py 142DD0
python build/eras/office/xref.py 522190
python build/eras/office/iffls.py --entry fr.iff
```

The other files in `build/eras/office/` (glTF exports, previews, `scripts/`,
`office_inventory.*`, `camera_keys.json` ...) belong to `FRANCHISE-OFFICE-SCENE.md` and
`OFFICE-RETEXTURE.md`.

# My NFL Eras: native menus (research and build plan)

2026-10-15. How NFL 2K5 builds its front-end menus, how this project already adds
screens to them, where the MAIN MENU is defined, and a concrete plan for the
My NFL Eras screens built the same way (user rule: inside the game's own menus,
with its own fonts and art, no separate window).

Evidence levels used below:

* **verified (XBE)**: read from `original/default.xbe` with the scripts in
  `build/eras/native-menu/` (section-mapped, no running game needed).
* **verified (code)**: read in this project's sources or in the generated code
  (`src/recomp/gen/*.c`, functions `sub_XXXXXXXX` = Xbox address).
* **inferred**: follows from reading the code but was not watched live.
* **to test**: needs a run of the game (not done here: this task does not build or launch).

Nothing below has been run in game for My NFL Eras yet.

---

## 0. Short version

* Every front-end screen is an **FSM state descriptor** (the "menu header", 0x34 bytes in
  `.rdata`). The front-end FSM object keeps a stack of up to 32 `{descriptor, selected row}`
  pairs; push `sub_0006E390`, pop `sub_0006E400`, replace-top `sub_0006E2E0`.
* A descriptor names its **handler** (+0x08), its **event table** (+0x04: per-message lists
  of actions, including "call a function" and "push state X"), its **rows** (+0x10, 0x34-byte
  rows ended by type 3), its **help bar** (+0x0C), its **layout** (+0x18, a `LAYT` resource
  name) and **flags** (+0x28).
* All behaviour hangs off function pointers in that data, and the project already serves
  such pointers from C: addresses in **0xFEC00000..** (Video Settings and friends) and
  **0xFED00000..** (Edit Player "Pads") are resolved by `recomp_lookup_manual` before the
  generated lookup. A complete new settings screen therefore needs **no generated-code
  patch** (`src/nfl2k5_video_menu.c` has none).
* **Main menu** = descriptor `0x00515660` ("Main Menu", layout `main_menu_sub`), rows at
  `0x005154C0`: Quick Game, Game Modes, The Crib (an action row), Features, Options,
  Xbox Live, Extras. Adding "My NFL Eras" is a copy of that row array with one more type-0
  (link) row, exactly like `vm_add_link` does for the Options menus. Whether the
  `main_menu_sub` screen shows an 8th row without scrolling is **to test** (section 3.4).
* Plan: hub (link menu) -> Era Select (custom "spreadsheet" state modelled on Controller
  Setup: LT/RT pages through eras, text drawn with the game's own font calls) ->
  Association Setup (settings rows like Video Settings, START advances like Franchise
  Options) -> team list (settings rows first; a drawn grid later) -> Review (link rows +
  a "Create" action row).

---

## 1. How the game's menus work

### 1.1 The front-end FSM (verified, code)

| Function | Does |
|---|---|
| `sub_0006E390` (ecx = FSM, edx = descriptor) | **push**: message 5 to the old top, depth++, store descriptor, message 1 to the new top, then message 3 if 1 returned nonzero. Refuses at depth >= 0x20. |
| `sub_0006E400` (ecx = FSM) | **pop**: message 5, message 2 (if 5 returned nonzero), depth--, message 3 to the new top. |
| `sub_0006E2E0` (ecx, edx) | **replace top**: 5, 2 to the old, store the new, 1, then 3. |
| `sub_0006E340` (ecx, edx) | insert a state *under* the top. |
| `sub_0006E480` (ecx) | pop to the root (index 0). |
| `sub_0006E4E0` (ecx = FSM, edx = 0, push msg) | **send a message**: runs the event-table actions of the top state for that message, then calls the state's handler (+0x08) with ecx = FSM, edx = message. |
| `sub_0006E630` (ecx) | `&stack[depth]` (8 bytes: descriptor, selected row index). |
| `sub_0006E2D0` (ecx) | the FSM's menu runtime object, `[FSM + 0x10C]`. |

FSM object: stack of 8-byte entries at `FSM + 8*i`, depth at `+0x100`, dt float at
`+0x104`, "changed" at `+0x108`, runtime menu object at `+0x10C`. The front-end FSM is
`0x00A84B18` (`PROJECT_STATUS.md` line 2134). The Edit Player page manager uses the same
layout (`nfl2k5_oldschool_camera_row` reads `manager + 0x100` and `manager + depth*8`).

Each stack entry keeps its own **selected row** (+4), so returning from a submenu
restores the cursor by index (`sub_0014FC50` writes it). Inserting a row in a menu shifts
the indices of the rows after it.

### 1.2 The state descriptor ("menu header", 0x34 bytes)

Fields, from the dumps in section 3 and the code that reads them:

| Off | Meaning | Evidence |
|---|---|---|
| +0x00 | title, UTF-16 (copied to runtime +0x598 on activate) | `sub_000F3D60` |
| +0x04 | **event table**: `{u32 msg, u32 action_list}` pairs, ended by msg 0 | `sub_0006E4E0` |
| +0x08 | **handler** `fn(ecx = FSM, edx = msg)`, returns eax = 1 | `sub_0006E4E0` loc_0006E58C |
| +0x0C | **help bar**: 12-byte entries `{u32 id, wstr alt, wstr text}`, ended by id 0 | `sub_000F3CD0`, `sub_000F35A0/35F0` |
| +0x10 | **rows** (0x34 bytes each, ended by type 3) | `sub_0014FF80`, `sub_0027C4D0` |
| +0x14 | table definition for table screens: `{root cell, x0, x1, y0, y1}` (floats) | Edit Player `0x56F208` -> `{0x569F90, 28, 357, 77, 396}` |
| +0x18 | layout name, UTF-16, a `LAYT` resource (`navigation`, `options`, `spreadsheet`, `main_menu_sub`, `create_a_player`) | `sub_000F37E0` loads it with type `'LAYT'` (0x5459414C) |
| +0x1C | layout binding table for that layout (in `.data`) | `sub_000F37E0` -> `sub_00143EA0` |
| +0x20, +0x24 | `0x02400044`, `0x018D0052` in every menu seen | meaning unknown |
| +0x28 | flags (below) | `sub_000F3970`, `sub_00150020` |
| +0x2C, +0x30 | vary (main menu: floats -99.0, 0.88; Game Modes: 1, 0x148B40) | meaning unknown; copy them from the template |

Flags (+0x28), as far as the code shows:

* `0x10`: B/Back (button mask 0x220) sends message 10, then pops the screen unless the
  message handler set runtime `+0xA84` (`sub_000F3970` loc_000F3A4A). Every submenu has
  it; the main menu (flags `0x02`) does not, so Back does nothing there.
* `0x40`: START (bit 0x10) sends message 11 ("advance") and plays "Ambient: Menu
  Select" instead of acting like A (`sub_000F3970` loc_000F3AA5; `sub_00150020` builds the
  accept mask as `((~flags & 0x40) | 0x400) >> 2`). Franchise/Tournament/Situation
  settings use `0x55` with help bar "|START| to Advance".
* `0x04`: the list scrolls past the visible rows (this project's observation, set by
  `vm_add_link`: `PROJECT_STATUS.md` line 2237).
* `0x01`, `0x02`, `0x20`: not identified.

Handlers seen (all verified, XBE + code):

| Handler | Kind | Used by |
|---|---|---|
| `0x000F3E90` | **link list** (rows type 0/9; A pushes) | Main Menu, Game Modes, Features, Options, Extras |
| `0x000F3FC0` | **settings list** (value rows 5/7) via `sub_0027C0xx` | Game Options, Franchise/Tournament/Situation Options, Video Settings |
| `0x000F3DA0` | **custom screen**: no rows; all behaviour in event-table actions | Controller Setup, Team Select, Customize League, Xbox Live |
| `0x000F40F0` | **table screen** (cells, header +0x14) | Edit Player pages, Historic Teams "Team Slots" |

### 1.3 Messages (verified unless marked)

| Msg | When | Notes |
|---|---|---|
| 1 | state created (push/replace) | |
| 2 | state destroyed (pop/replace) | |
| 3 | state activated (pushed, or uncovered by a pop) | generic handlers rebuild the row items, load the layout and help bar here (`sub_000F3CD0`, `sub_0014FF80`, `sub_0027C4D0`) |
| 4 | sent by the handler at the end of activation (`sub_000F3D60`) | "screen shown": Controller Setup inits here (`0x3573A0`) |
| 5 | deactivated (covered or leaving) | Game Options saves here (`0x10EE70`) |
| 6 | every frame | generic per-frame input `sub_000F3970` turns buttons into messages 10-18 for the top state; link lists handle A in `sub_00150020` |
| 7 | draw (layout) | `sub_000F2F70` draws the LAYT layout |
| 8 | draw overlay (2D) | `sub_000F26F0` draws the help bar; Controller Setup draws its rows here (`0x357530`); the main menu draws a text line here (`0x24D490`) |
| 10 | Back (B / Back, mask 0x220), if flag 0x10 | then pop |
| 11 | START, if flag 0x40 | "advance" |
| 12, 13 | two more buttons (bits 0x100 / 0x800 of `sub_000F3780`) | inferred: the X/Y-type actions; help ids 7 and 8 label them (Controller Setup, Team Slots) |
| 14, 15 | page next / previous | Controller Setup binds its LT/RT layout paging here (`0x357400` / `0x357450`, gen patches CONTROLLER_SETUP_NEXT/PREV). Exact bits depend on runtime `+0x650`; when runtime `+0x64C` is set the same input moves the cursor instead (**to test**) |
| 18 | bit 0x80 | not identified |
| 19-22 | row refresh (`sub_0014FC60`: re-runs every row's +0x2C callback) | link lists |
| 27, 28 | `sub_000F3700(1/0)` | not identified |

### 1.4 Event tables and actions (verified, code `sub_0006E4E0`, jump table `0x6E5F8`)

An action list is an array of 0x24-byte records ended by type 0:

| Type | Action |
|---|---|
| 1 | call `[+4]` with ecx = FSM |
| 2 | call `[+8]` with ecx = FSM, edx = arg |
| 3 | call `[+0xC]` (no arguments) |
| 4 | call `[+0x10]` with ecx = `[+0x1C]` |
| 5 | call `[+0x14]` with the frame dt float pushed |
| 6 | call `[+0x18]` |
| 7 | **pop** (`sub_0006E400`) |
| 8 | insert `[+0x20]` under the top |
| 9 | **replace top** with `[+0x20]` |
| 10 | **push** `[+0x20]` |
| 11 | pop to root |

Actions after a push/pop/replace stop the list (the top state changed). So "START pushes
the next screen" can be pure data: `{msg 11 -> [{type 10, ..., +0x20 = next descriptor}]}`.
Example in the game: Franchise Options (`0x500DC8`) event table `0x500AB0`: msg 11 -> type 1
`sub_00148CC0` (validates, then pushes `0x52ABE0` "Customize League").

### 1.5 Rows (0x34 bytes; verified, XBE + code)

| Off | Meaning |
|---|---|
| +0x00 | type: **0 link**, 1 replace-link, **5 Off/On**, **7 value list**, **9 action**, **3 end** |
| +0x04 | label, UTF-16 (may contain button/trademark tokens such as `|TM|`) |
| +0x08 | type 0/1: the descriptor to push / replace with |
| +0x0C..+0x24 | type 5/7: seven functions **max, min, get, next, prev, value string, widest string** |
| +0x28 | type 9: action `fn(ecx = FSM, edx = &item)` |
| +0x2C | optional per-row update `fn(ecx = FSM, edx = &item)`: sets item `+8` = 1 to **hide** the row, item `+0xC` = 1 to **grey it out** |
| +0x30 | 0 in every row seen |

What A does (`sub_00150020`, row-type switch at `0x15024C`/`0x15023C`): type 0 ->
`sub_0006E390(FSM, row+8)`; type 1 -> `sub_0006E2E0(FSM, row+8)`; types 2..8 -> nothing
(left/right are handled by the settings engine); type 9 -> `call [row+0x28]`. The game's own
type-9 actions (The Crib `0x24D440`, Celebrations `0x2C2930`) call `sub_001423B0(0)` and, if
it does not return negative, push their screen (`0x524FA0`, `0x5755A4`).
Examples of +0x2C: Options "Online Options" `0x14CBA0` hides itself; Celebrations
`0x2C2950` hides itself when `sub_000627C0()` is nonzero; Game Options rows grey out.

Value-row function conventions (verified, `src/nfl2k5_video_menu.c` lines 3-14 and
219-269): slots 0-5 take no stack arguments and return in eax (max index, min index,
current index, next -> 1, prev -> 1, pointer to the current value string). Slot 6 gets the
font in ecx and must return the widest value string's width; the project does it by calling
`sub_000771A0` (which pops its two pushed arguments) on a table of all value strings.
Value strings are fetched every frame, so lists may change while the screen is open.

### 1.6 The runtime menu object (`[FSM + 0x10C]`, verified, code)

`+0x00` animation float, `+0x04` its rate (1.0), `+0x08` item count, `+0x0C` timer,
**`+0x10` items[32]** of 0x2C bytes (item `+4` row pointer, `+8` hidden, `+0xC` greyed,
`+0x28` label pointer), `+0x590` port filter callback, `+0x594` active port, `+0x598`
title, `+0x5A0` layout instance, `+0x5AC + 4*id` help text, `+0x5F8 + 4*id` help alt
text, `+0xA7C` list mode, `+0xA84` "veto the pop after Back".

**Hard limit: 32 rows per list.** Both builders stop at 0x20 (`sub_0014FF80` link lists,
`sub_0027C4D0` settings lists) and the item array ends exactly at `+0x590`.

### 1.7 Layouts, visible rows, drawing (verified unless marked)

* The screen art is the `LAYT` resource named at +0x18, loaded by `sub_000F37E0` together
  with the shared title bars `title_bar_short`, `title_bar_sub`, `title_bar_wide`,
  `title_bar_short_sub`. Link lists also load `nav_menu_a`, `nav_bar_b`, `nav_bar_c`,
  `main_navi` (`sub_002C8810`, names at `0xAD0148`).
* Link-list row text is bound to layout text elements through the table at `0xAD0018`:
  18 entries `{0xAAE5C209, name hash, 0x2C8730, slot}` with slots +1..+9 and -1..-9. The
  callback at `0x2C8730` records the highest slot the layout asks for in `0xC8E1D0`
  (**visible rows**) and draws item `first + slot - 1` (first visible = `0xC8E1CC`, kept
  around the cursor by `sub_002C8880`), greyed items in `0xFF606060`. So a layout can show
  **at most 9 rows** at once, and shows as many as it has row text elements. The
  `navigation` screens show 7 (project observation).
* 2D text uses the game's own font calls. The cleanest example is the main menu's own
  overlay `sub_0024D490`:

  ```
  sub_00046920(ecx = ctx)                  ; init a 0x80-byte text context
  sub_00046B50(ecx = ctx, edx = 0xFF101010) ; colour ARGB
  sub_000EF850(ecx = 3) -> font             ; game font n (2, 3, 4, 5, 6 seen)
  sub_000469B0(ecx = ctx, edx = font)
  sub_00046A00(ecx = ctx, edx = 1)          ; alignment (1, 2, 3 seen)
  push z, push y, push x; sub_00046A70(ecx = ctx)   ; position, callee pops (inferred)
  sub_000F1C70(ecx = ctx, edx = wstr)       ; draw
  ```

  Controller Setup's row drawing (`sub_00357530`) adds `sub_00046AC0(ecx, edx = &float2)`
  (position from a vector), `sub_00046A10` (second alignment), a backing panel
  `sub_0006BBE0(ecx = 0x80000000, 5 floats)` and a centred page title with the paging
  icons `|RANALOGLEFT|` / `|RANALOGRIGHT|` drawn by `sub_0006BC30(ecx = wstr, edx = 1, ...)`
  (argument meaning inferred). UI sounds: `sub_0014D390(ecx = L"Ambient: Menu Select"`
  `0xE7FD70` / `L"Ambient: Menu Move Cursor"` `0xE7FD3C`).

---

## 2. How this project already adds native menu content

### 2.1 Data-only screens and links: `src/nfl2k5_video_menu.c` (verified, code)

* Installed once at boot from `src/main.c` line 3806, before the recompiled entry point
  runs, into a 512 KB guest heap (`xbox_HeapAlloc`).
* `vm_build_screen` (lines 281-310): rows of type 5/7, label strings, and seven function
  pointers per row set to `0xFEC00000 + screen*0x1000 + row*0x40 + slot*4`; the end row
  and the header are **copies of Game Options** (`0x501E48`: handler `0xF3FC0`, layout
  `options`, flags 0x15), then title and rows are replaced.
* `nfl2k5_video_menu_lookup` (lines 271-279) accepts only valid screen/row/slot
  combinations, remembers the called address in a TLS variable and returns one function
  `vm_row_fn` (lines 219-269) that decodes it. Plain `ret`: it ends with `g_esp += 4`.
* `vm_add_link` (lines 313-335): copies a link menu's rows, appends `{type 0, label,
  descriptor}` before the end row, repoints header `+0x10` and sets flag 0x4.
  Used on the four Options menus (`0x503288` front end, `0x503458`, `0x503628`, `0x5038C8`
  pause), Coach Match Up (`0x585474`, "Presentation") and Extras ("Mods").
* `vm_link_target` / `vm_remove_links` (lines 363-398, uncommitted): find a submenu by its
  label in the main menu (Extras) and drop rows. Note: the "ESPN" prefix test only applies
  to type-0 rows, and Extras' "ESPN VIDEOGAMES" is a type-9 row, so it stays.
* No gen patch. Side effect worth knowing: the clones keep Game Options' event table
  `0x501C28` (msg 1 sets the options-dirty flag `0xAA26B0`, msg 5 runs `sub_0010EE70`).

### 2.2 Function pointers served from C: `recomp_lookup_manual` (verified, code)

`src/recomp_manual.c` lines 2584-2593: `0xFEC0xxxx` -> `nfl2k5_video_menu_lookup`,
`0xFED0xxxx` -> `nfl2k5_oldschool_lookup`, before any other lookup. The indirect-call
macros (`src/recomp/gen/recomp_types.h` lines 1040-1090) try `recomp_lookup_manual`
first, and `RECOMP_ICALL_IS_CODE` (lines 121-123) lets every address >= 0xFE000000
through. Kernel thunks live at `0xFE000000..0xFE0005E7`
(`external/xboxrecomp/src/kernel/kernel_bridge.c` lines 430-431, 9832-9842).

Hazards found:

* **Mod-pack interpreter**: `src/interp/x86_interp.c` lines 339-346 send every target
  >= 0xFE000000 to `recomp_lookup_kernel` only. A menu function that runs interpreted
  (changed by a pack) cannot reach `0xFEC/0xFED/0xFEE` functions. Same exposure as the
  current Video Settings rows; **to test** in a pack build, or make `native_for` try
  `recomp_lookup_manual` for these ranges.
* The ranges are Xbox device windows: `0xFEC00000` is the AC97 block (gen patch
  AC97_RESET_ACK writes `0xFEC0010B`), and generated code writes `0xFEF00000..`
  (`recomp_0031.c` line 68065). Function addresses are never dereferenced, so this is
  harmless, but **use `0xFEE00000..` for My NFL Eras** (no use found in `src/`).

### 2.3 A page added inside an existing screen: `src/nfl2k5_controller_setup.c` (verified, code)

Controller Setup (`0x52C6FC`, handler `0xF3DA0`, layout `spreadsheet`, flags 0x15) is a
custom screen: its event table `0x52C680` binds msg 4 `0x3573A0` (init), 5 `0x3573F0`,
6 `0x357500` (frame), 7 `0x6C080`, 8 `0x357530` (draw rows), 10 `0x3574E0` (back),
12 `0x3574A0`, 13 `0x3574C0`, 14 `0x357400` (next layout), 15 `0x357450` (previous).
The project adds a fourth layout with gen patches on those functions
(`tools/apply-gen-patches.py` lines 683-704: CONTROLLER_SETUP_NEXT/PREV/LABELS/NAME/
PORT_LAYOUT/APPLY/ROW_COLOR), plus:

* help text: the screen keeps the help-bar string pointers it read on activation, so the
  pointers in the help entries (`0x52C6E0`, `0x52C6EC`) are aimed at boot-allocated buffers
  once, and only their contents change (`help_update`, lines 261-294);
* input: `nfl2k5_cs_input` is called from the input HLE (`src/nfl2k5_input_hle.c` line 298)
  with the host pad state before the game sees it, and can swallow it (lines 311-386);
* art: the controller picture is replaced through the renderer's texture override
  (`src/nfl2k5_controller_art.cpp`), by content hash.

### 2.4 A row added to a table page: Edit Player "Pads" (`src/nfl2k5_oldschool.c` lines 584-747, verified)

Edit Player pages are table screens (handler `0xF40F0`, layout `create_a_player`,
header +0x14 -> table definition). A page's rows are a null-terminated list of pointers to
**0xB0-byte row descriptors** at `owner + 0x9C` (equipment owners `0x567468`,
`0x5676B0`, 16 rows each, Helmet `0x562E40`). Descriptor: three 0x18-byte function slots
(value-name `+0x08`, next `+0x20`, previous `+0x38`; `+0x04` = 3), column width float
`+0x50` (155.0), name pointer twice (`+0x70`, `+0x88`, each after `{fn 0x27CCD0, 0x10000}`).
`nfl2k5_oldschool_table_build` (gen patch OLDSCHOOL_ROWS at the top of `sub_00174140`, which
counts `owner + 0x9C` into table `+0xA0` and allocates the cell array) swaps the owner for a
clone with one more descriptor whose functions are `0xFED00000/4/8`.

**The fixed-array trap**: the preview camera is looked up per row in fixed arrays
(`0x5559E0`, `0x5559F4`, `0x555A08`, `0x555A20`, `0x555A60`, `0x555AA0`; camera records
0x30 bytes at `0x555D30`), so row 16 read past them. OLDSCHOOL_CAMERA (`sub_003433C0`)
redirects the index to a slot holding a new camera id. Any new row in a game list must be
checked for such per-row side tables.

### 2.5 Two more extension patterns (verified, code)

* **Extending a fixed native list**: Celebrations (37 retail entries) gets 4 more by gen
  patches at every lookup of the label table `0x5858D4 + i*8` and selector rows
  `0x50CFC8 + i*12` (CELEBRATION_*, `tools/apply-gen-patches.py` lines 92-131).
* **Stepping a native chooser through more data**: Team Select's next/previous/random
  calls are wrapped (TEAM_SELECT_*, lines 706-723) so it steps through historic and added
  teams (`nfl2k5_ts_step`, `src/nfl2k5_teams.c` line 412).

---

## 3. The MAIN MENU

### 3.1 Descriptor `0x00515660` (verified, XBE)

| Field | Value |
|---|---|
| title | `0xE8B1CC` "Main Menu" |
| +0x04 events | `0x515490` |
| +0x08 handler | `0x000F3E90` (link list) |
| +0x0C help | 0 |
| +0x10 rows | `0x005154C0` (7 rows + end; the descriptor follows the end row) |
| +0x18 layout | `0xE8B1E0` "main_menu_sub" |
| +0x1C binding | `0xAC60D0` (one entry: `{0x6AFC8790, 0x24D460}`, not the nav text slots) |
| +0x28 flags | `0x02` (no Back, no scrolling) |
| +0x2C, +0x30 | floats -99.0, 0.88 |

Event table `0x515490`: msg 3 -> `sub_000F2F00`; msg 4 -> `sub_00327A50`; msg 6 ->
`sub_00327A90`, `sub_00192090`; msg 7 -> `sub_000F2F00`; msg 8 -> `sub_0024D490` (draws one
string returned by `sub_001280B0` at (350, 66) in font 3; what string is unverified).

### 3.2 Rows (verified, XBE)

| # | Row | Type | Label | Target |
|---|---|---|---|---|
| 0 | `0x5154C0` | 0 | Quick Game | `0x52728C` "Team Select" (custom, `spreadsheet`, flags 0x65) |
| 1 | `0x5154F4` | 0 | Game Modes | `0x5015CC` Game Modes: Franchise `0x500DC8`, First Person Football `0x526948`, ESPN 25th Anniversary `0x529AE0`, Practice `0x529344`, Situation `0x501298`, Tournament `0x501434` |
| 2 | `0x515528` | 9 | The Crib\|TM\| | action `0x24D440` -> pushes `0x524FA0` |
| 3 | `0x51555C` | 0 | Features | `0x525830`: VIP, Roster Manager, Historic Teams (`0x5726EC` "Team Slots"), Team Create, Player Create, Celebrations (type 9), Stadium Music |
| 4 | `0x515590` | 0 | Options | `0x503288`: Game Options `0x501E48`, Difficulty, Presentation, Weather, Penalties, Controller Setup `0x52C6FC`, Load / Save, Online Options, Audio Test |
| 5 | `0x5155C4` | 0 | Xbox Live | `0x525FCC` "XboxLive Sign-in" (custom) |
| 6 | `0x5155F8` | 0 | Extras | `0x5408A8`: Offensive/Defensive Training, ESPN VIDEOGAMES (type 9), Game Manual, Reference Guide, Game Registration, Credits |
| 7 | `0x51562C` | 3 | (end) | |

Reproduce: `python build/eras/native-menu/dump_menu.py 515660 1`.

### 3.3 How the main menu is reached (verified, code)

The attract state `0x4F6870` pushes `0x515660` on START (`sub_000F5AB0`; also
`PROJECT_STATUS.md` line 2135), and `0x515660` is loaded by code in about ten places
(`grep 515660 src/recomp/gen`), for example the returns to the main menu. All of them use
the same descriptor, so repointing its `+0x10` once at boot changes the menu everywhere.
The item list is rebuilt from `+0x10` on every activation (`sub_0014FF80`), so a boot-time
change is enough.

### 3.4 Adding "My NFL Eras"

Mechanism (same as `vm_add_link`, but inserting at a chosen index):

```c
/* new: insert a link row at index `at` of a link menu (copy, insert, repoint) */
static int eras_insert_link(uint32_t menu, int at, uint32_t target, uint32_t label)
{
    uint32_t old = MEM32(menu + 0x10), copy, row;
    int n = 0;
    while (n < 32 && MEM32(old + 0x34u * n) != 3) n++;
    if (n >= 31 || at > n) return 0;                 /* the runtime holds 32 items */
    copy = eras_alloc(0x34u * (n + 2));
    vm_copy(copy, old, 0x34u * at);
    row = copy + 0x34u * at;                          /* zeroed 0x34 bytes */
    MEM32(row + 0x00) = 0;  MEM32(row + 0x04) = label;  MEM32(row + 0x08) = target;
    vm_copy(row + 0x34, old + 0x34u * at, 0x34u * (n - at + 1));   /* the rest + end row */
    MEM32(menu + 0x10) = copy;
    return n + 1;
}
/* eras_insert_link(0x515660, 2, eras_hub, wstr("My NFL Eras")); -> after Game Modes */
```

The risk is only visual: the main menu is drawn by `main_menu_sub` plus the nav layouts,
and an 8th row needs an 8th row text element (the binding allows 9, section 1.7). Options,
in order:

1. **Insert as the 8th row** (after Game Modes or before Extras) and look. If it is clipped,
   try header flag `0x04` (scrolling, as on the Options menus). **To test.**
2. If the main menu cannot show 8, **take the "Xbox Live" slot**: same row, label
   "My NFL Eras", target the hub. The menu stays at 7 rows. The original Xbox Live service
   is not available to this PC build, so the row leads to a sign-in screen with no service
   (whether that screen does anything useful offline was not checked). Precedent: Extras
   already loses "Game Registration". This replaces a retail entry, so it is the user's call.
3. Fallback that needs no test: a link in **Game Modes** (6 rows -> 7, fits the
   `navigation` layout as the Extras change does). Not a main-menu entry, so only if 1 and 2
   are rejected.

Test recipe (when a build is allowed): launch with `--retail` (user rule), press START at
attract, screenshot the main menu (`tools/drive-menu.ps1`); `NFL2K5_MEMDUMP=515660:34`
writes the descriptor to `logs\memdump_00515660.bin` to confirm the repoint.

---

## 4. Build plan

### 4.1 Module and plumbing

* New `src/nfl2k5_eras_menu.c`, installed right after `nfl2k5_video_menu_install()`
  (`src/main.c` line 3807), with its own guest heap (`xbox_HeapAlloc`, ~256 KB).
* Function range **`0xFEE00000`**: `0xFEE00000 + screen*0x1000 + 0x000..0x7FF` for
  event actions (`slot*4`), `+ 0x800 + row*0x40 + slot*4` for row functions.
  `nfl2k5_eras_menu_lookup()` added to `recomp_lookup_manual` next to the two existing
  ranges; one dispatcher per kind, TLS "called address" as in `vm_row_fn`.
* Every guest-visible structure (descriptors, rows, event tables, action lists, help bars,
  UTF-16 strings) lives in that heap. Descriptors are copies of a retail template with
  title, `+0x04`, `+0x0C`, `+0x10`, `+0x28` replaced. **Always replace `+0x04`** (the
  template's event table runs the template screen's own code, see 2.1).
* Calling game code from C (text, sounds, push): set `g_ecx`/`g_edx`, push stack
  arguments, push a fake return address, call `sub_XXXXXXXX()` directly, as `vm_row_fn`
  does for `sub_000771A0`. Our own entry points end with `g_esp += 4` (plain `ret`).
* League data comes from the PC side (`docs/eras/PHASE1-ARCHITECTURE.md` section 4):
  presets and teams from `data/eras/`, per-save state under the per-user folder
  (`xbox_user_path`, as `controller_layouts.ini` does).

### 4.2 Hub: "My NFL Eras" (link list, data only)

Template: **Features `0x525830`** (handler `0xF3E90`, layout `navigation`, binding
`0xAA281C`, flags 0x11, no event table, no help bar: the cleanest link menu).

| Row | Type | Does |
|---|---|---|
| New Association | 0 | push Era Select |
| Continue Association | 9 | `0xFEE0xxxx`: load the save, push the association screen; +0x2C hides the row when there is no save |
| Association Settings | 0 | push Association Setup (edit mode) |
| (later) History / Records | 0 | |

Keep it at 7 rows or fewer (no scrolling needed).

### 4.3 Era Select (custom screen, LT/RT carousel + description panel)

Template: **Controller Setup `0x52C6FC`** (handler `0xF3DA0`, layout `spreadsheet`): the
game's own "page through choices with LT/RT, draw text in the body" screen.

* Descriptor: title "My NFL Eras", flags **0x55** (Back pops, START advances), help bar
  `{9, "", "to Select"}` plus `{7, "", "for Era Details"}` if X is used. Ids 7/8/9 are the
  ones the game uses with msgs 12/13/11 (button icons per id: **to test**).
* Event table (all type-1 actions at `0xFEE0xxxx`):

  | Msg | Action |
  |---|---|
  | 4 | enter: load presets, keep the last choice |
  | 14 / 15 | next / previous era, sound "Ambient: Menu Move Cursor" |
  | 8 | draw: the strip of era names across the top (centred title with the paging icons via `sub_0006BC30`, neighbours in grey `0xFFC0C0C0`, the current one in `0xFFC0C000` as Controller Setup does), then the description panel: wrapped lines with the text calls of 1.7 (font 2 or 5), and a few facts lines (season span, team count, rule set) from the preset data |
  | 11 | choose: store the preset, push Association Setup (or a type-10 action after the type-1 store) |
  | 10 | nothing special: the pop happens by flag 0x10 |

* D-pad left/right as well as LT/RT: either confirm which bits produce msgs 14/15 (1.3), or
  read the pad in the input HLE as `nfl2k5_cs_input` does.
* Photos (user rule: the user's photos, not drawn art): a picture per era through the
  texture override, as the controller art replaces the Duke picture. Which texture slot can
  carry it on this screen is open (Controller Setup draws its picture with `sub_00069440`
  from the sprite at `0xAEA010`). Phase 2.
* Text sources: era names and descriptions come from the preset data with their source
  URLs (no historical text is hard-coded in C).

Minimal first version (data only, no drawing code): a settings screen with one type-7
row "Era  < name >" and the description shown through the help bar text buffer (the
Controller Setup buffer trick) or as read-only rows. Useful to get the flow working first.

### 4.4 Association Setup (settings list, like Video Settings)

Template: **Franchise Options `0x500DC8`** (handler `0xF3FC0`, layout `options`, flags 0x55,
help bar `0x500AD8` "|START| to Advance"), with **our own event table**: msg 11 -> type 1
"validate and store" then type 10 push Team list. (Game Options `0x501E48` is the template
`vm_build_screen` uses, flags 0x15, no START advance.)

* Rows: type 7 / 5 with the seven functions at `0xFEE0x800+`, exactly `VmScreen` semantics
  (max, min, get, next, prev, value string, widest). Defaults from the chosen preset.
  Candidates: Start Season, Seasons to Play, Quarter Length, Rules (preset / modern),
  Salary Cap, Draft, Injuries, Trades, User Teams. Each row needs a real consumer in the
  league code or game hooks before it ships (PHASE1 section 4: a data field alone does not
  count).
* Limits: 32 rows (game); `VmScreen` itself stops at 16 rows and 64 values per row;
  value strings 64 characters. Over 7 rows needs flag 0x4 (Game Options has 9 rows with
  0x15). Rows can be hidden/greyed per preset through +0x2C.

### 4.5 Team selection

Phase A (data only, works today): a settings list with one type-7 row per franchise of the
chosen season, label = team name from the league data, values "CPU / User / Out" (or
"User / CPU"). The 1970-1980 seasons in `data/eras/league/nfl_seasons_1970_1980.json`
have 26 or 28 clubs (`team_count`), under the 32-row limit; needs flag 0x4.
START advances to Review. A 32-club season fills the list exactly (32 items, no room for
any extra row), so for those seasons split the list by conference or go to Phase B.

Phase B (native grid): a custom screen like Era Select: a 4-column grid of team names
(and abbreviations) drawn with the text calls, cursor moved by the d-pad (input HLE or
msgs), A toggles user control, START advances. Team logos and helmets: the game draws them
on Team Select (`0x52728C`, code around `0x2C1000..0x2C2400`, art found through the name
CRC hooks ASSET_HASH_HOOK_CS / CACHE_EXTRA_*), so drawing them here means finding that
drawing call (open). Reference screens that already pick teams: Franchise "Customize
League" `0x52ABE0` (custom, `spreadsheet`, code `0x3550D0..0x355E30`, works on the retail
league's 32 clubs) and Historic Teams "Team Slots" `0x5726EC` (table screen, X = Add/Edit,
Y = Delete).

Playing a game uses the native Team Select / match flow with the two franchises loaded
into USER1/USER2 (PHASE1 section 2), not a new screen.

### 4.6 Review / Confirm

A link list (template Features) built on activation:

| Row | Type | Does |
|---|---|---|
| "Era: <preset>" | 0 | back to Era Select (replace or pop to it) |
| "Settings: <summary>" | 0 | Association Setup |
| "Teams: <n> clubs, <k> user" | 0 | team list |
| Create Association | 9 | `0xFEE0xxxx`: write the save, then replace the stack top with the association screen (`sub_0006E2E0`) |

Labels are UTF-16 buffers allocated once (64 chars) and rewritten by a msg-3/4 event action,
because items copy the label **pointer** on activation. Long labels may be clipped by the
layout (**to test**; "First Person Football|TM|" fits).

---

## 5. Limits and hazards

| Limit | Value | Where |
|---|---|---|
| Rows per list | 32 | `sub_0014FF80`, `sub_0027C4D0` (`cmp 0x20`), items end at runtime `+0x590` |
| Visible link rows | <= 9 (text slots), 7 on `navigation` | `0xAD0018`, project observation |
| FSM depth | 32 | `sub_0006E390` (`cmp 0x20`) |
| Help bar ids | slots `+0x5AC + 4*id` up to `+0x5F8` (19 ids) | runtime object |
| `VmScreen` | 16 rows, 64 values, 64-char strings, 512 KB heap | `src/nfl2k5_video_menu.c` lines 45-49, 444 |
| Per-row side tables | e.g. Edit Player cameras | section 2.4 |
| Interpreted menu code | cannot call `0xFE[C-E]` functions | section 2.2 |
| Template event tables | run the template's code | section 2.1 |
| Row indices shift | remembered cursor per stack entry | section 1.1 |

---

## 6. Open questions (to test in game)

1. Does `main_menu_sub` show an 8th row (with or without flag 0x4)?
2. Which buttons produce messages 12-15 on a `spreadsheet` screen, and what runtime
   `+0x64C` / `+0x650` select?
3. Which button icon goes with help ids 7, 8, 9?
4. Exact arguments of `sub_0006BC30` (paging title) and `sub_00046A70` (position).
5. Header flags 0x01 / 0x02 / 0x20 and fields +0x20..+0x30.
6. A texture slot on a `spreadsheet` screen that the override can fill with an era photo.
7. Whether any pack build interprets the menu code (section 2.2).

## 7. Scripts used (in `build/eras/native-menu/`)

* `xbe.py`: maps a guest address to `original/default.xbe` bytes (sections, u32, UTF-16).
* `dump_menu.py <hex> [depth]`: a descriptor and its rows, following links.
* `evt.py e:<hex> h:<hex>`: an event table (with action types) or a help bar.
* `fn.py sub_XXXXXXXX ...`: a generated function from `src/recomp/gen`, compacted.

Example: `python build/eras/native-menu/evt.py e:52C680 h:52C6D8` (Controller Setup).

# Franchise office scene: resources, code, camera, and how an Eras copy reuses it

2026-10-15. Read-only research for "My NFL Eras". The user wants an Eras hub that looks like
Franchise mode's office, built as a **copy** (not the Franchise screens themselves), with a
different office for each era from the 1970s to 2025. This note covers the 3D scene: where it
lives on the disc, which code loads it, binds its team art, draws it and moves the camera
for each menu item, what is in the room, and how a copy can load it with era textures.

Nothing was built, launched or edited in `src/`, `tools/`, `external/`, `config/` or `saves/`.
Retexturing options (texture-pack folders gated by context, salted clones, prop morphs, mesh
injection) are covered in the companion note **`OFFICE-RETEXTURE.md`**, written by a parallel
task. This note focuses on the scene, the menu and the camera, and refers to it for
texture-pack details.

Evidence levels:

* **verified (XBE)**: read from `original/disc/default.xbe` (disassembled, section-mapped).
* **verified (disc)**: decoded from `original/disc/vc_53450030/` with 2K Football Mod Tools'
  readers (`nfl_outer`, `nfl_scene_probe`, `nfl_scne_inventory`, `nfl_txtr`, `nfl2k5_models`).
* **verified (code)**: read in this project's sources or in `build/gen-locals/*.c`.
* **inferred**: follows from the above, not checked.
* **to test**: needs a game run (none was done).

---

## 0. Short version

* The Franchise hub is the **Coach's Desk** screen, descriptor `0x00522190` (title "Coach's
  Desk", layout `coach_desk`). It is a **real 3D room**: SCNE `coach_desk` in `fr.iff`
  (archive outer 23). The camera is a camera object stored in the scene (`coach_cameraShape`),
  animated by keyframe curves that are **also stored in the scene**. Each menu item moves the
  camera between two times on that curve: to the monitor, the day planner, the notebook or the
  door. A fifth stop, the **file cabinet, is keyed in the data but no retail item uses it**.
* Team dressing comes from a small per-team pack `fr<code>.iff` (4 TXTRs: team logo, PDA logo,
  `wallphoto`, `office_photos`). The loader writes those textures into named **material slots
  of the scene at run time** (`material + 0x30`). This is the hook for era dressing: the game
  already re-skins the room per team, and even swaps it live when you change user team with
  the triggers.
* The room is drawn **behind the menu list** by a one-slot "3D background" callback
  (`[0xA84498]`, set by `sub_00069BB0`). Which background a menu gets is chosen by the
  **low 4 bits of the descriptor's flags** (`+0x28`): 7 = the callback slot (new finding, see
  2.3). The Front Office screen (`0x0052533C`) is a second, self-contained copy of the same
  machinery with its own scene (`front_office`), camera (`FOcamShape`) and globals, and it
  draws its room from its own message-7 action. That pattern is the easiest one to copy.
* The Crib is a much bigger, code-driven system (41 scenes, a pack table of 27 files, item
  placement markers, a camera computed in code, a 25-state machine). **Copy the Coach's Desk /
  Front Office mechanism, not the Crib.**
* Exports (glTF 2.0 with embedded PNG textures, plus loose PNGs): `build/eras/office/`
  `coach_desk_o23c4/`, `front_office_o23c6/`, `draft_menu_o23c5/`, `Menubackground_o23c7/`,
  `standalone_textures/` and `crib_reference/room_o4248c2/`. Preview renders of every camera
  stop: `build/eras/office/previews/*_camera_poses.png`.

![Coach's Desk camera stops](../../../build/eras/office/previews/coach_desk_camera_poses.png)

(The previews are rendered offline by a small rasterizer from the exported glTF with the
Cardinals' `fr00.iff` art in the runtime slots, at the keyed camera positions. The lighting
is the baked vertex colour; the game's real look is in `PROJECT_STATUS.md` line 2231, which
records the Coach's Desk 3D office running in this PC port.)

---

## 1. Resources on the disc (verified (disc))

Archive entries are found by `CRC32(UTF-16LE(upper-case file name))`
(`apf_nfl_wrapup_followup.py:88`, `src/nfl2k5_team_files.c` `name_crc`). The `.iff` names
below were matched that way against every UTF-16 `*.iff` string in the XBE.

| Outer | File | Size | Contents relevant here |
|---:|---|---:|---|
| 23 | `fr.iff` (tag `FRANCHISE1`) | 2,500,144 | SCNE `coach_desk`, `draft_menu`, `front_office`, `Menubackground`, small 2D scenes; 39 MRKS (2D panels); 12 TXTR (offseason icons, mail, NFL Draft logo); STRG; 25 LAYT incl. **`coach_desk`** and **`front_office`**; 4 AUDO (draft whooshes) |
| 24-108 | `fr00.iff` .. `fr99.iff` (tag `FRANCHISE2`), 85 files | 95-118 KB each | 4 TXTR: `<code>_teamlogo_00_h0` 256x256, `pdalogo` 64x64, `wallphoto` 256x256, `office_photos` 256x256 (all P8) |
| 301 | `mini.iff` (tag `FRMINI`) | 3,936 | one `CACR` (a sorted list of 953 resource hashes, `mini.cdf`; not office art) |
| 8 | `frontend_sync.iff` | 1,365,200 | main menu scenes (`main_menu`, `sc_studio`, `team_background_loop` ...) |
| 4248 | `crib.iff` | 5,131,344 | The Crib (section 5) |

The four 3D scenes in `fr.iff`:

| Scene (key) | Shapes | Vertices | Materials | Embedded textures | Stored / decoded bytes | Used by |
|---|---:|---:|---:|---:|---|---|
| `coach_desk` (o23c4) | 52 | 8,124 | 39 | 31 | 347,712 / 571,264 | Coach's Desk (Franchise hub) |
| `front_office` (o23c6) | 51 | 8,482 | 42 | 32 | 479,088 / 958,464 | The Front Office |
| `draft_menu` (o23c5) | 41 | 8,624 | 16 | 12 | 349,216 / 613,504 | NFL Draft room |
| `Menubackground` (o23c7) | 1 quad | 4 | 1 | 1 (`BACKDROP1`) | 50,432 / 68,352 | background mode 6 (section 2.3) |

Layouts: LAYT `coach_desk` = `title_bar_sub`, `nav_bar_c` (navigation list),
`coach_desk_info_a`, `coach_desk_info_b` (the two info panels, MRKS with "Next Opponent / Last
Game / Power Ranking / Record" and "Players / Cap Room / Total Salary / Record").
LAYT `front_office` = `title_bar_wide`, `nav_bar_c`.

Per-team art (`fr00.iff` shown in `previews/textures_fr_team_packs.png`): `wallphoto` is an
atlas with a big team pennant, one large action photo and six small framed photos;
`office_photos` holds two diplomas, a pennant and three framed photos; the team logo is the
monitor wallpaper and the notebook sticker. The codes above 31 (Create-a-Team slots) use
black-and-white action photos.

---

## 2. Screens and code paths

### 2.1 The Coach's Desk descriptor `0x00522190` (verified (XBE))

| Field | Value |
|---|---|
| +0x00 title | "Coach's Desk" |
| +0x04 event table | `0x00521EE8` |
| +0x08 handler | `0x000F3E90` (link list) |
| +0x10 rows | `0x00521F20` (11 action rows) |
| +0x18 layout | `coach_desk` (LAYT in `fr.iff`), bindings `0x00ACF1A8` |
| +0x28 flags | `0x07` (background mode 7, see 2.3; no 0x10, so Back does not pop: there is a Quit row) |

Event table:

| Msg | Action | Function | What it does |
|---|---|---|---|
| 1 created | call | `sub_00142DA0` | camera targets = 0, then `sub_00142D00` (load packs) |
| 4 shown | call | `sub_00142FB0` | ambience, find scene, set the 3D callback, play the "return" camera move |
| 5 covered | call | `sub_00142BA0` | `[0xAA2140]` = 0 (no scene pointer, so the callback draws nothing) |
| 6 every frame | call | `sub_00142DD0` | advance the camera; when it arrives, push the pending screen; triggers change user team |
| 8 2D overlay | call | `sub_00142A30` | with 2+ user teams, draws the current user team's name (inferred from the font calls) |
| 2 destroyed | call | `sub_00142B80` | `sub_000F6510(1)`, `sub_00165C10` (stops the ambience, inferred), `sub_00142B40` (unload the three packs), `sub_0027D3A0` |

Rows (all type 9 actions; each action plays a sound, starts a camera move and stores the
screen to push when the camera arrives):

| Row | Action | Camera move (a, b) | Screen pushed | Camera looks at |
|---|---|---|---|---|
| Schedule / Playoff Schedule / Super Bowl Schedule / Off-Season Schedule (one shown per phase, row updates `0x2C01C0`..`0x2C0250`) | `sub_00142880` "Ambient: Calendar In" | (5, 7) | `0x00522828` calendar | day planner |
| Front Office | `sub_00142910` "Front Office In" | (13, 15) | `0x0052533C` The Front Office | door |
| Gameplan | `sub_00142950` "Gameplan In" | (9, 11) | `0x00503D6C` Gameplan | notebook |
| ESPN.com | `sub_00142990` | (1, 3) | `0x00525664` ESPN.com | monitor |
| The Crib | `sub_001428C0` "Front Office In" | (13, 15) | `0x00524FA0` The Crib (Franchise packs unloaded first) | door |
| Features | `sub_001429F0` "Statistics In" | (1, 3) | `0x00525930` Features | monitor |
| Options | `sub_001429B0` "Statistics In" | (1, 3) | `0x00503458` Options | monitor |
| Quit | `0x000C8190` | none | | |

Who pushes the Coach's Desk (verified (XBE)): the end of franchise creation (`0x0013F2D9`,
`sub_0006E390`), the return after a franchise game (`0x0016DDDA`, pop to root + replace top)
and two "pop back to the desk" sites (`0x003625FF`, `0x0036323D`, `sub_0006E450`).

### 2.2 Load, team bind, unload (verified (XBE))

```
sub_00142D00  (once; guard [0xAA240C])
  sub_00043F50(ctx "FRANCHISE1", "fr.iff", slot 0xAA2410, heap sub_00038FB0(0,0))
  sub_00142BB0(0)                                   ; the per-team pack, below
  sub_000441B0(ctx "FRMINI", "mini.iff", slot 0xAA2458, heap sub_00038FB0(0x22,0))
  LAYT "title_bar_news", "title_bar_news_sub" -> sub_00143EA0 (text bindings)
  [0xAA240C] = 1

sub_00142BB0(wait)
  code = [sub_0013EC80() + 0x10C]                   ; the user team's asset code, e.g. "00"
  sub_00043F50(ctx "FRANCHISE2", "fr" + code + ".iff", slot 0xAA2434)
  scene = sub_000449E0(0, 'SCNE', "coach_desk")     ; global lookup by name
  for "teamlogo", "wallphoto": mat = sub_001426E0(scene, name); mat+8 |= 1; mat+0x30 = 0
  wait ? sub_000432D0() : sub_0014E4B0(0)           ; finish the queued load
  scene = sub_000449E0(0, 'SCNE', "coach_desk")
  "teamlogo":  mat+8 &= ~1; mat+0x30 = sub_00142AF0()            ; TXTR code + "_teamlogo_00_h0"
  "wallphoto": mat+8 &= ~1; mat+0x30 = sub_000449E0(0, 'TXTR', "wallphoto")
  [0xAA247C] = sub_0013EC80()

sub_00142CE0  = unload "FRANCHISE2"; sub_00142BB0(1)       ; reload for another team
sub_00142B40  = unload "FRANCHISE1", "FRANCHISE2", "FRMINI" (sub_000432F0); [0xAA240C] = 0
```

* `sub_000449E0(ecx = context or 0, edx = fourcc, push name)` returns the resource's object
  (`+0x14` of the record). With `ecx = 0` it walks every loaded package (list `[0xB09578]`),
  and the first package holding that name wins. With a context string it searches only that
  package (this project already calls it that way: `src/nfl2k5_teams.c`, context
  `L"historic"`).
* `sub_001426E0` (and its copy `sub_0034D6E0`) finds a material by name: `[scene+0x1C]`
  materials of 0x80 bytes at `[scene+0x20]`, UTF-16 name pointer at `+0`.
* Material slot: `+0x08` bit 0 hidden while the art loads; `+0x30` the texture descriptor
  pointer. In the file every material's `+0x30` points at its embedded texture, and is 0 for
  the runtime ones (`teamlogo`, `wallphoto`, `office_photos`), so **the same field serves
  embedded and runtime textures** (verified (disc)). Overriding a material that has an
  embedded texture is **inferred** to work the same way (to test).

### 2.3 Drawing: the background mode in the descriptor flags (verified (XBE), new)

The generic handlers draw, on message 7, in this order (link-list handler `0x000F3E90`,
case `0x000F3F10`): **background** (`sub_000F2810` -> `sub_0006C080`), **layout**
(`sub_000F2F70`), **list** (`sub_00150260`). The settings (`0xF3FC0`) and table (`0xF40F0`)
handlers do the same. `sub_0006C080` reads the top descriptor's flags:

```
mode = (int)(descriptor[+0x28] << 28) >> 28        ; low 4 bits, sign-extended
sub_0006BFD0(mode)                                  ; jump table at 0x6C05C
```

| Mode | Draws | Seen on |
|---|---|---|
| 0 | nothing | The Front Office (flags 0x10): it draws its own room in its msg-7 action |
| 1, 3, 5 | `sub_0006B490` (draws the layout instance at `[0xA84490]` or `[0xA8448C]`, a shared backdrop; inferred) | Gameplan, Options, Features, ESPN.com (0x11); calendar (0x95) |
| 2 | `sub_0006BEF0` | Main Menu (0x02) |
| 4 | a viewport/panel call (`sub_00069AB0`) | |
| 6 | SCNE `Menubackground` with texture `BACKDROP1` (`sub_0006BF70`) | |
| 7 | **the 3D callback `[0xA84498]`** | Coach's Desk (0x07) |

`sub_00069BB0(ecx = fn)` stores `fn` in `[0xA84498]`. Only the Coach's Desk sets it
(`0x00142FE2`, fn = `0x00142820`); the frontend reset `sub_0006B740` clears it.
Consequences:

* A copied hub with **flags low nibble 7** gets the 3D room drawn under its layout and list,
  exactly like the Coach's Desk, once it has set the callback.
* Do not set bit 0x08 (the nibble would turn negative). `NATIVE-MENU.md` notes bit 0x04 as
  "list scrolls" (`vm_add_link` sets it on 0x11 menus). That is harmless there (mode 1 becomes
  5, the same pass), but on a mode-7 hub it would change the background to mode 3. Keep the
  nibble at exactly 7 and test whether the hub needs scrolling at all.
* Screens pushed from the desk (Gameplan, Options ...) use mode 1, and msg 5 cleared the scene
  pointer, so **the office is not drawn behind the submenus**. The zoom into the monitor,
  planner or notebook hides the cut. The desk zooms back out when it is shown again.

The draw callback `sub_00142820`:

```
if ([0xAA2140]) {
  sub_0002AD80(&saved)                                  ; save the current camera (0x2BC-byte frame)
  cam = sub_00142730([0xAA2140], "coach_cameraShape")   ; scene camera by name: [scene+0x3C] records of 0x60 at [scene+0x40]
  sub_0013E710(ecx = 0xAA2150, edx = cam)               ; camera object from the record (look-at + FOV)
  sub_0002AC80(0xAA2150)                                ; make it current
  sub_0002DE60([0xAA2140])                              ; draw every node of the scene
  sub_0002AC80(&saved)                                  ; restore
}
```

`sub_0002DE60` draws the `[scene+0x24]` nodes (0x60 bytes each, at `[scene+0x28]`) through
`sub_00021860`, which **skips a node whose `+0x0C` flags have bit 0x10** (verified (XBE)).
The node also holds a shape pointer at `+0x08` (filled at load) and a 4x4 matrix at
`+0x20..+0x5C` (identity for every office node, verified (disc)). So a whole prop can be
hidden per node (for example the phone, whose `lambert1_DOUBLESIDED` part a per-material hide
would leave behind). Whether the matrix is applied when drawing is **to test**; if it is, props
can be moved or scaled per era without touching vertices.

### 2.4 Camera moves and the pending screen (verified (XBE))

```
sub_001427A0(a, b)                      ; called by every row action
  [0xAA2404] = b ; [0xAA2400] = a       ; b = where the "return" move starts later
  if a <= 0: scene time = 0; return
  scene time = a - 1.0                  ; sub_0002F010: [scene+4] = t, evaluate
  if b > 0: [0xAA2400] = a + 0.4        ; target

sub_00142DD0 (msg 6)
  if [scene+4] < [0xAA2400]:
      [0xBD8050] = 0                    ; menu input off while moving
      sub_0002F030(scene, frame_dt * 1.5)
  else:
      [0xBD8050] = 1
      if pending [0xAA2408]:
          sub_0002F010(scene, [0xAA2400])
          if pending == 0x524FA0 (Crib): sub_0027D3A0(); sub_00142B40()   ; free memory first
          sub_0006E390(FSM, pending)    ; push
  (info panel a/b chosen by season phase)
  with 2+ user teams: LT/RT on any pad -> next/previous user team,
      unload FRANCHISE2, sub_00142BB0(1)  ; the room re-skins live

sub_00142FB0 (msg 4)
  sub_00165BE0("coachambience", 20.0, 40.0)        ; ambient loop (AUSB lookup)
  [0xAA2140] = sub_000449E0(0, 'SCNE', "coach_desk")
  sub_00069BB0(0x142820)                            ; the 3D callback
  sub_00142D00(); [0xAA2408] = 0; sub_0014FCD0(FSM, 0)
  sub_001427A0([0xAA2404], 0)                       ; return move: from b-1 to b
```

So an "in" move for an item plays from `a - 1` to `a + 0.4` (1.4 time units at 1.5x speed,
about 0.93 s), and the screen opens. Coming back, the desk plays from `b - 1` to `b` (about
0.67 s) and rests at the idle pose. Times are seconds; the evaluator turns them into ticks
with `tick = t * 300 + 0.5` (`sub_0002D7B0`, constants 300.0 and 0.5).

### 2.5 The Front Office `0x0052533C`: the self-contained copy (verified (XBE))

Title "The Front Office", handler `0xF3E90`, layout `front_office`, flags `0x10` (Back pops,
background mode 0). Its code (`sub_0034D6E0`..`sub_0034DBB0`) is a duplicate of the desk's
with its own globals: scene `[0xCB8D10]`, camera object `0xCB8D20`, target `[0xCB8FD0]`,
return time `[0xCB8FD4]`, pending screen `[0xCB8FD8]`.

| Msg | Function | What it does |
|---|---|---|
| 1 | `sub_0034DAA0` | scene `front_office`; `teamlogo` -> `sub_00142AF0()`; `office_photos` -> TXTR `office_photos`; time 0, return time 1.0 |
| 4 | `sub_0034DBB0` | "coachambience"; move from 0 to 1.0 (the entrance dolly, at 1x speed) |
| 6 | `sub_0034DB50` | same advance/push logic as the desk |
| **7** | **`sub_0034DA20`** | **draws the room**: camera `FOcamShape`, `sub_0013E710`, `sub_0002AC80`, `sub_0002DE60`, restore |
| 10 | `sub_0034DA80` | "Statistics Out" sound |

Rows: Franchise Status, Cap Management, Team Needs, Trades, Trading Block -> camera (3, 5),
the computer; Contracts, Player Progress -> (7, 9), the contract on the desk; Pro Bowl Votes
-> (3, 5). Because the room is drawn by an event-table action that runs before the generic
handler's own msg-7 work, this screen needs neither the shared callback slot nor a special
flag. **This is the simplest pattern for an Eras copy.**

The draft room (`sub_00326AD0`) is a third variant: scene `draft_menu`, `pdalogo` and
`draft_logo` bound the same way, its own camera curve (a fly-in from above the stage, then
stops on the left/centre/right monitors, the PDA and the draft board).

---

## 3. Camera data (verified (disc) + verified (XBE))

### 3.1 Scene runtime header

The decoded SCNE has its runtime header at decoded offset `0x100` (the object that the code
calls `scene`). Pointers in the file are **self-relative with bit 0 set**:
`target = field_offset + value - 1`.

| Offset | Count / pointer | Records | Used by |
|---|---|---|---|
| +0x04 | float | current animation time | `sub_0002F010`, `sub_0002F030` |
| +0x0C / +0x10 | animation bindings | 0x14 bytes | `sub_0002D7B0` |
| +0x14 / +0x18 | embedded textures | 0x20 | |
| +0x1C / +0x20 | materials | 0x80 | `sub_001426E0` |
| +0x24 / +0x28 | nodes | 0x60 | `sub_0002DE60` |
| +0x2C / +0x30 | shapes | 0x100 | |
| +0x34 / +0x38 | markers | 0x40 | (the Crib room has 252) |
| +0x3C / +0x40 | **cameras** | 0x60 | `sub_00142730`, `sub_0034D730` |

### 3.2 Camera record (0x60 bytes)

| Offset | Meaning (read by `sub_000386F0`, `sub_00038760`) |
|---|---|
| +0x00 | name pointer (UTF-16): `coach_cameraShape`, `FOcamShape`, `draftcameraShape1` |
| +0x10 | right vector |
| +0x20 | up vector |
| +0x30 | back vector (negated for the view direction) |
| +0x40 | eye position (w = 1) |
| +0x50 | focal length, **35.0** for all three rooms. FOV = 2 * atan(30 / (2 * 35)) = 46.4 degrees (`[0xA6D820]` = 30.0; which axis it applies to is to test) |

### 3.3 Animation binding and curve format

Binding record (0x14 bytes): `{u32 hash, u32 0, u32 flags, ptr target, ptr data}`. For the
camera: `hash` = `CRC32(UTF-16LE(camera name))`, case kept (checked: `coach_cameraShape` ->
`0xE282C29E`, `FOcamShape` -> `0xA46A2D4E`, `draftcameraShape1` -> `0x66C55B73`); flags
`0x220001C7`; target = camera record `+0x10`. The data block is a list of channels,
`{u32 segment_count, ptr times (u16 ticks, count+1), ptr coefficients}` (12 bytes each).
Each segment holds four floats for a cubic `((a*x + b)*x + c)*x + d`, where `x` is the tick
offset inside the segment. The camera has **6 channels: eye x, y, z and look-at target
x, y, z**. A key every 300 ticks is one time unit. The decoder is
`build/eras/office/scripts/camcurve.py`, and every pose is in `build/eras/office/camera_keys.json`.

### 3.4 Keyed poses

`coach_desk` (scene units; the desk top is at y = -232, the back wall at z = -1300):

| t | Eye | Target | Shows | Used by |
|---:|---|---|---|---|
| 0, 3, 4, 7, 8, 11, 12, 15, 16, 19 | (260, 250, 801) | (0, 0, 87) | idle: the desk from behind the chair, monitor, lamp, phone, wall photos | hub |
| 1-2 | (-37.8, -40, 306.1) | (134, -40, 21) | computer monitor (team-logo wallpaper) | ESPN.com, Features, Options |
| 5-6 | (-106, -29, -3.8) | (-106, -204, -4) | day planner, top-down | Schedule |
| 9-10 | (-194, -22, 147.3) | (-194, -199, 147) | notebook, top-down | Gameplan |
| 13-14 | (908.5, -16.4, -450.8) | (904, -16, -1270) | the office door | Front Office, The Crib |
| 17-18 | (327.4, 16.0, 548.7) | (928, -130.8, 330) | **file cabinets and plant: no retail item uses this stop** | free for Eras |

`front_office`:

| t | Eye | Target | Shows | Used by |
|---:|---|---|---|---|
| 0 | (-650, 180.7, 1200) | (-62.1, 123, 4.5) | entrance, wide | start of the dolly |
| 1-2, 5-6, 9 | (-600, 180.7, 825) | (-62.1, 123, 4.5) | idle: desk, wall TV, diplomas, trophies, plant | menu |
| 3-4 | (100, 72, 180) | (223, 72, 4.5) | the computer screen (ESPN desktop) | Status, Cap, Needs, Trades, Block, Pro Bowl |
| 7-8 | (-16.1, 155.7, 131.6) | (-16.1, -50.9, 105.6) | the contract and pen, top-down | Contracts, Player Progress |

`draft_menu`: 38 keyed stops (fly-in t = 0-4; centre (0, -45, 315); left/right monitors at
x = -430 / 430; PDA (214.7, -25, 155) looking down; draft board (0, 2000, -4000)), see
`camera_keys.json` and `previews/draft_menu_camera_poses.png`.

![Front Office camera stops](../../../build/eras/office/previews/front_office_camera_poses.png)

---

## 4. What is in the rooms: objects and materials (verified (disc))

"Runtime" means the material has no embedded texture and is filled by code (2.2), or is
drawn with vertex colour only. Texture files are in `<scene>/textures/` (index_material.png),
contact sheets in `previews/textures_*.png`.

### 4.1 `coach_desk`

Objects (52 nodes): kidney-shaped glass `desktop001` on poles `deskpole005/006`; `monitor_group1`
with `teamwallpaper` (screen) and `monitor_shadow`; `phone_group1` (cordless phone base);
`new_lamp_group` (chrome desk lamp) and `lamp_shadow`; `mug_group` and `mug_shadow`;
`blotter`, `dayplanner_group`, `notebook` with `notebook_logo`, `pad_group` (legal pad),
`mousepad`; `couch005_group` (black leather couch) with `shadow`; `coffeetable_group`;
`endtable001_group` and `tlamp001_group` (orange floor lamp); `shelf` (bookcase) with
`book_group`..`book_group7` and four trophies (`football_trophy_group`, `handoff_trophy_group`,
`receiver_trophy_group`, `crystal_trophy_group`); `wall_pic01`..`wall_pic08`, `wall_pennant`;
`file_cabinet`, `file_cabinet1`; `plant002`; `door`, `doorframe`, `doorknob`, `lightswitch`;
`room_walls`, `carpet`.

| Material | Texture (size) | What it shows | Objects |
|---|---|---|---|
| `coach01` | 000 (256) | the shared office atlas: wood, the day planner's red leather cover with gold NFL shield, book spines, the NOTEBOOK spine, phone keys, metal knobs, dark wall tone | books, day planner, door frame, door knob, light switch, mousepad, mug, notebook, legal pad, room walls, shelf, picture frames |
| `coffee_table002_export_vcSwShader2` | 001 (128) | cherry wood | coffee table, bookcase |
| `carpet1` | 002 (128) | tan patterned carpet | carpet |
| `vcHwShader27` | 003 (128) | light oak raised panel | door, end table, floor lamp |
| `vcSwShader16` | 004 (32) | pale blue-green glass gradient | desk top and poles |
| `coach03` | 005 (64) | soft shadow blobs | lamp and mug shadows |
| `padline` | 006 (32) | yellow ruled lines | legal pad |
| `cabinet` | 007 (64) | dark green metal drawer front | file cabinets |
| `vcSwShader12` | 008 (32) | grey speckle | blotter |
| `vcSwShader2`, `3`, `4`, `5`, `6` | 011, 012, 009, 010, 013 | cordless phone: keypad and LCD, handset, buttons, silver | phone |
| `vcSwShader1` | 014 (128) | computer monitor bezel and stand, glare on the screen | monitor |
| `vcSwShader9`, `vcSwShader11` | 015, 016 | chrome dome and arm | desk lamp |
| `zshadow` | 017 | floor shadow | under the couch |
| `vcHwShader2`..`5` | 018-021 (128) | black leather cushions, one rust throw pillow | couch |
| `vcHwShader30` | 022 (64) | wood | end table |
| `lambert6`, `lambert103`, `lambert104` | 025, 023, 024 | leaf, soil, terracotta pot | plant |
| `vcHwShader31`, `vcHwShader32` | 027, 026 | orange lamp shade, lamp base | floor lamp |
| `reflection`, `trophy001c_football_high_vcHwShader2` | 028, 029 | gold gradient, dark wood base | three gold trophies |
| `reflection_blue` | 030 (128x256) | blue glass | crystal trophy |
| `teamlogo` | runtime: `<code>_teamlogo_00_h0` | team logo | monitor wallpaper, notebook sticker |
| `wallphoto` | runtime: per-team `wallphoto` | team pennant and action photos | `wall_pic01`..`08`, pennant, mousepad and mug decals |
| `lambert1`, `lambert1_DOUBLESIDED`, `mon_shadow`, `vcSwShader13`..`15` | none | vertex colour only | parts of end table, lamps, phone, `wall_pic08`, blotter |

### 4.2 `front_office`

Objects (51 nodes): `desktop` (black desk) with `table_drawerL/R`; `chair` (leather);
`monitor_group` with `monitorscreen`, `keyboard_group`, `keyboard_rest`, `mouse`, `mousepad`;
`phone_group`; `blotter_group`, `dayplanner_group`, `notebook` + logo, `contract` and `pen`
(with shadows), `inbox`, `paperbox`; `wall_tv_group` (wall-mounted TV) with `screen_shadow`;
`diploma01`, `diploma02`, `photo02`..`photo04`; `counter_top` with drawer faces and handles
(credenza along the back wall), `books`; `console_table001_group`; trophies
(`trophy010_group`, `trophy013_group`, `primetime012_rbhurdle_low_group1`, the hurdler);
`ballcase_group` with `gameball` and `glass`; `plant001G`; wall outlets; and the shell
(`old_walls_carpet_foor_ceiling_*`: walls, drop ceiling, carpet, door). The camera side of the
room (z > 0) is unfinished (`previews/front_office_overview_back.png`), so camera moves must
stay in the front half.

| Material | Texture | What it shows | Objects |
|---|---|---|---|
| `old_walls_carpet_foor_ceiling_frontoffice01`, `frontoffice01` | 000 (256) | room atlas: drop-ceiling tiles, air vents, wall and wood trims, outlets | ceiling, walls, door group, drawer faces and handles, counter top, desk |
| `old_walls_carpet_foor_ceiling_coach01`, `coach01` | 002 (256) | the coach desk atlas (same image) | door group, blotter, books, day planner, keyboard, notebook |
| `old_walls_carpet_foor_ceiling_vcSwShader10` | 003 (128) | carpet | carpet |
| `vcHwShader27` | 001 | light oak panel | door, console table |
| `drawerface` | 005 (64) | wood drawer fronts | desk drawers |
| `frontoffice03` | 006 (256) | a contract page with the NFL shield, a gold and black pen | contract, pen |
| `update_jan03_wall_coach01`, `update_jan03_wall_frontoffice01` | 012, 013 | atlas variants | wall TV frame |
| `monitor_screen` | 014 (256) | NFL shield on blue | wall TV picture |
| `monscreen` | 031 (256) | a computer desktop with the ESPN NFL Football wallpaper and icons | computer screen |
| `vcSwShader1` | 015 | monitor bezel (same as the desk's) | computer |
| `vcSwShader7` | 004 | mouse | mouse |
| `vcSwShader2`..`6` | 016-020 | phone (same as the desk's) | phone |
| `lambert81` | 021 | black leather | chair |
| `plant001`, `plant001_basket` | 008, 009 | big leaf, wicker | plant |
| `primetime012_rbhurdle_low_vcHwShader1`, `vcHwShader2`, `vcHwShader30` | 010, 011, 007 | gold, dark wood | trophies, console table |
| `GB_laces`, `GB_wilson`, `GB_skin` | 024-026 | laces, "Wilson NFL" stamp, pebbled leather | game ball |
| `case_wood`, `case_highlights`, `case_brass_sides`, `case_brass_tops` | 027-030 | mahogany and brass | ball case |
| `vcSwShader23`, `vcSwShader18` | 022, 023 | paper lines, metal | paper box, inbox |
| `office_photos` | runtime: per-team `office_photos` | diplomas, pennant, photos | `diploma01/02`, `photo02`..`04`, mousepad |
| `teamlogo` | runtime | team logo | notebook sticker |
| `lambert1`, `lambert1_DOUBLESIDED`, `lambert80`, `update_jan03_wall_lambert80`, `case_stand`, `case_glass` | none | vertex colour | desk top, keyboard, mouse, pen, phone parts, shadows, case stand, glass |

### 4.3 `draft_menu` (brief)

Stage with curtains, valances and team flags; three CRT monitors on the desk; a PDA; a big
draft board ("NFL ENTRY DRAFT / DRAFT ORDER SUMMARY"); shelves with helmets; two "VIP" figures
and a GM figure (`gm01`..`gm03`); sepia player collages on the walls (`franchise04/05`).
Atlases `opaque`, `transparent`, `helmets` (32 helmets + draft board parts), `stagefloor1`.

---

## 5. The Crib, for comparison

| | Coach's Desk / Front Office | The Crib (`0x00524FA0`) |
|---|---|---|
| Packages | `fr.iff` + one 100 KB per-team pack | `crib.iff` (5.1 MB) + a table of 27 tag/file pairs at `0x00AC9048` (`crib_loading.iff`, `crib_16k/32k/64k`, `crib_av`, furniture packs, `crib_intro.iff`, `crib_trophy.iff` ...) |
| Scenes | 1 room | 41: `room` (79 shapes, 252 placement markers), `gui`, `cursor`, two skyboxes, eight trophy scenes, 27 collectibles; the 242 item textures live in outer 4274 |
| Camera | stored in the scene, keyed curve, chosen by time | **computed in code** (`sub_00275B70`: eye/target `0xAC8530`/`0xAC8540`, FOV `[0xAC8528]`, camera `0xAC8820`); the intro is a separate scene with `cameraShape1` and 61 animation bindings |
| Logic | about 20 small functions | custom handler `0xF3DA0`, a 25-state machine (`sub_00278390`), purchase and placement, Crib credits, the save |
| Size | 2.6 MB of packages on disc (`fr.iff` + one team pack) | 5.1 MB for `crib.iff` alone plus item packs; the desk unloads the Franchise packs before pushing the Crib |
| Retexture | per-team TXTR binds already exist | 498 textures, all editable on disc with the Mod Studio (`nfl2k5_crib_backend.md`) |

The Crib is the better looking room, but its look comes from its item systems, and its camera
is code. Reusing it for Eras would mean either running the whole Crib (which owns its save
data and credits) or re-implementing the camera, the item placement and the state machine.
The desk and the Front Office need a load call, a material walk, a draw call and a timer.
**Reuse the Coach's Desk / Front Office mechanism.** Crib geometry export, for reference only:
`build/eras/office/crib_reference/room_o4248c2/room.gltf`.

---

## 6. How an Eras copy loads the same scene with era textures

### 6.1 Recommendation

1. **Menu and camera:** a new "Eras Office" descriptor built from C (served function
   addresses, as `src/nfl2k5_video_menu.c` does), modelled on the **Front Office pattern**
   (room drawn from its own msg-7 action, its own globals). Use the Coach's Desk callback slot
   (flags nibble 7) only if the hub must also be drawn under something else. Rows are type 9
   actions that call one shared "move camera, then push" helper.
2. **Scene:** a **copy** of the room in its own package, loaded under an Eras tag and looked
   up with that tag, never with the global lookup, so Franchise and Eras can never pick up each
   other's resources.
3. **Era dressing:** per-era texture binds into the copy's material slots (the mechanism the
   game uses for team art), per-node hides for props an era should not have, and a per-era
   room choice (`coach_desk` or `front_office`). For extra resolution or surfaces that need no
   bind code, use the context-gated texture folders in `OFFICE-RETEXTURE.md` option A.
4. **Camera:** start with the retail curves (five stops in the desk, including the unused
   file cabinet; two in the Front Office). Move to code-driven keyframes when the hub needs
   more stops.

### 6.2 The copied package

Two ways, both cheap:

* **New archive entries (recommended).** `nfl2k5_archive_extend` (`src/nfl2k5_team_files.c`
  line 296, gen patch `ARCHIVE_EXTRA_PACK`) already appends files from `mods\teams\*\files\`
  and `mods\oldschool\files\` to an extra pack G at boot, and the game opens them by name like
  its own (verified for added teams, `PROJECT_STATUS.md` 2026-10-09). Adding a
  `mods\eras\office\files\` scan is the same ten lines. Then ship:
  * `erasoffice.iff`: the chunks copied out of `fr.iff`: SCNE `coach_desk` (chunk 4),
    `front_office` (6), optionally `draft_menu` (5), LAYT `coach_desk` (89) and `front_office`
    (90), plus MRKS 51/52 if the info panels are kept. A `.iff` is just a run of 0x20-byte
    chunk headers and payloads, so this is a byte copy: about 0.8 MB, 1.2 MB with the draft room.
  * `erasoffice_<era>.iff` (one per era): TXTRs named after the materials they replace
    (`carpet1`, `coach01`, `vcHwShader27`, `wallphoto`, `teamlogo`, `monscreen` ...), built from
    the user's photos with the team creator's P8 encoder (`tools/team-creator/logos.py`
    `_encode_p8`) and the Mod Tools' VC-LZ compressor, starting from an existing TXTR of the
    same size as a template. Giving a template TXTR a new name needs its name field rewritten
    (tool work, to test).
  * Because the copied file has its own name (and CRC), it decodes into its own allocation
    (inferred). The scene keeps the name `coach_desk`, so the Eras code must look it up with its
    own context, and Franchise must never be loaded at the same time. Unloading on msg 2
    guarantees that.
* **Load `fr.iff` itself under an Eras tag** (`OFFICE-RETEXTURE.md` option B). This needs no
  new file, but it is closer to "using Franchise directly", and whether a second tag gives a
  separate decoded copy is unverified there.

If the scene in the copy is also **baked** (era textures written into its embedded P8
textures with the Mod Studio's scene texture writer, one copy per era), its texture hashes
differ from Franchise's automatically, so plain `mods\textures` hi-res files work without a
context gate. That costs about 350-480 KB per era and room, versus 100-300 KB for a TXTR pack.

### 6.3 The C side (sketch; names are the game's functions, the flow is the desk's)

```
msg 1 (create)      gcall(sub_00043F50, ctx L"ERASOFFICE", L"erasoffice.iff")   ; + heap, slot as 0x2D17B0 / nfl2k5_teams.c
                    gcall(sub_00043F50, ctx L"ERASSKIN",   L"erasoffice_1970.iff")
                    gcall(sub_000432D0)                                          ; wait
                    scene = sub_000449E0(L"ERASOFFICE", 'SCNE', era.room)        ; "coach_desk" or "front_office"
                    for each material m (scene+0x1C count, +0x20 array, 0x80 each):
                        t = sub_000449E0(L"ERASSKIN", 'TXTR', name(m)); if t: m+0x30 = t, m+8 &= ~1
                    for each node n in era.hide (scene+0x24 count, +0x28 array, 0x60 each): n+0x0C |= 0x10
                    time = 0, target = 0, pending = 0
msg 4 (shown)       sub_00165BE0(era.ambience or "coachambience", 20.0, 40.0)
                    move(return_time, 0)                                         ; as sub_001427A0
msg 6 (frame)       as sub_00142DD0: advance at dt*1.5 until target, [0xBD8050] input lock, then push pending;
                    LT/RT: unload ERASSKIN, load the next era's pack, rebind (live re-skin, as the desk does per team)
msg 7 (draw)        as sub_0034DA20: save camera; cam = camera record by name; sub_0013E710; sub_0002AC80;
                    sub_0002DE60(scene); restore
msg 2 (destroy)     unload ERASSKIN, ERASOFFICE (sub_000432F0); stop ambience
row action          sound; move(a, a+2); pending = Eras screen
```

Notes:

* Served function addresses: `0xFEC0xxxx` / `0xFED0xxxx` are resolved by
  `recomp_lookup_manual` before the generated lookup (`NATIVE-MENU.md` 2.1). These do not run
  inside interpreted mod-pack code (`PHASE2-PLAN.md` hazard 8), so the hub is `--retail` only
  until that is solved.
* Release the office packages before a match starts (the desk frees its packs before the
  Crib); see `MATCH-LAUNCH.md` for the launch path.
* Layout: LAYT `front_office` (title + list) is the simplest. `coach_desk` brings the two info
  panels, whose text comes from the Franchise binding table `0x00ACF1A8`; Eras would need its
  own binding table to show the era, season and record there.

### 6.4 Camera stops for the Eras hub

With the retail `coach_desk` curve (time pairs for `move(a, b)`):

| Stop (a, b) | Looks at | Suggested Eras rows |
|---|---|---|
| (1, 3) | the monitor | League, Standings, Statistics, News |
| (5, 7) | the day planner | Schedule |
| (9, 11) | the notebook | Team / Roster / Depth |
| (13, 15) | the door | Play the next game, leave the office |
| (17, 19) | the file cabinets (unused in retail) | League History, Records, Hall of Fame |

With `front_office`: (3, 5) the computer, (7, 9) the contract on the desk.

For more stops or different angles there are two routes:

* **Code-driven camera (recommended next step):** keep a small table of eye/target keys per
  room and era in C, interpolate, and write the camera record's vectors (`+0x10..+0x4C`)
  before `sub_0013E710`, or build the camera directly with the look-at and FOV calls
  `sub_0013E710` uses (`sub_0002BA10`, `sub_0002B860`). No scene edit is needed.
* **Authored curves:** write new channels into a copy of the scene. The format is decoded
  (3.3). The binding hash is the camera name's CRC, and the times are u16 ticks, so curves
  longer than about 218 seconds do not fit. This needs a SCNE rebuild (Mod Tools'
  `nfl2k5_scne_builder.py`).

### 6.5 Era dressing, per surface

Which surface carries an era is set by the table in 4.1/4.2. Example recipes follow. The
pictures must be the user's own photos (wall photos, diplomas, screens, logos):

| Era | Room | Re-skin (material -> the user's image) | Hide (nodes) |
|---|---|---|---|
| 1970s den | `coach_desk` | `coach01` walls -> wood paneling; `carpet1` -> shag; `vcSwShader16` desk -> wood; couch -> brown leather or plaid; `wallphoto` / `teamlogo` -> 1970s photos and logo | `monitor_group1`, `teamwallpaper`, `monitor_shadow`, `mousepad`; `phone_group1` until a period phone exists |
| 1980s corporate | `front_office` | carpet, walls; `monscreen` -> green-text terminal; `monitor_screen` (wall TV) -> a 1980s broadcast still; `office_photos` -> 1980s photos | |
| 1990s | `front_office` | `monscreen` -> a 1990s desktop; wall TV still; photos | |
| 2000s | `coach_desk` | close to retail (it is the 2004 office); era photos and logo | |
| 2010s | `coach_desk` | darker walls, modern carpet, a flat-screen look on `vcSwShader1`; photos | |
| 2020-2025 | `front_office` or `coach_desk` | modern finishes, the wall TV and monitor with current broadcast art; photos | |

Props that change shape (rotary phone, typewriter, console TV, flat screen) cannot come from
textures. Use prop morphs or renderer mesh injection (`OFFICE-RETEXTURE.md` options E and F;
`src/nfl2k5_ncaa_render.inc.c` is the precedent), or the node matrix if it turns out to be
applied (2.3).

### 6.6 To test, in order

1. A served descriptor with flags nibble 7 and the callback set shows the room behind its list
   (or nibble 0 plus a msg-7 draw action, the Front Office way).
2. `erasoffice.iff` from pack G loads under its own context, and
   `sub_000449E0(ctx, 'SCNE', "coach_desk")` finds the copy, not Franchise's.
3. `material + 0x30` overrides on materials that have embedded textures (`carpet1`) render, and
   Franchise afterwards still looks retail.
4. Node flag 0x10 hides `phone_group1` completely.
5. Whether the node matrix (`+0x20`) is applied (move a prop).
6. The FOV axis (46.4 degrees horizontal or vertical) and the 1.5x speed feel at 60 fps.

---

## 7. Files (`build/eras/office/`, local; textures and models are retail pixels and must not be committed)

Written by this task:

* `coach_desk_o23c4/`, `front_office_o23c6/`, `draft_menu_o23c5/`, `Menubackground_o23c7/`:
  `<scene>.gltf` + `.bin` (Blender-readable, Mod Tools exporter, `nfl2k5_texture_id` on every
  material), `<scene>-README.txt`, `scene_summary.json` (nodes, materials, textures),
  `textures/NNN_<material>.png`.
* `standalone_textures/o23` (fr.iff icons), `o24`, `o25`, `o59`, `o108` (fr00/01/36/99 team packs).
* `crib_reference/room_o4248c2/`: the Crib room, for comparison.
* `camera_keys.json`: cameras, bindings and the pose at every whole time unit for the four
  `fr.iff` scenes.
* `previews/`: `*_camera_poses.png`, single poses `*_tNN.png`, `coach_desk_overview_a/b.png`,
  `front_office_overview_back.png`, texture contact sheets `textures_*.png`.
* `scripts/`: `export.py` (glTF + PNG), `scenecams.py` + `camcurve.py` (cameras and curves),
  `txtr_dump.py` (standalone TXTRs), `render.py` + `render_office.py` (previews),
  `catalog.py` (named list of all 4,616 SCNE), `sheet.py`.

The other files in that folder (`office_inventory.*`, `office_shapes.*`, `models/`,
`textures/`, `office-textures.draft.ini` ...) belong to `OFFICE-RETEXTURE.md`.

Regenerate: `python .tmp-2k-football-mod-tools/tools/nfl_resource_scan.py
original/disc/vc_53450030/0 --json <dir>/inventory.json` (about 2 minutes), then
`python build/eras/office/scripts/export.py <dir> build/eras/office o23c4 o23c6 o23c5 o23c7`,
`python build/eras/office/scripts/scenecams.py <dir> o23c4 o23c6 o23c5 o23c7` and
`python build/eras/office/scripts/render_office.py`.

# Era offices: how to retexture (and reshape) the copied Franchise office

2026-10-15. Read-only research for "My NFL Eras": the hub copies the Franchise office and
dresses it differently for each era (1970s wood paneling and a rotary phone, 1980s, 1990s
CRTs, 2000s, 2010s, 2025 flat screens). Nothing was built, launched or edited in `src/`,
`tools/`, `external/`, `config/` or `saves/`.

Labels: **VERIFIED (code)** = read in this project's code or the generated code in
`build/gen-locals`; **VERIFIED (disc)** = read from `original/disc/vc_53450030` by the
scripts in `build/eras/office/`; **INFERRED** = follows from the above but not checked;
**UNVERIFIED** = needs a game run. No claim below was seen in game.

## 1. Short answer

The project already has every piece needed. Two of them together give a dressed office per
era quickly, without touching Franchise:

1. **Era texture folders on the existing texture-pack hook, switched on only while the Eras
   office is showing** (option A). Any office texture is replaced at any resolution from
   `mods\eras\offices\<era>\`. Franchise never turns the context on, so it always draws
   retail. About 150 lines in `src/nfl2k5_texture_packs.cpp` and
   `src/nfl2k5_controller_art.cpp`, no gen patch.
2. **Material edits in the Eras copy of the office loader** (option B): hide or show props,
   point a material at another texture, pick the room per era. The game already does exactly
   this to the same two scenes for the team logo and wall photos (`sub_00142BB0`,
   `sub_0034DAA0`). The edits are read on the game thread and land in the captured
   pushbuffer, so they are exact and never leak into Franchise.

For per-material photo files that need no hash list, and for exact renderer identity, add
**salted texture clones** (option C). For era props the game never had (a real rotary phone,
a typewriter, a CRT TV), use **same-topology morphs** (option E) or a **renderer mesh
injection** (option F), which is simpler for static props than the NCAA body because the
draw's own vertex program can transform the new mesh.

## 2. What the Franchise office is

### 2.1 Packages (VERIFIED (disc))

| file (outer) | tag in the XBE | contents |
| --- | --- | --- |
| `fr.iff` (23, 2,500,144 bytes) | `FRANCHISE1` | 4 AUDO, SCNE `coach_desk`, `draft_menu`, `front_office`, `Menubackground`, `power_meter`, `player_photo_a`, 4 weekly-prep/gamecast plates, 39 MRKS, 12 TXTR (combine, draft, email ... icons), STRG, 25 LAYT |
| `fr<code>.iff` (24-108, 85 files) | `FRANCHISE2` | 4 TXTR: `<code>_teamlogo_00_h0` 256x256, `pdalogo` 64x64, `wallphoto` 256x256, `office_photos` 256x256 (all P8) |
| `mini.iff` (301) | `FRMINI` | one CACR |

Codes are `00`-`31`, `33`-`37`, `40`-`46`, `50`-`85`, `95`-`99`. The codes above 31 carry
Create-a-Team logos (FW, aces, axes, crown, jester, banana ...) and a shared set of black and
white action photos, a neutral "old photo" look that could serve as a 1970s fallback
(`build/eras/office/historic_wallphotos.png`).

### 2.2 Scenes, props and their textures (VERIFIED (disc))

Every office texture is P8 (paletted), embedded in the scene. The prop to material mapping
comes from the shapes' submesh tables (`office_shapes.json`). Texture hashes are the
renderer's `tex_content_hash`, computed offline from the disc bytes
(`office-textures.draft.ini`). They are **INFERRED** to match the game, and one
`texture_dump = 1` run confirms them (section 8).

`coach_desk` (the Coach's Desk menu: 52 shapes, 39 materials, 31 textures):

| prop (shape) | material | texture (hash, size) | era use |
| --- | --- | --- | --- |
| `room_walls`, `doorframe`, books x8, `shelf`, `dayplanner_group`, `notebook`, `lightswitch` | `coach01` | office atlas `67A3C2EF` 256 | the walls are cells of this shared atlas: paint wood paneling or wallpaper into them |
| `door`, `endtable001_group`, `tlamp001_group` | `vcHwShader27` | wood panel `60BEEEB5` 128 | wood tone per era |
| `carpet` | `carpet1` | `DD2E1927` 128 (tiled) | shag, berber, gray |
| `desktop001`, `deskpole005/006` | `vcSwShader16` | desk metal `9B096D0A` 32 | wood desk (70s/80s), glass (2025) |
| `monitor_group1` | `vcSwShader1` (only user) | flat monitor `4C544C90` 128 | hide (70s), beige PC (80s), CRT face (90s), thin bezel (2010s+) |
| `monitor_shadow` | `mon_shadow` | none | hide with the monitor |
| `phone_group1` | `vcSwShader2`-`6` + shared `lambert1_DOUBLESIDED` | phone `E3D7F0CD` 128 + 4 parts | era phone look; real rotary shape needs E/F |
| `wall_pic01`-`08`, `wall_pennant`, `mug_group`, `mousepad` | `wallphoto` | per team, from `fr<code>.iff` (85 hashes) | **the user's era photos** |
| `teamwallpaper`, `notebook_logo` | `teamlogo` | per team `<code>_teamlogo_00_h0` (85 hashes) | the franchise's era logo |
| `couch005_group` | `vcHwShader2`-`5` | leather `56919D8E` `02610720` `EF302289` `AB402618` | vinyl, plaid, leather |
| `tlamp001_group` | `vcHwShader31/32` | lamp shade `3A8B7DD9`, base `97656C21` | period lamp |
| `file_cabinet`, `file_cabinet1` | `cabinet` | `17A7086C` 64 | steel green (70s) |
| trophies, plant, blotter, pad, shadows | ... | see the draft ini | |

`front_office` (the Front Office menu: 51 shapes, 42 materials, 32 textures) shares 11
textures with `coach_desk` (office atlas, wood panel, carpet, phone parts, brass, trophies)
and adds a **wall TV** (`wall_tv_group`: frame in `B2DA7D94`/`CA0F59C6`, picture
`monitor_screen` `2B516A5F` 256), a desk computer (`monitor_group` `vcSwShader1` `2707E62F`,
screen `monscreen` `9C802FB4` 256), keyboard, mouse, diplomas and photos (`office_photos`,
per team), a game ball and a ball case. The wall TV picture shows UV rows 0.05-0.81 (about
4:3); the monitor screen shows rows 0-0.80 (5:4). Size user images to those shapes.

`draft_menu` (41 shapes) is the draft room: three big monitors, a draft board and VIPs. It is
a third room the Eras hub could use.

Local views (never commit them, they hold retail pixels): `build/eras/office/coach_desk_sheet.png`,
`front_office_sheet.png`, `fr00_sheet.png`, `textures/`, and `models/coach_desk.gltf` and
`models/front_office.gltf` (Blender-readable, exported with 2K Football Mod Tools'
exporter).

### 2.3 How the game loads, binds and draws it (VERIFIED (code), `build/gen-locals/recomp_0009.c`)

| step | function | what it does |
| --- | --- | --- |
| load | `sub_00142D00` | if `[0xAA240C]` is 0: loads `fr.iff` as `FRANCHISE1` (slot `0xAA2410`), calls `sub_00142BB0(0)`, loads `mini.iff` as `FRMINI`, finds two LAYT title bars, sets `[0xAA240C]` = 1 |
| per-team bind | `sub_00142BB0` | formats `fr%s.iff` with the team code at `[sub_0013EC80() + 0x10C]`, loads it as `FRANCHISE2` (`0xAA2434`), finds SCNE `coach_desk` into `[0xAA2140]`; material `teamlogo`: clears hidden bit, `+0x30` = TXTR `%s_teamlogo_00_h0` (`sub_00142AF0`); material `wallphoto`: clears hidden bit, `+0x30` = TXTR `wallphoto` |
| front office bind | `sub_0034DAA0` (`recomp_0023.c`) | SCNE `front_office` into `[0xCB8D10]`; `teamlogo` and `office_photos` get their TXTR the same way |
| find material | `sub_001426E0` / `sub_0034D6E0` | `esi` = scene; walks `[scene+0x1C]` slots of 0x80 bytes at `[scene+0x20]`, compares the UTF-16 name at slot `+0` |
| find resource | `sub_000449E0` | `ecx` = package or 0 (every loaded package, list at `[0xB09578]`), `edx` = fourcc, arg = name; returns the resource object (record `+0x14`) |
| draw | `sub_00142820` | saves the view, takes camera `coach_cameraShape` from the scene (`sub_00142730`), applies it (`sub_0013E710`, `sub_0002AC80`), draws the scene (`sub_0002DE60`, the shared SCNE draw with 69 callers), restores |
| background hook | `sub_00069BB0` | `[0xA84498]` = `ecx`; the menu draw dispatcher tail-calls `[0xA84498]` each frame (`recomp_0003.c`). Coach's Desk activation stores `0x142820` there |
| unload | `sub_00142B40` | unloads `FRANCHISE1`, `FRANCHISE2`, `FRMINI`, `[0xAA240C]` = 0 |

Material slot (0x80 bytes, VERIFIED (code), the same layout `src/nfl2k5_oldschool.c` uses):
`+0x00` name pointer (UTF-16), `+0x08` bit 0 hidden, `+0x09` reflection, `+0x30` texture
descriptor pointer, `+0x34` reflection map. The descriptor is 0x20 bytes (disc layout:
`+4` pixels, `+8` palette, `+0xC` format, `+0x10` size, `+0x14` flags). Its runtime contents
after relocation are **UNVERIFIED**.

## 3. What the project already has, and what each piece can do here

### 3.1 Texture packs (`src/nfl2k5_texture_packs.cpp`, VERIFIED (code))

* `mods\textures\**\<HASH>*.png|jpg|bmp|dds` replaces the texture whose content hash
  matches, at any size up to 16384. Larger images get a mip chain. Decoded images go in a
  768 MB LRU. `texture_dump = 1` writes `<HASH>_<w>x<h>_fmt<NN>.png`.
* The hash is FNV over every 32-bit word of the base level plus the 1,024-byte palette for P8
  (`tex_content_hash`). It depends on content only, not on address or context. **The Franchise
  office and an Eras copy of it have the same hashes**, so a plain texture pack changes both.
* It plugs into the renderer through `g_xbox_tex_override*`, which
  `src/nfl2k5_controller_art.cpp` owns: `wants(fmt,w,h)` (bit 0 may replace, bit 1 follows
  `gen()`), `gen()`, `override(...)`. A texture pack is asked first, then the controller art.

### 3.2 The renderer's texture path (`external/xboxrecomp/src/kernel/nv2a_gpu_d3d11.inc.c`, VERIFIED (code))

* `gpu_texture()` keys its cache by address, format, size, pitch and palette address
  (P8). It re-hashes contents once per frame with a sampled hash (`gpu_tex_hash`). For shapes
  whose `wants` has bit 1 it mixes in `g_xbox_tex_override_gen()`, so **a change of `gen()`
  makes those textures upload again on the next frame**.
* The override is called with the full content hash, and **with the game's own decoded texels
  (`orig`) for P8 and the other decoded formats**. A replacement can therefore be composed at
  run time on top of the retail image, for example the user's photos pasted into the retail
  wall-photo atlas, with no retail pixels shipped.
* DXT textures are offered to the override before upload, and decoded formats after decoding.
  `tex_upload_override` builds mips for larger images.
* The **controller art is already context-dependent retexturing**: the Duke picture changes
  with the connected controller and the highlighted button through `gen()`. Era offices are
  the same idea with a different key.
* **Timing (VERIFIED (code), `nv2a_pb_scan.c`):** with the async render thread, the game
  thread captures the pushbuffer at kick and the render thread runs it later. *"Data the
  commands only point at (vertex arrays, textures) is still read when drawn."* Two rules
  follow:
  * a material edit on the game thread is exact, because it changes which texture
    address goes into the captured commands;
  * guest texture or vertex memory a queued frame may still read must not be overwritten.
    Write it once before first use, or allocate new memory for each change. A context flag
    the game thread sets is seen by the render thread up to the queue depth late, about one
    frame.

### 3.3 Old-school helmet: per-draw material edits (`src/nfl2k5_oldschool.c`, VERIFIED (code))

`nfl2k5_oldschool_draw` (gen patch `OLDSCHOOL_DRAW` in `sub_0008FAD0`) runs on the game
thread for every player draw. It walks the scene's material slots by name, sets or clears
`+8` bit 0 to hide parts, and copies a texture pointer (`+0x30`) from one slot to another
(team helmet art onto shell B), clearing reflection (`+9`, `+0x34`). It does this per draw
because player scenes are shared and reused. The office is one scene drawn once per frame,
so its edits can be made once after load, or each frame in the Eras draw callback. The body
part of that file, which writes vertex positions into the shared block and restores them,
shows the risk of per-draw memory writes under the async render thread. The office should
write such data once, into its own copy.

### 3.4 NCAA body: renderer-side per-draw changes (`src/nfl2k5_ncaa_render.inc.c`, VERIFIED (code))

* **Identify a draw on the render thread** by (primitive, index count, FNV of the index
  list) (`ncaa_route`). Players are told apart by matching the vertex-program constants
  against poses the game thread captured, because a game-thread flag does not line up with
  the render thread.
* **Change it:**
  * add per-vertex offsets to attribute 0 during fetch (`g_os_delta` in `fetch_attr`,
    `nv2a_pb_exec.c`);
  * replace its vertices with a CPU-built mesh and bind an own texture view
    (`ncaa_mesh == 1`, `ncaa_texture_srv`);
  * drop it (`return 2`).
* For a static office prop, a replacement mesh can go through the draw's own vertex program
  (`vp_run`) in the prop's object space. No skinning and no projection recovery are needed,
  unlike the NCAA body. This is **INFERRED** from how `vp_vertex` and `fetch_attr` work.

## 4. Constraints that decide the design

1. **Franchise must not change.** Anything keyed only by content hash needs a gate, because
   the Eras office reuses Franchise's textures.
2. **Async render thread.** Exact options act on the game thread (material slots) or on
   identities carried in the captured commands (own texture addresses). A plain context
   flag can be up to about one frame late, which only matters at screen transitions.
3. **Shared textures.** The office atlas, wood panel, carpet and phone are shared by both
   rooms. The walls and the books sit in the same atlas. Wall photos and logos come from 85
   per-team packages, so 85 hashes per slot.
4. **Mod packs.** Gen patches and `0xFEE` C functions do not run inside interpreted code
   (PHASE2-PLAN hazard 8). Option A lives in the renderer and works with any pack. B to F
   depend on the Eras C code, which is `--retail` only for now.
5. **No retail pixels in the repository.** Ship hashes, names and cell rectangles. Templates
   are exported locally from the player's own disc, as 2K Football Mod Tools does.
6. **The user's rule:** era art comes from the user's photos, never from drawn art.

## 5. Options, ranked by effort and risk

| | option | changes | effort | risk | Franchise isolation | exact? |
| --- | --- | --- | --- | --- | --- | --- |
| **A** | era texture folders on the texture-pack hook, gated by an "Eras office" context | any texture, any resolution, animated screens | S | low | the context is only set by the Eras hub | about 1 frame at transitions |
| **B** | material edits in the Eras office copy (hide/show, pointer swaps, room choice, logo/photo bind) | visibility, which loaded texture a prop uses | S-M | low | own load tag and own draw callback; Franchise reloads retail | yes |
| **C** | salted clones: the Eras office binds cloned descriptors with a 1 KB palette copy, one unused entry nudged; the salted hash is registered to the user's file | per-material, per-era textures, name-based files | M | medium | by construction | yes |
| **D** | own guest textures from PNG (own pixels) bound to materials | as C, renders even without the pack layer | M | medium | by construction | yes |
| **E** | same-topology prop morphs per era (positions written once into the Eras copy, or `g_os_delta` per draw) | prop shapes: deeper CRT, chunkier phone | M | medium | own scene copy / gated | yes |
| **F** | renderer mesh injection for static props, through the draw's own vertex program | new models from the user's glTF: rotary phone, typewriter, CRT TV | L | medium-high | gated by C's identity or context | yes with C |
| G | disc-level copy (new `.iff` entries through the overlay) | anything, offline | XL | high | yes | yes |

### A. Context-gated era texture folders (recommended first)

**What.** Extend `nfl2k5_texture_packs.cpp` with era folders that are consulted only while
a context is set:

```cpp
extern "C" void nfl2k5_texpack_context(int year);   // 0 = off; 1978 -> 1978\, 1970s\, default\
extern "C" uint32_t nfl2k5_texpack_gen(void);       // bumps on every context change
```

* **Scan** `mods\eras\offices\` at init, per era folder. File names are either raw
  `<HASH>*.png`, the texture-dump workflow, or friendly names resolved through
  `mods\eras\offices\office-textures.ini` (friendly name = one or more hashes; draft:
  `build/eras/office/office-textures.draft.ini`).
* **Lookup.** In `nfl2k5_texpack_find`, when a context is set, the era map is searched
  before `mods\textures`. Cache by file path rather than by hash, because one hash maps to a
  different file per era.
* **Hook changes.** In `controller_art.cpp`: `wants()` returns 3 for the shapes that occur
  in the era maps (P8 16x16 to 256x256 here; 128x128 and 64x64 P8 already return 3), and
  `gen()` mixes in `nfl2k5_texpack_gen()`. Every texture of those shapes uploads once again
  when the era or the context changes: one short hitch at hub entry or era switch, softened
  by the texture pool.
* **Warm-up.** When the context is set, decode the era's files on a worker thread, so the
  first frame does not decode PNGs on the render thread.
* **Who calls it.** The Eras hub sets the context when it registers its office draw
  callback, and clears it when it leaves.
* **Composition.** For `wall_photos`, `office_photos` and `team_logo`, if the era folder has
  single photos (`wall_photo_1.png` ...) instead of a full atlas, paste them into the cell
  rectangles of the retail atlas (`orig`) at 4x and return the result. Cell rectangles go in
  a small JSON file with no pixels. They are measured once from the glTF UVs; the per-mesh
  UV boxes in `office_uv_boxes.json` are union boxes and still need per-face refinement.
* **Animated screens (eye candy).** A `tv_slideshow\` folder cycles images on the wall TV
  or monitor: bump `gen()` every N seconds for that one shape, and the override returns the
  next file.

**Isolation.** Franchise never sets the context, so even with era folders installed it
draws retail. A user's global `mods\textures` pack still applies to Franchise as before.

**Limits.** Only pixels change, at the original UVs and aspect. Shared textures change
everywhere in the hub (both rooms), which is the goal here. A texture that also appears on
a non-office screen would change there for the roughly one frame the context lags at a
transition. None of the office hashes is known to be on such a screen.

**Effort and risk.** About 150 lines of C++, no gen patch, no guest memory writes. It
works with mod packs because it is renderer-side.

### B. Material edits in the Eras copy of the office (recommended with A)

**What.** The Eras hub gets its own copy of the office plumbing, in C or as `0xFEE` served
functions:

* **Load.** Load `fr.iff` under the Eras' own tag (for example `ERAS1`) with the game's
  loader `sub_00043F50`, in the same way as `sub_00142D00` does it. Do **not** touch
  `[0xAA240C]` or `[0xAA2140]`, so Franchise always loads its own fresh copy. Whether a
  second tag decodes a separate copy is **UNVERIFIED**; if it does not, restore every edited
  slot on exit.
* **Draw.** Register an Eras draw function in `[0xA84498]` (as `sub_00069BB0` does for
  `0x142820`). It applies the era's material state, then draws the chosen scene
  (`coach_desk`, `front_office` or `draft_menu`) with its camera, as `sub_00142820` does.
* **Material state.** Find each material by name (the `sub_001426E0` walk), then:
  * hide: `+8 |= 1`; show: `+8 &= ~1`;
  * swap: `+0x30` = another slot's `+0x30`;
  * logo and photos: `+0x30` = `sub_000449E0(0, 'TXTR', name)` from any loaded package,
    for example the franchise's own `fr<code>.iff`.

These are exactly the writes `sub_00142BB0` and `sub_0034DAA0` make to the same scenes.

**Era uses.**
* Hide `vcSwShader1` and `mon_shadow` in the 1970s (no computer).
* Choose `front_office` for the 1990s (wall TV, computer, CRT-looking TV frame) and
  `coach_desk` for others.
* Point `vcSwShader16` (metal desk top) at the wood panel's texture for a wooden desk.

**Limits.**
* Hiding works per material. `vcSwShader1` is used only by the monitor, so it hides
  cleanly. The phone shares `lambert1_DOUBLESIDED` with the floor lamp, so hiding the
  phone's own materials leaves that untextured part; drop it with F's route drop.
* The walls share the `coach01` atlas with books and shelves, so the walls change through A
  (the atlas image), not through B.

**Isolation and exactness.** The edits are read on the game thread and go into the captured
pushbuffer, so they are exact and can even change per frame. Franchise's own code path
is untouched.

### C. Salted clones (exact, per material, name-based files)

**What.** After the Eras load, for each material the era retextures:

* clone its 0x20-byte descriptor into guest heap;
* clone the 1,024-byte palette;
* change one palette entry the texture does not use (or one texel's index to an
  identical-colour entry);
* point `+0x30` at the clone;
* compute the clone's content hash (the same FNV the renderer uses; the bytes are known);
* register `hash -> mods\eras\offices\<era>\<material or friendly name>.png` with a new
  `nfl2k5_texpack_register()`.

**Why.**
* The user's files can be named after props (`computer_monitor.png`), with no hash list.
* Two props that share one retail texture can get different images.
* The renderer sees a distinct texture address and hash in the captured commands, so no
  context flag and no `gen()` bump are needed, and no Franchise draw can ever match.
* It also gives F a reliable way to know "this is the Eras phone".

**Risk.**
* The runtime layout of the descriptor and of the P8 palette binding (palette address vs a
  D3DPalette object) must be read once in game (section 8).
* Clones need about 1 KB of guest memory each.
* The palette copy must be written once and never overwritten while queued.

### D. Own guest textures from the user's PNG

As C, but the clone also gets new pixel memory with the user's image converted to the
original size and format (P8 quantized, or A8R8G8B8 swizzled with the format field changed).
It shows at native size even without the pack layer, and the registered hash still brings
the full-resolution file. It costs 64-256 KB of guest memory per texture and needs memory
the renderer's `dma_resolve` maps correctly. Package loads (`sub_00043F50`) probably use
the right allocator (INFERRED); whether `xbox_HeapAlloc` addresses work as texture memory
is UNVERIFIED. Use it only where C cannot (a material with
no texture, or a format change).

### E. Same-topology prop morphs

The Mod Studio already exports and imports same-count, same-topology vertex edits (glTF to
Blender and back; Crib electronics, stadiums, bodies). For eras: deepen the flat monitor
(`monitor_group1`, 376 vertices) into a CRT box, or reshape the phone (234 vertices). Ship
`mods\eras\offices\<era>\shapes\<shape>.base.bin/.new.bin` diffs, the same scheme as
`mods\oldschool\hi_body.*.bin`. Write them **once** into the Eras copy's block after load
(never per draw), or apply them renderer-side as `g_os_delta` for draws that match the
shape's route and the Eras context. UVs stay, so the textures must follow the new shape.

### F. Renderer mesh injection for static props

For props the game never had (rotary phone, typewriter, wood-cabinet TV), use the user's
glTF, converted offline to a small mesh file with its own texture. The renderer replaces
draws matching the prop's route (`ncaa_route` style: primitive, index count, index FNV,
plus the bound texture's hash from C) while Eras is active. Static props need no skinning;
the new vertices, in the prop's object space and divided by the shape's position
scale/offset, go through the draw's own vertex program. Baked lighting comes from the mesh
file's vertex colours. The same route table also gives "drop this draw" (`return 2`) for
leftovers such as the phone's shared part. This is the largest piece, and it should come
after A to C have proved the routes in game.

### G. Disc-level copy (not recommended)

New `.iff` entries mean growing the archive directory through the overlay. Mod Studio's
model tools refuse topology changes, and the user's photos would need an offline build
step. A to F already give an independent copy at run time.

## 6. Recommended plan

1. **A + B for the first playable hub**, in the Eras office copy:
   * era texture folders: wood paneling, carpet, desk, phone look, monitor screen, the
     user's wall photos and era logos;
   * hide or show props, and pick the room per era;
   * Franchise untouched by construction.
2. **C** when the user wants files named after props, and before F.
3. **E** for cheap shape changes (CRT depth), **F** for true era props, prop by prop.

Hooks the hub copy needs (for whoever designs the copied menu): its own package tag, its
own `[0xA84498]` draw callback (`0xFEE0xxxx` served function), and a call to
`nfl2k5_texpack_context(year)` on entry and `0` on exit.

## 7. Folder layout for the user's photos

```
mods\eras\offices\
  README.txt
  office-textures.ini       friendly name = hash(es), generated from the disc (no pixels)
  atlas-cells.json          photo cells of wall_photos / office_photos / office_atlas (no pixels)
  default\                  used by every era that lacks its own file
  1970s\  1980s\  1990s\  2000s\  2010s\  2020s\
  1985\                     optional exact-year folder, beats 1980s\
```

Inside an era folder (every file optional):

```
  era.ini                   [room] scene = coach_desk|front_office|draft_menu
                            [hide] computer_monitor, phone        (option B)
                            [swap] desk_metal = wood_panel        (option B)
  wood_panel.png            door, end table, lamp base           (square, tileable)
  office_atlas.png          walls, books, door frame ...          (paint over the guide)
  carpet.png                (tileable)
  desk_metal.png            desk top
  phone.png  computer_monitor.png  computer_screen.png  wall_tv_picture.png
  couch_leather_1.png ...   floor_lamp_shade.png  file_cabinet.png
  wall_photos.png           full atlas, OR:
  wall_photo_1.png .. wall_photo_8.png, pennant.png   pasted into the retail cells
  office_photos.png / photo_1.png .. / diploma_1.png ..
  team_logo.png             or team_logo_<ABBR>.png for one franchise
  tv_slideshow\*.jpg        cycles on the wall TV / monitor
  60BEEEB5.png              raw hash names still work (texture-dump workflow)
```

* **Era for a season.** Folders are searched in this order: `<year>\`, `<decade>\`,
  `default\`, the global `mods\textures`, then retail.
* **Team logo.** If the era folder has none, use `mods\teams\<ABBR>\logos\<decade>.png`,
  then `logos\scorebug.png` (team folders already carry `logos\scorebug.png`).
* **Image rules.**
  * Any resolution; 4x the original is a good target (128 to 512, 256 to 1024).
  * Same aspect as the original: square for most, 4:3 for the TV picture, 5:4 for the
    monitor screen.
  * Tileable for carpet, wood and leather.
  * PNG with alpha for leaves and cut-outs.
* **Guides.** A tool (later, `tools/`) exports the retail textures from the player's own disc
  into `%LOCALAPPDATA%\NFL2K5-PC\eras\office-guides\`, named like the friendly names, to
  paint over. The repository ships names, hashes and cells only.

### Era recipes (concrete slots; the pictures are the user's)

| era | room | textures (A/C) | material edits (B) | shapes (E/F) |
| --- | --- | --- | --- | --- |
| 1970s | coach_desk | dark wood paneling in the `office_atlas` wall cells and `wood_panel`; shag `carpet`; wood `desk_metal`; beige/black `phone`; vinyl `couch_leather_*`; green `file_cabinet`; 1970s team photos; era logo | hide `vcSwShader1`, `mon_shadow` | rotary phone (F) |
| 1980s | coach_desk | oak paneling; berber carpet; beige computer face on `computer_monitor`; push-button phone | none | deeper monitor (E) |
| 1990s | front_office | beige CRT face, Windows-95-era `computer_screen` (the user's screenshot), CRT `wall_tv_picture` | room = front_office | CRT depth (E) |
| 2000s | coach_desk | retail look plus the user's 2000s photos and logo | none | none |
| 2010s | front_office or coach_desk | thin-bezel `computer_monitor`, HD `wall_tv_picture`, VoIP `phone`, gray carpet | none | none |
| 2020s (2025) | coach_desk | glass `desk_metal`, white or light wood, modern `computer_screen`, wall TV slideshow of the user's 2025 photos | hide phone parts (smartphone era) | flat wall TV (F) |

## 8. What to check in game later (nothing below was run)

1. **Hashes.** Run once with `texture_dump = 1` (`--retail`), open Franchise > Coach's Desk
   and the Front Office, and check that the dump names include `60BEEEB5_128x128_fmt0B`,
   `DD2E1927_128x128_fmt0B`, `E3D7F0CD_128x128_fmt0B` and `4C544C90_128x128_fmt0B`. That
   confirms the offline hash list.
2. **Option A smoke test.** Put a channel-swapped `wood_panel.png` in `1970s\`, set the
   context from a test hook, and see the door change. Clear the context, enter Franchise,
   and see the retail door.
3. **Descriptor layout (for C and D).** A study log, behind an env switch, of the 0x20 bytes
   at `[material+0x30]` for `vcHwShader27` and `teamlogo` after the coach desk loads, and
   of the palette address the renderer receives for that draw (`t->palette`).
4. **Second load tag.** Load `fr.iff` under an Eras tag while nothing else is loaded, edit a
   slot, unload, then enter Franchise and confirm the slot is retail.
5. **Routes (for E and F).** `RECOMP_IDX_DUMP` on the office to record (primitive, count,
   index hash) for `phone_group1` and `monitor_group1`.

## 9. Open questions

* Does a second package tag give an independent decoded copy of `fr.iff`? (B; check 4.)
* What is the runtime layout of the texture descriptor and its palette? (C, D; check 3.)
* Exact photo cell rectangles in `wallphoto` and `office_photos` (union UV boxes measured;
  per-face refinement pending).
* Which room and camera the copied hub uses. Another workflow owns the menu copy; the
  camera `coach_cameraShape` and the MRKS chunks in `fr.iff` are the Coach's Desk camera
  path.
* Whether the user wants the phone and monitor hidden or reshaped in the eras without them.

## 10. Research files (`build/eras/office/`, local; textures and models hold retail pixels)

* `office_inventory.py` writes `office_inventory.json` / `.txt`: every resource of `fr.iff`,
  the 85 `fr<code>.iff` and `mini.iff`, the scenes' materials and embedded textures, and the
  offline content hashes. `--png` exports textures.
* `office_shapes.py` writes `office_shapes.json`: shape (prop) to material.
* `export_office_models.py` writes `models/*.gltf` and `office_uv_boxes.json`, using Mod
  Tools' exporter.
* `contact_sheet.py` writes `coach_desk_sheet.png` and `front_office_sheet.png`.
* `office-textures.draft.ini`: friendly name to hash, for both rooms and the 85 per-team
  textures (input for `mods\eras\offices\office-textures.ini`).
* `fr00_sheet.png`, `historic_wallphotos.png`: per-team atlases.

Run: `python -I build/eras/office/office_inventory.py .tmp-2k-football-mod-tools/tools
original/disc/vc_53450030/0 build/eras/office [--png]` (the other scripts take the same
arguments; `export_office_models.py` takes the Mod Tools root instead of its `tools`).

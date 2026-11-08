# My NFL Eras: draft show feasibility ("With the first pick in the NFL Draft, Detroit selects...")

2026-10-16. Research only. Nothing in `src/` was edited, nothing was built, `NFL2K5.exe` was not
run and nothing was committed. Every picture below is either a retail in-game reference frame
that already existed (`logs/franchise-ref/58-the-draft-room.png`) or an **offline render** of
retail assets exported from the player's disc (glTF through the 2K Football Mod Tools, drawn
by the small numpy rasteriser `build/eras/office/scripts/render.py`, posed by a CPU skinning
script written for this note). Frames marked **MOCK** are offline composites that show what
the scene could look like; they were not produced by the game.

Sheet: **`logs/draftfx/draftfx-sheet.png`** (18 tiles). Single frames: `logs/draftfx/*.png`.

The user's idea: a 3D draft-day sequence. A commissioner (a coach model) at a podium announces
the pick, the drafted player walks up on stage in a suit (it must not look like he has shoulder
pads under it) and holds up his new team's jersey, then college highlights play. Options: the
whole first round, skip to your pick, or off.

Evidence levels: **verified (disc)** = decoded from `original/disc/vc_53450030`;
**verified (XBE)** = read from the XBE or generated code; **verified (offline)** = shown by an
offline render or decode made for this note; **inferred**; **to test** (needs a game run).

---

## 0. Short version

The game already has almost every piece, and two of them are better than the brief assumed:

1. **Stage:** Franchise's own NFL Draft room, SCNE `draft_menu` in `fr.iff` (archive outer 23,
   chunk 5), has a real empty stage (about 9 m wide), curtains, team banners, helmet shelves and
   the "NFL ENTRY DRAFT" board. The Eras office already copies scenes out of `fr.iff` and draws
   them with its own camera (`tools/eras/build-office-pack.py`, `src/nfl2k5_eras_office.c`), so
   the stage is a one-line pack change plus the existing runtime.
2. **Commissioner: the game has one.** SCNE `commish01` (outer 2725, chunk 7) is a fully skinned
   man in a navy suit, white shirt, red tie and glasses, made for the **Super Bowl MVP trophy
   cutscene**. The same package has the presentation set: `cutscene` (chunk 0) = stage
   platform, wooden **podium**, two microphone stands and five folding chairs, plus
   `propsmvptrophy` (chunk 8).
3. **Suited player: use the commissioner's body.** `commish01` is rigged on the **player
   skeleton** (the same 25 joint names and order as the player body `LO_res`, including the
   shoulder-pad joints, but his mesh has natural shoulders). Every 23-channel player clip on the
   disc therefore drives him directly. Verified offline: walk, idle, trophy lift and two-hand
   raise poses all skin cleanly (`posed_commish_coach.png`, `posed_hold_up_candidates.png`).
   Retexture the coat and trousers per player and give him a different head, and he is the
   drafted player in a suit. No pad flattening is needed. The player body itself cannot lose its
   pads: they are baked into the jersey geometry plus a body-type morph.
4. **Animations exist as cutscene actor clips:** the MVP presentation `ex023` (3 players, coach,
   commissioner, trophy prop, 9 s, one shared timeline), the captains' walk to midfield
   `fpcoinrun*`, the anthem idle `anthemstand*`, the tunnel run, handshakes and a "hold the
   helmet overhead" clip `ex021player02`. Two or more actors on one timeline is how the game's
   cutscenes already work.
5. **Jersey prop:** each team's in-game kit file (`<code>h<style>.iff`) has the 512x256 torso
   atlas (front and back halves), the jersey digits 0-9 and a name-plate atlas. A two-triangle
   (or small cloth) mesh between the hands with that texture is enough
   (`mock_02_jersey_hold.png`). The Crib's `framed_jersey` item has a runtime-bound `jersey_m`
   material and shows the game already binds jersey art to a flat prop.
6. **Overlay:** the HTML presentation layer can draw the "1980 NFL Draft, Round 1 Pick 1"
   card and a college highlight `<video>`. Today it **only runs during a match**
   (`src/nfl2k5_presentation.cpp:4026`), and there is **no C API to post a custom event**. Both
   are small changes.

**Recommended route:** copy `draft_menu` (and the podium set) into an Eras pack, draw it with
the Eras office runtime and a code-driven camera, add the HTML pick card, and draw the actors
(commissioner and suited player, both `commish01`) with a **PC-side actor renderer**: CPU
skinning of the exported mesh, posed by the retail SMCD clips, drawn after the scene in the same
camera. The offline preview built for this note already does exactly that in Python.
MVP = stage + camera + cards + idle commissioner; then the walk-up; then the jersey hold.

---

## 1. Stages

### 1.1 Franchise's Draft Room, SCNE `draft_menu` (verified (disc), verified (XBE))

| Item | Value |
|---|---|
| Resource | `fr.iff` (outer 23, tag `FRANCHISE1`), SCNE `draft_menu` = key `o23c5`, 41 nodes, 8,624 vertices, 16 materials, 12 embedded textures (349,216 stored / 613,504 decoded bytes) |
| Code | `sub_00326AD0` (Franchise "The Draft", screen `0x537E38`): `sub_000449E0(0,'SCNE',"draft_menu")` stored at `[0xC90790]`, binds `pdalogo` and `draft_logo` (`src/recomp/gen/recomp_0022.c:57616`) |
| Camera | `draftcameraShape1` (CRC `0x66C55B73`), 38 keyed time units: fly-in from above the stage t=0-4, centre (0,-45,315), left/right monitors at x=-430/430, PDA, draft board (0,2000,-4000). `build/eras/office/camera_keys.json` |
| Retail reference | `logs/draftfx/ingame_58_retail_draft_room.png` (the retail screen: board with names, helmets, curtain stage, "3:30 Round 1" clock) |
| Export | `build/eras/office/draft_menu_o23c5/draft_menu.gltf` + textures (local, retail pixels, never committed) |

What is in it (node bounds, scene units):

| Part | Nodes | Where |
|---|---|---|
| Stage | `stagefloor_scissor` (wood floor, `stagefloor1`), `stage_front`, `stage_trim`, `stagefloor_trim` | floor top y = 474, x = -4500..4500, z = -4281..-7463 (front edge to back wall) |
| Curtains | `curtain_group`, `valance01/02`, `backwall`, `black_wall` | behind and in front of the stage |
| Board | `draft_board` (`drafttitle`), `DB_elements_group`, `draftboard_trim`, `stagecabinet` | centre back, x = -1206..1206, y = 1282..3058 |
| Dressing | `helmet_group` + shadow (32 helmets atlas), `hor/vert_shelf_group`, `wall_panel_group`, `wall_photo_group` (`franchise04/05` sepia photos), `teamflag01-04` banners, `W_walllight01/02` | back wall |
| War-room desk (hide for a show) | `table_scissor`, `monitor_left/center/right_group`, `LM/CM/RM_screen_group`, `PDA_group`, shadows | in front of the stage, z = -744..256 |
| People | `GM_group` (1,013 vertices, textures `gm01`/`gm02`), `vip01`, `vip02` (`gm03`) | **static, unskinned** low-poly figures in front of the stage (`draftroom_vip_gm_static_figures_front.png`) |

Scale: the static figures are about 1,130 units tall and a CRT is 335 units wide, so the room is
built at roughly **6 units per centimetre** (inferred). The stage floor is about 9 m by 3.2 m
and about 2 m above the room floor at that scale.

There is **no podium** in this room. The podium comes from the trophy cutscene (1.4).

Renders: `draftroom_stage_wide_audience.png` (from the audience), `draftroom_stage_flyin_t2.png`
and `_t3.png` (the retail fly-in), `draftroom_stage_podium_close.png`,
`draftroom_stage_floor_level.png`, `draftroom_retail_camera_stops.png`.

### 1.2 How the Eras office copied the Coach's Desk (the pattern to reuse)

From `docs/eras/results/M3-office.md` and the code:

* `tools/eras/build-office-pack.py` reads `fr.iff` through the archive index and byte-copies
  records listed in `WANT` (line 25: SCNE `coach_desk`, `front_office`, LAYT `coach_desk`,
  `front_office`) into `mods/eras/office/files/erasoffice.iff` (gitignored, built on the
  player's machine). **Adding `('SCNE', 'draft_menu')` copies the draft room** (about
  +350 KB). A separate `erasdraft.iff` is cleaner if the draft show must load without the
  office.
* `src/nfl2k5_team_files.c:371` appends `mods\eras\office\files\*.iff` to the extra pack G at
  boot, so the game opens the copy by name.
* `src/nfl2k5_eras_office.c` loads the pack under its own context (`ERASOFFICE`, never
  `FRANCHISE1`), finds the scene with `sub_000449E0(ctx,'SCNE',name)`, hides nodes with flag
  `0x10` at node `+0x0C`, swaps material textures (`+0x30`, clear bit 0 of `+8`), and draws on
  message 7: save camera `sub_0002AD80`, camera object `sub_0013E710`, make current
  `sub_0002AC80`, draw `sub_0002DE60`, restore. Camera stops use the scene's keyed curve
  (`move()` = `sub_001427A0`), and it frees its packages before a match.
* So the draft stage needs: the pack record, a room entry `draft_menu` with camera
  `draftcameraShape1`, a hide list (desk, monitors, PDA, static figures), and either retail
  curve stops or **code-driven camera keys** (FRANCHISE-OFFICE-SCENE.md 6.4: write the camera
  record's vectors before `sub_0013E710`). A broadcast-style show wants code-driven keys.

### 1.3 ESPN studio (main menu background), SCNE `sc_studio` (verified (disc))

`frontend_sync.iff` (outer 8, chunk 2): 20 meshes, 5,033 vertices, **unskinned set only, no
hosts**: the anchor desk with a screen, stairs and rail to an upper deck, LED ticker ("ESPN NFL
FOOTBALL"), NFL banners, glass cylinders (`espn_studio_berman.png`). The main menu draws it
through background mode 2 (`sub_0006BEF0`, `src/recomp/gen/recomp_0003.c:10070`: scene from
`sub_00068D50`, first camera record, `sub_0002DE60`). A good alternative "studio" stage (e.g.
for a draft-day studio intro), but it has no podium and no stage edge to walk up to.

`bermanintro` (outer 21, chunk 0): the SportsCenter desk set with **Chris Berman**, a skinned
suit body (`r_bermanbody` 19 joints), hands and a 29-joint head. He is modelled **waist up
only** (the desk hides his legs), so he cannot walk on a stage. His coat texture has the same
layout as the commissioner's coat.

### 1.4 Podium / presentation set: the Super Bowl MVP trophy cutscene (verified (disc))

Archive outer **2725** (pack 2; duplicate at outer **3020** in pack 3):

| Chunk | Kind | Name | Content |
|---|---|---|---|
| 0 | SCNE | `cutscene` | `STAGE` (platform quad), `podium` (161 vertices, `podiumwood`), `Microphone`, `Microphone1`, `FOLDING_CHAIR`..`FOLDING_CHAIR4`; unskinned. Bounds in cm: podium 71 x 113 x 39 (y 75..188) |
| 1 | SMCD | `ex023coach01` | coach, 21 channels (referee skeleton), 137 frames, 15 Hz, 9.03 s |
| 2 | SMCD | `ex023commish01` | commissioner, 25 channels, 9.03 s |
| 3-5 | SMCD | `ex023player01..03` | players, 23 channels (player skeleton), 9.03 s |
| 6 | SMCD | `ex023propsmvptrophy` | trophy prop, 1 channel |
| 7 | SCNE | `commish01` | the commissioner (section 2.1) |
| 8 | SCNE | `propsmvptrophy` | the MVP trophy, 69 vertices |

This is the only podium/presentation scene in the 4,616-scene catalogue (searched names:
podium, stage, trophy, press, interview, ceremony, commish). There is no post-game press
conference scene. Render: `commish01_trophy_cutscene.png`.

### 1.5 The Crib (for completeness)

`crib.iff` (outer 4248) is a large code-driven room (41 scenes, computed camera, 25-state
machine; FRANCHISE-OFFICE-SCENE.md 5). No avatar or casual-clothes person model was found
(celebrity challengers are portraits and voice). Its `framed_jersey` item (o4248c93, 64
vertices) is a picture frame whose inner material `jersey_m` has no embedded texture, i.e. it
is bound at run time (`jersey_and_suits_extra.png`). Not a stage candidate.

### 1.6 Recommendation: stage

Use the **`draft_menu` copy** as the stage, with the desk, monitors, PDA and static figures
hidden, the trophy-cutscene **podium and microphone** placed stage-left (copied as a second
record and drawn after the room with its own node matrix, or drawn by the actor renderer as a
static mesh), and a code-driven broadcast camera. Mock: `mock_01_wide_walkup.png`,
`mock_03_commissioner_podium.png`.

The draft board can show the real pick list: it is a flat textured quad (`drafttitle`); the
retail screen draws the rows itself. In the copy, the board rows can come from the HTML
overlay (simplest) or from a texture rendered on the PC side and bound to the `drafttitle`
material slot.

---

## 2. Coach and character models

Catalogue: all 4,616 SCNE resources were listed with the Mod Tools (`catalog.py`); character
candidates were exported to `build/eras/draftfx/<name>_<key>/` (glTF + textures,
`scene_summary.json`; local only).

### 2.1 `commish01` (o2725c7): the commissioner (verified (disc), verified (offline))

* One skinned mesh, 2,337 vertices, 8 submeshes, **25 joints**: `root, lfemur, ltibia, lfoot,
  ltoes, rfemur, rtibia, rfoot, rtoes, waist, thorax, neck, head, lcollar, lhumerus, lelbow,
  lwrist, lhand, rcollar, rhumerus, relbow, rwrist, rhand, lshoulderpad, rshoulderpad`. That
  is **exactly the player `LO_res` skeleton** (same names, order and identity bind rotations).
* Textures: `shoes`, `pants` (khaki), `coat` (navy suit, white shirt, red tie), `hand`, `face`
  (grey-haired man with glasses; resembles the 2004 commissioner), `neckmaterial`, `glasses`.
* Height about 190 cm. Slim shoulders: no pad bulk anywhere.
* Posed offline with player clips (player channel map `PLAYER_MAP` of
  `nfl2k5_animation_math.py`): `anthemstandplayer` (idle), `fpcoinrunhomeplayer01` (walking
  out), `ex023player01` (trophy lift), `ex018player04`, `ex021player02` (both arms up). All
  skin correctly: `posed_commish_coach.png`, `posed_hold_up_candidates.png`.
* The commissioner's own clip `ex023commish01` has 25 channels; its channel map is not proved
  (the Mod Tools only prove the 23-channel player map and 21-channel referee map). Using a
  player clip for him (as in the renders) avoids that question.

### 2.2 Sideline head coaches, outers 348-383 (verified (disc))

* 36 packages, SCNE `coach` chunk 0 each (the range is `COACH_OUTERS = range(348, 384)` in the
  Mod Tools' `tools/nfl_bone_binding.py:43`), plus `c_shadow`.
* Three skinned meshes: `coachBodyGrp1` (about 4,150-4,630 vertices, 25 joints on the
  **referee** skeleton: it has `ltwist`/`rtwist` instead of shoulder-pad joints),
  `coachLodGrp1` (637 vertices) and `coachHeadGrp1` (about 1,050 vertices, 29-joint face rig:
  jaw, eyes, brows, lips).
* Textures: `handRightDefault`, `arms`, `accessories` (Motorola headset, sneakers),
  `shirt`, `shirtLogo`, `jacket`, `pants`, `head`, `z_eyeShadow`. **Outfits are team sideline
  jackets / windbreakers with headsets** (o348 red, o355 red with a cap, o372 navy with gold
  trim): `coaches_sideline.png`. **No coach pack wears a suit.** Heads differ per pack (36
  real-coach faces), which makes them a good **head library** for 2.5.
* Code (Mod Tools `tools/nfl_rest_orientation.py:40-61`): coach scene setup `0x00095D40`,
  hierarchy builder `0x00096050`, twist dispatch `0x00095FB0`; coach and referee share bone map
  `0x0051D010`. Coach records: 35 x `0xA8`, `body` at `+0x18`; no outfit field is documented.
* Coach clips exist (21 channels): `ex023coach01` (raises both arms with a clipboard at the
  end), `ex007coach01/02`, `focuscoachcoach01`, `challenge03coach01`, `es2xx coach` ... and
  pack-3 `ANM_COACH_*` (e.g. `ANM_COACH_HAPPY_CLAP_PACE`). Posed offline: `posed_coach.png`
  (body only: the separate head skin did not attach in the offline preview; in game the coach
  head follows the body, **to test** in a custom scene).
* A coach could be the announcer (user's idea), but he would be in a team windbreaker and
  headset. `commish01` is the better commissioner; a coach head on the commissioner body is the
  better "coach as commissioner" (2.5).

### 2.3 Suited extras (verified (disc))

| Model | Key | Rig | Look |
|---|---|---|---|
| `vip1` | o346c117 (`gamedata.iff`) | 15 joints, 6 LODs (105-491 vertices) | charcoal suit, blue tie, moustache; one 128 px `SIDELINE` atlas |
| `vip2`, `vip3` | o346c118/119 | same | maroon / olive sport coats, ties, credentials |
| `media1`, `security`, `coach_asst`, `sideline_player` | o346c112/115/111/116 | 15 joints | photographer vest, security, assistant with clipboard, bench player |
| `cameraman01` | o2717c6 | 25 joints | cameraman, cap, camera |
| `cheerleadera`, crowd | cutscene packs 2532-2623, 2787-2791 | 31 channels | not useful here |

Renders: `sideline_extras.png`, `jersey_and_suits_extra.png`. These are low detail (sideline
LODs) and use a 15-joint rig, so player clips do not drive them. Background dignitaries at
best.

### 2.4 Can a character be placed in a front-end scene?

* The draft room and the offices only animate the **camera**; their people are static props.
* The game's cutscenes pose several skinned actors on one timeline: cutscene pose builder
  `0x0012F670` (switch on actor type, jump tables `0x12F9F4`/`0x12FA08`), descriptor
  constructor `0x00130150`; animation lookup `0x001685B0`, root `0x001685E0`, sampler
  `0xDF700`, trajectory `0xDEE30`, skin palette `0x00022C00`, render dispatch `0x00021860`
  (Mod Tools `tools/nfl_pose_matrix_apply.py`, `nfl_motion_sampler_inventory.py`). These run
  inside a match. **Whether they work in a menu is untested**; nothing has ever drawn a skinned
  character in a front-end scene in this port except the Create/Edit Player preview.
* The PC port already has a renderer-side mesh path: `src/nfl2k5_ncaa_render.inc.c`
  (`ncaa_player_draw`) CPU-skins a custom mesh with a live skeleton and draws it in the D3D11
  path. In a front-end scene we own the camera, so projection is simpler than in that path.
* **Recommendation:** a PC-side **actor renderer** for the draft show (section 7): load the
  actor meshes (exported from the player's disc at pack-build time, like the office pack),
  sample the retail SMCD clips with a C port of the Mod Tools sampler (smallest-three
  quaternion decode, fixed-table interpolation, player/referee channel maps), CPU-skin, and draw
  after `sub_0002DE60` with the same view/projection. The offline preview
  (`posed_commish_coach.png`, mocks) is a working reference implementation of the skinning.
  Native drawing through `sub_00022C00`/`sub_00021860` is the alternative; try it as a spike
  first because it would give the game's own lighting.

### 2.5 Recommendation: commissioner

`commish01` at the podium. Idle on `anthemstandplayer` (or the coin-toss loop
`cointossloopplayer01`), a short gesture loop from `ex023player0x` while he reads the card.
For era looks: the 1980 commissioner was Pete Rozelle, and `commish01`'s face resembles the
2004 commissioner. Per the user's rule (their own photos, not drawn art), an era face is a
`face` texture made from a photo the user supplies; a coach head from 2.2 is the fallback.

---

## 3. Player bodies without pads

### 3.1 The player mesh (verified (disc), from the Mod Tools and the old-school work)

* Shared base meshes in outer 3: `hi_body` (o3c114, 7,396 vertices, 62 joints), `lo_body`
  (o3c113, 5,065 vertices, 25 joints), `hi_head` (o3c115, 11,727 vertices, 27 joints, 38 face
  morph channels). Per-player looks come from morph weights and textures.
* **Pads are not a separate part.** They are built into the jersey geometry (`UNIF_jersey`,
  `NECKROLL*`, `NUMBER_shoulder_*`), and their size is a body-type morph
  (`jersey_shoulderpad_big/small`, plus gut and arm morphs). The morph data format is not
  decoded. Body Type (roster `+0x18` bits 3-4) only picks Skinny..Extra Large, all with pads.
  Render: `player_body_pads.png`.
* So "hide or scale down the pads" is not a switch. It would mean reshaping the base mesh.

### 3.2 The parked old-school work

`parked/oldschool-1953` (branch, commit `c3bd172`; working-tree files `src/nfl2k5_oldschool.c`,
`src/nfl2k5_ncaa_body.c/.h`, `src/nfl2k5_ncaa_render.inc.c`) reshaped the pads offline
(`build/oldschool/sculpt_body.py` `carve()`/`taper_to_arm()`, `torso_compare.png`,
`v5_shoulder.png`) and tried three in-game routes: rewriting the shared vertex block, renderer
vertex offsets (`mods/oldschool/v7/*.delta`, applied in `fetch_attr`), and NCAA 07 mesh
replacement (`NFL2K5_NCAA_MESH=1`). Logs (`logs/coliseum6/8.stderr.log`) show the offsets
loaded and matched for the 25-joint far body, but the user saw no change; likely causes are the
62-joint close body not being matched, the game's per-player morphed buffer, or the raw-vertex
GPU path skipping `fetch_attr` (inferred, untested). Even if fixed, it is still the jersey's UVs
and texture: no lapels, shirt, tie or jacket hem.

### 3.3 NCAA 07 extractor (`tools/ncaa07-extractor`)

Exports NCAA 07 players (`.glb` on their skeleton), 1,510 animation clips (30-bone rig) and
stadiums, with a Blender bridge (`blender_addon/ncaa07_bridge`). NCAA 07 has a **coach in a
suit** (PLYDATA bank 22, materials `suit_jacket`, `suit_pants`, `suit_shirt`;
`ncaa_player.py:98-100`), not yet exported. It does not retarget to the 2K5 skeleton (the 2K5
retarget path is `custom-animations/tool/retarget_full.py`, which expects Fortnite bone names).
Useful as a second suit body (a different cut or colour), with a re-rig.

### 3.4 Studio hosts, VIPs, Crib

Berman is waist-up only (1.3). `vip1-3` are suited but 15-joint, low detail (2.3). The Crib has
no avatar (1.5). None beats `commish01`.

### 3.5 Recommendation: suited player

**`commish01` as the drafted player**, with:

* **Suit:** recolour `coat` and `pants` per player or per team (charcoal, black, navy; a tie
  in team colours) by texture swap. Mock: the right-hand figure in `mock_01_wide_walkup.png`
  (coat and trousers recoloured offline).
* **Head:** hide the commissioner's head submeshes (`face`, `glasses`, part of
  `neckmaterial`; per-material hide is bit 0 of material `+8`) and attach a head:
  (a) a coach head (`coachHeadGrp1`, 36 faces, 29-joint face rig whose `thorax/neck/head` joints
  match the body by name), chosen by skin tone and hair; or (b) the drafted player's own
  `hi_head` with his morphs and face texture (best likeness, harder: the head is drawn by the
  player renderer with morph channels; **to test**). Start with (a).
* **Body size:** scale the actor (a 6'5" tackle vs a 5'10" corner) with the actor transform;
  per-bone scale is possible in the actor renderer because it is our own skinning.
* No pad work at all: the suit body has none.

Second choice: generalise the old-school mesh injection (`ncaa_player_draw`) to put a suit mesh
on a real player in the draft scene, keeping his real head and animation. More moving parts
(skeleton capture, projection recovery, GPU VP path off) for the same picture.

---

## 4. Animations

### 4.1 Where they live (verified (disc))

* 5,198 clip resources: 4,559 SMCD (one actor) and 639 MMCD (several actors), 6,068 roots,
  nearly all 15 Hz. Each clip carries its name. Rotations are smallest-three packed quaternions
  (u32 per channel per frame); root motion is a separate trajectory stream (signed shorts x
  0.125 cm, stride 6 or 8). Decoder, sampler and same-length key writer:
  `.tmp-2k-football-mod-tools/mod_editor/core/nfl2k5_animation.py` (+ `_math.py`).
* Cutscenes are SCNE packages at outers **2502-2796** (pack 2) with a duplicate set at
  **2797-3091** (pack 3): one SMCD per actor, a `cutscene` SCNE, prop SCNEs and a `Cuts` chunk
  (198 in all). Several actors share one timeline, which answers "can two characters be driven
  in one scene": yes, that is the format.
* Gameplay locomotion (walk/jog cycles) is not in the archive: those roots are unnamed and
  embedded in the XBE (e.g. locomotion descriptor `0x50F4EC`).
* Full clip name list made for this note:
  `build/eras/draftfx/anim_names.json` (outer, chunk, kind, name, roots, channels, frames, seconds); scene names in `build/eras/draftfx/scne_catalog.json`.

### 4.2 Candidates (verified (offline) film strips: `anim_ex023_trophy_presentation.png`, `anim_walk_idle_candidates.png`, `anim_cointoss_walk.png`)

| Need | Clip (identity) | Skeleton | Length | Notes |
|---|---|---|---|---|
| Walk on stage | `fpcoinrunhomeplayer01` (archive:2761/1), `fpcoinrunawayplayer01` (2760/1) | player | 6.85 s | captains walking out to the coin toss; trim one stride and loop, or play the walk part once |
| Idle | `anthemstandplayer` (2514/3), `cointossloopplayer01` (2530/1) | player | 7.0 / 6.48 s | standing still with small weight shifts |
| Hold something up, overhead | `ex021player02` (2723/2) t~3.3 s (helmet overhead), `ex018player04` (2722/4) t~5.6 s (championship paper prop `propschamppaper`), `ex023player01` (2725/3) t~8.6 s (MVP trophy, one arm) | player | 4.4-9 s | two-hand raise frames found by a scan of every 23-channel cutscene clip (hands 35-80 cm apart, above the head) |
| Handshake with the commissioner | `fphandshakeplayer` (2763/7), `cointossshakeplayer01..04` (2531/x) | player | 4.0 / 6.85 s | a two-actor handshake exists in the coin toss |
| Commissioner gestures | `ex023player0x`, `ex023commish01` (map unproven) | player / ? | 9 s | |
| Coach as announcer | `ex023coach01`, `focuscoachcoach01`, `ANM_COACH_*` | referee | | coach body only |
| Applause | `ANM_GETUP_HAPPY_FRONT_CLAPPING` (pack 3) | player | | for a crowd or the commissioner |
| Tunnel run (not a walk) | `fptunnelplayer` (2768/18) | player | 7.47 s | 11 m run with arm pumps |

A chest-height, arms-forward "show the jersey to the cameras" hold does not exist on disc.
Options: (1) use the overhead hold (it reads as "holds up the jersey"); (2) author one clip with
the proven custom-animation pipeline (`custom-animations/tool/retarget_full.py` ->
`build_clip.py`, four clips already installed in game); (3) in the actor renderer, override
the arm joints procedurally (two-bone IK to hand targets at chest height). (3) is cheapest once
the actor renderer exists, because it is our own skinning.

### 4.3 Recommendation: animation

Idle `anthemstandplayer`; walk-up = `fpcoinrunhomeplayer01` with its trajectory (or a scripted
straight path with the stride looped); handshake `fphandshakeplayer`; hold = `ex021player02`
frozen near 3.3 s (overhead) or a procedural chest-height hold. Blend between clips with a
0.2-0.3 s quaternion crossfade in the actor renderer.

---

## 5. Jersey prop

### 5.1 Art (verified (disc))

* In-game kit `<code>h<style>.iff` / `<code>a<style>.iff` (ERA-LOGOS.md section 1; Lions code
  `09`): TSET torso **512x256** (left half front, right half back, with collars and the
  "LIONS" chest wordmark), sleeves 128x128, pants; TXTR jersey digits `48`..`57` ('0'..'9',
  64x64), helmet digits `hn48..57`, arm digits `an48..57`, `names` (1024x32 name-plate atlas),
  `logo`, `chiclet`. Decoded offline for this note: `build/eras/draftfx/uniform_09h0/`
  (`tset01_512x256.png` torso, `txtr014_49.png` digit 1).
* Team Select card `unif_h09_<style>` (`logos.iff`/`logos.cdf`, 256x256) is a rendered jersey
  with a helmet in front: good for a 2D overlay card, not for the prop.
* The style comes from the team's uniform style (team `+0x192`), which the Eras 1980 staging
  already sets per era (ERA-LOGOS.md).

### 5.2 Prop

* MVP: a flat jersey quad (two triangles, alpha-cut silhouette) between the hand joints, back
  half of the torso atlas plus the digit(s) of the rookie's number and the name from the
  name-plate atlas. `build/eras/draftfx/jersey_prop_lions_1.png` is the composed texture;
  `mock_02_jersey_hold.png` shows it held overhead.
* Better: a small cloth-like grid (e.g. 6x8 vertices) with sagging corners and short sleeves,
  still one texture. Attach two top corners to `lhand`/`rhand` each frame.
* The draft custom was a jersey with the pick number ("1") or the player's number and name.
  Both come from the same atlases.

---

## 6. Text, graphics, video

### 6.1 HTML presentation overlay (verified (code))

* Packages: `mods/presentations/<pkg>/mod.json` + html/css/js; shared runtime
  `mods/presentations/_runtime/nfl2k5-presentation.js` (`window.NFL2K5.onState/onEvent`).
  Virtual hosts (`src/presentation/webview2_host.cpp:318-335`): `presentation.local`,
  `teams.local`, **`erateams.local` (`mods/eras/teams`)**, `portraits.local`. Era logos made by
  `tools/eras/extract_era_logos.py` are already reachable.
* Compositing: transparent page, captured as premultiplied BGRA and drawn as one alpha quad on
  the 16:9 area. Full-screen card precedents: NBC_SNF `#snf-intro` matchup open
  (`broadcast-scenes.js:39-68`), player profile, pregame strips.
* **Blocker 1: front end.** The host only runs when
  `match_active && custom_active() && pkg.html` (`src/nfl2k5_presentation.cpp:4026`), and is
  stopped otherwise. A "front-end card" mode is needed: start the host on demand for the draft
  show, skip the match gate, stop it when the show ends.
* **Blocker 2: no C entry for custom events.** `PresentationHost::post_event(json)` exists
  (`presentation_host.h:71`) but is reachable only through the file-static `html_post()`
  (`nfl2k5_presentation.cpp:2927`); the public `nfl2k5_broadcast_event()` takes a fixed enum.
  Add e.g. `extern "C" void nfl2k5_presentation_post_json(const char *json)`; packages already
  accept arbitrary event names. Test hook today: `NFL2K5_PRES_EVENT=NAME`.
* Cards for the show: `DRAFT_PICK_START` (team on the clock, clock), `DRAFT_PICK_ANNOUNCE`
  ("With the 1st pick in the 1980 NFL Draft, the Detroit Lions select..."), `DRAFT_PICK_CARD`
  (name, position, school, photo/portrait, college stats), `DRAFT_BOARD` (round summary). Mock:
  `mock_04_overlay_pick_card.png` (1980 #1 overall: Billy Sims, RB, Oklahoma, to Detroit).

### 6.2 Highlights video

* No package uses `<video>` yet. WebView2 plays H.264/AAC MP4 normally (runtime-dependent), and
  local files load through the virtual hosts, so a highlight window is plain HTML once the
  front-end mode exists: e.g. `mods/eras/draft/highlights/<prospect>.mp4`, user-supplied (no
  invented footage). Mock: `mock_05_overlay_highlight_card.png`. **To test:** codec playback
  and frame pacing inside the capture path.
* Alternative: the native Media Foundation player used for `presentation.json`
  `"pregame_video"` (`nfl2k5_presentation.cpp:1352-1437`): opaque, full screen, letterboxed,
  and it takes priority over the HTML image in the single HUD slot. Fine for a full-screen
  highlight reel between picks; no overlay on top of it.
* Without user clips: a "college card" with the portrait, school logo and stats (all HTML).

---

## 7. Recommended route, staged plan, effort and risk

### Route per element

| Element | Route | Why |
|---|---|---|
| Stage | `draft_menu` copied into an Eras pack, drawn by the Eras office runtime, desk/monitors/PDA/static figures hidden, code-driven camera | proven copy-and-draw path; the room is the game's own draft stage |
| Podium | `podium` + `Microphone` from the trophy cutscene (o2725c0), placed stage-left | the only podium on the disc |
| Commissioner | `commish01`, idle player clip | real suited, skinned model; player skeleton |
| Suited player | `commish01` with per-player coat/trouser recolour and a swapped head (coach head library first, own `hi_head` later) | no pads by construction; any player clip drives it |
| Walk | `fpcoinrunhomeplayer01` (+ trajectory or scripted path) | captains' walk-out, player skeleton |
| Jersey hold | jersey quad/cloth from the team kit atlases, attached to the hands; pose `ex021player02` (overhead) or a procedural chest-height hold | art exists per team and style |
| Overlay | HTML package `ERAS_DRAFT` with pick card, board and highlight `<video>`; new front-end mode + `post_json` C entry | layout freedom, era logos already served |
| Options | Eras settings row "Draft Show: First Round / Your Picks / Off" | |

### Stage 1, MVP: stage + camera + cards + idle commissioner

1. `build-office-pack.py`: add `('SCNE','draft_menu')` and the trophy-cutscene `cutscene` SCNE
   (or write `erasdraft.iff`), `--check` against the disc.
2. A "Draft Show" screen in the Eras menu (Front Office pattern: draw on message 7), loading
   the draft room under its own context, hiding the war-room desk and figures, and running a
   code-driven camera script per pick: wide establishing shot -> podium close-up -> stage.
3. Presentation: front-end mode + `nfl2k5_presentation_post_json`; package `ERAS_DRAFT` with
   the "on the clock" and "With the Nth pick..." cards and a board summary.
4. Commissioner: the actor renderer, minimal version (one actor, one looping clip, CPU skinning,
   textured, unlit or vertex-lit), drawn after the room in the same camera. Fallback if the
   actor renderer slips: no figure, podium only (the cards carry the announcement).
5. Options row and flow: First Round = every pick of round 1 with a short sequence (6-10 s per
   pick, "skip" on A/Start); Your Picks = jump straight to the user's picks (others fill the
   board); Off = the plain draft screen.

Effort: about 2-3 sessions (pack + screen + camera 1; overlay front-end mode + package 1; actor
renderer minimal 1, plus test time). Risks: the actor renderer's integration with the D3D11 path
(depth sharing with the scene, projection from our own camera, the GPU vertex-program path:
the old-school renderer had to disable it); WebView2 start-up time in the front end (it was
deliberately kept out of Press Start); the draft show needs the Eras draft data (order,
prospects), which is a later Eras milestone ("Draft" in the approved order), so the MVP can run
on a test class first.

### Stage 2: suited player walk-up

1. Second actor (`commish01` again), per-player coat/trouser colours, head swap from the coach
   head library (hide the commissioner's head submeshes, attach `coachHeadGrp1` by joint name).
2. Clip sequencing with crossfades: enter from the wings (walk), handshake with the
   commissioner (`fphandshakeplayer` + the commissioner on `cointossshake*` or a mirrored
   copy), turn to camera.
3. Camera: follow shot along the stage, then two-shot at the podium.

Effort: about 2 sessions. Risks: the coach head attaching to a different body (bind positions
differ; the offline preview did not attach it, so this needs care), clip foot sliding without
proper root motion (use a scripted path matched to the stride length), likeness (a library head
is not the real player; the own-`hi_head` route needs the player renderer's morph path, about
2+ more sessions and uncertain).

### Stage 3: jersey hold (+ highlights)

1. Jersey prop mesh, texture composed at pack-build or run time from `<code>h<style>.iff`
   (torso back half + number digits + name-plate letters), attached to the hand joints.
2. Hold pose: `ex021player02` overhead first; then a procedural two-bone IK chest-height hold
   (or one authored clip through the custom-animation pipeline).
3. Photo-op beat: player and commissioner side by side holding the jersey, flash-bulb effect
   in HTML.
4. College highlight: `<video>` window or full-screen MF reel from user clips, else the HTML
   college card.

Effort: about 1-2 sessions. Risks: the jersey texture style per era (uniform style table must
be the era's; ERA-LOGOS already handles 1980), cloth look (keep it simple), video codec/pacing
in the capture path.

### Overall risks and unknowns

* **Native vs PC-side actor drawing.** A one-day spike should try the game's own path first:
  load `commish01` from the pack, build its pose with the game's sampler for a cutscene clip,
  palette `sub_00022C00`, draw `sub_00021860`, inside the draft screen's message 7. If that
  works, actors get the game's lighting for free. If not, the PC-side renderer is the plan of
  record.
* **Licensing:** all models and textures are retail data read from the player's disc at
  pack-build time, never shipped (same rule as the office pack). Era faces (e.g. Rozelle) must
  come from the user's photos.
* **Isolation:** the show must never touch Franchise's draft (`sub_00326AD0`, `FRANCHISE1`).
  The copy loads under an Eras context, as the office does.
* **Performance:** two CPU-skinned actors of 2,337 vertices plus a 8.6k-vertex room are tiny.

---

## 8. Files made by this task

* `logs/draftfx/` (renders, local; logs are gitignored): `draftfx-sheet.png`;
  `ingame_58_retail_draft_room.png` (copy of the existing retail reference);
  `draftroom_*.png` (stage, fly-in, static figures, retail camera stops);
  `commish01_trophy_cutscene.png`; `coaches_sideline.png`; `sideline_extras.png`;
  `jersey_and_suits_extra.png`; `espn_studio_berman.png`; `player_body_pads.png`;
  `posed_commish_coach.png`; `posed_coach.png`; `posed_hold_up_candidates.png`;
  `anim_ex023_trophy_presentation.png`; `anim_walk_idle_candidates.png`;
  `anim_cointoss_walk.png`; `mock_01..05_*.png`.
* `build/eras/draftfx/` (exports, local, retail pixels, gitignored): glTF + textures for
  `commish01`, `cutscene` (o2725c0), `propsmvptrophy`, `coach` (o348/o355/o372), `vip1-3`,
  `media1`, `security`, `coach_asst`, `sideline_player`, `cameraman01`, `hi_body`, `lo_body`,
  `hi_head`, `framed_jersey`, `sc_studio`, `bermanintro`, `sc_intro`, Crib `intro`,
  `table_set`, Crib trophies; `logos/` (all 1,585 `logos.cdf` cards); `uniform_09h0/` (Lions
  home kit); `jersey_prop_lions_1.png`.
* Offline scripts (`build/eras/draftfx/scripts/`, local): `rmodel.py` (model turntables),
  `posed.py` (CPU skinning with SMCD clips), `film.py` (clip film strips). Regenerate exports
  with `nfl_resource_scan.py` + `build/eras/office/scripts/export.py <dir> build/eras/draftfx
  o2725c7 o2725c0 ...` (FRANCHISE-OFFICE-SCENE.md section 7).

## 9. Not done / blocked

* No in-game capture of the trophy cutscene or of a skinned actor in a menu: that needs either
  winning a Super Bowl in a test run or code changes (out of scope: no `src/` edits). The
  retail draft room frame already existed.
* `ex023commish01`'s 25-channel map is unproven (player clips were used for him instead).
* The coach head (`coachHeadGrp1`) did not attach to a body in the offline preview; its
  in-game attachment logic was not traced.
* The NCAA 07 suited coach (PLYDATA 22) was not exported.
* No Mel Kiper or other studio host model exists; Berman is waist-up only.

# NCAA 07 to Blender

Extracts NCAA Football 07 (Xbox) players, animations, stadiums and fields from
your own game files, straight into the Blender you already have open. The goal
is porting NCAA 07 content to ESPN NFL 2K5.

Started by ChatGPT on 2026-10-14 (archive/TMDL/texture readers, stadium and
field assembly, the skeleton). Fixed and extended in this repository.

## Run

`Open_NCAA07_Tool_v3.cmd` (in `H:\NCAA07_Extractor_Work`) starts the packaged
app; from source: `python ncaa_app.py gui`. Build the app with `build_app.ps1`
(needs `tools\quickbms.exe`, which is not kept in git).

1. **Install Blender add-on** once. Every Blender you open then listens for the
   extractor (sidebar > **NCAA 07**).
2. Pick a category, **Load models**, select, **Export selected**. With
   "Send to Blender after export" on, the models appear in your open Blender.
   If no Blender is open, one is started once and reused after that.

## Players

Every player model is exported on its skeleton (75-bone bodies with their face
model, 32-bone low-detail and throwback bodies, the coach in his suit). The
configured player is what you see: one helmet, facemask, hand pose, shoes and
so on, chosen with the game's own Swap tables. Every alternative (4 helmets,
9 hair styles, 14 facemasks, 10 hand poses per hand, elbow/wrist/shoe options,
shadow and effect meshes) is kept, tagged, and filed in a hidden
`<player> Variants` collection. In Blender's sidebar, **NCAA 07** swaps them:
pick Afro and the helmet, facemask and their companions go away.

Textures are the game's: player models only name materials, and the game binds
them at runtime from the PLYDATA gear bank, a team's UNIFORM entry (the
"Uniform" number in the app), PLADATA player art (faces, hair), the low-detail
family banks and the coach bank. Numbers and decals are lifted 3 mm off the
jersey so they don't z-fight; decals use clamped (not repeating) textures.

Models are listed by what they are ("Player, 1940s-50s leather helmet (high)",
"Coach", "Face", ...) with the raw file name in brackets, and export as
`<name>_<file>.glb`. Old-era bodies use the throwback uniform in the
**Historic** box (default 0x2A0, 1920s Notre Dame: gold leather helmet, blue
jersey, khaki pants); **Pick uniform...** shows both kinds as thumbnails.

## Animations

The body animation codec is decoded from the game's own code in default.xbe
(see `ncaa_anim.py`: block loader 0x37AF0, frame fetch 0x1D8CF0, codec
0x1C65B0, bit unpacker 0x1C0830, root motion 0x1C66D0). Each frame has 90
channels: the local rotations of the 30 body-rig bones, in the skeleton's own
Euler convention, plus root position and heading in its header.

The **Animations** category lists the 17 groups (1,510 clips) by their tags:
tackles, passes, fumbles, celebrations, coaches, referees, injuries, stances.
Exporting a group gives the player with every clip as a Blender action;
mirrored clips are named `_M`.

### Playing the animations in Blender

1. In the app: **Install Blender add-on** (once; again after updating the app).
2. Pick the **Animations** category, **Load models**, select a group (e.g.
   tackles), **Export selected** with "Send to Blender after export" on.
3. In Blender, click the player's skeleton (the armature) to select it.
4. Press **N** in the 3D view and open the **NCAA 07** tab.
5. Click a clip's name (the play buttons under "Animations") to play that
   clip, or press **Line up all clips**: every clip goes one after another on
   an NLA track and Space plays the whole group.
6. By hand: Dope Sheet > Action Editor; the action drop-down lists every clip.

## Stadiums and fields

Every stadium exports complete, as one GLB with **Stadium**, **Field** and
**Props** (collections in Blender), using the field the game builds for it
(`ncaa_stadium.py`): league.dat TEAM/STAD tables; xbe 0x401760 `select TGID,
TMFL, TL35, TEZ1, TEZ2 from TEAM where SGID = stadium` gives midfield
0x4c+TMFL, end zones 0x445+TEZ1/TEZ2, 35-yard logos 0x281+TL35; 0x401ae0
picks the turf from STAD.SFTY. Goal posts (STAD.SGPT), pylons, benches and
tables are added. **Complete stadium to Blender** sends one straight over.

## Still open

- Bench/table spots are a standard layout, not read from the game; SGPT 1 =
  white posts is inferred. Bowl sites get a plain field. No sky or wear overlay.
- Low-detail players' team colours come from a runtime palette, so their
  textures look dark; hand-finger animation (17-bone hand rigs) is not
  exported yet; animation playback speed assumes 30 fps.
- The partial-cluster skinning matches the bind pose; exact in-game blending
  of partially weighted vertices is an approximation.

## Tests

`python -m unittest discover -s . -p "test_*.py" -v`; `work/test_bridge_player.py`
and `work/render_anim.py` render players and animations headlessly in Blender.

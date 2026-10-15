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
jersey so they don't z-fight.

## Animations

The body animation codec is decoded from the game's own code in default.xbe
(see `ncaa_anim.py`: block loader 0x37AF0, frame fetch 0x1D8CF0, codec
0x1C65B0, bit unpacker 0x1C0830, root motion 0x1C66D0). Each frame has 90
channels: the local rotations of the 30 body-rig bones, in the skeleton's own
Euler convention, plus root position and heading in its header.

The **Animations** category lists the 17 groups (1,510 clips) by their tags:
tackles, passes, fumbles, celebrations, coaches, referees, injuries, stances.
Exporting a group gives the player with every clip as a Blender action
(sidebar > NCAA 07 picks one); mirrored clips are named `_M`.

## Stadiums and fields

All 623 stadium models export with their textures. **Build field + stadium**
assembles the FLDDATA turf, lines, numbers, midfield logo, both end zones and
goal posts with a selected stadium, in your open Blender.

## Still open

- Field team art (midfield logo, end zones) is mapped for the first four teams
  only; the rest need the game's team table.
- Turf colour is an editable tint, not the game's runtime palette.
- Low-detail players' team colours come from a runtime palette, so their
  textures look dark; hand-finger animation (17-bone hand rigs) is not
  exported yet; animation playback speed assumes 30 fps.
- The partial-cluster skinning matches the bind pose; exact in-game blending
  of partially weighted vertices is an approximation.

## Tests

`python -m unittest discover -s . -p "test_*.py" -v`; `work/test_bridge_player.py`
and `work/render_anim.py` render players and animations headlessly in Blender.

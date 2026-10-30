# Team Studio

Launch `E:\NFL2K5-PC\Team Studio.cmd` or `Team Creator.cmd`. The classic Tk editor remains at `Team Creator (Classic).cmd`.

This is a local application. It binds only to 127.0.0.1, has no online dependencies, and loads your own disc assets. Keep its launcher window open while using it. A complete archive under `original/disc` is preferred; if that directory is incomplete, it uses the complete `original` archive as a whole, without mixing volumes.

## Use it

1. Set the team identity, palette and transparent PNG logo in **Team identity**. Installed custom teams can be opened as project copies.
2. In **Uniform lab**, choose the base team and any disc uniform variant. Drag the actual player mesh to rotate and scroll to zoom. Home and away designs are separate.
3. Click any texture to paint, erase, flood-fill, sample colors, undo, import a same-size PNG or download the texture. **Apply to uniform** saves that file/texture override in this project. **Clear custom edit** restores its connection to design colors. The library includes clean/mud jerseys, pants, sleeves, equipment, helmet atlases, number and name sheets, logos and bump textures. Unsupported write formats remain extractable and are marked non-editable.
4. **Extract all team textures** exports the base team's discovered uniform and crowd files as PNGs plus a texture manifest. It is an original-source export, not a build of your design.
5. **Roster** edits 53 players, depth order, names, numbers, positions, body/face IDs and all 28 ratings. Import/export creator-layout CSV, roster JSON or use the existing Madden/NCAA EA DB importer. Overall is an editing aid, not a promise about the game's calculated overall.
6. **Home stadium** assigns a disc venue by exact name or an installed stadium pack. It can make a separate named copy of an installed venue with location, capacity and surface/indoor settings. This is a venue-pack clone, **not a stadium geometry modeler**.
7. **Build & share** saves a portable project ZIP or builds the team. Build staging is separate from installed teams; a successful replacement backs up the old team under `outputs/team-studio/backups`. Restart the game to load it. Keep backups until the team has been tested in-game.

Projects, artwork and imported rosters stay under `tools/team-creator/projects`. Team packs use the existing `mods/teams/<folder>` architecture and their own asset code. Stadium copies use `mods/eras/stadiums`. Original game archives are never patched.

## Preview limits

The preview uses the real hi_body/hi_head meshes, original UVs and source bind-anchor alignment. Detailed uniforms sample their jersey/pants/sleeve TSET images. Optional equipment/helmet variants are hidden to avoid overlapping duplicate parts. It is a static bind-pose inspection view, not the game's animated renderer, complete face system, or a guarantee of pixel-identical in-game lighting and lettering. Edited equipment that is not selected on the preview model can still be extracted and written into the pack.

P8 images are quantized back to the game's palette. Clean/mud pairs share indices: a clean edit regenerates the mud palette; a mud-only edit refits its palette while preserving clean indices. Wrong-size images are rejected rather than changing the original UV layout. The builder reads the encoded output back before accepting a texture change.

The old creator's generic hue remapping is still used for other team assets. Detailed per-part painting is enabled only for recipes with `studio_version`, preserving legacy recipes' behavior. Source logos on sleeve art may require painting or PNG replacement for a fully original uniform.

## Verification, October 29, 2026

- Five real-disc tests passed: texture discovery, TSET replacement/decompression/readback, unchanged unrelated chunks, clean/mud isolation, malformed dimensions/paths, and actual model topology/head anchor.
- A complete isolated verification pack was built: 24 uniform/crowd IFF files, 53-player CSV, roster template and logo cache. Its custom jersey was decoded from the built IFF and matched the expected pixels. Original source IFF hash remained unchanged.
- Build receipt: `outputs/team-studio/tests/build-result.json`.
- Browser checks: real mesh displays, roster edits update, texture brush paints and applying the texture persists a project asset and updates the preview.
- Full game testing: **NOT RUN**. No game executable, saves or existing user team packs were changed by the tests.

Run checks with the Studio runtime:

```powershell
& "$env:LOCALAPPDATA\Programs\2K5-Mod-Studio\runtime\python.exe" tools/team-creator/test_studio.py
& "$env:LOCALAPPDATA\Programs\2K5-Mod-Studio\runtime\python.exe" tools/team-creator/test_studio.py --build
```

The second command builds only into `outputs/team-studio/tests`; it does not install a team.

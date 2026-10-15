# NCAA 07 to Blender

Version 2 adds source FLDDATA field assembly and a configured, textured player
with three editable meshes on one source skeleton. See `outputs/fixes/READ_ME_v2.md`.
The main app now has **Build field + stadium** and **Build clean Alabama player**
buttons, producing packed `.blend` and `.glb` files through the installed Blender.

Windows desktop model browser and GLB exporter, validated against the user's NCAA Football 07 archives. Start with `outputs/START_HERE.md` and `outputs/VERIFIED_FORMATS.md` for the current guide and complete format status. The packaged executable includes its Python runtime.

## Run

```powershell
python .\ncaa_app.py gui
python .\ncaa_app.py export "H:\NCAA07_Extractor_Work\stadiums_raw" .\outputs\stadiums --filter ALBAMA
python .\ncaa_app.py export .\outputs\plydata_extracted .\outputs\players --rig-preview
python .\ncaa07_tool.py scan "H:\07\NCAA Football 07  (axekin.com)\data" --report .\work\scan_report.json
python .\ncaa07_tool.py extract "H:\07\NCAA Football 07  (axekin.com)\data" .\outputs\extracted
python .\ncaa07_tool.py quickbms "H:\07\NCAA Football 07  (axekin.com)\data\PLYDATA.DAT" .\outputs\plydata --quickbms C:\tools\quickbms.exe --script C:\tools\madden_terf.bms
```

The main app loads or decompresses player, stadium and stadium-prop archives, searches source model names, exports GLB and opens exports in Blender. `ncaa07_tool.py` remains the lower-level archive inspector; `ncaa_blender_tool.py` provides an additional source-only browser with individual mesh selection. `Launch.cmd` opens the main app from source. Source use requires Python with Tkinter; experimental rigs additionally require NumPy. Stadium decoding uses the standard library. The included QuickBMS runtime handles game compression.

## Evidence from the supplied files

Verified in the actual files at `H:\07\NCAA Football 07  (axekin.com)\data`:

- All sampled asset containers begin with `TERF` and expose a `DIR1` directory marker.
- `Geom` node/stream tables, positions/normals, UVs, colors and triangle-strip indices are decoded and bounds-checked.
- All 623 full stadium models exported with embedded textures and no decoder failures. All 26 player geometry models and 46 stadium props also exported.
- `PLYDATA.DAT` is verified to use COMP algorithm 5 (`ea_madden`). After decompression, the supplied archive produces 26 TMdl models, 8 MMAP texture archives, and one SEVT payload. Player TMDLs contain a verified `Skel` block with a bone count and 16-byte bone-name table; the sample player contains 75 named bones.
- `ANIMDATA.dat` decompresses into 18 `.vap` animation payloads plus associated `.sdb`, `.1lk`, `.nfc`, `.trg`, and `.app` resources. Their association with animation data is verified from the extracted archive layout, but their track/keyframe codec is still pending.
- GLB export works in Blender 5.2. A 75-bone experimental player skin is editable, with provisional bind rotations/blending explicitly labeled. Animation export and native FBX export are pending.

## Status

Implemented: Windows executable, responsive model browser, read-only game extraction, GLB export, embedded stadium textures/materials, source skeleton metadata, optional experimental skins, and Blender opening. Eight real-fixture/export tests pass.

Pending: exact player partial-cluster deformation/bind conventions, sparse morphs, runtime player texture selection, compressed animation keyframes/timing, source shaders, stadium field/prop assembly, native FBX export.

## Verified versus pending

See `outputs/VERIFIED_FORMATS.md`. Detailed skeleton, pose/seam, animation and texture evidence is preserved in the source ZIP's research notes. Preview GLB skin mechanics are tested independently of the still-unverified game deformation model.

Run `python -m unittest discover -s . -p "test_*.py" -v` for the integration checks. Fixture-specific tests skip when their local game files are unavailable. No game files are included in the source-code distribution.

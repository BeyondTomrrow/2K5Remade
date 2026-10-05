# NFL 2K5 native C reconstruction handoff

## Goal

Build a standalone Windows C/C++ `NFL2K5_Rebuild.exe` from the user's legally extracted Xbox game files. Do not use xemu, do not execute `default.xbe`, and do not download or redistribute copyrighted game data. The `default.xbe` is available only as a behavior/reference source; generated C is also reference material.

## Current working result

`E:\NFL2K5-PC\build\Release\NFL2K5_Rebuild.exe` is a standalone Win32 C executable. It mounts the local extraction and now displays an original NFL/Players Inc. legal-page texture decoded directly from `original\vc_53450030\0`.

Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\build.ps1
```

Run:

```powershell
E:\NFL2K5-PC\build\Release\NFL2K5_Rebuild.exe
```

Export the verified texture:

```powershell
E:\NFL2K5-PC\build\Release\NFL2K5_Rebuild.exe --export-legal
```

It writes `analysis\legalpage.bmp`. It was converted to `analysis\legalpage.png` and visually verified: NFL + Players Inc. legal artwork.

## Important source files

| File | Purpose |
|---|---|
| `src\rebuild\native_main.c` | Win32 window, local-game mount, GDI display, `--export-legal`. |
| `src\rebuild\legal_texture.c` | Working title-specific HITX P8 reader for the legal-page asset. |
| `src\rebuild\legal_texture.h` | Decoder public declaration. |
| `tools\asset_index.c` | Read-only archive index exploration; outputs `analysis\archive-index.csv`. |
| `config\game.cmake` and `src\main.c` | Separate earlier Xbox recompilation attempt; do not merge it into the native reconstruction. |
| `analysis\disasm\functions.json` | Original XBE function map. |

## Extracted data

- `original\default.xbe`: 11,948,032 bytes, North American retail, title ID `0x53450030`.
- `original\vc_53450030\0` through `F`: 16 game archives, total roughly 6.3 GB.
- All original files are user-owned local input; preserve them and treat them read-only.

## Archive findings

Archive `0` header:

- byte `0`: entry count `4323`
- byte `8`: archive bank count `16`
- byte `12`: 16 little-endian sector counts; each count × 2048 exactly equals the matching archive file length.
- entry table begins at byte `156`; every record is 12 bytes.
- records look like: `u32 identifier`, `u32 packed_size_flags`, `u32 sector_address`.
- sector addresses are in a stream concatenating the 16 files. Map an address by subtracting each preceding file length until inside a bank.
- candidate payload size is `packed_size_flags & 0x0FFFFFFF`.
- `tools\asset_index.c` reports 4310 contained records and 13 unresolved ones. Treat this format model as exploratory; it needs cross-bank handling and validation.

## Verified HITX texture

Archive record 0:

- identifier `0xEDD549BD`
- bank `0`
- offset `53248`
- starts `HITX`
- `TXTR` chunk begins at relative `44`
- UTF-16LE asset name: `legalpage`
- 32-byte container prefix, then a 128-byte HITX header.
- Xbox texture format word at relative `96`: `0x09910B29`.
- It is P8 indexed, 512×512. Texture indices begin at relative `160` and are 262,144 bytes.
- Palette follows indices and contains 256 BGRA32 colors.
- Indices are Xbox Morton-swizzled. `morton()` in `legal_texture.c` is verified visually.

The next HITX block begins at record-relative `0x404A0` (the previous scan observed a signature around `0x404A0`; confirm offsets before generalizing). Generalize the decoder only after checking each chunk's size and headers.

Known useful archive labels from `analysis\archive-index.csv`:

- record 0: `legalpage`
- record 6: `logos.cdf` and `ports/us/bin/xbox/nfl/`
- record 3103: `logo_s05_2`

## Recommended next task

1. Add a safe generic HITX scanner for record 0 that locates every `HITX` block using the header's length and validates `TXTR`/dimensions/format.
2. Export each valid P8 texture to `analysis\textures\` with an identifier-derived name. Do not overwrite `legalpage.bmp`.
3. Add left/right navigation in `NFL2K5_Rebuild.exe` to display the decoded textures.
4. Look for original menu/title textures, then rebuild menu navigation in native C. Do not label custom placeholder UI as original game UI.

## Current limits

This is an asset viewer and native foundation, not yet the 2K5 game or menus. There is no renderer for models, game logic, audio, controller integration, or recreated original menus yet. No emulator is used by the reconstruction executable.

## Verification

After changes, run the build script and `NFL2K5_Rebuild.exe --export-legal`. Confirm the exported image remains readable and displays NFL/Players Inc. legal artwork; that protects the working decoder while it is generalized.

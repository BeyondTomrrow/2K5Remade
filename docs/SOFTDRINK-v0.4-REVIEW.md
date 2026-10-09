# SOFTDRINK 2K28 v0.4 review build

Built: 2026-10-07

This package is for technical review of the native Windows build that runs the SOFTDRINK 2K28 v0.4 XBE. It does not include ESPN NFL 2K5 game files, the SOFTDRINK patch, or an ISO.

## What changed in this build

The previous build crashed after the Berman segment in Play Now because an injected SOFTDRINK routine at `0x014DD6E6` was converted as an empty stub. The real routine restores registers and continues into the retail animation path. This review build converts that routine and related injected state-transition helpers.

The program now exits after a guest crash unless its explicit synthetic-display diagnostic is enabled. This prevents background audio and timer threads from making a crashed game appear to still be running.

## Required test environment

- Use the SOFTDRINK 2K28 v0.4 XBE with SHA-256 `5a9dc534b50c7f13b5124bfff9e39c99f96f62be4cc1de33309895c330c75f29`.
- Place `NFL2K5.exe` and `NFL2K5.pdb` in that pack's `native` folder.
- Use the Direct3D 11 hardware renderer. Do not use software rendering.
- Use 128 MB guest RAM. This is required by the pack's expanded assets.

For Xemu, load a legally made patched ISO and set **Settings > System > Memory** to **128 MiB** before starting the game.

## Test order

1. Boot to the main menu and press Start.
2. Start a normal Play Now game. Let Berman finish and verify that team selection, stadium selection, coin toss, and live football load.
3. Select MyCareer and verify it opens a usable screen rather than a black screen.
4. Check Demo Mode and Situation Mode for input and normal progression.
5. View a player close-up or roster model and check faces, helmets, and bodies for missing triangles or black holes.
6. If a crash occurs, keep the matching `.stderr.log`; it contains the exact guest function and call stack.

## Known open issues

This build specifically targets the confirmed Play Now post-Berman crash. MyCareer, mode flow, and face geometry still require visual verification and are not claimed fixed until they pass the checklist above.

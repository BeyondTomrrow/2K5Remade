# Steam Deck test results — October 16, 2026

Tested published Windows repack: `v0.6.1-hotfix`. Proton Experimental:
`1791989105 experimental-11.0-20261014-x86_64`. Original Xbox disc files were
supplied by their owner. The initial installation used a new, empty Proton prefix.
No game executable patch, franchise addon or ray tracing was used.

## Working configuration

- Proton `runinprefix`, launched through a separate non-Steam shortcut.
- Microsoft x64 `d3dcompiler_47.dll` with a process-local native override.
- Wine's OpenGL renderer, explicitly selected; DXVK gameplay is not verified.
- Stock **ESPN (Original)** presentation, borderless 1280×800 output, 2× scale.

The owner confirmed exhibition gameplay and working controls. A captured frame
shows the original ESPN scorebug with score, possession, down/distance, game clock
and play clock. Screenshots and game assets are not included in this PR.

Recent runtime samples averaged 58.1, 59.2, 60.0 and 59.9 FPS; corresponding 1% lows
were 31.1, 33.0, 44.6 and 47.0 FPS. The separate compiler was paused while playing.
These are observations, not a locked-60 claim or a controlled same-scene comparison.

## Why the helper does this

The default Proton `run`/`waitforexitandrun` path initially failed at Xbox memory
initialization (access violation at host `0x142BF621B`, address `0xFFFF1000`).
`runinprefix` passed that startup stage. Wine's built-in shader compiler then
rejected the shader; the Microsoft compiler removed that error.

The selected NBC HTML presentation logged `CreateCoreWebView2Environment failed`
and hid the original bug. Selecting the original ESPN presentation restored the
scorebug without installing WebView2. Wine's junction command also made an empty
`disc?` directory, so the helper creates the normal Linux disc symlink first.

The repack executable remains unchanged, SHA-256:
`9a7d8da69912955019a9f539db98739202a169835985eaf35f78ac6333a3f48d`.

## Automated dependency and tests

The first working session used the owner's existing Windows SDK compiler DLL.
The helper instead downloads the standard x64 SDK cabinet from Microsoft, verifies
both cabinet and DLL hashes, and installs only the compiler dependency locally.
The downloaded DLL passed separate Windows/Proton HLSL smoke checks for `vs_5_0`
with `precise` and `ps_5_0` with `earlydepthstencil`.

Cabinet/member metadata can be cross-checked in the
[Winetricks recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks).
No Winetricks code or Microsoft binary is included in this contribution.

Eleven component tests and `bash -n` pass on macOS and the actual Steam Deck.
Run them without game assets, Steam, network downloads or paid services:

```sh
cd tools/proton-setup
python3 -m unittest -v
bash -n setup.sh
```

## Remaining acceptance

- Full fresh-install gameplay rerun using the new automated dependency download.
- Explicit save/exit/reload and LT/RT mapping regression checks for this stock entry.
- Latest source `master` is not gameplay-verified by these release-binary tests.

The script deliberately pins the tested published installer and checksum. It does
not silently replace it with an untested build or claim full gameplay acceptance.

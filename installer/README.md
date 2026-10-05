# Installer build

`NFL2K5-PC.iss` builds a Windows x64 installer with Inno Setup 7.

It packages the native executable, presentation packages, `mods\README.md`,
the default `nfl2k5_video.ini`, music-folder instructions, and documentation.
It excludes local mod packs, XBE/ISO files, debug symbols, saves, logs, and
user-supplied music or video. During
setup the player may either select an existing extracted disc directory or
select their own `.iso`/`.xiso` file. In ISO mode, Setup extracts it into an
`ESPN NFL 2K5` folder under the parent folder the player chooses.

Setup verifies `default.xbe` and `vc_53450030`, then makes an NTFS junction at
`original\disc` inside the install directory. The selected game-data folder is
never an uninstall target; if Windows leaves the link behind, it is safe to
remove that link without touching its target.

Build after a Release game build:

```powershell
& "C:\Program Files\Inno Setup 7\ISCC.exe" .\installer\NFL2K5-PC.iss
```

The installer is written to `dist\NFL2K5-PC-Setup.exe`.

To rebuild the game, installer, source ZIP, and SHA-256 manifest together:

```powershell
.\tools\build-release.ps1
```

For an unattended local test or deployment, pass the extracted-disc directory
explicitly:

```powershell
.\dist\NFL2K5-PC-Setup.exe /VERYSILENT /GAMEPATH="E:\Games\NFL2K5\disc"
```

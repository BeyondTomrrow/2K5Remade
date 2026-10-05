# ESPN NFL 2K5 Native PC

An experimental native Windows version of ESPN NFL 2K5 with modern video
settings, local music, organized saves, and selectable broadcast presentation
packages.

> **Preview release:** The game is playable, but this project is still under
> active development and has known bugs. Back up your saves before testing.

## What you need

- 64-bit Windows 10 or Windows 11
- A legally owned Xbox copy of ESPN NFL 2K5
- Either an extracted disc folder or your own Xbox ISO/XISO

The project and installer do not include or download the retail game.

## Install and play

1. Download `NFL2K5-PC-Setup.exe` from the latest GitHub Release.
2. Choose any installation folder.
3. Select your extracted NFL 2K5 folder, or select your own ISO/XISO and choose
   where Setup should extract it.
4. Launch **ESPN NFL 2K5 Native PC** from the Start menu or desktop shortcut.

An extracted disc folder must contain `default.xbe` and `vc_53450030`.

## New features

### Video settings

Press **F1** while the game is running to open the video menu. It includes:

- Windowed, borderless, and fullscreen modes
- Resolution and aspect-ratio controls, including ultrawide
- Internal resolution scaling and filtering
- VSync, brightness, gamma, crop, anisotropic filtering, and FPS display

### Broadcast presentations

Before a Quick Game, open:

**Coach Match Up → Presentation**

Choose the original ESPN presentation or an installed FOX, CBS, NBC/SNF, or
custom package. Presentation packages can provide scorebugs, team logos,
animations, contextual player statistics, and broadcast music.

### Local music

Place music you have permission to use inside the `Music` folder. Supported
formats are MP3, WAV, OGG, and WMA. Each subfolder acts as a playlist.

- Press **L3** to show **Now Playing** with the song and artist.
- Press **R3** to skip to the next song.

The Now Playing title automatically changes back to the normal menu title.

### Organized saves and imports

PC saves are mirrored into readable folders:

| Save type | Folder |
| --- | --- |
| Franchise | `saves/franchise/` |
| Rosters | `saves/rosters/` |
| Settings | `saves/settings/` |
| VIP profiles | `saves/vip/` |

To import a save, copy it into `saves/import/` and launch the game. Valid Xbox
saves are installed automatically. PS2 roster and franchise conversion uses
the optional save converter described in [docs/SAVE-LIBRARY.md](docs/SAVE-LIBRARY.md).
Always keep the original save as a backup.

## Known issues

- Some crowd, sound-effect, music, or player-voice audio can distort or stall.
- Certain cameras can show field, lighting, texture, face, or player-model
  artifacts.
- Gameplay performance may fall below 60 FPS on some systems and scenes.
- Some custom scorebug animations and contextual player-stat graphics remain
  incomplete or may appear at the wrong time.
- Save importing is still experimental, especially cross-platform PS2 saves.
- Untested game modes may freeze or crash. Please report the exact menu path,
  teams, stadium, and game situation when this happens.

## Reporting a bug

Include:

- Windows version, GPU, display resolution, and controller
- The exact steps that caused the problem
- The teams, stadium, mode, and game clock when applicable
- A screenshot or short video
- Relevant files from the `logs` folder, without uploading retail game data

## Building from source

The project currently builds with Visual Studio 2022 and the Desktop
development with C++ workload:

```powershell
.\tools\build.ps1 -Configuration Release -Game
```

To build the game, Inno Setup installer, source ZIP, and checksum file:

```powershell
.\tools\build-release.ps1
```

More technical information is available in [PROJECT_STATUS.md](PROJECT_STATUS.md)
and the documents under [`docs/`](docs/).

## Third-party software

The installer includes `extract-xiso` 2.7.1 by in <in@fishtank.com>. Its
license is included at
[`docs/third-party/extract-xiso-LICENSE.txt`](docs/third-party/extract-xiso-LICENSE.txt).

This is a community project and is not affiliated with or endorsed by Sega,
Visual Concepts, ESPN, the NFL, or any television network.

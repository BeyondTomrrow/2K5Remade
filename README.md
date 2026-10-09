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
2. Choose an installation folder. Setup runs without Administrator rights; the
   default is under your user profile, so the game can write its saves, logs
   and caches. If you pick `C:\Program Files`, run the game as Administrator.
3. Select your extracted NFL 2K5 folder, or select your own ISO/XISO and choose
   where Setup should extract it.
4. Optionally select a supported 2K5 Mod Studio `.2k5patch`. The patch file is
   validated against your clean game and paired with its exact native build.
5. Launch **ESPN NFL 2K5 Native PC** from the Start menu or desktop shortcut.

An extracted disc folder must contain `default.xbe` and `vc_53450030`.

## New features

### Video settings

Open Options and go down to Video Settings It includes:

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

Press **F2** at any time (menu or game) to cycle to the next installed
presentation; a short on-screen message names it.

### Discord Rich Presence

The game can show the teams, score and clock (or the menu you are in) on your
Discord profile. Create an application at
https://discord.com/developers/applications, copy its Application ID into
`mods\discord.ini` (`client_id = ...`), upload a picture named `nfl2k5` under
Rich Presence > Art Assets, and keep the Discord desktop app running.

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

### Native mod packages

Supported `.2k5patch` files install beside the original game. Setup checks the
clean retail hashes, reconstructs and verifies every changed file, and selects
a native executable by the patched `default.xbe` SHA-256. A mismatched or
unsupported patch is rejected instead of being launched with incompatible
code.

To add one after Setup, drop the file into the game's `mods\patches` folder:
a `.2k5patch`, or the patched `.iso`/`.xiso` disc image that 2K5 Mod Studio
built (SOFTDRINK 2K28 v0.4 is distributed as a recipe patch that only Mod
Studio can apply, so use the disc image it produces). The game installs it in
the background and shows a message when it is done; restart, then switch it
on from **Options > Mod Packs**. Turning it off returns to Original ESPN NFL
2K5. A file that could not be installed gets a `.failed` marker next to it
and a log in `logs`.

Packs built with Mod Studio's widescreen option (SOFTDRINK 2K28 has it) fill
the whole window at any aspect ratio, including 21:9 and 32:9. Setup also
prepares a **Widescreen** pack from your own game: the original ESPN NFL 2K5
with that same widescreen patch (2K5 Mod Studio, cruuz and contributors).
Switch it on under **Options > Mod Packs** for a full-screen picture with no
black bars; the HUD keeps its normal size.

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

The installer supports the `.2k5patch` format produced by
[2K5 Mod Studio](https://github.com/cruuz/2k-football-mod-tools). Optional
[SOFTDRINK 2K28](https://github.com/cruuz/softdrink-2k28) packages are supplied
separately by their authors. Credits and notices are included under
[`docs/third-party`](docs/third-party/).

This is a community project and is not affiliated with or endorsed by Sega,
Visual Concepts, ESPN, the NFL, or any television network.

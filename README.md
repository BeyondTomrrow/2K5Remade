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

### Settings (F1)

Press **F1** while the game is running (or **Back + Start** on a controller) to
open the settings. **LB / RB** (Page Up / Page Down) switch pages:

- **Video**:
  - display mode, resolution and aspect ratio (including ultrawide);
  - widescreen;
  - internal resolution up to 12x (8640x5760) and upscaling filter;
  - VSync, brightness, gamma, crop and anisotropic filtering;
  - frame rate cap, and Show FPS.
- **Controls**:
  - connected controllers, vibration, button prompts (Automatic / Xbox / PlayStation);
  - left/right stick and trigger dead zones;
  - mouse as controller.
- **Controller Buttons**: map any game button to any controller button (select
  a row and press the button to use), or step through with Left/Right.
- **Keyboard**: rebind every game button and the left stick (select a row and
  press the key).
- **System**:
  - how many backups to keep of each save, and a shortcut to open the saves folder;
  - expanded limits.

With Show FPS on, the counter also shows frame time, the 1% low, GPU and CPU
time per frame, and video memory in use.

Settings, shader caches and logs are kept in `%LOCALAPPDATA%\NFL2K5-PC`, so the
game never needs administrator rights, even under Program Files. To keep
everything in the game folder instead, put an empty `portable.txt` there.

### Controllers

Xbox controllers and PlayStation controllers (DualShock 4, DualSense, and
DualSense Edge, over USB or Bluetooth) work without extra software.

- Up to four controllers are connected to the game's four controller ports, as on
  the Xbox.
- The keyboard and mouse always control player 1.
- Vibration is sent to Xbox controllers, and to PlayStation controllers over USB.
- Button prompts follow the controller you last used. With a PlayStation
  controller, the game's button icons show Cross, Circle, Square and Triangle,
  plus Options, Share and L1/R1/L2/R2/L3/R3. To fix the prompts to one style, use
  F1 > Controls > Button prompts.

So far this has been tested with one Xbox controller. Two or more controllers,
and PlayStation controllers, are implemented but not yet tested on real hardware.
Please report how they behave.

### Expanded limits

Two limits of the original game are lifted (F1 > System, on by default):

- Create a Team offers all 82 stadiums, up from 67.
- A franchise can run 95 seasons, through 2099, up from 30.

See [docs/XBOX-LIMITATION-AUDIT.md](docs/XBOX-LIMITATION-AUDIT.md) for every
limit that was examined.

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

Each time the game saves over a save, the previous version is kept in
`saves/backups/<type>/<name>/<date and time>/`. By default the last three are
kept; F1 > System changes the number or turns backups off.

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

A pack does not need its own build. The game runs the pack's changed code
through a built-in interpreter, and everything else at full native speed.

Widescreen is built in (F1 > Video > Widescreen, on by default). It uses the
Mod Studio widescreen patch (2K5 Mod Studio, cruuz and contributors) and fills
the whole window at any aspect ratio, including 21:9 and 32:9, with no black
bars. The HUD keeps its normal size.

## Known issues

- Some crowd, sound-effect, music, or player-voice audio can distort or stall.
- Certain cameras can show field, lighting, or texture artifacts.
- Gameplay holds 60 FPS on the test system, but brief hitches remain in some
  scenes (see [docs/BENCHMARKS.md](docs/BENCHMARKS.md)).
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

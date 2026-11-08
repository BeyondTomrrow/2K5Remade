# ESPN NFL 2K5 Native — 0.8.0

ESPN NFL 2K5 (the 2004 Xbox game), rebuilt to run natively on **Windows**,
**Linux** and **Android**. It is the original game's own code, translated so it
runs on a modern PC or phone without an emulator, with modern video settings,
modern controllers, drop-in mod packs, your own music, and a new **MyCareer
home** for SOFTDRINK 2K28's MyCareer mode.

> Version `0.8.0` is a preview. The game is fully playable, but there are known
> issues (listed below). Back up your saves.

## You need your own copy of the game

Nothing from the game is included or downloaded. You need a **clean dump of
your own USA retail Xbox disc** of ESPN NFL 2K5:

- an `.iso` / `.xiso` you made from your disc, or
- the extracted disc folder (it has `default.xbe` and a `vc_53450030` folder).

The game checks the disc and only accepts the clean USA retail version.
European (PAL) discs, PS2 discs, and disc images that already have a mod
patched into them (for example a "SOFTDRINK ISO") are refused, with a message
that says why. To play a mod, use your clean dump plus the mod's `.2k5patch`
file (see **Mod packs**).

## Downloads

| File | What it is |
|---|---|
| `NFL2K5-PC-0.8.0-windows.zip` | The game for Windows 10 / 11 (64-bit) |
| `NFL2K5-Sandbox-pack-0.8.0.zip` | Sandbox, an optional add-on for Windows (see **Sandbox**) |
| `NFL2K5-linux-x86_64-0.8.0.tar.gz` | The game for 64-bit Linux (**experimental**) |
| `NFL2K5-android-0.8.0.apk` | The game for 64-bit Android phones and handhelds |
| `SHA256SUMS.txt` | Checksums of the files above |

## Install

### Windows

1. Unzip `NFL2K5-PC-0.8.0` anywhere you can write to (Desktop, Documents,
   `D:\Games` ... not `C:\Program Files`).
2. Put your extracted disc inside it so that this file exists:
   `NFL2K5-PC-0.8.0\original\disc\default.xbe`.
   If you only have an ISO, open a command prompt in the game folder and run:
   `tools\extract-xiso.exe -x -d original\disc "C:\path\to\your.iso"`
3. Double-click `NFL2K5.exe`. If Windows SmartScreen says "Windows protected
   your PC", click **More info**, then **Run anyway** (the build is not
   signed).

You need a graphics card from about the last eight years. Updating: unzip
the new version over the old folder. Your settings and saves stay.

### Linux (experimental)

**This build has only been tested in a virtual machine with software
graphics, never on real Linux hardware.** Please report how it runs.

1. You need 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12,
   Fedora 36, Arch, SteamOS 3 or later), a working Vulkan driver, and X11 or
   Wayland.
2. `tar xzf NFL2K5-linux-x86_64-0.8.0.tar.gz` and copy your whole extracted
   disc into the `original` folder inside it (`original/default.xbe`,
   `original/vc_53450030/...`).
3. Run `./nfl2k5.sh`.

The archive's `README.txt` has the details (where settings and logs go,
controllers, Steam Deck notes). Linux does not have broadcast presentation
packages, Sandbox, the Team Builder or the save converter yet.

### Android

1. You need a 64-bit Android phone or handheld with Vulkan graphics, and about
   6 GB of free space. A controller is optional: touch controls are built in.
2. Copy `NFL2K5-android-0.8.0.apk` to the phone and tap it to install. If
   Android asks, allow "Install unknown apps" for the app you opened it from.
   This version installs over earlier test builds.
3. Copy your disc image (`.iso`) into the phone's **Download** folder.
4. Open NFL 2K5 and turn on **All files access** when Android asks. The game
   finds the ISO, unpacks it into `/NFL2K5` (keep the phone plugged in and the
   app open), then boots. You can delete the ISO afterwards.

Phone settings are under **Options > Phone Settings** (touch controls, size and
opacity, Battery Saver, Pause When Leaving, Add Songs), plus **Options >
Controls** (remap, dead zone, vibration) and a touch layout editor.

## Video and renderers

- **Windows** has three renderers: **Direct3D 11**, **Direct3D 12** and
  **Vulkan** (F1 > Video > **Graphics API**; the change applies on the next
  start). Direct3D 11 is the default and the smoothest in testing.
- **Linux** and **Android** use Vulkan.
- Press **F1** (or **Back + Start** on a controller) for the settings menu on
  PC, or use **Options > Video Settings** in the game's own menu on every
  platform: Render Scale, Dynamic Resolution, widescreen and aspect ratio
  (including ultrawide), display mode and resolution, lighting and materials,
  post-processing and sharpening, upscaling filter, VSync, brightness, gamma,
  overscan crop, texture filtering, frame rate cap and a performance overlay.
  Direct3D 12 also has ambient occlusion, HDR output, FSR upscaling and frame
  generation.

**Render Scale is your choice.** It sets the internal resolution (1x is the
Xbox's own 720x480; up to 16x). Nothing in the game caps it behind your back:

- With **Dynamic Resolution Off**, the scale stays exactly where you set it.
- With **Dynamic Resolution Auto**, your Render Scale is the limit. The game
  may lower the resolution while the graphics card cannot keep up, and goes
  back up when it can, but **never above the Render Scale you set**.
- On Android, only Battery Saver, and Heat Control if you turn it on, can lower
  it further, and the Render Scale row shows it when they do.

## Controls and controllers

- Xbox controllers, PlayStation controllers (DualSense, DualShock 4) and most
  other pads work without extra software, up to four at once, plugged in and
  out while playing. Keyboard and mouse always control player 1.
- Button icons follow the pad you last used (Xbox or PlayStation), or set them
  in F1 > Controls > Button prompts.
- **Options > Controller Setup** shows your pad and lets you build a Custom
  Control layout (press X / Square to customise, swap any two buttons).
- F1 > Controller Buttons and F1 > Keyboard remap everything; F1 > Controls has
  dead zones and vibration.
- On Android: touch controls (Auto / On / Off), a layout editor, and phone
  vibration as rumble.

## Music Player

Put your own songs (MP3, WAV, OGG, WMA) in the `Music` folder; each sub-folder
is a playlist. On PC press **F1 > Music Player** to pick a folder or a song,
skip to the next song, and see what's playing. On Android use **Options > Music
Player**, and **Options > Phone Settings > Add Songs** to copy songs from the
phone. In the menus, **L3** shows Now Playing and **R3** skips to the next
song. A pick made during a match starts back at the Main Menu.

## Mod packs (SOFTDRINK 2K28 and other 2K5 Mod Studio mods)

Mods come as a `.2k5patch` file. The game installs them itself:

1. Put the `.2k5patch` in the game folder's `mods\patches` (Windows/Linux), or
   in the phone's **Download** folder (Android). Start the game once first if
   `mods\patches` does not exist yet.
2. Start the game. A few seconds after the menus appear it installs the pack in
   the background (a message shows the progress; it takes a few minutes and you
   can keep playing).
3. When it says the pack is installed and selected, quit and start the game
   again. **Extras > Mods** switches between installed packs and the original
   game; the game asks first and restarts.

**SOFTDRINK 2K28 v0.9:** use the regular download,
`SOFTDRINK-2K28-v0.9.2k5patch` (the **128 MB** build, about 1.2 GB), **not**
the "-64MB" one. Patched ISOs are not accepted: use your clean disc dump plus
the `.2k5patch`. SOFTDRINK v0.7 works too.

To play the original game while a pack is selected, start the game with
`--retail` (for example `NFL2K5.exe --retail`).

**The audible slide** (SOFTDRINK's L/R slide audible) now works for the player
controlling the quarterback.

## MyCareer home (new, with SOFTDRINK v0.9)

SOFTDRINK 2K28 v0.9 has a MyCareer mode. This release adds our own layer on top
of it (SOFTDRINK's files are not changed): instead of a menu, your career starts
in **your apartment**, a 3D room you walk around in.

Start it from **Game Modes > MyCareer**: start a career (Undrafted Free Agent is
the quickest), go through the MyPlayer pages, sign, and you are home.

- **Walk around**: left stick or D-pad up/down to walk, right stick, D-pad
  left/right or LB/RB to turn. **A** uses what you are facing (its name shows at
  the bottom), **Y** switches to fixed views of each spot and back, **B** leaves
  to the Career Menu list.
- **Front door**: play your next game or practice.
- **Phone**: call your agent (I want to stay / trade request / trade interest)
  or your coach. Coach calls show **your head coach's own photo**, taken from
  your game disc or the mod pack. Calls are conversation only for now: they are
  remembered, but no trade really happens yet.
- **Mirror**: **Edit Appearance** (the game's own appearance and equipment
  editor) and **Edit Attributes** (your MyPlayer's ratings).
- **Computer**: My Stats, Schedule, Bank, buying a home, and the **Furniture
  Shop**. Your contract pays into your bank every game week.
- **Furniture Shop**: buy decor for the room (wall pieces, corner pieces,
  table-top items, sofas, coffee tables, rugs, side tables). What you buy
  appears in the room and is saved with your career. The shop sells free CC0
  furniture that comes with the game, and **the Crib's own items** (sofas,
  tables, rugs, posters, banners, pennants, standees, signs, the jukebox and
  more). The Crib items are made **from your own game disc** the first time you
  need each one (a short pause) and kept in `cache\mycareer-crib`; they are
  never part of the download. Crib items are Windows-only for now.
- **TV**: SOFTDRINK's full Career Menu list (Upgrades, Settings + Save, Quit).
- **Saving**: Career Menu > Settings + Save > Save. Your bank and purchases are
  saved on their own in `saves\mycareer-home`.

**In-game stat bar**: during your MyCareer games, your MyPlayer's live stat
line and a **game grade** sit in the top-right corner, in two looks: **2K5**
(the game's own style) or **Modern**. Change it in MyCareer's Settings with the
**Stat bar look** row (it appears when SOFTDRINK's "MyPlayer stat line" is On).
When the stat bar is shown, the NBC Sunday Night Football and CBS broadcast
packages move their corner graphic out of its way.

**Supersim**: simulated games get a modern backdrop panel behind SOFTDRINK's
Supersim text.

MyCareer home needs SOFTDRINK 2K28 v0.9; with other packs or the original game
nothing changes.

## Sandbox (Windows, separate download)

Sandbox is a scene editor (camera control, scene setup and game state, saving a
scene). It is not in the main download: unzip `NFL2K5-Sandbox-pack-0.8.0` and
copy its `mods` folder into your game folder, so that
`mods\sandbox\pack.json` exists. Sandbox then appears under **Game Modes**. The
player move/pose tools and the clip picker are still work in progress. Sandbox
is not available on Linux; Android has what it already had.

## Android GPU drivers (optional, Snapdragon / Adreno only)

**Options > GPU Driver.** Most players should stay on the **System Driver**
(the default). Snapdragon phones and handhelds can try a replacement driver
such as **Mesa Turnip**, the way emulators do:

1. Get a driver package for your GPU generation (a `.zip` with `meta.json`, or a
   bare `.so`). None is included; one source is
   github.com/K11MCH1/AdrenoToolsDrivers. Use a build you trust; third-party
   builds are not tested by us.
2. **Options > GPU Driver > Add Driver** and pick the file.
3. Choose it on the **GPU Driver** row, then close the game fully and open it
   again. **In Use Now** shows what is running. **Remove Driver** (press twice)
   deletes the chosen one.

If a driver crashes or never reaches the screen, the next start goes back to the
System Driver by itself and tells you why, so a bad driver cannot lock you out.
The setting is not offered on phones without an Adreno GPU.

## Other features

- **Broadcast presentations** (Windows and Android): before a Quick Game,
  Coach Match Up > Presentation picks the original ESPN look or an installed
  package (FOX, CBS, NBC Sunday Night Football or your own). **F2** cycles
  them on PC.
- **Every team in Team Select**: past the last current team come the disc's 75
  historic teams, then any new teams in `mods\teams`.
- **New teams** sit beside the 32 clubs (Windows; the Team Creator and
  `tools\NFL2K5-TeamBuilder.exe` build them from your own disc).
- **Texture packs**: replace any texture with an image of any size, in
  `mods\textures` (see `mods\textures\README.txt`).
- **Organised saves** with automatic backups, under `saves`.
- **Save converter** (Windows, `tools\nfl2k5-save-converter.exe`): the same
  October build as before, unchanged in this release. Keep your original saves
  as a backup.
- **Discord Rich Presence** (Windows): see `mods\discord.ini`.

## Known issues

- Menus can hitch briefly now and then.
- On Direct3D 12 the game can stall for a moment under load, mostly when the
  PC is busy with other work. Direct3D 11 and Vulkan were smoother in testing.
- The boot intro movie flickers a little more than before on PC.
- **Linux is untested on real hardware**: it has only run in a virtual machine
  with software graphics.
- **Android**: some things have not been proven on real phones yet: the
  choppiness reported on the Retroid Pocket 6, phone heat, a blink in the intro
  movie, and Turnip drivers on real Adreno GPUs.
- **MyCareer**: only the apartment exists. Buying the house or the mansion
  changes the title, but the room stays the apartment. The Crib's "Team" items
  (sofa, mug, cap) use the Crib's default colours, not your team's colours.
  The Crib's big pieces (home theater, chopper, fish tank ...) wait for the
  house and mansion. No social media yet.
- With SOFTDRINK active, Team Select uses the mod's own team list.

## Reporting a bug

Tell us your platform (Windows version / Linux distribution / phone model),
graphics card, controller, and the exact steps. Attach a screenshot or short
video and the log: on Windows `%LOCALAPPDATA%\NFL2K5-PC\xbox_kernel.log` and
the `logs` folder there; on Linux `~/.local/share/NFL2K5-PC/logs/session.log`;
on Android `/NFL2K5/nfl2k5.log`. Never upload game files.

## Credits and licences

- **SOFTDRINK 2K28** and **2K5 Mod Studio / 2k-football-mod-tools** by
  **cruuz** and contributors. SOFTDRINK is distributed separately by its
  authors; the Team Creator's texture and archive codecs come from
  2k-football-mod-tools (MIT). The built-in widescreen uses the Mod Studio
  widescreen patch.
- **MyCareer home assets** (all CC0): **Kenney** (Furniture Kit), **Poly Haven**
  (models and the window skyline HDRI), **ambientCG** (surfaces). See
  `mods\mycareer-home\CREDITS.txt` and `mods\mycareer-home\MANIFEST.md` (every
  file with its source and checksum); the licences are in
  `mods\mycareer-home\models\LICENSES`.
- **cgltf** by Johannes Kuhlmann (MIT) loads the MyCareer models.
- **SDL3** (zlib licence), **glslang** (Linux/Android), **zstd** (BSD),
  **miniz** (MIT).
- **libadrenotools** by Billy Laws (BSD-2-Clause) loads custom GPU drivers on
  Android.
- **extract-xiso** 2.7.1 by in <in@fishtank.com>.
- **Input Prompts** button icons by **Kenney** (CC0).
- The recompiler is based on **xboxrecomp**.

Licence texts and notices are in `docs\third-party`.

This is a community project. It is not affiliated with or endorsed by Sega,
Visual Concepts, 2K, ESPN, the NFL, the NCAA, EA, or any television network.

## Building from source

Windows: Visual Studio 2022 with the Desktop C++ workload, then
`.\tools\build.ps1 -Game`. The build needs `analysis\disasm\functions.json`
(made by `tools\analyze.ps1` from your own disc) or mod packs will not run; the
build stops with a message if it is missing. Linux: see `linux/README.md`.
Android: `android/CMakeLists.txt` and `android/build_apk.py`.

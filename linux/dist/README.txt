ESPN NFL 2K5 - native Linux build (x86-64)
==========================================

EXPERIMENTAL BUILD. It has only been run in a software-rendered virtual machine,
never on real Linux hardware or a real GPU. Its speed is unknown. Please report
what you find, with logs/session.log.

This archive holds the game program only. It contains no game data. You need
your own legally dumped Xbox copy of ESPN NFL 2K5.


WHAT YOU NEED
-------------
- 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12,
  Fedora 36, Arch, SteamOS 3 and later).
- A GPU with a working Vulkan driver, and the driver's 32/64-bit Vulkan loader
  (libvulkan.so.1). Check with: vulkaninfo --summary
  There is no OpenGL or Direct3D renderer on Linux.
- X11 or Wayland, and PipeWire, PulseAudio or ALSA. These come from your system.
  SDL3 is bundled in lib/, you do not need to install it.
- Optional: an `ffmpeg` program on your PATH, with the wmav2 encoder, for your
  own songs in the Music folder. Without it the Music folder stays empty.


DISC FILES
----------
Copy your whole extracted disc into the `original` folder:

    original/default.xbe
    original/update.xbe, original/dashupdate.xbe    (mod packs need these)
    original/vc_53450030/0 ... F       (16 files, about 5.8 GB)

Names are not case-sensitive (VC_53450030 works too). The disc must be the USA
retail release. To unpack a disc image use extract-xiso (https://github.com/
XboxDev/extract-xiso).


INSTALL AND RUN
---------------
    tar xzf NFL2K5-linux-x86_64-*.tar.gz
    cd NFL2K5-linux-x86_64-*
    (copy your disc files into original/)
    ./nfl2k5.sh

Keep the folder writable: saves and mod packs are stored in it.

    ./nfl2k5.sh --retail     the clean game, whatever mod pack is selected

F1 opens the settings menu (video, system, Music Player).


WHERE THINGS GO
---------------
    In the game folder:
        saves/                 your saved games and rosters
        mods/, Music/          mod packs, your own songs
    In ~/.local/share/NFL2K5-PC/:
        nfl2k5_video.ini, nfl2k5_game.ini, controls.ini    settings
        cache/                 shader cache
        logs/session.log       this run; logs/session.prev.log is the run before

Create an empty file named portable.txt next to the program to keep settings,
logs and cache in the game folder too.

If something goes wrong, logs/session.log is what to send with a bug report.
A crash writes a report into it.


VIDEO
-----
The renderer is Vulkan through SDL3. Windowed, borderless and fullscreen modes,
internal resolution, and the F1 video rows work as on Windows, except for the
rows listed under "Not on Linux". To force a window system:

    SDL_VIDEO_DRIVER=wayland ./nfl2k5.sh
    SDL_VIDEO_DRIVER=x11 ./nfl2k5.sh


CONTROLLERS
-----------
Controllers are read through SDL3 and can be plugged in while the game runs.
Xbox, DualSense and DualShock 4 pads are the ones designed for. If a pad is not
seen, your user may lack access to /dev/input or /dev/hidraw; your distribution's
game-devices-udev or steam-devices package fixes that.


MOD PACKS
---------
Put a .2k5patch file in mods/patches/. The game installs it in the background
a few seconds after it starts (a message shows the progress; it needs the disc
files above). Switch packs from Features > Mod Packs; the game asks first and
restarts itself.

Patched disc images (.iso) cannot be installed on Linux; use the .2k5patch.


STEAM DECK
----------
Not tested on a Steam Deck yet; nobody has run this build on one. It should
work as a non-Steam game: add nfl2k5.sh in desktop mode, keep the archive on
the internal drive or an ext4 card, and give the F1 key a button through Steam
Input (the settings menu has no controller shortcut). The Deck's Vulkan driver
and Steam Input's virtual pad are what the game expects, but expect to tune
the scale and frame limit in F1 yourself.


MYCAREER HOME (SOFTDRINK v0.9)
------------------------------
With SOFTDRINK v0.9 installed, Game Modes > MyCareer starts your career in a
3D apartment: walk with the left stick, turn with the right stick, A uses the
front door, mirror, computer (bank, Furniture Shop) and phone. The furniture
is free CC0 art (mods/mycareer-home/CREDITS.txt). The stat bar and the
Supersim backdrop work as on Windows. The Crib's own items in the shop are
made from the disc on Windows only for now; Linux shows the CC0 items.
Known issue: in our test VM (software Vulkan) the 3D room drew black behind
the on-screen labels; it has not been tried on a real GPU yet.


NOT ON LINUX
------------
- Broadcast presentation packages (FOX, CBS, NBC scorebugs, player cards,
  broadcast music). The game keeps its original ESPN presentation.
- Sandbox. Its panel is an HTML page, and Linux has no HTML host yet, so the
  Sandbox pack is not part of this archive.
- Added teams and the Team Builder, the PS2/Xbox save converter, and installing
  mod packs from patched disc images. Those tools are Windows programs.
- FSR 3 upscaling and frame generation, HDR output, and the Direct3D choices in
  the Graphics API row.
- DDS replacement textures (PNG, JPEG and BMP work).
- SOFTDRINK moment audio clips (they use the presentation's sound player).


THIRD-PARTY SOFTWARE
--------------------
See docs/third-party/. SDL3 is under the zlib license.

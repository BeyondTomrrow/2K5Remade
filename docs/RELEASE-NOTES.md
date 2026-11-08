# ESPN NFL 2K5 Native — 0.8.0

Windows, Linux (experimental) and Android. You need a clean dump of your own
USA retail Xbox disc; nothing from the game is included. Install steps are in
`HOW-TO-INSTALL.txt` (Windows), the archive's `README.txt` (Linux) and
`README.md`.

## What's new

### MyCareer home (with SOFTDRINK 2K28 v0.9)

- Your career starts in a 3D apartment you walk around in, instead of a menu.
- Front door: next game or practice. Phone: call your agent or your coach
  (your head coach's own photo). Mirror: Edit Appearance and Edit Attributes.
  Computer: stats, schedule, bank, homes and the Furniture Shop.
- Furniture Shop: free CC0 furniture, plus the Crib's own items, made from your
  own game disc the first time they are needed (Windows). Purchases and your
  weekly pay are saved with your career.
- In-game MyPlayer stat bar with a live game grade, top-right, in a 2K5 look
  or a Modern look (MyCareer Settings > Stat bar look). NBC SNF and CBS move
  their corner graphic out of its way.
- A modern backdrop for SOFTDRINK's Supersim screen.

### Platforms

- Windows: Direct3D 11, Direct3D 12 and Vulkan renderers (F1 > Video >
  Graphics API).
- Linux x86-64: new, experimental, Vulkan only. Tested only in a virtual
  machine with software graphics so far.
- Android: custom Adreno GPU drivers (Mesa Turnip and others) under Options >
  GPU Driver, Phone Settings, touch layout editor, Controls screen, Heat
  Control, Music Player rows. Signed with a new release key: uninstall an
  earlier test APK once first. Your /NFL2K5 folder (saves, settings, mods,
  songs) stays; re-add a custom GPU driver and allow All files access again.

### SOFTDRINK 2K28 v0.9

- Supported as a drop-in `.2k5patch` (use the 128 MB build, not the "-64MB"
  one). Patched ISOs are refused with a message: use your clean dump plus the
  `.2k5patch`. v0.7 still works.
- The audible L/R slide works for the player controlling the quarterback.
- Extras > Mods switches packs; the game asks before it restarts.

### Video

- Render Scale is your choice. Dynamic Resolution Auto may lower the
  resolution when the graphics card falls behind, but never goes above the
  Render Scale you set.
- Menus no longer get stuck in a narrow 4:3 strip after you skip an attract
  demo.

### Music Player

- F1 > Music Player on PC (Options > Music Player on Android): pick a folder or
  a song, skip, see what's playing.

### Sandbox

- Ships as its own pack for Windows (`NFL2K5-Sandbox-pack-0.8.0.zip`). Not on
  Linux.

## Not in this release

- My NFL Eras and the old-school gear: still in development.
- The CBS and NBC broadcast packages' own fonts (licence unclear); those
  packages fall back to other fonts.
- The save converter is the same October build as in earlier releases,
  unchanged.

## Known issues

- Menus can hitch briefly now and then.
- Direct3D 12 can stall for a moment under load; Direct3D 11 and Vulkan were
  smoother in testing.
- The boot intro movie flickers a little more than before on PC.
- Linux has not been run on real hardware yet.
- Android, not yet proven on real phones: the choppiness reported on the
  Retroid Pocket 6, heat, an intro blink, Turnip on real Adreno GPUs.
- MyCareer on Linux/Android: in the Linux test VM (software Vulkan) the 3D room
  drew black behind the labels; menus, shop and stat bar run. Not yet seen on
  real Linux GPUs or phones. Windows draws the room on all three renderers.
- MyCareer: only the apartment is built (house and mansion change the title
  only); the Crib's Team items use the Crib's default colours, not your team's.

## Third-party software

SOFTDRINK 2K28 and 2K5 Mod Studio / 2k-football-mod-tools by cruuz and
contributors; Kenney, Poly Haven and ambientCG (CC0) for the MyCareer home;
cgltf (MIT); SDL3 (zlib); libadrenotools (BSD-2-Clause, Android); extract-xiso
2.7.1 by in <in@fishtank.com>; Kenney Input Prompts (CC0). Licences and
notices are in `docs\third-party`; the MyCareer assets are listed file by file
in `mods\mycareer-home\MANIFEST.md`.

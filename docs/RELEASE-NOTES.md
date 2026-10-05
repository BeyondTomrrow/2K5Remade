# ESPN NFL 2K5 Native PC — 0.1.0 Preview

This is a preview build of the native Windows recompilation. It requires game
files extracted from a legally owned Xbox copy of ESPN NFL 2K5. The installer
does not contain or download the retail game.

## Installation

Run `NFL2K5-PC-Setup.exe`, choose any installation folder, then either:

- select an existing extracted game folder containing `default.xbe` and
  `vc_53450030`; or
- select your own Xbox ISO/XISO and a destination. Setup creates an
  `ESPN NFL 2K5` folder there and extracts the disc.

The selected game-data folder is linked into the installation and is not
deleted when the application is uninstalled.

## Included

- Native x64 Release executable using the normal accelerated renderer.
- In-game video settings, editable with F1.
- Presentation packages and mod documentation.
- Local music-folder support and instructions.
- Save library folders for franchise, roster, settings, and VIP saves.
- Import handling that converts valid Xbox NFL 2K5 save signatures for the PC
  runtime without changing the save payload.

## Preview limitations

This project remains under active development. Rendering, audio, presentation
graphics, save compatibility, and performance may still have game-specific
issues. Keep backups of imported saves.

## Third-party software

This product includes `extract-xiso` 2.7.1, developed by in
<in@fishtank.com>, for extracting a user's own Xbox disc image. Its copyright,
license conditions, and warranty disclaimer are installed at
`docs\third-party\extract-xiso-LICENSE.txt`.

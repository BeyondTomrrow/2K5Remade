# NFL 2K5 PC save library

The native game still reads and writes the Xbox title layout under:

`saves/UDATA/53450030/<12-digit container>/`

Do not remove that working tree. The PC save library mirrors it into folders
that people can understand and safely copy:

| Save type | Player-facing folder | Xbox type |
|---|---|---|
| Franchise | `saves/franchise/<save name>/` | `FXG` |
| Roster / Team Management | `saves/rosters/<save name>/` | `ROS` / `TMM` |
| Settings | `saves/settings/<save name>/` | `STG` |
| VIP / profile | `saves/vip/<save name>/` | `USR` |

Each named folder contains the real `SAVEGAME.DAT`, `EXTRA`, `TYPE`, and
`SaveMeta.xbx`, plus `.nfl2k5-save.ini`, which records the Xbox container ID.
The game synchronizes both trees every 2.5 seconds while it is running and at
the beginning of the next launch. Files are copied through a temporary file
and replaced atomically.

## Importing saves

Put a save in `saves/import/` and launch the game. Supported inputs are:

- Xbox NFL 2K5 save ZIPs
- Xbox raw `SAVEGAME.DAT` files
- Xbox memory-unit `.bin` / `.img` files
- PlayStation 2 `.max` and `.psu` saves
- PlayStation 2 `.ps2` memory-card images

The importer converts PS2 saves to the Xbox payload format, produces the
required Xbox HMAC in `EXTRA`, writes Xbox metadata, assigns a unique
12-digit container ID, and places the result in `franchise` or `rosters`.
The original input moves to `saves/import/processed/`. A file that cannot be
validated or converted moves to `saves/import/failed/`; it is never deleted.

An already-extracted Xbox folder can also be copied directly under the right
named folder. It must contain both `SAVEGAME.DAT` and its valid `EXTRA` HMAC.
Unsigned raw payloads are not installed as if they were valid saves.

Settings and VIP files are Xbox-specific and can be imported as complete
extracted Xbox folders. PS2-to-Xbox conversion currently applies to roster and
franchise payloads.

## 2026 roster CSV

`tools/convert-roster-csv.py` converts the supplied 2026 roster table into
NFL2K5Tool input. It preserves the CSV's stable player-slot order, maps all 32
NFL team abbreviations to NFL 2K5's internal team names, places the historic
and unassigned rows in the FreeAgents pool, and translates appearance and
equipment values to the game's enums.

The verified Week 2 conversion processed 1,937 players with no invalid
attributes, team-capacity failures, or player-name-pool failures. It is
installed locally as `saves/rosters/2026 NFL Rosters Week 2`.

## Converter dependency and credit

Cross-platform conversion is provided by BAD-AL's
[`nfl2k5tool_dart`](https://github.com/BAD-AL/nfl2k5tool_dart), based on the
original NFL2K5Tool. The project performs Xbox/PS2 parsing, signing, metadata,
and packaging. The local build is `tools/nfl2k5-save-converter.exe`.

The upstream repository describes the project as open source but currently
does not include a license file or explicit redistribution terms. Credit alone
does not grant redistribution rights. Before putting its compiled executable
inside the public installer, obtain explicit permission/license terms from
BAD-AL or make the converter a separately obtained optional dependency. Keep
this attribution in either case.

For a reproducible local build, install Dart SDK 3.10.8 or newer and run
`tools/build-save-converter.ps1`. The script clones the upstream project when
needed, records the verified upstream revision, applies this project's
player-name-pool fix, restores dependencies, and compiles the converter. The
patch is tracked at `tools/nfl2k5tool-name-pool.patch`; the upstream checkout
and compiled executable remain ignored because redistribution permission has
not yet been established.

ZIP extraction inside the native game uses Rich Geldreich's public-domain
`miniz` implementation already present in the xemu source tree; its license is
at `external/xemu-src/util/miniz/LICENSE`.

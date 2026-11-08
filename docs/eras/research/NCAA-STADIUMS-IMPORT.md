# NCAA Football 07 stadiums that were NFL homes, as My NFL Eras stadium packs (2026-10-16)

Follows the Coliseum route (`docs/eras/research/COLISEUM-IMPORT.md`): NCAA 07 STADIUMS.DAT model -> 2K5 stadium
bundles `<code><t><w>.iff` -> Eras stadium pack `mods\eras\stadiums\<Folder>\stadium.json` + `files\`.
Nothing in `src/` was changed and no build was run. No retail or NCAA pixels are in git (see "Git").

Status: **16 of 16 converted, previewed and installed. 3 seen in game** (Orange Bowl, Cotton Bowl, Alamodome).

## The list

| code | pack folder | `name` (teams.csv) | NCAA source (STAD.SRES, entries day/night) | clone row | host row | NFL spell |
|---|---|---|---|---|---|---|
| s61 | Miami_Orange_Bowl | MIAMI ORANGE BOWL | Miami Stadium (63, MIAMFL 0x2F4/0x2F8) | Pro Player Stadium | Electra Coliseum | Dolphins 1966-1986 |
| s62 | Cotton_Bowl | COTTON BOWL | Cotton Bowl, neutral dressing (179, NEUCOT 0x864/0x868) | Arrowhead Stadium | Funk Field | Cowboys 1960 - Oct 1971 |
| s63 | Franklin_Field | FRANKLIN FIELD | Franklin Field (94, PENNSY 0x468/0x46C) | Lincoln Financial Field | Dream Superdome | Eagles 1958-1970 |
| s64 | Yale_Bowl | YALE BOWL | Yale Bowl (143, YALEXX 0x6B4/0x6B8) | Giants Stadium | Alien Arena | Giants 1973-1974 |
| s65 | Harvard_Stadium | HARVARD STADIUM | Harvard Stadium (43, HARVAR 0x204/0x208) | Gillette Stadium | Atomic Dome | Patriots 1970 |
| s66 | Alumni_Stadium | ALUMNI STADIUM | Alumni Stadium, Chestnut Hill (17, BOSTON 0xCC/0xD0) | Gillette Stadium | Metro Field | Patriots 1969 |
| s67 | Nippert_Stadium | NIPPERT STADIUM | Nippert Stadium (23, CINCIN 0x114/0x118) | Paul Brown Stadium | Bat Field | Bengals 1968-1969 |
| s68 | Rice_Stadium | RICE STADIUM | Rice Stadium (99, RICEXX 0x4A4/0x4A8) | Tampa Bay Stadium | Axe Superdome | Oilers 1965-1967 |
| s69 | Liberty_Bowl_Memorial_Stadium | LIBERTY BOWL MEMORIAL STADIUM | Liberty Bowl, bowl dressing (158, LIBERT 0x768/0x76C) | Titans Coliseum | Angry Dog Park | Oilers 1997 |
| s70 | Vanderbilt_Stadium | VANDERBILT STADIUM | Vanderbilt Stadium (133, VANDER 0x63C/0x640) | Titans Coliseum | Express Dome | Oilers 1998 |
| s71 | Clemson_Memorial_Stadium | CLEMSON MEMORIAL STADIUM | Clemson Memorial Stadium (24, CLEMSO 0x120/0x124) | B of A Stadium | Cat Park | Panthers 1995 |
| s72 | Husky_Stadium | HUSKY STADIUM | Husky Stadium (137, WASHIN 0x66C/0x670) | Qwest Field | Cobra Field | Seahawks 2000-2001 |
| s73 | Memorial_Stadium_Champaign | MEMORIAL STADIUM CHAMPAIGN | Memorial Stadium, Champaign (48, ILLINO 0x240/0x244) | Chicago Field | Royal Arena | Bears 2002 |
| s74 | Tiger_Stadium_Baton_Rouge | TIGER STADIUM BATON ROUGE | Tiger Stadium (59, LSUXXX 0x2C4/0x2C8) | Tampa Bay Stadium | Dragon Dome | Saints 2005 (4 games) |
| s75 | Alamodome | ALAMODOME | Alamodome (144, ALAMOX 0x6C0; one model) | Louisiana Super Dome | Gold Coast Dome | Saints 2005 (3 games) |
| s76 | Ladd_Peebles_Stadium | LADD-PEEBLES STADIUM | GMAC Bowl stadium, Mobile (161, MOBILE 0x78C/0x790) | Tampa Bay Stadium | Scimitar Stadium | Senior Bowl (future) |

(s60 / LA_Coliseum / LOCO ARENA is the earlier Coliseum pack.) Where NCAA has two dressings of a venue, the
neutral or bowl one was taken over the college one: Cotton Bowl 179 (NEUCOT) not 148 (COTTON), Liberty Bowl 158
(LIBERT) not 62 (MEMPHI, the Memphis Tigers'). Yale and Harvard are the same EA model (identical extents) with
different textures. The Alamodome has only a FAIR model (it is used for night too).

## Tools

* `tools/eras/stadiums/ncaa_stadiums.py`: the table above and the pipeline:
  `list | build | preview | pack | install | textures | all [slug ...] [--only dd nd]`.
  It drives `coliseum_build.py` / `coliseum_neutral.py` (their module settings are set per venue).
* `tools/eras/stadiums/ncaa_eras_test.ps1`: the background-mode in-game driver with a sandbox root
  (`setup | stadium | launch | press | shot | watch | stop | teardown`).
* Small changes for reuse: `coliseum_build.py` (`SHAPE_PREFIX`, no prop squeeze when the wall is beyond the
  deepest prop) and `coliseum_neutral.py` (`SKYLINE` style dict: ridge / foothills / palms / conifers / towers;
  end-zone box keeps block proportions for short names). The Coliseum's own defaults are unchanged.
* `tools/eras/make_stadium_history.py`: `PACKS` table (below).

Commands used: export `python tools/ncaa07-extractor/ncaa_app.py build-stadium H:/NCAA07_Extractor_Work/stadiums_raw/<entry>.tmd data/eras/stadiums/<slug>/ncaa07/<entry>`
(all 16 exported, GLB + assembly_report; `logs/eras/ncaa-export.log`); then `ncaa_stadiums.py build`, `preview`,
`pack`, `install` (4 venues in parallel, about 6 min per venue for the nine bundles; logs
`logs/eras/ncaa-build-w1..4.log`).

## Conversion

Per venue, as for the Coliseum:

* **Base bundles: retail s24 (QUALCOMM) for every venue**, including the Alamodome. The field, detail layer,
  intro cameras, sideline props and the neutral-art routines (end zones, pads, banners, backdrop) were fitted to
  s24 in the Coliseum work, so every pack reuses them. The NCAA model replaces the Qualcomm bowl.
* **Clone row (the stadium record the pack copies: climate floats, indoor and grass words)**: an open-air grass
  NFL venue of a similar climate/region (table); for the **Alamodome the Louisiana Super Dome row (indoor 1,
  grass 0)**. The NCAA Alamodome model has its own roof, so the s24 sky is not seen; in game it plays as a dome
  (frames `logs/eras/al1-h*.png`).
* `stadium.json`: `indoor`/`grass` set as the clone (all open-air ones grass 1; turf seasons such as Franklin
  Field after 1969 or Husky 2000 keep the s24 grass look).
* NCAA ground near field level: dropped under the playing field, lifted 3 cm elsewhere inside the retail
  outside-grass rectangle (no z-fighting with the retail grass).
* Sideline props / sideline people squeezed inside each venue's wall (wall measured from the model).
* NCAA baked shadow overlays (Cotton Bowl NEUSHAD, Vanderbilt, Husky) are dropped: NCAA blends them, the 2K5
  materials used here are opaque or alpha-tested.
* Husky Stadium (the biggest model) had its textures of >= 128x128 cut to 64 px to stay within retail stadium
  sizes (first build: 1.70 MB bundle, stadium scene video 1.08 MB vs retail max 1.19 MB; now 1.53 MB). Every
  other bundle is within retail sizes (largest s74dd 1.59 MB; retail s21dd 1.61 MB).

### Team-neutral art pass

* **End zones**: the NFL team's city in white block letters, both ends: MIAMI, DALLAS, PHILADELPHIA, NEW YORK,
  BOSTON (Harvard, Alumni), CINCINNATI, HOUSTON, TENNESSEE (Liberty Bowl and Vanderbilt: the 1997-98 Tennessee
  Oilers), CAROLINA, SEATTLE, CHICAGO, NEW ORLEANS (Tiger Stadium, Alamodome), MOBILE (Ladd-Peebles).
* **Midfield**: no logo (transparent `center_logo`). **Goal-post pads**: plain slate. **Fan banners**: neutral
  slogans with the city ("LET'S GO MIAMI!", "WELCOME TO DALLAS", "ORANGE CROWD"...). **Backdrop**: per-venue
  style (Miami palms, Seattle Cascades and evergreens, Clemson Blue Ridge, flat towns elsewhere); the San Diego
  trolley is gone.
* **On the NCAA model** (per texture, `art` in the table; night textures inherit the day rules by pixels):
  * `fan`: college flags, tarps and rail banners -> the neutral fan-banner materials (every venue);
  * `board`: scoreboards with college / EA SPORTS panels -> a plain lamp board "HOME / VISITORS / QTR / DOWN TO
    GO BALL ON" (Orange Bowl, Cotton Bowl, Franklin, Yale, Harvard, Nippert, Rice, Liberty, Vanderbilt,
    Clemson, Husky, Champaign, Tiger, Alamodome, Ladd-Peebles);
  * `flat`: walls / signs with college lettering -> their own median colour (Cotton Bowl upper fascia, Liberty
    edge wall, Husky fence and building sign, Champaign building, Tiger tunnel, Alamodome fascia bands, Vandy
    jumbotron and tunnel, Clemson wall, Alumni tunnel hang, Ladd-Peebles GMAC swirl walls);
  * `rect`: part of a texture (Franklin / Clemson hill-crowd logo, Alamodome fascia strips);
  * `drop`: decals not converted (Franklin PENN letters and logo, Alumni BC logos / jerseys / lettering, Vandy
    "V" and SEC signs, Clemson Howard's Rock and paw rail, Ladd-Peebles EA SPORTS sign, Orange Bowl cannon).
* Kept: `banner_corp` (the retail 2004 sponsor atlas, as at the Coliseum) and NCAA ad placeholders routed to it.

Known leftovers seen in the previews (not team or college marks): Rice Stadium shows large light-grey concrete
and fewer seats with crowd (the NCAA model's own); the Alamodome fascia keeps purple/red star bands and a few
small green wall plaques (not identified, left); the Tiger Stadium scoreboard is the generic lamp board.
The in-game end-zone paint is grey-ish under the game's field shading (`logs/eras/ob1-h18.png`), as retail's.

## Packs and the host row

`nfl2k5_teams_stadium_pack` (src/nfl2k5_teams.c) at each Eras launch: restores any borrowed row first (one
borrowed row at a time), scans `mods\eras\stadiums\*\stadium.json` for the `name` teams.csv gives, copies the
`clone` row into the `host` row (`host` defaults to LOCO ARENA), sets the pack's strings and asset code, and
the clean-up restores it. `nfl2k5_archive_extend` adds every pack's `files\*` to the extra archive at boot.

So several packs need no code change: only the home stadium of the current game is borrowed, codes are unique
(s60..s76) so the archive names never collide, and each pack has **its own host row** anyway (17 different
fictional rows 44..60, none used by a real franchise), so even a missed restore cannot make two packs share a
row. Notes for later (no change made):

* every boot copies all pack files into the extra archive pack G: with 17 packs that is 251 files, 326 MB
  (`[TEAMFILES] 251 added-team files in an extra archive pack (325702 KB)`), a few seconds more at boot;
* `stadium.json` is read into a 4 KB buffer; `ncaa_stadiums.py pack` refuses a file over 4000 bytes;
* the launcher does not split a "name|fallback" string; the fallback lives in the history json (below).

## Stadium history

`tools/eras/make_stadium_history.py`: a `PACKS` table maps (franchise, stadium, first season) to the pack name.
Those spells get `ingame` = the pack name (`exact`) and a new `fallback` field = the retail stand-in. The
Coliseum rows (Rams 1946-79, 2016-19, Raiders 1982-94) now use the same mechanism. The Saints' 2005 "mixed" row
is split into Alamodome (3 games), Tiger Stadium (4 games) and Giants Stadium (1 game). Regenerated
`data/eras/league/stadiums_1966_2025.json` and `docs/eras/research/STADIUMS.md` (95 spells). teams.csv is not
generated by this tool. Of the 1980 teams only the **Dolphins** play in one of these venues: their row in
`data/eras/runtime/1980/teams.csv` still reads `PRO PLAYER STADIUM,standin,Miami Orange Bowl`. Setting its
`stadium` to `MIAMI ORANGE BOWL` (exact) is the one-cell change that puts 1980 Dolphins home games in the
Orange Bowl; it was not made here (teams.csv was to stay byte-identical), and without the pack installed the
launcher would log "not found" and use the template's stadium.

## In-game checks (background mode)

Driver `ncaa_eras_test.ps1`: RECOMP_BACKGROUND=1, RECOMP_AUDIO_MUTE=1, NFL2K5_NO_HOST_PAD=always,
NFL2K5_PRIVATE_PACKS=1, own exe copy `build\Release\NFL2K5_stadiums.exe` (deleted after each run), press file,
`-AppData` sandbox (`logs\eras\<tag>-home`), sandbox `NFL2K5_ROOT` `logs\eras\stadsbx-root` (junctions to
original, mods, cache, Music, font, assets, config, tools, dependencies, external and every data\ folder except
`data\eras\runtime\1980`, which is copied; a copied `saves\` without the old Partition*.img and backups). Only
the sandbox teams.csv was edited (the Vikings' stadium), so the real one never changed. NFL2K5.exe was not
touched. Sequence: Press START, VIP OK, Team Select back, Main Menu -> My NFL Eras -> New League (1980) ->
Play: Falcons at Vikings -> Start Game (CPU vs CPU), 60 s quarters.

| run | pack (stadium) | log | result |
|---|---|---|---|
| ob1 | MIAMI ORANGE BOWL (s61, 2 packs + Coliseum installed) | `[TEAMS] Eras stadium pack Miami_Orange_Bowl: ... (code s61, from Pro Player Stadium) in row 811CE870 (Electra Coliseum)` | **pass**: intro flyover, sideline intros, coin toss, kickoff, plays in the Orange Bowl (palms, light towers, press box), MIAMI end zones; no STALL/CRASH (`logs/eras/ob1-h*.png`, `ob1-h16.png`, `ob1-h18.png`) |
| cb1 | COTTON BOWL (s62) | `... Cotton_Bowl: COTTON BOWL (code s62, from Arrowhead Stadium) in row 811CE8F0 (Funk Field)` | **pass**: "WELCOME TO DALLAS" / "LET'S GO DALLAS!" banners, DALLAS end zones, kickoff and plays (`logs/eras/cb1-h*.png`, `cb1-h15.png`). One boot-time `[GGL] STALL` line before the menus (line 269, long before the launch), no effect |
| al1 | ALAMODOME (s75, **all 17 packs installed**) | `... Alamodome: ALAMODOME (code s75, from Louisiana Super Dome) in row 811CEF70 (Gold Coast Dome)` | **pass**: boot with 251 pack files; under the roof, star fascia, kickoff and plays (`logs/eras/al1-h*.png`, `al1-h20.png`) |

Not run in game: the other 13 venues (same pipeline, previews only); night/rain/snow variants and the
afternoon light (the three games were day/clear); the restore at the Eras clean-up (proved for the Coliseum).

Afterwards: real `data\eras\runtime\1980\teams.csv` SHA-256 `523b21c0...af04d2b` before and after (never
edited); real `saves\UDATA` 507 files, SHA-256 list identical before and after
(`logs/eras/ncaa-udata-before.sha256` / `-after`); sandbox removed with `cmd /c rmdir` per junction; the private
archive folder `original\disc\VC_53450030.NFL2K5_stadiums` (hard links + its own pack G) removed.

## Sheet

`logs/eras/ncaa-stadiums-sheet.png`: per venue the day end zone, blimp, sideline, night end zone, and the in-game
frame for the three checked. Previews: `data/eras/stadiums/<slug>/preview/<code>{dd_blimp, dd_endzone,
dd_sideline, dd_high_sideline, dd_midfield, dd_flyover, nd_blimp, nd_endzone, ds_midfield}.png`.

## Git

`.gitignore` gains `/data/eras/stadiums/` and `mods/eras/stadiums/*/files/`: bundles, GLB exports and
previews stay local. `mods/eras/stadiums/*/stadium.json` (no pixels) can be committed. Nothing was committed.

## Failures

None: all 16 exported, built (9 bundles each), previewed, packed and installed.

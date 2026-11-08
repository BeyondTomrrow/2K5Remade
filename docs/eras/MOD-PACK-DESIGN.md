# My NFL Eras as a separately downloaded mod pack: design

2026-10-16. Design only: nothing in `src/` was changed, nothing was built or run.

The user's goal: **My NFL Eras is "a mod pack that someone downloads separately".** A player who
has not installed it sees the plain game, with no main-menu row and no Eras hooks doing anything.
A player who drops the pack in gets the whole mode. No retail NFL 2K5 or NCAA 07 pixels or models
are ever shipped. Assets that need them are built on the player's PC from the player's own discs.

Inputs read for this design:
* `docs/eras/PHASE2-PLAN.md`, `PHASE2-RESULTS.md`, `results/*.md`;
* `docs/eras/research/NATIVE-MENU.md`, `COLISEUM-IMPORT.md`, `NCAA-STADIUMS-IMPORT.md` and `COMMUNITY-ROSTERS.md`;
* `docs/HANDOFF-MODS.md` and `mods/README.md`;
* the code in `src/nfl2k5_eras*.c`, `src/main.c`, `src/recomp_manual.c`, `src/nfl2k5_teams.c`,
  `src/nfl2k5_team_files.c`, `src/nfl2k5_texture_packs.cpp`, `src/nfl2k5_presentation.cpp`,
  `src/nfl2k5_mod_packs.c`, `src/interp/*`, `tools/apply-gen-patches.py`, `tools/eras/**`,
  `tools/build-pack-installer.ps1`, `tools/build-team-builder.ps1` and `installer/NFL2K5-PC.iss`.

---

## 0. Recommendation in one paragraph

Use **option (c): a built-in Eras module behind a versioned plugin seam, switched on by a pack that
holds data only.** The Eras code stays in `NFL2K5.exe`, but every place the exe reaches Eras goes
through a single hook table. The table is filled only when a valid `mods\packs\MyNFLEras\pack.json`
is installed on a supported base game. Otherwise the exe behaves as if Eras were never compiled in:
* no main-menu row;
* no archive files added;
* the gen-patch shims return at once.

The pack ships these:
* data: the league package, `team.json` files and office looks drawn from scratch;
* recipes: what to build from which disc;
* a credits file.

It ships no code. The asset builder that turns the player's discs into the office, logos, card
badges and stadium bundles is a standalone `NFL2K5-ErasBuilder.exe` that ships with the game
release, as `NFL2K5-TeamBuilder.exe` already does. That keeps all executable code on the trusted
game-release channel. The seam is the plugin API of option (b), specified in section 5. If Eras
later needs to update faster than the game, the same module can be compiled into a signed
`MyNFLEras.dll` without touching the hook sites again (migration step 9). Do not start with the DLL.
Today it would cost a large refactor and a native-code download trust problem, and it would buy
little, because the game and Eras are released by the same person from the same repository.

---

## 1. What Eras touches today, and what already gates it

### 1.1 Inventory

| layer | where | size |
|---|---|---|
| Eras module | `src/nfl2k5_eras.c` (league, package, save), `_menu.c` (native screens), `_launch.c` (staging, launch, return), `_capture.c` (final whistle, box), `_office.c` (3D office), `_depth.c`, `_sheets.c` (Schedule / Standings copies), `_selectors.h` | 5,260 lines; **untracked in git** (only on `snapshot/eras-2026-10-16`) |
| shared host code used by Eras | `src/nfl2k5_teams.c` staging (412-800), Eras stadium packs (803-890), test hooks (340, 698, 982-1135) | ~750 lines |
| | `src/nfl2k5_team_files.c` archive extension for Eras stadium/office files (342-385), guest-string arena (511) | ~60 lines |
| | `src/nfl2k5_texture_packs.cpp` era folders and team-art folders (126-365) | ~240 lines |
| | `src/nfl2k5_presentation.cpp` Eras identity / package / API fields (314-333, 410-500, 642-660, 759, 773-880, 2633-2636, 2825, 3752); `presentation_host.h:47`, `webview2_host.cpp:326` | ~200 lines |
| | `src/nfl2k5_controller_art.cpp` 349-376 (era context in the shape hash) | ~10 lines |
| boot / dispatch | `src/main.c` 890-895, 3152-3157, 3290-3292, 3825-3827; `src/recomp_manual.c` 2629-2633, 2777-2795 | |
| gen patches | `tools/apply-gen-patches.py` 777-803: `ERAS_GAME_FINAL` (sub_000A2910), `ERAS_MATCH_EXIT` (sub_000645D0), `ERAS_RETURN` (sub_0034C1F0), `WALK_DIAG_ENTER/STEP` (sub_000561F0) | |
| build | `config/game.cmake:18` (the seven Eras sources) | |
| data | `data/eras/runtime/1980/` (1.8 MB, read at run time); `data/eras/league/*.json`, `community_rosters/` (53 MB), `ps2_saves/` / `ps2_max/` / `ps2_psu/` (122 MB), `stadiums/` (606 MB of intermediates) are **build inputs** | |
| assets | `mods/eras/office/files/*.iff` (disc), `offices/<era>/` (drawn art and `era.ini`, plus the IBM photo), `teams/1980/<id>/team.json` + logos (disc), `teams/1980/art/*.png` (disc + user), `stadiums/<Folder>/` (211 MB; NCAA + disc), `eraselect/voice/1980s.mp3` (ElevenLabs), `presentations.ini` | |
| tools | `tools/eras/*.py`, `tools/eras/stadiums/*.py`, `tools/ncaa07-extractor/`; **and untracked scripts under `build/eras/`** (`cdf.py`, `make_runtime_package.py`, `roster_membership.py`, `extract_community_rosters.py`, `ps2max.py`, `league_schedule.py`, ...) | |

Eras calls **91 distinct guest functions** (`sub_XXXXXXXX`) directly through its own `gcall`
helpers. It also reads and writes guest memory with `MEM32` and uses the TLS guest registers
(`g_eax`, `g_ecx`, `g_edx`, `g_esp`, ...).

### 1.2 Does the exe already gate Eras? Partly

There is **one real gate**, on the menu:

```c
/* src/main.c:3825-3827 */
nfl2k5_eras_menu_install(!g_interp_pack && NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) == 0);
```

`nfl2k5_eras_menu_install` (`src/nfl2k5_eras_menu.c:789-800`) returns when:
* the game is not running the retail or widescreen XBE;
* an interpreted XBE pack (SOFTDRINK) is on;
* `NFL2K5_ERAS_MENU=off` is set.

So under SOFTDRINK there is no Eras row. Under `--retail`, or with no pack selected, the row is
always inserted. Nothing checks whether Eras data or assets are installed. If
`data\eras\runtime\1980` is missing, the row still appears and fails later.

There are two soft fallbacks:
* the office falls back to the plain list when `erasoffice.iff` / `erasskin.iff` are missing
  (`_office.c:459`);
* the presentation package falls back to the player's own when `presentations.ini` names a
  package that is not installed.

Everything else is **dormant by state, not gated**. It runs or is registered on every boot and
returns because no Eras launch is active.

### 1.3 Every place that needs a gate (or a move behind the hook table)

| # | place | today | gate needed |
|---|---|---|---|
| G1 | `src/main.c:3825-3827` menu install | retail-only gate | `nfl2k5_eras_enabled()` (pack valid, base supported, not `--vanilla`) |
| G2 | `src/main.c:3152-3157` `nfl2k5_eras_interp_hooks()` + `nfl2k5_eras_capture_init()` | always | only when enabled; when disabled, register no interp hooks |
| G3 | `src/recomp_manual.c:2777-2795` interp hooks at 0xA2910 / 0x645D0 / 0x34C208 | always registered (via G2) | same as G2 |
| G4 | `src/recomp_manual.c:2629-2633` VA range 0xFEE00000 -> `nfl2k5_eras_menu_lookup` | always | route through the hook table; return NULL when disabled |
| G5 | gen patch `ERAS_GAME_FINAL` (recomp_0004.c, sub_000A2910) | always calls `nfl2k5_eras_game_final`, which returns unless armed (logs when `NFL2K5_ERAS_LOG`) | call a stable shim `nfl2k5_hook_game_final(ecx, edx)`: one load and a branch on the table |
| G6 | gen patch `ERAS_MATCH_EXIT` (sub_000645D0) | always calls `nfl2k5_eras_match_exit` | shim `nfl2k5_hook_match_exit()` |
| G7 | gen patch `ERAS_RETURN` (sub_0034C1F0) | always calls `nfl2k5_eras_return_desc(edx)` | shim `nfl2k5_hook_return_desc(edx)`; must stay memory-only |
| G8 | gen patches `WALK_DIAG_*` (sub_000561F0), `src/main.c:3290` | env-gated diagnostic, code in `_launch.c` | move the code out of `_launch.c` into a diag file in the exe; it is not Eras |
| G9 | `src/main.c:890-895` `nfl2k5_eras_fsm_pop` in `nfl2k5_diag_fsm` | always called on every FSM pop | shim `nfl2k5_hook_fsm_pop(fsm, top)` |
| G10 | `src/nfl2k5_team_files.c:342-385` adds `mods\eras\stadiums\*\files\*` and `mods\eras\office\files\*.iff` to archive pack G | **always, when the folders exist, on retail and under XBE packs**: 211 MB of stadium bundles go into the extra pack at every boot | ask the module for its files (`on_archive_extend`); nothing when disabled |
| G11 | `src/nfl2k5_teams.c:803-890` stadium-pack row borrow, `mods\eras\stadiums\*\stadium.json` | called by Eras only | path from the pack root; keep as a host service |
| G12 | `src/nfl2k5_teams.c:982-1135` `NFL2K5_ERAS_STAGE_TEST` / `_STAGE_ALL`, hard-coded `data\eras\runtime\1980` | env-gated test hooks at Team Select | path from the pack root; dev builds only (or keep env-gated) |
| G13 | `src/nfl2k5_teams.c:340`, `698` (`HISTORIC_INVENTORY`, `ERAS_TEST_DROP`) | env-gated | leave (diagnostics) |
| G14 | `src/nfl2k5_texture_packs.cpp:166-170` reads `mods\eras\offices\office-textures.ini`; 221-222 `mods\eras\offices\<era>\` | lazy, only after `nfl2k5_texpack_era()` | path from the pack root |
| G15 | `src/nfl2k5_presentation.cpp:2633-2636` maps `mods/eras/teams/` to `erateams.local`; `3752` + `webview2_host.cpp:326` always map the virtual host | always (harmless) | map only when enabled; folder from the pack root |
| G16 | `src/nfl2k5_presentation.cpp:435, 780` `mods/eras/teams/<season>/<id>`, `mods\eras\presentations.ini` | only during an Eras game | path from the pack root |
| G17 | hard-coded paths in the module: `_eras.c:33` `data\eras\runtime\1980`, `_office.c:301, 459-460`, `_eras.c` team-art folder, launch box paths | | `nfl2k5_eras_path(rel)` resolving against the pack root |
| G18 | `config/game.cmake:18` | compiled in | unchanged under (c); removed only at the optional DLL step |

---

## 2. Options

### (a) Eras code in the exe, dormant unless the pack is installed

This is G1-G17 done as plain `if (!nfl2k5_eras_enabled()) return;` checks, with the module still
called by name.

* **XBE packs and `--retail`.** As today: Eras is enabled only on the retail or widescreen base,
  with no interpreted pack. `--retail` keeps meaning "ignore the selected XBE pack", so Eras still
  works there (the user's Eras tests use it). A new `--vanilla` turns off feature packs too.
* **Versioning.** The code and the exe are one build. The pack carries data, so only the **data
  schema** has to match: `pack.json` `data_schema` against the exe's supported range, and
  `min_game_version`. This is easy to explain to players.
* **Security.** No native code is downloaded. The attack surface is the exe's own parsers for pack
  files (CSV/JSON/INI in C with fixed buffers, PNG, MP3) and path handling.
* **Code moved.** Almost none: about 20 call sites plus a path resolver.
* **Testing.** As today, plus "pack absent" and "pack bad" boots.
* **Weakness.** An Eras code fix needs a game release. Gates spread across many files invite a
  missed site. Nothing stops new Eras hooks from being added without a gate.

### (b) Plugin DLL `MyNFLEras.dll` in the pack

The exe exposes a versioned API. The gen patches call stable extern shims that dispatch to the
plugin when one is loaded.

* **XBE packs and `--retail`.** Same activation rules as (a). Additionally, the plugin's guest-call
  list can be checked against the interpreter's dirty map: a function a pack changed is a
  compatibility failure (section 6.3).
* **Versioning.** Three things must agree:
  * the host API (major must match, minor must be at least the plugin's);
  * the guest ABI, meaning the retail XBE's addresses. These are frozen forever because the
    retail XBE never changes. The plugin pins `xbe_sha256`;
  * the data schema.
  The host API is the moving part. Every host change in staging, presentation or texture packs is
  potentially breaking.
* **Security.** This is the real cost. A DLL from a download runs with the player's full rights,
  and nothing sandboxes it. It needs:
  * signature checks (an ECDSA P-256 signature over the DLL, the public key in the exe, verified
    with BCrypt before `LoadLibraryExW`);
  * a full-path load with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`
    against DLL planting;
  * an explicit per-user opt-in for unsigned (third-party) plugins.
* **Code moved.** All of the module, about 5,300 lines. Every one of the 91 `gcall(sub_X)` sites
  becomes `host->guest_call(0xVA, ecx, edx)`, because the DLL cannot link generated code. Every
  `g_eXX` access becomes an accessor, because the registers are `__declspec(thread)` in the exe
  and a DLL cannot address another module's static TLS. The service table must also expose:
  * `MEM32`, the guest heap and `recomp_lookup`;
  * the staging, stadium, presentation and texture services;
  * stat readers, the user path, logging and toasts.
  Expect about 1,000 lines of new host glue.
* **Testing.** Everything twice (built-in and DLL) during the move, and an ABI test per release.
* **Strength.** Eras releases on its own cadence. Third parties can write other modes against the
  same API.

### (c) Recommended: (a) with the (b) seam, and a code-free pack

* One file, `src/nfl2k5_plugin_host.c/.h`, owns a single `NfModuleHooks` table (section 5). Every
  G-site calls a shim there.
* The built-in Eras module registers itself with `nfl2k5_eras_module_register(&hooks)`, and the
  host calls it **only after** the pack check passes. While the table is empty, every shim is one
  load and a branch.
* The module reaches host features only through `NfHostServices`. At first the members are thin
  wrappers that call the existing functions directly, so nothing is rewritten.
* Guest calls stay `gcall(sub_X)` while the module is built in. They are listed in one table
  (`nfl2k5_eras_guest_calls.h`: VA plus name) so that:
  * the dirty-map compatibility check (section 6.3) works;
  * the DLL move is mechanical.
* The pack is data plus recipes. The builder exe and all code come with the game release.

How (c) compares:
* **Against (a):** one gate instead of seventeen. New hooks cannot bypass it, because the module
  has no other entry points. The API is defined and exercised from day one, so the DLL step becomes
  a build change, not a redesign.
* **Against (b):** no native-code download, no signing infrastructure, no ABI breakage risk now,
  and almost no code moves. The cost is that an Eras code fix needs a game release. That is
  acceptable, because both ship from this repository, and the GitHub release flow already
  exists (`tools/build-release.ps1`).

### (d) Rejected alternatives

* **Eras as an XBE/interpreted pack** (guest code in a patched XBE). Rejected:
  * XBE packs are exclusive (one active), so Eras would conflict with SOFTDRINK;
  * interpreted code cannot reach the `0xFEE` served menu functions (NATIVE-MENU 2.2);
  * the code would have to be written in x86 against the guest.
* **A scripting runtime** (Lua, or JS in the existing WebView2). Rejected: the menu, launch and
  office code calls guest functions at frame rate, on the guest thread, with register state. A
  script bridge would be the same service table plus a VM, at more cost and risk than a DLL.

---

## 3. Activation rules (how Eras coexists with XBE packs and the launch switches)

There are two pack classes:
* **XBE packs** (SOFTDRINK): exclusive, `mods\active-pack.ini`, interpreted on the retail exe;
* **feature packs** (My NFL Eras): additive, independent On/Off.

`nfl2k5_modpacks_scan` already skips a `mods\packs\<folder>` that has no `default.xbe`, so an Eras
pack folder is never offered or launched as an XBE pack. Make it explicit with
`"kind": "feature"` in `pack.json`, so the scanner stops relying on the missing file.

`nfl2k5_eras_enabled()` is true only if all of these hold:

1. `mods\packs\MyNFLEras\pack.json` parses, has `"id": "my-nfl-eras"` and `"kind": "feature"`, and
   its `data_schema` and `min_game_version` are supported by this exe.
2. The feature is not switched off: `mods\features.ini` has no `my-nfl-eras=0` line, `--vanilla`
   was not given, and `NFL2K5_ERAS` is not `0`.
3. The base is supported: the retail or widescreen XBE with no interpreted pack
   (`!g_interp_pack && NFL2K5_BASE_CMP(...) == 0`), or a base listed in `pack.json`
   `compatible_bases` that passes the dirty-map check (6.3; future).
4. The runtime package named by `pack.json` `league.packages[]` is present (`teams.csv`,
   `schedule.csv`, `rosters\`).

If 1 or 4 fails with the pack present, the Main Menu row is still inserted, greyed. Its help text
gives the reason, for example "My NFL Eras 0.3 needs NFL2K5-PC 1.7 or newer". This uses the
existing row-update grey bit (+0xC). If 3 fails (SOFTDRINK on), there is no row, the log reads
`[ERAS] off: SOFTDRINK 2K28 v0.6 is on (feature not supported on that base)`, and a toast appears
once per session. With no pack, nothing happens and one log line is written.

Launch switches:

| switch | XBE pack | feature packs (Eras) |
|---|---|---|
| (none) | `active-pack.ini` | `features.ini` |
| `--retail` | off | follow `features.ini` (so Eras tests keep using `--retail`) |
| `--vanilla` (new) | off | all off: the truly clean game |
| `NFL2K5_ERAS=0` / `=1` | | force off / force the dev layout (section 4.3) |

---

## 4. Pack folder layout

### 4.1 What the download contains

```
mods\packs\MyNFLEras\
  pack.json                      manifest (section 4.2)
  CREDITS.txt                    sources, licences, permissions (section 8)
  README.txt                     player instructions: install, "Build my assets", uninstall
  league\
    1980\                        runtime package (today data\eras\runtime\1980)
      teams.csv  schedule.csv  coaches.csv  PROVENANCE.txt
      rosters\<id>.csv  rosters\<id>.flags.txt
      depth\...
    <season>\                    later seasons, one folder each
  teams\
    1980\<id>\team.json          identity and colours (no pixels)
  offices\                       today mods\eras\offices, without reference\
    office-textures.ini
    default\era.ini
    1970s\ ... 2020s\            era.ini + drawn PNGs (make-office-art.py output)
  stadiums\
    <Folder>\stadium.json        name, code, clone, host, capacity, the NFL seasons,
                                 and "fallback" (the stand-in row from stadiums_1966_2025.json)
  presentations.ini              decade or season -> package name (no packages included)
  eraselect\                     the era-select cards (when built); voice\ only if cleared (section 8)
  recipes\
    build.json                   the asset build plan (section 7.2)
    era_logos_1980.json          eras id -> disc logo code and style (today in extract_era_logos.py)
    team_art_1980.json           hash -> member map (today build/eras/logos/hashes.json)
    ncaa_stadiums.json           venue table (today the VENUES table in ncaa_stadiums.py)
```

### 4.2 Written on the player's PC (never in the download)

```
mods\packs\MyNFLEras\generated\
  build.json                     what was built: builder version, input hashes, output hashes, time
  office\erasoffice.iff  erasskin.iff        (from the NFL 2K5 disc)
  teams\1980\<id>\logos\scorebug.png mini.png (disc)
  teams\1980\art\<HASH>_<what>.png           (disc; and user art the player supplies)
  stadiums\<Folder>\files\<code><t><w>.iff   (NCAA 07 + NFL 2K5 discs; optional)
  user\                          the player's own optional art (IBM photo, Bengals logo, narration)
```

`generated\` lives inside the pack folder, so deleting the folder uninstalls everything. A pack
update keeps `generated\`: the updater replaces only shipped files. `build.json` lets the game
decide what is stale. League saves stay where they are (`%LOCALAPPDATA%\NFL2K5-PC\eras\league1\`).

Lookup order for every Eras path: `generated\<rel>` first, then `<pack>\<rel>`. This mirrors
today's split between git-tracked `team.json` files and gitignored PNGs in one folder.

### 4.3 The developer layout

During migration, and for development, `NFL2K5_ERAS=1` (or a `mods\packs\MyNFLEras\dev.txt`
marker) maps the pack root onto the current repo folders:
* `league\` -> `data\eras\runtime\`;
* everything else -> `mods\eras\`;
* `generated\` -> `mods\eras\`.

The working tree keeps playing while the files move. The final step replaces this with an NTFS
junction `mods\packs\MyNFLEras -> mods\eras-pack` (or moves the files) and deletes the mapping.

---

## 5. pack.json fields

```json
{
  "id": "my-nfl-eras",
  "kind": "feature",
  "name": "My NFL Eras",
  "display_name": "My NFL Eras",
  "version": "0.3.0",
  "author": "Matthew",
  "description": "Historical franchise mode: real seasons, rosters and schedules from 1980.",
  "homepage": "https://github.com/<owner>/<repo>",

  "manifest_version": 1,
  "data_schema": 1,
  "min_game_version": "1.7.0",
  "plugin_api": "1.0",

  "compatible_bases": [
    { "name": "ESPN NFL 2K5 (retail)", "xbe_sha256": "73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9" },
    { "name": "retail + built-in widescreen", "xbe_sha256": "<NFL2K5_WIDESCREEN_XBE_SHA256>" }
  ],

  "module": { "builtin": "eras", "dll": null, "dll_sha256": null, "signature": null },

  "league": {
    "packages": [ { "season": 1980, "dir": "league/1980", "teams": 28, "games": 224 } ],
    "default_season": 1980
  },

  "assets": {
    "builder_min_version": "1.0.0",
    "recipes": "recipes/build.json",
    "required_inputs": [ "nfl2k5_xbox_disc" ],
    "optional_inputs": [ "ncaa07_xbox_disc", "user_art" ]
  },

  "labels": { "rosters": "unverified community roster" },

  "files": [ { "path": "league/1980/teams.csv", "sha256": "..." } ],

  "credits": "CREDITS.txt",
  "licence": "data: see CREDITS.txt; code ships with NFL2K5-PC"
}
```

| field | meaning | checked by |
|---|---|---|
| `id`, `kind` | identity; `kind: feature` keeps it out of the XBE-pack list | scanner, gate 3.1 |
| `version` | the pack's own version, shown in the hub ("My NFL Eras 0.3.0") | hub label, logs |
| `manifest_version` | the layout of this file | loader: refuses an unknown major |
| `data_schema` | the CSV/INI/JSON formats the module reads: teams.csv columns, era.ini keys, stadium.json keys | module: `ERAS_DATA_SCHEMA_MIN..MAX` compiled in |
| `min_game_version` | the oldest exe with the needed module behaviour | gate 3.1, greyed row with the reason |
| `plugin_api` | the `NfModuleHooks` / `NfHostServices` version this pack expects (built-in: informative; DLL: enforced) | host |
| `compatible_bases` | XBE SHA-256 values Eras supports; more can be added after the dirty-map check | gate 3.3 |
| `module` | `builtin: "eras"` now; later `dll`, plus `dll_sha256` and a detached `signature` file | host loader |
| `league.packages` | seasons available; `dir` relative to the pack | module |
| `assets.*` | builder version needed, recipe file, which discs are needed and which are optional | builder, hub "Build my assets" row |
| `labels.rosters` | the label shown in the UI for roster provenance (today hard-coded) | menu, capture |
| `files` | sha256 of every shipped file: integrity on load (a warning, not a refusal) and the updater's diff | loader, updater |
| `credits`, `licence` | required by section 8 | release checker |

Parsing: a real JSON parser (the presentation code already parses JSON in C++). Do not use the
`strstr` style of `mp_read_json_name`. All paths in the manifest are validated: relative,
no `..`, no drive letters, under the pack root. Use the `mp_safe_folder` rules for each component.

---

## 6. Plugin API surface

C, with a fixed calling convention (`__cdecl`) and only POD structs. Every struct starts with
`uint32_t size`, so either side can tell which fields the other knows. Header:
`src/nfl2k5_plugin_api.h`, shared by the exe and a future DLL.

### 6.1 Handshake and versioning

```c
#define NF_PLUGIN_API_MAJOR 1
#define NF_PLUGIN_API_MINOR 0

typedef struct NfHostInfo {
    uint32_t size;
    uint16_t api_major, api_minor;       /* host's */
    const char *game_version;            /* "1.7.0" */
    const char *game_build;              /* git describe */
    const char *xbe_sha256;              /* the loaded base */
    uint32_t base_kind;                  /* NF_BASE_RETAIL, NF_BASE_WIDESCREEN, NF_BASE_INTERP_PACK */
    const char *xbe_pack_name;           /* NULL unless an XBE pack is on */
    const char *root_dir, *pack_dir, *generated_dir, *user_dir;
    uint32_t flags;                      /* NF_HOST_VANILLA, NF_HOST_PORTABLE, NF_HOST_DEV_LAYOUT ... */
} NfHostInfo;

typedef struct NfModuleInfo {
    uint32_t size;
    uint16_t api_major, api_minor;       /* what the module was built against */
    const char *id, *version;            /* "my-nfl-eras", "0.3.0" */
    uint32_t data_schema_min, data_schema_max;
    const uint32_t *guest_calls;         /* every guest VA the module calls (6.3) */
    uint32_t guest_call_count;
    uint32_t va_range_base, va_range_size;   /* served functions: 0xFEE00000, 0x100000 */
} NfModuleInfo;

/* built-in: called directly. DLL: exported by name. */
int nf_module_query(NfModuleInfo *out);
int nf_module_init(const NfHostInfo *host, const NfHostServices *svc, NfModuleHooks *hooks_out);
void nf_module_shutdown(void);
```

The host refuses (no row, logged reason, toast) when:
* `api_major` differs, or the module's `api_minor` is newer than the host's;
* `pack.json` `data_schema` is outside the module's range;
* any VA in `guest_calls` is dirty on the current base (6.3);
* `va_range` overlaps a range already served: 0xFEC00000 is Video Settings, 0xFED00000 is
  oldschool Pads.

The host grows by adding fields at the end of the structs and members at the end of the tables.
A module checks `svc->size` before using a newer member.

### 6.2 Hooks: host -> module (`NfModuleHooks`)

| hook | called from (today's site) | thread | carries | returns / rules |
|---|---|---|---|---|
| `on_boot(const NfBootCtx*)` | `main.c:3152` (after `xbox_MemoryLayoutInit`, before entry) | main | guest memory base, `NfHostInfo`; the module registers its interp hooks through `svc->interp_add_hook` | 0 = OK; nonzero disables the module |
| `on_archive_extend(NfArchiveSink*)` | `team_files.c:342-385` inside `nfl2k5_archive_extend` (gen patch at archive mount) | guest | a sink: `add(sink, host_path, archive_name)`, count limits | the module lists its stadium bundles and office packages from `generated\` |
| `on_menus_ready()` | `main.c:3825` | main | none | the module builds its screens (guest heap via `svc`) and calls `svc->menu_insert_link(MAIN_MENU, 3, header, label)` |
| `lookup_va(uint32_t va)` | `recomp_manual.c:2629` for `va_range` | guest | the VA | a native function (`void(*)(void)`) following the served-function rules (plain `ret`, `g_esp += 4`, keep EBX/ESI/EDI/EBP), or NULL |
| `on_fsm_pop(uint32_t fsm, uint32_t top)` | `main.c:890` | guest | FSM object, new top descriptor | none; the after-game clean-up and depth-chart end |
| `on_game_final(uint32_t home_pts, uint32_t away_pts)` | gen `ERAS_GAME_FINAL` + interp hook 0xA2910 | guest | ecx / edx at sub_000A2910 entry; `[0xE602B8]` still the old state | none; capture when armed |
| `on_match_exit()` | gen `ERAS_MATCH_EXIT` + interp hook 0x645D0 | guest | none | none; marks an armed capture abandoned |
| `return_desc(uint32_t edx)` | gen `ERAS_RETURN` + interp hook 0x34C208 | guest, **registers not published** | edx = 0x515660 (Main Menu) | the descriptor to pop to; memory reads only, no guest calls, no services except `mem` |
| `on_team_select_step(int step)` | `teams.c` Team Select hook (today's env test hooks G12) | guest | step index | dev/test only (`NF_HOST_DEV_LAYOUT`) |
| `on_frame()` | optional, the frame tick | guest | none | reserved; not needed today |
| `on_shutdown()` | process exit | main | none | flush the league log |

Every shim in `nfl2k5_plugin_host.c` looks like this:

```c
void nfl2k5_hook_game_final(uint32_t ecx, uint32_t edx)
{ if (g_mod.on_game_final) g_mod.on_game_final(ecx, edx); }
```

The gen patches call these shims by their stable names. A future DLL changes nothing in
`apply-gen-patches.py`.

### 6.3 Services: module -> host (`NfHostServices`)

Guest machine (needed by a DLL; the built-in module may keep its direct macros):

| service | wraps | notes |
|---|---|---|
| `uint8_t *mem_base` | `g_xbox_mem_offset` | `MEM32(a)` becomes `*(uint32_t *)(mem_base + a)` |
| `uint32_t guest_call(uint32_t va, uint32_t ecx, uint32_t edx)` | `recomp_lookup(va)` + today's `gcall` | resolves dirty functions to the interpreter |
| `uint32_t reg_get(int r)`, `void reg_set(int r, uint32_t v)` | the TLS `g_eax..g_esp` | needed by served menu functions (`ret`, args at `[esp+4]`) |
| `uint32_t guest_alloc(uint32_t size, uint32_t align)` | `xbox_HeapAlloc` | |
| `void interp_add_hook(uint32_t va, uint32_t from, uint32_t (*fn)(void))` | `recomp_interp_add_hook` | packs that interpret the hooked functions |
| `int guest_va_dirty(uint32_t va)` | the interpreter's dirty map (`interp_setup.c`) | compatibility check |

Menus:

| service | wraps |
|---|---|
| `int menu_insert_link(uint32_t menu, int at, uint32_t header, uint32_t label_w)` | `em_insert_link` (moves from `_menu.c` to the host: Video Settings could use it too) |
| `void menu_set_flags(uint32_t menu, uint32_t or_mask)` | the `main-scroll` case |

Teams and stadiums (today `nfl2k5_teams.c`, `nfl2k5_team_files.c`):

| service | carries |
|---|---|
| `int stage_check(int side)`, `const char *stage_message(int rc)` | USER1/USER2 availability |
| `int stage(int side, int template_entry, const char *roster_csv, const char *city, const char *name, const char *abbr)` | roster CSV path inside the pack |
| `void release(int side)`, `uint32_t staged_slot(int side)` | |
| `uint32_t stadium_find(const char *name, uint32_t *id)` | retail rows |
| `uint32_t stadium_borrow(const NfStadiumDef *def, uint32_t *id)`, `void stadium_restore(void)` | today's `nfl2k5_teams_stadium_pack`: the def (name, display, location, code, clone, host, capacity, indoor, grass) comes from the module's `stadium.json`; the host only patches the row |
| `int team_roster_file(...)`, `int identity_check(...)` | as used by `_depth.c` / M2 |

Presentation and art:

| service | carries |
|---|---|
| `void pres_eras_begin(const NfPresGame *g)` | season, week, and per side: Eras id, abbr, city, name, colours (primary, secondary, text), logo path (scorebug, mini); the package name from `presentations.ini`, already resolved by the module |
| `void pres_eras_end(void)` | restores the player's package (unchanged behaviour) |
| `void pres_set_virtual_host(const char *host, const char *dir)` | `erateams.local` -> `generated\teams` (today always mapped) |
| `void texpack_set_era(const char *season_dir, const char *decade_dir)` | office looks (`nfl2k5_texpack_era`) |
| `void texpack_set_team_art(const char *dir_or_null)` | card badges and logos (`nfl2k5_texpack_eras_teams`) |
| `const char *texpack_names_ini` | set by the module: `offices\office-textures.ini` |

Stats, I/O, UI:

| service | wraps |
|---|---|
| `int player_stat(uint32_t player, uint32_t selector, uint32_t scope, double *out)` | `nfl2k5_guest_player_stat_scope` |
| `int team_stat(uint32_t team, uint32_t selector, uint32_t scope, double *out)` | `nfl2k5_guest_team_stat` |
| `char *user_path(const char *rel, char *out, size_t n)` | `xbox_user_path` (league saves) |
| `void log(const char *fmt, ...)` | stderr, the `[ERAS]` prefix |
| `void toast(const char *header, const char *l1, const char *l2, unsigned ms)` | `xbox_PresentToast` |
| `int run_builder(const char *args, void (*done)(int code))` | starts `tools\NFL2K5-ErasBuilder.exe` hidden, like `mp_patch_thread` |

The 91 guest functions Eras calls go in one list (`guest_calls`). The retail XBE is immutable,
so these VAs are a frozen ABI. They are not a versioning problem on the retail base. They are a
problem under an XBE pack, which may rewrite any of them. At boot, with an interpreted pack on, the
host walks `guest_calls` (and the three hooked VAs, plus the menu headers Eras copies:
`MAIN_MENU 0x515660`, `FEATURES 0x525830`, `GAME_OPTIONS 0x501E48`, ...) through
`guest_va_dirty`. If any of them is dirty, Eras stays off for that pack, and the log names the
functions. This turns "does Eras work under SOFTDRINK?" from a guess into a boot-time answer. It
only becomes relevant if `compatible_bases` ever lists a pack. That also needs the interpreter's
`native_for` to try `recomp_lookup_manual` for 0xFEE00000 (NATIVE-MENU 2.2).

---

## 7. First-run asset builder

### 7.1 Python, in-game, or a bundled exe?

The tools that make the assets:

| tool | input | output | deps | time |
|---|---|---|---|---|
| `tools/eras/build-office-pack.py` | NFL 2K5 disc archive | `erasoffice.iff`, `erasskin.iff` (byte copies of SCNE records) | stdlib only | seconds |
| `tools/eras/build-office-ibmpc.py` | `erasoffice.iff` + the IBM photo | the `coach_d80s` record + `computer_monitor.png` | numpy, Pillow | seconds |
| `tools/eras/make-office-art.py` | nothing (Windows fonts) | the drawn office PNGs | Pillow | seconds; **ship its output** |
| `build/eras/logos/cdf.py` (**untracked**) + `tools/eras/extract_era_logos.py` | disc `logos.cdf`, `mini.cdf` | `teams\1980\<id>\logos\*.png`, `team.json` | Pillow | seconds |
| `tools/eras/build_era_team_art.py` (+ `build/eras/logos/hashes.json`, **untracked**) | disc logos + user Bengals logo | `teams\1980\art\<HASH>_*.png` | Pillow | seconds |
| `tools/ncaa07-extractor` (STADIUMS.DAT -> `stadiums_raw\*.tmd`, needs **QuickBMS**, not in git) + `ncaa_app.py build-stadium` | NCAA 07 Xbox disc | per-venue GLB / parts | numpy | minutes |
| `tools/eras/stadiums/ncaa_stadiums.py build/pack` (+ `coliseum_build.py`, `coliseum_neutral.py`, `scne.py`, `p8.py`, vendored `nfl_txtr`) | NCAA parts + the retail s24 bundles | nine `<code><t><w>.iff` per venue | numpy | **about 6 min per venue**, 17 venues (~100 min single-threaded, ~25 min on 4 workers), ~600 MB of intermediates |

Choice: **a bundled exe, `NFL2K5-ErasBuilder.exe` (PyInstaller `--onefile`, numpy + Pillow), shipped
in the game release's `tools\`**. This is the same pattern as `NFL2K5-ModPack-Installer.exe` and
`NFL2K5-TeamBuilder.exe` (`tools/build-pack-installer.ps1`, `tools/build-team-builder.ps1`,
`installer/NFL2K5-PC.iss:313-322`):
* players never need Python;
* developers keep running the scripts with `python -I`;
* all executable code stays on the game-release channel, so the pack stays code-free.

In-game C was considered and rejected for the whole job. The stadium converter is about 2,500 lines
of numpy geometry and texture work, too large to port. One later, small exception is worth making:
`build-office-pack.py` is a byte copy of two archive records, and could run in-game in C at the
first Eras open, so the 3D office works even if the builder never ran. It is optional, and not
part of the first migration.

Required builder changes (all in the migration plan):
* move the untracked `build/eras/logos/cdf.py` and `hashes.json` (and the runtime-package tools
  that matter) into `tools/eras/`;
* replace the hard-coded `H:\NCAA07_Extractor_Work` (`tools/eras/stadiums/ncaa_source.py:25`) with an
  `--ncaa-dir` argument;
* add one orchestrator, `tools/eras/build_assets.py`, that reads `recipes/build.json` and writes
  `generated/build.json`;
* replace QuickBMS for the single archive the stadiums need (STADIUMS.DAT) with a Python reader.
  Otherwise the player must install QuickBMS themselves: it cannot be bundled casually, because
  it is GPL. If no reader is written, the NCAA step asks the player for an already-extracted
  `stadiums_raw` folder.

### 7.2 `recipes/build.json`

```json
{
  "schema": 1,
  "steps": [
    { "id": "office",       "needs": ["nfl2k5_xbox_disc"], "tool": "office_pack",      "out": "office/" },
    { "id": "office_1980s", "needs": ["office", "user_art:ibm_pc_5150"], "tool": "office_ibmpc", "optional": true },
    { "id": "logos_1980",   "needs": ["nfl2k5_xbox_disc"], "tool": "era_logos",        "args": {"map": "recipes/era_logos_1980.json"} },
    { "id": "art_1980",     "needs": ["logos_1980"],       "tool": "era_team_art",     "args": {"map": "recipes/team_art_1980.json", "user_logos": {"bengals": "user_art:bengals_1980"}} },
    { "id": "stadiums",     "needs": ["nfl2k5_xbox_disc", "ncaa07_xbox_disc"], "tool": "ncaa_stadiums", "optional": true, "per_item": "recipes/ncaa_stadiums.json" }
  ]
}
```

Inputs are identified by hash, not by path. The NFL 2K5 disc is `original\disc` (`default.xbe`
SHA-256 = retail, and `vc_53450030` present). The installer already links it there
(`NFL2K5-PC.iss` CreateDiscLink). The NCAA 07 disc is a folder the player picks, recognised by the
STADIUMS.DAT size and hash. `user_art:<name>` is a file the player puts in `generated\user\`
(section 8).

### 7.3 The flow

1. **Install.** The player unzips `MyNFLEras-<ver>.zip` into `mods\packs\`, or drops it into
   `mods\patches\`. The existing drop-folder installer (`mp_patch_thread`) learns `.eraspack`/zip
   with `pack.json kind=feature`, verifies the `files` hashes, and unpacks it.
2. **First boot with the pack.** The gate passes, and the Main Menu shows "My NFL Eras". The hub
   reads `generated\build.json`. If it is missing or stale (builder version, input hash or recipe
   hash changed), the hub's first row is **"Build My Assets"**, with help text that states what is
   missing ("The 3D office and era logos are made from your own NFL 2K5 disc, once. About a
   minute.").
3. **Build.** The row calls `svc->run_builder("--pack <dir> --steps office,logos_1980,art_1980")`.
   The builder:
   * runs hidden at below-normal priority;
   * writes `logs\eras-build.log`;
   * reports progress in a small status file that the hub polls and shows as a toast ("Building:
     office ... logos ... done");
   * writes each output to a temp name and renames it at the end, so a crash never leaves a
     half-written `.iff`.
   The game keeps running. Archive files (the office `.iff`) only enter pack G at the next boot,
   because `nfl2k5_archive_extend` runs at archive mount. When the build finishes, the hub says
   "Restart the game to use the new office".
4. **Stadiums (optional, long).** A separate row, "Build Historic Stadiums (needs NCAA Football 07)",
   starts the builder's GUI mode: a folder picker for the NCAA disc and a progress window with
   per-venue resume. It runs outside the game, because it is long and memory-hungry. It keeps
   intermediates in `%TEMP%` and deletes them. It writes only the nine bundles per venue (about
   12 MB each).
5. **Every launch.** The module never assumes an asset exists:
   * no office packages: the plain list (as today);
   * no `coach_d80s`: `coach_desk` (as today);
   * no era logos: the template team's own disc logos, and `team.json` keeps the identity text;
   * no team art: no texture swap;
   * no stadium pack: the `stadium_match = standin` row from `teams.csv` (already in the data: the
     M&T Bank Stadium stand-in for the 1980 Colts). This replaces today's fallback to the template
     team's stadium. `stadium.json` gains a `fallback` field for the same purpose;
   * no narration: the era-select screen is silent.

The builder never writes outside `<pack>\generated\` and `logs\`, and never touches `original\`.
It reads each disc file only by its known name and verifies its size and hash first.

---

## 8. Redistribution checklist

Legend: **SHIP** = may go in the download. **BUILD** = made on the player's PC from their own
copy. **ASK** = needs permission or a licence check before shipping. **NEVER** = must not be
distributed in any form.

| item | where today | source | verdict | action |
|---|---|---|---|---|
| Office packages `erasoffice.iff`, `erasskin.iff`, the `coach_d80s` geometry | `mods/eras/office/files/` (gitignored) | retail NFL 2K5 disc | **BUILD** | builder step `office` |
| Era logos (scorebug, mini), card chips, PHI/CHI-style logos | `mods/eras/teams/1980/*/logos`, `teams/1980/art` (gitignored) | retail disc | **BUILD** | steps `logos_1980`, `art_1980` |
| Stadium bundles (all 17) | `mods/eras/stadiums/*/files/` (gitignored), `data/eras/stadiums/` | NCAA 07 models + retail s24 bundle | **BUILD**, optional | step `stadiums`; never ship previews (`data/eras/stadiums/*/preview` are renders of retail/NCAA pixels) |
| `stadium.json`, `team.json`, `era.ini`, `office-textures.ini` (hashes and names only) | tracked | own work | **SHIP** | |
| Drawn office art (`make-office-art.py` output) | `mods/eras/offices/<era>/*.png` | own work, rendered with Windows fonts | **SHIP** | rendered text in an image is normally fine; avoid embedding any font file |
| IBM PC 5150 photo and the crops in `1980s/computer_monitor.png` | `mods/eras/offices/1980s/reference/ibm_pc_5150.jpg` | **user-supplied photo, origin not recorded** | **ASK** | If the user took it, they grant it, and it is credited in CREDITS.txt. If it came from the web: the widely used Wikipedia photo of the 5150 is CC BY-SA, which needs attribution and share-alike for the crops. Until the source is known, do not ship it: make it `user_art:ibm_pc_5150`, and without it the 1980s office falls back to `coach_desk`. |
| 1980 Bengals logo (`bengals/source.jpg`, `logo.png`) | `mods/eras/teams/1980/bengals/` | **user-supplied image, origin not recorded** | **ASK** (copyright of the image) + trademark | Do not ship: `user_art:bengals_1980`. Without it the disc's `logo_06` art is used (`build_era_team_art.py` already treats it as a user logo). |
| Era-select narration `1980s.mp3` | `mods/eras/eraselect/voice/` | **ElevenLabs**, user-generated | **ASK** | ElevenLabs output may be redistributed only under the plan's terms: free tier = attribution and non-commercial; paid tiers include commercial rights. Confirm the plan, credit "Voice: ElevenLabs" in CREDITS.txt, and confirm the voice is not a clone of a real person (a broadcaster). If any doubt remains, make it `user_art`. |
| "Era photos" the user supplies for cards or screens | not found on disk yet besides the IBM photo | user | **ASK**, per photo | Record the source and licence for each in `CREDITS.txt` before it goes into the pack. Default: `user_art`. |
| Community PS2 roster saves (48 seasons) | `data/eras/ps2_saves`, `ps2_max`, `ps2_psu` (122 MB) | fan-made `.max` saves the user supplied; authors unknown | **NEVER** as files | Build inputs only. |
| Extracted community rosters | `data/eras/community_rosters/*.json` (53 MB) | derived from those saves | **ASK** | Needs the roster makers' permission. The authors are not recorded; find them (the saves' directory names and save titles, and the Operation Sports 2K5 roster threads), credit them, or don't ship. |
| Runtime rosters `league/1980/rosters/*.csv` | `data/eras/runtime/1980/rosters` | **membership** from PFR (facts) + **names, bodies, faces and all 28 ratings copied from a community save record** | **ASK** (the ratings are the save authors' work) | Choose one: (1) permission + credit, and ship as is; or (2) ship membership only (PFR facts: name, position, number, experience) plus a builder step `ratings` that copies ratings from saves the player supplies (`user_art:community_saves`), with generated ratings otherwise. (2) needs a ratings generator, which does not exist yet. Until one of these is done, a public pack cannot include `rosters\*.csv`. |
| Teams, divisions, schedule, scores, standings, head coaches, stadium history, franchise moves, rules by season | `data/eras/league/*.json`, `league/1980/*.csv`, `PROVENANCE.txt` | PFR (via Wayback), Wikipedia, NFL Record & Fact Book | **SHIP** (facts) | Credit the sources in CREDITS.txt (keep PROVENANCE.txt). Wikipedia text is CC BY-SA: ship only the facts, never copied prose. Sports Reference's terms of use limit bulk reuse of its site data: ship the small factual subset per season, credit "Pro-Football-Reference.com", and never ship scraped HTML or the `pfr_cache`. |
| NFL and team names, abbreviations, colours | `team.json`, `teams.csv` | factual, but trademarks | **SHIP** with a disclaimer | "Not affiliated with or endorsed by the NFL, its teams, 2K or EA." No NFL shield, team logo or wordmark files in the download. |
| Broadcast packages named in `presentations.ini` ("NFL on CBS": CBS eye PNG, 2023 theme MP3, NFL Today open MP4) | `mods/presentations/NFL on CBS` | broadcaster trademarks and copyrighted media | **NEVER** in this pack | `presentations.ini` names a package and falls back when it is absent (already implemented). Ship no package, or a drawn generic "1980s Broadcast" package of your own. |
| 2K Football Mod Tools code (vendored `nfl_txtr`, `nfl_outer`, `scne.py`) | `tools/team-creator/vendor`, `tools/eras/stadiums/scne.py` | MIT (Noah and contributors) | in the **builder exe** | Include `LICENSE-2k-football-mod-tools.txt` in the game release's `tools\licenses\` (MIT requires the notice). |
| `tools/ncaa07-extractor` | tracked | written in this repo | in the builder exe | |
| QuickBMS | not in git | GPL-2.0 | **do not bundle** | Use a Python STADIUMS.DAT reader, or the player installs QuickBMS. |
| PyInstaller-bundled numpy and Pillow | builder exe | BSD / HPND | in the builder exe | ship their licence texts beside the exe |
| Face, skin and photo **indices** in rosters | `rosters/*.csv` | numbers that point into the player's disc | SHIP (with the roster decision above) | |

The release step `tools/eras/pack-release.ps1` (migration step 7) enforces this list.

---

## 9. Migration plan (every step testable; Eras keeps working after each)

The tests use the existing background-mode drivers (`tools/eras/office80_test.ps1`,
`sched_test.ps1`, `tools/eras-capture-test.ps1`, `tools/eras/stadiums/ncaa_eras_test.ps1`) on
`NFL2K5.exe --retail` with a sandbox `-AppData`. Run them only when the PC is free (memory: "PC may
be in use").

**Step 0. Put the module under version control.**
* Commit `src/nfl2k5_eras*` and `tools/eras/*`, which are untracked on master.
* Move the scripts the pack depends on from untracked `build/eras/` into `tools/eras/`:
  `cdf.py`, `hashes.json`, `make_runtime_package.py`, `roster_membership.py`,
  `league_schedule.py`, `extract_community_rosters.py`, `ps2max.py`.
* Test: a clean clone builds (`tools\build.ps1 -Game`) and `make_runtime_package.py --season 1980
  --check` reproduces `data/eras/runtime/1980` byte for byte.

**Step 1. The path resolver.**
* `nfl2k5_eras_path(rel)` / `nfl2k5_eras_gen_path(rel)` in `_eras.c`, used by every G14-G17
  path and by `teams.c` (G11, G12), `team_files.c` (G10) and `presentation.cpp` (G15, G16).
* With no pack, it resolves to today's folders (the dev layout, 4.3), so nothing changes.
* Test:
  * the `[ERAS]`, `[TEAMFILES]`, `[TEXPACK]` and `[PRES]` log lines show the same files as before;
  * office80_test passes;
  * one Eras game is captured.

**Step 2. The hook table and the shims (no gate yet).**
* Add `src/nfl2k5_plugin_api.h` and `src/nfl2k5_plugin_host.c`.
* Point the gen patches `ERAS_GAME_FINAL/MATCH_EXIT/RETURN` at the `nfl2k5_hook_*` shims
  (G5-G7), plus the interp hooks (G3), `main.c` fsm pop (G9), the VA range (G4), archive extend
  (G10) and menu install (G1).
* The built-in module registers unconditionally, so behaviour is identical.
* Move `WALK_DIAG` out of `_launch.c` (G8).
* Test:
  * `apply-gen-patches.py --check`, and the patch markers in `recomp_0002/0004.c`;
  * eras-capture-test (final whistle, abandon, rematch ignored);
  * the M4b return to the office.

**Step 3. The services table (built-in wrappers).**
* `_launch.c`, `_depth.c`, `_office.c`, `_eras.c` and `_capture.c` call teams, presentation,
  texpack, stats and toast through `svc->`.
* Keep `gcall(sub_X)`, but collect every guest VA into `nfl2k5_eras_guest_calls.h`. A build-time
  check (a small script) compares that list with a grep of the sources.
* Test: as in step 2, plus the M9 identity log line (`[PRES] eras game: ...`) and the Depth Chart
  stage and release.

**Step 4. `pack.json`, the loader and the gate.**
* Parse `mods\packs\MyNFLEras\pack.json` (section 5). Add `nfl2k5_eras_enabled()` (section 3),
  `features.ini`, `--vanilla`, and the greyed row with a reason.
* The dev layout stays on while `NFL2K5_ERAS=1` is set or `mods\eras` exists without a pack
  (transition only).
* `nfl2k5_modpacks_scan` skips `kind: feature`.
* Test matrix:
  * no pack, no dev layout: 7-row Main Menu, no `[TEAMFILES] My NFL Eras` line, the archive
    pack G file count lower by the Eras files;
  * pack: the 8-row menu and today's behaviour;
  * a pack with `min_game_version` too high: greyed row, reason shown;
  * `--vanilla`: no row;
  * SOFTDRINK on (only with the user's OK; memory says plain launches load it): no row, one toast,
    and SOFTDRINK plays as before.

**Step 5. Move the files into the pack layout.**
* `data/eras/runtime/<season>` -> `league/<season>`; `mods/eras/*` -> pack root or `generated/`
  as in section 4.
* In the repo, a source folder `packs/MyNFLEras/` (tracked) plus a junction
  `mods\packs\MyNFLEras` -> it, with `generated\` gitignored.
* Remove the dev-layout mapping.
* Test: the full Eras slice on a sandbox:
  * New League;
  * Play Next Game (the Coliseum or Orange Bowl pack);
  * capture, standings;
  * office looks (LT/RT through the six decades);
  * Depth Chart.

**Step 6. The builder.**
* `tools/eras/build_assets.py` (recipes -> `generated/build.json`), `--ncaa-dir`, the STADIUMS.DAT
  reader (or the "pick a `stadiums_raw` folder" fallback), and `tools/build-eras-builder.ps1`
  (PyInstaller).
* `svc->run_builder`, the hub's "Build My Assets" row, and the status file and toast.
* Test:
  * delete `generated\`;
  * boot: the plain list office and fallback logos;
  * run Build: a restart gives the 3D office and era logos;
  * output hashes equal today's files (the build is deterministic);
  * NCAA absent: an Eras game at a stand-in stadium, with the log naming the stand-in;
  * kill the builder halfway: no partial `.iff`, and the next run resumes.

**Step 7. The release split and checker.**
* `tools/eras/pack-release.ps1` zips only an allow-list (section 4.1) and fails on:
  * any `.iff`, `.cdf`, `.tmd`, `.glb`, `.max`, `.psu` or `.mp4`;
  * any PNG whose content hash appears in `build/eras/logos/hashes.json` or in a disc texture dump;
  * any file not listed in `pack.json files`;
  * a missing `CREDITS.txt` entry for each `ASK` row cleared in section 8.
* Test: the checker passes on the allow-listed tree and fails on the dev tree. Unzip the pack into a
  fresh install made by the installer smoke test (`installer-smoke-test/`), then build and play.

**Step 8. Installer and menu.**
* The game installer ships `NFL2K5-ErasBuilder.exe` and the licence texts.
* Features > Mod Packs gets a "Feature Packs" section with My NFL Eras On/Off (writes
  `features.ini`; the change applies at restart, the same as XBE packs).
* Test: toggle Off, restart, no row; toggle On, restart, the row returns and the league continues.

**Step 9 (optional, later). `MyNFLEras.dll`.**
* Only if Eras must release independently of the game.
* Build the module as a DLL against `nfl2k5_plugin_api.h`: `gcall(sub_X)` -> `svc->guest_call(VA)`,
  registers through `reg_get/set`, `MEM32` through `mem_base`.
* The host loads `pack.json module.dll` by full path with the signature check (section 2(b)).
* Keep the built-in module selectable (`NFL2K5_ERAS_MODULE=builtin|dll`).
* Test: the same scripted Eras game under both, with the `[ERAS]` logs diffed (timestamps
  stripped); then remove the built-in sources from `config/game.cmake`.

---

## 10. Risks

| risk | effect | mitigation |
|---|---|---|
| A missed gate site (an Eras effect outside the table) | Eras behaviour without the pack: today G10 already adds 211 MB to the archive under SOFTDRINK | the module has no exported names except `nf_module_*`. A step-4 test diffs the boot log and archive file count with and without the pack. Grep check: no `nfl2k5_eras_` symbol referenced outside the plugin host and the module. |
| Stale or partial `generated\` after a game or pack update | wrong office or logos, or a crash loading a bad `.iff` | `build.json` with input, builder and recipe hashes; atomic renames; the hub offers a rebuild when stale |
| The builder is slow (stadiums ~25-100 min) or fails on another PC (paths, memory, antivirus flagging a PyInstaller exe) | no stadiums | stadiums optional, with per-venue resume and stand-in fallback. Sign the exe if possible, and keep the Python path for developers. |
| QuickBMS dependency for NCAA extraction | the NCAA step cannot run on a player's PC | a Python STADIUMS.DAT reader, or a documented manual step |
| Roster licensing (community ratings) | a public pack cannot ship today's `rosters/*.csv` | section 8 decision before the first public release; the 1980 membership (facts) ships either way |
| User-supplied art with unknown origin (IBM photo, Bengals logo, narration, era photos) | takedown or licence breach | default to `user_art` slots; ship only items with a recorded source and permission |
| League save format drift between pack versions | a Continue League failure after an update | `league.ini format=` exists. Add a migration on load, and never delete a league; copy it to `league1.bak-<ver>` before migrating. |
| Under XBE packs, Eras needs anchors the pack rewrote (menus, the 91 calls) | crashes if Eras is ever enabled there blindly | stays off by default. The dirty-map check (6.3) is the only way to opt a base in, plus the interpreter `native_for` fix for 0xFEE. |
| `--retail` meaning changes | the user's habit (memory: "launch with --retail") breaks | `--retail` keeps Eras on (feature packs follow `features.ini`); `--vanilla` is the new fully clean switch |
| DLL step (if taken): ABI breaks, TLS registers, native code from downloads | crashes after a game update; security exposure | size-prefixed structs, major/minor rules, signature check, built-in fallback; take the step only with a demonstrated need |
| Uncommitted Eras code mixed with the user's other uncommitted work in shared files (teams.c, presentation.cpp, main.c) | step 0 and later steps touch files with unrelated pending edits | do step 0 per file with `git add -p` on a branch. Memory notes the snapshot branch `snapshot/eras-2026-10-16`. |
| Parsers in C reading pack files from a download (CSV, INI, JSON, PNG, MP3) | malformed files crash the game; a path in `stadium.json` points outside the pack | validated relative paths, length-checked fields (most are already `snprintf`), size caps (the archive's 4096-file and 96-character name limits), and a fuzz pass over `teams.csv`, `era.ini` and `stadium.json` loaders |

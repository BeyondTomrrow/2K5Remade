# M9 results: era-correct team looks for My NFL Eras 1980

2026-10-16. Built from `docs/eras/research/ERA-LOGOS.md` (steps B, D and C) and the "Everything
follows the season" and "Era-appropriate scorebugs" notes in `docs/eras/PHASE2-PLAN.md`.
Nothing was committed.

**The in-game tests did not run.** EA SPORTS College Football 27 was running in the foreground
from 16:53 until at least 19:45, with its CPU time still rising, so the user was playing. One
test launch (m9a) started by mistake before I saw this, and I stopped it within seconds, at the
Main Menu load. I then waited for the game to close for about three hours, and it did not. So
everything below is built, compiled and checked offline only. The in-game checks are listed
under "Still to test".

## Status

| part | code | offline check | in game |
|---|---|---|---|
| 1. `style` column, `team_looks.json`, written to `+0x192` (launch, depth chart, Schedule/Standings records) | done | package T0.1 OK; M0 negative checks 16 caught, 0 missed (new case: Bills style 2) | **not tested** |
| 2. Eras scorebug identity, `league/season/decade/week` in the API, `mods\eras\presentations.ini` | done | builds clean | **not tested** |
| 3. Eras-only team-art texture swap (card chips, PHI/CHI logos, Bengals logo) | done; 20 replacement textures built | hashes from `build/eras/logos/hashes.json` (offline content hash) | **not tested**; the texture-dump hash check did not run |

Build: `tools\build.ps1 -Game` succeeded (log `logs\eras\m9-build.log`, UTF-16). No gen patches.

## 1. Uniform style per team

* **`data/eras/league/team_looks.json`** (new). Each team maps to a list of
  `{from_season, to_season, style, source}`.
  * It is filled for 1980 for all 28 teams.
  * Values from ERA-LOGOS.md: Bills 4, Jets 3, Bengals 3, Patriots 6, Seahawks 2, Giants 3,
    Packers 2, Vikings 2, Saints 5.
  * Every other team keeps its template's current style: BAL 5, MIA 3, CLE 1, HOU 2, PIT 1,
    DEN 2, KC 4, OAK 0, SD 4, DAL 3, PHI 5, STL 3, WAS 2, CHI 1, DET 3, TB 3, ATL 6, LA 4, SF 3.
    Note that ERA-LOGOS step 1 lists DEN 1 and KC 3, but its table marks the templates' 2 and 4
    as "=". I followed "the rest keep their current style".
  * Each `source` names the disc style and its disc year label, the cited SportsLogos or
    Wikipedia page, and whether the style replaces the template's.
  * **Other seasons are empty.** I did not add a multi-season span: the disc labels plus the
    cited pages do not make any span certain (the Bengals label is a known wrong one). A season
    with no entry keeps the template's style.
* **`build/eras/make_runtime_package.py`** reads `team_looks.json`
  (`team_style(looks, fid, season, tpl)`), writes a `style` column into `teams.csv`, and checks it
  in `--check`. `teams.csv` was regenerated. Only `teams.csv` and `PROVENANCE.txt` changed;
  rosters and depth files are byte-identical. The pre-M9 `teams.csv` is kept as
  `logs\eras\m9-teams.csv.before`.
  * **Dolphins stadium (coordinator heads-up).** The regenerated `teams.csv` now names
    **MIAMI ORANGE BOWL** (exact) for the Dolphins. The old value was PRO PLAYER STADIUM
    (standin). This follows the updated `stadiums_1966_2025.json` and is intended. If the
    Orange Bowl pack is missing at run time, the launcher falls back to the team's default
    stadium.
* **`build/eras/m0_negative_checks.py`** has a new tamper case: the Bills back on style 2. It is
  caught. Totals: 16 caught, 0 missed.
* **Game side:**
  * `src/nfl2k5_eras.h/.c`:
    * `ErasTeam.style` is read from the `style` column by header name. A package without the
      column still loads (style -1, the template's). The stadium column is now also found by
      name.
    * New `nfl2k5_eras_apply_style(record, team)` writes `+0x192` and logs
      `look <id>: uniform style S written ... (template style T)` to `eras.log`.
  * `src/nfl2k5_eras_launch.c`: the style is applied right after staging each side, before
    `sub_000E3000` / `sub_000E30E0` (step 6), so the uniform slots carry it.
  * `src/nfl2k5_eras_depth.c`: applied after staging for the office Depth Chart.
  * `src/nfl2k5_eras_sheets.c` `build_records`: `+0x192` is the team's style, else the template
    variant. This drives the Schedule/Standings mini helmets.

## 2. Scorebug and presentation identity

`src/nfl2k5_presentation.cpp`:

* **Team identity.**
  * `nfl2k5_presentation_eras_begin(season, week, home_id, away_id)` is called by the launcher
    once the game is armed. `nfl2k5_presentation_eras_end()` is called first thing in the
    after-game clean-up.
  * While an Eras game runs, each side's team comes from
    `mods\eras\teams\<season>\<eras id>\team.json` (abbreviation, city, name, colours,
    `logos/scorebug.png`). It is keyed by the launched fixture's home and away Eras ids, never
    by the playbook's 2004 code:
    * `read_state` replaces each side's code with the Eras abbreviation;
    * `team_info()` answers from the Eras teams first;
    * so BAL resolves to the Baltimore Colts and HOU to the Houston Oilers.
  * The first state logs what the playbook name said: `[PRES] eras: playbook codes away X / home Y
    -> Eras ...`. This answers ERA-LOGOS's open check once a game runs.
  * Outside Eras the lookup is the old one: `s_eras_on` is 0 and nothing else changes.
* **Logo URL.** Eras logos are served as `https://erateams.local/<season>/<id>/logos/scorebug.png`:
  * a new WebView2 virtual host on `mods/eras/teams`;
  * `PresentationHostConfig.eras_teams_dir`, set up in `src/presentation/webview2_host.cpp`;
  * `team_asset_url()` maps the path to the URL.

  JSON packages and the broadcast state get the file path as before.
* **Presentation API.** New top-level fields:
  * `league` (`"eras"` or null);
  * `season` (e.g. 1980);
  * `decade` (`"1980s"`);
  * `week` (the fixture's week).

  All are null outside Eras. They are documented in `docs/PRESENTATION-SYSTEM.md` (State table
  and the team-assets paragraph).
* **`mods\eras\presentations.ini`** (new):
  * keys are a season (`1980 = ...`), a range (`1978-1989 = ...`) or a decade (`1980s = ...`);
    the most specific key wins;
  * the value is a package folder or its display name;
  * a package that is not installed is skipped, and then the player's selection is kept.
* **Selection handling.** While the Eras package is on:
  * the player's own selection (package, intro and outro) is held;
  * `save_settings()` always writes the player's own selection, so `presentation.ini` is never
    saved over;
  * it is put back at `_end()`;
  * if the player picks a package in Coach Match Up during an Eras game, that pick wins and is
    saved.

  Quick Game, Franchise, retail and SOFTDRINK never call `_begin`, so their selection never
  changes.
* **The shipped ini maps `1980s = NFL on CBS`.** That is the JSON package, and JSON packages
  draw `{away.logo}` / `{home.logo}`, so they show the Eras logo with no package change. Change
  or comment out the line to keep your own selection in Eras games.

**Package caveat (not changed).** The HTML packages pick their own `assets/logos/<ABBR>.png`
first and use `team.logo` only as a fallback: NBC_SNF and SNF_Claude in `logoSrc`, CBS_2026 in
`image()`. That art is 2004 art, so in an Eras game they would show the Ravens for BAL. I did not
edit them, because NBC_SNF has uncommitted work of yours. The fix in each is one line: use
`team.logo` when `state.league === 'eras'`. The API docs say so.

## 3. Card badges and the Eagles / Bears / Bengals logos

* **`tools/eras/build_era_team_art.py <extracted dir>`** (new) writes
  `mods\eras\teams\1980\art\<HASH>_<what>.png`. These are disc pixels plus the user's Bengals
  image, and they are gitignored by `mods/eras/teams/**/*.png`.
  * Card chip (`<code>_flipchip_00_h*`): the 12 wrong clubs (MIA, NE, NYJ, CIN, HOU, DEN, SEA,
    NYG, PHI, TB, ATL, LA) plus DET and SD, whose 2003/2002 chips ERA-LOGOS calls only "close".
    Each gets the club's 1980 logo (`logo_<code>_<S>`, 256 px) fitted to a 128 px chip. The
    Bengals get the user's image.
  * `logo_21_5` / `logo_s21_5` come from `logo_21_6` / `logo_s21_6` (the flying eagle).
    `logo_05_1` / `logo_s05_1` come from `logo_05_0` / `logo_s05_0` (the "C").
  * `logo_06_3` / `logo_s06_3` come from the user's `mods\eras\teams\1980\bengals\logo.png`.
  * `--check` lists the plan and anything missing. This run: 20 textures, 0 missing.
* **`src/nfl2k5_texture_packs.cpp`**:
  * a second era folder list, `g_teams`, is searched before the office folders, using the same
    `era_dir` / `era_find` machinery and hash file names;
  * `nfl2k5_texpack_eras_teams(season|NULL)` sets it and bumps `g_era_gen`, so the textures
    upload again when it changes;
  * `nfl2k5_texpack_era_on()` and `nfl2k5_texpack_active()` include it;
  * the 64x64 / 128x128 / 256x256 paletted shapes already follow `gen()` in
    `nfl2k5_controller_art.cpp`.
* **On / off rule.** `nfl2k5_eras_team_art_update()` in `src/nfl2k5_eras.c` turns the swap on
  only while `nfl2k5_eras_launch_active() || nfl2k5_eras_depth_editing()`. It is called:
  * at launch, after `active = 1`;
  * at the top of `nfl2k5_eras_after_game`, which runs at the pop to the office, hub or Main
    Menu, on a backed-out Coach Match Up, and on a quit;
  * when the Eras depth chart opens;
  * at `nfl2k5_eras_depth_edit_end`.

  Quick Game and Franchise never set either flag. The office's own era context is unchanged.

## Sheet

`logs\eras\era-looks-sheet.png` (local only: retail pixels). **It is an offline sheet, not the
requested in-game sheet.** It shows each of the 20 swaps as the disc picture today next to its
Eras replacement, with the hash. The in-game rows (helmets and uniforms for BUF, NYJ, CIN, NE,
HOU and BAL; the scorebug; the Schedule mini helmets; the depth chart badges; a retail Quick Game
afterwards) were not captured.

## Still to test (in this order, background mode, sandbox root)

1. **Texture-dump run** (`NFL2K5_TEXDUMP=1`): confirm the 20 hashes upload under these values,
   in particular the flipchip hashes. The P8 content hash has to match the offline
   `content_hash`.
2. **Eras games** with `NFL2K5_ERAS_TEST_NEXT`: `colts` (Colts at Jets), `bengals` (Bucs at
   Bengals), `bills` (Dolphins at Bills), `patriots` (Browns at Patriots), `oilers_titans`
   (Oilers at Steelers). In each, check:
   * Coach Match Up art, the in-game helmets and uniforms, and the "Bills white helmet, Jets
     green JETS, Bengals orange BENGALS with no stripes" checks;
   * `eras.log` `look ...` lines and the launch line's `uniforms S/S`;
   * the `[PRES] eras game:` and `playbook codes` lines, and scorebug frames with NFL on CBS;
   * the lineup cards.
3. **Office Schedule / Standings** mini helmets, for example Bills style 4 and Jets style 3.
4. **Depth Chart cards** for the Oilers (derrick, not the Titans "T"), Patriots and Bengals.
5. **Afterwards**, in the same process: a Quick Game with the same clubs and a Franchise screen
   should show 2004 art (`[TEXPACK] eras team art off`), with the user's package selection back.

## Defects / risks

* Nothing above was seen in game (PC in use). Each step has a log line so a single run can
  confirm it.
* A non-template style must be accepted by `sub_000E3000` and the Team Select / Coach Match Up
  art (`E2A90` / `E2F20` could step an index whose year pair is zero). ERA-LOGOS says every style
  in the club's table is valid. Unverified in game.
* HTML packages show their own 2004 per-ABBR logos in Eras games until each package honours
  `state.league` (see the caveat above).
* The Bengals chip and logos use the user's helmet image; `logo_s06_3`'s hash is shared with
  `logo_s06_2`, so both are swapped (only in Eras). Shared hashes across a club's styles mean a
  swap applies to every style of that club, but only while the Eras context is on.
* `original\disc\VC_53450030.NFL2K5_sched` exists, last written at 14:54, before this session's
  run. I left it alone; an earlier test's private pack copy is the likely source.

## Housekeeping

* The sandbox root is `logs\eras\m9-root`. Its junctions were removed with `cmd /c rmdir`; the
  copied `saves\` is still there.
* The real `saves\UDATA` was MD5-checked before and after: 507 files, 0 differences.
* Scratch package build: `logs\eras\m9-pkg`.

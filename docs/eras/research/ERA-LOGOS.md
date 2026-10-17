# My NFL Eras: era-correct logos, helmets and uniforms (1980)

2026-10-16. Research and plan only: nothing in `src/` was changed, nothing was built, and the
game was not run (offline extraction from the disc was enough). Contact sheet:
`logs\eras\era-logos-sheet.png` (retail pixels; it stays local). Extraction scripts:
`build/eras/logos/` (`arc.py`, `cdf.py`, `years.py`, `htab.py`, `hashes.py`, `final_sheet.py`;
run them from a scratch folder, because they write `mini/`, `logos/` and `flipchip/` next to themselves).

## Summary

* **The disc already has era-correct helmets and uniforms for all 28 1980 teams.** Every club
  keeps a numbered uniform *style* (0..14). Each style has its own kit files
  (`<code>h<S>.iff` / `<code>a<S>.iff`) and its own front-end art, and a year range in the
  roster's uniform table. The Bengals are the only partial case: style 3 has the exact 1980 helmet
  (orange, "BENGALS", black facemask), but its jersey is black where the 1980 home jersey was orange.
* **The historic template picks the style today, and for 3 teams it picks the wrong one.** The
  Bills get the red 1987-2002 helmet, the Jets the white 1965-68 helmet, and the Bengals the 1981
  tiger stripes. No disc template sits inside the right years for these three, so no template
  swap can fix them. Six more teams have a closer style than the one their template uses (NE,
  SEA, NYG, GB, MIN, NO).
* **The fix is one number per team.** Add a `style` column to `teams.csv` and write it to team
  `+0x192` after staging. The historic files carry the franchise's whole uniform table, so every
  style is valid on a staged team. That one byte drives the in-game kit, the mini helmets on
  Schedule/Standings, and the helmet, uniform and logo art on Team Select and Coach Match Up.
* **Three surfaces still show 2004 art, and these need an Eras-only override:**
  * The roster/depth-chart card chip (`flipchip.cdf`) has one modern picture per club, whatever the
    style. It is wrong for 12 teams; the Oilers show the Titans "T".
  * The HTML scorebug reads `mods\teams\<ABBR>\logos\scorebug.png`, which is the 2004 logo (BAL
    there is the Ravens).
  * Two clubs' style logos are modern (PHI style 5 shows the 1996 eagle head; CHI style 1 shows
    the bear head).

  The disc has the right picture for 27 of 28 teams, so a tool can build every replacement from
  the player's own disc.
* **The user only needs to supply one image: the Bengals' 1970-80 logo.** No disc file has it.
  See "What the user supplies".

## 1. What the disc has

### Where team art lives

Each team has an asset code (team `+0x10C`, e.g. `06` Bengals, `28` Oilers/Titans) and a uniform
style (team `+0x192`). Everything below is keyed by (code, style):

| Surface | Disc file | Member name | Size |
|---|---|---|---|
| In-game uniform, helmet, numbers | `<code>h<S>.iff`, `<code>a<S>.iff` | TSET/TXTR | many |
| Team Select / Team Rosters / Coach Match Up big art | `logos.iff` index + `logos.cdf` (1,585 slots of 0x10500 bytes) | `logo_<code>_<S>`, `helm_h<code>_<S>`, `helm_a..`, `unif_h..`, `unif_a..` | 256x256 P8 |
| Schedule/Standings mini helmet; small logo | `mini.iff` + `mini.cdf` (953 slots of 0x1500) | `mini_a<code>_<S>`, `mini_h<code>_<S>`, `logo_s<code>_<S>` | 64x64 P8 |
| Roster / depth chart / lineup card chip | `flipchip.iff` + `flipchip.cdf` (317 slots) | `<code>_flipchip_00_h<S>` | 64x64 P8 |
| Crowd gear, cheerleaders, stadium signs, tunnel helmet | `tg<code>`, `ch<code>`, `ns/np<code>`, `ih<code>.iff` | fanplate, tshirt, boxlogo, crowdsign, inflatablehelmet... | various |

The `.iff` files of the three caches are CACR indexes: member count and slot size at 0x60, then
sorted name CRCs. The pixels are in the matching `.cdf`.

`mini.iff` members (the schedule agent's question): 317 each of `mini_a`, `mini_h` and `logo_s`,
plus `unknown_a`/`unknown_h`. Example: Rams `mini_a23_0..12`, Buccaneers `mini_a27_0..7`,
Falcons `mini_a01_0..13`. The throwbacks the schedule agent saw are these per-style members:
Buccaneers style 3 is the creamsicle, Rams style 4 is the 1978-79 helmet, Falcons style 6 is
the red 1978-79 helmet.

The card chip is `flipchip`. It matches the depth-chart badge in `logs\eras\m6a-dc-card.png`
pixel for pixel (Vikings horn on purple). All of a club's flipchip slots hold the same modern
picture, so its content hash is the same across styles: one hash per club, and no club shares
one with another (`build/eras/logos/hashes.json`).

### Style year ranges (disc)

The main roster (archive id 0x4A37581D) gives each club 14 `(first, last)` u16 pairs at team
`+0x15A`, one for each of styles 1..14. Style 0 is the 2004 kit. A pair `(2003, 1)` marks a
2003 alternate. Each historic file (`h-<code>-<year>-<name>-<style>.iff`) carries the same
table, so the full range is valid on a staged slot. Mod Studio's notes say `E2A90` treats an
index as valid when its pair is non-zero, and `E2F20` steps an invalid index down toward 0.

### Per-team table

Column notes:
* **1980 look** is from the sources cited. SL means SportsLogos.net. I could not cite the
  Gridiron Uniform Database: it renders with JavaScript and returned empty pages.
* **Disc style** is the recommended style, with its disc year range in brackets.
* **Template now** is the style the `teams.csv` template gives today.
* A mark of "=" means the template already gives the recommended style. A mark of "~" means it is
  close, but the recommended style is nearer to 1980. "FIX" means it is wrong.

| Team (code) | 1980 look | Source | Disc style (years) | Template now | Era logo on disc | Gap |
|---|---|---|---|---|---|---|
| Colts (11) | White helmet, blue horseshoe | [SL 1978 helmet](https://www.sportslogos.net/logos/view/k1u3w6ligllz1nyimydsgrnnv/Baltimore-Colts-Logo/1978/Helmet) | 5 (1968-70); 4 (1982) is the alternative | BAL_1970 style 5 = | `logo_11_5` horseshoe | Facemask: grey on style 5, navy on style 4, white in 1980 (SL). Cosmetic. |
| Bills (03) | White helmet, blue charging buffalo, blue facemask; the red helmet came in 1984 | [SL 1976 helmet](https://www.sportslogos.net/logos/view/f7huooayam1gfabojobkiloar/Buffalo-Bills-Logo/1976/Helmet), [WP](https://en.wikipedia.org/wiki/Logos_and_uniforms_of_the_Buffalo_Bills) | **4 (1976-83)**, white helmet with navy facemask | BUF_1990 style 2: red helmet. **FIX** | `logo_03_4` | None once style 4 is used |
| Dolphins (14) | White helmet, dolphin in a sunburst (new 1980 logo) | [SL 1980 helmet](https://www.sportslogos.net/logos/view/730fsmec2n5yzsfmny1kffeh6/Miami-Dolphins-Logo/1980/Helmet) | 3 (1979-80) | MIA_1984 style 3 = | `logo_14_3` old logo | The card chip is the 1997 dolphin (override). Facemask grey on the disc; aqua in 1980 per one source. |
| Patriots (16) | White helmet, Pat Patriot, red jersey | [SL 1971 helmet](https://www.sportslogos.net/logos/view/pmghrhemwcxu7q9an5v7d0nkw/New-England-Patriots-Logo/1971/Helmet), [WP](https://en.wikipedia.org/wiki/New_England_Patriots) | **6 (1975-81)** | NE_1985 style 5 (1984-87) ~ | `logo_16_6` Pat Patriot | The card chip is Flying Elvis (override) |
| Jets (19) | Green helmet, white "JETS" wordmark (1978-89) | [SL 1978 helmet](https://www.sportslogos.net/logos/view/e4ifghvyj68fzhwhzgj7iz3od/New-York-Jets-Logo/1978/Helmet), [WP](https://en.wikipedia.org/wiki/Logos_and_uniforms_of_the_New_York_Jets) | **3 (1978-85)** | NYJ_1968 style 4: white 1965-68 helmet. **FIX** | `logo_19_3` JETS | The card chip is the 1998 oval (override) |
| Bengals (06) | Orange helmet, "BENGALS", **black facemask** (1980 only); orange home jersey; stripes from 1981 | [SL 1980 helmet](https://www.sportslogos.net/logos/view/h9ybj5xf8ceimhhd9f90idoac/Cincinnati-Bengals-Logo/1980/Helmet), [Bengals.com](https://www.bengals.com/uniforms/1980-before-the-stripes), [SL 1970 logo](https://www.sportslogos.net/logos/view/3107/Cincinnati-Bengals-Logo/1970/Primary-Logo) | **3** (disc label 1968-78; the art is the 1980 helmet with the black facemask). Style 2, labelled 1979-80, wrongly shows stripes | CIN_1981 style 1: stripes. **FIX** | **None**: every `logo_06_*` is the tiger head or the "B" | **Logo: the user supplies it.** The jersey is black on style 3 (orange in 1980); accept it, or repaint later. |
| Browns (30) | Orange helmet, brown and white stripes | [SL 1975 helmet](https://www.sportslogos.net/logos/view/fwyhhhxkag7yfrgrghsewnglt/Cleveland-Browns-Logo/1975/Helmet) | 1 (1983-84) | CLE_1986 style 1 = | `logo_30_1` helmet | None |
| Oilers (28) | White helmet, derrick, grey facemask (red from 1981) | [SL 1975 helmet](https://www.sportslogos.net/logos/view/576/Houston-Oilers-Logo/1975/Helmet), [SL 1981](https://www.sportslogos.net/logos/view/577/Houston-Oilers-Logo/1981/Helmet) | 2 (1974-75), grey facemask | HOU_1979 style 2 = | `logo_28_2` derrick | The card chip is the Titans "T", and the scorebug shows the Titans (override) |
| Steelers (22) | Black helmet, Steelmark on the right | [SL 1977](https://www.sportslogos.net/logos/view/972/Pittsburgh-Steelers-Logo/1977/Helmet) | 1 (1968-76) | PIT_1978 style 1 = | `logo_22_1` | None |
| Broncos (08) | Royal blue helmet, orange "D" horse, white facemask | [SL 1980 home](https://www.sportslogos.net/logos/view/epifqfe9c5tixf4h8hy7vbmgy/Denver-Broncos-Logo/1980/Home-Uniform) | 1 (1985-96), or 2 (1969-73) | DEN_1977 style 2 = | `logo_08_1` "D" | The card chip is the 1997 horse head (override) |
| Chiefs (13) | Red helmet, arrowhead | [SL 1974](https://www.sportslogos.net/logos/view/mbtef36mxmdxosrpvl4hf3e8i/Kansas-City-Chiefs-Logo/1974/Helmet) | 3 (1970-71), or 4 | KC_1969 style 4 = | `logo_13_3` | None |
| Raiders (20) | Silver helmet, shield | [SL 1964](https://www.sportslogos.net/logos/view/f9q5ot20if84eyjqjdgy1h4gw/Oakland-Raiders-Logo/1964/Helmet) | 0 (unchanged) | OAK_1976 style 0 = | `logo_20_0` | None |
| Chargers (24) | **Navy** helmet, gold bolt, gold facemask; white jerseys at home (1978-83). Not white helmets. | [SL 1974](https://www.sportslogos.net/logos/view/z4b8kq38uw5aof6eb9kll9hug/San-Diego-Chargers-Logo/1974/Helmet) | 4 (1978-79), navy with gold facemask | SD_1980 style 4 = | `logo_24_4` | None. The chip is the 2002 bolt, close enough. |
| Seahawks (26) | Silver helmet, original hawk, grey facemask (blue from 1983) | [SL 1976](https://www.sportslogos.net/logos/view/18099701976/Seattle-Seahawks-Logo/1976/Helmet) | **2 (1976-82)** | SEA_1984 style 1 (1983-2002) ~ | `logo_26_2` original hawk | The card chip is the 2002 hawk (override) |
| Cowboys (07) | Silver-blue helmet, star | [SL 1977](https://www.sportslogos.net/logos/view/o1wgfvp4km9109vj1p4fkq212/Dallas-Cowboys-Logo/1977/Helmet) | 3 (1977-80) | DAL_1977 style 3 = | `logo_07_3` | None |
| Giants (18) | Blue helmet, italic "GIANTS" (1976-99); white jerseys at home in 1980 | [WP](https://en.wikipedia.org/wiki/Logos_and_uniforms_of_the_New_York_Giants), [SL 1976](https://www.sportslogos.net/logos/view/wht7ewlnxvejpqh8axa0bhtyi/New-York-Giants-Logo/1976/Helmet) | **3 (1976-80)** | NYG_1986 style 2 (1981-99) ~ | `logo_18_3` GIANTS | The card chip is "ny" (override) |
| Eagles (21) | Kelly green helmet, silver wings; flying-eagle logo | [SL 1974 helmet](https://www.sportslogos.net/logos/view/uw1whwij2ph1judzpgfotkoyj/Philadelphia-Eagles-Logo/1974/Helmet), [SL logo](https://www.sportslogos.net/logos/view/16754201974/Philadelphia-Eagles-Logo/1974/Helmet-Logo) | 5 (1979-80) | PHI_1980 style 5 = | Flying eagle in `logo_21_6` / `logo_s21_6`; style 5's own logo is the modern head | Style 5's logo and the card chip are modern. Override from style 6 (disc). |
| Cardinals (00) | White helmet, cardinal head | [SL 1960](https://www.sportslogos.net/logos/view/36jfqw30f7uckxe3e2myp5ofr/St-Louis-Cardinals-Logo/1960/Helmet) | 3 (1971-72) | STL_1975 style 3 = | `logo_00_3` | None |
| Redskins (29) | Burgundy helmet, Indian head, gold facemask (1978-) | [SL 1978](https://www.sportslogos.net/logos/view/1065/Washington-Redskins-Logo/1978/Helmet) | 2 (1982) | WAS_1982 style 2 = | `logo_29_2` | Facemask grey on styles 2/3 (cosmetic) |
| Bears (05) | Navy helmet, orange "C", grey facemask | [SL 1973](https://www.sportslogos.net/logos/view/rfj55oyfob8zsrubdjyvetcna/Chicago-Bears-Logo/1973/Helmet), [WP](https://en.wikipedia.org/wiki/Logos_and_uniforms_of_the_Chicago_Bears) | 1 (1974-82) | CHI_1985 style 1 = | "C" in `logo_05_0` / `logo_s05_0`; style 1's own logo is the bear head | Logo override from style 0 (disc) |
| Lions (09) | Silver helmet, leaping lion | [SL 1970](https://www.sportslogos.net/logos/view/e0ffv95gmh18txqrzfhwrk6dp/Detroit-Lions-Logo/1970/Helmet) | 3 (1980-93) | DET_1991 style 3 = | `logo_09_3` | None. The chip is the 2003 lion, close. |
| Packers (10) | Gold helmet, "G", green facemask (1980-) | [SL 1980 helmet](https://www.sportslogos.net/logos/view/559/Green-Bay-Packers-Logo/1980/Helmet) | **2 (1983-84)**, green facemask | GB_1966 style 3 (grey facemask) ~ | `logo_10_2` | None |
| Vikings (15) | Purple helmet, horn (new 1980 entry) | [SL 1980 helmet](https://www.sportslogos.net/logos/view/17231951980/Minnesota-Vikings-Logo/1980/Helmet) | **2 (1980-84)** | MIN_1973 style 3 (1972-73) ~ | `logo_15_2` | None |
| Buccaneers (27) | White helmet, Bucco Bruce, orange jersey | [SL 1976](https://www.sportslogos.net/logos/view/17696191976/Tampa-Bay-Buccaneers-Logo/1976/Helmet) | 3 (1976-91) | TB_1979 style 3 = | `logo_27_3` Bruce | The card chip is the flag (override) |
| Falcons (01) | **Red** helmet (black only from 1990), black falcon | [SL 1976](https://www.sportslogos.net/logos/view/2h5gltkmz9ku66ynff8jkedzp/Atlanta-Falcons-Logo/1976/Helmet), [SL 1978 home](https://www.sportslogos.net/logos/view/nspdzpqselfcw15exppau2g2i/Atlanta-Falcons-Logo/1978/Home-Uniform) | 6 (1978-79) | ATL_1980 style 6 = | `logo_01_6` | The card chip is the 2003 falcon (override) |
| Rams (23) | Blue helmet, yellow horns, grey facemask (blue from 1981) | [SL 1973](https://www.sportslogos.net/logos/view/865/Los-Angeles-Rams-Logo/1973/Helmet), [SL 1981](https://www.sportslogos.net/logos/view/864/Los-Angeles-Rams-Logo/1981/Helmet) | 4 (1978-79) | LA_1979 style 4 = | `logo_23_4` (helmet) | The card chip is the 2000 St. Louis horn (override) |
| Saints (17) | Gold helmet, fleur-de-lis, black facemask | [SL 1976](https://www.sportslogos.net/logos/view/911/New-Orleans-Saints-Logo/1976/Helmet) | **5 (1976-82)** | NO_1991 style 4 (1991-92) ~ | `logo_17_5` | Facemask grey on the disc (cosmetic) |
| 49ers (25) | Gold helmet, SF oval, grey facemask | [SL 1964](https://www.sportslogos.net/logos/view/jzh5fu3cy78v1o3u1m024pp0b/San-Francisco-49ers-Logo/1964/Helmet), [WP](https://en.wikipedia.org/wiki/Logos_and_uniforms_of_the_San_Francisco_49ers) | 3 (1964-95) | SF_1981 style 3 = | `logo_25_3` | None |

Counts:
* **Helmet and uniform:** era-correct on the disc for all 28 teams; the Bengals jersey colour is
  off. The current templates already select an era-correct helmet for 25 teams.
* **Big and small logo:** the per-style art is era-correct for 25 teams. Another 2 (PHI, CHI)
  can borrow from another of their own styles. The Bengals have none.
* **Card chip:** wrong for 12 teams (MIA, NE, NYJ, CIN, HOU, DEN, SEA, NYG, PHI, TB, ATL, LA).
  All 12 can be rebuilt from disc art except the Bengals.
* **Scorebug:** wrong wherever the 2004 logo differs, and the identity itself is a problem (see
  section 3).

## 2. How Eras picks these today

* `teams.csv` `template_entry` loads a disc historic team into the USER slot
  (`nfl2k5_teams_stage`). The template gives the slot its asset code (always the club's) and its
  default style `+0x192`. The launch (`src/nfl2k5_eras_launch.c`, step 6) passes that byte to
  `sub_000E3000`/`sub_000E30E0` → `[0xE60210]/[0xE60214]`, and the game loads `<code>h<S>.iff`.
  The Schedule/Standings copies (`src/nfl2k5_eras_sheets.c build_records`) copy the template
  entry's variant (entry `+2`) into `+0x192` of their Eras records, so the mini helmets follow
  the template as well.
* Choosing a better template does not help:
  * There is no disc template within the right years for the Bills (1964 and 1990 only), the
    Jets (1968, 1998) or the Bengals (1981, 1988).
  * For the six "~" teams, every template of the club is further away than the style itself.
  * A template also brings other fields (read from the files):
    * a 2004 stadium and city (`h-11-1970-colts-5.iff` says "RCA Dome, Indianapolis, IN"); the
      launch uses it when the `teams.csv` stadium is not found;
    * a 2004 head coach ("Tony Dungy", "Jeff Fisher", "Mike Martz"), unless the coach comes from
      `coaches.csv`;
    * 53 players (the roster is replaced by staging anyway, up to the template's 53).

    Colours and crowd come from the code, which every template of a club shares.
* **So keep the templates and override the style.** It is one byte, and it is valid for any style
  in the club's table, which every historic file carries in full (section 1).

## 3. Gaps and the cleanest Eras-only fill

| Gap | Teams | Fill | Images from |
|---|---|---|---|
| Kit and helmet of the wrong era | BUF, NYJ, CIN (+6 closer) | `style` column → `+0x192` | disc (nothing to supply) |
| Card chip (`<code>_flipchip_00_h<S>`) | 12 | Eras texture context, keyed by content hash | disc `logo_s<code>_<S>` (64x64, the same size and format), except CIN |
| Style logo modern (`logo_21_5`, `logo_s21_5`, `logo_05_1`, `logo_s05_1`) | PHI, CHI | the same context | disc `logo_21_6`, `logo_s21_6`, `logo_05_0`, `logo_s05_0` |
| Bengals logo (`logo_06_3`, `logo_s06_3`, chip) | CIN | the same context | **user** |
| HTML scorebug logo and identity | all 28 | Eras team folder looked up before `mods\teams` while an Eras game runs | disc `logo_<code>_<S>` (256x256); CIN from the user |
| Office monitor / notebook sticker (`skin_team_logo`) | the user team | fall back to the era scorebug logo instead of `mods\teams\<ABBR>` | as above |
| Crowd gear, stadium signs, tunnel helmet, cheerleaders (`tg/ns/np/ih/ch<code>.iff`) | all | later, the same context | 2004 logos plus details such as an "INDIANA ... 04" licence plate for the Colts. Not part of this request. |

The three mechanisms the request names:

* **`mods\teams\<ABBR>\logos\scorebug.png`** (`src/nfl2k5_presentation.cpp` `load_teams` /
  `team_info`):
  * Keyed by the abbreviation, which the presentation reads from the playbook file name
    (`B307D0`/`B30810`, "XX-pb.iff"). For a historic team that is probably the 2004 club's
    playbook; this needs a check in a live game.
  * The folders are the 2004 clubs (BAL is the Ravens, HOU the Texans, STL the Rams, and there is
    no LA folder), so a plain per-ABBR file cannot serve Eras.
  * Put the Eras logos in an era folder that is looked up first while
    `nfl2k5_eras_launch_active()`. Map each side to the launched fixture's home or away Eras team
    id, which avoids every abbreviation clash.
* **The texture override path** (`g_xbox_tex_override` in `src/nfl2k5_controller_art.cpp` →
  `nfl2k5_texpack_find` in `src/nfl2k5_texture_packs.cpp`; content hash `tex_content_hash` in
  `external/xboxrecomp/src/kernel/nv2a_gpu_d3d11.inc.c`):
  * It already has an era context, `nfl2k5_texpack_era(season, decade)`. The office sets it
    while its room shows. Lookups go through the folder list, then the hash files and `[grade]`
    lines, and `g_era_gen` bumps the cache on every change. `wants()` already asks for every P8
    texture while an era is on.
  * The card chips and logos are all P8 64x64 or 256x256, so the same machinery fits.
  * Hashes can be computed offline with the office's rule (`build/eras/office/office_inventory.py
    content_hash`); the values are in `build/eras/logos/hashes.json`. Examples: the Oilers chip is
    `D12EDDC8`, `logo_21_5` is `6209B7FE`, `logo_05_1` is `4498C540`, `logo_06_3` is `51049C50`.
* **A name-keyed cache-slot override** (extending `nfl2k5_team_files.c` CACHE_EXTRA_READ to
  existing indices) would need a new gen patch, and it fights the game's resident slot cache when
  the context changes. Not recommended.
* **Rewriting the archive** with spare styles, the way Mod Studio's `nfl2k5_historic_styles`
  does, changes retail and Franchise for everyone. Rejected: Eras must not touch them.

## 4. Approaches: effort and risk

| Approach | Fixes | Effort | Risk |
|---|---|---|---|
| A. Swap templates | nothing for BUF/NYJ/CIN; a closer style for none of the "~" teams | data only | Brings in another template's stadium, coach and ratings. Not recommended. |
| **B. `style` column** (`teams.csv` → `ErasTeam.style`; write `+0x192` after staging in `nfl2k5_eras_launch.c`, and use it in `nfl2k5_eras_sheets.c build_records`) | In-game kit and helmet for all 28; mini helmets; Team Select and Coach Match Up art; per-style big and small logos for 25 | ~30 lines of C, plus a column in `build/eras/make_runtime_package.py`, plus the loader | **Low.** Every style is valid on the staged slot (the table is in the template file), and Franchise and Quick Game are untouched. One check: the game must accept a style that is not the template's own; the retail uniform picker does exactly that. |
| **C. Eras team-art texture context** (`mods\eras\teams\<season>\art\<HASH>.png`, on while an Eras screen or an Eras game is up) | Card chips (12), PHI and CHI logos, Bengals logo | ~80 lines of C++ in `nfl2k5_texture_packs.cpp` (a second folder list beside the office's), plus calls from the Eras menu, launch and after-game; plus a ~200-line disc tool | **Medium-low.** Each hash must match the upload; check once with `texture_dump = 1`. The context must stay on through Coach Match Up and the match and switch off at the after-game pop, or a retail screen would show 1980 art. Hashes are unique per club, so nothing outside these 28 teams changes. |
| **D. Era scorebug identity** (`team_info()` asks Eras first; `mods\eras\teams\<season>\<id>\team.json` + `scorebug.png`; extend the `teams.local` URL mapping) | Scorebug and presentation logo, abbreviation, city, name and colours for all 28 | ~40 lines of C++ | **Low.** Every HTML package (NBC SNF and the others) gets the logo through the team JSON or URL. Check that each one uses it rather than its own per-ABBR art. |
| E. Crowd, stadium and tunnel art | Signs, fan gear, inflatable helmet | large (many textures per club) | Needs art for every club; later |

**Recommended: B, then D, then C.** B alone fixes the most visible thing, the helmets and
uniforms in the game.

## 5. Implementation steps

1. **Data.** Add a `style` column to `data/eras/runtime/1980/teams.csv` (written by
   `build/eras/make_runtime_package.py`), with one recommended value per team:
   * BAL 5, BUF **4**, MIA 3, NE **6**, NYJ **3**, CIN **3**, CLE 1, HOU 2, PIT 1, DEN 1, KC 3, OAK 0, SD 4, SEA **2**;
   * DAL 3, NYG **3**, PHI 5, STL 3, WAS 2, CHI 1, DET 3, GB **2**, MIN **2**, TB 3, ATL 6, LA 4, NO **5**, SF 3.

   Accept a missing or empty column as "the template's" (-1). Keep `template_entry` as it is.
2. **Loader.** Add `int style` to `ErasTeam` (`src/nfl2k5_eras.h`). Parse the column by header
   name in `nfl2k5_eras.c`; a table of 9 columns or more still loads.
3. **Launch.** In `nfl2k5_eras_launch.c` step 6, before `sub_000E3000`/`sub_000E30E0`, add
   `if (t->style >= 0) MEM8(slot[s] + 0x192) = t->style;` for both sides.
4. **Sheets.** In `nfl2k5_eras_sheets.c build_records`, set `variant = e->style >= 0 ? e->style :
   template variant`.
5. **Test B** (game copy, `NFL2K5_PRIVATE_PACKS=1`, sandbox root):
   * Bills at Jets, then Bengals at Oilers. Check the helmets and jerseys in game, the Coach Match
     Up uniform label ("1976-1983 Uniform"), and the mini helmets on Schedule and Standings.
   * Diff `[0xE60210]/[0xE60214]` and the restored snapshot.
   * Confirm Quick Game and Franchise are unchanged.
6. **Disc tool** `tools/eras/build-team-art.py`: reads the player's disc and writes
   `mods\eras\teams\1980\`, which is gitignored like `mods\eras\office\files\`.
   * `<id>\scorebug.png` (256x256 from `logo_<code>_<logo style>`), and `<id>\team.json` (the
     `teams.csv` abbreviation, city and nickname, plus 1980 colours).
   * `art\<HASH>.png` for the 12 card chips (from `logo_s<code>_<S>`, 64x64) and for
     `logo_21_5`, `logo_s21_5`, `logo_05_1` and `logo_s05_1`.
   * A user file `<id>\logo.png`, when present, replaces the disc source for that team. The
     Bengals need one; see below.
   * `--check` lists the hashes and the files missing.
7. **Texture context (C).** In `nfl2k5_texture_packs.cpp`:
   * Add `nfl2k5_texpack_eras_teams(const char *season)`, a second active folder list searched
     before the office's, with `g_era_gen++` on every change, and make `era_on()` true while it is
     set.
   * Call it from the Eras menu, office, sheets and depth screens (on), from `nfl2k5_eras_launch`
     (on), and from the after-game clean-up and the Eras exit (off).
8. **Scorebug (D).** In `nfl2k5_presentation.cpp`, `team_info()` takes a side. While
   `nfl2k5_eras_launch_active()`:
   * read the launched fixture's home and away team ids;
   * load `mods\eras\teams\<season>\<id>\team.json` and `scorebug.png`;
   * map them to `https://teams.local/eras/<season>/<id>/scorebug.png`.

   Point the office `skin_team_logo` fallback at the same file.
9. **Test C and D.** Run with `texture_dump = 1` once to confirm the hashes. Check the depth chart
   and lineup cards (Oilers derrick, not the Titans "T"), the PHI and CHI logos, and the
   scorebug. Then run a retail Quick Game with the same clubs to confirm the 2004 art there.
10. **`.gitignore`:** add `mods/eras/teams/*/art/` and `mods/eras/teams/*/*/*.png`. `team.json`
    holds no pixels and can be committed.

## What the user supplies

Only for the one gap that no disc file covers:

| File | Content | Format |
|---|---|---|
| `mods\eras\teams\1980\bengals\logo.png` | The Bengals' 1970-1980 primary logo (the "BENGALS" wordmark logo, see the [SL 1970 primary logo](https://www.sportslogos.net/logos/view/3107/Cincinnati-Bengals-Logo/1970/Primary-Logo) page) | PNG, RGBA with a transparent background, square, **512x512** (256x256 minimum), logo centred with about 4% margin. The tool scales it to 256 (scorebug, `logo_06_3`) and 64 (`logo_s06_3`, card chip). |

Optional:
* `mods\eras\teams\1980\<id>\logo.png`, in the same format: any team where the user prefers
  another picture to the disc's.
* `mods\eras\teams\1980\<id>\chip.png`: a 64x64 (or 256x256) square crop for the depth-chart
  chip, if the scaled logo reads poorly at that size. Without it, the chip is the scaled
  `logo.png` or the disc's `logo_s`.

Nothing is drawn by us. Every other replacement is the player's own disc art, moved between
slots of the same club.

## Open checks

* The scorebug abbreviation for a historic team: whose playbook file name the presentation reads
  (`B307D0`/`B30810`) in an Eras game. Log it once.
* Coach Match Up's background scroll showed "DALLAS COWBOYS" in an Eras game
  (`logs\eras\m8a-cmu.png`). It is not per-team art from the files above; find its source if it
  should change.
* Stadiums: a stand-in stadium shows its 2004 team branding (`logs\eras\m8a-g2.png`, Seahawks
  signage). That is an Eras stadium-pack question (`docs/eras/research/STADIUMS.md`), not part of
  this plan.

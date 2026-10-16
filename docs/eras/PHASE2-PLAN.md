# My NFL Eras: phase 2 plan (first playable slice)

2026-10-15. This plan builds on the phase 1 architecture (`docs/eras/PHASE1-ARCHITECTURE.md`),
five research reports under `docs/eras/research/` (RESULT-CAPTURE, MATCH-LAUNCH, NATIVE-MENU,
COMMUNITY-ROSTERS, LEAGUE-HISTORY) and the verification passes run on them.

**Status: nothing in this plan has been built or run for My NFL Eras.** Every engine finding
below comes from reading code (generated code, the retail XBE, project sources). The only
exceptions, marked as such, are facts the project's already-working code depends on, such
as the scorebug addresses, the live-QB stat call and historic teams in Quick Game. A step
counts as done only when its in-game test passes.

Labels:

| label | meaning |
|---|---|
| **VERIFIED** | read in code or XBE bytes and confirmed by a second, independent reading (or used by working project code, where stated) |
| **STRONG** | several static readings agree; not yet seen at run time |
| **INFERRED** | follows from the code structure; needs a run-time check |
| **UNVERIFIED** | a guess, a second-hand label, or not looked at |
| **RUNTIME** | needs an in-game test; none has been run |

Historical facts in this plan come from the verified league data
(`data/eras/league/*.json`, sources listed there and in `research/LEAGUE-HISTORY.md`).
Rosters come from **community PS2 roster saves, which are unverified**.

---

## 0. Summary

**The slice.** From the game's own front end the player can:

1. open "My NFL Eras";
2. start a **1980** league with that season's 28 franchises and alignment;
3. see the next scheduled game and launch it (user against CPU, or CPU against CPU);
4. play it in the normal match engine, with both teams staged in the two spare team slots;
5. have the final score and box score captured at the final whistle;
6. come back to a standings screen that includes the result;
7. quit and later continue the league.

**Build order.** M0 (data) can run in parallel with M1 and M2.

| step | what it builds | what its test proves |
|---|---|---|
| **M0** | 1980 runtime package: teams, schedule, rosters, provenance (offline Python) | schedule and rosters are consistent with the verified 1980 standings |
| **M1** | result capture: gen patches at the game-over and match-exit points, tested in **retail Quick Game** | the hook fires exactly once; the score and box score match the game's own stats screen; the abandon path works |
| **M2** | team staging: a historic team as template plus a database roster written into USER1/USER2 (debug trigger in Team Select) | the right names, numbers and positions reach the match, and the players at the hook map 1:1 to database rows |
| **M3** | native menu shell and league save: hub, league screen, standings (no launch yet) | the menus work in the game's own UI; the league persists across restarts |
| **M4** | launch from the league screen (Coach Match Up route, "Option A"), commit the result, update standings, then return to the Eras screen | the full loop |
| **M5** | slice acceptance: two games in a row, restart, quit and abandon, overtime, user-controlled side | definition of done (section 2.1) |

**Why this order.** Result capture (M1) is self-contained and runs on the existing Quick Game
path. It can be tested with the drivers the project already has (`tools/drive-quickgame.ps1
-Retail`, `NFL2K5_QUARTER_SECONDS=60`), so the most important piece of static analysis gets
its first run-time check before anything depends on it. Staging (M2) reuses the historic-team
loader, which already works in game. The launch (M4) has the most unknowns: it runs retail
match code from a screen other than Team Select. It therefore comes last, when every other
part is already proven.

**Decisions that need the user** (details in section 6): season (1980 is recommended; 1976 has
the cleanest rosters); where "My NFL Eras" goes if the main menu cannot show an 8th row; what
happens to the games in a week that the user does not play; and what to do when a Create-a-Team
team occupies USER1/USER2.

---

## 1. What is established, what was refuted, what is unknown

### 1.1 Established

Engine (static; second reader agreed unless noted):

| area | finding | label | doc |
|---|---|---|---|
| Final whistle | `sub_000A2910` is entered only by the tail jump at `0xAEDA6` in `sub_000AED90` (the entry callback of flow state **0x0A**, game over). At its first label `ecx` = home points and `edx` = away points. | VERIFIED | RESULT-CAPTURE §0, §2.4 |
| Exactly-once guard | `[0xE602B8]` still holds the old state when `sub_000A2910` runs. All 23 callers of `sub_000AF260` write it only after the call returns. It is zeroed per match by `sub_00055DD0` / `sub_00055B70`. | VERIFIED | §2.4; check 2 |
| Game-over paths | Clock expiry: `sub_00157D40` -> `sub_000A2970` -> `sub_000B8A60` -> `sub_000B8A00(5)` -> pending 0x19 -> state 0x0A. Post-play evaluator `sub_000A11F0`: overtime sudden death, or Q4 with clock 0, a PAT pending and the scoring team leading. Both paths reach state 0x0A. | VERIFIED (path B), STRONG (path A, now confirmed by the second reader) | §2.2-2.3 |
| No ties in exhibition | Outside mode 7, a tied overtime expiry starts another overtime period. An Eras game run in mode 4 **cannot end tied**. | VERIFIED (code path) | check P5 |
| Live score and clock | Home score object `0xB25E30`, away `0xB25E48` (+0 points, +4 timeouts), reached through `[0xE5FC28]` / `[0xE5FC68]`. Period `[0xE602C4]` (5 or more = overtime); clock `[[0xE6028C]+0x10]`. | VERIFIED (the HTML scorebug reads them in game) | §1.1 |
| Stat accessors | `sub_000CB240` (player) and `sub_000CB2D0` (team-data record): `ecx` = entity, `edx` = selector, one stack word = scope, result in ST0, `ret 4`. Scope 0 = match, 1-4 = quarters, **5 = overtime** (one slot, `+0x264`, shared by every OT period), 6/7 = halves, 11 = season. | VERIFIED (the player accessor is already used by `nfl2k5_guest_player_stat`) | §4.1; check P3 |
| Scope 11 | Season scope **already includes the match in progress** for the two registered match teams. Never add scope 0 on top of it. | VERIFIED (team path; player path not checked) | check P4 |
| Team blocks, rosters | Live team stat blocks: home `0xBF08E0`, away `0xBF0B5C`. Team-data records: home `0xB30864`, away `0xB30A58` (up to 71 player pointers, count byte at `+0x11C`). Player `+0x10`/`+0x14` names, `+0x20` jersey bits, `+0x35` position, `+0x30` -> live stat slot. | VERIFIED | §4.2-4.3 |
| Franchise commit | `sub_000645D0` (match exit) calls the franchise commit `sub_000C5D60` only in **modes 5, 6, 7**. An Eras game must therefore run in **mode 4**, or it would mark cells in the retail franchise grid. | VERIFIED | §3; check 10 |
| Match start | No retail "start (home, away)" function exists. Game state `0x4E7EC0` reads home `[0xE5FE68]` and away `[0xE5FE6C]` and deep-copies them into `0xB30864` / `0xB30A58`. The Quick Game chain is `sub_0034CA40` -> push `0x55D088` (enter `sub_0034BC10` copies the teams) -> Coach Match Up `0x585474` -> Start Game `sub_00367400` -> `sub_0034C1B0` pushes the game. | VERIFIED | MATCH-LAUNCH §2 |
| Mode word | Team Select's enter `sub_002C20F0` writes `[0xE5FF80] = 4`. This answers RESULT-CAPTURE's open question "Quick Game = 4?" statically; the run-time value still has to be logged. | VERIFIED (static) | MATCH-LAUNCH check 8 |
| Return path | After the game, Coach Match Up's msg 3 `sub_0034C1F0` re-allocates `[0xAE84D4]` and pops to the Main Menu `0x515660` (`loc_0034C201`). The bytes there are `8B CE` (`mov ecx,esi`), `BA 60 56 51 00` (`mov edx,0x515660`), `5E` (`pop esi`) at `0x34C208`, then `E9 ..` (`jmp sub_0006E450`). | VERIFIED (code, and XBE bytes read for this plan) | MATCH-LAUNCH §2.2 step 10 |
| Setters | set_home `0x2C1780` / set_away `0x2C17C0`. The stadium is set only when mode >= 4. set_home also calls `sub_002C0A90`, a Team Select texture lookup that would crash on a null stadium pointer. Franchise mode sets the same globals without that call (`sub_0015DB50`). | VERIFIED | MATCH-LAUNCH §2.4; check 7 |
| Settings | Quarter length `[0xE6000C]`, difficulty `[0xE5FF84]` (+ preset applier `sub_000E3740`), play calling, game speed, time of day, weather, penalties: all getters read the stated addresses. | VERIFIED | MATCH-LAUNCH §4 |
| Menus | Each screen is a 0x34-byte FSM descriptor (FSM `0xA84B18`). Push `sub_0006E390`, pop `sub_0006E400`, replace `sub_0006E2E0`, pop-to `sub_0006E450`. Row types: 0 link, 9 action (`+0x28`, `ecx` = FSM, `edx` = &item), 5/7 value rows. `+0x2C` hides or greys a row. Event-table action types include 1 = call and 10 = push. Limits: 32 rows per list, at most 9 visible rows (7 on `navigation`), FSM depth 32. | VERIFIED | NATIVE-MENU §1 |
| Main menu | Descriptor `0x515660`, 7 rows at `0x5154C0`, layout `main_menu_sub`, flags `0x02`. | VERIFIED | NATIVE-MENU §3 |
| C-served functions | `recomp_lookup_manual` resolves `0xFEC0xxxx` (video menu) and `0xFED0xxxx` (old-school pads) ahead of the generated lookup (`src/recomp_manual.c:2584-2593`). | VERIFIED (works in game) | NATIVE-MENU §2.2 |
| Interpreter hazard | Under a mod pack, `native_for()` sends every target at or above `0xFE000000` to the kernel lookup only (`src/interp/x86_interp.c:339-346`). Gen patches do not fire inside interpreted functions. | VERIFIED | NATIVE-MENU §2.2 |
| Historic teams | All 75 disc historic teams load into USER1/USER2 with the game's own loader, each with 53 players, and play in Quick Game. | VERIFIED (in game, phase 1) | HISTORIC-TEAMS-INVENTORY |
| Roster writer | `nfl2k5_team_roster` (`src/nfl2k5_team_files.c:420`) writes names, position, jersey, face, height, weight, years pro, skin, hand, 28 ratings, depth and photo into the players an import created. | VERIFIED (used by added teams) | MATCH-LAUNCH §1.3 |

Data:

| area | finding | label | doc |
|---|---|---|---|
| 1980 league | 28 teams (14 AFC / 14 NFC). Divisions: AFC East 5, Central 4, West 5; NFC East 5, Central 5, West 4. 16 games, 10 playoff teams, Wild Card round. Every team, record and division matches two sets of PFR Wayback snapshots and the NFL Record & Fact Book ("Past Standings", pp. 388-393). | VERIFIED | LEAGUE-HISTORY; `nfl_seasons_1970_1980.json` |
| 1970-1980 overall | Same verification for every season. Four tied, non-playoff pairs follow the NFL's official order, not PFR's (1974 AFC East, 1976 NFC West, 1977 AFC East, 1977 AFC Central). | VERIFIED | LEAGUE-HISTORY check |
| Franchise lineage 1970-2025 | 67 events. 66 were confirmed by a second source; one note was corrected (the Raiders spent 13 Los Angeles seasons, 1982-1994). | VERIFIED | `franchise_events_1970_2025.json` |
| Community rosters | 48 PS2 saves, 1970-2022, all extracted. Integrity checked: 43 CRC-32, 5 HMAC-SHA1. Every club slot has 53 players (2012 IND has 52). The content is **unverified history**. | VERIFIED (extraction), UNVERIFIED (content) | COMMUNITY-ROSTERS |
| 1980 roster save | `ros1980`. In the 28 slots a 1980 league uses: 5 cloned-body records, 24 duplicate-player records, 11 implausible heights or weights, and every slot has at least one QB, K and P (counted for this plan). Its birth-year convention is "mixed or unclear", so ages are unreliable. | VERIFIED (counts) | COMMUNITY-ROSTERS; `community_rosters/1980.json` |

### 1.2 Refuted or corrected by the verification passes

| claim in a report | correction | consequence for the plan |
|---|---|---|
| `sub_000A130D` is a working copy of the post-play evaluator (RESULT-CAPTURE §2.3) | At `0xA130D` the bytes are `pop esi; ret`, a shared epilogue; the code after it is dead. Path B has one live site (`0xA1373`). | None for the hook. Never patch "both copies". |
| `is_final()` "misses untimed downs" (RESULT-CAPTURE §6) | It misses overtime sudden death and ties, **and** it reports a game as final too early: at Q4 0:00 with an untimed down owed (bit 7 of `[0xE602FC]`), or when a trailing or tied scoring team still has a PAT to kick. | Eras must use the new hook, never `is_final()`. Separately, the HTML `GAME_ENDED` event could be driven from the hook (optional). |
| Scope 5 = OT is "inferred" | Verified: jump table `0xCA444` maps scope 5 to `+0x264`; the writer clamps the period index to 0..4. | Every OT period's points sit in one slot; the scoreboard total stays authoritative. |
| Option B cleanup: "call `sub_0034BCD0` after the game" (MATCH-LAUNCH §5) | **Heap corruption.** `sub_0034C1B0` has already freed `[0xAE84D4]` and zeroed it. `sub_0034BCD0` -> `sub_0034B000` -> `sub_00048870(0)` frees null with no check. | Option B is out of the slice. When it is built: call `sub_0034BD30` first (as retail does), or restore `[0xE5FE68]/[0xE5FE6C]` from `[0xAE81B4]/[0xAE81B8]` by hand. |
| Supersim `0x10B940`: home/away in ECX/EDX (MATCH-LAUNCH summary) | `0x10B940` takes them as **stack** arguments 1 and 2 (`0x10B280` does take ECX/EDX plus six stack words). `0x10B280` overwrites the live match block (calls `sub_00061730`), and `sub_0010B980` writes `[0xE602C4]`. | Not used in the slice. If ever used: never while a match is set up or running. |
| A kind-9 action can use a fake address "as the video menu does" | `nfl2k5_video_menu_lookup` returns NULL for slot > 6, and the action pointer is not a value-row slot. | The Eras menu gets its own range `0xFEE00000` and its own lookup (NATIVE-MENU recommends it anyway: `0xFEC` is the AC97 window). |
| "All settings are dwords in the settings block" (MATCH-LAUNCH §4) | Playbooks `0xE5FE78/7C`, stadium `0xE5FE64`, home/away `0xE5FE68/6C`, uniform slots `0xE60210/14`, port sides `0xE5FE88+port*0x1C` and the live `0xE602B0/0xE602C4` are outside `0xE5FF80..0xE60260`. | The Eras snapshot saves and restores those addresses as well as the 0x2E0-byte block. |
| Mod tools: team selector `0x62` = "total_yards" | The game's own labels put it after "Penalties"; probably penalty yards. Total yards is `0xD0`. | Settled in M1. Until then the box score does not label `0x62` as total yards. |
| League data: tie order; "same-division rule is Wikipedia-only"; tied wild-card hosts open | Corrected to the NFL order; rule now sourced to the NFL book; the 1978 and 1980 hosts answered. | None for the slice (no playoffs yet). |

Classification issues that were left in the data (no wrong facts): Patriots 1971 is filed as
`rename` although the team also moved; the Ravens 1996 are `new_franchise` although the NFL
regards them as an expansion team; 1969 NFL conferences are null.

### 1.3 Still unknown

| unknown | blocks | settled by |
|---|---|---|
| The hook fires once per game at run time; `[0xE5FF80]` is 4 in Quick Game; `ERAS_MATCH_EXIT` runs after the final | M1 | M1 tests T1.1-T1.3 |
| Quitting from the pause menu reaches `sub_000645D0` without state 0x0A | abandon detection | T1.3 |
| Meaning of team `0x62`, units of `0x5F`, player `0x30`, `0x63` at scope 0 | box-score labels | T1.1 (compare with GameCast Stats) |
| Players at the hook are in the same order as the staged slot (through two copies) | mapping stats to database players | T2.2 (name-pointer check) |
| Option A works from a screen other than Team Select; nothing reads Team Select state (`0xACF66C..94`) during the match | M4 | T4.1 |
| Port side 0 = CPU; whether a side-1/2 port must also be "connected" | user and CPU sides | T4.1, T5.4 |
| `sub_002C0A90` (Team Select texture lookup) outside Team Select | whether set_home can be used | avoided: M4 uses the individual setters, as Franchise does |
| Readers of `[0xBA04B0+side*4]`, `[0xAE844C]`, `[0xAE84D4]` | Option B only | after the slice |
| `main_menu_sub` shows an 8th row (with or without flag `0x04`) | menu placement | T3.1 |
| Buttons behind messages 12-15 on a `spreadsheet` screen; help icons for ids 7-9; text-call arguments | Era Select carousel | after the slice (the slice uses link and settings lists only) |
| Whether any pack build interprets the menu code | packs | after the slice; the slice is `--retail` only |
| **1980 schedule** (not collected) | "scheduled game" | M0 task D2 |
| Lineage of the four renamed slots in `ros1980` (ARZ = St. Louis Cardinals, IND = Baltimore Colts, STL = Los Angeles Rams, TEN = Houston Oilers) | correct rosters for 4 of 28 teams | M0 task D3 (only the 1970 Colts slot was spot-checked, by COMMUNITY-ROSTERS) |
| 1980 overtime and tie rules, and the tiebreak procedure | rule fidelity, playoff seeding | not researched; not needed for the slice |
| Whether a disc historic uniform resembles the 1980 uniform | presentation fidelity | not claimed; the user can override per franchise |
| Whether template special-teams slots (`+0x194..+0x199`, mod tools) still point at a kicker or punter after the roster writer changes positions | correct kicker or punter in game | T2.1 |
| Whether the PS2 retail roster equals the Xbox disc roster | retail-leftover detection only | not needed |

### 1.4 How the reports fit together

* RESULT-CAPTURE's open question "Quick Game mode = 4?" is answered statically by MATCH-LAUNCH
  (Team Select enter writes 4). Eras writes 4 itself before every launch.
* MATCH-LAUNCH's open question "where is the final score at End Of Game?" is answered by
  RESULT-CAPTURE's hook, which runs before End Of Game.
* NATIVE-MENU §4.5 ("playing a game uses the native Team Select") is superseded by
  MATCH-LAUNCH's Option A, which skips Team Select and goes straight to Coach Match Up.
* The franchise abbreviations in the league data are a display convention, not history
  (LEAGUE-HISTORY: Raiders 1982-1994 shown as `LARD`).
* Found in passing (NATIVE-MENU, verified): in the uncommitted Extras change,
  `vm_remove_links` drops only type-0 rows, so Extras' "ESPN VIDEOGAMES" (a type-9 row) is never
  removed. This is unrelated to Eras.

---

## 2. The slice

### 2.1 Definition of done

All of these must pass on `NFL2K5.exe --retail`:

1. "My NFL Eras" is reachable from the game's own front end: the main menu if T3.1 passes,
   otherwise the fallback the user picks (section 6).
2. "New League (1980)" creates a league save with the 28 franchises of 1980 in their verified
   divisions, plus the schedule from M0. "Continue League" reopens it after a restart.
3. "Play Next Game" launches the next unplayed scheduled game. Both teams show the league's
   city and nickname and play with the M0 roster for that franchise; the user controls their
   chosen team or watches CPU against CPU.
4. At the final whistle the score, the per-period points and a team and player box score are
   captured exactly once and written to the league save. A rematch or a quit does not produce a
   second or a false result.
5. The standings screens (AFC and NFC, by division) show W-L-T, PF and PA, including the new
   result.
6. Afterwards the user's own settings and the two spare team slots are as they were before the
   game.
7. Every screen and file that shows roster data says it comes from an unverified community
   roster.

Out of scope for the slice: playoffs, simulated or replayed games, Era Select carousel, photos,
rule hooks, other seasons, packs, Option B, hiding Rematch and Options.

### 2.2 Season choice

All 1970-1980 seasons have equally verified league data, and no season has a schedule yet.
The differences are in the roster saves and the disc art:

| | 1976 | 1977 | 1980 |
|---|---|---|---|
| teams / games / playoff teams | 28 / 14 / 8 | 28 / 14 / 8 | 28 / 16 / 10 |
| roster save | `ros1976` | `ros1977` | `ros1980` |
| cloned-body / duplicate / implausible records in the 28 used slots | 0 / 6 / 2 | 0 / 8 / 2 | 5 / 24 / 11 |
| birth-year convention | age-shifted to 2004 (consistent) | mixed or unclear | mixed or unclear |
| disc historic teams labelled with this season / within 3 seasons | 1 / 9 | 2 / 11 | 3 / 13 |
| alignment notes | the one-season SEA (NFC West) / TB (AFC West) placement | | |

Counts were made for this plan from `data/eras/community_rosters/<season>.json` and
`data/eras/historic_teams_2k5.json`. The "within 3" count for 1980 includes LA_1983 for the
Raiders, which the city rule below rejects.

**Recommendation: 1980.** It is the season the user named. Its roster defects are a handful
per team (at most 3 duplicates, 2 implausible bodies and 2 clones in any used slot), and none
of them blocks a game. It also has the most disc art from the right period. **1976 has the
cleanest roster data** in the verified range. Because the season is a data package (M0), a
1976 package is the same work as a 1980 one: run M0 with the season set to 1976 and research
the 1976 schedule.

### 2.3 How the pieces connect

```
data/eras/runtime/1980/  (M0, read-only at run time)
   teams.csv  schedule.csv  rosters/<franchise>.csv  PROVENANCE.txt
        |
        v
<user dir>\eras\league1\  (M3, per save)
   league.ini  results.csv  box\<game_id>.json  eras.log
        |
  league screen (M3) --"Play Next Game"--> launch (M4):
     snapshot settings -> stage home/away into USER1/USER2 (M2) -> mode 4, sides,
     setters -> arm capture -> sub_0034CA40 + push 0x55D088 -> retail Coach Match Up
     -> Start Game -> match
        |
  final whistle: ERAS_GAME_FINAL (M1) -> snapshot -> host writes box\<id>.json, results.csv
  match exit:    ERAS_MATCH_EXIT (M1) -> abandoned if armed and no snapshot
  Coach Match Up msg 3: ERAS_RETURN (M4b) -> pop to the league screen instead of the Main Menu
  league screen msg 3: restore settings, release slots, recompute standings
```

**Why CSV and INI.** The C side already parses `roster.csv` (`nfl2k5_team_roster`) and flat
keys (`json_str` in `nfl2k5_team_files.c`). There is no general JSON parser in the C code, and
the 53 MB community JSON is not something to load in game. The box score JSON is written by C
and never read back by C.

### 2.4 Team identity rule (M0 writes it into `teams.csv`)

For each franchise, the **template** is a disc historic team. It provides the uniform variant,
the asset code (helmet and uniform files) and 53 player records for the roster writer to
overwrite. The rule:

1. take historic entries of the same franchise (lineage aliases: `oilers`/`titans` ->
   `oilers_titans`, `redskins` -> `washington`);
2. prefer an entry whose city matches the franchise's city in that season (from
   `franchise_events_1970_2025.json` identities);
3. then pick the nearest season, preferring the earlier one on a tie.

City, nickname and abbreviation are then overwritten from the league data, using the same
field writes as `nfl2k5_team_identity` (`+0x104`, `+0x108`, `+0x138`). All 28 franchises of
1980 have at least one disc entry. Whether a template's uniform looks like the 1980 uniform
is **not claimed**, and `teams.csv` lets the user change the template per franchise.

| franchise (1980 identity) | `ros1980` slot (2004 name) | template (historic entry index) | season gap |
|---|---|---|---|
| bills: Buffalo Bills | 3 BUF | BUF_1990 (9) | +10 |
| patriots: New England Patriots | 21 NE | NE_1985 (51) | +5 |
| dolphins: Miami Dolphins | 12 MIA | MIA_1984 (29) | +4 |
| colts: Baltimore Colts | 10 IND (lineage, INFERRED) | BAL_1970 (24) | -10 |
| jets: New York Jets | 17 NYJ | NYJ_1968 (40) | -12 |
| browns: Cleveland Browns | 5 CLE | CLE_1986 (14) | +6 |
| oilers_titans: Houston Oilers | 30 TEN (lineage, INFERRED) | HOU_1979 (45) | -1 |
| steelers: Pittsburgh Steelers | 28 PIT | PIT_1978 (68) | -2 |
| bengals: Cincinnati Bengals | 2 CIN | CIN_1981 (6) | +1 |
| chargers: San Diego Chargers | 8 SD | SD_1980 (20) | 0 |
| raiders: Oakland Raiders | 22 OAK | OAK_1976 (55) (city rule; LA_1983 is nearer but Los Angeles) | -4 |
| chiefs: Kansas City Chiefs | 9 KC | KC_1969 (22) | -11 |
| broncos: Denver Broncos | 4 DEN | DEN_1977 (10) | -3 |
| seahawks: Seattle Seahawks | 27 SEA | SEA_1984 (65) | +4 |
| eagles: Philadelphia Eagles | 13 PHI | PHI_1980 (31) | 0 |
| cowboys: Dallas Cowboys | 11 DAL | DAL_1977 (26) | -3 |
| washington: Washington Redskins | 25 WAS | WAS_1982 (60) | +2 |
| cardinals: St. Louis Cardinals | 7 ARZ (lineage, INFERRED) | STL_1975 (18) | -5 |
| giants: New York Giants | 15 NYG | NYG_1986 (36) | +6 |
| vikings: Minnesota Vikings | 31 MIN | MIN_1973 (73) | -7 |
| lions: Detroit Lions | 18 DET | DET_1991 (43) | +11 |
| bears: Chicago Bears | 1 CHI | CHI_1985 (5) | +5 |
| buccaneers: Tampa Bay Buccaneers | 6 TB | TB_1979 (15) | -1 |
| packers: Green Bay Packers | 19 GB | GB_1966 (46) | -14 |
| falcons: Atlanta Falcons | 14 ATL | ATL_1980 (33) | 0 |
| rams: Los Angeles Rams | 23 STL (lineage, INFERRED) | LA_1979 (57) | -1 |
| 49ers: San Francisco 49ers | 0 SF | SF_1981 (0) | +1 |
| saints: New Orleans Saints | 26 NO | NO_1991 (63) | +11 |

`ros1980` slots 16 (JAX), 20 (CAR), 24 (BAL, Ravens) and 29 (HOU, Texans) are not used. They
are the save's filler slots for franchises that did not exist in 1980 (COMMUNITY-ROSTERS).

---

## 3. Build order

Conventions for every step:

* Tests run on `NFL2K5.exe --retail` (user rule). The game may be closed and relaunched for
  tests without asking.
* New C entry points that the game calls preserve EBX/ESI/EDI/EBP. Gen-patch callbacks also
  preserve every guest register and the x87/XMM state around guest calls, as
  `nfl2k5_guest_player_stat` does.
* Gen patches go in `tools/apply-gen-patches.py` as `(id, function, anchor, 'before'|'after',
  code)`. Each patch has a matching `recomp_interp_add_hook` entry, so a pack that interprets
  the function still reaches the code. The table holds 32 hooks and about 12 are in use;
  the slice adds 3.
* Logs: a new `NFL2K5_ERAS_LOG=1` turns on `[ERAS]` lines in stderr.

### M0: 1980 runtime package (offline; no game build)

Goal: everything the game needs for the 1980 slice, in files the C side can read, with
provenance.

**D1. Package builder.** Create `build/eras/make_runtime_package.py` (`--season 1980`). It reads:

* `data/eras/league/nfl_seasons_1970_1980.json` (teams, conferences, divisions, abbreviations);
* `data/eras/league/franchise_events_1970_2025.json` (identity per season, for the city rule);
* `data/eras/historic_teams_2k5.json` (template entries);
* the `ros1980` payload, through the mod tools' `RosterDocument`. It reads the payload rather
  than the extracted JSON, because the JSON lacks skin, face and hand, and the roster writer
  needs those.

It writes:

* `data/eras/runtime/1980/teams.csv`: `id,city,name,abbr,conference,division,template_entry,
  template_id,roster_file,community_slot,lineage_status`.
* `data/eras/runtime/1980/rosters/<id>.csv`: exactly the `roster.csv` format
  (`first,last,pos,number,height,weight,years_pro,skin,face,hand,<28 ratings>,photo`).
  * Rows are in the save's team-list order. That order is assumed to be the depth order
    (UNVERIFIED); the writer ranks depth within each position by row order.
  * Photo is 39999 ("no photo").
  * Implausible heights and weights are kept as stored and listed in a sidecar
    `rosters/<id>.flags.txt`, not "fixed".
* `data/eras/runtime/1980/PROVENANCE.txt`: league data, verified, with the source URLs from
  the JSON; rosters "community PS2 roster (unverified)", stem `ros1980`, sha256
  `857ef4d4...c327`; the template rule.

**D2. 1980 schedule (historical).**

* Create `build/eras/league_schedule.py`, using the same method as `league_history.py`: a
  Wayback copy of PFR `/years/1980/games.htm`, cached in `build/eras/league/pfr_cache/`, with
  the snapshot URL recorded.
* Write `data/eras/league/nfl_schedule_1980.json` (week, date, away, home, real score, sources)
  and `data/eras/runtime/1980/schedule.csv` (`game_id,week,date,away,home`; no scores, so real
  results never leak into play).
* Assertions:
  * every team plays 16 games;
  * 224 games in all (28 x 16 / 2);
  * no team appears twice in one week;
  * every team id is in the 1980 list;
  * **each team's W-L-T recomputed from the real scores equals its record in
    `nfl_seasons_1970_1980.json`**, which is already verified against the NFL book. This is the
    second check.
* Document the result in `docs/eras/research/SCHEDULE-1980.md`.
* If D2 cannot be completed, use a generated schedule only as a test fallback. It must carry
  `historical=false` in `schedule.csv` and must never be presented as the 1980 schedule.

**D3. Lineage check for the four renamed slots.** For ARZ, IND, STL and TEN in `ros1980`,
compare at least 5 names per slot against a sourced 1980 roster of the franchise (for example
PFR `/teams/<code>/1980_roster.htm` through Wayback). Record the outcome as `lineage_status`
(`checked:<url>` or `inferred`) in `teams.csv`. Skipping D3 does not block the slice; the
status stays `inferred`.

**Files.** Create the three scripts above and the outputs under `data/eras/runtime/1980/` and
`data/eras/league/`. Nothing under `src/`.

**Test (offline).**

* T0.1 `python -I build/eras/make_runtime_package.py --season 1980 --check`:
  * 28 teams, division sizes 5/4/5 and 5/5/4;
  * 28 roster files of 53 rows each;
  * positions 0..16, ratings 0..99;
  * every `template_entry` is between 0 and 74;
  * `PROVENANCE.txt` present.
* T0.2 `python -I build/eras/league_schedule.py --check`: the D2 assertions, including the
  standings recomputation.

**Exit:** both checks pass; the doc for D2 is written; the D3 status is recorded.

### M1: result capture in retail Quick Game

Goal: prove the static result-capture findings at run time, without any Eras UI.

**Create `src/nfl2k5_eras_capture.c`:**

* `nfl2k5_eras_game_final(uint32_t home_pts, uint32_t away_pts)`, following RESULT-CAPTURE
  §5.2:
  * act only when armed;
  * skip when `[0xE602B8] == 0x0A`;
  * cross-check the points against `[[0xE5FC28]]` / `[[0xE5FC68]]`;
  * read the period, clock, old state, mode, timeouts and per-period points
    (`block + 0x254 + 4*q`, q = 0..4);
  * read the team box through `sub_000CB2D0(team, sel, 0)` and the player box through
    `sub_000CB240(player, sel, 0)`, skipping players whose `[[p+0x30]]` is 0;
  * record each player's name pointers (`+0x10`/`+0x14`), jersey and position for the M2 check;
  * then disarm.
* `nfl2k5_eras_match_exit()`: if still armed, the game is "abandoned"; if a snapshot is
  pending, hand it to the writer.
* A host-side writer that emits `box\<game_id>.json` and appends to `results.csv`. In M1, with
  no league yet, it writes `logs\eras-capture-<n>.json`.
* The selector lists, generated from `data/eras/stat_selectors_2k5.json` into a static table:
  the player and team lists in RESULT-CAPTURE §4.4. Use team `0xD0` for total yards and team
  `0x5B` scopes 1-5 for period points. Never use scope 11 on top of scope 0.
* Debug arming: with `NFL2K5_ERAS_CAPTURE=1`, the capture is armed at boot and re-armed after
  each `ERAS_MATCH_EXIT`. A Rematch pops back to the game state without leaving it, so it
  should not pass match exit and should not be re-armed (INFERRED; T1.4 checks it).

**Change:**

* `src/recomp_manual.c`: generalise `nfl2k5_guest_player_stat` into
  `nfl2k5_guest_stat(fn_va, fn, entity, selector, scope, *out)` (exported) and add a team
  wrapper. Register the Eras interpreter hooks in a new `nfl2k5_eras_interp_hooks()`.
* `src/main.c`: call `nfl2k5_eras_interp_hooks()` next to `nfl2k5_scorebug_interp_hooks()`
  (line 3144).
* `config/game.cmake`: add `src/nfl2k5_eras_capture.c` to `add_executable`.
* `tools/apply-gen-patches.py`, two patches (both anchors exist once in the compiled tree:
  `src/recomp/gen/recomp_0004.c` around line 65031 and `recomp_0002.c:78700`):

```python
("ERAS_GAME_FINAL", "sub_000A2910", "loc_000A2910: ;", "after", """\
    { extern void nfl2k5_eras_game_final(uint32_t, uint32_t); nfl2k5_eras_game_final(ecx, edx); }
"""),
("ERAS_MATCH_EXIT", "sub_000645D0", "loc_000645D0: ;", "after", """\
    { extern void nfl2k5_eras_match_exit(void); nfl2k5_eras_match_exit(); }
"""),
```

* Interpreter hooks: `recomp_interp_add_hook(0x000A2910u, 0, ...)` reading `g_ecx`/`g_edx`,
  and `recomp_interp_add_hook(0x000645D0u, 0, ...)`.
* Optional, separate change: have `src/nfl2k5_presentation.cpp` `is_final()` (lines 1684-1688)
  ask the capture module instead of guessing.

**Data:** reads `data/eras/stat_selectors_2k5.json` at code-generation time only.

**In-game tests** (`tools/drive-quickgame.ps1 -Retail`, CPU against CPU, with
`NFL2K5_QUARTER_SECONDS=60`, `NFL2K5_ERAS_CAPTURE=1` and `NFL2K5_ERAS_LOG=1`; record the whole
game):

* **T1.1 normal game.**
  * Exactly one `[ERAS] final` line, then one `[ERAS] match exit`.
  * Logged mode = 4; old state logged (0x15 for clock expiry).
  * Points equal the scoreboard in the last screenshot, and the per-period sum equals the total.
  * Pass: on End Of Game -> GameCast Stats, these match the JSON: team first downs `0x0E`,
    passing `0x4C`, rushing `0x50`, total `0xD0`, turnovers `0x42`, time of possession `0x5F`;
    both QBs' comp/att/yds/TD/INT; one RB and one WR line.
  * Read `0x62`, `0x5F`, `0x30` and `0x63`, and record their meaning in RESULT-CAPTURE.
  * No `[LIVE-QB-ABI]`- or `[ERAS-ABI]`-style errors.
* **T1.2 overtime.** Repeat T1.1 until a game reaches OT; one-minute quarters make 0-0
  regulation likely, but repeats may be needed. Pass: period >= 5 at the hook, OT points in
  the `+0x264` slot, still exactly one final.
* **T1.3 quit.** Quit from the pause menu mid-game. Pass: `match exit` with `abandoned` and no
  `final`. If the quit does not reach `sub_000645D0`, record that and add a second
  abandon point before M4.
* **T1.4 rematch.** On End Of Game choose Rematch, then finish the second game. Pass: no
  second capture (the guard was disarmed); the first game's file is unchanged.

**Exit:** T1.1-T1.4 pass. RESULT-CAPTURE §8 is updated with the run-time answers.

### M2: staging a database team into USER1/USER2

Goal: put a franchise's template plus its roster into a spare slot, and prove the match uses
exactly those players.

**Change `src/nfl2k5_teams.c`:**

* Export `int nfl2k5_teams_stage(int side, int template_entry, const char *roster_csv, const
  char *city, const char *nickname, const char *abbr)`. It:
  * uses `spare_slot(side)`;
  * refuses when the slot holds a user-created team (the same test as `slot_usable`) and
    returns an error code the menu can show;
  * calls `load_historic(slot, template_entry, side)`;
  * applies the identity strings;
  * calls the roster writer.
* Export `nfl2k5_teams_release(side)` (wraps `release_slot`).
* Debug trigger: with `NFL2K5_ERAS_STAGE_TEST=<home_id>,<away_id>`, the first Team Select step
  stages both sides from `data/eras/runtime/1980/` and publishes them. This follows the
  `NFL2K5_HISTORIC_INVENTORY` first-step pattern.

**Change `src/nfl2k5_team_files.c`:**

* Split `nfl2k5_team_roster` into a path wrapper and `nfl2k5_team_roster_file(path, slot,
  arena)`.
* Write names into a **reusable per-side string arena**: 53 rows x 2 names x 32 UTF-16 chars,
  allocated once with `xbox_HeapAlloc`. The current writer allocates new strings on every load
  and never frees them, which over a season of games becomes a slow leak.
* After writing positions, set the special-teams slots `+0x194..+0x199` (holder, KR1, KR2, K,
  LS, PR; field meanings from the mod tools, UNVERIFIED) to the first player of the matching
  position. Do this only if T2.1 shows the template's values pointing at the wrong players.
* Do not write birth dates in the slice: the 1980 convention is mixed, so neither decoding is
  reliable. In-game ages are therefore meaningless in the slice; this is a known defect.

**Gen patches:** none (Team Select already routes steps to `nfl2k5_ts_step`).

**In-game tests** (`tools/drive-quickgame.ps1 -Retail -HomeTeam 'rt'`, with
`NFL2K5_ERAS_STAGE_TEST=colts,patriots`, `NFL2K5_TEAMS_LOG=1` and the M1 capture on):

* **T2.1 staging.**
  * Team Select shows the two staged teams with the league's city and nickname.
  * Coach Match Up and kickoff work.
  * The `[TEAMS]` log shows 53 rows written per side.
  * The in-game roster and depth screens show the M0 names and numbers.
  * A kickoff and a punt use a K/P from the M0 roster. If not, apply the special-teams fix and
    repeat.
* **T2.2 identity at the hook.** At `ERAS_GAME_FINAL`, every player pointer in `0xB30864`
  (home) and `0xB30A58` (away) has name pointers equal to the arena strings of the same row.
  Pass: 0 mismatches and 53 players per side. This settles home = `0xB30864` (verifier note P7)
  and the order through both copies.
* **T2.3 release.** After the game, a normal Quick Game Team Select shows the retail list and
  the spare slots are empty created slots (category 2, identity 90/91).

**Exit:** T2.1-T2.3 pass.

### M3: native menu shell and league save (no launch)

> **SUPERSEDED 2026-10-15 (user requirement):** the Eras hub is a COPY of the Franchise
> office, not plain list screens. Implement M3 from `docs/eras/ERAS-OFFICE-DESIGN.md`
> section 3 (its revised M3: office clone, era skins, league save, tests), using
> `docs/eras/research/FRANCHISE-OFFICE-MENUS.md` / `FRANCHISE-OFFICE-SCENE.md` /
> `OFFICE-RETEXTURE.md`. The league-save parts below (`src/nfl2k5_eras.c`, `league.ini`,
> `results.csv`, standings) still apply. Era art: our own made textures by default, the
> user's photos override them (no "photos only" rule). Default decisions: walk straight
> into the office (no lobby); the 1980 slice uses the 1980s office.

Goal: My NFL Eras exists in the game's own menus and can create, show and reload a 1980
league.

**Create `src/nfl2k5_eras_menu.c`.** Function range **`0xFEE00000`**:
`+ screen*0x1000 + slot*4` for event actions, `+ screen*0x1000 + 0x800 + row*0x40 + slot*4`
for row functions. Lookup is `nfl2k5_eras_menu_lookup()`, with a TLS called-address dispatcher
as in `vm_row_fn`. All descriptors, rows, event tables, help bars and strings live in its own
guest heap (about 256 KB), and **each descriptor gets its own event table at `+0x04`**, because
a template's event table runs the template's code (Game Options' `0x501C28` would mark options
dirty and save). Screens:

| screen | template | rows |
|---|---|---|
| main menu insert | `0x515660`, rows copied from `0x5154C0` | "My NFL Eras" (type 0 -> hub) inserted after Game Modes (`eras_insert_link`, NATIVE-MENU §3.4) |
| fallback link | Game Modes `0x5015CC` | the same row appended (used only if T3.1 fails and the user picks it) |
| Hub | Features `0x525830` (link list, `navigation`, flags 0x11) | "New League (1980)" type 9: create the save, push League; "Continue League" type 9, hidden by `+0x2C` when there is no save |
| League | Features | "Play Next Game: <AWAY> at <HOME> (Week n)" type 9 (in M3 it shows a "not yet available" help line); "Your Team: <x>" type 0 -> Your Team; "AFC Standings" / "NFC Standings" type 0. Event table: msg 3 -> refresh the label buffers (64 chars, rewritten before the handler rebuilds the items) |
| Your Team | Game Options `0x501E48` handler (settings list), own event table | one type-7 row: None (watch) / 28 franchises; locked after the first game |
| AFC / NFC Standings | settings list, flags with `0x04` (scroll) | 3 greyed division header rows plus 14 team rows; value string "W-L-T  PF-PA"; next/prev do nothing; help bar line "Unofficial order (wins, losses, point difference); unverified community rosters" |

The standings order is a display rule, **not** the 1980 NFL tiebreak procedure, which was not
researched.

**Create `src/nfl2k5_eras.c` and `src/nfl2k5_eras.h`** (league state):

* Read `data\eras\runtime\1980\teams.csv` and `schedule.csv` (relative to the working
  directory, like `mods\teams`).
* Save under `xbox_user_path("eras\\league1\\...")`:
  * `league.ini`: format version, season, package path, user team, created time;
  * `results.csv`: `game_id,home_pts,away_pts,final_period,end_state,status`;
  * `box\<game_id>.json`;
  * `eras.log`.
* Functions: next unplayed fixture (the user team's next game, otherwise schedule order),
  standings computed from `results.csv`, and a format-version check that refuses unknown
  versions with a message instead of guessing.

**Change:**

* `src/recomp_manual.c`: in `recomp_lookup_manual`, add
  `if ((address & 0xFFF00000u) == 0xFEE00000u) return nfl2k5_eras_menu_lookup(address);`.
* `src/main.c`: call `nfl2k5_eras_menu_install()` right after `nfl2k5_video_menu_install()`
  (line 3807).
* `config/game.cmake`: add both new sources.
* `tools/drive-menu.ps1`: add a `-Retail` switch that passes `--retail`, as
  `drive-quickgame.ps1` does. Today it follows `mods\active-pack.ini` to the pack's executable.

**Gen patches:** none.

**In-game tests** (`tools/drive-menu.ps1 -Retail`):

* **T3.1 main menu.** Screenshot the main menu, and dump the descriptor with
  `NFL2K5_MEMDUMP=515660:34`. Pass: 8 rows, all readable and selectable. If the 8th row is
  clipped, retry with header flag `0x04`. If it still fails, stop and ask the user (section 6).
* **T3.2 flow.** Hub -> New League -> League -> AFC Standings shows 3 divisions and 14 teams at
  0-0-0 -> Back -> NFC Standings -> Back to the main menu. Pass: the `[FSMOP]` push/pop log
  balances, and `eras\league1\league.ini` exists in the user folder.
* **T3.3 persistence.** Pick a Your Team value, restart the game, then Continue League. Pass:
  same team, same next fixture. "Continue League" is hidden in a fresh profile.
* **T3.4 Game Options untouched.** Open Options -> Game Options before and after the Eras
  screens. Pass: the values and the dirty or save behaviour are unchanged.

**Exit:** T3.1 (or the user's fallback choice), T3.2-T3.4 pass.

### M4: launch, capture, commit, standings

Goal: the full loop from the League screen.

**M4a. Launch with the retail return to the Main Menu (no gen patch).**

The "Play Next Game" type-9 action, in `src/nfl2k5_eras.c` (called with `ecx` = FSM; returns
with `g_esp += 4`):

1. **Refuse cleanly** if USER1/USER2 holds a user-created team (help-bar message).
2. **Snapshot:**
   * the 0x2E0-byte block `0xE5FF80..0xE60260`;
   * `0xE5FE64`, `0xE5FE68/6C`, `0xE5FE70/74`, `0xE5FE78/7C`;
   * `0xE60210/14`;
   * the side words `0xE5FE88 + port*0x1C` for ports 0-7.
3. **Stage:** home -> side 0 (USER1), away -> side 1 (USER2) with `nfl2k5_teams_stage` (M2).
4. **Mode:** `[0xE5FF80] = 4`, `[0xE5FFE4] = 0`. `[0xACF614]` is left alone; only Team Select
   code reads it (verifier).
5. **Sides:** `sub_00077200(port, side)` for each port. The port that pressed A gets 1 if the
   user team is at home, 2 if away; every other port, and all ports when watching, gets 0
   (CPU; INFERRED).
6. **Teams, without the Team Select texture lookup,** mirroring Franchise `sub_0015DB50`:
   `sub_00077AE0(home)`, `sub_00077B20(away)`, `sub_00077470([home+0x114])`, and uniform slots
   through `sub_000E3000` / `sub_000E30E0` from `+0x192`. Read each setter's register
   convention in the generated code before wiring it. If any setter turns out to need Team
   Select state, fall back to set_home/set_away, which are proven with historic teams in Team
   Select, and test `sub_002C0A90` explicitly.
7. **Arm** the capture with the fixture's `game_id`, then `gcall(sub_0034CA40)` and
   `gcall(sub_0006E390, ecx = 0xA84B18, edx = 0x55D088)`. This is exactly the flow-1 arm of
   `sub_002C0E70`.
8. The user sees the retail Coach Match Up screen and presses Start Game; the rest is retail.
   After End Of Game -> Exit the game returns to the **Main Menu**. The result was already
   written at the final whistle (M1). When the user re-enters My NFL Eras, the League screen's
   msg 3 does the clean-up: restore the snapshot, release both slots, recompute standings.
   Clean-up is idempotent and keyed on a "launched" flag in `league.ini`, so a crash before it
   is repaired on the next visit. Known wart of M4a: until that clean-up runs, the Eras teams
   stay in USER1/USER2 and the snapshot is not restored. A user who goes straight to Quick Game
   sees them there. M4b removes the wart.

**Home stadium per era (user-observed 2026-10-16):** the M2 test played the 1980 Colts in
the Indianapolis dome because the template slot is modern. The launch must set the home
team's era-correct stadium (`0xE5FE64` / the stadium setter) from a `stadium` column in
`teams.csv` (e.g. Baltimore Colts 1980 -> the nearest retail stadium for Memorial Stadium, an
open-air grass field; never a dome for an open-air team). Record the mapping and any
stand-ins in PROVENANCE.
Source table: `data/eras/league/stadiums_1966_2025.json` (`docs/eras/research/STADIUMS.md`):
for season Y, take the franchise's spell with first <= Y <= last and use its `ingame` name,
resolved to a stadium record index by name at runtime (the 1980 Colts map to M&T BANK STADIUM).

**M4b. Return to the League screen** (one gen patch). In `sub_0034C1F0`, insert after the line
`edx = 0x515660;` (in `loc_0034C201`; compiled tree `src/recomp/gen/recomp_0023.c` around
line 142463):

```python
("ERAS_RETURN", "sub_0034C1F0", "edx = 0x515660;", "after", """\
    { extern uint32_t nfl2k5_eras_return_desc(uint32_t); edx = nfl2k5_eras_return_desc(edx); }
"""),
```

`nfl2k5_eras_return_desc` returns the League descriptor only when an Eras game was launched
**and** that descriptor is on the FSM stack (scan `0xA84B18 + 8*i` up to depth `[+0x100]`).
Otherwise it returns `edx` unchanged; `sub_0006E450` pops until the stack is empty if the
target is missing.

When the redirect happens, the League screen's msg 3 runs the clean-up (restore the snapshot,
release both slots). When it does not, `nfl2k5_eras_return_desc` runs the clean-up itself.
This point comes after End Of Game has been left, which is when MATCH-LAUNCH §1.4 says the
slots may be released. Releasing them while Coach Match Up and the prep state are still being
popped is INFERRED to be safe; T4.2 checks it. Interpreter hook: `recomp_interp_add_hook(0x0034C208u, 0, ...)`. That
address is the `pop esi` after `mov edx, 0x515660` (XBE bytes read for this plan), so the hook
can rewrite `g_edx` before the `jmp`.

**Files:** `src/nfl2k5_eras.c` (launch, snapshot and restore, commit, standings) and
`src/nfl2k5_eras_menu.c` (wire the row). `src/nfl2k5_eras_capture.c` now arms only from the
launcher; `NFL2K5_ERAS_CAPTURE` stays as a test switch. `tools/apply-gen-patches.py` gets
ERAS_RETURN.

**In-game tests** (`drive-menu.ps1 -Retail`, `NFL2K5_QUARTER_SECONDS=60`, CPU against CPU):

* **T4.1 launch.**
  * League -> Play Next Game -> Coach Match Up shows the two league teams.
  * `[FSMOP]` shows push `0x55D088`, then `0x585474`.
  * Start Game -> kickoff -> final.
  * Pass: one `[ERAS] final` with the fixture's `game_id`; `results.csv` has one line;
    `box\<game_id>.json` exists.
* **T4.2 return** (M4a: Main Menu; M4b: League screen).
  * After returning, the standings show 1-0-0 / 0-1-0 with the correct PF and PA, and the
    next fixture has advanced.
  * Options -> Game Options values equal their values before the launch.
  * A Quick Game Team Select shows empty spare slots.
* **T4.3 snapshot.** Change quarter length in Game Options to an unusual value (for example 7)
  before the launch. Pass: it is still 7 afterwards, and the Eras game used it.

**Exit:** T4.1-T4.3 pass, with M4b in place, or with the user accepting the Main Menu return
for now.

### M5: slice acceptance

* **T5.1** Two Eras games back to back; standings and fixtures are correct after each.
* **T5.2** Restart between the two games; Continue League resumes correctly.
* **T5.3** Quit from the pause menu during an Eras game. The fixture stays unplayed and is
  marked "abandoned" in `eras.log`; slots and settings are restored.
* **T5.4** A user-controlled game (Your Team set, the controller's side given 1 or 2). The user
  controls the right team, and the CPU controls the other.
* **T5.5** An Eras game that goes to OT: a single result, OT noted, no tie possible (mode 4).
* **T5.6** A Create-a-Team team in USER1: Play Next Game refuses with a message, and the
  created team is untouched.
* **T5.7** With a pack active (no `--retail`): record what happens. Expected: the Eras menus
  may be unreachable or inert if the menu code is interpreted. It must not corrupt the save.
  The result goes into the risk table; it is not a slice blocker.

**Exit:** T5.1-T5.6 pass. Results go in `docs/PROJECT_STATUS.md` and in a short
`docs/eras/PHASE2-RESULTS.md`.

---

## 4. Risks

| # | risk | where it shows | mitigation |
|---|---|---|---|
| 1 | Option A has never run outside Team Select; something in Coach Match Up or the match may read Team Select state | T4.1 | Verifier: `0xACF614/63C/640` are referenced only by Team Select code (0x2C0D60..0x2C21F0). Stage through the M2-proven path. If it fails, fall back to Option C (preselected Team Select, MATCH-LAUNCH §5). |
| 2 | ERAS_RETURN pops past everything when the League descriptor is not on the stack | T4.2 | Return the League descriptor only after confirming it is on the stack; M4a works without the patch. |
| 3 | Heap corruption when Option B is added later | (future) | Never call `sub_0034BCD0` without first calling `sub_0034BD30` (section 1.2). |
| 4 | Wrong game mode (5/6/7 run the franchise commit; 8 runs anniversary code; below 4 skips the stadium) | T1.1 log, T4.1 | Eras always writes mode 4 and logs it at the hook. |
| 5 | Players reordered or replaced between the slot and the match block | T2.2 | Name-pointer check at the hook; record mismatches and skip those players rather than misattribute their stats. |
| 6 | Roster writer string leak over a season | long sessions | Reusable per-side arena (M2). |
| 7 | Incomplete settings snapshot leaves the user's Quick Game changed | T4.2-T4.3 | Snapshot the block plus the out-of-block list (section 1.2). Changes the user makes in Coach Match Up "Options" during an Eras game are reverted afterwards (documented). |
| 8 | Packs: gen patches don't fire in interpreted functions, and `0xFEE` functions can't be reached from interpreted menu code | T5.7 | Interpreter hooks for all three patch points. The slice is `--retail` only. Later: have `native_for` (`src/interp/x86_interp.c:339`) try `recomp_lookup_manual` for `0xFEC..0xFEE`. |
| 9 | Rematch plays a second game whose result is ignored, and Coach Match Up "Options" or "VIP" change things mid-league | T1.4 | Documented for the slice. Later: hide Rematch (End Of Game `0x4E935C` row copy with a `+0x2C` hide) and Options during Eras games, and decide on VIP recording. |
| 10 | Rule fidelity: a mode-4 game never ties and plays unlimited OT periods; the 1980 OT and tie rules are not researched | T5.5 | State it in the UI and docs. A future rule hook lives in `sub_000B8A60` (the tie path) and `sub_000A11F0`. Research the rule with sources first. |
| 11 | No historical 1980 schedule yet | M0 D2 | D2 with a standings-recompute check; the generated fallback is clearly flagged. |
| 12 | Roster data is unverified, with defects (24 duplicates, 11 implausible bodies, 5 clones in used slots), mixed birth years, a rating scale specific to the author, and inferred lineage for 4 slots | T2.1, data review | Label it everywhere; keep defects visible in the flags sidecar; D3 lineage check; no ages displayed or written in the slice. |
| 13 | Template special-teams and coach fields point at the template's own players or coach | T2.1 | Special-teams fix in M2; coach left as the template's (presentation only). |
| 14 | Disc template uniforms come from other seasons | visual | No fidelity claim; per-franchise override in `teams.csv`. |
| 15 | The main menu cannot show 8 rows | T3.1 | Flag `0x04`; otherwise the user chooses between the Xbox Live slot and the Game Modes link. |
| 16 | The capture runs about 4,800 guest calls on the guest thread at the final whistle | T1.1 timing | Measure once. If there is a visible hitch, split the player loop across frames before End Of Game, while the data is still live. |
| 17 | Distribution: community rosters are fan-made files the user supplied; the 53 MB of JSON under `data/eras/` is not gitignored | release | The user decides what ships. The runtime package for one season is small and can be regenerated. |
| 18 | Save-format churn during development | M3+ | Version field in `league.ini`; refuse unknown versions. |
| 19 | Seed-split functions | gen patches | All three anchors are labels inside single functions in the compiled tree today. `apply-gen-patches.py` fails loudly if an anchor moves; re-check after any regeneration (project seed-hazard note). |

---

## 5. After the slice (ordered, not committed)

1. **The rest of the week**, by the user's choice (section 6): leave games unplayed, replay the
   real 1980 results from D2 (sourced, no invention), or simulate them. Native Supersim needs
   its own study: stack arguments, and it clobbers the live match block.
2. **Full regular season and playoffs** with the 1978-1980 format already in the data. Needs
   the tiebreak procedure researched with sources; the NFL Record & Fact Book states each
   season's applied tiebreaks.
3. **Option B** direct start, with the corrected clean-up. Hide Rematch and Options in Eras
   games.
4. **Box score and results screens** (data-only lists first), using the M1 JSON.
5. **Era Select carousel and Association Setup** (NATIVE-MENU §4.3-4.4), and other 1970-1979
   seasons. Each needs its schedule (D2 method) and roster package (D1).
6. **Packs:** extend `native_for` to the `0xFEC..0xFEE` ranges; test the interpreter hooks.
7. **Era photos** through the texture override (the user's photos, not drawn art).
8. **Season rules** (OT, ties, and so on), each with a source and an engine hook, and tested in
   game (PHASE1 §4: a data field alone does not count).

---

## 6. Decisions for the user

1. **Season for the slice:** 1980 (recommended; the user's target) or 1976 (cleanest roster
   data, consistent birth years).
2. **Menu placement if T3.1 fails:** take the main menu's "Xbox Live" slot (replaces a retail
   entry) or add a link in Game Modes (no test needed, but not on the main menu).
3. **Games the user does not play:** leave unplayed (slice default), use the real 1980 results,
   or simulate (later milestone).
4. **USER1/USER2 occupied by a created team:** refuse (slice default) or save and restore it.
5. **Community rosters:** confirm they may be used in game with an "unverified community
   roster" label, and whether any of them may ship in a release.
6. **VIP recording** for Eras games: allowed or blocked.
7. **Return path:** whether landing on the Main Menu after an Eras game (M4a) is acceptable
   until M4b passes.

---

## 7. References

* `docs/eras/PHASE1-ARCHITECTURE.md`, `docs/eras/HISTORIC-TEAMS-INVENTORY.md`
* `docs/eras/research/RESULT-CAPTURE.md` (+ `data/eras/stat_selectors_2k5.json`,
  `build/eras/result-capture/stat_selectors.py`)
* `docs/eras/research/MATCH-LAUNCH.md` (+ `build/eras/match-launch/fsm_decode.py`,
  `menu_rows.py`)
* `docs/eras/research/NATIVE-MENU.md` (+ `build/eras/native-menu/*.py`)
* `docs/eras/research/COMMUNITY-ROSTERS.md` (+ `data/eras/community_rosters/`,
  `build/eras/extract_community_rosters.py`)
* `docs/eras/research/LEAGUE-HISTORY.md` (+ `data/eras/league/*.json`,
  `build/eras/league_history.py`). The 1980 sources used here are
  https://www.pro-football-reference.com/years/1980/ (via Wayback, snapshot URLs in the JSON)
  and the NFL Record & Fact Book past standings,
  https://web.archive.org/web/20130925182142/http://static.nfl.com/static/content/public/image/history/pdfs/History/Past_Standings_2011.pdf
* Project code referenced: `src/nfl2k5_teams.c`, `src/nfl2k5_team_files.c`,
  `src/nfl2k5_video_menu.c`, `src/recomp_manual.c` (lines 39-77, 2584-2593, 2712-2730),
  `src/main.c` (lines 1181-1210, 3144, 3807), `src/interp/x86_interp.c` (lines 300, 339-346),
  `tools/apply-gen-patches.py`, `tools/drive-quickgame.ps1`, `tools/drive-menu.ps1`,
  `config/game.cmake`.

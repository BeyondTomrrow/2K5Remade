# My NFL Eras: starting a match from our own menu

2026-10-15. Research only: nothing here has been built or run in game. My NFL Eras has to
start a game between two given teams from its own native menu, with no manual Team Select.
This document says how the project stages teams today, how Quick Game gets from Team Select
to kickoff, which native functions can start a match, where the game settings live, and
which approach to build first.

It makes no historical claims, so no sources are cited for history. Each technical claim
carries an evidence tag:

| Tag | Meaning |
| --- | --- |
| **[code]** | Read in this project's generated code (`build/gen-locals/*.c`, function `sub_XXXXXXXX` = Xbox address) or in the retail XBE's data with the scripts in `build/eras/match-launch/` |
| **[log]** | Seen in an earlier run's log (`logs/fullgame.stderr.log`, `logs/v7a.stderr.log`, `[FSMOP]` lines from gen patches `DIAG_FSMPUSH/DIAG_FSMPOP`) |
| **[project]** | This project's own source or tools, file named |
| **[mod tools]** | 2K Football Mod Tools research (`.tmp-2k-football-mod-tools/`), not re-checked here |
| **[inferred]** | My reasoning from the above, not observed |

Addresses are Xbox virtual addresses of the retail USA XBE.

---

## 0. The answer in brief

* There is **no single retail "start exhibition (home, away)" function.** Quick Game is a chain
  of front-end states. Only its last link, the game state `0x4E7EC0`, starts the match itself,
  and it reads the two teams from two globals: home `[0xE5FE68]`, away `[0xE5FE6C]` **[code]**.
* The Quick Game chain after Team Select uses only retail functions that can be called without
  the Team Select screen:
  `sub_0034CA40()` → `sub_0034BC10()` (copies the two teams) → `sub_00366E20()` (Coach Match Up
  side records) → `sub_00367400(ecx = screen stack)` (the **Start Game** row action), which
  resolves weather and time of day and pushes the game state **[code]**.
* **Recommended:** a kind-9 action row on a My NFL Eras screen that stages the two teams into
  USER1/USER2 with the existing loader, writes the settings, sets the controller sides and calls
  the retail setters, and then does one of two things:
  * **A (first milestone):** push the Coach Match Up prep state `0x55D088`. The player then
    sees the retail Coach Match Up screen and presses Start Game.
  * **B (target):** call the Start Game chain directly. The match starts at once, and when it
    ends the game returns to the Eras screen.

  Section 5 gives the steps, the risks and the reasons for this order.
* Settings are plain globals inside the 736-byte settings block `0xE5FF80..0xE60260`. Examples:
  quarter length in minutes `[0xE6000C]` (1..15), difficulty `[0xE5FF84]` (0..4 plus the preset
  applier `sub_000E3740`), time of day `[0xE60180]`, weather `[0xE601D0]` **[code]**.

---

## 1. Staging teams into the two spare slots (what exists)

### 1.1 The slots

`src/nfl2k5_teams.c` **[project]**:

* Roster root `[0xB72918]`. Team count is at `root+0x18` and the team table at `root+0x1C`
  (stride 500 = `0x1F4`). The historic list has its count at `root+0x58` and 16-byte entries at
  `root+0x5C`: +0 u16 year, +2 u8 variant, +4 inline UTF-16 asset code, +12 pointer to the
  franchise name.
* `spare_slot(n)` returns the n-th team in table order whose category `+0x128` is 2 (created) or
  4 (historic): **n = 0 is USER1 (home side), n = 1 is USER2 (away side).**
* `slot_usable(side, slot)` refuses a slot that holds the other side's team, a user-created team
  (category 2 with players), or anything outside categories 2/4. **My NFL Eras has to decide what
  to do when the user's own Create-a-Team occupies USER1/USER2 (refuse, or save and restore it).**
* `release_slot` clears the slot (`sub_000C2300`) and makes it an empty created slot again:
  count 0, category 2, identity `+0x118` = 90/91.

### 1.2 Loading a team into a slot

* **Historic team** (`load_historic`, a port of retail `0x2D17B0`) **[project]**:
  1. Build the name `h-<code>-<year>-<franchise>-<variant>.iff`.
  2. Queue it: `sub_00043F50(ecx = L"historic" 0xE9F9A8, edx = name; stack: 2nd buffer, loader
     heap from sub_00038FB0, 0, 0)`.
  3. Wait (`sub_000432D0`), then look up the ROST resource
     (`sub_000449E0(ecx = ctx, edx = 'ROST', stack ctx)`).
  4. **Clear the slot (`sub_000C2300(ecx = slot)`), then import (`sub_000C1030(ecx = resource,
     edx = slot)`, non-zero on success).**
  5. Set category `+0x128 = 4`, copy the label/playbook `+0x110` from the club with the same
     asset code, apply the old-school hook, and pop the context (`sub_000432F0`).
* **Added team** (`load_custom`) **[project]**:
  1. Read `mods\teams\<name>\team.rost` into a 256 KB guest buffer at +0x40, then clear and
     import as above.
  2. `nfl2k5_team_identity` (`src/nfl2k5_team_files.c`) writes code `+0x10C`, nickname
     `+0x104`, abbreviation `+0x108` and city `+0x138` from `team.json` as new guest strings, and
     the team number `+0x118` = the base team.
  3. `nfl2k5_team_roster` writes `roster.csv` into the players the import created.
* **Publish** (`publish`): `sub_002C1780(ecx = team)` for home and `sub_002C17C0(ecx = team)` for
  away (section 2.4).
* The `gcall` helper sets ECX/EDX, pushes a fake return address and calls the generated
  function. Callers save and restore EBX/ESI/EDI(/EBP) and ESP around it **[project]**.

### 1.3 Writing a roster into a slot

`nfl2k5_team_roster` **[project]** is the model for a database roster writer. It changes only
the players that the import created. **The player count `+0x11C` (u8) and the 65 player
pointers `+0x000..+0x100` come from the imported `.rost`.** So a database team with N players
needs a template `.rost` with at least N players. Fields it writes per player (0x54-byte
record):

| Field | Where | Note |
| --- | --- | --- |
| first / last name | `+0x10` / `+0x14` pointers | new UTF-16 strings from `xbox_HeapAlloc`; **each load allocates new strings and never frees them** (fine for one game, a slow leak over a season of loads: use a reusable string area) |
| position | `+0x35` | game code 0 QB .. 16 DE |
| jersey | `+0x20` bits 3..9 | |
| face | `+0x20` bits 17..20 | |
| height | `+0x2B` | inches |
| weight | `+0x2A` | pounds - 150 |
| years pro | `+0x25` bits 0..4 | |
| skin | `+0x18` bit 7, `+0x19` bits 0..3 | |
| hand | `+0x18` bit 1 | |
| 28 ratings | `+0x36..+0x51` | record order (see the file's comment) |
| depth | `+0x28` bits 10..15 (rank, side) | rank within position by row order; `+0x52` low 5 bits cleared |
| play-by-play name | `+0x04` = 0 | no recorded name |
| photo | `+0x06` | portrait number, 39999 = no photo |

Fields from the mod tools' full 0x54 map (`mod_editor/core/nfl2k5_roster_records.py`,
`FIELDS`) **[mod tools]** that an era roster will also want, and that the current writer does
not touch:

* birth month `+0x19` bits 4..7, day `+0x1A` bits 0..4, year in 7 bits (`+0x1A` bits 5..7 +
  `+0x1B` bits 0..3): needed for ages;
* college pointer `+0x00`;
* contract fields `+0x0A`, `+0x24`, `+0x26`, `+0x27`;
* the equipment bits `+0x0C`, `+0x1C..+0x1F`.

Team-record fields that matter for a match:

| Field | Offset | Evidence |
| --- | --- | --- |
| player pointers | `+0x000` (65) | [project], [mod tools] |
| nickname / abbreviation / code / label (playbook) | `+0x104 / +0x108 / +0x10C / +0x110` | [project] |
| home stadium pointer | `+0x114` (set_home passes it to `sub_00077470` -> `[0xE5FE64]`) | [code] |
| team number | `+0x118` u16 | [project] |
| player count | `+0x11C` u8 | [project], [mod tools] |
| category | `+0x128` (2 created, 4 historic) | [project] |
| coach pointer | `+0x14C` (Coach Match Up reads it, `sub_00366E20`) | [code], [mod tools] |
| scheme word | `+0x150` | [mod tools] |
| default uniform style | `+0x192` u8 (set_home passes it to `sub_000E3000` -> `[0xE60210]`) | [code]; meaning [mod tools] `nfl2k5_historic_styles.py` |
| special-teams slots | `+0x194..+0x199` (holder, KR1, KR2, K, LS, PR) | [mod tools] |

### 1.4 The match copies the teams twice, so the slots must stay loaded

1. `0x55D088` enter (`sub_0034BC10`) **[code]**:
   * saves the originals: `[0xAE81B4]` = home, `[0xAE81B8]` = away;
   * copies the 500-byte home record to `0xAE7DCC` and the away record to `0xAE7FC0`;
   * copies the player pointer arrays (count × 4 bytes) to `0xAE81BC` / `0xAE82C0`;
   * points `[0xE5FE68]` / `[0xE5FE6C]` at the copies.

   The copies still point at the slot's own player records.
2. The game state's enter (`sub_00064C70` -> `sub_00064590` -> `sub_00062BE0` ->
   `sub_000617E0` -> `sub_00061730(ecx = [0xE5FE68], edx = [0xE5FE6C])`) **[code]**:
   * copies both team records into the static match block, home `0xB30864`, away `0xB30A58`;
   * copies their players to `0xB30C4C` / `0xB321A0` (0x54 each) and repoints the copies;
   * sets player `+0x34` to 1 (home) / 2 (away).

   The mod tools' E2 witness lists the same two static match teams **[mod tools]**.
3. The End Of Game **Rematch** row (`sub_0006EDA0`: pop to `0x4E7EC0`, then `sub_00064B10`)
   restarts the match from `[0xE5FE68]` / `[0xE5FE6C]` **[code]**; the re-copy is
   **[inferred]**.

**So the USER1/USER2 players must stay loaded and unchanged until the player has left the End
Of Game screen.**

---

## 2. Quick Game, from the main menu to kickoff

### 2.1 The front-end state machine (screen stack `0xA84B18`)

**[code]** (sub_0006E390, 0x6E400, 0x6E2E0, 0x6E450, 0x6E4E0; jump table 0x6E5F8):

| Function | Use | What it does |
| --- | --- | --- |
| `sub_0006E390(ecx = fsm, edx = desc)` | push | msg 5 to the old top; depth `[fsm+0x100]`++; store desc at `fsm + depth*8`; msg 1 to the new top; if the top did not change, msg 3; `[fsm+0x108] = 1`. Refuses at depth 0x20 |
| `sub_0006E400(ecx = fsm)` | pop | msg 5 to the top; if unchanged, msg 2; depth--; msg 3 to the new top |
| `sub_0006E2E0(ecx, edx)` | replace top | msgs 5 and 2 to the old top, store the new one, msgs 1 and 3 |
| `sub_0006E450(ecx, edx)` | pop to | pop until the top is `desc` (or the stack is empty) |
| `sub_0006E4E0(ecx = fsm, edx = arg, push msg)` | send a message | runs the top state's action records for that message; returns 1 if the top state is unchanged |

* A descriptor is `{title, message list, screen handler, ...}` with rows at `+0x10` and flags at
  `+0x28`.
* Action-record kinds: 1 `call [+4](ecx = fsm)`, 3 `call [+0C]()` with no arguments, 7 pop,
  9 replace, 10 push, and more (`build/eras/match-launch/fsm_decode.py` lists them all).
* Message 6 is the per-frame tick (PROJECT_STATUS.md, 2026-09-24); message 11 is START
  (event `0xB` from `0xF3970`) **[mod tools]**. That 1 = enter, 2 = leave, 3 = activate and
  5 = deactivate is **[inferred]** from the order in which these functions send them.
* **Nested pushes complete inside the outer push.** In the logs, the outer `[FSMOP] push`
  line prints after the inner one.

The stack during a Quick Game, verified in two runs **[log]**:
`logs/fullgame.stderr.log` lines 2228-2514 and `logs/v7a.stderr.log` lines 2315-2668.

| Depth | State | Name / role |
| --- | --- | --- |
| 0 | `0x4F6870` | attract / press START |
| 1 | `0x515660` | Main Menu |
| 2 | `0x52728C` | Team Select (Quick Game) |
| 3 | `0x55D088` | Coach Match Up prep (no screen) |
| 4 | `0x585474` | Coach Match Up ("quickgame_opponent_match") |
| 5 | `0x4E7EC0` | the game |
| 6 | `0x4F6708`, then `0x4E3F38`, `0x4F1BE0`, `0x4E935C` | loading; in-game overlays (roles not traced); "End Of Game" menu |

### 2.2 Step by step

All **[code]** unless marked. The decode commands are in section 7.

1. **Main Menu row 0 "Quick Game"** (`0x5154C0`, kind 0) pushes Team Select `0x52728C`.
2. **Team Select enter**, msg 1 = `sub_002C20F0(ecx = fsm)`:
   1. Sets **`[0xE5FF80] = 4`** (game mode: exhibition).
   2. Picks the teams. With a favorite team `[0xE5FE60]` (`sub_00077560`) that
      `sub_00142620` resolves, home is that team and away is a random other team
      (`sub_000779E0` count, `sub_00048BC0` random, `sub_00077A10`). Otherwise home is the last
      home `[0xE5FE70]` (`sub_00077AA0`) and away the last away `[0xE5FE74]` (`sub_00077AC0`).
   3. Calls `sub_002C1950(eax = 1)`, which:
      * stores **flow `[0xACF614] = 1`**;
      * offline, sets the entering port's side to 1 (`sub_00077200(port, 1)`);
      * resets the Team Select UI words `0xACF66C..0xACF694`;
      * calls set_home(home) and set_away(away);
      * steps either side whose team is not in the flow's list (`sub_002C0EE0`, list mask
        `sub_002C0D60`).
3. **Stepping teams** (LT/RT etc. in Team Select): the gen patches `TEAM_SELECT_*` route each
   step to `nfl2k5_ts_step`. It steps past the resident list into the historic and added teams,
   loads the entry into the spare slot and publishes it **[project]**.
4. **START**, msg 11/12 = `sub_002C1700(ecx = fsm)`:
   * checks: the FPF controller count when `[0xE5FFE4]` is set (`sub_0033BFB0`), online state
     (`sub_00070870`, `sub_00128C70`), and team validity (`sub_002C1230`, which always returns 1);
   * plays the sound cue "Ambient: Game Start" (`sub_002C0A70(ecx = 0xE9AAFC)`);
   * then **`sub_002C0E70` (ESI = fsm)** switches on the flow through the jump table `0x2C0EB4`:
     * flows 0, 1, 2, 7, 9: `sub_0034CA40()`, then **push `0x55D088`**;
     * flows 3, 8: pop to Main Menu `0x515660`, then **push the game `0x4E7EC0`** (Practice);
     * flows 4, 5, 6: **replace Team Select with the game `0x4E7EC0`**.
5. **`0x55D088` enter** (`sub_0034BC10`, kind 3, no arguments):
   * `[0xAE7C4C] = 0` and `sub_0034A7D0` (-> `sub_000E2DB0`, wind direction **[mod tools]**);
   * `[0xAE844C] = sub_001311C0()`;
   * the team copies of 1.4;
   * `[0xAE7C40] = [0xAE7C44] = [0xAE7C48] = 1`.

   Its msg 3 (`sub_0034BD00`) **pushes Coach Match Up `0x585474`** when `[0xAE7C48]` is set and
   pops otherwise. Its msg 5 clears `[0xAE7C48]`. Its msg 2 (`sub_0034BCD0`) restores
   `[0xE5FE68]`/`[0xE5FE6C]` from `[0xAE81B4]`/`[0xAE81B8]`, clears the flags and frees
   `[0xAE84D4]` (`sub_0034B000`). So Back from Coach Match Up falls through to Team Select.
6. **Coach Match Up enter** (`sub_00366E20`, kind 3, no arguments) fills two 0x58-byte side
   records: away at `0xCC2468`, home at `0xCC24C0`. Each holds the team, the coach (team
   `+0x14C`; cleared when a human plays that side), the VIP profile of the first port on that
   side, and the count of human ports on that side (`[0xCC246C]` away, `[0xCC24C4]` home). To
   build them it walks ports 0..7: connected (`sub_000709B0(port, 0)`), side
   `[0xE5FE88 + port*0x1C]` (1 = home, 2 = away), VIP `[0xE5FE98 + port*0x1C]`
   (`sub_00077280`). Its msg 4 (`sub_00367280`) queues a load into context `0xEB95BC`
   (`sub_000441B0`); msg 5 (`sub_00367320`) pops that context.
   * Rows (`0x585308`, from `menu_rows.py`): **Start Game** (kind 9, action `0x367400`),
     VIP Coaches, NFL Coaches, Lineup, Options (Team Options `0x526C10`), VIP. The project's video
     menu adds "Presentation" (`src/nfl2k5_video_menu.c`).
7. **Start Game** (`sub_00367400(ecx = fsm, edx = row state)`, called by the menu dispatcher
   `0x150020`, which passes ECX = screen stack and EDX = state entry **[mod tools]**):
   * for each side with no human port (side 1 = away record `0xCC2468`, 0 = home `0xCC24C0`):
     `sub_000F7280(ecx = profile, edx = side)` sets `[0xBA04B0 + side*4]`. The profile is the
     VIP coach picked on Coach Match Up (record `+0x18`, or `+0x08` through `sub_00191CE0` /
     `sub_00142370`), otherwise 0. With 0, the team copy's coach `+0x14C` becomes the record's
     NFL coach (`+0x10`). Sides with a human port are not written;
   * `sub_00270B00()` if `[0xE601FC]`;
   * **`sub_000E3150()`** resolves the weather preset into temperature, precipitation and wind;
   * **`sub_000E3350()`** resolves time of day into `[0xE60184]`;
   * then tail **`sub_0034C1B0(ecx = fsm)`**: free `[0xAE84D4]` (`sub_0034B000`), set
     `[0xE5FE68] = 0xAE7DCC` and `[0xE5FE6C] = 0xAE7FC0`, set `[0xAE7C4C] = 1`, and
     **push the game `0x4E7EC0`**.
8. **Game enter** (msg 1 = `sub_00064C70` -> `sub_00064590`):
   * runs `sub_000F6510(2)`, `F6290`, `F6210`, `F5FE0` and `sub_00064530`;
   * `sub_00062BE0` snapshots weather and time into `0xB30850..0xB30860`, does the stadium and
     sky setup and makes the team deep copy of 1.4;
   * `sub_00064560` pushes **loading `0x4F6708`** through `sub_000F5430`. Its mode is 2 when
     FPF `[0xE5FFE4]` is set, otherwise the table `0x4E7CB0[[0xE5FF80]]`: 0 for modes 4..7,
     1 for the others.
   * Match init `sub_00055DD0` sets the **match quarter length `[0xE602B0] = [0xE6000C] × 60.0`**
     (constant `0x4E6D14`) and `[0xE602AC] = 40.0` (probably the play-clock length
     **[inferred]**).
9. **End of game**: state `0x4E935C` "End Of Game" is pushed over the game. Its rows: Rematch,
   Replay, GameCast Stats `0x504248`, Crib Tally, Exit (`sub_0006ED30`: pops the menu), Save VIP
   and Exit **[code]**; the push itself is **[log]** (fullgame line 10052).
10. **Return**: Coach Match Up msg 3 runs `sub_00367140`, then **`sub_0034C1F0`**. With
    `[0xAE7C4C]` set, that calls `sub_0034BD30` (re-allocates `[0xAE84D4]`) and **pops to the
    Main Menu `0x515660`** **[code]**. Retail Quick Game therefore always ends at the Main Menu.

### 2.3 Team Select flows (`[0xACF614]`)

**[code]**: each Team Select descriptor's msg 1 calls `sub_002C1950` with its own flow.
A role is from the mod tools where marked.

| Flow | Descriptor | Reached from | START arm |
| --- | --- | --- | --- |
| 1 Quick Game | `0x52728C` | Main Menu "Quick Game" | push `0x55D088` |
| 3 Practice | `0x5275F8` | code at `0x148B39` (`0x148B40`, settings START) [mod tools] | pop to Main Menu, push game |
| 4 | `0x527950` | code near `0x146868` | replace with game |
| 6 | `0x527CC0` | no reference found | replace with game |
| 7 First Person Football | `0x526948` | Game Modes row 1 (entry `0x2C1FC0` sets mode 4, `[0xE5FFE4] = 1`) [mod tools] | push `0x55D088` |
| 8 | `0x5265F0` | code near `0x14736F` | pop to Main Menu, push game |
| 9 | `0x5262D0` | code near `0x272853` | push `0x55D088`; its leave clears `[0xE601FC]` |

* Team Select's leave (msg 2, `sub_002C1660`) resets the flow to 0. Flow 7 also clears the FPF
  flag (`sub_002C0AF0`) and flow 9 clears `[0xE601FC]` (`sub_002C0B10`).
* `nfl2k5_ts_step` acts only in flows 1, 3 and 7 and only offline **[project]**.

### 2.4 The setters set_home `0x2C1780` / set_away `0x2C17C0`

**[code]**

* **set_home(ecx = team)**:
  * `sub_000E3000(team+0x192)` -> uniform slot `[0xE60210]`;
  * `[0xACF63C] = team`;
  * `sub_00077AE0(team)` -> `[0xE5FE68] = team`, home playbook `[0xE5FE78] = team+0x110`;
  * **only if `[0xE5FF80] >= 4`**: stadium `[0xE5FE64] = team+0x114` (`sub_00077470`), then
    `sub_002C0A90`, which looks up a Team Select stadium texture in context `0xE9A87C`.
* **set_away(ecx = team)**:
  * `sub_000E30E0(team+0x192)` -> `[0xE60214]`;
  * `[0xACF640] = team`;
  * `sub_00077B20(team)` -> `[0xE5FE6C] = team`, away playbook `[0xE5FE7C] = team+0x110`.
* **Set the mode to 4 before set_home**, as Team Select does; otherwise the home stadium is not
  set.
* The Franchise does the same thing without the Team Select texture (`sub_0015DB50`):
  `sub_00077AE0(home)`, `sub_00077B20(away)`, `sub_00077470(home+0x114)`, uniforms 0/0, climate
  `sub_00133FC0`.

### 2.5 Who plays which side

Per-port records of 0x1C bytes at `0xE5FE88 + port*0x1C` **[code]**:

| Field | Address | Accessors | Meaning |
| --- | --- | --- | --- |
| side | `+0` = `0xE5FE88` | getter `sub_000771F0(ecx = port)`, setter `sub_00077200(ecx = port, edx = side)` | 1 home, 2 away (Coach Match Up counts these). 0 = neither, so the CPU plays that side: **[inferred]**, matching drive-quickgame's "one Left press = middle = CPU vs CPU" |
| controller layout | `+8` | gen patch `CONTROLLER_SETUP_PORT_LAYOUT` | |
| VIP profile | `+0x10` | `sub_00077280` | |
| connected | `[0xB37A70 + port*0x244]` | `sub_000709B0(port, 0)` | |

The mod tools also name `0xE5FE88 + port*28` the controller team assignment **[mod tools]**.

### 2.6 How the drivers do it today (button presses)

**[project]**

* `tools/press.ps1` writes button names to `logs\press.txt`. The game reads that file every
  100 ms when `NFL2K5_PRESS_FILE` is set (`file_press` in `src/nfl2k5_input_hle.c`): 200 ms per
  press, 250 ms between presses, host pads ignored with `NFL2K5_NO_HOST_PAD=1`.
* `tools/drive-quickgame.ps1`:
  1. boot (90 s);
  2. `start` (title -> Settings popup), `a`, `a` (popups), `start` (main menu);
  3. `a` (Quick Game; 18 s for Team Select to load);
  4. optional home steps (`-HomeTeam (,'lt'*11)` = 49ers);
  5. `left` (home -> middle = CPU vs CPU);
  6. `start` (-> Coach Match Up);
  7. `a` (Start Game -> loading, 30 s);
  8. `a` (skip the pregame);
  9. screenshots every 30 s.
* `tools/drive-menu.ps1` runs arbitrary steps (`'down x3:2'`, `'a:6'`, `'shot'`) after the same
  title sequence.
* Inside the runtime there is already one injected press: `nfl2k5_input_press_a(ms)`, used by
  the presentation code to skip the pregame segment.

---

## 3. Candidate native "start a match" functions

There is no single exhibition-start function that takes two team records.

| Function | Inputs | What it does | Usable directly? |
| --- | --- | --- | --- |
| `sub_0034C1B0` | ECX = screen stack | free `[0xAE84D4]`, home/away := the copies `0xAE7DCC`/`0xAE7FC0`, `[0xAE7C4C] = 1`, push game `0x4E7EC0` | Only after `sub_0034BC10` has made the copies, otherwise the copies are stale **[code]** |
| `sub_00367400` | ECX = screen stack (EDX unused) | Coach Match Up "Start Game": CPU-side coach profiles, weather `sub_000E3150`, time `sub_000E3350`, then `sub_0034C1B0` | Yes, after `sub_0034BC10` and `sub_00366E20` **[code]**; never run outside its screen **[inferred]** |
| `sub_0034BC10` | none (kind 3) | `0x55D088` enter: copies home/away from `[0xE5FE68]/[0xE5FE6C]` | Yes **[code]** |
| `sub_00366E20` | none (kind 3) | Coach Match Up enter: side records from the teams and ports | Yes **[code]** |
| `sub_0006E390(fsm, 0x55D088)` | screen stack | the flow-1 arm of `sub_002C0E70`, preceded there by `sub_0034CA40()` | Yes: the retail plumbing from here on **[code]** |
| `sub_0006E390(fsm, 0x4E7EC0)` | screen stack | push the game directly, as flows 3/8 do (flows 4/5/6 replace with it) | Yes, but skips `0x55D088` and Start Game (no team copies, no weather/time resolution, `[0xAE7C4C]` unset) **[code]** |
| `sub_002C0E70` | **ESI** = screen stack | flow dispatcher after START | Possible; it switches on `[0xACF614]`, and its caller `sub_002C1700` adds the readiness checks **[code]** |
| `sub_00064590` / `sub_000F5430` | | the game state's enter / loading-screen push | Internal; not a start API **[code]** |
| `sub_0015DB50` | ECX = fixture | Franchise: home/away from the schedule, stadium, uniforms, climate; no push | Reference for the setup a scheduled game does **[code]** |
| `sub_00061730` | ECX = home, EDX = away | deep copy into the static match block | Called by the game enter and by `sub_0010B280`; not a start API **[code]** |
| `sub_0010B940` / `sub_0010B280` | `0x10B280`: ECX = home, EDX = away, six stack words (week, fixture slot, special-result flag, live-import flag, two config pointers; `ret 24`). `0x10B940`: five stack words (`ret 20`) | **native abstract simulation** of a full game (Supersim): `0x10B940` initializes through `0x10B280`, ticks `sub_0010B250` until 5 or -1, then finalizes with `sub_001053B0` | For games the user does not play, which a league needs every week. Bounded-executed by the mod tools **[mod tools]**; callers in this build `sub_0010B906/B980/B9C0` **[code]** |

Result reading for later: the score getter `0x250DD0` (ECX = side, EDX = 0 for the game total)
**[mod tools]**; the End Of Game state `0x4E935C` and its GameCast Stats screen `0x504248`
**[code]**.

---

## 4. Game settings

All the values below are dwords inside the settings block. That block is a flat image of RAM
`0xE5FF80..0xE60260` (0x2E0 = 736 bytes) and is what Settings1/`SAVEGAME.DAT` stores
**[mod tools]** (`nfl2k5_save_writer.py`). Row addresses come from the menu tables
(`menu_rows.py`) and the getters' generated code **[code]**.

| Setting | Address | Values | Menu row (getter) | Applier / when it takes effect |
| --- | --- | --- | --- | --- |
| Game mode | `0xE5FF80` | practice 0/1/2, Basic Training 3, games 4+, Team Select writes 4, 25th Anniversary 8 [mod tools] | none | read by set_home (>= 4), the loading-mode table `0x4E7CB0` and many others |
| First Person Football | `0xE5FFE4` | 0/1 | Franchise Settings row [mod tools] | must be 0 for a normal game (loading mode 2 when set) |
| Difficulty | `0xE5FF84` | 0 Rookie, 1 Pro, 2 All Pro, 3 Legend, 4 Custom (text `0x4F25A8`) | Difficulty Settings `0x148D80`, Team Options `0x2C2B20` | **call `sub_000E3740`** after a change: it writes the human/CPU slider presets. The row's next/prev do the same |
| Quarter length | `0xE6000C` | minutes 1..15 (text table `0x4ED8F0` holds 0..20) | Game Options `0x149170`, Team Options `0x2C2BB0` | match init `sub_00055DD0`: `[0xE602B0] = minutes × 60.0` |
| Play calling | `0xE60018` | 0 By Package, 1 By Formation, 2 Let Coach Pick | Game Options `0x149220` | |
| Game speed | `0xE5FFA8` | 0 Slow, 1 Normal, 2 Fast | Game Options `0x1490E0` | frame dispatcher `0x11A7C0` [mod tools] |
| Challenges / Coach Mode / Performance EQ / Multiple Hot Routes | `0xE6003C` / `0xE6002C` / `0xE60034` / `0xE60040` | 0/1 | Game Options | |
| FPF auto-swap / receiver progression | `0xE60048` / `0xE6004C` | 0/1 | Game Options | |
| Time of day | `0xE60180` | 0 Day, 1 Afternoon, 2 Night, 3 Random | Weather Settings `0x14B510`, Team Options `0x2C2C60` | `sub_000E3350` -> `[0xE60184]` (Random picks one; called by Start Game and by the row setters) |
| Weather | `0xE601D0` | 0 Clear, 1 Light Rain, 2 Heavy Rain, 3 Flurries, 4 Heavy Snow, 5 Random (text `0x4F2524`) | Weather Settings `0x14C170`, Team Options `0x2C2D00` | **`sub_000E3150`** (Start Game) -> temperature `0xE5FFA4`, precipitation `0xE5FFAC`, wind `0xE600C0` [mod tools for the outputs] |
| All penalties | `0xE6008C` | 0/1 | Penalty Settings `0x149B70` | individual flags `0xE60060..0xE60080`; the 10 slider rows are kind 4 |
| Sliders | human `+0x154`, CPU `+0x178` (9 each), mirrors `+0x298`/`+0x2BC`, injury `+0x284`, fumble `+0x288`, interception `+0x28C` (offsets from `0xE5FF80`) | | Difficulty Settings (kind 4 rows) | [mod tools]; keep editable and mirror copies consistent |
| Uniform slot home / away | `0xE60210` / `0xE60214` | 0..14 | set_home/set_away from team `+0x192` | [code]; meaning [mod tools] |
| Home / away playbook | `0xE5FE78` / `0xE5FE7C` | label pointer | Team Options rows | set by `sub_00077AE0` / `sub_00077B20` |
| Stadium | `0xE5FE64` | stadium pointer | Team Options "Stadium" | `sub_00077470`, from home `+0x114` |
| Match quarter length (live) | `0xE602B0` float seconds; quarter `0xE602C4`; clock `[0xE6028C]+0x10` | | | `NFL2K5_QUARTER_SECONDS` in `src/main.c` already overrides any length 10..900 s at run time [project] |

**Persistence.** The block is the user's saved Settings. An Eras game should snapshot the
0x2E0 bytes before it writes era settings and restore them after the game. Otherwise the user's
Quick Game options change, and a later settings save would store the era values
**[inferred]**.

---

## 5. Recommended approach (ranked)

All options share the same staging step:

1. **Stage the teams.** Generalise `load_historic` / `load_custom` into one exported function,
   e.g. `nfl2k5_teams_stage(side, source)` (side 0 = USER1, 1 = USER2), plus a database roster
   writer modelled on `nfl2k5_team_roster` (1.3). Check `slot_usable`-style ownership first.
2. **Mode and flags.**
   * `[0xE5FF80] = 4` and `[0xE5FFE4] = 0`.
   * `[0xACF614] = 1`: only Team Select code reads it, but it keeps the flow-1 assumptions
     **[inferred]**.
3. **Settings.**
   * Snapshot `0xE5FF80..0xE60260`.
   * Write quarter length, play calling, game speed, time of day and weather.
   * Write the difficulty, then `gcall(sub_000E3740)`.
4. **Sides.** For each connected port, `sub_00077200(port, 0/1/2)`: 1 = home, 2 = away, 0 = CPU.
5. **Teams.** `gcall(sub_002C1780, home_slot)` and `gcall(sub_002C17C0, away_slot)`, or, to
   avoid the Team Select texture lookup in `sub_002C0A90`, the individual setters from 2.4.

The menu side uses the existing framework. `src/nfl2k5_video_menu.c` builds screens from
0x34-byte rows whose functions are fake addresses (`0xFEC00000..`) that `recomp_lookup_manual`
resolves. A **kind-9 row** ("Play Game") with its action at `+0x28` is the retail way to run
code from a menu (The Crib, Start Game). The dispatcher calls it indirectly through
`RECOMP_ICALL_SAFE` (`sub_00150020`, `call [esi+0x28]`) **[code]**, with ECX = screen stack
`0xA84B18` and EDX = its state entry, and the action must preserve EBX/EBP/ESI/EDI
**[mod tools]**. A kind-9 action at a fake address has not been tried yet **[inferred]**.

### Option A: push the Coach Match Up prep state (first milestone)

After steps 1-5: `gcall(sub_0034CA40)`, then `gcall(sub_0006E390, fsm, 0x55D088)`. This is
exactly the flow-1 arm of `sub_002C0E70`.

* **What the user sees:** the retail Coach Match Up screen, with the right teams, coaches and
  VIP. They press **Start Game**; everything from there is retail.
* **Risks:**
  * After the game, `sub_0034C1F0` **pops to the Main Menu**, which also removes the Eras
    screen. Returning to Eras needs a gen patch at `loc_0034C201` (pop to the Eras descriptor
    when an Eras game is running), or result capture before that point.
  * Coach Match Up "Options" lets the player change difficulty, quarter length and weather.
    For league games, hide it in a copy of the row array, as `vm_remove_links` does.
  * Never tested outside Team Select **[inferred]**.
* **Why first:** almost all the code it runs is retail, so a failure points at staging,
  settings or sides.

### Option B: direct start through the retail chain (target)

After steps 1-5, in the order retail runs them:

```
gcall(sub_0034CA40)                 /* what 0x2C0E70 does before the 0x55D088 push         */
gcall(sub_0034BC10)                 /* 0x55D088 enter: team copies 0xAE7DCC / 0xAE7FC0      */
gcall(sub_00366E20)                 /* Coach Match Up enter: side records 0xCC2468/0xCC24C0  */
gcall(sub_00367400, fsm, 0)         /* Start Game: coaches, weather, time, push the game     */
```

* **What happens:**
  * The game is pushed over the Eras screen, with no Coach Match Up.
  * When the game state pops, the Eras screen gets msg 3: the natural place to record the
    result and show the next fixture.
  * Afterwards, call `sub_0034BCD0` (0x55D088's own leave: restore `[0xE5FE68]/[0xE5FE6C]`,
    clear flags, free `[0xAE84D4]`), clear `[0xAE7C4C]`, restore the settings snapshot and
    release the slots.
* **Risks:**
  * Coach Match Up's msg 3/4/5/6 and 0x55D088's msg 3/5 never run. As far as read, these only
    load or release Coach Match Up art and manage the screen chain **[code]**; nothing else was
    found **[inferred]**.
  * `[0xBA04B0 + side*4]` (the CPU coach profile) is written for every CPU side, 0 when no VIP
    coach was picked, because `sub_00366E20` clears the picks. Human sides keep their old value,
    as in retail **[code]**. Its readers were not checked.
  * The match load runs synchronously inside the push **[log]** (the outer push line prints
    after loading pops), as in retail.

### Option C: preselected Team Select (fallback)

* Put the slots in `[0xE5FE70]` / `[0xE5FE74]` (the last home/away read by `sub_002C20F0`) and
  push Team Select `0x52728C`.
* **Risks:** a favorite team `[0xE5FE60]` overrides home. The list check `sub_002C0EE0` may step
  off a category-4 slot. The user still presses START and Start Game. Mostly useful if A and B
  fail.

### Option D: injected presses (testing only)

* Queue presses like `file_press` / `nfl2k5_input_press_a`.
* **Risks:** fragile. It depends on timing, cursor positions, rows added by mods (Video Settings,
  Presentation, Mods) and the controller side. Keep it for the drive scripts.

### Other notes for every option

* **Packs:** with a mod pack active, functions in the pack's dirty map run in the x86
  interpreter (`RECOMP_INTERP_GATE`), and gen patches inside them do not fire. Test with
  `NFL2K5.exe --retail` first (memory: launch with `--retail`).
* **Simulated games:** a league's CPU-vs-CPU games can use the native Supersim
  (`0x10B940`/`0x10B280`, section 3) rather than being played.
* **VIP:** Quick Game records to VIP profiles ("Save VIP and Exit"). Decide whether Eras games
  should.
* **Rematch:** it replays from the copies. Hide or handle it for league games.

---

## 6. What is unknown (to verify in game)

1. Whether `push 0x55D088` (A) and the direct chain (B) work from a non-Team-Select screen; in
   particular whether anything reads Team Select state (`0xACF66C..0xACF694`,
   `[0xACF63C]/[0xACF640]`) during the match.
2. The return path in A: exactly when `0x4E7EC0` pops (after End Of Game "Exit"?), and whether
   msg 3 reaches the screen below.
3. That side 0 = CPU for every port (only 1/2 are proven to be counted), and whether a port
   with side 1 or 2 must also be "connected" for the game to give it control.
4. The readers of `[0xBA04B0 + side*4]` (CPU coach profile), `[0xAE844C]` (from
   `sub_001311C0`, which returns `[0xBB83B4]`) and the `[0xAE84D4]` buffer (primary player count
   × 4 bytes, allocated by `sub_0034BD30`, freed by `sub_0034B000`): what they hold and whether
   Option B's order of calls leaves them as retail would.
5. `sub_002C0A90` (Team Select stadium texture lookup in context `0xE9A87C`) called outside
   Team Select: probably a harmless failed lookup, unverified.
6. The roles of flows 0, 2, 4, 5, 6, 8 and 9 and of `[0xE601FC]`.
7. Messages 1/2/3/5 as enter/leave/activate/deactivate are inferred from the call order;
   message 12 is unidentified.
8. Whether `[0xE5FE70]/[0xE5FE74]` (last home/away) are written by Team Select leave or START,
   and by whom (needed for C).
9. Quarter lengths above 15 minutes: the text table has 16..20 but the row max is 15; values
   above 15 were not tested.
10. A user-created team in USER1/USER2: Eras must refuse or preserve it; nothing does that yet.
11. The final score and box score at End Of Game (`0x4E935C`): where they live (mod tools' score
    getter `0x250DD0`; live team contexts `0xE5FC20`/`0xE5FC60`), to be confirmed in this
    build.

Suggested first test (needs a build, so not done here): add a debug trigger that runs
Option A's steps with a historic team in each slot (the inventory shows all 75 load). Watch
`[FSMOP]` for `push 0x55D088` -> `0x585474`, press Start Game, and check the Coach Match Up
names, the kickoff, and `NFL2K5_TEAMS_LOG`. Then do the same with Option B, and check that the
Eras screen gets control back after End Of Game -> Exit.

---

## 7. Reproduce

Read-only scripts. They use `tools/xbe_peek.py` and `original/disc/default.xbe` and write
nothing:

```
python build/eras/match-launch/fsm_decode.py 0x52728C 0x55D088 0x585474 0x4E7EC0 0x4F6708 0x4E935C
python build/eras/match-launch/menu_rows.py 0x515660 0x585474 0x4E935C 0x501E48 0x526C10 0x5023CC 0x502A9C 0x503054
python tools/xbe_peek.py 0x2C0EB4 10      # flow -> START arm (sub_002C0E70)
python tools/xbe_peek.py 0x4E7CB0 10      # game mode -> loading mode
```

Generated functions read for this note (`build/gen-locals/`):

* FSM: `sub_0006E2E0`, `sub_0006E390`, `sub_0006E400`, `sub_0006E450`, `sub_0006E4E0`.
* Team Select: `sub_002C20F0`, `sub_002C1950`, `sub_002C1700`, `sub_002C0E70`, `sub_002C1660`,
  `sub_002C1780`, `sub_002C17C0`.
* Coach Match Up prep (`0x55D088`): `sub_0034CA40`, `sub_0034BC10`, `sub_0034BD00`,
  `sub_0034BCD0`, `sub_0034BD20`.
* Coach Match Up: `sub_00366E20`, `sub_00367280`, `sub_00367400`, `sub_0034C1B0`,
  `sub_0034C1F0`.
* Game state: `sub_00064C70`, `sub_00064590`, `sub_00064560`, `sub_000F5430`, `sub_00062BE0`,
  `sub_000617E0`, `sub_00061730`, `sub_00055DD0`.
* End Of Game: `sub_0006ED30`, `sub_0006EDA0`.
* Franchise: `sub_0015DB50`.
* Port and match globals: `sub_00077200`, `sub_000771F0`, `sub_00077280`, `sub_00077AE0`,
  `sub_00077B20`, `sub_00077AD0`, `sub_00077B10`.
* Settings rows: `sub_00149120`..`sub_001494E0`, `sub_002C2AA0`..`sub_002C2D70`,
  `sub_000E3150`, `sub_000E3350`, `sub_000E3740`.

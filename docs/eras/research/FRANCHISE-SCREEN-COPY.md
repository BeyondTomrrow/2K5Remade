# My NFL Eras: copying Franchise's own screens (Schedule first)

2026-10-16. Research only: nothing in `src/` was edited, nothing was built, and the game was not
run. xemu was not used (optional; everything below is static). The user's point:

> "I keep saying copy from franchise because it has the schedule screen you could've used that"

Goal: Eras uses copies of Franchise's own screens (Schedule first, then Standings, League
Leaders / stats, Depth Chart, Roster, Trades, Free Agents), fed with Eras league data. They must
never touch the user's Franchise save and never change Franchise behaviour.

Evidence levels used below:

* **verified (XBE)**: read from `original/default.xbe` with capstone (`build/eras/office/xdis.py`,
  `xref.py`, `fsm_graph.py`, plus small dump scripts).
* **verified (gen)**: the same function in `src/recomp/gen/recomp_*.c` (file:line given).
* **cross-checked**: matches the Mod Tools' `docs/mod_editor/franchise_save.md` or this project's
  `RESULT-CAPTURE.md`.
* **inferred** / **to test**.

Companion notes: `FRANCHISE-OFFICE-MENUS.md` (descriptor format, FSM, event records, engine
primitives), `RESULT-CAPTURE.md` section 3 (the franchise result commit), `NATIVE-MENU.md`.

---

## 0. Short version

* **The Franchise Schedule is a thin shell around the game's generic spreadsheet engine.** The
  descriptor `0x522828` is a custom screen with 6 event callbacks (`0x3275C0..0x327A00`). It
  owns a 2-page table (Team Schedule / Weekly Schedule). Each page builds one *sheet*
  (constructor `sub_001749D0`) from a static sheet definition. Sheet definitions, pages and
  columns are plain `.rdata` tables. **Every cell the player sees comes from tiny read-only column
  getters** that read one fixed global block: the franchise season block at
  **`0xE576A0..0xE5FC00`**, and through it the team records it points at.
* **No pointer can be swapped.** The grid, scores, week and stage are read at fixed absolute
  addresses (`0xE57C40`, `0xE587F0`, `0xE576B4`...), not through a base pointer. Option (a)
  therefore becomes a **content swap**: snapshot the 0x8560-byte block, write Eras data in
  Franchise's layout, and restore it byte for byte when the screen goes away.
* **Team identity is a pointer, not a club id.** The grid stores team *indices* 0..33. In
  league modes 1/2/3, `sub_000C4C50` resolves them through the pointer table `0xE5786C`. The
  cells then read the team record: `+0x138` city, `+0x104` nickname, `+0x108` abbreviation,
  `+0x10C` asset code and `+0x192` variant for the mini helmet. So **28 Eras-owned 0x1F4-byte
  team records** (copies of a club record with the strings replaced) show "Baltimore",
  "Houston", "St. Louis"... and need no club. The helmet comes from the asset code: the modern
  club's, or the historic template's code and variant when `mini.iff` has that helmet (to test).
* **The retail callbacks themselves are not safe outside Franchise.** Msg 3 runs the Coach's
  Desk package loader (`sub_00142D00` -> `sub_00142BB0`, which dereferences the coached team
  `[0xE3C0A0]`). The per-frame input function (`sub_0024D1B0`) runs **season-stage processing
  every frame** (`sub_002486F0`, `sub_0024BF80`). Its START button sims and advances weeks
  (`sub_0024CD20`), and A rewrites grid cells (`sub_000C67E0`).
* **Recommended (option c, a hybrid):** clone the descriptor and give it **its own event table**.
  Own msgs 1/2/3/4/6 are small C functions that call the retail sheet engine directly. The safe
  retail msgs 5 and 8 dispatch through an Eras-owned page table. The retail sheet definitions are
  cloned, with the A activation repointed, and the retail pages, columns, getters and draw code
  are reused unchanged. Data reaches the screen through the season-block content swap plus the
  Eras team records. No gen patch is needed.
* Other screens:

  | Screen | Feasibility | Why |
  |---|---|---|
  | Depth Chart | **easy** | already done (`nfl2k5_eras_depth_edit`) |
  | Roster | **medium** | same staging trick |
  | Standings | **medium-hard** | data is in the same block; the layout is fixed to 4 divisions per conference |
  | League Leaders / stats | **hard** | player season stats live on player records in the roster arena |
  | Trades / Free Agents / Contracts | **hard, do not copy** | they mutate the roster arena and the front-office block |

  Details are in section 6.

---

## 1. The Franchise Schedule screen

### 1.1 Descriptor and events (verified (XBE); gen `recomp_0022.c:58678..60060`)

`0x00522828`: title 0 (drawn by msg 8), event table `0x522618..`, handler **`0x000F3DA0`
(custom)**, no rows or layout, flags **`0x95`** (Back pops, START sends msg 11 is *not* set,
background nibble 5), `+0x30` = 3. It is pushed only by the desk row action `sub_00142880`
(`[0xAA2408] = 0x522828`, deferred push). Its twin, the E-mail Inbox `0x522A68`, shares
everything except msg 3's second call.

| Msg | Record | Function (gen) | Does | Franchise coupling |
|---|---|---|---|---|
| 1 created | `0x5227A8` | `sub_00327A00` (0022.c:60060) | zero `[0xC95E10/14/E8/EC]`; `sub_00327980(sub_00069E70())` (UI team colours `[0xC95DF0..FC]`); `[0xC95E18] = 0` (button-legend table), `[0xC95E0C] = 0`, `[0xC95E00] = 0xEA3E60` (title ""); tail `sub_0024C3A0` (clears `[0xC178F8]`, `[0xC177EC]`, `[0xC177E8]`, `[0xC17904]`) | `sub_00069E70` returns `[0xA84494]` if set, else the coached team in mode 2 |
| 3 activated | `0x522618` | `sub_003275F0` (0022.c:59256), then `sub_00327340` (0022.c:58678) | **`sub_00142D00`** (the desk's package loader: `fr.iff` ctx `FRANCHISE1`, **`sub_00142BB0` = fr\<code\>.iff from `[[0xE3C0A0]+0x10C]`**, `mini.iff` ctx `FRMINI`, unless `[0xAA240C]`); `sub_0027D380` (`[0xC3AC84] = 1`); resolves 3 TXTRs (`L"mailpieces"`...) at `0xAE37B0..0xAE3828` by `sub_0006A510` (no null check); `sub_00069E60(sub_0013EC80())` = `[0xA84494] = [0xE3C0A0]`. Then `sub_00327340`: page table `[0xC95E04]` = `0xAE3750` (2 pages) in stages 7/8/9, else `0xAE3770` (1 page); `[0xC95E08]` = count; clamp `[0xC95DEC]` | **yes**: dereferences the coached team; needs `fr.iff` |
| 4 shown | `0x522688` | `sub_00327620` (0022.c:59392) | calls the current page's init `[[0xC95E04] + 16*[0xC95DEC] + 0]` | via the page init (1.2) |
| 5 covered | `0x5226D0` | `sub_003275C0` (0022.c:59103) | calls the page's leave `[page + 4]` | no |
| 6 frame | `0x522718` | `sub_003276C0` (0022.c:59550) | pads 0..7: `0x2000` / `0x1000` change page (cursor sound); then page leave + init, or the page frame `[page + 8]`; nonzero return pops (`sub_0006E400`) | via the page frame (1.3) |
| 8 overlay | `0x522760` | `sub_00327690` (0022.c:59498) | `sub_0006C080` (background), `sub_003274D0` (title text `[0xC95E00]`), `sub_003271C0` (button legend from `[0xC95E18]`, empty here), page draw `[page + 0xC]` | via the page draw |

There is no msg 2. The sheet is destroyed by the page leave.

### 1.2 Pages and the sheet (verified (XBE))

Page table `0xAE3750` (`.data`, 16 bytes per page: init, leave, frame, draw):

| Page | Init | Leave | Frame | Draw | Mode passed to `sub_0024B740` |
|---|---|---|---|---|---|
| 0 | `sub_0024C410` | `sub_0024C440` | `sub_0024D430` -> `sub_0024D1B0` | `sub_0024C480` | 1 = **Team Schedule** |
| 1 | `sub_0024C3F0` | same | same | same | 0 = **Weekly Schedule** |

`0xAE3770` (offseason) has one page with mode 2 (the offseason calendar).

`sub_0024B740` (gen `recomp_0016.c:59769`; `eax` = FSM, `edi` = mode) frees any old sheet
(`sub_001707F0(0xC177F0)` if `[0xC178F8]`) and then builds one:

| Mode | Before | Sheet def (wrapper ptr) | After |
|---|---|---|---|
| 0 weekly | `sub_000C6A90(sub_000C4E70())`: selected week `[0xE576C0]` = current week `[0xE576B4]` | `[0x537B90]` -> wrapper `0x537B54` = {sheet `0x537858`, rect 33, 607, 71, 382} | `sub_001707C0(row = sub_000C6920(team, week))`, `sub_000F32C0(FSM)` |
| 1 team | **`sub_000C62B0(sub_0013EC80())`**: schedule team `[0xE576CC]` = **coached team** | `[0x537BE4]` -> `0x537BA8` = {sheet `0x537958`, same rect} | `sub_001707C0(row = current week)` |
| 2 offseason | | `[0x537C38]` -> `0x537BFC` = {sheet `0x537A58`, rect 33, 607, 71, 394} | |

All modes then set `[0xC178F8] = 1` (sheet alive) and `sub_003278F0(0)`, which clears the button
table.

Sheet construction: `sub_001749D0(ecx = 0xC177F0 (the static widget), edx = sheet def; stack:
FSM, w+0x0C, w+0x10, w+0x04, w+0x08)`, `ret 0x14` (verified (XBE); gen `recomp_0011.c:78576`;
the Mod Tools' tests call it the same way). Page selection is `sheet def + 0xF4`.

**Sheet definitions** (`.rdata`, `0x100` bytes each; offsets that are set):

| Def | +0x30 tab value | +0x48 tab label | +0x60 tab count | +0x78 / +0x8C next / prev tab | +0xC4 / +0xDC **A activation** | +0xF4 page |
|---|---|---|---|---|---|---|
| `0x537858` weekly | `sub_000C5240` = `[0xE576C0]` week | `sub_0024B8C0` ("") | `sub_000C4E50` = `[0xE576B0]` weeks in stage | `sub_000C6AA0` / `sub_000C6AD0` (wrap `[0xE576C0]`) | **`sub_000C67E0`**, args `0x00020039` | `0x537640` |
| `0x537958` team | `sub_000C62A0` = `[0xE576CC]` team | | `sub_000C7D40` (team count) | `sub_000C7E30` / `sub_000C7E60` (cycle `[0xE576CC]` through `sub_000C62E0`) | **`sub_000C8170`** | `0x5376F8` |

**Pages and columns** (`.rdata`; page row count at `+0x30`, column list at `+0x98`, columns are
0xB0 bytes with type `+0`, getter `+8`, arg descriptor `+0xC`, width float `+0x48`, image
callbacks `+0x54..+0x60`, header text `+0x6C`):

| Page | Rows | Columns (type, getter, header) |
|---|---|---|
| `0x537640` weekly | `sub_000C4F20(week)` = games in the row (scan to type 7, max 17) | `0x536D50` (6, `sub_000C80B0`: status mark), `0x536E00` (0xA, image `sub_0024BBE0`: **away** helmet + score), `0x536EB0` (1, **`sub_0024CAD0`**, "AWAY"), `0x536F60` (1, **`sub_0024CA10`**, "HOME"), `0x537010` (0xA, `sub_0024BDA0`: home helmet + score) |
| `0x5376F8` team | `sub_000C4E50` (weeks) | `0x5370C0` (6, `sub_000C8150`), `0x537170` (0xA, `sub_0024B900` opponent helmet), `0x537220` (1, `sub_0024C8A0`, "OPPONENT"), `0x5372D0` (1, `sub_0024CB90`, "SCORE") |

**What the getters read** (verified (XBE); `sub_0024CAD0` gen `recomp_0016.c:64205`,
`sub_0024BBE0` 0016.c:60625, `sub_0024B220` 0016.c:58923):

* `sub_0024CAD0` (AWAY): if `!sub_000C4FB0(week, slot)` it shows "TBD". Otherwise
  `team = sub_000C4E20(week, slot)`. If `team+0x118` is 0x22/0x23 (the AFC/NFC Pro Bowl teams,
  `sub_001345C0`) it shows `team+0x104`. Otherwise it shows `"%s\n(%u-%u)"` or `"%s\n(%u-%u-%u)"`
  with **`team+0x138` (city)** and W/L/T from `sub_000C7780` / `sub_000C7750` / `sub_000C7720`.
  Those three sum the grid over 22 rows (`sub_000C75A0`: type 3 cells, comparing
  `sub_000C5110` / `sub_000C5150`). **The record is computed from the grid, not stored.**
  `sub_0024CA10` (HOME) is the same with `sub_000C4F90` / `sub_000C4E00`.
* `sub_0024BBE0` / `sub_0024BDA0` (helmets): `sub_0024B220(team)` looks up TXTR
  `"mini_a%s_%d"` (`team+0x10C`, `team+0x192`), then `"mini_a%s_0"`, then `"mini_h..."`, then
  `"unknown_a"` / `"unknown_h"`, **always in ctx `L"FRMINI"`**. If nothing is found, nothing is
  drawn (`je 0x24B515`); there is no crash. For played cells (`sub_000C4FD0 == 3`) the score is
  `"%d"`, highlighted `0xC0B100` for the winner.
* The page draw `sub_0024C480` (gen `recomp_0016.c:62960`):
  * `"%s Record: (%u-%u-%u)"` for `sub_00069E70()` (`[0xA84494]`, `+0x108`);
  * `"week %d"` (`sub_000C5240() + 1`), or the team-page title `[0xE576CC]+0x104`;
  * the selected game's date `"%s %d, %d"` (month table `0xAC6084[cell+3]`, day `cell+4`, year
    `[0xE576B8] + 2004`, +1 for months before June);
  * the stage banner from `[0xE576A4]`: 7 "PRE-SEASON", 8 "SEASON" (rows 17..21: WILD CARD
    ROUND, DIVISION CHAMP, CONFERENCE CHAMP, SUPER BOWL, PRO BOWL), others "OFF-SEASON";
  * finally the sheet: `sub_00176380(0xC177F0, FSM)`.

  It reads only. It re-initialises the page only if page mode is 2 and the stage is 7/8.

### 1.3 The page frame `sub_0024D1B0`: why it must not run in Eras (verified (XBE); gen 0016.c:65499)

Every frame:

1. `sub_0024C2F0`: help text "to start Regular Season" / "to start Playoffs" / "to start
   Off-Season" by stage; help entries 0xD "for Franchise Options" (page 2), 0xF "for Team / Weekly
   Schedule", 0xB "to watch this week's SportsCenter".
2. Pads 0..7 (`sub_00070A30` = pressed):

   | Button | Calls | Effect |
   |---|---|---|
   | A `0x100` | `sub_00176B50` -> sheet `+0xC4` = **`sub_000C67E0`** | rewrites the cell type (0 -> 4/5/1: the sim / play marks) |
   | Y `0x800` | `sub_00176C20` | |
   | **START `0x10`** | **`sub_0024CD20`** | "You are about to skip one or more select..." dialogs, then **sims and advances**; it can push Player Contracts `0x540650` and Free-Agent Wire `0x53EC38` |
   | up / down `0x1000000` / `0x2000000` | `sub_001703D0` / `sub_00170420` | cursor |
   | `0x80` | `sub_001345E0` -> `sub_00134690` | SportsCenter |
   | RT `0x8000` | `sub_00174D50` | next tab; on page mode 2 it **pushes Franchise Settings `0x52BDD8`** |
   | LT `0x4000` | `sub_00174DA0` | previous tab |
   | Back `0x220` | `sub_00070A50(pad, -1)` | return 1, which pops |

3. After the loop, **every frame**:
   * `sub_00327980(sub_0013EC80())` (team colours from `[0xE3C0A0]`, and `[0xA84494]` through
     `sub_00069E60`);
   * **`sub_002486F0`**: if the stage's weeks are done, `sub_002480B0` advances the stage and
     `sub_00247D40` runs weekly processing;
   * **`sub_0024BF80`**: the auto offseason steps of Franchise Settings `[0xE60130..0xE6013C]`,
     which can push The Draft `0x537E38`;
   * then it re-initialises the page if the stage changed.

So the retail frame is Franchise game logic, not just UI. It must be replaced, not filtered.

### 1.4 The franchise season block (cross-checked with `franchise_save.md`; verified (XBE) where marked)

`sub_000C7530` (gen `recomp_0005.c:61598`) zeroes `0x2158` dwords from `0xE576A0`, so the
block is **`0xE576A0..0xE5FC00` = 0x8560 bytes** (verified). It then sets `0xE5A240` (10 dwords)
to 1. The serialiser `sub_000C5310` (0005.c:55085) writes it into the save at file `0x91320`.

| RAM | Field | Schedule screen use |
|---|---|---|
| `0xE576A0` | league mode: 0 none, 1 Tournament, 2 Franchise, 3 online season | `sub_000C4C50` picks the team mapping (1.5) |
| `0xE576A4` | stage: 7 preseason, 8 regular, 9 postseason, 0..6 offseason | page table, banner, many gates |
| `0xE576A8` | sub-state | help text (`sub_000C4BD0`) |
| `0xE576AC` | team count | `sub_000C4BE0` (modes != 0) |
| `0xE576B0` | weeks in the stage | tab count, `sub_002486F0` |
| `0xE576B4` | current week (row) | initial tab, `sub_002486F0` |
| `0xE576B8` | year field, 2004 + n (dword in RAM) | date line |
| `0xE576BC` | current slot | |
| `0xE576C0` | **selected week (tab)** | weekly sheet `+0x30` |
| `0xE576CC` | **team-page team pointer** | team sheet `+0x30`, page title |
| `0xE576D4` | division 0..7 per team (34 u32) | Standings |
| `0xE5775C` | user control per team (34 u32) | `sub_000C4D50` (user-team marks) |
| `0xE5786C` | **34 team pointers** (`sub_000C4C50` modes 1..3) | every name and helmet |
| `0xE57954` | cell flags, 2 bytes per cell: home known, away known | "TBD" vs a team |
| `0xE57C40` | **grid**: 22 rows x 17 slots x 8 bytes `{type, home, away, month, day, code, hour, minute}`. Type 0 scheduled, 3 played, 7 row end, 1/4/5 sim marks (`sub_000C67E0`). Rows 0..16 weeks, 17 WC, 18 DIV, 19 CONF, 20 SB, 21 Pro Bowl | everything |
| `0xE587F0` | quarter scores, 10 bytes per cell: **home `+0..+4`, away `+5..+9`** (`sub_000C5110` sums home, `sub_000C5150` away; verified) | scores, records |
| `0xE5968C..` | awards, league stat tables (`0x32C` rows), tail | not used by Schedule |

The grid accessors are leaf functions at fixed addresses (verified (XBE); gen
`recomp_0005.c:53520..54626`):

* `sub_000C4E00` / `sub_000C4E20(week, slot)`: home / away team pointer (byte +1 / +2 ->
  `sub_000C4C50`);
* `sub_000C4F90` / `sub_000C4FB0`: the flags;
* `sub_000C4F10`: the cell address;
* `sub_000C4FD0`: the status (type);
* `sub_000C5110` / `sub_000C5150`: the scores;
* `sub_000C4F20(week)`: games in the row;
* getters `sub_000C4B80` (mode), `sub_000C4BB0` (stage), `sub_000C4E50`, `sub_000C4E70`,
  `sub_000C4EB0`, `sub_000C5240`, `sub_000C62A0`.

Many callers also inline `0xE57C40 + 8*(17*week+slot)` themselves (`sub_000C80B0`,
`sub_000C67E0`, the date line).

### 1.5 Team ids -> names and logos (verified (XBE))

`sub_000C4C50(ecx = index)` dispatches on the mode `[0xE576A0]`:

* **mode 0**: `[0xB72918]+0x1C + index*0x1F4`, the roster arena's own team table (count
  `+0x18`);
* **modes 1, 2, 3**: `[0xE5786C + 4*index]`, so **any pointer works**.

The reverse lookup `sub_000C4CA0(team ptr)` scans 0..team count.

Team-record fields the Schedule touches (names from `src/nfl2k5_teams.c` and its inventory
writer):

* `+0x104` nickname (team-page title; Pro Bowl ids);
* `+0x108` abbreviation (header "Record" line);
* **`+0x138` city (the AWAY / HOME cells)**;
* `+0x10C` asset code (mini helmet; colours via `sub_00068D70` -> `sub_00030B90`);
* `+0x118` ident (0x22/0x23 = Pro Bowl);
* `+0x192` helmet variant.

So: **the screen can show any text for a team that is not a club**, provided it has a team
record in guest memory. It never asks for a club id. The 1980 teams therefore get Eras-owned
records:

* a copy of the mapped modern club's record (colours, codes; Colts -> IND, Oilers -> TEN,
  Rams -> STL, Raiders -> OAK, Cardinals -> ARI);
* with `+0x138` / `+0x104` / `+0x108` pointing at Eras UTF-16 strings from `teams.csv`
  ("Baltimore" / "Colts" / "BAL");
* optionally `+0x10C` / `+0x192` from the historic template (`data/eras/historic_teams_2k5.json`
  `asset_code` and `variant`, e.g. BAL_1970 = "11", 5), for a throwback mini helmet.

Whether `mini.iff` carries the variant helmets is **to test**. `mini.iff` is a CACR collection
(`mini.cdf`) whose members could not be resolved statically. The `_0` fallback always gives the
club helmet.

---

## 2. How to copy it for Eras

### 2.1 Option (a): duplicate the descriptor, same draw code, redirect the data

The pointer swap the brief suggests is **not possible**: there is no base pointer to swap
(1.4). The workable form is a **content swap** of the fixed block:

* at activation: `memcpy(snap, 0xE576A0, 0x8560)`, then fill the block in Franchise's layout
  from `schedule.csv` and `results.csv`;
* restore `snap` when the screen leaves the top of the stack.

Duplicating the descriptor while keeping the **retail event table** fails, because msg 3 and
msg 6 run Franchise code (1.1, 1.3). So (a) is only viable with own callbacks, which is option
(c).

| What could leak into a Franchise save | Guarantee |
|---|---|
| The swapped block being serialised | The only writer is `sub_000C5310`, reached from the Save Game Data screen `0x507EC8` and the load/save I/O machine. Retail has **no** Franchise autosave (the Mod Tools' r63 report adds one as an option; this project does not). Neither is reachable while the Eras screen is on top. Restore on **msg 5 and msg 2**, so the block is Eras-filled only while the screen is the top screen. |
| Franchise commit / week processing writing the roster arena | Never call `sub_000C5D60`, `sub_00135310`, `sub_002480B0`, `sub_00247D40`, `sub_002486F0`, `sub_0024BF80`, `sub_0024CD20`, `sub_00135790`, or the retail page frame `sub_0024D1B0`. |
| The coached-team pointer `[0xE3C0A0]` | Never read or written on the Eras path (the retail msgs 1, 3 and 6 that touch it are replaced). |
| UI override `[0xA84494]` (set by retail msg 3) | Snapshot it at msg 3 and restore it at msg 5 / msg 2. |
| The Coach's Desk package globals `[0xAA240C]`, `0xAA2410..0xAA2458` | Not used: load `mini.iff` into ctx `FRMINI` with an Eras-owned slot, the way the online schedule's `sub_00372500` does (2.3). |
| A crash while swapped | The block is RAM only; nothing reaches disk. A later Franchise new game (`sub_000C7530`) or load (`sub_000C5800`) overwrites the block anyway. |
| "Dirty flag `0xAA26B0`" | That is the **Game Options** dirty flag (`NATIVE-MENU.md`, set by `0x501C28` msg 1, saved at msg 5), not a Franchise flag. The Schedule never touches it. Test that it stays 0. |

Verification hook: hash the block and `[0xA84494]` before the fill and after the restore. A
mismatch is logged, and the restore is retried from the snapshot.

### 2.2 Option (b): gen patches on the reads, gated by an Eras flag

Patch the ~20 leaf accessors (`sub_000C4B80`, `C4BB0`, `C4E00`, `C4E20`, `C4E50`, `C4E70`,
`C4EB0`, `C4F10`, `C4F20`, `C4F90`, `C4FB0`, `C4FD0`, `C5110`, `C5150`, `C5240`, `C62A0`,
`C4C50`, `C6920`, `C7720/50/80`...) so they return Eras data when an Eras flag is set.

* Leaks: none to the save, because the block is never written.
* Problems:
  * reads that bypass the accessors would be missed, for example `sub_000C80B0` (date/status
    column), `sub_000C67E0` and the date line in `sub_0024C480`, which compute `0xE57C40 + ...`
    themselves;
  * it touches `src/recomp/gen` in about 25 places across `recomp_0005.c` and `recomp_0016.c`,
    with the seed hazards in memory (`nfl2k5_seed_hazards.md`);
  * a stuck flag would corrupt a real Franchise at runtime.

**Not recommended.**

### 2.3 Option (c, recommended): cloned descriptor + own callbacks + retail engine + content swap

What is reused unchanged:

* the handler `0x000F3DA0`;
* the sheet engine (`sub_001749D0`, `sub_001707F0`, `sub_001707C0`, `sub_00174C70`,
  `sub_00174D50` / `sub_00174DA0`, `sub_001703D0` / `sub_00170420`, `sub_00176380`);
* the retail pages and columns (`0x537640`, `0x5376F8` and their 9 columns);
* all column getters;
* the page draw `sub_0024C480`, page leave `sub_0024C440`, msg 5 `sub_003275C0` and msg 8
  `sub_00327690`.

What is Eras-owned:

* the descriptor;
* the event table;
* the page table (so the retail msg 5 and msg 8 dispatch to Eras code);
* two sheet-definition clones with the A activation replaced;
* the 28 team records and the season-block fill.

Why it works:

* every retail function left in the path reads only the season block, the team records, the
  widget `0xC177F0` and the UI statics `0xC95DE8..0xC95E1C` / `0xC177E8..0xC17908`. Those statics
  are Franchise UI state, not saved, and are re-initialised at msg 1 as retail does;
* `sub_0024C480` re-initialises the page only in page mode 2 with stage 7/8, which Eras never
  uses.

---

## 3. Implementation plan: the Schedule screen

Screen slot: reuse `EM_SCHEDULE` in `src/nfl2k5_eras_menu.c` (served functions at
`0xFEE0S000`), or add a new `EM_FSCHED` so the old list stays available as a fallback.

### 3.1 Boot-time data (guest heap, `em_alloc`)

| Object | Size | Content |
|---|---|---|
| `ERAS_SCHED` descriptor | 0x34 (copy of `0x522828`) | `+0x04` own event table for msgs **1, 2, 3, 4, 5, 6, 8**. `+0x08` `0xF3DA0`. `+0x28` keeps `0x95`. msg 5 and msg 8 are served stubs that tail-call `sub_003275C0` / `sub_00327690` |
| `ERAS_PAGES` | 2 x 16 | `{EINIT_TEAM (served), sub_0024C440, EFRAME (served), sub_0024C480}`, `{EINIT_WEEK (served), sub_0024C440, EFRAME, sub_0024C480}` |
| `ERAS_SHEET_WEEK` | 0x100, copy of `0x537858` | `+0xC4` and `+0xDC` = served `EACT_WEEK` (same `0x00020039` args: `ecx` = week, `edx` = slot) |
| `ERAS_SHEET_TEAM` | 0x100, copy of `0x537958` | `+0xC4` / `+0xDC` = served `EACT_TEAM` (`ecx` = team, `edx` = week) |
| `ERAS_TEAMS` | 28 x 0x1F4 | copies of the mapped club records (`[0xB72918]+0x1C + club*0x1F4`), with `+0x138`, `+0x104` and `+0x108` set to Eras UTF-16 strings. Optionally `+0x10C` / `+0x192` from the historic template. **Do not change the arena records.** Rebuild after a roster reload |
| `SNAP` | 0x8560 + 4 | the block snapshot + `[0xA84494]` |
| FRMINI slot | 0x24 | load slot for `mini.iff` |

### 3.2 Filling the block (C, `eras_sched_fill()`)

Start from zeroes, as `sub_000C7530` does, without calling it. Then set:

* **League and counters:**
  * `[0xE576A0] = 2` (Franchise mode: the screen's tested path. Mode 1 also maps through the
    pointer table; see risk R3);
  * `[0xE576A4] = 8`;
  * `[0xE576AC] = 28`;
  * `[0xE576B0] = nfl2k5_eras_week_count()` (16 for 1980);
  * `[0xE576B4]` = the week of `nfl2k5_eras_next_game()`, 0-based. **Keep it below
    `[0xE576B0]`** so the stage-complete test can never pass; there is no retail frame anyway;
  * `[0xE576B8] = season - 2004` (signed; -24 gives 1980; the date line adds 0x7D4).
* **Selection:** `[0xE576C0] = [0xE576B4]`, and `[0xE576CC] = &ERAS_TEAMS[user team or 0]`.
* **Teams:**
  * `0xE5786C[i] = &ERAS_TEAMS[i]` for i < 28, the rest 0;
  * `0xE576D4[i]` = division 0..5 (for Standings later);
  * `0xE5775C[user] = 1` (cosmetic user mark; to test).
* **Grid:** for each week w (0..15), slot s in schedule order:
  * cell `0xE57C40 + 8*(17w+s)` = {type 0, home, away, month-1? (to test against
    `0xAC6084`), day, 80, hour 13, minute 0};
  * flags `0xE57954 + 2*(17w+s)` = {1, 1};
  * terminator type 7 at slot n;
  * rows 16..21: type 7 at slot 0.
* **Results:** for played games (`nfl2k5_eras_result`):
  * type 3;
  * `0xE587F0 + 10*cell`: home points in byte 0 and away points in byte 5, or per-quarter from
    the league's `box\` file when it has them. Each byte is < 256.

### 3.3 Event callbacks

* **msg 1 (create):** do what `sub_00327A00` does, using the Eras team for colours:
  * zero `[0xC95E10]`, `[0xC95E14]`, `[0xC95DE8]`, `[0xC95DEC]`, `[0xC95E18]`, `[0xC95E0C]`,
    `[0xC177E8]`, `[0xC177EC]`, `[0xC178F8]`, `[0xC17904]`;
  * `[0xC95E00] = 0xEA3E60`;
  * set `[0xA84494]` to the Eras team, then `sub_00327980(ecx = that team)`.

  Retail `sub_00327A00` can be called directly once `[0xA84494]` is set, because
  `sub_00069E70` then returns it. Doing it in C is clearer.
* **msg 3 (activated):**
  * snapshot the block and `[0xA84494]`, then `eras_sched_fill()`;
  * `[0xA84494]` = Eras team;
  * load `mini.iff` like `sub_00372500` does: `sub_000441B0(ecx = L"FRMINI", edx = L"mini.iff";
    stack slot, sub_00038FB0(), 0x22, 0)`, then wait with `sub_000432D0`;
  * `sub_0027D380()`;
  * `[0xC95E04] = ERAS_PAGES`, `[0xC95E08] = 2`, clamp `[0xC95DEC]`.

  Do **not** resolve the `mailpieces` icons: the button table is 0 for the Schedule, and the
  lookup has no null check.
* **msg 4 (shown):** call `ERAS_PAGES[[0xC95DEC]].init`.
  * **EINIT_WEEK** (port of `sub_0024B740` mode 0):
    1. `[0xC177EC] = 0`;
    2. free the old sheet;
    3. `sub_001749D0(0xC177F0, ERAS_SHEET_WEEK, FSM, 71.0, 382.0, 33.0, 607.0)`;
    4. `[0xC178F8] = 1`;
    5. `sub_001707C0(0xC177F0, sub_000C6920(team, week) or 0)`;
    6. `sub_000F32C0(FSM)`;
    7. `sub_003278F0(0)`.
  * **EINIT_TEAM**: the same with mode 1 and `ERAS_SHEET_TEAM`. `[0xE576CC]` was already set by
    the fill; never `sub_0013EC80`.
* **msg 6 (frame), EFRAME:** a port of `sub_003276C0` plus the safe part of `sub_0024D1B0`:
  * for pads 0..7 with `sub_000709B0`, read `b = sub_00070A30(pad)`:
    * `0x2000` / `0x1000`: change page (leave and init, cursor sound);
    * LT `0x4000`: `sub_00174DA0`;
    * RT `0x8000`: `sub_00174D50`, **never** the Franchise Settings push;
    * `0x1000000` / `0x2000000`: `sub_001703D0` / `sub_00170420`;
    * A `0x100`: `sub_00176B50(0xC177F0)`, which now reaches `EACT_*`;
    * Back `0x220`: `sub_00070A50(pad, -1)` and pop (`sub_0006E400`).
  * Help entries: `sub_000F35A0(FSM, 0xF, "for Weekly / Team Schedule")`, and help 0xB / 0xD off.
  * **START, `0x80` and Y are ignored.**
  * No `sub_00327980` per frame: it is already done in msg 1.
* **EACT_WEEK / EACT_TEAM (A on a game):**
  * phase 1: a modal with the game's result line (`sub_0014E520`);
  * phase 2: when the cell is `nfl2k5_eras_next_game()`, offer "Play this game"
    (`sub_0014E540`), restore the block first, then `nfl2k5_eras_launch`.

  Never `sub_000C67E0`.
* **msg 5 (covered):** the retail `sub_003275C0` (page leave `sub_0024C440`: help 0xB/0xD off,
  free the sheet). Then **restore the block and `[0xA84494]`**, and unload `FRMINI`
  (`sub_000432F0`).
* **msg 8:** the retail `sub_00327690`.
* **msg 2 (destroyed):** restore if still swapped (idempotent), `sub_0027D3A0()`, unload
  `FRMINI`.

Push order check (verified (XBE), `FRANCHISE-OFFICE-MENUS.md` 8.6): push = 1, 3, 4; covered = 5;
uncovered = 3, 4; pop = 5, 2. So the block is swapped exactly while the screen is on top.

### 3.4 Wiring

The office's Schedule row (`OFFICE_STOP_SCHEDULE`, day planner camera (5, 7)) pushes
`ERAS_SCHED` instead of the list screen. Keep the old list screen behind
`NFL2K5_ERAS_SCHEDULE=list` as a fallback. `--retail` only: served functions do not run in
interpreted packs (`NATIVE-MENU.md` 2.2).

### 3.5 Tests

1. Open Eras > Schedule on a fresh 1980 league. Weekly page:
   * week 1 shows 14 games, with "Baltimore" / "Buffalo" ... and records (0-0);
   * helmets are drawn (club or throwback);
   * the header shows "week 1", "SEASON" and "September 7, 1980".
2. LT/RT steps through 16 weeks and wraps. Up/down moves the cursor. Page switch gives the
   Team Schedule for the user team, titled with the nickname, with OPPONENT / SCORE.
3. After one played game: type 3, score shown, winner highlighted, records updated (1-0 / 0-1).
4. **No-leak test:**
   * `NFL2K5_MEMDUMP=E576A0:8560` before opening, after closing, and after 20 open/close cycles:
     byte-identical;
   * the same for `A84494:4`, `E3C0A0:4`, `AA240C:4` and `AA26B0:4`.
5. **Franchise regression:**
   * load a real Franchise save, open the desk Schedule, sim a week, save;
   * compare the saved file against the same steps on a build without the Eras screen (the
     franchise_save regions must match);
   * then open Eras Schedule from the Main Menu and load the Franchise again: the desk still shows
     the user's own season.
6. START / Y / `0x80` on the Eras Schedule do nothing. No "skip" dialog appears and no push
   happens. `[FSMOP]` log balanced.
7. A on the next fixture launches the game (if phase 2 is built). After the match, the Schedule
   shows the result, and the block snapshot equals the pre-launch dump.

---

## 4. Risks (Schedule)

| # | Risk | Mitigation |
|---|---|---|
| R1 | Some sheet-engine path calls a retail activation or a column `+0x64` binding not seen statically | The sheet defs are cloned with `EACT_*`. Log every served call; test A / Y on every column |
| R2 | `sub_00176C20` (Y) or a tab callback reaches Franchise code | Y is not forwarded; tab callbacks `sub_000C6AA0` / `C6AD0` / `C7E30` / `C7E60` only cycle `[0xE576C0]` / `[0xE576CC]` (verified) |
| R3 | Front-end code that runs while the screen is up behaves differently when `[0xE576A0] == 2` (82 call sites of `sub_000C4B80` in gen, e.g. music context, `sub_00069E70`) | The swap lasts only while the screen is on top. Test the music and ambience. Fall back to mode 1 (Tournament mapping, same pointer table) if something misbehaves |
| R4 | Month byte base (0- or 1-based against `0xAC6084`) and year sign | Test with one known date |
| R5 | `mini.iff` memory alongside the office packages | It is already loaded by the desk in Franchise with `fr.iff`, which is bigger. Unload it at msg 5 |
| R6 | A roster reload (Roster Manager, community roster switch) leaves `ERAS_TEAMS` copied from stale records | Rebuild the records on every msg 3 (cheap: 28 x 500 bytes) |
| R7 | Playoff rows (17..20) with 1980's 10-team bracket: stage 9's tab indexing was not traced | Phase 1 shows the regular season only; playoffs are to test |
| R8 | Re-entrancy: the Eras launch from the screen pushes Coach Match Up | msg 5 restores before the push. `nfl2k5_eras_after_game` must not assume the block is swapped |

---

## 5. Which Franchise screens open outside Franchise mode (task 4)

None of them assert. They depend implicitly on Franchise state.

| Screen | Outside Franchise? | Evidence |
|---|---|---|
| Schedule `0x522828` | **No** (crash or Franchise logic) | msg 3 `sub_00142BB0` dereferences `[[0xE3C0A0]+0x10C]`; the frame runs `sub_002486F0` / `sub_0024BF80` and dereferences the team through `sub_00327980` |
| Online "Schedule" `0x589558` (League Calendar, table, layout `null`) | Only with an online league | Pushed by `sub_003544E0`; data from the XSN league object `sub_00294AD0`. Its msg 3 / 5 (`sub_00372500` / `sub_00372530`) are a clean FRMINI load/unload template |
| Team Standings `0x54CEA0` | Probably, with empty data (to test) | msg 1 `sub_003550F0` reads `[0xE3C0A0]` (highlight); everything else comes from the season block |
| League Leaders `0x54A910`, Player Stats `0x54A5A8`, Team Stats `0x54CACC`, Rookie Watch | **Yes**: also reached from the online **League Statistics** menu `0x57BC18` (Season `0x528660` / Tournament `0x5289C8`) | xrefs `0x57BB50`, `0x57BB1C`, `0x57BAE8` |
| Depth Chart `0x532D14` | **Yes** (Roster Manager) | already used by `nfl2k5_eras_depth.c` (`[0xC1749C]` team, `[0xC174B4] = 0`) |
| Team Rosters `0x555098` / `0x5557C0`, Team Roster `0x5555F8`, Roster `0x5559B0` | **Yes** (Roster Manager `0x555200`) | team source by mode `[0xACECD0]` -> `[0xACECD4]` (`sub_002B98F0`, `sub_002B8020`; some modes use the coached team) |
| Player Trade `0x55A0F0`, Trading Block, Player Contracts `0x540650`, Free-Agent Wire `0x53EC38` / `0x583470`, Cap, Team Needs, Franchise Status | **No** (front-office block `0xE3C0A0..0xE5xxxx`, coached team) | `FRANCHISE-OFFICE-MENUS.md` 6; the online Player Trade copies `0x588B88` / `0x588DC8` belong to the online league |

---

## 6. Other screens: data and copy feasibility (task 3)

| Screen | Where its data lives | Copy approach | Feasibility |
|---|---|---|---|
| **Standings** `0x54CEA0` (custom, `league_news_common`) | The same season block: W/L/T from the grid (`sub_000C7720/50/80` in `sub_003554B0`, gen `recomp_0023.c:195467`), divisions `0xE576D4`, team pointers `0xE5786C`, clinch marks `sub_002A7C40` ("x - clinched playoff berth"...). The layout is a `.data` table at `0xAE92C0..0xAE9400` with fixed division headings (`0xEB3C14` "NORTH" etc.) | Same content swap as the Schedule, plus own msgs 1 and 6. Problems: 2004 layout of 4 divisions x 4 teams per conference against 1980's 3 divisions of 5/4/5. The headings would need a temporary `.data` swap, and a 5th team per division probably has no row (to test) | **medium-hard**. Keep the Eras standings list until the 5-team test is done |
| **Depth Chart** `0x532D14` -> `0x533088` | Player records of the staged team (rank/side bits in `+0x28`) | Done: stage into USER1/2, `[0xC1749C]`, push the retail screen, write back (`nfl2k5_eras_depth_edit`) | **easy (done)** |
| **Roster** (Roster Manager Team Roster `0x5555F8` / Roster `0x5559B0`) | Team record player lists in the arena | Stage the team as for the Depth Chart, set the screen mode / team `[0xACECD0]` / `[0xACECD4]` to the slot, push. Must stay read-only (Team Rosters has trade/release actions that edit the arena): use the single-team "Roster" variant, or clone its sheet and drop the activations | **medium** |
| **League Leaders / Player Stats / Team Stats** | Player season stats on player records (arena history pool; accessor `sub_000CB240` scope 11 "This Season", `RESULT-CAPTURE.md` 4), team season fields `team+0x1AA..+0x1E8`, league stat rows in the block `+0x2AD0` | Needs all 28 Eras rosters' season stats on resident player records. Only 2 spare slots exist, so the only route is a whole Eras roster arena (6.1) | **hard** |
| **Trades / Trading Block / Free Agents / Contracts** | Front-office block (trade records `0xE3C27C`, FA bids `0xE3C600`, cap `0xE3C278`, IR...) and **writes to the arena** (player moves, salaries `sub_000C3F00`) | Copying them means running Franchise transaction code on Eras teams. Every action is a mutation that would need a write-back to the Eras CSVs | **hard; do not copy now** |

### 6.1 Later: an Eras roster arena (research note, not a plan)

The community 1980 save is a full `ROST` arena (52 team records, `COMMUNITY-ROSTERS.md`), and
there are matching community franchise saves (`FXG`, 720,044 bytes) with their own season
blocks.

Idea: point the roster root `[0xB72918]` at an Eras-owned arena while the Eras hub is open, and
restore it on leaving. Rosters, Leaders, Player Cards and stats would then all read Eras data.

* It is a pointer swap, but the game caches team and player pointers widely (`0xE5786C`,
  `[0xB72A50..5C]`, depth chart, Team Select).
* Saves of rosters (Save Roster `0x507B38`) and the Eras staging code (USER1/USER2 from the
  retail arena) would have to be fenced.
* **Risk: high. Needs its own research task.**

---

## 7. Reproduce

```
python build/eras/office/fsm_graph.py 522828 522A68 54CEA0 589558 57BC18 --depth 0
python build/eras/office/xdis.py 327A00      # msg 1; also 3275F0 327340 327620 3275C0 3276C0 327690
python build/eras/office/xdis.py 24B740      # page init; 24C480 draw; 24D1B0 frame; 24CAD0 / 24BBE0 / 24B220 cells
python build/eras/office/xdis.py C4C50 40 --all   # team index -> record
python build/eras/office/xdis.py C7530       # block size 0x2158 dwords
python build/eras/office/xref.py 54A910 54A5A8 54CACC 55A0F0 540650 53EC38
```

Tables dumped with `build/eras/native-menu/xbe.py` (`Xbe().u32`): page table `0xAE3750`, sheet
wrappers `0x537B54` / `0x537BA8` / `0x537BFC`, sheets `0x537858` / `0x537958` / `0x537A58`, pages
`0x537640` / `0x5376F8` / `0x5377A8`, columns `0x536D50..0x5372D0`.

## 8. Blocked or not done

* **xemu:** not used. All findings are static; nothing was observed live.
* **`mini.iff` members:** throwback mini-helmet availability is to test, because the CDF could
  not be decoded statically.
* **Not traced:** the stage 9 (playoffs) tab-to-row mapping, `sub_00176C20` (Y), and the 5-team
  division rendering in Standings.

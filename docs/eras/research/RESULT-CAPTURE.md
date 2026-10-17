# My NFL Eras: reading the result of a played game

2026-10-15. Static research only: generated code (`build/gen-locals/*.c`, and the compiled
tree `src/recomp/gen/*.c` for patch anchors), the retail `default.xbe`
(sha256 `73105b17…a4a9`), this project's sources, and 2K Football Mod Tools' research in
`.tmp-2k-football-mod-tools/`. The game was not launched, nothing was built, nothing under
`src/`, `tools/` or `config/` was changed. No historical facts are claimed in this note.

Confidence labels:

| label | meaning |
|---|---|
| **VERIFIED** | read directly in generated code or XBE bytes in this pass, or already used by working project code |
| **STRONG** | several independent static reads agree; not yet watched at run time |
| **INFERRED** | follows from the code structure; needs a run-time check |
| **UNVERIFIED** | a guess or a second-hand label; must be checked before use |

Files written alongside this note:

* `data/eras/stat_selectors_2k5.json` — every stat selector the engine knows (183 base,
  49 derived), its type, getters, career-history field, and the labels the game's own stat
  screens give it (271 labelled descriptors).
* `build/eras/result-capture/stat_selectors.py` — read-only generator for that JSON
  (checks the XBE hash, reads `.data`).

---

## 0. Answer in one page

**Hook point (recommended): `sub_000A2910`, label `loc_000A2910` (function entry).**
`sub_000A2910` is the game's "final whistle" routine. It is reached only from
`sub_000AED90`, the entry callback of match-flow state **0x0A (game over)**, which
`sub_000AF260` calls through the state table at `0xB71C88` (slot 10 = `0xB71CB0` =
`0x000AED90`, built by `sub_000AF150`). Every way to end a game found in this pass — clock
expiry at the end of the 4th quarter or an OT period, OT sudden death, a final-play touchdown
that makes the PAT moot, and the franchise-regular-season OT tie — converges on that one
state entry. At the label:

* `ecx` = **home points**, `edx` = **away points** (loaded by `sub_000AED90` from
  `[[0xE5FC28]]` and `[[0xE5FC68]]`, the same score objects the HTML scorebug reads);
* `[0xE602B8]` still holds the **previous** flow state (the caller stores `0x0A` only after
  `sub_000AF260` returns), so `[0xE602B8] == 0x0A` means "re-entry, ignore" — a built-in
  exactly-once guard;
* teams, rosters, team stat blocks and per-player live stat records are all still alive:
  nothing between here and match exit was seen clearing them, the franchise season commit
  reads the team stats again at match exit (VERIFIED), and the post-game stat screens use the
  same accessors (INFERRED).

**Companion hook (recommended): `sub_000645D0`, label `loc_000645D0` (match exit).** Runs
when the match state is left (it is the routine that, in modes 5/6/7, calls the franchise
season commit `sub_000C5D60`). Use it to hand the snapshot to the league database, and to
detect an **abandoned** game (no snapshot since the match was armed).

Readable at the main hook (details in §5): final score, per-period score (team stat block
`+0x254..+0x264` or team selector `0x5B` scopes 1..5), final period (`[0xE602C4]`, ≥5 = OT),
clock (`[[0xE6028C]+0x10]`), how it ended (old flow state), game mode (`[0xE5FF80]`), both
team-data records (`0xB30864` home, `0xB30A58` away: up to 71 player pointers, count at
`+0x11C`), each team's box score through `sub_000CB2D0(team, selector, scope)` and each
player's box score through `sub_000CB240(player, selector, scope)` with scope 0 =
"this match" — the same accessors the game's own box-score screens and the franchise
season merge use.

---

## 1. Live score and match globals (task item 1)

### 1.1 What the scorebug reads — VERIFIED

`src/nfl2k5_presentation.cpp:251-352` (`read_state`) and the header comment at `:252-266`
(credited there to 2K5 Mod Studio's `nfl2k5_overtime.py` / `nfl2k5_scorebug_runtime.py`).
The HTML scorebug runs on these reads in game.

| address | meaning | evidence |
|---|---|---|
| `0xE5FC20` | **home** team object (0x3C bytes) | presentation.cpp:260, 298; mod tools `nfl2k5_overtime.py` `HOME_TEAM` |
| `0xE5FC60` | **away** team object | same |
| `[team+0x08]` | → score object: `+0` points (u32), `+4` timeouts left | presentation.cpp:327-331 |
| `0xE5FC28` / `0xE5FC68` | the two score-object pointers (team `+8`) | `sub_00055DD0` gen-locals recomp_0002.c:37171 |
| `0xB25E30` / `0xB25E48` | the score objects themselves (fixed RAM, set at match init) | same function: `[0xE5FC28]=0xB25E30`, `[0xE5FC68]=0xB25E48`, timeouts `[+4]=3` (:37143) |
| `0xE6028C` | → clock object; `+0x10` game clock, float seconds | presentation.cpp:317-318 |
| `0xE602C4` | **period**: 0 pregame, 1..4 quarters, 5 = OT, 6+ further OT | presentation.cpp:320; `sub_000B8910` writes it at every period start |
| `0xE602B0` | period length, float seconds (= `[0xE6000C]` minutes × 60, `sub_00055DD0`) | presentation.cpp:321 |
| `0xE602B4` | play phase: 0 pregame/toss, 1 safety kick, 2 kickoff, 3 PAT, 4 scrimmage | presentation.cpp:255 |
| `0xE60280` / `0xE60284` | team object with the ball / the other team | presentation.cpp:333 |
| `0xE602EC` | → down state (`+4` down, `+0x18` ball, `+0x28` line to gain) | presentation.cpp:335-340 |
| `0xB307D0` / `0xB30810` | home / away playbook file name, UTF-16 `"SF-pb.iff"` (team code before `-`) | presentation.cpp:263, 299 |

Note on `src/main.c:1181-1210` (short-quarter harness, `NFL2K5_QUARTER_SECONDS`): it reads
the **period** from `0xE602C4` and shortens the **running clock** at `[0xE6028C]+0x10` and
the length at `0xE602B0`. (`[0xE6028C]+0x10` is the clock, not the period.)

### 1.2 The live QB sample — VERIFIED (how the project already reads live stats)

* Gen patch `LIVE_QB_SAMPLE` (`tools/apply-gen-patches.py:652-654`) calls
  `nfl2k5_live_qb_guest_tick()` at `sub_000FCE70` entry (native scorebug update) on the
  guest thread; `nfl2k5_input_hle.c:440-450` also calls it from `XInputGetState`; packs get
  an interpreter hook (`recomp_manual.c:2719`).
* `nfl2k5_live_qb_guest_tick` (`src/recomp_manual.c:368-424`) picks the current QB from the
  depth chart (`sub_00061CD0` away / `sub_00061CB0` home; charts `0xB33A28` away,
  `0xB336F4` home) and reads his stats with `nfl2k5_guest_player_stat`
  (`recomp_manual.c:39-77`), a register-and-x87-preserving guest call of
  **`sub_000CB240(ecx=player, edx=selector, [esp+4]=scope 0)`**. Selectors used:
  completions `0x04`, attempts `0x23`, yards `0x4C`, TD `0x40`, INT `0x16`.
* Rosters it walks: away `0xB30A58` (count byte `0xB30B74` = `+0x11C`), home `0xB30864`
  (count `0xB30980`). These are the match **team-data records** (§4.3).

### 1.3 Match team object layout (0x3C bytes, zeroed by `sub_00087160`) — STRONG

`sub_00087160` (gen-locals recomp_0003.c:112304), `sub_00055DD0`, `sub_001F1D70`
(recomp_0014.c:96121):

| offset | home (`0xE5FC20`) | away (`0xE5FC60`) | meaning |
|---|---|---|---|
| `+0x00` | `0xE5FC60` | `0xE5FC20` | opponent object |
| `+0x04` | | | first controller link (list at `0xE60268`) |
| `+0x08` | `0xB25E30` | `0xB25E48` | score object (points, timeouts) |
| `+0x14` | `0xB663E0` | `0xB663F0` | `sub_00098F00`/`sub_00098F10` object, not decoded |
| `+0x18` | `0xBF08E0` | `0xBF0B5C` | **team live stat block**, 0x27C bytes (`sub_001F1AE0`: `0xBF08E0 + 0x27C*i`) |
| `+0x1C` | `0xB30864` | `0xB30A58` | **match team-data record** (`sub_00061C50` / `sub_00061C60`) |
| `+0x20` | `[0xE5FE80]` | `[0xE5FE84]` | selected source team (`sub_000777D0`/`sub_000777F0`) — meaning INFERRED |

---

## 2. Match flow state and how the game decides it is over (task item 2)

### 2.1 Flow-state globals — STRONG

| address | meaning | evidence |
|---|---|---|
| `0xE602B8` | match **flow state** (ids below) | stored right after each `sub_000AF260(ecx=state)` call (`sub_00055880`, `sub_000A11F0`, `sub_000B8A00`, `sub_000B8160`, ...); zeroed by match init/clear `sub_00055DD0` / `sub_00055B70` |
| `0xE602C8` | pending next state | `sub_000B8A00`; consumed by `sub_00055BD0` |
| `0xE602CC` | float delay before the pending state applies | `sub_000B8A00` (0 at game end, 100.2 otherwise); counted down in `sub_00055BD0` |
| `0xB71C88` | 26 state-entry callbacks, built at match init by `sub_000AF150` (gen-locals recomp_0004.c:109651) | e.g. `[0xB71CB0]` (state 0x0A) = `0x000AED90` (:109678) |
| `0xE5FF80` | game mode word | see below |

Known flow-state ids (only those this note relies on): **0x0A game over** (entry
`sub_000AED90`), 0x0B next play / post-play (entry `0x000AEDB0`), 0x15 end-of-period
presentation (entry `0x000AF140`), 0x09 halftime (set with pending 0x17), 0x07 coin toss
(`sub_000B8160`), 0x0E excluded from period expiry by the clock tick (probably ball in play;
INFERRED). Pending ids: 0x16 restart the next period (`sub_000B8B30`), 0x17 halftime, 0x19
→ state 0x0A.

Game mode `[0xE5FF80]` (from the mod tools: `nfl2k5_overtime.py` `MODE_GLOBAL`, practice
modules, `nfl2k5_anniversary_kickoff` docstrings): 0–3 practice / Basic Training, 5
tournament, 6 franchise postseason, 7 franchise pre/regular season, 8 25th Anniversary
moments. **4 = Quick Game/exhibition is UNVERIFIED** (only "games 4+" is documented). Read
it once in an Eras test game before relying on it.

### 2.2 Path A: the clock runs out — STRONG

1. Clock tick `sub_00157D40` (gen-locals recomp_0010.c:805373): when the clock reaches 0 it
   stores 0 and, unless the flow state is one of 0x0A/0x0E/0x10/0x11/0x12/0x14/0x15,
   tail-jumps to `sub_000A2970(ecx = period)` (`loc_00157EA8`, :805608). Extra conditions
   (jump table `0x157EB8` = Q1–Q3 → `0x157E88`, Q4 → `0x157E97`): in Q1–Q4 not while a PAT
   is pending (phase 3); in Q2, Q4 and OT not while bit 7 of `[0xE602FC]` is set (meaning
   INFERRED: an untimed down is owed).
2. `sub_000A2970` (recomp_0004.c:69294) does the period-end bookkeeping and calls
   `sub_000B8A60(ecx = period)`.
3. `sub_000B8A60` (recomp_0005.c:21701) decides:
   * period 1–3 → `sub_000B8A00(1|2|3)`: quarter break (halftime after Q2);
   * period 4 → if home ≠ away (`[[0xE5FC28]]` vs `[[0xE5FC68]]`, `loc_000B8AB0`) and bit 7
     of `[0xE602FC]` is clear → `sub_000B8A00(5)` = **game over**, returns 1; tied →
     `sub_000B8A00(4)` = go to overtime;
   * period ≥ 5 → scores differ → game over; tied → if mode 7 and period 5 → **game over
     as a tie** (`loc_000B8ADF`, :21794); otherwise `sub_000B8A00(7)` = another OT period.
4. `sub_000B8A00(kind)` (recomp_0005.c:21622) sets state 0x15 and pending
   `0x19` (kind 5) or `0x16` (others) with delay 0 / 100.2.
5. `sub_00055BD0` (per-frame flow tick, recomp_0002.c:36704) applies the pending state;
   pending 0x19 becomes state 0x0A (`loc_00055C35`, :36782) via `sub_00055880` →
   `sub_000AF260(0x0A)`.

This matches the mod tools' overtime research (`nfl2k5_overtime.py` docstring lines 1-25).

### 2.3 Path B: the post-play evaluator — VERIFIED

`sub_000A11F0` (recomp_0004.c:63456) runs after each play. From `loc_000A130F`:

* period ≥ 5 and home ≠ away → game over (OT sudden death);
* period 4, clock ≤ 0, phase 3 (PAT pending) and the team with the ball leads → game over
  (no PAT needed);
* then `sub_00089260(0x1B)`, `sub_000AF260(0x0A)`, `[0xE602B8] = 0x0A`
  (`loc_000A1364`..`loc_000A1378`, :63716-63724).

`sub_000A130D` (:63946) is a seed-split copy of the same tail (it also contains
`loc_000A1378`); the project's seed-hazard note applies. The recommended hook is not in
either copy.

### 2.4 Entering state 0x0A — VERIFIED

`sub_000AF260(ecx = new)` (recomp_0004.c:109710): calls `sub_000A14C0(old, new)`, zeroes
`[[0xE60290]+0x10]`, then calls `[0xB71C88 + 4*new]`. The **caller** writes `[0xE602B8]`
afterwards. For 0x0A the callback is `sub_000AED90` (:108813):

```
push 1; call sub_000AEC60
ecx = [[0xE5FC28]]   ; home points
edx = [[0xE5FC68]]   ; away points
jmp  sub_000A2910
```

`sub_000A2910` (:69191; compiled tree `src/recomp/gen/recomp_0004.c:65022`, label at
:65033) has **no other caller**: the only code reference is that tail jump, and the XBE holds
no data pointer to `0x000A2910` (it is listed in the recompiler's dispatch table like every
function). `0x000AED90` itself appears once, as the immediate stored by `sub_000AF150`
(`.text` 0xAF1C9). `sub_000A2910` calls, in order: `sub_001BAEB0` (mode ≥ 4),
`sub_00059AE0(period)`, `sub_00158490`, `sub_001CEEA0`, `sub_00088620`, `sub_001D1890`,
`sub_0009FC30`, `sub_000EA500(home, away)`, `sub_00111250`, `sub_001401F0` (81 calls to the
team stat accessor; purpose not decoded), `sub_0020C670(home, away)` (25th-Anniversary
moment result, mode 8), `sub_00116380`. None of these were seen clearing stat storage.

### 2.5 After the final whistle

Post-game presentation and screens run in state 0x0A (not traced). Leaving the match runs
the top-level handlers `sub_00064C70` / `sub_00064CA0` (recomp_0002.c:85162/85250; their
only data references are `.rdata` 0x4E7CDC / 0x4E7D74, inside the top-level state registry
described in PROJECT_STATUS.md around the 2026-09-22 `0x4E7DFC` entry), which call
`sub_000645D0` (:83857). `sub_000645D0` calls `sub_000834F0`, `sub_00061950`,
`sub_000F6030`, `sub_000F72A0`, then **if mode is 5, 6 or 7** `sub_000C5D60` (the franchise
result commit, §3), then teardown (`sub_000F5C70`, `sub_000F5170`, `sub_000F61E0`,
`sub_000432D0`, `sub_000F6230`, `sub_000F6510(1)`). Quitting from the pause menu was not
traced; it should reach `sub_000645D0` without passing state 0x0A (INFERRED).

---

## 3. How franchise mode commits a result (reference) — VERIFIED unless noted

Chain: `sub_000645D0` → (modes 5/6/7) `sub_000C5D60` (recomp_0005.c:60573) → requires
`[0xA83A18]` ∉ {0, 1} → marks the current grid cell **played** (type byte = 3) at
`0xE57C40 + 8*(17*[0xE576B4] + [0xE576BC])` → `sub_001356C0(cell)` → `sub_00135310(cell)`
+ `sub_00073770(0)` → `sub_00134140` (restores six settings words) → `sub_000C4BC0(1)`.
Simulated games use the same commit: `sub_0010B9C0` → `sub_001356D0` → `sub_00135310`.
`sub_00132ED0` (playoff-clinch what-if) saves and restores the grid; it is not a commit.

`sub_00135310` (recomp_0009.c:41434):

* `esi = sub_00061C50()` = `0xB30864` (**home** team-data), `ebp = sub_00061C60()` =
  `0xB30A58` (away);
* team points by period: `sub_000CB2D0(team, 0x5B, scope 1..5)` → `sub_000C5050` (home,
  bytes +0..+4) / `sub_000C5090` (away, +5..+9) into the quarter-score matrix
  `0xE587F0 + 10*cell` (`loc_00135418`.., :41593). This settles the mod tools' "side order
  HYPOTHESIS: home first" in favour of home first (STRONG: depends on `0xB30864` = home);
* `sub_00134DD0(team, side 0 home/1 away, won)` adds per-game team stats into the team
  record's season fields (`+0x1AA..`, games `+0x1DC`), using `sub_000CB2D0(team, sel, 0)`
  with the selectors in the mod tools' `TEAM_STAT_FIELDS`;
* coach W/L via `sub_00135260` with totals from `sub_000C5110` / `sub_000C5150`;
* stage `[0xE576A4]` 7/8/9 (preseason/regular/postseason) drives the playoff branch.

Franchise RAM (mod tools `docs/mod_editor/franchise_save.md`, re-checked here): grid
`0xE57C40` (22 rows × 17 × 8 bytes `{type, home, away, month, day, code, hour, minute}`;
type 0 scheduled, 3 played), cell flags `0xE57954`, quarter scores `0xE587F0`, current row
`0xE576B4`, slot `0xE576BC`, stage `0xE576A4`, weekly league stat rows (0x32C bytes, written
by `sub_00073770`). **For My NFL Eras this path is a model, not a dependency**: Eras keeps
its own league, and in a non-franchise mode `sub_000C5D60` does not run.

---

## 4. Where the statistics live (task item 3)

### 4.1 The two accessors — VERIFIED

Both are `thiscall`-style: `ecx` = entity, `edx` = selector, one stack argument = scope,
`ret 4`, result in x87 ST0 (float).

| accessor | entity | path |
|---|---|---|
| `sub_000CB240` (recomp_0006.c:5905) | **player record** | scope 0..7 → `sub_000CA470` (descriptor `0xA8A510 + 0x1C*sel`) → `sub_000C9510` → record = `[0xB72A60](player)` = `sub_000C96B0` = `[[player+0x30]]`, then the descriptor's `+4` getter; selector ≥ 0xB7 → `sub_000CB0C0` (derived); scope 8/9/10/≥11 → `sub_000CAD50` (history) |
| `sub_000CB2D0` (recomp_0006.c:6028) | **team-data record** (`0xB30864`/`0xB30A58`) | scope 0..7 → `sub_000CB080` → `sub_000CAA00` → block = `sub_000CA4C0(team)` (`[0xB72A58]` if team = `[0xB72A50]`, `[0xB72A5C]` if `[0xB72A54]`), then the descriptor's `+8` getter; selector ≥ 0xB7 → `sub_000CA690`; scope ≥ 11 or 10 → `sub_000CA530` |

`sub_000CA7B0` (recomp_0006.c:1363) registers the pair at match setup:
`sub_001F1D70` passes home `0xB30864`/`0xBF08E0`, away `0xB30A58`/`0xBF0B5C`.

**Scope** (VERIFIED from the game's own labels in `.data`, §4.5): 0 = this match ("Today"
labels), 1–4 = Q1–Q4 ("1st".."4th"), 5 = OT (INFERRED: franchise writes it into the 5th
quarter-score byte), 6 = first half, 7 = second half, 11 = season ("This Season" labels).
Only selectors of descriptor type 2 or 3 answer scopes 1–7; types 0/1 return 0.0 for any
non-zero scope (both `sub_000CA470` and `sub_000CB080` check this). Type-2/3 selectors
include points `0x5B`, pass att/comp/yds/TD `0x23/0x04/0x4C/0x40`, rush att/yds/TD
`0x03/0x50/0x3F`, receptions/yards `0x2E/0x4F`.

The project's `nfl2k5_guest_player_stat` already calls `sub_000CB240` correctly. A team
version is the same call with `sub_000CB2D0`.

### 4.2 Team live stat block (0x27C bytes) — VERIFIED for the fields listed

Home `0xBF08E0`, away `0xBF0B5C` (`sub_001F1AE0`; zeroed by `sub_001F1AF0`; pointer also at
team object `+0x18`). Raw fields seen in the team getters:

| offset | content | getter |
|---|---|---|
| `+0x254,+0x258,+0x25C,+0x260,+0x264` | **points Q1, Q2, Q3, Q4, OT** (u32) | `sub_000CA3F0` (selector `0x5B`): scope 0 = sum of all five, 1–5 = one slot, 6 = Q1+Q2, 7 = Q3+Q4 (jump table `0xCA444`) |
| `+0xE4,+0xE6,+0xE8` | passing yards, three s16 parts summed | `sub_000CAB70` (`0x4C`, scope 0) |
| `+0x110..+0x116` | rushing yards, four s16 parts summed | `sub_000CAC40` (`0x50`, scope 0) |
| `+0x1B4` / `+0x1B5` | u8 counters for selectors `0x02` / `0x0F` | `sub_000C9C30` / `sub_000C9C20` |
| `+0x164` | u8, selector `0x0C` (blocked FG) | `sub_000C9E80` |
| `+0x244` | float, selector `0x62` (see the caveat in §4.4) | `sub_000C97D0` |

Several team getters fall back to a play log for per-period values (`sub_000CA870` →
`sub_002511F0`); that is why the accessor, not raw offsets, is recommended for anything but
the period points. Multiple OT periods: only one OT slot exists (`+0x264`); where period 6+
points go in a *played* game was not traced (the simulator, `sub_00106480`, indexes
`+0x254 + 4*min(period index, 4)`). The scoreboard total `[[0xE5FC28]]` stays authoritative.

### 4.3 Players

**Rosters** — VERIFIED: team-data record `+0x000..+0x118` = up to 71 player-record
pointers, `+0x11C` = count (u8), `+0x14C` = coach record pointer (used by `sub_00135310`).
The record is the 500-byte team layout `src/nfl2k5_teams.c:34-46` uses (`+0x10C` asset code
text, `+0x110` label, `+0x118` ident, `+0x128` category 2 created / 4 historic); `+0x108`
(abbreviation pointer) is empty during a match (presentation.cpp:263-265).

**Player record** (0x54 bytes; `src/recomp_manual.c:159-166, 338-358`; `src/main.c:981-1006`)
— VERIFIED: `+0x06` photo id u16, `+0x10`/`+0x14` first/last name pointers (absolute or
field-relative), `+0x20` jersey = `(u32 >> 3) & 0x7F`, `+0x2C` career-history pointer (mod
tools), `+0x30` → live slot, `+0x35` position (0..16; names via pointer table `0x4F2688`).

**Live per-player record** — partly decoded: `rec = [[player+0x30]]`; getters test float
`rec+4` (zero → the stat is 0) and read counters through `rec+8` (bytes/shorts, e.g.
passing TD = bytes `+0xC,+0xD,+0xE` summed in `sub_0024E3E0`); when `rec+8` is null they
fall back to a log at `[0xAC13D8]` (`sub_00285xxx`). Skip a player when
`[[player+0x30]]` is 0 (the live QB code does the same, `recomp_manual.c:134`).

### 4.4 Selectors for an Eras box score

Full table with types, getters, history field ids and every in-game label:
`data/eras/stat_selectors_2k5.json`. Names below come from the game's own stat-screen labels
(VERIFIED as labels) and, where marked †, from the career-history field mapping in the mod
tools' `nfl2k5_career_stats.py` (`FIELDS`), which the descriptor table's `+0x0C` column
confirms for every one of its 30 selectors.

Player, scope 0:

| group | selectors |
|---|---|
| passing | `0x04` comp†, `0x23` att†, `0x4C` yds†, `0x40` TD†, `0x16` INT†, `0x1F` long; `0xCF` passer rating (derived from `0x23,0x04,0x4C,0x40,0x16`) |
| rushing | `0x03` att†, `0x50` yds†, `0x3F` TD†, `0x1A` long, `0x0F` fumbles, `0x60` yards after first hit, `0xC2` avg |
| receiving | `0x2E` rec†, `0x4F` yds†, `0x3E` TD†, `0x1B` long, `0x44` YAC, `0xC5` avg |
| defence | `0x32` tackles†, `0xB8` sacks (= `0x2F` × 0.5; `0x2F` is half-sacks†), `0x17` INT†, `0x48` INT return yds†, `0x11` forced fumbles†, `0x12` fumble recoveries, `0x47` fumble return yds, `0x34` tackles for loss ("For Loss"), `0x15` hurries, `0x22` passes tipped ("Tips") |
| kicking | `0xAD..0xB0` FG made 0-29/30-39/40-49/50+†, `0xA9..0xAC` FG att by range†, `0xC8` FGM, `0xC7` FGA, `0x1C` long, `0x0C` blocked, `0x58` XP made†, `0x59` XP att†, `0x5A` XP blocked, `0x19` kickoffs, `0x39` kickoff touchbacks |
| punting | `0x2B` punts†, `0x4E` yds†, `0x20` long, `0x2D` inside 20†, `0x3A` touchbacks†, `0x2C` blocked, `0xC0` net average (= `0x61` / `0x2B`) |
| returns | `0x18` KR, `0x4B` KR yds, `0x3C` KR TD, `0x2A` PR, `0x4D` PR yds, `0x41` PR TD |
| other | `0x63` games† (value at scope 0 UNVERIFIED), `0x30` label "Sacks" in the defensive group (meaning UNVERIFIED) |

Team, scope 0 (except points):

| group | selectors |
|---|---|
| score | `0x5B` points (scopes 1–5 per period; also raw block `+0x254..+0x264`) |
| offence | `0x0E` first downs, `0x5D` possessions, `0x5F` time of possession (unit UNVERIFIED), `0x4C` passing yds, `0x50` rushing yds, `0xD0` total yds (derived `0x50+0x4C`), `0x3F` rushing TD |
| conversions | `0x53`/`0x27` 3rd down made/att, `0x54`/`0x28` 4th down made/att, `0x52`/`0x43` 2-pt made/att |
| red zone | `0xB3` trips, `0xB2` TD, `0xB1` FG; defence: `0xB6` stands, `0xB5` TD, `0xB4` FG |
| defence | `0x45` rushing yds allowed, `0x46` passing yds allowed, `0xDE` total allowed, `0x32` tackles, `0xB8` sacks, `0x17` interceptions, `0x0F` fumbles |
| turnovers/penalties | `0x42` turnovers, `0x10` fumbles lost (label "Fum" under Turnovers, INFERRED), `0x16` INT thrown, `0x24` penalties, `0x62` "Yards" |
| other | `0x64` points off turnovers, `0x3C`+`0x41` return TDs, `0x0C`/`0x5E`/`0x57` "Blk", `0x22` "Pass" |

**Caveat on `0x62`**: the mod tools name it `total_yards`. The game's labels put `0x62`
("Yards") directly after `0x24` ("Penalties") on the match screen, and "Yds/Game"
(`0x62`/`0x63`) directly after "Penalties/Game" on the season screen, while total yards on
the match screen is `0xD0` ("Total" = rush + pass). Its getter reads a separate float at
block `+0x244`. Penalty yards is the more likely meaning — UNVERIFIED either way; compare it
with `0xD0` in one test game.

### 4.5 How the labels were obtained — VERIFIED

The game's stat screens use 32-byte descriptors in `.data` (`0xAE3C60..0xAE5E80`):
`{u32, u32, label (UTF-16, .string_), formatter, selector, scope, selector2, scope2}`.
Formatter `0x329400` calls `sub_000CB240` (player) twice and formatter `0x329460` calls
`sub_000CB2D0` (team) twice (recomp_0022.c:69195 / :69293), then a format routine from
`0xAE3A70`. Examples: `0xAE52A0` "Pts" team `0x5B` scope 0; `0xAE5280` "Time of Possession"
`0x5F`; `0xAE5180` "1st Half" player `0x23`/`0x4C` scope 6; `0xAE5BC0` "Comp/Att" team
`0x04`/`0x23` scope 1; `0xAE4500` "Tackles Today" `0x32` scope 0; `0xAE4980` "Sacks" `0xB8`
scope 11. The two leading dwords' meaning (format / kind) was not settled.

### 4.6 Derived selectors (0xB8..0xE8) — VERIFIED table, op names STRONG

`sub_000CB0C0` reads `0xA8B918 + 24*(sel-0xB8)`: `{op, a, b, c, d, e}`; op 0 a+b, 1 a−b,
2 average (b converted to int by `sub_000C9660`), 3 ratio, 4 a+b+c+d, 5 passer rating
(`sub_000CA880`), 6 a×`f32[0x4E4184]` (= 0.5). Listed in the JSON with inputs.

---

## 5. Recommended hook and what it can read

### 5.1 Patch entries (proposed; not applied)

```python
# tools/apply-gen-patches.py
("ERAS_GAME_FINAL", "sub_000A2910", "loc_000A2910: ;", "after", """\
    { extern void nfl2k5_eras_game_final(uint32_t home_points, uint32_t away_points);
      nfl2k5_eras_game_final(ecx, edx); }
"""),
("ERAS_MATCH_EXIT", "sub_000645D0", "loc_000645D0: ;", "after", """\
    { extern void nfl2k5_eras_match_exit(void); nfl2k5_eras_match_exit(); }
"""),
```

Both labels exist once each in the compiled tree (`src/recomp/gen/recomp_0004.c:65033`,
`src/recomp/gen/recomp_0002.c:78700`). A mod pack that changes either function runs it in
the interpreter, where gen patches do not fire; register the same callbacks with
`recomp_interp_add_hook(0x000A2910u, 0, …)` and `recomp_interp_add_hook(0x000645D0u, 0, …)`
the way `nfl2k5_scorebug_interp_hooks` does (`src/recomp_manual.c:2710-2730`; the table
holds 32 hooks, about 12 are used). In the interpreter callback read `g_ecx` / `g_edx`.

`sub_000A2910` uses `ecx`/`edx` after the label, so the callback must save and restore every
guest register and the x87/XMM state around any guest call, exactly as
`nfl2k5_guest_player_stat` does.

### 5.2 Callback logic

```c
/* guest thread; nothing here may block */
void nfl2k5_eras_game_final(uint32_t home_pts, uint32_t away_pts)
{
    if (!eras_armed) return;                    /* set by the Eras match launcher only */
    if (MEM32(0x00E602B8) == 0x0A) return;      /* already in game over: re-entry */
    /* sanity: these equal the scoreboard */
    if (home_pts != MEM32(MEM32(0x00E5FC28)) || away_pts != MEM32(MEM32(0x00E5FC68))) log...;
    snap.home_points = home_pts; snap.away_points = away_pts;
    snap.period      = MEM32(0x00E602C4);       /* 4 regulation, >=5 OT (period-4 OT periods) */
    snap.clock       = MEMF(MEM32(0x00E6028C) + 0x10);   /* 0 on expiry, >0 on sudden death */
    snap.end_state   = MEM32(0x00E602B8);       /* 0x15 = clock expiry path, else post-play */
    snap.mode        = MEM32(0x00E5FF80);
    snap.tie         = home_pts == away_pts;    /* retail: only mode 7, OT period 5 */
    for side in {home: team 0xB30864, obj 0xE5FC20, block 0xBF08E0;
                 away: team 0xB30A58, obj 0xE5FC60, block 0xBF0B5C}:
        timeouts      = MEM32(MEM32(obj + 8) + 4);
        period_points = MEM32(block + 0x254 + 4*q), q = 0..4;   /* cross-check with sum */
        team box      = guest_team_stat(team, sel, 0) for the team list in §4.4;
        n = MEM8(team + 0x11C) (<= 71);
        for i < n: p = MEM32(team + 4*i);
            if (!p || !MEM32(MEM32(p + 0x30))) continue;
            identity: names (+0x10/+0x14), jersey, position (+0x35), photo (+6), pointer p;
            player box = guest_player_stat(p, sel, 0) for the player list in §4.4;
    snap.valid = 1; eras_armed = 0;
    hand a copy to a host worker (JSON under the Eras save folder); return;
}

void nfl2k5_eras_match_exit(void)
{
    if (eras_armed) { record "abandoned"; eras_armed = 0; }   /* armed, never reached 0x0A */
    else if (snap.valid) commit snap to the league database (host side);
}
```

`guest_team_stat` = `nfl2k5_guest_player_stat` with `sub_000CB2D0` instead of
`sub_000CB240` (same ABI: push scope, push a real return site such as `0x001353ABu`, call,
check `g_esp` restored and one x87 push). Values are floats: round as the existing helper
does; sacks come back in halves when read via `0x2F`, whole when read via `0xB8`.

Cost: about 106 players × ~45 selectors ≈ 4,800 short guest calls, once per game.

### 5.3 Fields readable at the hook (summary)

| field | source | confidence |
|---|---|---|
| home / away final points | `ecx` / `edx`; `[[0xE5FC28]]` / `[[0xE5FC68]]` | VERIFIED |
| points by period | block `+0x254..+0x264`; `sub_000CB2D0(team,0x5B,1..5)` | VERIFIED (OT slot meaning INFERRED) |
| final period, OT count | `[0xE602C4]` | VERIFIED |
| clock left | `[[0xE6028C]+0x10]` | VERIFIED |
| how it ended | old `[0xE602B8]` (0x15 expiry vs post-play) | STRONG |
| tie | equal points (retail: mode 7 OT only) | VERIFIED (code path) |
| timeouts left | score object `+4` | VERIFIED |
| game mode | `[0xE5FF80]` | VERIFIED read; value for Quick Game UNVERIFIED |
| home/away identity | team-data `0xB30864`/`0xB30A58` fields, playbook codes `0xB307D0`/`0xB30810`, `[0xE5FE80]`/`[0xE5FE84]` | STRONG / INFERRED |
| rosters | team-data `+0x00..`, count `+0x11C` | VERIFIED |
| player identity | record `+0x06,+0x10,+0x14,+0x20,+0x35` | VERIFIED |
| team box score | `sub_000CB2D0(team, sel, 0)` | VERIFIED accessor; labels per §4.4 |
| player box score | `sub_000CB240(player, sel, 0)` | VERIFIED accessor; labels per §4.4 |

### 5.4 Run-time checks still needed (for the session that can build and launch)

1. One Quick Game CPU vs CPU with `NFL2K5_QUARTER_SECONDS=60` (the existing harness): log
   once at the hook — mode word, old state, period, clock, both scores, period points,
   team `0x5B` scope 0 — and confirm `ERAS_GAME_FINAL` fires exactly once and
   `ERAS_MATCH_EXIT` after it. Compare the logged box score with the game's own post-game
   stats screen.
2. A tied regulation (OT) game: confirm period ≥ 5 at the hook and where OT points sit.
3. Quit from the pause menu mid-game: confirm `ERAS_MATCH_EXIT` runs without
   `ERAS_GAME_FINAL` (the abandon path).
4. Read `0x62`, `0x5F`, `0x30`, `0x63` once and settle their meanings.

---

## 6. Existing project code worth knowing

* `src/nfl2k5_presentation.cpp:1684-1688` `is_final()` treats "period ≥ 4, clock ≤ 0,
  scores differ" as final. That misses OT sudden death (clock > 0), ties, and untimed downs
  owed after the clock expires. State `0x0A` (or the new hook) is the exact signal; the HTML
  `GAME_ENDED` event (`:2706`) and `NFL2K5_EVENT_GAME_END` (`src/nfl2k5_broadcast.h:31`)
  could be driven from it.
* Rules relevant to eras: the tie/OT decision is `sub_000B8A60` (tie only for mode 7 in
  period 5) and OT sudden death is `sub_000A11F0`; the mod tools' `nfl2k5_overtime.py`
  documents patching both. Any era rule set that ends games differently needs a hook there.

---

## 7. Evidence index

Generated-code locations (`build/gen-locals/` file:line of the function; the compiled tree
`src/recomp/gen/` line in brackets):

| function | gen-locals | role |
|---|---|---|
| `sub_000A2910` | recomp_0004.c:69191 [65022] | final whistle (hook) |
| `sub_000AED90` | recomp_0004.c:108813 [103063] | state 0x0A entry |
| `sub_000AF260` | recomp_0004.c:109710 [103882] | set flow state, call entry callback |
| `sub_000AF150` | recomp_0004.c:109651 [103829] | builds the callback table `0xB71C88` |
| `sub_000A11F0` / `sub_000A130D` | recomp_0004.c:63456 / 63946 | post-play evaluator / split copy |
| `sub_000A2970` | recomp_0004.c:69294 [65113] | period expired |
| `sub_000B8A60` | recomp_0005.c:21701 [20258] | end of game / OT / tie decision |
| `sub_000B8A00` | recomp_0005.c:21622 [20185] | queue state 0x15 + pending |
| `sub_000B8B30` | recomp_0005.c:21845 [20396] | restart next period |
| `sub_000B8910` / `sub_0009F940` / `sub_000B81C0` | recomp_0005.c:21443 / recomp_0004.c:56405 / recomp_0005.c:20165 | period start / begin period / OT start |
| `sub_00055BD0` / `sub_00055880` | recomp_0002.c:36704 / 36508 | pending-state tick / apply |
| `sub_00055DD0` / `sub_00055B70` | recomp_0002.c:37081 / 36648 | match init / match clear |
| `sub_00157D40` | recomp_0010.c:805373 | clock tick |
| `sub_000645D0` | recomp_0002.c:83857 [78689] | match exit (companion hook) |
| `sub_00064C70` / `sub_00064CA0` | recomp_0002.c:85162 / 85250 | top-level callers of match exit |
| `sub_000C5D60` | recomp_0005.c:60573 [56609] | franchise commit |
| `sub_00135310` / `sub_001356C0` / `sub_001356D0` | recomp_0009.c:41434 / 41934 / 41967 | season result commit (played / sim) |
| `sub_00134DD0` / `sub_00073770` | recomp_0009.c:40647 / recomp_0003.c:37590 | team season merge / weekly league row |
| `sub_000C5050` / `sub_000C5090` / `sub_000C5110` / `sub_000C5150` | recomp_0005.c:58306..58512 | quarter-score set (home/away) and totals |
| `sub_000CB240` / `sub_000CB2D0` | recomp_0006.c:5905 / 6028 | player / team stat accessor |
| `sub_000CA470`, `sub_000C9510`, `sub_000C96B0`, `sub_000CA4C0`, `sub_000CB080`, `sub_000CAA00`, `sub_000CB0C0` | recomp_0006.c:581, recomp_0005.c:85359, 85750, recomp_0006.c:661, 5607, 1802, 5688 | accessor internals |
| `sub_000CA7B0` / `sub_001F1D70` / `sub_001F1AE0` | recomp_0006.c:1363 / recomp_0014.c:96121 / 95873 | team block registration / match stat setup / block address |
| `sub_000CA3F0` | recomp_0006.c:407 | team points by period |
| `sub_00087160` | recomp_0003.c:112304 | team object init |
| `sub_00329400` / `sub_00329460` | recomp_0022.c:69195 / 69293 | stat-screen formatters |

XBE data (read with `build/eras/result-capture/stat_selectors.py`): base selector table
`0xA8A510`, derived table `0xA8B918`, stat-screen descriptors `0xAE3C60..0xAE5E80`, team
points jump table `0xCA444` = `{CA3FC, CA41D×5, CA425, CA432}`, `f32[0x4E4184]` = 0.5.

---

## 8. Open questions

### 8.1 Run-time answers (2026-10-15, PHASE2-PLAN M1)

Measured with the M1 capture (`src/nfl2k5_eras_capture.c`, gen patches `ERAS_GAME_FINAL` /
`ERAS_MATCH_EXIT`) in retail Quick Game, `NFL2K5.exe --retail`, CPU vs CPU, one-minute
quarters, background mode. Logs and frames: `logs/eras/t11.stderr.log`, `logs/eras/t11-*.png`;
box score `%LOCALAPPDATA%\NFL2K5-PC\logs\eras-capture-1.json`. Details in
`docs/eras/results/M1.md`.

| question | answer at run time |
|---|---|
| `[0xE5FF80]` in Quick Game | **4** (every final and match exit logged mode 4; the attract-mode demo game is mode 4 too). Situation mode not run. |
| hook fires once per game | yes: one `final` per game; the state-0x0A re-entry guard never had to act. A rematch game's final reaches the hook (`final ignored (not armed)`) |
| old state at the hook | `0x15` for a Q4 clock expiry (13-0 game, and the uncaptured 7-0 rematch); `0x12` for an OT field goal with 27 s left (state 0x12 = post-play path, not 0x15) |
| ecx/edx at the hook | equal the scoreboard in both captured games (0-3 OT, 0-13); no warning |
| period at an OT final | `[0xE602C4]` = **7** after three OT periods (5, 6, 7) |
| where OT points go | the single slot `+0x264` (team `0x5B` scope 5 agrees): TEN `0,0,0,0,3`; GameCast's line score shows the same under "OT" |
| `ERAS_MATCH_EXIT` after the final | yes, once, when End Of Game "Exit" is chosen; not on Rematch |
| Rematch | does **not** pass `sub_000645D0`: the second game runs with the capture disarmed, and the first game's file was unchanged (same SHA-256) |
| pause-menu quit | reaches `sub_000645D0` without state 0x0A (`match exit: abandoned`); leaving an attract-mode demo game does too |
| state at match exit | period and flow state are already 0 there; record the score at the final, not at exit |
| team `0x62` | **penalty yards** ("Yards Penalized" on GameCast Team Stats: 5 vs `sel_62` = 5) |
| team `0x5F` | **seconds** of possession (1184 = 0:19:44, 888 = 0:14:48 on GameCast) |
| player/team `0x63` at scope 0 | **1** for every dressed player and both teams: games played, this game |
| player `0x30` | 0 for every player in the games read (still unsettled) |
| player `0xCF` (passer rating) | a fraction: GameCast's "RAT" is the value x 100 (0.750 -> 75.0, 0.299 -> 29.9) |
| cost | 6,458 guest accessor calls in 0.80 ms on the guest thread (106 players, all with live records) |

### 8.2 Still open

* `[0xE5FF80]` value in Situation mode.
* Points scored in OT period 6+: the one game read scored only in period 7 and the points sat
  in `+0x264`, so later OT periods share that slot (consistent with the clamp); a game with
  points in two different OT periods was not seen.
* Player `0x30` ("Sacks" label in a defensive group): read 0 for every player so far.
* Whether a post-game screen changes match stats before exit: GameCast Stats after the final
  showed exactly the captured values; nothing else was checked.
* `[0xA83A18]` semantics (franchise commit requires it not 0/1); `[0xE5FE80]`/`[0xE5FE84]`
  as the selected source teams.
* Injuries suffered during the game were not looked at; an Eras league that tracks them
  needs a separate read.

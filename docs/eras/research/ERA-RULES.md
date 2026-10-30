# Era rules: what NFL 2K5 can change per Eras season

Research only (2026-10-16). Nothing was edited, built or run.

* **Rule history:** `data/eras/league/rules_1966_2025.json`, readable as
  `RULES-HISTORY-1966-2025.md`.
* **2K5 evidence:**
  * `src/recomp/gen` and `src/recomp_manual.c`;
  * floats read from `original/default.xbe`;
  * MATCH-LAUNCH §4, PHASE2-PLAN, M4, M6-depth and M8.
* **Cross-check:** the untracked third-party *2K Football Mod Tools* checkout at
  `.tmp-2k-football-mod-tools/`:
  * `mod_editor/core/nfl2k5_kick_rules.py`, `nfl2k5_overtime.py`, `nfl2k5_penalties.py`,
    `nfl2k5_era_rules*.py`;
  * `data/nfl2k5_era_rules.json`: a 50-row era profile table with hook sites. Its sites match the
    recomp sites below. It patches `default.xbe` bytes; we would use gen patches instead.

## How a rule is switched

**(a) Settings block.** A value in `0xE5FF80..0xE60260`. The Eras launch snapshots that block,
writes the season's value, and restores it after the game (M4). No code change is needed, and
there is no risk to non-Eras play.

**(b) Gen patch reading an Eras flag.** `tools/apply-gen-patches.py` anchors on a function's
`void sub_X(void)` line plus the first exact generated line after it. It inserts C before or after
that line, skips a patch that is already applied, and fails loudly if an anchor moves. Eras
already uses it for `ERAS_GAME_FINAL`, `ERAS_MATCH_EXIT` and `ERAS_RETURN`.

* **The pattern:** the inserted line reads a host global such as `g_eras_rules.kickoff_yd`. The
  global is only non-zero while an Eras game is armed (set at launch, cleared at
  `ERAS_MATCH_EXIT`), so retail play is untouched.
* **Shared caveat 1:** functions dirtied by a mod pack run through the x86 interpreter, which
  skips gen patches. Era rules hold on the retail build or on patch-clean functions.
* **Shared caveat 2:** never rewrite the shared `.rdata` constants (`0x4EDD24`, `0x4F0F98`,
  `0x50A53C`). Each one also feeds AI and geometry comparisons elsewhere. Scale at the use site.

**(c) Not feasible.** The rule is outside the game, or needs field geometry, animation or AI
changes.

**Units.** Field coordinates are in cm, with midfield at 0 and the goal lines at ±4572:

| cm from midfield | Yard line |
|---|---|
| 1828.8 | the 30 |
| 2743.2 | the 20 |
| 4389.12 | the 2 |

Play phase `[0xE602B4]`: 0 toss, 1 free kick, 2 kickoff, 3 PAT, 4 scrimmage.

**The retail baseline is 1994-2010 rules.** Retail 2K5 has kickoffs from the 30, touchbacks at the
20, PATs at the 2, two-point tries, sudden-death OT, a 40/25 play clock and a two-minute warning. So
1994-2010 Eras seasons need no rule changes, and each earlier or later era needs a few patches.

## Per-rule table

Confidence: V = verified in code, S = strong inference, G = guess.

| Rule (NFL years) | Can 2K5 do it? | How | Risk |
|---|---|---|---|
| **Kickoff spot** (40 to 1973, 35 1974-93, 30 1994-2010, 35 2011+) | **Yes** (V). See [Kickoff spot](#kickoff-spot). | (b) | Low-medium. A 35 kick gives more touchbacks: the power curve reaches about 85 yd. Coverage line-ups follow the ball (S). |
| **Dynamic kickoff** (2024+) | Partly | (b), large. The mod tools have a working `nfl2k5_dynamic_kickoff` design. | High: alignment, landing zone, no fair catch |
| **Free kick after a safety** (the 20) | Yes, as shipped (V). `sub_0022E050` branch `loc_0022E0EF` × `0x4F0F98`. | none | n/a |
| **Two-point try** (AFL to 1969; NFL from 1994) | **Changeable** (V). See [Two-point try](#two-point-try). | (b): see the same section | Low. The stat tally and the HTML `TWO_POINT_CONVERSION` event would still say "2pt", so result capture must remap it. |
| **Defensive try return** (2015+) | No knob | (b), medium | Medium |
| **PAT spot** (2 to 2014, 15 from 2015) | Yes, as shipped for ≤2014 (V). See [PAT spot](#pat-spot). | 2015+: (b), 5 hooks | Medium-high |
| **Goal posts on the goal line** (to 1973) | No | (c): stadium geometry, FG/PAT aim and the AI kick range all assume the end line | n/a |
| **Hash marks** (wide to 1971) | Not researched | Probably (c) | n/a |
| **Sudden-death OT** (1974-2011 regular season, postseason to 2009) | Yes, as shipped (V). See [Overtime](#overtime). | none | n/a |
| **OT length** (15 min; 10 from 2017) | Partly (V). See [Overtime](#overtime). | 10-minute OT: (b), scale the clock in OT | Low |
| **Ties after one regular-season OT** (1974+) | **Changeable** (V). See [Overtime](#overtime). | (b): see the same section | Low-medium. **Do not set mode 7:** it is franchise mode with many side effects. |
| **No regular-season OT** (to 1973) | Changeable | (b): end the game tied at the end of period 4. The mod tools have a hook at `0xB8AB0`. | Low-medium |
| **Modified OT** (2012+), **both possess** (2022 post, 2025 regular season) | Not shipped | (b): possession bits in spare `0xE602A8`. The mod tools have designs. | Medium |
| **OT timeouts** | Changeable (V). `sub_000B81C0` gives `setne(mode!=7)+2`: 2 in mode 7, 3 otherwise. | (b) | Low. The real per-era count is G. |
| **Play clock** (about 30 s to 1987*; 45/30 1988; 45/25 1990; 40/25 1993+) | No option, but constants (V). See [Play clock](#play-clock). | (b): after the 40.0 store, and on the 25.0 immediate | Low. `0xE602AC` is outside the snapshot block, but match init rewrites it, so it needs no restore. |
| **Quarter length** | **Yes** (V). See [Quarter length](#quarter-length). | (a) | None |
| **Penalty sliders** | **Yes** (V). See [Penalty settings](#penalty-settings). | (a) for frequencies. Yardage: Eras writes and restores the `.data` records itself. | Low. Use frequency only to approximate eras such as the pre-1978 contact rules; 1974 holding is already 10 yd. |
| **Two-minute warning** (unchanged 1966-2025) | Correct as shipped (V). See [Two-minute warning](#two-minute-warning). | none | n/a |
| **Out-of-bounds clock rule** | Fixed (V). See [Two-minute warning](#two-minute-warning). | (b) at `0x4E6D58`/`0x4E4188` uses in `sub_000B7110` | Low. Needs history research first. |
| **Touchback spot** (20; kickoff 25 from 2016; 30/35 2024-25) | Yes, as shipped to 2015 (V). See [Touchback spot](#touchback-spot). | 2016+: (b) after that line, only when `[0xE602B4]==2` | Low |
| **Fair catch on a kickoff to the 25** (2023) | No knob | (b), medium | Medium |
| **Receiver contact / Mel Blount rule** (1978), **pass-blocking hands** (1978) | No rule switch | Approximate with (a): the DPI and defensive-holding sliders. True bump-and-run behaviour is AI: (c). | n/a |
| **"In the grasp"** (1979/1991) | No knob | (c): the sack and whistle logic is not exposed | n/a |
| **Instant replay** | 2K5 has no challenges | n/a. A video game needs none. | n/a |
| **Roster limits** (40-53 by year) | **Yes, 42-53** (V, M8). See [Roster limits](#roster-limits). | Eras staging (data) | 40-man seasons (1966-73) must keep 42, or the build-time check fails |
| **Schedule length, strikes, replacement games, playoff field** | Not engine rules | Eras league layer: schedule, standings and seeding data from `rules_1966_2025.json` | 1987: needs replacement rosters for weeks 4-6 |

\* *medium* confidence in the history data.

## Evidence notes

### Kickoff spot

* **Retail kicks from the 30.** Seven sites multiply by `MEMF(0x4EDD24)` (1828.8):

  | Function | Site | Kickoff |
  |---|---|---|
  | `sub_001584A0` | | opening |
  | `sub_00158620` | | second half |
  | `sub_001587F0` | | OT |
  | `sub_0022E050` | `0x22E0E7` | after a FG |
  | `sub_0022E050` | `0x22E173` | after a good try |
  | `sub_0022E250` | `0x22E2B0` | after a failed try |
  | `sub_0022BC70` | `0x22BCD2` | uses `0x50EB74` = −1828.8 |

  The period-start routine `sub_000B8910` calls the first three.
* **Gen patch:** after each multiply, insert `fp_top() *= (50 - yd) / 20.0`. `sub_0022E050`
  repeats its anchor line, so the second patch anchors on `loc_0022E171: ;`.

### Two-point try

* **No menu option.** The XBE has no "Two Point" option string; "2PT CONV." is a stat label.
* **How a try works.** The Field Goal formation kicks; any other play is a two-point try.
* **Scoring:**
  * `sub_000B8400` scores the TD and sets phase 3;
  * `sub_000B8420` scores a good kick (+1);
  * `sub_000B8480` scores a good two-point try: `MEM32(eax) + 2`, x86 `83 00 02` at `0xB8487`.
* **CPU decision (V).** `sub_00208470` calls `sub_00206E70` (callers `0x20B381`, `0x20B500`,
  `0x20B6E9`). The CPU goes for two when:
  * the score difference before the try (`sub_00205F40`) is −2, −10, −5, +1 or +5, and
  * it is late in the game: period 3 or 4 with time ≤ 300 s (`sub_00205FE0`, `0x4E4188`). The
    "late game" part is S.
* **Pre-1994 patch.** A run or pass on the try scored 1 point. Make `sub_000B8480` add 1 instead
  of 2, and make `sub_00206E70`/`sub_00208470` return 0 so the CPU always kicks.

### PAT spot

* `sub_0022E050` (`loc_0022E1BF`) sets the try at `rec+0x48 = ±MEMF(0x50A53C)` (4389.12, the 2).
* `sub_0022E4D0` copies it to the line of scrimmage and the ball, and sets phase 3.
* Kick and two-point try share this spot, so seasons up to 2014 are correct as shipped.
* **2015+ (kick from the 15, two-point try from the 2).** The spot is fixed when the play is picked,
  so the mod tools needed five hooks:
  * the record at `0x22E1E1`;
  * formation re-spot hooks at `0xA328A`, `0xA333C` and `0xA24E7`;
  * the line-up entry `sub_001CEAC0`.

### Overtime

* **Sudden death (V).** `sub_000A11F0` ends the game after any play in period ≥ 5 with the scores
  unequal.
* **OT length (V).** OT is as long as a quarter: `sub_000B6DC0` loads `[0xE602B0]` at every period
  start. For 1974-2016 this is right in proportion: a 15-minute OT with 15-minute quarters.
* **Ties (V), in `sub_000B8A60`:**
  * period 4 tied: `sub_000B8A00(4)` starts OT;
  * period ≥ 5 tied and `[0xE5FF80]==7` and period 5: a tie, `(5)`;
  * otherwise another OT, `(7)`.
  * So Eras' mode 4 never ties (period 33 was seen in M6-depth). That is right for playoffs and
    wrong for 1974+ regular seasons.
* **Patch for ties.** Make the `cmp [0xE5FF80],7` at `loc_000B8ADF` also accept
  `g_eras_rules.regular_tie`. The mod tools' `regular_tie` hook sits at the same site.

### Play clock

* `sub_00055DD0` (match init) sets `[0xE602AC] = 40.0` (`0x42200000`). `sub_000B6DC0` and
  `sub_000B6E30` load it into `[[0xE60294]+0x10]`.
* After a timeout, `sub_000B6DE0` sets 25.0 (`0x41C80000`). It is called from `sub_000B8810` and
  `sub_000B90A0`.

### Quarter length

* `[0xE6000C]` holds minutes, 1-15 in Game Options (`0x149170`).
* Match init `sub_00055DD0` sets `[0xE602B0] = min × 60` (`0x4E6D14`).
* `NFL2K5_QUARTER_SECONDS` overrides it. M4 T4.3 showed the snapshot and restore work.

### Penalty settings

* **Toggles (0/1):**
  * `E60058` offsides
  * `E6005C` encroachment
  * `E60060` false start
  * `E60064` chop block (inert: `sub_000B1440` never reads it; the clipping slider governs chop blocks)
  * `E60068` intentional grounding
  * `E6006C` illegal forward pass
  * `E60070` fair-catch interference
  * `E60074` ineligible receiver
  * `E60078` illegal kick
  * `E6007C` illegal onside kick
  * `E60080` delay of game
  * `E6008C` all penalties
* **Float sliders (default 0.5):**
  * `E60090` face mask
  * `E60094` neutral zone
  * `E60098` DPI
  * `E6009C` OPI
  * `E600A0` offensive holding
  * `E600A4` defensive holding
  * `E600A8` late hit (no menu row)
  * `E600AC` clipping
  * `E600B0` roughing the passer
  * `E600B4` roughing the kicker
  * `E600B8` ineligible downfield
  * `E600BC` crowd noise
* **Defaults:** `sub_000E2C70`.
* **Reader:** `sub_000B1440`. Every penalty is off when the mode is below 4.
* **Yardage:** in `.data` at `0xA89950 + idx×0x70`, +0x40 (float cm), so it is not part of the
  snapshot block.

### Two-minute warning

* **Firing condition (V).** In `sub_00157D40` the warning fires when all of these hold:
  * `[0xE6000C] > 2`;
  * the quarter is longer than 120 s;
  * period 2 or period ≥ 4;
  * clock ≤ 120.0 (`0x4E6D58`);
  * bit `0x20` of `[0xE602FC]` is clear.
* **Effect (S).** `sub_000A0190` calls `sub_001588B0`, which sets bits `0x60`, stops the clock and
  sets presentation state `0x18`.
* **Out-of-bounds clock rule (V).** `sub_000B7110` keeps the clock stopped after out-of-bounds
  plays under 120 s in Q2 and under 300 s in Q4. Elsewhere it restarts on the ready signal.

### Touchback spot

* `sub_000B6340` `loc_000B639D` uses `[ctx+0x17C] × MEMF(0x4F0F98)` (2743.2, the 20) for every kind
  of kick.
* The second `0x4F0F98` use at `loc_000B6454` is a different calculation. Leave it.

### Roster limits

* The team record holds 65 pointers; the count is at `+0x11C`.
* The game requires at least 42 players: `sub_0016D6F0` and `sub_002BF990`.
* The franchise maximum is 54: `sub_0036ECE0` and `sub_0036F830`.
* Eras M8 stages the real 1980 45-man rosters with no fillers.

## What each era needs (beyond retail)

| Seasons | Patches needed (b) | Settings (a) | Not feasible / approximated |
|---|---|---|---|
| 1966-1969 AFL | kickoff 40, no regular-season OT (tie at end of Q4) | penalty preset | goal-line posts, wide hashes, contact rules |
| 1966-1973 NFL | kickoff 40, one-point tries, no regular-season OT | penalty preset | goal-line posts, wide hashes (to 1971), contact rules |
| 1974-1977 | kickoff 35, one-point tries, regular-season tie after one OT | penalty preset (pre-Blount contact: lower the DPI and defensive-holding sliders) | contact rules |
| **1978-1993 (incl. 1980)** | **kickoff 35, one-point tries + CPU two-point chart off, regular-season tie after one OT**; later the play clock (30 s to 1987, 45/30, 45/25) | quarter length, penalties | none important |
| 1994-2010 | none (retail) | none | none |
| 2011 | kickoff 35, regular-season tie after one OT | none | none |
| 2012-2016 | + modified OT; 2015+ PAT kick from the 15; 2016 kickoff touchback 25 | none | none |
| 2017-2023 | + 10-minute OT (2/3 of a quarter) | none | none |
| 2024-2025 | + dynamic kickoff, 30/35 touchback, 2025 both teams possess in OT | none | dynamic kickoff is high risk |

## Recommended first set (for the playable 1980 season and every 1974-1993 season)

1. **Eras rules global.**
   * A host struct `g_eras_rules`, filled from `rules_1966_2025.json` for the season at Eras
     launch, and zeroed at `ERAS_MATCH_EXIT` and on abandon.
   * Every patch is a no-op while it is zero.
   * Log the active profile with the existing `[ERAS]` lines.
2. **One-point tries.** Two small patches, low risk:
   * `sub_000B8480`: +2 becomes +1;
   * the CPU two-point decision returns 0.
   * Also remap the "2pt" stat and event in result capture.
3. **Kickoff from the 35.** Seven use-site scalings in `sub_001584A0`, `sub_00158620`,
   `sub_001587F0`, `sub_0022E050` (x2), `sub_0022E250` and `sub_0022BC70`, with low-medium risk.
   * A test should record touchback rate and return yardage over CPU-vs-CPU games, compared with
     retail.
4. **Regular-season ties after one OT.**
   * Patch the mode-7 test at `loc_000B8ADF`. Playoff games keep mode-4 behaviour (unlimited OT,
     correct for every era).
   * Result capture must accept `tie: true` (already a field in the box JSON). Standings already
     use W-L-T.
5. **Per-era settings presets (a).**
   * Quarter length stays the player's choice.
   * Optional penalty-slider presets per era, applied through the existing snapshot and restore.
6. **Later.** Play-clock values, after the pre-1988 history is checked in a rulebook, and the
   out-of-bounds clock rule. Then the post-2010 set: 10-minute OT, modified OT, PAT from the 15,
   touchback 25. The dynamic kickoff goes last.

**Leave out:** goal posts on the goal line, hash-mark width, the receiver-contact, hands and
in-the-grasp rules (beyond slider approximation), and replay. Schedule length, the strike seasons,
the 1987 replacement weeks, the playoff field and roster limits belong in the Eras league and data
layer, not the engine.

**Tests for the first set** (when building is allowed): CPU vs CPU 1980 games with 2-minute
quarters.
* Confirm a try play scores 1.
* Confirm the CPU never goes for two.
* Confirm the kickoff spot reads the 35 on the HTML scorebug field position.
* Force a 0-0 game with 10 s quarters and confirm it ends tied after one OT period, with the box
  JSON showing `tie: true`.
* Run a retail (non-Eras) game afterwards and confirm the kickoff is from the 30 again and
  two-point tries score 2.

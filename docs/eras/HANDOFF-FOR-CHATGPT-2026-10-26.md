# My NFL Eras — handoff (for ChatGPT or another assistant)

Written 2026-10-26, project `E:\NFL2K5-PC` (static recompilation of the Xbox game
ESPN NFL 2K5 into a native Windows program). This hands off **My NFL Eras**, a
MyNBA-Eras-style historical franchise mode built inside the game's own menus.

**Read this first, then the tail of `PROJECT_STATUS.md` (long — read the last few
entries), then `docs/eras/PHASE2-PLAN.md` (the living plan) and the `docs/eras/results/M*.md`
milestone reports in order. Don't start over; inspect first. Don't silently switch to
other tasks (Sandbox mode, the ESPN presentation extraction, the SDL3 renderer, installers).**

---

## 1. What My NFL Eras is

A historical franchise mode. You pick a start era (target 1966–2025), play real
seasons with real rosters, and history evolves (expansion, relocation, rule changes)
under three modes: Historical Automatic, Commissioner Approval, Alternate History.
It runs **inside NFL 2K5's own native menus and a 3D "office"** (a copy of Franchise's
Coach's Desk, never Franchise itself), with a distinct office look per decade. It must
stay isolated from the retail game, from Franchise saves, and from the SOFTDRINK mod.

Engine approach: Eras can't use retail Franchise (fixed 32 teams, 2004 rules, writes
the Franchise save). Instead a PC-side league database drives games by staging
historical teams into the two spare team slots **USER1/USER2** and playing in **mode 4**
(so the retail franchise-commit never runs). A result-capture hook reads the final score
and box stats back out.

---

## 2. Rules (follow exactly)

- **Never invent historical data.** Rosters, schedules, coaches, stadiums, rules, draft
  classes all come from cited sources (Pro-Football-Reference via the Wayback Machine,
  Wikipedia, the NFL Record & Fact Book, SportsLogos.net). Where a fact isn't available,
  record it as missing/estimated and flag it — don't fabricate. Estimated values (e.g.
  low ratings for never-rated draft picks) must be clearly flagged as estimated.
- **Test in game, background mode only.** Every claim of "works" needs an in-game test:
  `RECOMP_BACKGROUND=1` (off-screen, never takes focus), `RECOMP_AUDIO_MUTE=1`,
  `NFL2K5_NO_HOST_PAD=always`, press-file input, `NFL2K5_PRIVATE_PACKS=1`, a renamed exe
  copy (e.g. `build\Release\NFL2K5_eras.exe`), **never kill `NFL2K5.exe`** (the user may
  be playing). The user mistook a visible test window for their game once — stay off-screen.
- **Saves live in `<root>\saves`, NOT `%LOCALAPPDATA%`.** Any test that could save must use a
  **sandbox `NFL2K5_ROOT`** (junctions to original/data/mods/assets/cache/config/dependencies/
  external/font/Music/tools + a **sparse** copy of `saves\`; remove junctions ONLY with
  `cmd /c rmdir`, never a recursive delete) and a sandbox `-AppData`. Confirm the real
  `saves\UDATA` is unchanged after. Copy `saves\` sparsely — the `Partition*.img` files are
  sparse and a plain copy balloons to ~5 GB each and has filled drive E: repeatedly.
- **Never touch retail, Franchise, or SOFTDRINK behavior** outside Eras. Never modify
  `original\disc\*` files (a parked experiment did and broke base-game face masks — restored
  2026-10-21). Changes go in packs/overlays.
- **Native menus, the user's own photos.** Build into the game's menus (like
  `src/nfl2k5_video_menu.c`), not popup windows. Use the user's supplied photos for art
  (IBM PC, Bengals logo, era-select voice clips), make textures by default; don't draw
  crude art. Controller-first (Xbox + DualSense/DS4), mouse optional.
- **Eras is a SEPARATE downloadable pack and is NOT in the public release yet** (it's
  unfinished). The 2026-10-23 release `v0.7.0-sandbox-preview` deliberately excludes Eras.
- **Disk hygiene:** keep E: > 15 GB free; delete raw BMP frame dumps after converting the
  few keepers to PNG. Build lock: create `build\.eras-build.lock` (name + time) before
  `tools\build.ps1` or a launch; wait while another live lock exists (stale after 45 min).
- **Gen patches** go in `tools/apply-gen-patches.py` (extern C calls only; generated files
  have no stdio). If you edit an already-applied patch, also fix its applied text in
  `src/recomp/gen`. Never hand-edit generated code otherwise.

---

## 3. Where things are / git state (important)

- Eras code: `src/nfl2k5_eras*.c/.h` (eras, launch, capture, menu, office, draftshow,
  depth, sheets, draft, draft_sheets, draft_room, playoffs, awards, rollover, module),
  plus Eras parts in `nfl2k5_teams.c`, `nfl2k5_team_files.c`, `nfl2k5_presentation.cpp`,
  `nfl2k5_plugin_host.c`, `main.c`, and gen patches.
- Data: `data/eras/` (runtime/1980, runtime/1981, league/*, community_rosters, drafts).
- Pack/assets: `mods/eras/` (offices, stadiums, teams art, presentations.ini, draft audio),
  and the installed pack `mods/packs/MyNFLEras/` (gitignored — disc-derived assets).
- Tools: `tools/eras/` (make_pack.py, build_assets.py, make_stadium_history.py,
  roster_membership.py, league_starters.py, make_runtime_package.py, league_schedule.py,
  head_coaches.py, make_progression.py, drafts/, stadiums/, season/, draftshow/, office).
- Docs: `docs/eras/` (PHASE1-ARCHITECTURE, PHASE2-PLAN, PHASE2-RESULTS, ERAS-OFFICE-DESIGN,
  MOD-PACK-DESIGN, HISTORIC-TEAMS-INVENTORY, research/*, results/M0..M16).

**Git divergence — do NOT merge or force-push without the owner.** This PC's `master`
(Eras + Sandbox + audio/fixes line) has diverged from GitHub's `master` (an SDL3 GPU
renderer / Linux line): ~50 ahead / ~53 behind. This PC's work is pushed to the GitHub
branch **`claude/eras-sandbox-audio-20261023`**. The owner's plan: this is the last D3D11
release before merging the two lines. Also note the working tree has ~300 uncommitted files
mixing Eras with the owner's other in-progress work — commit **only your own Eras hunks**
via a temporary index; never commit the owner's unrelated changes. Attribution line the
owner uses on commits: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` (or the
model you are).

---

## 4. What works now (the playable slice, all tested in game)

**The 1980 season plays end to end** from the office: Main Menu → **Game Modes → My NFL Eras**
→ New/Continue League → the 3D office → Play Next Game → the game → return to the office with
standings updated. Confirmed: back-to-back games, Continue after restart, quit/abandon,
user-controlled team, overtime, and the whole season through playoffs → Super Bowl XV →
awards → the 1981 draft → rollover into a 1981 season.

Feature-pack architecture (M10): Eras is gated behind a valid `mods\packs\MyNFLEras\pack.json`
(no pack → no menu row), reaches the game only through a hook table, has `--vanilla`/features.ini
switches, a `make_pack.py` packer and a first-run asset builder (NFL2K5-ErasBuilder). The Eras
code is compiled in but dormant without the pack.

---

## 5. Milestones done (see `docs/eras/results/M*.md`)

- **M0** — 1980 data (28 teams, 53-man rosters, 224-game schedule), cited, tamper-tested.
- **M1** — result capture (normal/OT/quit/rematch) matches GameCast.
- **M2** — staging 1980 teams into USER1/USER2, 0 name mismatches, slots released after.
- **M3 / M3-office** — the 3D office (six decade looks: 1970s–2020s; the 1980s has the user's
  IBM PC 5150), replacing the plain League screen; launch/return/clean-up moved into it.
- **M4 / M5** — launch, commit, return, standings; mode 4; abandon path.
- **M6** — depth chart (the game's own screen), 1980 starters fixed, real coaches; and a full
  roster rebuild (real 1980 membership; ratings from community saves; flagged gaps).
- **M7** — Schedule + Standings as copies of Franchise's own screens fed Eras data (Schedule is
  a true copy; Standings is rebuilt on the same sheet engine since 1980 has 3 divisions, not 4).
- **M8** — real-size rosters (42–45, game's own 42 minimum), no fillers.
- **M9** — era-correct team looks: per-team uniform `style`, 1980 logos extracted from the
  disc, scorebug identity per Eras team, card-badge swaps; Dolphins → Orange Bowl.
- **M10** — feature-pack migration (pack gate, hook table, make_pack, asset builder).
- **M11 / M12** — the draft show (announcement on the Draft-room stage, the user's chime +
  "with the Nth pick" + team clips, pick card, suited player walk-up + handshake + jersey
  hold, music bed, coach heads, suits/ties, caption timing, office Options row).
- **M13** — the real 1981 draft (class with real/estimated ratings + 5 sleepers; Historical
  real order / Alternate-History standings order; Big Board / candidates / recap; CPU + user
  picks; rookies written to the league's 1981 rosters).
- **M14** — season end (playoffs in 1980 format, Super Bowl XV/XVI, awards from league stats,
  draft hand-off, rollover into a real 1981 season with its schedule/coaches/looks/rosters +
  Alternate-History progression).
- **M16** — menus fix: the "disc is dirty or damaged" error (a full-disk-truncated boot archive)
  fixed with safe writes + a clear "disk full" message; My NFL Eras moved from the Main Menu to
  **Game Modes**; an **Exit** row added to the Main Menu.

Also: stadium history 1966–2025 (`data/eras/league/stadiums_1966_2025.json`), 16 NCAA 07
stadiums imported as Eras stadium packs (Orange Bowl, Cotton Bowl, LA Coliseum, etc.),
era rules research (`data/eras/league/rules_1966_2025.json` + ERA-RULES.md: what 2K5 can
switch per era), draft-class research (DRAFT-CLASSES.md), the Crib-phone-call copy research
(CRIB-PHONE-CALLS.md), and the ESPN draft-show feasibility (DRAFT-SHOW-FEASIBILITY.md).

---

## 6. Not done / open (the roadmap)

- **M15 — retail-look draft screens (NOT done).** M13's Big Board/candidates/recap are
  sheet-engine lists; the plan is to make them look like Franchise's player-card screens.
  An agent started this on 2026-10-17 but was interrupted and never finished.
- **Era-select screen** (2K5-styled decade cards 1970s–2020s + a start-year picker + per-decade
  voice narration that plays once per visit). Mockup exists at `build/eras/eraselect/`; the
  user supplied `mods\eras\eraselect\voice\1980s.mp3`. Not built in game.
- **League Setup screen** (era presets, History Mode, roster/rules options, team select, review).
  Only 1980 has data; other years show "coming soon".
- **More seasons (1982+).** Each needs real rosters/schedule/coaches/looks via the existing
  pipeline. 1980 and 1981 exist. Season-keyed tables: stadiums (full), rules (full), looks
  (1980/1981 + partial), events 1970–2025.
- **Front office screens:** Roster, Trades, Contracts with era rules (no FA before 1989, Plan B
  1989–92, FA 1993, cap 1994), Free Agents, League Leaders, News. The owner's stated order:
  Depth Chart (done) → Roster → Trades → season end (done) → Draft (done) → Contracts.
  Screens that can copy Franchise should; those that can't (trades/FA/contracts/leaders) must
  be built on retail UI parts and look like Franchise ("make it look real good, don't slack").
- **CPU trade phone calls** copied from The Crib's celebrity-call alert (icon + ring + call
  screen → Accept/Decline/Counter). Research done in CRIB-PHONE-CALLS.md; also fixes the stray
  phone icon beside the Eras row. Build after the Trades screen.
- **Senior Bowl** (North vs South prospects staged into the spare slots, at Ladd-Peebles =
  NCAA 07 "GMAC Bowl"; already imported) and the **full draft-day show** extensions
  (chest-height jersey hold, Draft Show options into League Setup, 1980 coach portraits,
  the four missing team clips: Colts/Oilers/Cardinals/Chargers, and re-record number_27/29).
- **Stadium polish:** sponsor banners in the imported stadiums still show 2004 sponsors; end
  zones/field lettering cleanup on the Coliseum etc.

---

## 7. Build and test

```
tools\build.ps1 -Game          # build build\Release\NFL2K5.exe (close the game first)
build\Release\NFL2K5.exe       # play (no args, or --retail)
```
Launch Eras: Main Menu → Game Modes → My NFL Eras → Continue/New League. Needs the installed
pack `mods\packs\MyNFLEras\` (rebuild it with `python tools/eras/make_pack.py --install`).
Runtime package for a season: `tools/eras/make_runtime_package.py` → `data/eras/runtime/<year>/`.
Background test harnesses already exist: `tools/eras-capture-test.ps1`, `tools/eras/m16_test.ps1`,
`tools/eras/season/season_test.ps1`, `tools/bg-drive.ps1`, and the `nav_*.sh` menu drivers.

Python is `python` (not python3). VS 2022 toolchain. xemu + gdb are the reference oracle.

---

## 8. Known defects / caveats

- **Halftime hang:** 1 freeze in ~12 Eras halftimes once (2026-10-16), not reproduced in 11
  later halftimes; possibly another process holding the game's cache files — unproven.
- **Coach Match Up portraits** still show 2004 coaches' faces (the 1980 names are correct;
  records show 0-0 because career numbers were left blank rather than guessed).
- **Roster ratings** come from community saves (unverified fan rosters), labelled in game; 117
  real 1980 players had no ratings source and were left off; some positions use estimates.
- **Draft show:** chest-hold wrists, one suit style per class default, 4 team clips + 2 number
  clips missing (owner to supply). The player wears the commissioner model's face (head swap).
- **Pack independence:** the Sandbox mode (separate project) must not depend on Eras' draft
  pose file — that was being addressed; keep Eras assets self-contained under `mods/eras`.

---

## 9. Recommended next work (owner's priorities)

1. **M15 — make the draft screens look like Franchise's** (Big Board/candidates/recap with the
   real player cards), picking up the interrupted 2026-10-17 attempt.
2. **Era-select screen** in game (decade cards + start-year + the 1980s voice clip), then
   **League Setup**.
3. **Front office: Roster → Trades → Contracts (era rules)**, then **CPU trade phone calls**.
4. **Fill seasons outward from 1980/1981** (1982, 1979, …) with the existing data pipeline.
5. **Senior Bowl + draft-show polish**, stadium sponsor/field cleanup.
6. Keep everything a **separate pack**, excluded from the public release until the owner says
   it's ready, and never merge the git lines without the owner.

Do not build anything that ships Eras in the public release, invent historical data, touch
retail/Franchise/SOFTDRINK/saves, or claim a feature works without an in-game background test.

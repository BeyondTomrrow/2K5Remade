# M11 results: the My NFL Eras draft show (1981 NFL Draft, round 1)

2026-10-16/17. Built on the route of `docs/eras/research/DRAFT-SHOW-FEASIBILITY.md`. Every in-game
test ran `NFL2K5.exe --retail` in background mode only (`RECOMP_BACKGROUND=1` off-screen,
`RECOMP_AUDIO_MUTE=1`, `NFL2K5_NO_HOST_PAD=always`, press file, `NFL2K5_PRIVATE_PACKS=1`, its own
exe copy `NFL2K5_draftshow.exe`, sandbox `NFL2K5_ROOT` = `logs\draftshow\sbx-root` (junctions plus a
copied `saves\`) and sandbox user folder `logs\draftshow\home`). The real `saves\UDATA` was checked
unchanged (same SHA-1 of its 507 files before and after).

**Status: stages 1, 2 and 3 work in game; stage 4 works through environment switches.** The whole
pick plays: stage, commissioner at the podium, on-the-clock graphic and board, the chime and the
call (number clip, team clip) with captions synced to the audio, the pick card, the suited player's
walk-up, the handshake, the jersey lifted overhead, then held at chest height with a camera push-in.

## Media

| What | Path |
|---|---|
| Labelled sheet (one pick, 10 beats) | `logs/draftshow/draftshow-sheet.png` |
| One full pick, video with the game's audio | `logs/draftshow/pick1.mp4` (30 s), `logs/draftshow/pick1.gif` (no sound) |
| One pick's audio (recorded game mix) + waveform/spectrogram | `logs/draftshow/pick-audio.wav`, `logs/draftshow/pick-audio.png` |
| Full first round (28 picks) | `logs/draftshow/round1-1981.mp4`, `logs/draftshow/round1-sheet.png` |
| Jersey textures made from the kits | `logs/draftshow/jersey_pick1.png` (Saints), `jersey_pick2.png` (Giants) |
| Offline camera preview | `logs/draftshow/offline_shots.png` |
| Single in-game frames | `logs/draftshow/ds6-f*.png`, `ds2-mp4/`, `ds6-mp4/` |

## What was built

### Pack builder `tools/eras/draftshow/build-draftshow-pack.py` (player's disc, nothing committed)

* `mods/eras/office/files/erasdraft.iff` (served from pack G by the office's existing extra-pack
  hook, no new hook): SCNE `draft_menu` (byte copy, fr.iff chunk 5), SCNE `cutscene` (the trophy
  set, outer 2725 chunk 0, with its 4-vertex STAGE platform reshaped into a unit quad facing +z
  with 0..1 UVs: the jersey prop), SCNE `commish01` (byte copy), SCNE `draftply1` (commish01
  renamed, palette-only edits: black suit and trousers, dark hair; glasses hidden at run time).
* `mods/eras/draft/files/draftposes.bin`: 7 retail clips sampled with the Mod Tools sampler (player
  channel map, twist joints completed) at their 15 Hz frames: `anthemstandplayer` (idle),
  `cointossloopplayer01`, `fpcoinrunhomeplayer01` (walk), `fphandshakeplayer`,
  `cointossshakeplayer01` (handshake), `ex021player02` (helmet raise), `ex023player01` (trophy).
* `mods/eras/draft/files/draftjerseys.bin`: one jersey per first-round pick: the drafting team's
  1980 home kit (`<code>h<style>.iff`, style from `teams.csv`) torso back half with the pick number
  in the kit's own jersey digits, quantised to the platform texture's P8 128x128 format.
* `mods/eras/draft/files/audio/*.wav`: PCM copies of the user's clips (fallback only; see Audio).
* `tools/eras/draftshow/make_draft_picks.py` -> `mods/eras/draft/picks_1981.csv` (committed facts:
  round, pick, Eras team id, PFR code, player, position, college; 332 picks).

### Runtime `src/nfl2k5_eras_draftshow.c/.h`

* Loads erasdraft.iff under its own context `ERASDRAFT` (Franchise's FRANCHISE1, fr.iff and globals
  are never touched), hides the war-room desk, monitors, PDA, shadows and static figures, keeps only
  the podium from the trophy set (scaled 6x to the room, placed stage left).
* **Characters are drawn by the game itself** (stage 1's spike succeeded): a skinned node's `+0x14`
  is its array of 25 current joint matrices (identity in the file). Writing
  `current[i] = local[i] * current[parent]` (row-vector, `local = M(q) + transform[+0x50]`, palette
  `T(-transform[+0x40]) * current[i]` applied by `sub_00022C00`) before `sub_0002DE60` makes the
  game's skinned renderer draw the posed commissioner with its own shading. No PC-side renderer was
  needed. Poses: nlerp between baked frames, 0.3 s crossfades, root height relative to each clip's
  first frame (the trophy clip stands on a 75 cm stage).
* Code-driven camera: our own camera record (eye / target / focal per shot, smoothstep blends) ->
  `sub_0013E710`; shots: wide, board, podium (straight on), two-shot, walk, hold, push-in.
* The chest-height hold is a pose authored on the 25-joint skeleton (`chest_hold`): two-bone IK on
  humerus/elbow (wrists and hands carried) bringing both hands 30 cm in front of the chest, 42 cm
  apart, elbows down and out, blended in over 0.45 s on top of the idle clip.
* The jersey: the reshaped STAGE quad, placed each frame between the player's hand joints (facing
  his way, 52-70 cm wide, centred on the hands overhead, hung from them at chest height), its
  texture overwritten in guest memory with the pick's jersey (pick 1 Saints "1", pick 2 Giants "2" ...).
* Sequence per pick: on the clock (3 s, fast clock; any button ends it) -> chime -> the number clip
  3.4 s into the chime (its tail rings under the voice) -> 0.25 s -> team clip -> card (when the
  team clip ends) -> walk-up -> handshake -> overhead lift -> chest-height hold with push-in -> next.
* Menu: a new screen `EM_DRAFT` in `src/nfl2k5_eras_menu.c` (one hidden row, a layout name that is
  not loaded so no list is drawn, flags 0x10: B pops), opened from a new office row
  **"Draft Show (preview)"** (greyed when the pack files are missing).

### Overlay `mods/eras/draft/show/` (HTML/CSS/JS) and the front-end mode

* `src/nfl2k5_presentation.cpp`: `nfl2k5_presentation_frontend_begin/end` (a page started on demand
  outside a match, its own host, `https://presentation.local/` = the Eras `draft` folder,
  `erateams.local` for the 1980 logos) and `nfl2k5_presentation_post_json` (any event object).
* Events: `DRAFT_SHOW_START`, `DRAFT_BOARD`, `DRAFT_ON_CLOCK`, `DRAFT_PICK_IN`, `DRAFT_ANNOUNCE`,
  `DRAFT_TEAM`, `DRAFT_PICK_CARD`, `DRAFT_HANDSHAKE`, `DRAFT_PHOTO`, `DRAFT_CLEAR`,
  `DRAFT_ROUND_SUMMARY`, each with round, pick, team id, city, name, colours, player, position,
  college. 2K5-ESPN look of the era-select mockup (red/gold slanted bars, Michroma/Saira with
  Bahnschrift fallback): draft bug, on-the-clock lower third with the fast clock, board panel, THE
  PICK IS IN slam, the announcement captions, the pick card with the 1980 logo, photo strip with
  flash bulbs, ticker with the last three picks.

## Audio

* Played by a new **voice channel in the presentation layer** (`nfl2k5_presentation_voice_play /
  _state / _stop / _duck`): 3 channels (chime, number, team), WAV/MP3 decoded by Media Foundation to
  48 kHz, XAudio2 source voices with the whole clip in one buffer, mixed into the game's recordings
  (`RECOMP_AUDIO_WAV` and the video-clip tap) through the capture mix. `RECOMP_AUDIO_MUTE` silences
  the speakers only, so a muted background run still records what would be heard.
* Files: `draft\audio\normalized\<rel>.wav` (the coordinator's loudness-matched copies, -18 LUFS)
  first, then the builder's PCM copy, then the user's MP3. A clip flagged `suspect_short` in
  `normalized\index.json` (number_27, number_29: 0.44 s) counts as missing: chime, then the team.
* Levels: voice 1.0, chime 0.8, the game's audio (menu music) ducked to 0.25 (-12 dB) from the
  chime to the end of the card beat, restored when the walk starts.
* Missing 1980 team clips (Colts, Oilers, Cardinals, Chargers): chime + number, the "...SELECT"
  caption held 0.9 s, then the card (pick 5 Cardinals verified in ds5).
* Evidence (ds6, `tools/eras/draftshow/audio_check.py` on the recorded game mix): chime found by
  cross-correlation at 94.55 s; number_01 at +3.42 s (97.97-99.19), 0.28 s gap, Saints clip
  99.47-101.61; peak -4.6 dBFS, 0 clipped samples; menu music -33.4 dBFS before, -44.9 dBFS under
  the card beat (-11.5 dB), -32.7 dBFS after. `silencedetect=-35dB:0.3` on the cut: no gap between
  the chime start and the end of the team clip; the only "silences" are the ducked music in the
  card beat (101.98-102.38, 103.08-103.46). Captions: `DRAFT_ANNOUNCE` is posted when the number
  clip's voice starts, `DRAFT_TEAM` when the team clip's starts, the card when it ends.
* Not ours: the retail menu-music track ends and restarts on its own (a ~2 s fade out/in, seen in
  ds4 at 134.95-136.80 s); it can fall inside a pick.

## Options and switches

| Switch | Effect |
|---|---|
| `NFL2K5_ERAS_DRAFTSHOW=off / first / mine` | Draft Show mode (default First Round; Your Picks puts the others straight onto the board) |
| A / X / Y | on the clock: end it now; otherwise skip the rest of this pick |
| START | on to the user's next pick, or the round's end |
| B | leave the show (screen pop) |
| `NFL2K5_ERAS_DRAFTSHOW_SKIP=mine` | start at the user's first pick ("Skip to my pick") |
| `NFL2K5_ERAS_DRAFTSHOW_PICKS=N`, `_START=k` | tests: only N picks / start at pick k |
| `NFL2K5_ERAS_DRAFTSHOW_TEST=1` | tests: "Draft Show (test)" first in the Main Menu (one A press from the menu) |
| `NFL2K5_ERAS_DRAFTSHOW_REC=s` | record s seconds of the presented frames + audio (with `NFL2K5_REC`) |
| `NFL2K5_ERAS_DRAFTSHOW_NOACTORS=1` | draw the stage only |

Wiring these into League Setup is left for that screen.

## Tests

| Run | What | Result |
|---|---|---|
| ds1 | first in-game run, 2 picks | stage, podium, commissioner, player, jersey, overlay all draw; player floated (trophy clip's 75 cm stage), chime failed under MCI |
| ds2 | 2 picks, 16:9 window | full pick sequence correct (`ds2-strip.png`) |
| ds3 | picks 4-6, skip | Cardinals (no team clip) card-only path; A skipped pick 6 and stopped its audio |
| ds4-ds6 | audio channel, timing, recording | see Audio; `pick-audio.*` from ds6 |
| ds7 | the full 1981 first round | see `round1-1981.mp4` (results below) |

## Blocked / not done

* Head swap: the suited player still has the commissioner's head (hair darkened, glasses hidden).
  The coach-head library (`coachHeadGrp1`) and the player's own `hi_head` need their attachment
  traced; skin tone per player is not in the data.
* Per-team ties / suits: one player suit for all picks (a scene copy per variant would be needed).
* The hold's wrists are the IK's carried orientation (hands show a little above the jersey top).
* A direct boot switch (`NFL2K5_ERAS_DRAFTSHOW=auto`) was not added: there is no safe game-thread
  hook before the Main Menu without a gen patch (the FSM pop hook runs inside generated code). The
  test row plus `tools/eras/draftshow/nav_to_draftshow.sh` gets from the title to the show in about
  40 s (the title's attract demo eats the first START presses).
* Stage 4's League Setup wiring (env switches only for now).
* The front-end HTML mode and the voice channel call the presentation layer directly
  (`extern`), like the office's texture-pack call; they should become host services
  (`NfHostServices`) when the Eras pack split lands.
* Test-tool changes in the vendored `external/xboxrecomp` (gitignored): `RECOMP_BACKGROUND_WINDOW`
  (a 1280x720 off-screen window for sharper captures) and the capture mix applied once per
  submission (the WAV recorder and the video tap each used to consume the mixers' positions, so with
  both on each got every other chunk).

## Files

New: `src/nfl2k5_eras_draftshow.c`, `src/nfl2k5_eras_draftshow.h`,
`tools/eras/draftshow/{build-draftshow-pack.py, make_draft_picks.py, preview_shots.py,
draftshow_test.ps1, nav_to_draftshow.sh, make_sheet.py, audio_check.py}`,
`mods/eras/draft/show/{index.html, show.css, show.js}`, `mods/eras/draft/picks_1981.csv`, this
file. Hooks: `src/nfl2k5_eras_menu.c` (EM_DRAFT, office row, test row), `src/nfl2k5_presentation.cpp`
(front-end mode, voice channel), `config/game.cmake` (one source), `.gitignore`
(`mods/eras/draft/files/`). The installed pack's `generated\` got copies of erasdraft.iff,
draft\files, draft\show, picks_1981.csv and the normalized audio (the pack's builder should take
them from `mods\eras` from now on).

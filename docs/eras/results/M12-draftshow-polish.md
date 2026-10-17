# M12 results: draft show polish (music, heads, suits, hold, captions, Options, missing clips)

2026-10-17. The polish round over `docs/eras/results/M11-draftshow.md`. Every in-game test ran
`NFL2K5.exe --retail` in background mode only (`RECOMP_BACKGROUND=1`, `RECOMP_AUDIO_MUTE=1`,
`NFL2K5_NO_HOST_PAD=always`, press file, `NFL2K5_PRIVATE_PACKS=1`, its own exe copy, sandbox root
`logs\draftshow\sbx-root`, sandbox user folder `logs\draftshow\home`, through
`tools/eras/draftshow/draftshow_test.ps1`; audio recorded from the pre-mute mix with
`RECOMP_AUDIO_WAV`). The real `saves\UDATA` was not touched (0 of its 507 files newer than the
session's start). The build lock `build\.eras-build.lock` was held for both builds.

## Media

| What | Path |
|---|---|
| Before / after sheet, one row per item | `logs/draftshow/polish-sheet.png` |
| One full pick with sound (pick 1, George Rogers; new head, suit, bed) | `logs/draftshow/pick1-v2.mp4` (38 s, 640x360, h264 + aac) |
| Music continuity: 192 s of show, the game mix | `logs/draftshow/m12a-audio.wav` (checked by `music_gap_check.py`, below) |
| Runs | `m12a` (test row, 7 picks, audio + pick-1 video), `m12b` (the real office path: Options row, league.ini, 4 picks); frames `logs/draftshow/m12{a,b}-frames`, logs `m12{a,b}.stderr.log` |
| Menu-music recording the bed was cut from | `logs/draftshow/mus1-audio.wav` (run `mus1`: 7 min idle at the Main Menu) |

## Results per item

### 1. Music never dies mid-pick: a looping bed, the retail music muted

Why it happened: the retail front-end music (`femusic`, the game's own streaming player at
`sub_00280620`) plays one track, ends it, picks the next and reopens the stream; in `ds7` that gap
was 1.9-4.3 s (136.4-138.3, 343.4-345.7, 465.8-470.1, 531.2-535.1 s) and fell inside picks. It is a
stream reopen, not a fade we could hold, so the show now brings its own music:

* `src/nfl2k5_presentation.cpp`: a fourth voice channel (`CH_BED`), `nfl2k5_presentation_voice_play_ex(ch,
  path, volume, loop)` (XAudio2 `XAUDIO2_LOOP_INFINITE` on the one buffer; the recording copy in
  `capture_mix` wraps the same way) and `nfl2k5_presentation_voice_gain(ch, gain)` (the level now,
  speakers and recording).
* `src/nfl2k5_eras_draftshow.c`: `bed_start()` when the screen is shown: `draft\audio\bed.wav` or
  `.mp3` (the user's), else `draft\files\audio\bed.wav`; the game's own music stream is muted with
  `xbox_AudioMuteGameMusic(1)` (the DSP's music mute, menu effects stay) while the show runs, and
  unmuted on cover / destroy. The call ducks the bed to 0.25 over 0.35 s (`duck()` / `bed_tick()`)
  instead of the game gain; without a bed file the old game-gain duck applies and the menu music
  plays as before (`NFL2K5_ERAS_DRAFTSHOW_NOBED=1` forces that).
* `tools/eras/draftshow/make_bed.py <recording.wav> <start> <end>`: cuts the bed from a recording of
  the game's own menu music (run `mus1`: track 0 plays alone from 40 s at the Main Menu), the last
  4 s equal-power crossfaded into the first so the loop point is seamless, normalised to -31 dBFS
  RMS (the menu music under the M11 show measured -33 dBFS), peaks under -1 dBFS; `--check` prints
  the dips over two laps. The track has a 0.25 s rest 16 s in and a quiet passage at 88 s, so the
  bed is 58-127 s of the recording: 65 s, quietest 100 ms -43.8 dBFS, no dip. The file is derived
  from retail audio and stays out of git (`mods/eras/draft/files/` is ignored); the user can drop in
  a `mods\eras\draft\audio\bed.wav` of their own, which wins.
* Verified: `tools/eras/draftshow/music_gap_check.py logs/draftshow/m12a-audio.wav m12a.stderr.log`
  aligns the show's clock to the recording by the first chime (cross-correlation: 147.78 s = clock
  6.53 s) and scans clock 0 to the last event (192.2 s, the 7-pick show): **no run >= 0.4 s under
  -50 dBFS, longest quiet run 0.0 s**, level mean -32.4 dBFS, quietest 100 ms -49.4 dBFS (the bed
  ducked under the call). `polish-sheet.png` row 1 plots ds7 (gaps) against m12a.

### 2. The player is not the commissioner: coach heads

* `tools/eras/draftshow/build-draftshow-pack.py`: 12 sideline head-coach packages (outers 348, 353,
  359, 367, 382 dark; 355, 362, 376 medium; 349, 357, 371, 377 light, picked off a sheet of the 36
  head textures, `build/eras/draftshow/heads/sheet.png`) copied into `erasdraft.iff` as SCNE
  `dhd01..dhd12` (every `coach` name -> `dhdNN`, same length: `dhdNNBodyGrp1 / LodGrp1 / HeadGrp1`),
  and `draft\files\draftheads.csv` (name, outer, median face colour r,g,b, group). erasdraft.iff is
  3.6 MB now (was 0.97); it loaded in 78 ms with no heap complaint.
* `src/nfl2k5_eras_draftshow.c`: at load the body and LOD nodes of each head scene are hidden, the
  head node drawn with no frustum test, the 29-joint table of `coachHeadGrp1` read (joint 0 is the
  thorax, its root; 1 the neck, 2 the head, with the same local offsets as the body's joints 10, 11,
  12; the rest the face). `head_write()` gives joints 0-2 the suited body's thorax / neck / head
  world transforms (kept in the `Actor` by `actor_write`) and the face joints ride rigidly under
  theirs; the game's skinned renderer draws the head where the body's own face would be, after the
  body. The body's `face` and `glasses` materials are hidden (bit 0 of material +8) once heads exist.
  The hands and neck (`hand`, `neckmaterial` palettes) are scaled from their mean skin entry to the
  head's face colour (`pal_skin`), so a dark head gets dark hands.
* Choice per pick: `draft\looks_1981.csv` gives a tone group (`light` / `medium` / `dark`); within
  the group the FNV hash of the player's name picks the head, never the previous pick's
  (`head_choose`). Picks 1-7 of m12a: dhd05, dhd04, dhd01, dhd02, dhd05, dhd10, ... (log
  `pick N look:` lines).
* **Skin tone in the data:** the 2K5 roster record does carry a skin value (Flying Finn's map, bit 0
  at +0x18 bit 7, bits 1-4 at +0x19; tone = value & 7 with variants at +8 / +16: the retail 2004
  roster puts Favre, Manning, Brady at 16, Urlacher and Bruschi at 0, Ray Lewis and McNabb at 3,
  Vick at 4, Moss at 5). `tools/eras/draftshow/make_draft_looks.py` read it for every first-round
  pick from the community rosters (`ros1981`-`ros1983`, the record named by
  `1981_ratings_match.json`) and it is **not set there**: George Rogers, Hugh Green and Dennis Smith
  sit at 0 with Brian Holloway and Keith Van Horne at 1 next to Lawrence Taylor. So the csv's
  `skin_group` is the editor's own table (from public photographs of the 28 players; the community
  value is kept in a `community_skin` column for the record) and is easy to correct by hand.

### 3. Suits and ties

`pal_suit()` rewrites the player's coat and trousers palettes in guest memory from the pristine
copies saved at load (`pal_snapshot`): suit = hash % 3 (charcoal as baked / navy / black), never the
previous pick's; the tie (the coat palette's saturated red entries) takes the drafting team's
primary colour with its shading kept, or the secondary when the primary is white / grey or within
90 (sum of channel differences) of the suit cloth (navy tie on a navy suit: Seahawks pick 4 got the
green). The commissioner's scene is a different SCNE with its own palettes: navy suit, red tie,
untouched. m12b: pick 1 black + Saints gold, 2 charcoal + Giants blue, 3 black + Jets white->grey
secondary, 4 navy + Seahawks green (`polish-sheet.png` row 3).

### 4. Chest hold

`chest_hold()` target 4 cm lower (`- 6.0f` from the thorax / neck midpoint, was `- 2.0f`) and the
jersey hung 7 cm above the hands (`s_jersey_drop` 0.40, was 0.45). Push-in frames of m12a / m12b
show the hands behind the jersey's top edge (`m12a-p134.png`, `m12b-p246.png`).

### 5. Captions and the card

`DRAFT_PICK_CARD` is posted at the team clip's last sample (unchanged); the overlay now hides the
captions at once (`.cut { transition: none }` for one frame) and then slides the card in, so no
frame shows "...SELECT" and the card together. 10 fps strip of `pick1-v2.mp4` 12.9-14.4 s
(`logs/draftshow/polish/m12a-cardin.png`): captions through 13.5, gone at 13.6 with the card
sliding in. College logos: no NCAA 07 logo set is extracted in this checkout (`tools/ncaa07-extractor`
has no logo output); left out.

### 6. Draft Show in the office Options

* `src/nfl2k5_eras.c/.h`: `draft_show = first | mine | off` in `league.ini` (`first` is the default
  and not written), `nfl2k5_eras_draft_show_saved()` / `nfl2k5_eras_set_draft_show_saved()`.
* `src/nfl2k5_eras_draftshow.c/.h`: `nfl2k5_eras_draftshow_mode()` is live: `NFL2K5_ERAS_DRAFTSHOW`
  when set (the value row then says so and refuses changes: `_mode_forced()`), else the league's
  setting, else First Round; `set_mode()` writes league.ini. The show re-reads it at start.
* `src/nfl2k5_eras_menu.c` (hunks left uncommitted, as in M11): Office Options row 1 "Draft Show"
  (Off / First Round / Your Picks), the "Eras" note moved to row 2.
* m12b, the real path (Main Menu -> My NFL Eras -> Continue League -> office -> Office Look):
  `m12b-opt2.png` row shown, Right -> `m12b-opt3.png` "Your Picks" and the sandbox's
  `home\NFL2K5-PC\eras\league1\league.ini` got `draft_show = mine`, Left -> First Round, B, then the
  office's "Draft Show (preview)" row started the show in First Round mode with the bed.

### 7. Missing clips

Each pick records whether its number / team clip was missing; at the round's end (and when the
show is left early) one line: `audio summary: 1 pick(s) had no clip to record: 5 cardinals (team)`
(m12a), or `every pick shown had its number and team clips` (m12b). The 0.9 s caption hold stays.

## Files

New: `tools/eras/draftshow/{make_bed.py, music_gap_check.py, make_draft_looks.py, make_polish_sheet.py}`,
`mods/eras/draft/looks_1981.csv`, this file. Changed: `src/nfl2k5_eras_draftshow.c/.h`,
`src/nfl2k5_presentation.cpp` (voice channel 4, loop, gain), `src/nfl2k5_eras.c/.h` (draft_show),
`tools/eras/draftshow/build-draftshow-pack.py` (heads), `mods/eras/draft/show/{show.js, show.css}`.
Uncommitted: `src/nfl2k5_eras_menu.c` hunks (the Options row). Generated (not in git): erasdraft.iff
with the 12 heads, `draftheads.csv`, `draft\files\audio\bed.wav`; copies refreshed in the installed
pack's `generated\` (office\files, draft\files, draft\show, draft\looks_1981.csv), which the runtime
reads first.

## Left

* The head's neck meets the suit collar with the body's `neckmaterial` still drawn (tinted); it
  reads fine at the push-in, no seam work done.
* The bed is menu music looped every 65 s; a longer or purpose-made bed goes in
  `mods\eras\draft\audio\bed.wav`.
* The coach heads' hair is the coach's; no per-player hair.
* Skin groups are hand-set for 1981 only (`looks_1981.csv`); other drafts fall back to the hash over
  every head until they get a csv.

## Addendum: the walk-up (user feedback, 2026-10-17)

"The player is doing the handshake animation all the way until he gets to the commissioner ...
and he kinda spawns in outta nowhere." Both true, and the cause of the first was in the baked
clips: the pack's `walk` (`fpcoinrunhomeplayer01`, archive 2761/1) decodes to the **same frames as
`shake2`** (`cointossshakeplayer01`, 2531/1: two steps, then the right hand out at 1.6-2.1 s), so
the walk-up played the handshake clip on a loop while the actor translated at 54 cm/s.

* **Walk clip:** `walk2` = `ANM_POSTPLAY_REACT_NEUTRAL_WALK_TO_HUDDLE_TALKING` (archive 3106/101,
  found by sampling every candidate's feet and hands per frame): a steady walk, stride period
  1.13 s, hands by the sides from 3.7 s. The runtime loops 3.72-6.04 s (two strides) at 1x and
  moves the actor at the clip's own root speed over that segment, **123 cm/s** (286 cm of root
  motion in 2.32 s, read from the clip's trajectory lanes), so the feet do not slide.
* **Entrance:** the entrance mark moved from x = 650 to **x = 1750** (stage right, wholly outside
  the walk camera's frame; 1450 still showed a sliver at the cut), and the camera **cuts** to the
  walk shot (`cam_go(SH_WALK, 0)`) instead of blending from the wide, so he walks into frame.
  The walk is 3.5-3.8 s (was 5.7 s).
* **Turn and handshake:** he faces the direction of travel, blending to face the commissioner over
  the last 0.3 s; on arrival both actors start `shake2` **from the same frame (1.45 s**, where the
  clip's root has stopped; contact 1.6-2.1 s), the commissioner turning 65 degrees to him, the
  player placed 110 cm away (was 95) so the hands meet rather than pass through. The handshake
  beat is 1.4 s, then the lift as before. Log line: `pick N: arrived (walk 3.47 s), handshake from
  clip 1.45 s`.
* **Verified:** run `m12c` / `m12d` (background, sandbox, build lock), `pick1-v3.mp4` (38 s with
  sound) and `logs/draftshow/walkup-sheet.png` (10 fps, 15.6-23.6 s): the cut frame has no
  player in it, he enters from the right edge, walks 3.5 s with the arms swinging and no handshake
  motion, turns, and both lean into the handshake together (hands at the same spot), then the lift.
* Files: `src/nfl2k5_eras_draftshow.c` (k_player_from/to, WALK_* / SHAKE_START, PH_CARD /
  PH_WALK / PH_SHAKE), `tools/eras/draftshow/build-draftshow-pack.py` (CLIPS: walk2),
  `tools/eras/draftshow/make_walkup_sheet.py`. Left: the pack's `walk` entry still bakes the
  handshake frames (unused now); whether 2761/1 and 2531/1 share one animation chunk in the
  archive or the sampler resolves both to one is not traced.

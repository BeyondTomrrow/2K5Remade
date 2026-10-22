# Audio report: "save what I just heard" (2026-10-22)

When the game's sound goes wrong -- a deep voice, chipmunk voices, the crowd missing, a crackle or a
gap -- press **F9** on the keyboard, or hold **Back** on the controller and click **R3** (press the
right stick in). The game keeps the last 90 seconds of its sound in memory all the time; the key
writes them to a file together with a second-by-second log of what the audio and the frame rate were
doing. Nothing else happens in the game, and the music does not skip.

A banner in the bottom-left corner says **SAVING AUDIO REPORT** and, a second later,
**AUDIO REPORT SAVED** with the file's name.

## Where the files go

In the game's logs folder:

* installed game: `%LOCALAPPDATA%\NFL2K5-PC\logs\` (paste that into the Explorer address bar);
* portable / development copy: the `logs` folder next to the game.

Each report is three files with the same name, for example `audio-report-20261022-014420`:

| File | What it is |
|---|---|
| `.wav` | The last 90 seconds of sound exactly as it went to the speakers (48 kHz stereo; before the Windows volume and the game's master volume, so it is never silent because of a volume setting). The end of the file is the moment you pressed the key. |
| `.txt` | A short summary: how many times the sound output ran dry, how much sound was lost waiting for the game, the lowest frame rate and the longest frame in those 90 seconds. |
| `.csv` | One line per second (opens in Excel): frame rate and longest frame, how full the speaker queue was, dropouts, and every sound whose pitch moved that second with its range in Hz and its loudness. Voices `044`/`045` during a match are the crowd-reaction layer; `04A`-`04F` the crowd beds; `046`-`049` the commentary. |

Press the key right after you hear the problem (within 90 seconds). Send the three files, and say
roughly how many seconds before the press it happened.

## What it costs

The recording is 17 MB of memory, set aside when the game starts. Each 5.3 ms block of sound is
copied into it once on the sound thread, without waiting on any lock (measured in 10-minute games:
0.5-1.7 us per block including the broadcast-theme mix, i.e. at most 0.3 ms per second of play,
about 5 us per 60 fps frame); the per-second log is a few comparisons per playing sound. The files are
written on a separate low-priority thread, so pressing the key does not stall the sound or the
game. It works in the base game and with mod packs (SOFTDRINK) alike.

## Settings (advanced)

* `RECOMP_AUDIO_NO_REPORT=1` -- turn the recording off (frees the 17 MB).
* `RECOMP_AUDIO_TARGET_MS=<10..120>` -- how far ahead of the speakers the sound is prepared
  (default 56 ms; see docs/AUDIO-FIX-20261021.md, dropouts).
* `RECOMP_AUDIO_LEGACY=1` -- the sound output as it was before 2026-10-22 (for comparison only).

If F9 is bound to a controller button in F1 > Controls (keyboard layout), that binding also fires;
the default layout does not use F9.

## For developers

`xbox_AudioReportRequest()` / `xbox_AudioReportSave()` (external/xboxrecomp/src/apu/apu_xaudio2.c,
diff in docs/AUDIO-REPORT-HOTKEY-20261022-xboxrecomp.diff); the per-second record is built in apu_vp.c
`diag_report()` (game fps from nv2a_gpu_perf.inc.c `g_xbox_perf_frames` / `g_xbox_perf_worst_us`).
F9 is handled in nv2a_gpu_present.inc.c `menu_key`, Back+R3 in src/nfl2k5_input_hle.c
(`audio_report_combo`; scripted runs can press it with the press-file name `back+r3`).

# User audio reports of 2026-10-22 and the A/B against xemu

The player pressed F9 (docs/AUDIO-REPORT-HOTKEY-20261022.md) several times during a SOFTDRINK match
(Cowboys vs 49ers, SOFTDRINK 2K28 v0.6.1, render scale 12, NBC SNF presentation) and said the sound is
"fucked up in general": "deep voice, then Alvin and the chipmunks", "no crowd". The reports are
`%LOCALAPPDATA%\NFL2K5-PC\logs\audio-report-20261022-123020` and `-123110` (89.5 s each). Their
summaries: 0 underruns, 0 dropped buffers, 0 ms ISR silence, 35-60 fps -- so nothing in these 90 s was
a dropout. This document is the analysis of those two captures and the first proper A/B of our APU
against the real game running in xemu. Plots and clips are in `logs\audio3\`.

## Verdicts per lead

| Lead | Verdict |
|---|---|
| 1. voice 050 at 16000..38055 Hz in one second | Voices 050-06D are the game's 2D one-shot pool (ADPCM mono, `fmt A200E0E6`: whistles, hits, PA, stings), each sound with its own rate (16000 / 18000 / 22050 Hz ...; the pool's most common start pitch is -6492 = 16 kHz). The CSV's per-second range is per voice *number*, not per sound, so "16000..38055" is two sounds on the same voice in one second (gain changes 0.27 -> 0.35 with it). 38055 Hz (pitch -1372) never occurs in a retail run (`logs\audio3\r1`: the pool's rates stay in 5805..22559 Hz); the player's captures are SOFTDRINK, whose pack replaces sound files -- it is the pack's own sound, not a resampler or header error. See "Lead 1" below. |
| 2. 3D voices swinging x1.3-x3.4 per second | Verified, not a defect: in our retail run `r1` (12180 3D voice-seconds) all 266 swings > 1.3x have a `VOICE ON` of that voice in the same second (a new sound on a reused voice); 96 % of 3D voice-seconds move < 2 % (doppler). `tools\audio\swing_vs_on.py`. |
| 3. steady ~390-550 Hz tone over 90 s | The dominant low tone is not one stuck frequency: the strongest 200-1500 Hz peak wanders (252, 345, 404, 1260 Hz ...), and 252 Hz with its harmonics (1259.8 = 5 x 252) is the most frequent (`logs\audio3\user-123110-tone.png`). 252 Hz is also present in our retail capture `fB` (14 of 722 s). What it is on the console: see the A/B. |
| 4. no crowd | In the player's captures the crowd-reaction layer (044/045) is on in 68-74 of 90 s at gain 0.16-0.18 mean, 0.32 max (retail `r1`: 0.12 mean, 0.33 max), with the game's own 19642..24751 Hz swell. The crowd beds (04A-04F) hold a constant pitch, so the CSV never lists them (it lists voices whose rate moved) -- their level comes from the runs: retail `r1` beds mean gain 0.28, never silent in 308 match seconds; SOFTDRINK lets its own 22 kHz bed decay to silence between reactions (docs/AUDIO-FIX-20261021.md). xemu's voice table in a match has the same bed set at the same gains (04A/04B 12 kHz 0.10, 04C/04D 22 kHz 0.02, 04E/04F 16 kHz 0.00; 044/045 at 0.07). |

(The A/B section below is the answer to "fucked up in general".)

## Method: the real game as the oracle, hands-off and silent for the player

xemu 0.8.136 (`E:\xemu-0.8.136-windows-x86_64\xemu.exe`) with the retail ISO
(`E:\NFL2K5-PC\original\NFL2K5.xiso.iso`) and the player's HDD image, driven and recorded without
focus, without the speakers, and without the player's own `xemu.toml`:

* `tools\audio\xemu_launch.ps1`: writes a private config (`%APPDATA%\xemu\xemu\xemu-audio3.toml`:
  keyboard on port 1, `volume_limit = 1.0`) fresh for every launch -- xemu rewrites its config on exit
  and had turned `volume_limit` into 0 (silent APU) and bound the player's pad to port 2 -- starts
  xemu with the QEMU monitor on 4444 and gdb on 1234, restores the window without activating it
  and parks it off-screen (`ShowWindow(SW_SHOWNOACTIVATE)` + `SetWindowPos(-4000,-4000)`; minimized
  xemu froze), and sets xemu's Windows audio session to volume 0.001 (`tools\audio\app_mute.ps1`,
  CoreAudio `ISimpleAudioVolume`): inaudible for the player.
* `tools\audio\proc_loopback\proc_loopback.exe <pid> <wav> <s> 1000`: Windows per-process WASAPI
  loopback (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`) in float, x1000 to undo the session
  volume. Why this and not something simpler: xemu's APU plays through SDL directly (not QEMU's
  audio subsystem, so `-audiodev wav` / `QEMU_AUDIO_DRV=wav` / the monitor's `wavcapture` give
  silence), its SDL has no `disk` driver, and a *muted* session gives the loopback silence too (the
  session volume is applied before the tap; a tiny volume keeps the float mix intact).
* `tools\audio\xemu_fb.py`: a screenshot without `screendump` (removed in xemu): `pmemsave` of the
  NV2A scanout buffer (PCRTC_START = 0x03710000, 768-pixel stride, 720 wide).
* `tools\audio\xemu_vp_dump.py` + `vp_decode.py` + `vp_timeline.py`: the VP voice table (256 x 128
  bytes at NV_PAPU_VPVADDR = 0x01108000, read over gdb) once a second, decoded to pitch / Hz, bins
  and volumes, envelopes, format, CBO/EBO/LBO, filter mode and coefficients. Our build writes the
  same bytes with `RECOMP_APU_VP_DUMP=<file>` (apu_vp.c `diag_report`), so both sides go through
  one decoder.
* `tools\audio\xemu_key.ps1`: keys by `PostMessage` (no focus) -- not needed in the end: the
  title screen's attract **Demo Mode** (a CPU-vs-CPU match that starts by itself) is the common
  scenario on both sides; our side idles at the title the same way (`audio_test.ps1 -Do launch`
  with no presses).
* `tools\audio\xemu_oracle_run.sh`: launch, ping the monitor every 15 s and relaunch on a hang
  (xemu deadlocked during boot in 3 of 5 background launches: every thread waiting, the main thread
  on a contended SRW lock -- `logs\audio3\xemu-hang-stacks-all.txt`), wait for Demo Mode, then
  record the loopback and the voice table together.
* `tools\audio\ab_compare.py`: side-by-side spectrograms, long-term spectra, per-second band
  levels, stereo side level, and the 20 s clips.

Our side: `tools\audio\audio_test.ps1` as before (background, muted, sandbox root
`E:\eras-sbx\audio3-root`, exe copy `NFL2K5_audio3.exe`, output `logs\audio3\`), with
`RECOMP_APU_PITCH_LOG`, `RECOMP_APU_BIN_LOG`, `RECOMP_APU_VOICE_LOG`.

## Lead 1 in detail

Retail run `r1` (Eagles at Cowboys quick game, 308 match seconds, `tools\audio\voice_table.py`):

| voices | what | rate (Hz) | gain | notes |
|---|---|---|---|---|
| 040-043 | DirectSound's four always-on helper voices (32-sample loop, 48 kHz) routed 8 bins each to all 32 mixbins at 0.50 | 48000 | 0.50 | same in xemu |
| 044/045 | crowd reaction stream (L/R) | 19642..23863, the game's swell | 0.12 mean, 0.33 max | low-pass on (`fmode 1`), same coefficients in xemu |
| 046-049 | commentary (centre bin) | 22049 | 0.16-0.31 | |
| 04A-04F | crowd beds (12 k / 22 k / 16 k pairs) | constant | 0.10 / 0.07 / 0.00 | |
| 050-06D | 2D one-shot pool, ADPCM mono | 5805..22559 (per sound) | 0.35 | 146 starts in 308 s; 62 start at 16 kHz |
| 000-037 | 3D sounds, ADPCM mono, HRTF/doppler | 5400..23800 (per sound) | 0.2-0.7 | 1295 starts |

The player's "16000..38055" entries (voices 050, 051, 057, 05A, always paired with a 21359 Hz entry
on 053/058) come from the SOFTDRINK session; 38055 Hz = 32 kHz pitched up 3 semitones or 44.1 kHz
pitched down 2.5, i.e. a replaced sound file with a pitch the retail pool never uses. It plays for
under a second at gain 0.27-0.35 -- a short sting, not the "chipmunk voice". What the player hears as
"deep voice then chipmunks" over seconds is answered by the A/B below.

## A/B: xemu vs NFL2K5-PC, the same scene

Scene: the title screen's attract **Demo Mode** on both sides (no input; the matchup is random: xemu
drew TEN at OAK, ours TB at DET), retail disc on both, 2D sound. Captures: `logs\audio3\o1-xemu.wav`
(240 s, the first ~125 s with a voice-table dump per 2 s, `o1-xemu-vp.bin`) and ours `d1.wav` (demo
at 149-401 s, build before the fixes), `d2.wav` (same build + `d2-vp.bin`), `d3.wav` (after the
fixes). Matched plays: `ab-xemu.wav` = o1 45-65 s (snap at 49 s), `ab-ours.wav` = d1 280-300 s
(plays at 288-296 s); picture `ab-demo.png`; before-the-fixes voice tables `ab-voice-tables.txt`.

### What is the same (so it is not the problem)

* **Every voice's setup.** The same decoder over xemu's and our voice tables (`vp_decode.py`) gives
  the same voices, rates, routing and gains: the four always-on helper voices 040-043 (48 kHz, 0.50
  into all 32 mixbins), crowd reaction 044/045 (19642..20813 Hz in the demo, 0.07-0.31, bins 0/1,
  low-pass `fmode 1`, filter words FFFFFB49/0000D104), commentary 046-049 (22049 Hz, 0.14-0.31, bin
  2 = centre, 046 also bin 1 at 0.25, 047 also effect send 11), beds 04A/04B (12 kHz, 0.10),
  04C/04D (22 kHz, 0.02-0.24), 04E/04F (16 kHz, 0.00), the 2D pool 050-070 (ADPCM mono, one rate
  per sound, 0.27-0.35, bins 0/1), and the 3D voices 000-03F (ADPCM mono, HRTF, bins 6/7 at 0.38
  with the reverb send 10 at 0.19). The game writes the same parameters in both; our VP reads them
  the same way. No sample-rate, header, ADPCM-alignment or "16 kHz floor" error: the rates are the
  game's own.
* **The crowd-reaction swell** (19642 -> 23850 Hz in both), the commentary pitch, the beds' gains.
* **The low tone of lead 3**: xemu's capture has the same wandering 200-800 Hz peaks 13-19 dB above
  the surrounding spectrum (328 Hz at 64-68 s, 404, 521, 709 Hz ...), i.e. it is the game's crowd /
  stadium content, not a stuck voice.
* **The overall spectral shape** below 8 kHz: a 30-80 Hz crowd bump, a 500-2000 Hz plateau, the
  same roll-off (`ab-demo.png`, long-term average spectra).

### Differences, largest first

| # | Difference | xemu | ours (before) | Cause | Status |
|---|---|---|---|---|---|
| 1 | **Voices reading RAM as audio.** A 3D (or 2D-pool) voice started with a zero-length play region: EBO written as -1 (0xFFFFFF), LBO garbage (0x1C7180 from 0xC71C7180), base sometimes near 0 (0x1FCC). The voice then streams RAM from its base as ADPCM at 10-21 kHz and gain 0.03-0.70 for minutes (CBO reached 0x413A00 = 4.3 M samples in d1): broadband garbage under everything -- the "fucked up in general", and the kind of thing heard as "deep voice / chipmunks" depending on what memory it crosses. | 0 such voices in 61 voice-table dumps | 6 starts in d1 (250 demo s), 2 in r1 (308 match s), 1 in d2; voice 00D in d2 was heard 108 s at 0.24 average | The game calls DirectSound's loop/play-region setter with length 0 from its sound state machine (`sub_0003DBC0` at 0x3E6E8 -> 0x445AC3 -> 0x450E1D, guest stacks in `d2.stderr.log`); length 0 should mean "to the end of the buffer" and gives -1 here. Why only in our port is open (a DirectSound-internal flags/size computation; the sound's buffer looks allocated: base 0x5D06A4). | **Mitigated in the APU**: a voice whose EBO is 0xFFFFFF plays nothing (`RECOMP_APU_PLAY_EMPTY=1` restores the old behaviour); the root cause in DirectSound's recompiled code is logged with a guest stack (`[VOICE] xxx EBO=-1 ... stack:`) for the next pass. |
| 2 | **Imaging above the sources' Nyquist.** xemu's output has nothing above ~11 kHz (the VP band-limits what it upsamples); ours carried the cubic interpolator's images to 20 kHz: a fizz on every voice, worst on 12-16 kHz sources (crowd beds, 2D pool). | 11-20 kHz band -98.6 dBFS (play, o1 45-65 s) | -59.5 dBFS (d1 280-300 s), -59.0 (d2 200-260 s) | No anti-imaging filter in our resampler (libsamplerate is stubbed). | **Fixed**: 4th-order Butterworth (two biquads) at 90 % of the source Nyquist per upsampled voice, in `voice_resample` (`RECOMP_APU_NO_LOWPASS=1` turns it off). |
| 3 | **Level.** Ours is louder: +4.5 dB in a matched play (-23.5 vs -28.0 dBFS mono), +7 dB between plays, with peaks at -2.7 dBFS against xemu's -8.1, so our soft limiter (knee at -6 dBFS) was working on peaks. | -28.0 dBFS, peak -8.1 | -23.5 dBFS, peak -2.7 | Partly #1 (the RAM voice is broadband at 0.24-0.7), partly the hand-made mixdown of the 3D bins 6-9, the reverb and the effect sends (the GP is not emulated). | Re-measured after #1/#2 below; the mixdown gains are left alone until a cleaner comparison (same matchup) is made. |
| 4 | **Stuck one-shot voices.** Our 3D voices stay active in their one-block end padding until the title re-binds them (3-13 voices active between plays, silent since the pad plays silence); xemu's end between plays (0 active). | 0 active between plays | 3 (d2) to 13 (d1) active, silent | DirectSound's stop condition on the real APU is not reproduced (the voice stays "active" with CBO in the pad). No audible effect after #1; costs voices. | Open (not audible). |
| 5 | **DSP effects.** The GP's I3DL2 reverb and the title's DSP program are not emulated; a Schroeder reverb stands in for the reverb send, effect sends are centred at -6 dB. | real DSP | approximation | known | Open; the spectral match below 8 kHz says it is not the main audible defect. |

### Before / after

Same build path, same scenario (Demo Mode, background, sandbox), before = `d1` (and `d2`), after =
`d3` (fixed build, 15:43). xemu is the reference. Clips (20 s, 48 kHz stereo):
`logs\audio3\ab-xemu.wav` (xemu, o1 45-65 s), `ab-ours.wav` = `ab-ours-before.wav` (d1 280-300 s),
`ab-ours-after.wav` (d3 300-320 s). Pictures: `ab-demo.png` (before), `ab-after.png` (after).

| measure | xemu | ours before | ours after |
|---|---|---|---|
| zero-length-region voices heard | 0 | 6 starts / 250 s (one heard 108 s at 0.24) | 6 starts, all muted (`[APU] voice xxx: zero-length region (EBO -1), muted`) |
| 11-20 kHz band, play (20 s) | -98.6 dBFS | -59.5 | -74.1 |
| 11-20 kHz band, whole demo (240 s) | - | -59.0 | -73.8 |
| 8-11 kHz band, play | -62.0 | -54.2 | -58.3 |
| mono level, play | -28.0 dBFS | -23.5 | -25.1 |
| peak, play | -8.1 dBFS | -2.7 | -2.9 |
| crowd beds / reaction mean gain (match s) | 0.10 + 0.02 / 0.13 (voice table) | 0.29 / 0.20 (d1) | 0.33 / 0.23 (d3, different plays) |

The imaging above 11 kHz is down 15 dB; the remaining 25 dB to xemu is the 48 kHz material (DirectSound's
helper voices, the reverb stand-in and the effect sends) and the soft limiter's distortion on peaks --
both part of difference #3/#5. The level is 1.6 dB closer.

## Open, in order

1. **Why our DirectSound writes EBO -1.** Two call paths from the game's sound state machine end in
   DirectSound's play-region update (0x450E1D): `0x3E6E8` (3D voices, base 0x1FCC = a sound that was
   never loaded) and `0x3E752` (2D pool, base 0). Both are "play a sound whose buffer is empty". On the
   console the same calls must either not happen or not start the voice; find which state differs
   (gdb on 0x445AC3 in xemu with the same hardware breakpoint method as `xemu_freq.gdb`). The APU-side
   mute is a safety net, not the cause.
2. **Mixdown level/limiter**: calibrate the 3D bins, reverb and effect-send gains against xemu on the
   same matchup (Demo Mode picks random teams; a Quick Game in xemu needs the keyboard path
   `xemu_key.ps1`, which worked at the title).
3. Stuck-but-silent 3D voices in the end pad (cost only).
4. The player's SOFTDRINK session: re-check with SOFTDRINK in xemu (`E:\ESPN NFL 2K5 (patched).xiso.iso`)
   -- its replaced sounds (the 38 kHz pool entries) and its crowd logic.

## Files

Vendored APU hunks (external/xboxrecomp is gitignored): `docs\AUDIO-USER-REPORTS-20261022-xboxrecomp.diff`
(apu_vp.c: `RECOMP_APU_VP_DUMP`, the EBO -1 stack log, the zero-length-region mute
(`RECOMP_APU_PLAY_EMPTY=1` off-switch), the anti-imaging low-pass (`RECOMP_APU_NO_LOWPASS=1` off-switch)).
Tools: `tools\audio\` `xemu_launch.ps1`, `xemu_oracle_run.sh`, `xemu_mon.py`, `xemu_fb.py`,
`xemu_key.ps1`, `xemu_vp_dump.py`, `vp_decode.py`, `vp_timeline.py`, `app_mute.ps1`,
`app_sessions.ps1`, `proc_loopback\proc_loopback.cpp`, `ab_compare.py`, `hf_band.py`, `tone_track.py`,
`voice_table.py`, `swing_vs_on.py`, `report_csv_plot.py`.


## Follow-up: matching xemu's spectral cliff (2026-10-23)

The largest remaining audible difference in `ab-after.png` was the top end: xemu's long-term spectrum
falls off a cliff by ~10-11 kHz (11-20 kHz near -100 dBFS), ours ran flat to 20 kHz -- harsh / "glitched".
On the console every voice is band-limited near the source Nyquist (sources are <=22 kHz), and the output
has nothing above ~11 kHz. Three changes, measured on a matched 20 s demo play (xemu reference clip vs our
build), clips `ab-xemu.wav` / `ab-ours-before.wav` (cubic) / `ab-ours-after.wav` (final), picture `ab-final.png`:

1. **Windowed-sinc voice resampler** (apu_vp.c `voice_resample`): replaces the cubic interpolator for
   upsampled voices with a 32-tap Kaiser-windowed sinc (beta 8, cutoff 0.42 of the source rate, 512
   phases), which is what xemu's libsamplerate does. Offline the images drop to -84..-88 dB.
   `RECOMP_APU_CUBIC=1` restores the old path.
2. **Output low-pass at 11 kHz** (apu_dsp.c, 4th-order Butterworth on the final L/R, after the soft
   limiter that itself makes harmonics): matches the cliff for every voice at once, including the 48 kHz
   helper voices and the limiter products. `RECOMP_APU_NO_OUTLP=1` off.
3. **-2.5 dB master trim** (`RECOMP_APU_OUT_GAIN`, default 0.75): the moderate play was ~4.5 dB hot.

| band | xemu | before (cubic) | after (sinc+LPF+trim) |
|---|---|---|---|
| rms (mono) | -28.0 dBFS | -23.5 | -26.1 |
| 0.5-8 kHz | -29.9 | -26.1 | -29.8 |
| 8-11 kHz | -62.2 | -54.1 | -54.9 |
| 11-15 kHz | -100.3 | -59.6 | -77.5 |
| 15-20 kHz | -100.6 | -71.1 | -101.9 |
| peak | -8.7 | -2.8 | -4.0 |

15-20 kHz now matches xemu (-102 vs -101, a 31 dB drop from before); 0.5-8 kHz and the overall level
match within 2 dB. 8-11 kHz (+7 dB) and 11-15 kHz (+22 dB) are still hot: that band holds the soft
limiter's intermodulation products, which the 11 kHz filter only attenuates by 3-18 dB. Closing it
would need a steeper output filter (risking a duller 9-10 kHz) or a cleaner limiter -- left open.
`toolsudio\scene_levels.py` classifies each second (quiet field / play) from the voice-table dump so
the level is compared on like scenes, not just one window.

The comb-like 500-1000 Hz stripes the coordinator flagged are not ours-only: a peak-spacing count found
9 peaks (median 67 Hz) in ours against 11 (59 Hz) in xemu over the same band -- crowd/voice content that
differs by matchup, not our reverb (whose combs resonate at 29-34 Hz, feeding only the 20-150 Hz bump).

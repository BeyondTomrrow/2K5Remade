# Local Music / Crib Jukebox

Status (2026-10-01): **phase 1 works.** `Music/<folder>` shows up as a soundtrack in
Create Clip / Stadium Music and the Crib Jukebox and plays through the game's own
WMA player (`src/nfl2k5_local_music.cpp`, `src/nfl2k5_wma_encode.cpp`). Getting
there needed four emulation fixes: `repe cmpsd` flag tracking in the lifter (the
WMA parser's GUID match), an overlapping decode in MSVC memcpy (stack shifted on
28-byte copies), memcpy run natively (its misaligned paths used unrecovered jump
tables; gen patches `MEMCPY_NATIVE_*`), and low-memory physical addresses tagged
(`XBOX_PHYS_LOW_TAG`) so the APU reads the song buffers instead of the contiguous
window. Songs are converted with Media Foundation, the Extended Content
Description object is stripped (the XDK parser rejects it), and the cache lives
in `saves/Soundtracks/cache`. Next: cover art (phase 2), FEATURES > SOUNDTRACK (phase 3).

Goal: the user drops folders of music into `<GameRoot>/Music/` (FOLDER = PLAYLIST,
no import step, no config) and they show up in the game's **real** music UI,
played by one shared player. Test library: `Music/Sam Spence & Da Riffs/`
(19 MP3s, copied from the dev PC and verified by SHA-256; `/Music/*` is
gitignored). Nothing in source or config may reference the original
download location.

`<GameRoot>` is the folder that holds `original\` (see `nfl2k5_root()` in
`src/main.c`: the exe's folder if it has `original\`, else `NFL2K5_ROOT`, else
the project root). On this PC: `E:\NFL2K5-PC`.

## What the game already has (default.xbe)

| Screen | Evidence | What it does |
|---|---|---|
| **Crib Jukebox** | `crib_jukebox.iff`, `JUKEBOX`, `L_song1..13`, `R_song1..5`, `NOW PLAYING`, `SHF`/`REP`/`NOR`, `|M_PRIMARY| Add Track`, `|M_SECONDARY| Remove Track`, `|M_PREVPAGE|`/`|M_NEXTPAGE|` (L/R = change the album), `Artist: %s`, `%d.` | Left: one album (cover art, title, numbered tracks with times, L/R to change album, A adds a track). Right: the playlist with NOW PLAYING, shuffle/repeat/normal, prev/play/next/pause/stop. The first time you build a playlist, the game offers to make it available in the menus as well ("Presentation Settings"). |
| **Stadium Music Manager** | `Welcome to the Stadium Music Manager!`, `Create New / Modify current / Load previously created Stadium Music`, `Select Soundtrack`, `Select Track`, `create_pa_music_clip_editor`, `Select Start Point / End Point`, `Clip Name`, events `Touchdown - HOME` ... `Two Minute Warning` | Clip editor: cuts start/end clips out of soundtrack songs and assigns them to stadium events. A spreadsheet UI, not a playlist browser. |

**The screen in the user's reference screenshot is the Crib Jukebox, not Create
Stadium Music.** The jukebox is almost exactly what the feature asks for:
album = playlist, cover, track list with durations, NOW PLAYING, shuffle and
repeat, and transport controls. Stadium Music is the second consumer of the
same data.

## How both screens get their albums: Xbox custom soundtracks

Both screens list the game's built-in licensed albums plus the console's
**custom soundtracks** (`Jukebox Soundtrack`, `< unavailable soundtrack >`,
`Soundtrack: %s`). The XBE links the standard XAPI soundtrack code, which
reads:

```
\Device\Harddisk0\Partition1\TDATA\FFFE0000\MUSIC\ST.DB          soundtrack database
\Device\Harddisk0\Partition1\TDATA\FFFE0000\MUSIC\%04x\%08x.WMA  songs (soundtrack id \ song id)
```

The XBE also has its own WMA decoder section (`WMADEC`). Our path layer
(`external/xboxrecomp/src/kernel/kernel_path.c`) maps `Partition1\` to the
game dir, so the game already looks for `original\disc\TDATA\FFFE0000\MUSIC\ST.DB`.
It isn't there, so today the jukebox shows built-in music only.

**So the original screens can be reused unchanged:** present `Music/<folder>`
as Xbox soundtracks, and the Crib Jukebox, the Stadium Music Manager and the
"use in menus" option all work through the game's own code: real layout,
fonts, prompts, sounds, navigation and transitions, with no UI to recreate.

## Plan

### Phase 1: Music folder as Xbox soundtracks (no UI changes)
1. `LocalMusicManager` (host, Windows): scan `<GameRoot>/Music/*`. Each
   immediate subfolder is one soundtrack named after the folder. Supported
   files: mp3, wma, m4a/aac, flac, wav, via Media Foundation. Read the title
   (fallback: file name with the track number stripped), artist and duration
   from the tags. Rescan when the game boots and when the jukebox opens.
2. Generate `ST.DB` in the documented Xbox layout (header with soundtrack ids;
   per soundtrack: name, song groups of 6 with song ids, names and lengths
   in ms). Check the exact layout against the recompiled XAPI reader before
   writing it. Serve it through the path layer, redirecting
   `TDATA\FFFE0000\MUSIC\` to a generated folder under `cache\music\`, so
   nothing is written into `original\`.
3. Songs: the game decodes WMA itself. Transcode each song once to
   WMA v2-compatible (format 0x161, 44.1 kHz stereo) with the Media
   Foundation WMA encoder into `cache\music\<id>\<song>.WMA`, keyed by the
   source file's size and mtime. Fallback if the Xbox decoder rejects it:
   intercept the game's WMA decoder object and feed host-decoded PCM.
4. Test: the Crib Jukebox lists "Sam Spence & Da Riffs" with 19 tracks and
   correct times; Add Track, then play; music keeps playing across menus;
   Stadium Music sees the same soundtrack.

### Phase 2: Cover art
The game shows covers for its built-in albums only; custom soundtracks get a
generic picture. Use `cover.png`/`cover.jpg`/`folder.png`/`folder.jpg`, else
embedded art, else the game's own fallback. Swap the jukebox's album-art
texture for that soundtrack while it is selected, through the existing
texture-override path. No layout change.

### Phase 3: FEATURES > MUSIC
Add a MUSIC entry to the Features menu that opens the same `crib_jukebox.iff`
screen (the same game code), so FEATURES > MUSIC and the Crib are literally
one screen and one player. The playlist and NOW PLAYING state are the game's
own, so the "two places share one playlist and position" rule holds
automatically. Needs the frontend menu-table work: find the Features menu
definition and the Crib's jump into the jukebox.

### One player
Playback stays in the game's own music system (the same slots/voices fixed in
commit a9ffd7b). No second host-side player.

### Portability
Only the `<GameRoot>/Music` lookup and the transcoder are platform-specific.
The ST.DB generator and the soundtrack mapping stay platform-neutral.

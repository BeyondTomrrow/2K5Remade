# Xbox Limitation Audit

Capacity limits NFL 2K5 inherited from the original Xbox, and the ones this port's
own runtime added, with what is known about each and what has been done.

**Status labels**

- **VERIFIED**: the limiting instruction, table, or constant was checked byte for byte
  in this executable (`tools/audit-xbox-limits.py`) or in this project's source,
  and/or observed in game.
- **PARTIAL**: the limit is real and located, but not every reader or consumer is
  audited, or the removal is not fully tested.
- **UNKNOWN**: reported by others or suspected, not checked here.

**Sources**

- This repository's code and runtime logs.
- The 2K Football Mod Tools reports in `.tmp-2k-football-mod-tools/` (third-party work,
  marked "EXPERIMENTAL / UNWITNESSED" by their authors). Their claims are only
  counted as VERIFIED after this project checked them against the retail executable.
- SOFTDRINK 2K28, a shipping mod that already lifts several of these limits in its
  own executable. `tools/audit-xbox-limits.py mods/packs/<pack>/default.xbe` shows
  which ones.

Run the byte checks with:

```
python tools/audit-xbox-limits.py                 # retail: 12 verified, 0 mismatched
python tools/audit-xbox-limits.py mods/packs/SOFTDRINK_2K28-v0.5/default.xbe
```

## Summary

| # | Limit | Status | Changed in this port |
|---|---|---|---|
| 1 | Create a Team stadium picker: 67 of the stadiums | VERIFIED | **Removed**: 82, tested in game |
| 2 | Franchise length: 30 seasons | VERIFIED | **Raised to 95** (through 2099); bytes and interpreter path verified, a 31st season not yet played |
| 3 | Created teams: 2 (team IDs 90/91) | VERIFIED | Not changed (needs roster-arena growth, see 7) |
| 4 | Guest RAM: 64 MB retail | VERIFIED | 128 MB layout available and used for mod packs |
| 5 | Contiguous (GPU) memory window: 64 MB | VERIFIED | Not changed (the title's addressing assumes it) |
| 6 | Internal render resolution: 720x480 | VERIFIED | Rendered up to 12x (8640x5760) on the PC |
| 7 | Roster arena / team record layout | PARTIAL | Not changed |
| 8 | Players per team (65 stored, 53 active) | PARTIAL | Not changed |
| 9 | Franchise save size | PARTIAL | Not changed (save compatibility) |
| 10 | Texture resolution / asset budgets | PARTIAL | Runtime decode cap removed (see 14); asset replacement not implemented |
| 11 | Settings, caches, logs beside the executable | VERIFIED | **Moved** to `%LOCALAPPDATA%\NFL2K5-PC` (no admin rights needed) |
| 12 | Mod packs listed in game: 16 | VERIFIED | Not changed (game menu engine row limit untested) |
| 13 | Inline vertex batches: 4096 dwords (port runtime) | VERIFIED | **Raised** to 0x20000 (xemu's batch size) |
| 14 | Paletted texture decode: 1024x1024 (port runtime) | VERIFIED | **Removed** (grows to the texture) |
| 15 | Game frame rate: 60 simulated frames a second | VERIFIED | Not changed by design (the clock is never sped up) |

## Details

### 1. Create a Team stadium picker: VERIFIED, removed

**Where the limit lives:**

- **ID list:** a 67-byte list of stadium IDs at VA `0x531B70` (`.rdata`).
- **Readers:** three instructions read it, at `0x3191C2`, `0x3192C9` and `0x319340`.
- **Bounds:** seven operands bound it:
  - `mov ecx,67` at `0x31926D` and `0x31928D`
  - `add eax,66` at `0x319289`
  - `cmp eax,67` at `0x3192D7`, `0x31934C` and `0x31A9FD`
  - `mov [C8F7F0h],66` at `0x31A9CE`

Of the game's stadium records, 15 are never offered in this picker. SOFTDRINK 2K28 lists
the same 15 extra IDs in its own build: 32, 36, 39–45, 48, 95–99.

**Change** (`src/main.c` `nfl2k5_expand_limits`, retail build only):

1. Every site is compared with the retail bytes. Any difference leaves all of them alone.
2. The list is copied into 96 bytes of guest heap, with the 15 IDs appended.
3. The three readers are repointed to the copy, and the bounds become 82/81.
4. The patched bytes are guest code that the native build has compiled in. So the
   functions holding them are marked for the interpreter (`nfl2k5_interp_mark_va`),
   which executes the patched bytes.

**Test** (`tools/drive-menu.ps1`): Features > Team Create > Choose Stadium, stepping right
through the whole list.

- **`NFL2K5_EXPANDED_LIMITS=0` (stock):** after Aloha Stadium the list wraps to Arizona
  Stadium.
- **Default (expanded):** after Aloha Stadium come Practice Facility, Visual Concepts Dome,
  Future Aloha Stadium, Super Bowl 2005/2008/2006/2007, Super Bowl Future, Ulterior Super
  Bowl, ESPN Stadium, Cheesesteak Dome, Loco Arena, Electra Coliseum, Funk Field, Dream
  Superdome, then Arizona Stadium.

No crash. The log shows the interpreter entering the picker functions (`0x319250`,
`0x31A8A0`, `0x31A980`). Captures: `logs/stadR1-names.png` vs `logs/stadR0-names.png`.

### 2. Franchise length: VERIFIED, raised

The limit is `cmp eax,1Eh; jle` at `0x2480CB`: a franchise ends after season index 30.
The mod tools' season-cap report found that the game's dates and ages are not repaired
after 2099. So the gate becomes 95: seasons 2004 through 2099, not 127.

- **Verified:** the instruction bytes, and that the function runs in the interpreter
  with the patched immediate.
- **Not yet verified:** playing or simulating past season 30, which takes a 30-season
  franchise. That is why the summary keeps this short of "removed".

Off switch: `expanded_limits = 0` in `nfl2k5_game.ini`.

### 3. Created teams: VERIFIED, unchanged

`sub` at `0x319370` reads `mov ax,[eax+118h]; cmp ax,5Ah; je; cmp ax,5Bh`: only team
IDs 90 and 91 count as created teams. Team Create shows exactly two empty slots.

More teams would need all of the following (ASTRA_ROSTER_ARENA_GROWTH_REPORT):

- new 500-byte team records in the roster arena;
- new labels and playbooks;
- new IDs (92–99 are already special teams);
- an audit of every consumer of the predicate.

That changes the roster-arena layout and saves (see 7, 9), so it was not done.

### 4. Guest RAM: VERIFIED

The retail Xbox has 64 MB (`XBOX_RETAIL_RAM`). The runtime also implements the
128 MB development-kit layout (`XBOX_DEVKIT_RAM`). Mod packs boot with it
(`[BOOT] Mod pack: 128 MB Xbox RAM`). SOFTDRINK 2K28 needs it and plays full games.

The retail game is left at 64 MB: its allocators size themselves for that, and
nothing in it uses more.

### 5. Contiguous memory window: VERIFIED, unchanged

`g_xbox_contig_size` is 64 MB, with xemu as the oracle (`xbox_memory_layout.c`).
GPU-visible allocations and the title's physical address arithmetic assume it.
Changing it would break address translation, so it stays.

### 6. Render resolution: VERIFIED, lifted on the PC side

The game renders 720x480 surfaces. The port renders every target at 1–12x (Video
Settings > Internal resolution). A target wider than D3D11's 16384 limit steps down
(`gpu_fit_scale`). Widescreen widens the field of view (Hor+).

Benchmarks are in `docs/BENCHMARKS.md`:

| Scale | Avg FPS | VRAM |
|---|---|---|
| 8x | 59.9 | 0.9 GB |
| 12x | 57.5 | 1.8 GB |

### 7. Roster arena and team records: PARTIAL

From the mod tools' reports, partly confirmed here:

- the roster/save arena is `0x91000` bytes (`0x90F60` used);
- team records are 500 bytes, with 52 team ordinals (0..51);
- each team holds a 65-pointer player prefix;
- the front-office table is 32 x 65 x 3 bytes.

The created-team predicate (3) was byte-verified here. The arena size and strides
were not.

Growing the arena is a save-format change: a new ROST version, migration of existing
saves, and relocation of every pointer. It is not attempted, to keep existing saves
compatible.

### 8. Players per team: PARTIAL

65 stored (the pointer prefix above) and 53 active in season, from the same reports;
not byte-verified here. A reserve overflow block (mod tools' design: 5 u16 indices per
team plus a 32-byte header) would need the arena growth of 7.

### 9. Franchise save size: PARTIAL

`SAVEGAME.DAT` for a franchise is 720,044 bytes (mod tools). The save library
(`src/nfl2k5_save_library.cpp`) keeps saves in the title's UDATA layout and exposes
them under `saves\franchise`, `rosters`, `settings` and `vip`. That is unchanged on
purpose, so existing saves keep working and Xbox saves import.

### 10. Textures and asset budgets: PARTIAL

The game's textures are fixed Xbox assets: DXT, paletted, 16-bit. The port's runtime
decode limits were removed (13, 14). The NV2A's own limits (4096x4096, its formats)
still apply to what the game submits.

A PC-side high-resolution texture replacement system is the right way to go beyond
the assets. The mod tools' HIRES pack research is a starting point. It is **not
implemented**.

### 11. Writable files beside the executable: VERIFIED, moved

These used to be written next to the executable, which fails without administrator
rights under Program Files:

- `nfl2k5_video.ini`
- `mods\presentation.ini`
- `cache\vp`, `cache\ps`, `cache\portraits`
- `logs\session.log` and `xbox_kernel.log`

They now go to `%LOCALAPPDATA%\NFL2K5-PC` through `xbox_user_path`
(`external/xboxrecomp/src/kernel/kernel_path.c`). Existing files are copied over on
first use.

**Portable layout is kept for:**

- the development checkout;
- `portable.txt` in the game folder;
- `NFL2K5_PORTABLE=1`.

**Test:** `NFL2K5_PORTABLE=0` played a game. Settings were migrated (identical
content), and shader caches and the kernel log were created in the profile.

**Saves and mods stay in the game folder.** The installer makes them user-writable,
and moving saves risks losing them.

### 12. Mod packs in the in-game list: VERIFIED, unchanged

There are two caps:

- `MP_MAX_PACKS 16` (`src/nfl2k5_mod_packs.c`);
- `VM_MAX_ROWS 16` for the screens the port adds to the game's own Options menus
  (`src/nfl2k5_video_menu.c`).

Raising them needs a test that the game's list widget scrolls past 16 rows. The same
row limit applies to Video Settings (currently 13 rows).

### 13. Inline vertex batches (port runtime): VERIFIED, raised

`NV_MAX_INLINE` was 4096 dwords. An `INLINE_ARRAY` draw longer than that silently
lost its tail; the NV2A has no such limit. It is now `0x20000`, xemu's batch length.

The log reports the first batch that the old cap would have cut (`[GPU] INLINE_ARRAY
batch past 4096 dwords`) and any batch over the new one (`tail dropped`).

### 14. Paletted texture decode (port runtime): VERIFIED, removed

The decode buffer for 8-bit paletted textures was a fixed 1 MB, so any paletted
texture over 1024x1024 failed to decode. It now grows to the texture's size.

### 15. Simulation rate: VERIFIED, by design

The game simulates at most 60 frames a second, tied to its vblank. The frame-rate cap
setting can lock 30 or 60, or keep the game's own choice. 120/Unlimited does not
create new frames, because that needs presentation-side interpolation, which is not
implemented. The game clock is never sped up.

## Not limits

The NV2A's 136 vertex-program slots and 192 constants (`VP_PROG_LEN`, `VP_CONSTS`)
are the hardware's own, and the game is written for them.

The port's 8 render-target slots and 4096-texture cache are caches and working sets,
not caps on content: they evict or fall back.

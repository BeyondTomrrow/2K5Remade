# Los Angeles Memorial Coliseum from NCAA Football 07 (2026-10-16)

Goal: the Coliseum (USC's home in NCAA 07) as a real NFL 2K5 stadium for the Rams (1946-1979, 2016-2019)
and the Raiders (1982-1994), which `data/eras/league/stadiums_1966_2025.json` maps to the stand-in
QUALCOMM STADIUM today. It is a new stadium: no retail venue, retail file, SOFTDRINK file or disc
byte is changed.

Status: **converted, previewed offline, packaged; NOT yet seen in game** (the no-code test path turned out
to be closed by the roster import, see "In-game test"; the Eras path needs the patch below, which was not
applied because another workflow owns `src/` and the build right now).

## What was done

1. **Export** (step 1): `tools/ncaa07-extractor/ncaa_app.py build-stadium` on STADIUMS.DAT entries
   0x600 (`USCXXX_FAIR`, day) and 0x604 (`USCXXX_NIGHT`). USC is `STAD.SRES` 128 (SNAM "USC Stadium",
   STNN "The Coliseum", 92,000 seats), so its entries are 128 * 12 = 0x600 onwards.
   GLBs: `data/eras/stadiums/coliseum/ncaa07/00000600/00000600_Complete.glb` and `.../00000604/...`.
   NCAA data: `H:\07\NCAA Football 07  (axekin.com)\data`, raw entries in `H:\NCAA07_Extractor_Work\stadiums_raw`.
2. **Converted** to the 2K5 stadium format (below): `tools/eras/stadiums/coliseum_build.py` writes the nine
   bundles `s60{d,a,n}{d,r,s}.iff` to `data/eras/stadiums/coliseum/build/` (+ `build_report.json`).
3. **Previewed** with a software renderer of the built bundles (`tools/eras/stadiums/render_preview.py`):
   `data/eras/stadiums/coliseum/preview/*.png`.
4. **Packaged** two ways: a test team that needs no code change
   (`data/eras/stadiums/coliseum/test-team/zzColiseumTest`) and the Eras stadium pack
   (`data/eras/stadiums/coliseum/pack/LA_Coliseum`), which needs the code patch below.

All tools: `tools/eras/stadiums/` (`ncaa_source.py` NCAA reader, `scne.py` SCNE parser/writer vendored from the
2K Football Mod Tools, MIT, `p8.py` P8 texture encoder, `coliseum_build.py`, `render_preview.py`,
`coliseum_test_team.py`, `coliseum_ingame_test.ps1`). Run Python normally (repo code); the mod tools were only
read, never run.

## The 2K5 stadium format (what matters here)

* A venue is a stadium record in the main ROST (0x80 bytes; `+0x00` name, `+0x04` capacity, `+0x08` location,
  `+0x0C` **asset code**, `+0x10` display name, `+0x18` indoor word, `+0x1C` grass word, climate floats,
  `+0x7C` id byte). 82 records; 53 asset codes (s00..s59 with gaps); the fictional rows (Loco Arena...) share
  s50..s59 (mod tools `docs/research/nfl2k5_weather_time_of_day.md` has the full row/prefix table).
* The asset code names nine archive files: `<code><t><w>.iff`, t = d/a/n (day, afternoon, night),
  w = d/r/s (dry, rain, snow); ~1.5 MB each. Nothing else on the disc is keyed by the stadium code.
* A bundle is a run of 0x20-byte-header chunks (`SCNE`, `TXTR`, `Fldd`), most VC-LZ compressed
  (`nfl_txtr.py`). s24dd: SCNE `field`, `detail_layer`, loose TXTR, `Fldd`, SCNE `cityscape`, SCNE
  `stadium`, SCNE `intro_cameras` (animated), TXTR.
* SCNE `stadium` = static scene (layout in `tools/eras/stadiums/scne.py`): textures (P8, swizzled, BGRA
  palette), materials (0x80, by name), shapes (0x100; FLOAT3 positions in stream 0, D3DCOLOR + NORMSHORT2 UV
  in stream 1 with a per-shape UV scale/offset at +0x30, push words = NV2A BEGIN/ARRAY_ELEMENT16/END triangle
  strips), nodes (0x60, one per shape), markers (0x40: sideline people, cameras...). Centimetres, y up, z along
  the field, right-handed (looking +z, +x is on the left). Strip triangle k (odd k swapped) faces the viewer
  when cross(b-a, c-a) points at it; NCAA uses the same numeric convention.
* The executable looks up **materials by name** (`crowd` animated atlas, `jumbo_tron` live feed, `digit_*`,
  `banner_*` team/ad banners, `LIGHT_*` self-lit, `nozwrite_*`), not shapes, so shapes can be removed and added.

## The conversion (`coliseum_build.py`)

Base: the retail s24 (QUALCOMM STADIUM) bundle of the same variant, so the field, detail layer, intro
cameras, loose TXTRs and (mostly) the cityscape stay retail.

* NCAA yards (Z up, field along +Y) -> 2K5 cm: X = -91.44 x, Y = 91.44 z, Z = 91.44 y (a rotation: winding
  and text kept; press box on -x like every retail home sideline; peristyle at +z).
* Removed from the stadium scene: the Qualcomm bowl (`groupNN`), its boards (`group37`), the Chargers wall
  pads (`explode_ps_*`), the retail field-level banners. Kept: every material and marker, `sideline_*`, `pyG*`
  pylons, the sideline yard markers.
* Added: the NCAA model in 40 m cells (76 shapes, ~10.8k vertices, 5.6k triangles), one material per NCAA
  texture (P8, <=128 px, the peristyle 256), cloned from `cement01` (opaque), `rail01` (cut-out) or
  `LIGHT_boxseat01` (lamps).
  * NCAA crowd cards (and band sections) -> the game's animated `crowd` material: same atlas convention
    with U and V swapped (NCAA v picks the strip, u runs along ~8.8 m per repeat; 2K5 9.14 m).
  * The two peristyle video boards -> `jumbo_tron` (UV 0..0.625 x 0..0.875, the retail feed rectangle).
  * NCAA ad placeholders (BNR_G/BNR_S) -> `banner_corp` cells; field-level RAIL banners and USC's tarps/curtains
    (Sign01/02, EdgeWall03) -> `banner_home_team` / `banner_away_team` / `banner_home_player` (the game puts
    the teams' art there). Dropped: the Trojan jerseys, NCAA scoreboard digits, field light rigs. The EA SPORTS
    panel of the NCAA scoreboard texture is painted over.
  * Vertex colours are NCAA's baked lighting (FAIR model for day/afternoon, NIGHT for night), afternoon tint
    (0.95, 0.88, 0.80), rain/snow overcast tints.
* Retail props fitted: the sideline props and the sideline people's markers beyond 30 m across are squeezed
  so the deepest one (51 m) ends at 46 m, inside the Coliseum's wall (46.6 m).
* Retail textures only the removed bowl used shrink to 8x8 (materials kept by name).
* Cityscape: the parking/road triangles inside the bowl's footprint become the cityscape's own grass.
* Sizes (every variant smaller than retail): s60dd 1,479,680 bytes (s24dd 1,555,392); stadium scene decodes to
  549,888 + 727,040 bytes (retail 701,312 + 875,264).

Previews: `data/eras/stadiums/coliseum/preview/s60dd_{blimp,endzone,sideline,high_sideline,peristyle}.png`,
`s60nd_{blimp,endzone}.png`, `s60ar_endzone.png`. The field shows retail s24's default art ("SAN DIEGO" end
zones); in game the home team's field art replaces it, as at every venue.

## Loading it

### A. Tried without code: an added team whose own stadium record names s60 (does NOT work)

Every added team (`mods\teams\<folder>\team.json` with a Z/Q code + `team.rost`) carries one stadium record
(root pair 3; the team record's `+0x114` points at it), and every file in `mods\teams\<folder>\files\` is put in
the extra archive pack at boot (`nfl2k5_archive_extend`). `coliseum_test_team.py build` makes
`zzColiseumTest` (code Z9, base 23 = Rams files, the Army test team's roster) whose stadium record reads
"LA Coliseum", Los Angeles, CA, code **s60**, outdoor, grass, and whose `files\` holds the nine bundles. The
folder sorts last, so in Team Select twelve LT presses from the default Cowboys (11 reach the 49ers, the first
resident team) wrap onto it. Result: the roster import rebinds the team's stadium to a resident row by asset
code, so s60 is ignored (see "In-game test").
`coliseum_ingame_test.ps1` installs it, drives a background Quick Game (`tools\bg-drive.ps1`), keeps frames
and removes it again. It is a test path only: it puts a "Coliseum Test" team in Team Select while installed.

### B. Eras only: the stadium pack (ready-to-apply patch, NOT applied)

The pack: `mods\eras\stadiums\LA_Coliseum\stadium.json` + `files\s60*.iff`
(`coliseum_test_team.py install-pack` copies it from `data/eras/stadiums/coliseum/pack/LA_Coliseum`):

```json
{"name": "LA MEMORIAL COLISEUM", "display": "L.A. Memorial Coliseum", "location": "Los Angeles, CA",
 "code": "s60", "clone": "QUALCOMM STADIUM", "host": "LOCO ARENA", "capacity": 93607, "indoor": 0, "grass": 1}
```

Nothing reads it until the two changes below are applied. With them, the files are in the archive (unused by
any retail row), and the Coliseum row exists only while a game the Eras launcher started is on; retail Quick
Game, Franchise and SOFTDRINK never see it. Not applied here because another workflow was building the game; no
`src/` file or `tools/apply-gen-patches.py` was edited. No gen patch is needed.

**Patch 1, `src/nfl2k5_team_files.c`, `nfl2k5_archive_extend`**: after the `mods\oldschool\files` block, add:

```c
    {   /* My NFL Eras stadium packs: mods\eras\stadiums\<name>\files\* (docs/eras/research/COLISEUM-IMPORT.md) */
        WIN32_FIND_DATAA sd, fd;
        HANDLE hs = FindFirstFileA("mods\\eras\\stadiums\\*", &sd);
        if (hs != INVALID_HANDLE_VALUE) {
            do {
                char pat[MAX_PATH];
                HANDLE h;
                if (!(sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || sd.cFileName[0] == '.') continue;
                snprintf(pat, sizeof pat, "mods\\eras\\stadiums\\%s\\files\\*", sd.cFileName);
                h = FindFirstFileA(pat, &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || nf >= 4096 || strlen(fd.cFileName) >= 96) continue;
                    snprintf(files[nf].host, sizeof files[nf].host, "mods\\eras\\stadiums\\%s\\files\\%s", sd.cFileName, fd.cFileName);
                    snprintf(files[nf].name, sizeof files[nf].name, "%s", fd.cFileName);
                    files[nf].size = fd.nFileSizeLow;
                    files[nf].block = blocks;
                    blocks += (fd.nFileSizeLow + 0x7FF) / 0x800;
                    nf++;
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            } while (FindNextFileA(hs, &sd));
            FindClose(hs);
        }
    }
```

(The log line after it says "added-team files"; optionally reword it to "mod files".)

**Patch 2, `src/nfl2k5_teams.c` + `src/nfl2k5_eras_launch.c`**: the Eras launcher (step 6) resolves
teams.csv's `stadium` by name with `nfl2k5_teams_stadium_find` and writes the record to the home slot's
`+0x114`, then calls `sub_00077470` (no remap by asset code on that path, unlike the roster import; see the
test). The pack stadium **borrows a fictional row inside the disc's table for the length of the game**
(default LOCO ARENA, row 44, "Los Angeles, CA"): its 0x80 bytes are saved, overwritten with the clone row's
(climate, indoor, grass), then given the pack's strings and code; the clean-up restores them. Staying inside
the table keeps any code that turns a stadium pointer back into a row index (climate, weather, Create a Team)
in range, which a heap copy would not. Only row 44 changes; the other rows that share s55 keep their files.

In `src/nfl2k5_teams.c`, after `nfl2k5_teams_stadium_find`:

```c
/* My NFL Eras stadium packs (docs/eras/research/COLISEUM-IMPORT.md): a stadium the disc does not have.
 * mods\eras\stadiums\<folder>\stadium.json {"name", "display", "location", "code", "clone", "host",
 * "capacity", "indoor", "grass"}; its bundles <code><t><w>.iff are in the archive (nfl2k5_archive_extend).
 * nfl2k5_teams_stadium_pack borrows the "host" row (a fictional stadium, default LOCO ARENA) for one game:
 * saved, filled from the "clone" row, given the pack's strings and code. nfl2k5_teams_stadium_pack_restore
 * puts it back (Eras clean-up). */
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
static uint32_t s_pack_host;
static uint8_t s_pack_saved[0x80];

static int pack_json(const char *text, const char *key, char *out, size_t n)
{
    char pat[48];
    const char *p, *q;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    if (!(p = strstr(text, pat)) || !(p = strchr(p + strlen(pat), ':'))) return 0;
    p++;
    while (*p == ' ') p++;
    if (*p == '"') {
        if (!(q = strchr(++p, '"')) || (size_t)(q - p) >= n) return 0;
    } else {
        for (q = p; *q && *q != ',' && *q != '}' && *q != '\r' && *q != '\n'; q++) {}
        if ((size_t)(q - p) >= n) return 0;
    }
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = 0;
    return 1;
}

static uint32_t pack_wstr(const char *s)      /* kept for the session: a few hundred bytes per pack */
{
    uint32_t n = (uint32_t)strlen(s), g = xbox_HeapAlloc(2 * (n + 1), 4), j;
    if (!g) return 0;
    for (j = 0; j <= n; j++) MEM16(g + 2 * j) = (uint16_t)(unsigned char)s[j];
    return g;
}

void nfl2k5_teams_stadium_pack_restore(void)
{
    if (!s_pack_host) return;
    memcpy((uint8_t *)xbox_GetMemoryOffset() + s_pack_host, s_pack_saved, 0x80);
    fprintf(stderr, "[TEAMS] Eras stadium pack: row %08X restored\n", s_pack_host);
    s_pack_host = 0;
}

uint32_t nfl2k5_teams_stadium_pack(const char *name, int *id_out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    nfl2k5_teams_stadium_pack_restore();          /* one borrowed row at a time */
    if (!name || !*name || (h = FindFirstFileA("mods\\eras\\stadiums\\*", &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        char p[MAX_PATH], text[4096], v[64], code[16], clone[64], host[64], disp[64], loc[64];
        uint32_t src, row, g;
        FILE *f;
        size_t n;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        snprintf(p, sizeof p, "mods\\eras\\stadiums\\%s\\stadium.json", fd.cFileName);
        if (!(f = fopen(p, "rb"))) continue;
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        if (!pack_json(text, "name", v, sizeof v) || _stricmp(v, name) != 0) continue;
        if (!pack_json(text, "code", code, sizeof code) || !pack_json(text, "clone", clone, sizeof clone)) continue;
        if (!pack_json(text, "host", host, sizeof host)) snprintf(host, sizeof host, "LOCO ARENA");
        if (!(src = nfl2k5_teams_stadium_find(clone, NULL)) || !(row = nfl2k5_teams_stadium_find(host, NULL))) continue;
        if (!pack_json(text, "display", disp, sizeof disp)) snprintf(disp, sizeof disp, "%s", v);
        if (!pack_json(text, "location", loc, sizeof loc)) loc[0] = 0;
        memcpy(s_pack_saved, (uint8_t *)xbox_GetMemoryOffset() + row, 0x80);
        s_pack_host = row;
        memcpy((uint8_t *)xbox_GetMemoryOffset() + row, (uint8_t *)xbox_GetMemoryOffset() + src, 0x7C); /* keeps the id byte */
        if ((g = pack_wstr(v))) MEM32(row + 0x00) = g;
        if (loc[0] && (g = pack_wstr(loc))) MEM32(row + 0x08) = g;
        if ((g = pack_wstr(code))) MEM32(row + 0x0C) = g;      /* the file-name key of the nine bundles */
        if ((g = pack_wstr(disp))) MEM32(row + 0x10) = g;
        if (pack_json(text, "capacity", v, sizeof v)) MEM32(row + 0x04) = (uint32_t)atoi(v);
        if (pack_json(text, "indoor", v, sizeof v)) MEM32(row + 0x18) = (uint32_t)atoi(v);
        if (pack_json(text, "grass", v, sizeof v)) MEM32(row + 0x1C) = (uint32_t)atoi(v);
        fprintf(stderr, "[TEAMS] Eras stadium pack %s: %s (code %s, from %s) in row %08X (%s)\n",
                fd.cFileName, name, code, clone, row, host);
        FindClose(h);
        if (id_out) *id_out = MEM8(row + 0x7C);
        return row;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
```

Add both prototypes to `src/nfl2k5_teams.h`; `<stdlib.h>` for `atoi` if missing; `xbox_GetMemoryOffset` and
`MEM8/16/32` as `nfl2k5_team_files.c` uses them. Pick a host whose name is not the target of a real
franchise (Loco Arena is unused by every NFL team).

In `src/nfl2k5_eras_launch.c` step 6, after the existing first line:

```c
    stadium = home->stadium[0] ? nfl2k5_teams_stadium_find(home->stadium, &stadium_id) : 0;
    if (!stadium && home->stadium[0]) stadium = nfl2k5_teams_stadium_pack(home->stadium, &stadium_id);
```

and call `nfl2k5_teams_stadium_pack_restore();` in the Eras clean-up next to the snapshot restore and slot
release (idempotent; nothing happens when no row is borrowed).

**Data, after the in-game check**: in `tools/eras/make_stadium_history.py` the Rams 1946-1979 and 2016-2019 and
Raiders 1982-1994 rows get `ingame = "LA MEMORIAL COLISEUM"` (`exact`), regenerate
`data/eras/league/stadiums_1966_2025.json`; teams.csv's `stadium` column then carries the name, and a
missing pack falls back to the template's stadium (the launcher already logs "not found ... the template's is
used"), so QUALCOMM STADIUM stays the safe fallback only if the generator keeps it: preferably the launcher
tries the pack name first and a `fallback` name second (teams.csv `stadium` = "LA MEMORIAL COLISEUM|QUALCOMM
STADIUM", split on `|` in `nfl2k5_eras_launch.c` step 6).

## In-game test (2026-10-16, background mode)

`tools/eras/stadiums/coliseum_ingame_test.ps1` (RECOMP_BACKGROUND=1, RECOMP_AUDIO_MUTE=1, `--retail`, its own
press file and frame folder, its own exe copy `build\Release\NFL2K5_coliseum.exe` so the other workflow's builds
and tests, which stop `NFL2K5.exe`, neither stop it nor are stopped by it; the copy is deleted afterwards).
Runs coliseum1/2 were stopped by the other workflow's rebuilds (03:43, 04:05); coliseum3-9 were driver fixes
(boot timing, PowerShell array flattening, Team Select stepping); **coliseum10** went all the way:

* the test team loaded on the home side (`[TEAMS] side 0 loads added team mods\teams\zzColiseumTest\team.rost`,
  Z9 colours), Team Select "EAGLES at TEST" with Rams uniforms, Coach Matchup, pregame, coin toss, plays;
  its nine s60 bundles were in the extra pack (`105 added-team files`); no crash;
* **but the game was played in the Edward Jones Dome, not the Coliseum**
  (`logs/coliseum10-sheet.png`, frames `logs/coliseum10-*.png`).

Why: the roster import `sub_000C1030` does not keep the imported team's stadium record. At `0x000C10EA` it
calls `sub_000BFB60(slot+0x114, imported+0x114, slot)`, which walks the resident stadium table
(`[0xB72918]+0x10` count, `+0x14` base, 0x80 each) comparing asset codes (`+0x0C`, `sub_00030C40`) and binds
the slot to the resident row with the same code. An unknown code ("s60") matches nothing, so the slot keeps
the stadium it had (here the Rams' dome). So an added team or a .rost can only ever name an existing venue
code; **a new stadium needs code** (the Eras patch above sets `+0x114` after the import, so it is not affected).
The stadium bundles are not opened through `0x49090`, so `NFL2K5_ASSET_LOG` does not list them either.

Not attempted: a test through a SOFTDRINK-style pack editing a ROST row (would touch the user's active pack
setup), or a private build with the patch (no builds were to be run).

## Open / next

* Crowd density, the stadium's huge size (the bowl is ~310 m long: check the far clip in wide shots), the
  retail s24 intro cameras (flyovers built for Qualcomm; they may pass through the peristyle), night
  brightness (NCAA's night colours are brighter than retail's), the NCAA lamps' LIGHT_ material at night.
* The cityscape is San Diego's (Mission Valley hills); a Los Angeles skyline would be new art.
* The field-level ad banners use retail banner_corp cells; the 1940s-70s look (no ads) would want them blank.

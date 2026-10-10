/*
 * Added teams' own asset files (2026-10-09).
 *
 * Every file the game opens by name goes through 0x49090 (name -> device via
 * 0x3A530, then the device's open); a name without a device prefix comes from
 * the disc archive. A team's art is a family of files named after its asset
 * code (team record +0x10C): uniforms "<code>h0.iff" / "<code>a0.iff",
 * helmet sets "<code>hdn.iff", the team file "c<code>.iff", cut-scenes
 * "cgfair<code>.iff", intro "ih<code>.iff", Team Select cards "unif_<code>..."
 * and more.
 *
 * An added team (mods\teams\<folder>\, team.json) has an asset code of its own
 * ("code": "Z1"; codes start with Z, which no game file name has before a
 * digit or letter pair). When the game asks for one of its files:
 *   - mods\teams\<folder>\files\<name> exists: it is opened from there, through
 *     the game's loose-file device "dvd:" -- the kernel maps the disc folder
 *     D:\NFL2K5-TEAMS\ to the game's mods\teams (xbox_path_map_disc_dir);
 *   - otherwise the same file of the team's base code ("base": "22") is
 *     loaded, so a team with no art of its own yet still plays.
 * No other team's files are touched: the new team is its own entry.
 *
 * NFL2K5_ASSET_LOG=1 logs every name the game asks for and what it got.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void xbox_path_map_disc_dir(const char *name, const char *host_dir);

#define MAX_TEAMS 512
typedef struct { char folder[96], code[8], base[8]; uint32_t primary, secondary, entry; } TeamFiles;
static TeamFiles s_teams[MAX_TEAMS];
static int s_n = -1, s_log = -1;
static uint32_t s_names;                         /* guest buffers for rewritten names */
static unsigned s_next;

/* "key": "value" from a small JSON file (team.json); enough for flat string fields. */
static int json_str(const char *text, const char *key, char *out, size_t n)
{
    char pat[48];
    const char *p, *q;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(text, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    p = strchr(p, '"');
    if (!p) return 0;
    q = strchr(++p, '"');
    if (!q || (size_t)(q - p) >= n) return 0;
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = 0;
    return 1;
}

/* "key": "#RRGGBB" -> 0xFFRRGGBB; 0 when missing. */
static uint32_t json_color(const char *text, const char *key)
{
    char v[16];
    if (!json_str(text, key, v, sizeof v) || v[0] != '#' || strlen(v) != 7) return 0;
    return 0xFF000000u | (uint32_t)strtoul(v + 1, NULL, 16);
}

static void scan(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char cwd[MAX_PATH], dir[MAX_PATH];
    s_n = 0;
    GetCurrentDirectoryA(sizeof cwd, cwd);
    snprintf(dir, sizeof dir, "%s\\mods\\teams", cwd);
    xbox_path_map_disc_dir("NFL2K5-TEAMS", dir);
    h = FindFirstFileA("mods\\teams\\*", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char p[MAX_PATH], text[8192];
        FILE *f;
        size_t n;
        TeamFiles t;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' || s_n >= MAX_TEAMS) continue;
        snprintf(p, sizeof p, "mods\\teams\\%s\\team.json", fd.cFileName);
        if (!(f = fopen(p, "rb"))) continue;
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        memset(&t, 0, sizeof t);
        snprintf(t.folder, sizeof t.folder, "%s", fd.cFileName);
        if (!json_str(text, "code", t.code, sizeof t.code) || (toupper((unsigned char)t.code[0]) != 'Z' && toupper((unsigned char)t.code[0]) != 'Q')) continue;
        if (!json_str(text, "base", t.base, sizeof t.base)) strcpy(t.base, "22");
        t.primary = json_color(text, "primary");
        t.secondary = json_color(text, "secondary");
        s_teams[s_n++] = t;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (s_n) fprintf(stderr, "[TEAMFILES] %d added teams with their own asset codes\n", s_n);
}

static uint32_t guest_wstr(const char *s)
{
    uint32_t at;
    size_t i, n = strlen(s);
    if (!s_names) s_names = xbox_HeapAlloc(64 * 512, 16);
    if (!s_names || n > 250) return 0;
    at = s_names + (s_next++ % 64) * 512;          /* a ring: the loader copies the name at once */
    for (i = 0; i <= n; i++) MEM16(at + 2 * (uint32_t)i) = (uint16_t)(unsigned char)s[i];
    return at;
}

/* Called at 0x38650 -- the CRC32 of an upper-cased UTF-16 file name, which
 * the archive's open and the game's resource bookkeeping both key files by --
 * with the name; returns the name to hash. A name of an added team with no
 * file of its own hashes as its base team's file, everywhere at once, so the
 * game keeps calling it by the added team's name and still finds the data. */
static int own_file(int k, const char *a);
static int team_of(const char *low, char **hit);
uint32_t nfl2k5_asset_hash_name(uint32_t name)
{
    char a[256], low[256], other[256], *hit;
    int i, k;
    if (s_n < 0) scan();
    if (!name || !s_n) return name;
    for (i = 0; i < 255 && MEM16(name + 2u * (uint32_t)i); i++) {
        uint16_t c = MEM16(name + 2u * (uint32_t)i);
        a[i] = c < 128 ? (char)c : '?';
        low[i] = (char)tolower((unsigned char)a[i]);
    }
    a[i] = low[i] = 0;
    if (strchr(a, ':') || (k = team_of(low, &hit)) < 0) return name;
    {
        static int hl = -1, hn;
        static char seen[256][40];
        int j;
        if (hl < 0) hl = getenv("NFL2K5_HASH_LOG") != NULL;
        for (j = 0; hl && j < hn && strncmp(seen[j], a, 39); j++) {}
        if (hl && j == hn && hn < 256) snprintf(seen[hn++], 40, "%s", a);
        else j = -1;
        if (j >= 0) fprintf(stderr, "[HASH] %s%s\n", a, own_file(k, a) ? " (own file)" : "");
    }
    if (own_file(k, a)) return name;
    snprintf(other, sizeof other, "%.*s%s%s", (int)(hit - low), a, s_teams[k].base, a + (hit - low) + strlen(s_teams[k].code));
    {
        uint32_t g = guest_wstr(other);
        return g ? g : name;
    }
}

static int team_of(const char *low, char **hit)
{
    int k;
    for (k = 0; k < s_n; k++) {
        char code[8];
        size_t cl;
        snprintf(code, sizeof code, "%s", s_teams[k].code);
        for (cl = 0; code[cl]; cl++) code[cl] = (char)tolower((unsigned char)code[cl]);
        if ((*hit = strstr((char *)low, code)) != NULL) return k;
    }
    return -1;
}

static int own_file(int k, const char *a)
{
    char host[MAX_PATH];
    snprintf(host, sizeof host, "mods\\teams\\%s\\files\\%s", s_teams[k].folder, a);
    if (GetFileAttributesA(host) != INVALID_FILE_ATTRIBUTES) return 1;
    snprintf(host, sizeof host, "mods\\teams\\%s\\cache\\%s.bin", s_teams[k].folder, a);   /* a front-end logo slot */
    return GetFileAttributesA(host) != INVALID_FILE_ATTRIBUTES;
}

/* NFL2K5_ASSET_DESC=1: once, the default device's entry for a known file (study). */
static void desc_debug(void)
{
    static int done;
    uint32_t dev, out, save_esp = g_esp, save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, i;
    if (done++ || !getenv("NFL2K5_ASSET_DESC")) return;
    g_esp -= 0x100;
    out = g_esp + 0x40;
    PUSH32(g_esp, 0x0BADC0DEu);
    sub_0003A520();
    dev = g_eax;
    fprintf(stderr, "[DESC] default device %08X: +38 %08X +188 %08X (+44 %08X) +19C %08X +1A0 %08X\n", dev,
            MEM32(dev + 0x38), MEM32(dev + 0x188), MEM32(dev + 0x188) ? MEM32(MEM32(dev + 0x188) + 0x44) : 0,
            MEM32(dev + 0x19C), MEM32(dev + 0x1A0));
    {
        static const char nm[] = "25A2.IFF";
        uint32_t w = out + 0x60;
        for (i = 0; i <= sizeof nm - 1; i++) MEM16(w + 2 * i) = (uint16_t)(i < sizeof nm - 1 ? nm[i] : 0);
        PUSH32(g_esp, out);
        g_ecx = dev; g_edx = w;
        PUSH32(g_esp, 0x0BADC0DEu);
        sub_0003AAB0();
        fprintf(stderr, "[DESC] open 25A2.IFF -> %u:", g_eax);
        for (i = 0; i < 0x30; i += 4) fprintf(stderr, " %08X", MEM32(out + i));
        fprintf(stderr, "\n");
    }
    g_esp = save_esp; g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi;
}

/* Called at 0x49090 with the requested name (UTF-16, guest); returns the name to use. */
uint32_t nfl2k5_asset_name(uint32_t name)
{
    desc_debug();
    char a[256], low[256];
    int i, k;
    if (s_log < 0) s_log = getenv("NFL2K5_ASSET_LOG") != NULL;
    if (s_n < 0) scan();
    if (!name) return name;
    for (i = 0; i < 255 && MEM16(name + 2u * (uint32_t)i); i++) {
        uint16_t c = MEM16(name + 2u * (uint32_t)i);
        a[i] = c < 128 ? (char)c : '?';
        low[i] = (char)tolower((unsigned char)a[i]);
    }
    a[i] = low[i] = 0;
    for (k = 0; k < s_n; k++) {
        char code[8], *hit, path[400], host[MAX_PATH], other[256];
        size_t cl;
        snprintf(code, sizeof code, "%s", s_teams[k].code);
        for (cl = 0; code[cl]; cl++) code[cl] = (char)tolower((unsigned char)code[cl]);
        if (strchr(a, ':') || !(hit = strstr(low, code))) continue;
        if (own_file(k, a)) {
            if (s_log) fprintf(stderr, "[ASSET] %s (the added team's own file)\n", a);
            return name;
        }
        if (s_log) fprintf(stderr, "[ASSET] %s (an added team's name; its base team's data)\n", a);
        return name;
    }
    if (s_log) fprintf(stderr, "[ASSET] %s\n", a);
    return name;
}

/* After an added team is imported into `slot` (nfl2k5_teams.c): its identity
 * from team.json beside its team.rost -- asset code, nickname, city,
 * abbreviation -- as new strings, so nothing of the team the roster file
 * started from shows. */
void nfl2k5_team_identity(const char *rost_path, uint32_t slot)
{
    static const struct { const char *key; uint32_t field; } k_fields[] = {
        { "code", 0x10C }, { "nickname", 0x104 }, { "abbreviation", 0x108 }, { "city", 0x138 },
    };
    char p[MAX_PATH], text[8192], v[96];
    const char *slash = strrchr(rost_path, '\\');
    FILE *f;
    size_t n, i;
    if (!slash) return;
    snprintf(p, sizeof p, "%.*s\\team.json", (int)(slash - rost_path), rost_path);
    if (!(f = fopen(p, "rb"))) return;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (json_str(text, "base", v, sizeof v) && isdigit((unsigned char)v[0]))
        MEM16(slot + 0x118) = (uint16_t)atoi(v);   /* the team number: the base team's (crowd, colour tables) */
    for (i = 0; i < sizeof k_fields / sizeof k_fields[0]; i++) {
        uint32_t g, j, len;
        if (!json_str(text, k_fields[i].key, v, sizeof v)) continue;
        len = (uint32_t)strlen(v);
        g = xbox_HeapAlloc(2 * (len + 1), 4);        /* kept: the team record points at it */
        if (!g) continue;
        for (j = 0; j <= len; j++) MEM16(g + 2 * j) = (uint16_t)(unsigned char)v[j];
        MEM32(slot + k_fields[i].field) = g;
    }
}


/* ---- the archive's extra pack (2026-10-09) --------------------------------
 *
 * The game mounts its archive at 0x42450: the index header (0x9C bytes) goes
 * to 0xA764F8 -- entry count, reserved, pack count (0xA76500), 36 pack sizes
 * in 2 KB blocks (0xA76504) -- and the 12-byte entries {CRC32 of the upper-case
 * UTF-16 name, size, offset in blocks} to a table at [0xA76594]; files are
 * found by a linear search (0x41FC0) and read from pack files
 * "VC_53450030\<0..F>". Before the packs are opened (gen patch
 * ARCHIVE_EXTRA_PACK), every added team's files go into one more pack, G,
 * written to the disc folder, and their entries are appended to a copy of the
 * table: the game then opens them by name like its own files. */
extern BOOL xbox_translate_path(const char *xbox_path, WCHAR *host, DWORD n);

static uint32_t name_crc(const char *name)
{
    uint32_t c = 0xFFFFFFFFu;
    const unsigned char *p;
    for (p = (const unsigned char *)name; *p; p++) {
        unsigned v = (unsigned)toupper(*p), b, k;
        for (b = 0; b < 2; b++) {                        /* UTF-16LE: the char, then 0 */
            c ^= b ? 0u : v;
            for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

void nfl2k5_archive_extend(void)
{
    typedef struct { char host[MAX_PATH]; char name[96]; uint32_t size, block; } F;
    static F files[4096];
    int nf = 0, k;
    uint32_t blocks = 0, count = MEM32(0xA764F8), packs = MEM32(0xA76500), table = MEM32(0xA76594);
    uint32_t start = 0, i, nt;
    WCHAR packw[MAX_PATH];
    if (s_n < 0) scan();
    if (packs >= 36 || !table) return;
    for (k = 0; k < s_n; k++) {
        WIN32_FIND_DATAA fd;
        char pat[MAX_PATH];
        HANDLE h;
        snprintf(pat, sizeof pat, "mods\\teams\\%s\\files\\*", s_teams[k].folder);
        h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || nf >= 4096 || strlen(fd.cFileName) >= 96) continue;
            snprintf(files[nf].host, sizeof files[nf].host, "mods\\teams\\%s\\files\\%s", s_teams[k].folder, fd.cFileName);
            snprintf(files[nf].name, sizeof files[nf].name, "%s", fd.cFileName);
            files[nf].size = fd.nFileSizeLow;
            files[nf].block = blocks;
            blocks += (fd.nFileSizeLow + 0x7FF) / 0x800;
            nf++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!nf) return;
    {
        char xp[64];
        snprintf(xp, sizeof xp, "D:\\VC_53450030\\%c", "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[packs]);
        if (!xbox_translate_path(xp, packw, MAX_PATH)) return;
    }
    {   /* the pack: each file at a 2 KB boundary */
        FILE *out = _wfopen(packw, L"wb");
        static char buf[1 << 16];
        if (!out) { fprintf(stderr, "[TEAMFILES] cannot write the extra pack\n"); return; }
        for (i = 0; i < (uint32_t)nf; i++) {
            FILE *in = fopen(files[i].host, "rb");
            size_t n, tot = 0;
            _fseeki64(out, (long long)files[i].block * 0x800, SEEK_SET);
            if (in) { while ((n = fread(buf, 1, sizeof buf, in)) > 0) { fwrite(buf, 1, n, out); tot += n; } fclose(in); }
            if (tot != files[i].size) fprintf(stderr, "[TEAMFILES] %s: read %zu of %u bytes\n", files[i].host, tot, files[i].size);
        }
        _fseeki64(out, (long long)blocks * 0x800 - 1, SEEK_SET);
        fputc(0, out);
        fclose(out);
    }
    for (i = 0; i < packs; i++) start += MEM32(0xA76504 + 4 * i);
    nt = xbox_HeapAlloc((count + (uint32_t)nf) * 12, 16);
    if (!nt) return;
    memcpy((uint8_t *)xbox_GetMemoryOffset() + nt, (uint8_t *)xbox_GetMemoryOffset() + table, count * 12);
    for (i = 0; i < (uint32_t)nf; i++) {
        uint32_t e = nt + (count + i) * 12;
        MEM32(e) = name_crc(files[i].name);
        MEM32(e + 4) = files[i].size;
        MEM32(e + 8) = start + files[i].block;
    }
    MEM32(0xA76594) = nt;
    MEM32(0xA764F8) = count + (uint32_t)nf;
    MEM32(0xA76504 + 4 * packs) = blocks;
    MEM32(0xA76500) = packs + 1;
    fprintf(stderr, "[TEAMFILES] %d added-team files in an extra archive pack (%u KB)\n", nf, blocks * 2);
}


/* ---- roster.csv: an added team's players (2026-10-09) --------------------
 *
 * Written by the Team Creator (tools/team-creator), from its roster generator
 * or from an imported Madden / NCAA roster. After the team is imported, each
 * of its players (team record: 65 player pointers, count at +0x11C) gets the
 * row of the same index:
 *
 *   first,last,pos,number,height,weight,years_pro,skin,face,hand,<28 ratings>
 *
 * pos is the game's position code (0 QB .. 16 DE); ratings in the record's own
 * order (+0x36: speed, agility, arm, stamina, kick power, durability, strength,
 * jumping, coverage, route running, tackle, break tackle, accuracy, reading
 * coverage, catch, run block, pass block, ball security, pass rush, run
 * coverage, kick accuracy, kicking style, leadership, power run style,
 * composure, scramble, consistency, aggressiveness). Rows are in depth order
 * within each position; names are new strings. */
static uint32_t new_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), g = xbox_HeapAlloc(2 * (n + 1), 4), j;
    if (!g) return 0;
    for (j = 0; j <= n; j++) MEM16(g + 2 * j) = (uint16_t)(unsigned char)s[j];
    return g;
}

static int csv_fields(char *line, char **out, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        out[n++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = 0;
    }
    for (int i = 0; i < n; i++) {
        char *e = out[i] + strlen(out[i]);
        while (e > out[i] && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    }
    return n;
}

void nfl2k5_team_roster(const char *rost_path, uint32_t slot)
{
    char p[MAX_PATH], line[1024];
    const char *slash = strrchr(rost_path, '\\');
    FILE *f;
    int row = 0, count = MEM8(slot + 0x11C), depth[17];
    if (!slash) return;
    snprintf(p, sizeof p, "%.*s\\roster.csv", (int)(slash - rost_path), rost_path);
    if (!(f = fopen(p, "r"))) return;
    memset(depth, 0, sizeof depth);
    while (fgets(line, sizeof line, f) && row < count) {
        char *v[40];
        uint32_t pl, w;
        int n, i, pos;
        if (!isdigit((unsigned char)line[0]) && strchr(line, ',') && strncmp(line, "first,", 6) == 0) continue;
        n = csv_fields(line, v, 40);
        if (n < 10 + 28) continue;
        pl = MEM32(slot + 4u * (uint32_t)row);
        if (!pl) break;
        pos = atoi(v[2]);
        if (pos < 0 || pos > 16) pos = 0;
        MEM32(pl + 0x10) = new_wstr(v[0]);
        MEM32(pl + 0x14) = new_wstr(v[1]);
        MEM8(pl + 0x35) = (uint8_t)pos;
        w = MEM32(pl + 0x20);
        w = (w & ~(0x7Fu << 3)) | ((uint32_t)(atoi(v[3]) & 0x7F) << 3);          /* jersey */
        w = (w & ~(0xFu << 17)) | ((uint32_t)(atoi(v[8]) & 0xF) << 17);           /* face */
        MEM32(pl + 0x20) = w;
        MEM8(pl + 0x2B) = (uint8_t)atoi(v[4]);                                    /* height, inches */
        MEM8(pl + 0x2A) = (uint8_t)(atoi(v[5]) > 150 ? atoi(v[5]) - 150 : 0);     /* weight - 150 */
        MEM8(pl + 0x25) = (uint8_t)((MEM8(pl + 0x25) & 0xE0) | (atoi(v[6]) & 0x1F));
        {   /* skin: +0x18 bit 7 and +0x19 bits 0-3; hand: +0x18 bit 1 */
            int skin = atoi(v[7]) & 0x1F;
            MEM8(pl + 0x18) = (uint8_t)((MEM8(pl + 0x18) & 0x7D) | ((skin & 1) << 7) | ((atoi(v[9]) & 1) << 1));
            MEM8(pl + 0x19) = (uint8_t)((MEM8(pl + 0x19) & 0xF0) | (skin >> 1));
        }
        for (i = 0; i < 28; i++) {
            int r = atoi(v[10 + i]);
            MEM8(pl + 0x36 + (uint32_t)i) = (uint8_t)(r < 0 ? 0 : r > 99 ? 99 : r);
        }
        {   /* depth chart: rank in this position by row order (the auto depth chart's rule) */
            int rank = depth[pos] < 7 ? depth[pos] : 7, side = rank == 0 ? 2 : rank == 1 ? 0 : rank == 2 ? 1 : rank;
            uint16_t d = MEM16(pl + 0x28);
            depth[pos]++;
            d = (uint16_t)((d & 0x03FF) | (rank << 10) | (side << 13));
            MEM16(pl + 0x28) = d;
            MEM8(pl + 0x52) &= 0xE0;
        }
        MEM16(pl + 0x04) = 0;                        /* no recorded play-by-play name */
        /* photo: the portrait.cdf name (a number); the Team Creator's own photos have
         * numbers from 40000 (served from cache\<n>.bin). None: 39999, which no
         * portrait has ("No photo"), never the template player's face. */
        MEM16(pl + 0x06) = (uint16_t)(n > 38 && atoi(v[38]) > 0 ? atoi(v[38]) : 39999);
        row++;
    }
    fclose(f);
    fprintf(stderr, "[TEAMFILES] %s: %d players from roster.csv\n", p, row);
}


/* ---- team colours (2026-10-09) ---------------------------------------------
 *
 * The front end's team colours -- the Team Rosters backdrop, row highlights,
 * chiclet backgrounds -- come from a table of 80 entries of 0x1C bytes at
 * 0x4E7FE0: {code string, brightness float, primary, secondary, two u16 flags,
 * accent, shadow}. Seven lookups (0x68D70 ...) walk it comparing the team's
 * asset code, falling back to blue 0xFF0065E6. For an added team (gen patch
 * TEAM_COLOR_ENTRY) the walk starts at an entry of its own built from
 * team.json's colours, placed so the lookups' index arithmetic
 * (0x4E7FE0 + i * 0x1C) lands on it; the first compare then matches. */
uint32_t nfl2k5_team_color_entry(uint32_t code)
{
    int k, i;
    if (s_n < 0) scan();
    if (!code || !s_n) return 0;
    for (k = 0; k < s_n; k++) {
        TeamFiles *t = &s_teams[k];
        uint32_t e, r, g, b;
        for (i = 0; t->code[i] && toupper((unsigned char)t->code[i]) == toupper(MEM16(code + 2u * (uint32_t)i) & 0xFF) && MEM16(code + 2u * (uint32_t)i) < 0x100; i++) {}
        if (t->code[i] || MEM16(code + 2u * (uint32_t)i) || !t->primary) continue;
        if (!t->entry) {
            uint32_t blk = xbox_HeapAlloc(0x1C * 3, 4);
            if (!blk) return 0;
            e = blk + (0x1Cu - (blk - 0x4E7FE0u) % 0x1Cu) % 0x1Cu;
            r = (t->primary >> 16) & 0xFF; g = (t->primary >> 8) & 0xFF; b = t->primary & 0xFF;
            MEM32(e + 0x04) = 0x3F800000u;                                 /* brightness 1.0 */
            MEM32(e + 0x08) = t->primary;
            MEM32(e + 0x0C) = t->secondary ? t->secondary : 0xFFFFFFFFu;
            MEM16(e + 0x10) = 0; MEM16(e + 0x12) = 0;
            MEM32(e + 0x14) = t->primary;                                  /* accent */
            MEM32(e + 0x18) = 0xC0000000u | ((r / 2) << 16) | ((g / 2) << 8) | (b / 2);   /* shadow */
            t->entry = e;
            if (s_log > 0) fprintf(stderr, "[TEAMFILES] %s colours %08X/%08X at %08X\n", t->code, t->primary, MEM32(e + 0x0C), e);
        }
        MEM32(t->entry) = code;               /* the walk compares this with the team's code: same string */
        return t->entry;
    }
    return 0;
}


/* ---- front-end logos: extra slots of the streamed caches (2026-10-09) -------
 *
 * logos.cdf (Team Rosters backdrops "logo_<code>_<style>") and flipchip.cdf
 * (roster card chips "<code>_flipchip_00_<side><style>") are caches of
 * fixed-size slots; their index (CACR chunk of logos.iff / flipchip.iff:
 * name, count +0x40, slot size +0x44, sorted name CRCs +0x48) is
 * binary-searched at 0x42D50 and slot i is read from offset i * size
 * (0x442F0, completion 0x44280). An added team's slots
 * (mods\teams\<folder>\cache\<name>.bin, made by the Team Creator) get indices
 * past the end (gen patch CACHE_EXTRA_INDEX), and those reads are served from
 * the files at once (CACHE_EXTRA_READ). */
extern void sub_00044280(void);

typedef struct { uint32_t crc; int flip; char path[MAX_PATH]; } CacheSlot;
static CacheSlot s_slots[2048];
static int s_nslots = -1;

static void cache_scan(void)
{
    int k;
    s_nslots = 0;
    if (s_n < 0) scan();
    for (k = 0; k < s_n; k++) {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        char pat[MAX_PATH];
        snprintf(pat, sizeof pat, "mods\\teams\\%s\\cache\\*.bin", s_teams[k].folder);
        if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) continue;
        do {
            CacheSlot *c;
            char name[MAX_PATH];
            const unsigned char *p;
            uint32_t crc = 0xFFFFFFFFu;
            size_t n = strlen(fd.cFileName);
            if (s_nslots >= (int)(sizeof s_slots / sizeof s_slots[0]) || n < 5) continue;
            snprintf(name, sizeof name, "%.*s", (int)(n - 4), fd.cFileName);
            for (p = (const unsigned char *)name; *p; p++) {    /* CRC32 of the UTF-16LE name (0x38600) */
                unsigned b, j;
                for (b = 0; b < 2; b++) {
                    crc ^= b ? 0u : *p;
                    for (j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
                }
            }
            c = &s_slots[s_nslots++];
            c->crc = ~crc;
            c->flip = strstr(name, "flipchip") ? 1 : isdigit((unsigned char)name[0]) ? 2 : 0;   /* the cache: portraits are numbers */
            snprintf(c->path, sizeof c->path, "mods\\teams\\%s\\cache\\%s", s_teams[k].folder, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    fprintf(stderr, "[TEAMFILES] %d front-end logo slots of %d added teams\n", s_nslots, s_n);
}

/* Which cache an index is (its file name first): 0 logos.cdf, 1 flipchip.cdf,
 * 2 portrait.cdf (player photos, named by number), -1 another. */
static int cache_is_flip(uint32_t cacr)
{
    static const char *const names[3] = { "logos.cdf", "flipchip.cdf", "portrait.cdf" };
    int k, i;
    for (k = 0; k < 3; k++) {
        for (i = 0; names[k][i] && (MEM16(cacr + 2u * (uint32_t)i) | 0x20) == (unsigned)names[k][i]; i++) {}
        if (!names[k][i] && !MEM16(cacr + 2u * (uint32_t)i)) return k;
    }
    return -1;
}

/* 0x42D50 found nothing: cacr = the index (its name first), crc = the name's.
 * Returns 1 and the extra index when an added team has that slot. */
int nfl2k5_cache_extra_index(uint32_t cacr, uint32_t crc, uint32_t out)
{
    int i, j = 0, flip;
    if (s_nslots < 0) cache_scan();
    if (!s_nslots || !cacr) return 0;
    if ((flip = cache_is_flip(cacr)) < 0) return 0;
    for (i = 0; i < s_nslots; i++) {
        if (s_slots[i].flip != flip) continue;
        if (s_slots[i].crc == crc) {
            if (out) MEM32(out) = MEM32(cacr + 0x40) + (uint32_t)j;
            return 1;
        }
        j++;
    }
    return 0;
}

/* 0x442F0 about to read slot [cache+0x90] into buf: an extra slot is read from
 * its file and completed through the game's own callback. Returns 1 if done. */
int nfl2k5_cache_extra_read(uint32_t cache, uint32_t buf)
{
    static uint32_t req;
    uint32_t cacr = MEM32(cache), idx = MEM32(cache + 0x90), count, size, save_esp, save_ebx, save_esi, save_edi, save_ebp;
    int i, j, flip;
    FILE *f;
    if (s_nslots <= 0 || !cacr) return 0;
    count = MEM32(cacr + 0x40);
    size = MEM32(cacr + 0x44);
    if (idx < count) return 0;
    if ((flip = cache_is_flip(cacr)) < 0) return 0;
    for (i = 0, j = 0; i < s_nslots; i++) {
        if (s_slots[i].flip != flip) continue;
        if ((uint32_t)j++ == idx - count) break;
    }
    if (i == s_nslots) return 0;
    {
        unsigned char tmp[0x20000];
        size_t n = 0;
        if (size > sizeof tmp) return 0;
        memset(tmp, 0, size);
        if ((f = fopen(s_slots[i].path, "rb")) != NULL) { n = fread(tmp, 1, size, f); fclose(f); }
        memcpy((void *)XBOX_PTR(buf), tmp, size);
        { static int ln; if (ln++ < 16) fprintf(stderr, "[TEAMFILES] logo slot %s (%u bytes) -> %08X\n", s_slots[i].path, (unsigned)n, buf); }
    }
    if (!req) req = xbox_HeapAlloc(0x40, 16);
    if (!req) return 0;
    MEM32(req + 0x18) = size;
    MEM32(req + 0x1C) = buf;
    MEM32(req + 0x20) = 0;
    save_esp = g_esp; save_ebx = g_ebx; save_esi = g_esi; save_edi = g_edi; save_ebp = g_ebp;
    PUSH32(g_esp, cache);
    g_edx = req;
    PUSH32(g_esp, 0x0BADC0DEu);
    sub_00044280();
    g_esp = save_esp; g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    return 1;
}

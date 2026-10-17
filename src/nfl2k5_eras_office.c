/* My NFL Eras: the office (docs/eras/ERAS-OFFICE-DESIGN.md, milestone M3-office).
 *
 * A copy of the Franchise office's machinery, never the Franchise code: the
 * Coach's Desk / Front Office rooms come from Eras' own package
 * erasoffice.iff (byte copies of the coach_desk and front_office SCNE / LAYT
 * records, built from the player's disc by tools/eras/build-office-pack.py and
 * served from the archive's extra pack G), loaded under the context
 * L"ERASOFFICE" and always looked up with it; the dressing (team logo, wall
 * photos, office photos) comes from erasskin.iff (a renamed copy of fr31.iff,
 * the NFL shield and black-and-white photo set) under L"ERASSKIN".
 *
 * What the Front Office does (sub_0034DAA0 / DB50 / DBB0 / DA20 / D7A0), done
 * here in C with Eras' own state:
 *
 *   move(a, b)   ret = b; target = a; a == 0: time 0; else time a - 1 and,
 *                for an "in" move (b > 0), target a + 0.4      (= sub_001427A0)
 *   msg 6        time < target: input off, advance dt * 1.5 (1.0 for the
 *                0 -> 1 entrance dolly); else input on, and a pending row
 *                arrives
 *   msg 7        save the camera, camera object from the scene camera
 *                (coach_cameraShape / FOcamShape), make it current, draw the
 *                scene, restore
 *
 * Engine primitives only (ERAS-OFFICE-DESIGN 1.2): package queue / wait /
 * unload (sub_00043F50, sub_000432D0, sub_000432F0), resource lookup by
 * context (sub_000449E0), scene time (sub_0002F010 / sub_0002F030), camera
 * (sub_00066B20, sub_0002AD80, sub_0013E710, sub_0002AC80), scene draw
 * (sub_0002DE60), input lock (sub_0014FC40 / sub_0014FC30), list mode
 * (sub_0014FCD0), sounds (sub_0014D390), ambience (sub_00165BE0 / C10). No
 * Franchise function, no Franchise global ([0xE3C0A0], 0xAA2140..0xAA247C,
 * 0xCB8D10..0xCB8FD8, [0xA84498]) is read or written.
 *
 * One office per era: the room (coach_desk or front_office), whole props
 * hidden by node (flag 0x10 at node +0x0C, skipped by sub_00021860), material
 * texture swaps (+0x30, +8 bit 0), and the texture-pack era context
 * (src/nfl2k5_texture_packs.cpp: era folders mods\eras\offices\<era>\ and the
 * era's colour grades of the retail textures, only while the office shows).
 * mods\eras\offices\<era>\era.ini overrides the built-in table. Every node
 * flag and material slot is snapshotted at load and restored before a look is
 * applied. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_office.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define FOURCC_SCNE     0x454E4353u
#define FOURCC_TXTR     0x52545854u
#define FOURCC_LAYT     0x5459414Cu
#define PAD_WORDS       0x00B37A78u   /* sub_00070A10: the game's button word, pad * 0x244 */
#define BTN_LT          0x4000u       /* table 0x4E95C0 (verified); the LT/RT order is inferred */
#define BTN_RT          0x8000u
#define FSM_DT          0x104u        /* the frame time the Front Office's msg 6 reads */
#define SPEED           1.5f
#define MAX_NODES       64
#define MAX_MATS        64
#define RET_ADDR        0xFEE0FFF0u

/* ROOM_DESK80: the Coach's Desk with the IBM PC 5150 (monitor on the system unit, keyboard),
 * a reshaped copy made by tools/eras/build-office-ibmpc.py; same camera curve as the desk. */
enum { ROOM_DESK, ROOM_FO, ROOM_DESK80, ROOMS };
static const char *const k_room[ROOMS] = { "coach_desk", "front_office", "coach_d80s" };
static const char *const k_cam[ROOMS] = { "coach_cameraShape", "FOcamShape", "coach_cameraShape" };

/* A stop (a, b) as sub_001427A0 takes it; a < 0 = reverse dolly out of the
 * front_office (1 -> 0), a == 0 = no move. */
typedef struct { float a, b; } Stop;

typedef struct {
    char id[8];
    int room;
    Stop stop[OFFICE_STOP_KINDS];
    char hide[16][32];
    int nhide;
    char swap[8][2][40];             /* material <- material */
    int nswap;
} EraLook;

static const Stop k_desk_stops[OFFICE_STOP_KINDS] = { { 13, 15 }, { 5, 7 }, { 1, 3 }, { 9, 11 }, { 17, 19 } };
static const Stop k_fo_stops[OFFICE_STOP_KINDS] = { { -1, 0 }, { 3, 5 }, { 3, 5 }, { 7, 9 }, { 3, 5 } };

static EraLook s_looks[OFFICE_LOOKS];
static const char *const k_look_name[OFFICE_LOOKS] = { "Auto", "1970s", "1980s", "1990s", "2000s", "2010s", "2020s" };

static struct {
    int init, log, ready_checked, ready;
    uint32_t mem, used, size;
    uint32_t camobj, camsave, slot[2];
    uint32_t w_ctx_office, w_ctx_skin, w_file_office, w_file_skin, w_room[ROOMS], w_amb;
    uint32_t w_tx_logo, w_tx_wall, w_tx_photos;
    uint32_t w_snd_cal, w_snd_fo, w_snd_gp, w_snd_stat, w_snd_out, w_snd_cursor;
    int loaded;                       /* both packages queued and waited for */
    int load_gen;                     /* bumps at each load: snapshots are retaken */
    uint32_t scenes[ROOMS];           /* looked up after the load */
    int snap_gen[ROOMS];
    uint32_t snap_node[ROOMS][MAX_NODES];
    uint32_t snap_m8[ROOMS][MAX_MATS], snap_m30[ROOMS][MAX_MATS];
    uint32_t scene;                   /* the room drawn now; 0 while covered */
    int room;
    float target, ret;
    int reverse;                      /* playing the dolly backwards to 0 */
    int pending;                      /* token for the menu when the camera arrives */
    int moving;
    int setting, live, look_changed;  /* setting (league.ini), live look (LT/RT) */
    float hold, held;                 /* NFL2K5_ERAS_OFFICE_HOLD: seconds at a stop before the push (tests) */
    uint32_t pad_prev[4];
    int amb_on;
} s_o;

#define OF_LOG(...) do { if (s_o.log) fprintf(stderr, "[ERAS] office " __VA_ARGS__); } while (0)

/* ---- guest helpers ------------------------------------------------------- */

static uint32_t o_alloc(uint32_t n, uint32_t align)
{
    uint32_t p = (s_o.mem + s_o.used + align - 1u) & ~(align - 1u), i;
    if (p + n > s_o.mem + s_o.size) return 0;
    s_o.used = p + n - s_o.mem;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static uint32_t o_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), p = o_alloc(2u * (n + 1u), 4), i;
    if (!p) return 0;
    for (i = 0; i <= n; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)s[i];
    return p;
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, RET_ADDR);
    fn();
    return g_eax;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* Case-insensitive compare of a guest UTF-16 string with ASCII. */
static int weq(uint32_t w, const char *s)
{
    uint32_t i;
    if (!w) return 0;
    for (i = 0; i < 80; i++) {
        uint16_t c = MEM16(w + 2u * i);
        if (tolower(c < 128 ? c : 0) != tolower((unsigned char)s[i])) return 0;
        if (!c) return 1;
    }
    return 0;
}

static void wtoa(uint32_t w, char *out, size_t n)
{
    size_t i;
    for (i = 0; w && i + 1 < n; i++) { uint16_t c = MEM16(w + 2u * i); if (!c) break; out[i] = c < 128 ? (char)c : '?'; }
    out[i] = 0;
}

static uint32_t lookup(uint32_t ctx, uint32_t fourcc, uint32_t wname)
{
    PUSH32(g_esp, wname);
    return gcall(sub_000449E0, ctx, fourcc);
}

static void sound(uint32_t w) { if (w) gcall(sub_0014D390, w, 0); }

static void scene_time(uint32_t scene, float t)
{
    if (!scene) return;
    PUSH32(g_esp, fbits(t));
    gcall(sub_0002F010, scene, 0);
}

static float scene_now(uint32_t scene) { return scene ? MEMF(scene + 4u) : 0.0f; }

/* ---- scene tables (runtime layout, ERAS-OFFICE-DESIGN 3 / SCENE 3.1) ------ */

static uint32_t find_rec(uint32_t scene, uint32_t cnt_off, uint32_t ptr_off, uint32_t stride, const char *name)
{
    uint32_t n = MEM32(scene + cnt_off), a = MEM32(scene + ptr_off), i;
    if (n > 256 || !a) return 0;
    for (i = 0; i < n; i++)
        if (weq(MEM32(a + i * stride), name)) return a + i * stride;
    return 0;
}

static uint32_t find_mat(uint32_t scene, const char *name) { return find_rec(scene, 0x1C, 0x20, 0x80, name); }
static uint32_t find_node(uint32_t scene, const char *name) { return find_rec(scene, 0x24, 0x28, 0x60, name); }
static uint32_t find_cam(uint32_t scene, const char *name)
{
    uint32_t r = find_rec(scene, 0x3C, 0x40, 0x60, name);
    if (!r && MEM32(scene + 0x3C)) r = MEM32(scene + 0x40);   /* the scene's only camera */
    return r;
}

static void snapshot(int room)
{
    uint32_t sc = s_o.scenes[room], i, n, a;
    if (!sc || s_o.snap_gen[room] == s_o.load_gen) return;
    n = MEM32(sc + 0x24); a = MEM32(sc + 0x28);
    for (i = 0; i < n && i < MAX_NODES; i++) s_o.snap_node[room][i] = MEM32(a + i * 0x60u + 0x0Cu);
    n = MEM32(sc + 0x1C); a = MEM32(sc + 0x20);
    for (i = 0; i < n && i < MAX_MATS; i++) {
        s_o.snap_m8[room][i] = MEM32(a + i * 0x80u + 0x08u);
        s_o.snap_m30[room][i] = MEM32(a + i * 0x80u + 0x30u);
    }
    s_o.snap_gen[room] = s_o.load_gen;
    OF_LOG("snapshot %s: scene %08X, %u nodes, %u materials, %u cameras\n", k_room[room], sc,
           MEM32(sc + 0x24), MEM32(sc + 0x1C), MEM32(sc + 0x3C));
}

static void restore(int room)
{
    uint32_t sc = s_o.scenes[room], i, n, a;
    if (!sc || s_o.snap_gen[room] != s_o.load_gen) return;
    n = MEM32(sc + 0x24); a = MEM32(sc + 0x28);
    for (i = 0; i < n && i < MAX_NODES; i++) MEM32(a + i * 0x60u + 0x0Cu) = s_o.snap_node[room][i];
    n = MEM32(sc + 0x1C); a = MEM32(sc + 0x20);
    for (i = 0; i < n && i < MAX_MATS; i++) {
        MEM32(a + i * 0x80u + 0x08u) = s_o.snap_m8[room][i];
        MEM32(a + i * 0x80u + 0x30u) = s_o.snap_m30[room][i];
    }
}

static void bind(uint32_t scene, const char *mat, uint32_t wtx)
{
    uint32_t m = find_mat(scene, mat), t;
    if (!m) return;
    t = lookup(s_o.w_ctx_skin, FOURCC_TXTR, wtx);
    if (!t) { OF_LOG("bind %s: TXTR not found in ERASSKIN\n", mat); return; }
    MEM32(m + 0x30) = t;
    MEM32(m + 0x08) &= ~1u;
}

/* ---- era looks ------------------------------------------------------------ */

static void look_defaults(void)
{
    static const struct { int room; const char *hide; const char *swap; } k[OFFICE_LOOKS] = {
        { ROOM_DESK, "", "" },                                                       /* Auto (unused) */
        { ROOM_DESK, "monitor_group1,teamwallpaper,monitor_shadow,mousepad,phone_group1", "vcSwShader16=vcHwShader27" },
        { ROOM_DESK80, "notebook_logo", "" },
        { ROOM_FO, "", "" },
        { ROOM_DESK, "", "" },
        { ROOM_DESK, "", "" },
        { ROOM_FO, "phone_group,phone_shadow", "" },
    };
    int l, s;
    for (l = 0; l < OFFICE_LOOKS; l++) {
        EraLook *e = &s_looks[l];
        const char *p;
        memset(e, 0, sizeof *e);
        snprintf(e->id, sizeof e->id, "%s", k_look_name[l]);
        e->room = k[l].room;
        for (s = 0; s < OFFICE_STOP_KINDS; s++) e->stop[s] = e->room == ROOM_FO ? k_fo_stops[s] : k_desk_stops[s];
        for (p = k[l].hide; *p && e->nhide < 16;) {
            size_t n = strcspn(p, ",");
            snprintf(e->hide[e->nhide++], 32, "%.*s", (int)n, p);
            p += n + (p[n] == ',');
        }
        if (k[l].swap[0]) {
            const char *eq = strchr(k[l].swap, '=');
            snprintf(e->swap[0][0], 40, "%.*s", (int)(eq - k[l].swap), k[l].swap);
            snprintf(e->swap[0][1], 40, "%s", eq + 1);
            e->nswap = 1;
        }
    }
    /* The 1970s den has no computer: the monitor rows go to the file cabinets
     * (the fifth keyed stop, unused by any retail row). */
    s_looks[OFFICE_LOOK_1970S].stop[OFFICE_STOP_STANDINGS].a = 17;
    s_looks[OFFICE_LOOK_1970S].stop[OFFICE_STOP_STANDINGS].b = 19;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

/* offices\<dir>\era.ini (nfl2k5_eras_path): [room] scene, [hide] nodes, [swap] a = b,
 * [stops] play / schedule / standings / team / settings = a,b. Each key
 * present replaces the built-in value. */
static int look_ini(EraLook *e, const char *dir)
{
    char path[MAX_PATH], rel[64], line[256], sect[16] = "";
    FILE *f;
    int swaps_reset = 0, hides_reset = 0;
    snprintf(rel, sizeof rel, "offices\\%s\\era.ini", dir);
    nfl2k5_eras_path(rel, path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *k, *v, *c = strchr(line, ';');
        if (c) *c = 0;
        s = trim(s);
        if (!*s) continue;
        if (*s == '[') { char *r = strchr(s, ']'); if (r) *r = 0; snprintf(sect, sizeof sect, "%s", s + 1); continue; }
        if (!(eq = strchr(s, '='))) continue;
        *eq = 0; k = trim(s); v = trim(eq + 1);
        if (!_stricmp(sect, "room") && !_stricmp(k, "scene")) e->room = !_stricmp(v, "front_office") ? ROOM_FO : !_stricmp(v, "coach_d80s") ? ROOM_DESK80 : ROOM_DESK;
        else if (!_stricmp(sect, "hide") && !_stricmp(k, "nodes")) {
            if (!hides_reset) { e->nhide = 0; hides_reset = 1; }
            while (*v && e->nhide < 16) {
                size_t n = strcspn(v, ",");
                char t[32];
                snprintf(t, sizeof t, "%.*s", (int)n, v);
                snprintf(e->hide[e->nhide], 32, "%s", trim(t));
                if (e->hide[e->nhide][0]) e->nhide++;
                v += n + (v[n] == ',');
            }
        } else if (!_stricmp(sect, "swap")) {
            if (!swaps_reset) { e->nswap = 0; swaps_reset = 1; }
            if (e->nswap < 8 && *v) {
                snprintf(e->swap[e->nswap][0], 40, "%s", k);
                snprintf(e->swap[e->nswap][1], 40, "%s", v);
                e->nswap++;
            }
        } else if (!_stricmp(sect, "stops")) {
            static const char *const names[OFFICE_STOP_KINDS] = { "play", "schedule", "standings", "team", "settings" };
            int i;
            float a, b;
            for (i = 0; i < OFFICE_STOP_KINDS; i++)
                if (!_stricmp(k, names[i]) && sscanf(v, "%f , %f", &a, &b) == 2) { e->stop[i].a = a; e->stop[i].b = b; }
        }
    }
    fclose(f);
    return 1;
}

/* Auto: the league season's decade (decision 2: the 1980 slice uses the 1980s office). */
int nfl2k5_eras_office_auto_look(void)
{
    int y = NFL2K5_ERAS_SEASON;
    return y < 1980 ? OFFICE_LOOK_1970S : y < 1990 ? OFFICE_LOOK_1980S : y < 2000 ? OFFICE_LOOK_1990S
         : y < 2010 ? OFFICE_LOOK_2000S : y < 2020 ? OFFICE_LOOK_2010S : OFFICE_LOOK_2020S;
}

int nfl2k5_eras_office_look(void)
{
    int l = s_o.live ? s_o.live : s_o.setting;
    if (l <= OFFICE_LOOK_AUTO || l >= OFFICE_LOOKS) l = nfl2k5_eras_office_auto_look();
    return l;
}

const char *nfl2k5_eras_office_look_name(int look) { return look >= 0 && look < OFFICE_LOOKS ? k_look_name[look] : "?"; }

/* The effective record: built-in, then the decade's era.ini, then (Auto only)
 * the exact season's. */
static const EraLook *current_look(void)
{
    static EraLook e;
    int l = nfl2k5_eras_office_look();
    char season[8];
    e = s_looks[l];
    look_ini(&e, k_look_name[l]);
    if (!s_o.live && s_o.setting == OFFICE_LOOK_AUTO) {
        snprintf(season, sizeof season, "%d", NFL2K5_ERAS_SEASON);
        look_ini(&e, season);
    }
    /* the reshaped 1980s desk is optional (needs the Mod Tools when the pack is built) */
    if (e.room == ROOM_DESK80 && s_o.loaded && !s_o.scenes[ROOM_DESK80]) e.room = ROOM_DESK;
    return &e;
}

static void texture_context(int on)
{
    char season[8];
    int l = nfl2k5_eras_office_look();
    snprintf(season, sizeof season, "%d", NFL2K5_ERAS_SEASON);
    if (!on) g_eras_svc->texpack_set_era(NULL, NULL);
    else g_eras_svc->texpack_set_era(!s_o.live && s_o.setting == OFFICE_LOOK_AUTO ? season : NULL, k_look_name[l]);
}

/* Restore the room, then hide, swap, bind; returns the room. */
static int apply_look(void)
{
    const EraLook *e = current_look();
    uint32_t sc = s_o.scenes[e->room];
    int i, hidden = 0, swapped = 0;
    if (!sc) { OF_LOG("look %s: scene %s not loaded\n", e->id, k_room[e->room]); return -1; }
    snapshot(e->room);
    restore(e->room);
    for (i = 0; i < e->nhide; i++) {
        uint32_t n = find_node(sc, e->hide[i]);
        if (n) { MEM32(n + 0x0C) |= 0x10u; hidden++; }
        else OF_LOG("look %s: no node %s in %s\n", e->id, e->hide[i], k_room[e->room]);
    }
    for (i = 0; i < e->nswap; i++) {
        uint32_t to = find_mat(sc, e->swap[i][0]), from = find_mat(sc, e->swap[i][1]);
        if (to && from && MEM32(from + 0x30)) {
            MEM32(to + 0x30) = MEM32(from + 0x30);
            MEM32(to + 0x08) &= ~1u;
            swapped++;
        } else OF_LOG("look %s: swap %s <- %s: material missing\n", e->id, e->swap[i][0], e->swap[i][1]);
    }
    bind(sc, "teamlogo", s_o.w_tx_logo);
    if (e->room != ROOM_FO) bind(sc, "wallphoto", s_o.w_tx_wall);
    else bind(sc, "office_photos", s_o.w_tx_photos);
    texture_context(1);
    OF_LOG("look %s: room %s, %d of %d nodes hidden, %d of %d swaps\n", e->id, k_room[e->room],
           hidden, e->nhide, swapped, e->nswap);
    return e->room;
}

/* ---- package ---------------------------------------------------------------- */

int nfl2k5_eras_office_init(void)
{
    if (s_o.init) return s_o.mem ? 0 : -1;
    s_o.init = 1;
    { const char *e = getenv("NFL2K5_ERAS_LOG"); s_o.log = e && *e && *e != '0'; }
    { const char *e = getenv("NFL2K5_ERAS_OFFICE_HOLD"); s_o.hold = e ? (float)atof(e) : 0.0f; }
    s_o.size = 16u * 1024u;
    s_o.mem = xbox_HeapAlloc(s_o.size, 16);
    if (!s_o.mem) return -1;
    s_o.camobj = o_alloc(0x2C0, 16);
    s_o.camsave = o_alloc(0x2C0, 16);
    s_o.slot[0] = o_alloc(0x40, 16);
    s_o.slot[1] = o_alloc(0x40, 16);
    s_o.w_ctx_office = o_wstr("ERASOFFICE");
    s_o.w_ctx_skin = o_wstr("ERASSKIN");
    s_o.w_file_office = o_wstr("erasoffice.iff");
    s_o.w_file_skin = o_wstr("erasskin.iff");
    s_o.w_room[ROOM_DESK] = o_wstr(k_room[ROOM_DESK]);
    s_o.w_room[ROOM_FO] = o_wstr(k_room[ROOM_FO]);
    s_o.w_room[ROOM_DESK80] = o_wstr(k_room[ROOM_DESK80]);
    s_o.w_amb = o_wstr("coachambience");
    s_o.w_tx_logo = o_wstr("31_teamlogo_00_h0");
    s_o.w_tx_wall = o_wstr("wallphoto");
    s_o.w_tx_photos = o_wstr("office_photos");
    s_o.w_snd_cal = o_wstr("Ambient: Calendar In");
    s_o.w_snd_fo = o_wstr("Ambient: Front Office In");
    s_o.w_snd_gp = o_wstr("Ambient: Gameplan In");
    s_o.w_snd_stat = o_wstr("Ambient: Statistics In");
    s_o.w_snd_out = o_wstr("Ambient: Statistics Out");
    s_o.w_snd_cursor = o_wstr("Ambient: Menu Move Cursor");
    look_defaults();
    if (!s_o.w_snd_cursor) { s_o.mem = 0; return -1; }
    return 0;
}

/* The files are in pack G when the archive index has their names (the
 * extra pack only gets the office\files of the Eras roots at boot). */
int nfl2k5_eras_office_ready(void)
{
    if (!s_o.ready_checked) {
        s_o.ready_checked = 1;
        s_o.ready = nfl2k5_eras_exists("office\\files\\erasoffice.iff") && nfl2k5_eras_exists("office\\files\\erasskin.iff");
        if (!s_o.ready) {
            char p[MAX_PATH];
            fprintf(stderr, "[ERAS] office: %s / erasskin.iff missing (build the Eras assets: "
                            "tools\\eras\\build_assets.py, or NFL2K5-ErasBuilder.exe); the office shows without its room\n",
                    nfl2k5_eras_path("office\\files\\erasoffice.iff", p, sizeof p));
        }
    }
    return s_o.ready && s_o.mem;
}

static void queue(uint32_t ctx, uint32_t file, uint32_t slot)
{
    uint32_t heap;
    PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    heap = gcall(sub_00038FB0, 0, 0);
    PUSH32(g_esp, heap);
    PUSH32(g_esp, slot);
    gcall(sub_00043F50, ctx, file);
}

int nfl2k5_eras_office_load(void)
{
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    int r;
    DWORD t0;
    if (!nfl2k5_eras_office_ready()) return -1;
    if (s_o.loaded) return 0;
    t0 = GetTickCount();
    queue(s_o.w_ctx_office, s_o.w_file_office, s_o.slot[0]);
    queue(s_o.w_ctx_skin, s_o.w_file_skin, s_o.slot[1]);
    gcall(sub_000432D0, 0, 0);
    for (r = 0; r < ROOMS; r++) s_o.scenes[r] = lookup(s_o.w_ctx_office, FOURCC_SCNE, s_o.w_room[r]);
    s_o.loaded = 1;
    s_o.load_gen++;
    {
        uint32_t lay = lookup(s_o.w_ctx_office, FOURCC_LAYT, s_o.w_room[ROOM_FO]);
        uint32_t logo = lookup(s_o.w_ctx_skin, FOURCC_TXTR, s_o.w_tx_logo);
        uint32_t wall = lookup(s_o.w_ctx_skin, FOURCC_TXTR, s_o.w_tx_wall);
        fprintf(stderr, "[ERAS] office load: ERASOFFICE coach_desk %08X (%u materials) front_office %08X (%u materials), "
                "LAYT front_office %08X; ERASSKIN logo %08X wallphoto %08X; %lu ms\n",
                s_o.scenes[0], s_o.scenes[0] ? MEM32(s_o.scenes[0] + 0x1C) : 0u,
                s_o.scenes[1], s_o.scenes[1] ? MEM32(s_o.scenes[1] + 0x1C) : 0u, lay, logo, wall,
                (unsigned long)(GetTickCount() - t0));
    }
    if (s_o.scenes[0]) snapshot(ROOM_DESK);
    if (s_o.scenes[1]) snapshot(ROOM_FO);
    if (s_o.scenes[2]) snapshot(ROOM_DESK80);
    r = s_o.scenes[0] || s_o.scenes[1] ? 0 : -2;
    if (r) {                                         /* not in the archive (pack G not written): release the contexts */
        gcall(sub_000432F0, s_o.w_ctx_skin, 0);
        gcall(sub_000432F0, s_o.w_ctx_office, 0);
        s_o.loaded = 0;
    }
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    return r;
}

void nfl2k5_eras_office_unload(void)
{
    int r;
    if (!s_o.loaded) return;
    for (r = 0; r < ROOMS; r++) restore(r);
    gcall(sub_000432F0, s_o.w_ctx_skin, 0);
    gcall(sub_000432F0, s_o.w_ctx_office, 0);
    s_o.loaded = 0;
    s_o.scene = 0;
    memset(s_o.scenes, 0, sizeof s_o.scenes);
    OF_LOG("unloaded ERASSKIN, ERASOFFICE\n");
}

/* ---- camera -------------------------------------------------------------- */

static void move(float a, float b)
{
    s_o.ret = b;
    s_o.target = a;
    s_o.reverse = 0;
    if (!s_o.scene) return;
    if (a <= 0.0f) { scene_time(s_o.scene, 0.0f); return; }
    scene_time(s_o.scene, a - 1.0f);
    if (b > 0.0f) s_o.target = a + 0.4f;
}

int nfl2k5_eras_office_go(int kind, int token)
{
    const EraLook *e;
    Stop st;
    static const int snd_kind[OFFICE_STOP_KINDS] = { 1, 0, 3, 2, 3 };
    uint32_t snd;
    if (!s_o.scene || kind < 0 || kind >= OFFICE_STOP_KINDS) return 0;
    e = current_look();
    st = e->stop[kind];
    {
        uint32_t snds[4];
        snds[0] = s_o.w_snd_cal; snds[1] = s_o.w_snd_fo; snds[2] = s_o.w_snd_gp; snds[3] = s_o.w_snd_stat;
        snd = snds[snd_kind[kind]];
    }
    sound(snd);
    if (st.a < 0.0f && s_o.room == ROOM_FO) {
        /* out of the front office: the entrance dolly backwards (1 -> 0) */
        s_o.reverse = 1;
        s_o.target = 0.0f;
        s_o.ret = 1.0f;
        if (scene_now(s_o.scene) > 1.0f) scene_time(s_o.scene, 1.0f);
    } else if (st.a > 0.0f) move(st.a, st.b);
    else { s_o.pending = token; return 1; }           /* no move: arrives on the next frame */
    s_o.pending = token;
    s_o.moving = 1;
    OF_LOG("row kind %d: %s %.1f -> %.1f (stop %.0f,%.0f), token %d\n", kind, s_o.reverse ? "reverse" : "move",
           scene_now(s_o.scene), s_o.target, st.a, st.b, token);
    return 1;
}

int nfl2k5_eras_office_busy(void) { return s_o.moving; }

void nfl2k5_eras_office_return(void)
{
    if (!s_o.scene) return;
    if (s_o.room == ROOM_FO && s_o.ret == 1.0f) move(1.0f, 0.0f);
    else move(s_o.ret, 0.0f);
    s_o.moving = 1;
}

/* ---- messages ------------------------------------------------------------- */

void nfl2k5_eras_office_create(uint32_t fsm)
{
    (void)fsm;
    if (nfl2k5_eras_office_load() == 0) gcall(sub_00066B20, s_o.camobj, 0);
    s_o.target = 0.0f;
    s_o.pending = 0;
    s_o.moving = 0;
    s_o.live = 0;
    s_o.scene = 0;
    s_o.room = current_look()->room;
    s_o.ret = s_o.room == ROOM_FO ? 1.0f : 0.0f;    /* the front office starts with its entrance dolly */
    s_o.reverse = 0;
    memset(s_o.pad_prev, 0, sizeof s_o.pad_prev);
    OF_LOG("create: look %s (setting %s), room %s\n", k_look_name[nfl2k5_eras_office_look()],
           k_look_name[s_o.setting], k_room[s_o.room]);
}

/* A match is launched: free the room first. Keeping it loaded under the match, as
 * Franchise keeps its desk (ERAS-OFFICE-DESIGN 1.7, INFERRED), left the match
 * short of memory: the pre-game load spun in sub_004262F0 (logs/eras/o4, and the
 * stadium agent's colv7 on the same build). */
static int s_freed;
void nfl2k5_eras_office_free_for_match(void)
{
    nfl2k5_eras_office_leaving();
    if (!s_o.loaded) return;
    nfl2k5_eras_office_unload();
    s_o.scene = 0;
    s_freed = 1;
    fprintf(stderr, "[ERAS] office: packages freed for the match\n");
}

/* Back from the match (ERAS_RETURN, or the office's msg 3): load the room again
 * before the handler resolves the office's LAYT. */
void nfl2k5_eras_office_back_from_match(void)
{
    if (!s_freed) return;
    s_freed = 0;
    if (nfl2k5_eras_office_load() == 0) gcall(sub_00066B20, s_o.camobj, 0);
    fprintf(stderr, "[ERAS] office: packages loaded again after the match (%s)\n", s_o.loaded ? "ok" : "FAILED");
}

void nfl2k5_eras_office_refresh(uint32_t fsm)
{
    (void)fsm;
    nfl2k5_eras_office_back_from_match();
    if (!s_o.loaded || !nfl2k5_eras_office_ready()) return;
    /* after a match: if the packages did not survive, load them again here,
     * before the handler shows the screen */
    if (!lookup(s_o.w_ctx_office, FOURCC_SCNE, s_o.w_room[ROOM_DESK])) {
        fprintf(stderr, "[ERAS] office: ERASOFFICE no longer loaded; reloading\n");
        s_o.loaded = 0;
        nfl2k5_eras_office_load();
        gcall(sub_00066B20, s_o.camobj, 0);
    }
}

static void ambience(int on)
{
    if (on && !s_o.amb_on) {
        PUSH32(g_esp, fbits(40.0f));
        PUSH32(g_esp, fbits(20.0f));
        gcall(sub_00165BE0, s_o.w_amb, 0);
        s_o.amb_on = 1;
    } else if (!on && s_o.amb_on) {
        gcall(sub_00165C10, 0, 0);
        s_o.amb_on = 0;
    }
}

void nfl2k5_eras_office_shown(uint32_t fsm)
{
    int room;
    if (!s_o.loaded) { OF_LOG("shown: no room (package not loaded)\n"); return; }
    room = apply_look();
    if (room < 0) return;
    if (room != s_o.room) s_o.ret = room == ROOM_FO ? 1.0f : 0.0f;
    s_o.room = room;
    s_o.scene = s_o.scenes[room];
    ambience(1);
    gcall(sub_0014FCD0, fsm, 0);                     /* rows drawn by the layout's nav bar */
    s_o.pending = 0;
    move(s_o.ret, 0.0f);
    s_o.moving = s_o.target > scene_now(s_o.scene);
    OF_LOG("shown: room %s, time %.2f -> %.2f\n", k_room[room], scene_now(s_o.scene), s_o.target);
}

void nfl2k5_eras_office_covered(void)
{
    s_o.scene = 0;
    s_o.moving = 0;
    gcall(sub_0014FC30, 0, 0);
    texture_context(0);
}

static void browse(int dir)
{
    int l = nfl2k5_eras_office_look() + dir, room;
    if (l < OFFICE_LOOK_1970S) l = OFFICE_LOOKS - 1;
    if (l >= OFFICE_LOOKS) l = OFFICE_LOOK_1970S;
    s_o.live = l;
    s_o.look_changed = 1;
    room = apply_look();
    if (room < 0) return;
    sound(s_o.w_snd_cursor);
    if (room != s_o.room) {
        s_o.room = room;
        s_o.scene = s_o.scenes[room];
        s_o.ret = room == ROOM_FO ? 1.0f : 0.0f;
        move(s_o.ret, 0.0f);                          /* into the front office with its dolly */
        s_o.moving = s_o.target > scene_now(s_o.scene);
    }
    OF_LOG("browse %+d: look %s\n", dir, k_look_name[l]);
}

int nfl2k5_eras_office_frame(uint32_t fsm)
{
    float t, dt;
    int p, i;
    if (!s_o.scene) return 0;
    dt = MEMF(fsm + FSM_DT);
    if (!(dt > 0.0f && dt < 1.0f)) dt = 1.0f / 60.0f;
    t = scene_now(s_o.scene);
    if (s_o.reverse) {
        gcall(sub_0014FC40, 0, 0);
        t -= dt;
        if (t <= 0.0f) { t = 0.0f; s_o.reverse = 0; }
        scene_time(s_o.scene, t);
        if (s_o.reverse) return 0;
    } else if (t < s_o.target) {
        PUSH32(g_esp, fbits(dt * (s_o.target == 1.0f ? 1.0f : SPEED)));
        gcall(sub_0014FC40, 0, 0);
        gcall(sub_0002F030, s_o.scene, 0);
        return 0;
    }
    if (s_o.pending && s_o.hold > 0.0f && s_o.held < s_o.hold) {
        /* test switch: stay at the stop (input still off) so a 1 fps capture sees it */
        s_o.held += dt;
        gcall(sub_0014FC40, 0, 0);
        return 0;
    }
    s_o.held = 0.0f;
    gcall(sub_0014FC30, 0, 0);
    s_o.moving = 0;
    if (s_o.pending) {
        if (!s_o.reverse && s_o.target > 0.0f) scene_time(s_o.scene, s_o.target);
        p = s_o.pending;
        s_o.pending = 0;
        OF_LOG("arrived at %.2f: token %d\n", scene_now(s_o.scene), p);
        return p;
    }
    /* LT / RT on any pad: browse the eras' offices (this session only) */
    for (i = 0; i < 4; i++) {
        uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u), edge = w & ~s_o.pad_prev[i];
        if (w != s_o.pad_prev[i] && w) OF_LOG("pad %d button word %08X\n", i, w);
        s_o.pad_prev[i] = w;
        if (edge & BTN_LT) { browse(-1); break; }
        if (edge & BTN_RT) { browse(+1); break; }
    }
    return 0;
}

void nfl2k5_eras_office_draw(uint32_t fsm)
{
    uint32_t cam;
    (void)fsm;
    if (!s_o.scene) return;
    cam = find_cam(s_o.scene, k_cam[s_o.room]);
    if (!cam) return;
    gcall(sub_0002AD80, s_o.camsave, 0);
    gcall(sub_0013E710, s_o.camobj, cam);
    gcall(sub_0002AC80, s_o.camobj, 0);
    gcall(sub_0002DE60, s_o.scene, 0);
    gcall(sub_0002AC80, s_o.camsave, 0);
}

void nfl2k5_eras_office_back(void) { sound(s_o.w_snd_out); }

void nfl2k5_eras_office_leaving(void)
{
    texture_context(0);
    ambience(0);
    gcall(sub_0014FC30, 0, 0);
}

void nfl2k5_eras_office_destroy(void)
{
    ambience(0);
    texture_context(0);
    gcall(sub_0014FC30, 0, 0);
    nfl2k5_eras_office_unload();
    s_o.live = 0;
    s_o.moving = 0;
    s_o.pending = 0;
}

void nfl2k5_eras_office_set_look(int setting)
{
    if (setting < 0 || setting >= OFFICE_LOOKS) setting = OFFICE_LOOK_AUTO;
    s_o.setting = setting;
    s_o.live = 0;
}

int nfl2k5_eras_office_look_changed(void)
{
    int c = s_o.look_changed;
    s_o.look_changed = 0;
    return c;
}

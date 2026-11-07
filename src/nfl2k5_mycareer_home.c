/* MyCareer home hub: our own layer on top of SOFTDRINK's MyCareer (prototype, branch
 * mycareer-home; not for release).
 *
 * SOFTDRINK's Career Home (descriptor 0x014F27D0, rows at 0x014BC0A0) is a plain list.
 * This puts a 3D home in front of it without changing a byte of the pack:
 *
 *  - One interpreter hook at 0x014EA768 (the pack's screen handler entry; the pack's
 *    code is always interpreted). On msg 6 with Career Home on top it pushes our screen.
 *  - Our screens are served from 0xFED10000. The room is the retail Crib `room` + sky SCNE,
 *    loaded from crib.iff under our own context L"MCHOME" and always looked up with it, so
 *    the game's Crib state (0xAC7440.., the VIP Crib record, its save) is never touched.
 *  - The Crib's camera is code, not curves (sub_00275B70): look-at (sub_0002BDF0) from an
 *    eye and a target, FOV (sub_0002B860). We do the same with our own stops.
 *  - The home has no list: hotspots (door, mirror, computer, phone, career menu), D-pad
 *    between them, the game's own A-button glyph over the one selected, prompts in the
 *    game's text tokens. The screen runs on the Crib's handler (0xF3DA0: events only).
 *  - Door, computer and phone open small native lists over the room (Front Office layout
 *    from fr.iff under our own context).
 *  - Nothing of the pack is reimplemented: a choice glides to its object, pops back to Career
 *    Home and the next Career Home frame tail-jumps into the pack's own row action with
 *    ecx = FSM, the call the list dispatcher makes. Door = 0x014E93D1, the pack's next game.
 *  - The mirror is the game's own edit-mode pages (0x56F410 body, 0x56F230 equipment, or the
 *    real-face pair), cloned only to stop before Position and ratings.
 *  - The phone: calls to the agent and the coach on a call screen drawn like the Crib's
 *    (sub_00274F30: portrait panel, name, wrapped text, choice bar). What was asked is kept
 *    in our own save file (see career_*), never in the pack's.
 *  - Pay and homes: the record's contract value per game week into a bank in our file;
 *    the apartment, the house and the Crib are three framings and looks of the one scene
 *    (hidden nodes, material swaps, camera stops), bought from the computer.
 *
 * Inert unless the pack's MCHOMEX1 block and the rows are exactly where this expects them.
 * NFL2K5_MC_HOME=0 turns it off. Test switches: NFL2K5_MC_HOME_LOG, _CAMFILE, _HIDE, _SWAP,
 * _MARKER=3d (the old 3D cursor scene), _GIFT=<dollars> (money once, to test buying). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "interp/x86_interp.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define MH_FN_BASE    0xFED10000u          /* + scr * 0x100 + msg * 4; + 0x800 + scr * 0x40 + row * 4 */
#define MH_RET        0xFED1FFF0u
#define MH_ROW        0x34u
#define FEATURES      0x00525830u          /* link-list template (handler 0xF3E90, layout navigation) */
#define SND_SELECT    0x00E7FD70u          /* L"Ambient: Menu Select" */
#define SND_CURSOR    0x00E7FD3Cu
#define FOURCC_SCNE   0x454E4353u
#define FOURCC_LAYT   0x5459414Cu
#define FOURCC_TXTR   0x52545854u
#define FSM_DT        0x104u

/* SOFTDRINK v0.9 (checked at install) */
#define SD_MAGIC      0x014BC000u          /* "MCHOMEX1" */
#define SD_HOME       0x014F27D0u          /* Career Home descriptor */
#define SD_HANDLER    0x014EA768u          /* the pack's screen handler */
#define SD_HANDLER_RET 0x014EA792u         /* a plain ret inside it */
#define SD_ROWS       0x014BC0A0u          /* Career Home rows */
#define SD_ACT_NEXT   0x014E93D1u          /* row 0: the next game */
#define SD_ACT_SCHED  0x014BD587u          /* row 1: Season Schedule */
#define SD_ACT_PLAYER 0x014E94DBu          /* row 3: MyPlayer */
#define SD_ACT_PRACTICE 0x014E9452u        /* row 2: Practice */
#define SD_PLAYER     0x014F30B0u          /* the pack's pointer to MyPlayer's record */
#define CRIB_DESC     0x00524FA0u          /* template: events-only handler 0xF3DA0 */
#define PAD_WORDS     0x00B37A78u          /* sub_00070A10: port * 0x244 */
#define BTN_A         0x00000100u          /* sub_00277570 */
#define BTN_B         0x00000220u
#define BTN_RIGHT     0x08000000u
#define BTN_LEFT      0x04000000u
#define BTN_DOWN      0x02000000u
#define BTN_UP        0x01000000u
#define REC_BYTES     0x54u
#define STR_CHARS     96u

/* our own row actions (the rest are the pack's) */
#define ACT_INFO      1u
#define ACT_BUY       2u
#define ACT_AGENT     3u
#define ACT_COACH     4u

enum { HS_DOOR, HS_MIRROR, HS_COMPUTER, HS_PHONE, HS_MENU, HOTS };
enum { SCR_HUB, SCR_DOOR, SCR_PC, SCR_PHONE, SCR_CALL, SCREENS };
static const char *const k_tier_name[3] = { "Apartment", "House", "The Crib" };
static const long long k_price[3] = { 0, 250000, 1000000 };
static const char *const k_hot_name[HOTS] = { "Front Door", "Mirror", "Computer", "Phone", "Career Menu" };
static const char *const k_hot_what[HOTS] = { "Game day or practice", "Change your look", "Stats, schedule, bank, homes",
                                              "Call your agent or coach", "The full MyCareer list" };
/* list rows: label, action (the pack's, or one of ours), link = a screen pushed over the room */
static const struct { const char *label; uint32_t action, link; } k_rows[SCREENS][5] = {
    { { 0 } },
    { { "Play Next Game", SD_ACT_NEXT, 0 }, { "Practice", SD_ACT_PRACTICE, 0 }, { 0 } },
    { { "My Stats", SD_ACT_PLAYER, 0 }, { "Schedule", SD_ACT_SCHED, 0 }, { "Bank", ACT_INFO, 0 }, { "Buy", ACT_BUY, 0 }, { 0 } },
    { { "Call My Agent", ACT_AGENT, 0 }, { "Call The Coach", ACT_COACH, 0 }, { 0 } },
    { { 0 } },
};
#define PC_ROW_BANK 2
#define PC_ROW_BUY  3

typedef struct { float eye[3], at[3], fov, mark[3]; } Stop;
/* Room space is the Crib's (cm, y up). mark = where the A glyph floats. A choice dollies
 * part of the way towards `at`. One set of stops per home. */
static Stop s_stops[3][HOTS] = {
    {   /* the apartment: the fireplace and bar corner */
        { { 2100.0f, 150.0f, -1750.0f }, {  200.0f, 100.0f, -2150.0f }, 58.0f, {  500.0f, 240.0f, -2100.0f } },
        { { 1500.0f, 130.0f, -1900.0f }, {  910.0f,  40.0f, -2225.0f }, 50.0f, {  910.0f, 185.0f, -2225.0f } },
        { { 2100.0f, 150.0f, -1750.0f }, { 2520.0f, 130.0f, -1410.0f }, 58.0f, { 2440.0f, 225.0f, -1560.0f } },
        { { 2000.0f, 140.0f, -1550.0f }, { 2560.0f,  20.0f, -1150.0f }, 50.0f, { 2560.0f, 110.0f, -1150.0f } },
        { { 2100.0f, 150.0f, -1750.0f }, { 3510.0f,  30.0f, -1890.0f }, 58.0f, { 3510.0f, 200.0f, -1890.0f } },
    },
    {   /* the house: the lobby and the living room */
        { {  260.0f, 150.0f,  -950.0f }, {  260.0f,  90.0f,   327.0f }, 58.0f, {    0.0f, 300.0f,  320.0f } },
        { { 1500.0f, 130.0f, -1900.0f }, {  910.0f,  40.0f, -2225.0f }, 50.0f, {  910.0f, 185.0f, -2225.0f } },
        { { 1500.0f, 150.0f,  -700.0f }, { 2500.0f, 100.0f, -1300.0f }, 58.0f, { 2500.0f, 250.0f, -1400.0f } },
        { { 2000.0f, 140.0f, -1550.0f }, { 2560.0f,  20.0f, -1150.0f }, 50.0f, { 2560.0f, 110.0f, -1150.0f } },
        { { -400.0f, 170.0f,  -300.0f }, {-1500.0f, 300.0f, -1400.0f }, 60.0f, {-1200.0f, 300.0f, -1100.0f } },
    },
    {   /* the Crib: the whole place */
        { {    0.0f, 170.0f, -1400.0f }, {    0.0f,  90.0f,   327.0f }, 62.0f, {    0.0f, 300.0f,  320.0f } },
        { { 1500.0f, 130.0f, -1900.0f }, {  910.0f,  40.0f, -2225.0f }, 50.0f, {  910.0f, 185.0f, -2225.0f } },
        { {  900.0f, 200.0f, -1800.0f }, { 2500.0f, 120.0f, -1000.0f }, 62.0f, { 2500.0f, 250.0f, -1400.0f } },
        { { 2000.0f, 140.0f, -1550.0f }, { 2560.0f,  20.0f, -1150.0f }, 50.0f, { 2560.0f, 110.0f, -1150.0f } },
        { { 2600.0f, 150.0f, -1700.0f }, { 2000.0f, 100.0f, -2600.0f }, 62.0f, { 2000.0f, 250.0f, -2600.0f } },
    },
};
#define STOP(h) (s_stops[s_c.tier][h])

/* The looks. Collectibles whose textures stream through the Crib's item cache stay hidden
 * (they would draw untextured); the rest is the tier. */
static const char k_hide_items[] =
    "poster_p01,poster_p02,poster_p03,poster_p04,poster_p05,poster_p06,poster_p07,poster_p08,poster_p09,"
    "poster_p10,poster_p11,poster_p12,poster_p13,banner_v_p01,banner_v_p02,banner_v_p03,banner_v_p04,"
    "banner_h_p01,banner_h_p02,banner_h_p03,standee_p01,standee_p03,standee_p04,license_01,"
    "sign_interstate_01,sign_parking_01,sign_parking_02,sign_parking_03,sign_street_01,pennant_p01,"
    "pennant_p02,pennant_p03,pennant_p04,wall_art_player_p01,wall_art_player_p02";
static const char *const k_hide_tier[3] = {
    ",bobblestands,bobblelogos,z_plant_palm,wall_art_generic_p01,jukebox,bar_glass",
    ",bobblestands,bobblelogos,jukebox",
    "",
};
static const char k_swap_base[] =
    "floor_carpet_01=concrete_m_01,floor_carpet_02=concrete_m_01,floor_carpet_03=concrete_m_01,"
    "floor_carpet_04=concrete_m_01,floor_tile_01=concrete_m_01";
static const char *const k_swap_tier[3] = {
    ",floor_wood_01=concrete_m_01,floor_wood_02=concrete_m_01,floor_wood_03=concrete_m_01,floor_wood_04=concrete_m_01,"
    "rug_m_01=concrete_m_01,rug_m_02=concrete_m_01,rug_m_03=concrete_m_01,rug_m_04=concrete_m_01,trim_dark=concrete_m_01",
    ",floor_wood_01=wood_cherry,floor_wood_02=wood_cherry,floor_wood_03=wood_cherry,floor_wood_04=wood_cherry,"
    "rug_m_01=wood_type_01,rug_m_02=wood_type_01,rug_m_03=wood_type_01,rug_m_04=wood_type_01,trim_dark=wood_cherry",
    ",floor_wood_01=wood_type_01,floor_wood_02=wood_type_01,floor_wood_03=wood_type_01,floor_wood_04=wood_type_01,"
    "rug_m_01=wood_cherry,rug_m_02=wood_cherry,rug_m_03=wood_cherry,rug_m_04=wood_cherry,trim_dark=wood_cherry",
};

static struct {
    int on, log;
    uint32_t mem, used, size;
    uint32_t header[SCREENS], title, w_name[HOTS], w_what[HOTS], w_prompt, w_prompt_menu, w_glyph;
    uint32_t rowlab[SCREENS][5];            /* the lists' label buffers */
    uint32_t mirror_body, mirror_equip;     /* our clones, by face route */
    uint32_t mirror_body_real, mirror_equip_real;
    uint32_t w_cursor, cursor, mtx, w_phone, phone, w_phone_icon, w_reflection;
    uint32_t pad_prev[4];
    int hot, marker3d;
    int top;                                /* our screen on top */
    uint8_t rec_before[REC_BYTES];
    uint32_t rec;                           /* being edited: diffed when Career Home returns */
    uint32_t camobj, camsave, slot, slot2, eye, at, light, vp;
    uint32_t w_ctx, w_file, w_room, w_sky_day, w_ctx2, w_file2, w_layout;
    int loaded, slim;
    char camfile[260];
    uint32_t room, sky;
    int shown;                     /* our screen is top: draw */
    float cur[7];                  /* eye, at, fov now */
    int cur_set;
    int leaving;                   /* choice made: dolly, then pop */
    float dolly;
    uint32_t action;               /* run on Career Home's next frame */
    int cool;                      /* frames with no re-push after a row's action */
    int manual;                    /* Career Menu: stay on the pack's list */
    char hide[2048], swap[2048];
    int hide_env, swap_env;
    float anim;                    /* seconds, for the glyph's bob and the call screen */
    /* the call screen */
    int who, phase, pick, nopts;
    float call_t;
    char reply[320];
    float proj_scale, proj_xsign, phone_at[3];
    uint32_t wstr[16];             /* scratch UTF-16 buffers for drawn text */
} s_h;

#define MH_LOG(...) do { if (s_h.log) { fprintf(stderr, "[MCHOME] " __VA_ARGS__); fflush(stderr); } } while (0)

/* ---- guest helpers (as src/nfl2k5_eras_office.c) -------------------------- */

static uint32_t h_alloc(uint32_t n, uint32_t align)
{
    uint32_t p = (s_h.mem + s_h.used + align - 1u) & ~(align - 1u), i;
    if (p + n > s_h.mem + s_h.size) return 0;
    s_h.used = p + n - s_h.mem;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static void h_put(uint32_t p, const char *s)
{
    uint32_t i;
    if (!p) return;
    for (i = 0; s[i] && i + 1 < STR_CHARS; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)s[i];
    MEM16(p + 2u * i) = 0;
}

static uint32_t h_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), p = h_alloc(2u * (n + 1u), 4), i;
    if (!p) return 0;
    for (i = 0; i <= n; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)s[i];
    return p;
}

static uint32_t h_buf(const char *s)
{
    uint32_t p = h_alloc(2u * STR_CHARS, 4);
    h_put(p, s);
    return p;
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, MH_RET);
    fn();
    return g_eax;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static void sound(uint32_t w) { gcall(sub_0014D390, w, 0); }

static uint32_t lookup(uint32_t ctx, uint32_t fourcc, uint32_t wname)
{
    PUSH32(g_esp, wname);
    return gcall(sub_000449E0, ctx, fourcc);
}

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

/* ---- our own career save: <root>\saves\mycareer-home\careers.ini ---------------
 * One section per career, keyed by the MyPlayer's name and birth date. Written at once on
 * every change; the pack's save is never read or written. */

typedef struct {
    char key[96];
    int valid;
    int tier;                 /* 0 apartment, 1 house, 2 the Crib */
    long long bank;           /* dollars */
    int week;                 /* last week paid ([0xE576B4]) */
    int salary;               /* dollars a year, last seen */
    int agent_req;            /* 0 none, 1 stay, 2 trade */
    int agent_week;           /* week the trade request was made */
    int agent_calls, coach_calls, coach_req;
} Career;

static Career s_c;
static char s_ini[MAX_PATH];

static void ini_path(void)
{
    char root[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("NFL2K5_ROOT", root, sizeof root);
    if (!n || n >= sizeof root) {
        GetModuleFileNameA(NULL, root, sizeof root);
        { char *b = strrchr(root, '\\'); if (b) *b = 0; }
    }
    snprintf(s_ini, sizeof s_ini, "%s\\saves\\mycareer-home", root);
    CreateDirectoryA(s_ini, NULL);
    snprintf(s_ini + strlen(s_ini), sizeof s_ini - strlen(s_ini), "\\careers.ini");
}

static void career_save(void)
{
    char v[64];
    if (!s_c.valid) return;
#define PUTI(k, x) snprintf(v, sizeof v, "%lld", (long long)(x)), WritePrivateProfileStringA(s_c.key, k, v, s_ini)
    PUTI("tier", s_c.tier); PUTI("bank", s_c.bank); PUTI("week", s_c.week); PUTI("salary", s_c.salary);
    PUTI("agent_req", s_c.agent_req); PUTI("agent_week", s_c.agent_week); PUTI("agent_calls", s_c.agent_calls);
    PUTI("coach_calls", s_c.coach_calls); PUTI("coach_req", s_c.coach_req);
#undef PUTI
}

static long long ini_get(const char *k, long long d)
{
    char v[64];
    GetPrivateProfileStringA(s_c.key, k, "", v, sizeof v, s_ini);
    return v[0] ? _strtoi64(v, NULL, 10) : d;
}

/* A name pointer of the record: absolute, or relative to its own field; ASCII or UTF-16. */
static void rec_name(uint32_t rec, uint32_t off, char *out, size_t n)
{
    uint32_t v = MEM32(rec + off), cand[2], k, i;
    out[0] = 0;
    cand[0] = v; cand[1] = rec + off + v;
    for (k = 0; k < 2; k++) {
        uint32_t p = cand[k];
        int ok = 1, wide;
        if (p < 0x10000u || p >= 0x04000000u) continue;
        wide = MEM8(p + 1) == 0;
        for (i = 0; i + 1 < n && i < 24; i++) {
            uint16_t c = wide ? MEM16(p + 2u * i) : MEM8(p + i);
            if (!c) break;
            if (c < 32 || c > 126) { ok = 0; break; }
            out[i] = (char)c;
        }
        out[i] = 0;
        if (ok && i >= 1) return;
    }
    out[0] = 0;
}

static void career_load(uint32_t rec)
{
    char first[32], last[32], key[96];
    int i, y, m, d;
    rec_name(rec, 0x10, first, sizeof first);
    rec_name(rec, 0x14, last, sizeof last);
    m = (MEM8(rec + 0x19) >> 4) & 15;
    d = MEM8(rec + 0x1A) & 31;
    y = ((MEM8(rec + 0x1A) >> 5) & 7) | ((MEM8(rec + 0x1B) & 15) << 3);
    snprintf(key, sizeof key, "%s %s %d-%d-%d pos%u", first[0] ? first : "MyPlayer", last, y, m, d, (unsigned)MEM8(rec + 0x35));
    for (i = 0; key[i]; i++) if (key[i] == '[' || key[i] == ']' || key[i] == '=') key[i] = '_';
    if (s_c.valid && !strcmp(s_c.key, key)) return;
    memset(&s_c, 0, sizeof s_c);
    snprintf(s_c.key, sizeof s_c.key, "%s", key);
    s_c.valid = 1;
    s_c.tier = (int)ini_get("tier", 0);
    s_c.bank = ini_get("bank", 0);
    s_c.week = (int)ini_get("week", 0);
    s_c.salary = (int)ini_get("salary", 0);
    s_c.agent_req = (int)ini_get("agent_req", 0);
    s_c.agent_week = (int)ini_get("agent_week", 0);
    s_c.agent_calls = (int)ini_get("agent_calls", 0);
    s_c.coach_calls = (int)ini_get("coach_calls", 0);
    s_c.coach_req = (int)ini_get("coach_req", 0);
    if (s_c.tier < 0 || s_c.tier > 2) s_c.tier = 0;
    fprintf(stderr, "[MCHOME] career \"%s\" from %s: tier %d, bank %lld, week %d\n", s_c.key, s_ini, s_c.tier, s_c.bank, s_c.week);
}

/* Pay: the record's contract value (+0x0A, $10,000 units) a year, a seventeenth of it for
 * every game week the league moved on ([0xE576B4]) since the last visit. */
static void career_pay(uint32_t rec)
{
    int week = (int)MEM32(0x00E576B4u), salary = (int)MEM16(rec + 0x0Au) * 10000;
    s_c.salary = salary;
    if (week < 0 || week > 60) { MH_LOG("pay: week %d out of range\n", week); return; }
    if (s_c.week == 0 || week < s_c.week) {
        s_c.week = week;                       /* first visit, or a new season */
    } else if (week > s_c.week) {
        long long pay = (long long)(week - s_c.week) * salary / 17;
        s_c.bank += pay;
        fprintf(stderr, "[MCHOME] pay: weeks %d -> %d at $%d a year: +$%lld, bank $%lld\n", s_c.week, week, salary, pay, s_c.bank);
        s_c.week = week;
    }
    {
        static int gifted;
        const char *g = getenv("NFL2K5_MC_HOME_GIFT");           /* tests only: money to buy with */
        if (g && !gifted) { gifted = 1; s_c.bank += _strtoi64(g, NULL, 10); fprintf(stderr, "[MCHOME] test gift: bank $%lld\n", s_c.bank); }
    }
    career_save();
    MH_LOG("week %d, salary $%d, bank $%lld, tier %d\n", week, salary, s_c.bank, s_c.tier);
}

static void money(long long v, char *b, size_t n)
{
    char t[32];
    int len, i, o = 0;
    snprintf(t, sizeof t, "%lld", v < 0 ? -v : v);
    len = (int)strlen(t);
    if (v < 0 && o + 1 < (int)n) b[o++] = '-';
    if (o + 1 < (int)n) b[o++] = '$';
    for (i = 0; i < len && o + 2 < (int)n; i++) {
        b[o++] = t[i];
        if ((len - i - 1) % 3 == 0 && i + 1 < len) b[o++] = ',';
    }
    b[o] = 0;
}

static int rec_starter(uint32_t rec, int *rank)
{
    uint8_t b = MEM8(rec + 0x29u);
    *rank = ((b >> 2) & 7) + 1;
    return (b >> 5) == 0 || ((b >> 2) & 7) == 0;
}


/* ---- the room ------------------------------------------------------------- */

static uint32_t find_mat(const char *name)
{
    uint32_t n = MEM32(s_h.room + 0x1C), a = MEM32(s_h.room + 0x20), i;
    for (i = 0; i < n && i < 256; i++)
        if (weq(MEM32(a + i * 0x80u), name)) return a + i * 0x80u;
    return 0;
}

/* "material=material" copies a texture slot; "material=@TXTR" binds a texture of crib.iff. */
static void room_swap(void)
{
    const char *p = s_h.swap;
    int done = 0, miss = 0;
    while (*p) {
        char pair[96], *eq;
        size_t l = strcspn(p, ",");
        snprintf(pair, sizeof pair, "%.*s", (int)l, p);
        if ((eq = strchr(pair, '=')) != NULL) {
            uint32_t to, tex = 0;
            *eq = 0;
            to = find_mat(pair);
            if (eq[1] == '@') {
                uint32_t w = s_h.wstr[15];
                h_put(w, eq + 2);
                tex = lookup(s_h.w_ctx, FOURCC_TXTR, w);
            } else {
                uint32_t from = find_mat(eq + 1);
                tex = from ? MEM32(from + 0x30) : 0;
            }
            if (to && tex) {
                MEM32(to + 0x30) = tex;
                MEM32(to + 0x08) &= ~1u;
                done++;
            } else { miss++; MH_LOG("swap %s <- %s: to %08X texture %08X\n", pair, eq + 1, to, tex); }
        }
        p += l + (p[l] == ',');
    }
    MH_LOG("%d material swaps, %d missed\n", done, miss);
}

static void room_hide(void)
{
    uint32_t n = MEM32(s_h.room + 0x24), a = MEM32(s_h.room + 0x28), i;
    const char *p = s_h.hide;
    while (*p) {
        char name[48];
        size_t l = strcspn(p, ",");
        snprintf(name, sizeof name, "%.*s", (int)l, p);
        for (i = 0; i < n && i < 256; i++)
            if (weq(MEM32(a + i * 0x60u), name)) MEM32(a + i * 0x60u + 0x0Cu) |= 0x10u;
        p += l + (p[l] == ',');
    }
}

static void room_log(void)
{
    MH_LOG("room %08X: %u nodes, %u materials, sky %08X, cursor %08X, phone %08X, tier %d\n", s_h.room,
           MEM32(s_h.room + 0x24), MEM32(s_h.room + 0x1C), s_h.sky, s_h.cursor, s_h.phone, s_c.tier);
}

static int room_load(void)
{
    uint32_t heap, save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    DWORD t0 = GetTickCount();
    if (s_h.loaded) return 0;
    PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    heap = gcall(sub_00038FB0, 0, 0);
    PUSH32(g_esp, heap);
    PUSH32(g_esp, s_h.slot);
    gcall(sub_00043F50, s_h.w_ctx, s_h.w_file);
    /* fr.iff for the Front Office's layout only (title bar + slim list), under our own context */
    if (s_h.slim) {
        PUSH32(g_esp, 0); PUSH32(g_esp, 0);
        heap = gcall(sub_00038FB0, 0, 0);
        PUSH32(g_esp, heap);
        PUSH32(g_esp, s_h.slot2);
        gcall(sub_00043F50, s_h.w_ctx2, s_h.w_file2);
    }
    gcall(sub_000432D0, 0, 0);
    s_h.room = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_room);
    s_h.sky = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_sky_day);
    s_h.cursor = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_cursor);
    s_h.phone = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_phone);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    fprintf(stderr, "[MCHOME] crib.iff under MCHOME: room %08X sky %08X, %lu ms\n", s_h.room, s_h.sky,
            (unsigned long)(GetTickCount() - t0));
    if (!s_h.room) {
        if (s_h.slim) gcall(sub_000432F0, s_h.w_ctx2, 0);
        gcall(sub_000432F0, s_h.w_ctx, 0);
        return -1;
    }
    s_h.loaded = 1;
    if (s_h.slim) {
        uint32_t lay = lookup(s_h.w_ctx2, FOURCC_LAYT, s_h.w_layout);
        int i;
        for (i = SCR_DOOR; i < SCR_CALL; i++) {
            MEM32(s_h.header[i] + 0x18) = lay ? s_h.w_layout : MEM32(FEATURES + 0x18);
            MEM32(s_h.header[i] + 0x1C) = lay ? 0 : MEM32(FEATURES + 0x1C);
        }
    }
    MEM32(s_h.room + 8) |= 1u;                    /* as sub_00276020 does to its room */
    if (!s_h.hide_env) snprintf(s_h.hide, sizeof s_h.hide, "%s%s", k_hide_items, k_hide_tier[s_c.tier]);
    if (!s_h.swap_env) snprintf(s_h.swap, sizeof s_h.swap, "%s%s,standee_02=@reflection", k_swap_base, k_swap_tier[s_c.tier]);
    room_hide();
    room_swap();
    room_log();
    return 0;
}

static void room_unload(void)
{
    if (!s_h.loaded) return;
    if (s_h.slim) gcall(sub_000432F0, s_h.w_ctx2, 0);
    gcall(sub_000432F0, s_h.w_ctx, 0);
    s_h.loaded = 0;
    s_h.room = s_h.sky = s_h.cursor = s_h.phone = 0;
    MH_LOG("unloaded\n");
}

/* The Crib's camera set-up (sub_00270490): viewport copy with its near/far, default
 * lights, its two light records. */
static void camera_init(void)
{
    uint32_t i;
    gcall(sub_00066B20, s_h.camobj, 0);
    for (i = 0; i < 0x20u; i += 4) MEM32(s_h.vp + i) = MEM32(s_h.camobj + 0x230u + i);
    MEM32(s_h.vp + 0x08) = 0xC0000000u;
    MEM32(s_h.vp + 0x18) = 0xC76A6000u;
    gcall(sub_0002BAD0, s_h.camobj, s_h.vp);
    gcall(sub_0013E7A0, s_h.camobj, 0);
    PUSH32(g_esp, 0x3E800000u);                   /* 0.25: ambient */
    gcall(sub_0002AF30, s_h.light, 0xFFFFFFFFu);
    PUSH32(g_esp, s_h.light);
    gcall(sub_0002ABE0, s_h.camobj, 0);
    PUSH32(g_esp, 0x3F4CCCCDu);                   /* 0.8: directional, 0x51B630 */
    PUSH32(g_esp, 0xFFFFFFFFu);
    gcall(sub_0002AF50, s_h.light, 0x0051B630u);
    PUSH32(g_esp, s_h.light);
    gcall(sub_0002ABE0, s_h.camobj, 0);
}

/* ---- screen events --------------------------------------------------------- */

/* NFL2K5_MC_HOME_CAMFILE (tests): "ex ey ez tx ty tz fov [mx my mz]" replaces the selected stop. */
static void cam_poll(int row)
{
    static DWORD next;
    FILE *f;
    Stop t = STOP(row);
    int n;
    if (!s_h.camfile[0] || GetTickCount() < next) return;
    next = GetTickCount() + 250;
    if (fopen_s(&f, s_h.camfile, "r") || !f) return;
    n = fscanf(f, "%f %f %f %f %f %f %f %f %f %f", &t.eye[0], &t.eye[1], &t.eye[2], &t.at[0], &t.at[1], &t.at[2], &t.fov,
               &t.mark[0], &t.mark[1], &t.mark[2]);
    if (n >= 7) STOP(row) = t;
    fclose(f);
    remove(s_h.camfile);
}

static void stop_vec(const Stop *s, float d, float o[7])
{
    int i;
    for (i = 0; i < 3; i++) { o[i] = s->eye[i] + (s->at[i] - s->eye[i]) * d; o[3 + i] = s->at[i]; }
    o[6] = s->fov;
}

static void leave(uint32_t action)
{
    s_h.action = action;
    s_h.leaving = 1;
    s_h.dolly = 0.0f;
}

static float frame_dt(uint32_t fsm)
{
    float dt = MEMF(fsm + FSM_DT);
    return dt > 0.0f && dt < 1.0f ? dt : 1.0f / 60.0f;
}

/* Camera ease for every screen of the home; true once the dolly ended and we popped. */
static int camera_step(uint32_t fsm, float zoom)
{
    float dt = frame_dt(fsm), want[7], k;
    int i;
    s_h.anim += dt;
    cam_poll(s_h.hot);
    if (s_h.leaving) {
        s_h.dolly += dt * 0.9f;
        gcall(sub_0014FC40, 0, 0);
    }
    stop_vec(&STOP(s_h.hot), s_h.leaving ? (s_h.dolly > 0.4f ? 0.4f : s_h.dolly) : zoom, want);
    k = dt * 5.0f; if (k > 1.0f) k = 1.0f;
    if (!s_h.cur_set) { memcpy(s_h.cur, want, sizeof want); s_h.cur_set = 1; }
    for (i = 0; i < 7; i++) s_h.cur[i] += (want[i] - s_h.cur[i]) * k;
    if (s_h.leaving && s_h.dolly >= 0.6f) {
        s_h.leaving = 0;
        s_h.dolly = 0.0f;
        gcall(sub_0014FC30, 0, 0);
        fprintf(stderr, "[MCHOME] %s: back to Career Home, action %08X, list %d\n", k_hot_name[s_h.hot], s_h.action, s_h.manual);
        gcall(sub_0006E450, fsm, SD_HOME);
        return 1;
    }
    return 0;
}

static uint32_t pad_edges(void)
{
    uint32_t edge = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
        edge |= w & ~s_h.pad_prev[i];
        s_h.pad_prev[i] = w;
    }
    return edge;
}

static void hub_frame(uint32_t fsm)
{
    uint32_t edge;
    int h = s_h.hot;
    if (camera_step(fsm, 0.0f)) return;
    if (s_h.leaving) return;
    edge = pad_edges();
    if (edge & (BTN_RIGHT | BTN_DOWN)) h = (h + 1) % HOTS;
    if (edge & (BTN_LEFT | BTN_UP)) h = (h + HOTS - 1) % HOTS;
    if (h != s_h.hot) { s_h.hot = h; sound(SND_CURSOR); MH_LOG("hotspot %s\n", k_hot_name[h]); return; }
    if (edge & BTN_A) {
        sound(SND_SELECT);
        switch (s_h.hot) {
        case HS_DOOR: gcall(sub_0006E390, fsm, s_h.header[SCR_DOOR]); break;
        case HS_COMPUTER: gcall(sub_0006E390, fsm, s_h.header[SCR_PC]); break;
        case HS_PHONE: gcall(sub_0006E390, fsm, s_h.header[SCR_PHONE]); break;
        case HS_MIRROR: leave(MH_FN_BASE + 0xC00u); break;
        default: s_h.manual = 1; leave(0); break;
        }
        return;
    }
    if (edge & BTN_B) { sound(SND_SELECT); s_h.hot = HS_MENU; s_h.manual = 1; leave(0); }
}

/* Rect: sub_0026DE30(x, y, z, w, h, 0, texture, colour, 0, 0, 0), 640 x 480 space. */
static void rect_tex(float x, float y, float w, float h, uint32_t tex, uint32_t argb)
{
    PUSH32(g_esp, 0); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, argb); PUSH32(g_esp, tex); PUSH32(g_esp, 0);
    PUSH32(g_esp, fbits(h)); PUSH32(g_esp, fbits(w)); PUSH32(g_esp, fbits(20.0f));
    PUSH32(g_esp, fbits(y)); PUSH32(g_esp, fbits(x));
    gcall(sub_0026DE30, 0, 0);
}
static void rect(float x, float y, float w, float h, uint32_t argb) { rect_tex(x, y, w, h, 0, argb); }

/* Text: sub_0006BC30, the call the pack's own card makes (font table 0xA90ECC); x is the centre. */
static void text(uint32_t w, int font, float x, float y, uint32_t argb)
{
    uint32_t f = MEM32(0x00A90ECCu + (uint32_t)font * 4u);
    if (!f || !w) return;
    PUSH32(g_esp, argb); PUSH32(g_esp, f); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, 0x43700000u); PUSH32(g_esp, 0x41A00000u);
    PUSH32(g_esp, fbits(y)); PUSH32(g_esp, fbits(x));
    gcall(sub_0006BC30, w, 0);
}

static void text_s(int slot, const char *s, float x, float y, uint32_t argb)
{
    h_put(s_h.wstr[slot], s);
    text(s_h.wstr[slot], 0, x, y, argb);
}

/* Up to `max` lines of at most `cols` characters, centred on x. */
static void text_wrap(int slot0, const char *s, int cols, float x, float y, float step, int max, uint32_t argb)
{
    int line = 0;
    while (*s && line < max) {
        int n = (int)strlen(s), cut = n;
        char b[STR_CHARS];
        if (n > cols) {
            for (cut = cols; cut > 0 && s[cut] != ' '; cut--) ;
            if (cut == 0) cut = cols;
        }
        snprintf(b, sizeof b, "%.*s", cut, s);
        text_s(slot0 + line, b, x, y + step * (float)line, argb);
        s += cut;
        while (*s == ' ') s++;
        line++;
    }
}

/* Where a room point lands in the 640 x 480 UI space, from the camera we set. */
static int project(const float p[3], float *sx, float *sy)
{
    float f[3], r[3], u[3], d[3], len, z, k;
    int i;
    for (i = 0; i < 3; i++) { f[i] = s_h.cur[3 + i] - s_h.cur[i]; d[i] = p[i] - s_h.cur[i]; }
    len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (len <= 0.0f) return 0;
    for (i = 0; i < 3; i++) f[i] /= len;
    r[0] = -f[2]; r[1] = 0.0f; r[2] = f[0];                 /* f x up, up = +y */
    len = sqrtf(r[0] * r[0] + r[2] * r[2]); if (len <= 0.0f) return 0;
    r[0] /= len; r[2] /= len;
    u[0] = r[1] * f[2] - r[2] * f[1]; u[1] = r[2] * f[0] - r[0] * f[2]; u[2] = r[0] * f[1] - r[1] * f[0];
    z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 10.0f) return 0;
    k = s_h.proj_scale / tanf(s_h.cur[6] * 0.5f * 3.14159265f / 180.0f);
    *sx = 320.0f + s_h.proj_xsign * k * (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / z;
    *sy = 240.0f - k * (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / z;
    return *sx > 20.0f && *sx < 620.0f && *sy > 20.0f && *sy < 380.0f;
}

static void draw_scene_at(uint32_t scene, const float p[3])
{
    int i;
    for (i = 0; i < 16; i++) MEMF(s_h.mtx + (uint32_t)i * 4u) = (i % 5) == 0 ? 1.0f : 0.0f;
    MEMF(s_h.mtx + 0x30u) = p[0]; MEMF(s_h.mtx + 0x34u) = p[1]; MEMF(s_h.mtx + 0x38u) = p[2];
    PUSH32(g_esp, s_h.mtx);
    PUSH32(g_esp, scene);
    gcall(sub_0026DFF0, 0, 0);
}

static void call_draw(void);

static void hub_title(char *b, size_t n)
{
    char m[32];
    money(s_c.bank, m, sizeof m);
    snprintf(b, n, "%s   Bank %s", k_tier_name[s_c.tier], m);
}

static void ev_draw(void)
{
    int i;
    float sx, sy;
    if (!s_h.shown || !s_h.room || !s_h.cur_set) return;
    for (i = 0; i < 3; i++) { MEMF(s_h.eye + i * 4u) = s_h.cur[i]; MEMF(s_h.at + i * 4u) = s_h.cur[3 + i]; }
    MEMF(s_h.eye + 12u) = 1.0f; MEMF(s_h.at + 12u) = 1.0f;
    gcall(sub_0002AD80, s_h.camsave, 0);
    PUSH32(g_esp, s_h.at);
    gcall(sub_0002BDF0, s_h.camobj, s_h.eye);
    gcall(sub_0002B860, s_h.camobj, (uint32_t)(int)(s_h.cur[6] * 182.04445f));
    gcall(sub_0002AC80, s_h.camobj, 0);
    if (s_h.sky) gcall(sub_0002DE60, s_h.sky, 0);
    gcall(sub_0002DE60, s_h.room, 0);
    if (s_h.phone) draw_scene_at(s_h.phone, s_h.phone_at);
    if (s_h.top == SCR_HUB && s_h.cursor && !s_h.leaving && s_h.marker3d) draw_scene_at(s_h.cursor, STOP(s_h.hot).mark);
    {
        uint32_t ui = gcall(sub_00069180, 0, 0);
        gcall(sub_0002AC80, ui, 0);
        if (s_h.top == SCR_HUB) rect(0.0f, 392.0f, 640.0f, 60.0f, 0xA0000000u);
        if (s_h.top == SCR_CALL) {
            rect(0.0f, 0.0f, 640.0f, 480.0f, 0xB0000000u);
            rect(40.0f, 96.0f, 232.0f, 232.0f, 0xFF26254Au);
            {
                uint32_t icon = lookup(0, FOURCC_TXTR, s_h.w_phone_icon);
                if (icon) rect_tex(64.0f, 120.0f, 184.0f, 184.0f, icon, 0xFFFFFFFFu);
            }
            if (s_h.phase == 1) rect(300.0f, 288.0f + 26.0f * (float)s_h.pick, 312.0f, 24.0f,
                                     (uint32_t)(0x90 + (int)(60.0f * sinf(s_h.anim * 5.0f))) << 24 | 0x26254Au);
        }
        gcall(sub_0002AC80, s_h.camsave, 0);
    }
    if (s_h.top == SCR_HUB) {
        char b[STR_CHARS];
        hub_title(b, sizeof b);
        h_put(s_h.title, b);
        text(s_h.title, 0, 150.0f, 36.0f, 0xFFFFFFFFu);
        text(s_h.w_name[s_h.hot], 0, 160.0f, 398.0f, 0xFFFFD040u);
        text(s_h.w_what[s_h.hot], 0, 160.0f, 422.0f, 0xFFFFFFFFu);
        text(s_h.hot == HS_MENU ? s_h.w_prompt_menu : s_h.w_prompt, 0, 470.0f, 410.0f, 0xFFFFFFFFu);
        if (!s_h.leaving && !s_h.marker3d && project(STOP(s_h.hot).mark, &sx, &sy))
            text(s_h.w_glyph, 0, sx, sy - 12.0f + 5.0f * sinf(s_h.anim * 4.0f), 0xFFFFFFFFu);
    } else if (s_h.top == SCR_CALL) call_draw();
}

/* ---- the phone ----------------------------------------------------------------- */

static const char *const k_agent_opts[] = { "I want to stay here.", "Get me outta here.", "Any trade interest?", "Hang up" };
static const char *const k_coach_opts[] = { "Where do I stand?", "I want more snaps.", "Hang up" };

static void call_greeting(char *b, size_t n)
{
    char m[32];
    money(s_c.salary, m, sizeof m);
    if (s_h.who == 0)
        snprintf(b, n, "Hey, it's Marcus. You're on %s a year. What do you need?", m);
    else
        snprintf(b, n, "Coach here. Make it quick, we've got film to watch.");
}

/* What was said: the reply, and the request or mood it leaves in our save file. */
static void call_answer(int opt)
{
    int week = (int)MEM32(0x00E576B4u), rank, starter;
    uint32_t rec = MEM32(SD_PLAYER);
    if (s_h.who == 0) {
        s_c.agent_calls++;
        switch (opt) {
        case 0:
            snprintf(s_h.reply, sizeof s_h.reply, s_c.agent_req == 2
                     ? "Okay, I'll call off the search. I'll tell them you're happy where you are."
                     : "Understood. I'll let the front office know you want to be here for the long run.");
            s_c.agent_req = 1;
            break;
        case 1:
            snprintf(s_h.reply, sizeof s_h.reply, s_c.agent_req == 2
                     ? "I'm already on it. These things take time. Keep playing well, it helps."
                     : "Heard. I'll start working the phones. Give me a few weeks before we hear anything real.");
            if (s_c.agent_req != 2) s_c.agent_week = week;
            s_c.agent_req = 2;
            break;
        default:
            if (s_c.agent_req == 2 && week - s_c.agent_week >= 2)
                snprintf(s_h.reply, sizeof s_h.reply, "A couple of clubs have called about you. Nothing firm yet. A real deal needs the front office to agree.");
            else if (s_c.agent_req == 2)
                snprintf(s_h.reply, sizeof s_h.reply, "Still early. Give it a couple of weeks and I'll have more.");
            else if (s_c.agent_req == 1)
                snprintf(s_h.reply, sizeof s_h.reply, "Nobody's pushing. They know you want to stay.");
            else
                snprintf(s_h.reply, sizeof s_h.reply, "Teams always ask about a player like you. Say the word and I'll listen.");
            break;
        }
    } else {
        starter = rec >= 0x10000u && rec < 0xF0000000u ? rec_starter(rec, &rank) : 0;
        if (!(rec >= 0x10000u && rec < 0xF0000000u)) rank = 0;
        s_c.coach_calls++;
        if (opt == 0) {
            if (starter) snprintf(s_h.reply, sizeof s_h.reply, "You're my starter. Keep doing what you're doing.");
            else snprintf(s_h.reply, sizeof s_h.reply, "You're number %d on the depth chart right now. Show me more in practice.", rank);
        } else {
            s_c.coach_req = 1;
            snprintf(s_h.reply, sizeof s_h.reply, starter ? "You're already getting them. Stay sharp."
                                                          : "Noted. Earn it in practice and you'll get your shot.");
        }
    }
    career_save();
    fprintf(stderr, "[MCHOME] call %s: \"%s\" -> agent_req %d (week %d), coach_req %d\n", s_h.who ? "coach" : "agent",
            s_h.who ? k_coach_opts[opt] : k_agent_opts[opt], s_c.agent_req, s_c.agent_week, s_c.coach_req);
}

static void call_open(uint32_t fsm, int who)
{
    s_h.who = who;
    s_h.phase = 0;
    s_h.pick = 0;
    s_h.call_t = 0.0f;
    s_h.nopts = who ? 3 : 4;
    gcall(sub_0006E390, fsm, s_h.header[SCR_CALL]);
}

static void call_frame(uint32_t fsm)
{
    uint32_t edge;
    float dt = frame_dt(fsm);
    s_h.anim += dt;
    s_h.call_t += dt;
    edge = pad_edges();
    if (edge & BTN_B) { sound(SND_SELECT); gcall(sub_0006E400, fsm, 0); return; }
    if (s_h.phase == 0) {
        if (s_h.call_t > 1.4f || (edge & BTN_A)) { s_h.phase = 1; s_h.call_t = 0.0f; }
        return;
    }
    if (s_h.phase == 1) {
        if (edge & BTN_DOWN) { s_h.pick = (s_h.pick + 1) % s_h.nopts; sound(SND_CURSOR); }
        if (edge & BTN_UP) { s_h.pick = (s_h.pick + s_h.nopts - 1) % s_h.nopts; sound(SND_CURSOR); }
        if (edge & BTN_A) {
            sound(SND_SELECT);
            if (s_h.pick == s_h.nopts - 1) { gcall(sub_0006E400, fsm, 0); return; }
            call_answer(s_h.pick);
            s_h.phase = 2;
            s_h.call_t = 0.0f;
        }
        return;
    }
    if (edge & BTN_A) {
        if (s_h.call_t * 40.0f < (float)strlen(s_h.reply)) s_h.call_t = 100.0f;
        else { s_h.phase = 1; s_h.call_t = 0.0f; sound(SND_SELECT); }
    }
}

static void call_draw(void)
{
    const char *who = s_h.who ? "Head Coach" : "Marcus Hale, Agent";
    char b[320];
    int i, n;
    text_s(0, s_h.who ? "COACH" : "AGENT", 156.0f, 70.0f, 0xFFC0C0C0u);
    n = (int)(s_h.call_t * 30.0f);                       /* the name types in, as the Crib's does */
    snprintf(b, sizeof b, "%.*s", n < 0 ? 0 : n, who);
    text_s(1, b, 156.0f, 340.0f, 0xFFC0C0C0u);
    if (s_h.phase == 0) {
        text_s(2, "Calling...", 456.0f, 160.0f, 0xFF808080u);
    } else if (s_h.phase == 1) {
        call_greeting(b, sizeof b);
        text_wrap(2, b, 28, 456.0f, 110.0f, 24.0f, 6, 0xFFC0C0C0u);
        for (i = 0; i < s_h.nopts; i++)
            text_s(9 + i, s_h.who ? k_coach_opts[i] : k_agent_opts[i], 456.0f, 290.0f + 26.0f * (float)i,
                   i == s_h.pick ? 0xFFC0C0C0u : 0xFF808080u);
    } else {
        n = (int)(s_h.call_t * 40.0f);
        snprintf(b, sizeof b, "%.*s", n < 0 ? 0 : n, s_h.reply);
        text_wrap(2, b, 28, 456.0f, 110.0f, 24.0f, 7, 0xFFC0C0C0u);
    }
    text_s(14, s_h.phase == 1 ? "|CROSS| Select   |CIRCLE| Hang up" : "|CROSS| Continue   |CIRCLE| Hang up",
           320.0f, 420.0f, 0xFFFFFFFFu);
}

/* ---- the lists' labels ------------------------------------------------------------ */

static void lists_refresh(void)
{
    char b[STR_CHARS], m[32], s[32];
    money(s_c.bank, m, sizeof m);
    money(s_c.salary, s, sizeof s);
    snprintf(b, sizeof b, "Bank %s  (%s a year)", m, s);
    h_put(s_h.rowlab[SCR_PC][PC_ROW_BANK], b);
    if (s_c.tier >= 2) snprintf(b, sizeof b, "You own the Crib");
    else {
        money(k_price[s_c.tier + 1], m, sizeof m);
        snprintf(b, sizeof b, "Buy %s  %s", s_c.tier == 0 ? "the House" : "the Crib", m);
    }
    h_put(s_h.rowlab[SCR_PC][PC_ROW_BUY], b);
}

static void buy(void)
{
    char m[32];
    if (s_c.tier >= 2) { sound(SND_CURSOR); return; }
    if (s_c.bank < k_price[s_c.tier + 1]) {
        money(k_price[s_c.tier + 1] - s_c.bank, m, sizeof m);
        { char b[STR_CHARS]; snprintf(b, sizeof b, "Need %s more", m); h_put(s_h.rowlab[SCR_PC][PC_ROW_BUY], b); }
        sound(SND_CURSOR);
        fprintf(stderr, "[MCHOME] buy %s: not enough (bank $%lld)\n", k_tier_name[s_c.tier + 1], s_c.bank);
        return;
    }
    s_c.bank -= k_price[s_c.tier + 1];
    s_c.tier++;
    career_save();
    fprintf(stderr, "[MCHOME] bought %s: bank $%lld; the new home shows on the next visit\n", k_tier_name[s_c.tier], s_c.bank);
    sound(SND_SELECT);
    s_h.hot = HS_DOOR;
    leave(0);                                     /* back to Career Home: the home comes back as the new one */
}

static void mh_event(int scr, int msg, uint32_t fsm)
{
    switch (msg) {
    case 1:
        if (scr == SCR_HUB) {
            s_h.leaving = 0; s_h.dolly = 0.0f; s_h.cur_set = 0; s_h.shown = 0;
            if (s_h.loaded) camera_init();    /* loaded before the push: the handler resolves the layout */
        }
        break;
    case 2:
        if (scr == SCR_HUB) {
            s_h.shown = 0;
            gcall(sub_0014FC30, 0, 0);
            room_unload();
        }
        break;
    case 4: {
        int i;
        if (scr != SCR_HUB && scr != SCR_CALL) gcall(sub_0014FCD0, fsm, 0);
        if (scr == SCR_PC) lists_refresh();
        s_h.shown = s_h.loaded;
        s_h.top = scr;
        for (i = 0; i < 4; i++) s_h.pad_prev[i] = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
        break;
    }
    case 5: s_h.shown = 0; break;
    case 6:
        if (scr == SCR_HUB) hub_frame(fsm);
        else if (scr == SCR_CALL) { camera_step(fsm, 0.22f); call_frame(fsm); }
        else camera_step(fsm, 0.22f);
        break;
    case 7: ev_draw(); break;
    }
}

/* The mirror, from Career Home: sub_00346730's set-up, then our first page. */
static void mirror_go(uint32_t fsm)
{
    uint32_t rec = MEM32(SD_PLAYER), i, real;
    if (rec < 0x10000u || rec >= 0xF0000000u) { fprintf(stderr, "[MCHOME] mirror: no MyPlayer record (%08X)\n", rec); return; }
    for (i = 0; i < REC_BYTES; i++) s_h.rec_before[i] = MEM8(rec + i);
    s_h.rec = rec;
    MEM32(0x00CB8B98u) = 0;
    MEM32(0x00CB8B14u) = rec;
    MEM32(0x00CB8820u) = 1;
    MEM32(0x00CB8B10u) = 0;
    MEM32(0x00CB8BA0u) = 1;
    MEM32(0x00CB8B9Cu) = MEM8(rec + 0x35u);
    gcall(sub_000F33C0, 0x00CB8BBCu, fsm);
    real = gcall(sub_000912E0, rec, 0);
    MEM32(0x00CB8BA4u) = real ? 1u : 0u;
    fprintf(stderr, "[MCHOME] mirror: record %08X, %s face\n", rec, real ? "real" : "created");
    gcall(sub_0006E390, fsm, real ? s_h.mirror_body_real : s_h.mirror_body);
}

/* Start on the first page: the equipment page, by route. */
static void mirror_next(uint32_t fsm)
{
    gcall(sub_0006E390, fsm, MEM32(0x00CB8BA4u) ? s_h.mirror_equip_real : s_h.mirror_equip);
}

static RECOMP_TLS uint32_t s_called;

static void mh_fn(void)
{
    uint32_t a = s_called, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, fsm = g_ecx;
    uint32_t off = a - MH_FN_BASE, esp;
    g_esp += 4;                                   /* the return address; every call here is a plain ret */
    esp = g_esp;
    if (off < 0x800u) mh_event((int)(off / 0x100u), (int)((off % 0x100u) / 4u), fsm);
    else if (off == 0xC00u) mirror_go(fsm);
    else if (off == 0xC04u) mirror_next(fsm);
    else {
        int scr = (int)((off - 0x800u) / 0x40u), row = (int)(((off - 0x800u) % 0x40u) / 4u);
        if (scr > 0 && scr < SCREENS && row < 5 && !s_h.leaving) {
            uint32_t act = k_rows[scr][row].action;
            MH_LOG("row %s\n", k_rows[scr][row].label);
            if (act == ACT_INFO) sound(SND_CURSOR);
            else if (act == ACT_BUY) buy();
            else if (act == ACT_AGENT || act == ACT_COACH) { sound(SND_SELECT); call_open(fsm, act == ACT_COACH); }
            else { sound(SND_SELECT); leave(act); }
        }
    }
    g_esp = esp;
    g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp;
}

recomp_func_t nfl2k5_mc_home_lookup(uint32_t address)
{
    uint32_t off = address - MH_FN_BASE;
    if (!s_h.on || off >= 0x1000u) return NULL;
    s_called = address;
    return mh_fn;
}

/* ---- the hook ---------------------------------------------------------------- */

static uint32_t ih_handler(void)
{
    uint32_t fsm = g_ecx, msg = g_edx, idx = MEM32(fsm + 0x100u), top;
    if (idx > 0x1Fu) return 0;
    top = MEM32(fsm + idx * 8u);
    /* Anything covering Career Home (a row's screen, the match): the home comes back after it. */
    if (msg == 5 && top != s_h.header[SCR_HUB]) { s_h.cool = 0; s_h.manual = 0; }
    if (top != SD_HOME) return 0;
    if (msg != 6) return 0;
    if (s_h.action) {
        uint32_t a = s_h.action;
        s_h.action = 0;
        s_h.cool = 600;                           /* room for the pack's fades before anything is pushed */
        fprintf(stderr, "[MCHOME] Career Home row action %08X\n", a);
        if (a >= 0xFE000000u) { mirror_go(fsm); return SD_HANDLER_RET; }   /* ours: the interpreter cannot continue there */
        return a;                                  /* tail call: ecx = FSM, returns to the handler's caller */
    }
    if (s_h.rec) {                                /* back from the mirror: what changed in the record */
        uint32_t i, n = 0;
        char b[256] = "";
        for (i = 0; i < REC_BYTES; i++)
            if (MEM8(s_h.rec + i) != s_h.rec_before[i] && n++ < 24)
                snprintf(b + strlen(b), sizeof b - strlen(b), " +%02X:%02X>%02X", i, s_h.rec_before[i], MEM8(s_h.rec + i));
        fprintf(stderr, "[MCHOME] mirror done: %u of %u record bytes changed:%s\n", n, REC_BYTES, b);
        s_h.rec = 0;
    }
    if (s_h.cool > 0) { s_h.cool--; return 0; }
    if (s_h.manual) return 0;
    {
        uint32_t rec = MEM32(SD_PLAYER);
        if (rec >= 0x10000u && rec < 0xF0000000u) { career_load(rec); career_pay(rec); }
    }
    fprintf(stderr, "[MCHOME] Career Home on top: pushing the home (%s)\n", k_tier_name[s_c.tier]);
    if (room_load()) { s_h.manual = 1; return 0; }  /* no room: the pack's list stays */
    gcall(sub_0006E390, fsm, s_h.header[SCR_HUB]);
    return SD_HANDLER_RET;
}

/* An edit page with its Start replaced. First page: + the identity page's create, destroy and
 * back records (0x56F5D0, 0x56F618, 0x56F588), Start = next page; last page: Start = Finish. */
static uint32_t clone_page(uint32_t src, int first)
{
    uint32_t page = h_alloc(MH_ROW, 4), ev = MEM32(src + 4u), t = h_alloc(8u * 10u, 4), n = 0, k, start;
    for (k = 0; k < 0x2Cu; k += 4) MEM32(page + k) = MEM32(src + k);
    start = h_alloc(0x48u, 4);
    MEM32(start) = 1;
    MEM32(start + 4u) = first ? MH_FN_BASE + 0xC04u : 0x00346C50u;
    for (k = 0; ev && MEM32(ev + k * 8u) && k < 8; k++) {
        if (MEM32(ev + k * 8u) == 11) continue;
        MEM32(t + n * 8u) = MEM32(ev + k * 8u);
        MEM32(t + n * 8u + 4u) = MEM32(ev + k * 8u + 4u);
        n++;
    }
    MEM32(t + n * 8u) = 11; MEM32(t + n * 8u + 4u) = start; n++;
    if (first) {
        MEM32(t + n * 8u) = 1;  MEM32(t + n * 8u + 4u) = 0x0056F5D0u; n++;
        MEM32(t + n * 8u) = 2;  MEM32(t + n * 8u + 4u) = 0x0056F618u; n++;
        MEM32(t + n * 8u) = 10; MEM32(t + n * 8u + 4u) = 0x0056F588u; n++;
    }
    MEM32(page + 4u) = t;
    return page;
}

void nfl2k5_mc_home_install(void)
{
    const char *e = getenv("NFL2K5_MC_HOME");
    uint32_t r;
    int i;
    if (e && *e == '0') return;
    if (MEM32(SD_MAGIC) != 0x4F48434Du || MEM32(SD_MAGIC + 4u) != 0x3158454Du ||
        MEM32(SD_ROWS + 0x28u) != SD_ACT_NEXT || MEM32(SD_ROWS + 0x34u + 0x28u) != SD_ACT_SCHED ||
        MEM32(SD_ROWS + 0x34u * 3u + 0x28u) != SD_ACT_PLAYER)
        return;                                    /* not SOFTDRINK v0.9 */
    { const char *l = getenv("NFL2K5_MC_HOME_LOG"); s_h.log = l && *l && *l != '0'; }
    { const char *h = getenv("NFL2K5_MC_HOME_HIDE"); if (h) { s_h.hide_env = 1; snprintf(s_h.hide, sizeof s_h.hide, "%s", h); } }
    { const char *h = getenv("NFL2K5_MC_HOME_SWAP"); if (h) { s_h.swap_env = 1; snprintf(s_h.swap, sizeof s_h.swap, "%s", h); } }
    { const char *h = getenv("NFL2K5_MC_HOME_CAMFILE"); if (h) snprintf(s_h.camfile, sizeof s_h.camfile, "%s", h); }
    { const char *h = getenv("NFL2K5_MC_HOME_SLIM"); s_h.slim = !(h && *h == '0'); }
    { const char *m = getenv("NFL2K5_MC_HOME_MARKER"); s_h.marker3d = m && !strcmp(m, "3d"); }
    s_h.proj_scale = 320.0f; s_h.proj_xsign = 1.0f;
    { const char *p = getenv("NFL2K5_MC_HOME_PROJ"); if (p) sscanf(p, "%f %f", &s_h.proj_scale, &s_h.proj_xsign); }
    s_h.phone_at[0] = 2560.0f; s_h.phone_at[1] = 8.0f; s_h.phone_at[2] = -1150.0f;
    { const char *p = getenv("NFL2K5_MC_HOME_PHONE_AT"); if (p) sscanf(p, "%f %f %f", &s_h.phone_at[0], &s_h.phone_at[1], &s_h.phone_at[2]); }
    ini_path();
    s_h.size = 24576u;
    s_h.mem = xbox_HeapAlloc(s_h.size, 16);
    if (!s_h.mem) return;
    s_h.camobj = h_alloc(0x2C0, 16);
    s_h.camsave = h_alloc(0x2C0, 16);
    s_h.slot = h_alloc(0x40, 16);
    s_h.slot2 = h_alloc(0x40, 16);
    s_h.eye = h_alloc(16, 16);
    s_h.at = h_alloc(16, 16);
    s_h.light = h_alloc(0x80, 16);
    s_h.vp = h_alloc(0x20, 16);
    s_h.mtx = h_alloc(0x40, 16);
    s_h.w_ctx = h_wstr("MCHOME");
    s_h.w_file = h_wstr("crib.iff");
    s_h.w_room = h_wstr("room");
    s_h.w_sky_day = h_wstr("skybox_day");
    s_h.w_cursor = h_wstr("cursor");
    s_h.w_phone = h_wstr("phone");
    s_h.w_phone_icon = h_wstr("celebrity_phone_icon");
    s_h.w_ctx2 = h_wstr("MCHOMEFR");
    s_h.w_file2 = h_wstr("fr.iff");
    s_h.w_layout = h_wstr("front_office");
    s_h.title = h_buf("Home");
    s_h.w_glyph = h_wstr("|CROSS|");
    s_h.w_prompt = h_wstr("|CROSS| Select   |CIRCLE| Career Menu");
    s_h.w_prompt_menu = h_wstr("|CROSS| Select");
    for (i = 0; i < 16; i++) s_h.wstr[i] = h_buf("");
    for (i = 0; i < HOTS; i++) { s_h.w_name[i] = h_wstr(k_hot_name[i]); s_h.w_what[i] = h_wstr(k_hot_what[i]); }
    for (i = 0; i < SCREENS; i++) s_h.header[i] = h_alloc(MH_ROW, 4);
    /* The hub and the call screen: the Crib's events-only handler, no rows, no layout. */
    for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header[SCR_HUB] + r) = MEM32(CRIB_DESC + r);
    for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header[SCR_CALL] + r) = MEM32(CRIB_DESC + r);
    /* The lists: the template's list, flags 0x10 (Back pops, no backdrop). */
    for (i = SCR_DOOR; i < SCR_CALL; i++) {
        static const int title_of[SCREENS] = { 0, HS_DOOR, HS_COMPUTER, HS_PHONE, 0 };
        int n = 0, k;
        uint32_t rows;
        while (n < 5 && k_rows[i][n].label) n++;
        rows = h_alloc(MH_ROW * (uint32_t)(n + 1), 4);
        for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header[i] + r) = MEM32(FEATURES + r);
        MEM32(s_h.header[i] + 0x00) = h_wstr(k_hot_name[title_of[i]]);
        MEM32(s_h.header[i] + 0x0C) = 0;
        MEM32(s_h.header[i] + 0x10) = rows;
        MEM32(s_h.header[i] + 0x28) = 0x10u;
        for (k = 0; k < n; k++) {
            uint32_t row = rows + MH_ROW * (uint32_t)k;
            s_h.rowlab[i][k] = h_buf(k_rows[i][k].label);
            MEM32(row + 0x04) = s_h.rowlab[i][k];
            if (k_rows[i][k].link) { MEM32(row + 0x00) = 0; MEM32(row + 0x08) = k_rows[i][k].link; }
            else { MEM32(row + 0x00) = 9; MEM32(row + 0x28) = MH_FN_BASE + 0x800u + (uint32_t)i * 0x40u + (uint32_t)k * 4u; }
        }
        MEM32(rows + MH_ROW * (uint32_t)n) = 3;
    }
    for (i = 0; i < SCREENS; i++) {
        static const int msgs[] = { 1, 2, 4, 5, 6, 7 };
        uint32_t t = h_alloc(8u * 7u, 4);
        int m;
        for (m = 0; m < 6; m++) {
            uint32_t list = h_alloc(0x48u, 4);
            MEM32(list) = 1;
            MEM32(list + 4u) = MH_FN_BASE + (uint32_t)i * 0x100u + (uint32_t)msgs[m] * 4u;
            MEM32(t + (uint32_t)m * 8u) = (uint32_t)msgs[m];
            MEM32(t + (uint32_t)m * 8u + 4u) = list;
        }
        MEM32(s_h.header[i] + 0x04) = t;
    }
    MEM32(s_h.header[SCR_HUB] + 0x00) = s_h.title;
    MEM32(s_h.header[SCR_CALL] + 0x00) = s_h.wstr[13];
    s_h.mirror_body = clone_page(0x0056F410u, 1);
    s_h.mirror_equip = clone_page(0x0056F230u, 0);
    s_h.mirror_body_real = clone_page(0x0056FAA0u, 1);
    s_h.mirror_equip_real = clone_page(0x0056F8C0u, 0);
    if (!s_h.mirror_equip_real) { fprintf(stderr, "[MCHOME] off: out of guest memory (%u bytes)\n", s_h.used); return; }
    s_h.on = 1;
    recomp_interp_add_hook(SD_HANDLER, 0, ih_handler);
    fprintf(stderr, "[MCHOME] on: SOFTDRINK Career Home found, home screen %08X (%u bytes), save file %s\n",
            s_h.header[SCR_HUB], s_h.used, s_ini);
}

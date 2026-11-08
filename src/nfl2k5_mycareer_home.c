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
 *  - The mirror opens a small list: Edit Appearance = the game's own edit-mode pages (0x56F410
 *    body, 0x56F230 equipment, or the real-face pair), cloned to stop before the ratings;
 *    Edit Attributes = the game's own Player Attributes page for the MyPlayer's position
 *    (sub_00344EB0's table), cloned to finish there. Both edit only the MyPlayer's record.
 *  - The phone: calls to the agent and the coach on a call screen drawn like the Crib's
 *    (sub_00274F30: portrait panel, name, wrapped text, choice bar). The coach's panel shows
 *    the team's head coach's own portrait (Portrait.iff, the pack's coach table). What was asked is kept
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
#include "../external/xboxrecomp/src/input/xinput_xbox.h"

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
#define BTN_Y         0x00000800u          /* logged in the host-room tests */
#define REC_BYTES     0x54u
#define STR_CHARS     96u

/* our own row actions (the rest are the pack's) */
#define ACT_INFO      1u
#define ACT_BUY       2u
#define ACT_AGENT     3u
#define ACT_COACH     4u
#define ACT_SHOP      7u
#define ACT_APPEAR    5u
#define ACT_ATTR      6u
#define MH_MIRROR_LOOK  (MH_FN_BASE + 0xC00u)   /* Career Home actions of ours: the mirror's two pages */
#define MH_MIRROR_ATTR  (MH_FN_BASE + 0xC08u)
#define ATTR_PAGES    0x00344FE0u          /* sub_00344EB0's jump table: Player Attributes page by position */
#define ATTR_POSITIONS 17
#define EDIT_FINISH   0x00346C50u          /* the edit pages' Finish */
#define PORTRAIT_CTX  0x00E6A3A0u          /* L"PORTRAIT": Portrait.iff, loaded with the frontend (sub_000F62D0) */
#define FRONTEND_UP   0x00B9E238u          /* 1 while the frontend packages are loaded */
#define SD_COACHES    0x014E18B0u          /* the pack's 2026 head-coach table: 32 x 0x2C, read by 0x014E15DE */
#define SD_COACHES_N  32u

enum { HS_DOOR, HS_MIRROR, HS_COMPUTER, HS_PHONE, HS_MENU, HOTS };
enum { SCR_HUB, SCR_DOOR, SCR_PC, SCR_PHONE, SCR_MIRROR, SCR_CALL, SCREENS };
static const char *const k_tier_name[3] = { "Apartment", "House", "The Crib" };
static const long long k_price[3] = { 0, 250000, 1000000 };
static const char *const k_hot_name[HOTS] = { "Front Door", "Mirror", "Computer", "Phone", "Career Menu" };
static const char *const k_hot_what[HOTS] = { "Game day or practice", "Your look and your attributes", "Stats, schedule, bank, homes",
                                              "Call your agent or coach", "The full MyCareer list" };
/* list rows: label, action (the pack's, or one of ours), link = a screen pushed over the room */
static const struct { const char *label; uint32_t action, link; } k_rows[SCREENS][5] = {
    { { 0 } },
    { { "Play Next Game", SD_ACT_NEXT, 0 }, { "Practice", SD_ACT_PRACTICE, 0 }, { 0 } },
    { { "My Stats", SD_ACT_PLAYER, 0 }, { "Schedule", SD_ACT_SCHED, 0 }, { "Bank", ACT_INFO, 0 }, { "Buy", ACT_BUY, 0 },
      { "Furniture Shop", ACT_SHOP, 0 } },
    { { "Call My Agent", ACT_AGENT, 0 }, { "Call The Coach", ACT_COACH, 0 }, { 0 } },
    { { "Edit Appearance", ACT_APPEAR, 0 }, { "Edit Attributes", ACT_ATTR, 0 }, { 0 } },
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
#define STOP(h) (*(s_h.host ? &s_host_stop[h] : &s_stops[s_c.tier][h]))

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
    uint32_t mirror_attr[ATTR_POSITIONS];   /* Player Attributes by position, first and last page */
    int mirror_mode;                        /* 0 appearance, 1 attributes */
    uint32_t portrait_rec;                  /* scratch record for sub_000E71E0 (+0x40 = portrait id) */
    uint32_t w_nophoto;
    int coach_portrait;                     /* the coach's portrait id, 0 none */
    char coach_name[48], coach_team[8];
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
    int host;                      /* NFL2K5_MC_HOME_ROOM=host: the host-drawn room */
    float host_ar;
    int y_was, free_target;
    uint32_t slot3, w_ctx3, w_file3;   /* the Crib's 64k texture cache (posters, banners, pennants, rugs) */
    int cache_loaded;
    int shop, shop_cat, shop_pick, shop_top;     /* the furniture shop over the room */
    char shop_msg[96];
    uint32_t trig_tex;
    uint32_t wstr[16];             /* scratch UTF-16 buffers for drawn text */
} s_h;

static uint32_t s_coach_tex;               /* the coach's portrait (or logo) on the call screen */
static uint32_t s_coach_logo, s_coach_t0;  /* the team's logo meanwhile; when the call opened */
static void coach_portrait_poll(void);
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
        { char *b = strrchr(root, '\\'); if (!b) b = strrchr(root, '/'); if (b) *b = 0; }
    }
    snprintf(s_ini, sizeof s_ini, "%s\\saves\\mycareer-home", root);
    CreateDirectoryA(s_ini, NULL);
    snprintf(s_ini + strlen(s_ini), sizeof s_ini - strlen(s_ini), "\\careers.ini");
}

static void shop_career_changed(void);
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
    y = 1900 + (((MEM8(rec + 0x1A) >> 5) & 7) | ((MEM8(rec + 0x1B) & 15) << 3));
    snprintf(key, sizeof key, "%s %s %d-%d-%d pos%u", first[0] ? first : "MyPlayer", last, y, m, d, (unsigned)MEM8(rec + 0x35));
    for (i = 0; key[i]; i++) if (key[i] == '[' || key[i] == ']' || key[i] == '=') key[i] = '_';
    if (s_c.valid && !strcmp(s_c.key, key)) return;
    memset(&s_c, 0, sizeof s_c);
    snprintf(s_c.key, sizeof s_c.key, "%s", key);
    shop_career_changed();                         /* another career: its own placements */
    s_c.valid = 1;
    s_c.tier = (int)ini_get("tier", 0);
    s_c.bank = ini_get("bank", -1);
    if (s_c.bank < 0) {                           /* a career this file has never seen */
        const char *sb = getenv("NFL2K5_MC_HOME_START_BANK");   /* test builds only: money to try the shop with */
        s_c.bank = sb ? _strtoi64(sb, NULL, 10) : 0;
        if (s_c.bank < 0) s_c.bank = 0;
        if (sb) fprintf(stderr, "[MCHOME] new career: starting bank $%lld (NFL2K5_MC_HOME_START_BANK)\n", s_c.bank);
    }
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

/* NFL2K5_MC_HOME_NODEDUMP (research): every node of the Crib room, name + its 0x60 bytes. */
static void room_node_dump(void)
{
    uint32_t n = MEM32(s_h.room + 0x24), a = MEM32(s_h.room + 0x28), i, k;
    static int done;
    if (done++ || !getenv("NFL2K5_MC_HOME_NODEDUMP")) return;
    for (i = 0; i < n && i < 256; i++) {
        uint32_t nd = a + i * 0x60u, w = MEM32(nd);
        char name[48];
        for (k = 0; k < 47 && w >= 0x10000u && w < 0xF0000000u && MEM16(w + k * 2u); k++) name[k] = (char)MEM16(w + k * 2u);
        name[k] = 0;
        fprintf(stderr, "[MCNODE] %3u %-28s", i, name);
        for (k = 1; k < 24; k++) {
            uint32_t v = MEM32(nd + k * 4u);
            float f; memcpy(&f, &v, 4);
            if (v && (v & 0x7F800000u) && (v & 0x7F800000u) != 0x7F800000u && fabsf(f) > 1e-3f && fabsf(f) < 1e6f) fprintf(stderr, " %.1f", f);
            else fprintf(stderr, " %X", v);
        }
        fprintf(stderr, "\n");
    }
    {   /* the materials (stride 0x80, name first), and node 4's sub-structure */
        uint32_t nm = MEM32(s_h.room + 0x1Cu), ma = MEM32(s_h.room + 0x20u), w;
        for (i = 0; i < nm && i < 256; i++) {
            char name[48];
            w = MEM32(ma + i * 0x80u);
            for (k = 0; k < 47 && w >= 0x10000u && w < 0xF0000000u && MEM16(w + k * 2u); k++) name[k] = (char)MEM16(w + k * 2u);
            name[k] = 0;
            fprintf(stderr, "[MCMAT] %3u %-24s flags %08X tex %08X\n", i, name, MEM32(ma + i * 0x80u + 8u), MEM32(ma + i * 0x80u + 0x30u));
        }
        for (i = 0; i < n && i < 256; i++) {
            uint32_t sub = MEM32(a + i * 0x60u + 8u);
            if (sub < 0x10000u || sub >= 0xF0000000u) continue;
            fprintf(stderr, "[MCSUB] %3u", i);
            for (k = 0; k < 64; k++) fprintf(stderr, " %08X", MEM32(sub + k * 4u));
            fprintf(stderr, "\n");
        }
    }
}

/* NFL2K5_MC_HOME_CRIBDUMP=<file> (research): the Crib's catalogue resource ('CRIB' "cribdata",
 * what sub_00276183's caller looks up at [0xAC9054]) from our crib.iff context, 64 KB raw, for
 * decoding offline. Written to the test sandbox only. */
static void crib_data_dump(void)
{
    const char *path = getenv("NFL2K5_MC_HOME_CRIBDUMP");
    static int done;
    uint32_t w, p, ctxs[3], i;
    if (!path || done++) return;
    w = s_h.wstr[15];
    h_put(w, "cribdata");
    ctxs[0] = s_h.w_ctx; ctxs[1] = 0; ctxs[2] = MEM32(0x00AC9054u);
    for (i = 0; i < 3; i++) {
        p = lookup(ctxs[i], 0x42495243u, w);
        fprintf(stderr, "[MCCRIB] cribdata in context %u (%08X): %08X\n", i, ctxs[i], p);
        if (p >= 0x10000u && p < 0xF0000000u) {
            FILE *f;
            if (!fopen_s(&f, path, "wb") && f) {
                uint32_t k;
                for (k = 0; k < 196608u; k += 4) { uint32_t v = MEM32(p + k); fwrite(&v, 4, 1, f); }
                fclose(f);
                fprintf(stderr, "[MCCRIB] wrote 64 KB from %08X to %s\n", p, path);
            }
            return;
        }
    }
}

/* NFL2K5_MC_HOME_CRIBONLY (research): only these Crib nodes draw in the CRIBXF pass. */
static void crib_only(int on)
{
    static uint32_t saved[256];
    uint32_t n = MEM32(s_h.room + 0x24), a = MEM32(s_h.room + 0x28), i;
    const char *only = getenv("NFL2K5_MC_HOME_CRIBONLY");
    static char file_list[1024];
    static DWORD next;
    if (!only) return;
    if (only[0] == '@') {                     /* "@path": the list is read from a file (tests switch it) */
        if (on && GetTickCount() >= next) {
            FILE *f;
            next = GetTickCount() + 500;
            if (!fopen_s(&f, only + 1, "r") && f) {
                size_t r = fread(file_list, 1, sizeof file_list - 1, f);
                file_list[r] = 0;
                while (r && (file_list[r - 1] == '\n' || file_list[r - 1] == '\r' || file_list[r - 1] == ' ')) file_list[--r] = 0;
                fclose(f);
            }
        }
        only = file_list;
    }
    for (i = 0; i < n && i < 256; i++) {
        uint32_t f = a + i * 0x60u + 0x0Cu;
        if (on) {
            const char *p = only;
            int keep = 0;
            saved[i] = MEM32(f);
            while (*p) {
                char name[48];
                size_t l = strcspn(p, ",");
                snprintf(name, sizeof name, "%.*s", (int)l, p);
                if (weq(MEM32(a + i * 0x60u), name)) keep = 1;
                p += l + (p[l] == ',');
            }
            MEM32(f) = keep ? (saved[i] & ~0x10u) : (saved[i] | 0x10u);
        } else MEM32(f) = saved[i];
    }
}

/* NFL2K5_MC_HOME_FURNPROBE (research): load crib_<kind>.iff files under contexts of our own and
 * log which scene names they answer to, with each scene's node names. */
/* Research: a context's chunks ([0xB09578] lists contexts: +0 next, +8 name; +0x10 its file:
 * +0x1A4 count, +0x1AC entries of 0x20 -- +4 name hash, +0xC fourcc, +0x14 the resource). */
static void ctx_list(uint32_t wname, const char *label)
{
    uint32_t c = MEM32(0x00B09578u), file, n, arr, i;
    char fc[5];
    for (; c >= 0x10000u && c < 0xF0000000u; c = MEM32(c)) {
        uint32_t w = MEM32(c + 8u), k;
        int same = 1;
        for (k = 0; k < 16; k++) {
            uint16_t a = MEM16(w + k * 2u), b = MEM16(wname + k * 2u);
            if (a != b) { same = 0; break; }
            if (!a) break;
        }
        if (!same) continue;
        file = MEM32(c + 0x10u);
        if (file < 0x10000u || file >= 0xF0000000u) { fprintf(stderr, "[MCCTX] %s: no file\n", label); return; }
        n = MEM32(file + 0x1A4u); arr = MEM32(file + 0x1ACu);
        fprintf(stderr, "[MCCTX] %s: %u chunks\n", label, n);
        for (i = 0; i < n && i < 400; i++) {
            uint32_t e = arr + i * 0x20u, cc = MEM32(e + 0xCu), res = MEM32(e + 0x14u), q;
            memcpy(fc, &cc, 4); fc[4] = 0;
            for (q = 0; q < 4; q++) if (fc[q] < 32 || fc[q] > 126) fc[q] = '?';
            fprintf(stderr, "[MCCTX] %s %3u %s hash %08X res %08X", label, i, fc, MEM32(e + 4u), res);
            if (cc == FOURCC_SCNE && res >= 0x10000u && res < 0xF0000000u) {
                uint32_t nn = MEM32(res + 0x24), na = MEM32(res + 0x28), j, t;
                fprintf(stderr, " nodes %u:", nn);
                for (j = 0; j < nn && j < 24; j++) {
                    uint32_t wn = MEM32(na + j * 0x60u);
                    char nm[40];
                    for (t = 0; t < 39 && wn >= 0x10000u && wn < 0xF0000000u && MEM16(wn + t * 2u); t++) nm[t] = (char)MEM16(wn + t * 2u);
                    nm[t] = 0;
                    fprintf(stderr, " %s", nm);
                }
            } else if (res >= 0x10000u && res < 0xF0000000u) {
                /* a name nearby: the first printable UTF-16 run in the resource's first 0x100 bytes */
                char nm[40];
                uint32_t o, t;
                for (o = 0; o < 0x100u; o += 2) {
                    for (t = 0; t < 39; t++) { uint16_t ch = MEM16(res + o + t * 2u); if (ch < 32 || ch > 126) break; nm[t] = (char)ch; }
                    nm[t] = 0;
                    if (t >= 4) { fprintf(stderr, " name@%X %s", o, nm); break; }
                }
            }
            fprintf(stderr, "\n");
        }
        return;
    }
    fprintf(stderr, "[MCCTX] %s: context not found\n", label);
}

static void furn_probe(void)
{
    static const char *const files[] = { "couch", "coffee_table", "end_table", "sofa_chair", "recliner", "console_table",
                                         "credenza", "bar_stool", "bar_light", "ceiling_fan", "wall_clock", "theater_seating", "av" };
    static const char *const pats[] = { "%s", "%s_p01", "%s_01", "%s_1", "%s_p1", "%s_lo", "%s_hi", "%s_p01_lo", "room", "scene", "%s_m", "%s_00", "%s_p00" };
    static int done;
    uint32_t slot, ctx, file, heap, w = s_h.wstr[15];
    size_t f, k;
    if (done++ || !getenv("NFL2K5_MC_HOME_FURNPROBE")) return;
    if (s_h.cache_loaded) ctx_list(s_h.w_ctx3, "crib_64k.iff");
    for (f = 0; f < sizeof files / sizeof files[0]; f++) {
        char b[64];
        slot = h_alloc(0x40, 16);
        snprintf(b, sizeof b, "MCF%u", (unsigned)f); ctx = h_wstr(b);
        snprintf(b, sizeof b, "crib_%s.iff", files[f]); file = h_wstr(b);
        PUSH32(g_esp, 0); PUSH32(g_esp, 0);
        heap = gcall(sub_00038FB0, 0, 0);
        PUSH32(g_esp, heap);
        PUSH32(g_esp, slot);
        gcall(sub_00043F50, ctx, file);
        gcall(sub_000432D0, 0, 0);
        for (k = 0; k < sizeof pats / sizeof pats[0]; k++) {
            char nm[64];
            uint32_t sc, t;
            snprintf(nm, sizeof nm, pats[k], files[f]);
            h_put(w, nm);
            sc = lookup(ctx, FOURCC_SCNE, w);
            t = lookup(ctx, FOURCC_TXTR, w);
            if (sc || t) {
                fprintf(stderr, "[MCFURN] %s: %s scene %08X texture %08X", b, nm, sc, t);
                if (sc) {
                    uint32_t n = MEM32(sc + 0x24), a = MEM32(sc + 0x28), i, j;
                    fprintf(stderr, " nodes %u:", n);
                    for (i = 0; i < n && i < 40; i++) {
                        uint32_t wn = MEM32(a + i * 0x60u);
                        char nn[40];
                        for (j = 0; j < 39 && wn >= 0x10000u && wn < 0xF0000000u && MEM16(wn + j * 2u); j++) nn[j] = (char)MEM16(wn + j * 2u);
                        nn[j] = 0;
                        fprintf(stderr, " %s", nn);
                    }
                }
                fprintf(stderr, "\n");
            }
        }
        fprintf(stderr, "[MCFURN] %s: probed\n", b);
        ctx_list(ctx, b);
    }
}

static void room_log(void)
{
    room_node_dump();
    crib_data_dump();
    furn_probe();
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
    if (s_h.host && !getenv("NFL2K5_MC_HOME_NO_CACHE")) {
        PUSH32(g_esp, 0); PUSH32(g_esp, 4);         /* the Crib's table: crib_64k.iff, a cache of 4 */
        heap = gcall(sub_00038FB0, 0, 0);
        PUSH32(g_esp, heap);
        PUSH32(g_esp, s_h.slot3);
        gcall(sub_000441B0, s_h.w_ctx3, s_h.w_file3);
        s_h.cache_loaded = 1;
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
    if (!s_h.swap_env) snprintf(s_h.swap, sizeof s_h.swap, "%s%s,standee_02=chrome", k_swap_base, k_swap_tier[s_c.tier]);
    room_hide();
    room_swap();
    room_log();
    return 0;
}

static void room_unload(void)
{
    if (!s_h.loaded) return;
    if (s_h.slim) gcall(sub_000432F0, s_h.w_ctx2, 0);
    if (s_h.cache_loaded) { gcall(sub_000432F0, s_h.w_ctx3, 0); s_h.cache_loaded = 0; }
    gcall(sub_000432F0, s_h.w_ctx, 0);
    s_h.loaded = 0;
    s_h.room = s_h.sky = s_h.cursor = s_h.phone = 0;
    s_h.trig_tex = 0;
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

/* ---- the host-drawn home (stage 1: test geometry, boxes only) ------------------------
 * The room is drawn by the renderer itself (the default; NFL2K5_MC_HOME_ROOM=crib: the Crib corner) (xbox_hostscene.h) at a
 * marker this screen writes into the guest pushbuffer, under the game's own prompts. The
 * apartment below is TEST GEOMETRY: coloured boxes with procedural wood, tile and fabric, no
 * downloaded assets. Metres, y up; the room spans x 0..7, z 0..9. */
#include "xbox_hostscene.h"

static XboxHostScene s_room;
static int s_hot_box[HOTS];                   /* the box each hotspot glows */

static int room_add(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb, uint32_t mat)
{
    XboxHostBox *b;
    if (s_room.nbox >= XBOX_HOSTSCENE_BOXES) return -1;
    b = &s_room.box[s_room.nbox];
    b->lo[0] = x0; b->lo[1] = y0; b->lo[2] = z0; b->hi[0] = x1; b->hi[1] = y1; b->hi[2] = z1;
    b->rgb = rgb; b->mat = mat;
    return s_room.nbox++;
}


/* ---- CC0 furniture (glTF) -------------------------------------------------------------
 * Models the player puts in <root>\mods\mycareer-home\models\<name>\<name>.gltf (or
 * NFL2K5_MC_HOME_MODELS=<folder>), drawn by the host renderer. Each slot: "name x z yaw y" --
 * the model's footprint centred on (x, z), turned by yaw degrees, its lowest point at y. */
extern int mc_model_load(const char *gltf_path, int *ids, int max, float bounds[6]);
extern void mc_model_textures(const char *dir);
#ifdef _WIN32
extern int mc_crib_model(const char *source, int team, char *out, size_t n);   /* nfl2k5_mycareer_crib.cpp */
#else
static int mc_crib_model(const char *source, int team, char *out, size_t n) { (void)source; (void)team; (void)out; (void)n; return 0; }
#endif
static const char *crib_source(const char *id);      /* the shop's Crib source for an item id */
static int crib_team(void);
static int slot_replaced(const char *name);
static const char *slot_replacing(const char *name);
extern int mc_window_view(const char *hdr_path, float wx, float z0, float z1, float y0, float y1, float turn, float exposure);
static char s_models_dir[MAX_PATH];
static int s_view_mesh = -1;
/* yc: a wall piece's centre height (0: stands at y); slot: the shop slot it is in; crib: a Crib
 * item from the cache (front towards +z at yaw 0, its back against the wall in a wall slot) */
typedef struct { char name[40], slot[8]; float x, z, yaw, y, size, height, sc, scy, yc; int ids[12], nid, crib; float b[6]; } RoomModel;
static RoomModel s_models[64];
static char s_shop_models[2048];      /* ";name x z yaw y size height" for each model the shop placed */
static int s_room_rebuild;
static int s_nmodels;

static const char k_models_default[] =
    /* living area, facing the TV wall (x = 0) */
    "k:rugRectangle 2.2 4.2 90 0 3.0;k:loungeDesignSofa 3.55 4.2 90 0 2.3 0.8;modern_coffee_table_01 2.1 4.2 90 0;"
    "k:cabinetTelevision 0.3 4.2 90 0 1.8 0.55;side_table_01 3.6 6.0 0 0;"
    "k:lampRoundFloor 3.7 2.6 0 0 0.4 1.6;"
    /* sleeping corner by the windows, the desk, the dining table by the kitchen */
    "k:bedDouble 6.0 2.6 90 0 2.1 0.6;metal_office_desk 6.0 8.45 180 0;k:laptop 6.0 8.5 180 -1 0.36;"
    "k:tableRound 1.8 6.9 0 0 1.0 0.75;k:chairModernCushion 1.05 6.9 90 0 0.48 0.9;k:chairModernCushion 2.55 6.9 270 0 0.48 0.9;"
    "potted_plant_02 6.55 6.9 0 0;"
    /* the kitchen run along the back wall (z = 9), base units and wall units */
    "k:kitchenCabinetDrawer 0.3 8.7 0 0 0.6 0.9;k:kitchenSink 0.9 8.7 0 0 0.6 0.9;k:kitchenCabinet 1.5 8.7 0 0 0.6 0.9;"
    "k:kitchenStove 2.1 8.7 0 0 0.6 0.9;k:kitchenCabinetDrawer 2.7 8.7 0 0 0.6 0.9;k:kitchenFridge 3.4 8.65 0 0 0.75 1.85;"
    "k:kitchenCabinetUpper 0.3 8.82 0 1.45 0.6 0.7;k:kitchenCabinetUpper 0.9 8.82 0 1.45 0.6 0.7;"
    "k:kitchenCabinetUpper 1.5 8.82 0 1.45 0.6 0.7;k:kitchenCabinetUpper 2.7 8.82 0 1.45 0.6 0.7";

static RoomModel *model_find(const char *name)
{
    int i;
    for (i = 0; i < s_nmodels; i++) if (s_models[i].nid && !strcmp(s_models[i].name, name)) return &s_models[i];
    return NULL;
}

static void models_load(void)
{
    char dir[MAX_PATH], list[4096];
    const char *e = getenv("NFL2K5_MC_HOME_MODELS"), *p;
    s_nmodels = 0;
    if (getenv("NFL2K5_MC_HOME_NO_MODELS")) return;
    if (e) snprintf(dir, sizeof dir, "%s", e);
    else {
        DWORD n = GetEnvironmentVariableA("NFL2K5_ROOT", dir, sizeof dir);
        if (!n || n >= sizeof dir) { GetModuleFileNameA(NULL, dir, sizeof dir); { char *b = strrchr(dir, '\\'); if (!b) b = strrchr(dir, '/'); if (b) *b = 0; } }
        snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "\\mods\\mycareer-home\\models");
    }
    snprintf(list, sizeof list, "%s%s", getenv("NFL2K5_MC_HOME_PLACE") ? getenv("NFL2K5_MC_HOME_PLACE") : k_models_default, s_shop_models);
    {   /* ambientCG surfaces for flat-coloured fabric and wood: NFL2K5_MC_HOME_TEXTURES or models\\ambientcg */
        char td[MAX_PATH + 16];
        if (getenv("NFL2K5_MC_HOME_TEXTURES")) snprintf(td, sizeof td, "%s", getenv("NFL2K5_MC_HOME_TEXTURES"));
        else snprintf(td, sizeof td, "%s\\ambientcg", dir);
        mc_model_textures(td);
    }
    for (p = list; *p && s_nmodels < (int)(sizeof s_models / sizeof s_models[0]); ) {
        RoomModel *m = &s_models[s_nmodels];
        size_t l = strcspn(p, ";");
        char b[128], path[MAX_PATH + 96];
        snprintf(b, sizeof b, "%.*s", (int)l, p);
        p += l + (p[l] == ';');
        memset(m, 0, sizeof *m);
        {   /* "name x z yaw y [size height slot]": y "c1.5" puts the centre at 1.5 m (wall pieces) */
            char ys[16] = "0";
            if (sscanf(b, "%39s %f %f %f %15s %f %f %7s", m->name, &m->x, &m->z, &m->yaw, ys, &m->size, &m->height, m->slot) < 4) continue;
            if (ys[0] == 'c') m->yc = (float)atof(ys + 1); else m->y = (float)atof(ys);
        }
        if (!m->slot[0] && slot_replaced(m->name)) continue;      /* a bought piece stands in its place */
        {   /* a second copy of a model reuses the first's meshes */
            RoomModel *same = model_find(m->name);
            if (same) { memcpy(m->ids, same->ids, sizeof m->ids); m->nid = same->nid; memcpy(m->b, same->b, sizeof m->b); m->crib = same->crib; s_nmodels++; continue; }
        }
        /* "k:name": the Kenney Furniture Kit's name.glb (NFL2K5_MC_HOME_KENNEY, or kenney\ in the models folder) */
        if (!strncmp(m->name, "k:", 2)) {
            const char *kd = getenv("NFL2K5_MC_HOME_KENNEY");
            if (kd) snprintf(path, sizeof path, "%s\\%s.glb", kd, m->name + 2);
            else snprintf(path, sizeof path, "%s\\kenney\\%s.glb", dir, m->name + 2);
        } else if (!strncmp(m->name, "crib:", 5)) {
            /* a Crib item: the cache's model, made from the disc the first time */
            const char *src = crib_source(m->name + 5);
            m->crib = 1;
            if (!src || !mc_crib_model(src, crib_team(), path, sizeof path)) { fprintf(stderr, "[MCHOME] %s: no Crib model\n", m->name); continue; }
        } else snprintf(path, sizeof path, "%s\\%s\\%s.gltf", dir, m->name, m->name);
        {   /* loaded once a session: a room rebuild (a purchase) reuses the meshes */
            static struct { char path[MAX_PATH + 96]; int ids[12], nid; float b[6]; } cache[128];
            static int ncache;
            int ci;
            for (ci = 0; ci < ncache && strcmp(cache[ci].path, path); ci++) ;
            if (ci < ncache) { memcpy(m->ids, cache[ci].ids, sizeof m->ids); m->nid = cache[ci].nid; memcpy(m->b, cache[ci].b, sizeof m->b); }
            else {
                m->nid = mc_model_load(path, m->ids, 12, m->b);
                if (m->nid && ncache < 128) {
                    snprintf(cache[ncache].path, sizeof cache[ncache].path, "%s", path);
                    memcpy(cache[ncache].ids, m->ids, sizeof m->ids); cache[ncache].nid = m->nid; memcpy(cache[ncache].b, m->b, sizeof m->b);
                    ncache++;
                }
            }
        }
        if (m->nid) s_nmodels++;
    }
    /* sizes, then heights: "y < 0" stands a model on the one under it (the TV on its cabinet) */
    {
        int i, j;
        for (i = 0; i < s_nmodels; i++) {
            RoomModel *m = &s_models[i];
            float ext = fmaxf(m->b[3] - m->b[0], m->b[5] - m->b[2]);
            m->sc = m->size > 0.0f && ext > 0.0f ? m->size / ext : 1.0f;
            /* height: a low-poly kit's proportions to real ones (a table 0.75 m high) */
            m->scy = m->height > 0.0f && m->b[4] > m->b[1] ? m->height / (m->b[4] - m->b[1]) : m->sc;
            if (m->size <= 0.0f && m->height > 0.0f) m->sc = m->scy;          /* a height alone: scaled evenly */
            if (m->yc > 0.0f) m->y = fmaxf(0.0f, m->yc - (m->b[4] - m->b[1]) * m->scy * 0.5f);
        }
        for (i = 0; i < s_nmodels; i++)
            if (s_models[i].y < 0.0f) {
                float y = 0.0f;
                for (j = 0; j < s_nmodels; j++)
                    if (j != i && s_models[j].y >= 0.0f && fabsf(s_models[j].x - s_models[i].x) < 0.5f && fabsf(s_models[j].z - s_models[i].z) < 0.5f)
                        y = s_models[j].y + (s_models[j].b[4] - s_models[j].b[1]) * s_models[j].scy;
                s_models[i].y = y;
            }
    }
    snprintf(s_models_dir, sizeof s_models_dir, "%s", dir);
    fprintf(stderr, "[MCHOME] host room: %d CC0 models from %s\n", s_nmodels, dir);
}

/* The placements, into the scene (y < 0 in a slot: stand on whatever model is under it). */
static void models_place(void)
{
    int i, k;
    s_room.nmesh = 0;
    if (s_view_mesh >= 0) {                       /* the window view, as registered (world space) */
        XboxHostMeshInst *in = &s_room.mesh[s_room.nmesh++];
        memset(in, 0, sizeof *in);
        in->mesh = s_view_mesh; in->m[0] = in->m[5] = in->m[10] = in->m[15] = 1.0f;
    }
    s_room.room[0] = 0.0f; s_room.room[1] = 0.0f; s_room.room[2] = 7.0f; s_room.room[3] = 9.0f; s_room.ceiling = 2.8f;
    s_room.nshadow = 0;
    for (i = 0; i < s_nmodels && s_room.nshadow < XBOX_HOSTSCENE_SHADOWS; i++) {
        const RoomModel *m = &s_models[i];
        float hx = (m->b[3] - m->b[0]) * 0.5f * m->sc, hz = (m->b[5] - m->b[2]) * 0.5f * m->sc;
        float c = fabsf(cosf(m->yaw * 3.14159265f / 180.0f)), sn = fabsf(sinf(m->yaw * 3.14159265f / 180.0f));
        float *sh = s_room.shadow[s_room.nshadow];
        if (m->y > 0.05f || (m->b[4] - m->b[1]) * m->scy < 0.15f) continue;                 /* rugs; things on things */
        sh[0] = m->x; sh[1] = m->z; sh[2] = hx * c + hz * sn - 0.04f; sh[3] = hx * sn + hz * c - 0.04f;
        s_room.nshadow++;
    }
    for (i = 0; i < s_nmodels; i++) {
        RoomModel *m = &s_models[i];
        float c = cosf(m->yaw * 3.14159265f / 180.0f), sn = sinf(m->yaw * 3.14159265f / 180.0f);
        float cx = (m->b[0] + m->b[3]) * 0.5f, cz = (m->b[2] + m->b[5]) * 0.5f, y = m->y, px = m->x, pz = m->z;
        if (m->crib && m->slot[0] == 'W') {          /* the slot is 0.12 m off the wall: the back goes against it */
            float d = (m->b[5] - m->b[2]) * 0.5f * m->sc + 0.01f - 0.12f;
            px += sn * d; pz += c * d;
        }
        c *= m->sc; sn *= m->sc;                     /* sc: size over the longest side (models_load) */
        for (k = 0; k < m->nid && s_room.nmesh < XBOX_HOSTSCENE_MESHES; k++) {
            XboxHostMeshInst *in = &s_room.mesh[s_room.nmesh++];
            memset(in, 0, sizeof *in);
            in->mesh = m->ids[k];
            in->m[0] = c;  in->m[2] = -sn;
            in->m[5] = m->scy;
            in->m[8] = sn; in->m[10] = c;
            in->m[12] = px - (cx * c + cz * sn);
            in->m[13] = y - m->b[1] * m->scy;
            in->m[14] = pz - (-cx * sn + cz * c);
            in->m[15] = 1.0f;
        }
    }
}

/* A model's top, for things put on it (the phone on the side table); 0 when it is not there. */
static float model_top(const char *name)
{
    RoomModel *m = model_find(name);
    if (!m) {                                         /* bought in its place: the slot's piece */
        int i;
        const char *slot = slot_replacing(name);
        for (i = 0; slot && i < s_nmodels && !m; i++) if (s_models[i].nid && !strcmp(s_models[i].slot, slot)) m = &s_models[i];
    }
    return m ? m->y + (m->b[4] - m->b[1]) * (m->scy > 0.0f ? m->scy : 1.0f) : 0.0f;
}

static int s_ceil[4] = { -1, -1, -1, -1 };
static void room_build_apartment(void)
{
    float top;
    memset(&s_room, 0, sizeof s_room);
    models_load();
    s_room.highlight = -1;
    /* shell: floor, ceiling, walls (the front wall has the door gap at x 3.0..4.0) */
    room_add(0, -0.05f, 0, 7, 0, 9, 0x8A6A4A, HS_MAT_WOOD);
    s_ceil[0] = room_add(0, 2.8f, 0, 7, 2.85f, 9, 0xE8E6E0, HS_MAT_MATTE);
    room_add(-0.1f, 0, 0, 0, 2.8f, 9, 0xD9D4CA, HS_MAT_MATTE);
    room_add(0, 0, 9, 7, 2.8f, 9.1f, 0xD9D4CA, HS_MAT_MATTE);
    room_add(0, 0, -0.1f, 3.0f, 2.8f, 0, 0xD9D4CA, HS_MAT_MATTE);
    room_add(4.0f, 0, -0.1f, 7, 2.8f, 0, 0xD9D4CA, HS_MAT_MATTE);
    room_add(3.0f, 2.2f, -0.1f, 4.0f, 2.8f, 0, 0xD9D4CA, HS_MAT_MATTE);
    /* window wall (x = 7): wall below and above a long window onto the evening sky */
    room_add(7, 0, 0, 7.1f, 0.9f, 9, 0xD9D4CA, HS_MAT_MATTE);
    room_add(7, 2.4f, 0, 7.1f, 2.8f, 9, 0xD9D4CA, HS_MAT_MATTE);
    {   /* the view: a CC0 skyline HDRI (NFL2K5_MC_HOME_VIEW or models\\views\\apartment.hdr), else plain sky */
        char vp[MAX_PATH + 32];
        int view;
        if (getenv("NFL2K5_MC_HOME_VIEW")) snprintf(vp, sizeof vp, "%s", getenv("NFL2K5_MC_HOME_VIEW"));
        else snprintf(vp, sizeof vp, "%s\\views\\apartment.hdr", s_models_dir);
        view = s_view_mesh >= 0 ? s_view_mesh : mc_window_view(vp, 7.1f, 0.0f, 9.0f, 0.9f, 2.4f, getenv("NFL2K5_MC_HOME_VIEW_TURN") ? (float)atof(getenv("NFL2K5_MC_HOME_VIEW_TURN")) : 0.25f, 1.6f);
        if (view >= 0) s_view_mesh = view;
        else room_add(7.08f, 0.9f, 0, 7.12f, 2.4f, 9, 0x6E8FC0, HS_MAT_EMISSIVE);
    }
    room_add(7.0f, 0.9f, 2.95f, 7.05f, 2.4f, 3.05f, 0x2B2B2B, HS_MAT_MATTE);
    room_add(7.0f, 0.9f, 5.95f, 7.05f, 2.4f, 6.05f, 0x2B2B2B, HS_MAT_MATTE);
    {   /* frames: a sill, a head, jambs and slim mullions between the big panes */
        static const float mz[] = { 1.5f, 4.5f, 7.5f };
        int q;
        room_add(6.88f, 0.86f, 0.0f, 7.06f, 0.9f, 9.0f, 0xEDEBE6, HS_MAT_MATTE);
        room_add(6.98f, 2.38f, 0.0f, 7.06f, 2.44f, 9.0f, 0x2B2B2B, HS_MAT_MATTE);
        room_add(6.98f, 0.9f, 0.0f, 7.06f, 2.4f, 0.06f, 0x2B2B2B, HS_MAT_MATTE);
        room_add(6.98f, 0.9f, 8.94f, 7.06f, 2.4f, 9.0f, 0x2B2B2B, HS_MAT_MATTE);
        for (q = 0; q < 3; q++) room_add(7.0f, 0.9f, mz[q] - 0.02f, 7.04f, 2.4f, mz[q] + 0.02f, 0x2B2B2B, HS_MAT_MATTE);
    }
    /* skirting */
    room_add(0, 0, 8.98f, 7, 0.1f, 9, 0x3A3A3A, HS_MAT_MATTE);
    room_add(0, 0, 0, 0.02f, 0.1f, 9, 0x3A3A3A, HS_MAT_MATTE);
    room_add(6.98f, 0, 0, 7.0f, 0.1f, 9, 0x3A3A3A, HS_MAT_MATTE);
    room_add(0, 0, 0, 3.0f, 0.1f, 0.02f, 0x3A3A3A, HS_MAT_MATTE);
    room_add(4.0f, 0, 0, 7.0f, 0.1f, 0.02f, 0x3A3A3A, HS_MAT_MATTE);
    /* the front door, in its gap */
    s_hot_box[HS_DOOR] = room_add(3.05f, 0, -0.06f, 3.95f, 2.15f, -0.02f, 0x5A3A24, HS_MAT_WOOD);
    room_add(3.8f, 1.0f, -0.02f, 3.86f, 1.06f, 0.02f, 0xC8C8C8, HS_MAT_GLASS);
    /* the living area faces the left wall (x = 0): rug, couch (back to the room), coffee table */
    if (!model_find("k:rugRectangle") && !slot_replaced("k:rugRectangle")) room_add(0.9f, 0, 2.6f, 3.9f, 0.01f, 5.8f, 0x7A6E62, HS_MAT_FABRIC);
    if (!model_find("k:loungeDesignSofa") && !slot_replaced("k:loungeDesignSofa")) {
        room_add(3.1f, 0, 3.0f, 4.0f, 0.45f, 5.4f, 0x3C4A5C, HS_MAT_FABRIC);
        room_add(3.7f, 0.45f, 3.0f, 4.0f, 0.95f, 5.4f, 0x3C4A5C, HS_MAT_FABRIC);
        room_add(3.1f, 0, 2.8f, 4.0f, 0.65f, 3.0f, 0x34404F, HS_MAT_FABRIC);
        room_add(3.1f, 0, 5.4f, 4.0f, 0.65f, 5.6f, 0x34404F, HS_MAT_FABRIC);
    }
    if (!model_find("modern_coffee_table_01") && !slot_replaced("modern_coffee_table_01")) room_add(1.7f, 0, 3.4f, 2.5f, 0.42f, 5.0f, 0x4A3526, HS_MAT_WOOD);
    /* the TV wall (x = 0): a low unit and the screen (Career Menu) */
    if (!model_find("modern_wooden_cabinet")) room_add(0.0f, 0, 2.9f, 0.4f, 0.45f, 5.5f, 0x2E2A26, HS_MAT_WOOD);
    if (model_find("k:cabinetTelevision")) {  /* a slim 65-inch screen on its stand, over the cabinet */
        float cb = model_top("k:cabinetTelevision");
        room_add(0.22f, cb, 4.05f, 0.30f, cb + 0.02f, 4.35f, 0x1A1A1C, HS_MAT_GLASS);           /* foot */
        room_add(0.24f, cb, 4.17f, 0.27f, cb + 0.14f, 4.23f, 0x1A1A1C, HS_MAT_GLASS);           /* neck */
        room_add(0.19f, cb + 0.12f, 3.47f, 0.23f, cb + 0.95f, 4.93f, 0x0E0E10, HS_MAT_GLASS);   /* panel */
        s_hot_box[HS_MENU] = room_add(0.23f, cb + 0.14f, 3.49f, 0.235f, cb + 0.93f, 4.91f, 0x1C3550, HS_MAT_SCREEN);
    } else {
        s_hot_box[HS_MENU] = room_add(0.12f, 0.75f, 3.3f, 0.16f, 1.75f, 5.1f, 0x1C3550, HS_MAT_SCREEN);
        room_add(0.08f, 0.7f, 3.25f, 0.12f, 1.8f, 5.15f, 0x111111, HS_MAT_MATTE);
    }
    /* the side table at the couch's end and the phone */
    top = model_top("side_table_01");
    if (top <= 0.0f) { room_add(3.3f, 0, 5.8f, 3.9f, 0.55f, 6.4f, 0x4A3526, HS_MAT_WOOD); top = 0.55f; }
    s_hot_box[HS_PHONE] = room_add(3.54f, top, 5.98f, 3.66f, top + 0.02f, 6.22f, 0x1A1A1A, HS_MAT_GLASS);
    /* the desk and the laptop (the computer) */
    if (!model_find("metal_office_desk")) {
        room_add(5.2f, 0.72f, 8.2f, 6.8f, 0.76f, 8.9f, 0x5A4030, HS_MAT_WOOD);
        room_add(5.25f, 0, 8.25f, 5.3f, 0.72f, 8.85f, 0x222222, HS_MAT_MATTE);
        room_add(6.7f, 0, 8.25f, 6.75f, 0.72f, 8.85f, 0x222222, HS_MAT_MATTE);
    }
    if (model_find("classic_laptop") || model_find("k:laptop")) {   /* the hotspot: a point at the laptop's screen */
        top = model_top("metal_office_desk");
        s_hot_box[HS_COMPUTER] = room_add(5.99f, top + 0.15f, 8.5f, 6.01f, top + 0.17f, 8.52f, 0x101010, HS_MAT_MATTE);
    } else {
        room_add(5.7f, 0.76f, 8.35f, 6.3f, 0.78f, 8.7f, 0x8C8C90, HS_MAT_GLASS);
        s_hot_box[HS_COMPUTER] = room_add(5.7f, 0.78f, 8.68f, 6.3f, 1.12f, 8.71f, 0x3A6EA5, HS_MAT_SCREEN);
    }
    room_add(5.8f, 0, 7.5f, 6.3f, 0.48f, 8.0f, 0x30302E, HS_MAT_FABRIC);
    /* the mirror by the door, in a black frame */
    room_add(0.0f, 0.25f, 1.15f, 0.04f, 2.15f, 2.05f, 0x101010, HS_MAT_MATTE);
    s_hot_box[HS_MIRROR] = room_add(0.04f, 0.3f, 1.2f, 0.06f, 2.1f, 2.0f, 0xD8DCE0, HS_MAT_MIRROR);
    /* kitchen counter along the back wall */
    if (!model_find("k:kitchenSink")) {
        room_add(0.0f, 0, 8.3f, 3.6f, 0.9f, 9.0f, 0xE5E2DC, HS_MAT_MATTE);
        room_add(0.0f, 0.9f, 8.25f, 3.6f, 0.94f, 9.0f, 0x2F3135, HS_MAT_TILE);
        room_add(0.0f, 1.5f, 8.65f, 3.6f, 2.2f, 9.0f, 0xE5E2DC, HS_MAT_MATTE);
    } else room_add(0.0f, 0.9f, 8.97f, 3.0f, 1.45f, 9.0f, 0xD8D4CC, HS_MAT_TILE);   /* the splashback */
    /* ceiling lights */
    s_ceil[1] = room_add(2.0f, 2.78f, 4.0f, 2.4f, 2.8f, 4.4f, 0xFFF2D8, HS_MAT_EMISSIVE);
    s_ceil[2] = room_add(5.8f, 2.78f, 8.0f, 6.2f, 2.8f, 8.4f, 0xFFF2D8, HS_MAT_EMISSIVE);
    s_ceil[3] = room_add(1.6f, 2.78f, 8.4f, 2.0f, 2.8f, 8.8f, 0xFFF2D8, HS_MAT_EMISSIVE);
    s_room.sky[0] = 0.36f; s_room.sky[1] = 0.38f; s_room.sky[2] = 0.44f;
    s_room.ground[0] = 0.16f; s_room.ground[1] = 0.13f; s_room.ground[2] = 0.11f;
    s_room.nlight = 4;
    { XboxHostLight l = { { 2.2f, 2.6f, 4.2f }, { 1.6f, 1.4f, 1.15f }, 7.0f }; s_room.light[0] = l; }
    { XboxHostLight l = { { 6.0f, 2.6f, 8.2f }, { 1.1f, 1.0f, 0.85f }, 4.5f }; s_room.light[1] = l; }
    { XboxHostLight l = { { 6.6f, 1.7f, 4.5f }, { 0.55f, 0.65f, 0.95f }, 7.0f }; s_room.light[2] = l; }
    { XboxHostLight l = { { 1.8f, 2.6f, 8.6f }, { 0.9f, 0.85f, 0.75f }, 4.0f }; s_room.light[3] = l; }
    models_place();
    fprintf(stderr, "[MCHOME] host room: test apartment, %d boxes\n", s_room.nbox);
}

/* Fixed stops for the host room (metres): looking at each hotspot's object. */
static Stop s_host_stop[HOTS] = {
    { { 3.5f, 1.6f, 2.6f }, { 3.5f, 1.1f, 0.0f }, 60.0f, { 3.5f, 2.35f, 0.0f } },
    { { 1.8f, 1.6f, 2.4f }, { 0.05f, 1.2f, 1.6f }, 60.0f, { 0.1f, 2.3f, 1.6f } },
    { { 5.2f, 1.6f, 6.6f }, { 6.0f, 0.95f, 8.7f }, 55.0f, { 6.0f, 1.35f, 8.7f } },
    { { 2.6f, 1.5f, 7.4f }, { 3.6f, 0.56f, 6.1f }, 55.0f, { 3.6f, 0.85f, 6.1f } },
    { { 4.6f, 1.6f, 4.2f }, { 0.14f, 1.2f, 4.2f }, 60.0f, { 0.14f, 2.0f, 4.2f } },
};

/* The marker: one SET_COLOR_CLEAR_VALUE (count 1, subchannel 0) with the magic value, written
 * where the engine writes its own clears (sub_000330C0: command buffer [[0xA6AA6C]+0xC]). */
static int emit_marker(void)
{
    /* [0xA6AA6C] heads a chain of command-buffer chunks; the engine writes into the last
     * one, at its +0x0C (sub_00027CA0, as it clears). */
    uint32_t head = MEM32(0x00A6AA6Cu), obj, p, n;
    if (head < 0x10000u || head >= 0xF0000000u) return 0;
    obj = MEM32(head);
    if (obj < 0x10000u || obj >= 0xF0000000u) return 0;
    for (n = 0; n < 64 && MEM32(obj) >= 0x10000u && MEM32(obj) < 0xF0000000u; n++) obj = MEM32(obj);
    p = MEM32(obj + 0x0Cu);
    if (p < 0x10000u || p >= 0xF0000000u) return 0;
    MEM32(p) = 0x00041D90u;
    MEM32(p + 4u) = XBOX_HOSTSCENE_MAGIC;
    MEM32(obj + 0x0Cu) = p + 8u;
    return 1;
}

static void rect_tex(float x, float y, float w, float h, uint32_t tex, uint32_t argb);
/* ---- Crib items in the host room ----------------------------------------------------
 * The Crib scene (crib.iff) is one set of nodes (stride 0x60: +0 name, +0x08 the node's
 * data -- its centre (x, y, z) in Crib centimetres first -- +0x0C flags, 0x10 hidden, +0x20 a
 * row-major 4x4 matrix, identity on the disc). The game draws them: for each item we show
 * only that node, give it a matrix that turns it about its centre and puts it on our slot
 * (metres x 100; the Crib's floor is y 0 as ours), draw the scene with our camera, and put
 * everything back. Nothing is extracted or stored. */
typedef struct { char node[32]; float x, z, yaw, dy, fx, fz, scale; char look[48]; } RoomItem;   /* dy: metres up or down; fx, fz: the node's pivot fix */
static RoomItem s_items[24];
static int s_nitems = -1;

static void money(long long v, char *b, size_t n);
static void text_s(int slot, const char *s, float x, float y, uint32_t argb);
static float model_top(const char *name);
static long long ini_get(const char *k, long long d);
static void career_save(void);
static int s_items_dirty;
static void items_from_places(void);
static const char k_items_default[] =
    /* the apartment's Crib pieces (items.ini, apartment tier): a palm by the door, a standee
     * by the TV wall, a game ball on the side table, wall art beside the door, two sconces */
    "z_plant_palm 6.45 0.7 0;standee_p02 0.6 6.2 90;game_ball_01 3.6 6.0 0;"
    "wall_art_generic_p01 1.6 0.06 0;sconce_002 0.05 2.9 90;sconce_003 0.05 5.5 90";

static void items_parse(const char *p)
{
    s_nitems = 0;
    while (*p && s_nitems < (int)(sizeof s_items / sizeof s_items[0])) {
        RoomItem it;
        size_t l = strcspn(p, ";");
        char b[96];
        snprintf(b, sizeof b, "%.*s", (int)l, p);
        memset(&it, 0, sizeof it);
        {   /* "node x z yaw [dy fx fz scale] [material=texture]" */
            char *tok, *ctx = NULL;
            float v[7] = { 0, 0, 0, 0, 0, 0, 1 };
            int nv = 0;
            tok = strtok_s(b, " \t\r\n", &ctx);
            if (tok) {
                snprintf(it.node, sizeof it.node, "%s", tok);
                while ((tok = strtok_s(NULL, " \t\r\n", &ctx)) != NULL) {
                    if (strchr(tok, '=')) snprintf(it.look, sizeof it.look, "%s", tok);
                    else if (nv < 7) v[nv++] = (float)atof(tok);
                }
                if (nv >= 3) {
                    it.x = v[0]; it.z = v[1]; it.yaw = v[2]; it.dy = v[3]; it.fx = v[4]; it.fz = v[5];
                    it.scale = v[6] > 0.0f ? v[6] : 1.0f;
                    s_items[s_nitems++] = it;
                }
            }
        }
        p += l + (p[l] == ';');
    }
}

/* NFL2K5_MC_HOME_ITEMS: "node x z yaw;..." replaces the list, or "@path" reads it from a file
 * every half second (tests move things live). */
static void items_poll(void)
{
    const char *e = getenv("NFL2K5_MC_HOME_ITEMS");
    static DWORD next;
    if (e && e[0] == '@') {
        if (GetTickCount() >= next) {
            FILE *f;
            char b[1024];
            next = GetTickCount() + 500;
            if (!fopen_s(&f, e + 1, "r") && f) {
                size_t r = fread(b, 1, sizeof b - 1, f);
                b[r] = 0;
                fclose(f);
                items_parse(b);
            }
        }
        if (s_nitems < 0) items_parse(k_items_default);
    } else if (e) { if (s_nitems < 0) items_parse(e); }
    else if (s_items_dirty) items_from_places();
}


/* ---- the furniture shop ------------------------------------------------------------------
 * data/mycareer-home/items.ini (NFL2K5_MC_HOME_ITEMS_INI to point elsewhere): one line an item,
 * "id = Name | tier | price | slot | source | price from". The shop sells what the home's tier
 * allows (tiers stack) and what this build can draw: the Crib's own pieces (crib.iff:<name>,
 * crib_<kind>.iff#modelN, #lookN for the Crib's pictures) as models of the Crib cache
 * (nfl2k5_mycareer_crib.cpp, made from the player's disc), and CC0 models (model:<name>).
 * ",size,height" after a source sets its size in metres (0 = the Crib's own). Each room has
 * fixed slots of each kind; a purchase goes in the first free one, or replaces the oldest.
 * Placements live in careers.ini (place_<slot>). */
enum { SL_WALL, SL_CORNER, SL_TABLE, SL_SOFA, SL_COFFEE, SL_RUG, SL_SIDE, SL_KINDS };
static const char *const k_slot_kind[SL_KINDS] = { "Wall", "Corner", "Table Top", "Sofa", "Coffee Table", "Rug", "Side Table" };
typedef struct { char id[40], name[48], node[32], look[48], model[64], src[96]; float msize, mheight; int tier, kind; long long price; } ShopItem;
static ShopItem s_shop[160];
static int s_nshop = -1;

/* the apartment's slots: where, which way the item faces (degrees; 0 = towards +z), how high
 * (wall pieces: their centre's height; -1/-2 stand on the side table / the desk), and the
 * room's own piece a purchase there takes the place of */
typedef struct { const char *key; int kind; float x, z, face, dy; const char *replaces; } Slot;
static const Slot k_slots[] = {
    { "W1", SL_WALL, 1.5f, 0.12f, 0.0f, 1.5f, NULL },   { "W2", SL_WALL, 5.5f, 0.12f, 0.0f, 1.5f, NULL },
    { "W3", SL_WALL, 0.12f, 7.0f, 90.0f, 1.5f, NULL },  { "W4", SL_WALL, 4.4f, 8.88f, 180.0f, 1.5f, NULL },
    { "C1", SL_CORNER, 6.45f, 0.7f, 0.0f, 0.0f, NULL }, { "C2", SL_CORNER, 0.6f, 6.2f, 90.0f, 0.0f, NULL },
    { "C3", SL_CORNER, 4.6f, 8.45f, 180.0f, 0.0f, NULL },
    { "T1", SL_TABLE, 3.6f, 6.0f, 0.0f, -1.0f, NULL },  { "T2", SL_TABLE, 5.4f, 8.5f, 180.0f, -2.0f, NULL },
    /* the living area faces the TV wall (x = 0): face 270 */
    { "S1", SL_SOFA, 3.55f, 4.2f, 270.0f, 0.0f, "k:loungeDesignSofa" },
    { "F1", SL_COFFEE, 2.1f, 4.2f, 270.0f, 0.0f, "modern_coffee_table_01" },
    { "R1", SL_RUG, 2.2f, 4.2f, 270.0f, 0.0f, "k:rugRectangle" },
    { "E1", SL_SIDE, 3.6f, 6.0f, 270.0f, 0.0f, "side_table_01" },
};
#define SLOTS ((int)(sizeof k_slots / sizeof k_slots[0]))
static char s_place[SLOTS][40];
static const ShopItem *shop_find(const char *id);
static void shop_load(void);

static const char *crib_source(const char *id)
{
    const ShopItem *si;
    if (s_nshop < 0) shop_load();
    si = shop_find(id);
    return si && si->src[0] ? si->src : NULL;
}

/* The Crib's team pictures are by the team's asset number ("25" the 49ers): the MyPlayer's team
 * (my_team below), or NFL2K5_MC_HOME_CRIB_TEAM. */
static uint32_t my_team(uint32_t rec, int *index);
static uint32_t res_ptr(uint32_t field);
static void wtoa(uint32_t w, char *out, size_t n);
static int crib_team(void)
{
    const char *e = getenv("NFL2K5_MC_HOME_CRIB_TEAM");
    uint32_t rec, slot;
    int index = -1;
    char code[8] = "";
    if (e && *e) return atoi(e);
    rec = MEM32(SD_PLAYER);
    if (rec < 0x10000u || rec >= 0xF0000000u) return 0;
    slot = my_team(rec, &index);
    if (!slot) return 0;
    wtoa(res_ptr(slot + 0x10Cu), code, sizeof code);
    return code[0] >= '0' && code[0] <= '9' ? atoi(code) : 0;
}

/* a filled slot's room piece stands aside (slot_replacing: which slot took it) */
static const char *slot_replacing(const char *name)
{
    int i;
    for (i = 0; i < SLOTS; i++)
        if (k_slots[i].replaces && s_place[i][0] && !strcmp(k_slots[i].replaces, name)) return k_slots[i].key;
    return NULL;
}
static int slot_replaced(const char *name) { return slot_replacing(name) != NULL; }
static int s_place_loaded = -1;
static int s_items_dirty = 1;
static void shop_career_changed(void) { s_place_loaded = -1; s_items_dirty = 1; }

/* which way each Crib node faces in the Crib (degrees, as Slot.face), so a slot can turn it */
static float node_face(const char *node)
{
    static const struct { const char *prefix; float face; } k[] = {
        { "poster_", 0.0f }, { "banner_", 0.0f }, { "pennant_", 0.0f }, { "wall_art", 0.0f },
        { "sconce_", 0.0f }, { "standee_", 0.0f }, { "jukebox", 0.0f },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++) if (!strncmp(node, k[i].prefix, strlen(k[i].prefix))) return k[i].face;
    return 0.0f;
}

static void shop_trim(char *t)
{
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\r' || t[n - 1] == '\n' || t[n - 1] == '\t')) t[--n] = 0;
    while (*t == ' ' || *t == '\t') memmove(t, t + 1, strlen(t));
}

static void shop_load(void)
{
    char path[MAX_PATH], line[512];
    FILE *f = NULL;
    s_nshop = 0;
    if (getenv("NFL2K5_MC_HOME_ITEMS_INI")) snprintf(path, sizeof path, "%s", getenv("NFL2K5_MC_HOME_ITEMS_INI"));
    else {
        DWORD n = GetEnvironmentVariableA("NFL2K5_ROOT", path, sizeof path);
        if (!n || n >= sizeof path) { GetModuleFileNameA(NULL, path, sizeof path); { char *b = strrchr(path, '\\'); if (!b) b = strrchr(path, '/'); if (b) *b = 0; } }
        snprintf(path + strlen(path), sizeof path - strlen(path), "\\data\\mycareer-home\\items.ini");
    }
    if (fopen_s(&f, path, "r") || !f) { fprintf(stderr, "[MCHOME] shop: no %s\n", path); return; }
    while (fgets(line, sizeof line, f) && s_nshop < (int)(sizeof s_shop / sizeof s_shop[0])) {
        char *eq = strchr(line, '='), *fld[6], *q;
        ShopItem *it = &s_shop[s_nshop];
        int nf = 0, look = -1;
        if (line[0] == ';' || line[0] == '[' || !eq) continue;
        *eq = 0;
        memset(it, 0, sizeof *it);
        snprintf(it->id, sizeof it->id, "%s", line); shop_trim(it->id);
        for (q = eq + 1; nf < 6 && q; nf++) { fld[nf] = q; q = strchr(q, '|'); if (q) *q++ = 0; }
        if (nf < 5) continue;
        { int k; for (k = 0; k < nf; k++) shop_trim(fld[k]); }
        snprintf(it->name, sizeof it->name, "%s", fld[0]);
        it->tier = !strcmp(fld[1], "mansion") ? 2 : !strcmp(fld[1], "house") ? 1 : 0;
        it->price = _strtoi64(fld[2], NULL, 10);
        if (!strcmp(fld[3], "wall")) it->kind = SL_WALL;
        else if (!strcmp(fld[3], "floor_corner")) it->kind = SL_CORNER;
        else if (!strcmp(fld[3], "table_top")) it->kind = SL_TABLE;
        else if (!strcmp(fld[3], "big_floor") && (strstr(fld[4], "crib_couch") || strstr(fld[4], "crib_recliner"))) it->kind = SL_SOFA;
        else if (!strcmp(fld[3], "floor") && strstr(fld[4], "crib_coffee_table")) it->kind = SL_COFFEE;
        else if (!strcmp(fld[3], "floor") && strstr(fld[4], "rug_")) it->kind = SL_RUG;
        else if (!strcmp(fld[3], "floor") && (strstr(fld[4], "crib_end_table") || strstr(fld[4], "crib_bench"))) it->kind = SL_SIDE;
        else continue;                                       /* other slot kinds: the house and mansion */
        if (!strncmp(fld[4], "model:", 6)) {                /* CC0 decor: "model:<name>,<size>,<height>" */
            char *c1;
            snprintf(it->model, sizeof it->model, "%s", fld[4] + 6);
            it->msize = 0.0f; it->mheight = 0.0f;
            if ((c1 = strchr(it->model, ',')) != NULL) {
                *c1 = 0;
                sscanf(c1 + 1, "%f,%f", &it->msize, &it->mheight);
            }
            s_nshop++;
            continue;
        }
#ifndef _WIN32
        continue;      /* the Crib cache is built on Windows only (nfl2k5_mycareer_crib.cpp): never sell what cannot show */
#endif
        {   /* a Crib piece, drawn from the Crib cache: "crib:<id>" in the room's model list */
            char *c1;
            snprintf(it->src, sizeof it->src, "%s", fld[4]);
            it->msize = 0.0f; it->mheight = 0.0f;
            if ((c1 = strchr(it->src, ',')) != NULL) { *c1 = 0; sscanf(c1 + 1, "%f,%f", &it->msize, &it->mheight); }
            snprintf(it->model, sizeof it->model, "crib:%s", it->id);
        }
        s_nshop++;
    }
    fclose(f);
    fprintf(stderr, "[MCHOME] shop: %d items the apartment can show, from %s\n", s_nshop, path);
}

static const ShopItem *shop_find(const char *id)
{
    int i;
    for (i = 0; i < s_nshop; i++) if (!strcmp(s_shop[i].id, id)) return &s_shop[i];
    return NULL;
}

static void place_load(void)
{
    int i;
    char k[32];
    if (!s_c.valid) return;
    for (i = 0; i < SLOTS; i++) {
        snprintf(k, sizeof k, "place_%s", k_slots[i].key);
        GetPrivateProfileStringA(s_c.key, k, "", s_place[i], sizeof s_place[i], s_ini);
    }
    if (!ini_get("furnished", 0)) {               /* a first home comes with a few Crib pieces */
        static const char *const starter[][2] = { { "C1", "potted_plant" } };
        int j;
        for (j = 0; j < 1; j++)
            for (i = 0; i < SLOTS; i++)
                if (!strcmp(k_slots[i].key, starter[j][0]) && !s_place[i][0]) {
                    snprintf(s_place[i], sizeof s_place[i], "%s", starter[j][1]);
                    snprintf(k, sizeof k, "place_%s", k_slots[i].key);
                    WritePrivateProfileStringA(s_c.key, k, s_place[i], s_ini);
                }
        WritePrivateProfileStringA(s_c.key, "furnished", "1", s_ini);
    }
    s_place_loaded = 1;
    s_items_dirty = 1;
}

/* The room's Crib items from the placements. */
static void items_from_places(void)
{
    int i;
    if (s_nshop < 0) shop_load();
    if (s_place_loaded < 0) place_load();
    if (s_place_loaded < 0) return;               /* no career yet: try again next frame */
    s_nitems = 0;
    {
        char old[sizeof s_shop_models];
        snprintf(old, sizeof old, "%s", s_shop_models);
        s_shop_models[0] = 0;
        for (i = 0; i < SLOTS; i++) {
            const ShopItem *si = s_place[i][0] ? shop_find(s_place[i]) : NULL;
            char b[160];
            if (!si || !si->model[0]) continue;
            {
                char ys[16];
                if (k_slots[i].dy < 0.0f) snprintf(ys, sizeof ys, "-1");
                else if (k_slots[i].kind == SL_WALL) snprintf(ys, sizeof ys, "c%.2f", k_slots[i].dy);
                else snprintf(ys, sizeof ys, "%.2f", k_slots[i].dy);
                snprintf(b, sizeof b, ";%s %.2f %.2f %.0f %s %.2f %.2f %s", si->model, k_slots[i].x, k_slots[i].z, k_slots[i].face,
                         ys, si->msize, si->mheight, k_slots[i].key);
            }
            if (strlen(s_shop_models) + strlen(b) < sizeof s_shop_models) strcat(s_shop_models, b);
        }
        if (strcmp(old, s_shop_models)) s_room_rebuild = 1;
    }
    for (i = 0; i < SLOTS && s_nitems < (int)(sizeof s_items / sizeof s_items[0]); i++) {
        const ShopItem *si = s_place[i][0] ? shop_find(s_place[i]) : NULL;
        RoomItem *it = &s_items[s_nitems];
        if (!si || si->model[0]) continue;
        memset(it, 0, sizeof *it);
        snprintf(it->node, sizeof it->node, "%s", si->node);
        it->x = k_slots[i].x; it->z = k_slots[i].z;
        it->yaw = k_slots[i].face - node_face(si->node);
        it->dy = k_slots[i].dy < 0.0f ? (k_slots[i].dy < -1.5f ? model_top("metal_office_desk") : model_top("side_table_01")) : k_slots[i].dy;
        snprintf(it->look, sizeof it->look, "%s", si->look);
        if (!strcmp(si->node, "standee_p01")) it->dy -= 4.6f;   /* measured (t52): the Crib hangs it high */
        it->scale = 1.0f;
        s_nitems++;
        if (!strcmp(si->node, "bobblestands") && s_nitems < (int)(sizeof s_items / sizeof s_items[0])) {
            s_items[s_nitems] = *it;                  /* the case's logos are a node of their own */
            snprintf(s_items[s_nitems].node, sizeof s_items[s_nitems].node, "bobblelogos");
            s_nitems++;
        }
    }
    s_items_dirty = 0;
}

/* The shop's list for the current kind: what the tier allows, cheapest first. */
static int shop_list(int out[], int max)
{
    int i, n = 0;
    for (i = 0; i < s_nshop && n < max; i++)
        if (s_shop[i].kind == s_h.shop_cat && s_shop[i].tier <= (s_c.tier > 2 ? 2 : s_c.tier)) out[n++] = i;
    return n;
}

static void shop_buy(const ShopItem *si)
{
    int i, slot = -1;
    char m[32], k[32];
    if (s_c.bank < si->price) {
        money(si->price - s_c.bank, m, sizeof m);
        snprintf(s_h.shop_msg, sizeof s_h.shop_msg, "Need %s more", m);
        sound(SND_CURSOR);
        return;
    }
    for (i = 0; i < SLOTS && slot < 0; i++) if (k_slots[i].kind == si->kind && !s_place[i][0]) slot = i;
    if (slot < 0) {                                /* all full: replace the first of the kind */
        for (i = 0; i < SLOTS && slot < 0; i++) if (k_slots[i].kind == si->kind) slot = i;
    }
    if (slot < 0) return;
    s_c.bank -= si->price;
    snprintf(s_place[slot], sizeof s_place[slot], "%s", si->id);
    snprintf(k, sizeof k, "place_%s", k_slots[slot].key);
    WritePrivateProfileStringA(s_c.key, k, si->id, s_ini);
    career_save();
    s_items_dirty = 1;
    money(si->price, m, sizeof m);
    snprintf(s_h.shop_msg, sizeof s_h.shop_msg, "Bought %s (%s) - it's in the room", si->name, m);
    fprintf(stderr, "[MCHOME] shop: bought %s for $%lld into %s; bank $%lld\n", si->id, si->price, k_slots[slot].key, s_c.bank);
    sound(SND_SELECT);
}

/* The shop's input, in the room: up/down picks, left/right changes the kind, A buys, B closes. */
static void shop_frame(uint32_t edge)
{
    int list[160], n;
    if (s_nshop < 0) shop_load();
    n = shop_list(list, 160);
    if (edge & (BTN_LEFT | BTN_RIGHT)) {
        s_h.shop_cat = (s_h.shop_cat + ((edge & BTN_RIGHT) ? 1 : SL_KINDS - 1)) % SL_KINDS;
        s_h.shop_pick = s_h.shop_top = 0; s_h.shop_msg[0] = 0;
        sound(SND_CURSOR);
        return;
    }
    if (n && (edge & BTN_DOWN)) { s_h.shop_pick = (s_h.shop_pick + 1) % n; s_h.shop_msg[0] = 0; sound(SND_CURSOR); }
    if (n && (edge & BTN_UP)) { s_h.shop_pick = (s_h.shop_pick + n - 1) % n; s_h.shop_msg[0] = 0; sound(SND_CURSOR); }
    if (s_h.shop_pick < s_h.shop_top) s_h.shop_top = s_h.shop_pick;
    if (s_h.shop_pick >= s_h.shop_top + 8) s_h.shop_top = s_h.shop_pick - 7;
    if (n && (edge & BTN_A)) shop_buy(&s_shop[list[s_h.shop_pick]]);
    if (edge & BTN_B) { s_h.shop = 0; sound(SND_SELECT); }
}

static void shop_draw(void)
{
    int list[160], n, i;
    char b[STR_CHARS], m[32];
    n = shop_list(list, 160);
    money(s_c.bank, m, sizeof m);
    snprintf(b, sizeof b, "FURNITURE SHOP   %s   Bank %s", k_slot_kind[s_h.shop_cat], m);
    text_s(0, b, 320.0f, 64.0f, 0xFFFFD040u);
    text_s(1, "|DPADLEFT| |DPADRIGHT| Kind      |CROSS| Buy      |CIRCLE| Back", 320.0f, 380.0f, 0xFFC0C0C0u);
    for (i = 0; i < 8 && s_h.shop_top + i < n; i++) {
        const ShopItem *si = &s_shop[list[s_h.shop_top + i]];
        int own = 0, k;
        for (k = 0; k < SLOTS; k++) if (!strcmp(s_place[k], si->id)) own = 1;
        money(si->price, m, sizeof m);
        snprintf(b, sizeof b, "%s%s   %s", si->name, own ? "  (in your home)" : "", m);
        text_s(2 + i, b, 320.0f, 110.0f + 30.0f * (float)i,
               s_h.shop_top + i == s_h.shop_pick ? 0xFFFFFFFFu : 0xFF909090u);
    }
    if (!n) text_s(2, "Nothing of this kind for this home yet", 320.0f, 140.0f, 0xFF909090u);
    if (s_h.shop_msg[0]) text_s(10, s_h.shop_msg, 320.0f, 350.0f, 0xFF80FF80u);
}

static void room_build_apartment(void);
static void host_draw_items(void)
{
    uint32_t n, a, i, k, save_flags[256], save_m[256][16];
    int it;
    if (!s_h.room || getenv("NFL2K5_MC_HOME_NO_ITEMS")) return;
    items_poll();
    if (s_room_rebuild) { s_room_rebuild = 0; room_build_apartment(); }
    if (s_nitems <= 0) return;
    n = MEM32(s_h.room + 0x24); a = MEM32(s_h.room + 0x28);
    if (n > 256) n = 256;
    for (i = 0; i < n; i++) {
        save_flags[i] = MEM32(a + i * 0x60u + 0x0Cu);
        for (k = 0; k < 16; k++) save_m[i][k] = MEM32(a + i * 0x60u + 0x20u + k * 4u);
        MEM32(a + i * 0x60u + 0x0Cu) = save_flags[i] | 0x10u;
    }
    /* our camera, in Crib centimetres */
    for (i = 0; i < 2; i++) {
        uint32_t dst = i ? s_h.at : s_h.eye;
        for (k = 0; k < 3; k++) MEMF(dst + k * 4u) = s_h.cur[i * 3 + k] * 100.0f;
        MEMF(dst + 12u) = 1.0f;
    }
    PUSH32(g_esp, s_h.at);
    gcall(sub_0002BDF0, s_h.camobj, s_h.eye);
    gcall(sub_0002B860, s_h.camobj, (uint32_t)(int)(s_h.cur[6] * 182.04445f));
    gcall(sub_0002AC80, s_h.camobj, 0);
    /* The camera's projection (viewport folded in, right-handed view): row 2 col 2 and row 3
     * col 2 give depth-buffer z = -m22 + m32 / distance, w = distance. The host scene writes the
     * same into the game's depth buffer, so these items hide behind our walls and furniture.
     * (A t40 readback of the Crib floor suggested a 1.352 factor, but that floor is sunken;
     * t59 with real Crib items in front of and behind our walls confirms the matrix as is.) */
    if (MEMF(s_h.camobj + 44u) == -1.0f) {
        s_room.zmap[0] = -MEMF(s_h.camobj + 40u);
        s_room.zmap[1] = MEMF(s_h.camobj + 56u);
        s_room.zmap[2] = 100.0f;
        s_room.zmap[3] = 1.0f;
    }
    if (getenv("RECOMP_HOSTSCENE_LOG")) {     /* research: the camera object, for the depth encoding */
        static int told;
        if (told++ == 120) {
            fprintf(stderr, "[MCCAM] eye %.1f %.1f %.1f at %.1f %.1f %.1f fov %.1f obj", s_h.cur[0], s_h.cur[1], s_h.cur[2],
                    s_h.cur[3], s_h.cur[4], s_h.cur[5], s_h.cur[6]);
            for (k = 0; k < 96; k++) fprintf(stderr, " %g", MEMF(s_h.camobj + k * 4u));
            fprintf(stderr, "\n");
        }
    }
    for (it = 0; it < s_nitems; it++) {
        for (i = 0; i < n; i++) {
            uint32_t nd = a + i * 0x60u, sub = MEM32(nd + 8u), m = nd + 0x20u;
            float cx, cy, cz, c, sn, e[2][3];
            int q;
            if (!weq(MEM32(nd), s_items[it].node) || sub < 0x10000u || sub >= 0xF0000000u) continue;
            {   /* scale about the placement: the node matrix (the camera move keeps the turn and the slot) */
                float sc = s_items[it].scale > 0.0f ? s_items[it].scale : 1.0f;
                for (q = 0; q < 16; q++) MEMF(m + (uint32_t)q * 4u) = 0.0f;
                MEMF(m + 0x00u) = sc; MEMF(m + 0x14u) = sc; MEMF(m + 0x28u) = sc; MEMF(m + 0x3Cu) = 1.0f;
                MEMF(m + 0x30u) = MEMF(sub + 0u) * (1.0f - sc);
                MEMF(m + 0x34u) = MEMF(sub + 4u) * (1.0f - sc);
                MEMF(m + 0x38u) = MEMF(sub + 8u) * (1.0f - sc);
            }
            /* The Crib draws each node only when the camera stands where the Crib's own rooms can
             * see it, so instead of moving the item into our room, our camera goes into the Crib:
             * the item stays where it is, and the camera is put where it would be if the item stood
             * on its slot (the inverse of: turn by yaw about the node's placement, then move it
             * to the slot). Same picture, same depth, and the Crib's culling is satisfied. */
            cx = MEMF(sub + 0u) + s_items[it].fx * 100.0f; cy = MEMF(sub + 4u); cz = MEMF(sub + 8u) + s_items[it].fz * 100.0f;
            c = cosf(s_items[it].yaw * 3.14159265f / 180.0f); sn = sinf(s_items[it].yaw * 3.14159265f / 180.0f);
            for (q = 0; q < 2; q++) {
                float rx = s_h.cur[q * 3 + 0] * 100.0f - s_items[it].x * 100.0f;
                float ry = s_h.cur[q * 3 + 1] * 100.0f - s_items[it].dy * 100.0f;
                float rz = s_h.cur[q * 3 + 2] * 100.0f - s_items[it].z * 100.0f;
                /* room = R(crib - centre) + slot with R the row-vector turn [c 0 -s; 0 1 0; s 0 c]:
                 * crib = R^T(room - slot) + centre */
                e[q][0] = rx * c - rz * sn + cx;
                e[q][1] = ry;
                e[q][2] = rx * sn + rz * c + cz;
                (void)cy;
            }
            for (q = 0; q < 3; q++) { MEMF(s_h.eye + q * 4u) = e[0][q]; MEMF(s_h.at + q * 4u) = e[1][q]; }
            PUSH32(g_esp, s_h.at);
            gcall(sub_0002BDF0, s_h.camobj, s_h.eye);
            gcall(sub_0002B860, s_h.camobj, (uint32_t)(int)(s_h.cur[6] * 182.04445f));
            gcall(sub_0002AC80, s_h.camobj, 0);
            {   /* "material=texture": a Crib texture-cache look for this draw (posters and the like) */
                uint32_t mat = 0, tex = 0, keep_tex = 0, keep_fl = 0;
                if (s_items[it].look[0] && s_h.cache_loaded) {
                    char mname[48], *eq;
                    uint32_t w = s_h.wstr[15];
                    snprintf(mname, sizeof mname, "%s", s_items[it].look);
                    eq = strchr(mname, '=');
                    *eq = 0;
                    mat = find_mat(mname);
                    h_put(w, eq + 1);
                    tex = lookup(s_h.w_ctx3, FOURCC_TXTR, w);
                    if (!tex) tex = lookup(s_h.w_ctx, FOURCC_TXTR, w);
                    if (mat && tex) {
                        keep_tex = MEM32(mat + 0x30u); keep_fl = MEM32(mat + 0x08u);
                        MEM32(mat + 0x30u) = tex; MEM32(mat + 0x08u) &= ~1u;
                    } else {
                        static int told;
                        if (told++ < 20) fprintf(stderr, "[MCHOME] look %s: material %08X texture %08X\n", s_items[it].look, mat, tex);
                        mat = 0;
                    }
                }
                MEM32(nd + 0x0Cu) = save_flags[i] & ~0x10u;
                gcall(sub_0002DE60, s_h.room, 0);
                MEM32(nd + 0x0Cu) = save_flags[i] | 0x10u;
                if (mat) { MEM32(mat + 0x30u) = keep_tex; MEM32(mat + 0x08u) = keep_fl; }
            }
        }
    }
    for (i = 0; i < n; i++) {
        MEM32(a + i * 0x60u + 0x0Cu) = save_flags[i];
        for (k = 0; k < 16; k++) MEM32(a + i * 0x60u + 0x20u + k * 4u) = save_m[i][k];
    }
}

static void host_draw_scene(void)
{
    static int logged;
    int i;
    for (i = 0; i < 3; i++) { s_room.eye[i] = s_h.cur[i]; s_room.at[i] = s_h.cur[3 + i]; }
    s_room.fov_y = s_h.cur[6];
    s_room.time = s_h.anim;
    s_room.highlight = s_h.top == SCR_HUB && !s_h.leaving ? s_hot_box[s_h.hot] : -1;
    if (s_h.cur[1] > 2.85f) {                 /* from above: no ceiling */
        static XboxHostScene top;
        int c;
        top = s_room;
        for (c = 0; c < 4; c++) if (s_ceil[c] >= 0) { int k; for (k = 0; k < 3; k++) top.box[s_ceil[c]].lo[k] = top.box[s_ceil[c]].hi[k] = -100.0f; }
        xbox_HostSceneSet(&top);
    } else xbox_HostSceneSet(&s_room);
    /* The trigger: a 2 x 2 rect with the first texture of the Crib's sky (the phone's if there
     * is no sky), which nothing else on these screens draws. The renderer drops that draw and
     * starts the next frame with the room, under all of the game's own drawing. A TXTR's +4
     * is its Data, the offset the pushbuffer's SET_TEXTURE_OFFSET carries. */
    {
        uint32_t trig = s_h.sky ? s_h.sky : s_h.phone;
        uint32_t ma = trig && MEM32(trig + 0x1Cu) ? MEM32(trig + 0x20u) : 0;
        uint32_t tx = ma ? MEM32(ma + 0x30u) : 0, data = tx >= 0x10000u && tx < 0xF0000000u ? MEM32(tx + 4u) : 0;
        if (data) {
            xbox_HostSceneTrigger(&data, 1);
            rect_tex(0.0f, 0.0f, 2.0f, 2.0f, tx, 0xFFFFFFFFu);
        }
        if (!logged++) fprintf(stderr, "[MCHOME] host room: trigger = 2D rect, texture %08X, data %08X\n", tx, data);
    }
}

/* The host renderer's projection, into the UI's 640 x 480: the surface is 720 x 480 and the
 * window shows its 640 x 448 action area (the UI's space), so the scene's aspect is the
 * window's widened by 720/640 and narrowed by 448/480. */
static int project_host(const float p[3], float *sx, float *sy)
{
    float f[3], r[3], u[3], d[3], len, z, t, ar;
    int i;
    for (i = 0; i < 3; i++) { f[i] = s_h.cur[3 + i] - s_h.cur[i]; d[i] = p[i] - s_h.cur[i]; }
    len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (len <= 0.0f) return 0;
    for (i = 0; i < 3; i++) f[i] /= len;
    r[0] = f[2]; r[1] = 0.0f; r[2] = -f[0];             /* up x f, as the host renderer */
    len = sqrtf(r[0] * r[0] + r[2] * r[2]); if (len <= 0.0f) return 0;
    r[0] /= len; r[2] /= len;
    u[0] = f[1] * r[2] - f[2] * r[1]; u[1] = f[2] * r[0] - f[0] * r[2]; u[2] = f[0] * r[1] - f[1] * r[0];
    z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 0.1f) return 0;
    t = 1.0f / tanf(s_h.cur[6] * 0.5f * 3.14159265f / 180.0f);
    ar = s_h.host_ar;
    *sx = (((d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / z) * t / ar * 0.5f + 0.5f) * 720.0f - 40.0f;
    *sy = (0.5f - ((d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / z) * t * 0.5f) * 480.0f;
    return *sx > 20.0f && *sx < 620.0f && *sy > 20.0f && *sy < 380.0f;
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

/* ---- stage 2: walking around the host room ---------------------------------------
 * Free mode (the default in the host room): left stick or D-pad up/down walks, right stick,
 * D-pad left/right or the shoulder buttons turn; the player is a 0.3 m circle that slides
 * along walls and furniture. The object the player faces (within ~25 degrees and 3.5 m)
 * glows and shows its prompt; A uses it. Y switches to the fixed stops and back. */
#include "../external/xboxrecomp/src/input/xinput_xbox.h"
extern void nfl2k5_input_last_pad(XBOX_GAMEPAD *g);


static struct { float x, z, yaw; int on, init; } s_free;

/* The boxes the player cannot walk into: anything between ankle and head height that is not
 * the floor, the ceiling, the rug or a light. */
static int free_blocked(float x, float z)
{
    const float r = 0.3f;
    int i;
    if (x < r || x > 7.0f - r || z < r || z > 9.0f - r) return 1;
    for (i = 0; i < s_room.nbox; i++) {
        const XboxHostBox *b = &s_room.box[i];
        if (b->hi[1] < 0.2f || b->lo[1] > 1.8f || b->mat == HS_MAT_EMISSIVE) continue;
        if (b->hi[0] - b->lo[0] > 6.0f && b->hi[2] - b->lo[2] > 6.0f) continue;       /* floor-sized */
        if (x + r > b->lo[0] && x - r < b->hi[0] && z + r > b->lo[2] && z - r < b->hi[2]) return 1;
    }
    for (i = 0; i < s_nmodels; i++) {        /* the furniture: its footprint, turned by its yaw */
        const RoomModel *m = &s_models[i];
        float hx = (m->b[3] - m->b[0]) * 0.5f * m->sc, hz = (m->b[5] - m->b[2]) * 0.5f * m->sc, c, sn, ex, ez;
        if ((m->b[4] - m->b[1]) * m->scy < 0.15f || m->y > 1.0f) continue;               /* rugs; things on things */
        c = fabsf(cosf(m->yaw * 3.14159265f / 180.0f)); sn = fabsf(sinf(m->yaw * 3.14159265f / 180.0f));
        ex = hx * c + hz * sn; ez = hx * sn + hz * c;
        if (fabsf(x - m->x) < ex + r && fabsf(z - m->z) < ez + r) return 1;
    }
    return 0;
}

static void box_centre(int bi, float c[3])
{
    const XboxHostBox *b = &s_room.box[bi];
    int k;
    for (k = 0; k < 3; k++) c[k] = (b->lo[k] + b->hi[k]) * 0.5f;
}

/* The hotspot the player is looking at, or -1. */
static int free_target(float *cos_out)
{
    float fx = sinf(s_free.yaw), fz = cosf(s_free.yaw), best = 0.90f;
    int h, pick = -1;
    for (h = 0; h < HOTS; h++) {
        float c[3], dx, dz, d, cs;
        if (s_hot_box[h] < 0) continue;
        box_centre(s_hot_box[h], c);
        dx = c[0] - s_free.x; dz = c[2] - s_free.z;
        d = sqrtf(dx * dx + dz * dz);
        if (d > 3.5f || d < 0.05f) continue;
        cs = (dx * fx + dz * fz) / d;
        if (cs > best) { best = cs; pick = h; }
    }
    if (cos_out) *cos_out = best;
    return pick;
}

/* Returns 1 when it handled the frame (free mode), 0 for the fixed stops. */
static int free_frame(uint32_t fsm, uint32_t edge)
{
    float dt = frame_dt(fsm), mv = 0.0f, turn = 0.0f, nx, nz;
    XBOX_INPUT_STATE st;
    int i, y_edge;
    if (!s_free.init) { s_free.init = 1; s_free.on = 1; s_free.x = 3.5f; s_free.z = 1.0f; s_free.yaw = 0.0f; }
    memset(&st, 0, sizeof st);
    nfl2k5_input_last_pad(&st.Gamepad);
    {
        float ly = st.Gamepad.sThumbLY / 32768.0f, rx = st.Gamepad.sThumbRX / 32768.0f;
        if (fabsf(ly) > 0.2f) mv += ly;
        if (fabsf(rx) > 0.2f) turn += rx;
        y_edge = (edge & BTN_Y) != 0;                                     /* the engine's own Y bit */
        s_h.y_was = st.Gamepad.bAnalogButtons[3] > 128;
        if (st.Gamepad.bAnalogButtons[4] > 128) turn += 1.0f;             /* black / RB turns right */
        if (st.Gamepad.bAnalogButtons[5] > 128) turn -= 1.0f;             /* white / LB turns left */
    }
    if (edge & ~(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_A | BTN_B) && edge) MH_LOG("free: pad edge %08X\n", edge);
    if (y_edge) {
        s_free.on = !s_free.on;
        sound(SND_CURSOR);
        fprintf(stderr, "[MCHOME] host room: %s\n", s_free.on ? "walking" : "fixed stops");
        if (!s_free.on) { s_h.hot = HS_DOOR; return 0; }
    }
    if (!s_free.on) return 0;
    /* held D-pad from the merged pad: in t21 the engine word moved the player far less than a hold should */
    if (st.Gamepad.wButtons & 0x01) mv += 1.0f;
    if (st.Gamepad.wButtons & 0x02) mv -= 1.0f;
    if (st.Gamepad.wButtons & 0x08) turn += 1.0f;
    if (st.Gamepad.wButtons & 0x04) turn -= 1.0f;
    s_free.yaw += turn * 2.0f * dt;
    nx = s_free.x + sinf(s_free.yaw) * mv * 2.2f * dt;
    nz = s_free.z + cosf(s_free.yaw) * mv * 2.2f * dt;
    if (!free_blocked(nx, s_free.z)) s_free.x = nx;           /* slide along whatever blocks */
    if (!free_blocked(s_free.x, nz)) s_free.z = nz;
    s_h.anim += dt;
    s_h.cur[0] = s_free.x; s_h.cur[1] = 1.6f; s_h.cur[2] = s_free.z;
    s_h.cur[3] = s_free.x + sinf(s_free.yaw); s_h.cur[4] = 1.5f; s_h.cur[5] = s_free.z + cosf(s_free.yaw);
    s_h.cur[6] = 62.0f;
    s_h.cur_set = 1;
    s_h.free_target = free_target(NULL);
    if (s_h.free_target >= 0) s_h.hot = s_h.free_target;
    if ((edge & BTN_A) && s_h.free_target >= 0) {
        /* glide from where the player stands to the object, then the hotspot's action */
        float c[3];
        Stop *st2 = &s_host_stop[s_h.hot];
        box_centre(s_hot_box[s_h.hot], c);
        for (i = 0; i < 3; i++) { st2->eye[i] = s_h.cur[i]; st2->at[i] = c[i]; }
        st2->fov = 62.0f;
        return -1;                                            /* the caller runs the hotspot */
    }
    if (edge & BTN_B) return -2;
    return 1;
}


static void hub_frame(uint32_t fsm)
{
    uint32_t edge;
    int h = s_h.hot;
    if (s_h.shop) { camera_step(fsm, 0.0f); shop_frame(pad_edges()); return; }
    if (s_h.host && s_h.leaving) { camera_step(fsm, 0.0f); return; }
    if (s_h.host && !s_h.leaving) {
        int f;
        edge = pad_edges();
        f = free_frame(fsm, edge);
        if (f == 1) return;
        if (f == -2) { sound(SND_SELECT); s_h.hot = HS_MENU; s_h.manual = 1; leave(0); return; }
        h = s_h.hot;                              /* free mode may have moved it */
        if (f == -1) edge = BTN_A;                /* use the hotspot looked at, as the fixed stops do */
        else if (camera_step(fsm, 0.0f)) return;
    } else {
        if (camera_step(fsm, 0.0f)) return;
        if (s_h.leaving) return;
        edge = pad_edges();
    }
    if (edge & (BTN_RIGHT | BTN_DOWN)) h = (h + 1) % HOTS;
    if (edge & (BTN_LEFT | BTN_UP)) h = (h + HOTS - 1) % HOTS;
    if (h != s_h.hot) { s_h.hot = h; sound(SND_CURSOR); MH_LOG("hotspot %s\n", k_hot_name[h]); return; }
    if (edge & BTN_A) {
        sound(SND_SELECT);
        switch (s_h.hot) {
        case HS_DOOR: gcall(sub_0006E390, fsm, s_h.header[SCR_DOOR]); break;
        case HS_COMPUTER: gcall(sub_0006E390, fsm, s_h.header[SCR_PC]); break;
        case HS_PHONE: gcall(sub_0006E390, fsm, s_h.header[SCR_PHONE]); break;
        case HS_MIRROR: gcall(sub_0006E390, fsm, s_h.header[SCR_MIRROR]); break;
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
    gcall(sub_0002AD80, s_h.camsave, 0);
    if (s_h.host) {
        const char *xf = getenv("NFL2K5_MC_HOME_CRIBXF");
        host_draw_scene();
        if (!xf) host_draw_items();
        /* Research (Crib furniture in the host room): the game draws the Crib scene with its
         * camera put where ours is. Crib units are centimetres; "sx sz ox oy oz" maps
         * crib = (s * host * 100) + o, so the Crib's own items can be lined up with our room. */
        if (xf) {
            float m[5] = { 1, 1, 0, 0, 0 };
            sscanf(xf, "%f %f %f %f %f", &m[0], &m[1], &m[2], &m[3], &m[4]);
            for (i = 0; i < 2; i++) {
                const float *v = s_h.cur + i * 3;
                uint32_t dst = i ? s_h.at : s_h.eye;
                MEMF(dst + 0u) = m[0] * v[0] * 100.0f + m[2];
                MEMF(dst + 4u) = v[1] * 100.0f + m[3];
                MEMF(dst + 8u) = m[1] * v[2] * 100.0f + m[4];
                MEMF(dst + 12u) = 1.0f;
            }
            PUSH32(g_esp, s_h.at);
            gcall(sub_0002BDF0, s_h.camobj, s_h.eye);
            gcall(sub_0002B860, s_h.camobj, (uint32_t)(int)(s_h.cur[6] * 182.04445f));
            gcall(sub_0002AC80, s_h.camobj, 0);
            crib_only(1);
            gcall(sub_0002DE60, s_h.room, 0);
            crib_only(0);
        }
    } else {
        for (i = 0; i < 3; i++) { MEMF(s_h.eye + i * 4u) = s_h.cur[i]; MEMF(s_h.at + i * 4u) = s_h.cur[3 + i]; }
        MEMF(s_h.eye + 12u) = 1.0f; MEMF(s_h.at + 12u) = 1.0f;
        PUSH32(g_esp, s_h.at);
        gcall(sub_0002BDF0, s_h.camobj, s_h.eye);
        gcall(sub_0002B860, s_h.camobj, (uint32_t)(int)(s_h.cur[6] * 182.04445f));
        gcall(sub_0002AC80, s_h.camobj, 0);
        if (s_h.sky) gcall(sub_0002DE60, s_h.sky, 0);
        gcall(sub_0002DE60, s_h.room, 0);
        if (s_h.phone) draw_scene_at(s_h.phone, s_h.phone_at);
        if (s_h.top == SCR_HUB && s_h.cursor && !s_h.leaving && s_h.marker3d) draw_scene_at(s_h.cursor, STOP(s_h.hot).mark);
    }
    {
        uint32_t ui = gcall(sub_00069180, 0, 0);
        gcall(sub_0002AC80, ui, 0);
        if (s_h.top == SCR_HUB) rect(0.0f, 392.0f, 640.0f, 60.0f, 0xA0000000u);
        if (s_h.top == SCR_HUB && s_h.shop) rect(40.0f, 40.0f, 560.0f, 360.0f, 0xC8101014u);
        if (s_h.top == SCR_CALL) {
            rect(0.0f, 0.0f, 640.0f, 480.0f, 0xB0000000u);
            rect(40.0f, 96.0f, 232.0f, 232.0f, 0xFF26254Au);
            if (s_h.who == 1) coach_portrait_poll();
            if (s_h.who == 1 && (s_coach_tex || s_coach_logo))
                rect_tex(56.0f, 112.0f, 200.0f, 200.0f, s_coach_tex ? s_coach_tex : s_coach_logo, 0xFFFFFFFFu);
            else {
                uint32_t icon = lookup(0, FOURCC_TXTR, s_h.w_phone_icon);
                if (icon) rect_tex(64.0f, 120.0f, 184.0f, 184.0f, icon, 0xFFFFFFFFu);
            }
            if (s_h.phase == 1) rect(300.0f, 266.0f + 26.0f * (float)s_h.pick, 312.0f, 22.0f,
                                     (uint32_t)(0x90 + (int)(60.0f * sinf(s_h.anim * 5.0f))) << 24 | 0x26254Au);
        }
        gcall(sub_0002AC80, s_h.camsave, 0);
    }
    if (s_h.top == SCR_HUB && s_h.shop) { shop_draw(); return; }
    if (s_h.top == SCR_HUB) {
        char b[STR_CHARS];
        hub_title(b, sizeof b);
        h_put(s_h.title, b);
        text(s_h.title, 0, 150.0f, 36.0f, 0xFFFFFFFFu);
        text(s_h.w_name[s_h.hot], 0, 160.0f, 398.0f, 0xFFFFD040u);
        text(s_h.w_what[s_h.hot], 0, 160.0f, 422.0f, 0xFFFFFFFFu);
        text(s_h.hot == HS_MENU ? s_h.w_prompt_menu : s_h.w_prompt, 0, 470.0f, 410.0f, 0xFFFFFFFFu);
        if (!s_h.leaving && !s_h.marker3d && (s_h.host ? project_host(STOP(s_h.hot).mark, &sx, &sy) : project(STOP(s_h.hot).mark, &sx, &sy)))
            text(s_h.w_glyph, 0, sx, sy - 12.0f + 5.0f * sinf(s_h.anim * 4.0f), 0xFFFFFFFFu);
        if (s_h.host && getenv("NFL2K5_MC_HOME_TAGS")) {
            int t;
            for (t = 0; t < HOTS; t++)
                if (project_host(STOP(t).mark, &sx, &sy)) text(s_h.w_name[t], 0, sx, sy - 10.0f, 0xFF40E0FFu);
            text_s(14, "TEST GEOMETRY", 560.0f, 36.0f, 0xFFFF6060u);
        }
    } else if (s_h.top == SCR_CALL) call_draw();
}

/* ---- the coach on the phone ------------------------------------------------------
 * The MyPlayer's team is the roster team holding his record (sub_000C4060, as the edit
 * pages' create does); its head coach is the team's coach record (+0x14C: first and last
 * name at +0x00 / +0x04, portrait id at +0x40). SOFTDRINK keeps its 2026 coaches in its own
 * table (SD_COACHES: team index +0x08, team ident +0x0A, portrait +0x0C, alternative +0x0E,
 * L"TEAM Lastname" +0x10) and picks the portrait from it in 0x014E15DE; we read it the same
 * way, so the coach follows the pack's data. The portrait is the game's own TXTR "%04d" in
 * Portrait.iff (ctx L"PORTRAIT", sub_000E71E0), the one Coach Match Up shows. No portrait:
 * the team's logo from logos.iff, else the phone icon. */

static uint32_t res_ptr(uint32_t field)
{
    uint32_t p = MEM32(field);
    if (!p) return 0;
    if ((p >= 0x10000u && p < 0x04000000u) || (p >= 0x80000000u && p < 0x88000000u)) return p;
    p += field;
    return p >= 0x10000u && p < 0x04000000u ? p : 0;
}

static uint32_t find_tex(uint32_t ctx, const char *name)
{
    uint32_t w = s_h.wstr[15];
    h_put(w, name);
    PUSH32(g_esp, w);
    if (!gcall(sub_00042FF0, ctx, FOURCC_TXTR)) return 0;
    return lookup(ctx, FOURCC_TXTR, w);
}

static uint32_t my_team(uint32_t rec, int *index)
{
    uint32_t r = MEM32(0x00B72918u), n, tbl, i;
    if (r < 0x10000u || r >= 0x04000000u) return 0;
    n = MEM32(r + 0x18u); tbl = MEM32(r + 0x1Cu);
    if (n > 0x80u || tbl < 0x10000u) return 0;
    for (i = 0; i < n; i++) {
        uint32_t slot = tbl + i * 0x1F4u;
        if (gcall(sub_000C4060, slot, rec)) { *index = (int)i; return slot; }
    }
    return 0;
}

static void coach_resolve(void)
{
    uint32_t rec = MEM32(SD_PLAYER), slot, coach, k;
    int index = -1, pid = 0;
    char first[32] = "", last[32] = "", code[8] = "", packname[32] = "";
    const char *how = "none";
    s_coach_tex = 0;
    s_h.coach_team[0] = 0;
    s_h.coach_portrait = 0;
    s_h.coach_name[0] = 0;
    if (rec < 0x10000u || rec >= 0xF0000000u) return;
    slot = my_team(rec, &index);
    {   /* tests only: NFL2K5_MC_HOME_TEAM=<roster team index> shows that team's coach */
        const char *t = getenv("NFL2K5_MC_HOME_TEAM");
        uint32_t r = MEM32(0x00B72918u);
        if (t && *t && r >= 0x10000u && r < 0x04000000u && (uint32_t)atoi(t) < MEM32(r + 0x18u)) {
            index = atoi(t);
            slot = MEM32(r + 0x1Cu) + (uint32_t)index * 0x1F4u;
        }
    }
    if (!slot) { fprintf(stderr, "[MCHOME] coach: the MyPlayer is on no roster team\n"); return; }
    wtoa(res_ptr(slot + 0x10Cu), code, sizeof code);
    snprintf(s_h.coach_team, sizeof s_h.coach_team, "%s", code);
    coach = res_ptr(slot + 0x14Cu);
    if (coach) {
        rec_name(coach, 0x00, first, sizeof first);
        rec_name(coach, 0x04, last, sizeof last);
        pid = MEM16(coach + 0x40u);
        how = "coach record";
    }
    for (k = 0; k < SD_COACHES_N; k++) {
        uint32_t e = SD_COACHES + k * 0x2Cu;
        uint16_t c0 = MEM16(e + 0x10u);
        if (c0 < 'A' || c0 > 'Z') break;                         /* not the table this expects */
        if (MEM16(e + 0x08u) != (uint32_t)index || MEM16(e + 0x0Au) != MEM16(slot + 0x118u) || MEM32(slot + 0x128u)) continue;
        {
            int tp = MEM16(e + 0x0Cu), alt = MEM16(e + 0x0Eu);
            if (alt && pid == alt) tp = alt;                      /* as 0x014E15DE */
            pid = tp;
        }
        wtoa(e + 0x10u, packname, sizeof packname);
        how = "SOFTDRINK coach table";
        break;
    }
    /* the name: the coach record's, unless the pack's table names someone else */
    if (packname[0]) {
        const char *sp = strchr(packname, ' ');
        if (sp && _stricmp(sp + 1, last)) { snprintf(last, sizeof last, "%s", sp + 1); first[0] = 0; }
    }
    snprintf(s_h.coach_name, sizeof s_h.coach_name, "%s%s%s", first, first[0] ? " " : "", last);
    s_h.coach_portrait = pid;
    s_coach_logo = 0;
    s_coach_t0 = GetTickCount();
    coach_portrait_poll();                                        /* asks Portrait.iff / logos.iff to stream them in */
    fprintf(stderr, "[MCHOME] coach: team %d %s, coach record %08X \"%s\" (pack: \"%s\"), portrait %04d via %s, logo %08X\n",
            index, code, coach, s_h.coach_name, packname, pid, how, s_coach_logo);
}

/* Portrait.iff streams: the first ask starts the load and finds nothing, later asks find it
 * (sub_00042EA0 states 1 / 2). Asked every frame of the call, so the panel shows the photo as
 * soon as it is in and never holds a pointer the cache may have dropped. */
static void coach_portrait_poll(void)
{
    char b[24];
    uint32_t t;
    if (s_h.coach_team[0] && !s_coach_tex) {                     /* L"LOGOS": shown until the portrait is in */
        snprintf(b, sizeof b, "logo_%s_0", s_h.coach_team);
        s_coach_logo = find_tex(0x00E6BFB8u, b);
    }
    if (!s_h.coach_portrait || getenv("NFL2K5_MC_HOME_NOPORTRAIT")) return;   /* tests: the logo panel */
    snprintf(b, sizeof b, "%04d", s_h.coach_portrait);
    t = find_tex(PORTRAIT_CTX, b);
    if (t && !s_coach_tex)
        fprintf(stderr, "[MCHOME] coach portrait %s in after %lu ms: texture %08X\n", b, (unsigned long)(GetTickCount() - s_coach_t0), t);
    s_coach_tex = t;
}

/* Hook for spoken lines (not built: voices are on hold). Every line the call screen types
 * comes through here with who (0 agent, 1 coach), a stable key and the text, so a voice
 * player can be added later without touching the call logic. */
static void call_line(int who, const char *key, const char *text)
{
    MH_LOG("line %s %s: \"%s\"\n", who ? "coach" : "agent", key, text);
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
    else if (s_h.coach_name[0])
        snprintf(b, n, "Coach %s here. Make it quick, we've got film to watch.", strrchr(s_h.coach_name, ' ') ? strrchr(s_h.coach_name, ' ') + 1 : s_h.coach_name);
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
    if (who) coach_resolve();
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
        if (s_h.call_t > 1.4f || (edge & BTN_A)) {
            char g[320];
            s_h.phase = 1; s_h.call_t = 0.0f;
            call_greeting(g, sizeof g);
            call_line(s_h.who, "greeting", g);
        }
        return;
    }
    if (s_h.phase == 1) {
        if (edge & BTN_DOWN) { s_h.pick = (s_h.pick + 1) % s_h.nopts; sound(SND_CURSOR); }
        if (edge & BTN_UP) { s_h.pick = (s_h.pick + s_h.nopts - 1) % s_h.nopts; sound(SND_CURSOR); }
        if (edge & BTN_A) {
            sound(SND_SELECT);
            if (s_h.pick == s_h.nopts - 1) { gcall(sub_0006E400, fsm, 0); return; }
            call_answer(s_h.pick);
            { char key[16]; snprintf(key, sizeof key, "answer%d", s_h.pick); call_line(s_h.who, key, s_h.reply); }
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
    char who[64], b[320];
    int i, n;
    if (s_h.who) snprintf(who, sizeof who, "%s%s", s_h.coach_name[0] ? "Coach " : "Head Coach", s_h.coach_name);
    else snprintf(who, sizeof who, "Marcus Hale, Agent");
    text_s(0, s_h.who ? "HEAD COACH" : "AGENT", 156.0f, 70.0f, 0xFFC0C0C0u);
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

/* The mirror, from Career Home: sub_00346730's set-up, then our first page: the body page
 * (Edit Appearance) or the MyPlayer's position's Player Attributes page (Edit Attributes;
 * the game's own ratings editor, which Edit Player reaches after the equipment page). */
static void mirror_go(uint32_t fsm, int attrs)
{
    uint32_t rec = MEM32(SD_PLAYER), i, real, pos;
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
    s_h.mirror_mode = attrs;
    pos = MEM8(rec + 0x35u);
    if (attrs) {
        if (pos >= ATTR_POSITIONS || !s_h.mirror_attr[pos]) {
            fprintf(stderr, "[MCHOME] mirror: no attributes page for position %u\n", pos);
            s_h.rec = 0;
            return;
        }
        MEM32(0x00CB8830u) = 0;                   /* as sub_00344EB0 does before the attributes page */
        fprintf(stderr, "[MCHOME] mirror: attributes, record %08X, position %u, page %08X\n", rec, pos, s_h.mirror_attr[pos]);
        gcall(sub_0006E390, fsm, s_h.mirror_attr[pos]);
        return;
    }
    fprintf(stderr, "[MCHOME] mirror: appearance, record %08X, %s face\n", rec, real ? "real" : "created");
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
    else if (off == 0xC00u || off == 0xC08u) mirror_go(fsm, off == 0xC08u);
    else if (off == 0xC04u) mirror_next(fsm);
    else {
        int scr = (int)((off - 0x800u) / 0x40u), row = (int)(((off - 0x800u) % 0x40u) / 4u);
        if (scr > 0 && scr < SCREENS && row < 5 && !s_h.leaving) {
            uint32_t act = k_rows[scr][row].action;
            MH_LOG("row %s\n", k_rows[scr][row].label);
            if (act == ACT_INFO) sound(SND_CURSOR);
            else if (act == ACT_BUY) buy();
            else if (act == ACT_SHOP) {
                sound(SND_SELECT);
                s_h.shop = 1; s_h.shop_cat = 0; s_h.shop_pick = s_h.shop_top = 0; s_h.shop_msg[0] = 0;
                gcall(sub_0006E400, fsm, 0);          /* back to the room, the shop over it */
            }
            else if (act == ACT_AGENT || act == ACT_COACH) { sound(SND_SELECT); call_open(fsm, act == ACT_COACH); }
            else if (act == ACT_APPEAR || act == ACT_ATTR) { sound(SND_SELECT); leave(act == ACT_ATTR ? MH_MIRROR_ATTR : MH_MIRROR_LOOK); }
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
        if (a >= 0xFE000000u) { mirror_go(fsm, a == MH_MIRROR_ATTR); return SD_HANDLER_RET; }   /* ours: the interpreter cannot continue there */
        return a;                                  /* tail call: ecx = FSM, returns to the handler's caller */
    }
    if (s_h.rec) {                                /* back from the mirror: what changed in the record */
        uint32_t i, n = 0;
        char b[256] = "";
        for (i = 0; i < REC_BYTES; i++)
            if (MEM8(s_h.rec + i) != s_h.rec_before[i] && n++ < 24)
                snprintf(b + strlen(b), sizeof b - strlen(b), " +%02X:%02X>%02X", i, s_h.rec_before[i], MEM8(s_h.rec + i));
        fprintf(stderr, "[MCHOME] mirror (%s) done: %u of %u record bytes changed:%s\n", s_h.mirror_mode ? "attributes" : "appearance",
                n, REC_BYTES, b);
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
static uint32_t clone_page(uint32_t src, int first, uint32_t start_fn)
{
    uint32_t page = h_alloc(MH_ROW, 4), ev = MEM32(src + 4u), t = h_alloc(8u * 10u, 4), n = 0, k, start;
    for (k = 0; k < 0x2Cu; k += 4) MEM32(page + k) = MEM32(src + k);
    start = h_alloc(0x48u, 4);
    MEM32(start) = 1;
    MEM32(start + 4u) = start_fn;
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
    {   /* the host-drawn apartment on Windows; on Linux and Android its pass (gx through glslang) shows
     * black in testing (2026-11-07), so there the home is the Crib corner, which the game draws
     * itself. NFL2K5_MC_HOME_ROOM=host / =crib chooses either anywhere. */
    const char *h = getenv("NFL2K5_MC_HOME_ROOM");
#ifdef _WIN32
    s_h.host = !(h && !_stricmp(h, "crib"));
#else
    s_h.host = h && !_stricmp(h, "host");
#endif
}
    s_h.host_ar = 16.0f / 9.0f * (720.0f / 640.0f) * (448.0f / 480.0f);
    if (s_h.host) room_build_apartment();
    s_h.phone_at[0] = 2560.0f; s_h.phone_at[1] = 8.0f; s_h.phone_at[2] = -1150.0f;
    { const char *p = getenv("NFL2K5_MC_HOME_PHONE_AT"); if (p) sscanf(p, "%f %f %f", &s_h.phone_at[0], &s_h.phone_at[1], &s_h.phone_at[2]); }
    ini_path();
    s_h.size = 32768u;
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
    s_h.w_reflection = h_wstr("reflection");
    s_h.w_ctx2 = h_wstr("MCHOMEFR");
    s_h.slot3 = h_alloc(0x40, 16);
    s_h.w_ctx3 = h_wstr("MCHOME64");
    s_h.w_file3 = h_wstr("crib_64k.iff");
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
        static const int title_of[SCREENS] = { 0, HS_DOOR, HS_COMPUTER, HS_PHONE, HS_MIRROR, 0 };
        int n = 0, k;
        uint32_t rows;
        while (n < 5 && k_rows[i][n].label) n++;
        if (i == SCR_PC && !s_h.host && n > 4) n = 4;        /* no Furniture Shop without the host-drawn room */
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
    s_h.mirror_body = clone_page(0x0056F410u, 1, MH_FN_BASE + 0xC04u);
    s_h.mirror_equip = clone_page(0x0056F230u, 0, EDIT_FINISH);
    s_h.mirror_body_real = clone_page(0x0056FAA0u, 1, MH_FN_BASE + 0xC04u);
    s_h.mirror_equip_real = clone_page(0x0056F8C0u, 0, EDIT_FINISH);
    /* Player Attributes pages: each jump-table case is `mov ecx, esi; mov edx, page; pop esi;
     * jmp 0x6E390`. One page for the position: first (the identity page's create, destroy and
     * back records) and last (Start = Finish) at once. */
    for (i = 0; i < ATTR_POSITIONS; i++) {
        uint32_t c = MEM32(ATTR_PAGES + 4u * (uint32_t)i), page;
        if (c < 0x00344EB0u || c > 0x00344FE0u || MEM8(c) != 0x8Bu || MEM8(c + 1u) != 0xCEu || MEM8(c + 2u) != 0xBAu) continue;
        page = MEM32(c + 3u);
        if (page < 0x00500000u || page >= 0x00600000u) continue;
        s_h.mirror_attr[i] = clone_page(page, 1, EDIT_FINISH);
    }
    s_h.portrait_rec = h_alloc(0x48u, 4);
    s_h.w_nophoto = h_wstr("nophoto");
    if (!s_h.mirror_equip_real) { fprintf(stderr, "[MCHOME] off: out of guest memory (%u bytes)\n", s_h.used); return; }
    s_h.on = 1;
    recomp_interp_add_hook(SD_HANDLER, 0, ih_handler);
    fprintf(stderr, "[MCHOME] on: SOFTDRINK Career Home found, home screen %08X (%u bytes), save file %s\n",
            s_h.header[SCR_HUB], s_h.used, s_ini);
}

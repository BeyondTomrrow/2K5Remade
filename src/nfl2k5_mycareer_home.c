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
 *    eye and a target, FOV (sub_0002B860). We do the same with our own stops per row.
 *  - The home has no list: hotspots (door, mirror, computer, career menu), D-pad between them,
 *    the Crib's own `cursor` scene over the one selected, button prompts in the game's text
 *    tokens. The screen runs on the Crib's handler (0xF3DA0: events only, no layout).
 *  - Door and computer open small native lists over the room (Front Office layout from fr.iff).
 *  - Nothing is reimplemented: a choice glides to its object, pops back to Career Home and the
 *    next Career Home frame tail-jumps into the pack's own row action with ecx = FSM, the
 *    call the list dispatcher makes. Door = 0x014E93D1, the pack's next-game flow.
 *  - The mirror is the game's own edit-mode pages (0x56F410 body, 0x56F230 equipment, or the
 *    real-face pair), cloned only to stop before Position and ratings: sub_00346730's set-up,
 *    the first page gets the identity page's create / destroy / back, Start on the last
 *    page runs Finish (0x346C50).
 *
 * Inert unless the pack's MCHOMEX1 block and the rows are exactly where this expects them.
 * NFL2K5_MC_HOME=0 turns it off. NFL2K5_MC_HOME_CAM="ex ey ez tx ty tz fov;..." overrides the
 * stops (one per row, tests); NFL2K5_MC_HOME_HIDE="node,node" hides room nodes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "interp/x86_interp.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define MH_FN_BASE    0xFED10000u          /* + msg * 4; + 0x800 + row * 4: row actions */
#define MH_RET        0xFED1FFF0u
#define MH_ROW        0x34u
#define FEATURES      0x00525830u          /* link-list template (handler 0xF3E90, layout navigation) */
#define SND_SELECT    0x00E7FD70u          /* L"Ambient: Menu Select" */
#define SND_CURSOR    0x00E7FD3Cu
#define FOURCC_SCNE   0x454E4353u
#define FOURCC_LAYT   0x5459414Cu
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
#define ESPN_COM      0x00525664u
#define PAD_WORDS     0x00B37A78u          /* sub_00070A10: port * 0x244 */
#define BTN_A         0x00000100u          /* sub_00277570 */
#define BTN_B         0x00000220u
#define BTN_RIGHT     0x08000000u
#define BTN_LEFT      0x04000000u
#define BTN_DOWN      0x02000000u
#define BTN_UP        0x01000000u
#define REC_BYTES     0x54u

enum { HS_DOOR, HS_MIRROR, HS_COMPUTER, HS_MENU, HOTS };
enum { SCR_HUB, SCR_DOOR, SCR_PC, SCREENS };
static const char *const k_hot_name[HOTS] = { "Front Door", "Mirror", "Computer", "Career Menu" };
static const char *const k_hot_what[HOTS] = { "Game day or practice", "Change your look", "Stats, schedule, ESPN.com", "The full MyCareer list" };
/* list rows: label, the pack's action; link = a screen pushed over the room */
static const struct { const char *label; uint32_t action, link; } k_rows[SCREENS][4] = {
    { { 0 } },
    { { "Play Next Game", SD_ACT_NEXT, 0 }, { "Practice", SD_ACT_PRACTICE, 0 }, { 0 } },
    { { "My Stats", SD_ACT_PLAYER, 0 }, { "Schedule", SD_ACT_SCHED, 0 }, { "ESPN.com", 0, ESPN_COM }, { 0 } },
};

typedef struct { float eye[3], at[3], fov, mark[3]; } Stop;
/* Room space is the Crib's (cm, y up). mark = where the cursor scene floats. A choice
 * dollies part of the way towards `at`. */
static Stop s_stop[HOTS] = {
    { {  260.0f, 150.0f,  -950.0f }, {  260.0f,  90.0f,   327.0f }, 58.0f, {    0.0f, 300.0f,  320.0f } },
    { { -400.0f, 170.0f,  -300.0f }, {-1500.0f, 300.0f, -1400.0f }, 60.0f, {-1200.0f, 300.0f, -1100.0f } },
    { { 1500.0f, 150.0f,  -700.0f }, { 2500.0f, 100.0f, -1300.0f }, 58.0f, { 2500.0f, 250.0f, -1400.0f } },
    { { 2000.0f, 150.0f, -2200.0f }, { 3000.0f, 100.0f, -2500.0f }, 60.0f, { 3060.0f, 220.0f, -2500.0f } },
};

static struct {
    int on, log;
    uint32_t mem, used, size;
    uint32_t header[SCREENS], title, w_name[HOTS], w_what[HOTS], w_prompt, w_prompt_menu;
    uint32_t mirror_body, mirror_equip;     /* our clones, by face route */
    uint32_t mirror_body_real, mirror_equip_real;
    uint32_t w_cursor, cursor, mtx;
    uint32_t pad_prev[4];
    int hot, marker;                        /* the hotspot selected; draw the cursor scene */
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
    char hide[1024], swap[1024];
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

static uint32_t h_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), p = h_alloc(2u * (n + 1u), 4), i;
    if (!p) return 0;
    for (i = 0; i <= n; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)s[i];
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

/* ---- the room ------------------------------------------------------------- */

/* The first home: the Crib's collectibles out, and the surfaces its streaming item cache
 * (crib_16k/32k/64k.iff, sub_000441B0) would dress pointed at textures embedded in the room.
 * This table is the tier: another home is another list. */
static const char k_hide_default[] =
    "poster_p01,poster_p02,poster_p03,poster_p04,poster_p05,poster_p06,poster_p07,poster_p08,poster_p09,"
    "poster_p10,poster_p11,poster_p12,poster_p13,banner_v_p01,banner_v_p02,banner_v_p03,banner_v_p04,"
    "banner_h_p01,banner_h_p02,banner_h_p03,standee_p01,standee_p02,standee_p03,standee_p04,license_01,"
    "sign_interstate_01,sign_parking_01,sign_parking_02,sign_parking_03,sign_street_01,pennant_p01,"
    "pennant_p02,pennant_p03,pennant_p04,wall_art_player_p01,wall_art_player_p02,bobblestands,bobblelogos";
static const char k_swap_default[] =
    "floor_wood_01=wood_cherry,floor_wood_02=wood_cherry,floor_wood_03=wood_cherry,floor_wood_04=wood_cherry,"
    "floor_tile_01=concrete_m_01,floor_carpet_01=concrete_m_01,floor_carpet_02=concrete_m_01,"
    "floor_carpet_03=concrete_m_01,floor_carpet_04=concrete_m_01,rug_m_01=wood_cherry,rug_m_02=wood_cherry,"
    "rug_m_03=wood_cherry,rug_m_04=wood_cherry,trim_dark=wood_cherry";

static uint32_t find_mat(const char *name)
{
    uint32_t n = MEM32(s_h.room + 0x1C), a = MEM32(s_h.room + 0x20), i;
    for (i = 0; i < n && i < 256; i++)
        if (weq(MEM32(a + i * 0x80u), name)) return a + i * 0x80u;
    return 0;
}

static void room_swap(void)
{
    const char *p = s_h.swap;
    int done = 0, miss = 0;
    while (*p) {
        char pair[96], *eq;
        size_t l = strcspn(p, ",");
        snprintf(pair, sizeof pair, "%.*s", (int)l, p);
        if ((eq = strchr(pair, '=')) != NULL) {
            uint32_t to, from;
            *eq = 0;
            to = find_mat(pair); from = find_mat(eq + 1);
            if (to && from && MEM32(from + 0x30)) {
                MEM32(to + 0x30) = MEM32(from + 0x30);
                MEM32(to + 0x08) &= ~1u;
                done++;
            } else { miss++; MH_LOG("swap %s <- %s: to %08X from %08X\n", pair, eq + 1, to, from); }
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
    uint32_t n = MEM32(s_h.room + 0x24), a = MEM32(s_h.room + 0x28), i;
    char b[64];
    if (!s_h.log) return;
    for (i = 0; i < n && i < 256; i++) {
        wtoa(MEM32(a + i * 0x60u), b, sizeof b);
        MH_LOG("node %3u %-28s flags %08X\n", i, b, (unsigned)MEM32(a + i * 0x60u + 0x0Cu));
    }
    MH_LOG("room %08X: %u nodes, %u materials, %u cameras, %u markers, sky %08X\n", s_h.room, n,
           MEM32(s_h.room + 0x1C), MEM32(s_h.room + 0x3C), MEM32(s_h.room + 0x34), s_h.sky);
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
    /* fr.iff for the Front Office's layout only (title bar + slim list): the
     * navigation layout's panel would hide the room. MyCareer never loads it
     * under FRANCHISE1 (the pack replaces the desk), and this is our own context. */
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
        fprintf(stderr, "[MCHOME] layout front_office %08X\n", lay);
        for (int i = SCR_DOOR; i < SCREENS; i++) {
            MEM32(s_h.header[i] + 0x18) = lay ? s_h.w_layout : MEM32(FEATURES + 0x18);
            MEM32(s_h.header[i] + 0x1C) = lay ? 0 : MEM32(FEATURES + 0x1C);
        }
    }
    MEM32(s_h.room + 8) |= 1u;                    /* as sub_00275xxx does to its room */
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
    s_h.room = s_h.sky = s_h.cursor = 0;
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
    Stop t = s_stop[row];
    int n;
    if (!s_h.camfile[0] || GetTickCount() < next) return;
    next = GetTickCount() + 250;
    if (fopen_s(&f, s_h.camfile, "r") || !f) return;
    n = fscanf(f, "%f %f %f %f %f %f %f %f %f %f", &t.eye[0], &t.eye[1], &t.eye[2], &t.at[0], &t.at[1], &t.at[2], &t.fov,
               &t.mark[0], &t.mark[1], &t.mark[2]);
    if (n >= 7) s_stop[row] = t;
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

/* Camera ease for every screen of the home; true once the dolly ended and we popped. */
static int camera_step(uint32_t fsm, float zoom)
{
    float dt = MEMF(fsm + FSM_DT), want[7], k;
    int i;
    if (!(dt > 0.0f && dt < 1.0f)) dt = 1.0f / 60.0f;
    cam_poll(s_h.hot);
    if (s_h.leaving) {
        s_h.dolly += dt * 0.9f;
        gcall(sub_0014FC40, 0, 0);
    }
    stop_vec(&s_stop[s_h.hot], s_h.leaving ? (s_h.dolly > 0.4f ? 0.4f : s_h.dolly) : zoom, want);
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

static void hub_frame(uint32_t fsm)
{
    uint32_t edge = 0;
    int i, h = s_h.hot;
    if (camera_step(fsm, 0.0f)) return;
    if (s_h.leaving) return;
    for (i = 0; i < 4; i++) {
        uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
        edge |= w & ~s_h.pad_prev[i];
        if (w != s_h.pad_prev[i] && w) MH_LOG("pad %d word %08X\n", i, w);
        s_h.pad_prev[i] = w;
    }
    if (edge & (BTN_RIGHT | BTN_DOWN)) h = (h + 1) % HOTS;
    if (edge & (BTN_LEFT | BTN_UP)) h = (h + HOTS - 1) % HOTS;
    if (h != s_h.hot) { s_h.hot = h; sound(SND_CURSOR); MH_LOG("hotspot %s\n", k_hot_name[h]); return; }
    if (edge & BTN_A) {
        sound(SND_SELECT);
        switch (s_h.hot) {
        case HS_DOOR: gcall(sub_0006E390, fsm, s_h.header[SCR_DOOR]); break;
        case HS_COMPUTER: gcall(sub_0006E390, fsm, s_h.header[SCR_PC]); break;
        case HS_MIRROR: leave(MH_FN_BASE + 0xC00u); break;
        default: s_h.manual = 1; leave(0); break;
        }
        return;
    }
    if (edge & BTN_B) { sound(SND_SELECT); s_h.hot = HS_MENU; s_h.manual = 1; leave(0); }
}

/* Rect: sub_0026DE30(x, y, z, w, h, 0, texture, colour, 0, 0, 0), 640 x 480 space. */
static void rect(float x, float y, float w, float h, uint32_t argb)
{
    PUSH32(g_esp, 0); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, argb); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, fbits(h)); PUSH32(g_esp, fbits(w)); PUSH32(g_esp, fbits(20.0f));
    PUSH32(g_esp, fbits(y)); PUSH32(g_esp, fbits(x));
    gcall(sub_0026DE30, 0, 0);
}

/* Text: sub_0006BC30, the call the pack's own card makes (font table 0xA90ECC). */
static void text(uint32_t w, int font, float x, float y, uint32_t argb)
{
    uint32_t f = MEM32(0x00A90ECCu + (uint32_t)font * 4u);
    if (!f) return;
    PUSH32(g_esp, argb); PUSH32(g_esp, f); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, 0x43700000u); PUSH32(g_esp, 0x41A00000u);
    PUSH32(g_esp, fbits(y)); PUSH32(g_esp, fbits(x));
    gcall(sub_0006BC30, w, 0);
}

static void ev_draw(void)
{
    int i;
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
    if (s_h.top == SCR_HUB && s_h.cursor && !s_h.leaving && s_h.marker) {
        const Stop *t = &s_stop[s_h.hot];
        for (i = 0; i < 16; i++) MEMF(s_h.mtx + (uint32_t)i * 4u) = (i % 5) == 0 ? 1.0f : 0.0f;
        MEMF(s_h.mtx + 0x30u) = t->mark[0]; MEMF(s_h.mtx + 0x34u) = t->mark[1]; MEMF(s_h.mtx + 0x38u) = t->mark[2];
        PUSH32(g_esp, s_h.mtx);
        PUSH32(g_esp, s_h.cursor);
        gcall(sub_0026DFF0, 0, 0);
    }
    if (s_h.top == SCR_HUB) {
        uint32_t ui = gcall(sub_00069180, 0, 0);
        gcall(sub_0002AC80, ui, 0);
        rect(0.0f, 392.0f, 640.0f, 60.0f, 0xA0000000u);
        gcall(sub_0002AC80, s_h.camsave, 0);
        text(s_h.title, 0, 48.0f, 36.0f, 0xFFFFFFFFu);
        text(s_h.w_name[s_h.hot], 0, 48.0f, 398.0f, 0xFFFFD040u);
        text(s_h.w_what[s_h.hot], 0, 48.0f, 422.0f, 0xFFFFFFFFu);
        text(s_h.hot == HS_MENU ? s_h.w_prompt_menu : s_h.w_prompt, 0, 340.0f, 410.0f, 0xFFFFFFFFu);
    } else gcall(sub_0002AC80, s_h.camsave, 0);
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
        if (scr != SCR_HUB) gcall(sub_0014FCD0, fsm, 0);
        s_h.shown = s_h.loaded;
        s_h.top = scr;
        for (i = 0; i < 4; i++) s_h.pad_prev[i] = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
        break;
    }
    case 5: s_h.shown = 0; break;
    case 6:
        if (scr == SCR_HUB) hub_frame(fsm);
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
        if (scr > 0 && scr < SCREENS && !s_h.leaving) {
            sound(SND_SELECT);
            leave(k_rows[scr][row].action);
            MH_LOG("row %s\n", k_rows[scr][row].label);
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
    fprintf(stderr, "[MCHOME] Career Home on top: pushing the home\n");
    if (room_load()) { s_h.manual = 1; return 0; }  /* no room: the pack's list stays */
    gcall(sub_0006E390, fsm, s_h.header[SCR_HUB]);
    return SD_HANDLER_RET;
}

/* NFL2K5_MC_HOME_CRIBCAM=1 (tests): where the real Crib looks, once a second. */
static DWORD WINAPI cribcam_thread(LPVOID p)
{
    (void)p;
    for (;;) {
        Sleep(1000);
        if (MEM32(0x00AC7440u))
            fprintf(stderr, "[MCHOME] crib state %u: eye %.1f %.1f %.1f at %.1f %.1f %.1f fov %.2f station %u\n",
                    (unsigned)MEM32(0x00AC7448u), MEMF(0x00AC8530u), MEMF(0x00AC8534u), MEMF(0x00AC8538u),
                    MEMF(0x00AC8540u), MEMF(0x00AC8544u), MEMF(0x00AC8548u), MEMF(0x00AC8528u), (unsigned)MEM32(0x00AC8518u));
    }
    return 0;
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
    { const char *h = getenv("NFL2K5_MC_HOME_HIDE"); snprintf(s_h.hide, sizeof s_h.hide, "%s", h ? h : k_hide_default); }
    { const char *h = getenv("NFL2K5_MC_HOME_SWAP"); snprintf(s_h.swap, sizeof s_h.swap, "%s", h ? h : k_swap_default); }
    { const char *h = getenv("NFL2K5_MC_HOME_CAMFILE"); if (h) snprintf(s_h.camfile, sizeof s_h.camfile, "%s", h); }
    { const char *h = getenv("NFL2K5_MC_HOME_SLIM"); s_h.slim = !(h && *h == '0'); }
    {
        const char *c = getenv("NFL2K5_MC_HOME_CAM");
        for (i = 0; c && *c && i < HOTS; i++) {
            Stop t;
            if (sscanf(c, "%f %f %f %f %f %f %f", &t.eye[0], &t.eye[1], &t.eye[2], &t.at[0], &t.at[1], &t.at[2], &t.fov) == 7) s_stop[i] = t;
            c = strchr(c, ';');
            if (c) c++;
        }
    }
    s_h.size = 16384u;
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
    s_h.w_ctx = h_wstr("MCHOME");
    s_h.w_file = h_wstr("crib.iff");
    s_h.w_room = h_wstr("room");
    s_h.w_sky_day = h_wstr("skybox_day");
    s_h.w_ctx2 = h_wstr("MCHOMEFR");
    s_h.w_file2 = h_wstr("fr.iff");
    s_h.w_layout = h_wstr("front_office");
    s_h.title = h_wstr("Home");
    s_h.w_cursor = h_wstr("cursor");
    s_h.mtx = h_alloc(0x40, 16);
    s_h.w_prompt = h_wstr("|CROSS| Select   |CIRCLE| Career Menu");
    s_h.w_prompt_menu = h_wstr("|CROSS| Select");
    for (i = 0; i < HOTS; i++) { s_h.w_name[i] = h_wstr(k_hot_name[i]); s_h.w_what[i] = h_wstr(k_hot_what[i]); }
    for (i = 0; i < SCREENS; i++) s_h.header[i] = h_alloc(MH_ROW, 4);
    { const char *m = getenv("NFL2K5_MC_HOME_MARKER"); s_h.marker = !(m && *m == '0'); }
    /* The hub: the Crib's events-only handler, no rows, no layout. */
    for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header[SCR_HUB] + r) = MEM32(CRIB_DESC + r);
    /* The lists: the template's list, flags 0x10 (Back pops, no backdrop). */
    for (i = SCR_DOOR; i < SCREENS; i++) {
        int n = 0, k;
        uint32_t rows;
        while (n < 4 && k_rows[i][n].label) n++;
        rows = h_alloc(MH_ROW * (uint32_t)(n + 1), 4);
        for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header[i] + r) = MEM32(FEATURES + r);
        MEM32(s_h.header[i] + 0x00) = h_wstr(k_hot_name[i == SCR_DOOR ? HS_DOOR : HS_COMPUTER]);
        MEM32(s_h.header[i] + 0x0C) = 0;
        MEM32(s_h.header[i] + 0x10) = rows;
        MEM32(s_h.header[i] + 0x28) = 0x10u;
        for (k = 0; k < n; k++) {
            uint32_t row = rows + MH_ROW * (uint32_t)k;
            MEM32(row + 0x04) = h_wstr(k_rows[i][k].label);
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
    s_h.mirror_body = clone_page(0x0056F410u, 1);
    s_h.mirror_equip = clone_page(0x0056F230u, 0);
    s_h.mirror_body_real = clone_page(0x0056FAA0u, 1);
    s_h.mirror_equip_real = clone_page(0x0056F8C0u, 0);
    s_h.on = 1;
    recomp_interp_add_hook(SD_HANDLER, 0, ih_handler);
    if (getenv("NFL2K5_MC_HOME_CRIBCAM")) CloseHandle(CreateThread(NULL, 0, cribcam_thread, NULL, 0, NULL));
    fprintf(stderr, "[MCHOME] on: SOFTDRINK Career Home found, home screen %08X (%u bytes)\n", s_h.header[SCR_HUB], s_h.used);
}

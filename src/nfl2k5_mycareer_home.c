/* MyCareer home hub: our own layer on top of SOFTDRINK's MyCareer (prototype, branch
 * mycareer-home; not for release).
 *
 * SOFTDRINK's Career Home (descriptor 0x014F27D0, rows at 0x014BC0A0) is a plain list.
 * This puts a 3D home in front of it without changing a byte of the pack:
 *
 *  - One interpreter hook at 0x014EA768 (the pack's screen handler entry; the pack's
 *    code is always interpreted). On msg 6 with Career Home on top it pushes our screen.
 *  - Our screen is a link list like the Eras office (src/nfl2k5_eras_office.c), served
 *    from 0xFED10000. Its room is the retail Crib `room` + sky SCNE, loaded from crib.iff
 *    under our own context L"MCHOME" and always looked up with it, so the game's Crib
 *    state (0xAC7440.., the VIP Crib record, its save) is never read or written.
 *  - The Crib's camera is code, not curves (sub_00275B70): look-at (sub_0002BDF0) from an
 *    eye and a target, FOV (sub_0002B860). We do the same with our own stops per row.
 *  - A row does not reimplement anything. It glides to its object, pops back to Career
 *    Home, and the next Career Home frame tail-jumps into the pack's own row action
 *    with ecx = FSM: the same call the list dispatcher makes. Door = 0x014E93D1, the pack's
 *    next-game flow.
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

enum { ROW_DOOR, ROW_MIRROR, ROW_COMPUTER, ROW_MENU, ROWS };
static const char *const k_label[ROWS] = { "Play Next Game", "MyPlayer", "Schedule", "Career Menu" };
static const uint32_t k_action[ROWS] = { SD_ACT_NEXT, SD_ACT_PLAYER, SD_ACT_SCHED, 0 };

typedef struct { float eye[3], at[3], fov; } Stop;
/* Room space is the Crib's (cm, y up). The first entry per row is where the cursor
 * looks; arrival dollies a third of the way towards the target. */
static Stop s_stop[ROWS] = {
    { {    0.0f, 130.0f,  -620.0f }, {    0.0f, 110.0f,   327.0f }, 55.0f },   /* the front door */
    { {    0.0f, 130.0f,  -620.0f }, { -900.0f, 130.0f,  -300.0f }, 55.0f },
    { { 1900.0f, 130.0f, -1000.0f }, { 2500.0f, 150.0f, -1400.0f }, 55.0f },   /* the bar screens */
    { {    0.0f, 130.0f,  -620.0f }, {  900.0f, 130.0f,  -300.0f }, 55.0f },
};

static struct {
    int on, log;
    uint32_t mem, used, size;
    uint32_t header, rows, events, title;
    uint32_t camobj, camsave, slot, eye, at, light, vp;
    uint32_t w_ctx, w_file, w_room, w_sky_day, w_sky_night;
    int loaded;
    uint32_t room, sky;
    int shown;                     /* our screen is top: draw */
    float cur[7];                  /* eye, at, fov now */
    int cur_set;
    int leaving;                   /* row chosen: dolly, then pop */
    float dolly;
    uint32_t action;               /* run on Career Home's next frame */
    int cool;                      /* frames with no re-push after a row's action */
    int manual;                    /* Career Menu: stay on the pack's list */
    int last_row;
    char hide[256];
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
    gcall(sub_000432D0, 0, 0);
    s_h.room = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_room);
    s_h.sky = lookup(s_h.w_ctx, FOURCC_SCNE, s_h.w_sky_day);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    fprintf(stderr, "[MCHOME] crib.iff under MCHOME: room %08X sky %08X, %lu ms\n", s_h.room, s_h.sky,
            (unsigned long)(GetTickCount() - t0));
    if (!s_h.room) { gcall(sub_000432F0, s_h.w_ctx, 0); return -1; }
    s_h.loaded = 1;
    MEM32(s_h.room + 8) |= 1u;                    /* as sub_00275xxx does to its room */
    room_hide();
    room_log();
    return 0;
}

static void room_unload(void)
{
    if (!s_h.loaded) return;
    gcall(sub_000432F0, s_h.w_ctx, 0);
    s_h.loaded = 0;
    s_h.room = s_h.sky = 0;
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

static void stop_vec(const Stop *s, float d, float o[7])
{
    int i;
    for (i = 0; i < 3; i++) { o[i] = s->eye[i] + (s->at[i] - s->eye[i]) * d; o[3 + i] = s->at[i]; }
    o[6] = s->fov;
}

/* ---- screen events --------------------------------------------------------- */

static int top_row(uint32_t fsm) { return (int)MEM32(fsm + MEM32(fsm + 0x100u) * 8u + 4u); }

static void ev_frame(uint32_t fsm)
{
    float dt = MEMF(fsm + FSM_DT), want[7], k;
    int r = top_row(fsm), i;
    if (!(dt > 0.0f && dt < 1.0f)) dt = 1.0f / 60.0f;
    if (r < 0 || r >= ROWS) r = ROW_DOOR;
    if (r != s_h.last_row) { MH_LOG("cursor %d\n", r); s_h.last_row = r; }
    if (s_h.leaving) {
        s_h.dolly += dt * 0.9f;
        gcall(sub_0014FC40, 0, 0);
    }
    stop_vec(&s_stop[s_h.leaving ? s_h.leaving - 1 : r], s_h.leaving ? (s_h.dolly > 0.4f ? 0.4f : s_h.dolly) : 0.0f, want);
    k = dt * 5.0f; if (k > 1.0f) k = 1.0f;
    if (!s_h.cur_set) { memcpy(s_h.cur, want, sizeof want); s_h.cur_set = 1; }
    for (i = 0; i < 7; i++) s_h.cur[i] += (want[i] - s_h.cur[i]) * k;
    if (s_h.leaving && s_h.dolly >= 0.6f) {
        int row = s_h.leaving - 1;
        s_h.leaving = 0;
        s_h.dolly = 0.0f;
        gcall(sub_0014FC30, 0, 0);
        s_h.action = k_action[row];
        if (row == ROW_MENU) s_h.manual = 1;
        fprintf(stderr, "[MCHOME] row %d (%s): back to Career Home, action %08X\n", row, k_label[row], s_h.action);
        gcall(sub_0006E450, fsm, SD_HOME);
    }
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
    gcall(sub_0002AC80, s_h.camsave, 0);
}

static void mh_event(int msg, uint32_t fsm)
{
    switch (msg) {
    case 1:
        s_h.leaving = 0; s_h.dolly = 0.0f; s_h.cur_set = 0; s_h.last_row = -1; s_h.shown = 0;
        if (room_load() == 0) camera_init();
        break;
    case 2:
        s_h.shown = 0;
        gcall(sub_0014FC30, 0, 0);
        room_unload();
        break;
    case 4:
        gcall(sub_0014FCD0, fsm, 0);
        s_h.shown = s_h.loaded;
        MH_LOG("shown (room %08X)\n", s_h.room);
        break;
    case 5: s_h.shown = 0; break;
    case 6: ev_frame(fsm); break;
    case 7: ev_draw(); break;
    case 10: s_h.manual = 1; break;           /* B: the pack's list, as Career Menu */
    }
}

static RECOMP_TLS uint32_t s_called;

static void mh_fn(void)
{
    uint32_t a = s_called, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, fsm = g_ecx;
    uint32_t off = a - MH_FN_BASE, esp;
    g_esp += 4;                                   /* the return address; event and row calls are plain ret */
    esp = g_esp;
    if (off < 0x800u) mh_event((int)(off / 4u), fsm);
    else {
        int row = (int)((off - 0x800u) / 4u);
        if (row >= 0 && row < ROWS && !s_h.leaving) {
            sound(SND_SELECT);
            s_h.leaving = row + 1;
            s_h.dolly = 0.0f;
            MH_LOG("row %d chosen\n", row);
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
    if (msg == 5 && top != s_h.header && s_h.cool) s_h.cool = 0;
    if (top != SD_HOME) return 0;
    if (msg != 6) return 0;
    if (s_h.action) {
        uint32_t a = s_h.action;
        s_h.action = 0;
        s_h.cool = 600;                           /* room for the pack's fades before anything is pushed */
        fprintf(stderr, "[MCHOME] Career Home row action %08X\n", a);
        return a;                                  /* tail call: ecx = FSM, returns to the handler's caller */
    }
    if (s_h.cool > 0) { s_h.cool--; return 0; }
    if (s_h.manual) return 0;
    fprintf(stderr, "[MCHOME] Career Home on top: pushing the home\n");
    gcall(sub_0006E390, fsm, s_h.header);
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
    { const char *h = getenv("NFL2K5_MC_HOME_HIDE"); if (h) snprintf(s_h.hide, sizeof s_h.hide, "%s", h); }
    {
        const char *c = getenv("NFL2K5_MC_HOME_CAM");
        for (i = 0; c && *c && i < ROWS; i++) {
            Stop t;
            if (sscanf(c, "%f %f %f %f %f %f %f", &t.eye[0], &t.eye[1], &t.eye[2], &t.at[0], &t.at[1], &t.at[2], &t.fov) == 7) s_stop[i] = t;
            c = strchr(c, ';');
            if (c) c++;
        }
    }
    s_h.size = 8192u;
    s_h.mem = xbox_HeapAlloc(s_h.size, 16);
    if (!s_h.mem) return;
    s_h.camobj = h_alloc(0x2C0, 16);
    s_h.camsave = h_alloc(0x2C0, 16);
    s_h.slot = h_alloc(0x40, 16);
    s_h.eye = h_alloc(16, 16);
    s_h.at = h_alloc(16, 16);
    s_h.light = h_alloc(0x80, 16);
    s_h.vp = h_alloc(0x20, 16);
    s_h.w_ctx = h_wstr("MCHOME");
    s_h.w_file = h_wstr("crib.iff");
    s_h.w_room = h_wstr("room");
    s_h.w_sky_day = h_wstr("skybox_day");
    s_h.title = h_wstr("Home");
    /* Header: the template's list, our events, our rows; flags 0x10: Back pops, no backdrop. */
    s_h.rows = h_alloc(MH_ROW * (ROWS + 1), 4);
    s_h.header = h_alloc(MH_ROW, 4);
    for (r = 0; r < 0x2Cu; r += 4) MEM32(s_h.header + r) = MEM32(FEATURES + r);
    {
        static const int msgs[] = { 1, 2, 4, 5, 6, 7, 10 };
        uint32_t t = h_alloc(8u * 8u, 4);
        for (i = 0; i < 7; i++) {
            uint32_t list = h_alloc(0x48u, 4);
            MEM32(list) = 1;
            MEM32(list + 4u) = MH_FN_BASE + (uint32_t)msgs[i] * 4u;
            MEM32(t + (uint32_t)i * 8u) = (uint32_t)msgs[i];
            MEM32(t + (uint32_t)i * 8u + 4u) = list;
        }
        s_h.events = t;
    }
    MEM32(s_h.header + 0x00) = s_h.title;
    MEM32(s_h.header + 0x04) = s_h.events;
    MEM32(s_h.header + 0x0C) = 0;
    MEM32(s_h.header + 0x10) = s_h.rows;
    MEM32(s_h.header + 0x28) = 0x10u;
    for (i = 0; i < ROWS; i++) {
        uint32_t row = s_h.rows + MH_ROW * (uint32_t)i;
        MEM32(row + 0x00) = 9;
        MEM32(row + 0x04) = h_wstr(k_label[i]);
        MEM32(row + 0x28) = MH_FN_BASE + 0x800u + (uint32_t)i * 4u;
    }
    MEM32(s_h.rows + MH_ROW * ROWS) = 3;
    s_h.on = 1;
    recomp_interp_add_hook(SD_HANDLER, 0, ih_handler);
    if (getenv("NFL2K5_MC_HOME_CRIBCAM")) CloseHandle(CreateThread(NULL, 0, cribcam_thread, NULL, 0, NULL));
    fprintf(stderr, "[MCHOME] on: SOFTDRINK Career Home found, home screen %08X (%u bytes)\n", s_h.header, s_h.used);
}

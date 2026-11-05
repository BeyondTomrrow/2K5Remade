/* "Exit" in the Main Menu (M16, 2026-10-21): quits the game to the desktop.
 *
 * The Xbox game has no way out, so the PC build adds one: a last row of the
 * Main Menu (0x515660) that opens a small screen of the game's own kind,
 * "Exit NFL 2K5?" with Yes and No (a copy of the Features link list, as the
 * My NFL Eras hub is; docs/eras/research/NATIVE-MENU.md). No and B go back to
 * the Main Menu; Yes closes the game the way closing its window does (an SDL
 * quit event: the window thread ends the process), so nothing differs
 * from the title-bar close the player already uses.
 *
 * The two rows are type-9 actions whose function (+0x28) is an address in
 * 0xFEC0F000.. (the Video Settings range, screen 15, which that file never
 * uses), handed out by nfl2k5_exit_menu_lookup through recomp_lookup_manual.
 * The Main Menu and the template are found by their row labels, so a mod pack
 * whose XBE moves them gets no Exit row instead of a broken one.
 *
 * Two more screens live here, the same way:
 *  - the mod-pack prompt (2026-11-02): switching a pack on or off in Extras >
 *    Mods restarts the game, so the game's own popup box (sub_0014E440, as
 *    "Exit your Crib?") asks first (nfl2k5_exit_menu_confirm_pack, called by
 *    the Mod Packs rows);
 *  - Features > VIP > "Jukebox" (2026-11-02): the Crib's own jukebox, opened
 *    straight away. The row runs the Main Menu's Crib action (VIP choice,
 *    loading, the room), then, once the Crib is in its browse state (3), acts
 *    as A on the jukebox object (sub_00277220, kind 13); when the jukebox is
 *    closed (browse state again), the Crib menu's Exit (sub_00270B20, with its
 *    own save prompt) takes the player back to the VIP menu. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <SDL3/SDL.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define XM_FN_BASE  0xFEC0F000u   /* + row * 0x40 */
#define XM_ROW      0x34u
#define XM_CHARS    32
#define MAIN_MENU   0x00515660u   /* "Main Menu": Quick Game ... Extras */
#define SND_SELECT  0x00E7FD70u   /* L"Ambient: Menu Select" (retail) */
#define SND_CURSOR  0x00E7FD3Cu   /* L"Ambient: Menu Move Cursor" (retail) */

/* rows 0-1: "Exit NFL 2K5?"; 2: VIP > Jukebox */
enum { XR_YES, XR_NO, XR_JUKEBOX, XR_ROWS };

#define FRONTEND_FSM 0x00A84B18u  /* the front end's screen stack (NATIVE-MENU.md 1.1) */
#define XM_TEXT_CHARS 192
#define POPUP_OWNER  0x00E3C040u  /* the popup box object sub_0014E440 runs on */
#define VIP_MENU     0x00574BF8u  /* Features > VIP: Viewer, Stat Book, Manage */
#define CRIB_SCREEN  0x00524FA0u  /* The Crib (custom handler, 25-state machine) */
#define CRIB_STATE   0x00AC7448u  /* its state; 3 = moving the cursor round the room */
#define CRIB_BROWSE  3u
#define CRIB_JUKEBOX 13u          /* object kind of the jukebox (sub_00277220 -> sub_0032AD70) */
#define CRIB_HOTSPOT     0x00AC8514u  /* the room cursor's hotspot */
#define CRIB_ITEM_INDEX  0x00AC851Cu  /* item within that hotspot */
#define CRIB_ITEM        0x00AC8518u  /* the item's kind: what state 5 shows */
#define CRIB_HOTSPOT_OBJ 0x0051AD40u  /* + hotspot * 20: {count, kinds[]} */
#define CRIB_HOTSPOTS    64u

static uint32_t s_heap, s_used, s_size, s_confirm, s_pack_text, s_pack_choices;
static uint32_t s_crib_action;     /* the Main Menu's "The Crib" row action */
static int s_jukebox;              /* 0 idle, 1 Crib asked for, 2 jukebox open */
static unsigned s_jukebox_frames;
static RECOMP_TLS uint32_t s_called;
static int s_sounds;               /* the retail sound names are where they should be */

static uint32_t xm_alloc(uint32_t n)
{
    uint32_t p, i;
    n = (n + 3u) & ~3u;
    if (s_used + n > s_size) return 0;
    p = s_heap + s_used;
    s_used += n;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static uint32_t xm_wstr(const char *s)
{
    uint32_t p = xm_alloc(XM_CHARS * 2u), i;
    if (!p) return 0;
    for (i = 0; i + 1 < XM_CHARS && s[i]; i++) MEM16(p + i * 2u) = (uint16_t)(unsigned char)s[i];
    MEM16(p + i * 2u) = 0;
    return p;
}

static int xm_wstr_is(uint32_t text, const char *s)
{
    if (!text) return 0;
    while (*s && MEM16(text) == (uint16_t)(unsigned char)*s) { s++; text += 2u; }
    return !*s && MEM16(text) == 0;
}

/* The link target of the row labelled `label` in a link menu, or 0. */
static uint32_t xm_link(uint32_t menu, const char *label, int *count)
{
    uint32_t rows = MEM32(menu + 0x10), found = 0;
    int n;
    if (count) *count = 0;
    if (!rows) return 0;
    for (n = 0; n < 32; n++) {
        uint32_t r = rows + XM_ROW * (uint32_t)n;
        if (MEM32(r) == 3u) break;
        if (MEM32(r) == 0u && xm_wstr_is(MEM32(r + 0x04), label)) found = MEM32(r + 0x08);
    }
    if (count) *count = n;
    return n < 32 ? found : 0;
}

/* The action (+0x28) of the type-9 row whose label starts with `prefix`, or 0. */
static uint32_t xm_action(uint32_t menu, const char *prefix)
{
    uint32_t rows = MEM32(menu + 0x10);
    int n;
    if (!rows) return 0;
    for (n = 0; n < 32; n++) {
        uint32_t r = rows + XM_ROW * (uint32_t)n, t;
        const char *s = prefix;
        if (MEM32(r) == 3u) break;
        if (MEM32(r) != 9u || !(t = MEM32(r + 0x04))) continue;
        while (*s && MEM16(t) == (uint16_t)(unsigned char)*s) { s++; t += 2u; }
        if (!*s) return MEM32(r + 0x28);
    }
    return 0;
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, XM_FN_BASE);
    fn();
    return g_eax;
}

static void xm_fn(void)
{
    uint32_t row = (s_called - XM_FN_BASE) / 0x40u, fsm = g_ecx;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    if (row == XR_JUKEBOX) {
        recomp_func_t crib = recomp_lookup(s_crib_action);
        uint32_t item = g_edx;
        fprintf(stderr, "[JUKEBOX] VIP > Jukebox: opening the Crib's jukebox\n");
        if (crib) {
            s_jukebox = 1;
            s_jukebox_frames = 0;
            g_ecx = fsm; g_edx = item;
            PUSH32(g_esp, XM_FN_BASE);
            crib();                                  /* the Crib, as from the Main Menu */
        }
    } else if (row == XR_YES) {
        SDL_Event quit;
        fprintf(stderr, "[EXITMENU] Exit NFL 2K5? Yes: closing the game\n");
        fflush(stderr);
        fflush(stdout);
        if (s_sounds) gcall(sub_0014D390, SND_SELECT, 0);
        /* The same quit the window's close button sends: the window thread
         * shuts the game down. (The SDL3 build's window is an SDL_Window, so
         * the old WM_CLOSE to it went nowhere and Yes did nothing.) */
        SDL_zero(quit);
        quit.type = SDL_EVENT_QUIT;
        if (!SDL_PushEvent(&quit)) ExitProcess(0);
    } else {
        fprintf(stderr, "[EXITMENU] Exit NFL 2K5? No: back to the Main Menu\n");
        if (s_sounds) gcall(sub_0014D390, SND_CURSOR, 0);
        gcall(sub_0006E400, fsm, 0);                 /* pop: the Main Menu again */
    }
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    g_esp += 4;
}

recomp_func_t nfl2k5_exit_menu_lookup(uint32_t address)
{
    uint32_t off = address - XM_FN_BASE, row = off / 0x40u;
    if (row >= XR_ROWS || !(row < XR_JUKEBOX ? s_confirm : s_crib_action))
        return NULL;
    s_called = address;
    return xm_fn;
}

/* Every front-end frame (from the main FSM's frame, as the Sandbox service):
 * steers the Crib opened by VIP > Jukebox into its jukebox and back out. */
void nfl2k5_vip_jukebox_service(void)
{
    uint32_t depth, top, state;
    uint32_t save_eax = g_eax, save_ecx = g_ecx, save_edx = g_edx, save_ebx = g_ebx,
             save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    if (!s_jukebox) return;
    depth = MEM32(FRONTEND_FSM + 0x100u);
    top = depth < 0x20u ? MEM32(FRONTEND_FSM + depth * 8u) : 0;
    state = MEM32(CRIB_STATE);
    s_jukebox_frames++;
    if (s_jukebox == 1) {
        if (top == VIP_MENU && s_jukebox_frames > 120) {   /* the Crib did not open (no VIP chosen...) */
            fprintf(stderr, "[JUKEBOX] the Crib was not opened; nothing to do\n");
            s_jukebox = 0;
        } else if (top == CRIB_SCREEN && state == CRIB_BROWSE) {
            /* A on the jukebox, as the room's cursor does it: the hotspot and
             * item of the jukebox selected, state 4, then the item's action */
            uint32_t h, k = 0, found = 0;
            for (h = 1; h < CRIB_HOTSPOTS && !found; h++) {
                uint32_t obj = MEM32(CRIB_HOTSPOT_OBJ + h * 20u), count, list;
                if (!obj || !(list = MEM32(obj + 4u))) continue;
                count = MEM32(obj);
                for (k = 0; k < count && k < 8u; k++)
                    if (MEM32(list + k * 4u) == CRIB_JUKEBOX) { found = h; break; }
            }
            fprintf(stderr, "[JUKEBOX] Crib ready (state 3): jukebox at hotspot %u item %u\n", (unsigned)found, (unsigned)k);
            s_jukebox = 2;
            if (found) {
                MEM32(CRIB_HOTSPOT) = found;
                MEM32(CRIB_ITEM_INDEX) = k;
            }
            MEM32(CRIB_ITEM) = CRIB_JUKEBOX;
            PUSH32(g_esp, 4u);
            g_eax = FRONTEND_FSM;
            PUSH32(g_esp, XM_FN_BASE);
            sub_00276810();                           /* state 4 (ret 4) */
            g_eax = CRIB_JUKEBOX; g_esi = FRONTEND_FSM; g_ecx = FRONTEND_FSM;
            PUSH32(g_esp, XM_FN_BASE);
            sub_00277220();                           /* the jukebox's action */
        } else if (s_jukebox_frames > 60u * 120u) {
            fprintf(stderr, "[JUKEBOX] gave up waiting for the Crib (top %08X, state %u)\n",
                    (unsigned)top, (unsigned)state);
            s_jukebox = 0;
        }
    } else if (s_jukebox == 2) {
        if (top != CRIB_SCREEN) {
            if (top == VIP_MENU) s_jukebox = 0;      /* left some other way */
        } else if (state == CRIB_BROWSE) {
            fprintf(stderr, "[JUKEBOX] jukebox closed: leaving the Crib\n");
            s_jukebox = 0;
            g_ecx = FRONTEND_FSM; g_edx = 0;
            PUSH32(g_esp, XM_FN_BASE);
            sub_00270B20();                           /* the Crib menu's Exit (asks to save) */
        }
    }
    g_eax = save_eax; g_ecx = save_ecx; g_edx = save_edx; g_ebx = save_ebx;
    g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
}

/* A mod pack is about to be switched on or off from the Mod Packs screen
 * (whose header is `screen`), which closes the game and starts it again: ask
 * first, in the game's popup box. 1 = Yes, 0 = No, -1 = the box could not be
 * shown (the caller goes ahead as before). */
int nfl2k5_exit_menu_confirm_pack(int on, const char *name, uint32_t screen)
{
    char text[XM_TEXT_CHARS];
    uint32_t depth, i, answer, save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp,
             save_ecx = g_ecx, save_edx = g_edx;
    if (!s_pack_text) return -1;
    depth = MEM32(FRONTEND_FSM + 0x100u);
    /* only when the Mod Packs screen is the front end's top screen */
    if (depth == 0 || depth >= 0x20u || MEM32(FRONTEND_FSM + depth * 8u) != screen) {
        fprintf(stderr, "[EXITMENU] mod pack prompt skipped: Mod Packs is not the top screen (depth %u, top %08X)\n",
                (unsigned)depth, (unsigned)(depth < 0x20u ? MEM32(FRONTEND_FSM + depth * 8u) : 0));
        return -1;
    }
    snprintf(text, sizeof text, "%s will be %s. The game will close and restart now. Continue?",
             name && *name ? name : "The mod pack", on ? "applied" : "turned off");
    for (i = 0; i + 1 < XM_TEXT_CHARS && text[i]; i++) MEM16(s_pack_text + i * 2u) = (uint16_t)(unsigned char)text[i];
    MEM16(s_pack_text + i * 2u) = 0;
    fprintf(stderr, "[EXITMENU] mod pack prompt: \"%s\"\n", text);
    /* the arguments of the Crib's "Exit your Crib?" (sub_00270B20) */
    PUSH32(g_esp, 0);
    PUSH32(g_esp, 0xFFFFFFFFu);
    PUSH32(g_esp, 1);
    PUSH32(g_esp, FRONTEND_FSM);
    PUSH32(g_esp, 0);
    PUSH32(g_esp, s_pack_choices);
    answer = gcall(sub_0014E440, POPUP_OWNER, s_pack_text);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp; g_ecx = save_ecx; g_edx = save_edx;
    fprintf(stderr, "[EXITMENU] mod pack prompt: %s\n", answer == 1u ? "Yes" : "No");
    return answer == 1u;
}

void nfl2k5_exit_menu_install(void)
{
    uint32_t tmpl, header, rows, copy, old, link;
    int n, vn, i;
    const char *off = getenv("NFL2K5_NO_EXIT_ROW");
    if (off && *off && *off != '0') { fprintf(stderr, "[EXITMENU] off (NFL2K5_NO_EXIT_ROW)\n"); return; }
    /* the Main Menu, recognised by its rows; the template is its Features list */
    tmpl = xm_link(MAIN_MENU, "Features", &n);
    if (!tmpl || !xm_link(MAIN_MENU, "Options", NULL) || n < 3 || n >= 31) {
        fprintf(stderr, "[EXITMENU] off: no Main Menu at %08X (a mod pack's layout?)\n", (unsigned)MAIN_MENU);
        return;
    }
    s_sounds = xm_wstr_is(SND_SELECT, "Ambient: Menu Select") && xm_wstr_is(SND_CURSOR, "Ambient: Menu Move Cursor");
    s_size = 4096u;
    if (!(s_heap = xbox_HeapAlloc(s_size, 16))) { fprintf(stderr, "[EXITMENU] no guest memory\n"); return; }
    header = xm_alloc(XM_ROW);
    rows = xm_alloc(XM_ROW * 3u);                  /* Yes, No, end */
    if (!header || !rows) return;
    for (i = 0; i < (int)XM_ROW; i += 4) MEM32(header + (uint32_t)i) = MEM32(tmpl + (uint32_t)i);
    MEM32(header + 0x00) = xm_wstr("Exit NFL 2K5?");
    MEM32(header + 0x04) = 0;                      /* no event table: the template's runs Features code */
    MEM32(header + 0x0C) = 0;                      /* no help bar */
    MEM32(header + 0x10) = rows;
    for (i = XR_YES; i <= XR_NO; i++) {
        uint32_t r = rows + XM_ROW * (uint32_t)i;
        MEM32(r + 0x00) = 9;                       /* action row */
        MEM32(r + 0x04) = xm_wstr(i == XR_YES ? "Yes" : "No");
        MEM32(r + 0x28) = XM_FN_BASE + (uint32_t)i * 0x40u;
    }
    MEM32(rows + XM_ROW * 2u) = 3;                 /* end row */
    /* the Main Menu's rows copied with "Exit" before the end row (as the Eras row was added, M3) */
    old = MEM32(MAIN_MENU + 0x10);
    copy = xm_alloc(XM_ROW * (uint32_t)(n + 2));
    if (!copy || !MEM32(header) || !MEM32(rows + 0x04) || !MEM32(rows + XM_ROW + 0x04)) { fprintf(stderr, "[EXITMENU] no guest memory\n"); return; }
    for (i = 0; i < (int)(XM_ROW * (uint32_t)n); i += 4) MEM32(copy + (uint32_t)i) = MEM32(old + (uint32_t)i);
    link = copy + XM_ROW * (uint32_t)n;
    MEM32(link + 0x00) = 0;
    MEM32(link + 0x04) = xm_wstr("Exit");
    MEM32(link + 0x08) = header;
    for (i = 0; i < (int)XM_ROW; i += 4) MEM32(link + XM_ROW + (uint32_t)i) = MEM32(old + XM_ROW * (uint32_t)n + (uint32_t)i);
    if (!MEM32(link + 0x04)) { fprintf(stderr, "[EXITMENU] no guest memory\n"); return; }
    s_confirm = header;
    MEM32(MAIN_MENU + 0x10) = copy;
    /* Features > VIP > Jukebox: a type-9 row after Manage, if the Crib (or a
     * pack's renamed Crib) and the retail VIP menu are where they should be */
    s_crib_action = xm_action(MAIN_MENU, "The Crib");
    if (!s_crib_action) s_crib_action = xm_action(MAIN_MENU, "Trophy Room");
    if (s_crib_action && xm_wstr_is(MEM32(VIP_MENU), "VIP") && xm_link(VIP_MENU, "Manage", &vn) && vn >= 1 && vn < 31) {
        uint32_t vold = MEM32(VIP_MENU + 0x10), vcopy = xm_alloc(XM_ROW * (uint32_t)(vn + 2)), jr;
        if (vcopy) {
            for (i = 0; i < (int)(XM_ROW * (uint32_t)vn); i += 4) MEM32(vcopy + (uint32_t)i) = MEM32(vold + (uint32_t)i);
            jr = vcopy + XM_ROW * (uint32_t)vn;
            for (i = 0; i < (int)XM_ROW; i += 4) MEM32(jr + (uint32_t)i) = 0;
            MEM32(jr + 0x00) = 9;
            MEM32(jr + 0x04) = xm_wstr("Jukebox");
            MEM32(jr + 0x28) = XM_FN_BASE + XR_JUKEBOX * 0x40u;
            for (i = 0; i < (int)XM_ROW; i += 4) MEM32(jr + XM_ROW + (uint32_t)i) = MEM32(vold + XM_ROW * (uint32_t)vn + (uint32_t)i);
            if (MEM32(jr + 0x04)) {
                MEM32(VIP_MENU + 0x10) = vcopy;
                fprintf(stderr, "[JUKEBOX] \"Jukebox\" added to Features > VIP (Crib action %08X)\n", (unsigned)s_crib_action);
            }
        }
    } else {
        s_crib_action = 0;
        fprintf(stderr, "[JUKEBOX] off: no Crib row or VIP menu where expected\n");
    }
    /* the mod-pack prompt: {label, value} pairs for the popup box, its text filled in when shown */
    {
        uint32_t choices = xm_alloc(24u), no = xm_wstr("No"), yes = xm_wstr("Yes"), text = xm_alloc(XM_TEXT_CHARS * 2u);
        if (choices && no && yes && text) {
            MEM32(choices + 0x00) = no;
            MEM32(choices + 0x04) = 0;
            MEM32(choices + 0x08) = yes;
            MEM32(choices + 0x0C) = 1;
            s_pack_choices = choices;
            s_pack_text = text;
        }
    }
    fprintf(stderr, "[EXITMENU] \"Exit\" is the Main Menu's last row (%d rows, flags %02X); prompt %08X (template %08X)\n",
            n + 1, (unsigned)MEM32(MAIN_MENU + 0x28), (unsigned)header, (unsigned)tmpl);
}

/* "Exit" in the Main Menu (M16, 2026-10-21): quits the game to the desktop.
 *
 * The Xbox game has no way out, so the PC build adds one: a last row of the
 * Main Menu (0x515660) that opens a small screen of the game's own kind,
 * "Exit NFL 2K5?" with Yes and No (a copy of the Features link list, as the
 * My NFL Eras hub is; docs/eras/research/NATIVE-MENU.md). No and B go back to
 * the Main Menu; Yes closes the game the way closing its window does (WM_CLOSE
 * to the game window: the window thread ends the process), so nothing differs
 * from the title-bar close the player already uses.
 *
 * The two rows are type-9 actions whose function (+0x28) is an address in
 * 0xFEC0F000.. (the Video Settings range, screen 15, which that file never
 * uses), handed out by nfl2k5_exit_menu_lookup through recomp_lookup_manual.
 * The Main Menu and the template are found by their row labels, so a mod pack
 * whose XBE moves them gets no Exit row instead of a broken one. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern HWND xbox_FramebufferWindowHwnd(void);

#define XM_FN_BASE  0xFEC0F000u   /* + row * 0x40 */
#define XM_ROW      0x34u
#define XM_CHARS    32
#define MAIN_MENU   0x00515660u   /* "Main Menu": Quick Game ... Extras */
#define SND_SELECT  0x00E7FD70u   /* L"Ambient: Menu Select" (retail) */
#define SND_CURSOR  0x00E7FD3Cu   /* L"Ambient: Menu Move Cursor" (retail) */

enum { XR_YES, XR_NO, XR_ROWS };

static uint32_t s_heap, s_used, s_size, s_confirm;
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
    if (row == XR_YES) {
        HWND w = xbox_FramebufferWindowHwnd();
        fprintf(stderr, "[EXITMENU] Exit NFL 2K5? Yes: closing the game\n");
        fflush(stderr);
        fflush(stdout);
        if (s_sounds) gcall(sub_0014D390, SND_SELECT, 0);
        if (w) PostMessageA(w, WM_CLOSE, 0, 0);    /* the window thread ends the process */
        else ExitProcess(0);
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
    uint32_t off = address - XM_FN_BASE;
    if (!s_confirm || off / 0x40u >= XR_ROWS) return NULL;
    s_called = address;
    return xm_fn;
}

void nfl2k5_exit_menu_install(void)
{
    uint32_t tmpl, header, rows, copy, old, link;
    int n, i;
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
    rows = xm_alloc(XM_ROW * (XR_ROWS + 1u));
    if (!header || !rows) return;
    for (i = 0; i < (int)XM_ROW; i += 4) MEM32(header + (uint32_t)i) = MEM32(tmpl + (uint32_t)i);
    MEM32(header + 0x00) = xm_wstr("Exit NFL 2K5?");
    MEM32(header + 0x04) = 0;                      /* no event table: the template's runs Features code */
    MEM32(header + 0x0C) = 0;                      /* no help bar */
    MEM32(header + 0x10) = rows;
    for (i = 0; i < XR_ROWS; i++) {
        uint32_t r = rows + XM_ROW * (uint32_t)i;
        MEM32(r + 0x00) = 9;                       /* action row */
        MEM32(r + 0x04) = xm_wstr(i == XR_YES ? "Yes" : "No");
        MEM32(r + 0x28) = XM_FN_BASE + (uint32_t)i * 0x40u;
    }
    MEM32(rows + XM_ROW * XR_ROWS) = 3;            /* end row */
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
    fprintf(stderr, "[EXITMENU] \"Exit\" is the Main Menu's last row (%d rows, flags %02X); prompt %08X (template %08X)\n",
            n + 1, (unsigned)MEM32(MAIN_MENU + 0x28), (unsigned)header, (unsigned)tmpl);
}

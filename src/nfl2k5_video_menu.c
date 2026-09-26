/* "Video Settings" inside the game's own Options menu.
 *
 * NFL 2K5's menus are data. A menu header (0x34 bytes) holds the title (a
 * UTF-16 string), a screen handler and, at +0x10, an array of 0x34-byte rows
 * ended by a row of type 3. The Options screens list links (type 0: label at
 * +4, submenu header at +8); a settings screen such as Game Options (header
 * 0x501E48) lists value rows (type 7: "< value >", type 5: Off/On) whose
 * behaviour is seven functions:
 *
 *   +0x0C max   +0x10 min   +0x14 get   +0x18 next   +0x1C previous
 *   +0x20 value string (UTF-16)   +0x24 widest value string (sub_000771A0)
 *
 * so a new settings screen needs no game code: rows whose function pointers
 * are addresses in 0xFEC00000.. that recomp_lookup_manual hands to
 * nfl2k5_video_menu_lookup below, and values from the presenter's video
 * settings (nv2a_gpu_present.inc.c). A "Video Settings" link is appended to
 * each of the four Options menus (front end, two mode variants, the in-game
 * pause menu), in copies of their row arrays. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void xbox_VideoSettingsLoad(void);
extern int xbox_VideoSettingCount(void);
extern const char *xbox_VideoSettingName(int i);
extern int xbox_VideoSettingIsToggle(int i);
extern int xbox_VideoSettingValues(int i);
extern void xbox_VideoSettingValueName(int i, int idx, char *buf, size_t n);
extern int xbox_VideoSettingGet(int i);
extern void xbox_VideoSettingSet(int i, int idx);

#define VM_FN_BASE    0xFEC00000u
#define VM_ROW        0x34u
#define VM_MAX_ROWS   16
#define GAME_OPTIONS_HEADER 0x00501E48u    /* template for the new screen */

static const uint32_t k_options_headers[] = { 0x00503288u, 0x00503458u, 0x00503628u, 0x005038C8u };

static uint32_t s_heap, s_heap_used, s_heap_size;
static uint32_t s_value_table[VM_MAX_ROWS];   /* guest array of UTF-16 string pointers per row */
static int s_rows;
static RECOMP_TLS uint32_t s_called;          /* fake address being called (set by the lookup) */

static uint32_t vm_alloc(uint32_t n)
{
    uint32_t p;
    n = (n + 3u) & ~3u;
    if (s_heap_used + n > s_heap_size) return 0;
    p = s_heap + s_heap_used;
    s_heap_used += n;
    return p;
}

static uint32_t vm_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), p = vm_alloc((n + 1u) * 2u), i;
    if (!p) return 0;
    for (i = 0; i <= n; i++)
        MEM16(p + i * 2u) = (uint16_t)(unsigned char)s[i];
    return p;
}

static void vm_copy(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4)
        MEM32(dst + i) = MEM32(src + i);
}

/* One function for every row slot: the lookup remembers which address the
 * game is calling. Plain `ret`, results in eax. */
static void vm_row_fn(void)
{
    uint32_t off = s_called - VM_FN_BASE;
    int row = (int)(off / 0x40u), slot = (int)((off % 0x40u) / 4u);
    int n = xbox_VideoSettingValues(row), v = xbox_VideoSettingGet(row);
    switch (slot) {
    case 0: g_eax = (uint32_t)(n - 1); break;
    case 1: g_eax = 0; break;
    case 2: g_eax = (uint32_t)v; break;
    case 3: xbox_VideoSettingSet(row, v + 1 >= n ? 0 : v + 1); g_eax = 1; break;
    case 4: xbox_VideoSettingSet(row, v <= 0 ? n - 1 : v - 1); g_eax = 1; break;
    case 5: g_eax = MEM32(s_value_table[row] + (uint32_t)v * 4u); break;
    case 6:
        /* Same as the game's own rows: widest string of table[0..n-1] in
         * the font passed in ecx. sub_000771A0 pops its two arguments. */
        PUSH32(g_esp, s_value_table[row]);
        PUSH32(g_esp, (uint32_t)(n - 1));
        g_edx = 0;
        PUSH32(g_esp, VM_FN_BASE);
        sub_000771A0();
        break;
    default: g_eax = 0; break;
    }
    g_esp += 4;
}

recomp_func_t nfl2k5_video_menu_lookup(uint32_t address)
{
    uint32_t off = address - VM_FN_BASE;
    if (off / 0x40u >= (uint32_t)s_rows || (off % 0x40u) / 4u > 6u)
        return NULL;
    s_called = address;
    return vm_row_fn;
}

static uint32_t vm_build_screen(void)
{
    uint32_t title, rows, header;
    int i;
    s_rows = xbox_VideoSettingCount();
    if (s_rows > VM_MAX_ROWS) s_rows = VM_MAX_ROWS;
    title = vm_wstr("Video Settings");
    rows = vm_alloc(VM_ROW * (uint32_t)(s_rows + 1));
    header = vm_alloc(VM_ROW);
    if (!title || !rows || !header) return 0;
    for (i = 0; i < s_rows; i++) {
        uint32_t r = rows + VM_ROW * (uint32_t)i, k;
        int n = xbox_VideoSettingValues(i), v;
        for (k = 0; k < VM_ROW; k += 4) MEM32(r + k) = 0;
        MEM32(r + 0x00) = xbox_VideoSettingIsToggle(i) ? 5u : 7u;
        MEM32(r + 0x04) = vm_wstr(xbox_VideoSettingName(i));
        for (k = 0; k < 7; k++)
            MEM32(r + 0x0C + k * 4u) = VM_FN_BASE + (uint32_t)i * 0x40u + k * 4u;
        s_value_table[i] = vm_alloc(4u * (uint32_t)n);
        if (!s_value_table[i]) return 0;
        for (v = 0; v < n; v++) {
            char name[64];
            xbox_VideoSettingValueName(i, v, name, sizeof name);
            MEM32(s_value_table[i] + (uint32_t)v * 4u) = vm_wstr(name);
        }
    }
    /* End row and header copied from Game Options. */
    vm_copy(rows + VM_ROW * (uint32_t)s_rows, GAME_OPTIONS_HEADER - VM_ROW, VM_ROW);
    MEM32(rows + VM_ROW * (uint32_t)s_rows) = 3;
    vm_copy(header, GAME_OPTIONS_HEADER, VM_ROW);
    MEM32(header + 0x00) = title;
    MEM32(header + 0x10) = rows;
    return header;
}

/* Copy an Options menu's rows, add the link before the end row, repoint. */
static int vm_add_link(uint32_t options_header, uint32_t screen, uint32_t label)
{
    uint32_t old = MEM32(options_header + 0x10), copy, link;
    int n = 0;
    while (n < 32 && MEM32(old + VM_ROW * (uint32_t)n) != 3) n++;
    if (n >= 32) return 0;
    copy = vm_alloc(VM_ROW * (uint32_t)(n + 2));
    if (!copy) return 0;
    vm_copy(copy, old, VM_ROW * (uint32_t)n);
    link = copy + VM_ROW * (uint32_t)n;
    {
        uint32_t k;
        for (k = 0; k < VM_ROW; k += 4) MEM32(link + k) = 0;
    }
    MEM32(link + 0x04) = label;
    MEM32(link + 0x08) = screen;
    vm_copy(link + VM_ROW, old + VM_ROW * (uint32_t)n, VM_ROW);   /* end row */
    MEM32(options_header + 0x10) = copy;
    return n + 1;
}

/* Debug: NFL2K5_MEMDUMP=addr:len[,addr:len...] writes guest memory to
 * logs\memdump_<addr>.bin after NFL2K5_MEMDUMP_DELAY seconds (default 60),
 * then every 10 s -- used to read the menu layout the game builds at run
 * time. */
static DWORD WINAPI vm_memdump_thread(void *arg)
{
    const char *spec = getenv("NFL2K5_MEMDUMP"), *d = getenv("NFL2K5_MEMDUMP_DELAY");
    (void)arg;
    Sleep((d ? (DWORD)atoi(d) : 60u) * 1000u);
    for (;;) {
        const char *p = spec;
        while (p && *p) {
            char *end;
            uint32_t a = (uint32_t)strtoul(p, &end, 16), n = 0, i;
            if (*end == ':') n = (uint32_t)strtoul(end + 1, &end, 16);
            if (n) {
                char path[96];
                FILE *f;
                snprintf(path, sizeof path, "logs\\memdump_%08X.bin", (unsigned)a);
                if ((f = fopen(path, "wb")) != NULL) {
                    for (i = 0; i < n; i++) { uint8_t b = MEM8(a + i); fwrite(&b, 1, 1, f); }
                    fclose(f);
                }
            }
            p = (*end == ',') ? end + 1 : NULL;
        }
        Sleep(10000);
    }
}

void nfl2k5_video_menu_install(void)
{
    if (getenv("NFL2K5_MEMDUMP"))
        CloseHandle(CreateThread(NULL, 0, vm_memdump_thread, NULL, 0, NULL));
    uint32_t screen, label;
    unsigned k;
    if (getenv("NFL2K5_NO_VIDEO_MENU")) return;
    xbox_VideoSettingsLoad();
    s_heap_size = 64u * 1024u;
    s_heap = xbox_HeapAlloc(s_heap_size, 16);
    if (!s_heap) { fprintf(stderr, "[VIDEOMENU] no guest memory\n"); return; }
    screen = vm_build_screen();
    label = vm_wstr("Video Settings");
    if (!screen || !label) { fprintf(stderr, "[VIDEOMENU] build failed\n"); return; }
    for (k = 0; k < sizeof k_options_headers / sizeof k_options_headers[0]; k++) {
        int n = vm_add_link(k_options_headers[k], screen, label);
        /* Link menus show seven rows; header flag 0x4 (set on the settings
         * screens, 0x15 vs 0x11) makes the list scroll, so the eighth row
         * is reachable. */
        if (n > 0)
            MEM32(k_options_headers[k] + 0x28) |= 0x4u;
        fprintf(stderr, "[VIDEOMENU] Options menu %08X: %d rows\n", (unsigned)k_options_headers[k], n);
    }
    fprintf(stderr, "[VIDEOMENU] Video Settings screen %08X, %d settings, %u bytes\n",
            (unsigned)screen, s_rows, (unsigned)s_heap_used);
}

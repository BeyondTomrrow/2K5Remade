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

/* src/nfl2k5_presentation.cpp */
extern void nfl2k5_presentation_init(void);
extern int nfl2k5_pres_values(int what);
extern int nfl2k5_pres_get(int what);
extern void nfl2k5_pres_set(int what, int v);
extern void nfl2k5_pres_value_name(int what, int idx, char *buf, size_t n);

#define VM_FN_BASE    0xFEC00000u   /* + screen * 0x1000 + row * 0x40 + slot * 4 */
#define VM_ROW        0x34u
#define VM_MAX_ROWS   16
#define VM_MAX_VALUES 64
#define VM_STR_CHARS  64
#define GAME_OPTIONS_HEADER 0x00501E48u    /* template for the new screens */

static const uint32_t k_options_headers[] = { 0x00503288u, 0x00503458u, 0x00503628u, 0x005038C8u };
#define FEATURES_HEADER       0x00525830u   /* front-end Features link menu */
#define COACH_MATCH_UP_HEADER 0x00585474u   /* pre-game menu: Start Game ... VIP */

/* A settings screen's rows. */
typedef struct {
    const char *title;
    int rows;
    const char *const *labels;
    int (*toggle)(int row);
    int (*values)(int row);
    int (*get)(int row);
    void (*set)(int row, int v);
    void (*name)(int row, int v, char *buf, size_t n);
    /* guest scratch per row: current value string, and the table of all
     * values the width query measures */
    uint32_t cur[VM_MAX_ROWS], table[VM_MAX_ROWS], pool[VM_MAX_ROWS];
} VmScreen;

static uint32_t s_heap, s_heap_used, s_heap_size;
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

static void vm_put_wstr(uint32_t p, const char *s, uint32_t max_chars)
{
    uint32_t i;
    for (i = 0; i + 1 < max_chars && s[i]; i++)
        MEM16(p + i * 2u) = (uint16_t)(unsigned char)s[i];
    MEM16(p + i * 2u) = 0;
}

static uint32_t vm_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1u, p = vm_alloc(n * 2u);
    if (p) vm_put_wstr(p, s, n);
    return p;
}

static void vm_copy(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4)
        MEM32(dst + i) = MEM32(src + i);
}

/* ---- Video Settings rows (nv2a_gpu_present.inc.c) ---- */
static int vid_toggle(int r) { return xbox_VideoSettingIsToggle(r); }
static int vid_values(int r) { return xbox_VideoSettingValues(r); }
static int vid_get(int r) { return xbox_VideoSettingGet(r); }
static void vid_set(int r, int v) { xbox_VideoSettingSet(r, v); }
static void vid_name(int r, int v, char *b, size_t n) { xbox_VideoSettingValueName(r, v, b, n); }

/* ---- Presentation rows (nfl2k5_presentation.cpp) ---- */
static const char *const k_pres_labels[] = { "Broadcast Package", "Intro Theme", "Outro Theme",
                                             "Scorebug Animations", "Theme Volume" };
static int pres_toggle(int r) { return r == 3; }
static int pres_values(int r) { return nfl2k5_pres_values(r); }
static int pres_get(int r) { return nfl2k5_pres_get(r); }
static void pres_set(int r, int v) { nfl2k5_pres_set(r, v); }
static void pres_name(int r, int v, char *b, size_t n) { nfl2k5_pres_value_name(r, v, b, n); }

/* ---- Mod Packs rows (nfl2k5_mod_packs.c) ---- */
extern void nfl2k5_modpacks_init(const char *root);
extern int nfl2k5_modpacks_count(void);
extern const char *nfl2k5_modpacks_name(int row);
extern int nfl2k5_modpacks_get(int row);
extern void nfl2k5_modpacks_set(int row, int on);
static int mods_toggle(int r) { (void)r; return 1; }
static int mods_values(int r) { (void)r; return 2; }
static int mods_get(int r) { return nfl2k5_modpacks_get(r); }
static void mods_set(int r, int v) { nfl2k5_modpacks_set(r, v != 0); }
static void mods_name(int r, int v, char *b, size_t n)
{
    (void)r;
    snprintf(b, n, "%s", v ? "On" : "Off");
}

static const char *s_video_labels[VM_MAX_ROWS];
static const char *s_mod_labels[VM_MAX_ROWS];
static VmScreen s_screens[3] = {
    { "Video Settings", 0, s_video_labels, vid_toggle, vid_values, vid_get, vid_set, vid_name },
    { "Presentation", 5, k_pres_labels, pres_toggle, pres_values, pres_get, pres_set, pres_name },
    { "Mod Packs", 0, s_mod_labels, mods_toggle, mods_values, mods_get, mods_set, mods_name },
};
#define VM_SCREENS ((int)(sizeof s_screens / sizeof s_screens[0]))

/* One function for every row slot: the lookup remembers which address the
 * game is calling. Plain `ret`, results in eax. Value strings are written
 * on demand, because some lists (the themes of the selected package)
 * change while the screen is open. */
static void vm_row_fn(void)
{
    uint32_t off = s_called - VM_FN_BASE;
    VmScreen *sc = &s_screens[off / 0x1000u];
    int row = (int)((off % 0x1000u) / 0x40u), slot = (int)((off % 0x40u) / 4u);
    int n = sc->values(row), v = sc->get(row);
    char name[VM_STR_CHARS];
    if (n > VM_MAX_VALUES) n = VM_MAX_VALUES;
    if (n < 1) n = 1;
    switch (slot) {
    case 0: g_eax = (uint32_t)(n - 1); break;
    case 1: g_eax = 0; break;
    case 2: g_eax = (uint32_t)v; break;
    case 3: sc->set(row, v + 1 >= n ? 0 : v + 1); g_eax = 1; break;
    case 4: sc->set(row, v <= 0 ? n - 1 : v - 1); g_eax = 1; break;
    case 5:
        sc->name(row, v, name, sizeof name);
        vm_put_wstr(sc->cur[row], name, VM_STR_CHARS);
        g_eax = sc->cur[row];
        if (getenv("NFL2K5_MENU_LOG")) {
            static DWORD last[8][8];
            if (GetTickCount() - last[off / 0x1000u & 7][row & 7] > 2000) {
                last[off / 0x1000u & 7][row & 7] = GetTickCount();
                fprintf(stderr, "[VIDEOMENU] %s row %d value %d/%d \"%s\" -> %08X\n",
                        sc->title, row, v, n, name, (unsigned)g_eax);
            }
        }
        break;
    case 6: {
        /* Same as the game's own rows: widest string of table[0..n-1] in
         * the font passed in ecx. sub_000771A0 pops its two arguments. */
        int k;
        for (k = 0; k < n; k++) {
            uint32_t p = sc->pool[row] + (uint32_t)k * VM_STR_CHARS * 2u;
            sc->name(row, k, name, sizeof name);
            vm_put_wstr(p, name, VM_STR_CHARS);
            MEM32(sc->table[row] + (uint32_t)k * 4u) = p;
        }
        PUSH32(g_esp, sc->table[row]);
        PUSH32(g_esp, (uint32_t)(n - 1));
        g_edx = 0;
        PUSH32(g_esp, VM_FN_BASE);
        sub_000771A0();
        if (getenv("NFL2K5_MENU_LOG"))
            fprintf(stderr, "[VIDEOMENU] %s row %d width of %d values -> %u\n", sc->title, row, n, (unsigned)g_eax);
        break;
    }
    default: g_eax = 0; break;
    }
    g_esp += 4;
}

recomp_func_t nfl2k5_video_menu_lookup(uint32_t address)
{
    uint32_t off = address - VM_FN_BASE, scr = off / 0x1000u;
    if (scr >= (uint32_t)VM_SCREENS || (off % 0x1000u) / 0x40u >= (uint32_t)s_screens[scr].rows ||
        (off % 0x40u) / 4u > 6u)
        return NULL;
    s_called = address;
    return vm_row_fn;
}

static uint32_t vm_build_screen(int index)
{
    VmScreen *sc = &s_screens[index];
    uint32_t title, rows, header;
    int i;
    if (sc->rows > VM_MAX_ROWS) sc->rows = VM_MAX_ROWS;
    title = vm_wstr(sc->title);
    rows = vm_alloc(VM_ROW * (uint32_t)(sc->rows + 1));
    header = vm_alloc(VM_ROW);
    if (!title || !rows || !header) return 0;
    for (i = 0; i < sc->rows; i++) {
        uint32_t r = rows + VM_ROW * (uint32_t)i, k;
        for (k = 0; k < VM_ROW; k += 4) MEM32(r + k) = 0;
        MEM32(r + 0x00) = sc->toggle(i) ? 5u : 7u;
        MEM32(r + 0x04) = vm_wstr(sc->labels[i]);
        for (k = 0; k < 7; k++)
            MEM32(r + 0x0C + k * 4u) = VM_FN_BASE + (uint32_t)index * 0x1000u + (uint32_t)i * 0x40u + k * 4u;
        sc->cur[i] = vm_alloc(VM_STR_CHARS * 2u);
        sc->table[i] = vm_alloc(VM_MAX_VALUES * 4u);
        sc->pool[i] = vm_alloc(VM_MAX_VALUES * VM_STR_CHARS * 2u);
        if (!sc->cur[i] || !sc->table[i] || !sc->pool[i]) return 0;
    }
    /* End row and header copied from Game Options. */
    vm_copy(rows + VM_ROW * (uint32_t)sc->rows, GAME_OPTIONS_HEADER - VM_ROW, VM_ROW);
    MEM32(rows + VM_ROW * (uint32_t)sc->rows) = 3;
    vm_copy(header, GAME_OPTIONS_HEADER, VM_ROW);
    MEM32(header + 0x00) = title;
    MEM32(header + 0x10) = rows;
    return header;
}

/* Copy a link menu's rows, add the link before the end row, repoint. */
static int vm_add_link(uint32_t menu_header, uint32_t screen, uint32_t label)
{
    uint32_t old = MEM32(menu_header + 0x10), copy, link;
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
    MEM32(menu_header + 0x10) = copy;
    /* Link menus show seven rows; header flag 0x4 (set on the settings
     * screens, 0x15 vs 0x11) makes the list scroll, so more are reachable. */
    MEM32(menu_header + 0x28) |= 0x4u;
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
    uint32_t video, pres, mods = 0;
    char root[MAX_PATH];
    unsigned k;
    int i;
    if (getenv("NFL2K5_MEMDUMP"))
        CloseHandle(CreateThread(NULL, 0, vm_memdump_thread, NULL, 0, NULL));
    xbox_VideoSettingsLoad();
    nfl2k5_presentation_init();
    if (!GetCurrentDirectoryA(sizeof(root), root)) snprintf(root, sizeof(root), ".");
    nfl2k5_modpacks_init(root);
    if (getenv("NFL2K5_NO_VIDEO_MENU")) return;
    s_heap_size = 512u * 1024u;
    s_heap = xbox_HeapAlloc(s_heap_size, 16);
    if (!s_heap) { fprintf(stderr, "[VIDEOMENU] no guest memory\n"); return; }
    s_screens[0].rows = xbox_VideoSettingCount();
    for (i = 0; i < s_screens[0].rows && i < VM_MAX_ROWS; i++)
        s_video_labels[i] = xbox_VideoSettingName(i);
    s_screens[2].rows = nfl2k5_modpacks_count();
    for (i = 0; i < s_screens[2].rows && i < VM_MAX_ROWS; i++)
        s_mod_labels[i] = nfl2k5_modpacks_name(i);
    video = vm_build_screen(0);
    pres = vm_build_screen(1);
    /* Opt-in (NFL2K5_MODPACK_MENU=1) until it has a verified home: linked at
     * 0x00525830 it took the main menu's default selection, so A on the main
     * menu switched a code pack on and restarted into it (2026-10-03). */
    if (s_screens[2].rows > 0 && getenv("NFL2K5_MODPACK_MENU")) mods = vm_build_screen(2);
    if (!video || !pres) { fprintf(stderr, "[VIDEOMENU] build failed\n"); return; }
    for (k = 0; k < sizeof k_options_headers / sizeof k_options_headers[0]; k++) {
        int n = vm_add_link(k_options_headers[k], video, vm_wstr("Video Settings"));
        fprintf(stderr, "[VIDEOMENU] Options menu %08X: %d rows\n", (unsigned)k_options_headers[k], n);
    }
    /* "Presentation" right under VIP on the pre-game screen. */
    fprintf(stderr, "[VIDEOMENU] Coach Match Up: %d rows\n",
            vm_add_link(COACH_MATCH_UP_HEADER, pres, vm_wstr("Presentation")));
    if (mods)
        fprintf(stderr, "[VIDEOMENU] Features: %d rows\n",
                vm_add_link(FEATURES_HEADER, mods, vm_wstr("Mod Packs")));
    fprintf(stderr, "[VIDEOMENU] Video Settings %08X, Presentation %08X, Mod Packs %08X, %u bytes\n",
            (unsigned)video, (unsigned)pres, (unsigned)mods, (unsigned)s_heap_used);
}

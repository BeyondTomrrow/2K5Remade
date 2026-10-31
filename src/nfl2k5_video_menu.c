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
extern void nfl2k5_discord_init(void);
extern int nfl2k5_pres_values(int what);
extern int nfl2k5_pres_get(int what);
extern void nfl2k5_pres_set(int what, int v);
extern void nfl2k5_pres_value_name(int what, int idx, char *buf, size_t n);

#define VM_FN_BASE    0xFEC00000u   /* + screen * 0x1000 + row * 0x40 + slot * 4 */
#define VM_ROW        0x34u
#define VM_MAX_ROWS   24            /* Video Settings has 18 (row * 0x40 stays inside a screen's 0x1000) */
#define VM_MAX_VALUES 64
#define VM_STR_CHARS  64
#define GAME_OPTIONS_HEADER 0x00501E48u    /* template for the new screens */

static const uint32_t k_options_headers[] = { 0x00503288u, 0x00503458u, 0x00503628u, 0x005038C8u };
#define MAIN_MENU_HEADER      0x00515660u   /* front-end main menu */
#define COACH_MATCH_UP_HEADER 0x00585474u   /* pre-game menu: Start Game ... VIP */
#define TEAM_OPTIONS_HEADER   0x00526C10u   /* pre-game "Team Options": Stadium, Playbooks, Difficulty, ... */
#define CAMERA_OPTIONS_HEADER 0x0052B8A0u   /* in-game Camera settings: Camera, QB Pivot Mode, Runner Pivot Mode,
                                             * Pass Play Zoom Out (+8 holds the 0xF3FC0 menu magic; rows @ 0x52B700).
                                             * Found by matching the row label strings in the retail XBE. */

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
                                             "Scorebug Animations", "Theme Volume", "Custom Commentary" };
static int pres_toggle(int r) { return r == 3 || r == 5; }
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

/* ---- Coach Mode rows (nfl2k5_coach_mode.c) ---- */
extern int  nfl2k5_coach_enabled(void);
extern void nfl2k5_coach_set_enabled(int);
extern int  nfl2k5_coach_role(void);
extern void nfl2k5_coach_set_role(int);
extern int  nfl2k5_coach_team_home(void);
extern void nfl2k5_coach_set_team_home(int);
extern int  nfl2k5_coach_camera(void);
extern void nfl2k5_coach_set_camera(int);
static const char *const k_coach_labels[] = { "Coach Mode", "Coaching Role", "Your Team", "Sideline Camera" };
static int coach_toggle(int r) { return r == 0 || r == 3; }
static int coach_values(int r) { return r == 1 ? 3 : 2; }
static int coach_get(int r)
{
    switch (r) {
    case 0: return nfl2k5_coach_enabled() ? 1 : 0;
    case 1: return nfl2k5_coach_role();
    case 2: return nfl2k5_coach_team_home() ? 0 : 1;   /* 0 = Home, 1 = Away */
    case 3: return nfl2k5_coach_camera() ? 1 : 0;
    }
    return 0;
}
static void coach_set(int r, int v)
{
    switch (r) {
    case 0: nfl2k5_coach_set_enabled(v); break;
    case 1: nfl2k5_coach_set_role(v); break;
    case 2: nfl2k5_coach_set_team_home(v == 0); break;
    case 3: nfl2k5_coach_set_camera(v); break;
    }
}
static void coach_name(int r, int v, char *b, size_t n)
{
    static const char *const role[] = { "Head Coach", "Offensive Coord.", "Defensive Coord." };
    static const char *const team[] = { "Home", "Away" };
    if (r == 1) snprintf(b, n, "%s", role[(unsigned)v % 3u]);
    else if (r == 2) snprintf(b, n, "%s", team[(unsigned)v % 2u]);
    else snprintf(b, n, "%s", v ? "On" : "Off");
}

/* ---- Pro Broadcast camera row (nfl2k5_broadcast_cam.c) ---- */
extern int  nfl2k5_broadcast_cam_mode(void);
extern void nfl2k5_broadcast_cam_set_mode(int);
extern int  nfl2k5_broadcast_cam_mode_count(void);
extern const char *nfl2k5_broadcast_cam_mode_name(int);
static const char *const k_bcast_labels[] = { "Pro Broadcast" };
static int bcast_toggle(int r) { (void)r; return 0; }   /* type 7: a value selector */
static int bcast_values(int r) { (void)r; return nfl2k5_broadcast_cam_mode_count(); }
static int bcast_get(int r) { (void)r; return nfl2k5_broadcast_cam_mode(); }
static void bcast_set(int r, int v) { (void)r; nfl2k5_broadcast_cam_set_mode(v); }
static void bcast_name(int r, int v, char *b, size_t n) { (void)r; snprintf(b, n, "%s", nfl2k5_broadcast_cam_mode_name(v)); }

#ifdef __ANDROID__
/* ---- Phone Settings rows (src/platform/android/android_settings.c) ---- */
extern int nfl2k5_phone_rows(void);
extern const char *nfl2k5_phone_label(int r);
extern int nfl2k5_phone_toggle(int r);
extern int nfl2k5_phone_values(int r);
extern int nfl2k5_phone_get(int r);
extern void nfl2k5_phone_set(int r, int v);
extern void nfl2k5_phone_value_name(int r, int v, char *b, size_t n);
static void phone_name(int r, int v, char *b, size_t n) { nfl2k5_phone_value_name(r, v, b, n); }
static const char *s_phone_labels[VM_MAX_ROWS];
#endif

static const char *s_video_labels[VM_MAX_ROWS];
static const char *s_mod_labels[VM_MAX_ROWS];
static const char *const k_custom_labels[] = { "Griddy", "Slide", "Stank", "Slickback" };
static const char *const k_custom_resources[] = { "ANM_CUSTOM_GRIDDY_01", "ANM_CUSTOM_SLIDE_001",
                                                  "ANM_CUSTOM_STANK_001", "ANM_CUSTOM_SLICKBACK1" };
static uint32_t s_custom_label_names[4];
static uint32_t s_custom_resource_names[4];
static uint32_t s_custom_selector_rows;

uint32_t nfl2k5_celebration_label(uint32_t index)
{
    static int logs;
    if (logs++ < 16)
        fprintf(stderr, "[CELEBRATIONS] label index=%u ptr=%08X first=%04X\n",
                (unsigned)index,
                (unsigned)(index >= 37u && index < 41u ? s_custom_label_names[index - 37u] : 0),
                (unsigned)(index >= 37u && index < 41u && s_custom_label_names[index - 37u]
                    ? MEM16(s_custom_label_names[index - 37u]) : 0));
    if (index < 37u) return MEM32(0x005858D4u + index * 8u);
    return index < 41u ? s_custom_label_names[index - 37u] : 0;
}

uint32_t nfl2k5_celebration_selector_row(uint32_t index)
{
    if (index < 37u) return 0x0050CFC8u + index * 12u;
    return index < 41u ? s_custom_selector_rows + (index - 37u) * 12u : 0;
}

uint32_t nfl2k5_celebration_resource(uint32_t index)
{
    uint32_t row = nfl2k5_celebration_selector_row(index);
    static int logs;
    if (logs++ < 16)
        fprintf(stderr, "[CELEBRATIONS] resource index=%u row=%08X name=%08X first=%04X\n",
                (unsigned)index, (unsigned)row, (unsigned)(row ? MEM32(row + 4u) : 0),
                (unsigned)(row && MEM32(row + 4u) ? MEM16(MEM32(row + 4u)) : 0));
    return row ? MEM32(row + 4u) : 0;
}

/* Team touchdown celebrations always two dancers (the custom duos such as My
 * World replace both two-dancer clips). NFL2K5_FORCE_DUO=0/1 or
 * custom-animations\team.ini "force_duo = 1". */
int nfl2k5_team_force_duo(void)
{
    static int force = -1;
    if (force < 0) {
        const char *e = getenv("NFL2K5_FORCE_DUO");
        force = 0;
        if (e) {
            force = atoi(e) != 0;
        } else {
            FILE *f = fopen("custom-animations\\team.ini", "r");
            char line[128];
            int v;
            if (f) {
                while (fgets(line, sizeof line, f))
                    if (sscanf(line, " force_duo = %d", &v) == 1) { force = v != 0; break; }
                fclose(f);
            }
        }
        fprintf(stderr, "[CELEBRATIONS] team celebrations: %s\n", force ? "always the duo (My World)" : "game's own mix");
    }
    return force;
}

uint32_t nfl2k5_celebration_after_retail(uint32_t remaining)
{
    return remaining < 4u ? 37u + remaining : 41u;
}

static VmScreen s_screens[] = {
    { "Video Settings", 0, s_video_labels, vid_toggle, vid_values, vid_get, vid_set, vid_name },
    { "Presentation", 6, k_pres_labels, pres_toggle, pres_values, pres_get, pres_set, pres_name },
    { "Mod Packs", 0, s_mod_labels, mods_toggle, mods_values, mods_get, mods_set, mods_name },
    { "Coach Mode", 4, k_coach_labels, coach_toggle, coach_values, coach_get, coach_set, coach_name },
    { "Pro Broadcast", 1, k_bcast_labels, bcast_toggle, bcast_values, bcast_get, bcast_set, bcast_name },
#ifdef __ANDROID__
    { "Phone Settings", 0, s_phone_labels, nfl2k5_phone_toggle, nfl2k5_phone_values, nfl2k5_phone_get, nfl2k5_phone_set, phone_name },
#endif
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

/* Append a settings screen's VALUE rows directly onto an existing menu's row
 * array (e.g. Coach Mode rows onto the pre-game Team Options screen), before
 * its type-3 end row. The screen's per-row scratch (cur/table/pool) must
 * already be allocated (call vm_build_screen for it first). */
static int vm_append_value_rows(uint32_t menu_header, int index)
{
    VmScreen *sc = &s_screens[index];
    uint32_t old = MEM32(menu_header + 0x10), copy;
    int n = 0, i;
    if (!old) return 0;
    while (n < 32 && MEM32(old + VM_ROW * (uint32_t)n) != 3u) n++;
    if (n >= 32) return 0;
    copy = vm_alloc(VM_ROW * (uint32_t)(n + sc->rows + 1));
    if (!copy) return 0;
    vm_copy(copy, old, VM_ROW * (uint32_t)n);
    for (i = 0; i < sc->rows; i++) {
        uint32_t r = copy + VM_ROW * (uint32_t)(n + i), k;
        for (k = 0; k < VM_ROW; k += 4) MEM32(r + k) = 0;
        MEM32(r + 0x00) = sc->toggle(i) ? 5u : 7u;
        MEM32(r + 0x04) = vm_wstr(sc->labels[i]);
        for (k = 0; k < 7; k++)
            MEM32(r + 0x0C + k * 4u) = VM_FN_BASE + (uint32_t)index * 0x1000u + (uint32_t)i * 0x40u + k * 4u;
    }
    vm_copy(copy + VM_ROW * (uint32_t)(n + sc->rows), old + VM_ROW * (uint32_t)n, VM_ROW);   /* end row */
    MEM32(menu_header + 0x10) = copy;
    MEM32(menu_header + 0x28) |= 0x4u;     /* let the extra rows scroll into view */
    return n + sc->rows;
}

/* Resolve a main-menu link by its existing label.  This avoids hard-coding
 * the Extras submenu address and leaves the visible main-menu rows intact. */
static int vm_row_label_is(uint32_t row, const char *label)
{
    uint32_t text = MEM32(row + 0x04);
    const char *p = label;
    if (!text || !label) return 0;
    while (*p && MEM16(text) == (uint16_t)(unsigned char)*p) {
        ++p;
        text += 2u;
    }
    return !*p && MEM16(text) == 0u;
}

static int vm_row_label_starts(uint32_t row, const char *prefix)
{
    uint32_t text = MEM32(row + 0x04);
    const char *p = prefix;
    if (!text || !prefix) return 0;
    while (*p && MEM16(text) == (uint16_t)(unsigned char)*p) {
        ++p;
        text += 2u;
    }
    return !*p;
}

static uint32_t vm_link_target(uint32_t menu_header, const char *label)
{
    uint32_t rows = MEM32(menu_header + 0x10);
    int n;
    if (!rows || !label) return 0;
    for (n = 0; n < 32; n++) {
        uint32_t row = rows + VM_ROW * (uint32_t)n;
        if (MEM32(row + 0x00) == 3u) break;
        if (MEM32(row + 0x00) == 0u && vm_row_label_is(row, label))
            return MEM32(row + 0x08);
    }
    return 0;
}

/* Keep Extras short enough that its native menu does not scroll. */
static int vm_remove_links(uint32_t menu_header, const char *first, const char *second)
{
    uint32_t old = MEM32(menu_header + 0x10), copy;
    int n = 0, out = 0, removed = 0;
    if (!old) return 0;
    while (n < 32 && MEM32(old + VM_ROW * (uint32_t)n) != 3u) n++;
    if (n >= 32) return 0;
    copy = vm_alloc(VM_ROW * (uint32_t)(n + 1));
    if (!copy) return 0;
    for (int i = 0; i < n; ++i) {
        uint32_t row = old + VM_ROW * (uint32_t)i;
        if (MEM32(row) == 0u && (vm_row_label_is(row, first) || vm_row_label_is(row, second) || vm_row_label_starts(row, "ESPN"))) {
            removed++;
            continue;
        }
        vm_copy(copy + VM_ROW * (uint32_t)out++, row, VM_ROW);
    }
    vm_copy(copy + VM_ROW * (uint32_t)out, old + VM_ROW * (uint32_t)n, VM_ROW);
    if (removed) MEM32(menu_header + 0x10) = copy;
    return removed;
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
    return 0;   /* not reached */
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
    nfl2k5_discord_init();
    if (!GetCurrentDirectoryA(sizeof(root), root)) snprintf(root, sizeof(root), ".");
    nfl2k5_modpacks_init(root);
    if (getenv("NFL2K5_NO_VIDEO_MENU")) return;
    s_heap_size = 512u * 1024u;
    s_heap = xbox_HeapAlloc(s_heap_size, 16);
    if (!s_heap) { fprintf(stderr, "[VIDEOMENU] no guest memory\n"); return; }
    s_custom_selector_rows = vm_alloc(4u * 12u);
    if (!s_custom_selector_rows) { fprintf(stderr, "[VIDEOMENU] no celebration rows\n"); return; }
    for (i = 0; i < 4; ++i) {
        uint32_t row = s_custom_selector_rows + (uint32_t)i * 12u;
        s_custom_label_names[i] = vm_wstr(k_custom_labels[i]);
        s_custom_resource_names[i] = vm_wstr(k_custom_resources[i]);
        MEM32(row) = 0;
        MEM32(row + 4u) = s_custom_resource_names[i];
        /* Same as retail row 2 (ANM_CELEBRATE_USER_34), the clip every
         * custom record was built from: no left-hand variant, field 0x15. */
        MEM32(row + 8u) = 0x15u;
        fprintf(stderr, "[CELEBRATIONS] init %d label=%08X/%04X resource=%08X/%04X row=%08X\n",
                i, (unsigned)s_custom_label_names[i], (unsigned)MEM16(s_custom_label_names[i]),
                (unsigned)s_custom_resource_names[i], (unsigned)MEM16(s_custom_resource_names[i]),
                (unsigned)row);
    }
    s_screens[0].rows = xbox_VideoSettingCount();
    for (i = 0; i < s_screens[0].rows && i < VM_MAX_ROWS; i++)
        s_video_labels[i] = xbox_VideoSettingName(i);
    s_screens[2].rows = nfl2k5_modpacks_count();
    for (i = 0; i < s_screens[2].rows && i < VM_MAX_ROWS; i++)
        s_mod_labels[i] = nfl2k5_modpacks_name(i);
    video = vm_build_screen(0);
    pres = vm_build_screen(1);
    /* Coach Mode: build the screen (allocates its per-row scratch), then add its
     * rows straight onto the pre-game Team Options screen. */
    if (vm_build_screen(3)) {
        int cn = vm_append_value_rows(TEAM_OPTIONS_HEADER, 3);
        fprintf(stderr, "[VIDEOMENU] Coach Mode: %d rows on Team Options %08X\n", cn, (unsigned)TEAM_OPTIONS_HEADER);
    }
    /* Pro Broadcast is a camera choice, so it goes on the in-game Camera
     * settings screen (right under Camera / QB Pivot / Runner Pivot / Pass Play
     * Zoom Out), NOT on Team Options. */
    if (vm_build_screen(4)) {
        int bn = vm_append_value_rows(CAMERA_OPTIONS_HEADER, 4);
        fprintf(stderr, "[VIDEOMENU] Pro Broadcast: %d rows on Camera settings %08X\n", bn, (unsigned)CAMERA_OPTIONS_HEADER);
    }
    /* Build this even when no packs are currently installed.  The main-menu
     * entry should always be present so players can find it, and a pack that
     * is discovered on the next launch will populate this same screen. */
    if (!getenv("NFL2K5_NO_MODPACK_MENU")) mods = vm_build_screen(2);
    if (!video || !pres) { fprintf(stderr, "[VIDEOMENU] build failed\n"); return; }
    for (k = 0; k < sizeof k_options_headers / sizeof k_options_headers[0]; k++) {
        int n = vm_add_link(k_options_headers[k], video, vm_wstr("Video Settings"));
        fprintf(stderr, "[VIDEOMENU] Options menu %08X: %d rows\n", (unsigned)k_options_headers[k], n);
    }
#ifdef __ANDROID__
    {   /* touch controls, battery saver, pausing, adding songs: under Video Settings */
        uint32_t phone;
        s_screens[5].rows = nfl2k5_phone_rows();
        for (i = 0; i < s_screens[5].rows && i < VM_MAX_ROWS; i++)
            s_phone_labels[i] = nfl2k5_phone_label(i);
        phone = vm_build_screen(5);
        for (k = 0; phone && k < sizeof k_options_headers / sizeof k_options_headers[0]; k++)
            vm_add_link(k_options_headers[k], phone, vm_wstr("Phone Settings"));
    }
#endif
    /* "Presentation" right under VIP on the pre-game screen. */
    fprintf(stderr, "[VIDEOMENU] Coach Match Up: %d rows\n",
            vm_add_link(COACH_MATCH_UP_HEADER, pres, vm_wstr("Presentation")));
    if (mods) {
        uint32_t extras = vm_link_target(MAIN_MENU_HEADER, "Extras");
        int removed = extras ? vm_remove_links(extras, "Game Registration", "ESPN Videogames") : 0;
        fprintf(stderr, "[VIDEOMENU] Extras menu %08X: removed %d, Mods row %d\n",
                (unsigned)extras, removed, extras ? vm_add_link(extras, mods, vm_wstr("Mods")) : 0);
    }
    fprintf(stderr, "[VIDEOMENU] Video Settings %08X, Presentation %08X, Mod Packs %08X, Celebrations 37-40, %u bytes\n",
            (unsigned)video, (unsigned)pres, (unsigned)mods, (unsigned)s_heap_used);
}

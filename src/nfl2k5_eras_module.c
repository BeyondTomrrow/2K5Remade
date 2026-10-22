/* My NFL Eras as a feature module (docs/eras/MOD-PACK-DESIGN.md 6): the only
 * entry points the game has into Eras. The host (src/nfl2k5_plugin_host.c)
 * calls nfl2k5_eras_module_query / _init, and from then on reaches Eras only
 * through the hooks filled here. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_guest_calls.h"   /* tools/eras/check_guest_calls.py */

#define ERAS_MODULE_VERSION "0.3.0"
#define ERAS_VA_BASE 0xFEE00000u
#define ERAS_VA_SIZE 0x00100000u

extern void nfl2k5_eras_menu_install(int retail);
extern nf_guest_fn nfl2k5_eras_menu_lookup(uint32_t address);
extern void nfl2k5_eras_fsm_pop(uint32_t fsm, uint32_t top);
extern void nfl2k5_eras_game_final(uint32_t home_points, uint32_t away_points);
extern void nfl2k5_eras_match_exit(void);
extern uint32_t nfl2k5_eras_return_desc(uint32_t edx);
extern void nfl2k5_eras_capture_init(void);

static const NfHostInfo *s_host;
static const NfHostServices *s_svc;
const NfHostServices *g_eras_svc;      /* the module's other files (nfl2k5_eras.h) */

const char *nfl2k5_eras_limited(void)
{
    if (s_host && (s_host->flags & NF_HOST_LIMITED) && s_host->limited_reason) return s_host->limited_reason;
    /* M16: the office / stadium files did not make it into the archive (a full drive): every
     * screen that reads them would end in the game's "disc is dirty or damaged" error */
    if (s_svc && s_svc->size >= sizeof *s_svc && s_svc->archive_problem && s_svc->archive_problem()[0])
        return s_svc->archive_problem();
    return "";
}

/* The result capture and the M4b return for packs that interpret the hooked
 * functions (their gen patches never run there): sub_000A2910 entry (ecx = home
 * points, edx = away points), sub_000645D0 entry, and 0x34C208, the `pop esi`
 * right after `mov edx, 0x515660` in sub_0034C1F0 (XBE bytes), so g_edx can be
 * rewritten before the jmp. */
static uint32_t ih_final(void) { nfl2k5_eras_game_final(g_ecx, g_edx); return 0; }
static uint32_t ih_exit(void) { nfl2k5_eras_match_exit(); return 0; }
static uint32_t ih_return(void) { g_edx = nfl2k5_eras_return_desc(g_edx); return 0; }

static int on_boot(void)
{
    s_svc->interp_add_hook(0x000A2910u, 0, ih_final);    /* HOOK_GAME_FINAL */
    s_svc->interp_add_hook(0x000645D0u, 0, ih_exit);     /* HOOK_MATCH_EXIT */
    s_svc->interp_add_hook(0x0034C208u, 0, ih_return);   /* HOOK_RETURN */
    nfl2k5_eras_capture_init();                          /* the NFL2K5_ERAS_CAPTURE test switch */
    return 0;
}

static void on_menus_ready(void)
{
    nfl2k5_eras_menu_install(s_host->base_kind == NF_BASE_RETAIL || s_host->base_kind == NF_BASE_WIDESCREEN);
}

/* The stadium bundles (stadiums\<name>\files\*, docs/eras/research/COLISEUM-IMPORT.md) then
 * the office packages (office\files\*.iff: erasoffice.iff / erasskin.iff, built from the
 * player's disc; docs/eras/ERAS-OFFICE-DESIGN.md 1.2) of each Eras root, a name once (the
 * first root has it; the sink refuses a repeat). The office last, so the other files keep
 * their place in the pack. */
static void on_archive_extend(NfArchiveSink *sink)
{
    int pass, r, n[2] = { 0, 0 };
    for (pass = 0; pass < 2; pass++)
        for (r = 0; r < nfl2k5_eras_root_count(); r++) {
            WIN32_FIND_DATAA sd, fd;
            char base[MAX_PATH], pat[MAX_PATH], dir[MAX_PATH], host[MAX_PATH];
            HANDLE hs = INVALID_HANDLE_VALUE;
            nfl2k5_eras_root_path(r, pass ? "office" : "stadiums", base, sizeof base);
            if (pass == 0) {
                snprintf(pat, sizeof pat, "%s\\*", base);
                hs = FindFirstFileA(pat, &sd);
                if (hs == INVALID_HANDLE_VALUE) continue;
            }
            do {
                HANDLE h;
                if (pass == 0) {
                    if (!(sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || sd.cFileName[0] == '.') continue;
                    snprintf(dir, sizeof dir, "%s\\%s\\files", base, sd.cFileName);
                    snprintf(pat, sizeof pat, "%s\\*", dir);
                } else {
                    snprintf(dir, sizeof dir, "%s\\files", base);
                    snprintf(pat, sizeof pat, "%s\\*.iff", dir);
                }
                h = FindFirstFileA(pat, &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    snprintf(host, sizeof host, "%s\\%s", dir, fd.cFileName);
                    n[pass] += sink->add(sink, host, fd.cFileName) != 0;
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            } while (pass == 0 && FindNextFileA(hs, &sd));
            if (hs != INVALID_HANDLE_VALUE) FindClose(hs);
        }
    if (n[0]) fprintf(stderr, "[TEAMFILES] My NFL Eras stadiums: %d files\n", n[0]);
    if (n[1]) fprintf(stderr, "[TEAMFILES] My NFL Eras office: %d files\n", n[1]);
}

int nfl2k5_eras_module_query(NfModuleInfo *out)
{
    if (!out || out->size < sizeof *out) return -1;
    out->api_major = NF_PLUGIN_API_MAJOR;
    out->api_minor = NF_PLUGIN_API_MINOR;
    out->id = "my-nfl-eras";
    out->version = ERAS_MODULE_VERSION;
    out->data_schema_min = 1;
    out->data_schema_max = 1;
    out->guest_calls = k_eras_guest_calls;
    out->guest_call_count = NFL2K5_ERAS_GUEST_CALL_COUNT;
    out->va_range_base = ERAS_VA_BASE;
    out->va_range_size = ERAS_VA_SIZE;
    return 0;
}

int nfl2k5_eras_module_init(const NfHostInfo *host, const NfHostServices *svc, NfModuleHooks *h)
{
    if (!host || !svc || !h || svc->size < sizeof(NfHostServices) || h->size < sizeof *h) return -1;
    s_host = host;
    s_svc = svc;
    g_eras_svc = svc;
    h->on_boot = on_boot;
    h->on_archive_extend = on_archive_extend;
    h->on_menus_ready = on_menus_ready;
    h->lookup_va = nfl2k5_eras_menu_lookup;
    h->on_fsm_pop = nfl2k5_eras_fsm_pop;
    h->on_game_final = nfl2k5_eras_game_final;
    h->on_match_exit = nfl2k5_eras_match_exit;
    h->return_desc = nfl2k5_eras_return_desc;
    return 0;
}

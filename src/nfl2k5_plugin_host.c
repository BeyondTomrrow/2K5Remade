/* Feature modules: the host side of src/nfl2k5_plugin_api.h
 * (docs/eras/MOD-PACK-DESIGN.md 5-6).
 *
 * One hook table, g_mod, filled at boot by nfl2k5_plugins_boot. Every place
 * the game reaches a feature (the gen patches HOOK_GAME_FINAL / HOOK_MATCH_EXIT
 * / HOOK_RETURN, the front-end FSM pop, the served-function VA range, the
 * archive extension, the menu install) calls one of the nfl2k5_hook_* shims
 * below, and nothing else: with the table empty each is one load and a
 * branch. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_plugin_api.h"
#include "interp/x86_interp.h"

static NfModuleHooks g_mod;
static NfModuleInfo g_mod_info;
static NfHostInfo g_host;
static NfHostServices g_svc;

/* ---- the shims ------------------------------------------------------------ */

void nfl2k5_hook_game_final(uint32_t home_pts, uint32_t away_pts)
{ if (g_mod.on_game_final) g_mod.on_game_final(home_pts, away_pts); }

void nfl2k5_hook_match_exit(void)
{ if (g_mod.on_match_exit) g_mod.on_match_exit(); }

uint32_t nfl2k5_hook_return_desc(uint32_t edx)
{ return g_mod.return_desc ? g_mod.return_desc(edx) : edx; }

void nfl2k5_hook_fsm_pop(uint32_t fsm, uint32_t top)
{ if (g_mod.on_fsm_pop) g_mod.on_fsm_pop(fsm, top); }

nf_guest_fn nfl2k5_hook_lookup_va(uint32_t va)
{
    if (!g_mod.lookup_va || va - g_mod_info.va_range_base >= g_mod_info.va_range_size) return NULL;
    return g_mod.lookup_va(va);
}

void nfl2k5_hook_archive_extend(NfArchiveSink *sink)
{ if (g_mod.on_archive_extend) g_mod.on_archive_extend(sink); }

void nfl2k5_hook_menus_ready(void)
{ if (g_mod.on_menus_ready) g_mod.on_menus_ready(); }

void nfl2k5_hook_shutdown(void)
{ if (g_mod.on_shutdown) g_mod.on_shutdown(); }

const NfHostInfo *nfl2k5_plugin_host_info(void) { return &g_host; }

/* ---- services (built-in wrappers) ----------------------------------------- */

static void svc_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void svc_interp_add_hook(uint32_t va, uint32_t from, uint32_t (*fn)(void))
{
    recomp_interp_add_hook(va, from, fn);
}

/* ---- boot ----------------------------------------------------------------- */

void nfl2k5_plugins_boot(const char *xbe_sha256, uint32_t base_kind, const char *xbe_pack_name, uint32_t flags)
{
    static char root[MAX_PATH];
    NfModuleHooks h;
    GetCurrentDirectoryA(sizeof root, root);
    memset(&g_host, 0, sizeof g_host);
    g_host.size = sizeof g_host;
    g_host.api_major = NF_PLUGIN_API_MAJOR;
    g_host.api_minor = NF_PLUGIN_API_MINOR;
    g_host.game_version = NFL2K5_GAME_VERSION;
    g_host.xbe_sha256 = xbe_sha256 ? xbe_sha256 : "";
    g_host.base_kind = base_kind;
    g_host.xbe_pack_name = xbe_pack_name;
    g_host.root_dir = root;
    g_host.pack_dir = "";
    g_host.generated_dir = "";
    g_host.pack_version = "";
    g_host.limited_reason = "";
    g_host.flags = flags;

    memset(&g_svc, 0, sizeof g_svc);
    g_svc.size = sizeof g_svc;
    g_svc.log = svc_log;
    g_svc.interp_add_hook = svc_interp_add_hook;

    memset(&g_mod_info, 0, sizeof g_mod_info);
    g_mod_info.size = sizeof g_mod_info;
    if (nfl2k5_eras_module_query(&g_mod_info) != 0) return;
    if (g_mod_info.api_major != NF_PLUGIN_API_MAJOR || g_mod_info.api_minor > NF_PLUGIN_API_MINOR) {
        fprintf(stderr, "[PLUGIN] %s: built for plugin API %u.%u, this game has %u.%u; not loaded\n",
                g_mod_info.id, g_mod_info.api_major, g_mod_info.api_minor, NF_PLUGIN_API_MAJOR, NF_PLUGIN_API_MINOR);
        return;
    }
    memset(&h, 0, sizeof h);
    h.size = sizeof h;
    if (nfl2k5_eras_module_init(&g_host, &g_svc, &h) != 0) {
        fprintf(stderr, "[PLUGIN] %s: init refused; not loaded\n", g_mod_info.id);
        return;
    }
    if (h.on_boot && h.on_boot() != 0) {
        fprintf(stderr, "[PLUGIN] %s: boot failed; not loaded\n", g_mod_info.id);
        return;
    }
    g_mod = h;
    fprintf(stderr, "[PLUGIN] %s %s: on (built in, plugin API %u.%u)\n", g_mod_info.id, g_mod_info.version,
            g_mod_info.api_major, g_mod_info.api_minor);
}

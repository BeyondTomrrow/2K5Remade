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
#include "nfl2k5_teams.h"
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

/* ---- the pack roots ----------------------------------------------------------
 *
 * Every pack file is named by its path relative to the pack root
 * (league\1980\teams.csv, offices\1980s\era.ini, office\files\erasoffice.iff,
 * teams\1980\colts\team.json, stadiums\<folder>\stadium.json, presentations.ini ...)
 * and looked up in each root in turn; the first that has it wins. The
 * development layout (the repository folders) is a root that maps league\ to
 * data\eras\runtime\ and everything else to mods\eras\. No module on: no roots. */
#define MAX_ROOTS 4
static int g_root_n;
static char g_root[MAX_ROOTS][MAX_PATH];
static int g_root_dev[MAX_ROOTS];

static void root_add(const char *dir, int dev)
{
    if (g_root_n >= MAX_ROOTS) return;
    snprintf(g_root[g_root_n], MAX_PATH, "%s", dir);
    g_root_dev[g_root_n++] = dev;
}

int nfl2k5_pack_root_count(void) { return g_root_n; }

char *nfl2k5_pack_root_path(int i, const char *rel, char *out, size_t n)
{
    char *p;
    if (!out || !n) return out;
    out[0] = 0;
    if (i < 0 || i >= g_root_n || !rel) return out;
    while (*rel == '\\' || *rel == '/') rel++;
    if (g_root_dev[i] && (!_strnicmp(rel, "league\\", 7) || !_strnicmp(rel, "league/", 7)))
        snprintf(out, n, "data\\eras\\runtime\\%s", rel + 7);
    else if (g_root_dev[i] && !_stricmp(rel, "league"))
        snprintf(out, n, "data\\eras\\runtime");
    else if (*rel)
        snprintf(out, n, "%s\\%s", g_root[i], rel);
    else
        snprintf(out, n, "%s", g_root[i]);
    for (p = out; *p; p++) if (*p == '/') *p = '\\';
    return out;
}

char *nfl2k5_pack_path(const char *rel, char *out, size_t n)
{
    int i;
    if (out && n) out[0] = 0;
    for (i = 0; i < g_root_n; i++)
        if (GetFileAttributesA(nfl2k5_pack_root_path(i, rel, out, n)) != INVALID_FILE_ATTRIBUTES) return out;
    return g_root_n ? nfl2k5_pack_root_path(g_root_n - 1, rel, out, n) : out;   /* nowhere: the last root's name */
}

int nfl2k5_pack_exists(const char *rel)
{
    char p[MAX_PATH];
    int i;
    for (i = 0; i < g_root_n; i++)
        if (GetFileAttributesA(nfl2k5_pack_root_path(i, rel, p, sizeof p)) != INVALID_FILE_ATTRIBUTES) return 1;
    return 0;
}

/* ---- services (built-in wrappers) ----------------------------------------- */

extern char *xbox_user_path(const char *rel, char *out, size_t n);
extern const char *xbox_user_dir(void);
extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
extern void nfl2k5_presentation_eras_begin(int season, int week, const char *home_id, const char *away_id);
extern void nfl2k5_presentation_eras_end(void);
extern void nfl2k5_texpack_era(const char *season_dir, const char *decade_dir);
extern void nfl2k5_texpack_eras_teams(const char *season_dir);
extern int nfl2k5_guest_player_stat_scope(uint32_t player, uint32_t selector, uint32_t scope, double *out);
extern int nfl2k5_guest_team_stat(uint32_t team, uint32_t selector, uint32_t scope, double *out);

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
    g_svc.path = nfl2k5_pack_path;
    g_svc.exists = nfl2k5_pack_exists;
    g_svc.root_count = nfl2k5_pack_root_count;
    g_svc.root_path = nfl2k5_pack_root_path;
    g_svc.user_path = xbox_user_path;
    g_svc.user_dir = xbox_user_dir;
    g_svc.toast = xbox_PresentToast;
    g_svc.stage_check = nfl2k5_teams_stage_check;
    g_svc.stage = nfl2k5_teams_stage;
    g_svc.stage_message = nfl2k5_teams_stage_message;
    g_svc.release = nfl2k5_teams_release;
    g_svc.staged_slot = nfl2k5_teams_staged_slot;
    g_svc.spare_slot = nfl2k5_teams_spare_slot;
    g_svc.resident = nfl2k5_teams_resident;
    g_svc.identity_check = nfl2k5_teams_identity_check;
    g_svc.stadium_find = nfl2k5_teams_stadium_find;
    g_svc.stadium_borrow = nfl2k5_teams_stadium_pack;
    g_svc.stadium_restore = nfl2k5_teams_stadium_pack_restore;
    g_svc.pres_eras_begin = nfl2k5_presentation_eras_begin;
    g_svc.pres_eras_end = nfl2k5_presentation_eras_end;
    g_svc.texpack_set_era = nfl2k5_texpack_era;
    g_svc.texpack_set_team_art = nfl2k5_texpack_eras_teams;
    g_svc.player_stat = nfl2k5_guest_player_stat_scope;
    g_svc.team_stat = nfl2k5_guest_team_stat;

    g_root_n = 0;
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
    /* the pack roots: the development layout (the pack loader replaces this, step 4) */
    root_add("mods\\eras", 1);
    if (h.on_boot && h.on_boot() != 0) {
        fprintf(stderr, "[PLUGIN] %s: boot failed; not loaded\n", g_mod_info.id);
        g_root_n = 0;
        return;
    }
    g_mod = h;
    fprintf(stderr, "[PLUGIN] %s %s: on (built in, plugin API %u.%u)\n", g_mod_info.id, g_mod_info.version,
            g_mod_info.api_major, g_mod_info.api_minor);
}

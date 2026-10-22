/* Feature modules: the host side of src/nfl2k5_plugin_api.h
 * (docs/eras/MOD-PACK-DESIGN.md 5-6).
 *
 * One hook table, g_mod, filled at boot by nfl2k5_plugins_boot. Every place
 * the game reaches a feature (the gen patches HOOK_GAME_FINAL / HOOK_MATCH_EXIT
 * / HOOK_RETURN, the front-end FSM pop, the served-function VA range, the
 * archive extension, the menu install) calls one of the nfl2k5_hook_* shims
 * below, and nothing else: with the table empty each is one load and a
 * branch.
 *
 * The gate (docs/eras/MOD-PACK-DESIGN.md 3): My NFL Eras is on only when
 *   1. mods\packs\MyNFLEras\pack.json parses, says "id": "my-nfl-eras",
 *      "kind": "feature", manifest_version 1, a data_schema the module reads and
 *      a min_game_version this game meets;
 *   2. it is not switched off: no "my-nfl-eras=0" in mods\features.ini, no
 *      --vanilla, NFL2K5_ERAS is not 0;
 *   3. the base is the retail or widescreen XBE with no interpreted XBE pack
 *      (SOFTDRINK): there it stays off, with one log line and one toast;
 *   4. the season package the manifest names is in the pack (teams.csv,
 *      schedule.csv, rosters\).
 * 1 or 4 failing with the pack present: the module runs LIMITED (its menu only,
 * the reason in the hub; no capture hooks, no archive files). No pack: nothing,
 * one log line. NFL2K5_ERAS=1 (developers) adds the repository folders
 * (data\eras\runtime, mods\eras) as the last root, and runs them as the pack
 * when no pack is installed. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_json.h"
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

static char g_off_toast[160];       /* shown once, at the menus, when the base turned the feature off */

void nfl2k5_hook_menus_ready(void)
{
    extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
    if (g_off_toast[0]) xbox_PresentToast("My NFL Eras is off", g_off_toast, "It runs on the clean game (launch with --retail)", 6000);
    g_off_toast[0] = 0;
    if (g_mod.on_menus_ready) g_mod.on_menus_ready();
}

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

/* The asset builder ships with the game (tools\NFL2K5-ErasBuilder.exe, like the Team Builder);
 * a development checkout runs the script. Its output goes to the pack's generated\ only. */
static int svc_run_builder(const char *args)
{
    char cmd[2048], exe[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    const char *cands[] = { "tools\\NFL2K5-ErasBuilder.exe", "build\\tools\\NFL2K5-ErasBuilder.exe" };
    int i;
    if (!g_host.pack_dir || !*g_host.pack_dir) return -1;
    exe[0] = 0;
    for (i = 0; i < 2 && !exe[0]; i++)
        if (GetFileAttributesA(cands[i]) != INVALID_FILE_ATTRIBUTES) snprintf(exe, sizeof exe, "%s", cands[i]);
    if (exe[0]) snprintf(cmd, sizeof cmd, "\"%s\" --pack \"%s\" %s", exe, g_host.pack_dir, args ? args : "");
    else if (GetFileAttributesA("tools\\eras\\build_assets.py") != INVALID_FILE_ATTRIBUTES)
        snprintf(cmd, sizeof cmd, "python -I tools\\eras\\build_assets.py --pack \"%s\" %s", g_host.pack_dir, args ? args : "");
    else { fprintf(stderr, "[ERAS] builder: tools\\NFL2K5-ErasBuilder.exe not found\n"); return -1; }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[ERAS] builder: could not start %s (error %lu)\n", cmd, (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    fprintf(stderr, "[ERAS] builder started: %s\n", cmd);
    return 0;
}

/* ---- the pack manifest (docs/eras/MOD-PACK-DESIGN.md 5) ---------------------- */

#define ERAS_PACK_DIR "mods\\packs\\MyNFLEras"

/* A manifest path: relative, '/' or '\\' separated, no drive, no "..", only
 * ordinary names (the mp_safe_folder rules per component). */
static int safe_rel(const char *p)
{
    const char *c = p;
    if (!p || !*p || strlen(p) >= 96 || *p == '/' || *p == '\\' || strchr(p, ':')) return 0;
    while (*c) {
        const char *e = c + strcspn(c, "/\\");
        size_t n = (size_t)(e - c), k;
        if (!n || (n == 2 && c[0] == '.' && c[1] == '.') || (n == 1 && c[0] == '.')) return 0;
        for (k = 0; k < n; k++) {
            char ch = c[k];
            if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9') && !strchr("_-. ", ch)) return 0;
        }
        c = *e ? e + 1 : e;
    }
    return 1;
}

/* "1.7.0" against "1.10" ... : <0, 0, >0 */
static int version_cmp(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static int feature_switched_off(const char *id)
{
    char line[256];
    FILE *f = fopen("mods\\features.ini", "r");
    int off = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char key[96], val[32];
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        if (sscanf(line, " %95[^= \t] = %31s", key, val) == 2 && !_stricmp(key, id)) off = !strcmp(val, "0") || !_stricmp(val, "off");
    }
    fclose(f);
    return off;
}

static char g_pack_version[32], g_pack_name[64], g_limited[160], g_gen_dir[MAX_PATH];
static char g_pkg_dir[8][96];
static int g_pkg_n;
static uint32_t g_data_schema;

/* 4: every season package the manifest names has teams.csv, schedule.csv and rosters\
 * in the roots (0 and g_limited set when one is missing). */
static int packages_ok(void)
{
    int i;
    for (i = 0; i < g_pkg_n; i++) {
        char rel[160];
        snprintf(rel, sizeof rel, "%s/teams.csv", g_pkg_dir[i]);
        if (!nfl2k5_pack_exists(rel)) { snprintf(g_limited, sizeof g_limited, "season data missing: %s (see MISSING\\SUPPLY.txt)", rel); return 0; }
        snprintf(rel, sizeof rel, "%s/schedule.csv", g_pkg_dir[i]);
        if (!nfl2k5_pack_exists(rel)) { snprintf(g_limited, sizeof g_limited, "season data missing: %s", rel); return 0; }
        snprintf(rel, sizeof rel, "%s/rosters", g_pkg_dir[i]);
        if (!nfl2k5_pack_exists(rel)) { snprintf(g_limited, sizeof g_limited, "rosters missing: %s (see MISSING\\SUPPLY.txt)", rel); return 0; }
    }
    return 1;
}

/* 1: a valid pack (roots set); 0: no pack; -1: a pack that cannot run (g_limited says why). */
static int load_pack(const NfModuleInfo *mi)
{
    static NjDoc doc;
    char path[MAX_PATH], *text;
    FILE *f;
    long len;
    int root = 0, pk, i, n;
    const char *v;
    snprintf(path, sizeof path, "%s\\pack.json", ERAS_PACK_DIR);
    if (!(f = fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    if (len <= 0 || len > 256 * 1024 || !(text = (char *)malloc((size_t)len))) { fclose(f); snprintf(g_limited, sizeof g_limited, "pack.json is empty or too large"); return -1; }
    len = (long)fread(text, 1, (size_t)len, f);
    fclose(f);
    i = nj_parse(&doc, text, (size_t)len);
    free(text);
    if (i) { snprintf(g_limited, sizeof g_limited, "pack.json: %s", doc.error); return -1; }
    snprintf(g_pack_version, sizeof g_pack_version, "%s", nj_str(&doc, root, "version", "?"));
    snprintf(g_pack_name, sizeof g_pack_name, "%s", nj_str(&doc, root, "display_name", nj_str(&doc, root, "name", "My NFL Eras")));
    if (strcmp(nj_str(&doc, root, "id", ""), mi->id) || strcmp(nj_str(&doc, root, "kind", ""), "feature")) {
        snprintf(g_limited, sizeof g_limited, "pack.json is not the %s feature pack", mi->id); return -1;
    }
    if ((int)nj_num(&doc, root, "manifest_version", 0) != 1) {
        snprintf(g_limited, sizeof g_limited, "pack.json manifest_version %d is not supported", (int)nj_num(&doc, root, "manifest_version", 0)); return -1;
    }
    v = nj_str(&doc, root, "min_game_version", "0");
    if (version_cmp(v, NFL2K5_GAME_VERSION) > 0) {
        snprintf(g_limited, sizeof g_limited, "%s %s needs NFL2K5-PC %s or newer", g_pack_name, g_pack_version, v); return -1;
    }
    g_data_schema = (uint32_t)nj_num(&doc, root, "data_schema", 0);
    if (g_data_schema < mi->data_schema_min || g_data_schema > mi->data_schema_max) {
        snprintf(g_limited, sizeof g_limited, "%s %s: data schema %u needs a newer game", g_pack_name, g_pack_version, g_data_schema); return -1;
    }
    v = nj_str(&doc, nj_get(&doc, root, "module"), "builtin", "");
    if (strcmp(v, "eras")) { snprintf(g_limited, sizeof g_limited, "pack.json names no built-in module this game has"); return -1; }
    /* roots: generated\ first, then the pack */
    snprintf(g_gen_dir, sizeof g_gen_dir, "%s\\generated", ERAS_PACK_DIR);
    root_add(g_gen_dir, 0);
    root_add(ERAS_PACK_DIR, 0);
    /* the season packages (checked by packages_ok once every root is known) */
    pk = nj_get(&doc, nj_get(&doc, root, "league"), "packages");
    for (i = 0, n = 0; nj_at(&doc, pk, i) >= 0 && n < 8; i++) {
        const char *dir = nj_str(&doc, nj_at(&doc, pk, i), "dir", "");
        if (!safe_rel(dir)) { snprintf(g_limited, sizeof g_limited, "pack.json: package dir '%s' is not a pack path", dir); return -1; }
        snprintf(g_pkg_dir[n++], sizeof g_pkg_dir[0], "%s", dir);
    }
    g_pkg_n = n;
    if (!n) { snprintf(g_limited, sizeof g_limited, "pack.json names no season package"); return -1; }
    return 1;
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
    g_svc.run_builder = svc_run_builder;
    {   /* M16: the archive's extra pack (src/nfl2k5_team_files.c) */
        extern const char *nfl2k5_archive_problem(void);
        g_svc.archive_problem = nfl2k5_archive_problem;
    }

    g_root_n = 0;
    memset(&g_mod_info, 0, sizeof g_mod_info);
    g_mod_info.size = sizeof g_mod_info;
    if (nfl2k5_eras_module_query(&g_mod_info) != 0) return;
    if (g_mod_info.api_major != NF_PLUGIN_API_MAJOR || g_mod_info.api_minor > NF_PLUGIN_API_MINOR) {
        fprintf(stderr, "[PLUGIN] %s: built for plugin API %u.%u, this game has %u.%u; not loaded\n",
                g_mod_info.id, g_mod_info.api_major, g_mod_info.api_minor, NF_PLUGIN_API_MAJOR, NF_PLUGIN_API_MINOR);
        return;
    }
    {   /* the gate (top of this file) */
        const char *env = getenv("NFL2K5_ERAS");
        int dev = env && *env == '1', pack;
        if (flags & NF_HOST_VANILLA) { fprintf(stderr, "[ERAS] off: --vanilla (no feature packs)\n"); return; }
        if (env && *env == '0') { fprintf(stderr, "[ERAS] off: NFL2K5_ERAS=0\n"); return; }
        if (feature_switched_off(g_mod_info.id)) { fprintf(stderr, "[ERAS] off: switched off (mods\\features.ini %s=0)\n", g_mod_info.id); return; }
        g_limited[0] = 0;
        pack = load_pack(&g_mod_info);
        if (GetFileAttributesA(ERAS_PACK_DIR "\\dev.txt") != INVALID_FILE_ATTRIBUTES) dev = 1;
        if (pack == 0 && !dev) { fprintf(stderr, "[ERAS] off: no %s\\pack.json (My NFL Eras is not installed)\n", ERAS_PACK_DIR); return; }
        if (base_kind != NF_BASE_RETAIL && base_kind != NF_BASE_WIDESCREEN) {
            fprintf(stderr, "[ERAS] off: %s is on (feature not supported on that base)\n",
                    xbe_pack_name && *xbe_pack_name ? xbe_pack_name : "a modified game executable");
            snprintf(g_off_toast, sizeof g_off_toast, "%s is on", xbe_pack_name && *xbe_pack_name ? xbe_pack_name : "A mod pack");
            g_root_n = 0;
            return;
        }
        if (dev) {
            root_add("mods\\eras", 1);     /* the repository folders, last */
            g_host.flags |= NF_HOST_DEV_LAYOUT;
        }
        if (pack > 0 && !packages_ok()) pack = -1;
        if (pack < 0) {
            g_host.flags |= NF_HOST_LIMITED;
            g_host.limited_reason = g_limited;
            if (!g_root_n) root_add(ERAS_PACK_DIR, 0);
            fprintf(stderr, "[ERAS] limited: %s (menu only)\n", g_limited);
        }
        if (g_pack_version[0]) {
            g_host.pack_dir = ERAS_PACK_DIR;
            g_host.generated_dir = g_gen_dir;
            g_host.pack_version = g_pack_version;
            g_host.data_schema = g_data_schema;
        }
        fprintf(stderr, "[ERAS] pack %s %s%s: roots", g_pack_version[0] ? ERAS_PACK_DIR : "(none)", g_pack_version,
                dev ? " + development folders (NFL2K5_ERAS=1)" : "");
        for (pack = 0; pack < g_root_n; pack++) fprintf(stderr, " %s", g_root[pack]);
        fprintf(stderr, "\n");
    }
    memset(&h, 0, sizeof h);
    h.size = sizeof h;
    if (nfl2k5_eras_module_init(&g_host, &g_svc, &h) != 0) {
        fprintf(stderr, "[PLUGIN] %s: init refused; not loaded\n", g_mod_info.id);
        g_root_n = 0;
        return;
    }
    if (g_host.flags & NF_HOST_LIMITED) {
        /* the menu only: the hub shows why; nothing reaches the archive or the match */
        NfModuleHooks lim;
        memset(&lim, 0, sizeof lim);
        lim.size = sizeof lim;
        lim.on_menus_ready = h.on_menus_ready;
        lim.lookup_va = h.lookup_va;
        lim.on_fsm_pop = h.on_fsm_pop;
        h = lim;
    } else if (h.on_boot && h.on_boot() != 0) {
        fprintf(stderr, "[PLUGIN] %s: boot failed; not loaded\n", g_mod_info.id);
        g_root_n = 0;
        return;
    }
    g_mod = h;
    fprintf(stderr, "[PLUGIN] %s %s: %s (built in, plugin API %u.%u, %u guest functions)\n", g_mod_info.id, g_mod_info.version,
            g_host.flags & NF_HOST_LIMITED ? "limited" : "on", g_mod_info.api_major, g_mod_info.api_minor, g_mod_info.guest_call_count);
}

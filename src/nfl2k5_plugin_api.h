/* Feature modules (docs/eras/MOD-PACK-DESIGN.md 6): the seam between the game
 * and a feature such as My NFL Eras.
 *
 * Every place the game reaches a feature goes through one hook table
 * (NfModuleHooks), filled only when the feature's pack is installed and valid
 * (src/nfl2k5_plugin_host.c). While the table is empty each hook is one load
 * and a branch. A feature reaches the game only through NfHostServices.
 *
 * C, __cdecl, POD structs only. Every struct starts with its size, so either
 * side can tell which members the other knows; the host grows by adding
 * members at the end. Today the one module is built in (src/nfl2k5_eras_module.c);
 * the same header is what a future MyNFLEras.dll would be built against. */
#ifndef NFL2K5_PLUGIN_API_H
#define NFL2K5_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NF_PLUGIN_API_MAJOR 1
#define NF_PLUGIN_API_MINOR 0

/* The game's version, for a pack's min_game_version. */
#define NFL2K5_GAME_VERSION "0.7.0"

enum { NF_BASE_RETAIL = 1, NF_BASE_WIDESCREEN = 2, NF_BASE_INTERP_PACK = 3, NF_BASE_OTHER = 4 };
enum {
    NF_HOST_VANILLA = 1,        /* --vanilla: no feature packs (never set when a module runs) */
    NF_HOST_RETAIL_SWITCH = 2,  /* --retail: the selected XBE pack was ignored */
    NF_HOST_DEV_LAYOUT = 4,     /* the development folders are an Eras root (NFL2K5_ERAS=1) */
    NF_HOST_LIMITED = 8         /* the pack is installed but cannot run (reason in NfHostInfo.limited_reason) */
};

typedef struct NfHostInfo {
    uint32_t size;
    uint16_t api_major, api_minor;       /* the host's */
    const char *game_version;            /* NFL2K5_GAME_VERSION */
    const char *xbe_sha256;              /* the loaded base */
    uint32_t base_kind;                  /* NF_BASE_* */
    const char *xbe_pack_name;           /* NULL unless an XBE pack is on */
    const char *root_dir;                /* the game folder (the working directory) */
    const char *pack_dir;                /* the feature pack folder, relative to root_dir ("" = none) */
    const char *generated_dir;           /* <pack_dir>\generated ("" = none) */
    const char *pack_version;            /* pack.json version ("" = none) */
    uint32_t data_schema;                /* pack.json data_schema (0 = none) */
    uint32_t flags;                      /* NF_HOST_* */
    const char *limited_reason;          /* NF_HOST_LIMITED: why, for the menu ("" otherwise) */
} NfHostInfo;

typedef struct NfModuleInfo {
    uint32_t size;
    uint16_t api_major, api_minor;       /* what the module was built against */
    const char *id, *version;            /* "my-nfl-eras" */
    uint32_t data_schema_min, data_schema_max;
    const uint32_t *guest_calls;         /* every guest VA the module calls */
    uint32_t guest_call_count;
    uint32_t va_range_base, va_range_size;   /* its served menu functions */
} NfModuleInfo;

/* on_archive_extend: the module lists the files that go into the archive's
 * extra pack G. add() returns 0 when the file was not taken (full, name too
 * long, or a name already listed). */
typedef struct NfArchiveSink {
    uint32_t size;
    void *ctx;
    int (*add)(struct NfArchiveSink *sink, const char *host_path, const char *archive_name);
} NfArchiveSink;

typedef void (*nf_guest_fn)(void);

/* Host -> module. Any member may be NULL. */
typedef struct NfModuleHooks {
    uint32_t size;
    int (*on_boot)(void);                                  /* main thread, before the guest runs; nonzero disables the module */
    void (*on_archive_extend)(NfArchiveSink *sink);        /* guest thread, at archive mount */
    void (*on_menus_ready)(void);                          /* main thread, the menus are built */
    nf_guest_fn (*lookup_va)(uint32_t va);                 /* a served menu function in va_range, or NULL */
    void (*on_fsm_pop)(uint32_t fsm, uint32_t top);        /* guest thread, the front-end FSM popped */
    void (*on_game_final)(uint32_t home_pts, uint32_t away_pts);   /* guest thread, sub_000A2910 entry */
    void (*on_match_exit)(void);                           /* guest thread, sub_000645D0 entry */
    uint32_t (*return_desc)(uint32_t edx);                 /* guest thread, registers NOT published: memory only */
    void (*on_shutdown)(void);
} NfModuleHooks;

/* Module -> host (built-in wrappers of the existing functions; docs/eras/MOD-PACK-DESIGN.md 6.3).
 * The guest machine itself (MEM32, the g_ registers, gcall of sub_X, the guest heap)
 * stays direct while the module is built in. */
typedef struct NfHostServices {
    uint32_t size;
    void (*log)(const char *fmt, ...);
    void (*interp_add_hook)(uint32_t va, uint32_t from, uint32_t (*fn)(void));
    /* paths: a file by its pack-relative name, in the pack roots (generated\ first, then the pack) */
    char *(*path)(const char *rel, char *out, size_t n);
    int (*exists)(const char *rel);
    int (*root_count)(void);
    char *(*root_path)(int i, const char *rel, char *out, size_t n);
    /* the user folder (league saves) */
    char *(*user_path)(const char *rel, char *out, size_t n);
    const char *(*user_dir)(void);
    void (*toast)(const char *header, const char *line1, const char *line2, unsigned ms);
    /* teams and stadiums (src/nfl2k5_teams.c, src/nfl2k5_teams.h) */
    int (*stage_check)(int side);
    int (*stage)(int side, int template_entry, const char *roster_csv, const char *city, const char *nickname, const char *abbr);
    const char *(*stage_message)(int code);
    void (*release)(int side);
    uint32_t (*staged_slot)(int side);
    uint32_t (*spare_slot)(int side);
    uint32_t (*resident)(int n);
    int (*identity_check)(int side, uint32_t match_team);
    uint32_t (*stadium_find)(const char *name, int *id_out);
    uint32_t (*stadium_borrow)(const char *name, int *id_out);    /* a stadium pack's row, from stadiums\*\stadium.json */
    void (*stadium_restore)(void);
    /* presentation and art */
    void (*pres_eras_begin)(int season, int week, const char *home_id, const char *away_id);
    void (*pres_eras_end)(void);
    void (*texpack_set_era)(const char *season_dir, const char *decade_dir);
    void (*texpack_set_team_art)(const char *season_dir_or_null);
    /* stats (sub_000CB240 / sub_000CB2D0, every guest register preserved) */
    int (*player_stat)(uint32_t player, uint32_t selector, uint32_t scope, double *out);
    int (*team_stat)(uint32_t team, uint32_t selector, uint32_t scope, double *out);
} NfHostServices;

/* The host's shims: the only names the rest of the game calls (gen patches,
 * main.c, recomp_manual.c, nfl2k5_team_files.c). */
void nfl2k5_hook_game_final(uint32_t home_pts, uint32_t away_pts);
void nfl2k5_hook_match_exit(void);
uint32_t nfl2k5_hook_return_desc(uint32_t edx);
void nfl2k5_hook_fsm_pop(uint32_t fsm, uint32_t top);
nf_guest_fn nfl2k5_hook_lookup_va(uint32_t va);
void nfl2k5_hook_archive_extend(NfArchiveSink *sink);
void nfl2k5_hook_menus_ready(void);
void nfl2k5_hook_shutdown(void);

/* The pack roots for host code (the stadium-pack rows, the office texture
 * context, the presentation identity): empty while no module is on. */
int nfl2k5_pack_root_count(void);
char *nfl2k5_pack_root_path(int i, const char *rel, char *out, size_t n);
char *nfl2k5_pack_path(const char *rel, char *out, size_t n);
int nfl2k5_pack_exists(const char *rel);

/* main.c: decide and start the feature modules (after the XBE is loaded and
 * hashed, before the guest runs). */
void nfl2k5_plugins_boot(const char *xbe_sha256, uint32_t base_kind, const char *xbe_pack_name, uint32_t flags);
const NfHostInfo *nfl2k5_plugin_host_info(void);

/* The built-in Eras module (src/nfl2k5_eras_module.c). */
int nfl2k5_eras_module_query(NfModuleInfo *out);
int nfl2k5_eras_module_init(const NfHostInfo *host, const NfHostServices *svc, NfModuleHooks *hooks_out);

#ifdef __cplusplus
}
#endif

#endif

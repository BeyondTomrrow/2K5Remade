/* My NFL Eras: the league (src/nfl2k5_eras.c, docs/eras/PHASE2-PLAN.md M3).
 *
 * The season package (data\eras\runtime\<season>\teams.csv, schedule.csv, read
 * only) and the league save under the user folder (eras\league1\: league.ini,
 * results.csv, box\, eras.log). Standings are always recomputed from
 * results.csv; nothing else holds a result. Game thread only. */
#ifndef NFL2K5_ERAS_H
#define NFL2K5_ERAS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_ERAS_SEASON       1980
#define NFL2K5_ERAS_FORMAT       1     /* league.ini format this build reads and writes */
#define NFL2K5_ERAS_MAX_TEAMS    32
#define NFL2K5_ERAS_MAX_GAMES    320
#define NFL2K5_ERAS_MAX_WEEKS    20

typedef struct {
    char id[24];                  /* stable franchise id, e.g. "oilers_titans" */
    char city[32], name[32], abbr[8];
    char conference[4];           /* "AFC" / "NFC" */
    char division[16];            /* "AFC East" ... */
    int template_entry;           /* disc historic team (M2 staging template) */
    char roster_file[64];         /* relative to the package folder, '/' separators */
    char stadium[48];             /* retail venue for home games (teams.csv stadium, M4); "" = the template's */
    int style;                    /* M9: disc uniform style for team +0x192 (teams.csv style); -1 = the template's */
} ErasTeam;

typedef struct {
    char id[48];                  /* 1980w01-dolphins-bills; playoffs (M14): 1980wc-/1980dv-/1980cc-/1980sb-<away>-<home> */
    int week;                     /* 1..16; playoffs 17 wild card, 18 divisional, 19 conference, 20 Super Bowl */
    char date[12];
    int away, home;               /* team indices */
    int round;                    /* M14: 0 regular season, 1 wild card, 2 divisional, 3 conference, 4 Super Bowl */
    int seed_away, seed_home;     /* M14: playoff seeds (0 for a regular-season game) */
    char site[48];                /* M14: a neutral venue (the Super Bowl's), "" = the home team's stadium */
    char label[32];               /* M14: "SUPER BOWL XV", "AFC WILD CARD", ... ("" in the regular season) */
} ErasGame;

typedef struct { int w, l, t, pf, pa; } ErasRecord;

/* results.csv status of one game */
enum { NFL2K5_ERAS_UNPLAYED = 0, NFL2K5_ERAS_PLAYED = 1 };

/* nfl2k5_eras_league_load results */
enum {
    NFL2K5_ERAS_OK = 0,
    NFL2K5_ERAS_NO_SAVE = -1,       /* no league.ini */
    NFL2K5_ERAS_BAD_VERSION = -2,   /* format missing or newer than this build: refused */
    NFL2K5_ERAS_BAD_PACKAGE = -3,   /* the season package named in league.ini did not load */
    NFL2K5_ERAS_IO = -4,
    NFL2K5_ERAS_BAD_TEAM = -5       /* user_team names no team of the package: refused */
};

/* Paths (docs/eras/MOD-PACK-DESIGN.md 4): an Eras file by its pack-relative name
 * ("league\1980\teams.csv", "offices\1980s\era.ini", ...), looked up in each root
 * in turn (the pack's generated\ first, then the pack). nfl2k5_eras_path gives the
 * first that exists, else the last root's name (for messages). */
char *nfl2k5_eras_path(const char *rel, char *out, size_t n);
int nfl2k5_eras_exists(const char *rel);
int nfl2k5_eras_root_count(void);
char *nfl2k5_eras_root_path(int i, const char *rel, char *out, size_t n);

/* The host's services (src/nfl2k5_plugin_api.h), set by nfl2k5_eras_module_init:
 * the module reaches teams, presentation, texture packs, stats, toasts and the
 * user folder only through them. */
struct NfHostServices;
extern const struct NfHostServices *g_eras_svc;
/* "" when the pack runs; otherwise why it cannot (an installed pack the game
 * cannot use: the hub shows this and offers nothing else). */
const char *nfl2k5_eras_limited(void);

/* The season package (loaded once per season; 0 on success). nfl2k5_eras_package_load loads the
 * league's season (NFL2K5_ERAS_SEASON when no league is loaded); _load_season a given one (M14). */
int nfl2k5_eras_package_load(void);
int nfl2k5_eras_package_load_season(int season);
int nfl2k5_eras_package_ok(void);
const char *nfl2k5_eras_package_dir(void);
/* M14: the league's season (NFL2K5_ERAS_SEASON when no league is loaded); every label, the
 * draft year and the roster paths follow it. */
int nfl2k5_eras_season(void);
/* M14: the regular season's game count (the package schedule); playoff games are appended after it
 * by src/nfl2k5_eras_playoffs.c (nfl2k5_eras_add_game, returns the index or -1). */
int nfl2k5_eras_regular_game_count(void);
int nfl2k5_eras_regular_complete(void);        /* every regular-season game played */
int nfl2k5_eras_season_complete(void);         /* the Super Bowl played */
int nfl2k5_eras_add_game(const ErasGame *g);
/* M14 rollover (src/nfl2k5_eras_rollover.c): the league moves to `season` (package league\<season>):
 * league.ini rewritten, the package loaded, a fresh results.csv; 0 on success. */
int nfl2k5_eras_league_set_season(int season);
/* A league file's path without creating folders (for reads and existence checks). */
char *nfl2k5_eras_league_peek(const char *rel, char *out, unsigned n);

int nfl2k5_eras_team_count(void);
const ErasTeam *nfl2k5_eras_team(int i);
int nfl2k5_eras_team_find(const char *id);
int nfl2k5_eras_game_count(void);
const ErasGame *nfl2k5_eras_game(int i);
int nfl2k5_eras_week_count(void);

/* The save: eras\league1\ under the user folder. */
int nfl2k5_eras_league_exists(void);     /* league.ini present (creates nothing) */
int nfl2k5_eras_league_create(void);     /* a fresh 1980 league; an older league1 is moved aside, never deleted */
int nfl2k5_eras_league_load(void);       /* NFL2K5_ERAS_* */
int nfl2k5_eras_league_loaded(void);
const char *nfl2k5_eras_league_message(int code);
int nfl2k5_eras_league_format_found(void);   /* the format key of the last league.ini read (0 = none) */
/* Full path of a file in the league folder (parents created). */
char *nfl2k5_eras_league_path(const char *rel, char *out, unsigned n);

int nfl2k5_eras_user_team(void);         /* team index, -1 = none (watch CPU against CPU) */
int nfl2k5_eras_set_user_team(int team); /* written to league.ini at once; refused (-1) after the first game */
int nfl2k5_eras_games_played(void);
/* The game launched and not yet cleaned up (league.ini "launched"; "" = none).
 * Kept in the save so a crash between launch and clean-up is noticed on the next load. */
const char *nfl2k5_eras_launched(void);
int nfl2k5_eras_set_launched(const char *game_id);   /* "" or NULL clears; written to league.ini at once */
int nfl2k5_eras_game_find(const char *game_id);
/* M3-office: the Office Look setting ("auto", "1970s" .. "2020s"), written at once. */
const char *nfl2k5_eras_office_look_saved(void);
int nfl2k5_eras_set_office_look_saved(const char *look);
/* M12: the Draft Show setting ("first", "mine", "off"), written at once; "first" while no league is loaded. */
const char *nfl2k5_eras_draft_show_saved(void);
int nfl2k5_eras_set_draft_show_saved(const char *mode);
/* M13: the history mode (league.ini history = historical | alternate; 0 Historical, 1 Alternate
 * History) and the sleeper count (sleepers = 0..10, default 5). Written at once. */
int nfl2k5_eras_history(void);
int nfl2k5_eras_set_history(int alternate);
int nfl2k5_eras_sleepers(void);
int nfl2k5_eras_set_sleepers(int n);
/* A seed fixed for this league (from its creation stamp): sleepers, CPU draft randomness. */
uint32_t nfl2k5_eras_league_seed(void);
/* M13: the season whose rosters are staged: the league season, or NFL2K5_ERAS_ROSTER_SEASON (a
 * test switch) for a later season's rosters written by the draft. The roster and depth seed of a
 * team: the package's for the package season, else the league's rosters\<season>\<id>.csv and
 * depth\<season>\<id>.csv. Returns 0 when the file exists. */
int nfl2k5_eras_roster_season(void);
int nfl2k5_eras_roster_path(int team, char *out, size_t n);
int nfl2k5_eras_depth_seed_path(int team, char *out, size_t n);
/* M14: the roster / depth seed the package ships for the league's season (never the league's own
 * rosters\<season>\ files); 0 when the file exists. */
int nfl2k5_eras_package_roster_path(int team, char *out, size_t n);
int nfl2k5_eras_package_depth_path(int team, char *out, size_t n);

/* Results, from results.csv (re-read by nfl2k5_eras_refresh). */
void nfl2k5_eras_refresh(void);
int nfl2k5_eras_result(int game, int *home_pts, int *away_pts);   /* NFL2K5_ERAS_PLAYED or UNPLAYED */
/* Next fixture: the user team's next unplayed game, otherwise the first unplayed
 * game in schedule order; -1 when the season is complete. */
int nfl2k5_eras_next_game(void);
void nfl2k5_eras_standings(ErasRecord out[NFL2K5_ERAS_MAX_TEAMS]);
/* The teams of one division in display order (wins, then fewer losses, then
 * point difference, then points scored, then name). Returns the count. */
int nfl2k5_eras_division_order(const char *division, int out[NFL2K5_ERAS_MAX_TEAMS]);
/* The divisions of a conference in package order; returns the count (max 4). */
int nfl2k5_eras_divisions(const char *conference, const char *out[4]);

/* M9 era looks: write the team's season uniform style (teams.csv style) to a
 * staged team record (+0x192). Returns the style written, or -1 (template's kept). */
int nfl2k5_eras_apply_style(uint32_t team_record, int team);
/* M9: the Eras team-art texture context (mods\eras	eams\<season>rt\) on
 * while an Eras game is launched or an Eras depth chart is open; off otherwise. */
void nfl2k5_eras_team_art_update(void);

/* eras.log in the league folder (and [ERAS] on stderr with NFL2K5_ERAS_LOG=1). */
void nfl2k5_eras_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif

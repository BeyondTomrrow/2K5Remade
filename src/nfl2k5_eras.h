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
    char id[48];                  /* 1980w01-dolphins-bills */
    int week;
    char date[12];
    int away, home;               /* team indices */
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

/* The season package (loaded once; 0 on success). */
int nfl2k5_eras_package_load(void);
int nfl2k5_eras_package_ok(void);
const char *nfl2k5_eras_package_dir(void);

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

/* My NFL Eras: the league state (docs/eras/PHASE2-PLAN.md M3).
 *
 * Package (read only, relative to the working directory like mods\teams):
 *   data\eras\runtime\1980\teams.csv     id,city,name,abbr,conference,division,template_entry,...
 *   data\eras\runtime\1980\schedule.csv  game_id,week,date,away,home[,historical]
 * Both are made by build/eras/make_runtime_package.py and league_schedule.py
 * (M0): plain ASCII, no quoted fields.
 *
 * Save, under xbox_user_path (the user folder, %LOCALAPPDATA%\NFL2K5-PC):
 *   eras\league1\league.ini    format, season, package, user team, created
 *   eras\league1\results.csv   game_id,home_pts,away_pts,final_period,end_state,status
 *                              (one line per game, appended by the result
 *                              capture at the final whistle, src/nfl2k5_eras_capture.c)
 *   eras\league1\box\<game_id>.json, eras\league1\eras.log
 *
 * Standings are recomputed from results.csv every time a screen asks, so no
 * second copy of a result exists to disagree with it. A league.ini whose
 * format is missing or newer than NFL2K5_ERAS_FORMAT is refused with a
 * message, never guessed at. Unknown keys are kept when league.ini is
 * rewritten. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"


/* The season package by its pack-relative name (docs/eras/MOD-PACK-DESIGN.md 4.1);
 * league.ini keeps this name. Leagues made before the pack layout name the
 * development folder (PACKAGE_LEGACY): the same package. */
#define PACKAGE_REL     "league\\1980"
#define PACKAGE_LEGACY  "data\\eras\\runtime\\1980"
#define LEAGUE_DIR   "eras\\league1"

/* ---- paths (docs/eras/MOD-PACK-DESIGN.md 4) ------------------------------
 *
 * Every Eras file is named by its path relative to the pack root
 * (league\1980\teams.csv, offices\1980s\era.ini, office\files\erasoffice.iff,
 * teams\1980\colts\team.json, stadiums\<folder>\stadium.json, presentations.ini ...)
 * and found by the host in the pack roots (src/nfl2k5_plugin_host.c). */
char *nfl2k5_eras_path(const char *rel, char *out, size_t n) { return g_eras_svc->path(rel, out, n); }
int nfl2k5_eras_exists(const char *rel) { return g_eras_svc->exists(rel); }
int nfl2k5_eras_root_count(void) { return g_eras_svc->root_count(); }
char *nfl2k5_eras_root_path(int i, const char *rel, char *out, size_t n) { return g_eras_svc->root_path(i, rel, out, n); }

static char s_package_dir[MAX_PATH];

static const char *package_dir(void)
{
    if (!s_package_dir[0]) nfl2k5_eras_path(PACKAGE_REL, s_package_dir, sizeof s_package_dir);
    return s_package_dir;
}

static ErasTeam s_teams[NFL2K5_ERAS_MAX_TEAMS];
static int s_team_n;
static ErasGame s_games[NFL2K5_ERAS_MAX_GAMES];
static int s_game_n, s_weeks;
static int s_package = -1;          /* -1 not tried, 0 loaded, 1 failed */

static struct {
    int loaded;
    int format_found;
    int season;
    char package[MAX_PATH];
    int user_team;
    char created[32];
    char launched[48];              /* M4: a launched game not cleaned up yet */
    char office_look[12];           /* M3-office: "auto" (or empty) / "1970s" .. "2020s" */
    char extra[2048];              /* unknown lines, written back unchanged */
} s_league;

static signed char s_played[NFL2K5_ERAS_MAX_GAMES];
static short s_home_pts[NFL2K5_ERAS_MAX_GAMES], s_away_pts[NFL2K5_ERAS_MAX_GAMES];

static int log_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("NFL2K5_ERAS_LOG"); on = e && *e && *e != '0'; }
    return on;
}

/* The same path without creating anything (xbox_user_path makes the parent
 * folders), for existence checks and reads: looking for a league must not
 * leave an empty eras\league1\ in the user folder. */
static char *league_path_peek(const char *rel, char *out, size_t n)
{
    const char *dir = g_eras_svc->user_dir();
    if (dir && *dir) snprintf(out, n, "%s\\%s\\%s", dir, LEAGUE_DIR, rel);
    else snprintf(out, n, "%s\\%s", LEAGUE_DIR, rel);
    return out;
}

char *nfl2k5_eras_league_path(const char *rel, char *out, unsigned n)
{
    char r[MAX_PATH], *p;
    snprintf(r, sizeof r, "%s\\%s", LEAGUE_DIR, rel);
    g_eras_svc->user_path(r, out, n);
    /* parents also in the portable layout (xbox_user_path makes them only in the user folder) */
    for (p = out + 1; *p; p++)
        if ((*p == '\\' || *p == '/') && p[-1] != ':') {
            char c = *p;
            *p = 0;
            CreateDirectoryA(out, NULL);
            *p = c;
        }
    return out;
}

void nfl2k5_eras_log(const char *fmt, ...)
{
    char line[512], path[MAX_PATH], stamp[32];
    time_t now = time(NULL);
    struct tm tmv;
    va_list ap;
    FILE *f;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (log_on()) fprintf(stderr, "[ERAS] %s\n", line);
    if (!s_league.loaded) return;   /* eras.log belongs to a league */
    localtime_s(&tmv, &now);
    strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &tmv);
    if (fopen_s(&f, nfl2k5_eras_league_path("eras.log", path, sizeof path), "ab") == 0 && f) {
        fprintf(f, "%s %s\r\n", stamp, line);
        fclose(f);
    }
}

/* ---- the package ------------------------------------------------------- */

/* Split a CSV line in place (no quoting in the package files). */
static int split(char *line, char **f, int max)
{
    int n = 0;
    char *p = line, *e = line + strlen(line);
    while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    while (n < max) {
        f[n++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = 0;
    }
    return n;
}

static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); }

int nfl2k5_eras_team_find(const char *id)
{
    int i;
    for (i = 0; i < s_team_n; i++)
        if (!strcmp(s_teams[i].id, id)) return i;
    return -1;
}

int nfl2k5_eras_package_load(void)
{
    char path[MAX_PATH], line[1024], *f[16];
    FILE *fp;
    int n, row, col_style = -1, col_stadium = 11;
    if (s_package >= 0) return s_package ? -1 : 0;
    s_package = 1;
    s_team_n = s_game_n = s_weeks = 0;
    snprintf(path, sizeof path, "%s\\teams.csv", package_dir());
    if (fopen_s(&fp, path, "rb") || !fp) { fprintf(stderr, "[ERAS] package: %s not found\n", path); return -1; }
    for (row = 0; fgets(line, sizeof line, fp); row++) {
        ErasTeam *t;
        n = split(line, f, 16);
        if (row == 0) {
            if (n < 9 || strcmp(f[0], "id") || strcmp(f[6], "template_entry")) { fclose(fp); fprintf(stderr, "[ERAS] package: teams.csv header\n"); return -1; }
            {   /* optional columns by name (M9 style); a package without them still loads */
                int k;
                for (k = 0; k < n; k++) {
                    if (!strcmp(f[k], "style")) col_style = k;
                    else if (!strcmp(f[k], "stadium")) col_stadium = k;
                }
            }
            continue;
        }
        if (n < 9 || !f[0][0]) continue;
        if (s_team_n >= NFL2K5_ERAS_MAX_TEAMS) break;
        t = &s_teams[s_team_n++];
        copy(t->id, sizeof t->id, f[0]); copy(t->city, sizeof t->city, f[1]); copy(t->name, sizeof t->name, f[2]);
        copy(t->abbr, sizeof t->abbr, f[3]); copy(t->conference, sizeof t->conference, f[4]);
        copy(t->division, sizeof t->division, f[5]); t->template_entry = atoi(f[6]);
        copy(t->roster_file, sizeof t->roster_file, f[8]);
        copy(t->stadium, sizeof t->stadium, n > col_stadium ? f[col_stadium] : "");
        t->style = col_style >= 0 && n > col_style && f[col_style][0] ? atoi(f[col_style]) : -1;
        if (t->style < -1 || t->style > 14) t->style = -1;
    }
    fclose(fp);
    snprintf(path, sizeof path, "%s\\schedule.csv", package_dir());
    if (fopen_s(&fp, path, "rb") || !fp) { fprintf(stderr, "[ERAS] package: %s not found\n", path); return -1; }
    for (row = 0; fgets(line, sizeof line, fp); row++) {
        ErasGame *g;
        n = split(line, f, 16);
        if (row == 0) {
            if (n < 5 || strcmp(f[0], "game_id") || strcmp(f[3], "away") || strcmp(f[4], "home")) { fclose(fp); fprintf(stderr, "[ERAS] package: schedule.csv header\n"); return -1; }
            continue;
        }
        if (n < 5 || !f[0][0]) continue;
        if (s_game_n >= NFL2K5_ERAS_MAX_GAMES) break;
        g = &s_games[s_game_n];
        copy(g->id, sizeof g->id, f[0]); g->week = atoi(f[1]); copy(g->date, sizeof g->date, f[2]);
        g->away = nfl2k5_eras_team_find(f[3]); g->home = nfl2k5_eras_team_find(f[4]);
        if (g->away < 0 || g->home < 0 || g->week < 1 || g->week > NFL2K5_ERAS_MAX_WEEKS) {
            fprintf(stderr, "[ERAS] package: schedule row %d (%s) names an unknown team or week\n", row, f[0]);
            fclose(fp); return -1;
        }
        if (g->week > s_weeks) s_weeks = g->week;
        s_game_n++;
    }
    fclose(fp);
    if (s_team_n < 2 || s_game_n < 1) { fprintf(stderr, "[ERAS] package: %d teams, %d games\n", s_team_n, s_game_n); return -1; }
    s_package = 0;
    fprintf(stderr, "[ERAS] package %s: %d teams, %d games, %d weeks\n", package_dir(), s_team_n, s_game_n, s_weeks);
    return 0;
}

int nfl2k5_eras_package_ok(void) { return nfl2k5_eras_package_load() == 0; }

/* ---- M9 era looks ------------------------------------------------------ */

extern ptrdiff_t g_xbox_mem_offset;
#define LOOK_MEM8(a) (*(volatile uint8_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

int nfl2k5_eras_apply_style(uint32_t team_record, int team)
{
    const ErasTeam *t = nfl2k5_eras_team(team);
    unsigned was;
    if (!t || !team_record || t->style < 0) return -1;
    was = LOOK_MEM8(team_record + 0x192u);
    LOOK_MEM8(team_record + 0x192u) = (uint8_t)t->style;
    nfl2k5_eras_log("look %s: uniform style %d written to %08X+0x192 (template style %u)", t->id, t->style, team_record, was);
    return t->style;
}

extern int nfl2k5_eras_launch_active(void);
extern int nfl2k5_eras_depth_editing(void);

void nfl2k5_eras_team_art_update(void)
{
    static int last = -1;
    char season[8];
    int on = nfl2k5_eras_launch_active() || nfl2k5_eras_depth_editing();
    snprintf(season, sizeof season, "%d", NFL2K5_ERAS_SEASON);
    g_eras_svc->texpack_set_team_art(on ? season : NULL);
    if (on != last) nfl2k5_eras_log("team art context %s", on ? season : "off");
    last = on;
}
const char *nfl2k5_eras_package_dir(void) { return package_dir(); }
int nfl2k5_eras_team_count(void) { return s_team_n; }
const ErasTeam *nfl2k5_eras_team(int i) { return i >= 0 && i < s_team_n ? &s_teams[i] : NULL; }
int nfl2k5_eras_game_count(void) { return s_game_n; }
const ErasGame *nfl2k5_eras_game(int i) { return i >= 0 && i < s_game_n ? &s_games[i] : NULL; }
int nfl2k5_eras_week_count(void) { return s_weeks; }

/* ---- league.ini -------------------------------------------------------- */

int nfl2k5_eras_league_exists(void)
{
    char path[MAX_PATH];
    return GetFileAttributesA(league_path_peek("league.ini", path, sizeof path)) != INVALID_FILE_ATTRIBUTES;
}

int nfl2k5_eras_league_loaded(void) { return s_league.loaded; }
int nfl2k5_eras_league_format_found(void) { return s_league.format_found; }

const char *nfl2k5_eras_league_message(int code)
{
    switch (code) {
    case NFL2K5_ERAS_OK: return "";
    case NFL2K5_ERAS_NO_SAVE: return "No saved league";
    case NFL2K5_ERAS_BAD_VERSION: return "Saved by a newer version";
    case NFL2K5_ERAS_BAD_PACKAGE: return "Season data missing";
    case NFL2K5_ERAS_BAD_TEAM: return "Unknown saved team";
    default: return "Save could not be read";
    }
}

/* league.ini through a .tmp file and a rename, so a crash never leaves half a file. */
static int league_write(void)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    const ErasTeam *t = nfl2k5_eras_team(s_league.user_team);
    FILE *f;
    nfl2k5_eras_league_path("league.ini", path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (fopen_s(&f, tmp, "wb") || !f) return -1;
    fprintf(f, "; My NFL Eras league save (written by the game; docs/eras/PHASE2-PLAN.md M3)\r\n");
    fprintf(f, "format = %d\r\n", NFL2K5_ERAS_FORMAT);
    fprintf(f, "season = %d\r\n", s_league.season);
    fprintf(f, "package = %s\r\n", s_league.package);
    fprintf(f, "user_team = %s\r\n", t ? t->id : "none");
    fprintf(f, "created = %s\r\n", s_league.created);
    fprintf(f, "rosters = unverified community roster\r\n");
    if (s_league.launched[0]) fprintf(f, "launched = %s\r\n", s_league.launched);
    if (s_league.office_look[0]) fprintf(f, "office_look = %s\r\n", s_league.office_look);
    if (s_league.extra[0]) fputs(s_league.extra, f);
    if (fclose(f)) return -1;
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) return -1;
    return 0;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

int nfl2k5_eras_league_load(void)
{
    char path[MAX_PATH], line[512], user[32] = "none";
    FILE *f;
    memset(&s_league, 0, sizeof s_league);
    s_league.user_team = -1;
    if (fopen_s(&f, league_path_peek("league.ini", path, sizeof path), "rb") || !f)
        return NFL2K5_ERAS_NO_SAVE;
    while (fgets(line, sizeof line, f)) {
        char *eq, *key, *val;
        size_t used = strlen(s_league.extra);
        if (line[0] == ';' || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        key = trim(line); val = trim(eq + 1);
        if (!strcmp(key, "format")) s_league.format_found = atoi(val);
        else if (!strcmp(key, "season")) s_league.season = atoi(val);
        else if (!strcmp(key, "package")) copy(s_league.package, sizeof s_league.package, val);
        else if (!strcmp(key, "user_team")) copy(user, sizeof user, val);
        else if (!strcmp(key, "created")) copy(s_league.created, sizeof s_league.created, val);
        else if (!strcmp(key, "rosters")) { /* label, rewritten */ }
        else if (!strcmp(key, "launched")) copy(s_league.launched, sizeof s_league.launched, val);
        else if (!strcmp(key, "office_look")) copy(s_league.office_look, sizeof s_league.office_look, val);
        else if (used + strlen(key) + strlen(val) + 6 < sizeof s_league.extra)
            snprintf(s_league.extra + used, sizeof s_league.extra - used, "%s = %s\r\n", key, val);
    }
    fclose(f);
    if (s_league.format_found < 1 || s_league.format_found > NFL2K5_ERAS_FORMAT) {
        fprintf(stderr, "[ERAS] league.ini format %d refused (this build reads format %d)\n",
                s_league.format_found, NFL2K5_ERAS_FORMAT);
        return NFL2K5_ERAS_BAD_VERSION;
    }
    if (s_league.season != NFL2K5_ERAS_SEASON || (_stricmp(s_league.package, PACKAGE_REL) && _stricmp(s_league.package, PACKAGE_LEGACY)) || nfl2k5_eras_package_load())
        return NFL2K5_ERAS_BAD_PACKAGE;
    s_league.user_team = strcmp(user, "none") ? nfl2k5_eras_team_find(user) : -1;
    if (strcmp(user, "none") && s_league.user_team < 0) {
        /* refused, not quietly turned into watch mode */
        fprintf(stderr, "[ERAS] league.ini user_team '%s' is not a team of %s; league refused\n", user, package_dir());
        return NFL2K5_ERAS_BAD_TEAM;
    }
    s_league.loaded = 1;
    nfl2k5_eras_refresh();
    {
        int next = nfl2k5_eras_next_game();
        nfl2k5_eras_log("league loaded: season %d, user team %s, %d of %d games played, next %s",
                        s_league.season, user, nfl2k5_eras_games_played(), s_game_n,
                        next >= 0 ? s_games[next].id : "(season complete)");
    }
    return NFL2K5_ERAS_OK;
}

int nfl2k5_eras_league_create(void)
{
    char dir[MAX_PATH], path[MAX_PATH], stamp[32];
    time_t now = time(NULL);
    struct tm tmv;
    FILE *f;
    if (nfl2k5_eras_package_load()) return NFL2K5_ERAS_BAD_PACKAGE;
    localtime_s(&tmv, &now);
    /* An existing league is moved aside (league1-replaced-<time>), never deleted. */
    league_path_peek("", dir, sizeof dir);
    dir[strlen(dir) - 1] = 0;                                /* the folder itself, no trailing '\' */
    league_path_peek("results.csv", path, sizeof path);
    if (nfl2k5_eras_league_exists() || GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        char aside[MAX_PATH];
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
        snprintf(aside, sizeof aside, "%s-replaced-%s", dir, stamp);
        if (!MoveFileA(dir, aside)) {
            fprintf(stderr, "[ERAS] new league: could not move %s aside (error %lu)\n", dir, (unsigned long)GetLastError());
            return NFL2K5_ERAS_IO;
        }
        fprintf(stderr, "[ERAS] new league: the previous league was moved to %s\n", aside);
    }
    memset(&s_league, 0, sizeof s_league);
    s_league.format_found = NFL2K5_ERAS_FORMAT;
    s_league.season = NFL2K5_ERAS_SEASON;
    s_league.user_team = -1;
    copy(s_league.package, sizeof s_league.package, PACKAGE_REL);
    strftime(s_league.created, sizeof s_league.created, "%Y-%m-%dT%H:%M:%S", &tmv);
    nfl2k5_eras_league_path("box\\", path, sizeof path);     /* creates box\ */
    CreateDirectoryA(path, NULL);
    if (fopen_s(&f, nfl2k5_eras_league_path("results.csv", path, sizeof path), "wb") || !f) return NFL2K5_ERAS_IO;
    fprintf(f, "game_id,home_pts,away_pts,final_period,end_state,status\r\n");
    fclose(f);
    if (league_write()) return NFL2K5_ERAS_IO;
    s_league.loaded = 1;
    nfl2k5_eras_refresh();
    nfl2k5_eras_log("new league: season %d, package %s, %d teams, %d games",
                    s_league.season, s_league.package, s_team_n, s_game_n);
    return NFL2K5_ERAS_OK;
}

int nfl2k5_eras_user_team(void) { return s_league.loaded ? s_league.user_team : -1; }

const char *nfl2k5_eras_launched(void) { return s_league.loaded ? s_league.launched : ""; }

int nfl2k5_eras_set_launched(const char *game_id)
{
    if (!s_league.loaded) return -1;
    copy(s_league.launched, sizeof s_league.launched, game_id);
    if (league_write()) { fprintf(stderr, "[ERAS] league.ini could not be written\n"); return -1; }
    return 0;
}

const char *nfl2k5_eras_office_look_saved(void) { return s_league.loaded && s_league.office_look[0] ? s_league.office_look : "auto"; }

int nfl2k5_eras_set_office_look_saved(const char *look)
{
    if (!s_league.loaded) return -1;
    copy(s_league.office_look, sizeof s_league.office_look, look && _stricmp(look, "auto") ? look : "");
    if (league_write()) { fprintf(stderr, "[ERAS] league.ini could not be written\n"); return -1; }
    return 0;
}

int nfl2k5_eras_game_find(const char *game_id)
{
    int i;
    for (i = 0; game_id && i < s_game_n; i++)
        if (!strcmp(s_games[i].id, game_id)) return i;
    return -1;
}

int nfl2k5_eras_set_user_team(int team)
{
    if (!s_league.loaded || nfl2k5_eras_games_played() > 0) return -1;
    if (team < -1 || team >= s_team_n) team = -1;
    if (team == s_league.user_team) return 0;
    s_league.user_team = team;
    if (league_write()) { fprintf(stderr, "[ERAS] league.ini could not be written\n"); return -1; }
    nfl2k5_eras_log("user team: %s", team >= 0 ? s_teams[team].id : "none");
    return 0;
}

/* ---- results and standings -------------------------------------------- */

void nfl2k5_eras_refresh(void)
{
    char path[MAX_PATH], line[512], *f[8];
    FILE *fp;
    int row;
    memset(s_played, 0, sizeof s_played);
    if (!s_league.loaded) return;
    if (fopen_s(&fp, league_path_peek("results.csv", path, sizeof path), "rb") || !fp) return;
    for (row = 0; fgets(line, sizeof line, fp); row++) {
        int n = split(line, f, 8), i;
        if (row == 0 && !strcmp(f[0], "game_id")) continue;
        /* status 'final' (written at the whistle); a game id appears once, the
         * first line wins, and an unknown id is ignored. */
        if (n < 6 || (strcmp(f[5], "final") && strcmp(f[5], "committed"))) continue;
        for (i = 0; i < s_game_n; i++)
            if (!strcmp(s_games[i].id, f[0])) break;
        if (i == s_game_n || s_played[i]) continue;
        s_played[i] = 1;
        s_home_pts[i] = (short)atoi(f[1]);
        s_away_pts[i] = (short)atoi(f[2]);
    }
    fclose(fp);
}

int nfl2k5_eras_result(int game, int *home_pts, int *away_pts)
{
    if (game < 0 || game >= s_game_n || !s_played[game]) return NFL2K5_ERAS_UNPLAYED;
    if (home_pts) *home_pts = s_home_pts[game];
    if (away_pts) *away_pts = s_away_pts[game];
    return NFL2K5_ERAS_PLAYED;
}

int nfl2k5_eras_games_played(void)
{
    int i, n = 0;
    for (i = 0; i < s_game_n; i++) n += s_played[i] != 0;
    return n;
}

int nfl2k5_eras_next_game(void)
{
    int i, u = nfl2k5_eras_user_team();
    if (u < 0) {
        /* test only (M8): NFL2K5_ERAS_TEST_NEXT=<team id> makes watch mode's next game that
         * team's first unplayed one, still CPU vs CPU (a user team would give a port a side) */
        const char *t = getenv("NFL2K5_ERAS_TEST_NEXT");
        int k, n = nfl2k5_eras_team_count();
        for (k = 0; t && *t && k < n; k++)
            if (!strcmp(nfl2k5_eras_team(k)->id, t)) { u = k; break; }
    }
    if (u >= 0)
        for (i = 0; i < s_game_n; i++)
            if (!s_played[i] && (s_games[i].home == u || s_games[i].away == u)) return i;
    for (i = 0; i < s_game_n; i++)
        if (!s_played[i]) return i;
    return -1;
}

void nfl2k5_eras_standings(ErasRecord out[NFL2K5_ERAS_MAX_TEAMS])
{
    int i;
    memset(out, 0, sizeof(ErasRecord) * NFL2K5_ERAS_MAX_TEAMS);
    for (i = 0; i < s_game_n; i++) {
        ErasRecord *h, *a;
        int hp = s_home_pts[i], ap = s_away_pts[i];
        if (!s_played[i]) continue;
        h = &out[s_games[i].home]; a = &out[s_games[i].away];
        h->pf += hp; h->pa += ap; a->pf += ap; a->pa += hp;
        if (hp > ap) { h->w++; a->l++; }
        else if (ap > hp) { a->w++; h->l++; }
        else { h->t++; a->t++; }
    }
}

static ErasRecord s_sort_rec[NFL2K5_ERAS_MAX_TEAMS];

static int standing_cmp(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    const ErasRecord *x = &s_sort_rec[a], *y = &s_sort_rec[b];
    if (x->w != y->w) return y->w - x->w;
    if (x->l != y->l) return x->l - y->l;
    if (x->pf - x->pa != y->pf - y->pa) return (y->pf - y->pa) - (x->pf - x->pa);
    if (x->pf != y->pf) return y->pf - x->pf;
    return strcmp(s_teams[a].name, s_teams[b].name);
}

int nfl2k5_eras_division_order(const char *division, int out[NFL2K5_ERAS_MAX_TEAMS])
{
    int i, n = 0;
    nfl2k5_eras_standings(s_sort_rec);
    for (i = 0; i < s_team_n; i++)
        if (!strcmp(s_teams[i].division, division)) out[n++] = i;
    qsort(out, (size_t)n, sizeof out[0], standing_cmp);
    return n;
}

int nfl2k5_eras_divisions(const char *conference, const char *out[4])
{
    int i, j, n = 0;
    for (i = 0; i < s_team_n && n < 4; i++) {
        if (strcmp(s_teams[i].conference, conference)) continue;
        for (j = 0; j < n; j++)
            if (!strcmp(out[j], s_teams[i].division)) break;
        if (j == n) out[n++] = s_teams[i].division;
    }
    return n;
}

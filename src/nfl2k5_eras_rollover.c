/* My NFL Eras: the season rollover (M14, docs/eras/results/M14-season-end.md).
 *
 * After the Super Bowl and the draft, "Start the <season+1> season" in the office:
 *
 *   1. the season's files move to <league>\seasons\<season>\ (results.csv, box\, playoffs.csv,
 *      seeds.csv, awards.csv, the league's depth-chart edits depth\<id>.csv, champion.txt);
 *   2. the next season's rosters:
 *      Historical      the package's real rosters (data\eras\runtime\<season+1>\rosters, built from
 *                      PFR's membership by tools/eras/roster_membership.py: every player where he
 *                      really was, the draft class's rookies where they really went). The league's
 *                      own rosters\<season+1>\ written by the draft are set aside as
 *                      rosters\<season+1>-draft\ so the package applies. The user's own trades /
 *                      edits would go on top here (none exist yet).
 *      Alternate History  the league's rosters\<season+1>\<id>.csv (the season's roster plus the
 *                      draft's rookies, M13) with the package's progression.csv applied
 *                      (tools/eras/make_progression.py): a player with no <season+1> PFR season
 *                      retires (dropped, logged); a player whose <season+1> community record
 *                      exists takes those 28 ratings (no invented numbers); every other player
 *                      keeps his ratings; years_pro + 1 for all. Rosters are kept within 42..53
 *                      (releases of the lowest-rated non-starters, logged). Starters come from the
 *                      package's <season+1> depth seed (the real games started) for the players
 *                      present; the rest follow at their position.
 *   3. league.ini: season = <season+1>, package = league\<season+1>; a fresh results.csv.
 *
 * Nothing in the package folders is written. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_draft.h"
#include "nfl2k5_eras_rollover.h"

#define NR 28
#define ROSTER_MAX 53
#define ROSTER_MIN 42

typedef struct {
    char line[640];
    char first[32], last[32];
    int pos, number, years;
    uint8_t r[NR];
    int ovr, keep, starter, rookie;
    int rank, side, has_depth;
} Row;

typedef struct { char first[32], last[32]; int pos; char status[12]; uint8_t r[NR]; int has_r; } Prog;

static void field(char **p, char *out, size_t n)
{
    size_t k = 0;
    while (**p && **p != ',' && **p != '\r' && **p != '\n') { if (k + 1 < n) out[k++] = **p; (*p)++; }
    out[k] = 0;
    if (**p == ',') (*p)++;
}

static int ifield(char **p) { char b[16]; field(p, b, sizeof b); return atoi(b); }
static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

int nfl2k5_eras_rollover_season(void) { return nfl2k5_eras_season() + 1; }

int nfl2k5_eras_rollover_ready(char *msg, size_t n)
{
    char rel[64], path[MAX_PATH];
    int ny = nfl2k5_eras_rollover_season();
    if (msg) msg[0] = 0;
    if (!nfl2k5_eras_league_loaded()) { if (msg) snprintf(msg, n, "No league"); return -1; }
    if (!nfl2k5_eras_season_complete()) { if (msg) snprintf(msg, n, "Play the Super Bowl first"); return -2; }
    snprintf(rel, sizeof rel, "league\\%d\\teams.csv", ny);
    if (!nfl2k5_eras_exists(rel)) { if (msg) snprintf(msg, n, "No %d season data", ny); return -3; }
    snprintf(rel, sizeof rel, "draft\\%d\\rosters_written", ny);
    nfl2k5_eras_league_peek(rel, path, sizeof path);
    if (!exists(path)) { if (msg) snprintf(msg, n, "%d Draft first", ny); return -4; }
    return 0;
}

/* ---- archive ----------------------------------------------------------------- */

static void move_into(const char *rel, const char *dir)
{
    char from[MAX_PATH], to[MAX_PATH];
    const char *base = strrchr(rel, '\\');
    nfl2k5_eras_league_peek(rel, from, sizeof from);
    if (!exists(from)) return;
    snprintf(to, sizeof to, "%s\\%s", dir, base ? base + 1 : rel);
    if (!MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING))
        nfl2k5_eras_log("rollover: could not move %s to %s (error %lu)", from, to, (unsigned long)GetLastError());
}

static void archive(int season)
{
    char dir[MAX_PATH], rel[64], pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    FILE *f;
    int champ = -1;
    snprintf(rel, sizeof rel, "seasons\\%d\\", season);
    nfl2k5_eras_league_path(rel, dir, sizeof dir);          /* creates seasons\<season>\ */
    CreateDirectoryA(dir, NULL);
    dir[strlen(dir) - 1] = 0;
    {
        extern int nfl2k5_eras_champion(void);
        champ = nfl2k5_eras_champion();
        snprintf(pat, sizeof pat, "%s\\champion.txt", dir);
        if (!fopen_s(&f, pat, "wb") && f) {
            fprintf(f, "%d champion: %s %s\n", season, champ >= 0 ? nfl2k5_eras_team(champ)->city : "?", champ >= 0 ? nfl2k5_eras_team(champ)->name : "");
            fclose(f);
        }
    }
    move_into("results.csv", dir);
    move_into("playoffs.csv", dir);
    move_into("seeds.csv", dir);
    move_into("awards.csv", dir);
    move_into("box", dir);
    /* the league's depth-chart edits of this season (depth\<id>.csv) */
    snprintf(pat, sizeof pat, "%s\\depth", dir);
    CreateDirectoryA(pat, NULL);
    nfl2k5_eras_league_peek("depth\\*.csv", pat, sizeof pat);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char sub[MAX_PATH], d2[MAX_PATH];
            snprintf(rel, sizeof rel, "depth\\%s", fd.cFileName);
            snprintf(d2, sizeof d2, "%s\\depth", dir);
            snprintf(sub, sizeof sub, "%s", rel);
            move_into(sub, d2);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    nfl2k5_eras_log("rollover: the %d season archived under %s (results, box scores, playoffs, seeds, awards, depth edits)", season, dir);
}

/* ---- Alternate History rosters ----------------------------------------------- */

static Prog *s_prog;
static int s_nprog;

static int read_progression(int ny)
{
    char path[MAX_PATH], line[1024];
    FILE *f;
    s_nprog = 0;
    snprintf(path, sizeof path, "%s\\progression.csv", nfl2k5_eras_package_dir());
    if (fopen_s(&f, path, "rb") || !f) return -1;
    if (!s_prog) s_prog = (Prog *)calloc(3000, sizeof *s_prog);
    if (!s_prog) { fclose(f); return -1; }
    fgets(line, sizeof line, f);
    while (s_nprog < 3000 && fgets(line, sizeof line, f)) {
        Prog *p = &s_prog[s_nprog];
        char *c = line, b[32];
        int k;
        memset(p, 0, sizeof *p);
        field(&c, p->first, sizeof p->first); field(&c, p->last, sizeof p->last);
        p->pos = ifield(&c);
        field(&c, b, sizeof b);                 /* team_<season> */
        field(&c, b, sizeof b);                 /* pfr_id */
        field(&c, p->status, sizeof p->status);
        field(&c, b, sizeof b);                 /* team_<season+1> */
        for (k = 0; k < NR; k++) { field(&c, b, sizeof b); if (b[0]) { p->r[k] = (uint8_t)atoi(b); p->has_r = 1; } }
        if (!p->first[0] && !p->last[0]) continue;
        s_nprog++;
    }
    fclose(f);
    (void)ny;
    return s_nprog;
}

static const Prog *prog_find(const char *first, const char *last, int pos)
{
    int i;
    for (i = 0; i < s_nprog; i++)
        if (s_prog[i].pos == pos && !strcmp(s_prog[i].first, first) && !strcmp(s_prog[i].last, last)) return &s_prog[i];
    return NULL;
}

static int parse_row(Row *r)
{
    char *c = r->line, a[32];
    int k;
    field(&c, r->first, sizeof r->first); field(&c, r->last, sizeof r->last);
    r->pos = ifield(&c); r->number = ifield(&c);
    field(&c, a, sizeof a); field(&c, a, sizeof a);
    r->years = ifield(&c);
    for (k = 0; k < 3; k++) field(&c, a, sizeof a);
    for (k = 0; k < NR; k++) r->r[k] = (uint8_t)ifield(&c);
    r->ovr = nfl2k5_eras_draft_overall(r->pos, r->r);
    return r->pos >= 0 && r->pos <= 16;
}

/* the row rewritten with years + 1 and (maybe) new ratings; the other fields verbatim */
static void rewrite_row(Row *r)
{
    char *c = r->line, f[14][40], photo[16], out[640];
    int k;
    size_t o;
    for (k = 0; k < 10; k++) field(&c, f[k], sizeof f[k]);
    for (k = 0; k < NR; k++) field(&c, photo, sizeof photo);
    field(&c, photo, sizeof photo);
    o = (size_t)snprintf(out, sizeof out, "%s,%s,%s,%s,%s,%s,%d,%s,%s,%s", f[0], f[1], f[2], f[3], f[4], f[5], r->years, f[7], f[8], f[9]);
    for (k = 0; k < NR; k++) o += (size_t)snprintf(out + o, sizeof out - o, ",%d", r->r[k]);
    snprintf(out + o, sizeof out - o, ",%s", photo[0] ? photo : "39999");
    snprintf(r->line, sizeof r->line, "%s", out);
}

static const int k_stage_min[17] = { 1, 1, 0, 2, 2, 1, 0, 1, 0, 1, 2, 1, 1, 2, 2, 1, 2 };

static int alternate_team(int team, int season, int ny, FILE *rep)
{
    static Row rows[ROSTER_MAX + 60];
    const ErasTeam *t = nfl2k5_eras_team(team);
    char path[MAX_PATH], rel[96], header[640], line[256], flags[MAX_PATH];
    FILE *f;
    int n = 0, k, i, retired = 0, rated = 0, kept, released = 0, pos_n[17], nd = 0, next_rank[17];
    struct { char first[32], last[32]; int pos, rank, side; } *dep = NULL;
    if (!t) return -1;
    snprintf(rel, sizeof rel, "rosters\\%d\\%s.csv", ny, t->id);
    nfl2k5_eras_league_peek(rel, path, sizeof path);
    if (!exists(path)) { nfl2k5_eras_log("rollover %s: no %s (the draft did not write it); the package roster will be used", t->id, rel); return 0; }
    if (fopen_s(&f, path, "rb") || !f) return -1;
    if (!fgets(header, sizeof header, f)) { fclose(f); return -1; }
    while (n < ROSTER_MAX + 60 && fgets(rows[n].line, sizeof rows[n].line, f)) {
        rows[n].line[strcspn(rows[n].line, "\r\n")] = 0;
        if (!rows[n].line[0]) continue;
        memset(&rows[n].keep, 0, sizeof rows[n] - offsetof(Row, keep));
        rows[n].keep = 1;
        if (parse_row(&rows[n])) n++;
    }
    fclose(f);
    /* rookies (the flags file names them) keep their class ratings */
    snprintf(rel, sizeof rel, "rosters\\%d\\%s.flags.txt", ny, t->id);
    nfl2k5_eras_league_peek(rel, flags, sizeof flags);
    if (!fopen_s(&f, flags, "rb") && f) {
        while (fgets(line, sizeof line, f)) {
            int row;
            if (sscanf(line, "row %d:", &row) == 1 && strstr(line, "ROOKIE") && row >= 0 && row < n) rows[row].rookie = 1;
        }
        fclose(f);
    }
    /* progression: retirements and next-season ratings */
    for (k = 0; k < n; k++) {
        const Prog *p = rows[k].rookie ? NULL : prog_find(rows[k].first, rows[k].last, rows[k].pos);
        rows[k].years++;
        if (!p) continue;
        if (!strcmp(p->status, "retired")) {
            rows[k].keep = 0; retired++;
            nfl2k5_eras_log("rollover %s: %s %s (%d) retired: no %d season on Pro-Football-Reference", t->id, rows[k].first, rows[k].last, rows[k].pos, ny);
            if (rep) fprintf(rep, "%s,%s,%s,%d,retired\n", t->id, rows[k].first, rows[k].last, rows[k].pos);
        } else if (!strcmp(p->status, "rated") && p->has_r) {
            memcpy(rows[k].r, p->r, NR); rated++;
            rows[k].ovr = nfl2k5_eras_draft_overall(rows[k].pos, rows[k].r);
            if (rep) fprintf(rep, "%s,%s,%s,%d,rated %d\n", t->id, rows[k].first, rows[k].last, rows[k].pos, ny);
        }
    }
    /* starters: the package's <ny> depth seed (real games started) for the players present */
    dep = calloc(80, sizeof *dep);
    if (dep && nfl2k5_eras_package_depth_path(team, path, sizeof path) == 0 && !fopen_s(&f, path, "rb") && f) {
        while (nd < 80 && fgets(line, sizeof line, f)) {
            char *c = line, a[16];
            if (line[0] == '#' || !strncmp(line, "row,", 4) || !strncmp(line, "special,", 8)) continue;
            ifield(&c);
            field(&c, dep[nd].first, sizeof dep[nd].first); field(&c, dep[nd].last, sizeof dep[nd].last);
            dep[nd].pos = ifield(&c); ifield(&c); dep[nd].rank = ifield(&c) & 7; field(&c, a, sizeof a); dep[nd].side = atoi(a) & 7;
            nd++;
        }
        fclose(f);
    }
    for (k = 0; k < n; k++)
        for (i = 0; i < nd; i++)
            if (rows[k].pos == dep[i].pos && !strcmp(rows[k].first, dep[i].first) && !strcmp(rows[k].last, dep[i].last)) {
                rows[k].has_depth = 1; rows[k].rank = dep[i].rank; rows[k].side = dep[i].side;
                rows[k].starter = dep[i].rank == 0 || (dep[i].side == 0 && (dep[i].pos == 4 || dep[i].pos == 3 || dep[i].pos >= 10));
                break;
            }
    /* within 53: the lowest-rated non-starters go (never below the staging minimums) */
    memset(pos_n, 0, sizeof pos_n);
    kept = 0;
    for (k = 0; k < n; k++) if (rows[k].keep) { pos_n[rows[k].pos]++; kept++; }
    while (kept > ROSTER_MAX) {
        int worst = -1;
        for (k = 0; k < n; k++) {
            if (!rows[k].keep || rows[k].starter || pos_n[rows[k].pos] <= k_stage_min[rows[k].pos]) continue;
            if (worst < 0 || rows[k].ovr < rows[worst].ovr) worst = k;
        }
        if (worst < 0) break;
        rows[worst].keep = 0; pos_n[rows[worst].pos]--; kept--; released++;
        nfl2k5_eras_log("rollover %s: released %s %s (%d, overall %d) to stay at %d", t->id, rows[worst].first, rows[worst].last, rows[worst].pos, rows[worst].ovr, ROSTER_MAX);
        if (rep) fprintf(rep, "%s,%s,%s,%d,released (roster limit)\n", t->id, rows[worst].first, rows[worst].last, rows[worst].pos);
    }
    if (kept < ROSTER_MIN) nfl2k5_eras_log("rollover %s: WARNING %d players after the retirements, below the game's minimum %d", t->id, kept, ROSTER_MIN);
    /* write the roster and the depth file */
    snprintf(rel, sizeof rel, "rosters\\%d\\%s.csv", ny, t->id);
    nfl2k5_eras_league_path(rel, path, sizeof path);
    if (fopen_s(&f, path, "wb") || !f) { free(dep); return -1; }
    fputs(header, f);
    if (header[strlen(header) - 1] != '\n') fputc('\n', f);
    for (k = 0, i = 0; k < n; k++) if (rows[k].keep) { rewrite_row(&rows[k]); fprintf(f, "%s\n", rows[k].line); rows[k].number = i++; }
    fclose(f);
    if (!fopen_s(&f, flags, "ab") && f) {
        fprintf(f, "rollover to %d (Alternate History): %d retired (no %d PFR season), %d took their %d community ratings, %d released at the roster limit, years_pro + 1 for all; %d players\n",
                ny, retired, ny, rated, ny, released, kept);
        fclose(f);
    }
    snprintf(rel, sizeof rel, "depth\\%d\\%s.csv", ny, t->id);
    nfl2k5_eras_league_path(rel, path, sizeof path);
    if (!fopen_s(&f, path, "wb") && f) {
        for (k = 0; k < 17; k++) next_rank[k] = 0;
        for (k = 0; k < n; k++) if (rows[k].keep && rows[k].has_depth && rows[k].rank + 1 > next_rank[rows[k].pos]) next_rank[rows[k].pos] = rows[k].rank + 1;
        fprintf(f, "# My NFL Eras depth chart of %s %s for %d: the %d package's starters (real games started) where present, the rest behind them\n", t->city, t->name, ny, ny);
        fprintf(f, "row,first,last,pos,number,rank,side\n");
        for (k = 0; k < n; k++) {
            int rank, side;
            char *c = rows[k].line, a[32], b[32];
            if (!rows[k].keep) continue;
            field(&c, a, sizeof a); field(&c, a, sizeof a); field(&c, a, sizeof a); field(&c, b, sizeof b);   /* number */
            if (rows[k].has_depth) { rank = rows[k].rank; side = rows[k].side; }
            else { rank = next_rank[rows[k].pos] > 7 ? 7 : next_rank[rows[k].pos]++; side = rank == 0 ? 2 : rank == 1 ? 0 : rank == 2 ? 1 : rank; }
            fprintf(f, "%d,%s,%s,%d,%s,%d,%d\n", rows[k].number, rows[k].first, rows[k].last, rows[k].pos, b, rank, side);
        }
        fclose(f);
    }
    free(dep);
    nfl2k5_eras_log("rollover %s: %d players for %d (%d retired, %d re-rated from the %d community record, %d released)", t->id, kept, ny, retired, rated, ny, released);
    (void)season;
    return kept;
}

static int rename_aside(const char *rel_dir, int ny)
{
    char from[MAX_PATH], to[MAX_PATH], rel[64];
    snprintf(rel, sizeof rel, "%s\\%d", rel_dir, ny);
    nfl2k5_eras_league_peek(rel, from, sizeof from);
    if (!exists(from)) return 0;
    snprintf(to, sizeof to, "%s-draft", from);
    if (exists(to)) { char t2[MAX_PATH]; snprintf(t2, sizeof t2, "%s-%lu", to, (unsigned long)GetTickCount()); MoveFileExA(to, t2, 0); }
    if (!MoveFileExA(from, to, 0)) { nfl2k5_eras_log("rollover: could not set %s aside (error %lu)", from, (unsigned long)GetLastError()); return -1; }
    nfl2k5_eras_log("rollover (Historical): the draft's %s set aside as %s; the %d package's real rosters apply", rel, to, ny);
    return 1;
}

int nfl2k5_eras_rollover(char *msg, size_t n)
{
    int season = nfl2k5_eras_season(), ny = season + 1, t, alt = nfl2k5_eras_history(), rc;
    char path[MAX_PATH], rel[64];
    FILE *rep = NULL;
    if ((rc = nfl2k5_eras_rollover_ready(msg, n)) != 0) return rc;
    nfl2k5_eras_log("rollover: %d -> %d (%s)", season, ny, alt ? "Alternate History" : "Historical");
    archive(season);
    if (!alt) {
        rename_aside("rosters", ny);
        rename_aside("depth", ny);
    } else {
        /* the next package's progression table is read before the package switch (same folder layout) */
        char save_dir[MAX_PATH];
        snprintf(save_dir, sizeof save_dir, "%s", nfl2k5_eras_package_dir());
        if (nfl2k5_eras_package_load_season(ny)) { if (msg) snprintf(msg, n, "No %d season data", ny); return -3; }
        if (read_progression(ny) < 0) nfl2k5_eras_log("rollover: %s\\progression.csv missing: nobody retires and ratings stay (python -I tools\\eras\\make_progression.py)", nfl2k5_eras_package_dir());
        snprintf(rel, sizeof rel, "rosters\\%d\\rollover.csv", ny);
        nfl2k5_eras_league_path(rel, path, sizeof path);
        if (!fopen_s(&rep, path, "wb") && rep) fprintf(rep, "team,first,last,pos,what\n");
        for (t = 0; t < nfl2k5_eras_team_count(); t++) alternate_team(t, season, ny, rep);
        if (rep) fclose(rep);
        (void)save_dir;
    }
    if (nfl2k5_eras_league_set_season(ny)) { if (msg) snprintf(msg, n, "Could not switch to %d", ny); return -5; }
    if (msg) snprintf(msg, n, "%d season started", ny);
    return 0;
}

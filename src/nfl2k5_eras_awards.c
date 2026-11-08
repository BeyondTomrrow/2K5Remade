/* My NFL Eras: season awards and league leaders from the league's own box scores (M14,
 * docs/eras/results/M14-season-end.md).
 *
 * Source: box\<game_id>.json of every played regular-season game (src/nfl2k5_eras_capture.c, one
 * line per player: first, last, position, "box": {key: value, ...}). A game whose box score is
 * missing (a result seeded by the test tool, or a failed write) contributes nothing: the awards
 * then rest on the games that have one, and the screen says how many.
 *
 * Rule (named on the screen and in the doc; the game has no MVP vote):
 *   player points = pass yds / 25 + (rush yds + rec yds) / 10 + 6 x (pass TD + rush TD + rec TD)
 *                   - 2 x interceptions thrown; K: 3 x FGM + XPM
 *   defensive points = tackles + 4 x sacks + 6 x interceptions + 3 x forced fumbles
 *   Most Valuable Player      the most player points in the league
 *   Offensive Player of the Year   the most player points among the others
 *   Defensive Player of the Year   the most defensive points
 *   Coach of the Year         the head coach of the team with the best regular-season record
 *                             (percentage, then point difference), from the package's coaches.csv
 * Ties go to the player with more games, then the earlier name. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_awards.h"

static ErasPlayerSeason s_p[NFL2K5_AW_MAX_PLAYERS];
static int s_np, s_games_with_box, s_games_played, s_built_for = -1, s_built_season;
static ErasAward s_aw[4];
static int s_naw;
static char s_stat[4][64];
static const ErasPlayerSeason *s_lead[NFL2K5_AW_MAX_LEADERS];

static const char *const k_cat[8] = { "PASSING YARDS", "RUSHING YARDS", "RECEIVING YARDS", "TOUCHDOWNS", "TACKLES", "SACKS", "INTERCEPTIONS", "FIELD GOALS" };

const char *nfl2k5_eras_awards_rule(void)
{
    return "MVP = most points: pass yds/25 + rush+rec yds/10 + 6/TD - 2/INT (K: 3/FG + XP); DPOY = tackles + 4/sack + 6/INT + 3/FF";
}

static ErasPlayerSeason *find(const char *first, const char *last, int team, int pos)
{
    int i;
    for (i = 0; i < s_np; i++)
        if (s_p[i].team == team && s_p[i].pos == pos && !strcmp(s_p[i].first, first) && !strcmp(s_p[i].last, last)) return &s_p[i];
    if (s_np >= NFL2K5_AW_MAX_PLAYERS) return NULL;
    memset(&s_p[s_np], 0, sizeof s_p[s_np]);
    snprintf(s_p[s_np].first, sizeof s_p[s_np].first, "%s", first);
    snprintf(s_p[s_np].last, sizeof s_p[s_np].last, "%s", last);
    s_p[s_np].team = team; s_p[s_np].pos = pos;
    return &s_p[s_np++];
}

/* "key": value inside the player's "box": {...} */
static int box_int(const char *box, const char *key)
{
    char k[48];
    const char *p;
    snprintf(k, sizeof k, "\"%s\": ", key);
    p = strstr(box, k);
    return p ? atoi(p + strlen(k)) : 0;
}

static int str_field(const char *line, const char *key, char *out, size_t n)
{
    char k[32];
    const char *p, *e;
    size_t len;
    snprintf(k, sizeof k, "\"%s\": \"", key);
    p = strstr(line, k);
    if (!p) { out[0] = 0; return 0; }
    p += strlen(k);
    e = strchr(p, '"');
    if (!e) { out[0] = 0; return 0; }
    len = (size_t)(e - p);
    if (len >= n) len = n - 1;
    memcpy(out, p, len);
    out[len] = 0;
    return 1;
}

static int read_box(int game)
{
    const ErasGame *e = nfl2k5_eras_game(game);
    char rel[96], path[MAX_PATH], line[4096];
    FILE *f;
    int side = -1, any = 0;
    snprintf(rel, sizeof rel, "box\\%s.json", e->id);
    nfl2k5_eras_league_peek(rel, path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return 0;
    while (fgets(line, sizeof line, f)) {
        const char *box;
        char first[24], last[24], pos[8];
        ErasPlayerSeason *p;
        if (!strncmp(line, "  \"home\": {", 11)) { side = 0; continue; }
        if (!strncmp(line, "  \"away\": {", 11)) { side = 1; continue; }
        if (side < 0 || !strstr(line, "\"first\": ")) continue;
        box = strstr(line, "\"box\": {");
        if (!box) continue;
        str_field(line, "first", first, sizeof first);
        str_field(line, "last", last, sizeof last);
        { const char *q = strstr(line, "\"position\": "); snprintf(pos, sizeof pos, "%d", q ? atoi(q + 12) : 0); }
        p = find(first, last, side ? e->away : e->home, atoi(pos));
        if (!p) continue;
        any = 1;
        p->games++;
        p->pass_yds += box_int(box, "pass_yds"); p->pass_td += box_int(box, "pass_td"); p->pass_int += box_int(box, "pass_int");
        p->pass_comp += box_int(box, "pass_comp"); p->pass_att += box_int(box, "pass_att");
        p->rush_yds += box_int(box, "rush_yds"); p->rush_td += box_int(box, "rush_td"); p->rush_att += box_int(box, "rush_att");
        p->rec += box_int(box, "rec"); p->rec_yds += box_int(box, "rec_yds"); p->rec_td += box_int(box, "rec_td");
        p->tackles += box_int(box, "tackles"); p->sacks += box_int(box, "sacks"); p->def_int += box_int(box, "def_int");
        p->forced_fum += box_int(box, "forced_fum");
        p->fgm += box_int(box, "fgm"); p->fga += box_int(box, "fga"); p->xpm += box_int(box, "xpm");
    }
    fclose(f);
    return any;
}

static int cmp_points(const void *a, const void *b)
{
    const ErasPlayerSeason *x = *(const ErasPlayerSeason *const *)a, *y = *(const ErasPlayerSeason *const *)b;
    if (x->points != y->points) return y->points - x->points;
    if (x->games != y->games) return y->games - x->games;
    return strcmp(x->last, y->last);
}

static void coach_name(int team, char *out, size_t n)
{
    char path[MAX_PATH], line[256];
    FILE *f;
    const ErasTeam *t = nfl2k5_eras_team(team);
    out[0] = 0;
    if (!t) return;
    snprintf(path, sizeof path, "%s\\coaches.csv", nfl2k5_eras_package_dir());
    if (fopen_s(&f, path, "rb") || !f) return;
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, ','), *d;
        if (!c) continue;
        *c = 0;
        if (strcmp(line, t->id)) continue;
        d = strchr(c + 1, ',');
        if (d) { *d = 0; snprintf(out, n, "%s %s", c + 1, d + 1); d = strchr(out, ','); if (d) *d = 0; }
        break;
    }
    fclose(f);
}

int nfl2k5_eras_awards_build(void)
{
    int g, i, played = 0, best_c = -1;
    ErasRecord rec[NFL2K5_ERAS_MAX_TEAMS];
    const ErasPlayerSeason *mvp = NULL, *opoy = NULL, *dpoy = NULL;
    if (!nfl2k5_eras_league_loaded()) return 0;
    nfl2k5_eras_refresh();
    played = 0;
    for (g = 0; g < nfl2k5_eras_regular_game_count(); g++) { int hp, ap; played += nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED; }
    if (s_built_for == played && s_built_season == nfl2k5_eras_season()) return s_np;
    s_np = 0; s_games_with_box = 0; s_games_played = played;
    for (g = 0; g < nfl2k5_eras_regular_game_count(); g++) {
        int hp, ap;
        if (nfl2k5_eras_result(g, &hp, &ap) != NFL2K5_ERAS_PLAYED) continue;
        s_games_with_box += read_box(g);
    }
    for (i = 0; i < s_np; i++) {
        ErasPlayerSeason *p = &s_p[i];
        p->points = p->pass_yds / 25 + (p->rush_yds + p->rec_yds) / 10 + 6 * (p->pass_td + p->rush_td + p->rec_td) - 2 * p->pass_int;
        if (p->pos == 1) p->points += 3 * p->fgm + p->xpm;
        p->dpoints = p->tackles + 4 * p->sacks + 6 * p->def_int + 3 * p->forced_fum;
    }
    for (i = 0; i < s_np; i++) {
        const ErasPlayerSeason *p = &s_p[i];
        if (!mvp || p->points > mvp->points || (p->points == mvp->points && p->games > mvp->games)) mvp = p;
        if (!dpoy || p->dpoints > dpoy->dpoints || (p->dpoints == dpoy->dpoints && p->games > dpoy->games)) dpoy = p;
    }
    for (i = 0; i < s_np; i++) {
        const ErasPlayerSeason *p = &s_p[i];
        if (p == mvp) continue;
        if (!opoy || p->points > opoy->points || (p->points == opoy->points && p->games > opoy->games)) opoy = p;
    }
    nfl2k5_eras_standings(rec);
    for (i = 0; i < nfl2k5_eras_team_count(); i++) {
        const ErasRecord *r = &rec[i], *b = best_c >= 0 ? &rec[best_c] : NULL;
        long pr = r->w + r->l + r->t ? (r->w * 2 + r->t) * 10000L / (2L * (r->w + r->l + r->t)) : -1;
        long pb = b ? (b->w + b->l + b->t ? (b->w * 2 + b->t) * 10000L / (2L * (b->w + b->l + b->t)) : -1) : -2;
        if (pr > pb || (pr == pb && b && r->pf - r->pa > b->pf - b->pa)) best_c = i;
    }
    s_naw = 0;
    s_aw[s_naw].award = "Most Valuable Player"; s_aw[s_naw].p = mvp; s_aw[s_naw].coach_team = -1;
    if (mvp) snprintf(s_stat[s_naw], 64, "%d pts: %d pass, %d rush, %d rec yds, %d TD", mvp->points, mvp->pass_yds, mvp->rush_yds, mvp->rec_yds, mvp->pass_td + mvp->rush_td + mvp->rec_td);
    else snprintf(s_stat[s_naw], 64, "no box scores yet");
    s_aw[s_naw].stat[0] = 0; snprintf(s_aw[s_naw].stat, 64, "%s", s_stat[s_naw]); s_naw++;
    s_aw[s_naw].award = "Offensive Player of the Year"; s_aw[s_naw].p = opoy; s_aw[s_naw].coach_team = -1;
    if (opoy) snprintf(s_aw[s_naw].stat, 64, "%d pts: %d pass, %d rush, %d rec yds, %d TD", opoy->points, opoy->pass_yds, opoy->rush_yds, opoy->rec_yds, opoy->pass_td + opoy->rush_td + opoy->rec_td);
    else snprintf(s_aw[s_naw].stat, 64, "no box scores yet");
    s_naw++;
    s_aw[s_naw].award = "Defensive Player of the Year"; s_aw[s_naw].p = dpoy; s_aw[s_naw].coach_team = -1;
    if (dpoy) snprintf(s_aw[s_naw].stat, 64, "%d tackles, %d sacks, %d INT, %d FF", dpoy->tackles, dpoy->sacks, dpoy->def_int, dpoy->forced_fum);
    else snprintf(s_aw[s_naw].stat, 64, "no box scores yet");
    s_naw++;
    s_aw[s_naw].award = "Coach of the Year"; s_aw[s_naw].p = NULL; s_aw[s_naw].coach_team = best_c;
    if (best_c >= 0) snprintf(s_aw[s_naw].stat, 64, "%d-%d-%d, %+d points", rec[best_c].w, rec[best_c].l, rec[best_c].t, rec[best_c].pf - rec[best_c].pa);
    else s_aw[s_naw].stat[0] = 0;
    s_naw++;
    s_built_for = played;
    s_built_season = nfl2k5_eras_season();
    nfl2k5_eras_log("awards: %d players summed from %d box scores of %d played regular-season games; MVP %s %s (%d pts), DPOY %s %s, Coach %s",
                    s_np, s_games_with_box, played, mvp ? mvp->first : "-", mvp ? mvp->last : "", mvp ? mvp->points : 0,
                    dpoy ? dpoy->first : "-", dpoy ? dpoy->last : "", best_c >= 0 ? nfl2k5_eras_team(best_c)->name : "-");
    return s_np;
}

int nfl2k5_eras_awards_count(void) { return s_naw; }
const ErasAward *nfl2k5_eras_award(int i) { return i >= 0 && i < s_naw ? &s_aw[i] : NULL; }
int nfl2k5_eras_leader_categories(void) { return 8; }
const char *nfl2k5_eras_leader_category(int c) { return c >= 0 && c < 8 ? k_cat[c] : ""; }

int nfl2k5_eras_leader_value(int c, const ErasPlayerSeason *p)
{
    switch (c) {
    case 0: return p->pass_yds; case 1: return p->rush_yds; case 2: return p->rec_yds; case 3: return p->rush_td + p->rec_td;
    case 4: return p->tackles; case 5: return p->sacks; case 6: return p->def_int; case 7: return p->fgm;
    }
    return 0;
}

static int s_cat;
static int cmp_cat(const void *a, const void *b)
{
    const ErasPlayerSeason *x = *(const ErasPlayerSeason *const *)a, *y = *(const ErasPlayerSeason *const *)b;
    int vx = nfl2k5_eras_leader_value(s_cat, x), vy = nfl2k5_eras_leader_value(s_cat, y);
    if (vx != vy) return vy - vx;
    return strcmp(x->last, y->last);
}

int nfl2k5_eras_leaders(int category, const ErasPlayerSeason **out, int max)
{
    static const ErasPlayerSeason *all[NFL2K5_AW_MAX_PLAYERS];
    int i, n = 0;
    nfl2k5_eras_awards_build();
    for (i = 0; i < s_np; i++) if (nfl2k5_eras_leader_value(category, &s_p[i]) > 0) all[n++] = &s_p[i];
    s_cat = category;
    qsort(all, (size_t)n, sizeof all[0], cmp_cat);
    if (n > max) n = max;
    for (i = 0; i < n; i++) out[i] = all[i];
    (void)cmp_points; (void)s_lead;
    return n;
}

int nfl2k5_eras_awards_write(void)
{
    char path[MAX_PATH], coach[48];
    FILE *f;
    int i;
    if (!nfl2k5_eras_season_complete()) return -1;
    nfl2k5_eras_league_peek("awards.csv", path, sizeof path);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 0;
    nfl2k5_eras_awards_build();
    nfl2k5_eras_league_path("awards.csv", path, sizeof path);
    if (fopen_s(&f, path, "wb") || !f) return -1;
    fprintf(f, "award,first,last,team,pos,stat,rule\n");
    for (i = 0; i < s_naw; i++) {
        const ErasAward *a = &s_aw[i];
        if (a->p) fprintf(f, "%s,%s,%s,%s,%d,%s,%s\n", a->award, a->p->first, a->p->last, nfl2k5_eras_team(a->p->team)->id, a->p->pos, a->stat, nfl2k5_eras_awards_rule());
        else if (a->coach_team >= 0) { coach_name(a->coach_team, coach, sizeof coach); fprintf(f, "%s,%s,,%s,,%s,best record\n", a->award, coach, nfl2k5_eras_team(a->coach_team)->id, a->stat); }
        else fprintf(f, "%s,,,,,%s,\n", a->award, a->stat);
    }
    fprintf(f, "# %d of %d played regular-season games had a box score\n", s_games_with_box, s_games_played);
    fclose(f);
    nfl2k5_eras_log("awards written: %s", path);
    return 1;
}

/* for the screen: the coach's name of a team (coaches.csv) */
void nfl2k5_eras_awards_coach(int team, char *out, size_t n) { coach_name(team, out, n); }
int nfl2k5_eras_awards_box_games(int *played) { if (played) *played = s_games_played; return s_games_with_box; }

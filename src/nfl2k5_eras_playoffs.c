/* My NFL Eras: the playoffs (M14, docs/eras/results/M14-season-end.md).
 *
 * Format (1978-1989; data/eras/league/rules_1966_2025.json playoff_teams 1978-1981 = 10,
 * nfl_seasons_1970_1980.json playoff_format): three division winners and two wild cards per
 * conference. The two wild cards meet in the Wild Card game at the better seed; the divisional
 * round pairs the wild-card winner with the 1 seed unless they are from the same division (then
 * the 1 seed hosts the 3 seed and the 2 seed hosts the wild-card winner: the 1970-1989 rule that
 * two teams from one division could not meet before the conference championship, NFL Record &
 * Fact Book via league_history.py); the higher seed hosts the conference championship; the Super
 * Bowl is at the package's neutral site (postseason.csv) with the designated home conference.
 *
 * Seeding and tiebreakers follow the NFL procedure as it was applied in 1980, in the order the
 * 1980 cases show (Wikipedia, 1980 NFL season, "Tiebreakers": Cleveland over Houston in the AFC
 * Central on conference record; San Diego over Oakland on net points in division games; San Diego
 * the top AFC seed on conference record, Cleveland the 2 seed on common games; Oakland the first
 * wild card over Houston on conference record; Minnesota over Detroit on conference record;
 * Tampa Bay over Green Bay head-to-head) and the structure of the NFL's published procedure
 * (https://www.nfl.com/standings/tie-breaking-procedures: only one club advances per step,
 * remaining tied clubs restart at step 1; three or more wild-card clubs: the division procedure
 * first leaves one club per division). The steps the NFL added later (strength of victory /
 * schedule, 2002; the scoring rankings) are not used:
 *
 *   division (two or more clubs):  head-to-head, division record, conference record, common
 *                                  games, net points in division games, net points in all games,
 *                                  coin toss (a seeded, logged draw)
 *   wild card / seeding:           head-to-head (when every pair has met; three or more: a sweep),
 *                                  conference record, common games (at least four), net points in
 *                                  conference games, net points in all games, coin toss
 *
 * Division winners are seeded 1-3 with the wild-card procedure; the two best non-winners of the
 * conference are the wild cards (a tie between clubs of one division goes to the division
 * procedure; three or more clubs: one per division first). A percentage counts a tie as half a
 * win. Everything is recomputed from the league's regular-season results; the bracket is written
 * once (playoffs.csv, seeds.csv) and then read back. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_playoffs.h"

#define MAXT NFL2K5_ERAS_MAX_TEAMS
#define ROUND_WEEK0 16

static struct {
    int n;                         /* teams */
    ErasRecord all[MAXT], dv[MAXT], cf[MAXT];
    int h2h_w[MAXT][MAXT], h2h_l[MAXT][MAXT], h2h_t[MAXT][MAXT];   /* a's record against b */
    int gp[MAXT][MAXT];            /* games between a and b */
    int left[MAXT];                /* regular-season games not yet played */
    int log;
} s;

static int log_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("NFL2K5_ERAS_LOG"); on = e && *e && *e != '0'; }
    return on;
}

static int same_div(int a, int b) { return !strcmp(nfl2k5_eras_team(a)->division, nfl2k5_eras_team(b)->division); }
static int same_conf(int a, int b) { return !strcmp(nfl2k5_eras_team(a)->conference, nfl2k5_eras_team(b)->conference); }

static void tally(ErasRecord *r, int pf, int pa)
{
    r->pf += pf; r->pa += pa;
    if (pf > pa) r->w++; else if (pa > pf) r->l++; else r->t++;
}

static void compute(void)
{
    int i, n = nfl2k5_eras_team_count(), g;
    memset(&s, 0, sizeof s);
    s.log = log_on();
    s.n = n;
    for (g = 0; g < nfl2k5_eras_regular_game_count(); g++) {
        const ErasGame *e = nfl2k5_eras_game(g);
        int hp, ap;
        if (nfl2k5_eras_result(g, &hp, &ap) != NFL2K5_ERAS_PLAYED) { s.left[e->home]++; s.left[e->away]++; continue; }
        tally(&s.all[e->home], hp, ap); tally(&s.all[e->away], ap, hp);
        if (same_div(e->home, e->away)) { tally(&s.dv[e->home], hp, ap); tally(&s.dv[e->away], ap, hp); }
        if (same_conf(e->home, e->away)) { tally(&s.cf[e->home], hp, ap); tally(&s.cf[e->away], ap, hp); }
        s.gp[e->home][e->away]++; s.gp[e->away][e->home]++;
        if (hp > ap) { s.h2h_w[e->home][e->away]++; s.h2h_l[e->away][e->home]++; }
        else if (ap > hp) { s.h2h_w[e->away][e->home]++; s.h2h_l[e->home][e->away]++; }
        else { s.h2h_t[e->home][e->away]++; s.h2h_t[e->away][e->home]++; }
    }
    (void)i;
}

/* percentage x 10000 (a tie = half a win); -1 for no games */
static long pct(int w, int l, int t) { int g = w + l + t; return g ? (long)((w * 2 + t) * 10000L / (2L * g)) : -1; }
static long pct_rec(const ErasRecord *r) { return pct(r->w, r->l, r->t); }

/* ---- tiebreak steps ------------------------------------------------------- */

enum { ST_H2H, ST_DIV, ST_CONF, ST_COMMON, ST_NET_DIV, ST_NET_CONF, ST_NET_ALL, ST_COIN, ST_NONE };
static const int k_div_steps[] = { ST_H2H, ST_DIV, ST_CONF, ST_COMMON, ST_NET_DIV, ST_NET_ALL, ST_COIN, ST_NONE };
static const int k_wc_steps[]  = { ST_H2H, ST_CONF, ST_COMMON, ST_NET_CONF, ST_NET_ALL, ST_COIN, ST_NONE };
static const char *const k_step_name[] = { "head-to-head", "division record", "conference record", "common games",
                                           "net points in division games", "net points in conference games",
                                           "net points in all games", "coin toss (seeded draw)" };

/* record of `a` against the clubs of `set` (not itself) */
static long h2h_pct(int a, const int *set, int n)
{
    int k, w = 0, l = 0, t = 0;
    for (k = 0; k < n; k++) if (set[k] != a) { w += s.h2h_w[a][set[k]]; l += s.h2h_l[a][set[k]]; t += s.h2h_t[a][set[k]]; }
    return pct(w, l, t);
}

/* common opponents of the set: every club has played them; `a`'s record against those */
static long common_pct(int a, const int *set, int n, int *games_out)
{
    int o, k, w = 0, l = 0, t = 0, gms = 0;
    for (o = 0; o < s.n; o++) {
        int in = 1;
        for (k = 0; k < n; k++) if (set[k] == o || !s.gp[set[k]][o]) { in = 0; break; }
        if (!in) continue;
        w += s.h2h_w[a][o]; l += s.h2h_l[a][o]; t += s.h2h_t[a][o];
        gms += s.gp[a][o];
    }
    *games_out = gms;
    return pct(w, l, t);
}

static long metric(int step, int a, const int *set, int n, int wild, int *applicable)
{
    int gms;
    *applicable = 1;
    switch (step) {
    case ST_H2H:
        if (wild) {
            /* applicable only when every pair of the tied clubs has met */
            int i, j;
            for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) if (!s.gp[set[i]][set[j]]) { *applicable = 0; return 0; }
        }
        { long p = h2h_pct(a, set, n); if (p < 0) { *applicable = 0; return 0; } return p; }
    case ST_DIV: return pct_rec(&s.dv[a]);
    case ST_CONF: return pct_rec(&s.cf[a]);
    case ST_COMMON: { long p = common_pct(a, set, n, &gms); if (p < 0 || (wild && gms < 4)) { *applicable = 0; return 0; } return p; }
    case ST_NET_DIV: return s.dv[a].pf - s.dv[a].pa;
    case ST_NET_CONF: return s.cf[a].pf - s.cf[a].pa;
    case ST_NET_ALL: return s.all[a].pf - s.all[a].pa;
    case ST_COIN: {
        uint32_t h = nfl2k5_eras_league_seed() ^ 0xC01Cu;
        const char *p = nfl2k5_eras_team(a)->id;
        for (; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
        return (long)(h & 0x7FFFFFFFu);
    }
    }
    return 0;
}

/* The best club of a tied set by the division (wild = 0) or wild-card (wild = 1) procedure.
 * Only one club advances per step; when a step thins the set, it restarts at step 1. */
static int tb_best(const int *tied, int n, int wild, char *why, size_t wn)
{
    int set[MAXT], m = n, si = 0, k;
    const int *steps = wild ? k_wc_steps : k_div_steps;
    memcpy(set, tied, sizeof(int) * (size_t)n);
    if (why) why[0] = 0;
    if (n == 1) return set[0];
    while (steps[si] != ST_NONE) {
        long v[MAXT], best = 0;
        int app = 1, cnt = 0, keep[MAXT];
        for (k = 0; k < m; k++) {
            int a;
            v[k] = metric(steps[si], set[k], set, m, wild, &a);
            if (!a) app = 0;
        }
        if (!app) { si++; continue; }
        if (steps[si] == ST_H2H && wild && m > 2) {
            /* three or more wild-card clubs: head-to-head applies only as a sweep */
            int sweeper = -1, swept = -1, i;
            for (i = 0; i < m; i++) {
                int w = 0, l = 0, j;
                for (j = 0; j < m; j++) if (j != i) { w += s.h2h_w[set[i]][set[j]]; l += s.h2h_l[set[i]][set[j]]; }
                if (w && !l && w == m - 1) sweeper = i;
                if (l && !w && l == m - 1) swept = i;
            }
            if (sweeper >= 0) { if (why) snprintf(why, wn, "head-to-head sweep"); return set[sweeper]; }
            if (swept >= 0) { for (i = 0, cnt = 0; i < m; i++) if (i != swept) set[cnt++] = set[i]; m = cnt; si = 0; continue; }
            si++;
            continue;
        }
        for (k = 0; k < m; k++) if (k == 0 || v[k] > best) best = v[k];
        for (k = 0; k < m; k++) if (v[k] == best) keep[cnt++] = set[k];
        if (cnt == 1) {
            if (why) {
                const ErasRecord *r = NULL;
                char val[48] = "";
                if (steps[si] == ST_DIV) r = &s.dv[keep[0]]; else if (steps[si] == ST_CONF) r = &s.cf[keep[0]];
                if (r) snprintf(val, sizeof val, " (%d-%d-%d)", r->w, r->l, r->t);
                else if (steps[si] >= ST_NET_DIV && steps[si] <= ST_NET_ALL) snprintf(val, sizeof val, " (%+ld)", best);
                snprintf(why, wn, "%s%s", k_step_name[steps[si]], val);
            }
            return keep[0];
        }
        if (cnt < m) { memcpy(set, keep, sizeof(int) * (size_t)cnt); m = cnt; si = 0; continue; }
        si++;
    }
    if (why) snprintf(why, wn, "unresolved (order kept)");
    return set[0];
}

/* Order a group of teams best first: ties on percentage go to tb_best (wild: the wild-card
 * procedure; for a wild-card tie of clubs from several divisions the division procedure first
 * leaves one club per division). `how` gets the deciding step of each placed team. */
static int order_group(const int *teams, int n, int wild, int *out, char how[][96])
{
    int left[MAXT], m = n, placed = 0, k;
    memcpy(left, teams, sizeof(int) * (size_t)n);
    while (m > 0) {
        long best = -2;
        int tied[MAXT], nt = 0, pick, i;
        char why[96] = "";
        for (k = 0; k < m; k++) { long p = pct_rec(&s.all[left[k]]); if (p > best) best = p; }
        for (k = 0; k < m; k++) if (pct_rec(&s.all[left[k]]) == best) tied[nt++] = left[k];
        if (nt == 1) { pick = tied[0]; snprintf(why, sizeof why, "record"); }
        else if (!wild) pick = tb_best(tied, nt, 0, why, sizeof why);
        else {
            /* one club per division first (division procedure), then the wild-card procedure */
            int reps[MAXT], nr = 0, used[MAXT];
            memset(used, 0, sizeof used);
            for (i = 0; i < nt; i++) {
                int grp[MAXT], ng = 0, j;
                if (used[i]) continue;
                for (j = i; j < nt; j++) if (!used[j] && same_div(tied[i], tied[j])) { grp[ng++] = tied[j]; used[j] = 1; }
                reps[nr++] = ng == 1 ? grp[0] : tb_best(grp, ng, 0, why, sizeof why);
            }
            if (nr == 1) {
                char t[96];
                pick = reps[0];
                snprintf(t, sizeof t, "division procedure: %s", why[0] ? why : "record");
                snprintf(why, sizeof why, "%s", t);
            } else pick = tb_best(reps, nr, 1, why, sizeof why);
        }
        out[placed] = pick;
        if (how) snprintf(how[placed], 96, "%s", why);
        placed++;
        for (k = 0, i = 0; k < m; k++) if (left[k] != pick) left[i++] = left[k];
        m = i;
    }
    return placed;
}

/* ---- the picture / the field ----------------------------------------------- */

static int conf_of(int t) { return !strcmp(nfl2k5_eras_team(t)->conference, "NFC"); }

static int picture(int conference, ErasSeed *out, int max, int max_hunt)
{
    const char *divs[4];
    int nd = nfl2k5_eras_divisions(conference ? "NFC" : "AFC", divs), d, k, n = 0, i;
    int winners[4], others[MAXT], no = 0, ord[MAXT], final_ = nfl2k5_eras_regular_complete();
    char how[MAXT][96], whow[4][96];
    compute();
    for (d = 0; d < nd && d < 4; d++) {
        int members[MAXT], nm = 0;
        for (i = 0; i < s.n; i++) if (!strcmp(nfl2k5_eras_team(i)->division, divs[d])) members[nm++] = i;
        order_group(members, nm, 0, ord, how);
        winners[d] = ord[0];
        snprintf(whow[d], 96, "%s", how[0]);
        for (k = 1; k < nm; k++) others[no++] = ord[k];
    }
    /* seeds 1-3: the division winners by the wild-card procedure */
    {
        int so[4];
        char shw[4][96];
        order_group(winners, nd, 1, so, shw);
        for (k = 0; k < nd && n < max; k++) {
            ErasSeed *e = &out[n++];
            memset(e, 0, sizeof *e);
            e->team = so[k]; e->seed = k + 1; e->division_winner = 1;
            for (d = 0; d < nd; d++) if (winners[d] == so[k]) break;
            if (!strcmp(shw[k], "record") && d < nd && strcmp(whow[d], "record")) snprintf(e->how, sizeof e->how, "division: %s", whow[d]);
            else if (strcmp(shw[k], "record")) snprintf(e->how, sizeof e->how, "seed: %s", shw[k]);
            else snprintf(e->how, sizeof e->how, "record");
        }
    }
    /* wild cards and the hunt */
    order_group(others, no, 1, ord, how);
    for (k = 0; k < no && n < max && k < 2 + max_hunt; k++) {
        ErasSeed *e = &out[n++];
        memset(e, 0, sizeof *e);
        e->team = ord[k]; e->seed = k < 2 ? nd + 1 + k : 0; e->division_winner = 0;
        snprintf(e->how, sizeof e->how, "%s", how[k]);
    }
    /* clinch marks (in season): sufficient conditions without tiebreak look-ahead */
    if (!final_) {
        for (k = 0; k < n; k++) {
            int t = out[k].team, c = 0, i2, ahead_all = 1, ahead_div = 1, cannot = 0;
            long mine = (long)(s.all[t].w * 2 + s.all[t].t);
            for (i2 = 0; i2 < s.n; i2++) {
                long max2 = (long)(s.all[i2].w * 2 + s.all[i2].t + 2 * s.left[i2]);
                if (i2 == t || conf_of(i2) != conference) continue;
                if (max2 >= mine) { ahead_all = 0; if (same_div(t, i2)) ahead_div = 0; }
                else cannot++;
            }
            if (ahead_div) c |= 2;
            if (ahead_div || cannot >= (s.n / 2) - NFL2K5_PO_SEEDS) c |= 1;
            if (ahead_all) c |= 4 | 2 | 1;
            out[k].clinched = c;
        }
    } else for (k = 0; k < n; k++) out[k].clinched = out[k].seed ? (out[k].seed == 1 ? 7 : out[k].division_winner ? 3 : 1) : 0;
    return n;
}

int nfl2k5_eras_playoff_picture(int conference, ErasSeed *out, int max, int max_hunt)
{
    if (!nfl2k5_eras_league_loaded()) return 0;
    return picture(conference, out, max, max_hunt);
}

/* ---- postseason.csv ------------------------------------------------------------ */

static const char *const k_round_code[5] = { "", "wildcard", "divisional", "conference", "superbowl" };
static const char *const k_round_name[5] = { "Regular Season", "Wild Card", "Divisional", "Conference Championship", "Super Bowl" };

const char *nfl2k5_eras_round_name(int round) { return round >= 0 && round <= 4 ? k_round_name[round] : ""; }

static void field(char **p, char *out, size_t n)
{
    size_t k = 0;
    while (**p && **p != ',' && **p != '\r' && **p != '\n') { if (k + 1 < n) out[k++] = **p; (*p)++; }
    out[k] = 0;
    if (**p == ',') (*p)++;
}

int nfl2k5_eras_postseason_row(int round, char *date, size_t dn, char *label, size_t ln, char *site, size_t sn, char *home_conf, size_t hn)
{
    char path[MAX_PATH], line[512];
    FILE *f;
    int found = -1;
    if (date) date[0] = 0; if (label) label[0] = 0; if (site) site[0] = 0; if (home_conf) home_conf[0] = 0;
    if (round < 1 || round > 4) return -1;
    snprintf(path, sizeof path, "%s\\postseason.csv", nfl2k5_eras_package_dir());
    if (fopen_s(&f, path, "rb") || !f) return -1;
    fgets(line, sizeof line, f);
    while (fgets(line, sizeof line, f)) {
        char *c = line, code[16], d[16], l[48], st[48], sh[64], hc[8];
        field(&c, code, sizeof code); field(&c, d, sizeof d); field(&c, l, sizeof l); field(&c, st, sizeof st); field(&c, sh, sizeof sh); field(&c, hc, sizeof hc);
        if (strcmp(code, k_round_code[round])) continue;
        if (date) snprintf(date, dn, "%s", d);
        if (label) snprintf(label, ln, "%s", l);
        if (site) snprintf(site, sn, "%s", st);
        if (home_conf) snprintf(home_conf, hn, "%s", hc);
        found = 0;
        break;
    }
    fclose(f);
    return found;
}

const char *nfl2k5_eras_super_bowl_name(void)
{
    static char name[48];
    static int season;
    if (season != nfl2k5_eras_season()) {
        char up[48];
        size_t i;
        season = nfl2k5_eras_season();
        name[0] = 0;
        if (nfl2k5_eras_postseason_row(4, NULL, 0, up, sizeof up, NULL, 0, NULL, 0) == 0) {
            /* "SUPER BOWL XV" -> "Super Bowl XV" */
            snprintf(name, sizeof name, "%s", up);
            for (i = 0; name[i]; i++) if (i > 0 && name[i - 1] != ' ' && i < 10 && name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
        }
    }
    return name;
}

int nfl2k5_eras_champion(void)
{
    int g, hp, ap;
    for (g = nfl2k5_eras_regular_game_count(); g < nfl2k5_eras_game_count(); g++) {
        const ErasGame *e = nfl2k5_eras_game(g);
        if (e->round == 4 && nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED) return hp > ap ? e->home : e->away;
    }
    return -1;
}

/* ---- playoffs.csv ---------------------------------------------------------------- */

static int read_playoffs_csv(void)
{
    char path[MAX_PATH], line[512];
    FILE *f;
    int added = 0;
    nfl2k5_eras_league_peek("playoffs.csv", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return 0;
    fgets(line, sizeof line, f);
    while (fgets(line, sizeof line, f)) {
        ErasGame g;
        char *c = line, a[32], h[32], b[16];
        memset(&g, 0, sizeof g);
        field(&c, g.id, sizeof g.id);
        field(&c, b, sizeof b); g.round = atoi(b);
        field(&c, b, sizeof b); g.week = atoi(b);
        field(&c, g.date, sizeof g.date);
        field(&c, a, sizeof a); field(&c, h, sizeof h);
        field(&c, b, sizeof b);                        /* conference */
        field(&c, b, sizeof b); g.seed_away = atoi(b);
        field(&c, b, sizeof b); g.seed_home = atoi(b);
        field(&c, g.site, sizeof g.site);
        field(&c, g.label, sizeof g.label);
        g.away = nfl2k5_eras_team_find(a); g.home = nfl2k5_eras_team_find(h);
        if (!g.id[0] || g.away < 0 || g.home < 0 || g.round < 1 || g.round > 4) continue;
        if (nfl2k5_eras_game_find(g.id) >= 0) continue;
        if (nfl2k5_eras_add_game(&g) >= 0) added++;
    }
    fclose(f);
    return added;
}

static void append_playoff_game(const ErasGame *g, const char *conference)
{
    char path[MAX_PATH];
    FILE *f;
    int fresh;
    nfl2k5_eras_league_path("playoffs.csv", path, sizeof path);
    fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    if (fopen_s(&f, path, "ab") || !f) { nfl2k5_eras_log("playoffs: cannot write %s", path); return; }
    if (fresh) fprintf(f, "game_id,round,week,date,away,home,conference,seed_away,seed_home,site,label\n");
    fprintf(f, "%s,%d,%d,%s,%s,%s,%s,%d,%d,%s,%s\n", g->id, g->round, g->week, g->date, nfl2k5_eras_team(g->away)->id,
            nfl2k5_eras_team(g->home)->id, conference, g->seed_away, g->seed_home, g->site, g->label);
    fclose(f);
}

static const char *const k_round_prefix[5] = { "w", "wc", "dv", "cc", "sb" };

static int make_game(int round, int away, int home, int seed_away, int seed_home, const char *conference, const char *label_prefix)
{
    ErasGame g;
    char date[16], label[48], site[48], hc[8];
    memset(&g, 0, sizeof g);
    nfl2k5_eras_postseason_row(round, date, sizeof date, label, sizeof label, site, sizeof site, hc, sizeof hc);
    snprintf(g.id, sizeof g.id, "%d%s-%s-%s", nfl2k5_eras_season(), k_round_prefix[round], nfl2k5_eras_team(away)->id, nfl2k5_eras_team(home)->id);
    g.round = round; g.week = ROUND_WEEK0 + round; g.away = away; g.home = home; g.seed_away = seed_away; g.seed_home = seed_home;
    snprintf(g.date, sizeof g.date, "%s", date[0] ? date : "");
    if (round == 4) { snprintf(g.site, sizeof g.site, "%s", site); snprintf(g.label, sizeof g.label, "%s", label[0] ? label : "SUPER BOWL"); }
    else snprintf(g.label, sizeof g.label, "%s %s", label_prefix, label[0] ? label : k_round_name[round]);
    if (nfl2k5_eras_add_game(&g) < 0) return -1;
    append_playoff_game(&g, conference);
    nfl2k5_eras_log("playoffs: %s: %s (%d) at %s (%d), %s%s%s", g.label, nfl2k5_eras_team(away)->name, seed_away,
                    nfl2k5_eras_team(home)->name, seed_home, g.date, g.site[0] ? ", site " : "", g.site);
    return 0;
}

/* the result of a round's game of a conference: winner team, -1 if unplayed / none */
static int round_winner(int round, int conference, int which, int *loser_seed_out, int *winner_seed_out)
{
    int g, k = 0, hp, ap;
    for (g = nfl2k5_eras_regular_game_count(); g < nfl2k5_eras_game_count(); g++) {
        const ErasGame *e = nfl2k5_eras_game(g);
        if (e->round != round) continue;
        if (round < 4 && conf_of(e->home) != conference) continue;
        if (k++ != which) continue;
        if (nfl2k5_eras_result(g, &hp, &ap) != NFL2K5_ERAS_PLAYED) return -1;
        if (loser_seed_out) *loser_seed_out = hp > ap ? e->seed_away : e->seed_home;
        if (winner_seed_out) *winner_seed_out = hp > ap ? e->seed_home : e->seed_away;
        return hp > ap ? e->home : e->away;
    }
    return -2;   /* no such game */
}

static int round_games(int round)
{
    int g, n = 0;
    for (g = nfl2k5_eras_regular_game_count(); g < nfl2k5_eras_game_count(); g++) n += nfl2k5_eras_game(g)->round == round;
    return n;
}

static int round_complete(int round)
{
    int g, hp, ap, n = 0;
    for (g = nfl2k5_eras_regular_game_count(); g < nfl2k5_eras_game_count(); g++) {
        if (nfl2k5_eras_game(g)->round != round) continue;
        n++;
        if (nfl2k5_eras_result(g, &hp, &ap) != NFL2K5_ERAS_PLAYED) return 0;
    }
    return n > 0;
}

static void write_seeds(ErasSeed seeds[2][NFL2K5_PO_SEEDS])
{
    char path[MAX_PATH];
    FILE *f;
    int c, k;
    nfl2k5_eras_league_path("seeds.csv", path, sizeof path);
    if (fopen_s(&f, path, "wb") || !f) return;
    fprintf(f, "conference,seed,team,division_winner,record,how\n");
    for (c = 0; c < 2; c++)
        for (k = 0; k < NFL2K5_PO_SEEDS; k++) {
            const ErasSeed *e = &seeds[c][k];
            const ErasRecord *r = &s.all[e->team];
            fprintf(f, "%s,%d,%s,%d,%d-%d-%d,%s\n", c ? "NFC" : "AFC", e->seed, nfl2k5_eras_team(e->team)->id, e->division_winner,
                    r->w, r->l, r->t, e->how);
            nfl2k5_eras_log("playoffs: %s seed %d %s %s (%d-%d-%d, %s): %s", c ? "NFC" : "AFC", e->seed, nfl2k5_eras_team(e->team)->city,
                            nfl2k5_eras_team(e->team)->name, r->w, r->l, r->t, e->division_winner ? "division winner" : "wild card", e->how);
        }
    fclose(f);
}

/* seeds.csv read back (so the bracket never re-seeds after a later edit) */
static int read_seeds(ErasSeed seeds[2][NFL2K5_PO_SEEDS])
{
    char path[MAX_PATH], line[256];
    FILE *f;
    int n = 0;
    nfl2k5_eras_league_peek("seeds.csv", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return 0;
    fgets(line, sizeof line, f);
    while (fgets(line, sizeof line, f)) {
        char *c = line, conf[8], team[32], b[16];
        int seed, c2;
        field(&c, conf, sizeof conf); field(&c, b, sizeof b); seed = atoi(b); field(&c, team, sizeof team);
        c2 = !strcmp(conf, "NFC");
        if (seed < 1 || seed > NFL2K5_PO_SEEDS || nfl2k5_eras_team_find(team) < 0) continue;
        memset(&seeds[c2][seed - 1], 0, sizeof seeds[c2][seed - 1]);
        seeds[c2][seed - 1].team = nfl2k5_eras_team_find(team); seeds[c2][seed - 1].seed = seed;
        field(&c, b, sizeof b); seeds[c2][seed - 1].division_winner = atoi(b);
        field(&c, b, sizeof b); field(&c, seeds[c2][seed - 1].how, sizeof seeds[c2][seed - 1].how);
        n++;
    }
    fclose(f);
    return n == 2 * NFL2K5_PO_SEEDS;
}

static int team_of_seed(ErasSeed seeds[2][NFL2K5_PO_SEEDS], int c, int seed) { return seeds[c][seed - 1].team; }

int nfl2k5_eras_playoffs_update(void)
{
    static ErasSeed seeds[2][NFL2K5_PO_SEEDS];
    int added = 0, c;
    const char *cn[2] = { "AFC", "NFC" };
    if (!nfl2k5_eras_league_loaded()) return 0;
    added += read_playoffs_csv();
    if (!nfl2k5_eras_regular_complete()) return added > 0;
    if (!read_seeds(seeds)) {
        /* the regular season has just ended: seed both conferences and start the wild-card round */
        ErasSeed pic[MAXT];
        int n, k;
        for (c = 0; c < 2; c++) {
            n = picture(c, pic, MAXT, 0);
            if (n < NFL2K5_PO_SEEDS) { nfl2k5_eras_log("playoffs: %s has only %d seeds; the bracket is not built", cn[c], n); return added > 0; }
            for (k = 0; k < NFL2K5_PO_SEEDS; k++) seeds[c][k] = pic[k];
        }
        nfl2k5_eras_log("playoffs: the regular season is complete (%d games); seeding by the 1980 procedure (docs/eras/results/M14-season-end.md)",
                        nfl2k5_eras_regular_game_count());
        write_seeds(seeds);
    } else compute();
    if (round_games(1) == 0) {
        for (c = 0; c < 2; c++)
            if (make_game(1, team_of_seed(seeds, c, 5), team_of_seed(seeds, c, 4), 5, 4, cn[c], cn[c]) == 0) added++;
    }
    if (round_games(1) == 2 && round_complete(1) && round_games(2) == 0) {
        for (c = 0; c < 2; c++) {
            int ws, w = round_winner(1, c, 0, NULL, &ws), s1 = team_of_seed(seeds, c, 1), s2 = team_of_seed(seeds, c, 2), s3 = team_of_seed(seeds, c, 3);
            if (w < 0) continue;
            if (same_div(w, s1)) {
                /* the wild card may not meet its own division's champion before the conference final */
                if (make_game(2, s3, s1, 3, 1, cn[c], cn[c]) == 0) added++;
                if (make_game(2, w, s2, ws, 2, cn[c], cn[c]) == 0) added++;
            } else {
                if (make_game(2, w, s1, ws, 1, cn[c], cn[c]) == 0) added++;
                if (make_game(2, s3, s2, 3, 2, cn[c], cn[c]) == 0) added++;
            }
        }
    }
    if (round_games(2) == 4 && round_complete(2) && round_games(3) == 0) {
        for (c = 0; c < 2; c++) {
            int sa, sb, a = round_winner(2, c, 0, NULL, &sa), b = round_winner(2, c, 1, NULL, &sb);
            if (a < 0 || b < 0) continue;
            if (sa < sb) { if (make_game(3, b, a, sb, sa, cn[c], cn[c]) == 0) added++; }
            else if (make_game(3, a, b, sa, sb, cn[c], cn[c]) == 0) added++;
        }
    }
    if (round_games(3) == 2 && round_complete(3) && round_games(4) == 0) {
        int sa, sn, afc = round_winner(3, 0, 0, NULL, &sa), nfc = round_winner(3, 1, 0, NULL, &sn);
        char hc[8] = "";
        nfl2k5_eras_postseason_row(4, NULL, 0, NULL, 0, NULL, 0, hc, sizeof hc);
        if (afc >= 0 && nfc >= 0) {
            if (!strcmp(hc, "AFC")) { if (make_game(4, nfc, afc, sn, sa, "NFL", "") == 0) added++; }
            else if (make_game(4, afc, nfc, sa, sn, "NFL", "") == 0) added++;
        }
    }
    return added > 0;
}

int nfl2k5_eras_playoffs_explain(char *out, size_t n)
{
    ErasSeed pic[MAXT];
    int c, k, cnt;
    size_t o = 0;
    out[0] = 0;
    for (c = 0; c < 2; c++) {
        cnt = nfl2k5_eras_playoff_picture(c, pic, MAXT, 3);
        for (k = 0; k < cnt && o + 120 < n; k++) {
            const ErasRecord *r = &s.all[pic[k].team];
            o += (size_t)snprintf(out + o, n - o, "%s %s %d %s %d-%d-%d: %s\n", c ? "NFC" : "AFC", pic[k].seed ? "seed" : "hunt",
                                  pic[k].seed, nfl2k5_eras_team(pic[k].team)->name, r->w, r->l, r->t, pic[k].how);
        }
    }
    return (int)o;
}

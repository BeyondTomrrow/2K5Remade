/* My NFL Eras: the draft model (docs/eras/results/M13-draft.md; research in
 * docs/eras/research/DRAFT-CLASSES.md, decisions in PHASE2-PLAN.md "Draft decisions").
 *
 * Class: mods\eras\draft\class_<year>.csv (tools/eras/drafts/make_draft_class.py): every real pick
 * of the year with his 2K5 position, college, body, jersey, the 28 rookie ratings, the 28 ratings of
 * his real peak season (the potential), his source (rookie_rated / next_season / rated_later /
 * estimated) and flags. On the first open of a league's draft the class is copied to
 * <league>\draft\<year>\class.csv; in Alternate History N sleepers (league.ini sleepers, default 5)
 * are drawn there from the estimated / late-round pool with a seed fixed per league and get a hidden
 * star potential (flag `sleeper`, peak = the rookie profile scaled to 88 overall). The copy is what
 * the league reads from then on, so the class never changes under a league.
 *
 * Order: Historical = the real slots (team and pick number from the class, trades already in it).
 * Alternate History = the league's standings, worst record first, every round the same order, no
 * forfeits: fewer wins, then more losses, then worse point difference, then fewer points scored
 * (the reverse of the standings order in nfl2k5_eras.c), then the real 1981 order as the last
 * tiebreak (an unplayed league drafts in the real order).
 *
 * Picks: Historical CPU teams take their real pick while he is available (the user may have taken
 * him), else the best available by need and value. Alternate History CPU teams pick by need and
 * value with a little seeded randomness (0..4 overall points). Value = the rookie overall (never the
 * potential); need = how far the team is below a 45-man roster's typical count at the position.
 * Every pick is appended to <league>\draft\<year>\picks.csv at once and logged.
 *
 * Rookies: when the draft is complete the next season's roster files are written under the league
 * (rosters\<year>\<id>.csv + .flags.txt, depth\<year>\<id>.csv): the season's rows (the package's,
 * never changed on disk) plus the team's rookies after the last player of their position, kept
 * within 42..53 by releasing the lowest-rated players who are not starters (depth rank 0) and not
 * needed for the staging minimums, each release logged (draft\<year>\releases.log and eras.log). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_draft.h"

#define NR NFL2K5_DRAFT_RATINGS
#define MAXT NFL2K5_ERAS_MAX_TEAMS
#define ROSTER_MAX 53
#define ROSTER_MIN 42
#define SLEEPER_TARGET 88

enum { R_SPEED, R_AGILITY, R_ARM, R_STAMINA, R_KPOW, R_DUR, R_STR, R_JUMP, R_COV, R_ROUTE, R_TACKLE, R_BRKTKL, R_PACC,
       R_PREAD, R_CATCH, R_RBLK, R_PBLK, R_HOLD, R_PRUSH, R_RCOV, R_KACC, R_KSTYLE, R_LEAD, R_PRSTYLE, R_COMP, R_SCRAM,
       R_CONS, R_AGGR };
static const char *const k_pos[17] = { "QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE" };
/* the Mod Tools' position-weighted estimate (make_draft_class.py WEIGHTS), {rating, weight} */
typedef struct { signed char r, w; } Wt;
static const Wt k_w_qb[] = { {R_PACC,28},{R_PREAD,22},{R_ARM,18},{R_COMP,10},{R_SCRAM,6},{R_SPEED,6},{R_AGILITY,5},{R_CONS,5},{-1,0} };
static const Wt k_w_hb[] = { {R_SPEED,20},{R_AGILITY,18},{R_BRKTKL,18},{R_STR,10},{R_CATCH,8},{R_HOLD,8},{R_RBLK,6},{R_JUMP,5},{R_STAMINA,5},{-1,0} };
static const Wt k_w_fb[] = { {R_RBLK,24},{R_STR,18},{R_BRKTKL,16},{R_SPEED,12},{R_CATCH,10},{R_AGILITY,10},{R_HOLD,10},{-1,0} };
static const Wt k_w_wr[] = { {R_CATCH,28},{R_ROUTE,22},{R_SPEED,20},{R_AGILITY,12},{R_JUMP,10},{R_HOLD,8},{-1,0} };
static const Wt k_w_te[] = { {R_CATCH,24},{R_RBLK,18},{R_ROUTE,16},{R_SPEED,12},{R_PBLK,10},{R_STR,10},{R_AGILITY,10},{-1,0} };
static const Wt k_w_ol[] = { {R_RBLK,38},{R_PBLK,38},{R_STR,16},{R_AGILITY,8},{-1,0} };
static const Wt k_w_dt[] = { {R_PRUSH,28},{R_TACKLE,22},{R_STR,22},{R_RCOV,16},{R_AGILITY,6},{R_SPEED,6},{-1,0} };
static const Wt k_w_de[] = { {R_PRUSH,32},{R_TACKLE,20},{R_STR,18},{R_RCOV,14},{R_SPEED,10},{R_AGILITY,6},{-1,0} };
static const Wt k_w_lb[] = { {R_TACKLE,26},{R_RCOV,20},{R_COV,16},{R_PRUSH,14},{R_SPEED,12},{R_STR,12},{-1,0} };
static const Wt k_w_cb[] = { {R_COV,30},{R_SPEED,22},{R_AGILITY,16},{R_TACKLE,14},{R_JUMP,10},{R_CATCH,8},{-1,0} };
static const Wt k_w_ss[] = { {R_COV,22},{R_TACKLE,24},{R_SPEED,20},{R_AGILITY,14},{R_JUMP,10},{R_STR,10},{-1,0} };
static const Wt k_w_k[]  = { {R_KPOW,50},{R_KACC,50},{-1,0} };
static const Wt *const k_weights[17] = { k_w_qb, k_w_k, k_w_k, k_w_wr, k_w_cb, k_w_cb, k_w_ss, k_w_hb, k_w_fb, k_w_te, k_w_lb, k_w_lb, k_w_ol, k_w_ol, k_w_ol, k_w_dt, k_w_de };
/* typical counts of a 45-man 1980 roster per position (the package's means, rounded), for the need */
static const int k_typical[17] = { 3, 1, 1, 5, 4, 2, 2, 4, 2, 3, 3, 3, 2, 3, 4, 3, 4 };
/* position groups of the Big Board tabs */
static const int k_group[17] = { 1, 9, 9, 3, 8, 8, 8, 2, 2, 4, 7, 7, 5, 5, 5, 6, 6 };
static const char *const k_group_name[10] = { "ALL", "QB", "RB", "WR", "TE", "OL", "DL", "LB", "DB", "K/P" };
/* M8's staging minimums (src/nfl2k5_teams.c k_stage_min): never release below these */
static const int k_stage_min[17] = { 1, 1, 0, 2, 2, 1, 0, 1, 0, 1, 2, 1, 1, 2, 2, 1, 2 };

static struct {
    int open, year, alternate, log;
    ErasProspect p[NFL2K5_DRAFT_MAX_PROSPECTS];
    int np;
    ErasDraftSlot s[NFL2K5_DRAFT_MAX_SLOTS];
    int ns, rounds;
    int have[MAXT][17];              /* players per position per team (roster rows + rookies drafted) */
    int have_ok;
    uint32_t seed;
} s_dr;

#define DR_LOG(...) do { if (s_dr.log) fprintf(stderr, "[ERAS] draft " __VA_ARGS__); } while (0)

/* ---- small helpers ----------------------------------------------------------- */

static int overall(int pos_code, const uint8_t *r)
{
    const Wt *w = pos_code >= 0 && pos_code < 17 ? k_weights[pos_code] : k_w_lb;
    long tot = 0, ws = 0;
    for (; w->r >= 0; w++) { tot += (long)r[w->r] * w->w; ws += w->w; }
    return ws ? (int)((tot + ws / 2) / ws) : 0;
}

static uint32_t rnd_next(uint32_t *st) { *st = *st * 1664525u + 1013904223u; return *st >> 8; }

static void field(char **p, char *out, size_t n)
{
    size_t k = 0;
    while (**p && **p != ',' && **p != '\r' && **p != '\n') { if (k + 1 < n) out[k++] = **p; (*p)++; }
    out[k] = 0;
    if (**p == ',') (*p)++;
}

static int ifield(char **p) { char b[16]; field(p, b, sizeof b); return atoi(b); }

static void draft_file(const char *name, char *out, size_t n)
{
    char rel[96];
    snprintf(rel, sizeof rel, "draft\\%d\\%s", s_dr.year, name);
    nfl2k5_eras_league_path(rel, out, (unsigned)n);
}

static int file_exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

int nfl2k5_eras_draft_year(void) { return NFL2K5_ERAS_SEASON + 1; }

/* ---- the class --------------------------------------------------------------- */

static int read_class(const char *path)
{
    FILE *f;
    char line[1024];
    int k;
    if (fopen_s(&f, path, "rb") || !f) return -1;
    s_dr.np = 0;
    fgets(line, sizeof line, f);                       /* header (make_draft_class.py order) */
    while (s_dr.np < NFL2K5_DRAFT_MAX_PROSPECTS && fgets(line, sizeof line, f)) {
        ErasProspect *e = &s_dr.p[s_dr.np];
        char *c = line, team[24], pfr[8], b[16];
        memset(e, 0, sizeof *e);
        e->pick = ifield(&c); e->round = ifield(&c);
        field(&c, team, sizeof team); field(&c, pfr, sizeof pfr);
        field(&c, e->first, sizeof e->first); field(&c, e->last, sizeof e->last); field(&c, e->pos, sizeof e->pos);
        e->pos_code = ifield(&c);
        field(&c, e->college, sizeof e->college);
        e->height = ifield(&c); e->weight = ifield(&c); e->skin = ifield(&c); e->face = ifield(&c); e->hand = ifield(&c);
        e->number = ifield(&c);
        field(&c, e->source, sizeof e->source);
        e->ovr = ifield(&c); e->peak_ovr = ifield(&c);
        field(&c, b, sizeof b); e->peak_season = atoi(b);
        e->years_to_peak = ifield(&c); e->games = ifield(&c); e->av = ifield(&c);
        field(&c, e->flags, sizeof e->flags);
        for (k = 0; k < NR; k++) e->r[k] = (uint8_t)ifield(&c);
        for (k = 0; k < NR; k++) e->p[k] = (uint8_t)ifield(&c);
        e->team = nfl2k5_eras_team_find(team);
        e->sleeper = strstr(e->flags, "sleeper") != NULL;
        e->taken_slot = -1;
        if (e->pick <= 0 || e->round <= 0 || e->pos_code < 0 || e->pos_code > 16) continue;
        s_dr.np++;
    }
    fclose(f);
    return s_dr.np ? 0 : -1;
}

static int write_class(const char *path)
{
    static const char *const k_rn[NR] = { "speed", "agility", "pass_arm_strength", "stamina", "kick_power", "durability", "strength",
        "jumping", "coverage", "run_route", "tackle", "break_tackle", "pass_accuracy", "pass_read_coverage", "catch", "run_blocking",
        "pass_blocking", "hold_onto_ball", "pass_rush", "run_coverage", "kick_accuracy", "kicking_style", "leadership", "power_run_style",
        "composure", "scramble", "consistency", "aggressiveness" };
    FILE *f;
    int i, k;
    if (fopen_s(&f, path, "wb") || !f) return -1;
    fprintf(f, "pick,round,team,pfr_team,first,last,pos,pos_code,college,height,weight,skin,face,hand,number,source,ovr,peak_ovr,peak_season,years_to_peak,games,career_av,flags");
    for (k = 0; k < NR; k++) fprintf(f, ",r_%s", k_rn[k]);
    for (k = 0; k < NR; k++) fprintf(f, ",p_%s", k_rn[k]);
    fputc('\n', f);
    for (i = 0; i < s_dr.np; i++) {
        const ErasProspect *e = &s_dr.p[i];
        const ErasTeam *t = nfl2k5_eras_team(e->team);
        fprintf(f, "%d,%d,%s,,%s,%s,%s,%d,%s,%d,%d,%d,%d,%d,%d,%s,%d,%d,", e->pick, e->round, t ? t->id : "", e->first, e->last, e->pos,
                e->pos_code, e->college, e->height, e->weight, e->skin, e->face, e->hand, e->number, e->source, e->ovr, e->peak_ovr);
        if (e->peak_season) fprintf(f, "%d", e->peak_season);
        fprintf(f, ",%d,%d,%d,%s", e->years_to_peak, e->games, e->av, e->flags);
        for (k = 0; k < NR; k++) fprintf(f, ",%d", e->r[k]);
        for (k = 0; k < NR; k++) fprintf(f, ",%d", e->p[k]);
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

/* The style ratings keep their value; the others scale so the overall lands on target. */
static void scale_to(int pos_code, const uint8_t *from, uint8_t *to, int target)
{
    int base = overall(pos_code, from), k, pass;
    for (k = 0; k < NR; k++) {
        int v = from[k];
        if (k != R_KSTYLE && k != R_PRSTYLE && k != R_SCRAM && base > 0) v = (v * target + base / 2) / base;
        to[k] = (uint8_t)(v < 20 ? 20 : v > 99 ? 99 : v);
    }
    for (pass = 0; pass < 6 && overall(pos_code, to) != target; pass++) {
        const Wt *w = k_weights[pos_code];
        int d = overall(pos_code, to) < target ? 1 : -1;
        for (; w->r >= 0; w++) { int v = to[w->r] + d; to[w->r] = (uint8_t)(v < 20 ? 20 : v > 99 ? 99 : v); }
    }
}

/* Alternate History only: N sleepers from the estimated / late-round pool, seeded per league. */
static void draw_sleepers(int n)
{
    int pool[NFL2K5_DRAFT_MAX_PROSPECTS], np = 0, i, drawn = 0;
    uint32_t st = s_dr.seed ^ 0x5EEDu;
    char line[512];
    size_t o = 0;
    for (i = 0; i < s_dr.np; i++) {
        const ErasProspect *e = &s_dr.p[i];
        if ((!strcmp(e->source, "estimated") || e->round >= 7) && e->pos_code != 1 && e->pos_code != 2) pool[np++] = i;
    }
    line[0] = 0;
    while (drawn < n && np > 0) {
        int k = (int)(rnd_next(&st) % (uint32_t)np), i2 = pool[k];
        ErasProspect *e = &s_dr.p[i2];
        pool[k] = pool[--np];
        e->sleeper = 1;
        if (strlen(e->flags) + 9 < sizeof e->flags) { if (e->flags[0]) strcat(e->flags, " "); strcat(e->flags, "sleeper"); }
        scale_to(e->pos_code, e->r, e->p, SLEEPER_TARGET);
        e->peak_ovr = overall(e->pos_code, e->p);
        e->years_to_peak = 3 + (int)(rnd_next(&st) % 3u);
        e->peak_season = 0;
        if (o + 64 < sizeof line) o += (size_t)snprintf(line + o, sizeof line - o, "%s#%d %s %s (%s, R%d, %d -> %d)", o ? "; " : "",
                                                        e->pick, e->first, e->last, e->pos, e->round, e->ovr, e->peak_ovr);
        drawn++;
    }
    nfl2k5_eras_log("draft %d: %d sleeper(s) drawn (seed %08X, Alternate History, hidden): %s", s_dr.year, drawn, (unsigned)s_dr.seed, line);
}

/* ---- the order ----------------------------------------------------------------- */

static ErasRecord s_rec[MAXT];
static int s_real_rank[MAXT];       /* the team's real first-round slot (tiebreak) */

static int cmp_worst(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    const ErasRecord *x = &s_rec[a], *y = &s_rec[b];
    if (x->w != y->w) return x->w - y->w;
    if (x->l != y->l) return y->l - x->l;
    if (x->pf - x->pa != y->pf - y->pa) return (x->pf - x->pa) - (y->pf - y->pa);
    if (x->pf != y->pf) return x->pf - y->pf;
    return s_real_rank[a] - s_real_rank[b];
}

static void build_order(void)
{
    int i, r, n = nfl2k5_eras_team_count();
    s_dr.ns = 0;
    s_dr.rounds = 0;
    for (i = 0; i < s_dr.np; i++) if (s_dr.p[i].round > s_dr.rounds) s_dr.rounds = s_dr.p[i].round;
    if (!s_dr.alternate) {
        /* the real slots, in pick order (the class is sorted by pick) */
        for (i = 0; i < s_dr.np && s_dr.ns < NFL2K5_DRAFT_MAX_SLOTS; i++) {
            ErasDraftSlot *s = &s_dr.s[s_dr.ns++];
            s->round = s_dr.p[i].round; s->pick = s_dr.p[i].pick; s->team = s_dr.p[i].team; s->prospect = -1; s->by[0] = 0;
        }
        return;
    }
    {
        int order[MAXT], k;
        nfl2k5_eras_standings(s_rec);
        for (i = 0; i < n; i++) { order[i] = i; s_real_rank[i] = 999; }
        for (i = 0; i < s_dr.np; i++)
            if (s_dr.p[i].round == 1 && s_dr.p[i].team >= 0 && s_real_rank[s_dr.p[i].team] == 999) s_real_rank[s_dr.p[i].team] = s_dr.p[i].pick;
        qsort(order, (size_t)n, sizeof order[0], cmp_worst);
        /* every team picks every round, but never more slots than the class has players (the real
         * 1981 draft had 332 picks for 336 slots: forfeits), so the draft can complete */
        for (r = 1; r <= s_dr.rounds; r++)
            for (k = 0; k < n && s_dr.ns < NFL2K5_DRAFT_MAX_SLOTS && s_dr.ns < s_dr.np; k++) {
                ErasDraftSlot *s = &s_dr.s[s_dr.ns++];
                s->round = r; s->pick = s_dr.ns; s->team = order[k]; s->prospect = -1; s->by[0] = 0;
            }
        {
            char line[400];
            size_t o = 0;
            for (k = 0; k < n && o + 16 < sizeof line; k++) o += (size_t)snprintf(line + o, sizeof line - o, "%s%s", k ? " " : "", nfl2k5_eras_team(order[k])->abbr);
            nfl2k5_eras_log("draft %d order (Alternate History: worst record first, ties by losses, point difference, points scored, then the real 1981 order): %s", s_dr.year, line);
        }
    }
}

/* ---- picks.csv ------------------------------------------------------------------ */

static int find_prospect(int pick) { int i; for (i = 0; i < s_dr.np; i++) if (s_dr.p[i].pick == pick) return i; return -1; }

static void read_picks(void)
{
    char path[MAX_PATH], line[256];
    FILE *f;
    int n = 0;
    draft_file("picks.csv", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return;
    fgets(line, sizeof line, f);
    while (fgets(line, sizeof line, f)) {
        char *c = line, by[8], team[24];
        int slot = ifield(&c), round = ifield(&c), pick = ifield(&c), pk, pi;
        field(&c, team, sizeof team);
        pk = ifield(&c);
        field(&c, by, sizeof by);
        (void)round; (void)pick;
        pi = find_prospect(pk);
        if (slot < 0 || slot >= s_dr.ns || pi < 0 || s_dr.p[pi].taken_slot >= 0) continue;
        s_dr.s[slot].prospect = pi;
        snprintf(s_dr.s[slot].by, sizeof s_dr.s[slot].by, "%s", by);
        s_dr.p[pi].taken_slot = slot;
        n++;
    }
    fclose(f);
    DR_LOG("picks.csv: %d pick(s) made so far\n", n);
}

static void append_pick(int slot)
{
    char path[MAX_PATH];
    FILE *f;
    const ErasDraftSlot *s = &s_dr.s[slot];
    const ErasProspect *e = &s_dr.p[s->prospect];
    const ErasTeam *t = nfl2k5_eras_team(s->team);
    int fresh;
    draft_file("picks.csv", path, sizeof path);
    fresh = !file_exists(path);
    if (fopen_s(&f, path, "ab") || !f) { nfl2k5_eras_log("draft: cannot write %s", path); return; }
    if (fresh) fprintf(f, "slot,round,pick,team,prospect_pick,by,first,last,pos,college,ovr,source\n");
    fprintf(f, "%d,%d,%d,%s,%d,%s,%s,%s,%s,%s,%d,%s\n", slot, s->round, s->pick, t ? t->id : "", e->pick, s->by, e->first, e->last,
            e->pos, e->college, e->ovr, e->source);
    fclose(f);
    nfl2k5_eras_log("draft %d: round %d pick %d, %s select %s %s (%s, %s), %s%s", s_dr.year, s->round, s->pick, t ? t->name : "?",
                    e->first, e->last, e->pos, e->college, s->by, e->pick != s->pick ? " (real slot differs)" : "");
}

/* ---- team needs ------------------------------------------------------------------- */

static void count_roster(int team)
{
    char path[MAX_PATH], line[512];
    FILE *f;
    memset(s_dr.have[team], 0, sizeof s_dr.have[team]);
    if (nfl2k5_eras_roster_path(team, path, sizeof path)) return;
    if (fopen_s(&f, path, "rb") || !f) return;
    fgets(line, sizeof line, f);
    while (fgets(line, sizeof line, f)) {
        char *c = line, a[32];
        int pc;
        field(&c, a, sizeof a); field(&c, a, sizeof a);
        pc = ifield(&c);
        if (pc >= 0 && pc < 17) s_dr.have[team][pc]++;
    }
    fclose(f);
}

static void needs_init(void)
{
    int t, i;
    if (s_dr.have_ok) return;
    for (t = 0; t < nfl2k5_eras_team_count(); t++) count_roster(t);
    for (i = 0; i < s_dr.ns; i++)
        if (s_dr.s[i].prospect >= 0 && s_dr.s[i].team >= 0) s_dr.have[s_dr.s[i].team][s_dr.p[s_dr.s[i].prospect].pos_code]++;
    s_dr.have_ok = 1;
}

/* value of a prospect for a team: overall plus the need at his position, minus an early K/P */
static int value_for(int team, int pi, int round, uint32_t *rnd)
{
    const ErasProspect *e = &s_dr.p[pi];
    int need = team >= 0 ? k_typical[e->pos_code] - s_dr.have[team][e->pos_code] : 0;
    int v = e->ovr * 4 + (need > 4 ? 4 : need < -2 ? -2 : need) * 8;
    if ((e->pos_code == 1 || e->pos_code == 2) && round < 6) v -= 40;
    if (rnd) v += (int)(rnd_next(rnd) % 5u) * 4;         /* Alternate History: 0..4 overall points */
    return v;
}

static int best_available(int team, int round, uint32_t *rnd)
{
    int i, best = -1, bv = -1000000;
    for (i = 0; i < s_dr.np; i++) {
        int v;
        if (s_dr.p[i].taken_slot >= 0) continue;
        v = value_for(team, i, round, rnd);
        if (v > bv) { bv = v; best = i; }
    }
    return best;
}

static void make_pick(int slot, int pi, const char *by)
{
    s_dr.s[slot].prospect = pi;
    snprintf(s_dr.s[slot].by, sizeof s_dr.s[slot].by, "%s", by);
    s_dr.p[pi].taken_slot = slot;
    if (s_dr.have_ok && s_dr.s[slot].team >= 0) s_dr.have[s_dr.s[slot].team][s_dr.p[pi].pos_code]++;
    append_pick(slot);
}

/* ---- public: open / query --------------------------------------------------------- */

int nfl2k5_eras_draft_is_open(void) { return s_dr.open; }
void nfl2k5_eras_draft_close(void) { s_dr.open = 0; s_dr.have_ok = 0; }
int nfl2k5_eras_draft_prospect_count(void) { return s_dr.np; }
const ErasProspect *nfl2k5_eras_draft_prospect(int i) { return i >= 0 && i < s_dr.np ? &s_dr.p[i] : NULL; }
int nfl2k5_eras_draft_slot_count(void) { return s_dr.ns; }
const ErasDraftSlot *nfl2k5_eras_draft_slot(int i) { return i >= 0 && i < s_dr.ns ? &s_dr.s[i] : NULL; }
int nfl2k5_eras_draft_round_count(void) { return s_dr.rounds; }
const char *nfl2k5_eras_draft_group_name(int g) { return g >= 0 && g < 10 ? k_group_name[g] : ""; }
int nfl2k5_eras_draft_group_of(int pc) { return pc >= 0 && pc < 17 ? k_group[pc] : 0; }

int nfl2k5_eras_draft_round_first(int round)
{
    int i;
    for (i = 0; i < s_dr.ns; i++) if (s_dr.s[i].round == round) return i;
    return -1;
}

int nfl2k5_eras_draft_next_slot(void)
{
    int i;
    for (i = 0; i < s_dr.ns; i++) if (s_dr.s[i].prospect < 0) return i;
    return -1;
}

int nfl2k5_eras_draft_user_on_clock(void)
{
    int n = nfl2k5_eras_draft_next_slot(), u = nfl2k5_eras_user_team();
    return n >= 0 && u >= 0 && s_dr.s[n].team == u;
}

int nfl2k5_eras_draft_open(char *msg, unsigned n)
{
    char path[MAX_PATH], base[MAX_PATH], rel[64];
    const char *e;
    if (msg) msg[0] = 0;
    if (!nfl2k5_eras_league_loaded()) { if (msg) snprintf(msg, n, "No league"); return -1; }
    e = getenv("NFL2K5_ERAS_LOG"); s_dr.log = e && *e && *e != '0';
    s_dr.year = nfl2k5_eras_draft_year();
    s_dr.alternate = nfl2k5_eras_history();
    s_dr.seed = nfl2k5_eras_league_seed();
    draft_file("class.csv", path, sizeof path);
    if (file_exists(path)) {
        if (read_class(path)) { if (msg) snprintf(msg, n, "Draft class unreadable"); return -1; }
        DR_LOG("class: %d prospects from the league's copy %s\n", s_dr.np, path);
    } else {
        snprintf(rel, sizeof rel, "draft\\class_%d.csv", s_dr.year);
        nfl2k5_eras_path(rel, base, sizeof base);
        if (read_class(base)) {
            if (msg) snprintf(msg, n, "No %d draft class (class_%d.csv)", s_dr.year, s_dr.year);
            nfl2k5_eras_log("draft %d: class missing: %s (python -I tools\\eras\\drafts\\make_draft_class.py)", s_dr.year, base);
            return -1;
        }
        if (s_dr.alternate) draw_sleepers(nfl2k5_eras_sleepers());
        if (write_class(path)) { if (msg) snprintf(msg, n, "Cannot write the league's draft class"); return -1; }
        nfl2k5_eras_log("draft %d opened: %d prospects (%s) copied to %s; %s", s_dr.year, s_dr.np, base, path,
                        s_dr.alternate ? "Alternate History" : "Historical (the real order and picks)");
    }
    build_order();
    read_picks();
    s_dr.have_ok = 0;
    s_dr.open = 1;
    return 0;
}

int nfl2k5_eras_draft_available(int group, int *out, int max)
{
    int i, j, n = 0;
    for (i = 0; i < s_dr.np && n < max; i++) {
        const ErasProspect *e = &s_dr.p[i];
        int key;
        if (e->taken_slot >= 0 || (group && k_group[e->pos_code] != group)) continue;
        key = e->ovr * 1000 - e->pick;                    /* by overall, then the real slot */
        for (j = n; j > 0; j--) {
            const ErasProspect *o = &s_dr.p[out[j - 1]];
            if (o->ovr * 1000 - o->pick >= key) break;
            out[j] = out[j - 1];
        }
        out[j] = i;
        n++;
    }
    return n;
}

/* ---- public: picks ------------------------------------------------------------------ */

int nfl2k5_eras_draft_cpu_pick(void)
{
    int slot = nfl2k5_eras_draft_next_slot(), pi = -1;
    uint32_t rnd;
    const char *by = "cpu";
    if (!s_dr.open || slot < 0) return -1;
    needs_init();
    if (!s_dr.alternate) {
        /* Historical: the real pick of this slot while he is available */
        pi = find_prospect(s_dr.s[slot].pick);
        if (pi >= 0 && s_dr.p[pi].taken_slot < 0) by = "real"; else pi = -1;
    }
    if (pi < 0) {
        rnd = s_dr.seed ^ (uint32_t)(slot * 2654435761u);
        pi = best_available(s_dr.s[slot].team, s_dr.s[slot].round, s_dr.alternate ? &rnd : NULL);
    }
    if (pi < 0) return -1;
    make_pick(slot, pi, by);
    return slot;
}

int nfl2k5_eras_draft_user_pick(int prospect)
{
    int slot = nfl2k5_eras_draft_next_slot();
    if (!s_dr.open || slot < 0 || !nfl2k5_eras_draft_user_on_clock()) return -1;
    if (prospect < 0 || prospect >= s_dr.np || s_dr.p[prospect].taken_slot >= 0) return -2;
    needs_init();
    make_pick(slot, prospect, "user");
    return 0;
}

int nfl2k5_eras_draft_sim(int *out, int max)
{
    int n = 0;
    while (!nfl2k5_eras_draft_user_on_clock()) {
        int slot = nfl2k5_eras_draft_cpu_pick();
        if (slot < 0) break;
        if (n < max) out[n] = slot;
        n++;
    }
    return n;
}

/* ---- rookies into the roster files --------------------------------------------------- */

typedef struct { char line[640]; int pos, number, ovr, rookie, starter, keep; char first[32], last[32]; } Row;
typedef struct { int row, pos, num, rank, side; char first[32], last[32]; } DRow;

static int parse_row(Row *r)
{
    char *c = r->line, a[32];
    int k;
    uint8_t rt[NR];
    field(&c, r->first, sizeof r->first); field(&c, r->last, sizeof r->last);
    r->pos = ifield(&c); r->number = ifield(&c);
    for (k = 0; k < 6; k++) field(&c, a, sizeof a);     /* height weight years skin face hand */
    for (k = 0; k < NR; k++) rt[k] = (uint8_t)ifield(&c);
    r->ovr = overall(r->pos, rt);
    return r->pos >= 0 && r->pos <= 16;
}

static int number_free(const Row *rows, int n, int num)
{
    int i;
    if (num <= 0 || num > 99) return 0;
    for (i = 0; i < n; i++) if (rows[i].keep && rows[i].number == num) return 0;
    return 1;
}

static int assign_number(const Row *rows, int n, int pos, int want)
{
    static const struct { int lo, hi; } k_range[17][2] = {
        {{1,19},{1,19}}, {{1,19},{1,19}}, {{1,19},{1,19}}, {{80,89},{10,19}}, {{20,49},{20,49}}, {{20,49},{20,49}}, {{20,49},{20,49}},
        {{20,49},{20,49}}, {{30,49},{20,49}}, {{80,89},{40,49}}, {{50,59},{90,99}}, {{50,59},{90,99}}, {{60,79},{50,59}},
        {{60,79},{50,59}}, {{60,79},{50,59}}, {{60,79},{90,99}}, {{60,79},{90,99}} };
    int r, v;
    if (number_free(rows, n, want)) return want;
    for (r = 0; r < 2; r++)
        for (v = k_range[pos][r].lo; v <= k_range[pos][r].hi; v++) if (number_free(rows, n, v)) return v;
    for (v = 1; v <= 99; v++) if (number_free(rows, n, v)) return v;
    return want > 0 ? want : 99;
}

static int read_depth_rows(const char *path, DRow *d, int max, int *special, int *has_special)
{
    FILE *f;
    char line[256];
    int n = 0;
    *has_special = 0;
    if (fopen_s(&f, path, "rb") || !f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *c = line, a[32];
        if (line[0] == '#' || !strncmp(line, "row,", 4)) continue;
        if (!strncmp(line, "special,", 8)) {
            int i;
            c += 8;
            for (i = 0; i < 6; i++) special[i] = ifield(&c);
            *has_special = 1;
            continue;
        }
        if (n >= max) break;
        d[n].row = ifield(&c);
        field(&c, d[n].first, sizeof d[n].first); field(&c, d[n].last, sizeof d[n].last);
        d[n].pos = ifield(&c); d[n].num = ifield(&c); d[n].rank = ifield(&c) & 7;
        field(&c, a, sizeof a); d[n].side = atoi(a) & 7;
        n++;
    }
    fclose(f);
    return n;
}

static int write_team_rosters(int team, FILE *rel_log)
{
    static Row rows[ROSTER_MAX + 40];
    static DRow depth[80];
    const ErasTeam *t = nfl2k5_eras_team(team);
    char path[MAX_PATH], dpath[MAX_PATH], out[MAX_PATH], relp[96], header[640];
    FILE *f;
    int n = 0, nd, special[6], has_special, i, k, rookies = 0, released = 0, kept, pos_n[17], map[ROSTER_MAX + 40];
    int season = s_dr.year;
    if (!t) return -1;
    /* the season's roster rows (the package's: nfl2k5_eras_roster_path for the package season) */
    snprintf(path, sizeof path, "%s\\%s", nfl2k5_eras_package_dir(), t->roster_file);
    for (k = 0; path[k]; k++) if (path[k] == '/') path[k] = '\\';
    if (fopen_s(&f, path, "rb") || !f) { nfl2k5_eras_log("draft rookies %s: no roster %s", t->id, path); return -1; }
    if (!fgets(header, sizeof header, f)) { fclose(f); return -1; }
    while (n < ROSTER_MAX + 40 && fgets(rows[n].line, sizeof rows[n].line, f)) {
        rows[n].line[strcspn(rows[n].line, "\r\n")] = 0;
        if (!rows[n].line[0]) continue;
        rows[n].rookie = 0; rows[n].starter = 0; rows[n].keep = 1;
        if (parse_row(&rows[n])) n++;
    }
    fclose(f);
    /* the depth chart that applies (the league's edit, else the package seed): starters are rank 0 */
    {
        char lrel[64];
        snprintf(lrel, sizeof lrel, "depth\\%s.csv", t->id);
        nfl2k5_eras_league_path(lrel, dpath, sizeof dpath);
        if (!file_exists(dpath)) { snprintf(dpath, sizeof dpath, "%s\\depth\\%s.csv", nfl2k5_eras_package_dir(), t->id); for (k = 0; dpath[k]; k++) if (dpath[k] == '/') dpath[k] = '\\'; }
    }
    nd = read_depth_rows(dpath, depth, 80, special, &has_special);
    for (i = 0; i < nd; i++)
        for (k = 0; k < n; k++)
            if (!strcmp(rows[k].first, depth[i].first) && !strcmp(rows[k].last, depth[i].last) && rows[k].pos == depth[i].pos) {
                rows[k].starter |= depth[i].rank == 0 || (depth[i].side == 0 && (depth[i].pos == 4 || depth[i].pos == 3 || depth[i].pos >= 10));
                break;
            }
    /* the rookies, after the last player of their position */
    for (i = 0; i < s_dr.ns; i++) {
        const ErasProspect *e;
        int at, num;
        Row *r;
        if (s_dr.s[i].team != team || s_dr.s[i].prospect < 0 || n >= ROSTER_MAX + 40) continue;
        e = &s_dr.p[s_dr.s[i].prospect];
        for (at = n; at > 0 && rows[at - 1].pos > e->pos_code; at--) {}
        for (k = at; k < n && rows[k].pos == e->pos_code; k++) {}
        at = k;
        memmove(&rows[at + 1], &rows[at], (size_t)(n - at) * sizeof rows[0]);
        r = &rows[at];
        memset(r, 0, sizeof *r);
        num = assign_number(rows, n + 1, e->pos_code, e->number);
        snprintf(r->first, sizeof r->first, "%s", e->first); snprintf(r->last, sizeof r->last, "%s", e->last);
        r->pos = e->pos_code; r->number = num; r->ovr = e->ovr; r->rookie = 1; r->keep = 1;
        {
            size_t o = (size_t)snprintf(r->line, sizeof r->line, "%s,%s,%d,%d,%d,%d,0,%d,%d,%d", e->first, e->last, e->pos_code, num,
                                        e->height, e->weight, e->skin, e->face, e->hand);
            for (k = 0; k < NR; k++) o += (size_t)snprintf(r->line + o, sizeof r->line - o, ",%d", e->r[k]);
            snprintf(r->line + o, sizeof r->line - o, ",39999");
        }
        n++;
        rookies++;
        if (num != e->number) nfl2k5_eras_log("draft rookies %s: %s %s (%s) gets jersey %d (%s)", t->id, e->first, e->last, e->pos, num,
                                               e->number ? "his record's number is taken" : "no number in the class");
    }
    /* within 53: release the lowest-rated players who are not starters and not needed for the staging minimums */
    memset(pos_n, 0, sizeof pos_n);
    for (k = 0; k < n; k++) pos_n[rows[k].pos]++;
    kept = n;
    while (kept > ROSTER_MAX) {
        int worst = -1;
        for (k = 0; k < n; k++) {
            if (!rows[k].keep || rows[k].rookie || rows[k].starter || pos_n[rows[k].pos] <= k_stage_min[rows[k].pos]) continue;
            if (worst < 0 || rows[k].ovr < rows[worst].ovr) worst = k;
        }
        if (worst < 0) {       /* no non-starter left: the lowest-rated non-starter rookie goes instead */
            for (k = 0; k < n; k++) {
                if (!rows[k].keep || !rows[k].rookie || pos_n[rows[k].pos] <= k_stage_min[rows[k].pos]) continue;
                if (worst < 0 || rows[k].ovr < rows[worst].ovr) worst = k;
            }
            if (worst < 0) break;
        }
        rows[worst].keep = 0;
        pos_n[rows[worst].pos]--;
        kept--;
        released++;
        nfl2k5_eras_log("draft rookies %s: released %s %s (%s #%d, overall %d, not a starter) to stay at %d", t->id, rows[worst].first,
                        rows[worst].last, k_pos[rows[worst].pos], rows[worst].number, rows[worst].ovr, ROSTER_MAX);
        if (rel_log) fprintf(rel_log, "%s,%s,%s,%s,%d,%d,%s\n", t->id, rows[worst].first, rows[worst].last, k_pos[rows[worst].pos],
                             rows[worst].number, rows[worst].ovr, rows[worst].rookie ? "rookie" : "veteran not a starter");
    }
    if (kept < ROSTER_MIN) nfl2k5_eras_log("draft rookies %s: WARNING %d players, below the staging minimum %d", t->id, kept, ROSTER_MIN);
    /* rosters\<year>\<id>.csv and its flags */
    snprintf(relp, sizeof relp, "rosters\\%d\\%s.csv", season, t->id);
    nfl2k5_eras_league_path(relp, out, sizeof out);
    if (fopen_s(&f, out, "wb") || !f) { nfl2k5_eras_log("draft rookies %s: cannot write %s", t->id, out); return -1; }
    fputs(header, f);
    if (header[strlen(header) - 1] != '\n') fputc('\n', f);
    for (k = 0, i = 0; k < n; k++) { map[k] = rows[k].keep ? i : -1; if (rows[k].keep) { fprintf(f, "%s\n", rows[k].line); i++; } }
    fclose(f);
    snprintf(relp, sizeof relp, "rosters\\%d\\%s.flags.txt", season, t->id);
    nfl2k5_eras_league_path(relp, out, sizeof out);
    if (!fopen_s(&f, out, "wb") && f) {
        fprintf(f, "%s.csv: the %d roster (%s) plus the %d draft class rookies drafted by this league (draft\\%d\\picks.csv); ratings: community PS2 roster (unverified); estimated picks flagged.\n",
                t->id, NFL2K5_ERAS_SEASON, path, s_dr.year, s_dr.year);
        for (k = 0, i = 0; k < n; k++) {
            if (!rows[k].keep) continue;
            if (rows[k].rookie) {
                int j;
                for (j = 0; j < s_dr.ns; j++)
                    if (s_dr.s[j].team == team && s_dr.s[j].prospect >= 0 && !strcmp(s_dr.p[s_dr.s[j].prospect].first, rows[k].first) &&
                        !strcmp(s_dr.p[s_dr.s[j].prospect].last, rows[k].last)) break;
                if (j < s_dr.ns) {
                    const ErasProspect *e = &s_dr.p[s_dr.s[j].prospect];
                    char fl[72], *sp;
                    snprintf(fl, sizeof fl, "%s", e->flags);
                    if ((sp = strstr(fl, "sleeper")) != NULL) memmove(sp, sp + 7, strlen(sp + 7) + 1);   /* hidden potential stays hidden */
                    fprintf(f, "row %d: %s %s (%s #%d): ROOKIE, %d draft round %d pick %d (%s), %s, ratings %s%s%s\n", i, e->first, e->last, e->pos,
                            rows[k].number, s_dr.year, s_dr.s[j].round, s_dr.s[j].pick, s_dr.s[j].by, e->college, e->source,
                            fl[0] && fl[0] != ' ' ? ", flags: " : "", fl);
                }
            }
            i++;
        }
        fclose(f);
    }
    /* depth\<year>\<id>.csv: the veterans keep rank / side, rookies rank after them at their position */
    snprintf(relp, sizeof relp, "depth\\%d\\%s.csv", season, t->id);
    nfl2k5_eras_league_path(relp, out, sizeof out);
    if (!fopen_s(&f, out, "wb") && f) {
        int next_rank[17];
        for (k = 0; k < 17; k++) next_rank[k] = 0;
        for (i = 0; i < nd; i++) if (depth[i].rank + 1 > next_rank[depth[i].pos] && depth[i].pos >= 0 && depth[i].pos < 17) next_rank[depth[i].pos] = depth[i].rank + 1;
        fprintf(f, "# My NFL Eras depth chart of %s %s for %d: the %d depth with the draft's rookies behind the veterans\n", t->city, t->name, season, NFL2K5_ERAS_SEASON);
        fprintf(f, "row,first,last,pos,number,rank,side\n");
        for (k = 0; k < n; k++) {
            int rank = -1, side = 0;
            if (!rows[k].keep) continue;
            for (i = 0; i < nd; i++)
                if (!strcmp(rows[k].first, depth[i].first) && !strcmp(rows[k].last, depth[i].last) && rows[k].pos == depth[i].pos) { rank = depth[i].rank; side = depth[i].side; break; }
            if (rank < 0) {
                rank = next_rank[rows[k].pos] > 7 ? 7 : next_rank[rows[k].pos]++;
                side = rank == 0 ? 2 : rank == 1 ? 0 : rank == 2 ? 1 : rank;
            }
            fprintf(f, "%d,%s,%s,%d,%d,%d,%d\n", map[k], rows[k].first, rows[k].last, rows[k].pos, rows[k].number, rank, side);
        }
        if (has_special) {
            /* the special-teams rows renumbered (a released holder becomes 255: the staging fix picks again) */
            fprintf(f, "special");
            for (i = 0; i < 6; i++) {
                int v = special[i], nv = 255;
                if (v >= 0 && v < n && rows[v].keep) nv = map[v];
                else if (v >= 0 && v < nd) {                 /* the seed's row numbering: find him by name */
                    for (k = 0; k < n; k++) if (rows[k].keep && !strcmp(rows[k].first, depth[v].first) && !strcmp(rows[k].last, depth[v].last)) { nv = map[k]; break; }
                }
                fprintf(f, ",%d", nv);
            }
            fputc('\n', f);
        }
        fclose(f);
    }
    nfl2k5_eras_log("draft rookies %s: %d rookies joined, %d released, %d players -> %s", t->id, rookies, released, kept, relp);
    return kept;
}

int nfl2k5_eras_draft_rosters_written(void)
{
    char path[MAX_PATH];
    if (!s_dr.open) return 0;
    draft_file("rosters_written", path, sizeof path);
    return file_exists(path);
}

int nfl2k5_eras_draft_write_rosters(void)
{
    char path[MAX_PATH];
    FILE *f, *rel;
    int t, n = 0, fail = 0;
    if (!s_dr.open || nfl2k5_eras_draft_next_slot() >= 0) return -1;
    if (nfl2k5_eras_draft_rosters_written()) return nfl2k5_eras_team_count();
    draft_file("releases.log", path, sizeof path);
    rel = NULL;
    if (!fopen_s(&rel, path, "wb") && rel) fprintf(rel, "team,first,last,pos,number,ovr,why\n");
    for (t = 0; t < nfl2k5_eras_team_count(); t++) {
        if (write_team_rosters(t, rel) < 0) fail++; else n++;
    }
    if (rel) fclose(rel);
    if (!fail) {
        draft_file("rosters_written", path, sizeof path);
        if (!fopen_s(&f, path, "wb") && f) { fprintf(f, "%d teams\n", n); fclose(f); }
    }
    nfl2k5_eras_log("draft %d complete: %d season roster files written under the league (rosters\\%d, depth\\%d), %d failed; the %d package is unchanged",
                    s_dr.year, n, s_dr.year, s_dr.year, fail, NFL2K5_ERAS_SEASON);
    return n;
}

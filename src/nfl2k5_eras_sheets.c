/* My NFL Eras: copies of Franchise's own Schedule screen, and Standings on the
 * same retail sheet engine (docs/eras/research/FRANCHISE-SCREEN-COPY.md option
 * (c); results in docs/eras/results/M7-schedule.md).
 *
 * Schedule: a copy of the Franchise Schedule descriptor 0x522828 with its own
 * event table. msgs 1/2/3/4/5/6 are served here (msg 5 and 6 call the retail
 * sub_003275C0 / sub_003276C0 inside), msg 8 is the retail sub_00327690. The
 * page table is Eras-owned: {own init, retail leave sub_0024C440, own frame,
 * retail draw sub_0024C480}. The two sheet definitions are copies of
 * 0x537958 (Team Schedule) and 0x537858 (Weekly Schedule) with only the A
 * activation (+0xC4 / +0xDC) repointed; the retail pages, columns, getters and
 * draw code are used unchanged.
 *
 * Data: every retail getter reads the Franchise season block
 * 0xE576A0..0xE5FC00 at fixed addresses. While one of these screens is on top
 * (msg 1/3 .. msg 5/2) the block is a snapshot-and-fill: the 0x8560 bytes and
 * [0xA84494] are saved, the block is written in Franchise's layout from
 * schedule.csv / results.csv, and both are put back byte for byte when the
 * screen is covered or destroyed (hash checked, logged). The 34 team pointers
 * at 0xE5786C point at 28 Eras-owned team records (copies of the club that
 * owns the historic template's asset code, with the 1980 city, nickname and
 * abbreviation, and the season's uniform style from teams.csv, M9, else the
 * template's helmet variant), so no arena record and no
 * club is changed. mini.iff is loaded under ctx FRMINI in an Eras slot, unless
 * a FRMINI is already resident (then it is used and left alone).
 *
 * Never called: START / Y / SportsCenter, sub_0024CD20 (sim and advance),
 * sub_002486F0, sub_0024BF80, sub_000C67E0, the desk package loader
 * sub_00142D00, sub_0013EC80 ([0xE3C0A0]). The Franchise save is never read
 * or written.
 *
 * Standings: the retail Team Standings 0x54CEA0 is a custom-drawn screen with
 * a fixed 4 x 4 division layout (0xAE92C0 table); 1980 has 3 divisions of 5/4/5
 * per conference, which does not fit. So Standings is built the other way the
 * plan allows: the same descriptor copy and the same retail sheet engine, with
 * an Eras sheet (tabs = divisions, or conferences on the second page), an Eras
 * page and Eras text columns beside the retail helmet column 0x536E00. The
 * helmet column reads the away team of grid row (tab value) slot (row), so the
 * standings order is written into the unused grid rows 16..21 of the filled
 * block (type 0 cells: no score, no effect on the W/L/T sums). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_sheets.h"
#include "nfl2k5_eras_playoffs.h"
#include "nfl2k5_eras_awards.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern recomp_func_t recomp_lookup(uint32_t xbox_va);

#define SH_BASE         NFL2K5_ERAS_SHEETS_BASE
#define RET_ADDR        (SH_BASE + 0x7FF0u)

/* retail data */
#define DESC_SCHEDULE   0x00522828u   /* Franchise Schedule (handler 0xF3DA0, flags 0x95) */
#define SHEET_WEEK      0x00537858u
#define SHEET_TEAM      0x00537958u
#define PAGE_WEEK       0x00537640u
#define COL_HELMET_AWAY 0x00536E00u   /* type 0xA, draw sub_0024BBE0: away helmet of (tab value, row) */
#define COL_HELMET_HOME 0x00537010u   /* type 0xA, draw sub_0024BDA0: home helmet + score (M14 bracket) */
#define COL_TEXT_TMPL   0x00536EB0u   /* type 1 "AWAY" */
#define FN_MSG8         0x00327690u
#define FN_PAGE_LEAVE   0x0024C440u
#define FN_PAGE_DRAW    0x0024C480u
#define W_TITLE_EMPTY   0x00EA3E60u
#define W_FOR_TEAM      0x00E8AD88u   /* L"for Team Schedule" */
#define W_FOR_WEEKLY    0x00E8ADACu   /* L"for Weekly Schedule" */
#define W_EMPTY         0x00E89DB4u

/* the Franchise season block and the screen's statics */
#define BLOCK           0x00E576A0u
#define BLOCK_LEN       0x8560u
#define B_MODE          0x00E576A0u
#define B_STAGE         0x00E576A4u
#define B_TEAMS         0x00E576ACu
#define B_WEEKS         0x00E576B0u
#define B_WEEK          0x00E576B4u
#define B_YEAR          0x00E576B8u
#define B_SELWEEK       0x00E576C0u
#define B_SELTEAM       0x00E576CCu
#define B_DIV           0x00E576D4u
#define B_USER          0x00E5775Cu
#define B_TEAMPTR       0x00E5786Cu
#define B_FLAGS         0x00E57954u
#define B_GRID          0x00E57C40u
#define B_SCORES        0x00E587F0u
#define B_ONES          0x00E5A240u
#define UI_TEAM         0x00A84494u
#define DIRTY_OPTIONS   0x00AA26B0u
#define WIDGET          0x00C177F0u
#define SHEET_ALIVE     0x00C178F8u
#define PAGE_MODE       0x00C177ECu
#define SHEET_RESTORE   0x00C177E8u
#define SHEET_C17904    0x00C17904u
#define UI_STATICS      0x00C95DE8u   /* ..0xC95E20: page index, page table, title, buttons */
#define UI_STATICS_LEN  0x38u
#define S_PAGE_PREV     0x00C95DE8u
#define S_PAGE          0x00C95DECu
#define S_TITLE         0x00C95E00u
#define S_PAGES         0x00C95E04u
#define S_PAGE_N        0x00C95E08u
#define S_BTN_BUSY      0x00C95E0Cu
#define S_E10           0x00C95E10u
#define S_E14           0x00C95E14u
#define S_BUTTONS       0x00C95E18u
#define ROOT            0x00B72918u
#define TEAM_SIZE       0x1F4u
#define FOURCC_TXTR     0x52545854u

#define GRID_ROWS       22
#define GRID_SLOTS      17
#define PSEUDO_ROW0     16            /* standings rows: 16..21 (unused by a 16-week season) */
#define MAX_PSEUDO      6
#define STR_CH          48
#define NCOLS           7             /* the Standings columns */
#define MAXCOL          8             /* columns per page */

/* M14: the pseudo-row pages. Each page has its own sheet copy (tabs -> FN_TABVAL.. with the page
 * current), page record (rows FN_ROWS, its text columns beside the retail helmet columns) and a
 * block of grid rows 16..21 whose "away" team feeds the retail helmet column. */
enum { SC_SCHED, SC_STAND, SC_PLAYOFF, SC_AWARDS, SC_N };
enum { PG_STAND_DIV, PG_STAND_CONF, PG_PICTURE, PG_BRACKET, PG_AWARDS, PG_LEADERS, PG_N };
enum {
    FN_MSG1, FN_MSG2, FN_MSG3, FN_MSG4, FN_MSG5, FN_MSG6, FN_MSGS,          /* per screen: SC * FN_MSGS + .. */
    FN_SCHED_TEAM_INIT = SC_N * FN_MSGS, FN_SCHED_WEEK_INIT, FN_SCHED_FRAME,
    FN_PG_INIT0, FN_PG_INIT_END = FN_PG_INIT0 + PG_N,                        /* one init per pseudo page */
    FN_STAND_FRAME = FN_PG_INIT_END, FN_STAND_DRAW,
    FN_ACT, FN_TABVAL, FN_TABCOUNT, FN_TABNEXT, FN_TABPREV, FN_ROWS,
    FN_COL0, FN_COUNT = FN_COL0 + PG_N * MAXCOL                              /* column getters: page * MAXCOL + col */
};
enum { COL_TEAM, COL_W, COL_L, COL_T, COL_PCT, COL_PF, COL_PA };

typedef struct {
    int screen, ncols, ntabs, tab, row0, single;   /* single: every tab fills the same pseudo row (leaders) */
    const char *banner, *help;
    uint32_t sheet, page, col[MAXCOL], out[MAXCOL][GRID_SLOTS], w_help;
} Pg;

static const struct { int pg; const char *h; float w; uint32_t align; } k_pgcols[] = {
    { PG_PICTURE, "SEED", 44.0f, 3 }, { PG_PICTURE, "TEAM", 150.0f, 1 }, { PG_PICTURE, "W", 34.0f, 3 }, { PG_PICTURE, "L", 34.0f, 3 },
    { PG_PICTURE, "T", 34.0f, 3 }, { PG_PICTURE, "CLINCHED", 140.0f, 1 },
    { PG_BRACKET, "MATCHUP", 230.0f, 1 }, { PG_BRACKET, "DATE", 84.0f, 3 }, { PG_BRACKET, "RESULT", 110.0f, 1 },
    { PG_AWARDS, "AWARD", 170.0f, 1 }, { PG_AWARDS, "PLAYER", 140.0f, 1 }, { PG_AWARDS, "TEAM", 90.0f, 1 }, { PG_AWARDS, "SEASON", 170.0f, 1 },
    { PG_LEADERS, "#", 30.0f, 3 }, { PG_LEADERS, "PLAYER", 150.0f, 1 }, { PG_LEADERS, "TEAM", 100.0f, 1 }, { PG_LEADERS, "POS", 44.0f, 3 },
    { PG_LEADERS, "TOTAL", 70.0f, 3 },
};
static const char *const k_posname[17] = { "QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE" };

static struct {
    int built, log, mode;
    int sched_on, stand_on;
    uint32_t mem, used, size;
    uint32_t desc[SC_N], pages[SC_N];
    uint32_t sheet_week, sheet_team, sheet_stand, page_stand;
    uint32_t col[NCOLS], hdr[NCOLS];
    uint32_t out[NCOLS][GRID_SLOTS];
    Pg pg[PG_N];                      /* M14 */
    int cur_pg;                       /* the pseudo page on the sheet now */
    const void *pdata[MAX_PSEUDO][GRID_SLOTS];   /* M14: the award / leader behind a pseudo row cell */
    int pgame[MAX_PSEUDO][GRID_SLOTS];           /* M14: the bracket's game index per cell (-1) */
    ErasSeed seeds[2][GRID_SLOTS];
    int nseeds[2];
    uint32_t w_pgtitle, w_pgright;
    uint32_t rec[NFL2K5_ERAS_MAX_TEAMS], wcity[NFL2K5_ERAS_MAX_TEAMS], wnick[NFL2K5_ERAS_MAX_TEAMS], wabbr[NFL2K5_ERAS_MAX_TEAMS];
    uint32_t txt, wbuf, w_ctx_mini, w_file_mini, w_unknown, mini_slot;
    uint32_t w_for_conf, w_for_div, w_banner, w_tab[MAX_PSEUDO + 2 + SC_N];
    int mini_mine;
    /* the swap */
    int swapped, cycles, statics_saved;
    uint32_t hash_pre;
    uint8_t snap[BLOCK_LEN];
    uint32_t snap_ui;
    uint8_t statics[UI_STATICS_LEN];
    /* standings */
    int screen;                       /* the screen whose block fill is live */
    int stand_page;                   /* 0 division, 1 conference */
    int tab_div, tab_conf;
    int ndiv;
    const char *div_name[MAX_PSEUDO];
    int div_conf[MAX_PSEUDO];
    int prow_n[MAX_PSEUDO];
    int prow[MAX_PSEUDO][GRID_SLOTS];
    int focus;                        /* team index the screens open on */
} s;

static RECOMP_TLS uint32_t s_called;

#define SH_LOG(...) do { if (s.log) fprintf(stderr, "[ERAS] sheets " __VA_ARGS__); } while (0)

/* ---- guest helpers ------------------------------------------------------- */

static uint32_t alloc(uint32_t n, uint32_t align)
{
    uint32_t p = (s.mem + s.used + align - 1u) & ~(align - 1u), i;
    if (!s.mem || p + n > s.mem + s.size) return 0;
    s.used = p + n - s.mem;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static void wput(uint32_t p, const char *t, uint32_t max)
{
    uint32_t i;
    if (!p) return;
    for (i = 0; i + 1 < max && t[i]; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)t[i];
    MEM16(p + 2u * i) = 0;
}

static uint32_t wstr(const char *t)
{
    uint32_t n = (uint32_t)strlen(t) + 1u, p = alloc(2u * n + 2u, 4);
    wput(p, t, n);
    return p;
}

static void gcopy(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4) MEM32(dst + i) = MEM32(src + i);
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, RET_ADDR);
    fn();
    return g_eax;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t fn_addr(int id) { return SH_BASE + (uint32_t)id * 4u; }

static uint32_t hash_block(void)
{
    const uint8_t *p = (const uint8_t *)XBOX_PTR(BLOCK);
    uint32_t h = 2166136261u, i, ui = MEM32(UI_TEAM);
    for (i = 0; i < BLOCK_LEN; i++) { h ^= p[i]; h *= 16777619u; }
    for (i = 0; i < 4; i++) { h ^= (ui >> (8 * i)) & 0xFFu; h *= 16777619u; }
    return h;
}

/* NFL2K5_ERAS_SHEET_DUMP=<prefix>: <prefix>-pre.bin (the block, [0xA84494],
 * [0xAA26B0]) at the first swap of the process, <prefix>-post.bin after every
 * restore. The no-leak test compares the two. */
static void dump(const char *which)
{
    const char *pfx = getenv("NFL2K5_ERAS_SHEET_DUMP");
    char path[MAX_PATH];
    FILE *f;
    uint32_t tail[2];
    if (!pfx || !*pfx) return;
    snprintf(path, sizeof path, "%s-%s.bin", pfx, which);
    if (fopen_s(&f, path, "wb") || !f) return;
    fwrite((void *)XBOX_PTR(BLOCK), 1, BLOCK_LEN, f);
    tail[0] = MEM32(UI_TEAM); tail[1] = MEM32(DIRTY_OPTIONS);
    fwrite(tail, 4, 2, f);
    fclose(f);
}

/* ---- Eras team records --------------------------------------------------- */

/* The resident club whose asset code (+0x10C, UTF-16) equals the historic
 * entry's inline code (entry + 4). */
static uint32_t club_for_code(uint32_t code)
{
    uint32_t r = MEM32(ROOT), t, cnt, i, j;
    if (!r) return 0;
    t = MEM32(r + 0x1C); cnt = MEM32(r + 0x18);
    for (i = 0; i < cnt && i < 128; i++, t += TEAM_SIZE) {
        uint32_t a = MEM32(t + 0x10C), cat = MEM32(t + 0x128);
        if (!a || cat == 2 || cat == 4 || MEM16(t + 0x118) >= 32) continue;
        for (j = 0; j < 4 && MEM16(a + 2 * j) == MEM16(code + 2 * j) && MEM16(a + 2 * j); j++) {}
        if (j < 4 && MEM16(a + 2 * j) == 0 && MEM16(code + 2 * j) == 0) return t;
    }
    return 0;
}

/* Rebuilt at every swap (a roster reload would leave copies stale). */
static int build_records(void)
{
    uint32_t r = MEM32(ROOT);
    int n = nfl2k5_eras_team_count(), i, made = 0;
    if (!r) return -1;
    for (i = 0; i < n && i < NFL2K5_ERAS_MAX_TEAMS; i++) {
        const ErasTeam *e = nfl2k5_eras_team(i);
        uint32_t club = 0, variant = 0, ent;
        if (e->template_entry >= 0 && (uint32_t)e->template_entry < MEM32(r + 0x58)) {
            ent = MEM32(r + 0x5C) + (uint32_t)e->template_entry * 16u;
            variant = MEM8(ent + 2);
            club = club_for_code(ent + 4);
        }
        if (e->style >= 0) variant = (uint32_t)e->style;   /* M9: the season's uniform style (teams.csv style) */
        if (!club) club = MEM32(r + 0x1C);      /* never expected; the first record keeps the screen alive */
        gcopy(s.rec[i], club, TEAM_SIZE);
        wput(s.wcity[i], e->city, 32);
        wput(s.wnick[i], e->name, 32);
        wput(s.wabbr[i], e->abbr, 8);
        MEM32(s.rec[i] + 0x138) = s.wcity[i];
        MEM32(s.rec[i] + 0x104) = s.wnick[i];
        MEM32(s.rec[i] + 0x108) = s.wabbr[i];
        MEM8(s.rec[i] + 0x192) = (uint8_t)variant;   /* mini_a<code>_<variant>, else _0 (retail fallback); M9: the season's style */
        made++;
    }
    return made;
}

/* ---- the fill -------------------------------------------------------------- */

static int division_index(const char *div)
{
    int i;
    for (i = 0; i < s.ndiv; i++) if (!strcmp(s.div_name[i], div)) return i;
    return 0;
}

static int focus_team(void)
{
    int u = nfl2k5_eras_user_team(), g;
    if (u >= 0) return u;
    g = nfl2k5_eras_next_game();
    return g >= 0 ? nfl2k5_eras_game(g)->home : 0;
}

static ErasRecord s_rec_now[NFL2K5_ERAS_MAX_TEAMS];

static int cmp_team(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    const ErasRecord *x = &s_rec_now[a], *y = &s_rec_now[b];
    int gx = x->w + x->l + x->t, gy = y->w + y->l + y->t;
    /* percentage first (ties count half), then wins, losses, point difference, points, name */
    long px = gx ? (long)((x->w * 2 + x->t) * 1000L / (2L * gx)) : 0, py = gy ? (long)((y->w * 2 + y->t) * 1000L / (2L * gy)) : 0;
    if (px != py) return py > px ? 1 : -1;
    if (x->w != y->w) return y->w - x->w;
    if (x->l != y->l) return x->l - y->l;
    if (x->pf - x->pa != y->pf - y->pa) return (y->pf - y->pa) - (x->pf - x->pa);
    if (x->pf != y->pf) return y->pf - x->pf;
    return strcmp(nfl2k5_eras_team(a)->name, nfl2k5_eras_team(b)->name);
}

static void pseudo_clear(int k)
{
    uint32_t c = B_GRID + 8u * (uint32_t)((PSEUDO_ROW0 + k) * GRID_SLOTS), j;
    for (j = 0; j < 8u * GRID_SLOTS; j += 4) MEM32(c + j) = 0;
    for (j = 0; j < 2u * GRID_SLOTS; j += 2) MEM16(B_FLAGS + 2u * (uint32_t)((PSEUDO_ROW0 + k) * GRID_SLOTS) + j) = 0;
    for (j = 0; j < GRID_SLOTS; j++) { s.pdata[k][j] = NULL; s.pgame[k][j] = -1; }
    s.prow_n[k] = 0;
}

/* a pseudo row's cells: the team in both halves (type 0: helmet only, never a score) and the end mark */
static void pseudo_cells(int k)
{
    int i;
    for (i = 0; i < s.prow_n[k]; i++) {
        uint32_t cell = (uint32_t)((PSEUDO_ROW0 + k) * GRID_SLOTS + i);
        int g = s.pgame[k][i];
        MEM8(B_GRID + 8u * cell + 0) = 0;
        MEM8(B_GRID + 8u * cell + 1) = (uint8_t)s.prow[k][i];
        MEM8(B_GRID + 8u * cell + 2) = (uint8_t)s.prow[k][i];
        MEM8(B_FLAGS + 2u * cell + 1) = 1;             /* away known: the helmet column draws it */
        if (g >= 0) {
            /* M14 bracket: a real game cell (home, away, date; played = type 3 with both scores, so the
             * retail helmet columns draw the scores and highlight the winner) */
            const ErasGame *e = nfl2k5_eras_game(g);
            int hp, ap;
            MEM8(B_GRID + 8u * cell + 1) = (uint8_t)e->home;
            MEM8(B_GRID + 8u * cell + 2) = (uint8_t)e->away;
            MEM8(B_GRID + 8u * cell + 3) = (uint8_t)(e->date[0] ? atoi(e->date + 5) : 1);
            MEM8(B_GRID + 8u * cell + 4) = (uint8_t)(e->date[0] ? atoi(e->date + 8) : 1);
            MEM8(B_GRID + 8u * cell + 6) = 13;
            MEM8(B_FLAGS + 2u * cell + 0) = 1;
            if (nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED) {
                MEM8(B_GRID + 8u * cell + 0) = 3;
                MEM8(B_SCORES + 10u * cell + 0) = (uint8_t)(hp < 0 ? 0 : hp > 255 ? 255 : hp);
                MEM8(B_SCORES + 10u * cell + 5) = (uint8_t)(ap < 0 ? 0 : ap > 255 ? 255 : ap);
            }
        }
    }
    if (s.prow_n[k] < GRID_SLOTS) MEM8(B_GRID + 8u * (uint32_t)((PSEUDO_ROW0 + k) * GRID_SLOTS + s.prow_n[k])) = 7;
}

/* The standings order into grid rows 16.. (page 0: one row per division, page 1: one per conference). */
static void fill_standings_rows(void)
{
    int k, i, n = nfl2k5_eras_team_count(), rows = s.stand_page ? 2 : s.ndiv;
    nfl2k5_eras_standings(s_rec_now);
    for (k = 0; k < MAX_PSEUDO; k++) {
        pseudo_clear(k);
        if (k < rows) {
            for (i = 0; i < n && s.prow_n[k] < GRID_SLOTS; i++) {
                const ErasTeam *t = nfl2k5_eras_team(i);
                if (s.stand_page ? !strcmp(t->conference, k ? "NFC" : "AFC") : !strcmp(t->division, s.div_name[k]))
                    s.prow[k][s.prow_n[k]++] = i;
            }
            qsort(s.prow[k], (size_t)s.prow_n[k], sizeof(int), cmp_team);
        }
        pseudo_cells(k);
    }
}

/* M14: the Playoff Picture (rows 0, 1: AFC / NFC seeds and the hunt) and the bracket (rows 2..5: the
 * four rounds' games). */
static void fill_playoff_rows(void)
{
    int k, c, g;
    nfl2k5_eras_standings(s_rec_now);
    for (k = 0; k < MAX_PSEUDO; k++) pseudo_clear(k);
    for (c = 0; c < 2; c++) {
        s.nseeds[c] = nfl2k5_eras_playoff_picture(c, s.seeds[c], GRID_SLOTS - 1, 7);
        for (k = 0; k < s.nseeds[c]; k++) { s.prow[c][k] = s.seeds[c][k].team; s.pdata[c][k] = &s.seeds[c][k]; }
        s.prow_n[c] = s.nseeds[c];
    }
    for (g = nfl2k5_eras_regular_game_count(); g < nfl2k5_eras_game_count(); g++) {
        const ErasGame *e = nfl2k5_eras_game(g);
        int r = e->round;
        if (r < 1 || r > 4 || s.prow_n[1 + r] >= GRID_SLOTS - 1) continue;
        s.prow[1 + r][s.prow_n[1 + r]] = e->away;
        s.pgame[1 + r][s.prow_n[1 + r]++] = g;
    }
    for (k = 0; k < MAX_PSEUDO; k++) pseudo_cells(k);
}

/* M14: awards (row 0) and the leaders of the current category (row 1). */
static void fill_awards_rows(void)
{
    int k, i, n;
    static const ErasPlayerSeason *lead[GRID_SLOTS];
    nfl2k5_eras_standings(s_rec_now);
    nfl2k5_eras_awards_build();
    for (k = 0; k < MAX_PSEUDO; k++) pseudo_clear(k);
    for (i = 0; i < nfl2k5_eras_awards_count() && i < GRID_SLOTS - 1; i++) {
        const ErasAward *a = nfl2k5_eras_award(i);
        s.prow[0][i] = a->p ? a->p->team : a->coach_team >= 0 ? a->coach_team : 0;
        s.pdata[0][i] = a;
        s.prow_n[0] = i + 1;
    }
    n = nfl2k5_eras_leaders(s.pg[PG_LEADERS].tab, lead, GRID_SLOTS - 1);
    for (i = 0; i < n; i++) { s.prow[1][i] = lead[i]->team; s.pdata[1][i] = lead[i]; }
    s.prow_n[1] = n;
    for (k = 0; k < MAX_PSEUDO; k++) pseudo_cells(k);
}

static void fill_pseudo_rows(void)
{
    if (s.screen == SC_STAND) fill_standings_rows();
    else if (s.screen == SC_PLAYOFF) fill_playoff_rows();
    else if (s.screen == SC_AWARDS) fill_awards_rows();
}

static void fill_block(void)
{
    int n = nfl2k5_eras_team_count(), weeks = nfl2k5_eras_week_count(), g, i, next, cur;
    int ns[GRID_ROWS];
    uint32_t a;
    for (a = 0; a < BLOCK_LEN; a += 4) MEM32(BLOCK + a) = 0;   /* as sub_000C7530 */
    for (a = 0; a < 10; a++) MEM32(B_ONES + 4u * a) = 1;
    nfl2k5_eras_refresh();
    next = nfl2k5_eras_next_game();
    if (weeks > PSEUDO_ROW0) weeks = PSEUDO_ROW0;
    cur = next >= 0 ? nfl2k5_eras_game(next)->week - 1 : weeks - 1;
    if (cur < 0) cur = 0;
    if (cur >= weeks) cur = weeks - 1;                 /* below the week count: no stage end */
    MEM32(B_MODE) = (uint32_t)s.mode;
    MEM32(B_STAGE) = 8;                                /* regular season */
    MEM32(B_TEAMS) = (uint32_t)n;
    MEM32(B_WEEKS) = (uint32_t)weeks;
    MEM32(B_WEEK) = (uint32_t)cur;
    MEM32(B_YEAR) = (uint32_t)(nfl2k5_eras_season() - 2004);   /* the date line adds 2004 (M14: the league's season) */
    { char b[32]; snprintf(b, sizeof b, "%d Season", nfl2k5_eras_season()); wput(s.w_tab[MAX_PSEUDO + 1], b, 16); }
    MEM32(B_SELWEEK) = (uint32_t)cur;
    s.focus = focus_team();
    MEM32(B_SELTEAM) = s.rec[s.focus];
    for (i = 0; i < n; i++) {
        MEM32(B_TEAMPTR + 4u * (uint32_t)i) = s.rec[i];
        MEM32(B_DIV + 4u * (uint32_t)i) = (uint32_t)division_index(nfl2k5_eras_team(i)->division);
    }
    if (nfl2k5_eras_user_team() >= 0) MEM32(B_USER + 4u * (uint32_t)nfl2k5_eras_user_team()) = 1;
    memset(ns, 0, sizeof ns);
    for (g = 0; g < nfl2k5_eras_game_count(); g++) {
        const ErasGame *e = nfl2k5_eras_game(g);
        int w = e->week - 1, hp, ap;
        uint32_t cell;
        if (w < 0 || w >= weeks || ns[w] >= GRID_SLOTS - 1) continue;
        cell = (uint32_t)(w * GRID_SLOTS + ns[w]++);
        MEM8(B_GRID + 8u * cell + 0) = 0;
        MEM8(B_GRID + 8u * cell + 1) = (uint8_t)e->home;
        MEM8(B_GRID + 8u * cell + 2) = (uint8_t)e->away;
        MEM8(B_GRID + 8u * cell + 3) = (uint8_t)atoi(e->date + 5);   /* month 1..12 (table 0xAC6084) */
        MEM8(B_GRID + 8u * cell + 4) = (uint8_t)atoi(e->date + 8);
        MEM8(B_GRID + 8u * cell + 6) = 13;
        MEM8(B_FLAGS + 2u * cell + 0) = 1;
        MEM8(B_FLAGS + 2u * cell + 1) = 1;
        if (nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED) {
            MEM8(B_GRID + 8u * cell + 0) = 3;
            MEM8(B_SCORES + 10u * cell + 0) = (uint8_t)(hp < 0 ? 0 : hp > 255 ? 255 : hp);
            MEM8(B_SCORES + 10u * cell + 5) = (uint8_t)(ap < 0 ? 0 : ap > 255 ? 255 : ap);
        }
    }
    for (i = 0; i < GRID_ROWS; i++)
        if (ns[i] < GRID_SLOTS) MEM8(B_GRID + 8u * (uint32_t)(i * GRID_SLOTS + ns[i])) = 7;
    if (s.screen != SC_SCHED) fill_pseudo_rows();
}

/* ---- the swap ------------------------------------------------------------ */

static void swap_in(int screen, const char *why)
{
    s.screen = screen;
    if (!s.swapped) {
        memcpy(s.snap, (void *)XBOX_PTR(BLOCK), BLOCK_LEN);
        s.snap_ui = MEM32(UI_TEAM);
        s.hash_pre = hash_block();
        if (s.cycles == 0) dump("pre");
        s.swapped = 1;
        build_records();
        SH_LOG("swap in (%s): block %08X hash %08X, [A84494] %08X, [AA26B0] %u\n", why, (unsigned)BLOCK,
               (unsigned)s.hash_pre, (unsigned)s.snap_ui, (unsigned)MEM32(DIRTY_OPTIONS));
    }
    fill_block();
}

static void swap_out(const char *why)
{
    uint32_t h;
    if (!s.swapped) return;
    memcpy((void *)XBOX_PTR(BLOCK), s.snap, BLOCK_LEN);
    MEM32(UI_TEAM) = s.snap_ui;
    s.swapped = 0;
    s.cycles++;
    h = hash_block();
    if (h != s.hash_pre) {
        fprintf(stderr, "[ERAS] sheets: RESTORE MISMATCH (%s): hash %08X, expected %08X; restoring again\n", why, (unsigned)h, (unsigned)s.hash_pre);
        memcpy((void *)XBOX_PTR(BLOCK), s.snap, BLOCK_LEN);
        MEM32(UI_TEAM) = s.snap_ui;
        h = hash_block();
    }
    dump("post");
    nfl2k5_eras_log("franchise screen copy: block restored (%s), cycle %d, hash %08X %s, [AA26B0] %u",
                    why, s.cycles, (unsigned)h, h == s.hash_pre ? "= before" : "MISMATCH", (unsigned)MEM32(DIRTY_OPTIONS));
}

int nfl2k5_eras_sheets_swapped(void) { return s.swapped; }

/* M15: an Eras team record for another screen's UI team (the backdrop colours and name);
 * rebuilt from the clubs when no swap is live (a swap keeps its own copies). 0 = none. */
uint32_t nfl2k5_eras_sheets_team_rec(int team)
{
    if (!s.built || team < 0 || team >= NFL2K5_ERAS_MAX_TEAMS || team >= nfl2k5_eras_team_count()) return 0;
    if (!s.swapped && build_records() < 0) return 0;
    return s.rec[team];
}

/* ---- mini.iff ------------------------------------------------------------ */

static uint32_t txtr(uint32_t name)
{
    PUSH32(g_esp, name);
    return gcall(sub_000449E0, s.w_ctx_mini, FOURCC_TXTR);
}

static void mini_load(void)
{
    uint32_t heap;
    DWORD t0;
    if (s.mini_mine) return;
    {   /* probe: the first team's club helmet, mini_a<code>_0 (always in mini.iff) */
        uint32_t code = s.rec[0] ? MEM32(s.rec[0] + 0x10C) : 0, i;
        char b[24] = "mini_a";
        for (i = 0; code && i < 4 && MEM16(code + 2 * i); i++) b[6 + i] = (char)MEM16(code + 2 * i);
        b[6 + i] = 0;
        strcat(b, "_0");
        wput(s.w_unknown, b, 24);
    }
    /* the Coach's Desk packages (fr.iff + mini.iff, flag [0xAA240C]) or any other FRMINI already
     * loaded: use it and never unload it (the unload goes by context name) */
    if (MEM32(0x00AA240Cu) || txtr(s.w_unknown)) {
        SH_LOG("FRMINI already resident ([AA240C] %u): used as it is, not unloaded\n", (unsigned)MEM32(0x00AA240Cu));
        return;
    }
    t0 = GetTickCount();
    PUSH32(g_esp, 0);
    PUSH32(g_esp, 0x22);
    heap = gcall(sub_00038FB0, 0, 0);
    PUSH32(g_esp, heap);
    PUSH32(g_esp, s.mini_slot);
    gcall(sub_000441B0, s.w_ctx_mini, s.w_file_mini);
    gcall(sub_000432D0, 0, 0);
    s.mini_mine = 1;
    SH_LOG("mini.iff queued under FRMINI (slot %08X; probe %08X right after, it loads in the background), %lu ms\n", (unsigned)s.mini_slot,
           (unsigned)txtr(s.w_unknown), (unsigned long)(GetTickCount() - t0));
}

static void mini_unload(void)
{
    if (!s.mini_mine) return;
    gcall(sub_000432F0, s.w_ctx_mini, 0);
    s.mini_mine = 0;
    SH_LOG("FRMINI unloaded\n");
}

/* ---- pages ------------------------------------------------------------- */

static void free_sheet(void)
{
    if (MEM32(SHEET_ALIVE)) { gcall(sub_001707F0, WIDGET, 0); MEM32(SHEET_ALIVE) = 0; }
}

/* sub_001749D0 as sub_0024B740 calls it: rect {33, 607, 71, 382} of the retail wrapper. */
static void construct(uint32_t sheet, uint32_t fsm)
{
    PUSH32(g_esp, fbits(607.0f));
    PUSH32(g_esp, fbits(33.0f));
    PUSH32(g_esp, fbits(382.0f));
    PUSH32(g_esp, fbits(71.0f));
    PUSH32(g_esp, fsm);
    gcall(sub_001749D0, WIDGET, sheet);
    MEM32(SHEET_ALIVE) = 1;
}

/* sub_0024B740 mode 1, with the Eras team instead of the coached team. */
static void sched_team_init(uint32_t fsm)
{
    MEM32(PAGE_MODE) = 1;
    free_sheet();
    MEM32(B_SELTEAM) = s.rec[s.focus];
    construct(s.sheet_team, fsm);
    gcall(sub_001707C0, WIDGET, MEM32(B_WEEK));
    gcall(sub_000F32C0, fsm, 0);
    gcall(sub_003278F0, 0, 0);
}

/* sub_0024B740 mode 0. */
static void sched_week_init(uint32_t fsm)
{
    uint32_t slot;
    MEM32(PAGE_MODE) = 0;
    free_sheet();
    MEM32(B_SELWEEK) = MEM32(B_WEEK);
    construct(s.sheet_week, fsm);
    gcall(sub_000F32C0, fsm, 0);
    gcall(sub_00174DF0, WIDGET, MEM32(B_WEEK));
    slot = gcall(sub_000C6920, s.rec[s.focus], MEM32(B_WEEK));
    gcall(sub_001707C0, WIDGET, slot == 0xFFFFu ? 0u : slot);
    gcall(sub_003278F0, 0, 0);
}

static int stand_tab(void) { return s.stand_page ? s.tab_conf : s.tab_div; }

/* M14: the pseudo row a page's current tab shows (the grid row minus 16) */
static int pg_row(int pg)
{
    const Pg *p = &s.pg[pg];
    if (pg == PG_STAND_DIV || pg == PG_STAND_CONF) return stand_tab();
    return p->single ? p->row0 : p->row0 + p->tab;
}

static void pg_init(uint32_t fsm, int pg)
{
    Pg *p = &s.pg[pg];
    int k = 0, i, r;
    s.cur_pg = pg;
    if (pg == PG_STAND_DIV || pg == PG_STAND_CONF) s.stand_page = pg == PG_STAND_CONF;
    MEM32(PAGE_MODE) = 0;
    free_sheet();
    fill_pseudo_rows();
    construct(p->sheet, fsm);
    gcall(sub_000F32C0, fsm, 0);
    r = pg_row(pg);
    gcall(sub_00174DF0, WIDGET, (uint32_t)(PSEUDO_ROW0 + r));
    if (r >= 0 && r < MAX_PSEUDO) for (i = 0; i < s.prow_n[r]; i++) if (s.prow[r][i] == s.focus) k = i;
    gcall(sub_001707C0, WIDGET, (uint32_t)k);
    gcall(sub_003278F0, 0, 0);
    SH_LOG("page %d (%s), tab %d, row %d (%d rows)\n", pg, p->banner ? p->banner : "standings", p->tab, r, r >= 0 && r < MAX_PSEUDO ? s.prow_n[r] : -1);
}

static void stand_init(uint32_t fsm, int page) { pg_init(fsm, page ? PG_STAND_CONF : PG_STAND_DIV); }

static void help(uint32_t fsm, uint32_t slot, uint32_t text)
{
    PUSH32(g_esp, text);
    gcall(sub_000F35A0, fsm, slot);
}

/* The safe part of sub_0024D1B0: help bar, pads (A, up/down, LT/RT tabs, Back),
 * the team colours, the sheet update. START, Y, SportsCenter and every piece of
 * season processing are left out. Returns 1 to pop. */
static uint32_t page_frame(int screen, uint32_t fsm)
{
    uint32_t ctx = gcall(sub_0006E2D0, fsm, 0), pad, mode = MEM32(PAGE_MODE);
    help(fsm, 9, 0);
    help(fsm, 0xD, 0);
    help(fsm, 0xB, 0);
    if (screen == SC_SCHED) help(fsm, 0xF, mode == 0 ? W_FOR_TEAM : mode == 1 ? W_FOR_WEEKLY : 0);
    else if (screen == SC_STAND) help(fsm, 0xF, s.stand_page ? s.w_for_div : s.w_for_conf);
    else help(fsm, 0xF, s.pg[s.cur_pg ^ 1].w_help);   /* the other page of the pair */
    for (pad = 0; pad < 8; pad++) {
        uint32_t b, filt = ctx ? MEM32(ctx + 0x590) : 0;
        if (filt) {
            recomp_func_t f = recomp_lookup(filt);
            if (f) { PUSH32(g_esp, pad); if (!gcall(f, fsm, 0)) continue; }
        }
        if (!gcall(sub_000709B0, pad, 0)) continue;
        b = gcall(sub_00070A30, pad, 0);
        if (b & 0x100u) gcall(sub_00176B50, WIDGET, 0);          /* A: the sheet's activation (Eras no-op) */
        if (b & 0x1000000u) gcall(sub_001703D0, WIDGET, 0);
        if (b & 0x2000000u) gcall(sub_00170420, WIDGET, 0);
        if (b & 0x8000u) gcall(sub_00174D50, WIDGET, 0);         /* RT: next tab (never Franchise Settings) */
        if (b & 0x4000u) gcall(sub_00174DA0, WIDGET, 0);         /* LT */
        if (b & 0x220u) {                                       /* Back */
            PUSH32(g_esp, 0xFFFFFFFFu);
            gcall(sub_00070A50, pad, 0);
            return 1;
        }
    }
    if (MEM32(SHEET_ALIVE)) {
        uint32_t team = screen == SC_SCHED && mode == 1 ? MEM32(B_SELTEAM) : s.rec[s.focus];
        if (team) gcall(sub_00327980, team, 0);                  /* colours and [0xA84494], as retail */
        MEM32(SHEET_RESTORE) = 0;
        gcall(sub_00174C70, WIDGET, 0);
    }
    return 0;
}

/* ---- the standings page draw (the retail draw's parts, Eras text) --------- */

static void draw_record_line(void)
{
    uint32_t c = s.txt, font;
    ErasRecord r[NFL2K5_ERAS_MAX_TEAMS];
    char b[64];
    const ErasTeam *t = nfl2k5_eras_team(s.focus);
    if (!t) return;
    nfl2k5_eras_standings(r);
    snprintf(b, sizeof b, "%s Record: (%d-%d-%d)", t->abbr, r[s.focus].w, r[s.focus].l, r[s.focus].t);
    wput(s.wbuf, b, 64);
    gcall(sub_00046920, c, 0);                          /* as sub_0024C480's record line */
    font = gcall(sub_000EF850, 0, 0);
    gcall(sub_000469B0, c, font);
    gcall(sub_00046A00, c, 2);
    gcall(sub_00046B60, c, 1);
    gcall(sub_00046B50, c, 0xFFC0C0C0u);
    PUSH32(g_esp, fbits(20.0f));
    PUSH32(g_esp, fbits(20.0f));
    PUSH32(g_esp, fbits(607.0f));
    gcall(sub_00046A70, c, 0);
    gcall(sub_000F1C70, c, s.wbuf);
}

static void draw_title(uint32_t x, uint32_t y, uint32_t text, uint32_t align)
{
    PUSH32(g_esp, align);
    PUSH32(g_esp, text);
    gcall(sub_003273C0, x, y);
}

extern int nfl2k5_eras_awards_box_games(int *played);
extern void nfl2k5_eras_awards_coach(int team, char *out, size_t n);

static void stand_draw(uint32_t fsm)
{
    int tab = stand_tab();
    const Pg *p = &s.pg[s.cur_pg];
    char left[48], right[48];
    draw_record_line();
    if (s.screen == SC_STAND) {
        if (s.stand_page) wput(s.w_tab[MAX_PSEUDO], tab ? "NFC" : "AFC", 8);
        draw_title(0xA7, 0x2E, s.stand_page ? s.w_tab[MAX_PSEUDO] : s.w_tab[tab < s.ndiv ? tab : 0], 3);
        draw_title(0x258, 0x2E, s.w_tab[MAX_PSEUDO + 1], 2);
        MEM32(S_TITLE) = s.w_banner;
    } else {
        /* M14: the tab's name on the left, the season / the source note on the right */
        switch (s.cur_pg) {
        case PG_PICTURE: snprintf(left, sizeof left, "%s", p->tab ? "NFC" : "AFC"); break;
        case PG_BRACKET: snprintf(left, sizeof left, "%s", p->tab == 3 ? nfl2k5_eras_super_bowl_name() : nfl2k5_eras_round_name(p->tab + 1)); break;
        case PG_AWARDS: snprintf(left, sizeof left, "%d SEASON AWARDS", nfl2k5_eras_season()); break;
        default: snprintf(left, sizeof left, "%s", nfl2k5_eras_leader_category(p->tab)); break;
        }
        if (s.cur_pg == PG_AWARDS || s.cur_pg == PG_LEADERS) {
            int played, box = nfl2k5_eras_awards_box_games(&played);
            snprintf(right, sizeof right, "%d of %d games with box scores", box, played);
        } else if (s.cur_pg == PG_PICTURE) snprintf(right, sizeof right, "%s", nfl2k5_eras_regular_complete() ? "Final seeds" : "If the playoffs started today");
        else snprintf(right, sizeof right, "%d Season", nfl2k5_eras_season());
        for (tab = 0; left[tab]; tab++) if (left[tab] >= 'a' && left[tab] <= 'z') left[tab] = (char)(left[tab] - 32);
        wput(s.w_pgtitle, left, 48);
        wput(s.w_pgright, right, 48);
        draw_title(0xA7, 0x2E, s.w_pgtitle, 3);
        draw_title(0x258, 0x2E, s.w_pgright, 2);
        MEM32(S_TITLE) = s.w_tab[MAX_PSEUDO + 2 + s.screen];
    }
    if (MEM32(SHEET_ALIVE)) gcall(sub_00176380, WIDGET, fsm);
}

/* M14: the text columns of the pseudo pages */
static uint32_t pg_col_text(int pg, int col, uint32_t prow, uint32_t row)
{
    int k = (int)prow - PSEUDO_ROW0;
    char b[STR_CH * 2];
    uint32_t out;
    const Pg *p = &s.pg[pg];
    if (col < 0 || col >= p->ncols || row >= GRID_SLOTS) return W_EMPTY;
    out = p->out[col][row];
    if (k < 0 || k >= MAX_PSEUDO || (int)row >= s.prow_n[k]) return W_EMPTY;
    b[0] = 0;
    switch (pg) {
    case PG_PICTURE: {
        const ErasSeed *e = (const ErasSeed *)s.pdata[k][row];
        const ErasTeam *t = nfl2k5_eras_team(s.prow[k][row]);
        const ErasRecord *r = &s_rec_now[s.prow[k][row]];
        if (!e) break;
        switch (col) {
        case 0: if (e->seed) snprintf(b, sizeof b, "%d", e->seed); else snprintf(b, sizeof b, "--"); break;
        case 1: snprintf(b, sizeof b, "%s\n%s", t->city, t->name); break;
        case 2: snprintf(b, sizeof b, "%d", r->w); break;
        case 3: snprintf(b, sizeof b, "%d", r->l); break;
        case 4: snprintf(b, sizeof b, "%d", r->t); break;
        default:
            if (e->clinched & 4) snprintf(b, sizeof b, "Home Field Adv.");
            else if (e->clinched & 2) snprintf(b, sizeof b, "Division Title");
            else if (e->clinched & 1) snprintf(b, sizeof b, "Playoff Berth");
            else if (e->seed) snprintf(b, sizeof b, "%s", e->division_winner ? "Leads division" : "Wild card spot");
            else snprintf(b, sizeof b, "In the hunt");
            break;
        }
        break;
    }
    case PG_BRACKET: {
        int g = s.pgame[k][row], hp, ap;
        const ErasGame *e = g >= 0 ? nfl2k5_eras_game(g) : NULL;
        if (!e) break;
        switch (col) {
        case 0:
            if (e->round == 4) snprintf(b, sizeof b, "%s vs %s\n%s", nfl2k5_eras_team(e->away)->name, nfl2k5_eras_team(e->home)->name, e->site[0] ? e->site : "");
            else snprintf(b, sizeof b, "(%d) %s at (%d) %s", e->seed_away, nfl2k5_eras_team(e->away)->name, e->seed_home, nfl2k5_eras_team(e->home)->name);
            break;
        case 1: snprintf(b, sizeof b, "%s", e->date); break;
        default:
            if (nfl2k5_eras_result(g, &hp, &ap) == NFL2K5_ERAS_PLAYED)
                snprintf(b, sizeof b, "%s %d-%d", nfl2k5_eras_team(hp > ap ? e->home : e->away)->name, hp > ap ? hp : ap, hp > ap ? ap : hp);
            else snprintf(b, sizeof b, "%s", e->round == 4 && e->label[0] ? e->label : "to be played");
            break;
        }
        break;
    }
    case PG_AWARDS: {
        const ErasAward *a = (const ErasAward *)s.pdata[k][row];
        char coach[48];
        if (!a) break;
        switch (col) {
        case 0: snprintf(b, sizeof b, "%s", a->award); break;
        case 1:
            if (a->p) snprintf(b, sizeof b, "%s %s (%s)", a->p->first, a->p->last, a->p->pos >= 0 && a->p->pos < 17 ? k_posname[a->p->pos] : "?");
            else if (a->coach_team >= 0) { nfl2k5_eras_awards_coach(a->coach_team, coach, sizeof coach); snprintf(b, sizeof b, "%s", coach[0] ? coach : "(head coach)"); }
            else snprintf(b, sizeof b, "--");
            break;
        case 2: snprintf(b, sizeof b, "%s", a->p || a->coach_team >= 0 ? nfl2k5_eras_team(s.prow[k][row])->name : ""); break;
        default: snprintf(b, sizeof b, "%s", a->stat); break;
        }
        break;
    }
    case PG_LEADERS: {
        const ErasPlayerSeason *q = (const ErasPlayerSeason *)s.pdata[k][row];
        if (!q) break;
        switch (col) {
        case 0: snprintf(b, sizeof b, "%u", (unsigned)row + 1); break;
        case 1: snprintf(b, sizeof b, "%s %s", q->first, q->last); break;
        case 2: snprintf(b, sizeof b, "%s", nfl2k5_eras_team(q->team)->name); break;
        case 3: snprintf(b, sizeof b, "%s", q->pos >= 0 && q->pos < 17 ? k_posname[q->pos] : "?"); break;
        default: snprintf(b, sizeof b, "%d", nfl2k5_eras_leader_value(p->tab, q)); break;
        }
        break;
    }
    }
    wput(out, b, STR_CH);
    return out;
}

/* ---- standings sheet getters --------------------------------------------- */

static uint32_t col_text(int col, uint32_t prow, uint32_t row)
{
    int k = (int)prow - PSEUDO_ROW0, t;
    char b[STR_CH];
    const ErasTeam *e;
    const ErasRecord *r;
    uint32_t out;
    if (col < 0 || col >= NCOLS || row >= GRID_SLOTS) return W_EMPTY;
    out = s.out[col][row];
    if (k < 0 || k >= MAX_PSEUDO || (int)row >= s.prow_n[k]) return W_EMPTY;
    t = s.prow[k][row];
    e = nfl2k5_eras_team(t);
    r = &s_rec_now[t];
    switch (col) {
    case COL_TEAM: snprintf(b, sizeof b, "%s\n%s", e->city, e->name); break;
    case COL_W: snprintf(b, sizeof b, "%d", r->w); break;
    case COL_L: snprintf(b, sizeof b, "%d", r->l); break;
    case COL_T: snprintf(b, sizeof b, "%d", r->t); break;
    case COL_PCT: {
        int g = r->w + r->l + r->t;
        long p = g ? (long)((r->w * 2 + r->t) * 1000L + g) / (2L * g) : 0;
        if (p >= 1000) snprintf(b, sizeof b, "1.000"); else snprintf(b, sizeof b, ".%03ld", p);
        break;
    }
    case COL_PF: snprintf(b, sizeof b, "%d", r->pf); break;
    default: snprintf(b, sizeof b, "%d", r->pa); break;
    }
    wput(out, b, STR_CH);
    return out;
}

/* ---- events ---------------------------------------------------------------- */

static void save_statics(void)
{
    if (s.statics_saved) return;
    memcpy(s.statics, (void *)XBOX_PTR(UI_STATICS), UI_STATICS_LEN);
    s.statics_saved = 1;
}

static void restore_statics(void)
{
    if (!s.statics_saved) return;
    memcpy((void *)XBOX_PTR(UI_STATICS), s.statics, UI_STATICS_LEN);
    s.statics_saved = 0;
}

static void page_init(int screen, uint32_t fsm)
{
    int page = (int)MEM32(S_PAGE);
    if (screen == SC_SCHED) { if (page == 0) sched_team_init(fsm); else sched_week_init(fsm); }
    else if (screen == SC_STAND) stand_init(fsm, page ? 1 : 0);
    else if (screen == SC_PLAYOFF) pg_init(fsm, page ? PG_BRACKET : PG_PICTURE);
    else pg_init(fsm, page ? PG_LEADERS : PG_AWARDS);
}

static void event(int screen, int msg, uint32_t fsm)
{
    switch (msg) {
    case 1:                                            /* sub_00327A00, with the Eras team */
        save_statics();
        swap_in(screen, "created");
        MEM32(S_E10) = 0; MEM32(S_E14) = 0; MEM32(S_PAGE_PREV) = 0;
        if (screen != SC_PLAYOFF) MEM32(S_PAGE) = 0;   /* M14: the Playoffs row may open on the bracket page */
        MEM32(S_BUTTONS) = 0; MEM32(S_BTN_BUSY) = 0;
        MEM32(S_TITLE) = screen == SC_STAND ? s.w_banner : screen >= SC_PLAYOFF ? s.w_tab[MAX_PSEUDO + 2 + screen] : W_TITLE_EMPTY;
        MEM32(SHEET_ALIVE) = 0; MEM32(PAGE_MODE) = 0; MEM32(SHEET_RESTORE) = 0; MEM32(SHEET_C17904) = 0;   /* sub_0024C3A0 */
        MEM32(UI_TEAM) = s.rec[s.focus];
        gcall(sub_00327980, s.rec[s.focus], 0);
        break;
    case 3:                                            /* activated / uncovered */
        swap_in(screen, "shown");
        mini_load();
        MEM32(S_PAGES) = s.pages[screen];
        MEM32(S_PAGE_N) = 2;
        if (MEM32(S_PAGE) >= 2u) MEM32(S_PAGE) = 1;
        MEM32(UI_TEAM) = s.rec[s.focus];
        break;
    case 4:
        page_init(screen, fsm);
        break;
    case 5:                                            /* covered: retail page leave, then the block back */
        gcall(sub_003275C0, fsm, 0);
        swap_out("covered");
        mini_unload();
        break;
    case 6:
        gcall(sub_003276C0, fsm, 0);                   /* retail: page switch (LB/RB), then our page frame */
        break;
    case 2:
        free_sheet();
        swap_out("destroyed");
        mini_unload();
        restore_statics();
        break;
    }
    if (msg != 6) SH_LOG("%s msg %d\n", screen == SC_SCHED ? "schedule" : screen == SC_STAND ? "standings" : screen == SC_PLAYOFF ? "playoffs" : "awards", msg);
}

static uint32_t dispatch(int id, uint32_t ecx, uint32_t edx)
{
    if (id < FN_SCHED_TEAM_INIT) { event(id / FN_MSGS, id % FN_MSGS + 1, ecx); return 0; }
    switch (id) {
    case FN_SCHED_TEAM_INIT: sched_team_init(ecx); return 0;
    case FN_SCHED_WEEK_INIT: sched_week_init(ecx); return 0;
    case FN_SCHED_FRAME: return page_frame(SC_SCHED, ecx);
    case FN_STAND_FRAME: return page_frame(s.screen, ecx);
    case FN_STAND_DRAW: stand_draw(ecx); return 0;
    case FN_ACT:
        SH_LOG("A on (%u, %u): no action in Eras\n", (unsigned)ecx, (unsigned)edx);
        return 0;
    case FN_TABVAL: return (uint32_t)(PSEUDO_ROW0 + pg_row(s.cur_pg));
    case FN_TABCOUNT:
        if (s.cur_pg == PG_STAND_DIV) return (uint32_t)s.ndiv;
        if (s.cur_pg == PG_STAND_CONF) return 2u;
        return (uint32_t)s.pg[s.cur_pg].ntabs;
    case FN_TABNEXT: case FN_TABPREV: {
        if (s.cur_pg == PG_STAND_DIV || s.cur_pg == PG_STAND_CONF) {
            int n = s.stand_page ? 2 : s.ndiv, *t = s.stand_page ? &s.tab_conf : &s.tab_div;
            if (n > 0) *t = (*t + (id == FN_TABNEXT ? 1 : n - 1)) % n;
            if (s.stand_page) s.tab_div = s.tab_conf * (s.ndiv / 2);
            else s.tab_conf = s.ndiv ? s.tab_div / ((s.ndiv + 1) / 2) : 0;
        } else {
            Pg *p = &s.pg[s.cur_pg];
            if (p->ntabs > 0) p->tab = (p->tab + (id == FN_TABNEXT ? 1 : p->ntabs - 1)) % p->ntabs;
            if (p->single) fill_pseudo_rows();        /* the leaders: one pseudo row, refilled per category */
        }
        return 0;
    }
    case FN_ROWS: {
        int k = (int)ecx - PSEUDO_ROW0;
        return k >= 0 && k < MAX_PSEUDO ? (uint32_t)s.prow_n[k] : 0u;
    }
    default:
        if (id >= FN_PG_INIT0 && id < FN_PG_INIT_END) { pg_init(ecx, id - FN_PG_INIT0); return 0; }
        if (id >= FN_COL0 && id < FN_COL0 + NCOLS) return col_text(id - FN_COL0, ecx, edx);
        if (id >= FN_COL0 + MAXCOL && id < FN_COL0 + PG_N * MAXCOL) return pg_col_text((id - FN_COL0) / MAXCOL, (id - FN_COL0) % MAXCOL, ecx, edx);
        return 0;
    }
}

static void sh_fn(void)
{
    uint32_t id = (s_called - SH_BASE) / 4u, ecx = g_ecx, edx = g_edx, r;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    r = dispatch((int)id, ecx, edx);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    g_eax = r;
    g_esp += 4;
}

recomp_func_t nfl2k5_eras_sheets_lookup(uint32_t address)
{
    uint32_t off = address - SH_BASE;
    if (!s.built || address < SH_BASE || (off & 3u) || off / 4u >= (uint32_t)FN_COUNT) return NULL;
    s_called = address;
    return sh_fn;
}

/* ---- building -------------------------------------------------------------- */

static uint32_t events(int screen)
{
    static const int msgs[] = { 1, 2, 3, 4, 5, 6, 8 };
    uint32_t t = alloc(8u * 8u, 4), i;
    if (!t) return 0;
    for (i = 0; i < 7; i++) {
        uint32_t list = alloc(0x24u * 2u, 4);
        if (!list) return 0;
        MEM32(list + 0x00) = 1;                         /* call +4 (ecx = FSM) */
        MEM32(list + 0x04) = msgs[i] == 8 ? FN_MSG8 : fn_addr(screen * FN_MSGS + msgs[i] - 1);
        MEM32(t + i * 8u) = (uint32_t)msgs[i];
        MEM32(t + i * 8u + 4u) = list;
    }
    return t;
}

static uint32_t sheet_copy(uint32_t src)
{
    uint32_t p = alloc(0x100, 16);
    if (!p) return 0;
    gcopy(p, src, 0x100);
    MEM32(p + 0xC4) = fn_addr(FN_ACT);                 /* A activation (args 0x00020039 kept) */
    MEM32(p + 0xDC) = fn_addr(FN_ACT);
    return p;
}

static uint32_t text_column(int getter, const char *header, float width, uint32_t align)
{
    uint32_t c = alloc(0xB0, 16), h = wstr(header);
    if (!c || !h) return 0;
    gcopy(c, COL_TEXT_TMPL, 0xB0);
    MEM32(c + 0x04) = align;
    MEM32(c + 0x08) = fn_addr(getter);
    MEM32(c + 0x0C) = 0x00020039u;                     /* (tab value, row) */
    MEM32(c + 0x50) = fbits(width);
    MEM32(c + 0x70) = h;
    MEM32(c + 0x88) = h;
    return c;
}

int nfl2k5_eras_sheets_install(void)
{
    static const struct { const char *h; float w; uint32_t align; } k_cols[NCOLS] = {
        { "TEAM", 155.0f, 1 }, { "W", 34.0f, 3 }, { "L", 34.0f, 3 }, { "T", 34.0f, 3 },
        { "PCT", 52.0f, 3 }, { "PF", 44.0f, 3 }, { "PA", 44.0f, 3 },
    };
    int i, c, ok = 1;
    const char *e;
    const char *conf_div[2][4];
    int nd[2];
    if (s.built) return 0;
    e = getenv("NFL2K5_ERAS_LOG"); s.log = e && *e && *e != '0';
    e = getenv("NFL2K5_ERAS_SCHEDULE"); s.sched_on = !(e && !_stricmp(e, "list"));
    e = getenv("NFL2K5_ERAS_STANDINGS"); s.stand_on = !(e && !_stricmp(e, "list"));
    e = getenv("NFL2K5_ERAS_SHEET_MODE"); s.mode = e && atoi(e) == 1 ? 1 : 2;   /* 2 Franchise (tested path), 1 Tournament mapping */
    if (nfl2k5_eras_package_load()) { fprintf(stderr, "[ERAS] sheets: no season package; the list screens stay\n"); return -1; }
    s.size = 160u * 1024u;   /* M14: four screens, six pseudo pages */
    s.mem = xbox_HeapAlloc(s.size, 16);
    if (!s.mem) { fprintf(stderr, "[ERAS] sheets: no guest memory\n"); return -1; }

    /* divisions, AFC first, in package order */
    nd[0] = nfl2k5_eras_divisions("AFC", conf_div[0]);
    nd[1] = nfl2k5_eras_divisions("NFC", conf_div[1]);
    s.ndiv = 0;
    for (c = 0; c < 2; c++)
        for (i = 0; i < nd[c] && s.ndiv < MAX_PSEUDO; i++) { s.div_conf[s.ndiv] = c; s.div_name[s.ndiv++] = conf_div[c][i]; }

    for (c = 0; c < SC_N; c++) {
        s.desc[c] = alloc(0x34, 16);
        s.pages[c] = alloc(0x20, 16);
        if (!s.desc[c] || !s.pages[c]) ok = 0;
        else {
            gcopy(s.desc[c], DESC_SCHEDULE, 0x34);
            MEM32(s.desc[c] + 0x04) = events(c);
            if (!MEM32(s.desc[c] + 0x04)) ok = 0;
        }
    }
    s.sheet_team = sheet_copy(SHEET_TEAM);
    s.sheet_week = sheet_copy(SHEET_WEEK);
    s.sheet_stand = sheet_copy(SHEET_WEEK);
    s.page_stand = alloc(0x9C + 4u * (NCOLS + 3u), 16);
    if (!s.sheet_team || !s.sheet_week || !s.sheet_stand || !s.page_stand) ok = 0;
    if (ok) {
        /* Schedule pages: page 0 Team (retail order), page 1 Weekly */
        MEM32(s.pages[SC_SCHED] + 0x00) = fn_addr(FN_SCHED_TEAM_INIT);
        MEM32(s.pages[SC_SCHED] + 0x04) = FN_PAGE_LEAVE;
        MEM32(s.pages[SC_SCHED] + 0x08) = fn_addr(FN_SCHED_FRAME);
        MEM32(s.pages[SC_SCHED] + 0x0C) = FN_PAGE_DRAW;
        MEM32(s.pages[SC_SCHED] + 0x10) = fn_addr(FN_SCHED_WEEK_INIT);
        MEM32(s.pages[SC_SCHED] + 0x14) = FN_PAGE_LEAVE;
        MEM32(s.pages[SC_SCHED] + 0x18) = fn_addr(FN_SCHED_FRAME);
        MEM32(s.pages[SC_SCHED] + 0x1C) = FN_PAGE_DRAW;
        /* Standings pages: Division, Conference */
        MEM32(s.pages[SC_STAND] + 0x00) = fn_addr(FN_PG_INIT0 + PG_STAND_DIV);
        MEM32(s.pages[SC_STAND] + 0x04) = FN_PAGE_LEAVE;
        MEM32(s.pages[SC_STAND] + 0x08) = fn_addr(FN_STAND_FRAME);
        MEM32(s.pages[SC_STAND] + 0x0C) = fn_addr(FN_STAND_DRAW);
        MEM32(s.pages[SC_STAND] + 0x10) = fn_addr(FN_PG_INIT0 + PG_STAND_CONF);
        MEM32(s.pages[SC_STAND] + 0x14) = FN_PAGE_LEAVE;
        MEM32(s.pages[SC_STAND] + 0x18) = fn_addr(FN_STAND_FRAME);
        MEM32(s.pages[SC_STAND] + 0x1C) = fn_addr(FN_STAND_DRAW);
        /* the Standings sheet: weekly sheet copy, Eras tabs and page */
        MEM32(s.sheet_stand + 0x30) = fn_addr(FN_TABVAL);
        MEM32(s.sheet_stand + 0x34) = 0;
        MEM32(s.sheet_stand + 0x60) = fn_addr(FN_TABCOUNT);
        MEM32(s.sheet_stand + 0x64) = 0;
        MEM32(s.sheet_stand + 0x78) = fn_addr(FN_TABNEXT);
        MEM32(s.sheet_stand + 0x7C) = 0;
        MEM32(s.sheet_stand + 0x8C) = fn_addr(FN_TABPREV);
        MEM32(s.sheet_stand + 0x90) = 0;
        MEM32(s.sheet_stand + 0xF4) = s.page_stand;
        gcopy(s.page_stand, PAGE_WEEK, 0x9C);
        MEM32(s.page_stand + 0x30) = fn_addr(FN_ROWS);  /* desc 0x00010009 kept: (tab value) */
        MEM32(s.page_stand + 0x9C) = COL_HELMET_AWAY;   /* the retail helmet column, unchanged */
        for (c = 0; c < NCOLS; c++) {
            s.col[c] = text_column(FN_COL0 + c, k_cols[c].h, k_cols[c].w, k_cols[c].align);
            if (!s.col[c]) ok = 0;
            MEM32(s.page_stand + 0xA0 + 4u * (uint32_t)c) = s.col[c];
            for (i = 0; i < GRID_SLOTS; i++) if (!(s.out[c][i] = alloc(2u * STR_CH, 4))) ok = 0;
        }
        MEM32(s.page_stand + 0xA0 + 4u * NCOLS) = 0;
        /* M14: the Playoff Picture / Bracket and the Awards / Leaders pages */
        for (c = 0; c < PG_N; c++) {
            Pg *p = &s.pg[c];
            static const struct { int screen, row0, ntabs, single; const char *banner, *help; } k_pg[PG_N] = {
                { SC_STAND, 0, 0, 0, NULL, NULL }, { SC_STAND, 0, 0, 0, NULL, NULL },
                { SC_PLAYOFF, 0, 2, 0, "PLAYOFFS", "for the Playoff Picture" }, { SC_PLAYOFF, 2, 4, 0, "PLAYOFFS", "for the Bracket" },
                { SC_AWARDS, 0, 1, 0, "AWARDS", "for Season Awards" }, { SC_AWARDS, 1, 8, 1, "AWARDS", "for League Leaders" },
            };
            size_t j;
            int nc = 0;
            memset(p, 0, sizeof *p);
            p->screen = k_pg[c].screen; p->row0 = k_pg[c].row0; p->ntabs = k_pg[c].ntabs; p->single = k_pg[c].single;
            p->banner = k_pg[c].banner; p->help = k_pg[c].help;
            if (c == PG_STAND_DIV || c == PG_STAND_CONF) { p->sheet = s.sheet_stand; p->page = s.page_stand; continue; }
            p->w_help = wstr(p->help);
            p->sheet = sheet_copy(SHEET_WEEK);
            p->page = alloc(0x9C + 4u * (MAXCOL + 4u), 16);
            if (!p->sheet || !p->page || !p->w_help) { ok = 0; continue; }
            MEM32(p->sheet + 0x30) = fn_addr(FN_TABVAL); MEM32(p->sheet + 0x34) = 0;
            MEM32(p->sheet + 0x60) = fn_addr(FN_TABCOUNT); MEM32(p->sheet + 0x64) = 0;
            MEM32(p->sheet + 0x78) = fn_addr(FN_TABNEXT); MEM32(p->sheet + 0x7C) = 0;
            MEM32(p->sheet + 0x8C) = fn_addr(FN_TABPREV); MEM32(p->sheet + 0x90) = 0;
            MEM32(p->sheet + 0xF4) = p->page;
            gcopy(p->page, PAGE_WEEK, 0x9C);
            MEM32(p->page + 0x30) = fn_addr(FN_ROWS);
            MEM32(p->page + 0x9C) = COL_HELMET_AWAY;
            for (j = 0; j < sizeof k_pgcols / sizeof k_pgcols[0]; j++) {
                if (k_pgcols[j].pg != c || nc >= MAXCOL) continue;
                p->col[nc] = text_column(FN_COL0 + c * MAXCOL + nc, k_pgcols[j].h, k_pgcols[j].w, k_pgcols[j].align);
                if (!p->col[nc]) ok = 0;
                for (i = 0; i < GRID_SLOTS; i++) if (!(p->out[nc][i] = alloc(2u * STR_CH, 4))) ok = 0;
                MEM32(p->page + 0xA0 + 4u * (uint32_t)nc) = p->col[nc];
                nc++;
            }
            p->ncols = nc;
            if (c == PG_BRACKET) MEM32(p->page + 0xA0 + 4u * (uint32_t)nc++) = COL_HELMET_HOME;   /* the home helmet and score after the text */
            MEM32(p->page + 0xA0 + 4u * (uint32_t)nc) = 0;
        }
        for (c = SC_PLAYOFF; c <= SC_AWARDS; c++) {
            int p0 = c == SC_PLAYOFF ? PG_PICTURE : PG_AWARDS;
            MEM32(s.pages[c] + 0x00) = fn_addr(FN_PG_INIT0 + p0);
            MEM32(s.pages[c] + 0x04) = FN_PAGE_LEAVE;
            MEM32(s.pages[c] + 0x08) = fn_addr(FN_STAND_FRAME);
            MEM32(s.pages[c] + 0x0C) = fn_addr(FN_STAND_DRAW);
            MEM32(s.pages[c] + 0x10) = fn_addr(FN_PG_INIT0 + p0 + 1);
            MEM32(s.pages[c] + 0x14) = FN_PAGE_LEAVE;
            MEM32(s.pages[c] + 0x18) = fn_addr(FN_STAND_FRAME);
            MEM32(s.pages[c] + 0x1C) = fn_addr(FN_STAND_DRAW);
        }
    }
    for (i = 0; i < NFL2K5_ERAS_MAX_TEAMS; i++) {
        s.rec[i] = alloc(TEAM_SIZE + 12u, 16);
        s.wcity[i] = alloc(64, 4); s.wnick[i] = alloc(64, 4); s.wabbr[i] = alloc(16, 4);
        if (!s.rec[i] || !s.wcity[i] || !s.wnick[i] || !s.wabbr[i]) ok = 0;
    }
    s.txt = alloc(0x100, 16);
    s.wbuf = alloc(0x100, 4);
    s.mini_slot = alloc(0x40, 16);
    s.w_ctx_mini = wstr("FRMINI");
    s.w_file_mini = wstr("mini.iff");
    s.w_unknown = alloc(48, 4);
    s.w_for_conf = wstr("for Conference Standings");
    s.w_for_div = wstr("for Division Standings");
    s.w_banner = wstr("STANDINGS");
    for (i = 0; i < s.ndiv; i++) s.w_tab[i] = wstr(s.div_name[i]);
    s.w_tab[MAX_PSEUDO] = alloc(32, 4);
    s.w_tab[MAX_PSEUDO + 1] = alloc(32, 4);            /* "<season> Season", written at every swap (M14: the season moves) */
    s.w_tab[MAX_PSEUDO + 2 + SC_PLAYOFF] = wstr("PLAYOFFS");
    s.w_tab[MAX_PSEUDO + 2 + SC_AWARDS] = wstr("AWARDS");
    s.w_pgtitle = alloc(2u * 48u, 4);
    s.w_pgright = alloc(2u * 48u, 4);
    if (!s.txt || !s.wbuf || !s.mini_slot || !s.w_tab[MAX_PSEUDO + 1] || !s.w_pgtitle || !s.w_pgright || !s.w_tab[MAX_PSEUDO + 2 + SC_AWARDS]) ok = 0;
    if (!ok) { fprintf(stderr, "[ERAS] sheets: build failed (%u bytes used)\n", (unsigned)s.used); s.mem = 0; return -1; }
    s.built = 1;
    fprintf(stderr, "[ERAS] sheets: Schedule %08X, Standings %08X (%d divisions), Playoffs %08X, Awards %08X, season block mode %d, "
            "office rows: schedule %s, standings %s; %u bytes\n", (unsigned)s.desc[SC_SCHED], (unsigned)s.desc[SC_STAND],
            s.ndiv, (unsigned)s.desc[SC_PLAYOFF], (unsigned)s.desc[SC_AWARDS], s.mode, s.sched_on ? "copy" : "list", s.stand_on ? "copy" : "list", (unsigned)s.used);
    return 0;
}

int nfl2k5_eras_sheets_schedule_on(void) { return s.built && s.sched_on; }
int nfl2k5_eras_sheets_standings_on(void) { return s.built && s.stand_on; }
int nfl2k5_eras_sheets_playoffs_on(void) { return s.built; }

/* M14 */
void nfl2k5_eras_sheets_push_playoffs(uint32_t fsm, int bracket)
{
    int u = nfl2k5_eras_user_team();
    s.pg[PG_PICTURE].tab = u >= 0 && !strcmp(nfl2k5_eras_team(u)->conference, "NFC") ? 1 : 0;
    {   /* the bracket opens on the round being played */
        int g = nfl2k5_eras_next_game(), r = 0;
        if (g >= 0 && nfl2k5_eras_game(g)->round > 0) r = nfl2k5_eras_game(g)->round - 1;
        else if (g < 0 && nfl2k5_eras_season_complete()) r = 3;
        s.pg[PG_BRACKET].tab = r;
    }
    s.cur_pg = bracket ? PG_BRACKET : PG_PICTURE;
    MEM32(S_PAGE) = bracket ? 1u : 0u;
    SH_LOG("push Playoffs %08X (page %d)\n", (unsigned)s.desc[SC_PLAYOFF], bracket);
    gcall(sub_0006E390, fsm, s.desc[SC_PLAYOFF]);
}

void nfl2k5_eras_sheets_push_awards(uint32_t fsm)
{
    s.pg[PG_AWARDS].tab = 0;
    s.cur_pg = PG_AWARDS;
    MEM32(S_PAGE) = 0;
    SH_LOG("push Awards %08X\n", (unsigned)s.desc[SC_AWARDS]);
    gcall(sub_0006E390, fsm, s.desc[SC_AWARDS]);
}

void nfl2k5_eras_sheets_push_schedule(uint32_t fsm)
{
    SH_LOG("push Schedule %08X\n", (unsigned)s.desc[SC_SCHED]);
    gcall(sub_0006E390, fsm, s.desc[SC_SCHED]);
}

void nfl2k5_eras_sheets_push_standings(uint32_t fsm, int conference)
{
    int i, u = nfl2k5_eras_user_team();
    const ErasTeam *t = u >= 0 ? nfl2k5_eras_team(u) : NULL;
    s.tab_div = 0;
    for (i = 0; i < s.ndiv; i++)
        if (s.div_conf[i] == conference) { s.tab_div = i; break; }
    if (t && !strcmp(t->conference, conference ? "NFC" : "AFC")) s.tab_div = division_index(t->division);
    s.tab_conf = conference ? 1 : 0;
    s.stand_page = 0;
    SH_LOG("push Standings %08X (%s, tab %d)\n", (unsigned)s.desc[SC_STAND], conference ? "NFC" : "AFC", s.tab_div);
    gcall(sub_0006E390, fsm, s.desc[SC_STAND]);
}

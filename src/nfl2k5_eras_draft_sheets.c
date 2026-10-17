/* My NFL Eras: the draft screens, copies of Franchise's draft tables on the retail sheet engine
 * (docs/eras/results/M13-draft.md; the method is FRANCHISE-SCREEN-COPY.md option (c), as
 * src/nfl2k5_eras_sheets.c builds Standings).
 *
 * One descriptor (a copy of the Franchise Schedule 0x522828: handler 0xF3DA0, flags 0x95) with an
 * Eras event table and an Eras page table; two pages (LB / RB), each an Eras sheet built from a
 * copy of the weekly sheet definition 0x537858 with Eras tabs, an Eras page record and Eras text
 * columns (copies of the retail "AWAY" text column 0x536EB0 with our getters):
 *
 *   page 0  THE BIG BOARD / DRAFT CANDIDATES: tabs = position groups (LT / RT), rows = the
 *           available prospects by overall (the rookie ratings; the hidden potential is never
 *           read here), columns #, NAME, POS, SCHOOL, OVR, MOCK (the round the real draft took him
 *           in: a fact, Mel's mock); when the user's team is on the clock, A on a row arms the pick
 *           and a second A on the same row makes it (the title says so);
 *   page 1  DRAFT ORDER / RECAP: tabs = rounds, rows = that round's slots: PICK, TEAM, NAME, POS,
 *           SCHOOL (empty until the pick is made).
 *
 * START advances: CPU picks until the user's team is on the clock or the draft is complete (watch
 * mode sims the whole draft). The picks that the Draft Show setting covers (First Round: round 1;
 * Your Picks: the user's) are queued for the show (src/nfl2k5_eras_draftshow.c), which is pushed
 * over this screen and pops itself when done; this screen's msg 3 then rebuilds its rows. Y is
 * "Skip to my pick" (the same advance, no show). When the draft completes, the next season's
 * roster files are written (nfl2k5_eras_draft_write_rosters).
 *
 * Nothing here reads or writes the Franchise season block or the Franchise save: the retail
 * functions used are the sheet engine, the page switch (sub_003276C0), the page leave
 * (sub_003275C0 / sub_0024C440), the title draw (sub_003273C0) and the help bar. The screen's UI
 * statics (0xC95DE8..) are saved at create and restored at destroy, as the Schedule copy does. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_draft.h"
#include "nfl2k5_eras_draftshow.h"
#include "nfl2k5_eras_draft_sheets.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern recomp_func_t recomp_lookup(uint32_t xbox_va);
extern uint32_t nfl2k5_eras_menu_header(int which);     /* 2 = the draft show screen (EM_DRAFT) */

#define DS_BASE         NFL2K5_ERAS_DRAFT_SHEETS_BASE
#define RET_ADDR        (DS_BASE + 0x7FF0u)

/* retail data (see src/nfl2k5_eras_sheets.c) */
#define DESC_SCHEDULE   0x00522828u
#define SHEET_WEEK      0x00537858u
#define PAGE_WEEK       0x00537640u
#define COL_TEXT_TMPL   0x00536EB0u
#define FN_MSG8         0x00327690u
#define FN_PAGE_LEAVE   0x0024C440u
#define W_EMPTY         0x00E89DB4u
#define WIDGET          0x00C177F0u
#define SHEET_ALIVE     0x00C178F8u
#define PAGE_MODE       0x00C177ECu
#define SHEET_RESTORE   0x00C177E8u
#define SHEET_C17904    0x00C17904u
#define UI_STATICS      0x00C95DE8u
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
#define UI_TEAM         0x00A84494u
#define PAD_WORDS       0x00B37A78u   /* the game's button word per pad (stride 0x244) */

#define STR_CH          48
#define BOARD_ROWS      64            /* the Big Board shows the top 64 by overall of a group */
#define ORDER_ROWS      40
#define NGROUPS         10
#define BCOLS           6
#define OCOLS           5
#define MAX_ROUNDS      14

enum { FN_MSG1, FN_MSG2, FN_MSG3, FN_MSG4, FN_MSG5, FN_MSG6,
       FN_BOARD_INIT, FN_ORDER_INIT, FN_FRAME, FN_DRAW, FN_ACT,
       FN_B_TABVAL, FN_B_TABCOUNT, FN_B_NEXT, FN_B_PREV, FN_B_ROWS,
       FN_O_TABVAL, FN_O_TABCOUNT, FN_O_NEXT, FN_O_PREV, FN_O_ROWS,
       FN_BCOL0, FN_OCOL0 = FN_BCOL0 + BCOLS, FN_COUNT = FN_OCOL0 + OCOLS };
enum { BC_RANK, BC_NAME, BC_POS, BC_SCHOOL, BC_OVR, BC_MOCK };
enum { OC_PICK, OC_TEAM, OC_NAME, OC_POS, OC_SCHOOL };

static struct {
    int built, log;
    uint32_t mem, used, size;
    uint32_t desc, pages, sheet_board, sheet_order, page_board, page_order;
    uint32_t bcol[BCOLS], ocol[OCOLS];
    uint32_t bout[BCOLS][BOARD_ROWS], oout[OCOLS][ORDER_ROWS];
    uint32_t w_title, w_sub, w_help_board, w_help_order, w_banner, w_tab;
    int statics_saved;
    uint8_t statics[UI_STATICS_LEN];
    /* state */
    int page;                      /* 0 board, 1 order */
    int group, round;              /* board tab, order tab (0-based) */
    int avail[BOARD_ROWS], navail;
    int armed;                     /* prospect index armed by the first A, -1 none */
    int armed_row;
    uint32_t pad_prev[4];
    int pushed_show, reinit;      /* reinit: rebuild the sheet at the next frame (rows changed inside a frame) */
    uint32_t fsm;                 /* the front-end FSM, as the frame last saw it (the activation gets the tab value in ecx) */
    char msg[96];
} s;

static RECOMP_TLS uint32_t s_called;

#define DL(...) do { if (s.log) fprintf(stderr, "[ERAS] draft screen " __VA_ARGS__); } while (0)

/* ---- guest helpers (as nfl2k5_eras_sheets.c) --------------------------------- */

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
static uint32_t fn_addr(int id) { return DS_BASE + (uint32_t)id * 4u; }

/* ---- state ------------------------------------------------------------------- */

static void refresh_avail(void)
{
    s.navail = nfl2k5_eras_draft_available(s.group, s.avail, BOARD_ROWS);
}

static int order_rows(void)
{
    int first = nfl2k5_eras_draft_round_first(s.round + 1), n = 0, i;
    if (first < 0) return 0;
    for (i = first; i < nfl2k5_eras_draft_slot_count() && nfl2k5_eras_draft_slot(i)->round == s.round + 1 && n < ORDER_ROWS; i++) n++;
    return n;
}

/* ---- the sheet ---------------------------------------------------------------- */

static void free_sheet(void)
{
    if (MEM32(SHEET_ALIVE)) { gcall(sub_001707F0, WIDGET, 0); MEM32(SHEET_ALIVE) = 0; }
}

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

static void page_init(uint32_t fsm, int page)
{
    int cursor = 0;
    s.page = page;
    s.armed = -1;
    MEM32(PAGE_MODE) = 0;
    free_sheet();
    if (page == 0) {
        refresh_avail();
        construct(s.sheet_board, fsm);
        gcall(sub_000F32C0, fsm, 0);
        gcall(sub_00174DF0, WIDGET, (uint32_t)s.group);
    } else {
        int next = nfl2k5_eras_draft_next_slot(), first = nfl2k5_eras_draft_round_first(s.round + 1);
        construct(s.sheet_order, fsm);
        gcall(sub_000F32C0, fsm, 0);
        gcall(sub_00174DF0, WIDGET, (uint32_t)s.round);
        if (next >= 0 && first >= 0 && nfl2k5_eras_draft_slot(next)->round == s.round + 1) cursor = next - first;
    }
    gcall(sub_001707C0, WIDGET, (uint32_t)cursor);
    gcall(sub_003278F0, 0, 0);
    DL("page %s, tab %d, %d rows\n", page ? "order" : "board", page ? s.round : s.group, page ? order_rows() : s.navail);
}

static void help(uint32_t fsm, uint32_t slot, uint32_t text)
{
    PUSH32(g_esp, text);
    gcall(sub_000F35A0, fsm, slot);
}

/* ---- the draft show ------------------------------------------------------------- */

static int show_covers(int slot_index)
{
    const ErasDraftSlot *sl = nfl2k5_eras_draft_slot(slot_index);
    int mode = nfl2k5_eras_draftshow_mode(), u = nfl2k5_eras_user_team();
    if (!sl || mode == 0) return 0;
    if (mode == 1) return sl->round == 1;
    return u >= 0 && sl->team == u;
}

static void queue_slot(int slot_index)
{
    const ErasDraftSlot *sl = nfl2k5_eras_draft_slot(slot_index);
    const ErasProspect *e = sl && sl->prospect >= 0 ? nfl2k5_eras_draft_prospect(sl->prospect) : NULL;
    const ErasTeam *t = sl ? nfl2k5_eras_team(sl->team) : NULL;
    char name[72];
    int first = sl ? nfl2k5_eras_draft_round_first(sl->round) : -1;
    if (!e || !t) return;
    snprintf(name, sizeof name, "%s %s", e->first, e->last);
    nfl2k5_eras_draftshow_queue_add(sl->round, sl->pick, first >= 0 ? slot_index - first + 1 : 0, t->id, name, e->pos, e->college);
}

/* Push the show over this screen when picks were queued and the stage is there. */
static void show_if_queued(uint32_t fsm)
{
    uint32_t hdr = nfl2k5_eras_menu_header(2);
    int n = nfl2k5_eras_draftshow_queue_count();
    if (!n) return;
    if (!hdr || !nfl2k5_eras_draftshow_ready()) {
        nfl2k5_eras_log("draft: %d pick(s) would go to the show, but the show is not available (%s)", n, hdr ? "pack files missing" : "no screen");
        nfl2k5_eras_draftshow_queue_clear();
        return;
    }
    nfl2k5_eras_log("draft: the show plays %d pick(s)", n);
    s.pushed_show = 1;
    gcall(sub_0006E390, fsm, hdr);
}

static void complete_check(void)
{
    if (nfl2k5_eras_draft_next_slot() >= 0 || nfl2k5_eras_draft_rosters_written()) return;
    {
        int n = nfl2k5_eras_draft_write_rosters();
        snprintf(s.msg, sizeof s.msg, "Draft complete: %d team rosters written for %d", n, nfl2k5_eras_draft_year());
        g_eras_svc->toast("My NFL Eras", "The draft is complete", s.msg, 6000);
    }
}

/* START / Y: CPU picks up to the user's pick or the end; the covered picks go to the show. */
static void advance(uint32_t fsm, int with_show)
{
    static int made[NFL2K5_DRAFT_MAX_SLOTS];
    int n, i, queued = 0;
    if (nfl2k5_eras_draft_user_on_clock()) { DL("advance: the user is on the clock\n"); return; }
    n = nfl2k5_eras_draft_sim(made, NFL2K5_DRAFT_MAX_SLOTS);
    nfl2k5_eras_draftshow_queue_clear();
    if (with_show) for (i = 0; i < n; i++) if (show_covers(made[i])) { queue_slot(made[i]); queued++; }
    nfl2k5_eras_log("draft: advanced %d CPU pick(s)%s; %s", n, queued ? "" : " (none for the show)",
                    nfl2k5_eras_draft_user_on_clock() ? "the user's team is on the clock" : nfl2k5_eras_draft_next_slot() < 0 ? "the draft is complete" : "stopped");
    complete_check();
    if (queued) show_if_queued(fsm);
    else { s.page = nfl2k5_eras_draft_next_slot() < 0 ? 1 : s.page; MEM32(S_PAGE) = (uint32_t)s.page; s.reinit = 1; }
}

static void user_pick(uint32_t fsm, int prospect)
{
    const ErasProspect *e = nfl2k5_eras_draft_prospect(prospect);
    int slot = nfl2k5_eras_draft_next_slot(), r;
    if (!e || slot < 0) return;
    r = nfl2k5_eras_draft_user_pick(prospect);
    if (r) { DL("user pick refused (%d)\n", r); return; }
    snprintf(s.msg, sizeof s.msg, "You drafted %s %s, %s, %s", e->first, e->last, e->pos, e->college);
    g_eras_svc->toast("My NFL Eras", "The pick is in", s.msg, 5000);
    nfl2k5_eras_draftshow_queue_clear();
    if (show_covers(slot)) { queue_slot(slot); show_if_queued(fsm); }
    else s.reinit = 1;
    complete_check();
}

/* ---- frame and draw ------------------------------------------------------------- */

static uint32_t page_frame(uint32_t fsm)
{
    uint32_t ctx = gcall(sub_0006E2D0, fsm, 0), pad;
    int i;
    s.fsm = fsm;
    if (s.reinit) { s.reinit = 0; page_init(fsm, s.page); }
    help(fsm, 9, 0);
    help(fsm, 0xD, 0);
    help(fsm, 0xB, 0);
    help(fsm, 0xF, s.page ? s.w_help_order : s.w_help_board);
    for (pad = 0; pad < 8; pad++) {
        uint32_t b, filt = ctx ? MEM32(ctx + 0x590) : 0;
        if (filt) {
            recomp_func_t f = recomp_lookup(filt);
            if (f) { PUSH32(g_esp, pad); if (!gcall(f, fsm, 0)) continue; }
        }
        if (!gcall(sub_000709B0, pad, 0)) continue;
        b = gcall(sub_00070A30, pad, 0);
        if (b & 0x100u) gcall(sub_00176B50, WIDGET, 0);          /* A: the sheet's activation -> FN_ACT (tab, row) */
        if (b & 0x1000000u) gcall(sub_001703D0, WIDGET, 0);
        if (b & 0x2000000u) gcall(sub_00170420, WIDGET, 0);
        if (b & 0x8000u) gcall(sub_00174D50, WIDGET, 0);         /* RT: next tab */
        if (b & 0x4000u) gcall(sub_00174DA0, WIDGET, 0);         /* LT */
        if (b & 0x220u) {                                       /* Back */
            PUSH32(g_esp, 0xFFFFFFFFu);
            gcall(sub_00070A50, pad, 0);
            return 1;
        }
    }
    /* START (advance with the show) and Y (skip to my pick, no show): edges of the raw pad words */
    for (i = 0; i < 4; i++) {
        uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u), edge = w & ~s.pad_prev[i];
        s.pad_prev[i] = w;
        if (edge & 0x10u) { advance(fsm, 1); break; }
        if (edge & 0x800u) { advance(fsm, 0); break; }
    }
    if (MEM32(SHEET_ALIVE)) {
        MEM32(SHEET_RESTORE) = 0;
        gcall(sub_00174C70, WIDGET, 0);
    }
    return 0;
}

static void draw_title(uint32_t x, uint32_t y, uint32_t text, uint32_t align)
{
    PUSH32(g_esp, align);
    PUSH32(g_esp, text);
    gcall(sub_003273C0, x, y);
}

static void page_draw(uint32_t fsm)
{
    char b[96];
    int next = nfl2k5_eras_draft_next_slot();
    const ErasDraftSlot *sl = next >= 0 ? nfl2k5_eras_draft_slot(next) : NULL;
    const ErasTeam *t = sl ? nfl2k5_eras_team(sl->team) : NULL;
    /* short: the right-hand title shares the bar with the tab label (ALL / ROUND n) */
    if (s.armed >= 0 && s.page == 0) {
        const ErasProspect *e = nfl2k5_eras_draft_prospect(s.armed);
        snprintf(b, sizeof b, "A AGAIN: %c. %s", e && e->first[0] ? e->first[0] : ' ', e ? e->last : "");
    } else if (!sl) snprintf(b, sizeof b, "DRAFT COMPLETE");
    else if (nfl2k5_eras_draft_user_on_clock()) snprintf(b, sizeof b, "YOUR PICK: R%d #%d", sl->round, sl->pick);
    else snprintf(b, sizeof b, "R%d #%d: %s", sl->round, sl->pick, t ? t->name : "");
    wput(s.w_sub, b, 96);
    if (s.page == 0) wput(s.w_tab, nfl2k5_eras_draft_group_name(s.group), 16);
    else snprintf(b, sizeof b, "ROUND %d", s.round + 1), wput(s.w_tab, b, 16);
    draw_title(0xA7, 0x2E, s.w_tab, 3);
    draw_title(0x258, 0x2E, s.w_sub, 2);
    MEM32(S_TITLE) = s.page ? s.w_title : s.w_banner;
    if (MEM32(SHEET_ALIVE)) gcall(sub_00176380, WIDGET, fsm);
}

/* ---- column getters ------------------------------------------------------------- */

static uint32_t board_text(int col, uint32_t row)
{
    const ErasProspect *e;
    char b[STR_CH];
    uint32_t out;
    if (col < 0 || col >= BCOLS || row >= (uint32_t)BOARD_ROWS || (int)row >= s.navail) return W_EMPTY;
    e = nfl2k5_eras_draft_prospect(s.avail[row]);
    out = s.bout[col][row];
    if (!e) return W_EMPTY;
    switch (col) {
    case BC_RANK: snprintf(b, sizeof b, "%u", (unsigned)row + 1); break;
    case BC_NAME: snprintf(b, sizeof b, "%c. %s", e->first[0] ? e->first[0] : ' ', e->last); break;
    case BC_POS: snprintf(b, sizeof b, "%s", e->pos); break;
    case BC_SCHOOL: snprintf(b, sizeof b, "%s", e->college); break;
    case BC_OVR: snprintf(b, sizeof b, "%d%s", e->ovr, !strcmp(e->source, "estimated") ? "*" : ""); break;
    default: snprintf(b, sizeof b, "R%d", e->round); break;
    }
    wput(out, b, STR_CH);
    return out;
}

static uint32_t order_text(int col, uint32_t row)
{
    int first = nfl2k5_eras_draft_round_first(s.round + 1);
    const ErasDraftSlot *sl = first >= 0 ? nfl2k5_eras_draft_slot(first + (int)row) : NULL;
    const ErasProspect *e = sl && sl->prospect >= 0 ? nfl2k5_eras_draft_prospect(sl->prospect) : NULL;
    const ErasTeam *t = sl ? nfl2k5_eras_team(sl->team) : NULL;
    char b[STR_CH];
    uint32_t out;
    if (col < 0 || col >= OCOLS || row >= (uint32_t)ORDER_ROWS || !sl || sl->round != s.round + 1) return W_EMPTY;
    out = s.oout[col][row];
    switch (col) {
    case OC_PICK: snprintf(b, sizeof b, "%d", sl->pick); break;
    case OC_TEAM: snprintf(b, sizeof b, "%s", t ? t->name : ""); break;
    case OC_NAME: snprintf(b, sizeof b, "%s", e ? e->last : (nfl2k5_eras_draft_next_slot() == first + (int)row ? "on the clock" : "")); if (e) snprintf(b, sizeof b, "%c. %s", e->first[0], e->last); break;
    case OC_POS: snprintf(b, sizeof b, "%s", e ? e->pos : ""); break;
    default: snprintf(b, sizeof b, "%s", e ? e->college : ""); break;
    }
    wput(out, b, STR_CH);
    return out;
}

/* ---- events ----------------------------------------------------------------------- */

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

static void event(int msg, uint32_t fsm)
{
    int i;
    switch (msg) {
    case 1:
        save_statics();
        MEM32(S_E10) = 0; MEM32(S_E14) = 0; MEM32(S_PAGE_PREV) = 0; MEM32(S_PAGE) = (uint32_t)s.page;
        MEM32(S_BUTTONS) = 0; MEM32(S_BTN_BUSY) = 0;
        MEM32(S_TITLE) = s.w_banner;
        MEM32(SHEET_ALIVE) = 0; MEM32(PAGE_MODE) = 0; MEM32(SHEET_RESTORE) = 0; MEM32(SHEET_C17904) = 0;
        s.armed = -1;
        break;
    case 3:
        MEM32(S_PAGES) = s.pages;
        MEM32(S_PAGE_N) = 2;
        if (MEM32(S_PAGE) >= 2u) MEM32(S_PAGE) = 1;
        s.page = (int)MEM32(S_PAGE);
        for (i = 0; i < 4; i++) s.pad_prev[i] = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
        if (s.pushed_show) {
            s.pushed_show = 0;
            nfl2k5_eras_draftshow_queue_clear();
            if (nfl2k5_eras_draft_next_slot() < 0) { s.page = 1; MEM32(S_PAGE) = 1; }
            nfl2k5_eras_log("draft: back from the show; %s", nfl2k5_eras_draft_user_on_clock() ? "the user's team is on the clock" : "ready");
        }
        break;
    case 4:
        page_init(fsm, s.page);
        break;
    case 5:
        gcall(sub_003275C0, fsm, 0);
        break;
    case 2:
        free_sheet();
        restore_statics();
        nfl2k5_eras_draft_close();
        break;
    }
    if (msg != 6) DL("msg %d\n", msg);
}

static uint32_t dispatch(int id, uint32_t ecx, uint32_t edx)
{
    if (id <= FN_MSG6) { if (id == FN_MSG6) gcall(sub_003276C0, ecx, 0); else event(id + 1, ecx); return 0; }
    switch (id) {
    case FN_BOARD_INIT: page_init(ecx, 0); return 0;
    case FN_ORDER_INIT: page_init(ecx, 1); return 0;
    case FN_FRAME: return page_frame(ecx);
    case FN_DRAW: page_draw(ecx); return 0;
    case FN_ACT: {
        /* A on (tab, row): page 0 arms / makes the user's pick. ecx is the tab value, not the FSM:
         * the FSM is the one the frame saw (s.fsm). */
        uint32_t row = edx;
        ecx = s.fsm;
        if (s.page != 0 || !nfl2k5_eras_draft_user_on_clock() || row >= (uint32_t)s.navail) { s.armed = -1; return 0; }
        if (s.armed == s.avail[row] && s.armed_row == (int)row) {
            int p = s.armed;
            s.armed = -1;
            user_pick(ecx, p);
        } else {
            const ErasProspect *e;
            s.armed = s.avail[row];
            s.armed_row = (int)row;
            e = nfl2k5_eras_draft_prospect(s.armed);
            snprintf(s.msg, sizeof s.msg, "%s %s, %s, %s (overall %d)", e->first, e->last, e->pos, e->college, e->ovr);
            g_eras_svc->toast("My NFL Eras", "Press A again to draft", s.msg, 4000);
            DL("armed %s %s (row %u)\n", e->first, e->last, (unsigned)row);
        }
        return 0;
    }
    case FN_B_TABVAL: return (uint32_t)s.group;
    case FN_B_TABCOUNT: return (uint32_t)NGROUPS;
    case FN_B_NEXT: case FN_B_PREV:
        s.group = (s.group + (id == FN_B_NEXT ? 1 : NGROUPS - 1)) % NGROUPS;
        s.armed = -1;
        refresh_avail();
        return 0;
    case FN_B_ROWS: return (uint32_t)s.navail;
    case FN_O_TABVAL: return (uint32_t)s.round;
    case FN_O_TABCOUNT: return (uint32_t)(nfl2k5_eras_draft_round_count() > 0 ? nfl2k5_eras_draft_round_count() : 1);
    case FN_O_NEXT: case FN_O_PREV: {
        int n = nfl2k5_eras_draft_round_count() > 0 ? nfl2k5_eras_draft_round_count() : 1;
        s.round = (s.round + (id == FN_O_NEXT ? 1 : n - 1)) % n;
        return 0;
    }
    case FN_O_ROWS: return (uint32_t)order_rows();
    default:
        if (id >= FN_BCOL0 && id < FN_BCOL0 + BCOLS) return board_text(id - FN_BCOL0, edx);
        if (id >= FN_OCOL0 && id < FN_OCOL0 + OCOLS) return order_text(id - FN_OCOL0, edx);
        return 0;
    }
}

static void ds_fn(void)
{
    uint32_t id = (s_called - DS_BASE) / 4u, ecx = g_ecx, edx = g_edx, r;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    r = dispatch((int)id, ecx, edx);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    g_eax = r;
    g_esp += 4;
}

recomp_func_t nfl2k5_eras_draft_sheets_lookup(uint32_t address)
{
    uint32_t off = address - DS_BASE;
    if (!s.built || address < DS_BASE || (off & 3u) || off / 4u >= (uint32_t)FN_COUNT) return NULL;
    s_called = address;
    return ds_fn;
}

/* ---- building ---------------------------------------------------------------------- */

static uint32_t events(void)
{
    static const int msgs[] = { 1, 2, 3, 4, 5, 6, 8 };
    uint32_t t = alloc(8u * 8u, 4), i;
    if (!t) return 0;
    for (i = 0; i < 7; i++) {
        uint32_t list = alloc(0x24u * 2u, 4);
        if (!list) return 0;
        MEM32(list + 0x00) = 1;
        MEM32(list + 0x04) = msgs[i] == 8 ? FN_MSG8 : fn_addr(msgs[i] - 1);
        MEM32(t + i * 8u) = (uint32_t)msgs[i];
        MEM32(t + i * 8u + 4u) = list;
    }
    return t;
}

static uint32_t text_column(int fn, const char *header, float width, uint32_t align)
{
    uint32_t c = alloc(0xB0, 16), h = wstr(header);
    if (!c || !h) return 0;
    gcopy(c, COL_TEXT_TMPL, 0xB0);
    MEM32(c + 0x04) = align;
    MEM32(c + 0x08) = fn_addr(fn);
    MEM32(c + 0x0C) = 0x00020039u;                     /* (tab value, row) */
    MEM32(c + 0x50) = fbits(width);
    MEM32(c + 0x70) = h;
    MEM32(c + 0x88) = h;
    return c;
}

static uint32_t sheet_build(int tabval, int tabcount, int next, int prev, uint32_t page)
{
    uint32_t p = alloc(0x100, 16);
    if (!p) return 0;
    gcopy(p, SHEET_WEEK, 0x100);
    MEM32(p + 0x30) = fn_addr(tabval);  MEM32(p + 0x34) = 0;
    MEM32(p + 0x60) = fn_addr(tabcount); MEM32(p + 0x64) = 0;
    MEM32(p + 0x78) = fn_addr(next);    MEM32(p + 0x7C) = 0;
    MEM32(p + 0x8C) = fn_addr(prev);    MEM32(p + 0x90) = 0;
    MEM32(p + 0xC4) = fn_addr(FN_ACT);                 /* A activation (args 0x00020039 kept) */
    MEM32(p + 0xDC) = fn_addr(FN_ACT);
    MEM32(p + 0xF4) = page;
    return p;
}

int nfl2k5_eras_draft_sheets_install(void)
{
    static const struct { const char *h; float w; uint32_t align; } k_bcols[BCOLS] = {
        { "#", 30.0f, 3 }, { "NAME", 170.0f, 1 }, { "POS", 44.0f, 3 }, { "SCHOOL", 170.0f, 1 }, { "OVR", 46.0f, 3 }, { "MOCK", 52.0f, 3 },
    };
    static const struct { const char *h; float w; uint32_t align; } k_ocols[OCOLS] = {
        { "PICK", 44.0f, 3 }, { "TEAM", 120.0f, 1 }, { "NAME", 150.0f, 1 }, { "POS", 44.0f, 3 }, { "SCHOOL", 160.0f, 1 },
    };
    int i, c, ok = 1;
    const char *e;
    char b[48];
    if (s.built) return 0;
    e = getenv("NFL2K5_ERAS_LOG"); s.log = e && *e && *e != '0';
    if (nfl2k5_eras_package_load()) return -1;
    s.size = 112u * 1024u;          /* the row text buffers alone are 6 x 64 + 5 x 40 x 96 bytes */
    s.mem = xbox_HeapAlloc(s.size, 16);
    if (!s.mem) { fprintf(stderr, "[ERAS] draft screen: no guest memory\n"); return -1; }
    s.desc = alloc(0x34, 16);
    s.pages = alloc(0x20, 16);
    s.page_board = alloc(0x9C + 4u * (BCOLS + 2u), 16);
    s.page_order = alloc(0x9C + 4u * (OCOLS + 2u), 16);
    if (!s.desc || !s.pages || !s.page_board || !s.page_order) ok = 0;
    if (ok) {
        gcopy(s.desc, DESC_SCHEDULE, 0x34);
        MEM32(s.desc + 0x04) = events();
        if (!MEM32(s.desc + 0x04)) ok = 0;
        MEM32(s.pages + 0x00) = fn_addr(FN_BOARD_INIT); MEM32(s.pages + 0x04) = FN_PAGE_LEAVE;
        MEM32(s.pages + 0x08) = fn_addr(FN_FRAME);      MEM32(s.pages + 0x0C) = fn_addr(FN_DRAW);
        MEM32(s.pages + 0x10) = fn_addr(FN_ORDER_INIT); MEM32(s.pages + 0x14) = FN_PAGE_LEAVE;
        MEM32(s.pages + 0x18) = fn_addr(FN_FRAME);      MEM32(s.pages + 0x1C) = fn_addr(FN_DRAW);
        gcopy(s.page_board, PAGE_WEEK, 0x9C);
        MEM32(s.page_board + 0x30) = fn_addr(FN_B_ROWS);
        for (c = 0; c < BCOLS; c++) {
            s.bcol[c] = text_column(FN_BCOL0 + c, k_bcols[c].h, k_bcols[c].w, k_bcols[c].align);
            if (!s.bcol[c]) ok = 0;
            MEM32(s.page_board + 0x9C + 4u * (uint32_t)c) = s.bcol[c];
            for (i = 0; i < BOARD_ROWS; i++) if (!(s.bout[c][i] = alloc(2u * STR_CH, 4))) ok = 0;
        }
        MEM32(s.page_board + 0x9C + 4u * BCOLS) = 0;
        gcopy(s.page_order, PAGE_WEEK, 0x9C);
        MEM32(s.page_order + 0x30) = fn_addr(FN_O_ROWS);
        for (c = 0; c < OCOLS; c++) {
            s.ocol[c] = text_column(FN_OCOL0 + c, k_ocols[c].h, k_ocols[c].w, k_ocols[c].align);
            if (!s.ocol[c]) ok = 0;
            MEM32(s.page_order + 0x9C + 4u * (uint32_t)c) = s.ocol[c];
            for (i = 0; i < ORDER_ROWS; i++) if (!(s.oout[c][i] = alloc(2u * STR_CH, 4))) ok = 0;
        }
        MEM32(s.page_order + 0x9C + 4u * OCOLS) = 0;
        s.sheet_board = sheet_build(FN_B_TABVAL, FN_B_TABCOUNT, FN_B_NEXT, FN_B_PREV, s.page_board);
        s.sheet_order = sheet_build(FN_O_TABVAL, FN_O_TABCOUNT, FN_O_NEXT, FN_O_PREV, s.page_order);
        if (!s.sheet_board || !s.sheet_order) ok = 0;
    }
    snprintf(b, sizeof b, "%d NFL DRAFT", nfl2k5_eras_draft_year());
    s.w_banner = wstr("THE BIG BOARD");
    s.w_title = wstr(b);
    s.w_sub = alloc(2u * 96u, 4);
    s.w_tab = alloc(2u * 16u, 4);
    s.w_help_board = wstr("for Draft Order / Recap");
    s.w_help_order = wstr("for The Big Board");
    if (!s.w_banner || !s.w_title || !s.w_sub || !s.w_tab || !s.w_help_board || !s.w_help_order) ok = 0;
    if (!ok) { fprintf(stderr, "[ERAS] draft screen: build failed (%u bytes used)\n", (unsigned)s.used); s.mem = 0; return -1; }
    s.built = 1;
    s.armed = -1;
    fprintf(stderr, "[ERAS] draft screen: descriptor %08X (Big Board / Draft Order on the retail sheet engine), %u bytes\n", (unsigned)s.desc, (unsigned)s.used);
    return 0;
}

int nfl2k5_eras_draft_sheets_ready(void) { return s.built; }

int nfl2k5_eras_draft_sheets_push(uint32_t fsm, char *msg, unsigned n)
{
    int next;
    if (!s.built) { if (msg) snprintf(msg, n, "Draft screen not built"); return -1; }
    if (nfl2k5_eras_draft_open(msg, n)) return -1;
    next = nfl2k5_eras_draft_next_slot();
    s.group = 0;
    s.round = next >= 0 ? nfl2k5_eras_draft_slot(next)->round - 1 : 0;
    s.page = next < 0 ? 1 : 0;
    s.armed = -1;
    s.pushed_show = 0;
    complete_check();                 /* a draft completed earlier whose rosters are not written yet */
    nfl2k5_eras_log("draft screen opened: %s; next %s", s.page ? "recap" : "the Big Board",
                    next >= 0 ? (nfl2k5_eras_draft_user_on_clock() ? "the user's pick" : "a CPU pick") : "none (complete)");
    gcall(sub_0006E390, fsm, s.desc);
    return 0;
}

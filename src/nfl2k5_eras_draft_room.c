/* My NFL Eras: the draft screens as copies of Franchise's own draft screens
 * (docs/eras/results/M15-draft-screens.md; the M13 sheet-engine screens in
 * nfl2k5_eras_draft_sheets.c stay as the fallback).
 *
 * Three descriptors, each a copy of a retail one with an Eras event table, an Eras table
 * wrapper and Eras sheet definitions, on the retail TABLE handler 0xF40F0, the retail layouts
 * and the retail card bindings (0xAECC7C: name, college, height, weight, photo of the selected
 * row, read from [0xCC0F50]):
 *
 *   THE BIG BOARD      copy of 0x53BE3C (layout rookie_pane): the prospect card above the list
 *                      NAME POS STOCK SCHOOL OVR, one page per position group (LT / RT, as the
 *                      Rookie Report pages positions), the COLLEGE STATISTICS row as a second
 *                      sheet. START advances (CPU picks, the show), Y skips to the user's pick,
 *                      A (or the advance reaching the user's pick) opens the candidates, R3 the
 *                      Draft Order.
 *   NFL DRAFT          copy of 0x5603E4 (the Draft room candidates): the same card and list
 *                      with the pick clock in the bar below; A drafts the selected prospect
 *                      (retail's own Yes / No prompt), START is retail's "CPU Complete Draft?",
 *                      B returns to the Big Board. The clock runs only on the user's pick; when
 *                      it expires the CPU picks for the user.
 *   DRAFT ORDER        copy of the in-draft Draft Summary 0x560168 (layout spreadsheet): ROUND
 *                      PICK NAME POS TEAM, one page per round plus All Rounds; it doubles as
 *                      the recap once the draft is complete.
 *
 * Data: the list rows are pointers to Eras-owned PLAYER RECORDS (0x54 bytes, the retail roster
 * record layout: college pointer +0, photo +6, type +8, name pointers +0x10 / +0x14, looks,
 * weight +0x2A, height +0x2B, position +0x35, 28 ratings +0x36) built in guest memory from
 * the league's class, so the retail NAME / POS / SCHOOL getters and the card read them
 * unchanged. Nothing in the roster arena, the Franchise blocks or the save is touched: the
 * retail list globals (0xCC00D0.., the arena's draft-class window) are never written. The
 * only retail statics used are [0xCC0F50] (the selected prospect, cleared on leave) and
 * [0xA84494] (the UI team for the backdrop, saved and restored).
 *
 * Retail functions reused: the table handler and its sheet engine, sub_0035FAF0 (the retail
 * msg 6 of these screens: selected row -> [0xCC0F50], the stats sheet frame), sub_0014E540
 * (Yes / No), sub_003273C0 (bar text), sub_000449E0 (resource lookup). Never called: the
 * retail list builders (sub_0035F140 / FCA0 / FCC0), the scouting readers (0x31Cxxx), the
 * Franchise draft code (0x3254xx..0x3266xx), the Rookie Card. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_sheets.h"
#include "nfl2k5_eras_draft.h"
#include "nfl2k5_eras_draftshow.h"
#include "nfl2k5_eras_draft_sheets.h"
#include "nfl2k5_eras_draft_room.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern recomp_func_t recomp_lookup(uint32_t xbox_va);
extern uint32_t nfl2k5_eras_menu_header(int which);     /* 2 = the draft show screen (EM_DRAFT) */

#define DR_BASE         NFL2K5_ERAS_DRAFT_ROOM_BASE
#define RET_ADDR        (DR_BASE + 0x7FF0u)

/* retail descriptors, tables, sheets, pages and columns (docs/eras/results/M15-draft-screens.md 1) */
#define DESC_BIGBOARD   0x0053BE3Cu   /* The Big Board: table handler, layout rookie_pane, flags 0x15 */
#define DESC_CANDS      0x005603E4u   /* NFL Draft (the Draft room candidates), flags 0xC6 */
#define DESC_SUMMARY    0x00560168u   /* NFL Draft (the in-draft Draft Summary), layout spreadsheet */
#define TABLE_BIGBOARD  0x0053BE00u   /* {players sheet, rect} {stats sheet, rect} 0 */
#define TABLE_CANDS     0x005603A8u
#define TABLE_SUMMARY   0x00560140u
#define SHEET_ROOKIE    0x0053B568u   /* the Rookie Report sheet (18 position pages): the template */
#define SHEET_SUMMARY   0x0055FB88u
#define SHEET_STATS     0x0053BAE8u
#define PAGE_ROOKIE_ALL 0x00539900u   /* "All Positions": rows sub_0035FD40 (0x10008), row object sub_0035FD80 (0x20083) */
#define PAGE_SUMMARY_1  0x0055E088u   /* "Round 1": rows (0x10008), row object (0x20038) */
#define PAGE_STATS      0x0053B370u   /* one row, four columns */
#define COL_RANK        0x00537E68u   /* type 8, "1. " */
#define COL_NAME        0x00542518u   /* retail: sub_00145B60(record) */
#define COL_POS         0x005408E8u   /* retail: sub_000E5FC0(record) */
#define COL_STOCK       0x00538338u   /* type 8, the |COMB...| arrows */
#define COL_SCHOOL      0x005411D8u   /* retail: [[record]] (type 0 text) */
#define COL_ROUND       0x0055DB08u   /* type 5 (integer getter), right aligned */
#define COL_PICK        0x0055DBB8u
#define COL_TEAM        0x0055DD18u   /* type 0 text, left */
#define COL_HDR_LITERAL 0x00010000u
#define W_EMPTY         0x00EB65F8u
#define W_COMBGREEN     0x00EB64DCu   /* "|COMBGREEN|" (stock up) */
#define W_COMBRED       0x00EB64F4u
#define W_COMBYELLOW    0x00EB6508u
#define W_DASHES        0x00EB64D4u   /* "---" */
#define FOURCC_LAYT     0x5459414Cu
#define UI_TEAM         0x00A84494u
#define SEL_PROSPECT    0x00CC0F50u   /* the retail card's selected record */
#define RT_SHEET0       0x65Cu        /* the table handler's sheets in the FSM runtime (stride 0x108) */
#define SH_DEF          0x58u         /* sheet widget: definition, current page pointer, objects, rows, cursor */
#define SH_PAGE         0x5Cu
#define SH_OBJECTS      0x40u
#define SH_ROWS         0xA4u
#define SH_CURSOR       0xBCu
#define REC_SIZE        0x54u
#define NAME_CH         32u
#define COLLEGE_CH      40u
#define PHOTO_NONE      39999u        /* no portrait has it: the layout's "NO PHOTO" */
#define TXT_CH          128u
#define NTXT            8

#define NG              10            /* position groups (nfl2k5_eras_draft_group_name) */
#define MAXR            14            /* rounds */
#define NEV             11            /* event slots per screen: msgs 1 2 3 4 5 6 8 11 12 13 18 */

enum { SC_BB, SC_CAND, SC_ORD, SC_N };
enum { PEND_NONE, PEND_CANDS, PEND_POP };
enum { FN_EV0 = 0,
       FN_B_ROWS = SC_N * NEV, FN_B_OBJ, FN_O_ROWS, FN_O_OBJ,
       FN_RANK, FN_STOCK, FN_OVR, FN_ROUND, FN_PICK, FN_TEAM, FN_STAT0, FN_STAT1, FN_STAT2, FN_STAT3,
       FN_COUNT };
static const int k_msgs[NEV] = { 1, 2, 3, 4, 5, 6, 8, 11, 12, 13, 18 };
static const char *const k_page_name[NG] = { "All Positions", "Quarterbacks", "Running Backs", "Wide Receivers", "Tight Ends",
                                             "Offensive Line", "Defensive Line", "Linebackers", "Defensive Backs", "Kickers / Punters" };

static struct {
    int built, log, depth;
    uint32_t mem, used, size;
    uint32_t desc[SC_N], wrap[SC_N], sheet[SC_N], sheet_stats, page_stats;
    uint32_t page_b[NG], page_c[NG], page_o[MAXR + 1];
    uint32_t col_rank, col_stock, col_ovr, col_round, col_pick, col_team, col_stat[4];
    uint32_t rec, slotrec, names, colleges, blank_college;
    int ncolleges;
    uint32_t txt[NTXT]; int txt_i;
    uint32_t w_layout, w_modal, w_help[6];
    /* lists */
    uint32_t list[NFL2K5_DRAFT_MAX_PROSPECTS];
    int list_n, list_group, list_dirty, slots_dirty, recs_built, np_built;
    int rank_all[NFL2K5_DRAFT_MAX_PROSPECTS];     /* board rank in ALL (0-based), -1 taken */
    /* runtime */
    int scr;                        /* the screen whose message ran last */
    uint32_t fsm;
    int page[SC_N], cursor[SC_N], page_set[SC_N];
    int pending, need_rebuild, pushed_show, goto_cands, auto_all, in_modal, armed, arm_mode;
    int ui_saved; uint32_t ui_prev;
    int clock_slot, clock_on; DWORD deadline;
    char msg[160];
} s;

static RECOMP_TLS uint32_t s_called;

#define DL(...) do { if (s.log) fprintf(stderr, "[ERAS] draft room " __VA_ARGS__); } while (0)

/* ---- guest helpers (as nfl2k5_eras_draft_sheets.c) ------------------------------- */

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

static uint32_t fn_addr(int id) { return DR_BASE + (uint32_t)id * 4u; }

/* a getter's text: the next of the ring (the engine copies it at once, as it does retail's 0xCC0D48) */
static uint32_t text(const char *t)
{
    uint32_t p = s.txt[s.txt_i++ % NTXT];
    wput(p, t, TXT_CH);
    return p;
}

static uint32_t runtime(uint32_t fsm) { return fsm ? gcall(sub_0006E2D0, fsm, 0) : 0; }
static uint32_t widget0(uint32_t fsm) { uint32_t r = runtime(fsm); return r ? r + RT_SHEET0 : 0; }

/* ---- records ------------------------------------------------------------------------- */

static uint32_t college_of(const char *name)
{
    int i;
    uint32_t c;
    if (!name || !*name) return s.blank_college;
    for (i = 0; i < s.ncolleges; i++) {
        uint32_t w = MEM32(s.colleges + (uint32_t)i * (8u + 2u * COLLEGE_CH));
        size_t k;
        for (k = 0; k < COLLEGE_CH - 1 && name[k] && MEM16(w + 2u * (uint32_t)k) == (uint16_t)(unsigned char)name[k]; k++) {}
        if ((!name[k] || k == COLLEGE_CH - 1) && MEM16(w + 2u * (uint32_t)k) == 0) return s.colleges + (uint32_t)i * (8u + 2u * COLLEGE_CH);
    }
    if (s.ncolleges >= NFL2K5_DRAFT_MAX_PROSPECTS) return s.blank_college;
    c = s.colleges + (uint32_t)s.ncolleges * (8u + 2u * COLLEGE_CH);
    s.ncolleges++;
    MEM32(c + 0) = c + 8;                 /* +0 name pointer (the card and the SCHOOL column read [[record]]) */
    MEM32(c + 4) = (uint32_t)s.ncolleges; /* +4 index */
    wput(c + 8, name, COLLEGE_CH);
    return c;
}

static uint32_t rec_of(int i) { return s.rec + (uint32_t)i * REC_SIZE; }
static uint32_t slotrec_of(int i) { return s.slotrec + (uint32_t)i * REC_SIZE; }
static int prospect_of(uint32_t rec)
{
    if (rec >= s.rec && rec < s.rec + (uint32_t)NFL2K5_DRAFT_MAX_PROSPECTS * REC_SIZE && (rec - s.rec) % REC_SIZE == 0) return (int)((rec - s.rec) / REC_SIZE);
    if (rec >= s.slotrec && rec < s.slotrec + (uint32_t)NFL2K5_DRAFT_MAX_SLOTS * REC_SIZE && (rec - s.slotrec) % REC_SIZE == 0) {
        const ErasDraftSlot *sl = nfl2k5_eras_draft_slot((int)((rec - s.slotrec) / REC_SIZE));
        return sl ? sl->prospect : -1;
    }
    return -1;
}
static int slot_of(uint32_t rec)
{
    if (rec >= s.slotrec && rec < s.slotrec + (uint32_t)NFL2K5_DRAFT_MAX_SLOTS * REC_SIZE && (rec - s.slotrec) % REC_SIZE == 0) return (int)((rec - s.slotrec) / REC_SIZE);
    return -1;
}

/* One prospect into a retail-layout record (the fields src/nfl2k5_team_files.c writes for a staged player). */
static void build_record(uint32_t pl, const ErasProspect *e, uint32_t wfirst, uint32_t wlast)
{
    uint32_t i, w;
    int skin = e->skin & 0x1F, weight = e->weight > 150 ? e->weight - 150 : 0;
    for (i = 0; i < REC_SIZE; i += 4) MEM32(pl + i) = 0;
    MEM32(pl + 0x00) = college_of(e->college);
    MEM16(pl + 0x04) = 0;
    MEM16(pl + 0x06) = (uint16_t)PHOTO_NONE;
    MEM8(pl + 0x08) = 0x10;                                    /* a draft prospect (bit 4) */
    MEM32(pl + 0x10) = wfirst;
    MEM32(pl + 0x14) = wlast;
    MEM8(pl + 0x18) = (uint8_t)(((skin & 1) << 7) | ((e->hand & 1) << 1));
    MEM8(pl + 0x19) = (uint8_t)(skin >> 1);
    w = ((uint32_t)(e->number & 0x7F) << 3) | ((uint32_t)(e->face & 0xF) << 17);
    MEM32(pl + 0x20) = w;
    MEM8(pl + 0x25) = 0;                                       /* years pro */
    MEM8(pl + 0x2A) = (uint8_t)(weight > 255 ? 255 : weight);
    MEM8(pl + 0x2B) = (uint8_t)e->height;
    MEM8(pl + 0x35) = (uint8_t)(e->pos_code >= 0 && e->pos_code < 17 ? e->pos_code : 0);
    for (i = 0; i < NFL2K5_DRAFT_RATINGS; i++) MEM8(pl + 0x36 + i) = e->r[i];
}

static void build_records(void)
{
    int np = nfl2k5_eras_draft_prospect_count(), i;
    s.ncolleges = 0;
    for (i = 0; i < np && i < NFL2K5_DRAFT_MAX_PROSPECTS; i++) {
        const ErasProspect *e = nfl2k5_eras_draft_prospect(i);
        uint32_t nf = s.names + (uint32_t)i * 4u * NAME_CH, nl = nf + 2u * NAME_CH;
        wput(nf, e->first, NAME_CH);
        wput(nl, e->last, NAME_CH);
        build_record(rec_of(i), e, nf, nl);
    }
    s.np_built = np;
    s.recs_built = 1;
    s.list_dirty = s.slots_dirty = 1;
    DL("%d prospect records, %d colleges (%u bytes of guest memory used)\n", np, s.ncolleges, (unsigned)s.used);
}

/* The slot records of the order board: the pick's record, or a blank one (the next slot says so). */
static void build_slots(void)
{
    int ns = nfl2k5_eras_draft_slot_count(), next = nfl2k5_eras_draft_next_slot(), i;
    uint32_t wnone = s.names + (uint32_t)NFL2K5_DRAFT_MAX_PROSPECTS * 4u * NAME_CH;   /* L"" then L"On the clock" */
    for (i = 0; i < ns && i < NFL2K5_DRAFT_MAX_SLOTS; i++) {
        const ErasDraftSlot *sl = nfl2k5_eras_draft_slot(i);
        uint32_t pl = slotrec_of(i), k;
        if (sl->prospect >= 0 && sl->prospect < s.np_built) { gcopy(pl, rec_of(sl->prospect), REC_SIZE); continue; }
        for (k = 0; k < REC_SIZE; k += 4) MEM32(pl + k) = 0;
        MEM32(pl + 0x00) = s.blank_college;
        MEM16(pl + 0x06) = (uint16_t)PHOTO_NONE;
        MEM8(pl + 0x08) = 0x10;
        MEM32(pl + 0x10) = wnone;
        MEM32(pl + 0x14) = i == next ? wnone + 2u * NAME_CH : wnone;
        MEM8(pl + 0x35) = 0;
    }
    s.slots_dirty = 0;
}

/* The board list of a group, and every prospect's rank in ALL (for the stock arrows). */
static void build_list(int group)
{
    static int idx[NFL2K5_DRAFT_MAX_PROSPECTS];
    int n, i;
    if (!s.recs_built) return;
    n = nfl2k5_eras_draft_available(0, idx, NFL2K5_DRAFT_MAX_PROSPECTS);
    for (i = 0; i < NFL2K5_DRAFT_MAX_PROSPECTS; i++) s.rank_all[i] = -1;
    for (i = 0; i < n; i++) s.rank_all[idx[i]] = i;
    if (group) n = nfl2k5_eras_draft_available(group, idx, NFL2K5_DRAFT_MAX_PROSPECTS);
    for (i = 0; i < n; i++) s.list[i] = rec_of(idx[i]);
    s.list_n = n;
    s.list_group = group;
    s.list_dirty = 0;
}

static void lists_dirty(void) { s.list_dirty = 1; s.slots_dirty = 1; s.need_rebuild = 1; }

/* ---- the backdrop team ---------------------------------------------------------------- */

static void set_ui_team(void)
{
    int u = nfl2k5_eras_user_team(), next = nfl2k5_eras_draft_next_slot(), t = u;
    uint32_t rec;
    if (t < 0 && next >= 0) t = nfl2k5_eras_draft_slot(next)->team;
    if (t < 0) t = 0;
    rec = nfl2k5_eras_sheets_team_rec(t);
    if (!s.ui_saved) { s.ui_prev = MEM32(UI_TEAM); s.ui_saved = 1; }
    if (rec) MEM32(UI_TEAM) = rec;
}

static void restore_ui_team(void)
{
    if (s.ui_saved) { MEM32(UI_TEAM) = s.ui_prev; s.ui_saved = 0; }
}

/* ---- the show, picks, the clock ----------------------------------------------------- */

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

static int show_if_queued(uint32_t fsm)
{
    uint32_t hdr = nfl2k5_eras_menu_header(2);
    int n = nfl2k5_eras_draftshow_queue_count();
    if (!n) return 0;
    if (!hdr || !nfl2k5_eras_draftshow_ready()) {
        nfl2k5_eras_log("draft: %d pick(s) would go to the show, but the show is not available (%s)", n, hdr ? "pack files missing" : "no screen");
        nfl2k5_eras_draftshow_queue_clear();
        return 0;
    }
    nfl2k5_eras_log("draft: the show plays %d pick(s)", n);
    s.pushed_show = 1;
    gcall(sub_0006E390, fsm, hdr);
    return 1;
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

static int clock_seconds(int round)
{
    const char *e = getenv("NFL2K5_ERAS_PICK_CLOCK");
    if (e && *e) return atoi(e);
    return round <= 1 ? 210 : 150;          /* 3:30 in round 1 (retail's Draft room bar), 2:30 after */
}

static void clock_start(void)
{
    int next = nfl2k5_eras_draft_next_slot();
    const ErasDraftSlot *sl = next >= 0 ? nfl2k5_eras_draft_slot(next) : NULL;
    int secs;
    if (!sl || !nfl2k5_eras_draft_user_on_clock()) { s.clock_on = 0; return; }
    if (s.clock_on && s.clock_slot == next) return;             /* back from the show or the board: the same pick keeps its deadline */
    secs = clock_seconds(sl->round);
    s.clock_slot = next;
    s.clock_on = secs > 0;
    s.deadline = GetTickCount() + (DWORD)secs * 1000u;
    DL("clock: %d s for R%d #%d\n", secs, sl->round, sl->pick);
}

/* After the user's slot is filled (A, the clock, CPU Complete Draft): the show, then leave the candidates. */
static void after_user_slot(uint32_t fsm, int slot)
{
    nfl2k5_eras_draftshow_queue_clear();
    s.clock_on = 0;
    lists_dirty();
    complete_check();
    if (show_covers(slot)) { queue_slot(slot); if (show_if_queued(fsm)) return; }
    if (s.scr == SC_CAND) s.pending = PEND_POP;
}

/* START / Y on the board: CPU picks up to the user's pick or the end; the covered picks go to the show. */
static void advance(uint32_t fsm, int with_show)
{
    static int made[NFL2K5_DRAFT_MAX_SLOTS];
    int n = 0, i, queued = 0;
    if (nfl2k5_eras_draft_next_slot() < 0) return;
    if (nfl2k5_eras_draft_user_on_clock() && !s.auto_all) {
        DL("advance: the user is on the clock -> candidates\n");
        if (s.scr != SC_CAND) s.pending = PEND_CANDS;
        return;
    }
    nfl2k5_eras_draftshow_queue_clear();
    if (s.auto_all) {
        int slot;
        while ((slot = nfl2k5_eras_draft_cpu_pick()) >= 0) { if (n < NFL2K5_DRAFT_MAX_SLOTS) made[n] = slot; n++; }
    } else n = nfl2k5_eras_draft_sim(made, NFL2K5_DRAFT_MAX_SLOTS);
    if (with_show) for (i = 0; i < n && i < NFL2K5_DRAFT_MAX_SLOTS; i++) if (show_covers(made[i])) { queue_slot(made[i]); queued++; }
    nfl2k5_eras_log("draft: advanced %d CPU pick(s)%s; %s", n, queued ? "" : " (none for the show)",
                    nfl2k5_eras_draft_user_on_clock() ? "the user's team is on the clock" : nfl2k5_eras_draft_next_slot() < 0 ? "the draft is complete" : "stopped");
    lists_dirty();
    complete_check();
    s.goto_cands = nfl2k5_eras_draft_user_on_clock();
    if (queued && show_if_queued(fsm)) return;
    if (s.goto_cands && s.scr != SC_CAND) { s.pending = PEND_CANDS; s.goto_cands = 0; }
    if (nfl2k5_eras_draft_next_slot() < 0 && s.scr == SC_CAND) s.pending = PEND_POP;
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
    after_user_slot(fsm, slot);
}

static void clock_expired(uint32_t fsm)
{
    int slot;
    const ErasDraftSlot *sl;
    const ErasProspect *e;
    s.clock_on = 0;
    if (!nfl2k5_eras_draft_user_on_clock()) return;
    slot = nfl2k5_eras_draft_cpu_pick();                 /* the CPU picks for the team on the clock: the user's */
    sl = slot >= 0 ? nfl2k5_eras_draft_slot(slot) : NULL;
    e = sl && sl->prospect >= 0 ? nfl2k5_eras_draft_prospect(sl->prospect) : NULL;
    if (!e) return;
    snprintf(s.msg, sizeof s.msg, "Time expired: the pick is %s %s, %s, %s", e->first, e->last, e->pos, e->college);
    g_eras_svc->toast("My NFL Eras", "The clock ran out", s.msg, 6000);
    nfl2k5_eras_log("draft: the pick clock expired at R%d #%d: %s %s", sl->round, sl->pick, e->first, e->last);
    after_user_slot(fsm, slot);
}

/* The selected row's record of the focused (first) sheet, as retail's A handler reads it. */
static uint32_t selected_record(uint32_t fsm)
{
    uint32_t w = widget0(fsm), cur, objs;
    if (!w) return 0;
    cur = MEM32(w + SH_CURSOR);
    if (cur >= MEM32(w + SH_ROWS)) return 0;
    objs = MEM32(w + SH_OBJECTS);
    return objs ? MEM32(objs + 4u * cur) : 0;
}

static void draft_selected(uint32_t fsm)
{
    uint32_t rec = selected_record(fsm);
    int p = prospect_of(rec), yes;
    const ErasProspect *e = p >= 0 ? nfl2k5_eras_draft_prospect(p) : NULL;
    const ErasTeam *t = nfl2k5_eras_team(nfl2k5_eras_user_team());
    if (!e || !t || e->taken_slot >= 0 || !nfl2k5_eras_draft_user_on_clock() || s.in_modal) return;
    if (s.arm_mode) {
        /* NFL2K5_ERAS_DRAFT_CONFIRM=arm: M13's two presses (no modal) */
        if (s.armed == p) { s.armed = -1; user_pick(fsm, p); return; }
        s.armed = p;
        snprintf(s.msg, sizeof s.msg, "%s %s, %s, %s (overall %d)", e->first, e->last, e->pos, e->college, e->ovr);
        g_eras_svc->toast("My NFL Eras", "Press A again to draft", s.msg, 4000);
        return;
    }
    snprintf(s.msg, sizeof s.msg, "Draft %s %s (%s, %s) to the %s?", e->first, e->last, e->pos, e->college, t->name);
    wput(s.w_modal, s.msg, 160);
    s.in_modal = 1;
    yes = (int)gcall(sub_0014E540, fsm, s.w_modal);       /* retail's Yes / No; 1 = Yes */
    s.in_modal = 0;
    DL("draft %s %s: %s\n", e->first, e->last, yes == 1 ? "yes" : "no");
    if (yes == 1) user_pick(fsm, p);
}

static void cpu_complete_prompt(uint32_t fsm)
{
    int yes;
    if (s.in_modal) return;
    wput(s.w_modal, "CPU Complete Draft?", 160);           /* retail's own prompt text (0xEA3B2C) */
    s.in_modal = 1;
    yes = (int)gcall(sub_0014E540, fsm, s.w_modal);
    s.in_modal = 0;
    if (yes != 1) return;
    s.auto_all = 1;
    s.clock_on = 0;
    nfl2k5_eras_log("draft: CPU Complete Draft (the CPU picks for the user's team from here)");
    advance(fsm, 1);
    if (!nfl2k5_eras_draftshow_queued() && s.scr == SC_CAND) s.pending = PEND_POP;
}

/* ---- the sheet widget ------------------------------------------------------------------ */

static int page_count(uint32_t w)
{
    uint32_t def = MEM32(w + SH_DEF), k = 0;
    if (!def) return 0;
    while (MEM32(def + 0xF4u + 4u * k) && k < 64) k++;
    return (int)k;
}

static int page_index(uint32_t w)
{
    uint32_t def = MEM32(w + SH_DEF), cur = MEM32(w + SH_PAGE);
    if (!def || cur < def + 0xF4u) return 0;
    return (int)((cur - def - 0xF4u) / 4u);
}

static void remember(uint32_t fsm)
{
    uint32_t w = widget0(fsm);
    if (!w || !MEM32(w + SH_DEF) || s.scr < 0 || s.scr >= SC_N) return;
    s.page[s.scr] = page_index(w);
    s.cursor[s.scr] = (int)MEM32(w + SH_CURSOR);
}

static void rebuild_page(uint32_t fsm)
{
    uint32_t w = widget0(fsm), rows;
    if (!w || !MEM32(w + SH_DEF)) return;
    if (s.slots_dirty) build_slots();
    s.list_dirty = 1;
    g_eax = w;
    gcall(sub_00174140, w, 0);                              /* the current page again: rows and objects */
    rows = MEM32(w + SH_ROWS);
    if (MEM32(w + SH_CURSOR) >= rows) gcall(sub_001707C0, w, rows ? rows - 1u : 0u);
    s.need_rebuild = 0;
}

static void help(uint32_t fsm, uint32_t slot, uint32_t text_)
{
    PUSH32(g_esp, text_);
    gcall(sub_000F35A0, fsm, slot);
}

static void draw_bar_text(uint32_t x, uint32_t y, uint32_t text_, uint32_t align)
{
    PUSH32(g_esp, align);
    PUSH32(g_esp, text_);
    gcall(sub_003273C0, x, y);
}

/* ---- events ---------------------------------------------------------------------------- */

static void ev_created(int k)
{
    s.depth++;
    s.scr = k;
    s.armed = -1;
    DL("%s created (depth %d)\n", k == SC_BB ? "Big Board" : k == SC_CAND ? "candidates" : "order", s.depth);
}

static void ev_activated(int k, uint32_t fsm)
{
    int next;
    s.scr = k;
    s.fsm = fsm;
    s.armed = -1;
    MEM32(SEL_PROSPECT) = 0;
    if (s.pushed_show) {
        s.pushed_show = 0;
        nfl2k5_eras_draftshow_queue_clear();
        nfl2k5_eras_log("draft: back from the show; %s", nfl2k5_eras_draft_user_on_clock() ? "the user's team is on the clock" : "ready");
    }
    if (!s.recs_built) build_records();
    set_ui_team();
    lists_dirty();
    next = nfl2k5_eras_draft_next_slot();
    if (k == SC_CAND) {
        if (!nfl2k5_eras_draft_user_on_clock()) s.pending = PEND_POP;
    } else if (k == SC_BB && s.goto_cands && nfl2k5_eras_draft_user_on_clock()) {
        s.goto_cands = 0;
        s.pending = PEND_CANDS;
    }
    if (k == SC_ORD && !s.page_set[SC_ORD]) {
        int r = next >= 0 ? nfl2k5_eras_draft_slot(next)->round - 1 : nfl2k5_eras_draft_round_count() - 1, first;
        s.page[SC_ORD] = r < 0 ? 0 : r;
        first = nfl2k5_eras_draft_round_first(s.page[SC_ORD] + 1);
        s.cursor[SC_ORD] = next >= 0 && first >= 0 ? next - first : 0;
        s.page_set[SC_ORD] = 1;
    }
    complete_check();
}

static void ev_shown(int k, uint32_t fsm)
{
    uint32_t w = widget0(fsm);
    int n;
    gcall(sub_000F32C0, fsm, 0);                            /* LT / RT (and White / Black) page the sheet */
    if (w && MEM32(w + SH_DEF)) {
        n = page_count(w);
        if (s.page[k] >= 0 && s.page[k] < n && s.page[k] != page_index(w)) gcall(sub_00174D30, w, (uint32_t)s.page[k]);
        if (s.slots_dirty) build_slots();
        s.list_dirty = 1;
        g_eax = w;
        gcall(sub_00174140, w, 0);
        gcall(sub_001707C0, w, (uint32_t)(s.cursor[k] < 0 ? 0 : s.cursor[k]));
        s.need_rebuild = 0;
    }
    if (k == SC_CAND) clock_start();
    DL("%s shown: page %d, cursor %d, %u rows\n", k == SC_BB ? "Big Board" : k == SC_CAND ? "candidates" : "order",
       s.page[k], s.cursor[k], w ? (unsigned)MEM32(w + SH_ROWS) : 0u);
}

static void ev_covered(int k, uint32_t fsm)
{
    s.scr = k;
    remember(fsm);
    MEM32(SEL_PROSPECT) = 0;
}

static void ev_destroyed(int k, uint32_t fsm)
{
    (void)fsm;
    s.scr = k;
    s.depth--;
    MEM32(SEL_PROSPECT) = 0;
    if (s.depth <= 0) {
        s.depth = 0;
        restore_ui_team();
        nfl2k5_eras_draft_close();
        s.recs_built = 0;
        s.pending = PEND_NONE;
        s.auto_all = 0;
        s.clock_on = 0;
        memset(s.page_set, 0, sizeof s.page_set);
        DL("left the draft screens\n");
    }
}

static void ev_frame(int k, uint32_t fsm)
{
    s.scr = k;
    s.fsm = fsm;
    if (k != SC_ORD) gcall(sub_0035FAF0, fsm, 0);           /* retail: selected row -> [0xCC0F50]; the stats sheet frame */
    if (s.need_rebuild) rebuild_page(fsm);
    if (s.pending == PEND_CANDS) {
        s.pending = PEND_NONE;
        if (nfl2k5_eras_draft_user_on_clock()) { DL("-> candidates\n"); gcall(sub_0006E390, fsm, s.desc[SC_CAND]); return; }
    } else if (s.pending == PEND_POP) {
        s.pending = PEND_NONE;
        DL("<- back from the candidates\n");
        gcall(sub_0006E400, fsm, 0);
        return;
    }
    if (k == SC_CAND && s.clock_on && (int32_t)(GetTickCount() - s.deadline) >= 0) clock_expired(fsm);
    /* the help bar (slots: 8 Y, 9 START, 0xB RS) */
    help(fsm, 0xD, 0);
    help(fsm, 0xF, 0);
    if (k == SC_CAND) {
        help(fsm, 8, 0);
        help(fsm, 9, s.w_help[3]);
        help(fsm, 0xB, 0);
    } else {
        help(fsm, 8, nfl2k5_eras_user_team() >= 0 && nfl2k5_eras_draft_next_slot() >= 0 ? s.w_help[1] : 0);
        help(fsm, 9, nfl2k5_eras_draft_next_slot() >= 0 ? s.w_help[0] : 0);
        help(fsm, 0xB, k == SC_BB ? s.w_help[2] : s.w_help[4]);
    }
}

static void ev_overlay(int k, uint32_t fsm)
{
    char b[96];
    int next = nfl2k5_eras_draft_next_slot();
    const ErasDraftSlot *sl = next >= 0 ? nfl2k5_eras_draft_slot(next) : NULL;
    const ErasTeam *t = sl ? nfl2k5_eras_team(sl->team) : NULL;
    (void)fsm;
    if (k != SC_CAND || !sl) return;
    if (s.clock_on) {
        int32_t left = (int32_t)(s.deadline - GetTickCount()) / 1000;
        if (left < 0) left = 0;
        snprintf(b, sizeof b, "%d:%02d   Round %d  -  Pick %d  -  %s", left / 60, left % 60, sl->round, sl->pick, t ? t->name : "");
    } else snprintf(b, sizeof b, "Round %d  -  Pick %d  -  %s", sl->round, sl->pick, t ? t->name : "");
    draw_bar_text(0x4Bu, 0x19Cu, text(b), 1);
}

static void ev_start(int k, uint32_t fsm)
{
    s.fsm = fsm;
    if (k == SC_CAND) cpu_complete_prompt(fsm);
    else advance(fsm, 1);
}

static void ev_a(int k, uint32_t fsm)
{
    s.fsm = fsm;
    if (k == SC_CAND) draft_selected(fsm);
    else if (k == SC_BB && nfl2k5_eras_draft_user_on_clock()) s.pending = PEND_CANDS;
}

static void ev_y(int k, uint32_t fsm)
{
    s.fsm = fsm;
    if (k != SC_CAND) advance(fsm, 0);                      /* skip to my pick: no show */
}

static void ev_rstick(int k, uint32_t fsm)
{
    s.fsm = fsm;
    if (k == SC_CAND) return;
    remember(fsm);
    DL("%s -> %s\n", k == SC_BB ? "Big Board" : "order", k == SC_BB ? "order" : "Big Board");
    gcall(sub_0006E2E0, fsm, s.desc[k == SC_BB ? SC_ORD : SC_BB]);   /* replace the top screen */
}

static void event(int k, int msg, uint32_t fsm)
{
    switch (msg) {
    case 1: ev_created(k); break;
    case 2: ev_destroyed(k, fsm); break;
    case 3: ev_activated(k, fsm); break;
    case 4: ev_shown(k, fsm); break;
    case 5: ev_covered(k, fsm); break;
    case 6: ev_frame(k, fsm); break;
    case 8: ev_overlay(k, fsm); break;
    case 11: ev_start(k, fsm); break;
    case 12: ev_a(k, fsm); break;
    case 13: ev_y(k, fsm); break;
    case 18: ev_rstick(k, fsm); break;
    }
    if (msg != 6 && msg != 8) DL("screen %d msg %d\n", k, msg);
}

/* ---- rows, row objects and column getters ----------------------------------------------- */

static uint32_t board_rows(uint32_t value)
{
    int g = (int)value;
    if (g < 0 || g >= NG) g = 0;
    if (s.list_dirty || s.list_group != g) build_list(g);
    return (uint32_t)s.list_n;
}

static uint32_t board_obj(uint32_t row, uint32_t value)
{
    board_rows(value);
    return row < (uint32_t)s.list_n ? s.list[row] : slotrec_of(NFL2K5_DRAFT_MAX_SLOTS - 1);
}

static int order_first(uint32_t value, int *count)
{
    int rounds = nfl2k5_eras_draft_round_count(), ns = nfl2k5_eras_draft_slot_count(), first, n = 0, i;
    if ((int)value >= rounds) { *count = ns; return 0; }                  /* All Rounds */
    first = nfl2k5_eras_draft_round_first((int)value + 1);
    if (first < 0) { *count = 0; return 0; }
    for (i = first; i < ns && nfl2k5_eras_draft_slot(i)->round == (int)value + 1; i++) n++;
    *count = n;
    return first;
}

static uint32_t order_rows(uint32_t value)
{
    int n;
    if (s.slots_dirty) build_slots();
    order_first(value, &n);
    return (uint32_t)n;
}

static uint32_t order_obj(uint32_t row, uint32_t value)
{
    int n, first = order_first(value, &n);
    if (s.slots_dirty) build_slots();
    return (int)row < n ? slotrec_of(first + (int)row) : slotrec_of(NFL2K5_DRAFT_MAX_SLOTS - 1);
}

static uint32_t col_rank(uint32_t rec)
{
    int p = prospect_of(rec), i;
    char b[16];
    if (p < 0) return W_EMPTY;
    for (i = 0; i < s.list_n; i++) if (s.list[i] == rec) { snprintf(b, sizeof b, "%d. ", i + 1); return text(b); }
    return W_EMPTY;
}

/* STOCK: the board's own rank in ALL (by overall, 28 picks a round) against the round the real
 * draft took him in (the class's "mock"): higher on the board = up, lower = down. */
static uint32_t col_stock(uint32_t rec)
{
    int p = prospect_of(rec), expected, teams = nfl2k5_eras_team_count() > 0 ? nfl2k5_eras_team_count() : 28;
    const ErasProspect *e = p >= 0 ? nfl2k5_eras_draft_prospect(p) : NULL;
    if (!e || s.rank_all[p] < 0) return W_DASHES;
    expected = s.rank_all[p] / teams + 1;
    if (expected < e->round) return W_COMBGREEN;
    if (expected > e->round) return W_COMBRED;
    return W_COMBYELLOW;
}

static uint32_t col_ovr(uint32_t rec)
{
    int p = prospect_of(rec);
    const ErasProspect *e = p >= 0 ? nfl2k5_eras_draft_prospect(p) : NULL;
    char b[16];
    if (!e) return W_EMPTY;
    snprintf(b, sizeof b, "%d%s", e->ovr, !strcmp(e->source, "estimated") ? "*" : "");
    return text(b);
}

static uint32_t col_round(uint32_t rec) { int i = slot_of(rec); return i >= 0 ? (uint32_t)nfl2k5_eras_draft_slot(i)->round : 0u; }
static uint32_t col_pick(uint32_t rec) { int i = slot_of(rec); return i >= 0 ? (uint32_t)nfl2k5_eras_draft_slot(i)->pick : 0u; }

static uint32_t col_team(uint32_t rec)
{
    int i = slot_of(rec);
    const ErasTeam *t = i >= 0 ? nfl2k5_eras_team(nfl2k5_eras_draft_slot(i)->team) : NULL;
    char b[48];
    if (!t) return W_EMPTY;
    if (i == nfl2k5_eras_draft_next_slot()) { snprintf(b, sizeof b, "%s  (on the clock)", t->name); return text(b); }
    return text(t->name);
}

/* The COLLEGE STATISTICS row: the class has no college statistics (never invented), so the row
 * shows the facts it does have: the round the real draft took him in, his overall, his source. */
static uint32_t col_stat(int c)
{
    uint32_t rec = MEM32(SEL_PROSPECT);
    int p = prospect_of(rec);
    const ErasProspect *e = p >= 0 ? nfl2k5_eras_draft_prospect(p) : NULL;
    char b[32];
    if (!e) return W_EMPTY;
    switch (c) {
    case 0: snprintf(b, sizeof b, "Rd %d", e->round); return text(b);
    case 1: snprintf(b, sizeof b, "%d", e->ovr); return text(b);
    case 2: return text(!strcmp(e->source, "estimated") ? "est." : "roster");
    default: return text("N/A");
    }
}

static uint32_t dispatch(int id, uint32_t ecx, uint32_t edx)
{
    if (id < SC_N * NEV) { event(id / NEV, k_msgs[id % NEV], ecx); return 0; }
    switch (id) {
    case FN_B_ROWS: return board_rows(ecx);
    case FN_B_OBJ: return board_obj(ecx, edx);
    case FN_O_ROWS: return order_rows(ecx);
    case FN_O_OBJ: return order_obj(ecx, edx);
    case FN_RANK: return col_rank(ecx);
    case FN_STOCK: return col_stock(ecx);
    case FN_OVR: return col_ovr(ecx);
    case FN_ROUND: return col_round(ecx);
    case FN_PICK: return col_pick(ecx);
    case FN_TEAM: return col_team(ecx);
    case FN_STAT0: case FN_STAT1: case FN_STAT2: case FN_STAT3: return col_stat(id - FN_STAT0);
    default: return 0;
    }
}

static void dr_fn(void)
{
    uint32_t id = (s_called - DR_BASE) / 4u, ecx = g_ecx, edx = g_edx, r;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    r = dispatch((int)id, ecx, edx);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    g_eax = r;
    g_esp += 4;
}

recomp_func_t nfl2k5_eras_draft_room_lookup(uint32_t address)
{
    uint32_t off = address - DR_BASE;
    if (!s.built || address < DR_BASE || (off & 3u) || off / 4u >= (uint32_t)FN_COUNT) return NULL;
    s_called = address;
    return dr_fn;
}

/* ---- building ---------------------------------------------------------------------------- */

static uint32_t events(int k)
{
    uint32_t t = alloc(8u * (NEV + 1u), 4), i;
    if (!t) return 0;
    for (i = 0; i < NEV; i++) {
        uint32_t list = alloc(0x24u * 2u, 4);
        if (!list) return 0;
        MEM32(list + 0x00) = 1;                             /* kind 1: call(ecx = fsm) */
        MEM32(list + 0x04) = fn_addr(FN_EV0 + k * NEV + (int)i);
        MEM32(t + i * 8u) = (uint32_t)k_msgs[i];
        MEM32(t + i * 8u + 4u) = list;
    }
    return t;
}

static uint32_t column(uint32_t tmpl, int fn, const char *header, float width)
{
    uint32_t c = alloc(0xB0, 16), h = header ? wstr(header) : 0;
    if (!c || (header && !h)) return 0;
    gcopy(c, tmpl, 0xB0);
    MEM32(c + 0x08) = fn_addr(fn);
    if (width > 0.0f) { uint32_t u; memcpy(&u, &width, 4); MEM32(c + 0x50) = u; }
    if (header) { MEM32(c + 0x6C) = COL_HDR_LITERAL; MEM32(c + 0x70) = h; MEM32(c + 0x88) = h; }
    return c;
}

static uint32_t page(uint32_t tmpl, const char *name, int value, int rows_fn, int obj_fn, const uint32_t *cols, int ncols)
{
    uint32_t p = alloc(0x9Cu + 4u * ((uint32_t)ncols + 1u), 16), w = wstr(name);
    int i;
    if (!p || !w) return 0;
    gcopy(p, tmpl, 0x9C);
    MEM32(p + 0x08) = w;
    MEM32(p + 0x20) = (uint32_t)value;
    if (rows_fn >= 0) MEM32(p + 0x30) = fn_addr(rows_fn);
    if (obj_fn >= 0) MEM32(p + 0x48) = fn_addr(obj_fn);
    MEM32(p + 0x94) = 0;
    MEM32(p + 0x98) = 1;
    for (i = 0; i < ncols; i++) MEM32(p + 0x9Cu + 4u * (uint32_t)i) = cols[i];
    MEM32(p + 0x9Cu + 4u * (uint32_t)ncols) = 0;
    return p;
}

static uint32_t sheet(uint32_t tmpl, const uint32_t *pages, int npages)
{
    uint32_t p = alloc(0xF4u + 4u * ((uint32_t)npages + 1u), 16);
    int i;
    if (!p) return 0;
    gcopy(p, tmpl, 0xF4);
    MEM32(p + 0xC4) = 0; MEM32(p + 0xC8) = 0;              /* no A activation: A is the screen's msg 12 */
    MEM32(p + 0xDC) = 0; MEM32(p + 0xE0) = 0;
    for (i = 0; i < npages; i++) MEM32(p + 0xF4u + 4u * (uint32_t)i) = pages[i];
    MEM32(p + 0xF4u + 4u * (uint32_t)npages) = 0;
    return p;
}

/* The table wrapper: the retail one's rects, with the sheets replaced (entries of 0x14 bytes, 0-ended). */
static uint32_t wrapper(uint32_t tmpl, uint32_t sheet0, uint32_t sheet1)
{
    uint32_t p = alloc(0x14u * 3u, 16);
    if (!p) return 0;
    gcopy(p, tmpl, sheet1 ? 0x28u : 0x14u);
    MEM32(p + 0x00) = sheet0;
    if (sheet1) MEM32(p + 0x14) = sheet1;
    MEM32(p + (sheet1 ? 0x28u : 0x14u)) = 0;
    return p;
}

static uint32_t descriptor(uint32_t tmpl, int k, uint32_t wrap, const char *title)
{
    uint32_t d = alloc(0x34, 16), t = wstr(title);
    if (!d || !t) return 0;
    gcopy(d, tmpl, 0x34);
    MEM32(d + 0x00) = t;
    MEM32(d + 0x04) = events(k);
    MEM32(d + 0x14) = wrap;
    MEM32(d + 0x28) = 0x55;                                  /* Back pops, START -> msg 11, backdrop 5 (the retail Big Board's) */
    MEM32(d + 0x2C) = 0;
    MEM32(d + 0x30) = 0;
    return MEM32(d + 0x04) ? d : 0;
}

int nfl2k5_eras_draft_room_install(void)
{
    uint32_t bcols[5], ocols[5], scols[4], stats_pages[1];
    int ok = 1, i;
    const char *e;
    char b[48];
    if (s.built) return 0;
    e = getenv("NFL2K5_ERAS_LOG"); s.log = e && *e && *e != '0';
    e = getenv("NFL2K5_ERAS_DRAFT_CONFIRM"); s.arm_mode = e && !strcmp(e, "arm");
    if (nfl2k5_eras_package_load()) return -1;
    s.size = 192u * 1024u;
    s.mem = xbox_HeapAlloc(s.size, 16);
    if (!s.mem) { fprintf(stderr, "[ERAS] draft room: no guest memory\n"); return -1; }
    /* records, names, colleges */
    s.rec = alloc(REC_SIZE * NFL2K5_DRAFT_MAX_PROSPECTS, 16);
    s.slotrec = alloc(REC_SIZE * NFL2K5_DRAFT_MAX_SLOTS, 16);
    s.names = alloc(4u * NAME_CH * (NFL2K5_DRAFT_MAX_PROSPECTS + 1u), 16);
    s.colleges = alloc((8u + 2u * COLLEGE_CH) * NFL2K5_DRAFT_MAX_PROSPECTS, 16);
    s.blank_college = alloc(8u + 2u * COLLEGE_CH, 16);
    if (!s.rec || !s.slotrec || !s.names || !s.colleges || !s.blank_college) ok = 0;
    if (ok) {
        MEM32(s.blank_college) = s.blank_college + 8;        /* L"" */
        wput(s.names + (uint32_t)NFL2K5_DRAFT_MAX_PROSPECTS * 4u * NAME_CH + 2u * NAME_CH, "On the clock", NAME_CH);
    }
    for (i = 0; i < NTXT; i++) if (!(s.txt[i] = alloc(2u * TXT_CH, 4))) ok = 0;
    /* columns */
    s.col_rank = column(COL_RANK, FN_RANK, NULL, 0.0f);
    s.col_stock = column(COL_STOCK, FN_STOCK, NULL, 0.0f);
    s.col_ovr = column(COL_SCHOOL, FN_OVR, "OVR", 44.0f);
    s.col_round = column(COL_ROUND, FN_ROUND, NULL, 0.0f);
    s.col_pick = column(COL_PICK, FN_PICK, NULL, 0.0f);
    s.col_team = column(COL_TEAM, FN_TEAM, NULL, 0.0f);
    {
        static const char *const sh[4] = { "DRAFTED", "OVR", "RATINGS", "COLLEGE" };
        for (i = 0; i < 4; i++) if (!(s.col_stat[i] = column(COL_SCHOOL, FN_STAT0 + i, sh[i], 60.0f))) ok = 0;
        MEM32(s.col_stat[0] + 0x04) = 3; MEM32(s.col_stat[1] + 0x04) = 3; MEM32(s.col_stat[2] + 0x04) = 3; MEM32(s.col_stat[3] + 0x04) = 3;
    }
    if (!s.col_rank || !s.col_stock || !s.col_ovr || !s.col_round || !s.col_pick || !s.col_team) ok = 0;
    MEM32(s.col_ovr + 0x04) = 3;                              /* right aligned, as the retail numeric columns */
    bcols[0] = s.col_rank; bcols[1] = COL_NAME; bcols[2] = COL_POS; bcols[3] = s.col_stock; bcols[4] = COL_SCHOOL;
    ocols[0] = s.col_round; ocols[1] = s.col_pick; ocols[2] = COL_NAME; ocols[3] = COL_POS; ocols[4] = s.col_team;
    for (i = 0; i < 4; i++) scols[i] = s.col_stat[i];
    /* pages and sheets */
    if (ok) {
        uint32_t bcols6[6];
        memcpy(bcols6, bcols, sizeof bcols); bcols6[5] = s.col_ovr;
        for (i = 0; i < NG; i++) {
            s.page_b[i] = page(PAGE_ROOKIE_ALL, k_page_name[i], i, FN_B_ROWS, FN_B_OBJ, bcols6, 6);
            s.page_c[i] = page(PAGE_ROOKIE_ALL, k_page_name[i], i, FN_B_ROWS, FN_B_OBJ, bcols6, 6);
            if (!s.page_b[i] || !s.page_c[i]) ok = 0;
        }
        for (i = 0; i <= MAXR; i++) {
            if (i < MAXR) snprintf(b, sizeof b, "Round %d", i + 1); else snprintf(b, sizeof b, "All Rounds");
            s.page_o[i] = page(PAGE_SUMMARY_1, b, i, FN_O_ROWS, FN_O_OBJ, ocols, 5);
            if (!s.page_o[i]) ok = 0;
        }
        s.page_stats = page(PAGE_STATS, "", 0, -1, -1, scols, 4);
        stats_pages[0] = s.page_stats;
        s.sheet_stats = s.page_stats ? sheet(SHEET_STATS, stats_pages, 1) : 0;
        s.sheet[SC_BB] = sheet(SHEET_ROOKIE, s.page_b, NG);
        s.sheet[SC_CAND] = sheet(SHEET_ROOKIE, s.page_c, NG);
        s.sheet[SC_ORD] = 0;
        if (!s.sheet_stats || !s.sheet[SC_BB] || !s.sheet[SC_CAND]) ok = 0;
    }
    /* the wrappers and descriptors */
    if (ok) {
        s.wrap[SC_BB] = wrapper(TABLE_BIGBOARD, s.sheet[SC_BB], s.sheet_stats);
        s.wrap[SC_CAND] = wrapper(TABLE_CANDS, s.sheet[SC_CAND], s.sheet_stats);
        if (!s.wrap[SC_BB] || !s.wrap[SC_CAND]) ok = 0;
        s.desc[SC_BB] = ok ? descriptor(DESC_BIGBOARD, SC_BB, s.wrap[SC_BB], "The Big Board") : 0;
        s.desc[SC_CAND] = ok ? descriptor(DESC_CANDS, SC_CAND, s.wrap[SC_CAND], "NFL Draft") : 0;
        if (!s.desc[SC_BB] || !s.desc[SC_CAND]) ok = 0;
    }
    s.w_layout = wstr("rookie_pane");
    s.w_modal = alloc(2u * 160u, 4);
    s.w_help[0] = wstr("to advance the draft");
    s.w_help[1] = wstr("to skip to your pick");
    s.w_help[2] = wstr("for the Draft Order");
    s.w_help[3] = wstr("to let the CPU draft");
    s.w_help[4] = wstr("for The Big Board");
    if (!s.w_layout || !s.w_modal || !s.w_help[4]) ok = 0;
    if (!ok) { fprintf(stderr, "[ERAS] draft room: build failed (%u bytes used)\n", (unsigned)s.used); s.mem = 0; return -1; }
    s.built = 1;
    s.armed = -1;
    fprintf(stderr, "[ERAS] draft room: descriptors %08X (The Big Board) %08X (candidates) on the retail table handler, %u bytes\n",
            (unsigned)s.desc[SC_BB], (unsigned)s.desc[SC_CAND], (unsigned)s.used);
    return 0;
}

/* The order board needs the round count of the open draft: built at the first push. */
static int build_order_screen(void)
{
    int rounds = nfl2k5_eras_draft_round_count(), n;
    if (s.desc[SC_ORD]) return 0;
    if (rounds < 1) rounds = 1;
    if (rounds > MAXR) rounds = MAXR;
    n = rounds + 1;
    {
        uint32_t pages[MAXR + 1];
        int i;
        for (i = 0; i < rounds; i++) pages[i] = s.page_o[i];
        pages[rounds] = s.page_o[MAXR];
        s.sheet[SC_ORD] = sheet(SHEET_SUMMARY, pages, n);
    }
    s.wrap[SC_ORD] = s.sheet[SC_ORD] ? wrapper(TABLE_SUMMARY, s.sheet[SC_ORD], 0) : 0;
    s.desc[SC_ORD] = s.wrap[SC_ORD] ? descriptor(DESC_SUMMARY, SC_ORD, s.wrap[SC_ORD], "Draft Order") : 0;
    if (!s.desc[SC_ORD]) { fprintf(stderr, "[ERAS] draft room: the order board failed to build\n"); return -1; }
    /* the All Rounds page value is the round count, not MAXR */
    MEM32(s.page_o[MAXR] + 0x20) = (uint32_t)rounds;
    return 0;
}

int nfl2k5_eras_draft_room_ready(void) { return s.built; }

static int layout_resident(void)
{
    PUSH32(g_esp, s.w_layout);
    return gcall(sub_000449E0, 0, FOURCC_LAYT) != 0;
}

int nfl2k5_eras_draft_room_push(uint32_t fsm, char *msg, unsigned n)
{
    const char *e = getenv("NFL2K5_ERAS_DRAFT_SCREENS");
    int next;
    if (!s.built || (e && !strcmp(e, "sheet"))) return nfl2k5_eras_draft_sheets_push(fsm, msg, n);
    if (!layout_resident()) {
        nfl2k5_eras_log("draft screens: the rookie_pane layout is not resident (erasoffice.iff without it?): the M13 sheet screens open instead");
        return nfl2k5_eras_draft_sheets_push(fsm, msg, n);
    }
    if (nfl2k5_eras_draft_open(msg, n)) return -1;
    if (build_order_screen()) { if (msg) snprintf(msg, n, "Draft order screen not built"); return -1; }
    build_records();
    next = nfl2k5_eras_draft_next_slot();
    s.page[SC_BB] = 0; s.cursor[SC_BB] = 0;
    s.page[SC_CAND] = 0; s.cursor[SC_CAND] = 0;
    s.page_set[SC_ORD] = 0;
    s.pending = PEND_NONE;
    s.pushed_show = 0;
    s.auto_all = 0;
    s.clock_on = 0;
    s.goto_cands = 0;
    s.armed = -1;
    complete_check();
    nfl2k5_eras_log("draft screens opened (retail copies): %s; next %s", next < 0 ? "the order (complete)" : "The Big Board",
                    next >= 0 ? (nfl2k5_eras_draft_user_on_clock() ? "the user's pick" : "a CPU pick") : "none (complete)");
    gcall(sub_0006E390, fsm, next < 0 ? s.desc[SC_ORD] : s.desc[SC_BB]);
    return 0;
}

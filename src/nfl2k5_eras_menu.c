/* "My NFL Eras" inside the game's own front end (docs/eras/PHASE2-PLAN.md M3).
 *
 * The screens are menu data in the game's own format, as the Video Settings
 * screens are (src/nfl2k5_video_menu.c; docs/eras/research/NATIVE-MENU.md):
 * copies of a retail header (title, event table +0x04, handler +0x08, help
 * +0x0C, rows +0x10, layout +0x18), with rows and event tables of our own in a
 * guest heap. Every function the game calls through them is an address in
 * 0xFEE00000.. that recomp_lookup_manual hands to nfl2k5_eras_menu_lookup:
 *
 *   0xFEE00000 + screen * 0x1000 + slot * 4                       event actions (msg n -> slot n)
 *   0xFEE00000 + screen * 0x1000 + 0x800 + row * 0x40 + k * 4     row functions
 *        k 0..6: value row max, min, get, next, previous, value string, widest
 *        k 7:    type-9 action (+0x28, ecx = FSM, edx = &item)
 *        k 8:    row update (+0x2C, ecx = FSM, edx = &item: item +8 hide, +0xC grey)
 *
 * Every header gets its own event table (a template's table runs the
 * template's code: Game Options' 0x501C28 marks the options dirty and saves
 * at msg 5), or none. All entry points are plain `ret` (g_esp += 4) and keep
 * EBX/ESI/EDI/EBP.
 *
 * Screens: the hub (New / Continue League), the league screen (next fixture,
 * Your Team, Schedule, AFC and NFC Standings), Your Team, Schedule and the two
 * standings screens. "My NFL Eras" is the last row of Game Modes (M16, the
 * user's call: a game mode, not a Main Menu entry; B from the hub returns to
 * Game Modes); NFL2K5_ERAS_MENU=main puts it in the Main Menu after The Crib
 * instead (M3), =main-scroll also sets the main menu's scroll flag 0x04, =off
 * leaves the menus alone. "Play Next Game" launches the fixture (src/nfl2k5_eras_launch.c,
 * M4); the League and hub screens' msg 3 run the clean-up after it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_launch.h"
#include "nfl2k5_eras_office.h"
#include "nfl2k5_eras_depth.h"
#include "nfl2k5_eras_sheets.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define EM_FN_BASE      0xFEE00000u
#define EM_ROW          0x34u
#define EM_MAX_ROWS     32
#define EM_STR_CHARS    64
#define EM_MAX_VALUES   40

#define MAIN_MENU       0x00515660u   /* "Main Menu": 7 link rows at 0x5154C0, layout main_menu_sub */
#define GAME_MODES      0x005015CCu   /* Franchise ... Tournament */
#define FEATURES        0x00525830u   /* link-list template: handler 0xF3E90, layout navigation, flags 0x11 */
#define GAME_OPTIONS    0x00501E48u   /* settings-list template: handler 0xF3FC0, layout options, flags 0x15 */
#define SND_SELECT      0x00E7FD70u   /* L"Ambient: Menu Select" */
#define SND_CURSOR      0x00E7FD3Cu   /* L"Ambient: Menu Move Cursor" */

/* EM_OFFICE is the league screen: the 3D office (M3-office) when the office
 * package is there, else the plain list of M3. */
/* EM_DEPTH: the Depth Chart team picker in front of the game's own Depth Chart (M6);
 * EM_HOOK: never pushed, its row update hides retail rows in Eras games
 * (nfl2k5_eras_menu_game_rows). */
enum { EM_HUB, EM_OFFICE, EM_SCHEDULE, EM_AFC, EM_NFC, EM_YOURTEAM, EM_SETTINGS, EM_DEPTH, EM_HOOK, EM_SCREENS };
#define EM_LEAGUE EM_OFFICE
/* office rows */
enum { OR_PLAY, OR_SCHEDULE, OR_AFC, OR_NFC, OR_TEAM, OR_DEPTH, OR_LOOK, OR_ROSTERS, OR_ROWS };
/* depth chart picker rows */
enum { DR_TEAM, DR_EDIT, DR_QB, DR_RESET, DR_NOTE, DR_ROWS };
#define PAD_WORDS 0x00B37A78u   /* the game's button word per pad (stride 0x244); LT 0x4000, RT 0x8000 */
#define TOKEN_LAUNCH 100
enum { RK_LINK, RK_ACTION, RK_VALUE };   /* row kinds */

typedef struct {
    uint32_t header, rows, title;
    int nrows;
    int kind[EM_MAX_ROWS];
    uint32_t label[EM_MAX_ROWS];                         /* 64-char buffers, rewritten in place */
    uint32_t cur[EM_MAX_ROWS], table[EM_MAX_ROWS], pool[EM_MAX_ROWS];
} EmScreen;

static EmScreen s_em[EM_SCREENS];
static uint32_t s_heap, s_heap_used, s_heap_size;
static RECOMP_TLS uint32_t s_called;
static int s_log = -1;

/* Hub and league state for the labels. */
static int s_hub_error;          /* last Continue failure (NFL2K5_ERAS_*), 0 = none */
static char s_play_msg[EM_STR_CHARS];   /* why "Play Next Game" refused: shown on its row until the next refresh */
static int s_new_confirm;        /* New League pressed once over an existing league: the next press replaces it */
static int s_week = 1;           /* Schedule screen */
static int s_sched_games[24];    /* game indices of the shown week */
static int s_sched_n, s_sched_rows;
/* Standings rows: team index, or -1 - division for a header, or -100 for the note. */
static int s_stand_row[2][EM_MAX_ROWS];
static const char *s_stand_div[2][4];
static int s_office_room;        /* the league screen is the 3D office (package present) */
static int s_depth_team = -1;    /* the Depth Chart picker's team */
static char s_depth_msg[EM_STR_CHARS];   /* why Edit refused, until the next refresh */
static uint32_t s_depth_pad[4];

#define EM_LOG(...) do { if (s_log) fprintf(stderr, "[ERAS] menu " __VA_ARGS__); } while (0)

/* ---- guest heap and strings ------------------------------------------- */

static uint32_t em_alloc(uint32_t n)
{
    uint32_t p, i;
    n = (n + 3u) & ~3u;
    if (s_heap_used + n > s_heap_size) return 0;
    p = s_heap + s_heap_used;
    s_heap_used += n;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static void em_put(uint32_t p, const char *s)
{
    uint32_t i;
    if (!p) return;
    for (i = 0; i + 1 < EM_STR_CHARS && s[i]; i++)
        MEM16(p + i * 2u) = (uint16_t)(unsigned char)s[i];
    MEM16(p + i * 2u) = 0;
}

static uint32_t em_wstr(const char *s)
{
    uint32_t p = em_alloc(EM_STR_CHARS * 2u);
    em_put(p, s);
    return p;
}

static void em_copy(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4) MEM32(dst + i) = MEM32(src + i);
}

static uint32_t em_row_fn(int scr, int row, int k) { return EM_FN_BASE + (uint32_t)scr * 0x1000u + 0x800u + (uint32_t)row * 0x40u + (uint32_t)k * 4u; }
static uint32_t em_evt_fn(int scr, int msg) { return EM_FN_BASE + (uint32_t)scr * 0x1000u + (uint32_t)msg * 4u; }

/* A call into the game: registers in, a return address pushed, eax out. */
static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, EM_FN_BASE);
    fn();
    return g_eax;
}

static void em_sound(uint32_t name) { gcall(sub_0014D390, name, 0); }

/* ---- building screens -------------------------------------------------- */

/* Event table {msg, action list} ... {0}: one type-1 action per message,
 * calling 0xFEE0S000 + msg * 4 with ecx = FSM. */
static uint32_t em_events(int scr, const int *msgs, int n)
{
    uint32_t t = em_alloc(8u * (uint32_t)(n + 1)), i;
    if (!t) return 0;
    for (i = 0; i < (uint32_t)n; i++) {
        uint32_t list = em_alloc(0x24u * 2u);
        if (!list) return 0;
        MEM32(list + 0x00) = 1;
        MEM32(list + 0x04) = em_evt_fn(scr, msgs[i]);
        MEM32(t + i * 8u) = (uint32_t)msgs[i];
        MEM32(t + i * 8u + 4u) = list;
    }
    return t;
}

static int em_begin(int scr, uint32_t tmpl, const char *title, int nrows, const int *msgs, int nmsgs)
{
    EmScreen *s = &s_em[scr];
    if (nrows > EM_MAX_ROWS - 1) nrows = EM_MAX_ROWS - 1;
    s->nrows = nrows;
    s->title = em_wstr(title);
    s->rows = em_alloc(EM_ROW * (uint32_t)(nrows + 1));
    s->header = em_alloc(EM_ROW);
    if (!s->title || !s->rows || !s->header) return 0;
    em_copy(s->header, tmpl, EM_ROW);
    MEM32(s->header + 0x00) = s->title;
    MEM32(s->header + 0x04) = nmsgs ? em_events(scr, msgs, nmsgs) : 0;
    MEM32(s->header + 0x0C) = 0;                               /* no help bar */
    MEM32(s->header + 0x10) = s->rows;
    MEM32(s->rows + EM_ROW * (uint32_t)nrows) = 3;             /* end row */
    return nmsgs == 0 || MEM32(s->header + 0x04) != 0;
}

static int em_row(int scr, int row, int kind, const char *label, uint32_t target, int update, int values)
{
    EmScreen *s = &s_em[scr];
    uint32_t r = s->rows + EM_ROW * (uint32_t)row, k;
    s->kind[row] = kind;
    s->label[row] = em_wstr(label);
    if (!s->label[row]) return 0;
    MEM32(r + 0x04) = s->label[row];
    if (kind == RK_LINK) {
        MEM32(r + 0x00) = 0;
        MEM32(r + 0x08) = target;
    } else if (kind == RK_ACTION) {
        MEM32(r + 0x00) = 9;
        MEM32(r + 0x28) = em_row_fn(scr, row, 7);
    } else {
        MEM32(r + 0x00) = 7;
        for (k = 0; k < 7; k++) MEM32(r + 0x0C + k * 4u) = em_row_fn(scr, row, (int)k);
        if (values > EM_MAX_VALUES) values = EM_MAX_VALUES;
        s->cur[row] = em_alloc(EM_STR_CHARS * 2u);
        s->table[row] = em_alloc((uint32_t)values * 4u);
        s->pool[row] = em_alloc((uint32_t)values * EM_STR_CHARS * 2u);
        if (!s->cur[row] || !s->table[row] || !s->pool[row]) return 0;
    }
    if (update) MEM32(r + 0x2C) = em_row_fn(scr, row, 8);
    return 1;
}

/* ---- labels ------------------------------------------------------------ */

static void team_full(int t, char *b, size_t n)
{
    const ErasTeam *e = nfl2k5_eras_team(t);
    if (e) snprintf(b, n, "%s %s", e->city, e->name); else snprintf(b, n, "None (watch CPU vs CPU)");
}

/* The hub's third row: "Build My Assets" while the assets the builder makes from the
 * player's disc (generated\build.json, the office packages) are missing, the builder's
 * status while it runs (generated\build-status.txt), else the roster label. */
static int s_build_started;

static int assets_missing(void)
{
    return !nfl2k5_eras_limited()[0] &&
           (!nfl2k5_eras_exists("office\\files\\erasoffice.iff") || !nfl2k5_eras_exists("teams\\1980\\art"));
}

static const char *assets_label(void)
{
    static char b[EM_STR_CHARS];
    if (s_build_started) {
        char p[MAX_PATH], line[96] = "";
        FILE *f;
        g_eras_svc->root_path(0, "build-status.txt", p, sizeof p);
        if (!fopen_s(&f, p, "r") && f) { if (fgets(line, sizeof line, f)) line[strcspn(line, "\r\n")] = 0; fclose(f); }
        snprintf(b, sizeof b, "%s", line[0] ? line : "Building your assets ...");
        return b;
    }
    return assets_missing() ? "Build My Assets (office, era logos)" : "Rosters: unverified community roster";
}

static void hub_refresh(void)
{
    char b[EM_STR_CHARS];
    if (!nfl2k5_eras_league_exists()) snprintf(b, sizeof b, "Continue League");
    else if (s_hub_error) snprintf(b, sizeof b, "Continue League: %s", nfl2k5_eras_league_message(s_hub_error));
    else snprintf(b, sizeof b, "Continue League");
    em_put(s_em[EM_HUB].label[0], b);
    em_put(s_em[EM_HUB].label[2], assets_label());
    em_put(s_em[EM_HUB].label[1], nfl2k5_eras_limited()[0] ? nfl2k5_eras_limited()   /* the pack cannot run: why */
                                  : !nfl2k5_eras_package_ok() ? "New League (1980): season data missing"
                                  : s_new_confirm ? "Replace league? Press A again" : "New League (1980)");
}

static void league_refresh(void)
{
    char b[EM_STR_CHARS], t[48];
    int g;
    nfl2k5_eras_refresh();
    g = nfl2k5_eras_next_game();
    if (g < 0) snprintf(b, sizeof b, "Season complete");
    else {
        const ErasGame *e = nfl2k5_eras_game(g);
        if (s_play_msg[0]) snprintf(b, sizeof b, "%s", s_play_msg);
        /* The office list (nav_bar_c) clips at about 24 characters
         * (o1-office2.png): "Play: Lions at Rams", abbreviations when the
         * nicknames are long; the week is in the screen title. */
        else {
            const ErasTeam *a = nfl2k5_eras_team(e->away), *h = nfl2k5_eras_team(e->home);
            snprintf(b, sizeof b, "Play: %s at %s", a->name, h->name);
            if (strlen(b) > 24) snprintf(b, sizeof b, "Play: %s at %s", a->abbr, h->name);
            if (strlen(b) > 24) snprintf(b, sizeof b, "Play: %s at %s", a->abbr, h->abbr);
        }
    }
    em_put(s_em[EM_OFFICE].label[OR_PLAY], b);
    (void)t;
    snprintf(b, sizeof b, "Your Team: %s", nfl2k5_eras_user_team() >= 0 ? nfl2k5_eras_team(nfl2k5_eras_user_team())->name : "None (watch)");
    em_put(s_em[EM_OFFICE].label[OR_TEAM], b);
    snprintf(b, sizeof b, "Office Look: %s", nfl2k5_eras_office_look_name(nfl2k5_eras_office_look()));
    em_put(s_em[EM_OFFICE].label[OR_LOOK], b);
    if (g >= 0) snprintf(b, sizeof b, "1980 Season  -  Week %d", nfl2k5_eras_game(g)->week);
    else snprintf(b, sizeof b, "1980 Season");
    em_put(s_em[EM_OFFICE].title, b);
}

/* The Depth Chart picker: the user's team when the league has one, else any
 * team (A on the Team row or LT / RT steps through them). */
static int depth_locked(void) { return nfl2k5_eras_user_team() >= 0; }

static void depth_refresh(void)
{
    char b[EM_STR_CHARS], qb[48];
    const ErasTeam *t;
    int src;
    if (depth_locked()) s_depth_team = nfl2k5_eras_user_team();
    if (s_depth_team < 0 || s_depth_team >= nfl2k5_eras_team_count()) {
        int g = nfl2k5_eras_next_game();
        s_depth_team = g >= 0 ? nfl2k5_eras_game(g)->home : 0;
    }
    t = nfl2k5_eras_team(s_depth_team);
    if (!t) return;
    if (depth_locked()) snprintf(b, sizeof b, "Team: %s (your team)", t->name);
    else snprintf(b, sizeof b, "Team: %s %s  (LT / RT)", t->city, t->name);
    if (strlen(b) > 38) snprintf(b, sizeof b, "Team: %s  (LT / RT)", t->name);
    em_put(s_em[EM_DEPTH].label[DR_TEAM], b);
    em_put(s_em[EM_DEPTH].label[DR_EDIT], s_depth_msg[0] ? s_depth_msg : "Edit Depth Chart");
    nfl2k5_eras_depth_starter(s_depth_team, 0, qb, sizeof qb);
    snprintf(b, sizeof b, "Starting QB: %s", qb[0] ? qb : "(roster order)");
    em_put(s_em[EM_DEPTH].label[DR_QB], b);
    src = nfl2k5_eras_depth_source(s_depth_team);
    em_put(s_em[EM_DEPTH].label[DR_RESET], src == ERAS_DEPTH_LEAGUE ? "Reset to the community depth"
                                                                    : "Depth: community roster's");
    em_put(s_em[EM_DEPTH].label[DR_NOTE], src == ERAS_DEPTH_LEAGUE ? "Edited in this league only"
                                                                   : "Unverified community roster");
    snprintf(b, sizeof b, "Depth Chart  -  %s", t->name);
    em_put(s_em[EM_DEPTH].title, b);
}

static void depth_step(int dir)
{
    int n = nfl2k5_eras_team_count();
    if (depth_locked() || n <= 0) { em_sound(SND_CURSOR); return; }
    s_depth_team = (s_depth_team + dir + n) % n;
    s_depth_msg[0] = 0;
    depth_refresh();
    em_sound(SND_CURSOR);
    EM_LOG("depth chart picker: %s\n", nfl2k5_eras_team(s_depth_team)->id);
}

static void schedule_labels(void)
{
    int i, n = 0;
    char b[EM_STR_CHARS];
    for (i = 0; i < nfl2k5_eras_game_count() && n < s_sched_rows; i++)
        if (nfl2k5_eras_game(i)->week == s_week) s_sched_games[n++] = i;
    s_sched_n = n;
    for (i = 0; i < s_sched_rows; i++) {
        if (i < n) {
            const ErasGame *e = nfl2k5_eras_game(s_sched_games[i]);
            snprintf(b, sizeof b, "%s at %s", nfl2k5_eras_team(e->away)->name, nfl2k5_eras_team(e->home)->name);
        } else b[0] = 0;
        em_put(s_em[EM_SCHEDULE].label[1 + i], b);
    }
}

static void standings_labels(int conf)
{
    int scr = conf ? EM_NFC : EM_AFC, row, order[NFL2K5_ERAS_MAX_TEAMS], d = -1, k = 0, n = 0;
    char b[EM_STR_CHARS];
    for (row = 0; row < s_em[scr].nrows; row++) {
        int v = s_stand_row[conf][row];
        if (v <= -1 && v > -100) {           /* division header: re-sort that division */
            d = -1 - v;
            n = nfl2k5_eras_division_order(s_stand_div[conf][d], order);
            k = 0;
            em_put(s_em[scr].label[row], s_stand_div[conf][d]);
        } else if (v >= 0 && d >= 0 && k < n) {
            s_stand_row[conf][row] = order[k++];
            team_full(s_stand_row[conf][row], b, sizeof b);
            em_put(s_em[scr].label[row], b);
        }
    }
}

/* ---- row values ---------------------------------------------------------- */

/* A row that only shows a value (standings, schedule lines, notes) has two
 * identical values: the settings list draws no value at all for a row whose
 * max equals its min (seen in T3.2), and greyed labels do not show on its
 * light rows, so these rows stay selectable and Left/Right change nothing. */
/* The Office Look setting as an index (league.ini office_look). */
static int look_setting(void)
{
    const char *v = nfl2k5_eras_office_look_saved();
    int i;
    for (i = 1; i < OFFICE_LOOKS; i++) if (!_stricmp(v, nfl2k5_eras_office_look_name(i))) return i;
    return OFFICE_LOOK_AUTO;
}

static int value_count(int scr, int row)
{
    switch (scr) {
    case EM_YOURTEAM: return row == 0 ? nfl2k5_eras_team_count() + 1 : 2;
    case EM_SCHEDULE: return row == 0 ? (nfl2k5_eras_week_count() > 0 ? nfl2k5_eras_week_count() : 1) : 2;
    case EM_SETTINGS: return row == 0 ? OFFICE_LOOKS : 2;
    default: return 2;
    }
}

static int value_get(int scr, int row)
{
    if (scr == EM_YOURTEAM && row == 0) return nfl2k5_eras_user_team() + 1;
    if (scr == EM_SCHEDULE && row == 0) return s_week - 1;
    if (scr == EM_SETTINGS && row == 0) return look_setting();
    return 0;
}

/* Returns 1 when the value changed. */
static int value_set(int scr, int row, int v)
{
    if (scr == EM_YOURTEAM && row == 0) {
        if (nfl2k5_eras_games_played() > 0) return 0;      /* locked after the first game */
        return nfl2k5_eras_set_user_team(v - 1) == 0;
    }
    if (scr == EM_SCHEDULE && row == 0) {
        s_week = v + 1;
        schedule_labels();
        return 1;
    }
    if (scr == EM_SETTINGS && row == 0) {
        if (nfl2k5_eras_set_office_look_saved(nfl2k5_eras_office_look_name(v))) return 0;
        nfl2k5_eras_office_set_look(v);
        EM_LOG("office look setting: %s\n", nfl2k5_eras_office_look_name(v));
        return 1;
    }
    return 0;
}

static void value_name(int scr, int row, int v, char *b, size_t n)
{
    b[0] = 0;
    switch (scr) {
    case EM_YOURTEAM:
        if (row == 0) team_full(v - 1, b, n);
        else snprintf(b, n, "Unverified community roster");
        break;
    case EM_SCHEDULE:
        if (row == 0) {
            int i;
            const char *date = "";
            for (i = 0; i < nfl2k5_eras_game_count(); i++)
                if (nfl2k5_eras_game(i)->week == v + 1) { date = nfl2k5_eras_game(i)->date; break; }
            snprintf(b, n, "Week %d  (%s)", v + 1, date);
        } else if (row - 1 < s_sched_n) {
            int hp, ap;
            if (nfl2k5_eras_result(s_sched_games[row - 1], &hp, &ap) == NFL2K5_ERAS_PLAYED) snprintf(b, n, "%d - %d", ap, hp);
            else snprintf(b, n, "-");
        } else if (row == s_em[EM_SCHEDULE].nrows - 1) snprintf(b, n, "Unverified community rosters");
        break;
    case EM_SETTINGS:
        if (row == 0) {
            if (v == OFFICE_LOOK_AUTO) snprintf(b, n, "Auto (%s)", nfl2k5_eras_office_look_name(nfl2k5_eras_office_auto_look()));
            else snprintf(b, n, "%s", nfl2k5_eras_office_look_name(v));
        } else snprintf(b, n, "Browse with LT / RT in the office");
        break;
    case EM_AFC: case EM_NFC: {
        int t = s_stand_row[scr == EM_NFC][row];
        if (t >= 0) {
            ErasRecord r[NFL2K5_ERAS_MAX_TEAMS];
            nfl2k5_eras_standings(r);
            snprintf(b, n, "%d-%d-%d    %d-%d", r[t].w, r[t].l, r[t].t, r[t].pf, r[t].pa);
        } else if (t > -100) snprintf(b, n, "W-L-T    PF-PA");
        else snprintf(b, n, "Unofficial order");
        break;
    }
    }
}

static void value_fn(int scr, int row, int k)
{
    EmScreen *s = &s_em[scr];
    int n = value_count(scr, row), v = value_get(scr, row);
    char name[EM_STR_CHARS];
    if (n > EM_MAX_VALUES) n = EM_MAX_VALUES;
    if (n < 1) n = 1;
    if (v < 0 || v >= n) v = 0;
    switch (k) {
    case 0: g_eax = (uint32_t)(n - 1); break;
    case 1: g_eax = 0; break;
    case 2: g_eax = (uint32_t)v; break;
    case 3: g_eax = (uint32_t)value_set(scr, row, v + 1 >= n ? 0 : v + 1); break;
    case 4: g_eax = (uint32_t)value_set(scr, row, v <= 0 ? n - 1 : v - 1); break;
    case 5:
        value_name(scr, row, v, name, sizeof name);
        em_put(s->cur[row], name);
        g_eax = s->cur[row];
        break;
    case 6: {
        /* Widest value string in the font passed in ecx, as the game's own
         * rows do: sub_000771A0 over a table of all of them (it pops both). */
        int i;
        for (i = 0; i < n; i++) {
            uint32_t p = s->pool[row] + (uint32_t)i * EM_STR_CHARS * 2u;
            value_name(scr, row, i, name, sizeof name);
            em_put(p, name);
            MEM32(s->table[row] + (uint32_t)i * 4u) = p;
        }
        PUSH32(g_esp, s->table[row]);
        PUSH32(g_esp, (uint32_t)(n - 1));
        g_edx = 0;
        PUSH32(g_esp, EM_FN_BASE);
        sub_000771A0();
        EM_LOG("screen %d row %d: widest of %d values = %u\n", scr, row, n, (unsigned)g_eax);
        break;
    }
    }
}

/* ---- actions, row updates, events ------------------------------------- */

static void push(uint32_t fsm, int scr)
{
    EM_LOG("push %d (%08X)\n", scr, (unsigned)s_em[scr].header);
    gcall(sub_0006E390, fsm, s_em[scr].header);
    em_sound(SND_SELECT);
}

/* Into the office: the look from league.ini, and the room's package loaded
 * before the push, so its LAYT is there when the handler resolves +0x18. */
static void push_office(uint32_t fsm)
{
    if (s_office_room) {
        nfl2k5_eras_office_set_look(look_setting());
        if (nfl2k5_eras_office_load()) {
            /* no room (the package is not in the archive): the plain list of M3,
             * whose layout is always loaded (a missing LAYT draws no list) */
            fprintf(stderr, "[ERAS] menu: the office package did not load; the league screen is the plain list\n");
            MEM32(s_em[EM_OFFICE].header + 0x18) = MEM32(FEATURES + 0x18);
            MEM32(s_em[EM_OFFICE].header + 0x28) = MEM32(FEATURES + 0x28);
            s_office_room = 0;
        }
    }
    league_refresh();
    push(fsm, EM_OFFICE);
}

/* The camera arrived at a row's stop (or there is no room): open what the row opens. */
static void office_arrive(uint32_t fsm, int token)
{
    if (token == TOKEN_LAUNCH) {
        int g = nfl2k5_eras_next_game(), r;
        EM_LOG("play next game: %s\n", g >= 0 ? nfl2k5_eras_game(g)->id : "none");
        if (g < 0) { em_sound(SND_CURSOR); nfl2k5_eras_office_return(); return; }
        /* the room's packages go before the match loads (see nfl2k5_eras_office_free_for_match) */
        if (s_office_room) nfl2k5_eras_office_free_for_match();
        r = nfl2k5_eras_launch(fsm, s_play_msg, sizeof s_play_msg);
        if (r == 0) { s_play_msg[0] = 0; em_sound(SND_SELECT); }
        else {
            EM_LOG("play next game refused (%d): %s\n", r, s_play_msg);
            league_refresh();
            em_sound(SND_CURSOR);
            if (s_office_room) {                 /* still on the office: the room back, then the return move */
                nfl2k5_eras_office_back_from_match();
                nfl2k5_eras_office_shown(fsm);
            }
        }
    } else if (token > 0 && token - 1 < EM_SCREENS) {
        /* M7: Franchise's own Schedule screen (a copy), and Standings on the same
         * sheet engine; NFL2K5_ERAS_SCHEDULE / _STANDINGS = list keep the old lists. */
        if (token - 1 == EM_SCHEDULE && nfl2k5_eras_sheets_schedule_on()) {
            nfl2k5_eras_sheets_push_schedule(fsm);
            em_sound(SND_SELECT);
            return;
        }
        if ((token - 1 == EM_AFC || token - 1 == EM_NFC) && nfl2k5_eras_sheets_standings_on()) {
            nfl2k5_eras_sheets_push_standings(fsm, token - 1 == EM_NFC);
            em_sound(SND_SELECT);
            return;
        }
        if (token - 1 == EM_DEPTH) { s_depth_msg[0] = 0; depth_refresh(); }
        push(fsm, token - 1);
    }
}

static void office_row(int row, uint32_t fsm)
{
    /* Depth Chart: the Gameplan stop (notebook / contract), as Franchise's Gameplan -> Depth Chart */
    static const int kind[OR_ROWS] = { OFFICE_STOP_PLAY, OFFICE_STOP_SCHEDULE, OFFICE_STOP_STANDINGS, OFFICE_STOP_STANDINGS,
                                       OFFICE_STOP_TEAM, OFFICE_STOP_TEAM, OFFICE_STOP_SETTINGS, -1 };
    static const int target[OR_ROWS] = { 0, EM_SCHEDULE, EM_AFC, EM_NFC, EM_YOURTEAM, EM_DEPTH, EM_SETTINGS, -1 };
    int token;
    if (row < 0 || row >= OR_ROWS) return;
    if (nfl2k5_eras_office_busy()) return;
    if (row == OR_ROSTERS) {
        /* "Rosters: unverified community roster" is a label row */
        EM_LOG("label row %d/%d pressed (no action)\n", EM_OFFICE, row);
        em_sound(SND_CURSOR);
        return;
    }
    if (row == OR_PLAY && nfl2k5_eras_next_game() < 0) { em_sound(SND_CURSOR); return; }
    token = row == OR_PLAY ? TOKEN_LAUNCH : target[row] + 1;
    EM_LOG("office row %d pressed\n", row);
    if (!nfl2k5_eras_office_go(kind[row], token)) office_arrive(fsm, token);   /* no room: at once */
}

static void row_action(int scr, int row, uint32_t fsm)
{
    if (scr == EM_OFFICE) { office_row(row, fsm); return; }
    if (scr == EM_DEPTH) {
        if (row == DR_TEAM) depth_step(+1);
        else if (row == DR_EDIT) {
            int r = nfl2k5_eras_depth_edit(fsm, s_depth_team, s_depth_msg, sizeof s_depth_msg);
            EM_LOG("depth chart edit %s: %d %s\n", nfl2k5_eras_team(s_depth_team)->id, r, s_depth_msg);
            if (r) { depth_refresh(); em_sound(SND_CURSOR); }
        } else if (row == DR_RESET) {
            if (!nfl2k5_eras_depth_reset(s_depth_team)) em_sound(SND_CURSOR);
            else em_sound(SND_SELECT);
            depth_refresh();
        } else {
            EM_LOG("label row %d/%d pressed (no action)\n", scr, row);
            em_sound(SND_CURSOR);
        }
        return;
    }
    if (scr == EM_HUB && row <= 1 && nfl2k5_eras_limited()[0]) {
        /* M16: the pack cannot run (or its files are not in the archive: a full drive);
         * the New League row says why. Opening the office would end in the game's
         * "disc is dirty or damaged" error. */
        EM_LOG("hub row %d refused: %s\n", row, nfl2k5_eras_limited());
        hub_refresh();
        em_sound(SND_CURSOR);
        return;
    }
    if (scr == EM_HUB && row == 0) {                       /* Continue League */
        int r = nfl2k5_eras_league_load();
        EM_LOG("continue: %d (%s)\n", r, nfl2k5_eras_league_message(r));
        s_hub_error = r;
        s_new_confirm = 0;
        if (r == NFL2K5_ERAS_OK) push_office(fsm);
        else { hub_refresh(); em_sound(SND_CURSOR); }
    } else if (scr == EM_HUB && row == 1) {                /* New League (1980) */
        int r;
        if (nfl2k5_eras_league_exists() && !s_new_confirm) {
            /* A league is there: the first press only asks (the row relabels),
             * the second moves it aside (never deleted) and starts a new one. */
            s_new_confirm = 1;
            EM_LOG("new league: a league exists, asking for a second press\n");
            hub_refresh();
            em_sound(SND_CURSOR);
            return;
        }
        s_new_confirm = 0;
        r = nfl2k5_eras_league_create();
        EM_LOG("new league: %d (%s)\n", r, nfl2k5_eras_league_message(r));
        s_hub_error = 0;
        if (r == NFL2K5_ERAS_OK) push_office(fsm);
        else em_sound(SND_CURSOR);
    } else if (scr == EM_HUB && row == 2 && !s_build_started && assets_missing()) {
        /* Build My Assets: the office, era logos and card badges from the player's disc
         * (the asset builder, hidden; the archive files take effect at the next boot) */
        int r = g_eras_svc->run_builder("--steps office,office_1980s,logos_1980,art_1980");
        EM_LOG("build my assets: %d\n", r);
        if (r == 0) {
            s_build_started = 1;
            g_eras_svc->toast("My NFL Eras", "Building your assets from your NFL 2K5 disc",
                              "Restart the game when the hub says Done", 6000);
        }
        hub_refresh();
        em_sound(r == 0 ? SND_SELECT : SND_CURSOR);
    } else if (scr == EM_HUB && row == 2) {
        /* "Rosters: unverified community roster" is a label row: no action, but
         * give the same refusal sound as the other rows that do nothing yet. */
        EM_LOG("label row %d/%d pressed (no action)\n", scr, row);
        em_sound(SND_CURSOR);
    }
}

static void row_update(int scr, int row, uint32_t item)
{
    int hide = 0, grey = 0;
    if (scr == EM_HUB && row == 0) hide = !nfl2k5_eras_league_exists() || nfl2k5_eras_limited()[0];
    else if (scr == EM_HUB && row == 1) grey = !nfl2k5_eras_package_ok();
    else if (scr == EM_OFFICE && row == OR_PLAY) grey = nfl2k5_eras_next_game() < 0;
    else if (scr == EM_HOOK) hide = 1;                     /* a retail row hidden in an Eras game */
    MEM32(item + 0x08) = (uint32_t)hide;
    MEM32(item + 0x0C) = (uint32_t)grey;
}

static void event(int scr, int msg, uint32_t fsm)
{
    (void)fsm;
    switch (scr) {
    case EM_HUB:
        if (msg == 3) { nfl2k5_eras_after_game("hub shown"); s_new_confirm = 0; hub_refresh(); }
        break;
    case EM_OFFICE:
        switch (msg) {
        case 1: if (s_office_room) nfl2k5_eras_office_create(fsm); break;
        case 2: if (s_office_room) nfl2k5_eras_office_destroy(); break;
        case 3:
            /* after a launched game (or a crash during one): commit, release, restore first */
            nfl2k5_eras_after_game("office shown");
            if (s_office_room) nfl2k5_eras_office_refresh(fsm);
            s_play_msg[0] = 0;
            league_refresh();
            break;
        case 4: if (s_office_room) nfl2k5_eras_office_shown(fsm); break;
        case 5: if (s_office_room) nfl2k5_eras_office_covered(); break;
        case 6:
            if (s_office_room) {
                int tok = nfl2k5_eras_office_frame(fsm);
                if (nfl2k5_eras_office_look_changed()) league_refresh();
                if (tok) office_arrive(fsm, tok);
            }
            break;
        case 7: if (s_office_room) nfl2k5_eras_office_draw(fsm); break;
        case 10: if (s_office_room) nfl2k5_eras_office_back(); break;
        }
        break;
    case EM_SCHEDULE:
        if (msg == 1) {                                  /* opens on the next fixture's week */
            int g = nfl2k5_eras_next_game();
            s_week = g >= 0 ? nfl2k5_eras_game(g)->week : 1;
        }
        if (msg == 1 || msg == 3) { nfl2k5_eras_refresh(); schedule_labels(); }
        break;
    case EM_AFC: case EM_NFC:
        if (msg == 3) { nfl2k5_eras_refresh(); standings_labels(scr == EM_NFC); }
        break;
    case EM_DEPTH:
        if (msg == 3) {
            /* back from the game's Depth Chart: save the edit, release the slot */
            nfl2k5_eras_depth_edit_end("depth chart picker shown");
            depth_refresh();
            memset(s_depth_pad, 0xFF, sizeof s_depth_pad);   /* no LT/RT edge from a held button */
        } else if (msg == 6 && !nfl2k5_eras_depth_editing()) {
            int i;
            for (i = 0; i < 4; i++) {
                uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u), edge = w & ~s_depth_pad[i];
                s_depth_pad[i] = w;
                if (edge & 0x4000u) { depth_step(-1); break; }
                if (edge & 0x8000u) { depth_step(+1); break; }
            }
        }
        break;
    }
    if (msg >= 1 && msg <= 5) EM_LOG("screen %d msg %d\n", scr, msg);
}

/* The hub (0) or League (1) descriptor, for the launch's return and clean-up. */
uint32_t nfl2k5_eras_menu_header(int which)
{
    return which == 0 ? s_em[EM_HUB].header : which == 1 ? s_em[EM_LEAGUE].header : 0;
}

/* M6: in an Eras game, rows that would break the league are hidden through
 * their row update (+0x2C), swapped for EM_HOOK's (hide) while a launch is
 * active and put back after it: End Of Game Rematch and Save VIP and Exit,
 * the pause menu's Quit -> Rematch, and Coach Match Up Options (Team Options;
 * the settings snapshot reverts them after the game anyway) and VIP. Each row
 * is swapped only when it still holds the retail update function, so retail
 * games (no launch active) see the retail rows. */
void nfl2k5_eras_menu_game_rows(int eras)
{
    static const struct { uint32_t desc; int row; uint32_t orig; const char *what; } k[] = {
        { 0x004E935Cu, 0, 0x0006EB30u, "End Of Game: Rematch" },
        { 0x004E935Cu, 5, 0x0006EB80u, "End Of Game: Save VIP and Exit" },
        { 0x004E8D70u, 3, 0x0006EB30u, "Pause, Quit: Rematch" },
        { 0x00585474u, 4, 0x00367240u, "Coach Match Up: Options" },
        { 0x00585474u, 5, 0x00367260u, "Coach Match Up: VIP" },
    };
    static int on;
    uint32_t mine;
    size_t i;
    int n = 0;
    char done[240] = "";
    if (!s_em[EM_HOOK].header || eras == on) return;
    mine = em_row_fn(EM_HOOK, 0, 8);
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint32_t rows = MEM32(k[i].desc + 0x10), r;
        if (!rows) continue;
        r = rows + EM_ROW * (uint32_t)k[i].row + 0x2Cu;
        if (eras && MEM32(r) == k[i].orig) { MEM32(r) = mine; n++; }
        else if (!eras && MEM32(r) == mine) { MEM32(r) = k[i].orig; n++; }
        else continue;
        if (strlen(done) + strlen(k[i].what) + 3 < sizeof done) { strcat(done, " "); strcat(done, k[i].what); strcat(done, ";"); }
    }
    on = eras;
    nfl2k5_eras_log("game menus: %d rows %s:%s", n, eras ? "hidden for the Eras game" : "back to retail", done);
}

/* One function for every address: the lookup remembers which one is called. */
static void em_fn(void)
{
    uint32_t off = s_called - EM_FN_BASE, in = off % 0x1000u, fsm = g_ecx, item = g_edx;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    int scr = (int)(off / 0x1000u);
    if (in < 0x800u) {
        event(scr, (int)(in / 4u), fsm);
    } else {
        int row = (int)((in - 0x800u) / 0x40u), k = (int)((in % 0x40u) / 4u);
        if (k <= 6) value_fn(scr, row, k);
        else if (k == 7) row_action(scr, row, fsm);
        else row_update(scr, row, item);
    }
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    g_esp += 4;
}

recomp_func_t nfl2k5_eras_menu_lookup(uint32_t address)
{
    uint32_t off = address - EM_FN_BASE, scr = off / 0x1000u, in = off % 0x1000u;
    if (address >= NFL2K5_ERAS_SHEETS_BASE) return nfl2k5_eras_sheets_lookup(address);   /* M7 */
    if (scr >= EM_SCREENS || !s_em[scr].header) return NULL;
    if (in < 0x800u) {
        if (in / 4u > 31u) return NULL;
    } else if ((in - 0x800u) / 0x40u >= (uint32_t)s_em[scr].nrows || (in % 0x40u) / 4u > 8u) {
        return NULL;
    }
    s_called = address;
    return em_fn;
}

/* ---- main menu link ------------------------------------------------------ */

/* Copy a link menu's rows with one more link row at index `at` (or at the
 * end, at < 0) and repoint the header. The item list is rebuilt from +0x10
 * at every activation, so this once at boot changes the menu everywhere. */
static int em_insert_link(uint32_t menu, int at, uint32_t target, uint32_t label)
{
    uint32_t old = MEM32(menu + 0x10), copy, row;
    int n = 0;
    if (!old) return 0;
    while (n < 32 && MEM32(old + EM_ROW * (uint32_t)n) != 3u) n++;
    if (n >= 31) return 0;                          /* the runtime holds 32 items */
    if (at < 0 || at > n) at = n;
    copy = em_alloc(EM_ROW * (uint32_t)(n + 2));
    if (!copy) return 0;
    em_copy(copy, old, EM_ROW * (uint32_t)at);
    row = copy + EM_ROW * (uint32_t)at;              /* zeroed by em_alloc */
    MEM32(row + 0x00) = 0;
    MEM32(row + 0x04) = label;
    MEM32(row + 0x08) = target;
    em_copy(row + EM_ROW, old + EM_ROW * (uint32_t)at, EM_ROW * (uint32_t)(n - at + 1));   /* the rest + end row */
    MEM32(menu + 0x10) = copy;
    return n + 1;
}

/* retail: the clean game (no mod pack); the served functions cannot be reached
 * from interpreted pack code (NATIVE-MENU 2.2), so packs get no Eras menu
 * (NFL2K5_ERAS_MENU is ignored there). */
void nfl2k5_eras_menu_install(int retail)
{
    static const int hub_msgs[] = { 3 }, office_msgs[] = { 1, 2, 3, 4, 5, 6, 7, 10 }, sched_msgs[] = { 1, 3 }, stand_msgs[] = { 3 };
    const char *place = getenv("NFL2K5_ERAS_MENU");
    int ok = 1, i, c, rows;
    if (s_log < 0) { const char *e = getenv("NFL2K5_ERAS_LOG"); s_log = e && *e && *e != '0'; }
    /* Under a pack the menus at MAIN_MENU / GAME_MODES and the templates are
     * the pack's, laid out by its XBE: no Eras menu there, whatever the
     * variable says. */
    if (!retail) { fprintf(stderr, "[ERAS] menu: off (mod pack%s)\n", place && *place ? ", NFL2K5_ERAS_MENU ignored" : ""); return; }
    if (!place || !*place) place = "modes";   /* M16: Game Modes (was the Main Menu) */
    if (!strcmp(place, "off")) { fprintf(stderr, "[ERAS] menu: off (NFL2K5_ERAS_MENU=off)\n"); return; }
    s_heap_size = 256u * 1024u;
    s_heap = xbox_HeapAlloc(s_heap_size, 16);
    if (!s_heap) { fprintf(stderr, "[ERAS] menu: no guest memory\n"); return; }
    nfl2k5_eras_package_load();

    /* Hub: link list. Continue is hidden without a save. */
    ok &= em_begin(EM_HUB, FEATURES, "My NFL Eras", 3, hub_msgs, 1);
    ok &= em_row(EM_HUB, 0, RK_ACTION, "Continue League", 0, 1, 0);
    ok &= em_row(EM_HUB, 1, RK_ACTION, "New League (1980)", 0, 1, 0);
    ok &= em_row(EM_HUB, 2, RK_ACTION, "Rosters: unverified community roster", 0, 0, 0);

    /* Children of the league screen first (their headers are link targets). */
    ok &= em_begin(EM_YOURTEAM, GAME_OPTIONS, "Your Team", 2, NULL, 0);
    ok &= em_row(EM_YOURTEAM, 0, RK_VALUE, "Your Team", 0, 1, NFL2K5_ERAS_MAX_TEAMS + 1);
    ok &= em_row(EM_YOURTEAM, 1, RK_VALUE, "Rosters", 0, 0, 2);

    s_sched_rows = 0;
    for (i = 1; i <= nfl2k5_eras_week_count(); i++) {
        int g, n = 0;
        for (g = 0; g < nfl2k5_eras_game_count(); g++) n += nfl2k5_eras_game(g)->week == i;
        if (n > s_sched_rows) s_sched_rows = n;
    }
    if (s_sched_rows > (int)(sizeof s_sched_games / sizeof s_sched_games[0])) s_sched_rows = (int)(sizeof s_sched_games / sizeof s_sched_games[0]);
    ok &= em_begin(EM_SCHEDULE, GAME_OPTIONS, "Schedule", s_sched_rows + 2, sched_msgs, 2);
    ok &= em_row(EM_SCHEDULE, 0, RK_VALUE, "Week", 0, 1, NFL2K5_ERAS_MAX_WEEKS);
    for (i = 0; i < s_sched_rows; i++) ok &= em_row(EM_SCHEDULE, 1 + i, RK_VALUE, "", 0, 1, 2);
    ok &= em_row(EM_SCHEDULE, s_sched_rows + 1, RK_VALUE, "Rosters", 0, 0, 2);

    for (c = 0; c < 2; c++) {
        int scr = c ? EM_NFC : EM_AFC, nd = nfl2k5_eras_divisions(c ? "NFC" : "AFC", s_stand_div[c]), d, r = 0;
        rows = 1;
        for (d = 0; d < nd; d++) {
            int order[NFL2K5_ERAS_MAX_TEAMS];
            rows += 1 + nfl2k5_eras_division_order(s_stand_div[c][d], order);
        }
        if (rows > EM_MAX_ROWS - 1) rows = EM_MAX_ROWS - 1;
        ok &= em_begin(scr, GAME_OPTIONS, c ? "NFC Standings" : "AFC Standings", rows, stand_msgs, 1);
        /* The settings handler copies the header's flags: 0x15 includes the
         * scroll bit 0x04, so all rows are reachable. */
        for (d = 0; d < nd && r < rows - 1; d++) {
            int order[NFL2K5_ERAS_MAX_TEAMS], n = nfl2k5_eras_division_order(s_stand_div[c][d], order), k;
            s_stand_row[c][r] = -1 - d;
            ok &= em_row(scr, r++, RK_VALUE, s_stand_div[c][d], 0, 1, 2);
            for (k = 0; k < n && r < rows - 1; k++) {
                s_stand_row[c][r] = order[k];
                ok &= em_row(scr, r++, RK_VALUE, "", 0, 1, 2);
            }
        }
        s_stand_row[c][r] = -100;
        ok &= em_row(scr, r, RK_VALUE, "Rosters: unverified community", 0, 1, 2);
        standings_labels(c);
    }

    /* M6: the Depth Chart picker (a link list of action rows; LT / RT in its msg 6),
     * in front of the game's own Depth Chart screens. */
    {
        static const int depth_msgs[] = { 3, 6 };
        ok &= em_begin(EM_DEPTH, FEATURES, "Depth Chart", DR_ROWS, depth_msgs, 2);
        ok &= em_row(EM_DEPTH, DR_TEAM, RK_ACTION, "Team", 0, 0, 0);
        ok &= em_row(EM_DEPTH, DR_EDIT, RK_ACTION, "Edit Depth Chart", 0, 0, 0);
        ok &= em_row(EM_DEPTH, DR_QB, RK_ACTION, "Starting QB", 0, 0, 0);
        ok &= em_row(EM_DEPTH, DR_RESET, RK_ACTION, "Reset", 0, 0, 0);
        ok &= em_row(EM_DEPTH, DR_NOTE, RK_ACTION, "Unverified community roster", 0, 0, 0);
        ok &= em_begin(EM_HOOK, FEATURES, "", 1, NULL, 0);
        ok &= em_row(EM_HOOK, 0, RK_ACTION, "", 0, 1, 0);
    }

    /* Office settings: the Office Look (league.ini office_look). */
    ok &= em_begin(EM_SETTINGS, GAME_OPTIONS, "Office", 2, NULL, 0);
    ok &= em_row(EM_SETTINGS, 0, RK_VALUE, "Office Look", 0, 1, OFFICE_LOOKS);
    ok &= em_row(EM_SETTINGS, 1, RK_VALUE, "Eras", 0, 0, 2);

    /* The league screen: the office (M3-office). A link list like the Front
     * Office (0x52533C): flags 0x10 (Back pops, background mode 0: the room is
     * drawn by our own msg 7), layout front_office (the LAYT copied into
     * erasoffice.iff: title_bar_wide + nav_bar_c), every row a type-9 action
     * that moves the camera first. Without the office package it stays the
     * plain list of M3 (layout navigation, the animated backdrop). */
    s_office_room = nfl2k5_eras_office_init() == 0 && nfl2k5_eras_office_ready();
    ok &= em_begin(EM_OFFICE, FEATURES, "1980 Season", OR_ROWS, office_msgs, 8);
    ok &= em_row(EM_OFFICE, OR_PLAY, RK_ACTION, "Play Next Game", 0, 1, 0);
    ok &= em_row(EM_OFFICE, OR_SCHEDULE, RK_ACTION, "Schedule", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_AFC, RK_ACTION, "AFC Standings", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_NFC, RK_ACTION, "NFC Standings", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_TEAM, RK_ACTION, "Your Team", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_DEPTH, RK_ACTION, "Depth Chart", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_LOOK, RK_ACTION, "Office Look", 0, 0, 0);
    ok &= em_row(EM_OFFICE, OR_ROSTERS, RK_ACTION, s_office_room ? "Unverified rosters" : "Rosters: unverified community roster", 0, 0, 0);
    if (ok && s_office_room) {
        MEM32(s_em[EM_OFFICE].header + 0x18) = em_wstr("front_office");
        MEM32(s_em[EM_OFFICE].header + 0x1C) = 0;
        MEM32(s_em[EM_OFFICE].header + 0x28) = 0x10;
    }
    fprintf(stderr, "[ERAS] menu: league screen = %s\n", s_office_room ? "the 3D office (erasoffice.iff)"
            : "the plain list (no office package: run python -I tools\\eras\\build-office-pack.py)");

    if (!ok) { fprintf(stderr, "[ERAS] menu: build failed (%u bytes used)\n", (unsigned)s_heap_used); memset(s_em, 0, sizeof s_em); return; }
    hub_refresh();
    nfl2k5_eras_sheets_install();   /* M7: the Schedule copy and Standings (fall back to the lists on failure) */

    if (strcmp(place, "main") && strcmp(place, "main-scroll")) {
        /* 6 rows -> 7: fits the navigation layout without the 0x04 flag */
        rows = em_insert_link(GAME_MODES, -1, s_em[EM_HUB].header, em_wstr("My NFL Eras"));
        fprintf(stderr, "[ERAS] menu: \"My NFL Eras\" at the end of Game Modes (%d rows)\n", rows);
    } else {
        /* After The Crib, not before it: the celebrity-phone icon is drawn
         * beside the third row (index 2) whatever that row is, so The Crib
         * keeps index 2 (seen in M3 review: with Eras at index 2 the
         * phone sat beside "My NFL Eras"). */
        rows = em_insert_link(MAIN_MENU, 3, s_em[EM_HUB].header, em_wstr("My NFL Eras"));
        if (!strcmp(place, "main-scroll")) MEM32(MAIN_MENU + 0x28) |= 0x4u;
        fprintf(stderr, "[ERAS] menu: \"My NFL Eras\" in the Main Menu after The Crib (%d rows, flags %02X)\n",
                rows, (unsigned)MEM32(MAIN_MENU + 0x28));
    }
    fprintf(stderr, "[ERAS] menu: hub %08X office %08X your team %08X schedule %08X AFC %08X NFC %08X settings %08X, %u bytes\n",
            (unsigned)s_em[EM_HUB].header, (unsigned)s_em[EM_OFFICE].header, (unsigned)s_em[EM_YOURTEAM].header,
            (unsigned)s_em[EM_SCHEDULE].header, (unsigned)s_em[EM_AFC].header, (unsigned)s_em[EM_NFC].header,
            (unsigned)s_em[EM_SETTINGS].header, (unsigned)s_heap_used);
}

/*
 * My NFL Eras: the result of a played game (2026-10-15, docs/eras/PHASE2-PLAN.md M1).
 *
 * Two gen patches (tools/apply-gen-patches.py) and their interpreter hooks
 * (recomp_manual.c nfl2k5_eras_interp_hooks) call in here:
 *
 *   ERAS_GAME_FINAL  sub_000A2910 entry, the final whistle. Its only caller is
 *                    the tail jump of sub_000AED90, the entry callback of match
 *                    flow state 0x0A (game over); ecx = home points, edx = away
 *                    points, and [0xE602B8] still holds the previous state
 *                    (0x0A there means a re-entry). Teams, rosters and live
 *                    stats are all still in place.
 *   ERAS_MATCH_EXIT  sub_000645D0 entry, leaving the match. Still armed there
 *                    means the game never reached state 0x0A: abandoned.
 *
 * Nothing happens unless a capture is armed: by the Eras launcher
 * (nfl2k5_eras_capture_arm), or for tests by NFL2K5_ERAS_CAPTURE=1, which arms
 * every game and re-arms after each match exit. At the whistle the capture
 * reads, on the guest thread, the score, period, clock, old state, game mode,
 * timeouts and points per period of both sides, the team box through the
 * title's team accessor sub_000CB2D0 and every live player's box through the
 * player accessor sub_000CB240 (scope 0 = this match; selectors in
 * nfl2k5_eras_selectors.h, generated from data/eras/stat_selectors_2k5.json),
 * plus each player's record pointer, name pointers, jersey and position (the
 * M2 identity check). Then it disarms, and a worker thread writes the box
 * score JSON and one results line. Evidence for every address:
 * docs/eras/research/RESULT-CAPTURE.md.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_eras_capture.h"
#include "nfl2k5_eras_selectors.h"
#include "nfl2k5_teams.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"

/* recomp_manual.c: register- and x87-preserving calls of the stat accessors */

#define FLOW_STATE  0xE602B8u   /* match flow state: 0x0A game over */
#define PERIOD      0xE602C4u   /* 1..4 quarters, 5+ overtime */
#define PHASE       0xE602B4u   /* 3 = PAT pending, 4 = scrimmage */
#define CLOCK_OBJ   0xE6028Cu   /* -> clock object, +0x10 seconds left */
#define GAME_MODE   0xE5FF80u   /* 4 exhibition; 5-7 franchise */
#define MAX_PLAYERS 71u         /* team-data record: player pointers, count at +0x11C */
#define T_COUNT     0x11Cu
#define P_FIRST     0x10u       /* player record: first / last name */
#define P_LAST      0x14u
#define P_JERSEY    0x20u       /* (u32 >> 3) & 0x7F */
#define P_SLOT      0x30u       /* -> live stat record */
#define P_POSITION  0x35u

static const struct {
    uint32_t team, object, block, playbook;
    const char *name;
} k_side[2] = {
    { 0xB30864u, 0xE5FC20u, 0xBF08E0u, 0xB307D0u, "home" },
    { 0xB30A58u, 0xE5FC60u, 0xBF0B5Cu, 0xB30810u, "away" },
};

typedef struct {
    uint32_t record, first_ptr, last_ptr, slot;
    char first[40], last[48];
    int jersey, position, photo, live;
    float box[ERAS_PLAYER_BOX_COUNT];      /* NAN: the read failed */
} ErasPlayer;

typedef struct {
    uint32_t team, object, block, team_check;
    char city[48], nickname[48], code[16], playbook[40];
    int ident, category, count, live;
    int points, timeouts;
    uint32_t period_raw[5];                 /* block +0x254..+0x264: Q1..Q4, OT */
    float period_points[5];                 /* team 0x5B, scopes 1..5 */
    float box[ERAS_TEAM_BOX_COUNT];
    ErasPlayer players[MAX_PLAYERS];
} ErasSide;

typedef struct {
    char game_id[64], box_path[MAX_PATH], results_csv[MAX_PATH], roster_source[64];
    uint32_t reg_home, reg_away;            /* ecx / edx at the whistle */
    uint32_t period, old_state, mode, phase;
    float clock;
    unsigned calls, failures;
    double ms;
    char warnings[512];
    SYSTEMTIME when;
    ErasSide side[2];                       /* 0 home, 1 away */
} ErasSnapshot;

static ErasSnapshot s_snap;
static Nfl2k5ErasResult s_result;
static char s_box_path[MAX_PATH], s_results_csv[MAX_PATH], s_roster_source[64];
static int s_armed, s_pending;              /* guest thread only */
static int s_log = -1, s_test;
static unsigned s_test_n;

#define ERAS_LOG(...) do { if (s_log > 0) fprintf(stderr, __VA_ARGS__); } while (0)

/* ---- guest memory, read without trusting a pointer ----------------------- */

static int rd(uint32_t address, void *out, size_t size)
{
    uint64_t end = (uint64_t)address + size;
    if (!((address >= 0x10000u && end <= 0x10000000u) ||
          (address >= 0x80000000u && end <= 0x90000000u)))
        return 0;
    __try {
        memcpy(out, (const void *)((uintptr_t)address + g_xbox_mem_offset), size);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static uint32_t rd32(uint32_t address)
{
    uint32_t v = 0;
    rd(address, &v, sizeof v);
    return v;
}

/* UTF-16 or 8-bit text at a guest address, as bounded UTF-8. */
static int text_at(uint32_t address, char *out, size_t capacity)
{
    unsigned char first[2];
    size_t o = 0;
    unsigned i;
    int wide;
    out[0] = 0;
    if (!address || capacity < 4 || !rd(address, first, sizeof first) || !first[0]) return 0;
    wide = first[1] == 0;
    for (i = 0; i < 64; ++i) {
        unsigned ch;
        if (wide) { uint16_t w; if (!rd(address + 2u * i, &w, 2)) return 0; ch = w; }
        else { unsigned char b; if (!rd(address + i, &b, 1)) return 0; ch = b; }
        if (!ch) break;
        if (ch < 32 || (ch >= 0xD800 && ch < 0xE000)) { out[0] = 0; return 0; }
        if (ch < 0x80) {
            if (o + 1 >= capacity) break;
            out[o++] = (char)ch;
        } else if (ch < 0x800) {
            if (o + 2 >= capacity) break;
            out[o++] = (char)(0xC0 | (ch >> 6));
            out[o++] = (char)(0x80 | (ch & 0x3F));
        } else {
            if (o + 3 >= capacity) break;
            out[o++] = (char)(0xE0 | (ch >> 12));
            out[o++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    out[o] = 0;
    return o != 0;
}

/* A record's text field: a fixed-up pointer, or (added rosters) relative to
 * the field, as the presentation reader resolves it. */
static int field_text(uint32_t field, char *out, size_t capacity)
{
    uint32_t value = rd32(field);
    int64_t relative;
    out[0] = 0;
    if (!value) return 0;
    if (text_at(value, out, capacity)) return 1;
    relative = (int64_t)field + (int32_t)value - 1;
    if (relative > 0 && relative <= UINT32_MAX && text_at((uint32_t)relative, out, capacity)) return 1;
    relative++;
    if (relative > 0 && relative <= UINT32_MAX && text_at((uint32_t)relative, out, capacity)) return 1;
    out[0] = 0;
    return 0;
}

static void warn(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(s_snap.warnings);
    if (n + 2 >= sizeof s_snap.warnings) return;
    if (n) s_snap.warnings[n++] = ';';
    va_start(ap, fmt);
    _vsnprintf_s(s_snap.warnings + n, sizeof s_snap.warnings - n, _TRUNCATE, fmt, ap);
    va_end(ap);
}

static float team_stat(uint32_t team, uint32_t selector, uint32_t scope)
{
    double v;
    s_snap.calls++;
    if (g_eras_svc->team_stat(team, selector, scope, &v)) return (float)v;
    s_snap.failures++;
    return NAN;
}

static float player_stat(uint32_t player, uint32_t selector)
{
    double v;
    s_snap.calls++;
    if (g_eras_svc->player_stat(player, selector, 0u, &v)) return (float)v;
    s_snap.failures++;
    return NAN;
}

/* ---- the capture ---------------------------------------------------------- */

static void capture_side(int side)
{
    ErasSide *s = &s_snap.side[side];
    uint32_t score, i, q;
    uint16_t ident = 0;
    unsigned char count = 0, category = 0;
    s->team = k_side[side].team;
    s->object = k_side[side].object;
    /* team object +0x1C / +0x18: the team-data record and live stat block the
     * match bound to this side (sub_00087160) */
    s->team_check = rd32(s->object + 0x1Cu);
    s->block = rd32(s->object + 0x18u);
    if (s->team_check != s->team) warn("%s team object +0x1C = %08X", k_side[side].name, s->team_check);
    if (s->block != k_side[side].block) warn("%s stat block = %08X", k_side[side].name, s->block);
    score = rd32(s->object + 8u);
    s->points = (int)rd32(score);
    s->timeouts = (int)rd32(score + 4u);
    for (q = 0; q < 5; ++q) {
        s->period_raw[q] = s->block ? rd32(s->block + 0x254u + 4u * q) : 0u;
        s->period_points[q] = team_stat(s->team, 0x5Bu, q + 1u);
    }
    for (i = 0; i < ERAS_TEAM_BOX_COUNT; ++i)
        s->box[i] = team_stat(s->team, k_eras_team_box[i].selector, 0u);
    field_text(s->team + 0x138u, s->city, sizeof s->city);
    field_text(s->team + 0x104u, s->nickname, sizeof s->nickname);
    field_text(s->team + 0x10Cu, s->code, sizeof s->code);
    text_at(k_side[side].playbook, s->playbook, sizeof s->playbook);
    rd(s->team + 0x118u, &ident, sizeof ident);
    rd(s->team + 0x128u, &category, sizeof category);
    rd(s->team + T_COUNT, &count, sizeof count);
    s->ident = ident;
    s->category = category;
    if (count > MAX_PLAYERS) { warn("%s player count %u", k_side[side].name, count); count = 0; }
    s->count = count;
    for (i = 0; i < count; ++i) {
        ErasPlayer *p = &s->players[i];
        uint32_t k;
        unsigned char position = 0xFF;
        uint16_t photo = 0;
        p->record = rd32(s->team + 4u * i);
        if (!p->record) continue;
        p->first_ptr = rd32(p->record + P_FIRST);
        p->last_ptr = rd32(p->record + P_LAST);
        field_text(p->record + P_FIRST, p->first, sizeof p->first);
        field_text(p->record + P_LAST, p->last, sizeof p->last);
        p->jersey = (int)((rd32(p->record + P_JERSEY) >> 3) & 0x7Fu);
        rd(p->record + P_POSITION, &position, 1);
        rd(p->record + 6u, &photo, sizeof photo);
        p->position = position;
        p->photo = photo;
        p->slot = rd32(p->record + P_SLOT);
        /* No live stat record: the accessor would read nothing (the live QB
         * sampler skips these players the same way). */
        p->live = p->slot && rd32(p->slot);
        if (!p->live) continue;
        s->live++;
        for (k = 0; k < ERAS_PLAYER_BOX_COUNT; ++k)
            p->box[k] = player_stat(p->record, k_eras_player_box[k].selector);
    }
}

static int box_index(const ErasSelector *table, unsigned n, const char *key)
{
    unsigned i;
    for (i = 0; i < n; ++i) if (!strcmp(table[i].key, key)) return (int)i;
    return -1;
}

static int ival(float v) { return isnan(v) ? -1 : (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

static float team_box(const ErasSide *s, const char *key)
{
    int i = box_index(k_eras_team_box, ERAS_TEAM_BOX_COUNT, key);
    return i < 0 ? NAN : s->box[i];
}

/* One line per side for the log: the values T1.1 compares with GameCast Stats. */
static void log_summary(void)
{
    int side;
    int ia = box_index(k_eras_player_box, ERAS_PLAYER_BOX_COUNT, "pass_att");
    for (side = 0; side < 2; ++side) {
        const ErasSide *s = &s_snap.side[side];
        const ErasPlayer *qb = NULL;
        int i;
        ERAS_LOG("[ERAS]   %s %s %s (%s) pts=%d to=%d periods=%u,%u,%u,%u,%u acc=%d,%d,%d,%d,%d "
                 "1st=%d pass=%d rush=%d total=%d turnovers=%d top=%.2f sel62=%.2f sel63=%.2f players=%d live=%d\n",
                 k_side[side].name, s->city, s->nickname, s->code, s->points, s->timeouts,
                 s->period_raw[0], s->period_raw[1], s->period_raw[2], s->period_raw[3], s->period_raw[4],
                 ival(s->period_points[0]), ival(s->period_points[1]), ival(s->period_points[2]),
                 ival(s->period_points[3]), ival(s->period_points[4]),
                 ival(team_box(s, "first_downs")), ival(team_box(s, "pass_yds")), ival(team_box(s, "rush_yds")),
                 ival(team_box(s, "total_yds")), ival(team_box(s, "turnovers")),
                 team_box(s, "time_of_possession"), team_box(s, "sel_62"), team_box(s, "sel_63"),
                 s->count, s->live);
        for (i = 0; i < s->count; ++i) {
            const ErasPlayer *p = &s->players[i];
            if (p->live && ia >= 0 && (!qb || p->box[ia] > qb->box[ia])) qb = p;
        }
        if (qb) {
            int c = box_index(k_eras_player_box, ERAS_PLAYER_BOX_COUNT, "pass_comp");
            int y = box_index(k_eras_player_box, ERAS_PLAYER_BOX_COUNT, "pass_yds");
            int t = box_index(k_eras_player_box, ERAS_PLAYER_BOX_COUNT, "pass_td");
            int n = box_index(k_eras_player_box, ERAS_PLAYER_BOX_COUNT, "pass_int");
            ERAS_LOG("[ERAS]   %s QB #%d %s %s %d/%d %d yds TD=%d INT=%d\n", k_side[side].name, qb->jersey,
                     qb->first, qb->last, ival(qb->box[c]), ival(qb->box[ia]), ival(qb->box[y]),
                     ival(qb->box[t]), ival(qb->box[n]));
        }
    }
}

/* ---- writing (worker thread, on a copy) ----------------------------------- */

typedef struct { char *p; size_t n, cap; } Buf;

static void put(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int need;
    for (;;) {
        va_start(ap, fmt);
        need = _vsnprintf_s(b->p + b->n, b->cap - b->n, _TRUNCATE, fmt, ap);
        va_end(ap);
        if (need >= 0) { b->n += (size_t)need; return; }
        {
            char *grown = (char *)realloc(b->p, b->cap * 2);
            if (!grown) return;
            b->p = grown;
            b->cap *= 2;
        }
    }
}

static void put_str(Buf *b, const char *s)
{
    put(b, "\"");
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(b, "\\%c", c);
        else if (c < 32) put(b, "\\u%04x", c);
        else put(b, "%c", c);
    }
    put(b, "\"");
}

static void put_num(Buf *b, float v)
{
    if (isnan(v)) put(b, "null");
    else if (v == floorf(v) && fabsf(v) < 1e9f) put(b, "%d", (int)v);
    else put(b, "%.3f", v);
}

static void write_side(Buf *b, const ErasSnapshot *snap, int side)
{
    const ErasSide *s = &snap->side[side];
    unsigned i, k;
    put(b, "  \"%s\": {\n", k_side[side].name);
    put(b, "    \"team_record\": \"0x%08X\", \"team_object\": \"0x%08X\", \"stat_block\": \"0x%08X\",\n",
        s->team, s->object, s->block);
    put(b, "    \"city\": "); put_str(b, s->city);
    put(b, ", \"nickname\": "); put_str(b, s->nickname);
    put(b, ", \"code\": "); put_str(b, s->code);
    put(b, ", \"ident\": %d, \"category\": %d, \"playbook\": ", s->ident, s->category);
    put_str(b, s->playbook);
    put(b, ",\n    \"points\": %d, \"timeouts_left\": %d,\n", s->points, s->timeouts);
    put(b, "    \"period_points\": [%u, %u, %u, %u, %u],\n", s->period_raw[0], s->period_raw[1],
        s->period_raw[2], s->period_raw[3], s->period_raw[4]);
    put(b, "    \"period_points_accessor\": [");
    for (i = 0; i < 5; ++i) { if (i) put(b, ", "); put_num(b, s->period_points[i]); }
    put(b, "],\n    \"box\": {");
    for (i = 0; i < ERAS_TEAM_BOX_COUNT; ++i) {
        put(b, "%s\"%s\": ", i ? ", " : "", k_eras_team_box[i].key);
        put_num(b, s->box[i]);
    }
    put(b, "},\n    \"player_count\": %d, \"live_players\": %d,\n    \"players\": [\n", s->count, s->live);
    for (i = 0; i < (unsigned)s->count; ++i) {
        const ErasPlayer *p = &s->players[i];
        int any = 0;
        put(b, "      {\"index\": %u, \"record\": \"0x%08X\", \"first_ptr\": \"0x%08X\", \"last_ptr\": \"0x%08X\", \"first\": ",
            i, p->record, p->first_ptr, p->last_ptr);
        put_str(b, p->first);
        put(b, ", \"last\": ");
        put_str(b, p->last);
        put(b, ", \"jersey\": %d, \"position\": %d, \"photo\": %d, \"live\": %s, \"box\": {",
            p->jersey, p->position, p->photo, p->live ? "true" : "false");
        if (p->live) {
            for (k = 0; k < ERAS_PLAYER_BOX_COUNT; ++k) {
                /* absent = 0; a failed read is written as null */
                if (!isnan(p->box[k]) && p->box[k] == 0.0f) continue;
                put(b, "%s\"%s\": ", any++ ? ", " : "", k_eras_player_box[k].key);
                put_num(b, p->box[k]);
            }
        }
        put(b, "}}%s\n", i + 1 < (unsigned)s->count ? "," : "");
    }
    put(b, "    ]\n  }");
}

static void make_dirs(const char *path)
{
    char dir[MAX_PATH];
    char *p;
    strncpy_s(dir, sizeof dir, path, _TRUNCATE);
    for (p = dir + 3; *p; ++p) {
        if (*p != '\\' && *p != '/') continue;
        *p = 0;
        CreateDirectoryA(dir, NULL);
        *p = '\\';
    }
}

static DWORD WINAPI writer(LPVOID arg)
{
    ErasSnapshot *snap = (ErasSnapshot *)arg;
    Buf b = { NULL, 0, 1u << 18 };
    char tmp[MAX_PATH + 8];
    FILE *f;
    int written = 0;
    b.p = (char *)malloc(b.cap);
    if (!b.p) { free(snap); return 0; }
    b.p[0] = 0;
    put(&b, "{\n  \"schema\": \"nfl2k5_eras_box/v1\",\n  \"game_id\": ");
    put_str(&b, snap->game_id);
    if (snap->roster_source[0]) {
        put(&b, ",\n  \"roster_source\": ");
        put_str(&b, snap->roster_source);
    }
    put(&b, ",\n  \"captured_local\": \"%04u-%02u-%02u %02u:%02u:%02u\",\n",
        snap->when.wYear, snap->when.wMonth, snap->when.wDay, snap->when.wHour, snap->when.wMinute,
        snap->when.wSecond);
    put(&b, "  \"hook\": \"ERAS_GAME_FINAL sub_000A2910 (flow state 0x0A entry)\",\n");
    put(&b, "  \"home_points\": %u, \"away_points\": %u,\n", snap->reg_home, snap->reg_away);
    put(&b, "  \"scoreboard_points\": [%d, %d],\n", snap->side[0].points, snap->side[1].points);
    put(&b, "  \"final_period\": %u, \"overtime\": %s, \"tie\": %s, \"clock_seconds\": %.2f,\n",
        snap->period, snap->period >= 5 ? "true" : "false",
        snap->reg_home == snap->reg_away ? "true" : "false", snap->clock);
    put(&b, "  \"end_state\": \"0x%02X\", \"play_phase\": %u, \"mode\": %u,\n", snap->old_state, snap->phase, snap->mode);
    put(&b, "  \"capture_ms\": %.2f, \"guest_calls\": %u, \"guest_call_failures\": %u,\n",
        snap->ms, snap->calls, snap->failures);
    put(&b, "  \"warnings\": ");
    put_str(&b, snap->warnings);
    put(&b, ",\n  \"notes\": \"box: scope 0 (this match) through sub_000CB2D0 / sub_000CB240; player box lists only "
            "non-zero values; period_points = stat block +0x254..+0x264 (Q1-Q4, one OT slot for every OT period). "
            "Unsettled meanings: team sel_62, sel_63, time_of_possession units; player sel_30, sel_63.\",\n");
    write_side(&b, snap, 0);
    put(&b, ",\n");
    write_side(&b, snap, 1);
    put(&b, "\n}\n");

    /* never overwrite a box score: write a temporary file, then rename */
    make_dirs(snap->box_path);
    _snprintf_s(tmp, sizeof tmp, _TRUNCATE, "%s.tmp", snap->box_path);
    if (fopen_s(&f, tmp, "wb") == 0 && f) {
        written = fwrite(b.p, 1, b.n, f) == b.n;
        written = (fclose(f) == 0) && written;
        if (written && !MoveFileExA(tmp, snap->box_path, MOVEFILE_WRITE_THROUGH)) {
            fprintf(stderr, "[ERAS] %s exists or cannot be written (error %lu); kept %s\n",
                    snap->box_path, (unsigned long)GetLastError(), tmp);
            written = 0;
        }
    }
    if (written) ERAS_LOG("[ERAS] wrote %s (%u bytes)\n", snap->box_path, (unsigned)b.n);
    else fprintf(stderr, "[ERAS] box score for %s not written\n", snap->game_id);
    if (snap->results_csv[0]) {
        int row = 0;
        make_dirs(snap->results_csv);
        if (fopen_s(&f, snap->results_csv, "ab") == 0 && f) {
            if (fseek(f, 0, SEEK_END) == 0 && _ftelli64(f) == 0)
                fprintf(f, "game_id,home_pts,away_pts,final_period,end_state,status\r\n");
            row = fprintf(f, "%s,%u,%u,%u,0x%02X,final\r\n", snap->game_id, snap->reg_home, snap->reg_away,
                          snap->period, snap->old_state) > 0;
            row = (fclose(f) == 0) && row;
        }
        if (!row) fprintf(stderr, "[ERAS] results row for %s not written to %s\n", snap->game_id, snap->results_csv);
        written = written && row;
    }
    /* the launcher (src/nfl2k5_eras_launch.c) waits on this before it commits */
    if (!strcmp(snap->game_id, s_result.game_id)) InterlockedExchange(&s_result.written, written ? 1 : -1);
    free(b.p);
    free(snap);
    return 0;
}

/* ---- entry points --------------------------------------------------------- */

void nfl2k5_eras_capture_arm(const char *game_id, const char *box_path, const char *results_csv)
{
    if (s_log < 0) s_log = getenv("NFL2K5_ERAS_LOG") != NULL;
    memset(&s_result, 0, sizeof s_result);
    strncpy_s(s_result.game_id, sizeof s_result.game_id, game_id ? game_id : "", _TRUNCATE);
    strncpy_s(s_box_path, sizeof s_box_path, box_path ? box_path : "", _TRUNCATE);
    strncpy_s(s_results_csv, sizeof s_results_csv, results_csv ? results_csv : "", _TRUNCATE);
    s_roster_source[0] = 0;
    s_result.status = NFL2K5_ERAS_CAPTURE_ARMED;
    s_armed = 1;
    s_pending = 0;
    ERAS_LOG("[ERAS] armed game=%s box=%s\n", s_result.game_id, s_box_path);
}

void nfl2k5_eras_capture_set_roster_source(const char *label)
{
    strncpy_s(s_roster_source, sizeof s_roster_source, label ? label : "", _TRUNCATE);
}

void nfl2k5_eras_capture_disarm(void)
{
    if (s_armed) ERAS_LOG("[ERAS] disarmed game=%s\n", s_result.game_id);
    s_armed = 0;
    if (s_result.status == NFL2K5_ERAS_CAPTURE_ARMED) s_result.status = NFL2K5_ERAS_CAPTURE_IDLE;
}

void nfl2k5_eras_capture_result(Nfl2k5ErasResult *out)
{
    if (out) *out = s_result;
}

static void arm_test(void)
{
    char rel[64], path[MAX_PATH], csv[MAX_PATH], id[32];
    do {
        ++s_test_n;
        _snprintf_s(rel, sizeof rel, _TRUNCATE, "logs\\eras-capture-%u.json", s_test_n);
        g_eras_svc->user_path(rel, path, sizeof path);
    } while (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && s_test_n < 100000u);
    _snprintf_s(id, sizeof id, _TRUNCATE, "test-%u", s_test_n);
    g_eras_svc->user_path("logs\\eras-capture-results.csv", csv, sizeof csv);
    nfl2k5_eras_capture_arm(id, path, csv);
}

void nfl2k5_eras_capture_init(void)
{
    const char *e = getenv("NFL2K5_ERAS_CAPTURE");
    s_log = getenv("NFL2K5_ERAS_LOG") != NULL;
    s_test = e && *e && *e != '0';
    if (s_test) arm_test();
}

void nfl2k5_eras_game_final(uint32_t home_points, uint32_t away_points)
{
    LARGE_INTEGER t0, t1, freq;
    uint32_t old_state = rd32(FLOW_STATE), clock_object;
    ErasSnapshot *copy;
    HANDLE h;
    if (!s_armed) {
        ERAS_LOG("[ERAS] final ignored (not armed): home=%u away=%u old_state=0x%02X period=%u\n",
                 home_points, away_points, old_state, rd32(PERIOD));
        return;
    }
    if (old_state == 0x0Au) {
        ERAS_LOG("[ERAS] final ignored (re-entry, state already 0x0A)\n");
        return;
    }
    QueryPerformanceCounter(&t0);
    memset(&s_snap, 0, sizeof s_snap);
    strncpy_s(s_snap.game_id, sizeof s_snap.game_id, s_result.game_id, _TRUNCATE);
    strncpy_s(s_snap.box_path, sizeof s_snap.box_path, s_box_path, _TRUNCATE);
    strncpy_s(s_snap.results_csv, sizeof s_snap.results_csv, s_results_csv, _TRUNCATE);
    strncpy_s(s_snap.roster_source, sizeof s_snap.roster_source, s_roster_source, _TRUNCATE);
    GetLocalTime(&s_snap.when);
    s_snap.reg_home = home_points;
    s_snap.reg_away = away_points;
    s_snap.old_state = old_state;
    s_snap.period = rd32(PERIOD);
    s_snap.mode = rd32(GAME_MODE);
    s_snap.phase = rd32(PHASE);
    clock_object = rd32(CLOCK_OBJ);
    if (!clock_object || !rd(clock_object + 0x10u, &s_snap.clock, sizeof s_snap.clock)) s_snap.clock = NAN;
    capture_side(0);
    capture_side(1);
    /* M2 test T2.2: the staged teams' players, 1:1 with their arena rows */
    g_eras_svc->identity_check(0, k_side[0].team);
    g_eras_svc->identity_check(1, k_side[1].team);
    /* sub_000AED90 loads ecx/edx from these same score objects */
    if ((int)home_points != s_snap.side[0].points || (int)away_points != s_snap.side[1].points)
        warn("registers %u-%u differ from the scoreboard %d-%d", home_points, away_points,
             s_snap.side[0].points, s_snap.side[1].points);
    {
        int side;
        for (side = 0; side < 2; ++side) {
            const ErasSide *s = &s_snap.side[side];
            uint32_t sum = s->period_raw[0] + s->period_raw[1] + s->period_raw[2] + s->period_raw[3] + s->period_raw[4];
            if ((int)sum != s->points) warn("%s period points sum %u != %d", k_side[side].name, sum, s->points);
        }
    }
    if (s_snap.mode != 4u) warn("mode %u (Eras games run in mode 4)", s_snap.mode);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    s_snap.ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    s_armed = 0;
    s_pending = 1;
    s_result.status = NFL2K5_ERAS_CAPTURE_FINAL;
    s_result.home_points = (int)home_points;
    s_result.away_points = (int)away_points;
    s_result.final_period = (int)s_snap.period;
    s_result.end_state = (int)old_state;
    s_result.mode = (int)s_snap.mode;
    ERAS_LOG("[ERAS] final game=%s home=%u away=%u board=%d-%d period=%u clock=%.2f old_state=0x%02X "
             "mode=%u phase=%u calls=%u failures=%u ms=%.2f warnings=%s\n",
             s_snap.game_id, home_points, away_points, s_snap.side[0].points, s_snap.side[1].points,
             s_snap.period, s_snap.clock, old_state, s_snap.mode, s_snap.phase, s_snap.calls,
             s_snap.failures, s_snap.ms, s_snap.warnings[0] ? s_snap.warnings : "none");
    log_summary();
    /* the files are written off the guest thread, from a copy */
    copy = (ErasSnapshot *)malloc(sizeof *copy);
    if (!copy) { fprintf(stderr, "[ERAS] out of memory: box score for %s not written\n", s_snap.game_id); s_result.written = -1; return; }
    memcpy(copy, &s_snap, sizeof *copy);
    h = CreateThread(NULL, 0, writer, copy, 0, NULL);
    if (h) CloseHandle(h);
    else { fprintf(stderr, "[ERAS] writer thread failed; box score for %s not written\n", s_snap.game_id); free(copy); s_result.written = -1; }
}

void nfl2k5_eras_match_exit(void)
{
    uint32_t period = rd32(PERIOD), state = rd32(FLOW_STATE), mode = rd32(GAME_MODE);
    if (s_armed) {
        s_armed = 0;
        s_result.status = NFL2K5_ERAS_CAPTURE_ABANDONED;
        s_result.final_period = (int)period;
        s_result.end_state = (int)state;
        s_result.mode = (int)mode;
        ERAS_LOG("[ERAS] match exit: abandoned game=%s period=%u state=0x%02X mode=%u score=%u-%u\n",
                 s_result.game_id, period, state, mode, rd32(rd32(0xE5FC28u)), rd32(rd32(0xE5FC68u)));
    } else if (s_pending) {
        s_pending = 0;
        ERAS_LOG("[ERAS] match exit: committed game=%s final %d-%d period=%u state=0x%02X mode=%u\n",
                 s_result.game_id, s_result.home_points, s_result.away_points, period, state, mode);
    } else {
        ERAS_LOG("[ERAS] match exit (nothing armed) period=%u state=0x%02X mode=%u\n", period, state, mode);
    }
    if (s_test) arm_test();
}

/* My NFL Eras: depth charts and head coaches of staged teams (M6,
 * docs/eras/results/M6-depth.md).
 *
 * Depth. A player's depth is two 3-bit fields of his record's word +0x28: rank
 * (bits 10-12) and side (bits 13-15). For the paired positions (WR CB DE DT
 * OLB ILB G T) rank is the left chain and side the right chain (rank 0 = left
 * starter, side 0 = right starter); for the others only rank counts (the Mod
 * Tools' depth-lock report, retail table 0x5140D8). The roster writer
 * (nfl2k5_team_roster_file) ranks by row order; a depth file then sets both
 * fields per row:
 *
 *   row,first,last,pos,number,rank,side          (row = roster.csv row = slot player index)
 *   special,<holder>,<kr1>,<kr2>,<k>,<ls>,<pr>   (optional: team +0x194..+0x199, row indices)
 *
 * The league's own file is <league>\depth\<id>.csv (written here after an edit);
 * without it the package seed data\eras\runtime\<season>\depth\<id>.csv applies
 * (build/eras/make_runtime_package.py, from the community save's stored depth).
 * The package is read only.
 *
 * Editing uses the game's own Depth Chart (the Roster Manager one, never
 * Franchise's): the team is staged into a spare slot (USER1, else USER2),
 * [0xC1749C] (the depth chart's team, set by sub_00243E80 / sub_00243F10) is
 * pointed at it, [0xC174B4] (Franchise's flag: 0 outside Franchise) is 0, and
 * 0x532D14 (Offense / Defense / 3-4 Defense / Special Teams) is pushed. Those
 * screens edit the slot's player records. When the Eras screen below is shown
 * again, the fields are read back, written to the league's file if they
 * changed, and the slot is released.
 *
 * Coaches. Team +0x14C points at the team's coach record (0xA8 bytes: first
 * +0x00, last +0x04, biography lines +0x08..+0x10, career numbers 0x1C..0x3A;
 * the Mod Tools' coach map). Coach Match Up reads it at msg 1 (sub_00366E20).
 * A staged team's pointer is the template's, a retail record, so the staged
 * team gets a copy of its own with the 1980 coach's name instead; the career
 * numbers and biography are cleared (not sourced). The pointer is put back
 * before the slot is released. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_plugin_api.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_depth.h"
#include "nfl2k5_eras_launch.h"
#include "nfl2k5_teams.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void nfl2k5_eras_repoint_released(uint32_t slot0, uint32_t slot1);

#define DC_TEAM         0x00C1749Cu   /* the depth chart's team */
#define DC_FRANCHISE    0x00C174B4u   /* 1: opened from Franchise (its msg 2 then re-sorts the user team) */
#define DC_PAGE         0x00C174B8u   /* offense / defense / 3-4 / special */
#define DC_MENU         0x00532D14u   /* "Depth Chart": Offense / Defense / 3-4 Defense / Special Teams */
#define SND_SELECT      0x00E88968u   /* L"Ambient: Menu Select" */
#define T_ACTIVE        0x11Cu
#define T_SPECIAL       0x194u
#define T_COACH         0x14Cu
#define MAXP            71
#define COACH_SIZE      0xA8u

typedef struct {
    int n;
    struct { int row, pos, num, rank, side; char first[32], last[32]; } p[MAXP];
    int special[6], has_special;
} DepthFile;

static struct {
    int active, side, team;
    uint32_t slot, save_team, save_flag, save_page;
    uint8_t rank[MAXP], sidev[MAXP], special[6];
    int count, source;
} s_ed;

static const char *const k_pos[17] = {
    "QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE"
};

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, 0x0BADC0DEu);
    fn();
    return g_eax;
}

/* ---- files ------------------------------------------------------------- */

static void league_file(const ErasTeam *t, char *out, size_t n)
{
    char rel[64];
    snprintf(rel, sizeof rel, "depth\\%s.csv", t->id);
    nfl2k5_eras_league_path(rel, out, (unsigned)n);
}

static void seed_file(const ErasTeam *t, char *out, size_t n)
{
    char *p;
    snprintf(out, n, "%s\\depth\\%s.csv", nfl2k5_eras_package_dir(), t->id);
    for (p = out; *p; p++) if (*p == '/') *p = '\\';
}

static int read_depth(const char *path, DepthFile *d)
{
    char line[256];
    FILE *f;
    memset(d, 0, sizeof *d);
    if (fopen_s(&f, path, "r") || !f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *v[8], *p = line, *e;
        int k = 0;
        if (line[0] == '#' || !strncmp(line, "row,", 4)) continue;
        while (k < 8) { v[k++] = p; if (!(p = strchr(p, ','))) break; *p++ = 0; }
        for (e = v[k - 1] + strlen(v[k - 1]); e > v[k - 1] && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
        if (!strcmp(v[0], "special") && k == 7) {
            int i;
            for (i = 0; i < 6; i++) d->special[i] = atoi(v[1 + i]);
            d->has_special = 1;
        } else if (k == 7 && d->n < MAXP) {
            d->p[d->n].row = atoi(v[0]);
            snprintf(d->p[d->n].first, sizeof d->p[d->n].first, "%s", v[1]);
            snprintf(d->p[d->n].last, sizeof d->p[d->n].last, "%s", v[2]);
            d->p[d->n].pos = atoi(v[3]);
            d->p[d->n].num = atoi(v[4]);
            d->p[d->n].rank = atoi(v[5]) & 7;
            d->p[d->n].side = atoi(v[6]) & 7;
            d->n++;
        }
    }
    fclose(f);
    return d->n > 0;
}

/* The file that applies to the team: ERAS_DEPTH_LEAGUE / _SEED / _LIST. */
static int effective(const ErasTeam *t, DepthFile *d, char *path, size_t n)
{
    league_file(t, path, n);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && read_depth(path, d)) return ERAS_DEPTH_LEAGUE;
    seed_file(t, path, n);
    if (read_depth(path, d)) return ERAS_DEPTH_SEED;
    path[0] = 0;
    return ERAS_DEPTH_LIST;
}

int nfl2k5_eras_depth_source(int team)
{
    static DepthFile d;
    char path[MAX_PATH];
    const ErasTeam *t = nfl2k5_eras_team(team);
    return t ? effective(t, &d, path, sizeof path) : ERAS_DEPTH_LIST;
}

void nfl2k5_eras_depth_starter(int team, int pos, char *out, size_t n)
{
    static DepthFile d;
    char path[MAX_PATH];
    const ErasTeam *t = nfl2k5_eras_team(team);
    int i;
    out[0] = 0;
    if (!t || effective(t, &d, path, sizeof path) == ERAS_DEPTH_LIST) return;
    for (i = 0; i < d.n; i++)
        if (d.p[i].pos == pos && d.p[i].rank == 0) {
            snprintf(out, n, "%c. %s", d.p[i].first[0], d.p[i].last);
            return;
        }
}

int nfl2k5_eras_depth_reset(int team)
{
    char path[MAX_PATH], old[MAX_PATH + 16];
    const ErasTeam *t = nfl2k5_eras_team(team);
    int k;
    if (!t) return 0;
    league_file(t, path, sizeof path);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return 0;
    for (k = 1; k < 1000; k++) {             /* moved aside, never deleted */
        snprintf(old, sizeof old, "%s.old%d", path, k);
        if (GetFileAttributesA(old) == INVALID_FILE_ATTRIBUTES) break;
    }
    if (!MoveFileA(path, old)) return 0;
    nfl2k5_eras_log("depth chart %s: the league's depth chart moved aside to %s; the community roster's applies again", t->id, old);
    return 1;
}

/* ---- players ----------------------------------------------------------- */

static void wname(uint32_t w, char *out, size_t n)
{
    size_t i;
    for (i = 0; w && i + 1 < n; i++) { uint16_t c = MEM16(w + 2u * i); if (!c) break; out[i] = c >= 0x20 && c < 0x7F ? (char)c : '?'; }
    out[i] = 0;
}

static int wmatch(uint32_t w, const char *s)
{
    size_t i;
    if (!w) return 0;
    for (i = 0; i < 31; i++) {
        uint16_t c = MEM16(w + 2u * i);
        if (c != (uint16_t)(unsigned char)s[i]) return 0;
        if (!c) return 1;
    }
    return 1;
}

static uint32_t player(uint32_t slot, int i) { return MEM32(slot + 4u * (uint32_t)i); }
static int p_rank(uint32_t p) { return (MEM16(p + 0x28) >> 10) & 7; }
static int p_side(uint32_t p) { return (MEM16(p + 0x28) >> 13) & 7; }
static int p_num(uint32_t p) { return (int)((MEM32(p + 0x20) >> 3) & 0x7F); }

static int find_player(uint32_t slot, int count, const DepthFile *d, int k)
{
    int i = d->p[k].row;
    uint32_t p;
    if (i >= 0 && i < count && (p = player(slot, i)) && wmatch(MEM32(p + 0x10), d->p[k].first) &&
        wmatch(MEM32(p + 0x14), d->p[k].last) && MEM8(p + 0x35) == d->p[k].pos)
        return i;
    for (i = 0; i < count; i++) {             /* the order changed: by name, number and position */
        p = player(slot, i);
        if (p && wmatch(MEM32(p + 0x10), d->p[k].first) && wmatch(MEM32(p + 0x14), d->p[k].last) &&
            MEM8(p + 0x35) == d->p[k].pos && p_num(p) == d->p[k].num)
            return i;
    }
    return -1;
}

static void log_starters(const char *what, const ErasTeam *t, uint32_t slot)
{
    char line[400], nm[48];
    size_t o = 0;
    int count = MEM8(slot + T_ACTIVE), i, k;
    static const int show[] = { 0, 7, 3, 1, 2 };   /* QB HB WR K P */
    line[0] = 0;
    for (k = 0; k < 5; k++)
        for (i = 0; i < count && i < MAXP; i++) {
            uint32_t p = player(slot, i);
            if (p && MEM8(p + 0x35) == show[k] && p_rank(p) == 0) {
                wname(MEM32(p + 0x14), nm, sizeof nm);
                o += (size_t)snprintf(line + o, sizeof line - o, " %s %s #%d;", k_pos[show[k]], nm, p_num(p));
                break;
            }
        }
    nfl2k5_eras_log("depth chart %s %s: starters%s", t->id, what, line);
}

static int apply_to(uint32_t slot, const ErasTeam *t, int side)
{
    static DepthFile d;
    char path[MAX_PATH];
    int src = effective(t, &d, path, sizeof path), count = MEM8(slot + T_ACTIVE), k, set = 0, miss = 0;
    if (src == ERAS_DEPTH_LIST) {
        nfl2k5_eras_log("depth chart %s: no depth file; roster list order", t->id);
        return src;
    }
    for (k = 0; k < d.n; k++) {
        int i = find_player(slot, count, &d, k);
        uint32_t p;
        if (i < 0) { miss++; continue; }
        p = player(slot, i);
        MEM16(p + 0x28) = (uint16_t)((MEM16(p + 0x28) & 0x03FFu) | ((uint32_t)d.p[k].rank << 10) | ((uint32_t)d.p[k].side << 13));
        set++;
    }
    if (d.has_special)
        for (k = 0; k < 6; k++)
            if (d.special[k] == 255 || (d.special[k] >= 0 && d.special[k] < count))
                MEM8(slot + T_SPECIAL + (uint32_t)k) = (uint8_t)d.special[k];
    nfl2k5_eras_log("depth chart %s (side %d): %s %s, %d of %d players set%s%s", t->id, side,
                    src == ERAS_DEPTH_LEAGUE ? "the league's" : "the community roster's (seed)", path, set, count,
                    miss ? ", some rows not found" : "", d.has_special ? ", special teams roles" : "");
    log_starters("applied", t, slot);
    return src;
}

int nfl2k5_eras_depth_apply(int side, int team)
{
    const ErasTeam *t = nfl2k5_eras_team(team);
    uint32_t slot = g_eras_svc->staged_slot(side);
    if (!t || !slot) return ERAS_DEPTH_LIST;
    return apply_to(slot, t, side);
}

/* ---- editing ------------------------------------------------------------ */

int nfl2k5_eras_depth_editing(void) { return s_ed.active; }

int nfl2k5_eras_depth_edit(uint32_t fsm, int team, char *msg, size_t n)
{
    const ErasTeam *t = nfl2k5_eras_team(team);
    char roster[MAX_PATH], *p;
    int side, rc, i;
    uint32_t slot;
    msg[0] = 0;
    if (!t) { snprintf(msg, n, "No team"); return -1; }
    if (s_ed.active) { snprintf(msg, n, "A depth chart is still open"); return -1; }
    if (nfl2k5_eras_launch_active()) { snprintf(msg, n, "A league game is in progress"); return -1; }
    side = g_eras_svc->stage_check(0) == NFL2K5_STAGE_OK ? 0 : g_eras_svc->stage_check(1) == NFL2K5_STAGE_OK ? 1 : -1;
    if (side < 0) {
        rc = g_eras_svc->stage_check(0);
        snprintf(msg, n, rc == NFL2K5_STAGE_USER_TEAM ? "Can't open: Create-a-Team in USER1/2" : "Can't open: no free team slot");
        nfl2k5_eras_log("depth chart %s refused: %s (nothing changed)", t->id, g_eras_svc->stage_message(rc));
        return -2;
    }
    snprintf(roster, sizeof roster, "%s\\%s", nfl2k5_eras_package_dir(), t->roster_file);
    for (p = roster; *p; p++) if (*p == '/') *p = '\\';
    rc = g_eras_svc->stage(side, t->template_entry, roster, t->city, t->name, t->abbr);
    if (rc != NFL2K5_STAGE_OK) {
        g_eras_svc->release(side);
        snprintf(msg, n, "Can't open: %s not loaded", t->name);
        nfl2k5_eras_log("depth chart %s refused: staging failed: %s", t->id, g_eras_svc->stage_message(rc));
        return -3;
    }
    slot = g_eras_svc->staged_slot(side);
    nfl2k5_eras_apply_style(slot, team);   /* M9: the season's uniform style (helmet, card art) */
    s_ed.source = apply_to(slot, t, side);
    s_ed.count = MEM8(slot + T_ACTIVE);
    if (s_ed.count > MAXP) s_ed.count = MAXP;
    for (i = 0; i < s_ed.count; i++) {
        uint32_t pl = player(slot, i);
        s_ed.rank[i] = (uint8_t)(pl ? p_rank(pl) : 0);
        s_ed.sidev[i] = (uint8_t)(pl ? p_side(pl) : 0);
    }
    for (i = 0; i < 6; i++) s_ed.special[i] = MEM8(slot + T_SPECIAL + (uint32_t)i);
    s_ed.active = 1;
    s_ed.side = side;
    s_ed.team = team;
    s_ed.slot = slot;
    s_ed.save_team = MEM32(DC_TEAM);
    s_ed.save_flag = MEM32(DC_FRANCHISE);
    s_ed.save_page = MEM32(DC_PAGE);
    MEM32(DC_TEAM) = slot;
    MEM32(DC_FRANCHISE) = 0;
    nfl2k5_eras_log("depth chart %s: staged into USER%d (slot %08X, %d players); the game's Depth Chart %08X opens",
                    t->id, side + 1, slot, s_ed.count, DC_MENU);
    nfl2k5_eras_team_art_update();   /* M9: era card badges while the editor is open */
    gcall(sub_0006E390, fsm, DC_MENU);
    gcall(sub_0014D390, SND_SELECT, 0);
    return 0;
}

static void write_league(const ErasTeam *t, uint32_t slot, int count, char *path, size_t n)
{
    FILE *f;
    int i;
    league_file(t, path, n);
    if (fopen_s(&f, path, "w") || !f) { nfl2k5_eras_log("depth chart %s: cannot write %s", t->id, path); path[0] = 0; return; }
    fprintf(f, "# My NFL Eras depth chart of %s %s (this league only; edited in the office's Depth Chart)\n", t->city, t->name);
    fprintf(f, "row,first,last,pos,number,rank,side\n");
    for (i = 0; i < count; i++) {
        uint32_t p = player(slot, i);
        char a[40], b[40];
        if (!p) continue;
        wname(MEM32(p + 0x10), a, sizeof a);
        wname(MEM32(p + 0x14), b, sizeof b);
        fprintf(f, "%d,%s,%s,%u,%d,%d,%d\n", i, a, b, MEM8(p + 0x35), p_num(p), p_rank(p), p_side(p));
    }
    fprintf(f, "special,%u,%u,%u,%u,%u,%u\n", MEM8(slot + 0x194), MEM8(slot + 0x195), MEM8(slot + 0x196),
            MEM8(slot + 0x197), MEM8(slot + 0x198), MEM8(slot + 0x199));
    fclose(f);
}

int nfl2k5_eras_depth_edit_end(const char *why)
{
    const ErasTeam *t;
    uint32_t slot;
    int i, changed = 0, special = 0, count;
    char path[MAX_PATH] = "", detail[600];
    size_t o = 0;
    if (!s_ed.active) return -1;
    s_ed.active = 0;
    t = nfl2k5_eras_team(s_ed.team);
    slot = g_eras_svc->staged_slot(s_ed.side);
    detail[0] = 0;
    if (slot != s_ed.slot) {
        nfl2k5_eras_log("depth chart %s: the staged slot changed under the editor (%08X -> %08X); nothing saved",
                        t->id, s_ed.slot, slot);
    } else {
        count = MEM8(slot + T_ACTIVE);
        if (count > MAXP) count = MAXP;
        for (i = 0; i < count && i < s_ed.count; i++) {
            uint32_t p = player(slot, i);
            if (!p || (p_rank(p) == s_ed.rank[i] && p_side(p) == s_ed.sidev[i])) continue;
            changed++;
            if (o + 60 < sizeof detail) {
                char nm[32];
                wname(MEM32(p + 0x14), nm, sizeof nm);
                o += (size_t)snprintf(detail + o, sizeof detail - o, " %s %s %d/%d->%d/%d;", k_pos[MEM8(p + 0x35) % 17], nm,
                                      s_ed.rank[i], s_ed.sidev[i], p_rank(p), p_side(p));
            }
        }
        for (i = 0; i < 6; i++) special += MEM8(slot + T_SPECIAL + (uint32_t)i) != s_ed.special[i];
        if (changed || special) {
            write_league(t, slot, count, path, sizeof path);
            nfl2k5_eras_log("depth chart %s (%s): %d players and %d special-teams roles changed; saved to %s:%s",
                            t->id, why, changed, special, path[0] ? path : "(not written)", detail);
        } else {
            nfl2k5_eras_log("depth chart %s (%s): no change; %s depth chart kept", t->id, why,
                            s_ed.source == ERAS_DEPTH_LEAGUE ? "the league's" : "the community roster's");
        }
        log_starters("after the edit", t, slot);
    }
    MEM32(DC_TEAM) = s_ed.save_team;
    MEM32(DC_FRANCHISE) = s_ed.save_flag;
    MEM32(DC_PAGE) = s_ed.save_page;
    g_eras_svc->release(s_ed.side);
    nfl2k5_eras_repoint_released(s_ed.side == 0 ? s_ed.slot : 0, s_ed.side == 1 ? s_ed.slot : 0);
    nfl2k5_eras_team_art_update();   /* M9: the era card badges off with the editor */
    return changed + special;
}

/* ---- coaches ------------------------------------------------------------ */

static struct { uint32_t rec, strs, slot, orig; } s_coach[2];

int nfl2k5_eras_team_game_number(int team, int game)
{
    int i, n = 0;
    for (i = 0; i <= game && i < nfl2k5_eras_game_count(); i++) {
        const ErasGame *e = nfl2k5_eras_game(i);
        n += e->home == team || e->away == team;
    }
    return n;
}

static int coach_row(const ErasTeam *t, int game_no, char *first, char *last, size_t n)
{
    char path[MAX_PATH], line[160], *p;
    FILE *f;
    int found = 0;
    snprintf(path, sizeof path, "%s\\coaches.csv", nfl2k5_eras_package_dir());
    for (p = path; *p; p++) if (*p == '/') *p = '\\';
    if (fopen_s(&f, path, "r") || !f) return 0;
    while (!found && fgets(line, sizeof line, f)) {
        char *v[5], *q = line, *e;
        int k = 0;
        while (k < 5) { v[k++] = q; if (!(q = strchr(q, ','))) break; *q++ = 0; }
        if (k != 5 || strcmp(v[0], t->id)) continue;
        for (e = v[4] + strlen(v[4]); e > v[4] && (e[-1] == '\n' || e[-1] == '\r'); ) *--e = 0;
        if (game_no < atoi(v[3]) || game_no > atoi(v[4])) continue;
        snprintf(first, n, "%s", v[1]);
        snprintf(last, n, "%s", v[2]);
        found = 1;
    }
    fclose(f);
    return found;
}

static uint32_t put_w(uint32_t at, const char *s, uint32_t cap)
{
    uint32_t i;
    for (i = 0; i + 1 < cap && s[i]; i++) MEM16(at + 2u * i) = (uint16_t)(unsigned char)s[i];
    MEM16(at + 2u * i) = 0;
    return at;
}

int nfl2k5_eras_coach_apply(int side, int team, int game_no)
{
    static const uint8_t numbers[] = { 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A };
    const ErasTeam *t = nfl2k5_eras_team(team);
    uint32_t slot = g_eras_svc->staged_slot(side), orig, i;
    char first[40], last[40], was[48] = "-", wl[40];
    if (side < 0 || side > 1 || !t || !slot) return -1;
    if (!coach_row(t, game_no, first, last, sizeof first)) {
        nfl2k5_eras_log("coach %s game %d: not in coaches.csv; the template's coach stays", t->id, game_no);
        return -1;
    }
    orig = MEM32(slot + T_COACH);
    if (s_coach[side].slot == slot && orig == s_coach[side].rec) orig = s_coach[side].orig;   /* applied twice */
    if (!orig || (orig & 0x7FFFFFFFu) < 0x10000u || (orig & 0x7FFFFFFFu) >= 0x04000000u) {
        nfl2k5_eras_log("coach %s: the template has no coach record (%08X); not named", t->id, orig);
        return -1;
    }
    if (!s_coach[side].rec) {
        s_coach[side].rec = xbox_HeapAlloc(COACH_SIZE, 16);
        s_coach[side].strs = xbox_HeapAlloc(3u * 64u, 4);
        if (!s_coach[side].rec || !s_coach[side].strs) { s_coach[side].rec = 0; return -1; }
    }
    wname(MEM32(orig + 0x00), was, 20);
    wname(MEM32(orig + 0x04), wl, sizeof wl);
    snprintf(was + strlen(was), sizeof was - strlen(was), " %s", wl);
    for (i = 0; i < COACH_SIZE; i += 4) MEM32(s_coach[side].rec + i) = MEM32(orig + i);
    MEM32(s_coach[side].rec + 0x00) = put_w(s_coach[side].strs, first, 32);
    MEM32(s_coach[side].rec + 0x04) = put_w(s_coach[side].strs + 64u, last, 32);
    MEM16(s_coach[side].strs + 128u) = 0;
    for (i = 0x08; i <= 0x10; i += 4) MEM32(s_coach[side].rec + i) = s_coach[side].strs + 128u;   /* no biography */
    for (i = 0; i < sizeof numbers; i++) MEM16(s_coach[side].rec + numbers[i]) = 0;
    s_coach[side].slot = slot;
    s_coach[side].orig = orig;
    MEM32(slot + T_COACH) = s_coach[side].rec;
    nfl2k5_eras_log("coach %s (side %d, game %d): %s %s (the template's was %s, record %08X; ours %08X)",
                    t->id, side, game_no, first, last, was, orig, s_coach[side].rec);
    return 0;
}

void nfl2k5_eras_coach_restore(int side)
{
    if (side < 0 || side > 1 || !s_coach[side].slot) return;
    if (MEM32(s_coach[side].slot + T_COACH) == s_coach[side].rec) MEM32(s_coach[side].slot + T_COACH) = s_coach[side].orig;
    s_coach[side].slot = 0;
}

/*
 * Every team in Team Select (2026-10-09).
 *
 * The roster keeps 52 resident teams (the 32 clubs, two Create a Team slots,
 * the all-star and alumni sides). The game also ships 75 historic teams (the
 * '85 Bears, the '72 Dolphins...) as one-team roster files, reachable in
 * retail only through Create a Team's "Load Historic Team" (0x2D17B0).
 *
 * Here Quick Game, Practice and First Person Football Team Select (flows 1, 3
 * and 7 at [0xACF614]) step past the last resident team into an outside list
 * -- the historic teams, then any more teams added later -- and back to the
 * first team after the last one, both ways. Stepping onto an entry loads it
 * into that side's spare slot (home: USER1, away: USER2) with the game's own
 * loader calls, as 0x2D17B0 does: name "h-%s-%d-%s-%d.iff", queue 0x43F50 in
 * context "historic", wait 0x432D0, lookup 0x449E0 (ROST), clear the slot
 * 0xC2300, import 0xC1030, category 4, playbook label of the club with the
 * same asset code, pop 0x432F0. A slot holding a user-created team is never
 * touched (that side just skips the outside list); a Live or system-link
 * session keeps the retail list. Nothing is added to the roster or the saves:
 * a game only ever needs the two spare slots.
 *
 * Design after 2K5 Mod Studio's "historic teams in Quick Game" (its assembly
 * tools/nfl2k5_historic_teams_quick_game.S, never run there), ported to C on
 * generated-code patches (tools/apply-gen-patches.py TEAM_SELECT_*).
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_teams.h"

#define ROOT        0xB72918u   /* roster root pointer */
#define TS_MODE     0xACF614u   /* Team Select flow: 1 Quick Game, 3 Practice, 7 First Person Football */
#define TS_SIDE     0xACF63Cu   /* the home side's team; away at +4 */
#define R_TEAMS     0x18u
#define R_TABLE     0x1Cu
#define R_HCOUNT    0x58u
#define R_HTABLE    0x5Cu
#define T_CODE      0x10Cu      /* team: asset code text */
#define T_LABEL     0x110u      /* team: label (names the playbook) */
#define T_IDENT     0x118u
#define T_ACTIVE    0x11Cu      /* team: player count */
#define T_CAT       0x128u      /* team: category (2 created, 4 historic) */
#define TEAM_SIZE   500u
#define CTX         0xE9F9A8u   /* L"historic" */
#define ROST        0x54534F52u

static struct { uint32_t slot; uint16_t entry1, ident; } s_side[2];
static int s_log = -1;

static uint32_t root(void) { return MEM32(ROOT); }

/* A call into the game: registers in, return address pushed, eax out. Any
 * stack arguments are pushed by the caller first, exactly as the game does. */
static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, 0x0BADC0DEu);
    fn();
    return g_eax;
}

/* ---- the outside list -------------------------------------------------- */

/* Added teams: mods\teams\<name>\team.rost, a one-team roster in the format
 * of the historic files (made by the team creator, tools/team-creator). */
#define MAX_CUSTOM 512
static char s_custom[MAX_CUSTOM][MAX_PATH];
static int s_custom_n = -1;

static void custom_scan(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    s_custom_n = 0;
    h = FindFirstFileA("mods\\teams\\*", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char p[MAX_PATH];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        snprintf(p, sizeof p, "mods\\teams\\%s\\team.rost", fd.cFileName);
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES || s_custom_n >= MAX_CUSTOM) continue;
        strcpy(s_custom[s_custom_n++], p);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    fprintf(stderr, "[TEAMS] %d added teams in mods\\teams\n", s_custom_n);
}

static int historic_count(void)
{
    uint32_t r = root();
    return r ? (int)MEM32(r + R_HCOUNT) : 0;
}

static int outside_count(void)
{
    if (s_custom_n < 0) custom_scan();
    return historic_count() + s_custom_n;
}

/* ---- slots ------------------------------------------------------------- */

/* The n-th team (table order) in category 2 or 4: USER1, USER2. */
static uint32_t spare_slot(int n)
{
    uint32_t r = root(), t = MEM32(r + R_TABLE), i, cnt = MEM32(r + R_TEAMS);
    for (i = 0; i < cnt; i++, t += TEAM_SIZE) {
        uint32_t c = MEM32(t + T_CAT);
        if (c == 2 || c == 4) { if (n == 0) return t; n--; }
    }
    return 0;
}

/* The slot may be cleared and loaded: not the other side's team, and empty or historic. */
static int slot_usable(int side, uint32_t slot)
{
    uint32_t c;
    if (!slot || slot == MEM32(TS_SIDE + 4u * (uint32_t)(1 - side))) return 0;
    c = MEM32(slot + T_CAT);
    if (c == 2 && MEM8(slot + T_ACTIVE) != 0) return 0;   /* a user-created team */
    if (c != 2 && c != 4) return 0;
    return outside_count() > 0;
}

static int current_entry(int side, uint32_t slot)
{
    if (s_side[side].slot != slot || !s_side[side].entry1 || s_side[side].ident != MEM16(slot + T_IDENT))
        return -1;
    return s_side[side].entry1 - 1 < outside_count() ? s_side[side].entry1 - 1 : -1;
}

static void remember(int side, uint32_t slot, int entry)
{
    s_side[side].slot = slot;
    s_side[side].entry1 = (uint16_t)(entry + 1);
    s_side[side].ident = MEM16(slot + T_IDENT);
}

/* Back to an empty created-team slot (category 2, identity 90/91). */
static void release_slot(int side, uint32_t slot)
{
    if (!slot) return;
    gcall(sub_000C2300, slot, 0);                    /* its players go back to the spare pool */
    MEM8(slot + T_ACTIVE) = 0;
    MEM32(slot + T_CAT) = 2;
    MEM16(slot + T_IDENT) = (uint16_t)(0x5A + side);
}

/* ---- loading a historic team ------------------------------------------ */

static void wput(uint32_t va, const uint16_t *s)
{
    while (*s) { MEM16(va) = *s++; va += 2; }
    MEM16(va) = 0;
}

static size_t wcat_guest(uint16_t *out, size_t at, size_t cap, uint32_t va)
{
    while (va && MEM16(va) && at + 1 < cap) { out[at++] = MEM16(va); va += 2; }
    return at;
}

static size_t wcat_num(uint16_t *out, size_t at, size_t cap, unsigned v)
{
    char b[16];
    int i;
    snprintf(b, sizeof b, "%u", v);
    for (i = 0; b[i] && at + 1 < cap; i++) out[at++] = (uint16_t)b[i];
    return at;
}

/* Entry k of the roster's historic list into `slot`, as 0x2D17B0 does. */
static int load_historic(uint32_t slot, int k, int side)
{
    uint32_t r = root(), e = MEM32(r + R_HTABLE) + (uint32_t)k * 16u;
    uint32_t save_esp = g_esp, save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp;
    uint32_t frame, name, buf2, res, i;
    uint16_t fn[96];
    size_t n = 0;
    int ok = 0;
    /* "h-<code>-<year>-<franchise>-<throwback>.iff" */
    fn[n++] = 'h'; fn[n++] = '-';
    n = wcat_guest(fn, n, 90, e + 4);
    fn[n++] = '-';
    n = wcat_num(fn, n, 90, MEM16(e));
    fn[n++] = '-';
    n = wcat_guest(fn, n, 90, MEM32(e + 12));
    fn[n++] = '-';
    n = wcat_num(fn, n, 90, MEM8(e + 2));
    { static const char ext[] = ".iff"; for (i = 0; ext[i]; i++) fn[n++] = (uint16_t)ext[i]; }
    fn[n] = 0;

    g_esp -= 0x100;                                  /* a frame of our own on the guest stack */
    frame = g_esp;
    name = frame + 0x20;                             /* the file name, as 0x2D17B0's frame +0x20 */
    buf2 = frame + 0xA0;
    wput(name, fn);
    if (s_log) {
        char a[96];
        for (i = 0; fn[i] && i < 95; i++) a[i] = (char)fn[i];
        a[i] = 0;
        fprintf(stderr, "[TEAMS] side %d loads historic team %d: %s\n", side, k, a);
    }
    /* queue: two zero arguments, the loader heap, the second buffer; ecx context, edx name */
    PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    {
        uint32_t heap = gcall(sub_00038FB0, 0, 0);
        PUSH32(g_esp, heap);
        PUSH32(g_esp, buf2);
        gcall(sub_00043F50, CTX, name);
    }
    gcall(sub_000432D0, 0, 0);
    PUSH32(g_esp, CTX);
    res = gcall(sub_000449E0, CTX, ROST);
    if (res && getenv("NFL2K5_TEAMS_DUMP")) {          /* the resource as loaded, for study */
        char fname[64]; FILE *df;
        snprintf(fname, sizeof fname, "logs\\historic-%02d.rost", k);
        if ((df = fopen(fname, "wb")) != NULL) {
            fwrite((const uint8_t *)xbox_GetMemoryOffset() + res, 1, 0x8000, df);
            fclose(df);
        }
    }
    if (res) {
        gcall(sub_000C2300, slot, 0);                /* clear first: players back to the spare pool */
        if (gcall(sub_000C1030, res, slot)) {
            uint32_t t = MEM32(r + R_TABLE), cnt = MEM32(r + R_TEAMS);
            MEM32(slot + T_CAT) = 4;
            /* the playbook label of the club with the same asset code */
            for (i = 0; i < cnt; i++, t += TEAM_SIZE) {
                uint32_t a = MEM32(t + T_CODE), b = e + 4, j;
                if (!a || t == slot) continue;
                for (j = 0; j < 8 && MEM16(a + 2 * j) == MEM16(b + 2 * j) && MEM16(a + 2 * j); j++) {}
                if (j < 8 && MEM16(a + 2 * j) == 0 && MEM16(b + 2 * j) == 0) { MEM32(slot + T_LABEL) = MEM32(t + T_LABEL); break; }
            }
            ok = 1;
        } else {
            MEM8(slot + T_ACTIVE) = 0;
            MEM32(slot + T_CAT) = 2;
            MEM16(slot + T_IDENT) = (uint16_t)(0x5A + side);
        }
    } else if (s_log)
        fprintf(stderr, "[TEAMS] historic team %d: file not found\n", k);
    gcall(sub_000432F0, CTX, 0);
    g_esp = save_esp; g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    return ok;
}

/* The label (playbook) of the club with the team's asset code. */
static void copy_label(uint32_t slot)
{
    uint32_t r = root(), t = MEM32(r + R_TABLE), cnt = MEM32(r + R_TEAMS), i, b = MEM32(slot + T_CODE), j;
    if (!b) return;
    for (i = 0; i < cnt; i++, t += TEAM_SIZE) {
        uint32_t a = MEM32(t + T_CODE);
        if (!a || t == slot || a == b) continue;
        for (j = 0; j < 8 && MEM16(a + 2 * j) == MEM16(b + 2 * j) && MEM16(a + 2 * j); j++) {}
        if (j < 8 && MEM16(a + 2 * j) == 0 && MEM16(b + 2 * j) == 0) { MEM32(slot + T_LABEL) = MEM32(t + T_LABEL); return; }
    }
}

/* An added team: its file into guest memory, then the game's import (0xC1030). */
static int load_custom(uint32_t slot, int idx, int side)
{
    extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
    static uint32_t buf;
    enum { CAP = 0x40000 };
    uint32_t save_esp = g_esp, save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi;
    FILE *f = fopen(s_custom[idx], "rb");
    size_t n;
    int ok;
    if (!f) return 0;
    if (!buf) buf = xbox_HeapAlloc(CAP, 64);
    if (!buf) { fclose(f); return 0; }
    memset((uint8_t *)xbox_GetMemoryOffset() + buf, 0, CAP);
    n = fread((uint8_t *)xbox_GetMemoryOffset() + buf + 0x40, 1, CAP - 0x40, f);
    fclose(f);
    if (n < 0x100) return 0;
    if (s_log) fprintf(stderr, "[TEAMS] side %d loads added team %s (%zu bytes)\n", side, s_custom[idx], n);
    gcall(sub_000C2300, slot, 0);                    /* the slot's players back to the spare pool */
    ok = gcall(sub_000C1030, buf + 0x40, slot) != 0;
    if (ok) {
        extern void nfl2k5_team_identity(const char *rost_path, uint32_t slot);
        MEM32(slot + T_CAT) = 4;
        copy_label(slot);
        nfl2k5_team_identity(s_custom[idx], slot);   /* code, names from team.json */
        {
            extern void nfl2k5_team_roster(const char *rost_path, uint32_t slot);
            nfl2k5_team_roster(s_custom[idx], slot); /* players from roster.csv */
        }
    } else {
        MEM8(slot + T_ACTIVE) = 0;
        MEM32(slot + T_CAT) = 2;
        MEM16(slot + T_IDENT) = (uint16_t)(0x5A + side);
    }
    g_esp = save_esp; g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi;
    return ok;
}

static int load_entry(uint32_t slot, int k, int side)
{
    if (k < historic_count()) return load_historic(slot, k, side);
    if (k - historic_count() < s_custom_n) return load_custom(slot, k - historic_count(), side);
    return 0;
}

/* ---- stepping ---------------------------------------------------------- */

/* The retail iterator (0x778C0 / 0x77950), skipping teams in the historic category. */
static uint32_t resident_step(uint32_t team, int dir, uint32_t mask)
{
    int lap = (int)MEM32(root() + R_TEAMS) + 1;
    do {
        team = gcall(dir < 0 ? sub_00077950 : sub_000778C0, team, mask);
        if (!team || MEM32(team + T_CAT) != 4) break;
    } while (--lap > 0);
    return team;
}

static uint32_t publish(int side, uint32_t team)
{
    return gcall(side ? sub_002C17C0 : sub_002C1780, team, 0);
}

static int local_flow(void)
{
    uint32_t m = MEM32(TS_MODE);
    return (m == 1 || m == 3 || m == 7) && gcall(sub_00128C70, 0, 0) == 0;
}

/* ---- study: every historic team, as the game loads it ------------------
 *
 * NFL2K5_HISTORIC_INVENTORY=1 (My NFL Eras, phase 1): the first Team Select
 * step loads each historic entry into this side's spare slot with the game's
 * own loader and writes the team and its full roster to
 * logs\historic_inventory.jsonl (one team a line), then empties the slot.
 * Field offsets: 2K Football Mod Tools' nfl2k5_roster_records (team record,
 * 0x54 player record). Raw values only; build/eras/historic_inventory.py
 * names positions and ratings. */
static uint32_t inv_ptr(uint32_t field)
{
    /* loaded rosters hold absolute pointers; a raw ROST body holds relative ones */
    uint32_t p = MEM32(field);
    if (!p) return 0;
    if ((p >= 0x10000u && p < 0x04000000u) || (p >= 0x80000000u && p < 0x88000000u)) return p;
    return field + p;
}

static void inv_wstr(FILE *f, uint32_t va)
{
    int i;
    fputc('"', f);
    for (i = 0; va && i < 64 && MEM16(va + 2u * (uint32_t)i); i++) {
        unsigned c = MEM16(va + 2u * (uint32_t)i);
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c >= 0x20 && c < 0x7F) fputc((int)c, f);
        else fprintf(f, "\\u%04x", c);
    }
    fputc('"', f);
}

static void historic_inventory(uint32_t slot, int side)
{
    FILE *f = fopen("logs\\historic_inventory.jsonl", "w");
    uint32_t r = root(), n = (uint32_t)historic_count(), k, i;
    if (!f) return;
    fprintf(stderr, "[TEAMS] historic inventory: %u entries%c", n, 10);
    for (k = 0; k < n; k++) {
        uint32_t e = MEM32(r + R_HTABLE) + k * 16u, cnt;
        int ok = load_historic(slot, (int)k, side);
        fprintf(f, "{\"entry\":%u,\"year\":%u,\"variant\":%u,\"code\":", k, MEM16(e), MEM8(e + 2));
        inv_wstr(f, e + 4);
        fprintf(f, ",\"franchise\":");
        inv_wstr(f, MEM32(e + 12));
        fprintf(f, ",\"loaded\":%s", ok ? "true" : "false");
        if (!ok) { fprintf(f, "}\n"); continue; }
        fprintf(f, ",\"nickname\":"); inv_wstr(f, inv_ptr(slot + 0x104));
        fprintf(f, ",\"abbreviation\":"); inv_wstr(f, inv_ptr(slot + 0x108));
        fprintf(f, ",\"asset_code\":"); inv_wstr(f, inv_ptr(slot + T_CODE));
        fprintf(f, ",\"city\":"); inv_wstr(f, inv_ptr(slot + 0x138));
        fprintf(f, ",\"scheme\":%u,\"coach\":%u,\"slot\":%u,\"team_rec\":\"", MEM16(slot + 0x150), inv_ptr(slot + 0x14C), slot);
        for (i = 0; i < 0x160; i++) fprintf(f, "%02x", MEM8(slot + i));
        fprintf(f, "\"");
        cnt = MEM8(slot + T_ACTIVE);
        fprintf(f, ",\"players\":[");
        for (i = 0; i < cnt && i < 65; i++) {
            uint32_t p = inv_ptr(slot + 4u * i), j;
            if (!p) continue;
            fprintf(f, "%s{\"first\":", i ? "," : "");
            inv_wstr(f, inv_ptr(p + 0x10));
            fprintf(f, ",\"last\":");
            inv_wstr(f, inv_ptr(p + 0x14));
            fprintf(f, ",\"rec\":\"");
            for (j = 0; j < 0x54; j++) fprintf(f, "%02x", MEM8(p + j));
            fprintf(f, "\"}");
        }
        fprintf(f, "]}\n");
    }
    fclose(f);
    release_slot(side, slot);
    s_side[side].entry1 = 0;
    fprintf(stderr, "[TEAMS] historic inventory written to logs\\historic_inventory.jsonl%c", 10);
}

/* ---- My NFL Eras: staging a league team (docs/eras/PHASE2-PLAN.md M2) ----
 *
 * A franchise of the league is a disc historic team (its template: uniform,
 * asset code, 53 player records) loaded into the side's spare slot with the
 * loader above, then given the league's city, nickname and abbreviation and
 * the players of the runtime package's roster CSV (nfl2k5_team_roster_file).
 * The strings live in a per-side arena (nfl2k5_team_files.c) that every
 * staging rewrites. The match copies the slot twice (MATCH-LAUNCH 1.4), so a
 * staged slot stays loaded until End Of Game has been left. */
typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, seh_ebp;
    uint32_t rf_kind, rf_a, rf_b, rf_cf;
    int32_t rf_as, rf_bs;
    int df;
} GuestRegs;

static void regs_save(GuestRegs *r)
{
    r->eax = g_eax; r->ebx = g_ebx; r->ecx = g_ecx; r->edx = g_edx;
    r->esi = g_esi; r->edi = g_edi; r->ebp = g_ebp; r->esp = g_esp; r->seh_ebp = g_seh_ebp;
    r->rf_kind = g_rf_kind; r->rf_a = g_rf_a; r->rf_b = g_rf_b; r->rf_cf = g_rf_cf;
    r->rf_as = g_rf_as; r->rf_bs = g_rf_bs; r->df = g_df;
}

static void regs_restore(const GuestRegs *r)
{
    g_eax = r->eax; g_ebx = r->ebx; g_ecx = r->ecx; g_edx = r->edx;
    g_esi = r->esi; g_edi = r->edi; g_ebp = r->ebp; g_esp = r->esp; g_seh_ebp = r->seh_ebp;
    g_rf_kind = r->rf_kind; g_rf_a = r->rf_a; g_rf_b = r->rf_b; g_rf_cf = r->rf_cf;
    g_rf_as = r->rf_as; g_rf_bs = r->rf_bs; g_df = r->df;
}

static struct { uint32_t slot; int staged, rows, entry; } s_stage[2];

static const char *const k_pos[17] = {
    "QB", "K", "P", "WR", "CB", "FS", "SS", "RB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE"
};

static void ascii_name(uint32_t p, char *out, size_t n)
{
    size_t o = 0;
    int k;
    for (k = 0; k < 2; k++) {
        uint32_t va = MEM32(p + (k ? 0x14u : 0x10u)), i;
        if (k && o + 1 < n) out[o++] = ' ';
        for (i = 0; va && i < 40 && MEM16(va + 2 * i) && o + 1 < n; i++) {
            uint16_t c = MEM16(va + 2 * i);
            out[o++] = c >= 0x20 && c < 0x7F ? (char)c : '?';
        }
    }
    out[o] = 0;
}

/* Special-teams roles, team +0x194..+0x199: player indices for holder, KR1,
 * KR2, K, LS, PR (field meanings from the mod tools' SPECIAL_TEAM_OFFSETS,
 * UNVERIFIED; 255 = none). They come with the template and index ITS players,
 * so after the roster CSV has rewritten positions a role can point at anyone.
 * A role whose player has a position that cannot fill it is set again:
 * holder the first P (else the second QB), K the first K (else P), LS the
 * last C, KR1/KR2/PR the fastest backups (not the first of their position)
 * at a returner position. NFL2K5_ERAS_ST_FIX=0 only logs. */
static int st_eligible(int role, int pos)
{
    switch (role) {
    case 0: return pos == 0 || pos == 1 || pos == 2;                         /* holder */
    case 1: case 2: return pos >= 3 && pos <= 8;                              /* KR: WR CB FS SS RB FB */
    case 3: return pos == 1;                                                  /* K */
    case 4: return pos == 12 || pos == 13 || pos == 14 || pos == 9 || pos == 10 || pos == 11 || pos == 15 || pos == 16;
    default: return pos >= 3 && pos <= 7;                                     /* PR: WR CB FS SS RB */
    }
}

static void special_teams(uint32_t slot, int side)
{
    static const char *const role_name[6] = { "holder", "KR1", "KR2", "K", "LS", "PR" };
    int count = MEM8(slot + T_ACTIVE), rank[71], pos[71], seen[17], i, role, fix;
    const char *e = getenv("NFL2K5_ERAS_ST_FIX");
    fix = !(e && *e == '0');
    if (count > 71) count = 71;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < count; i++) {
        uint32_t p = MEM32(slot + 4u * (uint32_t)i);
        pos[i] = p ? MEM8(p + 0x35) : -1;
        rank[i] = pos[i] >= 0 && pos[i] <= 16 ? seen[pos[i]]++ : 99;
    }
    for (role = 0; role < 6; role++) {
        int cur = MEM8(slot + 0x194u + (uint32_t)role), pick = -1;
        char a[64] = "-", b[64] = "-";
        if (cur < count && pos[cur] >= 0 && st_eligible(role, pos[cur])) {
            if (s_log) {
                ascii_name(MEM32(slot + 4u * (uint32_t)cur), a, sizeof a);
                fprintf(stderr, "[TEAMS] side %d special teams %s: player %d %s %s (ok)\n",
                        side, role_name[role], cur, k_pos[pos[cur]], a);
            }
            continue;
        }
        if (role == 0) {
            for (i = 0; i < count && pick < 0; i++) if (pos[i] == 2) pick = i;
            for (i = 0; i < count && pick < 0; i++) if (pos[i] == 0 && rank[i] == 1) pick = i;
        } else if (role == 3) {
            for (i = 0; i < count && pick < 0; i++) if (pos[i] == 1) pick = i;
            for (i = 0; i < count && pick < 0; i++) if (pos[i] == 2) pick = i;
        } else if (role == 4) {
            for (i = 0; i < count; i++) if (pos[i] == 12) pick = i;
        } else {
            int best = -1, other = role == 2 ? MEM8(slot + 0x195) : -1;
            for (i = 0; i < count; i++) {
                uint32_t p = MEM32(slot + 4u * (uint32_t)i);
                if (pos[i] < 0 || !st_eligible(role, pos[i]) || rank[i] == 0 || i == other) continue;
                if (MEM8(p + 0x36) > best) { best = MEM8(p + 0x36); pick = i; }
            }
        }
        if (cur < count) ascii_name(MEM32(slot + 4u * (uint32_t)cur), a, sizeof a);
        if (pick >= 0) ascii_name(MEM32(slot + 4u * (uint32_t)pick), b, sizeof b);
        fprintf(stderr, "[TEAMS] side %d special teams %s: player %d %s %s cannot fill it -> %s%d %s %s\n",
                side, role_name[role], cur, cur < count && pos[cur] >= 0 && pos[cur] <= 16 ? k_pos[pos[cur]] : "none", a,
                fix ? "" : "(not fixed) ", pick, pick >= 0 ? k_pos[pos[pick]] : "none", b);
        if (fix && pick >= 0) MEM8(slot + 0x194u + (uint32_t)role) = (uint8_t)pick;
    }
}

/* ---- M8: real roster sizes (42..53) -------------------------------------
 *
 * The roster CSV's data rows, counted the way nfl2k5_team_roster_file reads
 * them (the header and short lines skipped). */
static int csv_data_rows(const char *path)
{
    char line[1024];
    FILE *f = fopen(path, "r");
    int n = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        int commas = 0;
        const char *c;
        if (!isdigit((unsigned char)line[0]) && strncmp(line, "first,", 6) == 0) continue;
        for (c = line; *c; c++) commas += *c == ',';
        if (commas + 1 >= 10 + 28) n++;
    }
    fclose(f);
    return n;
}

/* The template's players past `keep` leave the team, last first: the game's
 * own remove-from-team sub_000C3A90 (eax = team, ecx = index: count - 1, the
 * pointer list closed up, a special-teams role on that player set to 255 and
 * the ones above it moved down), then sub_000BFF90 (record +8 &= 0x7B), which
 * gives the record back to the spare pool exactly as the slot clear 0xC2300
 * does for every player. */
static int trim_roster(uint32_t slot, int keep)
{
    int i, n = MEM8(slot + T_ACTIVE), gone = 0;
    for (i = n - 1; i >= keep; i--) {
        uint32_t p = MEM32(slot + 4u * (uint32_t)i);
        g_eax = slot;
        gcall(sub_000C3A90, (uint32_t)i, 0);
        if (p) gcall(sub_000BFF90, p, 0);
        gone++;
    }
    return gone;
}

/* What a staged team must have at each position code (0 QB .. 16 DE) so the
 * game can line up its base offense, base defense and the kicking game:
 * QB, HB, 2 WR, TE, 2 T, 2 G, C; 2 DE, DT, 2 OLB, ILB, 2 CB, FS; K.
 * Neither P nor SS is required: M8 test m8b played a full game with no P and
 * no C on either side and no SS (the K punted). The 1980 Cowboys, Patriots
 * and Rams have no rated P, the Bengals no SS. The package builder asks for more
 * (roster_membership.py MIN_POS) where real players exist. */
static const uint8_t k_stage_min[17] = {
    /* QB K  P  WR CB FS SS RB FB TE OLB ILB C  G  T  DT DE */
       1, 1, 0, 2, 2, 1, 0, 1, 0, 1, 2,  1,  1, 2, 2, 1, 2
};

static int position_short(uint32_t slot, char *why, size_t n)
{
    int have[17], i, count = MEM8(slot + T_ACTIVE), bad = 0;
    size_t o = 0;
    memset(have, 0, sizeof have);
    for (i = 0; i < count; i++) {
        uint32_t p = MEM32(slot + 4u * (uint32_t)i);
        int pos = p ? MEM8(p + 0x35) : -1;
        if (pos >= 0 && pos <= 16) have[pos]++;
    }
    why[0] = 0;
    for (i = 0; i < 17; i++)
        if (have[i] < k_stage_min[i]) {
            bad++;
            if (o + 16 < n) o += (size_t)snprintf(why + o, n - o, " %s %d/%d", k_pos[i], have[i], k_stage_min[i]);
        }
    return bad;
}

const char *nfl2k5_teams_stage_message(int code)
{
    switch (code) {
    case NFL2K5_STAGE_OK: return "Teams ready.";
    case NFL2K5_STAGE_NO_ROSTER: return "The game's rosters are not loaded yet.";
    case NFL2K5_STAGE_NO_SLOT: return "No free team slot (USER1/USER2) is available.";
    case NFL2K5_STAGE_USER_TEAM: return "A Create-a-Team team is in USER1/USER2. Delete or move it to play My NFL Eras games.";
    case NFL2K5_STAGE_TEMPLATE: return "The team's template (a historic team) could not be loaded.";
    case NFL2K5_STAGE_ROSTER_CSV: return "The team's roster file is missing or incomplete.";
    default: return "The team could not be prepared.";
    }
}

uint32_t nfl2k5_teams_staged_slot(int side)
{
    return side >= 0 && side < 2 && s_stage[side].staged ? s_stage[side].slot : 0;
}

int nfl2k5_teams_stage(int side, int template_entry, const char *roster_csv, const char *city,
                       const char *nickname, const char *abbr)
{
    GuestRegs saved;
    uint32_t slot, cat, arena;
    int rows, want, template_n, gone, rc = NFL2K5_STAGE_OK;
    char why[160];
    if (s_log < 0) s_log = getenv("NFL2K5_TEAMS_LOG") != NULL;
    if (side < 0 || side > 1 || !roster_csv) return NFL2K5_STAGE_BAD_ARGS;
    if (!root()) return NFL2K5_STAGE_NO_ROSTER;
    slot = spare_slot(side);
    if (!slot) return NFL2K5_STAGE_NO_SLOT;
    cat = MEM32(slot + T_CAT);
    if (cat == 2 && MEM8(slot + T_ACTIVE) != 0) {    /* a user-created team: never touched */
        fprintf(stderr, "[TEAMS] stage side %d refused: slot %08X holds a user-created team (%u players)\n",
                side, slot, MEM8(slot + T_ACTIVE));
        return NFL2K5_STAGE_USER_TEAM;
    }
    if (cat != 2 && cat != 4) return NFL2K5_STAGE_NO_SLOT;
    if (template_entry < 0 || template_entry >= historic_count()) return NFL2K5_STAGE_TEMPLATE;
    if (GetFileAttributesA(roster_csv) == INVALID_FILE_ATTRIBUTES) return NFL2K5_STAGE_ROSTER_CSV;
    if (!(arena = nfl2k5_team_arena(side))) return NFL2K5_STAGE_NO_SLOT;
    if (cat == 4)   /* a historic or added team Team Select loaded: replaced, not kept */
        fprintf(stderr, "[TEAMS] stage side %d: slot %08X held a historic or added team (ident %u, %u players); it is replaced\n",
                side, slot, MEM16(slot + T_IDENT), MEM8(slot + T_ACTIVE));
    regs_save(&saved);
    s_stage[side].staged = 0;
    s_side[side].entry1 = 0;                         /* not an entry of Team Select's outside list */
    if (!load_historic(slot, template_entry, side) || MEM32(slot + T_CAT) != 4) {
        /* every failure leaves an empty created slot, as the header says: a
         * slot still holding an earlier historic team is cleared too */
        if (MEM32(slot + T_CAT) == 4) release_slot(side, slot);
        rc = NFL2K5_STAGE_TEMPLATE;
        goto out;
    }
    /* identity: the same fields as nfl2k5_team_identity */
    if (city) MEM32(slot + 0x138) = nfl2k5_team_arena_str(NFL2K5_ARENA_IDENT(arena, 0), city);
    if (nickname) MEM32(slot + 0x104) = nfl2k5_team_arena_str(NFL2K5_ARENA_IDENT(arena, 1), nickname);
    if (abbr) MEM32(slot + 0x108) = nfl2k5_team_arena_str(NFL2K5_ARENA_IDENT(arena, 2), abbr);
    /* M8: the CSV's own row count, 42 up to the template's 53 */
    template_n = MEM8(slot + T_ACTIVE);
    want = csv_data_rows(roster_csv);
    if (want < NFL2K5_ROSTER_MIN || want > template_n) {
        fprintf(stderr, "[TEAMS] stage side %d: %s has %d rows; a team takes %d..%d; slot released\n",
                side, roster_csv, want, NFL2K5_ROSTER_MIN, template_n);
        release_slot(side, slot);
        rc = NFL2K5_STAGE_ROSTER_CSV;
        goto out;
    }
    rows = nfl2k5_team_roster_file(roster_csv, slot, arena);
    if (rows != want) {
        fprintf(stderr, "[TEAMS] stage side %d: %s gave %d of its %d rows for %u players; slot released\n",
                side, roster_csv, rows, want, MEM8(slot + T_ACTIVE));
        release_slot(side, slot);
        rc = NFL2K5_STAGE_ROSTER_CSV;
        goto out;
    }
    gone = rows < template_n ? trim_roster(slot, rows) : 0;
    if ((int)MEM8(slot + T_ACTIVE) != rows) {
        fprintf(stderr, "[TEAMS] stage side %d: %d players left after removing %d (want %d); slot released\n",
                side, MEM8(slot + T_ACTIVE), gone, rows);
        release_slot(side, slot);
        rc = NFL2K5_STAGE_ROSTER_CSV;
        goto out;
    }
    if (position_short(slot, why, sizeof why)) {
        fprintf(stderr, "[TEAMS] stage side %d: %s is short at:%s; slot released\n", side, roster_csv, why);
        release_slot(side, slot);
        rc = NFL2K5_STAGE_ROSTER_CSV;
        goto out;
    }
    if (gone)
        fprintf(stderr, "[TEAMS] stage side %d: %d CSV rows written over the template's first %d of %d players; "
                "the other %d removed (game remove-from-team, records back to the pool)\n",
                side, rows, rows, template_n, gone);
    {   /* test only (M8 experiments): every player at the listed positions ("P,C") leaves the team */
        const char *d = getenv("NFL2K5_ERAS_TEST_DROP");
        if (d && *d) {
            int pos, i, n = 0;
            for (pos = 0; pos < 17; pos++) {
                const char *t = d;
                size_t len = strlen(k_pos[pos]);
                int hit = 0;
                while (*t) {
                    if (!_strnicmp(t, k_pos[pos], len) && (t[len] == ',' || !t[len])) hit = 1;
                    t = strchr(t, ',');
                    if (!t) break;
                    t++;
                }
                for (i = MEM8(slot + T_ACTIVE) - 1; hit && i >= 0; i--) {
                    uint32_t p = MEM32(slot + 4u * (uint32_t)i);
                    if (!p || MEM8(p + 0x35) != pos) continue;
                    g_eax = slot;
                    gcall(sub_000C3A90, (uint32_t)i, 0);
                    gcall(sub_000BFF90, p, 0);
                    n++;
                }
            }
            rows = MEM8(slot + T_ACTIVE);
            fprintf(stderr, "[TEAMS] TEST NFL2K5_ERAS_TEST_DROP=%s: side %d lost %d players at those positions, %d left "
                    "(identity rows past the first removed one no longer line up)\n", d, side, n, rows);
        }
    }
    special_teams(slot, side);
    s_stage[side].slot = slot;
    s_stage[side].staged = 1;
    s_stage[side].rows = rows;
    s_stage[side].entry = template_entry;
    fprintf(stderr, "[TEAMS] staged side %d: historic entry %d -> slot %08X (ident %u), %s %s (%s), %d rows written from %s, arena %08X\n",
            side, template_entry, slot, MEM16(slot + T_IDENT), city ? city : "-", nickname ? nickname : "-",
            abbr ? abbr : "-", rows, roster_csv, arena);
out:
    regs_restore(&saved);
    return rc;
}

uint32_t nfl2k5_teams_spare_slot(int side)
{
    return side >= 0 && side < 2 && root() ? spare_slot(side) : 0;
}

/* The n-th club of the roster table: a team with players, an identity below
 * 32 (the 32 clubs; the spare slots are 90/91) and neither a spare slot nor a
 * historic team. For a selected-team word left pointing at a released slot. */
uint32_t nfl2k5_teams_resident(int n)
{
    uint32_t r = root(), t, i, cnt;
    if (!r) return 0;
    t = MEM32(r + R_TABLE);
    cnt = MEM32(r + R_TEAMS);
    for (i = 0; i < cnt; i++, t += TEAM_SIZE) {
        uint32_t c = MEM32(t + T_CAT);
        if (c == 2 || c == 4 || MEM8(t + T_ACTIVE) == 0 || MEM16(t + T_IDENT) >= 32) continue;
        if (n-- == 0) return t;
    }
    return 0;
}

int nfl2k5_teams_stage_check(int side)
{
    uint32_t slot, cat;
    if (side < 0 || side > 1) return NFL2K5_STAGE_BAD_ARGS;
    if (!root()) return NFL2K5_STAGE_NO_ROSTER;
    if (!(slot = spare_slot(side))) return NFL2K5_STAGE_NO_SLOT;
    cat = MEM32(slot + T_CAT);
    if (cat == 2 && MEM8(slot + T_ACTIVE) != 0) return NFL2K5_STAGE_USER_TEAM;
    return cat == 2 || cat == 4 ? NFL2K5_STAGE_OK : NFL2K5_STAGE_NO_SLOT;
}

/* Stadium records: [root+0x10] of them, 0x80 bytes each from [root+0x14],
 * name string pointer at +0x00 (the mod tools' ROST_STRING_FIELDS), id byte
 * at +0x7C (the Create a Team picker, 0x3191B0). Names compare without case
 * and without trailing spaces ("M&T Bank Stadium " on the disc). */
uint32_t nfl2k5_teams_stadium_find(const char *name, int *id_out)
{
    uint32_t r = root(), base, n, i;
    size_t len;
    if (!r || !name || !*name) return 0;
    n = MEM32(r + 0x10);
    base = MEM32(r + 0x14);
    if (!base || n > 256) return 0;
    len = strlen(name);
    while (len && name[len - 1] == ' ') len--;
    for (i = 0; i < n; i++) {
        uint32_t rec = base + i * 0x80u, s = MEM32(rec), k;
        char buf[64];
        if (!s) continue;
        for (k = 0; k + 1 < sizeof buf && MEM16(s + 2u * k); k++) {
            uint16_t c = MEM16(s + 2u * k);
            buf[k] = c < 0x80 ? (char)c : '?';
        }
        while (k && buf[k - 1] == ' ') k--;
        buf[k] = 0;
        if (k == len && _strnicmp(buf, name, len) == 0) {
            if (id_out) *id_out = MEM8(rec + 0x7C);
            return rec;
        }
    }
    return 0;
}

/* My NFL Eras stadium packs (docs/eras/research/COLISEUM-IMPORT.md): a stadium the disc does not have.
 * mods\eras\stadiums\<folder>\stadium.json {"name", "display", "location", "code", "clone", "host",
 * "capacity", "indoor", "grass"}; its bundles <code><t><w>.iff are in the archive (nfl2k5_archive_extend).
 * nfl2k5_teams_stadium_pack borrows the "host" row (a fictional stadium, default LOCO ARENA) for one game:
 * saved, filled from the "clone" row, given the pack's strings and code. nfl2k5_teams_stadium_pack_restore
 * puts it back (Eras clean-up). */
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
static uint32_t s_pack_host;
static uint8_t s_pack_saved[0x80];

static int pack_json(const char *text, const char *key, char *out, size_t n)
{
    char pat[48];
    const char *p, *q;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    if (!(p = strstr(text, pat)) || !(p = strchr(p + strlen(pat), ':'))) return 0;
    p++;
    while (*p == ' ') p++;
    if (*p == '"') {
        if (!(q = strchr(++p, '"')) || (size_t)(q - p) >= n) return 0;
    } else {
        for (q = p; *q && *q != ',' && *q != '}' && *q != '\r' && *q != '\n'; q++) {}
        if ((size_t)(q - p) >= n) return 0;
    }
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = 0;
    return 1;
}

static uint32_t pack_wstr(const char *s)      /* kept for the session: a few hundred bytes per pack */
{
    uint32_t n = (uint32_t)strlen(s), g = xbox_HeapAlloc(2 * (n + 1), 4), j;
    if (!g) return 0;
    for (j = 0; j <= n; j++) MEM16(g + 2 * j) = (uint16_t)(unsigned char)s[j];
    return g;
}

void nfl2k5_teams_stadium_pack_restore(void)
{
    if (!s_pack_host) return;
    memcpy((uint8_t *)xbox_GetMemoryOffset() + s_pack_host, s_pack_saved, 0x80);
    fprintf(stderr, "[TEAMS] Eras stadium pack: row %08X restored\n", s_pack_host);
    s_pack_host = 0;
}

uint32_t nfl2k5_teams_stadium_pack(const char *name, int *id_out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    nfl2k5_teams_stadium_pack_restore();          /* one borrowed row at a time */
    if (!name || !*name || (h = FindFirstFileA("mods\\eras\\stadiums\\*", &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        char p[MAX_PATH], text[4096], v[64], code[16], clone[64], host[64], disp[64], loc[64];
        uint32_t src, row, g;
        FILE *f;
        size_t n;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        snprintf(p, sizeof p, "mods\\eras\\stadiums\\%s\\stadium.json", fd.cFileName);
        if (!(f = fopen(p, "rb"))) continue;
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        if (!pack_json(text, "name", v, sizeof v) || _stricmp(v, name) != 0) continue;
        if (!pack_json(text, "code", code, sizeof code) || !pack_json(text, "clone", clone, sizeof clone)) continue;
        if (!pack_json(text, "host", host, sizeof host)) snprintf(host, sizeof host, "LOCO ARENA");
        if (!(src = nfl2k5_teams_stadium_find(clone, NULL)) || !(row = nfl2k5_teams_stadium_find(host, NULL))) continue;
        if (!pack_json(text, "display", disp, sizeof disp)) snprintf(disp, sizeof disp, "%s", v);
        if (!pack_json(text, "location", loc, sizeof loc)) loc[0] = 0;
        memcpy(s_pack_saved, (uint8_t *)xbox_GetMemoryOffset() + row, 0x80);
        s_pack_host = row;
        memcpy((uint8_t *)xbox_GetMemoryOffset() + row, (uint8_t *)xbox_GetMemoryOffset() + src, 0x7C); /* keeps the id byte */
        if ((g = pack_wstr(v))) MEM32(row + 0x00) = g;
        if (loc[0] && (g = pack_wstr(loc))) MEM32(row + 0x08) = g;
        if ((g = pack_wstr(code))) MEM32(row + 0x0C) = g;      /* the file-name key of the nine bundles */
        if ((g = pack_wstr(disp))) MEM32(row + 0x10) = g;
        if (pack_json(text, "capacity", v, sizeof v)) MEM32(row + 0x04) = (uint32_t)atoi(v);
        if (pack_json(text, "indoor", v, sizeof v)) MEM32(row + 0x18) = (uint32_t)atoi(v);
        if (pack_json(text, "grass", v, sizeof v)) MEM32(row + 0x1C) = (uint32_t)atoi(v);
        fprintf(stderr, "[TEAMS] Eras stadium pack %s: %s (code %s, from %s) in row %08X (%s)\n",
                fd.cFileName, name, code, clone, row, host);
        FindClose(h);
        if (id_out) *id_out = MEM8(row + 0x7C);
        return row;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

void nfl2k5_teams_release(int side)
{
    GuestRegs saved;
    uint32_t slot;
    if (side < 0 || side > 1 || !root()) return;
    slot = spare_slot(side);
    /* only the team nfl2k5_teams_stage put there: a historic or added team that
     * Team Select loaded into the slot is not this function's to clear */
    if (!s_stage[side].staged || slot != s_stage[side].slot) { s_stage[side].staged = 0; return; }
    s_stage[side].staged = 0;
    if (!slot || MEM32(slot + T_CAT) != 4) return;   /* empty already, or a user-created team */
    regs_save(&saved);
    release_slot(side, slot);
    s_side[side].entry1 = 0;
    regs_restore(&saved);
    fprintf(stderr, "[TEAMS] released side %d: slot %08X category %u ident %u players %u\n",
            side, slot, MEM32(slot + T_CAT), MEM16(slot + T_IDENT), MEM8(slot + T_ACTIVE));
}

int nfl2k5_teams_identity_check(int side, uint32_t team)
{
    uint32_t arena, count, i;
    int bad = 0;
    if (side < 0 || side > 1 || !s_stage[side].staged || !(arena = nfl2k5_team_arena(side))) return -1;
    count = MEM8(team + T_ACTIVE);
    for (i = 0; i < count && i < 71; i++) {
        uint32_t p = MEM32(team + 4u * i), f = p ? MEM32(p + 0x10) : 0, l = p ? MEM32(p + 0x14) : 0;
        if (f == NFL2K5_ARENA_NAME(arena, i, 0) && l == NFL2K5_ARENA_NAME(arena, i, 1)) continue;
        if (bad++ < 8) {
            char a[64] = "-";
            if (p) ascii_name(p, a, sizeof a);
            fprintf(stderr, "[TEAMS] identity side %d player %u (%08X, %s): names %08X/%08X, arena row %u is %08X/%08X\n",
                    side, i, p, a, f, l, i, NFL2K5_ARENA_NAME(arena, i, 0), NFL2K5_ARENA_NAME(arena, i, 1));
        }
    }
    if ((int)count != s_stage[side].rows) bad += 1000;
    fprintf(stderr, "[TEAMS] identity at the whistle: side %d team %08X: %u players, %d staged rows, %d name mismatches%s\n",
            side, team, count, s_stage[side].rows, bad % 1000, bad >= 1000 ? " (player count differs)" : "");
    return bad;
}

/* M8: the staged slot against its CSV, read again from the file. */
static int wname_eq(uint32_t va, const char *s)
{
    uint32_t i;
    if (!va) return 0;
    for (i = 0; s[i] && i < NFL2K5_ARENA_CHARS - 1; i++)
        if (MEM16(va + 2u * i) != (uint16_t)(unsigned char)s[i]) return 0;
    return MEM16(va + 2u * i) == 0;
}

int nfl2k5_teams_verify_csv(int side, const char *roster_csv)
{
    char line[1024];
    uint32_t slot;
    int row = 0, bad = 0, count;
    FILE *f;
    if (side < 0 || side > 1 || !s_stage[side].staged || !(slot = s_stage[side].slot)) return -1;
    if (!(f = fopen(roster_csv, "r"))) return -1;
    count = MEM8(slot + T_ACTIVE);
    while (fgets(line, sizeof line, f)) {
        char *v[8], *p = line;
        int n = 0, pos, num;
        uint32_t pl;
        if (!isdigit((unsigned char)line[0]) && strncmp(line, "first,", 6) == 0) continue;
        while (n < 8) { v[n++] = p; if (!(p = strchr(p, ','))) break; *p++ = 0; }
        if (n < 8) continue;
        pos = atoi(v[2]);
        num = atoi(v[3]);
        pl = row < count ? MEM32(slot + 4u * (uint32_t)row) : 0;
        if (!pl || !wname_eq(MEM32(pl + 0x10), v[0]) || !wname_eq(MEM32(pl + 0x14), v[1]) ||
            MEM8(pl + 0x35) != pos || (int)((MEM32(pl + 0x20) >> 3) & 0x7F) != num) {
            if (bad++ < 8) {
                char a[64] = "-";
                if (pl) ascii_name(pl, a, sizeof a);
                fprintf(stderr, "[TEAMS] verify side %d row %d: CSV %s %s pos %d #%d, slot %08X %s pos %d #%u\n",
                        side, row, v[0], v[1], pos, num, pl, a, pl ? MEM8(pl + 0x35) : 0,
                        pl ? (MEM32(pl + 0x20) >> 3) & 0x7F : 0);
            }
        }
        row++;
    }
    fclose(f);
    if (row != count) bad += 1000;
    fprintf(stderr, "[TEAMS] verify side %d: %s: %d CSV rows, %d players in the slot, %d mismatches%s\n",
            side, roster_csv, row, count, bad % 1000, row != count ? " (player count differs)" : "");
    return bad;
}

/* ---- the M2 test trigger ---------------------------------------------------
 *
 * NFL2K5_ERAS_STAGE_TEST=<home_id>,<away_id> (franchise ids of
 * data\eras\runtime\1980\teams.csv): the first Team Select step stages both
 * sides (home USER1, away USER2) and publishes them; later steps re-publish the
 * staged team of that side. Back at the Main Menu (after the game, or Back)
 * both slots are released and the selected-team globals that pointed at them
 * get their values from before the staging back. */
#define ERAS_PACKAGE "data\\eras\\runtime\\1980"

typedef struct { char id[32], city[48], name[48], abbr[16], roster[MAX_PATH]; int entry; } ErasTeamRow;

static int eras_team_row(const char *id, ErasTeamRow *out)
{
    char line[1024];
    FILE *f = fopen(ERAS_PACKAGE "\\teams.csv", "r");
    int found = 0;
    if (!f) return 0;
    while (!found && fgets(line, sizeof line, f)) {
        char *v[12], *p = line, *e;
        int n = 0;
        while (n < 12) { v[n++] = p; if (!(p = strchr(p, ','))) break; *p++ = 0; }
        if (n < 9 || strcmp(v[0], id) != 0) continue;
        for (e = v[n - 1] + strlen(v[n - 1]); e > v[n - 1] && (e[-1] == '\n' || e[-1] == '\r'); ) *--e = 0;
        memset(out, 0, sizeof *out);
        snprintf(out->id, sizeof out->id, "%s", v[0]);
        snprintf(out->city, sizeof out->city, "%s", v[1]);
        snprintf(out->name, sizeof out->name, "%s", v[2]);
        snprintf(out->abbr, sizeof out->abbr, "%s", v[3]);
        out->entry = atoi(v[6]);
        snprintf(out->roster, sizeof out->roster, ERAS_PACKAGE "\\%s", v[8]);
        for (e = out->roster; *e; e++) if (*e == '/') *e = '\\';
        found = 1;
    }
    fclose(f);
    return found;
}

#define SEL_HOME 0xE5FE68u     /* selected home / away team; last-used home / away at +8 / +0xC */
static int s_stage_test;       /* 0 not tried, 1 both sides staged, -1 done */
static uint32_t s_sel_saved[4];

static int stage_test(int side, const char *spec)
{
    char ids[2][32];
    const char *comma = strchr(spec, ',');
    ErasTeamRow row[2];
    int s, rc = NFL2K5_STAGE_BAD_ARGS;
    s_stage_test = -1;
    if (!comma || comma - spec >= 32 || strlen(comma + 1) >= 32) goto bad;
    snprintf(ids[0], sizeof ids[0], "%.*s", (int)(comma - spec), spec);
    snprintf(ids[1], sizeof ids[1], "%s", comma + 1);
    for (s = 0; s < 2; s++)
        if (!eras_team_row(ids[s], &row[s])) {
            fprintf(stderr, "[TEAMS] stage test: no franchise '%s' in " ERAS_PACKAGE "\\teams.csv\n", ids[s]);
            goto bad;
        }
    for (s = 0; s < 4; s++) s_sel_saved[s] = MEM32(SEL_HOME + 4u * (uint32_t)s);
    for (s = 0; s < 2; s++) {
        rc = nfl2k5_teams_stage(s, row[s].entry, row[s].roster, row[s].city, row[s].name, row[s].abbr);
        if (rc != NFL2K5_STAGE_OK) goto bad;
    }
    fprintf(stderr, "[TEAMS] stage test: %s (home) vs %s (away) staged; unverified community roster\n", ids[0], ids[1]);
    s_stage_test = 1;
    publish(1 - side, s_stage[1 - side].slot);
    g_eax = publish(side, s_stage[side].slot);
    return 1;
bad:
    fprintf(stderr, "[TEAMS] stage test '%s' failed: %d %s\n", spec, rc, nfl2k5_teams_stage_message(rc));
    nfl2k5_teams_release(0);
    nfl2k5_teams_release(1);
    return 0;
}

/* M8 test NFL2K5_ERAS_STAGE_ALL=1: at the first Team Select step, every
 * franchise of teams.csv is staged into USER1 (then USER2), checked against
 * its CSV (nfl2k5_teams_verify_csv) and released; the step then goes on as
 * usual. One summary line per side. */
static void stage_all(void)
{
    char line[1024];
    FILE *f = fopen(ERAS_PACKAGE "\\teams.csv", "r");
    int teams = 0, ok = 0, mism = 0, side, minp = 99, maxp = 0;
    char minid[32] = "-";
    if (!f) { fprintf(stderr, "[TEAMS] stage all: no " ERAS_PACKAGE "\\teams.csv\n"); return; }
    while (fgets(line, sizeof line, f)) {
        ErasTeamRow r;
        char id[32];
        char *c = strchr(line, ',');
        if (!c || c - line >= 32 || strncmp(line, "id,", 3) == 0) continue;
        snprintf(id, sizeof id, "%.*s", (int)(c - line), line);
        if (!eras_team_row(id, &r)) continue;
        teams++;
        for (side = 0; side < 2; side++) {
            int rc = nfl2k5_teams_stage(side, r.entry, r.roster, r.city, r.name, r.abbr), bad, n;
            if (rc != NFL2K5_STAGE_OK) {
                fprintf(stderr, "[TEAMS] stage all: %s side %d refused: %d %s\n", id, side, rc, nfl2k5_teams_stage_message(rc));
                mism++;
                continue;
            }
            n = MEM8(s_stage[side].slot + T_ACTIVE);
            bad = nfl2k5_teams_verify_csv(side, r.roster);
            if (bad == 0) ok++; else mism++;
            if (side == 0 && n < minp) { minp = n; snprintf(minid, sizeof minid, "%s", id); }
            if (side == 0 && n > maxp) maxp = n;
            nfl2k5_teams_release(side);
        }
    }
    fclose(f);
    fprintf(stderr, "[TEAMS] stage all: %d franchises x 2 sides: %d staged and verified with 0 mismatches, %d failed; "
            "players %d..%d (fewest: %s)\n", teams, ok, mism, minp, maxp, minid);
}

/* From the front end's FSM pop (src/main.c nfl2k5_diag_fsm): the Main Menu is
 * on top again, so Team Select, Coach Match Up and the match are all gone. */
void nfl2k5_teams_fsm_pop(uint32_t fsm, uint32_t top)
{
    uint32_t slot[2];
    int s, k;
    if (s_stage_test != 1 || fsm != 0xA84B18u || top != 0x515660u) return;
    s_stage_test = -1;
    for (s = 0; s < 2; s++) slot[s] = s_stage[s].slot;
    nfl2k5_teams_release(0);
    nfl2k5_teams_release(1);
    for (k = 0; k < 4; k++) {
        uint32_t a = SEL_HOME + 4u * (uint32_t)k, v = MEM32(a);
        if (v && (v == slot[0] || v == slot[1])) {
            fprintf(stderr, "[TEAMS] stage test: [%08X] = %08X (a released slot) -> %08X\n", a, v, s_sel_saved[k]);
            MEM32(a) = s_sel_saved[k];
        }
    }
    fprintf(stderr, "[TEAMS] stage test: both slots released at the Main Menu\n");
}

/* One Team Select step. random: the Random Team spin (resident list only).
 * Returns 1 with g_eax set when it handled the step, 0 for the retail one. */
int nfl2k5_ts_step(int side, int dir, int random)
{
    uint32_t team, slot, mask, next;
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi;
    int usable, entry;
    if (s_log < 0) s_log = getenv("NFL2K5_TEAMS_LOG") != NULL;
    if (!root() || !local_flow()) return 0;
    team = MEM32(TS_SIDE + 4u * (uint32_t)side);
    if (!team) return 0;
    {
        static int all_done;
        if (!all_done && getenv("NFL2K5_ERAS_STAGE_ALL")) { all_done = 1; stage_all(); }
    }
    if (s_stage_test == 0) {
        const char *spec = getenv("NFL2K5_ERAS_STAGE_TEST");
        if (spec && *spec && stage_test(side, spec)) goto out;
    } else if (s_stage_test == 1 && s_stage[side].staged) {
        g_eax = publish(side, s_stage[side].slot);   /* the staged team stays */
        goto out;
    }
    {
        static int inventory_done;
        if (!inventory_done && getenv("NFL2K5_HISTORIC_INVENTORY")) {
            uint32_t s = spare_slot(side);
            inventory_done = 1;
            if (slot_usable(side, s) && team != s) historic_inventory(s, side);
        }
    }
    mask = gcall(sub_002C0D60, 0, 0);
    if (random) {
        next = resident_step(team, 1, mask);
        g_eax = publish(side, next);
        goto out;
    }
    slot = spare_slot(side);
    usable = slot_usable(side, slot);
    if (usable && team == slot && MEM32(slot + T_CAT) == 4) {
        /* in the outside list: the next or previous entry, or back out */
        entry = current_entry(side, slot);
        if (entry < 0) entry = dir > 0 ? -1 : outside_count();
        entry += dir;
        if (entry >= 0 && entry < outside_count() && load_entry(slot, entry, side)) {
            remember(side, slot, entry);
            g_eax = publish(side, slot);
            goto out;
        }
        release_slot(side, slot);
        s_side[side].entry1 = 0;
        next = resident_step(0, 1, mask);                       /* the first resident team */
        if (dir < 0) next = resident_step(next, -1, mask);      /* ...or the one before it: the last */
        g_eax = publish(side, next);
        goto out;
    }
    next = resident_step(team, dir, mask);
    /* The resident list runs in the game's own order (by name), so the end is
     * where the iterator wraps: forward onto its first team, back onto its last. */
    {
        uint32_t first = resident_step(0, 1, mask), last = resident_step(first, -1, mask);
        if (s_log)
            fprintf(stderr, "[TEAMS] side %d step %+d: %08X -> %08X (first %08X last %08X, spare %08X usable %d)\n",
                    side, dir, team, next, first, last, slot, usable);
        if (!(usable && ((dir > 0 && next == first && team != 0) || (dir < 0 && next == last))))
            goto publish_next;
    }
    {
        /* wrapped past the end of the resident list: into the outside list */
        entry = dir > 0 ? 0 : outside_count() - 1;
        if (load_entry(slot, entry, side)) {
            remember(side, slot, entry);
            next = slot;
        }
    }
publish_next:
    g_eax = publish(side, next);
out:
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi;
    return 1;
}


/* The game's team filter (0xE2E50: category -> list bit, tested against a
 * list's mask): an added team (asset code Z?/Q?, category 4 in its slot) also
 * counts as a created team (bit 4), so Roster Manager and the other lists of
 * created teams show it while it is loaded. -1: not an added team. */
int nfl2k5_team_filter(uint32_t team, uint32_t mask, uint32_t ret)
{
    uint32_t code;
    uint16_t c0;
    if (MEM32(team + T_CAT) != 4 || !(code = MEM32(team + T_CODE))) return -1;
    c0 = (uint16_t)(MEM16(code) & 0xFFDF);
    if (c0 != 'Z' && c0 != 'Q') return -1;
    if (s_log > 0) {
        static int n;
        if (n++ < 60) fprintf(stderr, "[TEAMS] filter: added team %08X, list mask %08X (from %08X)\n", team, mask, ret);
    }
    return (mask & 0x1Cu) ? 1 : 0;          /* created (4), historic (0x10), and lists with free agents (8): Roster Manager */
}

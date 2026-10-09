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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

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

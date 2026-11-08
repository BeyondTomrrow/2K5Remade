/* The halftime hang study (My NFL Eras M4 review), moved out of
 * src/nfl2k5_eras_launch.c: it is a game diagnostic, not part of Eras
 * (docs/eras/MOD-PACK-DESIGN.md G8). main.c sets nfl2k5_walk_diag_on from
 * NFL2K5_WALK_DIAG; the gen patches WALK_DIAG_ENTER / WALK_DIAG_STEP call in. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

/* One Eras game froze at the halftime show with the game thread in
 * sub_000561F0: a walk of 4-byte-headed entries (type = bits 13-15, size =
 * bits 0-12, id = the second word) from ecx + 0x10 for the id in dx; type 5 or
 * 7 ends it, type 6 continues at [esi + 8]. A zero size, or a type-6 link
 * back into the same entries, never ends. NFL2K5_WALK_DIAG=1 turns on the gen
 * patches WALK_DIAG_ENTER / WALK_DIAG_STEP: every call's arguments are kept
 * (per thread), and a walk that passes 4M entries is logged once with its
 * arguments, caller, stack and the entries it walks. Off, the patches cost a
 * load and a branch. */
int nfl2k5_walk_diag_on;
static __declspec(thread) uint32_t s_w_arg[4], s_w_steps;

void nfl2k5_walk_enter(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t esp)
{
    s_w_arg[0] = ecx; s_w_arg[1] = edx; s_w_arg[2] = esi; s_w_arg[3] = esp;
    s_w_steps = 0;
}

static int w_ok(uint32_t a) { a &= 0x7FFFFFFFu; return a >= 0x10000u && a + 16u < 0x04000000u; }

void nfl2k5_walk_step(uint32_t ecx)
{
    char buf[1400];
    size_t o = 0;
    uint32_t p, i, esi = s_w_arg[2], esp = s_w_arg[3];
    if (++s_w_steps != (1u << 22)) return;
    fprintf(stderr, "[WALK] sub_000561F0 spin: %u entries walked; entry ecx=%08X edx=%08X (id %04X) esi=%08X [esi+8]=%08X "
            "caller=%08X now at %08X (header %08X) thread %lu\n",
            s_w_steps, s_w_arg[0], s_w_arg[1], s_w_arg[1] & 0xFFFFu, esi, w_ok(esi) ? MEM32(esi + 8u) : 0u,
            w_ok(esp) ? MEM32(esp) : 0u, ecx, w_ok(ecx) ? MEM32(ecx) : 0u, GetCurrentThreadId());
    if (w_ok(esp)) {
        for (i = 0; i < 24 && o + 12 < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, " %08X", MEM32(esp + 4u * i));
        fprintf(stderr, "[WALK] stack:%s\n", buf);
    }
    /* the walk as the function makes it, from the entry ecx, 48 steps */
    o = 0; buf[0] = 0;
    for (p = s_w_arg[0] + 0x10u, i = 0; i < 48 && w_ok(p) && o + 40 < sizeof buf; i++) {
        uint32_t h = MEM32(p), t = (h >> 13) & 7u, sz = h & 0x1FFFu;
        o += (size_t)snprintf(buf + o, sizeof buf - o, " %08X:t%u/s%X/id%04X", p, t, sz, MEM16(p + 2u));
        if (t == 5 || t == 7) { o += (size_t)snprintf(buf + o, sizeof buf - o, " END"); break; }
        if (t == 6) { p = w_ok(esi) ? MEM32(esi + 8u) : 0; continue; }
        if (MEM16(p + 2u) == (s_w_arg[1] & 0xFFFFu)) { o += (size_t)snprintf(buf + o, sizeof buf - o, " FOUND"); break; }
        p += sz;
    }
    fprintf(stderr, "[WALK] entries:%s\n", buf);
    if (w_ok(esi)) {
        o = 0;
        for (i = 0; i < 16 && o + 12 < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, " %08X", MEM32(esi + 4u * i));
        fprintf(stderr, "[WALK] esi block:%s\n", buf);
    }
    fflush(stderr);
}

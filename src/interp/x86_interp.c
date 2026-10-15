/* x86 interpreter for code the native build does not contain.
 *
 * A mod pack changes the game's executable: it adds code in new sections and
 * patches retail functions (a jmp into its own code, a different constant, a
 * redirected call). The native build was converted from the retail
 * executable, so it has none of that. Instead of converting every pack ahead
 * of time, the game runs the changed code here and everything else natively:
 *
 *   - recomp_interp_dirty() says whether a guest function differs from the
 *     executable the native code came from (nfl2k5_interp_setup() compares
 *     them at boot). Every converted function checks it on entry
 *     (RECOMP_INTERP_GATE in recomp_types.h) and hands a changed one to
 *     recomp_interp_call().
 *   - An indirect call or jump to an address the native build has no
 *     function for (mod code) lands in recomp_interp_call() as well.
 *   - When interpreted code calls a function that exists natively and is
 *     unchanged, it calls the native one, which shares the guest registers
 *     (g_eax...), stack (g_esp), x87 stack and xmm registers with this file.
 *
 * Semantics follow the converter's (tools/recomp/lifter.py) so the two agree
 * across calls: x87 values are doubles on g_fp_stack, the control word lives in
 * g_fp_control_word, xmm registers in g_xmm0..7.
 *
 * Instructions are decoded each time they execute; a decoded-block cache can
 * come later if a pack's changed code turns out to be hot.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "platform/xbox_winnt.h"   /* XBOX_THREAD_LOCAL */

#include "recomp_types.h"

#include "x86_interp.h"

/* ---- state ---------------------------------------------------------------- */

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

typedef struct {
    uint32_t r[8];
    uint32_t eip;
    int cf, pf, af, zf, sf, of;
    /* decode state for the current instruction */
    uint32_t start;         /* address of the instruction */
    int opsize16;           /* 0x66 */
    int rep;                /* 0xF3 = 1, 0xF2 = 2 */
    int seg_fs;             /* 0x64 */
    int sse_prefix;         /* last of 0x66/0xF2/0xF3 seen, for SSE */
    /* ModRM */
    int mod, reg, rm;
    uint32_t ea;            /* effective address when mod != 3 */
} Cpu;

static int s_interp_log = -1;
static volatile LONG s_unknown_reported;

/* Dirty bitmap over guest code addresses: bit set = the function starting
 * there differs from the converted executable. Built by nfl2k5_interp_setup. */
uint8_t *g_interp_dirty_bits;
uint32_t g_interp_dirty_limit;   /* addresses >= this are never native */
int g_interp_active;             /* nonzero once a pack needs the interpreter */

static uint32_t (*s_fn_end)(uint32_t start);   /* optional: function extent */

/* ---- memory ---------------------------------------------------------------- */

static inline uint32_t seg(Cpu *c, uint32_t a) { return c->seg_fs ? a + g_fs_base : a; }
static inline uint8_t  rd8 (uint32_t a) { return MEM8(a); }
static inline uint16_t rd16(uint32_t a) { return MEM16(a); }
static inline uint32_t rd32(uint32_t a) { return MEM32(a); }
static inline void wr8 (uint32_t a, uint8_t v)  { MEM8(a) = v; }
static inline void wr16(uint32_t a, uint16_t v) { MEM16(a) = v; }
static inline void wr32(uint32_t a, uint32_t v) { MEM32(a) = v; }

static inline uint8_t  fetch8 (Cpu *c) { uint8_t v = rd8(c->eip); c->eip += 1; return v; }
static inline uint16_t fetch16(Cpu *c) { uint16_t v = rd16(c->eip); c->eip += 2; return v; }
static inline uint32_t fetch32(Cpu *c) { uint32_t v = rd32(c->eip); c->eip += 4; return v; }

static inline void push32(Cpu *c, uint32_t v) { c->r[ESP] -= 4; wr32(c->r[ESP], v); }
static inline uint32_t pop32(Cpu *c) { uint32_t v = rd32(c->r[ESP]); c->r[ESP] += 4; return v; }

/* ---- registers --------------------------------------------------------------- */

static inline uint32_t getreg(Cpu *c, int size, int i)
{
    if (size == 4) return c->r[i];
    if (size == 2) return c->r[i] & 0xFFFF;
    return i < 4 ? (c->r[i] & 0xFF) : ((c->r[i - 4] >> 8) & 0xFF);
}

static inline void setreg(Cpu *c, int size, int i, uint32_t v)
{
    if (size == 4) c->r[i] = v;
    else if (size == 2) c->r[i] = (c->r[i] & 0xFFFF0000u) | (v & 0xFFFF);
    else if (i < 4) c->r[i] = (c->r[i] & 0xFFFFFF00u) | (v & 0xFF);
    else c->r[i - 4] = (c->r[i - 4] & 0xFFFF00FFu) | ((v & 0xFF) << 8);
}

/* ---- ModRM ------------------------------------------------------------------- */

static void modrm(Cpu *c)
{
    uint8_t m = fetch8(c);
    c->mod = m >> 6; c->reg = (m >> 3) & 7; c->rm = m & 7;
    if (c->mod == 3) return;
    uint32_t a = 0;
    if (c->rm == 4) {
        uint8_t sib = fetch8(c);
        int scale = sib >> 6, idx = (sib >> 3) & 7, base = sib & 7;
        if (base == 5 && c->mod == 0) a = fetch32(c);
        else a = c->r[base];
        if (idx != 4) a += c->r[idx] << scale;
    } else if (c->rm == 5 && c->mod == 0) {
        a = fetch32(c);
    } else {
        a = c->r[c->rm];
    }
    if (c->mod == 1) a += (uint32_t)(int32_t)(int8_t)fetch8(c);
    else if (c->mod == 2) a += fetch32(c);
    c->ea = seg(c, a);
}

static inline uint32_t getrm(Cpu *c, int size)
{
    if (c->mod == 3) return getreg(c, size, c->rm);
    return size == 4 ? rd32(c->ea) : size == 2 ? rd16(c->ea) : rd8(c->ea);
}

static inline void setrm(Cpu *c, int size, uint32_t v)
{
    if (c->mod == 3) { setreg(c, size, c->rm, v); return; }
    if (size == 4) wr32(c->ea, v); else if (size == 2) wr16(c->ea, (uint16_t)v); else wr8(c->ea, (uint8_t)v);
}

/* ---- flags ------------------------------------------------------------------- */

static inline uint32_t mask_of(int size) { return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu; }
static inline uint32_t sign_of(int size) { return size == 4 ? 0x80000000u : size == 2 ? 0x8000u : 0x80u; }

static inline void szp(Cpu *c, int size, uint32_t r)
{
    r &= mask_of(size);
    c->zf = r == 0;
    c->sf = (r & sign_of(size)) != 0;
    c->pf = recomp_parity8(r);
}

static uint32_t alu(Cpu *c, int op, int size, uint32_t a, uint32_t b)
{
    uint32_t m = mask_of(size), s = sign_of(size), r = 0;
    a &= m; b &= m;
    switch (op) {
    case 0: /* add */
    case 2: { /* adc */
        uint32_t cin = op == 2 ? (uint32_t)c->cf : 0;
        uint64_t w = (uint64_t)a + b + cin;
        r = (uint32_t)w & m;
        c->cf = (w >> (size * 8)) & 1;
        c->of = ((~(a ^ b) & (a ^ r)) & s) != 0;
        c->af = ((a ^ b ^ r) & 0x10) != 0;
        szp(c, size, r);
        return r;
    }
    case 5: /* sub */
    case 3: /* sbb */
    case 7: { /* cmp */
        uint32_t cin = op == 3 ? (uint32_t)c->cf : 0;
        uint64_t w = (uint64_t)a - b - cin;
        r = (uint32_t)w & m;
        c->cf = ((uint64_t)a < (uint64_t)b + cin);
        c->of = (((a ^ b) & (a ^ r)) & s) != 0;
        c->af = ((a ^ b ^ r) & 0x10) != 0;
        szp(c, size, r);
        return r;
    }
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    case 6: r = a ^ b; break;
    }
    c->cf = 0; c->of = 0; c->af = 0;
    szp(c, size, r);
    return r & m;
}

static inline int cond(Cpu *c, int cc)
{
    switch (cc & 15) {
    case 0: return c->of;            case 1: return !c->of;
    case 2: return c->cf;            case 3: return !c->cf;
    case 4: return c->zf;            case 5: return !c->zf;
    case 6: return c->cf || c->zf;   case 7: return !c->cf && !c->zf;
    case 8: return c->sf;            case 9: return !c->sf;
    case 10: return c->pf;           case 11: return !c->pf;
    case 12: return c->sf != c->of;  case 13: return c->sf == c->of;
    case 14: return c->zf || c->sf != c->of;
    default: return !c->zf && c->sf == c->of;
    }
}

static uint32_t get_eflags(Cpu *c)
{
    return 0x202u | (c->cf ? 1u : 0) | (c->pf ? 4u : 0) | (c->af ? 0x10u : 0) | (c->zf ? 0x40u : 0) |
           (c->sf ? 0x80u : 0) | (g_df ? 0x400u : 0) | (c->of ? 0x800u : 0);
}

static void set_eflags(Cpu *c, uint32_t f)
{
    c->cf = f & 1; c->pf = (f >> 2) & 1; c->af = (f >> 4) & 1; c->zf = (f >> 6) & 1;
    c->sf = (f >> 7) & 1; g_df = (int)((f >> 10) & 1); c->of = (f >> 11) & 1;
}

/* ---- shifts ------------------------------------------------------------------ */

static uint32_t shift(Cpu *c, int op, int size, uint32_t v, unsigned n)
{
    uint32_t m = mask_of(size), s = sign_of(size);
    int bits = size * 8;
    v &= m;
    n &= 31;
    if (!n) return v;
    switch (op) {
    case 0: { /* rol */
        unsigned k = n % bits;
        uint32_t r = k ? ((v << k) | (v >> (bits - k))) & m : v;
        c->cf = r & 1;
        c->of = ((r & s) != 0) ^ c->cf;
        return r;
    }
    case 1: { /* ror */
        unsigned k = n % bits;
        uint32_t r = k ? ((v >> k) | (v << (bits - k))) & m : v;
        c->cf = (r & s) != 0;
        c->of = ((r & s) != 0) ^ ((r & (s >> 1)) != 0);
        return r;
    }
    case 2: { /* rcl */
        unsigned k = n % (bits + 1);
        uint64_t w = ((uint64_t)c->cf << bits) | v;
        for (unsigned i = 0; i < k; i++) w = ((w << 1) | (w >> bits)) & ((2ull << bits) - 1);
        c->cf = (int)((w >> bits) & 1);
        uint32_t r = (uint32_t)w & m;
        c->of = ((r & s) != 0) ^ c->cf;
        return r;
    }
    case 3: { /* rcr */
        unsigned k = n % (bits + 1);
        uint64_t w = ((uint64_t)c->cf << bits) | v;
        for (unsigned i = 0; i < k; i++) w = ((w >> 1) | ((w & 1) << bits));
        c->cf = (int)((w >> bits) & 1);
        uint32_t r = (uint32_t)w & m;
        c->of = ((r & s) != 0) ^ ((r & (s >> 1)) != 0);
        return r;
    }
    case 4: case 6: { /* shl/sal */
        uint32_t r = n >= (unsigned)bits ? 0 : (v << n) & m;
        c->cf = n <= (unsigned)bits ? (int)((v >> (bits - n)) & 1) : 0;
        c->of = ((r & s) != 0) ^ c->cf;
        szp(c, size, r);
        return r;
    }
    case 5: { /* shr */
        uint32_t r = n >= (unsigned)bits ? 0 : v >> n;
        c->cf = n <= (unsigned)bits ? (int)((v >> (n - 1)) & 1) : 0;
        c->of = (v & s) != 0;
        szp(c, size, r);
        return r;
    }
    default: { /* sar */
        int32_t sv = size == 4 ? (int32_t)v : size == 2 ? (int16_t)v : (int8_t)v;
        unsigned k = n >= (unsigned)bits ? (unsigned)bits - 1 : n;
        uint32_t r = (uint32_t)(sv >> k) & m;
        c->cf = (int)((sv >> (k == n ? n - 1 : k)) & 1);
        c->of = 0;
        szp(c, size, r);
        return r;
    }
    }
}

/* ---- calls into native code ---------------------------------------------------- */

static inline void sync_out(Cpu *c)
{
    g_eax = c->r[EAX]; g_ecx = c->r[ECX]; g_edx = c->r[EDX]; g_ebx = c->r[EBX];
    g_esp = c->r[ESP]; g_esi = c->r[ESI]; g_edi = c->r[EDI];
    g_ebp = c->r[EBP]; g_seh_ebp = c->r[EBP];
}

static inline void sync_in(Cpu *c)
{
    c->r[EAX] = g_eax; c->r[ECX] = g_ecx; c->r[EDX] = g_edx; c->r[EBX] = g_ebx;
    c->r[ESP] = g_esp; c->r[ESI] = g_esi; c->r[EDI] = g_edi;
}

/* ---- native hooks at guest instructions (x86_interp.h) ------------------------ */

static struct { uint32_t va, from; recomp_interp_hook_t fn; } s_hooks[32];
static int s_nhooks;
static uint32_t s_hook_lo = 0xFFFFFFFFu, s_hook_hi;

void recomp_interp_add_hook(uint32_t va, uint32_t from, recomp_interp_hook_t fn)
{
    if (s_nhooks >= (int)(sizeof s_hooks / sizeof s_hooks[0]) || !fn) return;
    s_hooks[s_nhooks].va = va;
    s_hooks[s_nhooks].from = from;
    s_hooks[s_nhooks].fn = fn;
    s_nhooks++;
    if (va < s_hook_lo) s_hook_lo = va;
    if (va > s_hook_hi) s_hook_hi = va;
}

/* Runs the hooks for the instruction at c->start (prev: the one before).
 * Returns the address to continue at, or 0 to execute c->start. */
static uint32_t run_hooks(Cpu *c, uint32_t prev)
{
    uint32_t next = 0;
    for (int i = 0; i < s_nhooks; i++) {
        uint32_t r;
        if (s_hooks[i].va != c->start || (s_hooks[i].from && s_hooks[i].from != prev)) continue;
        sync_out(c);
        r = s_hooks[i].fn();
        sync_in(c);
        if (r && !next) next = r;
    }
    return next;
}

int recomp_interp_dirty(uint32_t va)
{
    if (!g_interp_dirty_bits) return 0;
    if (va >= g_interp_dirty_limit) return 1;
    return (g_interp_dirty_bits[va >> 3] >> (va & 7)) & 1;
}

/* The native function for `va`, or NULL when it must be interpreted. */
static recomp_func_t native_for(uint32_t va)
{
    recomp_func_t fn;
    if (va >= 0xFE000000u) return recomp_lookup_kernel(va);   /* kernel thunk bridge */
    if (recomp_interp_dirty(va)) return NULL;
    fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    return fn;
}

/* The last instructions interpreted on this thread (oldest first in the
 * report), so a fatal stop shows how control got there. */
static XBOX_THREAD_LOCAL uint32_t t_trail[64];
static XBOX_THREAD_LOCAL unsigned t_trail_n;

static void fatal(Cpu *c, const char *what)
{
    {
        unsigned i, n = t_trail_n < 64 ? t_trail_n : 64;
        fprintf(stderr, "[INTERP] trail:");
        for (i = 0; i < n; i++) fprintf(stderr, " %08X", t_trail[(t_trail_n - n + i) & 63]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "[INTERP] %s at %08X: %02X %02X %02X %02X %02X %02X\n", what, c->start,
            rd8(c->start), rd8(c->start + 1), rd8(c->start + 2), rd8(c->start + 3),
            rd8(c->start + 4), rd8(c->start + 5));
    fflush(stderr);
}

/* x87 and SSE live in x86_interp_fpu.c. Each returns 0 when it does not know
 * the instruction. */
int interp_x87(Cpu *c, uint8_t op);
int interp_0f_sse(Cpu *c, uint8_t op2);

#include "x86_interp_fpu.inc.c"

/* ---- string instructions ------------------------------------------------------- */

static void string_op(Cpu *c, uint8_t op)
{
    int size = (op & 1) ? (c->opsize16 ? 2 : 4) : 1;
    int step = g_df ? -size : size;
    int rep = c->rep;
    for (;;) {
        if (rep && c->r[ECX] == 0) break;
        switch (op & 0xFE) {
        case 0xA4: { /* movs */
            uint32_t v = size == 4 ? rd32(seg(c, c->r[ESI])) : size == 2 ? rd16(seg(c, c->r[ESI])) : rd8(seg(c, c->r[ESI]));
            if (size == 4) wr32(c->r[EDI], v); else if (size == 2) wr16(c->r[EDI], (uint16_t)v); else wr8(c->r[EDI], (uint8_t)v);
            c->r[ESI] += step; c->r[EDI] += step;
            break;
        }
        case 0xA6: { /* cmps */
            uint32_t a = size == 4 ? rd32(seg(c, c->r[ESI])) : size == 2 ? rd16(seg(c, c->r[ESI])) : rd8(seg(c, c->r[ESI]));
            uint32_t b = size == 4 ? rd32(c->r[EDI]) : size == 2 ? rd16(c->r[EDI]) : rd8(c->r[EDI]);
            alu(c, 7, size, a, b);
            c->r[ESI] += step; c->r[EDI] += step;
            break;
        }
        case 0xAA: /* stos */
            if (size == 4) wr32(c->r[EDI], c->r[EAX]); else if (size == 2) wr16(c->r[EDI], (uint16_t)c->r[EAX]);
            else wr8(c->r[EDI], (uint8_t)c->r[EAX]);
            c->r[EDI] += step;
            break;
        case 0xAC: /* lods */
            setreg(c, size, EAX, size == 4 ? rd32(seg(c, c->r[ESI])) : size == 2 ? rd16(seg(c, c->r[ESI])) : rd8(seg(c, c->r[ESI])));
            c->r[ESI] += step;
            break;
        case 0xAE: { /* scas */
            uint32_t b = size == 4 ? rd32(c->r[EDI]) : size == 2 ? rd16(c->r[EDI]) : rd8(c->r[EDI]);
            alu(c, 7, size, getreg(c, size, EAX), b);
            c->r[EDI] += step;
            break;
        }
        }
        if (!rep) break;
        c->r[ECX]--;
        if ((op & 0xF6) == 0xA6) {           /* cmps/scas: repe / repne */
            if (rep == 1 && !c->zf) break;
            if (rep == 2 && c->zf) break;
        }
    }
}

/* ---- the interpreter loop ------------------------------------------------------ */

/* Run guest code from `va`. On entry g_esp points at the return address the
 * caller pushed, exactly as for a converted function; returns when that
 * frame returns. */
extern ptrdiff_t xbox_GetMemoryOffset(void);
/* The instruction being interpreted on this thread, for the fault report. */
static XBOX_THREAD_LOCAL Cpu *t_cur;
/* Diagnostic only. TODO(posix): report through a SIGSEGV handler. */
#ifdef _WIN32
static LONG WINAPI interp_fault_report(EXCEPTION_POINTERS *ep)
{
    static LONG reported;
    Cpu *c = t_cur;
    uint32_t gaddr = (uint32_t)((intptr_t)ep->ExceptionRecord->ExceptionInformation[1] - (intptr_t)xbox_GetMemoryOffset());
    if (c && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        gaddr < 0xFD000000u &&                    /* hardware registers are emulated on fault */
        InterlockedIncrement(&reported) <= 3) {
        unsigned i, n = t_trail_n < 64 ? t_trail_n : 64;
        fprintf(stderr, "[INTERP] guest address %08X; jumps:", gaddr);
        for (i = 0; i < n; i++) fprintf(stderr, " %08X", t_trail[(t_trail_n - n + i) & 63]);
        fprintf(stderr, "\n");
        fprintf(stderr, "[INTERP] fault interpreting %08X: eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
                c->start, c->r[EAX], c->r[ECX], c->r[EDX], c->r[EBX], c->r[ESP], c->r[EBP], c->r[ESI], c->r[EDI]);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void recomp_interp_call(uint32_t va)
{
    Cpu cpu, *c = &cpu;
    uint32_t esp0;
    memset(c, 0, sizeof *c);
    c->r[EAX] = g_eax; c->r[ECX] = g_ecx; c->r[EDX] = g_edx; c->r[EBX] = g_ebx;
    c->r[ESP] = g_esp; c->r[EBP] = g_ebp; c->r[ESI] = g_esi; c->r[EDI] = g_edi;
    c->eip = va;
    esp0 = c->r[ESP];
    if (s_interp_log < 0) {
        s_interp_log = getenv("NFL2K5_INTERP_LOG") != NULL;
#ifdef _WIN32
        AddVectoredExceptionHandler(1, interp_fault_report);
#endif
    }
    if (s_interp_log) { fprintf(stderr, "[INTERP] enter %08X esp %08X ret %08X\n", va, esp0, rd32(esp0)); }

    for (;;) {
        uint8_t op;
        int size;
        uint32_t target = 0, prev = c->start;
        c->start = c->eip;
        if (s_nhooks && c->start >= s_hook_lo && c->start <= s_hook_hi) {
            uint32_t to = run_hooks(c, prev);
            if (to) { c->eip = to; continue; }
        }
        t_cur = c;
        {   /* record control transfers only: from -> to */
            static XBOX_THREAD_LOCAL uint32_t last;
            if (c->start <= last || c->start > last + 15) {
                t_trail[t_trail_n++ & 63] = last;
                t_trail[t_trail_n++ & 63] = c->start;
            }
            last = c->start;
        }
        c->opsize16 = 0; c->rep = 0; c->seg_fs = 0; c->sse_prefix = 0;
        /* prefixes */
        for (;;) {
            op = fetch8(c);
            if (op == 0x66) { c->opsize16 = 1; c->sse_prefix = 0x66; continue; }
            if (op == 0xF3) { c->rep = 1; c->sse_prefix = 0xF3; continue; }
            if (op == 0xF2) { c->rep = 2; c->sse_prefix = 0xF2; continue; }
            if (op == 0x64) { c->seg_fs = 1; continue; }
            if (op == 0xF0 || op == 0x2E || op == 0x3E || op == 0x26 || op == 0x36 || op == 0x65 || op == 0x67) continue;
            break;
        }
        size = c->opsize16 ? 2 : 4;

        /* ALU block 00-3F (except the prefixes and BCD) */
        if (op < 0x40 && (op & 7) < 6) {
            int aop = op >> 3, form = op & 7;
            uint32_t a, b, r;
            if (form == 0 || form == 1) {            /* Eb,Gb / Ev,Gv */
                int sz = form == 0 ? 1 : size;
                modrm(c);
                a = getrm(c, sz); b = getreg(c, sz, c->reg);
                r = alu(c, aop, sz, a, b);
                if (aop != 7) setrm(c, sz, r);
            } else if (form == 2 || form == 3) {     /* Gb,Eb / Gv,Ev */
                int sz = form == 2 ? 1 : size;
                modrm(c);
                a = getreg(c, sz, c->reg); b = getrm(c, sz);
                r = alu(c, aop, sz, a, b);
                if (aop != 7) setreg(c, sz, c->reg, r);
            } else if (form == 4) {                  /* AL,ib */
                b = fetch8(c);
                r = alu(c, aop, 1, getreg(c, 1, EAX), b);
                if (aop != 7) setreg(c, 1, EAX, r);
            } else {                                 /* eAX,iv */
                b = size == 4 ? fetch32(c) : fetch16(c);
                r = alu(c, aop, size, getreg(c, size, EAX), b);
                if (aop != 7) setreg(c, size, EAX, r);
            }
            continue;
        }

        switch (op) {
        case 0x0F: {
            uint8_t op2 = fetch8(c);
            if (op2 >= 0x80 && op2 <= 0x8F) {        /* jcc rel32 */
                int32_t d = (int32_t)fetch32(c);
                if (cond(c, op2 & 15)) c->eip += d;
                break;
            }
            if (op2 >= 0x90 && op2 <= 0x9F) { modrm(c); setrm(c, 1, cond(c, op2 & 15)); break; }
            if (op2 >= 0x40 && op2 <= 0x4F) {        /* cmovcc */
                modrm(c);
                { uint32_t v = getrm(c, size); if (cond(c, op2 & 15)) setreg(c, size, c->reg, v); }
                break;
            }
            switch (op2) {
            case 0xB6: modrm(c); setreg(c, size, c->reg, getrm(c, 1)); break;
            case 0xB7: modrm(c); setreg(c, size, c->reg, getrm(c, 2)); break;
            case 0xBE: modrm(c); setreg(c, size, c->reg, (uint32_t)(int32_t)(int8_t)getrm(c, 1)); break;
            case 0xBF: modrm(c); setreg(c, size, c->reg, (uint32_t)(int32_t)(int16_t)getrm(c, 2)); break;
            case 0xAF: {                              /* imul Gv,Ev */
                modrm(c);
                int64_t p = (int64_t)(int32_t)getreg(c, 4, c->reg) * (int32_t)getrm(c, 4);
                if (size == 2) p = (int64_t)(int16_t)getreg(c, 2, c->reg) * (int16_t)getrm(c, 2);
                setreg(c, size, c->reg, (uint32_t)p);
                c->cf = c->of = size == 2 ? (p != (int16_t)p) : (p != (int32_t)p);
                break;
            }
            case 0xA2: {                              /* cpuid: a Pentium III with SSE */
                uint32_t leaf = c->r[EAX];
                if (leaf == 0) { c->r[EAX] = 2; c->r[EBX] = 0x756E6547; c->r[EDX] = 0x49656E69; c->r[ECX] = 0x6C65746E; }
                else { c->r[EAX] = 0x683; c->r[EBX] = 0; c->r[ECX] = 0; c->r[EDX] = 0x0383F9FF; }
                break;
            }
            case 0x31: {                              /* rdtsc */
                LARGE_INTEGER t; QueryPerformanceCounter(&t);
                c->r[EAX] = (uint32_t)t.QuadPart; c->r[EDX] = (uint32_t)(t.QuadPart >> 32);
                break;
            }
            case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {   /* bt/bts/btr/btc */
                int kind;
                uint32_t bit, addr_word, v;
                modrm(c);
                if (op2 == 0xBA) { kind = c->reg & 3; bit = fetch8(c); }
                else { kind = op2 == 0xA3 ? 0 : op2 == 0xAB ? 1 : op2 == 0xB3 ? 2 : 3; bit = getreg(c, size, c->reg); }
                if (c->mod == 3 || op2 == 0xBA) {
                    bit &= size * 8 - 1;
                    v = getrm(c, size);
                    c->cf = (v >> bit) & 1;
                    if (kind == 1) v |= 1u << bit; else if (kind == 2) v &= ~(1u << bit); else if (kind == 3) v ^= 1u << bit;
                    if (kind) setrm(c, size, v);
                } else {                              /* bit string in memory */
                    int32_t sb = (int32_t)bit;
                    addr_word = c->ea + (uint32_t)((sb >> 5) * 4);
                    bit = (uint32_t)sb & 31;
                    v = rd32(addr_word);
                    c->cf = (v >> bit) & 1;
                    if (kind == 1) v |= 1u << bit; else if (kind == 2) v &= ~(1u << bit); else if (kind == 3) v ^= 1u << bit;
                    if (kind) wr32(addr_word, v);
                }
                break;
            }
            case 0xBC: case 0xBD: {                   /* bsf / bsr */
                modrm(c);
                uint32_t v = getrm(c, size);
                c->zf = v == 0;
                if (v) {
                    unsigned long idx;
                    if (op2 == 0xBC) _BitScanForward(&idx, v); else _BitScanReverse(&idx, v);
                    setreg(c, size, c->reg, idx);
                }
                break;
            }
            case 0xA4: case 0xA5: case 0xAC: case 0xAD: {   /* shld / shrd */
                modrm(c);
                unsigned n = (op2 & 1) ? (c->r[ECX] & 31) : (fetch8(c) & 31);
                uint32_t d = getrm(c, size), s = getreg(c, size, c->reg), r;
                int bits = size * 8;
                if (n) {
                    if (op2 <= 0xA5) {
                        uint64_t w = ((uint64_t)d << bits) | s;
                        r = (uint32_t)((w << n) >> bits) & mask_of(size);
                        c->cf = (d >> (bits - n)) & 1;
                    } else {
                        uint64_t w = ((uint64_t)s << bits) | d;
                        r = (uint32_t)(w >> n) & mask_of(size);
                        c->cf = (d >> (n - 1)) & 1;
                    }
                    c->of = ((r ^ d) & sign_of(size)) != 0;
                    szp(c, size, r);
                    setrm(c, size, r);
                }
                break;
            }
            case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
                uint32_t v = c->r[op2 - 0xC8];
                c->r[op2 - 0xC8] = _byteswap_ulong(v);
                break;
            }
            case 0xC0: case 0xC1: {                   /* xadd */
                int sz = op2 == 0xC0 ? 1 : size;
                modrm(c);
                uint32_t d = getrm(c, sz), s = getreg(c, sz, c->reg);
                uint32_t r = alu(c, 0, sz, d, s);
                setreg(c, sz, c->reg, d);
                setrm(c, sz, r);
                break;
            }
            case 0xB0: case 0xB1: {                   /* cmpxchg */
                int sz = op2 == 0xB0 ? 1 : size;
                modrm(c);
                uint32_t d = getrm(c, sz);
                alu(c, 7, sz, getreg(c, sz, EAX), d);
                if (c->zf) setrm(c, sz, getreg(c, sz, c->reg));
                else setreg(c, sz, EAX, d);
                break;
            }
            case 0xC7: {                              /* cmpxchg8b */
                modrm(c);
                uint64_t m = ((uint64_t)rd32(c->ea + 4) << 32) | rd32(c->ea);
                uint64_t e = ((uint64_t)c->r[EDX] << 32) | c->r[EAX];
                if (m == e) { wr32(c->ea, c->r[EBX]); wr32(c->ea + 4, c->r[ECX]); c->zf = 1; }
                else { c->r[EAX] = (uint32_t)m; c->r[EDX] = (uint32_t)(m >> 32); c->zf = 0; }
                break;
            }
            case 0x0B: fatal(c, "ud2"); goto out;
            case 0x18: modrm(c); break;              /* prefetch */
            case 0x1F: modrm(c); break;              /* nop Ev */
            case 0x09: case 0x08: break;              /* wbinvd / invd */
            default:
                if (!interp_0f_sse(c, op2)) { fatal(c, "unknown 0F opcode"); goto out; }
                break;
            }
            break;
        }
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
            int saved = c->cf;
            setreg(c, size, op - 0x40, alu(c, 0, size, getreg(c, size, op - 0x40), 1));
            c->cf = saved;
            break;
        }
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            int saved = c->cf;
            setreg(c, size, op - 0x48, alu(c, 5, size, getreg(c, size, op - 0x48), 1));
            c->cf = saved;
            break;
        }
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            push32(c, c->r[op - 0x50]);
            break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
            uint32_t v = pop32(c);
            c->r[op - 0x58] = v;
            break;
        }
        case 0x60: {                                  /* pushad */
            uint32_t sp = c->r[ESP];
            push32(c, c->r[EAX]); push32(c, c->r[ECX]); push32(c, c->r[EDX]); push32(c, c->r[EBX]);
            push32(c, sp); push32(c, c->r[EBP]); push32(c, c->r[ESI]); push32(c, c->r[EDI]);
            break;
        }
        case 0x61:                                    /* popad */
            c->r[EDI] = pop32(c); c->r[ESI] = pop32(c); c->r[EBP] = pop32(c); c->r[ESP] += 4;
            c->r[EBX] = pop32(c); c->r[EDX] = pop32(c); c->r[ECX] = pop32(c); c->r[EAX] = pop32(c);
            break;
        case 0x68: push32(c, fetch32(c)); break;
        case 0x6A: push32(c, (uint32_t)(int32_t)(int8_t)fetch8(c)); break;
        case 0x69: case 0x6B: {                       /* imul Gv,Ev,imm */
            modrm(c);
            int32_t b = op == 0x69 ? (size == 4 ? (int32_t)fetch32(c) : (int16_t)fetch16(c)) : (int8_t)fetch8(c);
            int64_t p = size == 4 ? (int64_t)(int32_t)getrm(c, 4) * b : (int64_t)(int16_t)getrm(c, 2) * b;
            setreg(c, size, c->reg, (uint32_t)p);
            c->cf = c->of = size == 2 ? (p != (int16_t)p) : (p != (int32_t)p);
            break;
        }
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int8_t d = (int8_t)fetch8(c);
            if (cond(c, op & 15)) c->eip += d;
            break;
        }
        case 0x80: case 0x81: case 0x82: case 0x83: {
            int sz = (op == 0x80 || op == 0x82) ? 1 : size;
            modrm(c);
            uint32_t b = op == 0x81 ? (size == 4 ? fetch32(c) : fetch16(c)) : (uint32_t)(int32_t)(int8_t)fetch8(c);
            uint32_t r = alu(c, c->reg, sz, getrm(c, sz), b);
            if (c->reg != 7) setrm(c, sz, r);
            break;
        }
        case 0x84: case 0x85: {
            int sz = op == 0x84 ? 1 : size;
            modrm(c);
            alu(c, 4, sz, getrm(c, sz), getreg(c, sz, c->reg));
            break;
        }
        case 0x86: case 0x87: {
            int sz = op == 0x86 ? 1 : size;
            modrm(c);
            uint32_t a = getrm(c, sz), b = getreg(c, sz, c->reg);
            setrm(c, sz, b); setreg(c, sz, c->reg, a);
            break;
        }
        case 0x88: modrm(c); setrm(c, 1, getreg(c, 1, c->reg)); break;
        case 0x89: modrm(c); setrm(c, size, getreg(c, size, c->reg)); break;
        case 0x8A: modrm(c); setreg(c, 1, c->reg, getrm(c, 1)); break;
        case 0x8B: modrm(c); setreg(c, size, c->reg, getrm(c, size)); break;
        case 0x8C: modrm(c); setrm(c, 2, 0); break;   /* mov Ew, Sreg: flat segments */
        case 0x8E: modrm(c); break;                   /* mov Sreg, Ew */
        case 0x8D: {
            int fs = c->seg_fs;
            c->seg_fs = 0;
            modrm(c);
            c->seg_fs = fs;
            setreg(c, size, c->reg, c->ea);
            break;
        }
        case 0x8F: { uint32_t v = pop32(c); modrm(c); setrm(c, 4, v); break; }
        case 0x90: break;
        case 0x9B: break;                         /* fwait: x87 faults are masked */
        /* Port I/O: the converted code skips it too (RECOMP_TODO). */
        case 0xE4: case 0xE5: case 0xE6: case 0xE7: c->eip++; break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF: break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
            uint32_t a = getreg(c, size, EAX);
            setreg(c, size, EAX, getreg(c, size, op - 0x90));
            setreg(c, size, op - 0x90, a);
            break;
        }
        case 0x98:
            if (c->opsize16) setreg(c, 2, EAX, (uint32_t)(int16_t)(int8_t)c->r[EAX]);
            else c->r[EAX] = (uint32_t)(int32_t)(int16_t)c->r[EAX];
            break;
        case 0x99:
            if (c->opsize16) setreg(c, 2, EDX, (c->r[EAX] & 0x8000) ? 0xFFFF : 0);
            else c->r[EDX] = (c->r[EAX] & 0x80000000u) ? 0xFFFFFFFFu : 0;
            break;
        case 0x9C: push32(c, get_eflags(c)); break;
        case 0x9D: set_eflags(c, pop32(c)); break;
        case 0x9E: {                                  /* sahf */
            uint32_t ah = (c->r[EAX] >> 8) & 0xFF;
            c->cf = ah & 1; c->pf = (ah >> 2) & 1; c->af = (ah >> 4) & 1; c->zf = (ah >> 6) & 1; c->sf = (ah >> 7) & 1;
            break;
        }
        case 0x9F:                                    /* lahf */
            setreg(c, 1, 4, get_eflags(c) & 0xFF);
            break;
        case 0xA0: setreg(c, 1, EAX, rd8(seg(c, fetch32(c)))); break;
        case 0xA1: { uint32_t a = seg(c, fetch32(c)); setreg(c, size, EAX, size == 4 ? rd32(a) : rd16(a)); break; }
        case 0xA2: wr8(seg(c, fetch32(c)), (uint8_t)c->r[EAX]); break;
        case 0xA3: { uint32_t a = seg(c, fetch32(c)); if (size == 4) wr32(a, c->r[EAX]); else wr16(a, (uint16_t)c->r[EAX]); break; }
        case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            string_op(c, op);
            break;
        case 0xA8: alu(c, 4, 1, c->r[EAX], fetch8(c)); break;
        case 0xA9: alu(c, 4, size, c->r[EAX], size == 4 ? fetch32(c) : fetch16(c)); break;
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            setreg(c, 1, op - 0xB0, fetch8(c));
            break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            setreg(c, size, op - 0xB8, size == 4 ? fetch32(c) : fetch16(c));
            break;
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            int sz = (op == 0xC0 || op == 0xD0 || op == 0xD2) ? 1 : size;
            modrm(c);
            unsigned n = (op == 0xC0 || op == 0xC1) ? fetch8(c) : (op == 0xD0 || op == 0xD1) ? 1 : (c->r[ECX] & 0xFF);
            setrm(c, sz, shift(c, c->reg, sz, getrm(c, sz), n));
            break;
        }
        case 0xC2: case 0xC3: {
            uint32_t n = op == 0xC2 ? fetch16(c) : 0;
            uint32_t at = c->r[ESP];
            uint32_t ret = pop32(c);
            c->r[ESP] += n;
            if (at >= esp0) {                         /* the frame we were entered with */
                sync_out(c);
                if (s_interp_log) fprintf(stderr, "[INTERP] return to %08X\n", ret);
                return;
            }
            c->eip = ret;
            break;
        }
        case 0xC6: modrm(c); setrm(c, 1, fetch8(c)); break;
        case 0xC7: modrm(c); setrm(c, size, size == 4 ? fetch32(c) : fetch16(c)); break;
        case 0xC8: {                                  /* enter */
            uint16_t frame = fetch16(c);
            uint8_t level = fetch8(c);
            push32(c, c->r[EBP]);
            { uint32_t fp = c->r[ESP];
              for (int i = 1; i < level; i++) { c->r[EBP] -= 4; push32(c, rd32(c->r[EBP])); }
              if (level) push32(c, fp);
              c->r[EBP] = fp; }
            c->r[ESP] -= frame;
            break;
        }
        case 0xC9: c->r[ESP] = c->r[EBP]; c->r[EBP] = pop32(c); break;
        case 0xCC: fatal(c, "int3"); goto out;
        case 0xCD: fatal(c, "int"); goto out;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            if (!interp_x87(c, op)) { fatal(c, "unknown x87 opcode"); goto out; }
            break;
        case 0xE0: case 0xE1: case 0xE2: {            /* loopne / loope / loop */
            int8_t d = (int8_t)fetch8(c);
            c->r[ECX]--;
            if (c->r[ECX] && (op == 0xE2 || (op == 0xE1 && c->zf) || (op == 0xE0 && !c->zf))) c->eip += d;
            break;
        }
        case 0xE3: { int8_t d = (int8_t)fetch8(c); if (!c->r[ECX]) c->eip += d; break; }
        case 0xE8: { int32_t d = (int32_t)fetch32(c); target = c->eip + d; goto do_call; }
        case 0xE9: { int32_t d = (int32_t)fetch32(c); target = c->eip + d; goto do_jmp; }
        case 0xEB: { int8_t d = (int8_t)fetch8(c); c->eip += d; break; }
        case 0xF4: fatal(c, "hlt"); goto out;
        case 0xF5: c->cf = !c->cf; break;
        case 0xF6: case 0xF7: {
            int sz = op == 0xF6 ? 1 : size;
            modrm(c);
            uint32_t v = getrm(c, sz);
            switch (c->reg) {
            case 0: case 1: alu(c, 4, sz, v, sz == 1 ? fetch8(c) : sz == 2 ? fetch16(c) : fetch32(c)); break;
            case 2: setrm(c, sz, ~v); break;
            case 3: { uint32_t r = alu(c, 5, sz, 0, v); c->cf = (v & mask_of(sz)) != 0; setrm(c, sz, r); break; }
            case 4: {                                 /* mul */
                if (sz == 1) { uint32_t p = (c->r[EAX] & 0xFF) * v; setreg(c, 2, EAX, p); c->cf = c->of = (p >> 8) != 0; }
                else if (sz == 2) { uint32_t p = (c->r[EAX] & 0xFFFF) * v; setreg(c, 2, EAX, p); setreg(c, 2, EDX, p >> 16); c->cf = c->of = (p >> 16) != 0; }
                else { uint64_t p = (uint64_t)c->r[EAX] * v; c->r[EAX] = (uint32_t)p; c->r[EDX] = (uint32_t)(p >> 32); c->cf = c->of = c->r[EDX] != 0; }
                break;
            }
            case 5: {                                 /* imul */
                if (sz == 1) { int32_t p = (int8_t)c->r[EAX] * (int8_t)v; setreg(c, 2, EAX, (uint32_t)p); c->cf = c->of = p != (int8_t)p; }
                else if (sz == 2) { int32_t p = (int16_t)c->r[EAX] * (int16_t)v; setreg(c, 2, EAX, (uint32_t)p); setreg(c, 2, EDX, (uint32_t)p >> 16); c->cf = c->of = p != (int16_t)p; }
                else { int64_t p = (int64_t)(int32_t)c->r[EAX] * (int32_t)v; c->r[EAX] = (uint32_t)p; c->r[EDX] = (uint32_t)((uint64_t)p >> 32); c->cf = c->of = p != (int32_t)p; }
                break;
            }
            case 6: {                                 /* div */
                if (!v) { fatal(c, "divide by zero"); goto out; }
                if (sz == 1) { uint32_t n = c->r[EAX] & 0xFFFF; setreg(c, 1, EAX, n / v); setreg(c, 1, 4, n % v); }
                else if (sz == 2) { uint32_t n = ((c->r[EDX] & 0xFFFF) << 16) | (c->r[EAX] & 0xFFFF); setreg(c, 2, EAX, n / v); setreg(c, 2, EDX, n % v); }
                else { uint64_t n = ((uint64_t)c->r[EDX] << 32) | c->r[EAX]; c->r[EAX] = (uint32_t)(n / v); c->r[EDX] = (uint32_t)(n % v); }
                break;
            }
            default: {                                /* idiv */
                if (!v) { fatal(c, "divide by zero"); goto out; }
                if (sz == 1) { int32_t n = (int16_t)c->r[EAX]; setreg(c, 1, EAX, (uint32_t)(n / (int8_t)v)); setreg(c, 1, 4, (uint32_t)(n % (int8_t)v)); }
                else if (sz == 2) { int32_t n = (int32_t)(((c->r[EDX] & 0xFFFF) << 16) | (c->r[EAX] & 0xFFFF)); setreg(c, 2, EAX, (uint32_t)(n / (int16_t)v)); setreg(c, 2, EDX, (uint32_t)(n % (int16_t)v)); }
                else { int64_t n = (int64_t)(((uint64_t)c->r[EDX] << 32) | c->r[EAX]); c->r[EAX] = (uint32_t)(n / (int32_t)v); c->r[EDX] = (uint32_t)(n % (int32_t)v); }
                break;
            }
            }
            break;
        }
        case 0xF8: c->cf = 0; break;
        case 0xF9: c->cf = 1; break;
        case 0xFA: case 0xFB: break;                  /* cli / sti */
        case 0xFC: g_df = 0; break;
        case 0xFD: g_df = 1; break;
        case 0xFE: {
            modrm(c);
            int saved = c->cf;
            if (c->reg == 0) setrm(c, 1, alu(c, 0, 1, getrm(c, 1), 1));
            else if (c->reg == 1) setrm(c, 1, alu(c, 5, 1, getrm(c, 1), 1));
            else { fatal(c, "unknown FE /r"); goto out; }
            c->cf = saved;
            break;
        }
        case 0xFF: {
            modrm(c);
            switch (c->reg) {
            case 0: { int saved = c->cf; setrm(c, size, alu(c, 0, size, getrm(c, size), 1)); c->cf = saved; break; }
            case 1: { int saved = c->cf; setrm(c, size, alu(c, 5, size, getrm(c, size), 1)); c->cf = saved; break; }
            case 2: target = getrm(c, 4); goto do_call;
            case 4: target = getrm(c, 4); goto do_jmp;
            case 6: push32(c, getrm(c, size)); break;
            default: fatal(c, "unknown FF /r"); goto out;
            }
            break;
        }
        case 0x27: case 0x2F: case 0x37: case 0x3F: fatal(c, "BCD instruction"); goto out;
        default:
            fatal(c, "unknown opcode");
            goto out;
        }
        continue;

    do_call: {
            uint32_t ret = c->eip;
            recomp_func_t fn = native_for(target);
            push32(c, ret);
            if (fn) {
                sync_out(c);
                fn();
                sync_in(c);
                if (c->r[ESP] > esp0) {           /* the callee returned past our frame */
                    sync_out(c);
                    return;
                }
                c->eip = ret;
            } else {
                c->eip = target;
            }
            continue;
        }
    do_jmp: {
            recomp_func_t fn = native_for(target);
            if (fn) {
                /* A jump to a converted function's start is a tail call: it
                 * returns through the return address on top of our stack, to
                 * whoever pushed it. */
                uint32_t at = c->r[ESP];
                uint32_t ret = rd32(at);
                sync_out(c);
                fn();
                sync_in(c);
                if (at >= esp0 || c->r[ESP] > esp0) { sync_out(c); return; }
                c->eip = ret;
            } else {
                c->eip = target;
            }
            continue;
        }
    }
out:
    /* The guest did something this file cannot run. Give back what we have and
     * return to the caller; the game will most likely misbehave from here, and
     * the log says where. */
    InterlockedIncrement(&s_unknown_reported);
    sync_out(c);
    g_esp = esp0 + 4;
}

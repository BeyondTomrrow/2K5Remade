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
    uint32_t r[9];          /* r[8] is always 0: a decoded address with no base or index */
    uint32_t eip;
    int cf, pf, af, zf, sf, of;
    /* Lazy flags: after an ALU operation zf/sf/pf/af/of are not stored but
     * derived from the operation on demand (cf always is). lk is LK_NONE when
     * the six fields above are all current; mat() makes them so. */
    int lk, lsz;
    uint32_t la, lb, lr;
    /* decode state for the current instruction */
    uint32_t start;         /* address of the instruction */
    int opsize16;           /* 0x66 */
    int rep;                /* 0xF3 = 1, 0xF2 = 2 */
    int seg_fs;             /* 0x64 */
    int sse_prefix;         /* last of 0x66/0xF2/0xF3 seen, for SSE */
    /* ModRM */
    int mod, reg, rm;
    uint32_t ea;            /* effective address when mod != 3 */
    /* profiling (NFL2K5_INTERP_PROF) */
    uint64_t icount;
    int64_t native_ticks;
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

enum { LK_NONE, LK_ADD, LK_SUB, LK_LOGIC };

/* Materialise zf/sf/pf/af/of from the lazy record. */
static inline void mat(Cpu *c)
{
    int k = c->lk;
    if (k == LK_NONE) return;
    {
        uint32_t s = sign_of(c->lsz), a = c->la, b = c->lb, r = c->lr;
        c->zf = r == 0;
        c->sf = (r & s) != 0;
        c->pf = recomp_parity8(r);
        if (k == LK_LOGIC) { c->af = 0; c->of = 0; }
        else {
            c->af = ((a ^ b ^ r) & 0x10) != 0;
            c->of = k == LK_ADD ? ((~(a ^ b) & (a ^ r)) & s) != 0 : (((a ^ b) & (a ^ r)) & s) != 0;
        }
    }
    c->lk = LK_NONE;
}

static inline int flag_of(Cpu *c)
{
    uint32_t s, a, b, r;
    if (c->lk == LK_NONE) return c->of;
    if (c->lk == LK_LOGIC) return 0;
    s = sign_of(c->lsz); a = c->la; b = c->lb; r = c->lr;
    return c->lk == LK_ADD ? ((~(a ^ b) & (a ^ r)) & s) != 0 : (((a ^ b) & (a ^ r)) & s) != 0;
}
static inline int flag_zf(Cpu *c) { return c->lk == LK_NONE ? c->zf : c->lr == 0; }
static inline int flag_sf(Cpu *c) { return c->lk == LK_NONE ? c->sf : (c->lr & sign_of(c->lsz)) != 0; }
static inline int flag_pf(Cpu *c) { return c->lk == LK_NONE ? c->pf : recomp_parity8(c->lr); }

static inline uint32_t alu(Cpu *c, int op, int size, uint32_t a, uint32_t b)
{
    uint32_t m = mask_of(size), r = 0;
    a &= m; b &= m;
    switch (op) {
    case 0: /* add */
    case 2: { /* adc */
        uint32_t cin = op == 2 ? (uint32_t)c->cf : 0;
        uint64_t w = (uint64_t)a + b + cin;
        r = (uint32_t)w & m;
        c->cf = (w >> (size * 8)) & 1;
        c->lk = LK_ADD;
        break;
    }
    case 5: /* sub */
    case 3: /* sbb */
    case 7: { /* cmp */
        uint32_t cin = op == 3 ? (uint32_t)c->cf : 0;
        uint64_t w = (uint64_t)a - b - cin;
        r = (uint32_t)w & m;
        c->cf = ((uint64_t)a < (uint64_t)b + cin);
        c->lk = LK_SUB;
        break;
    }
    case 1: r = a | b; c->cf = 0; c->lk = LK_LOGIC; break;
    case 4: r = a & b; c->cf = 0; c->lk = LK_LOGIC; break;
    default: r = a ^ b; c->cf = 0; c->lk = LK_LOGIC; break;
    }
    c->la = a; c->lb = b; c->lr = r; c->lsz = size;
    return r;
}

static inline int cond(Cpu *c, int cc)
{
    if (c->lk != LK_NONE) {
        switch (cc & 15) {
        case 0: return flag_of(c);                case 1: return !flag_of(c);
        case 2: return c->cf;                     case 3: return !c->cf;
        case 4: return c->lr == 0;                case 5: return c->lr != 0;
        case 6: return c->cf || c->lr == 0;       case 7: return !c->cf && c->lr != 0;
        case 8: return flag_sf(c);                case 9: return !flag_sf(c);
        case 10: return recomp_parity8(c->lr);    case 11: return !recomp_parity8(c->lr);
        case 12: return flag_sf(c) != flag_of(c); case 13: return flag_sf(c) == flag_of(c);
        case 14: return c->lr == 0 || flag_sf(c) != flag_of(c);
        default: return c->lr != 0 && flag_sf(c) == flag_of(c);
        }
    }
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
    mat(c);
    return 0x202u | (c->cf ? 1u : 0) | (c->pf ? 4u : 0) | (c->af ? 0x10u : 0) | (c->zf ? 0x40u : 0) |
           (c->sf ? 0x80u : 0) | (g_df ? 0x400u : 0) | (c->of ? 0x800u : 0);
}

static void set_eflags(Cpu *c, uint32_t f)
{
    c->lk = LK_NONE;
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
/* One bit per hashed instruction address: clear = no hook there (the loop
 * tests it for every instruction). */
static uint8_t s_hook_bits[8192];
/* Bumped by every recomp_interp_add_hook: decoded instructions carry their
 * hook bit, so the per-thread caches start over (x86_interp.c, DInsn.hook). */
static volatile long s_hook_gen;
static inline uint32_t hook_hash(uint32_t va) { return (va ^ (va >> 16)) & 0xFFFFu; }

void recomp_interp_add_hook(uint32_t va, uint32_t from, recomp_interp_hook_t fn)
{
    if (s_nhooks >= (int)(sizeof s_hooks / sizeof s_hooks[0]) || !fn) return;
    s_hooks[s_nhooks].va = va;
    s_hooks[s_nhooks].from = from;
    s_hooks[s_nhooks].fn = fn;
    s_nhooks++;
    s_hook_bits[hook_hash(va) >> 3] |= (uint8_t)(1u << (hook_hash(va) & 7));
    s_hook_gen++;
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

/* Native versions of a known pack's hottest functions (tools/gen-pack-natives.py,
 * interp_setup.c): filled at the first interpreted call, each entry only if the
 * guest bytes are the ones it was generated from. */
#define PN_MAX 16
int nfl2k5_pack_natives_init(uint32_t *va, void (**fn)(void), int max);
static uint32_t s_pn_va[PN_MAX];
static recomp_func_t s_pn_fn[PN_MAX];
static int s_npn, s_pn_check;

static inline recomp_func_t pn_lookup(uint32_t va)
{
    for (int i = 0; i < s_npn; i++)
        if (s_pn_va[i] == va) return s_pn_fn[i];
    return NULL;
}

/* The native function for `va`, or NULL when it must be interpreted. */
static recomp_func_t native_for(uint32_t va)
{
    recomp_func_t fn = pn_lookup(va);
    if (fn) return fn;
    if (va >= 0xFE000000u) {
        /* the kernel thunk bridge, else the menu functions served from C (Video Settings,
         * the Main Menu's Exit prompt: 0xFEC0.., 0xFED0.., 0xFEE0..; NATIVE-MENU 2.2) */
        fn = recomp_lookup_kernel(va);
        return fn ? fn : recomp_lookup_manual(va);
    }
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
static int interp_x87_body(Cpu *c, uint8_t op);
int interp_0f_sse(Cpu *c, uint8_t op2);

#include "x86_interp_fpu.inc.c"

/* One-byte opcodes whose handlers touch zf/sf/pf/af/of only through alu() and
 * cond() (or not at all), so lazy flags may stay pending across them. Every
 * other instruction materialises the flags first. 0F and F6/F7 decide inside. */
static const uint8_t s_lazy_ok[256] = {
    [0x0F] = 1,
    [0x40] = 1, [0x41] = 1, [0x42] = 1, [0x43] = 1, [0x44] = 1, [0x45] = 1, [0x46] = 1, [0x47] = 1,
    [0x48] = 1, [0x49] = 1, [0x4A] = 1, [0x4B] = 1, [0x4C] = 1, [0x4D] = 1, [0x4E] = 1, [0x4F] = 1,
    [0x50] = 1, [0x51] = 1, [0x52] = 1, [0x53] = 1, [0x54] = 1, [0x55] = 1, [0x56] = 1, [0x57] = 1,
    [0x58] = 1, [0x59] = 1, [0x5A] = 1, [0x5B] = 1, [0x5C] = 1, [0x5D] = 1, [0x5E] = 1, [0x5F] = 1,
    [0x68] = 1, [0x6A] = 1,
    [0x70] = 1, [0x71] = 1, [0x72] = 1, [0x73] = 1, [0x74] = 1, [0x75] = 1, [0x76] = 1, [0x77] = 1,
    [0x78] = 1, [0x79] = 1, [0x7A] = 1, [0x7B] = 1, [0x7C] = 1, [0x7D] = 1, [0x7E] = 1, [0x7F] = 1,
    [0x80] = 1, [0x81] = 1, [0x82] = 1, [0x83] = 1, [0x84] = 1, [0x85] = 1, [0x86] = 1, [0x87] = 1,
    [0x88] = 1, [0x89] = 1, [0x8A] = 1, [0x8B] = 1, [0x8C] = 1, [0x8D] = 1, [0x8E] = 1, [0x8F] = 1,
    [0x90] = 1, [0x91] = 1, [0x92] = 1, [0x93] = 1, [0x94] = 1, [0x95] = 1, [0x96] = 1, [0x97] = 1,
    [0x98] = 1, [0x99] = 1,
    [0xA0] = 1, [0xA1] = 1, [0xA2] = 1, [0xA3] = 1, [0xA8] = 1, [0xA9] = 1,
    [0xB0] = 1, [0xB1] = 1, [0xB2] = 1, [0xB3] = 1, [0xB4] = 1, [0xB5] = 1, [0xB6] = 1, [0xB7] = 1,
    [0xB8] = 1, [0xB9] = 1, [0xBA] = 1, [0xBB] = 1, [0xBC] = 1, [0xBD] = 1, [0xBE] = 1, [0xBF] = 1,
    [0xC2] = 1, [0xC3] = 1, [0xC6] = 1, [0xC7] = 1, [0xC8] = 1, [0xC9] = 1,
    [0xE8] = 1, [0xE9] = 1, [0xEB] = 1,
    [0xF6] = 1, [0xF7] = 1, [0xFE] = 1, [0xFF] = 1,
};

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
            if (rep == 1 && !flag_zf(c)) break;
            if (rep == 2 && flag_zf(c)) break;
        }
    }
}


/* ---- profiling (NFL2K5_INTERP_PROF=1, diagnostics) ------------------------------
 * Every 10 s: the interpreted entry points by self time (time not spent in
 * native callees), the hottest instruction addresses, the native callees and
 * the native callers that enter the interpreter. */
static int s_prof = -1;
/* s_slow: any per-instruction diagnostic is on (trace or profile). */
static int s_slow, s_tr_on;
static uint32_t s_tr_lo, s_tr_hi;
static volatile LONG s_prof_lock;
typedef struct { uint32_t va, aux; uint64_t calls, instrs; int64_t incl, native; } ProfEnt;
#define PROF_N 8192
static ProfEnt s_pe[PROF_N];       /* by entry VA */
static ProfEnt s_pn[PROF_N];       /* native callees by VA */
static ProfEnt s_pc[PROF_N];       /* native callers (return address) of interpreter entries */
typedef struct { uint32_t va; uint32_t n; } ProfInsn;
static ProfInsn s_pi[1 << 16];     /* instruction start VAs */
static int64_t s_prof_last, s_prof_top_ticks, s_prof_freq;
static uint64_t s_prof_entries;
static XBOX_THREAD_LOCAL int t_prof_depth;

static inline int64_t prof_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static ProfEnt *prof_slot(ProfEnt *tab, uint32_t va)
{
    uint32_t h = (va * 2654435761u) >> 19;   /* 13 bits */
    for (unsigned k = 0; k < PROF_N; k++) {
        ProfEnt *e = &tab[(h + k) & (PROF_N - 1)];
        if (e->va == va) return e;
        if (!e->va) { e->va = va; return e; }
    }
    return &tab[0];
}

static inline void prof_insn(uint32_t va)
{
    uint32_t h = (va * 2654435761u) >> 16;
    for (unsigned k = 0; k < 64; k++) {
        uint32_t i = (h + k) & 0xFFFF;
        if (s_pi[i].va == va) { s_pi[i].n++; return; }
        if (!s_pi[i].va) { s_pi[i].va = va; s_pi[i].n = 1; return; }
    }
}

static void prof_lock(void) { while (InterlockedExchange(&s_prof_lock, 1)) { } }
static void prof_unlock(void) { InterlockedExchange(&s_prof_lock, 0); }

static int cmp_self(const void *a, const void *b)
{
    const ProfEnt *x = a, *y = b;
    int64_t sx = x->incl - x->native, sy = y->incl - y->native;
    return sx < sy ? 1 : sx > sy ? -1 : 0;
}
static int cmp_calls(const void *a, const void *b)
{
    const ProfEnt *x = a, *y = b;
    return x->calls < y->calls ? 1 : x->calls > y->calls ? -1 : 0;
}
static int cmp_insn(const void *a, const void *b)
{
    const ProfInsn *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}

static void prof_dump(int64_t now)
{
    static ProfEnt tmp[PROF_N];
    static ProfInsn itmp[1 << 16];
    double ms = 1000.0 / (double)s_prof_freq, secs = (double)(now - s_prof_last) / (double)s_prof_freq;
    int64_t self_all = 0, ntick = 0;
    uint64_t instrs = 0, ncalls = 0;
    unsigned i;
    for (i = 0; i < PROF_N; i++) { self_all += s_pe[i].incl - s_pe[i].native; instrs += s_pe[i].instrs; }
    for (i = 0; i < PROF_N; i++) { ncalls += s_pn[i].calls; ntick += s_pn[i].incl; }
    fprintf(stderr, "[IPROF] %.1fs: top-level interp %.1f ms/s, self %.1f ms/s, %llu entries, %.2f Minstr/s, %.1f ns/instr self, %llu native calls (%.1f ms/s incl)\n",
            secs, s_prof_top_ticks * ms / secs, self_all * ms / secs, (unsigned long long)s_prof_entries,
            instrs / secs / 1e6, instrs ? self_all * ms * 1e6 / (double)instrs : 0.0, (unsigned long long)ncalls, ntick * ms / secs);
    memcpy(tmp, s_pe, sizeof tmp);
    qsort(tmp, PROF_N, sizeof tmp[0], cmp_self);
    for (i = 0; i < 30 && tmp[i].va; i++)
        fprintf(stderr, "[IPROF] entry %08X self %6.2f ms/s incl %6.2f ms/s calls %8llu instrs %10llu (%.0f/call)\n", tmp[i].va,
                (tmp[i].incl - tmp[i].native) * ms / secs, tmp[i].incl * ms / secs, (unsigned long long)tmp[i].calls,
                (unsigned long long)tmp[i].instrs, tmp[i].calls ? (double)tmp[i].instrs / tmp[i].calls : 0.0);
    memcpy(tmp, s_pe, sizeof tmp);
    qsort(tmp, PROF_N, sizeof tmp[0], cmp_calls);
    for (i = 0; i < 15 && tmp[i].va; i++)
        fprintf(stderr, "[IPROF] entry-by-calls %08X calls %8llu self %6.2f ms/s instrs/call %.0f\n", tmp[i].va,
                (unsigned long long)tmp[i].calls, (tmp[i].incl - tmp[i].native) * ms / secs,
                tmp[i].calls ? (double)tmp[i].instrs / tmp[i].calls : 0.0);
    memcpy(tmp, s_pn, sizeof tmp);
    qsort(tmp, PROF_N, sizeof tmp[0], cmp_calls);
    for (i = 0; i < 20 && tmp[i].va; i++)
        fprintf(stderr, "[IPROF] native %08X calls %8llu incl %6.2f ms/s\n", tmp[i].va, (unsigned long long)tmp[i].calls, tmp[i].incl * ms / secs);
    memcpy(tmp, s_pc, sizeof tmp);
    qsort(tmp, PROF_N, sizeof tmp[0], cmp_calls);
    for (i = 0; i < 20 && tmp[i].va; i++)
        fprintf(stderr, "[IPROF] caller ret %08X -> entry %08X calls %8llu incl %6.2f ms/s\n", tmp[i].va, tmp[i].aux,
                (unsigned long long)tmp[i].calls, tmp[i].incl * ms / secs);
    memcpy(itmp, s_pi, sizeof itmp);
    qsort(itmp, 1 << 16, sizeof itmp[0], cmp_insn);
    for (i = 0; i < 48 && itmp[i].va; i++)
        fprintf(stderr, "%s %08X:%u%s", i % 8 ? "" : "[IPROF] insn", itmp[i].va, itmp[i].n, i % 8 == 7 ? "\n" : "");
    fprintf(stderr, "\n");
    fflush(stderr);
    memset(s_pe, 0, sizeof s_pe); memset(s_pn, 0, sizeof s_pn); memset(s_pc, 0, sizeof s_pc); memset(s_pi, 0, sizeof s_pi);
    s_prof_top_ticks = 0; s_prof_entries = 0;
    s_prof_last = now;
}

static void prof_native(uint32_t va, int64_t d)
{
    ProfEnt *e;
    prof_lock();
    e = prof_slot(s_pn, va); e->calls++; e->incl += d;
    prof_unlock();
}

static void prof_entry(uint32_t va, uint32_t ret, Cpu *c, int64_t incl, int top)
{
    ProfEnt *e;
    int64_t now;
    prof_lock();
    e = prof_slot(s_pe, va); e->calls++; e->instrs += c->icount; e->incl += incl; e->native += c->native_ticks;
    s_prof_entries++;
    if (top) {
        s_prof_top_ticks += incl;
        e = prof_slot(s_pc, ret); e->aux = va; e->calls++; e->incl += incl;
    }
    now = prof_now();
    if (now - s_prof_last > 10 * s_prof_freq) prof_dump(now);
    prof_unlock();
}


/* ---- decoded-instruction cache -------------------------------------------------
 *
 * Decoding an instruction (prefixes, ModRM, SIB, displacement, immediate) used
 * to happen every time it ran. Each thread now keeps the decoded form of the
 * instructions it runs, keyed by address, and the common instructions run from
 * it directly (the DK_ kinds below); the rest re-enter the generic decoder
 * after the prefixes. An entry holds the instruction's bytes and is used only
 * while guest memory still has exactly those bytes, so code written at run
 * time (packs patch themselves at load) can never run stale. */

enum {
    DK_GENERIC = 0,
    DK_MOV_RM_R, DK_MOV_R_RM, DK_MOV_R_I, DK_MOV_RM_I, DK_LEA,
    DK_ALU_RM_R, DK_ALU_R_RM, DK_ALU_A_I, DK_ALU_RM_I,
    DK_TEST_RM_R, DK_TEST_A_I, DK_TEST_RM_I,
    DK_PUSH_R, DK_POP_R, DK_PUSH_I, DK_INC_R, DK_DEC_R, DK_INCDEC_RM,
    DK_JCC, DK_JMP_SHORT, DK_JMP_NEAR, DK_CALL, DK_RET,
    DK_FF_CALL, DK_FF_JMP, DK_FF_PUSH,
    DK_MOVZX, DK_MOVSX, DK_SETCC, DK_SHIFT, DK_X87, DK_NOP,
};

typedef struct DInsn {
    uint32_t va;            /* tag; 0 = empty */
    uint8_t len;            /* bytes the entry depends on (and the instruction length for DK_ kinds) */
    uint8_t kind, sz, plen; /* plen: prefix bytes */
    uint8_t op, op2, mod, reg, rm;
    uint8_t base, index, scale;   /* base/index 8 = none (Cpu.r[8] is always 0) */
    uint8_t opsize16, rep, seg_fs, sse, sub;
    uint8_t hook;                 /* a native hook may be registered at va */
    uint32_t disp, imm;
} DInsn;

#define DC_BITS 10
/* Decoded straight-line blocks: up to BLK_MAX instructions that run without a
 * control transfer, ending at the first jump/call/return or generic
 * instruction (included as the last entry) or before an instruction with a
 * native hook. A block is used only while guest memory holds exactly the
 * bytes it was decoded from (one memcmp per block run), and stops early when
 * one of its own instructions stores into its bytes. */
#define BLK_MAX 24
#define BLK_BYTES 256
typedef struct Block {
    uint32_t va;                 /* 0 = empty */
    uint16_t n, nbytes;
    DInsn ins[BLK_MAX];
    uint8_t bytes[BLK_BYTES];
} Block;
#define DCACHE_SIZE (1u << DC_BITS)          /* blocks per thread */
static XBOX_THREAD_LOCAL Block *t_dcache;
static XBOX_THREAD_LOCAL long t_dcache_hook_gen;   /* s_hook_gen the cache was decoded under */

/* Record a control transfer in the fault trail. */
#define TRAIL(from, to) do { unsigned tn_ = t_trail_n; t_trail[tn_ & 63] = (from); \
                             t_trail[(tn_ + 1) & 63] = (to); t_trail_n = tn_ + 2; } while (0)
static int s_fast = -1;          /* NFL2K5_INTERP_FAST=0: decode every time, as before */
/* Diagnostics: NFL2K5_INTERP_FAST_KINDS=<hex mask of DK_ kinds run from the
 * cache> (others take the generic path), NFL2K5_INTERP_NCACHE=0. */
static uint64_t s_fast_kinds = ~0ull;
static int s_ncache = 1;

static inline uint32_t dc_index(uint32_t va) { return (va ^ (va >> DC_BITS)) & (DCACHE_SIZE - 1); }

static inline uint64_t rd64u(uint32_t a) { uint64_t v; memcpy(&v, (const void *)XBOX_PTR(a), 8); return v; }

/* Guest memory through a base held in a local: the generic helpers reload
 * g_xbox_mem_offset after every guest store (it might alias). */
#define FM8(a)  (*(volatile uint8_t  *)(mb + (uint32_t)(a)))
#define FM16(a) (*(volatile uint16_t *)(mb + (uint32_t)(a)))
#define FM32(a) (*(volatile uint32_t *)(mb + (uint32_t)(a)))
#define FMF(a)  (*(volatile float    *)(mb + (uint32_t)(a)))
#define FMD(a)  (*(volatile double   *)(mb + (uint32_t)(a)))

static inline uint64_t rd64m(uintptr_t mb, uint32_t a) { uint64_t v; memcpy(&v, (const void *)(mb + a), 8); return v; }


static inline uint32_t d_ea(const Cpu *c, const DInsn *d)
{
    uint32_t a = d->disp + c->r[d->base] + (c->r[d->index] << d->scale);
    return d->seg_fs ? a + g_fs_base : a;
}

static inline uint32_t d_getrm(uintptr_t mb, Cpu *c, const DInsn *d, int sz)
{
    uint32_t a;
    if (d->mod == 3) return getreg(c, sz, d->rm);
    a = d_ea(c, d);
    return sz == 4 ? FM32(a) : sz == 2 ? FM16(a) : FM8(a);
}

/* Returns the address written, or 0xFFFFFF00 for a register (far from any code). */
static inline uint32_t d_setrm(uintptr_t mb, Cpu *c, const DInsn *d, int sz, uint32_t v)
{
    uint32_t a;
    if (d->mod == 3) { setreg(c, sz, d->rm, v); return 0xFFFFFF00u; }
    a = d_ea(c, d);
    if (sz == 4) FM32(a) = v; else if (sz == 2) FM16(a) = (uint16_t)v; else FM8(a) = (uint8_t)v;
    return a;
}

/* ModRM operand bytes at p: fills mod/reg/rm and the address parts.
 * Returns the number of bytes, or 0 for a form not decoded here. */
static unsigned dc_modrm(DInsn *d, uint32_t p)
{
    uint8_t m = rd8(p);
    unsigned n = 1;
    d->mod = m >> 6; d->reg = (m >> 3) & 7; d->rm = m & 7;
    d->base = 8; d->index = 8; d->scale = 0; d->disp = 0;
    if (d->mod == 3) return 1;
    if (d->rm == 4) {
        uint8_t sib = rd8(p + 1);
        int b = sib & 7, idx = (sib >> 3) & 7;
        n = 2;
        d->scale = sib >> 6;
        if (idx != 4) d->index = (uint8_t)idx;
        if (b == 5 && d->mod == 0) { d->disp = rd32(p + 2); n = 6; }
        else d->base = (uint8_t)b;
    } else if (d->rm == 5 && d->mod == 0) {
        d->disp = rd32(p + 1); n = 5;
    } else {
        d->base = d->rm;
    }
    if (d->mod == 1) { d->disp += (uint32_t)(int32_t)(int8_t)rd8(p + n); n += 1; }
    else if (d->mod == 2) { d->disp += rd32(p + n); n += 4; }
    return n;
}

/* Decode the instruction at va into d. Unhandled forms become DK_GENERIC,
 * which depend only on their prefix and opcode bytes. Returns 0 when the
 * instruction cannot be cached at all (too many prefixes). */
static int dc_decode(DInsn *d, uint32_t va)
{
    uint32_t p = va;
    unsigned n, mlen;
    uint8_t op;
    memset(d, 0, sizeof *d);
    for (n = 0;; n++) {
        if (n >= 8) return 0;
        op = rd8(p++);
        if (op == 0x66) { d->opsize16 = 1; d->sse = 0x66; continue; }
        if (op == 0xF3) { d->rep = 1; d->sse = 0xF3; continue; }
        if (op == 0xF2) { d->rep = 2; d->sse = 0xF2; continue; }
        if (op == 0x64) { d->seg_fs = 1; continue; }
        if (op == 0xF0 || op == 0x2E || op == 0x3E || op == 0x26 || op == 0x36 || op == 0x65 || op == 0x67) continue;
        break;
    }
    d->va = va; d->plen = (uint8_t)n; d->op = op;
    {
        uint32_t h = hook_hash(va);
        d->hook = (uint8_t)((s_hook_bits[h >> 3] >> (h & 7)) & 1);
    }
    d->len = (uint8_t)(n + 1);            /* DK_GENERIC: prefixes + opcode */
    d->sz = d->opsize16 ? 2 : 4;
    d->base = 8; d->index = 8;
    /* Only plain 32-bit forms run from the cache; 16-bit and rep forms stay generic. */
    if (!d->opsize16 && !d->rep) {
        unsigned total = 0;
        if (op < 0x40 && (op & 7) < 6) {
            int form = op & 7;
            d->sub = op >> 3;
            if (form <= 3) {
                mlen = dc_modrm(d, p);
                d->sz = (form & 1) ? 4 : 1;
                d->kind = form <= 1 ? DK_ALU_RM_R : DK_ALU_R_RM;
                total = n + 1 + mlen;
            } else if (form == 4) { d->sz = 1; d->imm = rd8(p); d->kind = DK_ALU_A_I; total = n + 2; }
            else { d->imm = rd32(p); d->kind = DK_ALU_A_I; total = n + 5; }
        } else switch (op) {
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            d->reg = op - 0x40; d->kind = DK_INC_R; total = n + 1; break;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            d->reg = op - 0x48; d->kind = DK_DEC_R; total = n + 1; break;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            d->reg = op - 0x50; d->kind = DK_PUSH_R; total = n + 1; break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            d->reg = op - 0x58; d->kind = DK_POP_R; total = n + 1; break;
        case 0x68: d->imm = rd32(p); d->kind = DK_PUSH_I; total = n + 5; break;
        case 0x6A: d->imm = (uint32_t)(int32_t)(int8_t)rd8(p); d->kind = DK_PUSH_I; total = n + 2; break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            d->sub = op & 15; total = n + 2;
            d->imm = va + total + (uint32_t)(int32_t)(int8_t)rd8(p); d->kind = DK_JCC; break;
        case 0x80: case 0x81: case 0x83:
            mlen = dc_modrm(d, p);
            d->sz = op == 0x80 ? 1 : 4; d->sub = d->reg;
            if (op == 0x81) { d->imm = rd32(p + mlen); total = n + 1 + mlen + 4; }
            else { d->imm = (uint32_t)(int32_t)(int8_t)rd8(p + mlen); total = n + 1 + mlen + 1; }
            d->kind = DK_ALU_RM_I; break;
        case 0x84: case 0x85:
            mlen = dc_modrm(d, p); d->sz = op == 0x84 ? 1 : 4; d->kind = DK_TEST_RM_R; total = n + 1 + mlen; break;
        case 0x88: case 0x89:
            mlen = dc_modrm(d, p); d->sz = op == 0x88 ? 1 : 4; d->kind = DK_MOV_RM_R; total = n + 1 + mlen; break;
        case 0x8A: case 0x8B:
            mlen = dc_modrm(d, p); d->sz = op == 0x8A ? 1 : 4; d->kind = DK_MOV_R_RM; total = n + 1 + mlen; break;
        case 0x8D:
            mlen = dc_modrm(d, p);
            if (d->mod != 3) { d->kind = DK_LEA; total = n + 1 + mlen; }
            break;
        case 0x90: d->kind = DK_NOP; total = n + 1; break;
        case 0xA8: d->sz = 1; d->imm = rd8(p); d->kind = DK_TEST_A_I; total = n + 2; break;
        case 0xA9: d->imm = rd32(p); d->kind = DK_TEST_A_I; total = n + 5; break;
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            d->sz = 1; d->reg = op - 0xB0; d->imm = rd8(p); d->kind = DK_MOV_R_I; total = n + 2; break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            d->reg = op - 0xB8; d->imm = rd32(p); d->kind = DK_MOV_R_I; total = n + 5; break;
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            mlen = dc_modrm(d, p);
            d->sz = (op == 0xC0 || op == 0xD0 || op == 0xD2) ? 1 : 4; d->sub = d->reg;
            if (op <= 0xC1) { d->imm = rd8(p + mlen); total = n + 2 + mlen; }
            else { d->imm = op <= 0xD1 ? 1 : 0x100; total = n + 1 + mlen; }   /* 0x100: count in cl */
            d->kind = DK_SHIFT; break;
        case 0xC2: d->imm = rd16(p); d->kind = DK_RET; total = n + 3; break;
        case 0xC3: d->imm = 0; d->kind = DK_RET; total = n + 1; break;
        case 0xC6: case 0xC7:
            mlen = dc_modrm(d, p);
            if (d->reg != 0) break;                 /* only /0 is mov */
            d->sz = op == 0xC6 ? 1 : 4;
            if (op == 0xC6) { d->imm = rd8(p + mlen); total = n + 2 + mlen; }
            else { d->imm = rd32(p + mlen); total = n + 5 + mlen; }
            d->kind = DK_MOV_RM_I; break;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            mlen = dc_modrm(d, p);
            /* interp_x87 reads no bytes after ModRM for any form it knows */
            d->kind = DK_X87; total = n + 1 + mlen; break;
        case 0xE8: total = n + 5; d->imm = va + total + rd32(p); d->kind = DK_CALL; break;
        case 0xE9: total = n + 5; d->imm = va + total + rd32(p); d->kind = DK_JMP_NEAR; break;
        case 0xEB: total = n + 2; d->imm = va + total + (uint32_t)(int32_t)(int8_t)rd8(p); d->kind = DK_JMP_SHORT; break;
        case 0xF6: case 0xF7:
            mlen = dc_modrm(d, p);
            if (d->reg > 1) break;                  /* only test imm here */
            d->sz = op == 0xF6 ? 1 : 4;
            if (op == 0xF6) { d->imm = rd8(p + mlen); total = n + 2 + mlen; }
            else { d->imm = rd32(p + mlen); total = n + 5 + mlen; }
            d->kind = DK_TEST_RM_I; break;
        case 0xFE: case 0xFF:
            mlen = dc_modrm(d, p);
            d->sz = op == 0xFE ? 1 : 4; total = n + 1 + mlen;
            if (d->reg <= 1) d->kind = DK_INCDEC_RM;
            else if (op == 0xFF && d->reg == 2) d->kind = DK_FF_CALL;
            else if (op == 0xFF && d->reg == 4) d->kind = DK_FF_JMP;
            else if (op == 0xFF && d->reg == 6) d->kind = DK_FF_PUSH;
            else total = 0;
            break;
        case 0x0F: {
            uint8_t op2 = rd8(p);
            d->op2 = op2;
            if (op2 >= 0x80 && op2 <= 0x8F) {
                d->sub = op2 & 15; total = n + 6;
                d->imm = va + total + rd32(p + 1); d->kind = DK_JCC;
            } else if (op2 >= 0x90 && op2 <= 0x9F) {
                mlen = dc_modrm(d, p + 1); d->sub = op2 & 15; d->sz = 1; d->kind = DK_SETCC; total = n + 2 + mlen;
            } else if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
                mlen = dc_modrm(d, p + 1); d->sz = (op2 & 1) ? 2 : 1;
                d->kind = op2 <= 0xB7 ? DK_MOVZX : DK_MOVSX; total = n + 2 + mlen;
            }
            break;
        }
        }
        if (d->kind != DK_GENERIC && total) d->len = (uint8_t)total;
        else { d->kind = DK_GENERIC; d->base = 8; d->index = 8; d->len = (uint8_t)(n + 1); }
    }
    if (d->kind != DK_GENERIC && !((s_fast_kinds >> d->kind) & 1)) {
        d->kind = DK_GENERIC; d->base = 8; d->index = 8; d->len = (uint8_t)(n + 1);
    }
    if (d->len > 16) { d->kind = DK_GENERIC; d->len = (uint8_t)(n + 1); }
    if (d->len > 16) return 0;
    return 1;
}

/* Nonzero when guest code differs from a block's saved bytes (inline: the CRT
 * memcmp call cost more than the compare for these few dozen bytes). */
static inline int blk_differ(const uint8_t *g, const uint8_t *sv, unsigned n)
{
    uint64_t x, y;
    unsigned i = 0;
    if (n >= 8) {
        for (; i + 8 <= n; i += 8) {
            memcpy(&x, g + i, 8); memcpy(&y, sv + i, 8);
            if (x != y) return 1;
        }
        if (i < n) { memcpy(&x, g + n - 8, 8); memcpy(&y, sv + n - 8, 8); return x != y; }
        return 0;
    }
    for (; i < n; i++) if (g[i] != sv[i]) return 1;
    return 0;
}

static int dk_ends_block(int kind)
{
    return kind == DK_GENERIC || kind == DK_JCC || kind == DK_JMP_SHORT || kind == DK_JMP_NEAR ||
           kind == DK_CALL || kind == DK_RET || kind == DK_FF_CALL || kind == DK_FF_JMP;
}

/* Decode the block starting at pc into b. Returns 0 when not even its first
 * instruction can be cached. */
static int bc_build(uintptr_t mb, Block *b, uint32_t pc)
{
    uint32_t p = pc;
    unsigned n = 0;
    b->va = 0;
    while (n < BLK_MAX) {
        DInsn *d = &b->ins[n];
        if (p - pc + 16 > BLK_BYTES) break;
        if (!dc_decode(d, p)) break;
        if (n && d->hook) break;              /* a hooked instruction starts its own block */
        n++;
        p += d->len;
        if (dk_ends_block(d->kind)) break;
    }
    if (!n) return 0;
    b->n = (uint16_t)n;
    b->nbytes = (uint16_t)(p - pc);
    memcpy(b->bytes, (const void *)(mb + pc), b->nbytes);
    b->va = pc;
    return 1;
}

/* ---- native callee cache -------------------------------------------------------
 * native_for() (dirty bit, hand-written overrides, the dispatch table) for every
 * call interpreted code makes. A clean converted function stays clean unless
 * nfl2k5_interp_mark_va/force mark more (g_interp_dirty_gen), so its function
 * pointer is kept per thread. Kernel and module VAs and misses are not kept. */
volatile long g_interp_dirty_gen;
#define NC_SIZE 1024
typedef struct { uint32_t va; long gen; recomp_func_t fn; } NCache;
static XBOX_THREAD_LOCAL NCache *t_ncache;

static recomp_func_t native_for_cached(uint32_t va)
{
    NCache *e;
    recomp_func_t fn;
    if (va >= 0xFE000000u || !t_ncache || !s_ncache) return native_for(va);
    e = &t_ncache[(va ^ (va >> 10)) & (NC_SIZE - 1)];
    if (e->va == va && e->gen == g_interp_dirty_gen) return e->fn;
    fn = native_for(va);
    if (fn) { e->va = va; e->gen = g_interp_dirty_gen; e->fn = fn; }
    return fn;
}

/* ---- NFL2K5_PACK_NATIVE_CHECK=1: native pack functions against the interpreter ----
 * Runs the interpreter from the entry, keeps the result, rolls registers and
 * memory back, runs the native version and compares: registers, x87 state, the
 * frame's locals and the output buffer the bone functions fill ([ebp+8]). */
void recomp_interp_call(uint32_t va);
extern ptrdiff_t xbox_GetMemoryOffset(void);
static XBOX_THREAD_LOCAL int t_pn_nested;

typedef struct {
    uint32_t r[9];
    double fp[8];
    int top, cmp;
    uint16_t cc, cw;
} PnState;

static void pn_save(PnState *p)
{
    p->r[0] = g_eax; p->r[1] = g_ecx; p->r[2] = g_edx; p->r[3] = g_ebx; p->r[4] = g_esp;
    p->r[5] = g_ebp; p->r[6] = g_esi; p->r[7] = g_edi; p->r[8] = g_seh_ebp;
    memcpy(p->fp, g_fp_stack, sizeof p->fp);
    p->top = g_fp_top; p->cmp = g_fp_cmp; p->cc = g_fp_cc; p->cw = g_fp_control_word;
}

static void pn_load(const PnState *p)
{
    g_eax = p->r[0]; g_ecx = p->r[1]; g_edx = p->r[2]; g_ebx = p->r[3]; g_esp = p->r[4];
    g_ebp = p->r[5]; g_esi = p->r[6]; g_edi = p->r[7]; g_seh_ebp = p->r[8];
    memcpy(g_fp_stack, p->fp, sizeof p->fp);
    g_fp_top = p->top; g_fp_cmp = p->cmp; g_fp_cc = p->cc; g_fp_control_word = p->cw;
}

static void pn_check_run(uint32_t va, recomp_func_t fn)
{
    enum { OUTB = 0x2000, FRAME = 0x400 };
    static uint8_t out0[OUTB], out1[OUTB], fr0[FRAME], fr1[FRAME];
    static volatile LONG runs, bad;
    PnState in, a, b;
    uint32_t ebp = g_ebp, buf = MEM32(ebp + 8u), lo = g_esp - 0x74u;
    uint32_t flen = ebp + 0x20u - lo, k;
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    if (t_pn_nested || flen > FRAME) { fn(); return; }
    t_pn_nested = 1;
    pn_save(&in);
    memcpy(out0, mem + buf, OUTB); memcpy(fr0, mem + lo, flen);
    recomp_interp_call(va);
    pn_save(&a);
    memcpy(out1, mem + buf, OUTB); memcpy(fr1, mem + lo, flen);
    memcpy((void *)(mem + buf), out0, OUTB); memcpy((void *)(mem + lo), fr0, flen);
    pn_load(&in);
    fn();
    pn_save(&b);
    t_pn_nested = 0;
    /* g_ebp/g_seh_ebp (r[5], r[8]) are not compared: converted code keeps ebp in
     * a local and leaves the globals as its callees did, retail included; the
     * interpreter writes its own ebp there. No caller reads them after a return. */
    {
        int diff = 0;
        for (k = 0; k < 8; k++)
            if (k != 5 && k != 8 && a.r[k] != b.r[k]) diff |= 1;
        if (a.top != b.top || a.cc != b.cc || a.cw != b.cw) diff |= 8;
        for (k = 0; k < 8; k++)
            if (memcmp(&a.fp[k], &b.fp[k], 8) && (a.fp[k] == a.fp[k] || b.fp[k] == b.fp[k])) diff |= 16;
        if (memcmp(out1, mem + buf, OUTB)) diff |= 2;
        if (memcmp(fr1, mem + lo, flen)) diff |= 4;
        if (diff && InterlockedIncrement(&bad) <= 20) {
            fprintf(stderr, "[PACKNATIVE] %08X differs (mask %d): eax %08X/%08X ecx %08X/%08X edx %08X/%08X ebx %08X/%08X esp %08X/%08X esi %08X/%08X edi %08X/%08X ebp %08X/%08X top %d/%d cc %X/%X\n",
                    va, diff, a.r[0], b.r[0], a.r[1], b.r[1], a.r[2], b.r[2], a.r[3], b.r[3], a.r[4], b.r[4],
                    a.r[6], b.r[6], a.r[7], b.r[7], a.r[5], b.r[5], a.top, b.top, a.cc, b.cc);
            for (k = 0; k < OUTB; k++)
                if (out1[k] != mem[buf + k]) { fprintf(stderr, "[PACKNATIVE]   output +%X: %02X/%02X\n", k, out1[k], mem[buf + k]); break; }
            for (k = 0; k < flen; k++)
                if (fr1[k] != mem[lo + k]) { fprintf(stderr, "[PACKNATIVE]   frame +%X: %02X/%02X\n", k, fr1[k], mem[lo + k]); break; }
        }
        if (InterlockedIncrement(&runs) % 20000 == 0)
            fprintf(stderr, "[PACKNATIVE] %ld checks, %ld differ\n", (long)runs, (long)bad);
    }
}

static inline void call_native(recomp_func_t fn, uint32_t target)
{
    if (s_pn_check && pn_lookup(target) == fn) pn_check_run(target, fn);
    else fn();
}

/* ---- host sampling profiler (NFL2K5_INTERP_SAMPLE=<hex VA>, Windows, diagnostics) --
 * Every 2 ms every thread of the process is suspended and its RIP read. The
 * thread that first entered the interpreter at that VA (the game thread) also
 * gets its stack unwound so time in system calls is charged to the game code
 * that made them. Every 20 s: per-thread busy share, and for the game thread
 * the hottest host functions, source lines and wait callers. */
#ifdef _WIN32
#include <dbghelp.h>
#include <tlhelp32.h>
static DWORD s_samp_tid;
static volatile LONG s_samp_started;
static uint64_t s_exe_lo, s_exe_hi;
#define SAMP_N 16384
typedef struct { uint64_t rip; uint32_t n; uint32_t pad; } SampEnt;
static SampEnt s_samp[SAMP_N];      /* game thread: RIP */
static SampEnt s_samp_w[SAMP_N];    /* game thread outside the exe: first exe frames (chain hash) */
#define SAMP_CHAIN 8
static uint64_t s_samp_wchain[SAMP_N][SAMP_CHAIN];
static uint32_t s_samp_total, s_samp_out;
#define SAMP_T 64
static struct { DWORD tid; uint32_t n, in_exe; SampEnt top[256]; } s_thr[SAMP_T];

static int cmp_samp(const void *a, const void *b)
{
    const SampEnt *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}

static SampEnt *samp_slot(SampEnt *tab, unsigned size, uint64_t key)
{
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40);
    for (unsigned k = 0; k < size; k++) {
        SampEnt *e = &tab[(h + k) & (size - 1)];
        if (e->rip == key) return e;
        if (!e->rip) { e->rip = key; return e; }
    }
    return NULL;
}

static const char *samp_name(uint64_t rip, char *buf, size_t bufsz, DWORD64 *disp)
{
    char sb[sizeof(SYMBOL_INFO) + 128];
    SYMBOL_INFO *si = (SYMBOL_INFO *)sb;
    memset(sb, 0, sizeof sb);
    si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 127;
    *disp = 0;
    if (SymFromAddr(GetCurrentProcess(), rip, disp, si)) snprintf(buf, bufsz, "%s", si->Name);
    else snprintf(buf, bufsz, "%llx", (unsigned long long)rip);
    return buf;
}

static void samp_dump(void)
{
    static SampEnt tmp[SAMP_N];
    static struct { char name[96]; uint32_t n; } fn[1024];
    unsigned nf = 0, i, j;
    char nb[128], nb2[128], nb3[128];
    DWORD64 disp;
    fprintf(stderr, "[ISAMP] game thread %u samples, %.1f%% outside the exe\n", s_samp_total,
            s_samp_total ? 100.0 * s_samp_out / s_samp_total : 0.0);
    memcpy(tmp, s_samp, sizeof tmp);
    qsort(tmp, SAMP_N, sizeof tmp[0], cmp_samp);
    for (i = 0; i < SAMP_N && tmp[i].n; i++) {
        samp_name(tmp[i].rip, nb, sizeof nb, &disp);
        for (j = 0; j < nf; j++) if (!strcmp(fn[j].name, nb)) break;
        if (j == nf && nf < 1024) { snprintf(fn[nf].name, 96, "%s", nb); fn[nf].n = 0; nf++; }
        if (j < 1024) fn[j].n += tmp[i].n;
        if (i < 40) {
            IMAGEHLP_LINE64 ln; DWORD ld = 0;
            memset(&ln, 0, sizeof ln); ln.SizeOfStruct = sizeof ln;
            if (SymGetLineFromAddr64(GetCurrentProcess(), tmp[i].rip, &ld, &ln))
                fprintf(stderr, "[ISAMP] %5.2f%% %s+0x%llx line %lu\n", 100.0 * tmp[i].n / s_samp_total, nb, (unsigned long long)disp, ln.LineNumber);
            else
                fprintf(stderr, "[ISAMP] %5.2f%% %s+0x%llx\n", 100.0 * tmp[i].n / s_samp_total, nb, (unsigned long long)disp);
        }
    }
    for (i = 0; i < nf; i++)
        for (j = i + 1; j < nf; j++)
            if (fn[j].n > fn[i].n) { char t[96]; uint32_t k = fn[i].n; memcpy(t, fn[i].name, 96); memcpy(fn[i].name, fn[j].name, 96); fn[i].n = fn[j].n; memcpy(fn[j].name, t, 96); fn[j].n = k; }
    for (i = 0; i < nf && i < 45; i++)
        fprintf(stderr, "[ISAMP] func %5.2f%% %s\n", 100.0 * fn[i].n / s_samp_total, fn[i].name);
    /* system-call callers */
    memcpy(tmp, s_samp_w, sizeof tmp);
    for (i = 0; i < SAMP_N; i++) tmp[i].pad = i;
    qsort(tmp, SAMP_N, sizeof tmp[0], cmp_samp);
    for (i = 0; i < 25 && tmp[i].n; i++) {
        uint64_t *ch = s_samp_wchain[tmp[i].pad];
        fprintf(stderr, "[ISAMP] outside-exe %5.2f%% from %s", 100.0 * tmp[i].n / s_samp_total,
                samp_name(ch[0], nb, sizeof nb, &disp));
        for (j = 1; j < SAMP_CHAIN && ch[j]; j++) fprintf(stderr, " < %s", samp_name(ch[j], nb2, sizeof nb2, &disp));
        fprintf(stderr, "\n");
        (void)nb3;
    }
    /* threads */
    for (i = 0; i < SAMP_T; i++) {
        unsigned best = 0;
        if (!s_thr[i].tid || s_thr[i].n == 0) continue;
        for (j = 0; j < 256; j++) if (s_thr[i].top[j].n > s_thr[i].top[best].n) best = j;
        fprintf(stderr, "[ISAMP] thread %5lu%s samples %u in-exe %.1f%% top %s (%.1f%%)\n", s_thr[i].tid,
                s_thr[i].tid == s_samp_tid ? " (game)" : "", s_thr[i].n, 100.0 * s_thr[i].in_exe / s_thr[i].n,
                s_thr[i].top[best].n ? samp_name(s_thr[i].top[best].rip, nb, sizeof nb, &disp) : "-",
                100.0 * s_thr[i].top[best].n / s_thr[i].n);
        if (s_thr[i].in_exe * 20 >= s_thr[i].n) {     /* busy threads: their hottest functions */
            static struct { char name[96]; uint32_t n; } tf[256];
            unsigned ntf = 0, a, b2;
            for (j = 0; j < 256; j++) {
                if (!s_thr[i].top[j].n) continue;
                samp_name(s_thr[i].top[j].rip, nb, sizeof nb, &disp);
                for (a = 0; a < ntf; a++) if (!strcmp(tf[a].name, nb)) break;
                if (a == ntf) { snprintf(tf[ntf].name, 96, "%s", nb); tf[ntf].n = 0; ntf++; }
                tf[a].n += s_thr[i].top[j].n;
            }
            for (a = 0; a < 8 && a < ntf; a++) {
                unsigned m = a;
                for (b2 = a + 1; b2 < ntf; b2++) if (tf[b2].n > tf[m].n) m = b2;
                if (m != a) { char t[96]; uint32_t k = tf[a].n; memcpy(t, tf[a].name, 96); memcpy(tf[a].name, tf[m].name, 96); tf[a].n = tf[m].n; memcpy(tf[m].name, t, 96); tf[m].n = k; }
                fprintf(stderr, "[ISAMP]     %5.1f%% %s\n", 100.0 * tf[a].n / s_thr[i].n, tf[a].name);
            }
        }
    }
    fflush(stderr);
    memset(s_samp, 0, sizeof s_samp); memset(s_samp_w, 0, sizeof s_samp_w); memset(s_samp_wchain, 0, sizeof s_samp_wchain);
    s_samp_total = 0; s_samp_out = 0;
    memset(s_thr, 0, sizeof s_thr);
}

static int samp_in_exe(uint64_t rip) { return rip >= s_exe_lo && rip < s_exe_hi; }

/* First exe frames of a suspended thread's stack. */
static int samp_unwind(CONTEXT *ctx, uint64_t *out, int max)
{
    int n = 0;
    __try {
        for (int d = 0; d < 48 && n < max; d++) {
            DWORD64 ib = 0, est = 0;
            PVOID hd = NULL;
            uint64_t rip = ctx->Rip;
            PRUNTIME_FUNCTION f;
            if (!rip) break;
            if (samp_in_exe(rip)) out[n++] = rip;
            f = RtlLookupFunctionEntry(rip, &ib, NULL);
            if (!f) { ctx->Rip = *(DWORD64 *)ctx->Rsp; ctx->Rsp += 8; }
            else RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, rip, f, ctx, &hd, &est, NULL);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
    return n;
}

static DWORD WINAPI samp_main(void *arg)
{
    DWORD last = GetTickCount(), pid = GetCurrentProcessId();
    static DWORD tids[256];
    unsigned ntid = 0, tick = 0;
    (void)arg;
    timeBeginPeriod(1);
    for (;;) {
        Sleep(2);
        if ((tick++ & 255) == 0) {           /* refresh the thread list */
            HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;
            ntid = 0;
            te.dwSize = sizeof te;
            if (sn != INVALID_HANDLE_VALUE) {
                if (Thread32First(sn, &te)) do {
                    if (te.th32OwnerProcessID == pid && te.th32ThreadID != GetCurrentThreadId() && ntid < 256)
                        tids[ntid++] = te.th32ThreadID;
                    te.dwSize = sizeof te;
                } while (Thread32Next(sn, &te));
                CloseHandle(sn);
            }
        }
        for (unsigned t = 0; t < ntid; t++) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tids[t]);
            CONTEXT ctx;
            int game = tids[t] == s_samp_tid;
            if (!h) continue;
            if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
            memset(&ctx, 0, sizeof ctx);
            ctx.ContextFlags = game ? CONTEXT_FULL : CONTEXT_CONTROL;
            if (GetThreadContext(h, &ctx)) {
                uint64_t rip = ctx.Rip, chain[SAMP_CHAIN] = { 0 };
                int in_exe = samp_in_exe(rip);
                if (game) {
                    SampEnt *e = samp_slot(s_samp, SAMP_N, rip);
                    if (e) e->n++;
                    s_samp_total++;
                    if (!in_exe) {
                        s_samp_out++;
                        if (samp_unwind(&ctx, chain, SAMP_CHAIN) > 0) {
                            uint64_t key = 0;
                            for (int q = 0; q < SAMP_CHAIN; q++) key = key * 31 + chain[q];
                            e = samp_slot(s_samp_w, SAMP_N, key ? key : 1);
                            if (e) { e->n++; memcpy(s_samp_wchain[e - s_samp_w], chain, sizeof chain); }
                        }
                    }
                }
                {
                    unsigned k;
                    for (k = 0; k < SAMP_T; k++) if (s_thr[k].tid == tids[t] || !s_thr[k].tid) break;
                    if (k < SAMP_T) {
                        SampEnt *e;
                        s_thr[k].tid = tids[t]; s_thr[k].n++; s_thr[k].in_exe += in_exe;
                        e = samp_slot(s_thr[k].top, 256, rip);
                        if (e) e->n++;
                    }
                }
            }
            ResumeThread(h);
            CloseHandle(h);
        }
        if (GetTickCount() - last > 20000) { samp_dump(); last = GetTickCount(); }
    }
    return 0;
}

static void samp_maybe_start(uint32_t va)
{
    static uint32_t want = 1;
    if (want == 1) {
        const char *v = getenv("NFL2K5_INTERP_SAMPLE");
        want = v ? (uint32_t)strtoul(v, NULL, 16) : 0;
    }
    if (!want || va != want || s_samp_started) return;
    if (InterlockedExchange(&s_samp_started, 1)) return;
    {
        uint8_t *mb = (uint8_t *)GetModuleHandleW(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(mb + ((IMAGE_DOS_HEADER *)mb)->e_lfanew);
        s_exe_lo = (uint64_t)(uintptr_t)mb; s_exe_hi = s_exe_lo + nt->OptionalHeader.SizeOfImage;
    }
    s_samp_tid = GetCurrentThreadId();
    CreateThread(NULL, 0, samp_main, NULL, 0, NULL);
    fprintf(stderr, "[ISAMP] sampling all threads; game thread %lu entered %08X\n", s_samp_tid, va);
}
#pragma comment(lib, "winmm.lib")
#else
static void samp_maybe_start(uint32_t va) { (void)va; }
#endif

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

/* The interpreter frame entered last on any thread (guest threads run one at a
 * time), for the guest lock's stall report: diagnostics, may be stale. */
static Cpu *volatile s_any_cpu;
static void interp_stall_report(void)
{
    Cpu *c = s_any_cpu;
    if (c)
        fprintf(stderr, "[INTERP] last frame: at %08X eax %08X ecx %08X edx %08X ebx %08X esp %08X ebp %08X esi %08X edi %08X\n",
                c->start, c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7]);
}

static void interp_execute(Cpu *c, uint32_t va)
{
    uint32_t esp0;
    int fast;
    Block *dcache;
    const uintptr_t mb = (uintptr_t)g_xbox_mem_offset;
    memset(c, 0, sizeof *c);
    c->r[EAX] = g_eax; c->r[ECX] = g_ecx; c->r[EDX] = g_edx; c->r[EBX] = g_ebx;
    c->r[ESP] = g_esp; c->r[EBP] = g_ebp; c->r[ESI] = g_esi; c->r[EDI] = g_edi;
    /* Native detour callers publish the flags used by their continuation.
     * Mod wrappers commonly PUSHFD before calling a helper and POPFD before
     * branching. Starting with zero flags loses that caller comparison even
     * when the wrapper's own PUSHFD/POPFD is interpreted correctly. */
    if (g_rf_kind) {   /* recomp_rf_cond(2/10/4/8/0), evaluated once */
        uint32_t k = g_rf_kind, a = g_rf_a, b = g_rf_b;
        uint32_t r = k == 1 ? a - b : k == 2 ? (a & b) : a;
        c->cf = k == 1 ? a < b : (k == 3 || k == 4) ? (int)(g_rf_cf & 1u) : 0;
        c->pf = recomp_parity8(r);
        c->zf = k == 4 ? 0 : r == 0;
        c->sf = (int32_t)r < 0;
        c->of = k == 1 ? (int)((((a ^ b) & (a ^ r)) >> 31) & 1u) : 0;
    }
    c->eip = va;
    esp0 = c->r[ESP];
    if (s_interp_log < 0) {
        s_interp_log = getenv("NFL2K5_INTERP_LOG") != NULL;
#ifdef _WIN32
        AddVectoredExceptionHandler(1, interp_fault_report);
#endif
    }
    if (s_interp_log) { fprintf(stderr, "[INTERP] enter %08X esp %08X ret %08X\n", va, esp0, rd32(esp0)); }

    if (s_fast > 0 && !t_dcache) {
        t_dcache = (Block *)calloc(DCACHE_SIZE, sizeof(Block));
        t_ncache = (NCache *)calloc(NC_SIZE, sizeof(NCache));
    }
    fast = s_fast > 0 && t_dcache;
    dcache = t_dcache;
    if (fast && t_dcache_hook_gen != s_hook_gen) {
        memset(dcache, 0, DCACHE_SIZE * sizeof(Block));
        t_dcache_hook_gen = s_hook_gen;
    }
    t_cur = c;   /* nested interpreter calls restore it when they return */
    s_any_cpu = c;
    for (;;) {
        uint8_t op;
        int size;
        uint32_t target = 0, prev = c->start, pc = c->eip;
        c->start = pc;
        if (s_slow) {
            /* NFL2K5_INTERP_TRACE=lo-hi (hex VAs, diagnostics): every interpreted instruction in
             * that range with the registers it starts with. */
            if (s_tr_on && c->start >= s_tr_lo && c->start <= s_tr_hi)
                fprintf(stderr, "[ITRACE] %08X eax %08X ecx %08X edx %08X ebx %08X esp %08X ebp %08X esi %08X edi %08X\n",
                        c->start, c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7]);
            if (s_prof) { prof_insn(c->start); c->icount++; }
        }
        if (fast) {
            Block *blk = &dcache[dc_index(pc)];
            uint32_t blo, blen;
            int smc = 0;
            unsigned bi;
            if (blk->va != pc || blk_differ((const uint8_t *)(mb + pc), blk->bytes, blk->nbytes)) {
                if (!bc_build(mb, blk, pc)) goto check_hooks;
            }
            if (blk->ins[0].hook) {
                uint32_t to = run_hooks(c, prev);
                if (to) { c->eip = to; continue; }
                if (blk_differ((const uint8_t *)(mb + pc), blk->bytes, blk->nbytes)) { blk->va = 0; goto hooks_done; }
            }
            if (pc - prev - 1 >= 15u) TRAIL(prev, pc);
            blo = pc; blen = blk->nbytes;
/* a store at a that may touch the block's own bytes: finish the block after this instruction */
#define SMC(a) (smc |= (uint32_t)((uint32_t)(a) - (blo - 8u)) < blen + 8u)
            for (bi = 0;; bi++) {
                const DInsn *d = &blk->ins[bi];
                int sz = d->sz;
                c->start = d->va;
                if (d->kind == DK_GENERIC) {      /* always the last entry */
                    c->opsize16 = d->opsize16; c->rep = d->rep; c->seg_fs = d->seg_fs; c->sse_prefix = d->sse;
                    op = d->op;
                    c->eip = d->va + d->plen + 1;
                    goto decoded;
                }
                pc = d->va;
                c->eip = pc + d->len;
                switch (d->kind) {
                case DK_MOV_RM_R: SMC(d_setrm(mb, c, d, sz, getreg(c, sz, d->reg))); break;
                case DK_MOV_R_RM: setreg(c, sz, d->reg, d_getrm(mb, c, d, sz)); break;
                case DK_MOV_R_I: setreg(c, sz, d->reg, d->imm); break;
                case DK_MOV_RM_I: SMC(d_setrm(mb, c, d, sz, d->imm)); break;
                case DK_LEA: c->r[d->reg] = d->disp + c->r[d->base] + (c->r[d->index] << d->scale); break;
                case DK_ALU_RM_R: case DK_ALU_RM_I: {
                    uint32_t b = d->kind == DK_ALU_RM_I ? d->imm : getreg(c, sz, d->reg), r;
                    if (d->mod == 3) {
                        r = alu(c, d->sub, sz, getreg(c, sz, d->rm), b);
                        if (d->sub != 7) setreg(c, sz, d->rm, r);
                    } else {
                        uint32_t a = d_ea(c, d);
                        r = alu(c, d->sub, sz, sz == 4 ? FM32(a) : FM8(a), b);
                        if (d->sub != 7) { if (sz == 4) FM32(a) = r; else FM8(a) = (uint8_t)r; SMC(a); }
                    }
                    break;
                }
                case DK_ALU_R_RM: {
                    uint32_t r = alu(c, d->sub, sz, getreg(c, sz, d->reg), d_getrm(mb, c, d, sz));
                    if (d->sub != 7) setreg(c, sz, d->reg, r);
                    break;
                }
                case DK_ALU_A_I: {
                    uint32_t r = alu(c, d->sub, sz, getreg(c, sz, EAX), d->imm);
                    if (d->sub != 7) setreg(c, sz, EAX, r);
                    break;
                }
                case DK_TEST_RM_R: alu(c, 4, sz, d_getrm(mb, c, d, sz), getreg(c, sz, d->reg)); break;
                case DK_TEST_A_I: alu(c, 4, sz, c->r[EAX], d->imm); break;
                case DK_TEST_RM_I: alu(c, 4, sz, d_getrm(mb, c, d, sz), d->imm); break;
                case DK_PUSH_R: push32(c, c->r[d->reg]); SMC(c->r[ESP]); break;
                case DK_POP_R: { uint32_t v = pop32(c); c->r[d->reg] = v; break; }
                case DK_PUSH_I: push32(c, d->imm); SMC(c->r[ESP]); break;
                case DK_INC_R: { int saved = c->cf; c->r[d->reg] = alu(c, 0, 4, c->r[d->reg], 1); c->cf = saved; break; }
                case DK_DEC_R: { int saved = c->cf; c->r[d->reg] = alu(c, 5, 4, c->r[d->reg], 1); c->cf = saved; break; }
                case DK_INCDEC_RM: {
                    int saved = c->cf;
                    if (d->mod == 3) setreg(c, sz, d->rm, alu(c, d->reg ? 5 : 0, sz, getreg(c, sz, d->rm), 1));
                    else {
                        uint32_t a = d_ea(c, d);
                        uint32_t r = alu(c, d->reg ? 5 : 0, sz, sz == 4 ? FM32(a) : FM8(a), 1);
                        if (sz == 4) FM32(a) = r; else FM8(a) = (uint8_t)r;
                        SMC(a);
                    }
                    c->cf = saved;
                    break;
                }
                case DK_JCC: if (cond(c, d->sub)) { c->eip = d->imm; TRAIL(pc, d->imm); } break;
                case DK_JMP_SHORT: c->eip = d->imm; TRAIL(pc, d->imm); break;
                case DK_JMP_NEAR: target = d->imm; TRAIL(pc, target); goto do_jmp;
                case DK_CALL: target = d->imm; TRAIL(pc, target); goto do_call;
                case DK_FF_CALL: target = d_getrm(mb, c, d, 4); TRAIL(pc, target); goto do_call;
                case DK_FF_JMP: target = d_getrm(mb, c, d, 4); TRAIL(pc, target); goto do_jmp;
                case DK_FF_PUSH: push32(c, d_getrm(mb, c, d, 4)); SMC(c->r[ESP]); break;
                case DK_RET: {
                    uint32_t at = c->r[ESP];
                    uint32_t ret = pop32(c);
                    c->r[ESP] += d->imm;
                    if (at >= esp0) {                 /* the frame we were entered with */
                        sync_out(c);
                        if (s_interp_log) fprintf(stderr, "[INTERP] return to %08X\n", ret);
                        return;
                    }
                    c->eip = ret;
                    TRAIL(pc, ret);
                    if (t_dcache_hook_gen != s_hook_gen) {   /* hooks added while this frame ran */
                        memset(dcache, 0, DCACHE_SIZE * sizeof(Block));
                        t_dcache_hook_gen = s_hook_gen;
                    }
                    break;
                }
                case DK_MOVZX: c->r[d->reg] = d_getrm(mb, c, d, sz); break;
                case DK_MOVSX: {
                    uint32_t v = d_getrm(mb, c, d, sz);
                    c->r[d->reg] = sz == 1 ? (uint32_t)(int32_t)(int8_t)v : (uint32_t)(int32_t)(int16_t)v;
                    break;
                }
                case DK_SETCC: SMC(d_setrm(mb, c, d, 1, (uint32_t)cond(c, d->sub))); break;
                case DK_SHIFT: {
                    unsigned n = d->imm == 0x100 ? (c->r[ECX] & 0xFF) : d->imm;
                    mat(c);
                    if (d->mod == 3) setreg(c, sz, d->rm, shift(c, d->sub, sz, getreg(c, sz, d->rm), n));
                    else {
                        uint32_t a = d_ea(c, d);
                        uint32_t r = shift(c, d->sub, sz, sz == 4 ? FM32(a) : FM8(a), n);
                        if (sz == 4) FM32(a) = r; else FM8(a) = (uint8_t)r;
                        SMC(a);
                    }
                    break;
                }
                case DK_X87:
                    if (d->mod != 3) {   /* the common memory forms, as interp_x87_body runs them */
                        uint32_t a = d_ea(c, d);
                        int r = d->reg;
                        switch (d->op) {
                        case 0xD8: if (r != 2 && r != 3) { *st(0) = farith(r, *st(0), (double)FMF(a)); goto x87_done; } break;
                        case 0xDC: if (r != 2 && r != 3) { *st(0) = farith(r, *st(0), FMD(a)); goto x87_done; } break;
                        case 0xD9:
                            if (r == 0) { fpush((double)FMF(a)); goto x87_done; }
                            if (r == 2) { FMF(a) = (float)*st(0); SMC(a); goto x87_done; }
                            if (r == 3) { FMF(a) = (float)*st(0); fpop(); SMC(a); goto x87_done; }
                            break;
                        case 0xDD:
                            if (r == 0) { fpush(FMD(a)); goto x87_done; }
                            if (r == 2) { FMD(a) = *st(0); SMC(a); goto x87_done; }
                            if (r == 3) { FMD(a) = *st(0); fpop(); SMC(a); goto x87_done; }
                            break;
                        }
                    }
                    mat(c);
                    c->opsize16 = 0; c->rep = 0; c->sse_prefix = 0; c->seg_fs = d->seg_fs;
                    c->mod = d->mod; c->reg = d->reg; c->rm = d->rm;
                    if (d->mod != 3) c->ea = d_ea(c, d);
                    if (!interp_x87_body(c, d->op)) { fatal(c, "unknown x87 opcode"); goto out; }
                    if (d->mod != 3) SMC(c->ea);
                x87_done:
                    break;
                case DK_NOP: break;
                }
                if (bi + 1 >= blk->n || smc) break;
            }
#undef SMC
            continue;
        }
    check_hooks:
        {
            uint32_t h = hook_hash(pc);
            if ((s_hook_bits[h >> 3] >> (h & 7)) & 1) {
                uint32_t to = run_hooks(c, prev);
                if (to) { c->eip = to; continue; }
            }
        }
    hooks_done:
        /* record control transfers only: from -> to */
        if (pc - prev - 1 >= 15u) TRAIL(prev, pc);   /* pc <= prev || pc > prev + 15 */
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
    decoded:
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

        if (!s_lazy_ok[op]) mat(c);
        switch (op) {
        case 0x0F: {
            uint8_t op2 = fetch8(c);
            if (!((op2 >= 0x80 && op2 <= 0x9F) || (op2 >= 0x40 && op2 <= 0x4F) ||
                  op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF)) mat(c);
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
                if (flag_zf(c)) setrm(c, sz, getreg(c, sz, c->reg));
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
            if (c->reg >= 4) mat(c);
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
            if (fast && t_dcache_hook_gen != s_hook_gen) {   /* hooks added while this frame ran */
                memset(dcache, 0, DCACHE_SIZE * sizeof(Block));
                t_dcache_hook_gen = s_hook_gen;
            }
            recomp_func_t fn = native_for_cached(target);
            push32(c, ret);
            if (fn) {
                int64_t t0 = s_prof ? prof_now() : 0;
                sync_out(c);
                call_native(fn, target);
                sync_in(c);
                if (s_prof) { int64_t d = prof_now() - t0; c->native_ticks += d; prof_native(target, d); }
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
            recomp_func_t fn = native_for_cached(target);
            if (fn) {
                /* A jump to a converted function's start is a tail call: it
                 * returns through the return address on top of our stack, to
                 * whoever pushed it. */
                uint32_t at = c->r[ESP];
                uint32_t ret = rd32(at);
                int64_t t0 = s_prof ? prof_now() : 0;
                sync_out(c);
                call_native(fn, target);
                sync_in(c);
                if (s_prof) { int64_t d = prof_now() - t0; c->native_ticks += d; prof_native(target, d); }
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

void recomp_interp_call(uint32_t va)
{
    Cpu *parent = t_cur;
    Cpu cpu;
    int64_t t0 = 0;
    uint32_t ret = 0;
    if (s_prof < 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        s_prof_freq = f.QuadPart;
        s_prof_last = prof_now();
        { extern void (*g_ggl_stall_report)(void); g_ggl_stall_report = interp_stall_report; }
        {
            const char *fv = getenv("NFL2K5_INTERP_FAST");
            s_fast = !(fv && fv[0] == '0');
            fv = getenv("NFL2K5_INTERP_FAST_KINDS");
            if (fv && *fv) s_fast_kinds = strtoull(fv, NULL, 16);
            fv = getenv("NFL2K5_INTERP_NCACHE");
            s_ncache = !(fv && fv[0] == '0');
        }
        {
            const char *pv = getenv("NFL2K5_PACK_NATIVE");
            if (!(pv && pv[0] == '0')) {
                s_pn_check = getenv("NFL2K5_PACK_NATIVE_CHECK") != NULL;
                s_npn = nfl2k5_pack_natives_init(s_pn_va, s_pn_fn, PN_MAX);
            }
        }
        s_prof = getenv("NFL2K5_INTERP_PROF") != NULL;
        {
            const char *tv = getenv("NFL2K5_INTERP_TRACE");
            char *te;
            if (tv && *tv) {
                s_tr_lo = (uint32_t)strtoul(tv, &te, 16);
                s_tr_hi = *te == '-' ? (uint32_t)strtoul(te + 1, NULL, 16) : s_tr_lo;
                s_tr_on = 1;
            }
        }
        s_slow = s_prof || s_tr_on;
    }
    if (s_npn && !t_pn_nested) {          /* a native version of a function a pack changed */
        recomp_func_t pn = pn_lookup(va);
        if (pn) { call_native(pn, va); return; }
    }
    if (s_prof) { t0 = prof_now(); ret = rd32(g_esp); t_prof_depth++; }
    samp_maybe_start(va);
#ifdef _MSC_VER
    __try {
        interp_execute(&cpu, va);
    } __finally {
        /* Native calls may reenter the interpreter. Never leave the crash
         * reporter pointing into a returned inner interpreter's stack. */
        t_cur = parent;
    }
#else
    interp_execute(&cpu, va);
    t_cur = parent;
#endif
    if (s_prof) {
        int top = --t_prof_depth == 0;
        prof_entry(va, ret, &cpu, prof_now() - t0, top);
    }
}

/* Exercise the real interpreter's native-to-guest flag handoff, including
 * the PUSHFD / temporary XOR / POPFD pattern used by pack detours. */
int nfl2k5_test_interp_flags(uint32_t scratch)
{
    static const uint8_t code[] = {
        0x9c, 0x31, 0xc0, 0x9d, 0x74, 0x06,
        0xb8, 0, 0, 0, 0, 0xc3, 0xb8, 1, 0, 0, 0, 0xc3
    };
    static const struct { uint8_t jump; uint32_t a, b; int expected; } cases[] = {
        { 0x74, 3, 3, 1 }, { 0x74, 4, 3, 0 },
        { 0x72, 2, 3, 1 }, { 0x72, 4, 3, 0 },
        { 0x7c, 0xffffffffu, 1, 1 }, { 0x7c, 1, 0xffffffffu, 0 },
        { 0x70, 0x80000000u, 1, 1 }, { 0x70, 4, 3, 0 }
    };
    uint32_t regs[] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    uint32_t rf[] = { g_rf_kind, g_rf_a, g_rf_b, (uint32_t)g_rf_as, (uint32_t)g_rf_bs, g_rf_cf };
    uint32_t seh = g_seh_ebp;
    int df = g_df, ok = 1;
    memcpy((void *)((uintptr_t)xbox_GetMemoryOffset() + scratch), code, sizeof code);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        MEM8(scratch + 4) = cases[i].jump;
        g_rf_kind = 1; g_rf_a = cases[i].a; g_rf_b = cases[i].b;
        g_rf_as = (int32_t)cases[i].a; g_rf_bs = (int32_t)cases[i].b;
        g_esp = scratch + 4000; MEM32(g_esp) = 0;
        recomp_interp_call(scratch);
        if (g_eax != (uint32_t)cases[i].expected || g_esp != scratch + 4004) {
            fprintf(stderr, "[TEST] interpreter flags case %u failed (eax=%u esp=%08X)\n", i, g_eax, g_esp);
            ok = 0; break;
        }
    }
    /* This pack helper returns the mode comparison in flags, preserving EAX.
     * Exercise its native implementation: its callers branch on ZF without
     * another comparison. A stale flag export sends animation down the wrong
     * mode-specific path. Retail has no function at this address. */
    recomp_func_t mode_helper = recomp_lookup(0x014DB710u);
    if (mode_helper) {
        uint32_t old_mode = MEM32(0xE5FF80u), old_scene = MEM32(0xBF1858u);
        static const struct { uint32_t mode, scene; int zf; } modes[] = {
            { 7, 0, 0 }, { 8, 49, 1 }, { 8, 50, 0 }, { 9, 50, 0 }
        };
        for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
            MEM32(0xE5FF80u) = modes[i].mode;
            MEM32(0xBF1858u) = modes[i].scene;
            g_rf_kind = 1; g_rf_a = modes[i].zf ? 1 : 0; g_rf_b = 0;
            g_rf_as = (int32_t)g_rf_a; g_rf_bs = 0;
            g_eax = 0x12345678u;
            g_esp = scratch + 4000; MEM32(g_esp) = 0;
            mode_helper();
            if (recomp_rf_cond(4) != modes[i].zf ||
                g_eax != 0x12345678u || g_esp != scratch + 4004) {
                fprintf(stderr, "[TEST] pack mode flag return case %u failed\n", i);
                ok = 0;
            }
        }
        MEM32(0xE5FF80u) = old_mode; MEM32(0xBF1858u) = old_scene;
        if (ok) fprintf(stderr, "[TEST] pack mode helper native flag return passed (4 cases)\n");
    }
    g_eax = regs[0]; g_ecx = regs[1]; g_edx = regs[2]; g_ebx = regs[3];
    g_esp = regs[4]; g_ebp = regs[5]; g_esi = regs[6]; g_edi = regs[7];
    g_rf_kind = rf[0]; g_rf_a = rf[1]; g_rf_b = rf[2];
    g_rf_as = (int32_t)rf[3]; g_rf_bs = (int32_t)rf[4]; g_rf_cf = rf[5];
    g_seh_ebp = seh; g_df = df;
    return ok;
}

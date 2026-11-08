/* Differential test for the mod-pack interpreter (src/interp/x86_interp.c):
 * the current interpreter against the reference one from before the decoded-
 * instruction cache and lazy flags (commit 5ffca1d), on random single
 * instructions and short sequences from random states. Registers, flags (via
 * pushfd), x87 and xmm state and every written guest page must match.
 *
 *   powershell -File tools/interp-difftest/build.ps1
 *   harness.exe [cases] [max instructions per case]   (NFL2K5_INTERP_FAST=0 tests the
 *                                                       uncached path)
 *   harness.exe hooks      native hooks, including one added after the cache is warm
 *   harness.exe bench      ns per interpreted instruction, reference vs current */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "recomp_types.h"

/* ---- runtime stubs the interpreter needs ---- */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp;
RECOMP_TLS int g_df;
RECOMP_TLS uint16_t g_fp_control_word;
RECOMP_TLS int g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc;
RECOMP_TLS uint32_t g_rf_kind, g_rf_a, g_rf_b, g_rf_cf;
RECOMP_TLS int32_t g_rf_as, g_rf_bs;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top;
RECOMP_TLS uint32_t g_fs_base;
RECOMP_TLS uint32_t g_seh_ebp;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
ptrdiff_t g_xbox_mem_offset;
ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }
void (*g_ggl_stall_report)(void);
int nfl2k5_pack_natives_init(uint32_t *va, void (**fn)(void), int max) { (void)va; (void)fn; (void)max; return 0; }
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
volatile long g_interp_dirty_gen;

extern void recomp_interp_call(uint32_t va);
extern void orig_recomp_interp_call(uint32_t va);
extern uint8_t *g_interp_dirty_bits; extern uint32_t g_interp_dirty_limit; extern int g_interp_active;
extern uint8_t *orig_g_interp_dirty_bits; extern uint32_t orig_g_interp_dirty_limit; extern int orig_g_interp_active;

#define MEMSZ (64u << 20)
#define CODE 0x00010000u
#define DATA 0x00200000u      /* registers point around here */
#define STACK 0x00800000u
#define OUT_VA 0x00900000u

static uint8_t *mem;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }

typedef struct {
    uint32_t r[8];
    double fp[8]; int top; uint16_t cw; uint32_t cc, cmp;
    RecompXmm x[8];
    int df;
    uint32_t datahash, stackhash, out[4];
} State;

static uint32_t fnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }

static uint8_t init_data[0x40000];
static uint32_t init_regs[8];
static double init_fp[8];
static int init_top;
static RecompXmm init_x[8];

static uint8_t *pristine;
static PVOID s_pages[1 << 15];

/* Undo every page the last run (and setup) wrote, then clear the watch. */
static void restore_mem(void)
{
    ULONG_PTR cnt = sizeof s_pages / sizeof s_pages[0];
    DWORD gran = 0;
    if (GetWriteWatch(0, mem, MEMSZ, s_pages, &cnt, &gran) == 0)
        for (ULONG_PTR i = 0; i < cnt; i++) memcpy(s_pages[i], pristine + ((uint8_t *)s_pages[i] - mem), gran);
    ResetWriteWatch(mem, MEMSZ);
}

/* Hash of every page written since the last restore, with its address. */
static uint32_t dirty_hash(void)
{
    ULONG_PTR cnt = sizeof s_pages / sizeof s_pages[0];
    DWORD gran = 0;
    uint32_t h = 0;
    if (GetWriteWatch(0, mem, MEMSZ, s_pages, &cnt, &gran) == 0)
        for (ULONG_PTR i = 0; i < cnt; i++)
            h = h * 31 + fnv(s_pages[i], gran) + (uint32_t)((uint8_t *)s_pages[i] - mem);
    return h;
}

static void setup(const uint8_t *code, size_t len)
{
    restore_mem();
    memcpy(mem + CODE, code, len);
    memcpy(mem + DATA - 0x20000, init_data, sizeof init_data);
    memset(mem + STACK - 0x10000, 0xCC, 0x20000);
    memset(mem + OUT_VA, 0, 64);
    g_eax = init_regs[0]; g_ecx = init_regs[1]; g_edx = init_regs[2]; g_ebx = init_regs[3];
    g_ebp = init_regs[5]; g_esi = init_regs[6]; g_edi = init_regs[7];
    g_seh_ebp = 0;
    g_esp = STACK; *(uint32_t *)(mem + STACK) = 0xDEAD0000u;   /* return address */
    memcpy(g_fp_stack, init_fp, sizeof init_fp); g_fp_top = init_top;
    g_fp_control_word = 0x027F; g_fp_cc = 0; g_fp_cmp = 0;
    g_xmm0 = init_x[0]; g_xmm1 = init_x[1]; g_xmm2 = init_x[2]; g_xmm3 = init_x[3];
    g_xmm4 = init_x[4]; g_xmm5 = init_x[5]; g_xmm6 = init_x[6]; g_xmm7 = init_x[7];
    g_df = 0; g_rf_kind = 0;
}

static void capture(State *s)
{
    s->r[0] = g_eax; s->r[1] = g_ecx; s->r[2] = g_edx; s->r[3] = g_ebx; s->r[4] = g_esp; s->r[5] = g_ebp; s->r[6] = g_esi; s->r[7] = g_edi;
    memcpy(s->fp, g_fp_stack, sizeof s->fp); s->top = g_fp_top; s->cw = g_fp_control_word; s->cc = g_fp_cc; s->cmp = (uint32_t)g_fp_cmp;
    s->x[0] = g_xmm0; s->x[1] = g_xmm1; s->x[2] = g_xmm2; s->x[3] = g_xmm3; s->x[4] = g_xmm4; s->x[5] = g_xmm5; s->x[6] = g_xmm6; s->x[7] = g_xmm7;
    s->df = g_df;
    s->datahash = dirty_hash();
    s->stackhash = fnv(mem + STACK - 0x10000, 0x20000);
    memcpy(s->out, mem + OUT_VA, 16);
}

static int same(const State *a, const State *b)
{
    if (memcmp(a->r, b->r, sizeof a->r)) return 0;
    for (int i = 0; i < 8; i++) {
        if (memcmp(&a->fp[i], &b->fp[i], 8) && !(a->fp[i] != a->fp[i] && b->fp[i] != b->fp[i])) return 0;
        if (memcmp(&a->x[i], &b->x[i], 16)) return 0;
    }
    return a->top == b->top && a->cw == b->cw && a->cc == b->cc && a->cmp == b->cmp && a->df == b->df &&
           a->datahash == b->datahash && a->stackhash == b->stackhash && !memcmp(a->out, b->out, 16);
}

static void randomize(void)
{
    for (int i = 0; i < 8; i++) init_regs[i] = DATA - 0x8000 + (rnd() & 0xFFFC) + (rnd() & 3) * ((rnd() & 1));
    /* (register values stay addresses: any of them can be a base) */
    {
        static int filled;
        if (!filled) { for (size_t i = 0; i < sizeof init_data; i++) init_data[i] = (uint8_t)rnd(); filled = 1; }
        for (int i = 0; i < 64; i++) ((uint32_t *)init_data)[rnd() % (sizeof init_data / 4)] = rnd();
    }
    for (int i = 0; i < 8; i++) init_fp[i] = ((int32_t)rnd()) / 65536.0;
    init_top = rnd() & 7;
    for (int i = 0; i < 8; i++) for (int k = 0; k < 4; k++) init_x[i].u[k] = rnd();
}

/* ---- instruction generator ---- */
static int gen_modrm(uint8_t *p, int allow_reg)
{
    int mod = rnd() & 3, reg = rnd() & 7, rm = rnd() & 7, n = 1;
    if (!allow_reg && mod == 3) mod = rnd() % 3;
    /* avoid esp as base with arbitrary modification of memory near the return address: fine, stack is hashed */
    if (mod == 0 && rm == 5) { p[0] = (uint8_t)((mod << 6) | (reg << 3) | rm); *(uint32_t *)(p + 1) = DATA + (rnd() & 0x3FFC); return 5; }
    p[0] = (uint8_t)((mod << 6) | (reg << 3) | rm);
    if (mod != 3 && rm == 4) {
        int sc = rnd() & 3, idx = rnd() & 7, base = rnd() & 7;
        if (base == 4) base = 3;   /* no esp base: a store could rewrite a return address and loop forever */
        if (rnd() & 1) idx = 4;              /* no index often */
        if (idx != 4 && sc > 0 && (rnd() & 1)) sc = 0;
        if (base == 5 && mod == 0) { p[1] = (uint8_t)((sc << 6) | (idx << 3) | base); *(uint32_t *)(p + 2) = 0x100000u + (rnd() & 0xFFC); n = 6; }
        else { p[1] = (uint8_t)((sc << 6) | (idx << 3) | base); n = 2; }
        if (idx != 4) { /* keep scaled index addresses inside memory: the index register holds ~2 MB, times 8 = 16 MB: ok */ }
    }
    if (mod == 1) p[n++] = (uint8_t)(rnd() & 0xFF);
    else if (mod == 2) { *(uint32_t *)(p + n) = (rnd() & 0xFFFF) - 0x8000; n += 4; }
    return n;
}

static int gen_insn(uint8_t *p)
{
    int n = 0, k = rnd() % 40;
    if (rnd() % 12 == 0) p[n++] = 0x64 * 0 + 0x3E;      /* harmless prefix sometimes */
    switch (k) {
    case 0: case 1: case 2: { uint8_t op = (uint8_t)(((rnd() & 7) << 3) | (rnd() & 3)); p[n++] = op; n += gen_modrm(p + n, 1); break; }
    case 3: { uint8_t op = (uint8_t)(((rnd() & 7) << 3) | 4); p[n++] = op; p[n++] = (uint8_t)rnd(); break; }
    case 4: { uint8_t op = (uint8_t)(((rnd() & 7) << 3) | 5); p[n++] = op; *(uint32_t *)(p + n) = rnd(); n += 4; break; }
    case 5: p[n++] = (uint8_t)(0x40 + (rnd() & 15)); if ((p[n-1] & 7) == 4) p[n-1] ^= 1; break;   /* inc/dec, not esp */
    case 6: p[n++] = 0x50 + (rnd() & 7); p[n++] = (uint8_t)(0x58 + (rnd() & 7)); if (p[n-1] == 0x5C) p[n-1] = 0x58; break;
    case 7: p[n++] = rnd() & 1 ? 0x68 : 0x6A; if (p[n-1] == 0x68) { *(uint32_t *)(p + n) = rnd(); n += 4; } else p[n++] = (uint8_t)rnd(); p[n++] = 0x58 + (rnd() & 3); break;
    case 8: { uint8_t ops[] = { 0x80, 0x81, 0x83, 0x82 }; uint8_t op = ops[rnd() & 3]; p[n++] = op; n += gen_modrm(p + n, 1);
              if (op == 0x81) { *(uint32_t *)(p + n) = rnd(); n += 4; } else p[n++] = (uint8_t)rnd(); break; }
    case 9: p[n++] = 0x84 + (rnd() & 1); n += gen_modrm(p + n, 1); break;
    case 10: case 11: p[n++] = 0x88 + (rnd() & 3); n += gen_modrm(p + n, 1); break;
    case 12: p[n++] = 0x8D; n += gen_modrm(p + n, 0); break;
    case 13: p[n++] = 0xA8 + (rnd() & 1); if (p[n-1] == 0xA8) p[n++] = (uint8_t)rnd(); else { *(uint32_t *)(p + n) = rnd(); n += 4; } break;
    case 14: p[n++] = (uint8_t)(0xB0 + (rnd() & 15)); if (p[n-1] >= 0xB8) { *(uint32_t *)(p + n) = rnd(); n += 4; } else p[n++] = (uint8_t)rnd(); if (p[n-5] == 0xBC) p[n-5] = 0xB8; break;
    case 15: { uint8_t ops[] = { 0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3 }; uint8_t op = ops[rnd() % 6]; p[n++] = op; n += gen_modrm(p + n, 1); if (op <= 0xC1) p[n++] = (uint8_t)(rnd() & 0x3F); break; }
    case 16: p[n++] = 0xC6 + (rnd() & 1); { int m = gen_modrm(p + n, 1); p[n] &= 0xC7; n += m; } if (p[0 + (p[0] == 0x3E)] == 0xC6) p[n++] = (uint8_t)rnd(); else { *(uint32_t *)(p + n) = rnd(); n += 4; } break;
    case 17: p[n++] = 0xF6 + (rnd() & 1); { int m = gen_modrm(p + n, 1); p[n] &= 0xCF; /* /0 or /1 */ if (rnd() & 1) p[n] |= 0x08; n += m; }
             if (p[0 + (p[0] == 0x3E)] == 0xF6) p[n++] = (uint8_t)rnd(); else { *(uint32_t *)(p + n) = rnd(); n += 4; } break;
    case 18: p[n++] = 0xFE + (rnd() & 1); { int m = gen_modrm(p + n, 1); p[n] &= 0xC7; if (rnd() & 1) p[n] |= 0x08; n += m; } break;
    case 19: p[n++] = 0xFF; { int m = gen_modrm(p + n, 0); p[n] = (uint8_t)((p[n] & 0xC7) | 0x30); n += m; } p[n++] = 0x58; break;   /* push r/m; pop eax */
    case 20: case 21: p[n++] = 0x0F; p[n++] = (uint8_t)(0x90 + (rnd() & 15)); n += gen_modrm(p + n, 1); break;
    case 22: p[n++] = 0x0F; { uint8_t o[] = { 0xB6, 0xB7, 0xBE, 0xBF }; p[n++] = o[rnd() & 3]; } n += gen_modrm(p + n, 1); break;
    case 23: case 24: /* x87 memory, float/double */
        { uint8_t ops[] = { 0xD8, 0xD9, 0xDC, 0xDD }; uint8_t op = ops[rnd() & 3]; int m; p[n++] = op; m = gen_modrm(p + n, 0);
          { int r = (p[n] >> 3) & 7; if ((op == 0xD9 || op == 0xDD) && r != 0 && r != 2 && r != 3) p[n] &= 0xC7; } n += m; }
        break;
    case 25: case 26: /* x87 register forms */
        { uint8_t ops[] = { 0xD8, 0xD9, 0xDC, 0xDE, 0xDD }; uint8_t op = ops[rnd() % 5]; p[n++] = op;
          if (op == 0xD9) { uint8_t f[] = { 0xC0, 0xC1, 0xC9, 0xCA, 0xE0, 0xE1, 0xE8, 0xEE }; p[n++] = (uint8_t)(f[rnd() & 7] + 0); }
          else if (op == 0xDE) { uint8_t f[] = { 0xC1, 0xC9, 0xE1, 0xE9, 0xF1, 0xF9, 0xC2, 0xE3 }; p[n++] = f[rnd() & 7]; }
          else if (op == 0xDD) { uint8_t f[] = { 0xD8, 0xD9, 0xDA, 0xD1 }; p[n++] = f[rnd() & 3]; }
          else p[n++] = (uint8_t)(0xC0 | (rnd() & 0x3F)); }
        break;
    case 27: p[n++] = 0xDF; p[n++] = 0xE0; break;   /* fnstsw ax */
    case 28: p[n++] = 0x0F; p[n++] = 0xAF; n += gen_modrm(p + n, 1); break;   /* generic imul */
    case 29: p[n++] = 0x66; p[n++] = 0x89 + (rnd() & 2); n += gen_modrm(p + n, 1); break;   /* 16-bit mov: generic */
    case 30: p[n++] = 0x64; p[n++] = 0x8B; n += gen_modrm(p + n, 1); break;   /* fs: */
    case 31: p[n++] = (uint8_t)(0x70 + (rnd() & 15)); p[n++] = 2; p[n++] = 0x8B; p[n++] = (uint8_t)(0xC0 | (rnd() & 0x3F)); break;
    case 32: p[n++] = 0x0F; p[n++] = (uint8_t)(0x80 + (rnd() & 15)); *(uint32_t *)(p + n) = 2; n += 4; p[n++] = 0x8B; p[n++] = (uint8_t)(0xC0 | (rnd() & 0x3F)); break;
    case 33: p[n++] = 0xEB; p[n++] = 1; p[n++] = 0x40 + (rnd() & 3); break;
    case 34: p[n++] = 0xE8; *(uint32_t *)(p + n) = 0; n += 4; p[n++] = (uint8_t)(0x58 + (rnd() & 3)); break;
    case 35: { uint8_t pf[] = { 0xF3, 0x66, 0x00, 0xF2 }; uint8_t ops[] = { 0x10, 0x11, 0x28, 0x58, 0x59, 0x5C, 0x2E, 0x54, 0x57, 0x5A, 0x51 };
               uint8_t x = pf[rnd() & 3]; uint8_t o = ops[rnd() % 11]; if (x) p[n++] = x; p[n++] = 0x0F; p[n++] = o;
               if (o == 0x11 || o == 0x28 || o == 0x57 || o == 0x54) { p[n++] = (uint8_t)(0xC0 | (rnd() & 0x3F)); }
               else n += gen_modrm(p + n, 1);
               break; }
    case 36: p[n++] = 0xE8; *(uint32_t *)(p + n) = 1; n += 4; p[n++] = 0xC3; break;   /* call over a ret: returns to the ret, which then returns the frame */
    default: { uint8_t op = (uint8_t)(((rnd() & 7) << 3) | (rnd() & 3)); p[n++] = op; n += gen_modrm(p + n, 1); break; }
    }
    return n;
}

static int s_faults;
static int run_case(const uint8_t *code, size_t len, int verbose)
{
    State s0, s1, s2;
    uint64_t save = rng;
    int f0 = 0, f1 = 0;
    setup(code, len); __try { orig_recomp_interp_call(CODE); } __except (1) { f0 = 1; } capture(&s0);
    setup(code, len); __try { recomp_interp_call(CODE); } __except (1) { f1 = 1; } capture(&s1);
    if (f0 || f1) { s_faults++; if (f0 != f1) { printf("FAULT MISMATCH\n"); return 0; } return 1; }
    (void)save;
    {   /* again: now from the decoded-instruction cache */
        int f2 = 0;
        setup(code, len); __try { recomp_interp_call(CODE); } __except (1) { f2 = 1; } capture(&s2);
        if (f2 || !same(&s0, &s2)) { if (verbose) printf("SECOND-RUN MISMATCH\n"); s1 = s2; }
    }
    if (!same(&s0, &s1)) {
        if (verbose) {
            printf("MISMATCH code:");
            for (size_t i = 0; i < len; i++) printf(" %02X", code[i]);
            printf("\n  orig: "); for (int i = 0; i < 8; i++) printf("%08X ", s0.r[i]); printf("out %08X top %d cc %X dh %08X sh %08X\n", s0.out[0], s0.top, s0.cc, s0.datahash, s0.stackhash);
            printf("  new : "); for (int i = 0; i < 8; i++) printf("%08X ", s1.r[i]); printf("out %08X top %d cc %X dh %08X sh %08X\n", s1.out[0], s1.top, s1.cc, s1.datahash, s1.stackhash);
        }
        return 0;
    }
    (void)s2;
    return 1;
}

static uint32_t hook_add100(void) { g_eax += 0x100; return 0; }
static uint32_t hook_skip_inc(void) { return CODE + 9; }

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 100000, bad = 0, seqs = argc > 2 ? atoi(argv[2]) : 4;
    setvbuf(stdout, NULL, _IONBF, 0);
    { extern int __stdcall SymInitialize(void *, const char *, int); SymInitialize(GetCurrentProcess(), NULL, 1); }
    /* the whole 4 GB guest space reserved, so a stray address faults the same way in every run */
    mem = (uint8_t *)VirtualAlloc(NULL, (1ull << 32) + 0x10000, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    VirtualAlloc(mem, MEMSZ, MEM_COMMIT, PAGE_READWRITE);
    pristine = (uint8_t *)malloc(MEMSZ);
    for (size_t i = 0; i < MEMSZ / 4; i++) ((uint32_t *)mem)[i] = rnd();
    memcpy(pristine, mem, MEMSZ);
    ResetWriteWatch(mem, MEMSZ);
    g_xbox_mem_offset = (ptrdiff_t)mem;
    g_fs_base = 0x00A00000u;
    if (argc > 1 && !strcmp(argv[1], "hooks")) {
        /* mov eax,1; add eax,2 (hooked: +0x100); inc eax (hooked: skip to the
         * next instruction); add eax,eax; pushfd/pop [OUT_VA]; ret */
        static const uint8_t code[] = { 0xB8, 1, 0, 0, 0, 0x83, 0xC0, 0x02, 0x40, 0x01, 0xC0,
                                        0x9C, 0x8F, 0x05, 0x00, 0x00, 0x90, 0x00, 0xC3 };
        extern void orig_recomp_interp_add_hook(uint32_t, uint32_t, uint32_t (*)(void));
        extern void recomp_interp_add_hook(uint32_t, uint32_t, uint32_t (*)(void));
        State a, b;
        int ok = 1;
        randomize();
        setup(code, sizeof code); recomp_interp_call(CODE); capture(&b);        /* warm the cache without hooks */
        setup(code, sizeof code); orig_recomp_interp_call(CODE); capture(&a);
        if (!same(&a, &b)) { printf("hooks: pre-hook mismatch\n"); ok = 0; }
        orig_recomp_interp_add_hook(CODE + 5, 0, hook_add100); recomp_interp_add_hook(CODE + 5, 0, hook_add100);
        orig_recomp_interp_add_hook(CODE + 8, 0, hook_skip_inc); recomp_interp_add_hook(CODE + 8, 0, hook_skip_inc);
        for (int k = 0; k < 3; k++) {
            setup(code, sizeof code); orig_recomp_interp_call(CODE); capture(&a);
            setup(code, sizeof code); recomp_interp_call(CODE); capture(&b);
            if (!same(&a, &b)) { printf("hooks: mismatch run %d (eax %08X vs %08X)\n", k, a.r[0], b.r[0]); ok = 0; }
        }
        printf("hooks test %s (eax %08X)\n", ok ? "passed" : "FAILED", b.r[0]);
        return !ok;
    }
    if (argc > 1 && !strcmp(argv[1], "smc")) {
        /* mov byte [imm of the next instruction], 5; mov eax, 1 (becomes 5);
         * inc eax; then the same through a register store; pushfd/pop; ret */
        uint8_t code[64];
        size_t n = 0;
        State a, b;
        int ok = 1;
        code[n++] = 0xC6; code[n++] = 0x05; *(uint32_t *)(code + n) = CODE + 8; n += 4; code[n++] = 0x05;   /* 0..6 */
        code[n++] = 0xB8; *(uint32_t *)(code + n) = 1; n += 4;                                            /* 7..11 */
        code[n++] = 0x40;                                                                                 /* 12 */
        code[n++] = 0xB9; *(uint32_t *)(code + n) = CODE + 24; n += 4;                                   /* 13..17 mov ecx, &imm */
        code[n++] = 0xC7; code[n++] = 0x01; *(uint32_t *)(code + n) = 0x77; n += 4;                     /* 18..23 mov [ecx], 0x77 */
        code[n++] = 0xBA; *(uint32_t *)(code + n) = 2; n += 4;                                            /* 24..28: wrong: imm at 25 */
        code[n++] = 0x9C; code[n++] = 0x8F; code[n++] = 0x05; *(uint32_t *)(code + n) = OUT_VA; n += 4;
        code[n++] = 0xC3;
        *(uint32_t *)(code + 14) = CODE + 25;                                                             /* point at mov edx's imm */
        randomize();
        for (int k = 0; k < 3; k++) {
            setup(code, n); orig_recomp_interp_call(CODE); capture(&a);
            setup(code, n); recomp_interp_call(CODE); capture(&b);
            if (!same(&a, &b)) { printf("smc: mismatch run %d (eax %08X/%08X edx %08X/%08X)\n", k, a.r[0], b.r[0], a.r[2], b.r[2]); ok = 0; }
        }
        printf("smc test %s (eax %08X edx %08X)\n", ok ? "passed" : "FAILED", b.r[0], b.r[2]);
        return !ok;
    }
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        static const uint8_t body[] = {
            0x8B, 0x44, 0xB3, 0x10, 0xD9, 0x44, 0xB3, 0x20, 0xD8, 0x4B, 0x40, 0xD8, 0x44, 0xB3, 0x24,
            0xD9, 0x5C, 0xB3, 0x30, 0x8D, 0x14, 0x70, 0x83, 0xC2, 0x05, 0x89, 0x54, 0xB3, 0x50,
            0x85, 0xD7, 0x74, 0x02, 0x40, 0x40, 0x46, 0x81, 0xFE, 0, 0, 0, 0 };
        uint8_t code[64];
        size_t n = sizeof body;
        uint32_t iters_b = getenv("BENCH_N") ? (uint32_t)atoi(getenv("BENCH_N")) : 2000000;
        LARGE_INTEGER f, t0, t1, t2;
        memcpy(code, body, n);
        *(uint32_t *)(code + n - 4) = iters_b;
        code[n++] = 0x7C; code[n] = (uint8_t)(-(int)(n + 1)); n++;
        code[n++] = 0xC3;
        randomize();
        for (int i = 0; i < 8; i++) init_regs[i] = DATA;
        init_regs[6] = 0;   /* esi: counter */
        init_regs[3] = DATA - 0x10000;
        QueryPerformanceFrequency(&f);
        {
            double instrs = 15.0 * iters_b, bo = 1e30, bn = 1e30;
            for (int rep = 0; rep < 7; rep++) {
                double a, b;
                setup(code, n); QueryPerformanceCounter(&t0); orig_recomp_interp_call(CODE); QueryPerformanceCounter(&t1);
                a = (t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / instrs;
                setup(code, n); QueryPerformanceCounter(&t1); recomp_interp_call(CODE); QueryPerformanceCounter(&t2);
                b = (t2.QuadPart - t1.QuadPart) * 1e9 / f.QuadPart / instrs;
                if (a < bo) bo = a;
                if (b < bn) bn = b;
            }
            printf("orig %.2f ns/instr, new %.2f ns/instr (best of 7)\n", bo, bn);
            if (getenv("BENCH_SAMPLE")) for (int rep = 0; rep < 150; rep++) { setup(code, n); recomp_interp_call(CODE); }
        }
        return 0;
    }
    for (int it = 0; it < iters; it++) {
        uint8_t code[512];
        size_t n = 0;
        int count = 1 + (int)(rnd() % (unsigned)seqs);
        randomize();
        for (int k = 0; k < count; k++) n += gen_insn(code + n);
        /* materialise flags and lazy state: pushfd; pop [OUT_VA]; then a jcc on each condition into setcc chain */
        code[n++] = 0x9C;                                   /* pushfd */
        code[n++] = 0x8F; code[n++] = 0x05; *(uint32_t *)(code + n) = OUT_VA; n += 4;   /* pop [OUT_VA] */
        code[n++] = 0xC3;
        if (getenv("HV")) { printf("case %d:", it); for (size_t q = 0; q < n; q++) printf(" %02X", code[q]); printf("\n"); }
        if (!run_case(code, n, bad < 20)) bad++;
    }
    printf("%d of %d cases differ (%d faulted in both)\n", bad, iters, s_faults);
    return bad != 0;
}

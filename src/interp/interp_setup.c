/* Which functions a mod pack changed (src/interp/x86_interp.c runs those).
 *
 * The native code was converted from one executable (the retail one with the
 * built-in widescreen patch). A pack runs its own patched executable on the
 * same native code: every converted function whose bytes differ between the
 * two is marked, and RECOMP_INTERP_GATE sends calls to it to the interpreter.
 * Code the pack added has no native function at all, so calls to it reach the
 * interpreter through the dispatch fallbacks. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp/x86_interp.h"

extern const uint32_t g_nfl2k5_func_ranges[][2];
extern const unsigned g_nfl2k5_func_range_count;
extern ptrdiff_t xbox_GetMemoryOffset(void);

static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* File offset of `va` in an XBE image, or -1. */
static long xbe_offset(const uint8_t *x, size_t len, uint32_t va)
{
    uint32_t base, count, table;
    if (len < 0x180 || memcmp(x, "XBEH", 4)) return -1;
    base = rd32le(x + 0x104);
    count = rd32le(x + 0x11C);
    table = rd32le(x + 0x120) - base;
    if ((uint64_t)table + (uint64_t)count * 56 > len) return -1;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *s = x + table + i * 56;
        uint32_t sva = rd32le(s + 4), vsize = rd32le(s + 8), raw = rd32le(s + 12), rsize = rd32le(s + 16);
        if (va >= sva && va < sva + vsize) {
            if (va - sva >= rsize || (uint64_t)raw + (va - sva) >= len) return -1;
            return (long)(raw + (va - sva));
        }
    }
    return -1;
}

/* `base`: the executable the native code came from, as it is in memory at
 * load (patches applied). The pack's executable is already loaded into guest
 * memory. Returns the number of functions marked. */
unsigned nfl2k5_interp_setup(const uint8_t *base, size_t base_len)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t limit = 0;
    unsigned marked = 0;
    for (unsigned i = 0; i < g_nfl2k5_func_range_count; i++)
        if (g_nfl2k5_func_ranges[i][1] > limit) limit = g_nfl2k5_func_ranges[i][1];
    g_interp_dirty_limit = limit;
    g_interp_dirty_bits = (uint8_t *)calloc(limit / 8 + 1, 1);
    if (!g_interp_dirty_bits) return 0;
    for (unsigned i = 0; i < g_nfl2k5_func_range_count; i++) {
        uint32_t start = g_nfl2k5_func_ranges[i][0], end = g_nfl2k5_func_ranges[i][1];
        int dirty = 0;
        for (uint32_t va = start; va < end && !dirty; va++) {
            long off = xbe_offset(base, base_len, va);
            if (off < 0) break;
            /* Compare a run at once while it stays inside one section. */
            {
                uint32_t n = end - va;
                long last = xbe_offset(base, base_len, end - 1);
                if (last == off + (long)(n - 1)) {
                    dirty = memcmp(base + off, mem + va, n) != 0;
                    break;
                }
                dirty = base[off] != mem[va];
            }
        }
        if (dirty) {
            g_interp_dirty_bits[start >> 3] |= (uint8_t)(1u << (start & 7));
            marked++;
        }
    }
    g_interp_active = 1;
    fprintf(stderr, "[INTERP] %u of %u converted functions differ in this executable and will be interpreted\n",
            marked, g_nfl2k5_func_range_count);
    return marked;
}

/* Diagnostics (NFL2K5_INTERP_FORCE): mark every converted function starting
 * in [lo, hi) for the interpreter, in any build. Returns how many. */
unsigned nfl2k5_interp_force(uint32_t lo, uint32_t hi)
{
    unsigned marked = 0;
    if (!g_interp_dirty_bits) {
        uint32_t limit = 0;
        for (unsigned i = 0; i < g_nfl2k5_func_range_count; i++)
            if (g_nfl2k5_func_ranges[i][1] > limit) limit = g_nfl2k5_func_ranges[i][1];
        g_interp_dirty_limit = limit;
        g_interp_dirty_bits = (uint8_t *)calloc(limit / 8 + 1, 1);
        if (!g_interp_dirty_bits) return 0;
    }
    for (unsigned i = 0; i < g_nfl2k5_func_range_count; i++) {
        uint32_t start = g_nfl2k5_func_ranges[i][0];
        if (start < lo || start >= hi) continue;
        g_interp_dirty_bits[start >> 3] |= (uint8_t)(1u << (start & 7));
        marked++;
    }
    g_interp_active = 1;
    return marked;
}

/* Mark the converted function whose range contains `va` for the interpreter
 * (any build): a byte patch inside it then takes effect, because the
 * interpreter reads the patched guest bytes while the native code still has
 * the original ones compiled in. Returns the function's start, or 0. */
uint32_t nfl2k5_interp_mark_va(uint32_t va)
{
    unsigned i;
    if (!g_interp_dirty_bits) {
        uint32_t limit = 0;
        for (i = 0; i < g_nfl2k5_func_range_count; i++)
            if (g_nfl2k5_func_ranges[i][1] > limit) limit = g_nfl2k5_func_ranges[i][1];
        g_interp_dirty_limit = limit;
        g_interp_dirty_bits = (uint8_t *)calloc(limit / 8 + 1, 1);
        if (!g_interp_dirty_bits) return 0;
    }
    {   /* every range holding it: split functions overlap their parents */
        uint32_t found = 0;
        for (i = 0; i < g_nfl2k5_func_range_count; i++) {
            uint32_t start = g_nfl2k5_func_ranges[i][0], end = g_nfl2k5_func_ranges[i][1];
            if (va >= start && va < end) {
                g_interp_dirty_bits[start >> 3] |= (uint8_t)(1u << (start & 7));
                g_interp_active = 1;
                found = start;
            }
        }
        return found;
    }
}

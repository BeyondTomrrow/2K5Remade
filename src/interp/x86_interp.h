/* x86 interpreter for code the native build does not contain (see
 * x86_interp.c). */
#ifndef NFL2K5_X86_INTERP_H
#define NFL2K5_X86_INTERP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run guest code at `va` as if it had been called: g_esp points at the
 * return address. Returns when that call returns. */
void recomp_interp_call(uint32_t va);

/* Nonzero when the guest function at `va` must be interpreted. */
int recomp_interp_dirty(uint32_t va);

/* A native hook at a guest instruction, for functions a pack changed: the
 * generated code's patches (tools/apply-gen-patches.py) never run there, so
 * the interpreter calls the same C hook when it reaches `va` -- only when
 * falling through from the instruction at `from` if `from` is nonzero (a
 * patch placed before a loop label). The hook sees and may change the g_
 * registers; it returns 0, or the address to continue at instead of `va`. */
typedef uint32_t (*recomp_interp_hook_t)(void);
void recomp_interp_add_hook(uint32_t va, uint32_t from, recomp_interp_hook_t fn);

/* Built by nfl2k5_interp_setup(): one bit per guest code byte, set at the
 * start address of every function that differs from the converted image. */
extern uint8_t *g_interp_dirty_bits;
extern uint32_t g_interp_dirty_limit;
extern int g_interp_active;

#ifdef __cplusplus
}
#endif

#endif

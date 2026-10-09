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

/* Built by nfl2k5_interp_setup(): one bit per guest code byte, set at the
 * start address of every function that differs from the converted image. */
extern uint8_t *g_interp_dirty_bits;
extern uint32_t g_interp_dirty_limit;
extern int g_interp_active;

#ifdef __cplusplus
}
#endif

#endif

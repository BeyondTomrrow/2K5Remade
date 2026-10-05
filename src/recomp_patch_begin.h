/* Included by tools/gen-reg-locals.py output at the start of each gen patch
 * block (tools/apply-gen-patches.py): the block runs with the guest registers
 * in their globals, as it was written for. The function's locals are written
 * out, and the register names mean the globals until recomp_patch_end.h.
 * No include guard: included once per block. */
RECOMP_REGS_OUT();
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi

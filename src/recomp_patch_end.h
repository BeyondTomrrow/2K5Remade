/* End of a gen patch block (see recomp_patch_begin.h): the register names
 * mean the function's locals again, re-read from the globals the block may
 * have changed. No include guard: included once per block. */
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi
#define eax r_eax
#define ecx r_ecx
#define edx r_edx
#define esp r_esp
#define ebx r_ebx
#define esi r_esi
#define edi r_edi
RECOMP_REGS_IN();

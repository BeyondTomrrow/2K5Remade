/* Authored register/stack fixture using the production ABI and patch headers. */
#define RECOMP_REG_LOCALS 1
#define RECOMP_GENERATED_CODE 1
#include "recomp_types.h"
#include <stdio.h>
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi;
ptrdiff_t g_xbox_mem_offset;
static unsigned char memory[4096];
static int failure,calls;
static void callee(void) {
    ++calls;
    if(g_edx!=0 || g_esp!=1020 || MEM32(g_esp)!=0xCEF80) failure=1;
    g_eax=42;g_edx=99;g_esp+=4;
}
#undef RECOMP_REGS_OUT
#undef RECOMP_REGS_IN
#define RECOMP_REGS_OUT() (g_eax=r_eax,g_ecx=r_ecx,g_edx=r_edx,g_esp=r_esp,(void)0)
#define RECOMP_REGS_IN() (r_eax=g_eax,r_ecx=g_ecx,r_edx=g_edx,r_esp=g_esp,(void)0)
static void patched(int invoke) {
    uint32_t r_eax=g_eax,r_ecx=g_ecx,r_edx=g_edx,r_esp=g_esp;
#include "recomp_patch_begin.h"
    if(invoke) {
        uint32_t keep_eax=eax,keep_edx=edx;
        edx=0;
        PUSH32(esp,0xCEF80);
        RECOMP_ABI_CALL(0,callee);
        eax=keep_eax;edx=keep_edx;
    }
#include "recomp_patch_end.h"
    if(r_eax!=17 || r_edx!=23 || r_esp!=1024) failure=2;
    r_eax=31;
    RECOMP_REGS_OUT();
}
static void patched_goto(void) {
    uint32_t r_eax=g_eax,r_ecx=g_ecx,r_edx=g_edx,r_esp=g_esp;
#include "recomp_patch_begin.h"
    eax=123;
    /* This is how gen-reg-locals synchronizes a goto out of a patch. */
    {RECOMP_REGS_IN();goto resumed;}
#include "recomp_patch_end.h"
resumed:
    if(r_eax!=123) failure=3;
    r_eax=321;RECOMP_REGS_OUT();
}
static void patched_return(void) {
    uint32_t r_eax=g_eax,r_ecx=g_ecx,r_edx=g_edx,r_esp=g_esp;
#include "recomp_patch_begin.h"
    eax=456;return;
#include "recomp_patch_end.h"
    RECOMP_REGS_OUT();
}
int main(void) {
    g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)memory;
    g_eax=17;g_ecx=19;g_edx=23;g_esp=1024;
    patched(0);if(failure || calls || g_eax!=31)return 1;
    g_eax=17;patched(1);if(failure || calls!=1 || g_eax!=31 || g_edx!=23 || g_esp!=1024)return 2;
    patched_goto();if(failure || g_eax!=321)return 3;
    patched_return();if(failure || g_eax!=456)return 4;
    puts("Nested native calls retain global patch arguments, return slot and resumed locals.");return 0;
}

/* Build: cc -O2 -I external/xboxrecomp/src/platform -o /tmp/a64test tools/mac-debug/mmio_decode_arm64_test.c */
#include "mmio_decode_arm64.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
static uint8_t *page; static size_t psz; static uint8_t backing[65536];
static bool rd(void *d, uintptr_t h, int sz, uint64_t *o){ uint64_t v=0; memcpy(&v, backing+(h-(uintptr_t)page), sz); *o=v; return true; }
static bool wr(void *d, uintptr_t h, uint64_t v, int sz){ memcpy(backing+(h-(uintptr_t)page), &v, sz); return true; }
static int faults;
static void h(int sig, siginfo_t *si, void *uc){ faults++; if(!a64_mmio_emulate(uc,NULL,rd,wr)){ fprintf(stderr,"decode fail sig=%d\n",sig); _exit(2);} }
int main(void){
  psz=getpagesize(); page=mmap(NULL,psz,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
  struct sigaction sa={0}; sa.sa_sigaction=h; sa.sa_flags=SA_SIGINFO|SA_NODEFER; sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0);
  for(int i=0;i<256;i++) backing[i]=(uint8_t)(i*37+0x80);
  int bad=0; uint64_t exp, got;
  #define CHECK(name,expr,ref) do{ got=(uint64_t)(expr); exp=(uint64_t)(ref); if(got!=exp){printf("FAIL %s got %llx exp %llx\n",name,(unsigned long long)got,(unsigned long long)exp);bad++;} }while(0)
  volatile uint8_t *vp=page;
  CHECK("ldrb", *(volatile uint8_t*)(vp+5), backing[5]);
  CHECK("ldrsb", *(volatile int8_t*)(vp+5), (int64_t)(int8_t)backing[5]);
  uint16_t h16; memcpy(&h16,backing+6,2); CHECK("ldrh", *(volatile uint16_t*)(vp+6), h16);
  CHECK("ldrsh", (int64_t)*(volatile int16_t*)(vp+6), (int64_t)(int16_t)h16);
  uint32_t w; memcpy(&w,backing+8,4); CHECK("ldr w", *(volatile uint32_t*)(vp+8), w);
  CHECK("ldrsw", (int64_t)*(volatile int32_t*)(vp+8), (int64_t)(int32_t)w);
  uint64_t x; memcpy(&x,backing+16,8); CHECK("ldr x", *(volatile uint64_t*)(vp+16), x);
  CHECK("ldur", *(volatile uint32_t*)(vp+3), ({uint32_t t; memcpy(&t,backing+3,4); t;}));
  int idx = 7; CHECK("reg off", ((volatile uint32_t*)vp)[idx], ({uint32_t t; memcpy(&t,backing+28,4); t;}));
  *(volatile uint32_t*)(vp+0x40)=0xDEADBEEF; CHECK("str w", ({uint32_t t; memcpy(&t,backing+0x40,4); t;}), 0xDEADBEEF);
  *(volatile uint8_t*)(vp+0x45)=0x5A; CHECK("strb", backing[0x45], 0x5A);
  *(volatile uint16_t*)(vp+0x46)=0x1234; CHECK("strh", ({uint16_t t; memcpy(&t,backing+0x46,2); t;}), 0x1234);
  // pair + simd via inline asm
  uint64_t a,b; const uint8_t *src=page+0x10;
  __asm__ volatile("ldp %0, %1, [%2]" : "=r"(a),"=r"(b) : "r"(src) : "memory");
  CHECK("ldp x0", a, ({uint64_t t; memcpy(&t,backing+0x10,8); t;})); CHECK("ldp x1", b, ({uint64_t t; memcpy(&t,backing+0x18,8); t;}));
  uint8_t *p2=page+0x80; uint64_t A=0x1111222233334444ull,B=0x5555666677778888ull;
  __asm__ volatile("stp %1, %2, [%0, #16]!" : "+r"(p2) : "r"(A),"r"(B) : "memory");
  CHECK("stp pre wb", (uintptr_t)p2, (uintptr_t)(page+0x90)); CHECK("stp mem", ({uint64_t t; memcpy(&t,backing+0x98,8); t;}), B);
  uint8_t *p3=page+0x20; uint32_t r;
  __asm__ volatile("ldr %w0, [%1], #4" : "=r"(r), "+r"(p3) :: "memory");
  CHECK("post idx val", r, ({uint32_t t; memcpy(&t,backing+0x20,4); t;})); CHECK("post idx wb",(uintptr_t)p3,(uintptr_t)(page+0x24));
  uint64_t qlo,qhi; const uint8_t *p4=page+0x30;
  __asm__ volatile("ldr q0, [%2]\n umov %0, v0.d[0]\n umov %1, v0.d[1]" : "=r"(qlo),"=r"(qhi) : "r"(p4) : "v0","memory");
  CHECK("ldr q lo", qlo, ({uint64_t t; memcpy(&t,backing+0x30,8); t;})); CHECK("ldr q hi", qhi, ({uint64_t t; memcpy(&t,backing+0x38,8); t;}));
  float f; memcpy(&f,backing+0x48,4); float g=*(volatile float*)(vp+0x48); CHECK("ldr s", *(uint32_t*)&g, *(uint32_t*)&f);
  printf("%s: %d faults, %d failures\n", bad?"FAIL":"PASS", faults, bad); return bad;
}

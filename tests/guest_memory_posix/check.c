#define _GNU_SOURCE
#include "guest_memory.h"
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *memory=mmap(NULL,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char data[32],copy[32];
    struct sigaction before,after;
    assert(memory!=MAP_FAILED);
    memset(data,0x57,sizeof data);
    assert(sigaction(SIGSEGV,NULL,&before)==0);
    assert(xbox_guest_memory_write((uintptr_t)memory,4,data,sizeof data));
    assert(xbox_guest_memory_read((uintptr_t)memory,4,copy,sizeof copy));
    assert(memcmp(data,copy,sizeof data)==0);
    assert(mprotect(memory+page,page,PROT_READ)==0);
    assert(mprotect(memory+page*2,page,PROT_NONE)==0);
    assert(xbox_guest_memory_read((uintptr_t)memory,page,copy,sizeof copy));
    assert(!xbox_guest_memory_write((uintptr_t)memory,page,data,sizeof data));
    assert(!xbox_guest_memory_read((uintptr_t)memory,page*2,copy,sizeof copy));
    assert(!xbox_guest_memory_write((uintptr_t)memory,page*2,data,sizeof data));
    assert(!xbox_guest_memory_read((uintptr_t)memory,page*2-16,copy,sizeof copy));
    assert(!xbox_guest_memory_write((uintptr_t)memory,page-16,data,sizeof data));
    assert(!xbox_guest_memory_read((uintptr_t)memory,UINT64_C(0x100000000),copy,1));
    assert(!xbox_guest_memory_read((uintptr_t)memory,UINT32_MAX,copy,2));
    assert(!xbox_guest_memory_write(UINTPTR_MAX-1,2,data,1));
    assert(!xbox_guest_memory_read(UINTPTR_MAX-1,0,copy,2));
    assert(!xbox_guest_memory_read((uintptr_t)memory,0,NULL,1));
    assert(!xbox_guest_memory_write((uintptr_t)memory,0,NULL,1));
    assert(xbox_guest_memory_read((uintptr_t)memory,0,NULL,0));
    assert(xbox_guest_memory_write((uintptr_t)memory,0,NULL,0));
    assert(munmap(memory+page*2,page)==0);
    assert(!xbox_guest_memory_read((uintptr_t)memory,page*2,copy,sizeof copy));
    assert(!xbox_guest_memory_write((uintptr_t)memory,page*2,data,sizeof data));
    assert(sigaction(SIGSEGV,NULL,&after)==0);
    assert(before.sa_handler==after.sa_handler && before.sa_flags==after.sa_flags);
    assert(munmap(memory,page*2)==0);
    return 0;
}

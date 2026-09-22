/* ESPN NFL 2K5 host for the user's retail XBE (title 53450030). */
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "apu.h"
#include "recomp_icall_feedback.h"
#include <stdbool.h>
extern MCPXAPUState *g_apu_state;
extern bool apu_hook_handle_mmio(PCONTEXT, uintptr_t, uint32_t, int);

static LONG CALLBACK audio_mmio(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_apu_state)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t host = ep->ExceptionRecord->ExceptionInformation[1];
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    if (host < offset || host - offset < 0xFE800000ULL || host - offset >= 0xFE880000ULL)
        return EXCEPTION_CONTINUE_SEARCH;
    if (ep->ExceptionRecord->ExceptionInformation[0] > 1) return EXCEPTION_CONTINUE_SEARCH;
    return apu_hook_handle_mmio(ep->ContextRecord, host, (uint32_t)(host - offset),
        ep->ExceptionRecord->ExceptionInformation[0] == 1)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;

/* Scoped write-watchpoint, opt-in via RECOMP_WATCH_PB_WRITE=1.
 *
 * Session context: the native build submits a real SET_BEGIN_END(TRIANGLES)
 * / INLINE_ARRAY draw during boot, but every one of the 6 vertex words comes
 * back as 0x00000000 (traced via RECOMP_PB_DRAW_TRACE). Adding a source-VA
 * print to the pushbuffer scanner (nv2a_pb_scan.c's g_nv2a_pb_last_word_va)
 * showed those words live at a fixed, reproducible guest address every run
 * (0x83E50DC0..0x83E50DD4) -- meaning the CPU itself already wrote zero into
 * the pushbuffer there; this is not a downstream decode bug. The only way to
 * find out *which guest code* performs that write, given this is a static
 * recompiler (so "guest EIP" is really a specific line of already-compiled
 * native code, not something a normal debugger single-steps through xbox
 * instructions for), is to catch the write itself: page-guard the exact
 * 4KB page it falls in (PAGE_READONLY -- reads still work normally, so the
 * pushbuffer scanner itself is unaffected), and on the write fault, resolve
 * the faulting native Rip through the same symbol engine crash_report
 * already uses to name the generated function and source line responsible.
 * Then restore write access for exactly one instruction (EFLAGS.TF) and
 * re-guard on the resulting single-step trap, so the guest keeps running
 * normally afterward. Capped at a handful of hits; not a general-purpose
 * watchpoint API, just enough to answer this one question. Known limit: a
 * concurrent write from a second thread while the page is briefly writable
 * (mid single-step) would not be caught -- acceptable for a capped, opt-in
 * diagnostic, not for a real watchpoint tool. */
static uintptr_t g_watch_page_host_base;
static CRITICAL_SECTION g_watch_lock;
static int g_watch_hits;
static uintptr_t g_watch_last_rip;
static uintptr_t g_watch_min_offset;
static uintptr_t g_watch_exact_offset = (uintptr_t)-1;
static RECOMP_TLS int g_watch_stepping;

static LONG CALLBACK pb_write_watch(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!g_watch_page_host_base)
        return EXCEPTION_CONTINUE_SEARCH;

    if (code == EXCEPTION_SINGLE_STEP) {
        if (!g_watch_stepping)
            return EXCEPTION_CONTINUE_SEARCH;
        g_watch_stepping = 0;
        DWORD old_protect;
        VirtualProtect((void *)g_watch_page_host_base, 0x1000, PAGE_READONLY, &old_protect);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
    ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
    if (op != 1 || addr < g_watch_page_host_base || addr >= g_watch_page_host_base + 0x1000)
        return EXCEPTION_CONTINUE_SEARCH;

    EnterCriticalSection(&g_watch_lock);
    /* Unconditional, never capped: if RECOMP_WATCH_EXACT names one specific
     * byte offset in the page, always report a write there even after the
     * general cap below is exhausted by unrelated nearby traffic -- added
     * specifically to reliably answer "does 0x443040 ever get written
     * later in a long run" without a burst of earlier, already-understood
     * writes (sub_00428010's own format setup) silently eating the cap. */
    if (g_watch_exact_offset != (uintptr_t)-1 &&
        addr - g_watch_page_host_base == g_watch_exact_offset) {
        static int exact_hits;
        exact_hits++;
        fprintf(stderr, "  [PBWATCH-EXACT] hit #%d at target offset, rip=0x%llX value_being_written=(see next single-step)\n",
                exact_hits, (unsigned long long)ep->ContextRecord->Rip);
        char storage[sizeof(SYMBOL_INFO) + 256] = {0};
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, symbol))
            fprintf(stderr, "  [PBWATCH-EXACT] function=%s+0x%llX\n", symbol->Name,
                    (unsigned long long)displacement);
        {
            IMAGEHLP_LINE64 line = {0};
            DWORD line_displacement = 0;
            line.SizeOfStruct = sizeof line;
            if (SymGetLineFromAddr64(GetCurrentProcess(), ep->ContextRecord->Rip,
                                     &line_displacement, &line))
                fprintf(stderr, "  [PBWATCH-EXACT] source=%s:%lu+0x%lX eax=%08X ecx=%08X edx=%08X esp=%08X ebx=%08X esi=%08X edi=%08X\n",
                        line.FileName, line.LineNumber, line_displacement,
                        g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi);
        }
        fflush(stderr);
    }
    /* De-duplicate by call site: a single bulk memmove/memset can touch this
     * whole page one byte at a time, which would otherwise burn the entire
     * cap on one uninteresting repeated instruction and hide every distinct
     * call site after it. Only print when the faulting Rip changes.
     * Also skip the first 0xC00 bytes of the page entirely: the actual
     * target words this session is chasing are at page offset 0xDC0-0xDD8
     * (guest 0x83E50DC0), and everything before that so far has turned out
     * to be unrelated device render-state setup (sub_00426FE0/sub_00426EF0:
     * combiner/transform defaults, not vertex data) -- not worth spending
     * the cap on a second time. */
    uintptr_t off_in_page = addr - g_watch_page_host_base;
    if (off_in_page >= g_watch_min_offset && g_watch_hits < 30 &&
        (uintptr_t)ep->ContextRecord->Rip != g_watch_last_rip) {
        g_watch_last_rip = (uintptr_t)ep->ContextRecord->Rip;
        g_watch_hits++;
        fprintf(stderr, "  [PBWATCH] write #%d host_addr=0x%llX rip=0x%llX "
                "eax=%08X ecx=%08X edx=%08X esp=%08X ebx=%08X esi=%08X edi=%08X\n",
                g_watch_hits, (unsigned long long)addr,
                (unsigned long long)ep->ContextRecord->Rip,
                g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi);
        char storage[sizeof(SYMBOL_INFO) + 256] = {0};
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, symbol))
            fprintf(stderr, "  [PBWATCH] function=%s+0x%llX\n", symbol->Name,
                    (unsigned long long)displacement);
        {
            IMAGEHLP_LINE64 line = {0};
            DWORD line_displacement = 0;
            line.SizeOfStruct = sizeof line;
            if (SymGetLineFromAddr64(GetCurrentProcess(), ep->ContextRecord->Rip,
                                     &line_displacement, &line))
                fprintf(stderr, "  [PBWATCH] source=%s:%lu+0x%lX\n", line.FileName,
                        line.LineNumber, line_displacement);
        }
        fflush(stderr);
    }
    DWORD old_protect;
    VirtualProtect((void *)g_watch_page_host_base, 0x1000, PAGE_READWRITE, &old_protect);
    g_watch_stepping = 1;
    ep->ContextRecord->EFlags |= 0x100u; /* TF: trap after this one instruction */
    LeaveCriticalSection(&g_watch_lock);
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* Multi-address execution-count breakpoint, opt-in via RECOMP_EXEC_WATCH=1.
 *
 * Session context: mapped a whole cooperative per-frame scheduler cluster
 * (sub_00028C40, sub_00028BC0, sub_000292E0, sub_000294C0, sub_000366D0,
 * sub_00038EA0, sub_0002FC60, sub_0002AB20, sub_0002C030, sub_0002FC30,
 * sub_00036530, sub_00038FC0, sub_0012D150, sub_0012CB50, sub_0029B6D0,
 * sub_001461D0) by reading generated code, and proved several of its own
 * counters plateau permanently after a small burst while the DPC/timer
 * heartbeat underneath keeps running forever. The next question -- which
 * of these members keeps running on retail but stops on native -- needs
 * a per-function call count, not another write-watch. RECOMP_ABI_CALL is
 * a direct C call (not through a function pointer this host controls), so
 * the only way to intercept every call site to a given function at once is
 * a software breakpoint at that function's own compiled entry: patch the
 * first byte to 0xCC, catch EXCEPTION_BREAKPOINT, restore the byte and
 * single-step exactly one real instruction, then re-patch on the trap --
 * the same single-step re-arm shape as pb_write_watch above, applied to
 * code instead of a data page. */
#define EXEC_WATCH_MAX 128
static struct {
    uint32_t guest_va;
    unsigned char *host_addr;
    unsigned char orig_byte;
    volatile LONG hits;
    const char *name;
    volatile uint32_t last_return_addr; /* MEM32(g_esp) at the most recent
        hit -- the guest return address is always there at function entry
        regardless of frame type (standard_frame vs fpo_leaf only affects
        what the callee does with ITS OWN ebp afterward, not the caller's
        already-pushed return address). Lets a hit answer "entered from
        where" without a separate per-site trace. */
} g_exec_watch[EXEC_WATCH_MAX];
static int g_exec_watch_count;
static CRITICAL_SECTION g_exec_watch_lock;
static RECOMP_TLS int g_exec_watch_stepping_index = -1;

static LONG CALLBACK exec_count_watch(EXCEPTION_POINTERS *ep)
{
    if (g_exec_watch_count == 0)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_SINGLE_STEP) {
        if (g_exec_watch_stepping_index < 0)
            return EXCEPTION_CONTINUE_SEARCH;
        int i = g_exec_watch_stepping_index;
        g_exec_watch_stepping_index = -1;
        EnterCriticalSection(&g_exec_watch_lock);
        DWORD old_protect;
        VirtualProtect(g_exec_watch[i].host_addr, 1, PAGE_EXECUTE_READWRITE, &old_protect);
        *g_exec_watch[i].host_addr = 0xCC;
        VirtualProtect(g_exec_watch[i].host_addr, 1, old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), g_exec_watch[i].host_addr, 1);
        LeaveCriticalSection(&g_exec_watch_lock);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

    unsigned char *rip = (unsigned char *)ep->ContextRecord->Rip;
    for (int i = 0; i < g_exec_watch_count; i++) {
        if (g_exec_watch[i].host_addr != rip)
            continue;
        InterlockedIncrement(&g_exec_watch[i].hits);
        g_exec_watch[i].last_return_addr =
            *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + g_esp);
        EnterCriticalSection(&g_exec_watch_lock);
        DWORD old_protect;
        VirtualProtect(g_exec_watch[i].host_addr, 1, PAGE_EXECUTE_READWRITE, &old_protect);
        *g_exec_watch[i].host_addr = g_exec_watch[i].orig_byte;
        VirtualProtect(g_exec_watch[i].host_addr, 1, old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), g_exec_watch[i].host_addr, 1);
        LeaveCriticalSection(&g_exec_watch_lock);
        g_exec_watch_stepping_index = i;
        ep->ContextRecord->EFlags |= 0x100u; /* TF: trap after the restored instruction */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void exec_watch_add(uint32_t guest_va, const char *name)
{
    typedef void (*exec_watch_fn)(void);
    extern exec_watch_fn recomp_lookup(uint32_t address);
    if (g_exec_watch_count >= EXEC_WATCH_MAX) {
        fprintf(stderr, "  [EXECWATCH] table full (%d), dropping guest 0x%08X (%s)\n",
                EXEC_WATCH_MAX, guest_va, name);
        fflush(stderr);
        return;
    }
    void *fn = (void *)recomp_lookup(guest_va);
    if (!fn) {
        fprintf(stderr, "  [EXECWATCH] guest 0x%08X (%s) has no native mapping, skipping\n", guest_va, name);
        return;
    }
    int i = g_exec_watch_count++;
    g_exec_watch[i].guest_va = guest_va;
    g_exec_watch[i].host_addr = (unsigned char *)fn;
    g_exec_watch[i].name = name;
    DWORD old_protect;
    VirtualProtect(fn, 1, PAGE_EXECUTE_READWRITE, &old_protect);
    g_exec_watch[i].orig_byte = *g_exec_watch[i].host_addr;
    *g_exec_watch[i].host_addr = 0xCC;
    VirtualProtect(fn, 1, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), fn, 1);
    fprintf(stderr, "  [EXECWATCH] armed guest 0x%08X (%s) at host %p\n", guest_va, name, fn);
}

/* 2026-09-21 diagnostic: confirmed native does NOT use the one call site
 * (0x0007481B) the live hybrid-xemu reference showed as 100% hot -- so
 * sub_00028F70 (17 total static call sites) is reached through one of the
 * other 16 in native. Plain increments (no I/O), same reasoning as above:
 * fprintf-based tracing inside the hot scheduler files perturbed this
 * exact timing-sensitive race badly enough to make the working profile
 * essentially unreachable (0/20 vs the usual ~1-in-2). Always on,
 * negligible cost. Index order matches the call-site list in
 * PROJECT_STATUS.md / the grep for RECOMP_ABI_CALL(0x00028F70u. */
volatile long nfl2k5_28f70_site_hits[17];
static const uint32_t nfl2k5_28f70_site_retaddrs[17] = {
    0x000745A0u, 0x0007481Bu, 0x000F504Bu, 0x000F5D7Cu, 0x000F5D9Du,
    0x000F5DBCu, 0x000F5F7Bu, 0x0011B27Bu, 0x0012DF5Du, 0x0014E2BBu,
    0x0016A550u, 0x00177A16u, 0x00178539u, 0x0024A023u, 0x0024A1A4u,
    0x0024A302u, 0x0024A450u,
};

/* Bisecting sub_000748A0's own 208-instruction straight-line body
 * (recomp_0003.c:31162-31853) to find how far it actually gets before
 * whatever blocks it -- exec_watch_add can't target these, they're
 * internal labels, not registered function entries. */
volatile long nfl2k5_748a0_markers[8];

extern volatile LONG nfl2k5_gpu_wait_seed_calls;
extern volatile uint32_t nfl2k5_gpu_wait_seed_value;
extern volatile LONG nfl2k5_gpu_wait_entry_calls;
extern volatile uint32_t nfl2k5_gpu_wait_entry_value;
extern volatile LONG nfl2k5_gpu_wait_exit_calls;
extern volatile LONG nfl2k5_gpu_notify_service_calls;
extern volatile LONG nfl2k5_drain33660_entry_calls;
extern volatile uint32_t nfl2k5_drain33660_entry_context;
extern volatile uint32_t nfl2k5_drain33660_entry_value;
extern volatile LONG nfl2k5_drain33660_retry_calls;
extern volatile uint32_t nfl2k5_drain33660_retry_value;
extern volatile uint32_t nfl2k5_drain33660_first_value;
extern volatile LONG nfl2k5_scheduler_samples;
extern volatile uint32_t nfl2k5_scheduler_callback;
extern volatile uint32_t nfl2k5_scheduler_callback_entered, nfl2k5_scheduler_callback_returned;
extern volatile LONG nfl2k5_scheduler_callback_entries, nfl2k5_scheduler_callback_returns;
extern volatile LONG nfl2k5_scheduler_registration_attempts;
extern volatile uint32_t nfl2k5_scheduler_registration_callback, nfl2k5_scheduler_registration_caller;
extern volatile LONG nfl2k5_scheduler_dispatch_count;
extern volatile LONG nfl2k5_scheduler_registration_history_index;
extern volatile uint32_t nfl2k5_scheduler_registration_history_count[16];
extern volatile uint32_t nfl2k5_scheduler_registration_history_callback[16];
extern volatile uint32_t nfl2k5_scheduler_registration_history_caller[16];
extern volatile LONG nfl2k5_input_init_stage;
extern volatile LONG nfl2k5_network_init_stage;
extern volatile uint32_t nfl2k5_network_init_status;
extern volatile uint32_t nfl2k5_network_init_version;
extern volatile uint32_t nfl2k5_frontend_state_dispatch_this;
extern volatile LONG nfl2k5_state27_resource_list_calls;
extern volatile uint32_t nfl2k5_state27_resource_list_context;
extern volatile uint32_t nfl2k5_state27_resource_list_head;
extern volatile LONG nfl2k5_gpu_notify_register_calls;
extern volatile uint32_t nfl2k5_gpu_notify_register_callback;

/* Called from kernel_bridge.c's existing periodic [PIPE]/[DPC] stats block
 * so the counts are visible on every tick without a separate sample pass. */
void nfl2k5_execwatch_print(void)
{
    /* 2026-09-22: the game runs forever, so atexit() never fires and the
     * previously-existing dump only happened under RECOMP_NATIVE_SAMPLE,
     * which perturbs boot timing enough to change which path is taken.
     * RECOMP_ICALL_FEEDBACK_DUMP() itself is documented safe to call
     * repeatedly, but this function is NOT called only every 3 seconds --
     * it is also invoked directly from kernel_bridge.c's own internal
     * [PIPE]/[DPC] summary, which fires based on kernel call count and can
     * be far more frequent during a busy boot. That made the dump (an 8MB
     * g_icall_seen scan) fire often enough to visibly stall real progress
     * -- caught live via cdb, parked inside this exact fprintf call with
     * a full guest call stack behind it. Throttle independently here. */
    {
        static DWORD last_dump_tick;
        DWORD now = GetTickCount();
        if (now - last_dump_tick >= 30000) {
            last_dump_tick = now;
            RECOMP_ICALL_FEEDBACK_DUMP();
        }
    }
    for (int i = 0; i < 17; i++)
        if (nfl2k5_28f70_site_hits[i])
            fprintf(stderr, "  [SITECOUNT] ret=0x%08X hits=%ld\n",
                    nfl2k5_28f70_site_retaddrs[i], nfl2k5_28f70_site_hits[i]);
    {
        int any = 0;
        for (int i = 0; i < 8; i++) if (nfl2k5_748a0_markers[i]) any = 1;
        if (any) {
            fprintf(stderr, "  [748A0BISECT]");
            for (int i = 0; i < 8; i++)
                fprintf(stderr, " m%d=%ld", i, nfl2k5_748a0_markers[i]);
            fprintf(stderr, "\n");
        }
    }
    /* 2026-09-21: sub_00178150's new blocking wait -- see NFL2K5.exe main.c
     * comment near exec_watch_add(0x00178150...) for the full mechanism. */
    fprintf(stderr, "  [PEEK] BDEEF0=0x%08X\n",
            *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + 0xBDEEF0u));
    fprintf(stderr, "  [GPUWAIT] seed_calls=%ld seed_value=0x%08X entry_calls=%ld entry_value=0x%08X "
            "exit_calls=%ld notify_service_calls=%ld notify_register_calls=%ld notify_callback=0x%08X\n",
            nfl2k5_gpu_wait_seed_calls, nfl2k5_gpu_wait_seed_value,
            nfl2k5_gpu_wait_entry_calls, nfl2k5_gpu_wait_entry_value,
            nfl2k5_gpu_wait_exit_calls, nfl2k5_gpu_notify_service_calls,
            nfl2k5_gpu_notify_register_calls, nfl2k5_gpu_notify_register_callback);
    fprintf(stderr, "  [DRAIN33660] entry_calls=%ld context=0x%08X first_value=0x%08X entry_value=0x%08X "
            "retry_calls=%ld retry_value=0x%08X\n",
            nfl2k5_drain33660_entry_calls, nfl2k5_drain33660_entry_context,
            nfl2k5_drain33660_first_value, nfl2k5_drain33660_entry_value,
            nfl2k5_drain33660_retry_calls, nfl2k5_drain33660_retry_value);
    fprintf(stderr, "  [SCHED] samples=%ld callback=0x%08X entered=0x%08X entries=%ld returned=0x%08X returns=%ld "
            "reg_attempts=%ld reg_callback=0x%08X reg_caller=0x%08X dispatch_count=%ld\n",
            nfl2k5_scheduler_samples, nfl2k5_scheduler_callback,
            nfl2k5_scheduler_callback_entered, nfl2k5_scheduler_callback_entries,
            nfl2k5_scheduler_callback_returned, nfl2k5_scheduler_callback_returns,
            nfl2k5_scheduler_registration_attempts, nfl2k5_scheduler_registration_callback,
            nfl2k5_scheduler_registration_caller, nfl2k5_scheduler_dispatch_count);
    fprintf(stderr, "  [NETINIT] input_stage=%ld network_stage=%ld network_status=0x%08X network_version=0x%08X\n",
            nfl2k5_input_init_stage, nfl2k5_network_init_stage,
            nfl2k5_network_init_status, nfl2k5_network_init_version);
    fprintf(stderr, "  [STATEOBJ] this=0x%08X\n", nfl2k5_frontend_state_dispatch_this);
    fprintf(stderr, "  [STATE27LIST] calls=%ld context=0x%08X head=0x%08X\n",
            nfl2k5_state27_resource_list_calls, nfl2k5_state27_resource_list_context,
            nfl2k5_state27_resource_list_head);
    {
        LONG hn = nfl2k5_scheduler_registration_history_index;
        int hcount = hn > 16 ? 16 : (int)hn;
        for (int i = 0; i < hcount; i++)
            fprintf(stderr, "  [SCHEDREG] slot=%d count=%u callback=0x%08X caller=0x%08X\n",
                    i, nfl2k5_scheduler_registration_history_count[i],
                    nfl2k5_scheduler_registration_history_callback[i],
                    nfl2k5_scheduler_registration_history_caller[i]);
    }
    {
        /* 2026-09-22: direct live read of the archive-object candidate at
         * 0xA77E38, every tick -- cross-checking the hw_watch "never
         * written" result (0xA77E38+0x14 == 1 was read live in an earlier
         * session, but the field's as-compiled static value in the XBE's
         * .data section is 0, not 1) against what this exact build/run
         * actually shows right now, over time, not a single snapshot. */
        uintptr_t base = (uintptr_t)xbox_GetMemoryOffset() + 0xA77E38u;
        fprintf(stderr, "  [OBJWATCH] A77E38: +00=0x%08X +04=0x%08X +10=0x%08X +14=0x%08X +18=0x%08X +5C=0x%08X\n",
                *(uint32_t *)(base + 0x00), *(uint32_t *)(base + 0x04),
                *(uint32_t *)(base + 0x10), *(uint32_t *)(base + 0x14),
                *(uint32_t *)(base + 0x18), *(uint32_t *)(base + 0x5C));
        /* 2026-09-22: sub_00042F50 (the real live gate on sub_00043980, via
         * sub_00043BE0 -- sub_00043AC0 is dead code, 0 calls every run) is a
         * linked-list search whose head is MEM32(0xB09578). It returned 0 on
         * the one time this ran; watch the head directly to see whether the
         * list is simply empty or has entries that just never match. */
        {
            uintptr_t off = (uintptr_t)xbox_GetMemoryOffset();
            uint32_t head = *(uint32_t *)(off + 0xB09578u);
            fprintf(stderr, "  [LISTWATCH] B09578 head=0x%08X", head);
            uint32_t node = head;
            for (int i = 0; i < 8 && node; i++) {
                uint32_t key = *(uint32_t *)(off + node + 8);
                uint32_t next = *(uint32_t *)(off + node);
                fprintf(stderr, " -> [0x%08X key@+8=0x%08X]", node, key);
                if (next == node) { fprintf(stderr, "(self-loop)"); break; }
                node = next;
            }
            fprintf(stderr, "\n");
        }
    }
    if (g_exec_watch_count == 0)
        return;
    fprintf(stderr, "  [EXECWATCH]");
    for (int i = 0; i < g_exec_watch_count; i++)
        fprintf(stderr, " %s=%ld", g_exec_watch[i].name, g_exec_watch[i].hits);
    fprintf(stderr, "\n");
    for (int i = 0; i < g_exec_watch_count; i++)
        if (g_exec_watch[i].hits)
            fprintf(stderr, "  [EXECWATCH-RET] %s last_ret=0x%08X\n",
                    g_exec_watch[i].name, g_exec_watch[i].last_return_addr);
}

/* Fixed-interval flush of the above, independent of kernel_bridge.c's own
 * dispatch-count-gated summary (see call site in main() for why). */
static DWORD WINAPI nfl2k5_execwatch_timer_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(3000);
        fprintf(stderr, "  [TIMERFLUSH]\n");
        nfl2k5_execwatch_print();
        fflush(stderr);
    }
    return 0;
}

/* Self-contained 24-bit BMP writer, no window/message-pump/GDI dependency
 * at all -- see nfl2k5_forced_display_thread for why that matters. */
static void nfl2k5_write_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h, uint32_t pitch)
{
    uint32_t row = ((w * 3u) + 3u) & ~3u;
    uint32_t img = row * h, total = 54u + img, x, y;
    uint8_t hdr[54], *line;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &total, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    line = (uint8_t *)calloc(1, row);
    for (y = 0; y < h; y++) {
        const uint8_t *src = bgra + (size_t)(h - 1 - y) * pitch;
        for (x = 0; x < w; x++) {
            line[x * 3 + 0] = src[x * 4 + 0];
            line[x * 3 + 1] = src[x * 4 + 1];
            line[x * 3 + 2] = src[x * 4 + 2];
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

/* 2026-09-21: FORCED DISPLAY, at explicit user direction ("do anything to
 * get that GPU to draw, force it, I don't care") -- not a fix, not tied to
 * the real NV2A/D3D8 pipeline at all. The real rendering pipeline still
 * has never submitted a draw (draws=0 / rasterised 0 triangles in every
 * run tonight); rather than wait for that investigation to reach a visible
 * pixel, this writes a synthetic animated pattern directly into a guest
 * buffer this thread owns, completely bypassing the game's own logic, the
 * NV2A emulation, and every bug documented above.
 *
 * Also tried pointing the existing framebuffer display window
 * (external/xboxrecomp/src/video/fb_present.c) at this buffer -- live-
 * debugged (cdb, non-invasive attach) a real, reproducible hang in
 * PeekMessage itself, inside a system-wide MSCTF WinEvent hook contending
 * a lock with something else on this machine's very busy desktop
 * (confirmed not an IME-association issue on our own window: disassociating
 * IME and dropping TranslateMessage did not fix it, and the stack showed
 * USER32!_ClientCallWinEventProc firing from inside NtUserPeekMessage
 * itself, before our code even runs). That is an environment problem, not
 * this project's bug, and not fixable by us. Dumping a BMP directly
 * (no window, no message pump, no GDI/USER32 involvement at all) sidesteps
 * it completely and is at least as good for "prove something draws". */
static DWORD WINAPI nfl2k5_forced_display_thread(LPVOID unused)
{
    const uint32_t w = 640, h = 480, pitch = w * 4;
    uint32_t fb_va = xbox_HeapAlloc(pitch * h, 4096);
    uint32_t frame = 0;
    (void)unused;
    if (!fb_va) return 0;
    fprintf(stderr, "  [FORCED-DISPLAY] synthetic framebuffer at guest VA 0x%08X (%ux%u), dumping to "
            NFL2K5_PROJECT_ROOT "/logs/forced-display.bmp every ~1s\n", fb_va, w, h);
    for (;;) {
        uint8_t *px = (uint8_t *)((uintptr_t)xbox_GetMemoryOffset() + fb_va);
        uint32_t x, y;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint8_t *p = px + (size_t)y * pitch + (size_t)x * 4;
                int band = ((int)x + (int)frame) / 40 % 8;
                static const uint8_t bar_b[8] = {  0,255,  0,255,  0,255,  0,255};
                static const uint8_t bar_g[8] = {  0,  0,255,255,  0,  0,255,255};
                static const uint8_t bar_r[8] = {  0,  0,  0,  0,255,255,255,255};
                uint8_t stripe = ((x + y + frame) / 8) & 1 ? 255 : 200;
                p[0] = (uint8_t)((bar_b[band] * stripe) / 255);
                p[1] = (uint8_t)((bar_g[band] * stripe) / 255);
                p[2] = (uint8_t)((bar_r[band] * stripe) / 255);
                p[3] = 255;
            }
        }
        if (frame % 60 == 0)
            nfl2k5_write_bmp(NFL2K5_PROJECT_ROOT "/logs/forced-display.bmp", px, w, h, pitch);
        frame++;
        Sleep(16);
    }
    return 0;
}

/* Multi-address READ-or-write catch, opt-in via RECOMP_DATA_WATCH=1.
 *
 * Session context: traced "no rendering" all the way down to a vtable-style
 * callback slot (sub_00278310, a real thiscall/game_vtable C++ method, and
 * sub_00363350) that is never invoked -- confirmed at 0 hits by
 * RECOMP_EXEC_WATCH above, across every run tonight. Found where that
 * function pointer actually lives in the *original* XBE's raw bytes (not
 * reachable by grepping generated C, since nothing here calls it by name):
 * .rdata, guest VA 0x0051980C and 0x0051A32C hold literal 0x00278310, each
 * inside a small fixed record (a name-string-shaped field, some flag
 * dwords, then the callback, then zero padding) -- these are XBE-baked,
 * read-only, compile-time data, not something written at runtime. The
 * remaining question is who *reads* this record and invokes the callback
 * it holds -- unanswerable by grepping C source for the literal address,
 * since a table walk computes the address at runtime. PAGE_NOACCESS traps
 * both reads and writes (unlike pb_write_watch's PAGE_READONLY, which only
 * traps writes) -- exactly what's needed to catch a read of read-only
 * data. Same single-step re-arm shape as the other two watches above. */
#define DATA_WATCH_MAX 8
static struct {
    uint32_t guest_va;
    uintptr_t host_page_base;
    volatile LONG hits;
    const char *name;
} g_data_watch[DATA_WATCH_MAX];
static int g_data_watch_count;
static CRITICAL_SECTION g_data_watch_lock;
static RECOMP_TLS int g_data_watch_stepping_index = -1;

static LONG CALLBACK data_read_watch(EXCEPTION_POINTERS *ep)
{
    if (g_data_watch_count == 0)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_SINGLE_STEP) {
        if (g_data_watch_stepping_index < 0)
            return EXCEPTION_CONTINUE_SEARCH;
        int i = g_data_watch_stepping_index;
        g_data_watch_stepping_index = -1;
        EnterCriticalSection(&g_data_watch_lock);
        DWORD old_protect;
        VirtualProtect((void *)g_data_watch[i].host_page_base, 0x1000, PAGE_NOACCESS, &old_protect);
        LeaveCriticalSection(&g_data_watch_lock);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
    ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
    for (int i = 0; i < g_data_watch_count; i++) {
        if (addr < g_data_watch[i].host_page_base ||
            addr >= g_data_watch[i].host_page_base + 0x1000)
            continue;
        LONG n = InterlockedIncrement(&g_data_watch[i].hits);
        /* .rdata pages can be shared with unrelated nearby records; cap
         * printed output per address so an unexpectedly busy page can't
         * flood the log, while still counting every hit internally. */
        if (n <= 40) {
        fprintf(stderr, "  [DATAWATCH] hit #%ld %s guest_page=0x%08X faulted_host=0x%llX op=%s rip=0x%llX\n",
                n, g_data_watch[i].name, g_data_watch[i].guest_va,
                (unsigned long long)addr,
                op == 0 ? "READ" : op == 1 ? "WRITE" : "EXEC",
                (unsigned long long)ep->ContextRecord->Rip);
        char storage[sizeof(SYMBOL_INFO) + 256] = {0};
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, symbol))
            fprintf(stderr, "  [DATAWATCH] function=%s+0x%llX\n", symbol->Name,
                    (unsigned long long)displacement);
        {
            IMAGEHLP_LINE64 line = {0};
            DWORD line_displacement = 0;
            line.SizeOfStruct = sizeof line;
            if (SymGetLineFromAddr64(GetCurrentProcess(), ep->ContextRecord->Rip,
                                     &line_displacement, &line))
                fprintf(stderr, "  [DATAWATCH] source=%s:%lu+0x%lX\n", line.FileName,
                        line.LineNumber, line_displacement);
        }
        fflush(stderr);
        }
        EnterCriticalSection(&g_data_watch_lock);
        DWORD old_protect;
        VirtualProtect((void *)g_data_watch[i].host_page_base, 0x1000, PAGE_READWRITE, &old_protect);
        g_data_watch_stepping_index = i;
        ep->ContextRecord->EFlags |= 0x100u;
        LeaveCriticalSection(&g_data_watch_lock);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void data_watch_add(uint32_t guest_va, const char *name)
{
    if (g_data_watch_count >= DATA_WATCH_MAX) return;
    uintptr_t target = (uintptr_t)xbox_GetMemoryOffset() + guest_va;
    int i = g_data_watch_count++;
    g_data_watch[i].guest_va = guest_va;
    g_data_watch[i].name = name;
    g_data_watch[i].host_page_base = target & ~(uintptr_t)0xFFFu;
    DWORD old_protect;
    if (!VirtualProtect((void *)g_data_watch[i].host_page_base, 0x1000, PAGE_NOACCESS, &old_protect)) {
        fprintf(stderr, "  [DATAWATCH] guest 0x%08X (%s): failed to guard page\n", guest_va, name);
        g_data_watch_count--;
        return;
    }
    fprintf(stderr, "  [DATAWATCH] armed guest 0x%08X (%s) page host 0x%llX\n",
            guest_va, name, (unsigned long long)g_data_watch[i].host_page_base);
}

/* Hardware debug-register (DR0-DR3) watch, opt-in via RECOMP_HW_WATCH=1.
 *
 * Built 2026-09-21 specifically because RECOMP_DATA_WATCH's page-guard
 * approach (PAGE_NOACCESS + single-step re-arm) was confirmed to perturb
 * the exact timing-sensitive main-loop stall this is meant to diagnose:
 * guarding the page containing guest 0xB04EC0 alone was enough to make the
 * loop never start at all, in every attempt (see PROJECT_STATUS.md,
 * 2026-09-20/21). A CPU hardware breakpoint has no per-access cost when it
 * doesn't fire -- the processor itself compares the address on every
 * memory access, no exception until the exact watched bytes are touched --
 * so it should not have the same problem.
 *
 * Limitation, accepted deliberately rather than solved: DR0-DR3 are
 * per-thread state (loaded into the CPU on every context switch from each
 * thread's own CONTEXT), not global, so every host thread that might
 * execute the write needs it armed individually. Installed on every thread
 * that exists at install time, plus re-armed on every newly-seen thread ID
 * from a periodic rescan hooked into the existing [PIPE]/[DPC] stats tick
 * (kernel_bridge.c) -- cheap (one CreateToolhelp32Snapshot + a skip-if-
 * already-armed check) and catches worker/guest threads spawned after
 * install without needing a dedicated CreateThread hook. */
#define HW_WATCH_MAX_THREADS 64
static uint32_t g_hw_watch_guest_va;
static uintptr_t g_hw_watch_host_addr;
static volatile LONG g_hw_watch_active;
static volatile LONG g_hw_watch_hits;
static DWORD g_hw_watch_armed_tids[HW_WATCH_MAX_THREADS];
static int g_hw_watch_armed_count;

static BOOL hw_watch_arm_thread(DWORD tid)
{
    BOOL is_self = (tid == GetCurrentThreadId());
    HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                          FALSE, tid);
    if (!h) return FALSE;

    if (!is_self) SuspendThread(h);

    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    BOOL ok = GetThreadContext(h, &ctx);
    if (ok) {
        ctx.Dr0 = (DWORD64)g_hw_watch_host_addr;
        /* DR7: L0=1 (bit0, local enable), R/W0=01 (bits16-17, write-only),
         * LEN0=11 (bits18-19, 4-byte range). Leave DR1-DR3 (and their L/RW/
         * LEN fields) untouched -- some other legitimate debugger/runtime
         * facility could be using them; only ever set/clear bit 0 and its
         * own condition/length fields here. */
        ctx.Dr7 = (ctx.Dr7 & ~(DWORD64)0xF0003ull) | 0x1ull | (0x1ull << 16) | (0x3ull << 18);
        ok = SetThreadContext(h, &ctx);
    }

    if (!is_self) ResumeThread(h);
    CloseHandle(h);
    return ok;
}

static void hw_watch_rescan(void)
{
    if (!g_hw_watch_active) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {0};
    te.dwSize = sizeof te;
    DWORD self_pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != self_pid) continue;
            BOOL already = FALSE;
            for (int i = 0; i < g_hw_watch_armed_count; i++)
                if (g_hw_watch_armed_tids[i] == te.th32ThreadID) { already = TRUE; break; }
            if (already) continue;
            if (hw_watch_arm_thread(te.th32ThreadID)) {
                if (g_hw_watch_armed_count < HW_WATCH_MAX_THREADS)
                    g_hw_watch_armed_tids[g_hw_watch_armed_count++] = te.th32ThreadID;
                fprintf(stderr, "  [HWWATCH] armed thread %lu\n", (unsigned long)te.th32ThreadID);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static LONG CALLBACK hw_watch_handler(EXCEPTION_POINTERS *ep)
{
    if (!g_hw_watch_active)
        return EXCEPTION_CONTINUE_SEARCH;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    /* Dr6 bit 0 (B0) means DR0's condition matched. A hardware breakpoint
     * needs no re-arming and no permission changes -- just report and
     * clear the sticky status bits so the next real hit is visible. */
    if (!(ep->ContextRecord->Dr6 & 0x1))
        return EXCEPTION_CONTINUE_SEARCH;

    LONG n = InterlockedIncrement(&g_hw_watch_hits);
    fprintf(stderr, "  [HWWATCH] hit #%ld guest_va=0x%08X rip=0x%llX\n",
            n, g_hw_watch_guest_va, (unsigned long long)ep->ContextRecord->Rip);
    char storage[sizeof(SYMBOL_INFO) + 256] = {0};
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
    DWORD64 displacement = 0;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, symbol))
        fprintf(stderr, "  [HWWATCH] function=%s+0x%llX\n", symbol->Name,
                (unsigned long long)displacement);
    {
        IMAGEHLP_LINE64 line = {0};
        DWORD line_displacement = 0;
        line.SizeOfStruct = sizeof line;
        if (SymGetLineFromAddr64(GetCurrentProcess(), ep->ContextRecord->Rip,
                                 &line_displacement, &line))
            fprintf(stderr, "  [HWWATCH] source=%s:%lu+0x%lX\n", line.FileName,
                    line.LineNumber, line_displacement);
    }
    fflush(stderr);

    ep->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void hw_watch_install(uint32_t guest_va, const char *name)
{
    g_hw_watch_guest_va = guest_va;
    g_hw_watch_host_addr = (uintptr_t)xbox_GetMemoryOffset() + guest_va;
    if (!AddVectoredExceptionHandler(1, hw_watch_handler)) {
        fprintf(stderr, "  [HWWATCH] failed to install exception handler\n");
        return;
    }
    g_hw_watch_active = 1;
    hw_watch_rescan();
    fprintf(stderr, "  [HWWATCH] armed guest 0x%08X (%s) host 0x%llX on %d thread(s)\n",
            guest_va, name, (unsigned long long)g_hw_watch_host_addr, g_hw_watch_armed_count);
}

void nfl2k5_hw_watch_rescan(void)
{
    hw_watch_rescan();
}

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
extern volatile LONG nfl2k5_frontend_samples;
extern volatile uint32_t nfl2k5_frontend_queue, nfl2k5_frontend_pending;
extern volatile uint32_t nfl2k5_frontend_head, nfl2k5_frontend_tail;
extern volatile uint32_t nfl2k5_frontend_state, nfl2k5_frontend_request;
extern volatile uint32_t nfl2k5_frontend_active;
extern volatile LONG nfl2k5_frontend_prepare_calls, nfl2k5_frontend_prepare_ok;
extern volatile uint32_t nfl2k5_frontend_prepare_result;
extern volatile LONG nfl2k5_frontend_submit_calls, nfl2k5_frontend_submit_ok;
extern volatile uint32_t nfl2k5_frontend_submit_result;
extern volatile uint32_t nfl2k5_frontend_submit_status;
extern volatile uint32_t nfl2k5_frontend_range_source;
extern volatile uint32_t nfl2k5_frontend_range_size;
extern volatile uint32_t nfl2k5_frontend_range_low;
extern volatile uint32_t nfl2k5_frontend_range_high;
extern volatile uint32_t nfl2k5_frontend_range_end_low;
extern volatile uint32_t nfl2k5_frontend_range_end_high;
extern volatile uint32_t nfl2k5_frontend_range_request;
extern volatile uint32_t nfl2k5_frontend_range_provider;
extern volatile LONG nfl2k5_frontend_state_counts[9];
extern volatile LONG nfl2k5_title_init_74bf0_calls;
extern volatile LONG nfl2k5_title_init_38fc0_calls;
extern volatile LONG nfl2k5_title_init_network_calls;
extern volatile LONG nfl2k5_title_init_checkpoint;
extern volatile LONG nfl2k5_async_items, nfl2k5_async_dispatches;
extern volatile LONG nfl2k5_archive_init_44c10_calls;
extern volatile uint32_t nfl2k5_archive_init_44c10_context, nfl2k5_archive_init_44c10_source;
extern volatile LONG nfl2k5_archive_setup_44d00_calls;
extern volatile uint32_t nfl2k5_archive_setup_44d00_counter;
extern volatile LONG nfl2k5_boot_task_3be40_calls, nfl2k5_boot_task_42440_calls;
extern volatile uint32_t nfl2k5_boot_task_3be40_owner, nfl2k5_boot_task_3be40_record;
extern volatile uint32_t nfl2k5_boot_task_3be40_completion, nfl2k5_boot_task_42440_result_slot;
extern volatile uint32_t nfl2k5_boot_task_3be40_state, nfl2k5_boot_task_3be40_type;
extern volatile uint32_t nfl2k5_boot_task_3be40_target, nfl2k5_boot_task_3be40_argument;
extern volatile uint32_t nfl2k5_boot_task_3be40_dispatch_lock_before, nfl2k5_boot_task_3be40_dispatch_lock_after;
extern volatile LONG nfl2k5_archive_dispatch_438d0_calls;
extern volatile uint32_t nfl2k5_archive_dispatch_438d0_callback, nfl2k5_archive_dispatch_438d0_tag, nfl2k5_archive_dispatch_438d0_result;
extern volatile LONG nfl2k5_archive_completion_44df0_calls;
extern volatile uint32_t nfl2k5_archive_completion_44df0_context;
extern volatile uint32_t nfl2k5_async_dispatch_branch;
extern volatile uint32_t nfl2k5_async_item, nfl2k5_async_item_type;
extern volatile uint32_t nfl2k5_async_provider;
extern volatile uint32_t nfl2k5_async_dispatch_type, nfl2k5_async_dispatch_target;
extern volatile LONG nfl2k5_frontend_state_dispatch_calls;
extern volatile uint32_t nfl2k5_frontend_state_dispatch_value;
extern volatile uint32_t nfl2k5_frontend_state_dispatch_tick;
extern volatile uint32_t nfl2k5_frontend_state_dispatch_limit;
extern volatile uint32_t nfl2k5_frontend_state_dispatch_flags;
extern volatile LONG nfl2k5_frontend_enqueue_calls;
extern volatile uint32_t nfl2k5_frontend_enqueue_caller, nfl2k5_frontend_enqueue_node, nfl2k5_frontend_enqueue_descriptor;
extern volatile LONG nfl2k5_frontend_producer_178150, nfl2k5_frontend_producer_272a60;
extern volatile LONG nfl2k5_frontend_event_4953c4;
extern volatile LONG nfl2k5_frontend_event_492e9b;
extern volatile LONG nfl2k5_frontend_event_49582b;
extern volatile LONG nfl2k5_frontend_event_492414, nfl2k5_frontend_event_48bb78;
extern volatile uint32_t nfl2k5_frontend_packet_alloc, nfl2k5_frontend_packet_submit;
extern volatile uint32_t nfl2k5_frontend_packet_route, nfl2k5_frontend_packet_token, nfl2k5_frontend_packet_flags;
extern volatile LONG nfl2k5_frontend_packet_route_counts[5];
extern volatile LONG nfl2k5_packet_pump_49516f, nfl2k5_packet_processor_48e0a6;
extern volatile LONG nfl2k5_packet_dpc_entries, nfl2k5_packet_dpc_gate_open;
extern volatile uint32_t nfl2k5_packet_dpc_context, nfl2k5_packet_dpc_pending;
extern volatile uint32_t nfl2k5_packet_dpc_limit, nfl2k5_packet_dpc_gate_result;
extern volatile uint32_t nfl2k5_packet_dpc_list_head;
extern volatile LONG nfl2k5_packet_dpc_handler_calls;
extern volatile uint32_t nfl2k5_packet_dpc_handler_item, nfl2k5_packet_dpc_handler_flags;
extern volatile uint32_t nfl2k5_packet_dpc_handler_target;
extern volatile LONG nfl2k5_packet_dpc_dequeue_calls;
extern volatile uint32_t nfl2k5_packet_dpc_dequeue_item, nfl2k5_packet_dpc_dequeue_flags;
extern volatile uint32_t nfl2k5_packet_dpc_dequeue_target;
extern volatile uint32_t nfl2k5_packet_dpc_dequeue_link, nfl2k5_packet_dpc_dequeue_metadata;
extern volatile uint32_t nfl2k5_packet_dpc_owner_flags;
extern volatile LONG nfl2k5_packet_buffer_install_calls;
extern volatile uint32_t nfl2k5_packet_buffer_install_context, nfl2k5_packet_buffer_install_buffer;
extern volatile LONG nfl2k5_packet_delivery_calls;
extern volatile uint32_t nfl2k5_packet_delivery_context, nfl2k5_packet_delivery_item;
extern volatile uint32_t nfl2k5_packet_delivery_route, nfl2k5_packet_delivery_target;
extern volatile LONG nfl2k5_packet_fallback_calls;
extern volatile uint32_t nfl2k5_packet_fallback_context, nfl2k5_packet_fallback_item;
extern volatile uint32_t nfl2k5_packet_fallback_length, nfl2k5_packet_fallback_alloc_fn;
extern volatile uint32_t nfl2k5_packet_fallback_free_fn, nfl2k5_packet_fallback_block;
extern volatile uint32_t nfl2k5_packet_fallback_after_alloc;
extern volatile LONG nfl2k5_frontend_boot_steps;
extern volatile uint32_t nfl2k5_frontend_boot_stage, nfl2k5_frontend_boot_result;
extern volatile LONG nfl2k5_frontend_nonempty_samples, nfl2k5_frontend_queue_pump_calls;
extern volatile LONG nfl2k5_worker_task_dispatches;
extern volatile uint32_t nfl2k5_worker_task_target;
extern volatile LONG nfl2k5_worker_loop_entries, nfl2k5_worker_drain_calls;
extern volatile LONG nfl2k5_worker_entry_calls;
extern volatile uint32_t nfl2k5_worker_entry_target;
extern volatile LONG nfl2k5_worker_entry_359b0, nfl2k5_worker_entry_4d810, nfl2k5_worker_entry_other;
extern volatile LONG nfl2k5_worker_bridge_starts;
extern volatile uint32_t nfl2k5_worker_bridge_context;
extern volatile LONG nfl2k5_worker_handoff_parent_ready;
extern volatile LONG nfl2k5_worker_handoff_worker_ready;
extern volatile LONG nfl2k5_worker_handoff_worker_missed;
extern volatile uint32_t nfl2k5_worker_handoff_last_active;
extern volatile uint32_t nfl2k5_worker_handoff_last_ready;
extern volatile LONG nfl2k5_worker_idle_yields;
extern volatile LONG nfl2k5_worker_completion_calls, nfl2k5_worker_completion_idle_calls;
extern volatile LONG nfl2k5_worker_completion_active_calls, nfl2k5_worker_counter_releases;
extern volatile uint32_t nfl2k5_worker_completion_last_mode;
extern volatile LONG nfl2k5_worker_counter_acquires;
extern volatile uint32_t nfl2k5_worker_counter_acquire_caller, nfl2k5_worker_counter_release_caller;
extern volatile LONG nfl2k5_worker_reset_calls;
extern volatile uint32_t nfl2k5_worker_reset_caller;
extern volatile LONG nfl2k5_gpu_notify_register_calls;
extern volatile uint32_t nfl2k5_gpu_notify_register_callback, nfl2k5_gpu_notify_register_device;
extern volatile LONG nfl2k5_gpu_wait_seed_calls;
extern volatile uint32_t nfl2k5_gpu_wait_seed_value;
extern volatile LONG nfl2k5_gpu_notify_service_calls;
extern volatile LONG nfl2k5_gpu_wait_entry_calls;
extern volatile uint32_t nfl2k5_gpu_wait_entry_value;
extern volatile LONG nfl2k5_gpu_wait_exit_calls;
extern volatile LONG nfl2k5_scheduler_samples;
extern volatile uint32_t nfl2k5_scheduler_callback;
extern volatile uint32_t nfl2k5_scheduler_callback_entered, nfl2k5_scheduler_callback_returned;
extern volatile LONG nfl2k5_scheduler_callback_entries, nfl2k5_scheduler_callback_returns;
extern volatile LONG nfl2k5_scheduler_registration_attempts;
extern volatile uint32_t nfl2k5_scheduler_registration_callback, nfl2k5_scheduler_registration_caller;
extern volatile LONG nfl2k5_scheduler_registration_history_index;
extern volatile uint32_t nfl2k5_scheduler_registration_history_count[16];
extern volatile uint32_t nfl2k5_scheduler_registration_history_callback[16];
extern volatile uint32_t nfl2k5_scheduler_registration_history_caller[16];
extern volatile LONG nfl2k5_host_tick_count, nfl2k5_post_unlock_count;
extern volatile LONG nfl2k5_scheduler_dispatch_count;
extern volatile LONG nfl2k5_dpc_queued_count, nfl2k5_dpc_drained_count;
extern volatile LONG nfl2k5_dpc_unresolved_count;
extern volatile uint32_t nfl2k5_dpc_last_routine, nfl2k5_dpc_last_context;
extern volatile LONG nfl2k5_keinsert_dpc_count;
extern volatile uint32_t nfl2k5_keinsert_last_dpc, nfl2k5_keinsert_last_routine;
extern volatile uint32_t nfl2k5_keinsert_last_context;
extern volatile LONG nfl2k5_input_init_stage;
extern volatile LONG nfl2k5_network_init_stage;
extern volatile uint32_t nfl2k5_network_init_status;
extern volatile uint32_t nfl2k5_network_init_version;
extern volatile LONG nfl2k5_invalid_queue_objects;
extern volatile uint32_t nfl2k5_invalid_queue_object;
extern volatile uint32_t nfl2k5_invalid_queue_record;
typedef void (*guest_function)(void);
extern guest_function recomp_lookup(uint32_t address);
extern int recomp_dispatch_init(void);
extern void xbox_path_init(const char *game_dir, const char *save_dir);
static uint32_t main_stack_begin, main_stack_end;

static LONG CALLBACK crash_report(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "[CRASH] exception=0x%08lX host=0x%llX\n", code,
            (unsigned long long)ep->ContextRecord->Rip);
    if (code == EXCEPTION_ACCESS_VIOLATION)
        fprintf(stderr, "[CRASH] operation=%llu address=0x%llX\n",
                (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
                (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    fprintf(stderr, "[CRASH] eax=%08X ecx=%08X edx=%08X esp=%08X ebx=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi);
    if (g_esp >= main_stack_begin && g_esp < main_stack_end)
        for (uint32_t va = g_esp; va < main_stack_end && va - g_esp < 512; va += 4)
            fprintf(stderr, "    GS %08X %08X\n", va,
                    *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + va));
    if (code != EXCEPTION_STACK_OVERFLOW) {
        char storage[sizeof(SYMBOL_INFO) + 256] = {0};
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, symbol))
            fprintf(stderr, "[CRASH] function=%s+0x%llX\n", symbol->Name,
                    (unsigned long long)displacement);
        {
            IMAGEHLP_LINE64 line = {0};
            DWORD line_displacement = 0;
            line.SizeOfStruct = sizeof line;
            if (SymGetLineFromAddr64(GetCurrentProcess(), ep->ContextRecord->Rip,
                                     &line_displacement, &line))
                fprintf(stderr, "[CRASH] source=%s:%lu+0x%lX\n", line.FileName,
                        line.LineNumber, line_displacement);
        }
        /* 2026-09-21: a single frame isn't enough to tell which caller fed
         * this function a bad pointer/index -- walk the real host call
         * stack (every sub_XXXXXXXX is a real x86-64 function, called via
         * real `call` instructions) using the exception's own context,
         * the same info a live cdb `k` would use, but captured in-process
         * so every crash gets this for free without needing to catch the
         * process live under a debugger. */
        {
            STACKFRAME64 frame = {0};
            CONTEXT ctx = *ep->ContextRecord;
            frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
            fprintf(stderr, "[CRASH] backtrace:\n");
            for (int depth = 0; depth < 32; depth++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                                  GetCurrentThread(), &frame, &ctx, NULL,
                                  SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                    break;
                if (!frame.AddrPC.Offset) break;
                char fstorage[sizeof(SYMBOL_INFO) + 256] = {0};
                SYMBOL_INFO *fsym = (SYMBOL_INFO *)fstorage;
                DWORD64 fdisp = 0;
                fsym->SizeOfStruct = sizeof(SYMBOL_INFO);
                fsym->MaxNameLen = 255;
                if (SymFromAddr(GetCurrentProcess(), frame.AddrPC.Offset, &fdisp, fsym))
                    fprintf(stderr, "  [%d] %s+0x%llX\n", depth, fsym->Name,
                            (unsigned long long)fdisp);
                else
                    fprintf(stderr, "  [%d] 0x%llX\n", depth,
                            (unsigned long long)frame.AddrPC.Offset);
            }
        }
    }
    /* 2026-09-21: the fourth-crash family (sub_00038CD0's scheduler-slot
     * dispatch reading a corrupted esi right after invoking whichever
     * callback occupies the current slot; see PROJECT_STATUS.md) is only
     * explicable by an indirect call somewhere nearby leaving esp/esi/edi
     * misaligned. The last few real indirect-call targets executed, in
     * order, are exactly what's needed to identify which specific guest
     * callback did it -- print them here so any future crash carries this
     * for free. */
    {
        extern volatile uint32_t g_icall_trace[16];
        extern volatile uint32_t g_icall_trace_idx;
        uint32_t idx = g_icall_trace_idx;
        fprintf(stderr, "[CRASH] last 16 icall targets (oldest first):\n");
        for (int i = 0; i < 16; i++) {
            uint32_t slot = (idx + i) & 15;
            fprintf(stderr, "  0x%08X\n", g_icall_trace[slot]);
        }
    }
    fflush(stderr);
    /* A title that dies mid-boot is exactly the run whose indirect-call
     * targets are worth having; atexit (registered by the INIT call in
     * main()) does not reliably fire on this path. */
    RECOMP_ICALL_FEEDBACK_DUMP();
    return EXCEPTION_EXECUTE_HANDLER;
}

static uint32_t read32(const unsigned char *data, size_t offset)
{
    uint32_t value;
    memcpy(&value, data + offset, sizeof value);
    return value;
}

static DWORD WINAPI sample_native_startup(LPVOID thread)
{
    /* Sample later boot phases without repeatedly stopping the boot thread. */
    const char *delay_text = getenv("RECOMP_SAMPLE_DELAY_MS");
    unsigned long delay = delay_text ? strtoul(delay_text, NULL, 10) : 5000;
    if (delay < 100 || delay > 300000) delay = 5000;
    Sleep((DWORD)delay);
    CONTEXT context = {0};
    context.ContextFlags = CONTEXT_CONTROL;
    BOOL captured = FALSE;
    if (SuspendThread((HANDLE)thread) != (DWORD)-1) {
        captured = GetThreadContext((HANDLE)thread, &context);
        ResumeThread((HANDLE)thread);
    }
    CloseHandle((HANDLE)thread);
    if (captured) {
        HANDLE sample_file=CreateFileA(NFL2K5_PROJECT_ROOT "/logs/native-sample.txt",GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        char sample_text[512]; DWORD written;
        int sample_len=snprintf(sample_text,sizeof sample_text,"RIP=%llX RSP=%llX\r\n",(unsigned long long)context.Rip,(unsigned long long)context.Rsp);
        if(sample_file!=INVALID_HANDLE_VALUE)WriteFile(sample_file,sample_text,sample_len,&written,NULL);
        /* Write the timing-critical state before DbgHelp stack symbolization.
         * DbgHelp is process-global and can wait behind another thread, while
         * these counters and guest words are safe lock-free snapshots. */
        sample_len = snprintf(sample_text, sizeof sample_text,
            "LIVE scheduler=%ld callback=%08X enter=%08X/%ld return=%08X/%ld register=%08X/%08X/%ld frontend=%ld callbacks=%08X tick=%ld dispatch=%ld input=%ld network=%ld/%08X/%04X invalid-queue=%ld/%08X/%08X front-state=%u req=%08X active=%08X\r\n",
            nfl2k5_scheduler_samples, nfl2k5_scheduler_callback,
            nfl2k5_scheduler_callback_entered, nfl2k5_scheduler_callback_entries,
            nfl2k5_scheduler_callback_returned, nfl2k5_scheduler_callback_returns,
            nfl2k5_scheduler_registration_callback, nfl2k5_scheduler_registration_caller,
            nfl2k5_scheduler_registration_attempts,
            nfl2k5_frontend_samples,
            *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + 0x00B04D1Cu),
            nfl2k5_host_tick_count, nfl2k5_scheduler_dispatch_count,
            nfl2k5_input_init_stage, nfl2k5_network_init_stage,
            nfl2k5_network_init_status, nfl2k5_network_init_version,
            nfl2k5_invalid_queue_objects, nfl2k5_invalid_queue_object,
            nfl2k5_invalid_queue_record, nfl2k5_frontend_state,
            nfl2k5_frontend_request, nfl2k5_frontend_active);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "async items=%ld item=%08X type=%u provider=%08X dispatches=%ld type=%u target=%08X branch=%08X\r\n",
            nfl2k5_async_items, nfl2k5_async_item, nfl2k5_async_item_type,
            nfl2k5_async_provider, nfl2k5_async_dispatches,
            nfl2k5_async_dispatch_type, nfl2k5_async_dispatch_target,
            nfl2k5_async_dispatch_branch);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend state-dispatch calls=%ld state=%u tick=%u limit=%u flags=%02X queue-pump=%ld nonempty=%ld bridge-start=%ld/%08X worker-entry=%ld/%08X 359B0=%ld 4D810=%ld other=%ld loop=%ld drains=%ld tasks=%ld/%08X\r\n",
            nfl2k5_frontend_state_dispatch_calls,
            nfl2k5_frontend_state_dispatch_value,
            nfl2k5_frontend_state_dispatch_tick,
            nfl2k5_frontend_state_dispatch_limit,
            nfl2k5_frontend_state_dispatch_flags,
            nfl2k5_frontend_queue_pump_calls, nfl2k5_frontend_nonempty_samples,
            nfl2k5_worker_bridge_starts, nfl2k5_worker_bridge_context,
            nfl2k5_worker_entry_calls, nfl2k5_worker_entry_target,
            nfl2k5_worker_entry_359b0, nfl2k5_worker_entry_4d810,
            nfl2k5_worker_entry_other,
            nfl2k5_worker_loop_entries, nfl2k5_worker_drain_calls,
            nfl2k5_worker_task_dispatches, nfl2k5_worker_task_target);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet fallback calls=%ld ctx=%08X item=%08X len=%u alloc=%08X free=%08X block=%08X after=%08X\r\n",
            nfl2k5_packet_fallback_calls, nfl2k5_packet_fallback_context,
            nfl2k5_packet_fallback_item, nfl2k5_packet_fallback_length,
            nfl2k5_packet_fallback_alloc_fn, nfl2k5_packet_fallback_free_fn,
            nfl2k5_packet_fallback_block, nfl2k5_packet_fallback_after_alloc);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet DPC dequeue calls=%ld item=%08X flags=%08X target=%08X link=%08X meta=%08X owner=%08X\r\n",
            nfl2k5_packet_dpc_dequeue_calls, nfl2k5_packet_dpc_dequeue_item,
            nfl2k5_packet_dpc_dequeue_flags, nfl2k5_packet_dpc_dequeue_target,
            nfl2k5_packet_dpc_dequeue_link, nfl2k5_packet_dpc_dequeue_metadata,
            nfl2k5_packet_dpc_owner_flags);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "archive-completion 44DF0=%ld context=%08X\r\n",
            nfl2k5_archive_completion_44df0_calls, nfl2k5_archive_completion_44df0_context);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "archive-dispatch 438D0=%ld callback=%08X tag=%08X result=%08X\r\n",
            nfl2k5_archive_dispatch_438d0_calls, nfl2k5_archive_dispatch_438d0_callback,
            nfl2k5_archive_dispatch_438d0_tag, nfl2k5_archive_dispatch_438d0_result);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "boot-task 3BE40=%ld owner=%08X record=%08X completion=%08X state=%u type=%u target=%08X arg=%08X lock=%u/%u 42440=%ld slot=%08X\r\n",
            nfl2k5_boot_task_3be40_calls, nfl2k5_boot_task_3be40_owner,
            nfl2k5_boot_task_3be40_record, nfl2k5_boot_task_3be40_completion,
            nfl2k5_boot_task_3be40_state, nfl2k5_boot_task_3be40_type,
            nfl2k5_boot_task_3be40_target, nfl2k5_boot_task_3be40_argument,
            nfl2k5_boot_task_3be40_dispatch_lock_before, nfl2k5_boot_task_3be40_dispatch_lock_after,
            nfl2k5_boot_task_42440_calls, nfl2k5_boot_task_42440_result_slot);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "archive-init 44C10 calls=%ld context=%08X source=%08X\r\n",
            nfl2k5_archive_init_44c10_calls, nfl2k5_archive_init_44c10_context,
            nfl2k5_archive_init_44c10_source);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "archive-setup 44D00 calls=%ld counter=%08X\r\n",
            nfl2k5_archive_setup_44d00_calls, nfl2k5_archive_setup_44d00_counter);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "worker handoff parent-ready=%ld worker-ready=%ld worker-missed=%ld last=%u/%u\r\n",
            nfl2k5_worker_handoff_parent_ready,
            nfl2k5_worker_handoff_worker_ready,
            nfl2k5_worker_handoff_worker_missed,
            nfl2k5_worker_handoff_last_active,
            nfl2k5_worker_handoff_last_ready);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "worker idle yields=%ld\r\n", nfl2k5_worker_idle_yields);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "worker completion calls=%ld idle=%ld active=%ld acquire/release=%ld/%ld callers=%08X/%08X mode=%u\r\n",
            nfl2k5_worker_completion_calls, nfl2k5_worker_completion_idle_calls,
            nfl2k5_worker_completion_active_calls, nfl2k5_worker_counter_acquires,
            nfl2k5_worker_counter_releases, nfl2k5_worker_counter_acquire_caller,
            nfl2k5_worker_counter_release_caller,
            nfl2k5_worker_completion_last_mode);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "worker reset(359C0) calls=%ld caller=%08X\r\n",
            nfl2k5_worker_reset_calls, nfl2k5_worker_reset_caller);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "gpu notify(420810) register calls=%ld callback=%08X device=%08X\r\n",
            nfl2k5_gpu_notify_register_calls, nfl2k5_gpu_notify_register_callback,
            nfl2k5_gpu_notify_register_device);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "gpu wait(28DE0) seed calls=%ld value=%08X\r\n",
            nfl2k5_gpu_wait_seed_calls, nfl2k5_gpu_wait_seed_value);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "gpu notify service calls=%ld\r\n",
            nfl2k5_gpu_notify_service_calls);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "gpu wait(28DE0) entry calls=%ld value=%08X\r\n",
            nfl2k5_gpu_wait_entry_calls, nfl2k5_gpu_wait_entry_value);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "gpu wait(28DE0) exit calls=%ld\r\n",
            nfl2k5_gpu_wait_exit_calls);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend enqueue=%ld caller=%08X node=%08X descriptor=%08X\r\n",
            nfl2k5_frontend_enqueue_calls, nfl2k5_frontend_enqueue_caller,
            nfl2k5_frontend_enqueue_node, nfl2k5_frontend_enqueue_descriptor);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend producers 178150=%ld 272A60=%ld\r\n",
            nfl2k5_frontend_producer_178150, nfl2k5_frontend_producer_272a60);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend events 492E9B=%ld 49582B=%ld 4953C4=%ld 492414=%ld 48BB78=%ld\r\n",
            nfl2k5_frontend_event_492e9b, nfl2k5_frontend_event_49582b,
            nfl2k5_frontend_event_4953c4, nfl2k5_frontend_event_492414,
            nfl2k5_frontend_event_48bb78);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend packet alloc=%08X submit=%08X\r\n",
            nfl2k5_frontend_packet_alloc, nfl2k5_frontend_packet_submit);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend packet route=%u token=%08X flags=%08X\r\n",
            nfl2k5_frontend_packet_route, nfl2k5_frontend_packet_token,
            nfl2k5_frontend_packet_flags);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend packet routes=%ld,%ld,%ld,%ld,%ld\r\n",
            nfl2k5_frontend_packet_route_counts[0], nfl2k5_frontend_packet_route_counts[1],
            nfl2k5_frontend_packet_route_counts[2], nfl2k5_frontend_packet_route_counts[3],
            nfl2k5_frontend_packet_route_counts[4]);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet pump=49516F:%ld processor=48E0A6:%ld\r\n",
            nfl2k5_packet_pump_49516f, nfl2k5_packet_processor_48e0a6);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        {
            LONG history_end = nfl2k5_scheduler_registration_history_index;
            LONG history_start = history_end > 16 ? history_end - 16 : 0;
            for (LONG entry = history_start; entry < history_end; ++entry) {
                unsigned index = (unsigned)entry & 15u;
                sample_len = snprintf(sample_text, sizeof sample_text,
                    "scheduler-register[%ld] count=%u callback=%08X caller=%08X\r\n",
                    entry,
                    nfl2k5_scheduler_registration_history_count[index],
                    nfl2k5_scheduler_registration_history_callback[index],
                    nfl2k5_scheduler_registration_history_caller[index]);
                if (sample_file != INVALID_HANDLE_VALUE)
                    WriteFile(sample_file, sample_text, sample_len, &written, NULL);
            }
        }
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet DPC entries=%ld gate-open=%ld ctx=%08X pending=%u limit=%u gate=%08X list=%08X\r\n",
            nfl2k5_packet_dpc_entries, nfl2k5_packet_dpc_gate_open,
            nfl2k5_packet_dpc_context, nfl2k5_packet_dpc_pending,
            nfl2k5_packet_dpc_limit, nfl2k5_packet_dpc_gate_result,
            nfl2k5_packet_dpc_list_head);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet DPC handler calls=%ld item=%08X flags=%08X target=%08X\r\n",
            nfl2k5_packet_dpc_handler_calls, nfl2k5_packet_dpc_handler_item,
            nfl2k5_packet_dpc_handler_flags, nfl2k5_packet_dpc_handler_target);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet buffer install calls=%ld ctx=%08X buffer=%08X\r\n",
            nfl2k5_packet_buffer_install_calls, nfl2k5_packet_buffer_install_context,
            nfl2k5_packet_buffer_install_buffer);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "packet delivery calls=%ld ctx=%08X item=%08X route=%u target=%08X\r\n",
            nfl2k5_packet_delivery_calls, nfl2k5_packet_delivery_context,
            nfl2k5_packet_delivery_item, nfl2k5_packet_delivery_route,
            nfl2k5_packet_delivery_target);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend bootstrap steps=%ld stage=%u result=%08X\r\n",
            nfl2k5_frontend_boot_steps, nfl2k5_frontend_boot_stage,
            nfl2k5_frontend_boot_result);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "title init 74BF0=%ld 38FC0=%ld network=%ld checkpoint=%ld\r\n",
            nfl2k5_title_init_74bf0_calls, nfl2k5_title_init_38fc0_calls,
            nfl2k5_title_init_network_calls, nfl2k5_title_init_checkpoint);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend range req=%08X provider=%08X source=%08X size=%08X allowed=%08X:%08X end=%08X:%08X\r\n",
            nfl2k5_frontend_range_request, nfl2k5_frontend_range_provider,
            nfl2k5_frontend_range_source, nfl2k5_frontend_range_size,
            nfl2k5_frontend_range_high, nfl2k5_frontend_range_low,
            nfl2k5_frontend_range_end_high, nfl2k5_frontend_range_end_low);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "frontend prepare=%ld/%ld last=%08X submit=%ld/%ld last=%08X status=%08X states=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\r\n",
            nfl2k5_frontend_prepare_ok, nfl2k5_frontend_prepare_calls,
            nfl2k5_frontend_prepare_result, nfl2k5_frontend_submit_ok,
            nfl2k5_frontend_submit_calls, nfl2k5_frontend_submit_result,
            nfl2k5_frontend_submit_status,
            nfl2k5_frontend_state_counts[0], nfl2k5_frontend_state_counts[1],
            nfl2k5_frontend_state_counts[2], nfl2k5_frontend_state_counts[3],
            nfl2k5_frontend_state_counts[4], nfl2k5_frontend_state_counts[5],
            nfl2k5_frontend_state_counts[6], nfl2k5_frontend_state_counts[7],
            nfl2k5_frontend_state_counts[8]);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        for (unsigned slot = 0; slot < 9; ++slot) {
            uint32_t callback = *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() +
                                  0x00B04D24u + slot * 8u);
            sample_len = snprintf(sample_text, sizeof sample_text,
                "LIVE callback[%u]=%08X\r\n", slot, callback);
            if (sample_file != INVALID_HANDLE_VALUE)
                WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        }
        char storage[sizeof(SYMBOL_INFO) + 256] = {0};
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), context.Rip, &displacement, symbol)) {
            sample_len=snprintf(sample_text,sizeof sample_text,"[SAMPLE] %s+0x%llX\r\n",symbol->Name,(unsigned long long)displacement);
            if(sample_file!=INVALID_HANDLE_VALUE)WriteFile(sample_file,sample_text,sample_len,&written,NULL);
        }
        sample_len = snprintf(sample_text, sizeof sample_text,
            "scheduler samples=%ld callback=%08X frontend samples=%ld queue=%08X pending=%08X head=%08X tail=%08X\r\n",
            nfl2k5_scheduler_samples, nfl2k5_scheduler_callback,
            nfl2k5_frontend_samples, nfl2k5_frontend_queue,
            nfl2k5_frontend_pending, nfl2k5_frontend_head,
            nfl2k5_frontend_tail);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "tick source=%ld post-unlock=%ld completed-dispatch=%ld input-init-stage=%ld\r\n",
            nfl2k5_host_tick_count, nfl2k5_post_unlock_count,
            nfl2k5_scheduler_dispatch_count, nfl2k5_input_init_stage);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "dpc queued=%ld drained=%ld unresolved=%ld\r\n",
            nfl2k5_dpc_queued_count, nfl2k5_dpc_drained_count,
            nfl2k5_dpc_unresolved_count);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "dpc last routine=%08X context=%08X\r\n",
            nfl2k5_dpc_last_routine, nfl2k5_dpc_last_context);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        sample_len = snprintf(sample_text, sizeof sample_text,
            "KeInsertQueueDpc count=%ld dpc=%08X routine=%08X context=%08X\r\n",
            nfl2k5_keinsert_dpc_count, nfl2k5_keinsert_last_dpc,
            nfl2k5_keinsert_last_routine, nfl2k5_keinsert_last_context);
        if (sample_file != INVALID_HANDLE_VALUE)
            WriteFile(sample_file, sample_text, sample_len, &written, NULL);
        DWORD64 stack_words[256]; SIZE_T bytes=0;
        if(ReadProcessMemory(GetCurrentProcess(),(void *)(uintptr_t)context.Rsp,stack_words,sizeof stack_words,&bytes)) {
            for(unsigned i=0;i<bytes/8;i++)if(SymFromAddr(GetCurrentProcess(),stack_words[i],&displacement,symbol) && displacement<0x10000) {
                sample_len=snprintf(sample_text,sizeof sample_text,"stack+%X %s+%llX\r\n",i*8,symbol->Name,(unsigned long long)displacement);
                if(sample_file!=INVALID_HANDLE_VALUE)WriteFile(sample_file,sample_text,sample_len,&written,NULL);
            }
        }
        /* Keep this small state snapshot alongside the host stack sample.  It
         * lets a live Xemu run and the recompilation be compared without
         * stopping either one in a debugger. */
        {
            static const uint32_t addresses[] = {
                0x00B04EC0, 0x00B04EC4, 0x00B04D18, 0x00B04D1C,
                0x00B034F4, 0x00B034F8, 0x00B057D8, 0x00B068F0,
                0x00B068F4, 0x00A84B14, 0x00A84B18, 0x00AF58C8,
                0x00AF57C0, 0x00AF57C8, 0x00AF5884, 0x00AF5890, 0x00AF5898,
                0x00AF58D0, 0x00AF58D4, 0x00AF58D8, 0x004409A8,
                /* Worker queue ownership and completion state.  These show
                 * whether sub_00035D50 is waiting on work or has completed
                 * its drain before the frontend scheduler advances. */
                0x00B02678, 0x00B0267C, 0x00B02680, 0x00B02684,
                0x00B02688, 0x00B0268C, 0x00B02810, 0x00B02814,
                0x00B02818, 0x00B0281C, 0x00B02820, 0x00B02824,
                0x00B02828, 0x00B02848, 0x00B0284C,
                /* Scheduler callback function pointers, paired with the
                 * B04D1C count above. */
                0x00B04D24, 0x00B04D2C, 0x00B04D34, 0x00B04D3C,
                0x00B04D44, 0x00B04D4C, 0x00B04D54, 0x00B04D5C,
                0x00B04D64,
                /* Archive completion control blocks, paired with the live
                 * Xemu snapshot.  These are title records, not host pointers. */
                0x00B1206C, 0x00B12070, 0x00B1207C, 0x00B12080,
                0x00B12084, 0x00B12088, 0x00B120D0, 0x00B120E0,
                0x00B120E4, 0x00B120E8, 0x00B120EC,
                0x00B04E20, 0x00B04EC8, 0x00A75BC8, 0x00B057D4, 0x00B09570,
                0x00B122AC, 0x00B17BF0, 0x00B0957C, 0x00B12044,
                0x00B12058,
                /* Async dispatch queues used by the synchronous title-start
                 * request at 42820/3BE40. */
                0x00B05658, 0x00B0565C, 0x00B057D0,
                0x00A75DA8, 0x00A75DAC, 0x00A75DA4,
                0x00A75DE0, 0x00A75DE4
            };
            uintptr_t guest = (uintptr_t)xbox_GetMemoryOffset();
            for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
                uint32_t value = *(uint32_t *)(guest + addresses[i]);
                sample_len = snprintf(sample_text, sizeof sample_text,
                    "guest[%08X]=%08X\r\n", addresses[i], value);
                if (sample_file != INVALID_HANDLE_VALUE)
                    WriteFile(sample_file, sample_text, sample_len, &written, NULL);
            }
        }
        if(sample_file!=INVALID_HANDLE_VALUE)CloseHandle(sample_file);
    }
    /* The game runs forever, so atexit() never fires and the icall feedback
     * dump (registered by RECOMP_ICALL_FEEDBACK_INIT) never gets written
     * under a forced process kill. Piggyback on this sample thread, which
     * already fires once after a delay, to write it anyway -- the one
     * question this specific run needs answered is whether sub_001461D0
     * (the big generated-code dispatch-table function with zero static
     * callers, address 0x001461D0) is ever actually reached as a live
     * indirect-call target. */
    if (getenv("RECOMP_NATIVE_SAMPLE"))
        RECOMP_ICALL_FEEDBACK_DUMP();
    return 0;
}

int main(int argc, char **argv)
{
    /* A retail disc extraction has the XBE and its split vc_53450030 archive
     * segments beneath original/disc.  Mount that directory as D: when it is
     * available; keeping the original directory as the fallback preserves the
     * compact XBE-only development layout. */
    const char *disc_dir = NFL2K5_PROJECT_ROOT "/original/disc";
    const char *disc_xbe = NFL2K5_PROJECT_ROOT "/original/disc/default.xbe";
    const char *game_dir = GetFileAttributesA(disc_xbe) != INVALID_FILE_ATTRIBUTES
        ? disc_dir : NFL2K5_PROJECT_ROOT "/original";
    const char *save_dir = NFL2K5_PROJECT_ROOT "/saves";
    const char *xbe_path = GetFileAttributesA(disc_xbe) != INVALID_FILE_ATTRIBUTES
        ? disc_xbe : NFL2K5_PROJECT_ROOT "/original/default.xbe";
    const int validate_only = argc == 2 && strcmp(argv[1], "--validate") == 0;
    if (argc > 1 && !validate_only && !(argc == 2 && strcmp(argv[1], "--run") == 0)) {
        fprintf(stderr, "Usage: NFL2K5.exe [--validate | --run]\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    RECOMP_ICALL_FEEDBACK_INIT();
    setvbuf(stderr, NULL, _IONBF, 0);
    /* A double-click is a normal game launch.  Keep explicit --validate for
     * the allocator check, but give a no-argument launch the same essentials
     * as the diagnostic launcher so it opens the framebuffer window instead
     * of printing usage and closing its console. */
    if (!validate_only) {
        /* Runtime consumers use CRT getenv. SetEnvironmentVariable alone does
         * not update that environment cache after the CRT has initialized. */
        if (!getenv("RECOMP_AC97_READY")) _putenv_s("RECOMP_AC97_READY", "1");
        if (!getenv("RECOMP_SKIP_THREAD_PRIORITY")) _putenv_s("RECOMP_SKIP_THREAD_PRIORITY", "1");
        if (!getenv("RECOMP_FB_WINDOW")) _putenv_s("RECOMP_FB_WINDOW", "1");
        if (!getenv("RECOMP_PB_EXEC")) _putenv_s("RECOMP_PB_EXEC", "1");
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash_report);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    printf("ESPN NFL 2K5 native development build - gameplay bring-up in progress\n");
    printf("[BOOT] Mounted game disc root: %s\n", game_dir);
    FILE *file = fopen(xbe_path, "rb");
    if (!file) { fprintf(stderr, "Missing %s\n", xbe_path); return 2; }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length < 0x178) { fclose(file); return 2; }
    unsigned char *data = malloc((size_t)length);
    if (!data || fread(data, 1, length, file) != (size_t)length) {
        fclose(file); free(data); return 2;
    }
    fclose(file);
    BCRYPT_ALG_HANDLE sha = NULL;
    unsigned char digest[32];
    char digest_hex[65];
    const char *expected_sha = "73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9";
    if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) {
        free(data); return 2;
    }
    NTSTATUS hash_status = BCryptHash(sha, NULL, 0, data, (ULONG)length, digest, sizeof digest);
    BCryptCloseAlgorithmProvider(sha, 0);
    if (hash_status < 0) { free(data); return 2; }
    for (unsigned i = 0; i < sizeof digest; ++i)
        sprintf(digest_hex + i * 2, "%02x", digest[i]);
    if (strcmp(digest_hex, expected_sha)) {
        fprintf(stderr, "[BOOT] XBE hash changed. Reanalyze and rebuild the matching game integration.\n");
        free(data); return 2;
    }
    const uint32_t base = read32(data, 0x104);
    const uint32_t cert = read32(data, 0x118);
    if (memcmp(data, "XBEH", 4) || cert < base ||
        (uint64_t)(cert - base) + 12 > (uint64_t)length ||
        read32(data, cert - base + 8) != 0x53450030 ||
        (read32(data, 0x128) ^ 0xA8FC57AB) != 0x00016BD1) {
        fprintf(stderr, "XBE does not match this NFL 2K5 integration. Reanalyze before rebuilding.\n");
        free(data); return 2;
    }
    if (!xbox_MemoryLayoutInit(data, (size_t)length)) { free(data); return 3; }
    if (getenv("RECOMP_WATCH_PB_WRITE")) {
        /* See pb_write_watch's own comment for what this is and why. Default
         * address is the fixed, reproducible location this session found
         * the zero-filled INLINE_ARRAY vertex words at, via
         * nv2a_pb_scan.c's g_nv2a_pb_last_word_va under RECOMP_PB_DRAW_TRACE.
         * RECOMP_WATCH_ADDR overrides it (hex guest VA) -- used this same
         * session to retarget the same mechanism at 0x443040, the static
         * vertex-stream-descriptor data-pointer field that traces back to
         * the same bug (see PROJECT_STATUS.md, "User decision: fix the
         * pushbuffer path in place"), to catch whatever *should* write it. */
        const char *addr_override = getenv("RECOMP_WATCH_ADDR");
        uint32_t watch_guest_va = addr_override
            ? (uint32_t)strtoul(addr_override, NULL, 16) : 0x83E50DC0u;
        uintptr_t watch_target = (uintptr_t)xbox_GetMemoryOffset() + watch_guest_va;
        g_watch_min_offset = addr_override ? 0 : 0xD80u;
        if (addr_override)
            g_watch_exact_offset = watch_target & (uintptr_t)0xFFFu;
        InitializeCriticalSection(&g_watch_lock);
        g_watch_page_host_base = watch_target & ~(uintptr_t)0xFFFu;
        if (!AddVectoredExceptionHandler(1, pb_write_watch)) {
            fprintf(stderr, "[BOOT] Failed to install pushbuffer write watch.\n");
            return 3;
        }
        DWORD old_protect;
        if (!VirtualProtect((void *)g_watch_page_host_base, 0x1000, PAGE_READONLY, &old_protect)) {
            fprintf(stderr, "[BOOT] Failed to guard pushbuffer watch page.\n");
            return 3;
        }
        fprintf(stderr, "  [PBWATCH] armed on host page 0x%llX (guest 0x%08X)\n",
                (unsigned long long)g_watch_page_host_base, watch_guest_va);
    }
    /* Regression check: the old runtime scratch lived inside this XBE's
     * .rdata. Those bytes must survive initialization unchanged. */
    const uint32_t probes[] = {0x700000, 0x740000, 0x760000, 0x761000, 0x770000};
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const size_t file_offset = 0x4D9000 + probes[i] - 0x4E3AE0;
        const void *mapped = (void *)((uintptr_t)xbox_GetMemoryOffset() + probes[i]);
        if (memcmp(mapped, data + file_offset, 1024)) {
            fprintf(stderr, "[BOOT] Runtime corrupted game data at %08X.\n", probes[i]);
            return 3;
        }
    }
    printf("[BOOT] Game data preserved across memory initialization.\n");
    xbox_kernel_init();
    xbox_path_init(game_dir, save_dir);
    xbox_kernel_bridge_init();
    if (!validate_only && getenv("RECOMP_AC97_READY")) {
        g_apu_state = mcpx_apu_init_standalone((uint8_t *)(uintptr_t)xbox_GetMemoryOffset());
        if (!g_apu_state || !AddVectoredExceptionHandler(1, audio_mmio)) {
            fprintf(stderr, "[BOOT] Failed to initialize APU compatibility.\n");
            return 3;
        }
    }
    if (memcmp((void *)((uintptr_t)xbox_GetMemoryOffset() + 0x740000),
               data + 0x4D9000 + 0x740000 - 0x4E3AE0, 4096)) {
        fprintf(stderr, "[BOOT] Kernel data exports corrupted game data.\n");
        return 3;
    }
    /* The upstream fixed stack overlaps this title's large image. */
    uint32_t stack_base = xbox_HeapAlloc(XBOX_STACK_SIZE, 4096);
    if (!stack_base) { fprintf(stderr, "[BOOT] Main stack allocation failed.\n"); return 3; }
    g_esp = stack_base + XBOX_STACK_SIZE - 16;
    main_stack_begin = stack_base;
    main_stack_end = stack_base + XBOX_STACK_SIZE;
    *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + g_fs_base + 8) = stack_base;
    if (!recomp_dispatch_init()) fprintf(stderr, "[BOOT] Using fallback function lookup.\n");
    if (getenv("RECOMP_EXEC_WATCH")) {
        if (!AddVectoredExceptionHandler(1, exec_count_watch)) {
            fprintf(stderr, "[BOOT] Failed to install execution-count watch.\n");
        } else {
            InitializeCriticalSection(&g_exec_watch_lock);
            exec_watch_add(0x00028C40u, "28C40");
            exec_watch_add(0x00028BC0u, "28BC0");
            exec_watch_add(0x000292E0u, "292E0");
            /* 2026-09-21: sub_00027CA0 is the sole setter of MEM32(0xA6A9A0),
             * the "work pending" flag sub_00028DE0 checks before doing any
             * real per-frame update+Present work at all -- confirmed live
             * (28DE0_gpu_wait=35, 34110_present_trampoline=1: the flag was
             * true on only 1 of 35 checks). It has 18 static call sites;
             * return-address tracking (already built into exec_watch) names
             * which one actually fires live, instead of guessing. */
            exec_watch_add(0x00027CA0u, "27CA0_frame_ready_setter");
            exec_watch_add(0x000294C0u, "294C0");
            exec_watch_add(0x000366D0u, "366D0");
            exec_watch_add(0x00038EA0u, "38EA0");
            exec_watch_add(0x0002FC60u, "2FC60");
            exec_watch_add(0x0002AB20u, "2AB20");
            exec_watch_add(0x0002C030u, "2C030");
            exec_watch_add(0x0002FC30u, "2FC30");
            exec_watch_add(0x00036530u, "36530");
            exec_watch_add(0x00038FC0u, "38FC0");
            /* 2026-09-21: bisecting sub_00038FC0's own one-time title-init
             * sequence (37810,38CA0,3A390,35360,292E0,39AC0,3C3F0,352B0,
             * 2FC30,2C9D0,42820,43CC0,44D00 in order) past the 42820 gate
             * just bypassed (NFL2K5_FORCE_UNBLOCK_TASK42440), to find
             * whether anything else stops it short of 44D00 (archive/font
             * registration). See PROJECT_STATUS.md. */
            exec_watch_add(0x0002C9D0u, "s_2C9D0");
            exec_watch_add(0x00042820u, "s_42820");
            /* 2026-09-21: sub_0003A1C0 is the real worker dispatch loop --
             * walks the global file-object list, and for each object,
             * processes its per-object I/O completion queue (populated by
             * sub_0003B1B0's disc-read submission). Checking whether it's
             * even entered, and whether the async-item trace hooks already
             * embedded in it (nfl2k5_trace_async_item, now surfaced in
             * [PIPE]) ever see anything. See PROJECT_STATUS.md. */
            exec_watch_add(0x0003A1C0u, "s_3A1C0_worker_dispatch");
            /* 2026-09-21: is sub_000439B8 (the specific function that
             * registers sub_000438D0 as a disc-read completion callback
             * and submits the read via sub_0003B1B0) ever called at all?
             * See PROJECT_STATUS.md. */
            exec_watch_add(0x000439B8u, "s_439B8_archive_read_submit");
            /* 2026-09-22: 0x439B8 turned out to be a mis-split mid-body
             * label inside sub_00043980, not a real function -- the
             * disassembler flagged it "tail_jump_alias" (confidence 0.88)
             * and our own exec_watch on it was watching an address nothing
             * ever jumps or calls to directly. sub_00043980 is the real,
             * complete, correctly-recompiled function containing the exact
             * same disc-read submission logic, with two real static callers
             * (sub_00043AC0, sub_00043BE0). See PROJECT_STATUS.md. */
            exec_watch_add(0x00043980u, "s_43980_REAL_archive_read_submit");
            exec_watch_add(0x00043AC0u, "s_43AC0_caller1");
            exec_watch_add(0x00043BE0u, "s_43BE0_caller2");
            exec_watch_add(0x00043850u, "s_43850");
            exec_watch_add(0x00043E90u, "s_43E90");
            exec_watch_add(0x0008D2B0u, "s_8D2B0");
            exec_watch_add(0x0008D340u, "s_8D340");
            exec_watch_add(0x0008D490u, "s_8D490");
            exec_watch_add(0x0009F940u, "s_9F940");
            /* 2026-09-22 continued: sub_00064710 is the sole static caller of
             * s_8D490; sub_000B81A0/B81C0/B88C0/B8B30 are the four static
             * callers of s_9F940. None of these outer gate functions has ever
             * been exec_watch-instrumented -- if they never run, that's the
             * real answer to "why is the whole subsystem dormant." */
            exec_watch_add(0x00064710u, "gate_64710_caller_of_8D490");
            exec_watch_add(0x000B81A0u, "gate_B81A0_caller_of_9F940");
            exec_watch_add(0x000B81C0u, "gate_B81C0_caller_of_9F940");
            exec_watch_add(0x000B88C0u, "gate_B88C0_caller_of_9F940");
            exec_watch_add(0x000B8B30u, "gate_B8B30_caller_of_9F940");
            /* 2026-09-22: state 27's real handler (sub_0049215D) walks a
             * list at ebx+0x268 (relayed from ebx+0x8F0) and submits each
             * item via sub_00488B65 -- see PROJECT_STATUS.md for whether
             * this list is actually populated with real work. */
            exec_watch_add(0x00488B65u, "s_488B65_state27_submit");
            exec_watch_add(0x0004D920u, "s_4D920_input_init");
            exec_watch_add(0x0004B950u, "s_4B950_input_init_step2");
            exec_watch_add(0x00043CC0u, "s_43CC0");
            exec_watch_add(0x00044D00u, "s_44D00");
            exec_watch_add(0x0012D150u, "12D150");
            exec_watch_add(0x0012CB50u, "12CB50");
            exec_watch_add(0x0029B6D0u, "29B6D0");
            exec_watch_add(0x001461D0u, "1461D0");
            /* Both registered in recomp_dispatch.c's indirect-call table too
             * (not just the one static direct-call chain this session
             * traced from sub_00028C40) -- retail hits sub_00420160's own
             * entry 80+ times from this exact call site inside
             * sub_00033D50, but sub_00028C40 (the only STATIC caller of
             * sub_00033F00 -> sub_00033D50) is itself explicitly gated to
             * run exactly once (recomp_0000.c:73833-73839, "only on the
             * very first call ever"). If native's real count for these
             * matches sub_00028C40's (1), the guard is the whole story. If
             * it's higher, something reaches this chain indirectly too --
             * that's the path actually worth finding. */
            exec_watch_add(0x00033D50u, "33D50");
            exec_watch_add(0x00033F00u, "33F00");
            exec_watch_add(0x00420160u, "420160");
            exec_watch_add(0x00427890u, "427890");
            /* The rest of the 7 scheduler-register callbacks this session's
             * telemetry already showed as live (0003A1C0 already understood
             * as an empty-queue async pump, not watched again here). None of
             * the 20 functions above explain frontend_state_dispatch
             * climbing to 11 or DMA_PUT advancing a second time -- one of
             * these is the more likely real per-frame/steady-state driver. */
            exec_watch_add(0x0003E910u, "3E910");
            exec_watch_add(0x00041810u, "41810");
            exec_watch_add(0x003CD120u, "3CD120");
            exec_watch_add(0x00039380u, "39380");
            exec_watch_add(0x00051F00u, "51F00");
            exec_watch_add(0x0003A310u, "3A310");
            /* sub_003CD120 (game_vtable, 736 bytes) is one of the 5 above
             * that fires continuously (~125/tick, never plateaus) and calls
             * nfl2k5_trace_frontend at its own entry -- the same counters
             * ("front-state/req/active") that have read 0 in every sample
             * taken tonight. It walks a linked list (head 0xAF5884, sentinel
             * 0xAF57C8) that looks permanently empty. These are that list's
             * only two enqueue call sites in the whole generated codebase
             * (recomp_0025.c:107618 inside sub_003C4D8C, :119606 inside
             * sub_003CBBF0) -- if neither ever fires, that confirms nothing
             * ever feeds this queue, which is likely the real reason the
             * per-frame update that IS running continuously never does
             * anything visible. */
            exec_watch_add(0x003C4D8Cu, "3C4D8C");
            exec_watch_add(0x003CBBF0u, "3CBBF0");
            /* sub_00178150/sub_00272A60 (the frontend queue's producers,
             * matching this project's own pre-existing "frontend producers
             * 178150/272A60" telemetry, both always 0) are themselves only
             * called from sub_00363350 and sub_00278310 -- and NEITHER of
             * those two has a single static caller anywhere in the
             * generated codebase. Both are registered in the indirect-call
             * dispatch table (recomp_dispatch.c) and sub_00278310 is
             * explicitly thiscall/game_vtable -- a genuine C++ virtual
             * method. This is the same shape as sub_001461D0 above: only
             * reachable through a vtable slot this session hasn't found the
             * owner of yet. If these read 0 too, the real missing trigger
             * is confirmed to be "whatever should be calling through this
             * vtable slot," not anything already traced. */
            exec_watch_add(0x00363350u, "363350");
            exec_watch_add(0x00278310u, "278310");

            /* 2026-09-20: found live, on a correctly-rendering reference
             * hybrid-xemu session, by watching the real NV2A DMA_PUT MMIO
             * register (0xFD800040) during actual gameplay -- this is the
             * real, continuously-firing render/pushbuffer-submission path,
             * not a guess. sub_00426110 (Category: game_render) contains
             * the real GPU-kick write; sub_00426230 is its immediate,
             * repeatedly-observed caller. Both already exist as translated
             * functions in this project's own generated code (recomp_0028.c
             * area) -- the open question is whether native's own execution
             * ever reaches them at all, which the 0x00443040/sub_004308C0
             * thread (now confirmed unrelated -- that path never runs even
             * during correct reference rendering) cannot answer. */
            exec_watch_add(0x00426110u, "426110_render_kick");
            exec_watch_add(0x00426230u, "426230_render_caller");

            /* All 14 static callers of sub_00426230 (recomp_0028.c/0029.c),
             * each apparently a distinct per-draw-type render/flush routine.
             * Watching all of them at once identifies exactly which draw
             * types native ever attempts versus which it never reaches,
             * rather than guessing at a single "the" caller. */
            exec_watch_add(0x004207A0u, "4207A0");
            exec_watch_add(0x00423D90u, "423D90");
            exec_watch_add(0x00424750u, "424750");
            exec_watch_add(0x00424790u, "424790");
            exec_watch_add(0x00425180u, "425180");
            exec_watch_add(0x004251A0u, "4251A0_vertex_copy");
            exec_watch_add(0x00425310u, "425310");
            exec_watch_add(0x004254F0u, "4254F0");
            exec_watch_add(0x00425A00u, "425A00");
            exec_watch_add(0x004262F0u, "4262F0");
            exec_watch_add(0x00426460u, "426460");
            /* 0x00427890 is already watched above ("427890") -- known
             * one-shot init caller, not re-added here. */
            exec_watch_add(0x004324B0u, "4324B0");
            exec_watch_add(0x0043266Cu, "43266C");

            /* One hop further: callers of sub_004262F0 (the ring-buffer
             * flush/present check, hit 3x above) not already in this list. */
            exec_watch_add(0x00420780u, "420780");
            exec_watch_add(0x00426600u, "426600");
            exec_watch_add(0x00426620u, "426620");
            exec_watch_add(0x004266A0u, "4266A0");

            /* 2026-09-20, continued: found live on the hybrid reference,
             * by return-address capture, that the ENTIRE render-kick chain
             * (426110/426230/4262F0) is driven, 100% of the time (60/60
             * hits), through sub_00424790 (a Present()-shaped function --
             * double-buffer index at device+0x2478, back-buffer array at
             * device+0x1974) via a single call site inside sub_00034110,
             * an 8-byte "push 0; call Present(); ret" trampoline. Its own
             * caller connects straight back to the already-known
             * sub_00028DE0 (the GPU resource-notify wait loop, fixed
             * 2026-09-17) / sub_00028F70 (the worker-gate wrapper ChatGPT
             * reviewed tonight) -- not previously watched together with the
             * render chain. Watching this specific pairing directly tests
             * whether "the wait loop completes once, correctly, but its own
             * caller stops re-entering it for a next frame" (the same shape
             * as the 2026-09-17 fix) is what's actually stalling rendering. */
            exec_watch_add(0x00034110u, "34110_present_trampoline");
            /* 0x00424790 is already watched above as "424790" -- not
             * re-added (a duplicate registration here would silently never
             * get credited, since the INT3 lookup matches the first
             * matching table entry; caught and fixed this session). */
            exec_watch_add(0x00028DE0u, "28DE0_gpu_wait");
            exec_watch_add(0x00028F70u, "28F70_worker_wrap");
            /* 2026-09-21: confirmed via per-call-site counters that
             * native's entire 28F70_worker_wrap count comes from three
             * one-shot sites in recomp_0007.c, NOT from the live-reference-
             * confirmed hot site inside sub_00074790 -- so does native ever
             * even enter sub_00074BF0 (the real main loop) or sub_00074790
             * at all? Checking directly instead of continuing to infer it
             * from downstream effects. */
            exec_watch_add(0x00074BF0u, "74BF0_main_loop_outer");
            exec_watch_add(0x00074790u, "74790_main_loop_body");
            /* sub_00074BF0's own straight-line prologue, called in this
             * exact order before it ever reaches its do-while loop (see
             * recomp_0003.c:31857). Confirmed the loop body is NEVER
             * entered (74790=0 above) -- since these 9 run strictly
             * sequentially with no branches, whichever is the last nonzero
             * entry here is exactly where execution never returns from. */
            exec_watch_add(0x0003A6A0u, "s1_3A6A0");
            exec_watch_add(0x00073EC0u, "s2_73EC0");
            exec_watch_add(0x00038FB0u, "s3_38FB0");
            exec_watch_add(0x00040630u, "s4_40630");
            exec_watch_add(0x003CD400u, "s5_3CD400");
            exec_watch_add(0x00052090u, "s6_52090");
            /* s7 is sub_00038FC0, already watched above as "38FC0". */
            exec_watch_add(0x0003C3F0u, "s8_3C3F0");
            exec_watch_add(0x000748A0u, "s9_748A0");
            /* 2026-09-21: unifies tonight's whole thread with the project's
             * oldest finding (2026-09-17: sub_00178150/sub_00272A60 need
             * real player input, confirmed unreached on retail too during
             * passive boot). sub_000748A0 (s9 above) is entered but
             * exec_watch only counts entry, not return -- if it calls
             * sub_00178150 unconditionally near its own end and that
             * blocks waiting for input, s9 would show "entered" forever
             * without ever completing, which is exactly consistent with
             * 74790_main_loop_body staying at 0 even though every
             * prerequisite up to and including s9 shows nonzero. */
            exec_watch_add(0x00178150u, "s9_inner_178150");
            exec_watch_add(0x00272A60u, "s9_inner_272A60");
            /* 2026-09-21: sub_00178150 registers sub_00178130 as a
             * completion callback via the frontend enqueue (sub_003CBBF0),
             * resets MEM32(0xBDEEF0)=0, then spins (pumping messages via
             * sub_00038CD0) until MEM32(0xBDEEF0) becomes nonzero -- which
             * only sub_00178130 itself ever sets. If sub_00178130 never
             * fires, this is the new blocking gate (analogous to
             * 0xB09584/sub_000432C0 found earlier tonight). */
            exec_watch_add(0x00178130u, "cb_178130_completion");
            /* sub_003CBBF0 (frontend enqueue) already watched as "3CBBF0" above. */
            /* sub_000748A0 has ~150 straight-line calls before it finally
             * reaches the loop that calls sub_00178150 (confirmed at 0
             * hits above) -- bisecting which of them it actually gets
             * through, using real function addresses recomp_lookup can
             * resolve (not the internal labels between them). */
            exec_watch_add(0x000441B0u, "bis_25pct_441B0");
            exec_watch_add(0x000432F0u, "bis_50pct_432F0");
            exec_watch_add(0x00160710u, "bis_75pct_160710");
            /* 2026-09-22: triaging sub_000432F0's 100+ call sites (see
             * PROJECT_STATUS.md) instead of guessing -- sub_00061950 alone
             * accounts for 35 of them; watch it and its 3 known static
             * callers directly to see if any of this neighborhood ever
             * runs at all. */
            exec_watch_add(0x00061950u, "n432f0_61950_35x_caller");
            exec_watch_add(0x000645D0u, "n432f0_645D0_caller_of_61950");
            exec_watch_add(0x0028FD90u, "n432f0_28FD90_caller_of_61950");
            exec_watch_add(0x0028FE70u, "n432f0_28FE70_caller_of_61950");
            /* Next tier of sub_000432F0 callers by call-site count, per the
             * same PROJECT_STATUS.md triage (sub_000748A0's own direct call
             * is confirmed already-explained dead code via sub_00074180's
             * hang -- not re-watched here). */
            exec_watch_add(0x0028FC70u, "n432f0_28FC70_6x");
            exec_watch_add(0x00125C50u, "n432f0_125C50_5x");
            exec_watch_add(0x00074250u, "n432f0_74250_4x");
            exec_watch_add(0x00276810u, "n432f0_276810_3x");
            exec_watch_add(0x00142DD0u, "n432f0_142DD0_3x");
            exec_watch_add(0x00142DA0u, "n432f0_142DA0_3x");
            exec_watch_add(0x00142B40u, "n432f0_142B40_3x");
            exec_watch_add(0x00125CD0u, "n432f0_125CD0_3x");
            exec_watch_add(0x00074160u, "bis_90pct_74160");
            /* 2026-09-21 isolation test (since reverted): re-ran with only
             * 3 simultaneous watches (down from ~20) to check whether
             * exec_watch itself behaves differently under a much smaller
             * simultaneous-watch count. Result: identical -- 4/5 runs with
             * narrow_1786D0=1 and narrow_74180=1 still showed
             * bis_50pct_432F0=0. Rules out "too many simultaneous INT3s"
             * as the explanation; restored the full set above. */
            /* Narrowed further: reached 0x000441B0 (both its call sites)
             * but never 0x000432F0. sub_00043F50 dropped from this list --
             * confirmed 67 static call sites elsewhere, useless as a
             * bisection point (any hit could come from anywhere). Kept
             * only the two clean candidates: sub_001786D0 (exactly 1
             * static call site, recomp_0003.c:31508, unambiguous) and
             * sub_00074180 (4 call sites, all confirmed internal to this
             * same sub_000748A0, so still clean for this purpose). */
            exec_watch_add(0x001786D0u, "narrow_1786D0");
            exec_watch_add(0x00074180u, "narrow_74180");

            /* sub_00038F90: the only static writer of MEM32(0xB04EC0), the
             * real main-loop quit flag (see sub_00074BF0's do/while at
             * loc_00074C30, and sub_00038F50's check of this address).
             * RECOMP_DATA_WATCH's page-guard on this address measurably
             * perturbs this exact timing-sensitive stall (confirmed: with
             * it enabled, 8/8 runs landed in the profile where the main
             * loop never starts at all; disabling it restored the usual
             * ~good-profile rate). This lightweight INT3-based watch on
             * the setter itself does not have that problem. */
            exec_watch_add(0x00038F90u, "38F90_quit_setter");
        }
        /* 2026-09-21: the periodic [EXECWATCH]/[PEEK] printout is normally
         * driven by kernel_bridge.c's own dispatcher (gated on
         * g_kernel_call_count > 200 and a 2s tick), which never fires at all
         * in the shallow/stalled boot profile -- leaving short captures of
         * that profile with zero diagnostic output. This thread flushes the
         * same counters on a fixed 3s timer independent of kernel dispatch,
         * so every run (whichever profile it lands in) yields a readout. */
        CreateThread(NULL, 0, nfl2k5_execwatch_timer_thread, NULL, 0, NULL);
    }
    if (getenv("RECOMP_DATA_WATCH")) {
        if (!AddVectoredExceptionHandler(1, data_read_watch)) {
            fprintf(stderr, "[BOOT] Failed to install data read/write watch.\n");
        } else {
            InitializeCriticalSection(&g_data_watch_lock);
            /* The two .rdata locations where sub_00278310's literal address
             * was found embedded in a small record in the original XBE
             * (found via raw byte search + XBE section-table VA
             * translation, not reachable by grepping generated C). */
            data_watch_add(0x0051980Cu, "cb-278310-a");
            data_watch_add(0x0051A32Cu, "cb-278310-b");
            /* Same for sub_00363350. */
            data_watch_add(0x0053D120u, "cb-363350-a");
            data_watch_add(0x0085EFE4u, "cb-363350-b");

            /* 2026-09-20: found the real main-loop exit condition, traced
             * from sub_00074BF0 (the actual per-frame loop -- previously
             * mislabeled "one-shot title init" based only on a native
             * counter that never distinguished "entered once" from "loops
             * internally"; the hybrid-xemu reference confirms it really
             * does loop continuously on correct hardware). The loop exits
             * when sub_00038F50()'s check of MEM32(0xB04EC0) is nonzero.
             * Verified on the live reference: this address legitimately
             * stays 0 throughout normal play, and its only known writer
             * (sub_00038F90, a 2-instruction "request quit" setter with
             * zero static callers -- reachable only indirectly, and
             * confirmed to never fire during 45s of live reference
             * gameplay either) never runs. So whatever sets it nonzero in
             * native is not a legitimate quit request -- watching for the
             * actual write should identify it directly. */
            data_watch_add(0xB04EC0u, "quitflag-B04EC0");
        }
    }
    if (getenv("RECOMP_HW_WATCH")) {
        /* Lower-overhead alternative to RECOMP_DATA_WATCH for exactly this
         * address -- see hw_watch_install's own comment above for why. */
        if (getenv("RECOMP_HW_WATCH_SELFTEST")) {
            /* Positive control: 0xB0284C is written repeatedly from
             * sub_00035CE0, itself reached from sub_00028F70 (already
             * confirmed to run in this exact profile -- worker entry=3).
             * If this doesn't produce hits either, the mechanism itself is
             * broken, not "0xB04EC0 is genuinely never written." */
            hw_watch_install(0xB0284Cu, "selftest-B0284C");
        } else {
            /* 2026-09-22: repointed at the current investigation's real
             * target. The "final synthesis" root-cause entry in
             * PROJECT_STATUS.md pinned the whole dormant archive/font-load
             * chain on one live read: MEM32(0xA77E38+0x14) == 1 (a small
             * integer, not a valid object pointer), which is why
             * sub_00043980 is never safely force-callable. That was a single
             * point-in-time debugger snapshot, not a watch across a real
             * run -- this arms a hardware write-watch on that exact field
             * (0xA77E38+0x14 = 0xA77E4C) to find out whether anything ever
             * writes it during a normal run, and if so, from where. */
            hw_watch_install(0xA77E4Cu, "archive-obj-A77E38-plus14");
        }
    }
    guest_function entry = recomp_lookup(0x00016BD1);
    if (!entry) { fprintf(stderr, "[BOOT] Recompiled entry point missing.\n"); return 4; }
    printf("[BOOT] XBE loaded; memory, kernel and recompiled entry 0x00016BD1 ready.\n");
    if (validate_only) {
        extern int nfl2k5_test_memmove(uint32_t buffer);
        uint32_t test_buffer = xbox_HeapAlloc(4096, 16);
        if (!test_buffer || !nfl2k5_test_memmove(test_buffer)) return 8;
        printf("[TEST] Guest memmove overlap directions, zero length and stack cleanup passed.\n");
        uint32_t used_before = xbox_ContiguousAllocatedBytes();
        uint32_t a = xbox_ContiguousAlloc(8192, 4096);
        uint32_t b = xbox_ContiguousAlloc(4096, 4096);
        if (!a || !b || xbox_ContiguousBlockSize(a) != 8192 ||
            !xbox_ContiguousFree(a) || xbox_ContiguousBlockSize(a) != 0) return 7;
        uint32_t reused = xbox_ContiguousAlloc(8192, 4096);
        if (reused != a || !xbox_ContiguousFree(b) || !xbox_ContiguousFree(reused) ||
            xbox_ContiguousAllocatedBytes() != used_before) return 7;
        printf("[TEST] Contiguous allocation, size lookup, reuse and release passed.\n");
    }
    if (!validate_only) {
        /* XDK D3D fence wait at 00426427: device=[004409A8],
         * submitted=[device+2C], completion pointer=[device+30].
         * Use the upstream bring-up fence model; this advances CPU startup
         * only and does not prove that GPU commands have been rendered.
         * RECOMP_DISABLE_FENCE_MIRROR is a negative-control switch: with it
         * set, the spin-wait this fence exists to bypass is left standing,
         * to test whether the fence is masking a real completion signal
         * downstream (2026-09-17 external review, Finding 3) rather than
         * just being an inert bring-up shim. */
        if (!getenv("RECOMP_DISABLE_FENCE_MIRROR")) {
            if (xbox_Nv2aMirrorFence(0x004409A8, 0x2C, 0x30) != 0) return 6;
        } else {
            fprintf(stderr, "[BOOT] Fence mirror disabled (negative control run).\n");
        }
        xbox_WatchdogStart();
        /* Suspending the boot thread for a diagnostic sample can perturb the
         * title's early worker handoff.  Keep it opt-in while bringing up the
         * real boot path; the launcher can enable it for a focused trace. */
        if (getenv("RECOMP_NATIVE_SAMPLE")) {
            HANDLE main_thread = NULL;
            if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                                &main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                HANDLE sampler = CreateThread(NULL, 0, sample_native_startup, main_thread, 0, NULL);
                if (sampler) CloseHandle(sampler);
                else CloseHandle(main_thread);
            }
        }
        CreateThread(NULL, 0, nfl2k5_forced_display_thread, NULL, 0, NULL);
        printf("[BOOT] Entering recompiled NFL 2K5 code.\n");
        __try { entry(); }
        __except(crash_report(GetExceptionInformation())) {
            /* 2026-09-21: forced-display path -- a guest crash used to exit
             * main() and take the whole process (and the display thread
             * with it) down. Keep the process alive so the forced synthetic
             * window keeps painting regardless of what the guest logic did;
             * only Ctrl+C or closing the window should end the run now. */
            fprintf(stderr, "[BOOT] Guest crashed; process kept alive for the forced display window.\n");
            for (;;) Sleep(1000);
        }
        printf("[BOOT] Guest entry returned.\n");
    }
    xbox_kernel_bridge_shutdown();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(data);
    SymCleanup(GetCurrentProcess());
    return 0;
}

/* Explorer launches should show the framebuffer window rather than a
 * short-lived diagnostic console. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)instance; (void)previous; (void)command_line; (void)show;
    return main(__argc, __argv);
}

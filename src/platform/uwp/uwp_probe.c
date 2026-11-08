/*
 * uwp_probe.c -- run-time checks for the things the UWP/Xbox port depends on.
 *
 * Built into the UWP shell (NFL2K5_UWP). Each probe tries one capability the
 * recompiled game needs, in the AppContainer the package runs in, exactly the
 * way the xboxrecomp kernel uses it today (src/kernel/xbox_memory_layout.c):
 *
 *   - guest RAM: a 64 MB page-file mapping viewed at 0x00010000 plus mirrors
 *     every 64 MB above it (MapViewOfFileEx at fixed addresses)
 *   - the fixed apertures: contiguous window at 0x80000000, NV2A registers at
 *     0xFD000000, MCPX at 0xFE800000, flash at 0xFF000000 (all + 0x10000)
 *   - the same layout through the UWP-declared calls (VirtualAlloc2FromApp
 *     placeholder over the whole 4 GB guest span, MapViewOfFile3FromApp
 *     replacing slices of it), the fallback if fixed addresses are refused
 *   - a vectored exception handler catching a write to a read-only page (the
 *     APU/AC97 MMIO traps work this way)
 *   - loading dxcompiler.dll from the package (run-time shader compile)
 *   - file access: LocalState, the install folder, and an outside path
 *   - thread affinity/priority
 *
 * Compiled with the desktop headers on purpose: the xboxrecomp kernel will be
 * too, and tools/uwp/check-imports.py checks the linked imports against the
 * UWP API allow-list instead. Results go to <LocalState>\uwp-probe.txt.
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "uwp_probe.h"

#define GUEST_RAM        (64u * 1024u * 1024u)
#define GUEST_OFFSET     0x00010000u         /* where Windows builds put guest 0 */
#define GUEST_MIRRORS    28
#define CONTIG_BASE      0x80000000u
#define CONTIG_SIZE      (64u * 1024u * 1024u)
#define TILED_BASE       0xF0000000u
#define NV2A_BASE        0xFD000000u
#define NV2A_SIZE        (16u * 1024u * 1024u)
#define MCPX_BASE        0xFE800000u
#define MCPX_SIZE        (8u * 1024u * 1024u)
#define FLASH_BASE       0xFF000000u
#define FLASH_SIZE       (1u * 1024u * 1024u)

uwp_probe_result g_uwp_probe[UWP_PROBE_MAX];
int g_uwp_probe_count;

static FILE *s_log;

static void record(const char *name, int ok, const char *fmt, ...)
{
    uwp_probe_result *r;
    va_list ap;
    if (g_uwp_probe_count >= UWP_PROBE_MAX) return;
    r = &g_uwp_probe[g_uwp_probe_count++];
    r->ok = ok;
    snprintf(r->name, sizeof r->name, "%s", name);
    va_start(ap, fmt);
    vsnprintf(r->detail, sizeof r->detail, fmt, ap);
    va_end(ap);
    if (s_log) {
        fprintf(s_log, "[%s] %-26s %s\n", ok ? "PASS" : "FAIL", r->name, r->detail);
        fflush(s_log);
    }
    {
        char line[400];
        snprintf(line, sizeof line, "uwp-probe [%s] %s: %s\n", ok ? "PASS" : "FAIL", r->name, r->detail);
        OutputDebugStringA(line);
    }
}

static void record_alloc(const char *name, void *p, void *want)
{
    if (p && (!want || p == want)) record(name, 1, "at %p", p);
    else if (p) record(name, 0, "got %p, wanted %p", p, want);
    else record(name, 0, "error %lu", GetLastError());
}

/* ---- guest memory, the way xbox_memory_layout.c does it today ---------- */

static HANDLE s_ram;
static void *s_views[1 + GUEST_MIRRORS];

static void probe_fixed_layout(void)
{
    int mirrors = 0, tried = 0, m;
    void *base, *p;

    s_ram = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, GUEST_RAM, NULL);
    if (s_ram) record("file mapping 64MB", 1, "CreateFileMappingW ok");
    else record("file mapping 64MB", 0, "error %lu", GetLastError());
    if (!s_ram) return;

    base = MapViewOfFileEx(s_ram, FILE_MAP_ALL_ACCESS, 0, 0, GUEST_RAM, (void *)(uintptr_t)GUEST_OFFSET);
    record_alloc("RAM view @0x00010000", base, (void *)(uintptr_t)GUEST_OFFSET);
    if (!base) return;
    s_views[0] = base;

    for (m = 1; m <= GUEST_MIRRORS; m++) {
        uint64_t lo = (uint64_t)m * GUEST_RAM;
        if (lo >= TILED_BASE || (lo < (uint64_t)CONTIG_BASE + CONTIG_SIZE && lo + GUEST_RAM > CONTIG_BASE))
            continue;
        tried++;
        s_views[m] = MapViewOfFileEx(s_ram, FILE_MAP_ALL_ACCESS, 0, 0, GUEST_RAM,
                                     (void *)(uintptr_t)(lo + GUEST_OFFSET));
        if (s_views[m]) mirrors++;
    }
    /* Aliasing check: a write through view 0 shows in the last mirror. */
    {
        int alias = 0;
        volatile uint32_t *a = (volatile uint32_t *)((char *)base + 0x448);
        *a = 0x2C05C0DEu;
        for (m = GUEST_MIRRORS; m >= 1; m--)
            if (s_views[m]) { alias = *(volatile uint32_t *)((char *)s_views[m] + 0x448) == 0x2C05C0DEu; break; }
        record("RAM mirrors", mirrors == tried && alias, "%d/%d views, aliasing %s", mirrors, tried, alias ? "ok" : "BROKEN");
    }

    p = VirtualAlloc((void *)(uintptr_t)(CONTIG_BASE + GUEST_OFFSET), CONTIG_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    record_alloc("contig @0x80000000", p, (void *)(uintptr_t)(CONTIG_BASE + GUEST_OFFSET));
    p = VirtualAlloc((void *)(uintptr_t)(NV2A_BASE + GUEST_OFFSET), NV2A_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    record_alloc("NV2A @0xFD000000", p, (void *)(uintptr_t)(NV2A_BASE + GUEST_OFFSET));
    p = VirtualAlloc((void *)(uintptr_t)(MCPX_BASE + GUEST_OFFSET), MCPX_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    record_alloc("MCPX @0xFE800000", p, (void *)(uintptr_t)(MCPX_BASE + GUEST_OFFSET));
    p = VirtualAlloc((void *)(uintptr_t)(FLASH_BASE + GUEST_OFFSET), FLASH_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    record_alloc("flash @0xFF000000", p, (void *)(uintptr_t)(FLASH_BASE + GUEST_OFFSET));
}

/* ---- the same layout through the UWP-declared placeholder calls ------- */

typedef PVOID (WINAPI *pfnVirtualAlloc2FromApp)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *pfnMapViewOfFile3FromApp)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef HANDLE (WINAPI *pfnCreateFileMappingFromApp)(HANDLE, PSECURITY_ATTRIBUTES, ULONG, ULONG64, PCWSTR);

static HMODULE core_module(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    return k ? k : GetModuleHandleW(L"kernel32.dll");
}

static void probe_placeholder_layout(void)
{
    HMODULE k = core_module();
    pfnVirtualAlloc2FromApp va2 = k ? (pfnVirtualAlloc2FromApp)(void *)GetProcAddress(k, "VirtualAlloc2FromApp") : NULL;
    pfnMapViewOfFile3FromApp mv3 = k ? (pfnMapViewOfFile3FromApp)(void *)GetProcAddress(k, "MapViewOfFile3FromApp") : NULL;
    pfnCreateFileMappingFromApp cfm = k ? (pfnCreateFileMappingFromApp)(void *)GetProcAddress(k, "CreateFileMappingFromApp") : NULL;
    const SIZE_T span = 0x100000000ull;   /* all 32-bit guest addresses */
    char *base;
    HANDLE ram;
    int views = 0, m;

    if (!va2 || !mv3 || !cfm) {
        record("placeholder API", 0, "VirtualAlloc2FromApp/MapViewOfFile3FromApp missing");
        return;
    }
    base = (char *)va2(NULL, NULL, span, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
    record_alloc("4GB placeholder span", base, NULL);
    if (!base) return;

    ram = cfm(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, GUEST_RAM, NULL);
    if (!ram) { record("FromApp mapping", 0, "error %lu", GetLastError()); return; }

    /* Split the placeholder into RAM-sized slices and replace each with a view. */
    for (m = 0; m <= GUEST_MIRRORS; m++) {
        char *at = base + (SIZE_T)m * GUEST_RAM;
        if ((uint64_t)m * GUEST_RAM >= TILED_BASE) break;
        if (!VirtualFree(at, GUEST_RAM, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
            break;
        if (mv3(ram, NULL, at, 0, GUEST_RAM, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0))
            views++;
        else
            break;
    }
    {
        int alias = 0;
        if (views > 1) {
            *(volatile uint32_t *)(base + 0x100) = 0xFEEDF00Du;
            alias = *(volatile uint32_t *)(base + (SIZE_T)(views - 1) * GUEST_RAM + 0x100) == 0xFEEDF00Du;
        }
        record("placeholder views", views > 1 && alias, "%d views replaced placeholders, aliasing %s (err %lu)",
               views, alias ? "ok" : "BROKEN", GetLastError());
    }
}

/* ---- vectored exception handler (MMIO traps) --------------------------- */

static volatile LONG s_veh_hits;
static void *s_veh_page;

static LONG CALLBACK probe_veh(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        (char *)ep->ExceptionRecord->ExceptionInformation[1] >= (char *)s_veh_page &&
        (char *)ep->ExceptionRecord->ExceptionInformation[1] < (char *)s_veh_page + 4096) {
        DWORD old;
        InterlockedIncrement(&s_veh_hits);
        VirtualProtect(s_veh_page, 4096, PAGE_READWRITE, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void probe_veh_trap(void)
{
    DWORD old;
    PVOID h;
    s_veh_page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    h = AddVectoredExceptionHandler(1, probe_veh);
    if (!s_veh_page || !h || !VirtualProtect(s_veh_page, 4096, PAGE_READONLY, &old)) {
        record("VEH write trap", 0, "setup failed (%lu)", GetLastError());
        return;
    }
    *(volatile uint32_t *)((char *)s_veh_page + 0x130) = 0x100;   /* faults, handler unprotects */
    record("VEH write trap", s_veh_hits == 1 && *(volatile uint32_t *)((char *)s_veh_page + 0x130) == 0x100,
           "handler ran %ld time(s)", s_veh_hits);
    RemoveVectoredExceptionHandler(h);
}

/* ---- DXC from the package ---------------------------------------------- */

static void probe_dxc(void)
{
    HMODULE dxc = LoadLibraryW(L"dxcompiler.dll");
    void *create = dxc ? (void *)GetProcAddress(dxc, "DxcCreateInstance") : NULL;
    HMODULE dxil = LoadLibraryW(L"dxil.dll");
    if (create) record("dxcompiler.dll load", 1, "DxcCreateInstance found, dxil.dll %s", dxil ? "loaded" : "MISSING");
    else if (dxc) record("dxcompiler.dll load", 0, "no DxcCreateInstance export");
    else record("dxcompiler.dll load", 0, "error %lu", GetLastError());
}

/* ---- files ------------------------------------------------------------- */

static void probe_files(const wchar_t *local_dir, const wchar_t *install_dir, const wchar_t *outside_path)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD n = 0;
    char buf[64];

    swprintf(path, MAX_PATH, L"%ls\\uwp-probe-write.bin", local_dir);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, "save data", 9, &n, NULL);
        CloseHandle(h);
    }
    if (h != INVALID_HANDLE_VALUE) record("LocalState CreateFileW", n == 9, "wrote %lu bytes", n);
    else record("LocalState CreateFileW", 0, "error %lu", GetLastError());

    swprintf(path, MAX_PATH, L"%ls\\AppxManifest.xml", install_dir);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    n = 0;
    if (h != INVALID_HANDLE_VALUE) { ReadFile(h, buf, sizeof buf, &n, NULL); CloseHandle(h); }
    if (n) record("install dir read", 1, "read %lu bytes", n);
    else record("install dir read", 0, "error %lu", GetLastError());

    if (outside_path && *outside_path) {
        h = CreateFileW(outside_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        {
            DWORD e = GetLastError();
            char narrow[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, outside_path, -1, narrow, sizeof narrow, NULL, NULL);
            /* Informational: an AppContainer is expected to be refused here
             * unless broadFileSystemAccess is granted. Not a failure. */
            record("outside path (info)", 1, "%s: %s", narrow,
                   h != INVALID_HANDLE_VALUE ? "readable" : (e == ERROR_ACCESS_DENIED ? "ACCESS_DENIED (expected without broadFileSystemAccess)" : "not readable"));
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    }
}

/* ---- threads ------------------------------------------------------------ */

static DWORD WINAPI probe_thread(LPVOID arg)
{
    DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), 1);
    BOOL pri = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    *(int *)arg = (prev != 0) | (pri ? 2 : 0);
    return 0;
}

static void probe_threads(void)
{
    int res = 0;
    HANDLE t = CreateThread(NULL, 1 << 20, probe_thread, &res, 0, NULL);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    record("thread affinity/priority", res == 3, "affinity %s, priority %s", (res & 1) ? "ok" : "refused", (res & 2) ? "ok" : "refused");
}

void uwp_probe_run(const wchar_t *local_dir, const wchar_t *install_dir, const wchar_t *outside_path)
{
    wchar_t logpath[MAX_PATH];
    g_uwp_probe_count = 0;
    swprintf(logpath, MAX_PATH, L"%ls\\uwp-probe.txt", local_dir);
    s_log = _wfopen(logpath, L"w");
    if (s_log) fprintf(s_log, "NFL2K5 UWP probe\n");

    probe_fixed_layout();
    probe_placeholder_layout();
    probe_veh_trap();
    probe_dxc();
    probe_files(local_dir, install_dir, outside_path);
    probe_threads();

    if (s_log) { fclose(s_log); s_log = NULL; }
}

void uwp_probe_note(const wchar_t *local_dir, const char *name, int ok, const char *detail)
{
    wchar_t logpath[MAX_PATH];
    FILE *f;
    record(name, ok, "%s", detail);
    swprintf(logpath, MAX_PATH, L"%ls\\uwp-probe.txt", local_dir);
    f = _wfopen(logpath, L"a");
    if (f) { fprintf(f, "[%s] %-26s %s\n", ok ? "PASS" : "FAIL", name, detail); fclose(f); }
}

// Sampling profiler for a running process (symbols from its PDB).
// sampleprof.exe [seconds=20] [pid]   (no pid: the running NFL2K5.exe)
// Every ~2 ms each thread is suspended, its stack walked, and the functions
// on it counted: "self" = the function executing, "incl" = anywhere on the
// stack. Prints the busiest threads with their top functions. Threads
// parked in a kernel wait show up as Nt/ZwWait*; their share says how idle
// they are.
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

static DWORD find_pid(const wchar_t *name)
{
    PROCESSENTRY32W pe = { sizeof pe };
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (Process32FirstW(s, &pe)) do {
        if (!_wcsicmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return pid;
}

struct Thread {
    DWORD id;
    HANDLE h;
    unsigned samples = 0;
    std::unordered_map<DWORD64, unsigned> self, incl, owner;
};

static HANDLE g_proc;
static std::string g_exe = "NFL2K5!";
static std::unordered_map<DWORD64, DWORD64> g_start;     // pc -> function start
static std::unordered_map<DWORD64, std::string> g_name;  // function start -> name

static DWORD64 func_of(DWORD64 pc)
{
    auto it = g_start.find(pc);
    if (it != g_start.end()) return it->second;
    char buf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0, start = pc;
    IMAGEHLP_MODULE64 mi = {};
    mi.SizeOfStruct = sizeof mi;
    std::string mod = SymGetModuleInfo64(g_proc, pc, &mi) ? std::string(mi.ModuleName) + "!" : "?!";
    if (SymFromAddr(g_proc, pc, &disp, sym)) {
        start = pc - disp;
        // An export far behind pc is not this function (no PDB): name the
        // module and a 4 KB bucket instead.
        if (disp > 0x4000) {
            start = pc & ~0xFFFull;
            char t[64]; snprintf(t, sizeof t, "+0x%llX", (unsigned long long)(start - mi.BaseOfImage));
            if (!g_name.count(start)) g_name[start] = mod + t;
        } else if (!g_name.count(start)) g_name[start] = mod + sym->Name;
    } else if (!g_name.count(start)) {
        start = pc & ~0xFFFull;
        char t[64]; snprintf(t, sizeof t, "+0x%llX", (unsigned long long)(start - mi.BaseOfImage));
        if (!g_name.count(start)) g_name[start] = mod + t;
    }
    g_start[pc] = start;
    return start;
}

static void list_threads(DWORD pid, std::vector<Thread> &ts)
{
    THREADENTRY32 te = { sizeof te };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != pid) continue;
        bool have = false;
        for (auto &t : ts) if (t.id == te.th32ThreadID) { have = true; break; }
        if (have) continue;
        HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!th) continue;
        Thread t; t.id = te.th32ThreadID; t.h = th;
        ts.push_back(std::move(t));
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}

static void top(const std::unordered_map<DWORD64, unsigned> &m, unsigned total, int n, const char *what)
{
    std::vector<std::pair<unsigned, DWORD64>> v;
    for (auto &kv : m) v.push_back({ kv.second, kv.first });
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.first > b.first; });
    printf("  -- %s\n", what);
    for (int i = 0; i < n && i < (int)v.size(); i++)
        printf("  %6.2f%%  %s\n", 100.0 * v[i].first / total, g_name[v[i].second].c_str());
}

int wmain(int argc, wchar_t **argv)
{
    int seconds = argc > 1 ? _wtoi(argv[1]) : 20;
    DWORD pid = argc > 2 ? (DWORD)_wtoi(argv[2]) : find_pid(L"NFL2K5.exe");
    if (!pid) { fprintf(stderr, "no process\n"); return 1; }
    g_proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!g_proc) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(g_proc, nullptr, TRUE)) { fprintf(stderr, "SymInitialize failed\n"); return 1; }
    timeBeginPeriod(1);

    std::vector<Thread> ts;
    list_threads(pid, ts);
    DWORD t0 = GetTickCount(), relist = t0;
    unsigned rounds = 0;
    while (GetTickCount() - t0 < (DWORD)seconds * 1000) {
        if (GetTickCount() - relist > 1000) { list_threads(pid, ts); relist = GetTickCount(); }
        for (auto &t : ts) {
            if (SuspendThread(t.h) == (DWORD)-1) continue;
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(t.h, &ctx)) {
                STACKFRAME64 f = {};
                f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
                f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
                f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
                std::unordered_set<DWORD64> seen;
                bool owned = false;
                for (int i = 0; i < 64; i++) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_proc, t.h, &f, &ctx, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset)
                        break;
                    DWORD64 fn = func_of(f.AddrPC.Offset);
                    if (i == 0) t.self[fn]++;
                    // owner: the first frame in the main executable -- who
                    // is responsible for time spent inside a DLL.
                    if (!owned && g_name[fn].rfind(g_exe, 0) == 0) { t.owner[fn]++; owned = true; }
                    if (seen.insert(fn).second) t.incl[fn]++;
                }
                t.samples++;
            }
            ResumeThread(t.h);
        }
        rounds++;
        Sleep(2);
    }
    timeEndPeriod(1);

    // Busy = samples not parked in a wait.
    std::vector<std::pair<unsigned, Thread *>> order;
    for (auto &t : ts) {
        unsigned idle = 0;
        for (auto &kv : t.self) {
            const std::string &n = g_name[kv.first];
            if (n.find("Wait") != std::string::npos || n.find("Delay") != std::string::npos
                    || n.find("NtRemoveIoCompletion") != std::string::npos || n == "ZwYieldExecution"
                    || n.find("SleepEx") != std::string::npos)
                idle += kv.second;
        }
        order.push_back({ t.samples - idle, &t });
    }
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    printf("%u sampling rounds over %d s\n", rounds, seconds);
    for (auto &o : order) {
        Thread &t = *o.second;
        if (!t.samples || o.first * 20 < t.samples) continue;     // < 5% busy
        printf("=== thread %lu: busy %.1f%% of %u samples\n", t.id, 100.0 * o.first / t.samples, t.samples);
        top(t.self, t.samples, 30, "self");
        top(t.incl, t.samples, 45, "inclusive");
        top(t.owner, t.samples, 25, "owner (first frame in the exe)");
    }
    SymCleanup(g_proc);
    return 0;
}

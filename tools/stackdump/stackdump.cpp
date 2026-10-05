// Print every thread's call stack of a running process (symbols from its PDB).
// For hangs: stackdump.exe <pid>  (or no argument: the running NFL2K5.exe).
// Threads are suspended one at a time while their stack is walked.
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#pragma comment(lib, "dbghelp.lib")

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

int wmain(int argc, wchar_t **argv)
{
    DWORD pid = argc > 1 ? (DWORD)_wtoi(argv[1]) : find_pid(L"NFL2K5.exe");
    if (!pid) { fprintf(stderr, "no process\n"); return 1; }
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(proc, nullptr, TRUE)) { fprintf(stderr, "SymInitialize failed\n"); return 1; }

    THREADENTRY32 te = { sizeof te };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!th) continue;
        SuspendThread(th);
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, &ctx)) {
            printf("=== thread %lu\n", te.th32ThreadID);
            STACKFRAME64 f = {};
            f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
            f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
            f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
            for (int i = 0; i < 40; i++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &f, &ctx, nullptr,
                                 SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset)
                    break;
                char buf[sizeof(SYMBOL_INFO) + 256] = {};
                SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 255;
                DWORD64 disp = 0;
                if (SymFromAddr(proc, f.AddrPC.Offset, &disp, sym))
                    printf("  %s+0x%llX\n", sym->Name, (unsigned long long)disp);
                else
                    printf("  0x%llX\n", (unsigned long long)f.AddrPC.Offset);
            }
        }
        ResumeThread(th);
        CloseHandle(th);
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    SymCleanup(proc);
    CloseHandle(proc);
    return 0;
}

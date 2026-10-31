/*
 * appcontainer-run.c -- run a program inside an AppContainer (UWP sandbox)
 * without installing a package. Used to run the UWP exe's probes on a PC
 * where the package cannot be installed (Developer Mode off).
 *
 *   appcontainer-run.exe <exe> [args...]      ("%AC%" in args = the
 *                                             container's writable folder)
 *
 * Creates the AppContainer profile "NFL2K5.UwpProbe" (per user, like any
 * browser sandbox), runs the program with no capabilities, waits, prints its
 * exit code, and deletes the profile again. The exe's folder must grant
 * ALL APPLICATION PACKAGES (S-1-15-2-1) read+execute.
 *
 * Build: cl /nologo appcontainer-run.c userenv.lib
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

static const wchar_t *k_name = L"NFL2K5.UwpProbe";

int wmain(int argc, wchar_t **argv)
{
    PSID sid = NULL;
    HRESULT hr;
    PWSTR folder = NULL;
    wchar_t cmd[4096];
    SECURITY_CAPABILITIES caps = {0};
    STARTUPINFOEXW si = {0};
    PROCESS_INFORMATION pi = {0};
    SIZE_T attr_size = 0;
    DWORD code = 0xFFFFFFFF;
    int i;

    if (argc < 2) {
        fwprintf(stderr, L"usage: appcontainer-run <exe> [args, %%AC%% = container folder]\n");
        return 2;
    }
    hr = CreateAppContainerProfile(k_name, L"NFL2K5 UWP probe", L"NFL2K5 UWP probe", NULL, 0, &sid);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = DeriveAppContainerSidFromAppContainerName(k_name, &sid);
    if (FAILED(hr)) { fwprintf(stderr, L"AppContainer profile: 0x%08lx\n", hr); return 3; }
    {
        PWSTR s = NULL;
        ConvertSidToStringSidW(sid, &s);
        wprintf(L"AppContainer SID %ls\n", s ? s : L"?");
        LocalFree(s);
    }
    {
        PWSTR s = NULL;
        ConvertSidToStringSidW(sid, &s);
        hr = GetAppContainerFolderPath(s, &folder);
        LocalFree(s);
    }
    if (FAILED(hr) || !folder) { fwprintf(stderr, L"container folder: 0x%08lx\n", hr); return 4; }
    wprintf(L"container folder %ls\n", folder);

    /* Command line with %AC% expanded. */
    _snwprintf(cmd, 4096, L"\"%ls\"", argv[1]);
    for (i = 2; i < argc; i++) {
        wcscat(cmd, L" \"");
        if (wcscmp(argv[i], L"%AC%") == 0) wcscat(cmd, folder);
        else wcscat(cmd, argv[i]);
        wcscat(cmd, L"\"");
    }

    caps.AppContainerSid = sid;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_size);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                              &caps, sizeof caps, NULL, NULL);
    si.StartupInfo.cb = sizeof si;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &si.StartupInfo, &pi)) {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
    } else {
        wprintf(L"pid %lu running in the AppContainer\n", pi.dwProcessId);
        if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_TIMEOUT) {
            fwprintf(stderr, L"timeout; terminating pid %lu\n", pi.dwProcessId);
            TerminateProcess(pi.hProcess, 1);
        }
        GetExitCodeProcess(pi.hProcess, &code);
        wprintf(L"exit code %lu\n", code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    /* Print the probe log before the profile (and its folder) goes away. */
    {
        wchar_t log[MAX_PATH];
        FILE *f;
        _snwprintf(log, MAX_PATH, L"%ls\\uwp-probe.txt", folder);
        f = _wfopen(log, L"r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof line, f)) fputs(line, stdout);
            fclose(f);
        }
    }
    DeleteAppContainerProfile(k_name);
    CoTaskMemFree(folder);
    FreeSid(sid);
    return (int)code;
}

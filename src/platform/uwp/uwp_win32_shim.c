/*
 * uwp_win32_shim.c -- Win32 calls outside the UWP API set, for the UWP build.
 *
 * The PC sources are compiled unchanged with the desktop headers, so they
 * call user32/gdi32/shell32/dbghelp/... through import slots (__imp_X). The
 * UWP link has no desktop import libraries, so this file defines those slots
 * itself, pointing at UWP-safe stand-ins: no-ops for window chrome and
 * dialogs, the CoreWindow's state where the game asks about its window or
 * keys, LocalState for shell folders. Nothing here is imported from a DLL
 * that does not exist on Xbox; tools/uwp/check-imports.py verifies that.
 */
#include <windows.h>
#include <stdio.h>

#ifndef SE_ERR_NOASSOC
#define SE_ERR_NOASSOC 31   /* shellapi.h */
#endif

#define SHIM(ret, name, args) \
    static ret WINAPI shim_##name args; \
    void *__imp_##name = (void *)shim_##name; \
    static ret WINAPI shim_##name args

/* A generic "did nothing" answer: FALSE / NULL / 0. On x64 there is one
 * calling convention and the caller owns the stack, so one function can stand
 * in for any signature whose correct failure value is zero. */
static INT_PTR WINAPI shim_zero(void) { return 0; }
static INT_PTR WINAPI shim_true(void) { return 1; }

#define ZERO(name) void *__imp_##name = (void *)shim_zero;
#define TRUE_(name) void *__imp_##name = (void *)shim_true;

/* ---- windows, messages, menus: the CoreWindow belongs to uwp_main.cpp ---- */
ZERO(CreateWindowExA) ZERO(CreateWindowExW) ZERO(RegisterClassA) ZERO(RegisterClassW)
ZERO(UnregisterClassW) ZERO(DestroyWindow) ZERO(DefWindowProcA) ZERO(DefWindowProcW)
ZERO(PeekMessageA) ZERO(DispatchMessageA) ZERO(TranslateMessage) ZERO(PostMessageA)
ZERO(PostMessageW) ZERO(PostThreadMessageW) ZERO(SetPropW) ZERO(SetWindowTextA)
ZERO(GetWindowTextA) ZERO(GetWindowThreadProcessId) ZERO(EnumWindows) ZERO(GetActiveWindow)
ZERO(GetForegroundWindow) ZERO(ScreenToClient) ZERO(GetCursorPos) ZERO(LoadCursorA)
ZERO(LoadIconA) ZERO(CreateIconIndirect) ZERO(EnumDisplaySettingsA) ZERO(GetKeyNameTextA)
ZERO(MapVirtualKeyA) TRUE_(AdjustWindowRect)

/* ---- GDI: the F1 overlay text and prompt art draw into DIBs with GDI.
 * Failing CreateDIBSection/CreateCompatibleDC makes those paths skip; a
 * later stage draws that text another way (DirectWrite is in the UWP set). */
ZERO(GetDC) ZERO(ReleaseDC) ZERO(CreateCompatibleDC) ZERO(DeleteDC) ZERO(CreateDIBSection)
ZERO(CreateBitmap) ZERO(SelectObject) ZERO(DeleteObject) ZERO(GetStockObject) ZERO(CreateFontA)
ZERO(CreatePen) ZERO(CreateSolidBrush) ZERO(SetBkMode) ZERO(SetTextColor) ZERO(TextOutA)
ZERO(DrawTextA) ZERO(GetTextExtentPoint32A) ZERO(FillRect) ZERO(Rectangle) ZERO(RoundRect)
ZERO(Ellipse) ZERO(MoveToEx) ZERO(LineTo) ZERO(Polyline) ZERO(SetPixel) ZERO(GdiFlush)
ZERO(AddFontResourceExW)

/* ---- dbghelp (crash reporter symbol lookup): not on Xbox ---- */
ZERO(SymInitialize) ZERO(SymCleanup) ZERO(SymSetOptions) ZERO(SymFromAddr) ZERO(SymGetLineFromAddr64)

/* ---- the window rectangle the presenter asks for ---- */
LONG g_uwp_window_w = 1920, g_uwp_window_h = 1080;   /* uwp_main.cpp keeps these current */

SHIM(BOOL, GetClientRect, (HWND hwnd, LPRECT r))
{
    (void)hwnd;
    if (!r) return FALSE;
    r->left = r->top = 0;
    r->right = g_uwp_window_w;
    r->bottom = g_uwp_window_h;
    return TRUE;
}

/* ---- keyboard state from the CoreWindow (uwp_main.cpp fills it) ---- */
volatile unsigned char g_uwp_keys[256];

SHIM(SHORT, GetAsyncKeyState, (int vk))
{
    return (vk >= 0 && vk < 256 && g_uwp_keys[vk]) ? (SHORT)0x8000 : 0;
}

SHIM(SHORT, GetKeyState, (int vk))
{
    return (vk >= 0 && vk < 256 && g_uwp_keys[vk]) ? (SHORT)0x8000 : 0;
}

SHIM(UINT, GetDoubleClickTime, (void)) { return 500; }

/* ---- dialogs and shell ---- */
SHIM(int, MessageBoxA, (HWND hwnd, LPCSTR text, LPCSTR caption, UINT type))
{
    (void)hwnd; (void)type;
    fprintf(stderr, "[UWP] MessageBox: %s: %s\n", caption ? caption : "", text ? text : "");
    return IDOK;
}

SHIM(HINSTANCE, ShellExecuteA, (HWND h, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show))
{
    (void)h; (void)op; (void)params; (void)dir; (void)show;
    fprintf(stderr, "[UWP] ShellExecute not available: %s\n", file ? file : "");
    return (HINSTANCE)(INT_PTR)SE_ERR_NOASSOC;
}

SHIM(HINSTANCE, ShellExecuteW, (HWND h, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show))
{
    (void)h; (void)op; (void)file; (void)params; (void)dir; (void)show;
    return (HINSTANCE)(INT_PTR)SE_ERR_NOASSOC;
}

/* CSIDL_LOCAL_APPDATA and friends: the package's LocalState folder. */
SHIM(HRESULT, SHGetFolderPathW, (HWND h, int csidl, HANDLE token, DWORD flags, LPWSTR path))
{
    (void)h; (void)csidl; (void)token; (void)flags;
    if (!path) return E_INVALIDARG;
    if (!GetEnvironmentVariableW(L"NFL2K5_UWP_LOCALSTATE", path, MAX_PATH)) return E_FAIL;
    return S_OK;
}

SHIM(int, SHCreateDirectoryExW, (HWND h, LPCWSTR path, const SECURITY_ATTRIBUTES *sa))
{
    WCHAR buf[MAX_PATH];
    size_t i, n;
    (void)h; (void)sa;
    if (!path) return ERROR_BAD_PATHNAME;
    n = wcslen(path);
    if (n >= MAX_PATH) return ERROR_FILENAME_EXCED_RANGE;
    wcscpy_s(buf, MAX_PATH, path);
    for (i = 3; i <= n; i++) {
        if (buf[i] == L'\\' || buf[i] == L'/' || buf[i] == 0) {
            WCHAR c = buf[i];
            buf[i] = 0;
            CreateDirectoryW(buf, NULL);
            buf[i] = c;
        }
    }
    return (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) ? ERROR_SUCCESS : (int)GetLastError();
}

/* ---- used by FidelityFX frame generation's swap-chain helper ---- */
ZERO(GetWindowRect) ZERO(IntersectRect)

/* ---- local desktop-SDL extension (Vulkan pipeline cache, gx_sdl.c): the
 * UWP SDL has no Vulkan backend, so there is never a cache to save. ---- */
typedef struct SDL_GPUDevice SDL_GPUDevice;
int SDL_SaveGPUPipelineCacheVulkan(SDL_GPUDevice *device, const char *path)
{
    (void)device; (void)path;
    return 0;
}

/* ---- kernel32: PulseEvent is not in the UWP set. Set then reset gives the
 * same result: SetEvent satisfies the threads waiting at that moment (all of
 * them for a manual-reset event, one for auto-reset), ResetEvent clears it
 * when nobody was waiting. ---- */
SHIM(BOOL, PulseEvent, (HANDLE ev))
{
    BOOL ok = SetEvent(ev);
    ResetEvent(ev);
    return ok;
}

/* These three are called directly (their headers do not declare them
 * dllimport), so they are plain definitions; the desktop import libraries
 * (d3d11.lib, mf.lib, pdh.lib) are kept off the link (uwp/CMakeLists.txt). */

/* The native-D3D11 D3D8 device makes an HWND swap chain; the UWP build
 * renders through gx on SDL_GPU (D3D12) instead. */
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void *a, int b, HMODULE c, UINT d, const void *e, UINT f, UINT g,
                                             const void *h, void **i, void **j, void *k, void **l)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)k;
    if (i) *i = NULL;
    if (j) *j = NULL;
    if (l) *l = NULL;
    return E_NOTIMPL;
}

/* Media Foundation transcode helper (WMA music import): not in the UWP set. */
HRESULT WINAPI MFTranscodeGetAudioOutputAvailableTypes(REFGUID sub, DWORD flags, void *attrs, void **out)
{
    (void)sub; (void)flags; (void)attrs;
    if (out) *out = NULL;
    return E_NOTIMPL;
}

/* pdh (GPU/CPU counters for the perf HUD): "no such counter". */
#define SHIM_PDH_FAIL ((LONG)0xC0000BB8L)   /* PDH_CSTATUS_NO_OBJECT */
LONG WINAPI PdhOpenQueryW(const WCHAR *src, DWORD_PTR user, void **q) { (void)src; (void)user; if (q) *q = NULL; return SHIM_PDH_FAIL; }
LONG WINAPI PdhAddEnglishCounterW(void *q, const WCHAR *path, DWORD_PTR user, void **c) { (void)q; (void)path; (void)user; if (c) *c = NULL; return SHIM_PDH_FAIL; }
LONG WINAPI PdhCollectQueryData(void *q) { (void)q; return SHIM_PDH_FAIL; }
LONG WINAPI PdhGetFormattedCounterArrayW(void *c, DWORD fmt, DWORD *size, DWORD *count, void *items)
{
    (void)c; (void)fmt; (void)items;
    if (size) *size = 0;
    if (count) *count = 0;
    return SHIM_PDH_FAIL;
}

/* ---- winmm waveOut (an old APU output fallback): no driver; SDL/XAudio2 plays ---- */
static UINT WINAPI shim_mm_nodriver(void) { return 6; /* MMSYSERR_NODRIVER */ }
void *__imp_waveOutOpen = (void *)shim_mm_nodriver;
void *__imp_waveOutClose = (void *)shim_mm_nodriver;
void *__imp_waveOutPrepareHeader = (void *)shim_mm_nodriver;
void *__imp_waveOutUnprepareHeader = (void *)shim_mm_nodriver;
void *__imp_waveOutWrite = (void *)shim_mm_nodriver;
void *__imp_waveOutReset = (void *)shim_mm_nodriver;

/*
 * uwp_hwnd_test.cpp -- run the UWP build of the game unpackaged, in an
 * AppContainer on a Windows PC, with a plain Win32 window standing in for the
 * CoreWindow. Test scaffolding only: "NFL2K5.exe --hwnd-test <writable dir>",
 * started by tools/uwp/probe-appcontainer.ps1 -Game.
 *
 * Everything else is the real UWP code path (same exe, same sandbox rules,
 * UWP-minimal SDL, Win32 shim, generated code), so it shows whether the game
 * boots under UWP restrictions on a PC where the package itself cannot be
 * installed (Developer Mode off). user32 is loaded at run time here so the
 * exe still imports nothing outside the UWP API set; on Xbox this path is
 * never taken.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>

#include "uwp_game.h"

extern "C" volatile unsigned char g_uwp_keys[256];
extern "C" LONG g_uwp_window_w, g_uwp_window_h;

namespace {

struct User32 {
    HMODULE mod = nullptr;
    decltype(&RegisterClassW) RegisterClassW_ = nullptr;
    decltype(&CreateWindowExW) CreateWindowExW_ = nullptr;
    decltype(&DefWindowProcW) DefWindowProcW_ = nullptr;
    decltype(&PeekMessageW) PeekMessageW_ = nullptr;
    decltype(&TranslateMessage) TranslateMessage_ = nullptr;
    decltype(&DispatchMessageW) DispatchMessageW_ = nullptr;
    decltype(&ShowWindow) ShowWindow_ = nullptr;
    decltype(&AdjustWindowRect) AdjustWindowRect_ = nullptr;
    decltype(&LoadCursorW) LoadCursorW_ = nullptr;

    bool load()
    {
        mod = LoadLibraryW(L"user32.dll");
        if (!mod) return false;
#define GET(n) n##_ = reinterpret_cast<decltype(n##_)>(reinterpret_cast<void *>(GetProcAddress(mod, #n)))
        GET(RegisterClassW); GET(CreateWindowExW); GET(DefWindowProcW); GET(PeekMessageW);
        GET(TranslateMessage); GET(DispatchMessageW); GET(ShowWindow); GET(AdjustWindowRect); GET(LoadCursorW);
#undef GET
        return RegisterClassW_ && CreateWindowExW_ && DefWindowProcW_ && PeekMessageW_ && DispatchMessageW_;
    }
} u;

bool s_closed = false;

LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: s_closed = true; return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: g_uwp_keys[wp & 0xFF] = 1; break;
    case WM_KEYUP: case WM_SYSKEYUP: g_uwp_keys[wp & 0xFF] = 0; break;
    case WM_SIZE:
        if (LOWORD(lp) && HIWORD(lp)) { g_uwp_window_w = LOWORD(lp); g_uwp_window_h = HIWORD(lp); }
        break;
    }
    return u.DefWindowProcW_(h, msg, wp, lp);
}

} // namespace

extern "C" int uwp_hwnd_test_main(const wchar_t *local_dir)
{
    if (!u.load()) return 90;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NFL2K5UwpTest";
    if (u.LoadCursorW_) wc.hCursor = u.LoadCursorW_(nullptr, MAKEINTRESOURCEW(32512));
    if (!u.RegisterClassW_(&wc)) return 91;
    RECT r = {0, 0, 1280, 720};
    if (u.AdjustWindowRect_) u.AdjustWindowRect_(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = u.CreateWindowExW_(0, wc.lpszClassName, L"ESPN NFL 2K5 - UWP build (AppContainer test)",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                   r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 92;
    g_uwp_window_w = 1280;
    g_uwp_window_h = 720;
    u.ShowWindow_(hwnd, SW_SHOWNORMAL);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\'));
    uwp_game_start_hwnd(hwnd, local_dir, dir.c_str());

    int code = 0;
    while (!s_closed && !uwp_game_exited(&code)) {
        MSG m;
        while (u.PeekMessageW_(&m, nullptr, 0, 0, PM_REMOVE)) {
            u.TranslateMessage_(&m);
            u.DispatchMessageW_(&m);
        }
        uwp_game_tick();
        Sleep(4);
    }
    return code;
}

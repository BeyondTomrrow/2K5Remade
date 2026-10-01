// presentation-render: load a page in WebView2 (the engine NFL2K5.exe uses
// for HTML presentations) and save a 1:1 PNG of it. Used to check packages
// against reference screenshots (tools/nbc-snf/diff.py).
//
//   presentation-render.exe <url> <out.png> <width> <height> [delay-ms] [folder]
//
// With [folder], https://presentation.local/ serves that folder, as in the
// game (mods/presentations); pages need a real origin for CSS masks.
//
// The window is placed off screen; the PNG is the page at device scale 1.
#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>
#include <string>
#include <cstdio>
#include "WebView2.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static ComPtr<ICoreWebView2Controller> g_ctrl;
static ComPtr<ICoreWebView2> g_wv;
static std::wstring g_url, g_out, g_folder;
static int g_w, g_h, g_delay = 1500, g_rc = 1;
static HWND g_hwnd;

static void quit(int rc) { g_rc = rc; PostQuitMessage(rc); }

static void capture()
{
    IStream *stream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(g_out.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) {
        fwprintf(stderr, L"cannot write %ls\n", g_out.c_str());
        quit(3);
        return;
    }
    g_wv->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream,
        Callback<ICoreWebView2CapturePreviewCompletedHandler>([stream](HRESULT hr) -> HRESULT {
            stream->Release();
            if (FAILED(hr)) { fprintf(stderr, "capture failed 0x%08lx\n", hr); quit(4); }
            else { fwprintf(stdout, L"saved %ls\n", g_out.c_str()); quit(0); }
            return S_OK;
        }).Get());
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER) { KillTimer(h, 1); capture(); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: presentation-render <url> <out.png> <width> <height> [delay-ms]\n");
        return 2;
    }
    g_url = argv[1]; g_out = argv[2]; g_w = _wtoi(argv[3]); g_h = _wtoi(argv[4]);
    if (argc > 5) g_delay = _wtoi(argv[5]);
    if (argc > 6) g_folder = argv[6];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PresentationRender";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                             -20000, -20000, g_w, g_h, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring data = std::wstring(tmp) + L"presentation-render-profile";
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT r, ICoreWebView2Environment *env) -> HRESULT {
            if (FAILED(r)) { fprintf(stderr, "no WebView2 runtime\n"); quit(5); return S_OK; }
            env->CreateCoreWebView2Controller(g_hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([](HRESULT r2, ICoreWebView2Controller *c) -> HRESULT {
                    if (FAILED(r2) || !c) { fprintf(stderr, "controller failed\n"); quit(6); return S_OK; }
                    g_ctrl = c;
                    ComPtr<ICoreWebView2Controller3> c3;
                    if (SUCCEEDED(g_ctrl.As(&c3))) {
                        c3->put_ShouldDetectMonitorScaleChanges(FALSE);
                        c3->put_RasterizationScale(1.0);
                    }
                    RECT b{ 0, 0, g_w, g_h };
                    g_ctrl->put_Bounds(b);
                    g_ctrl->put_IsVisible(TRUE);
                    g_ctrl->get_CoreWebView2(&g_wv);
                    ComPtr<ICoreWebView2Settings> s;
                    if (SUCCEEDED(g_wv->get_Settings(&s))) s->put_IsStatusBarEnabled(FALSE);
                    ComPtr<ICoreWebView2_3> wv3;
                    if (!g_folder.empty() && SUCCEEDED(g_wv.As(&wv3)))
                        wv3->SetVirtualHostNameToFolderMapping(L"presentation.local", g_folder.c_str(),
                                                               COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                    g_wv->add_NavigationCompleted(
                        Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *a) -> HRESULT {
                            BOOL ok = FALSE;
                            a->get_IsSuccess(&ok);
                            if (!ok) { fprintf(stderr, "navigation failed\n"); quit(7); return S_OK; }
                            SetTimer(g_hwnd, 1, g_delay, nullptr);   // fonts, images, layout
                            return S_OK;
                        }).Get(), nullptr);
                    g_wv->Navigate(g_url.c_str());
                    return S_OK;
                }).Get());
            return S_OK;
        }).Get());
    if (FAILED(hr)) { fprintf(stderr, "CreateCoreWebView2Environment failed\n"); return 5; }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return g_rc;
}

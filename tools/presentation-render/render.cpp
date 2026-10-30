// presentation-render: load a page in WebView2 (the engine NFL2K5.exe uses
// for HTML presentations) and save a 1:1 PNG of it. Used to check packages
// against reference screenshots (tools/nbc-snf/diff.py).
//
//   presentation-render.exe <url> <out.png> <width> <height> [delay-ms] [folder]
//                          [--batch frames.tsv]
// Batch rows are strictly ascending milliseconds, TAB, PNG path (UTF-8).
// The preview page must expose NFL2K5PreviewClock.captureFrame(ms).
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
#include <vector>
#include <fstream>
#include <filesystem>
#include <climits>
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
struct PreviewFrame { int milliseconds; std::wstring out; };
static std::vector<PreviewFrame> g_frames;
static size_t g_frame_index = 0;
static bool g_waiting_frame = false, g_capturing = false;
static constexpr UINT WM_NEXT_PREVIEW_FRAME = WM_APP + 1;

static void quit(int rc) { g_rc = rc; PostQuitMessage(rc); }

static bool load_frames(const wchar_t *manifest)
{
    std::ifstream input{std::filesystem::path(manifest)};
    std::string line;
    int previous = -1, row = 0;
    while (std::getline(input, line)) {
        ++row;
        if (row == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto tab = line.find('\t');
        try {
            size_t parsed = 0;
            const long long milliseconds = std::stoll(line.substr(0, tab), &parsed);
            if (tab == std::string::npos || parsed != tab || milliseconds <= previous || milliseconds > INT_MAX)
                throw std::exception();
            const auto name = line.substr(tab + 1);
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), (int)name.size(), nullptr, 0);
            if (length <= 0) throw std::exception();
            std::wstring wide(length, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), (int)name.size(), &wide[0], length);
            auto out = std::filesystem::path(wide);
            if (out.is_relative()) out = std::filesystem::path(manifest).parent_path() / out;
            g_frames.push_back({(int)milliseconds, out.wstring()});
            previous = (int)milliseconds;
        } catch (...) { fprintf(stderr, "invalid batch row %d: expected ascending ms TAB PNG path\n", row); return false; }
    }
    if (!input.eof() || g_frames.empty()) { fprintf(stderr, "cannot read a nonempty batch manifest\n"); return false; }
    return true;
}

static void next_frame()
{
    if (g_frame_index >= g_frames.size()) { quit(0); return; }
    g_out = g_frames[g_frame_index].out;
    const int milliseconds = g_frames[g_frame_index].milliseconds;
    g_waiting_frame = true;
    SetTimer(g_hwnd, 2, 30000, nullptr);
    std::wstring script = L"(function(){if(!window.NFL2K5PreviewClock){chrome.webview.postMessage('SNF_PREVIEW_ERROR:missing deterministic preview clock');return;}"
        L"window.NFL2K5PreviewClock.captureFrame(" + std::to_wstring(milliseconds) + L").then(function(){chrome.webview.postMessage('SNF_PREVIEW_READY:"
        + std::to_wstring(milliseconds) + L"');}).catch(function(e){chrome.webview.postMessage('SNF_PREVIEW_ERROR:'+String(e));});})();";
    HRESULT started = g_wv->ExecuteScript(script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>([](HRESULT hr, LPCWSTR) -> HRESULT {
            if (FAILED(hr)) { fprintf(stderr, "preview seek script failed 0x%08lx\n", hr); quit(8); }
            return S_OK;
        }).Get());
    if (FAILED(started)) { fprintf(stderr, "cannot start preview seek\n"); quit(8); }
}

static void capture()
{
    IStream *stream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(g_out.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) {
        fwprintf(stderr, L"cannot write %ls\n", g_out.c_str());
        quit(3);
        return;
    }
    g_capturing = true;
    HRESULT started = g_wv->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream,
        Callback<ICoreWebView2CapturePreviewCompletedHandler>([stream](HRESULT hr) -> HRESULT {
            stream->Release();
            g_capturing = false;
            if (FAILED(hr)) { fprintf(stderr, "capture failed 0x%08lx\n", hr); quit(4); }
            else {
                fwprintf(stdout, L"saved %ls\n", g_out.c_str()); fflush(stdout);
                if (g_frames.empty()) quit(0);
                else { ++g_frame_index; PostMessageW(g_hwnd, WM_NEXT_PREVIEW_FRAME, 0, 0); }
            }
            return S_OK;
        }).Get());
    if (FAILED(started)) { stream->Release(); g_capturing = false; fprintf(stderr, "cannot start capture\n"); quit(4); }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NEXT_PREVIEW_FRAME) { next_frame(); return 0; }
    if (m == WM_TIMER && w == 1) {
        KillTimer(h, 1);
        if (g_frames.empty()) capture(); else next_frame();
        return 0;
    }
    if (m == WM_TIMER && w == 2) { KillTimer(h, 2); fprintf(stderr, "preview frame readiness timed out\n"); quit(8); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: presentation-render <url> <out.png> <width> <height> [delay-ms] [folder] [--batch frames.tsv]\n");
        return 2;
    }
    g_url = argv[1]; g_out = argv[2]; g_w = _wtoi(argv[3]); g_h = _wtoi(argv[4]);
    if (argc > 5) g_delay = _wtoi(argv[5]);
    if (argc > 6) g_folder = argv[6];
    if (argc > 7) {
        if (argc != 9 || std::wstring(argv[7]) != L"--batch" || !load_frames(argv[8])) return 2;
    }
    if (g_w <= 0 || g_h <= 0 || g_delay < 0) { fprintf(stderr, "invalid image size or delay\n"); return 2; }
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
                    if (!g_frames.empty()) g_wv->add_WebMessageReceived(
                        Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *a) -> HRESULT {
                            LPWSTR value = nullptr;
                            if (FAILED(a->TryGetWebMessageAsString(&value))) return S_OK;
                            std::wstring message = value; CoTaskMemFree(value);
                            if (message.compare(0, 18, L"SNF_PREVIEW_ERROR:") == 0) {
                                fwprintf(stderr, L"%ls\n", message.c_str()); quit(8);
                            } else if (g_waiting_frame && !g_capturing && g_frame_index < g_frames.size() &&
                                       message == L"SNF_PREVIEW_READY:" + std::to_wstring(g_frames[g_frame_index].milliseconds)) {
                                g_waiting_frame = false; KillTimer(g_hwnd, 2); capture();
                            }
                            return S_OK;
                        }).Get(), nullptr);
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

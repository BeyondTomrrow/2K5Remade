/* PresentationHost on WebView2 (the Edge engine that ships with Windows
 * 10/11; nothing extra for players to install).
 *
 * The page is never shown in a window of its own. WebView2's composition
 * controller renders it into a Windows.UI.Composition visual, and
 * Windows.Graphics.Capture captures that visual -- with its alpha channel --
 * as Direct3D textures, which are copied into a CPU surface. The game
 * composites that surface like any other HUD image, so no graphics backend
 * knows HTML exists.
 *
 * Everything WebView2 runs on one thread (the host thread): it needs a COM
 * single-threaded apartment with a message loop and a DispatcherQueue.
 * Capture frames arrive on a capture worker thread. */
#include "presentation_host.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>
#include <DispatcherQueue.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>

#include "WebView2.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace wuc = winrt::Windows::UI::Composition;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

namespace nfl2k5 {
namespace {

constexpr UINT WM_PRES_FLUSH = WM_APP + 1;

std::wstring widen(const std::string &s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring full_path(const std::string &p)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(widen(p).c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
    return n ? std::wstring(buf, n) : widen(p);
}

/* Package folder relative to the served root, with forward slashes. */
std::wstring relative_url_path(const std::wstring &root, const std::wstring &dir)
{
    std::wstring r = root, d = dir;
    for (auto &c : r) if (c == L'\\') c = L'/';
    for (auto &c : d) if (c == L'\\') c = L'/';
    while (!r.empty() && r.back() == L'/') r.pop_back();
    if (d.size() > r.size() && _wcsnicmp(d.c_str(), r.c_str(), r.size()) == 0 && d[r.size()] == L'/')
        return d.substr(r.size() + 1);
    return d;
}

class WebView2Host final : public PresentationHost {
public:
    ~WebView2Host() override { stop(); }

    bool start(const PresentationHostConfig &config) override
    {
        if (thread_.joinable()) return false;
        config_ = config;
        set_status("starting");
        thread_ = std::thread([this] { run(); });
        return true;
    }

    void stop() override
    {
        if (!thread_.joinable()) return;
        quitting_ = true;
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        thread_.join();
    }

    void post_state(const std::string &json) override
    {
        {
            std::lock_guard<std::mutex> g(msg_lock_);
            pending_state_ = "{\"type\":\"state\",\"state\":" + json + "}";
        }
        if (hwnd_) PostMessageW(hwnd_, WM_PRES_FLUSH, 0, 0);
    }

    void post_event(const std::string &json) override
    {
        {
            std::lock_guard<std::mutex> g(msg_lock_);
            pending_events_.push_back("{\"type\":\"event\",\"event\":" + json + "}");
        }
        if (hwnd_) PostMessageW(hwnd_, WM_PRES_FLUSH, 0, 0);
    }

    bool acquire_surface(PresentationSurface &out) override
    {
        std::lock_guard<std::mutex> g(frame_lock_);
        if (!front_serial_) return false;
        /* The consumer gets its own copy, taken only when a new frame exists;
         * it stays valid until the next call. */
        if (consumer_serial_ != front_serial_) {
            consumer_ = front_;
            consumer_serial_ = front_serial_;
        }
        out.pixels = consumer_.data();
        out.width = frame_w_;
        out.height = frame_h_;
        out.stride = frame_w_ * 4;
        out.serial = consumer_serial_;
        return true;
    }

    std::string status() const override
    {
        std::lock_guard<std::mutex> g(status_lock_);
        return status_;
    }

private:
    void set_status(const std::string &s)
    {
        {
            std::lock_guard<std::mutex> g(status_lock_);
            status_ = s;
        }
        fprintf(stderr, "[HTMLPRES] %s\n", s.c_str());
    }

    static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto *self = (WebView2Host *)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_PRES_FLUSH && self) { self->flush(); return 0; }
        if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
        if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(h, m, w, l);
    }

    void run()
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        DispatcherQueueOptions dq{ sizeof dq, DQTYPE_THREAD_CURRENT, DQTAT_COM_STA };
        ABI::Windows::System::IDispatcherQueueController *dqc = nullptr;
        if (FAILED(CreateDispatcherQueueController(dq, &dqc))) { set_status("error: no DispatcherQueue"); return; }

        int pw = (int)(config_.canvas_width * config_.raster_scale + 0.5f);
        int ph = (int)(config_.canvas_height * config_.raster_scale + 0.5f);

        WNDCLASSW wc{};
        wc.lpfnWndProc = wndproc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"NFL2K5PresentationHost";
        RegisterClassW(&wc);
        /* Never shown: WebView2 needs a parent window for focus and input
         * routing even when it renders into a composition visual. */
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                                0, 0, pw, ph, nullptr, nullptr, wc.hInstance, nullptr);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

        try {
            compositor_ = wuc::Compositor();
            root_ = compositor_.CreateContainerVisual();
            root_.Size({ (float)pw, (float)ph });
            if (!start_capture(pw, ph)) { set_status("error: Windows.Graphics.Capture unavailable"); }
        } catch (const winrt::hresult_error &e) {
            set_status("error: composition/capture " + winrt::to_string(e.message()));
        }

        create_webview(pw, ph);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        /* Teardown on this thread, in reverse order. */
        if (session_) { session_.Close(); session_ = nullptr; }
        if (pool_) { pool_.Close(); pool_ = nullptr; }
        if (controller_) controller_->Close();
        webview_.Reset(); comp_controller_.Reset(); controller_.Reset();
        root_ = nullptr; compositor_ = nullptr;
        hwnd_ = nullptr;
        if (dqc) dqc->Release();
        winrt::uninit_apartment();
    }

    void create_webview(int pw, int ph)
    {
        std::wstring user_data = full_path("cache/webview2");
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, pw, ph](HRESULT r, ICoreWebView2Environment *env) -> HRESULT {
                    if (FAILED(r) || !env) { set_status("error: WebView2 runtime not available"); return S_OK; }
                    ComPtr<ICoreWebView2Environment3> env3;
                    if (FAILED(env->QueryInterface(IID_PPV_ARGS(&env3)))) {
                        set_status("error: WebView2 runtime too old (no composition hosting)");
                        return S_OK;
                    }
                    env3->CreateCoreWebView2CompositionController(hwnd_,
                        Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                            [this, pw, ph](HRESULT r2, ICoreWebView2CompositionController *cc) -> HRESULT {
                                if (FAILED(r2) || !cc) { set_status("error: composition controller failed"); return S_OK; }
                                comp_controller_ = cc;
                                setup_webview(pw, ph);
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());
        if (FAILED(hr)) set_status("error: CreateCoreWebView2Environment failed");
    }

    void setup_webview(int pw, int ph)
    {
        comp_controller_.As(&controller_);
        comp_controller_->put_RootVisualTarget(winrt::get_unknown(root_));
        ComPtr<ICoreWebView2Controller3> c3;
        if (SUCCEEDED(controller_.As(&c3))) {
            c3->put_ShouldDetectMonitorScaleChanges(FALSE);
            c3->put_RasterizationScale(config_.raster_scale);
        }
        RECT bounds{ 0, 0, pw, ph };
        controller_->put_Bounds(bounds);
        ComPtr<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(controller_.As(&c2))) {
            COREWEBVIEW2_COLOR clear{ 0, 0, 0, 0 };   /* transparent page background */
            c2->put_DefaultBackgroundColor(clear);
        }
        controller_->put_IsVisible(TRUE);
        controller_->get_CoreWebView2(&webview_);

        ComPtr<ICoreWebView2Settings> s;
        if (SUCCEEDED(webview_->get_Settings(&s))) {
            s->put_AreDefaultContextMenusEnabled(FALSE);
            s->put_IsStatusBarEnabled(FALSE);
            s->put_IsZoomControlEnabled(FALSE);
            s->put_AreDevToolsEnabled(getenv("NFL2K5_HTML_DEVTOOLS") ? TRUE : FALSE);
        }

        std::wstring root = full_path(config_.root_dir);
        ComPtr<ICoreWebView2_3> wv3;
        if (SUCCEEDED(webview_.As(&wv3))) {
            wv3->SetVirtualHostNameToFolderMapping(L"presentation.local", root.c_str(),
                                                   COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
            if (!config_.teams_dir.empty())
                wv3->SetVirtualHostNameToFolderMapping(L"teams.local", full_path(config_.teams_dir).c_str(),
                                                       COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
        /* Packages can tell they are inside the game (vs. the editor). */
        webview_->AddScriptToExecuteOnDocumentCreated(L"window.NFL2K5_HOST = 'game';", nullptr);

        webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    BOOL ok = FALSE;
                    args->get_IsSuccess(&ok);
                    if (ok) {
                        page_ready_ = true;
                        set_status("running");
                        flush();
                    } else {
                        COREWEBVIEW2_WEB_ERROR_STATUS st{};
                        args->get_WebErrorStatus(&st);
                        set_status("error: page failed to load (web error " + std::to_string((int)st) + ")");
                    }
                    return S_OK;
                }).Get(), nullptr);
        /* console.log from the package goes to the game log. */
        webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    LPWSTR text = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
                        fprintf(stderr, "[HTMLPRES] page: %ls\n", text);
                        CoTaskMemFree(text);
                    }
                    return S_OK;
                }).Get(), nullptr);

        std::wstring url = L"https://presentation.local/" +
                           relative_url_path(root, full_path(config_.package_dir)) + L"/" + widen(config_.entry);
        fprintf(stderr, "[HTMLPRES] loading %ls\n", url.c_str());
        set_status("loading");
        webview_->Navigate(url.c_str());
    }

    void flush()
    {
        if (!page_ready_ || !webview_) return;
        std::string state;
        std::deque<std::string> events;
        {
            std::lock_guard<std::mutex> g(msg_lock_);
            state.swap(pending_state_);
            events.swap(pending_events_);
        }
        /* Events after the state they belong to. */
        if (!state.empty()) webview_->PostWebMessageAsJson(widen(state).c_str());
        for (auto &e : events) webview_->PostWebMessageAsJson(widen(e).c_str());
    }

    bool start_capture(int pw, int ph)
    {
        if (!wgc::GraphicsCaptureSession::IsSupported()) return false;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                     D3D11_SDK_VERSION, &d3d_, nullptr, &d3d_ctx_)))
            return false;
        ComPtr<IDXGIDevice> dxgi;
        d3d_.As(&dxgi);
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()))) return false;
        auto device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

        auto item = wgc::GraphicsCaptureItem::CreateFromVisual(root_);
        pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, { pw, ph });
        pool_.FrameArrived([this](wgc::Direct3D11CaptureFramePool const &sender, auto const &) { on_frame(sender); });
        session_ = pool_.CreateCaptureSession(item);
        session_.IsCursorCaptureEnabled(false);
        try { session_.IsBorderRequired(false); } catch (...) { }
        session_.StartCapture();
        return true;
    }

    void on_frame(wgc::Direct3D11CaptureFramePool const &sender)
    {
        auto frame = sender.TryGetNextFrame();
        if (!frame) return;
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(&tex)))) return;
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        if (!staging_ || staging_w_ != (int)d.Width || staging_h_ != (int)d.Height) {
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags = 0;
            sd.MipLevels = 1; sd.ArraySize = 1; sd.SampleDesc.Count = 1;
            staging_.Reset();
            if (FAILED(d3d_->CreateTexture2D(&sd, nullptr, &staging_))) return;
            staging_w_ = (int)d.Width; staging_h_ = (int)d.Height;
        }
        d3d_ctx_->CopyResource(staging_.Get(), tex.Get());
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(d3d_ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
        back_.resize((size_t)staging_w_ * staging_h_ * 4);
        for (int y = 0; y < staging_h_; y++)
            memcpy(&back_[(size_t)y * staging_w_ * 4], (const uint8_t *)m.pData + (size_t)y * m.RowPitch,
                   (size_t)staging_w_ * 4);
        d3d_ctx_->Unmap(staging_.Get(), 0);
        std::lock_guard<std::mutex> g(frame_lock_);
        front_.swap(back_);
        frame_w_ = staging_w_; frame_h_ = staging_h_;
        front_serial_++;
        if (front_serial_ == 1) fprintf(stderr, "[HTMLPRES] first frame %dx%d\n", frame_w_, frame_h_);
    }

    PresentationHostConfig config_;
    std::thread thread_;
    std::atomic<bool> quitting_{ false };
    HWND hwnd_ = nullptr;

    mutable std::mutex status_lock_;
    std::string status_;

    std::mutex msg_lock_;
    std::string pending_state_;
    std::deque<std::string> pending_events_;
    bool page_ready_ = false;

    wuc::Compositor compositor_{ nullptr };
    wuc::ContainerVisual root_{ nullptr };
    ComPtr<ICoreWebView2CompositionController> comp_controller_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;

    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID3D11DeviceContext> d3d_ctx_;
    ComPtr<ID3D11Texture2D> staging_;
    int staging_w_ = 0, staging_h_ = 0;
    wgc::Direct3D11CaptureFramePool pool_{ nullptr };
    wgc::GraphicsCaptureSession session_{ nullptr };

    std::mutex frame_lock_;
    std::vector<uint8_t> back_, front_, consumer_;
    int frame_w_ = 0, frame_h_ = 0;
    uint64_t front_serial_ = 0, consumer_serial_ = 0;
};

} // namespace

std::unique_ptr<PresentationHost> create_webview2_presentation_host()
{
    return std::make_unique<WebView2Host>();
}

} // namespace nfl2k5

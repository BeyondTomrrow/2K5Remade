/*
 * uwp_main.cpp -- UWP entry point for NFL2K5 (CoreApplication + CoreWindow).
 *
 * Stage 1 of the UWP/Xbox port (docs/UWP-PORT.md): a package that starts on
 * Windows and on Xbox Dev Mode, opens a CoreWindow, presents with D3D12 on a
 * CoreWindow swap chain, reads Windows.Gaming.Input gamepads, and runs the
 * run-time probes in uwp_probe.c, drawing the results on screen (and to
 * <LocalState>\uwp-probe.txt). No SDL: SDL3 has no WinRT/UWP backend.
 *
 * The status screen is drawn with ClearRenderTargetView rectangles only, so
 * the shell needs no shaders (and no run-time shader compiler) to show
 * something.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <cstdio>
#include <string>
#include <vector>

#include "uwp_probe.h"
#include "uwp_font5x7.h"
#ifdef NFL2K5_UWP_GAME
#include "uwp_game.h"
extern "C" volatile unsigned char g_uwp_keys[256];        /* uwp_win32_shim.c */
extern "C" LONG g_uwp_window_w, g_uwp_window_h;
extern "C" int uwp_hwnd_test_main(const wchar_t *local_dir);   /* uwp_hwnd_test.cpp */
#endif

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::UI::Core;

namespace {

constexpr UINT kFrames = 2;

struct Renderer {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12Resource> targets[kFrames];
    ComPtr<ID3D12CommandAllocator> alloc[kFrames];
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    UINT64 fence_values[kFrames] = {};
    UINT64 next_fence = 1;
    HANDLE fence_event = nullptr;
    UINT rtv_size = 0;
    UINT width = 0, height = 0;
    std::string adapter_name;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

    void wait_frame(UINT i)
    {
        if (fence->GetCompletedValue() < fence_values[i]) {
            fence->SetEventOnCompletion(fence_values[i], fence_event);
            WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        }
    }

    void wait_idle()
    {
        if (!queue) return;
        UINT64 v = next_fence++;
        queue->Signal(fence.Get(), v);
        fence->SetEventOnCompletion(v, fence_event);
        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
    }

    void make_targets()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrames; i++) {
            check_hresult(swap->GetBuffer(i, IID_PPV_ARGS(&targets[i])));
            device->CreateRenderTargetView(targets[i].Get(), nullptr, h);
            h.ptr += rtv_size;
        }
    }

    bool init(CoreWindow const &window, UINT w, UINT h)
    {
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            adapter->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
            static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                                       D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
            for (D3D_FEATURE_LEVEL fl : levels) {
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), fl, IID_PPV_ARGS(&device)))) {
                    level = fl;
                    char name[128];
                    WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
                    adapter_name = name;
                    break;
                }
            }
            if (device) break;
            adapter.Reset();
        }
        if (!device) return false;

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hresult(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = w;
        sd.Height = h;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kFrames;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Scaling = DXGI_SCALING_STRETCH;
        ComPtr<IDXGISwapChain1> sc1;
        check_hresult(factory->CreateSwapChainForCoreWindow(queue.Get(), static_cast<::IUnknown *>(winrt::get_abi(window)),
                                                            &sd, nullptr, &sc1));
        check_hresult(sc1.As(&swap));
        width = w;
        height = h;

        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = kFrames;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check_hresult(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap)));
        rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        make_targets();
        for (UINT i = 0; i < kFrames; i++)
            check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc[i])));
        check_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0].Get(), nullptr, IID_PPV_ARGS(&list)));
        list->Close();
        check_hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fence_event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        return true;
    }

    void resize(UINT w, UINT h)
    {
        if (!swap || w == 0 || h == 0 || (w == width && h == height)) return;
        wait_idle();
        for (auto &t : targets) t.Reset();
        if (SUCCEEDED(swap->ResizeBuffers(kFrames, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) {
            width = w;
            height = h;
        }
        make_targets();
    }
};

/* Rectangle batches by colour, flushed as ClearRenderTargetView calls. */
struct Canvas {
    struct Batch { float rgba[4]; std::vector<D3D12_RECT> rects; };
    std::vector<Batch> batches;

    Batch &batch(float r, float g, float b)
    {
        for (auto &bt : batches)
            if (bt.rgba[0] == r && bt.rgba[1] == g && bt.rgba[2] == b) return bt;
        batches.push_back({{r, g, b, 1.0f}, {}});
        return batches.back();
    }
    void rect(LONG x, LONG y, LONG w, LONG h, float r, float g, float b)
    {
        batch(r, g, b).rects.push_back({x, y, x + w, y + h});
    }
    /* Text in the 5x7 font; returns the x after the last glyph. */
    LONG text(LONG x, LONG y, LONG scale, const char *s, float r, float g, float b)
    {
        Batch &bt = batch(r, g, b);
        for (; *s; s++, x += 6 * scale) {
            const uint8_t *rows = uwp_glyph(*s);
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if (rows[row] & (0x10 >> col))
                        bt.rects.push_back({x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale});
        }
        return x;
    }
};

std::wstring g_local_dir, g_install_dir;

std::string narrow(winrt::hstring const &s)
{
    std::string out(s.size() * 3 + 1, '\0');
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), (int)out.size(), nullptr, nullptr);
    out.resize(n > 0 ? n : 0);
    return out;
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow window{nullptr};
    Renderer gfx;
    bool closed = false, visible = true, gfx_ok = false;
    float dpi_scale = 1.0f;
    UINT64 frame = 0;
    std::string device_family, mem_limit;

    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const &view)
    {
        view.Activated([](auto &&, IActivatedEventArgs const &) { CoreWindow::GetForCurrentThread().Activate(); });
        CoreApplication::Suspending([this](auto &&, SuspendingEventArgs const &) {
            /* D3D12 apps must trim on suspend (Xbox suspends games in the background). */
            gfx.wait_idle();
        });
    }

    void SetWindow(CoreWindow const &w)
    {
        window = w;
        window.Closed([this](auto &&, auto &&) { closed = true; });
        window.VisibilityChanged([this](auto &&, VisibilityChangedEventArgs const &a) { visible = a.Visible(); });
        window.SizeChanged([this](auto &&, WindowSizeChangedEventArgs const &a) {
            gfx.resize((UINT)(a.Size().Width * dpi_scale), (UINT)(a.Size().Height * dpi_scale));
        });
    }

    void Load(winrt::hstring const &) {}
    void Uninitialize() {}

    void Run()
    {
        g_local_dir = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
        g_install_dir = Package::Current().InstalledLocation().Path().c_str();
        device_family = narrow(winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily());

#ifdef NFL2K5_UWP_GAME
        /* The game owns the window from here (SDL_GPU D3D12 on this
         * CoreWindow); this thread only dispatches window events and turns
         * Windows.Gaming.Input readings into SDL virtual gamepads. */
        {
            float scale = DisplayInformation::GetForCurrentView().LogicalDpi() / 96.0f;
            auto bounds = window.Bounds();
            g_uwp_window_w = (LONG)(bounds.Width * scale);
            g_uwp_window_h = (LONG)(bounds.Height * scale);
            window.SizeChanged([scale](auto &&, WindowSizeChangedEventArgs const &a) {
                g_uwp_window_w = (LONG)(a.Size().Width * scale);
                g_uwp_window_h = (LONG)(a.Size().Height * scale);
            });
            window.KeyDown([](auto &&, KeyEventArgs const &a) { g_uwp_keys[(int)a.VirtualKey() & 0xFF] = 1; });
            window.KeyUp([](auto &&, KeyEventArgs const &a) { g_uwp_keys[(int)a.VirtualKey() & 0xFF] = 0; });
        }
        uwp_game_start(winrt::get_abi(window), g_local_dir.c_str(), g_install_dir.c_str());
        for (int code = 0; !closed && !uwp_game_exited(&code);) {
            window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            uwp_game_tick();
            Sleep(4);
        }
        CoreApplication::Exit();
        return;
#endif

        uwp_probe_run(g_local_dir.c_str(), g_install_dir.c_str(), L"E:\\2K5Remade\\README.md");

        {
            char buf[160];
            uint64_t lim = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
            snprintf(buf, sizeof buf, "%llu MB (%s)", (unsigned long long)(lim >> 20), device_family.c_str());
            mem_limit = buf;
            uwp_probe_note(g_local_dir.c_str(), "app memory limit (info)", 1, buf);
        }

        dpi_scale = DisplayInformation::GetForCurrentView().LogicalDpi() / 96.0f;
        auto b = window.Bounds();
        try {
            gfx_ok = gfx.init(window, (UINT)(b.Width * dpi_scale), (UINT)(b.Height * dpi_scale));
        } catch (...) {
            gfx_ok = false;
        }
        {
            char buf[200];
            snprintf(buf, sizeof buf, "%s, feature level %x_%x, CoreWindow swap chain %ux%u", gfx.adapter_name.c_str(),
                     (gfx.level >> 12) & 0xF, (gfx.level >> 8) & 0xF, gfx.width, gfx.height);
            uwp_probe_note(g_local_dir.c_str(), "D3D12 device", gfx_ok, gfx_ok ? buf : "no D3D12 device / swap chain");
        }

        while (!closed) {
            if (visible) {
                window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                if (gfx_ok) render();
            } else {
                window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            }
        }
        gfx.wait_idle();
    }

    std::string pad_status(bool &a_down)
    {
        a_down = false;
        auto pads = Gamepad::Gamepads();
        if (pads.Size() == 0) return "NO GAMEPAD - CONNECT ONE";
        GamepadReading r = pads.GetAt(0).GetCurrentReading();
        std::string s = "GAMEPAD 1 OF " + std::to_string(pads.Size()) + ":";
        struct { GamepadButtons b; const char *n; } names[] = {
            {GamepadButtons::A, " A"}, {GamepadButtons::B, " B"}, {GamepadButtons::X, " X"}, {GamepadButtons::Y, " Y"},
            {GamepadButtons::LeftShoulder, " LB"}, {GamepadButtons::RightShoulder, " RB"},
            {GamepadButtons::Menu, " START"}, {GamepadButtons::View, " BACK"},
            {GamepadButtons::DPadUp, " UP"}, {GamepadButtons::DPadDown, " DOWN"},
            {GamepadButtons::DPadLeft, " LEFT"}, {GamepadButtons::DPadRight, " RIGHT"}};
        for (auto &n : names)
            if ((r.Buttons & n.b) == n.b) s += n.n;
        a_down = (r.Buttons & GamepadButtons::A) == GamepadButtons::A;
        char sticks[64];
        snprintf(sticks, sizeof sticks, "  LX %+.2f LY %+.2f  LT %.2f RT %.2f", r.LeftThumbstickX, r.LeftThumbstickY,
                 r.LeftTrigger, r.RightTrigger);
        return s + sticks;
    }

    void render()
    {
        Renderer &g = gfx;
        UINT i = g.swap->GetCurrentBackBufferIndex();
        g.wait_frame(i);
        g.alloc[i]->Reset();
        g.list->Reset(g.alloc[i].Get(), nullptr);

        D3D12_RESOURCE_BARRIER bar = {};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource = g.targets[i].Get();
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g.list->ResourceBarrier(1, &bar);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g.rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (SIZE_T)i * g.rtv_size;

        bool a_down = false;
        std::string pad = pad_status(a_down);
        const float bg[4] = {0.05f, 0.07f, 0.12f, 1.0f};
        g.list->ClearRenderTargetView(rtv, bg, 0, nullptr);

        Canvas c;
        LONG W = (LONG)g.width, s = W >= 2400 ? 4 : (W >= 1500 ? 3 : 2);
        LONG x0 = 24 * s, y = 20 * s;
        /* Header band; turns orange while A is held (gamepad input check). */
        c.rect(0, 0, W, 14 * s, a_down ? 0.95f : 0.10f, a_down ? 0.55f : 0.32f, a_down ? 0.10f : 0.62f);
        c.text(x0, 4 * s, s, "ESPN NFL 2K5 - UWP SHELL (STAGE 1)", 1, 1, 1);
        y = 22 * s;
        char line[256];
        snprintf(line, sizeof line, "DEVICE FAMILY %s   MEMORY LIMIT %s", device_family.c_str(), mem_limit.c_str());
        c.text(x0, y, s, line, 0.8f, 0.85f, 0.9f);
        y += 12 * s;
        int pass = 0;
        for (int k = 0; k < g_uwp_probe_count; k++) pass += g_uwp_probe[k].ok;
        snprintf(line, sizeof line, "RUN-TIME PROBES: %d OF %d PASS   (LOG: LOCALSTATE\\UWP-PROBE.TXT)", pass, g_uwp_probe_count);
        c.text(x0, y, s, line, 1, 1, 1);
        y += 14 * s;
        for (int k = 0; k < g_uwp_probe_count; k++) {
            const uwp_probe_result &r = g_uwp_probe[k];
            float cr = r.ok ? 0.20f : 0.85f, cg = r.ok ? 0.75f : 0.20f, cb = r.ok ? 0.35f : 0.20f;
            c.rect(x0, y, 8 * s, 7 * s, cr, cg, cb);
            LONG x = c.text(x0 + 12 * s, y, s, r.name, 1, 1, 1);
            (void)x;
            c.text(x0 + 12 * s + 28 * 6 * s, y, s, r.detail, 0.70f, 0.75f, 0.80f);
            y += 10 * s;
        }
        y += 6 * s;
        c.text(x0, y, s, pad.c_str(), 1.0f, 0.85f, 0.3f);
        /* Moving tick: proves the present loop is live. */
        LONG tick = (LONG)((frame * 4 * s) % (UINT64)W);
        c.rect(tick, (LONG)g.height - 6 * s, 40 * s, 4 * s, 0.3f, 0.6f, 1.0f);

        for (auto &bt : c.batches)
            if (!bt.rects.empty())
                g.list->ClearRenderTargetView(rtv, bt.rgba, (UINT)bt.rects.size(), bt.rects.data());

        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g.list->ResourceBarrier(1, &bar);
        g.list->Close();
        ID3D12CommandList *lists[] = {g.list.Get()};
        g.queue->ExecuteCommandLists(1, lists);
        g.swap->Present(1, 0);
        g.fence_values[i] = g.next_fence++;
        g.queue->Signal(g.fence.Get(), g.fence_values[i]);
        frame++;
    }
};

} // namespace

/* Without package identity (CoreApplication needs one) the exe can still run
 * the probes: "NFL2K5.exe --probe <out dir>". tools/uwp/appcontainer-run
 * starts it that way inside an AppContainer, which tests the same sandbox
 * rules on a PC that cannot install the package (no Developer Mode). */
static int run_probe_only(const wchar_t *cmd)
{
    const wchar_t *arg = wcsstr(cmd, L"--probe");
    if (!arg) return -1;
    arg += 7;
    while (*arg == L' ' || *arg == L'"') arg++;
    std::wstring out(arg);
    while (!out.empty() && (out.back() == L'"' || out.back() == L' ')) out.pop_back();
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\'));
    uwp_probe_run(out.c_str(), dir.c_str(), L"E:\\2K5Remade\\README.md");
    int pass = 0;
    for (int k = 0; k < g_uwp_probe_count; k++) pass += g_uwp_probe[k].ok;
    return g_uwp_probe_count - pass;   /* exit code = failures */
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmd, int)
{
    int probe = run_probe_only(cmd ? cmd : L"");
    if (probe >= 0) return probe;
#ifdef NFL2K5_UWP_GAME
    if (cmd && wcsstr(cmd, L"--hwnd-test")) {
        const wchar_t *arg = wcsstr(cmd, L"--hwnd-test") + 11;
        while (*arg == L' ' || *arg == L'"') arg++;
        std::wstring dir(arg);
        while (!dir.empty() && (dir.back() == L'"' || dir.back() == L' ')) dir.pop_back();
        return uwp_hwnd_test_main(dir.c_str());
    }
#endif
    winrt::init_apartment();
    CoreApplication::Run(make<App>());
    return 0;
}

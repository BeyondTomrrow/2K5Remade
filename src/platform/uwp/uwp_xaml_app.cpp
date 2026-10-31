/*
 * uwp_xaml_app.cpp -- XAML host mode of the UWP game.
 *
 * The HTML scorebug packages need a browser engine; on Xbox the only one a
 * UWP app gets is the XAML WebView (EdgeHTML). A WebView lives in a XAML
 * tree, and a XAML app cannot also put a swap chain on its CoreWindow, so in
 * this mode the game draws into a SwapChainPanel (a composition swap chain;
 * local SDL patch, "SDL.window.uwp.setswapchain") and WebViews sit behind it
 * in the same Grid, never seen, captured by uwp_webview_host.cpp.
 *
 * Plain CoreWindow mode (uwp_main.cpp) stays as the fallback: it is used when
 * LocalState\xaml-mode.txt starts with "off", and automatically after a XAML
 * start that did not survive its first 45 seconds (the marker file
 * xaml-starting.marker is still there on the next launch).
 *
 * Built without .xaml files or the XAML compiler: the tree is made in code
 * from built-in controls only.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_4.h>
#include <windows.ui.xaml.media.dxinterop.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "uwp_game.h"
#include "uwp_xaml.h"

extern "C" volatile unsigned char g_uwp_keys[256];   /* uwp_win32_shim.c */
extern "C" LONG g_uwp_window_w, g_uwp_window_h;

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;

namespace {

std::atomic<bool> s_active{false};
std::atomic<bool> s_swap_ok{false};   /* the game's swap chain reached the panel */
CoreDispatcher s_dispatcher{nullptr};
Grid s_root{nullptr};
SwapChainPanel s_panel{nullptr};
std::wstring s_local;

std::wstring marker_path() { return s_local + L"\\xaml-starting.marker"; }

/* SDL (game thread) made the composition swap chain: hand it to the panel on
 * the UI thread and wait, so the first present goes to a visible panel. */
void SDLCALL set_swapchain(void *swapchain)
{
    auto apply = [swapchain]() {
        com_ptr<ISwapChainPanelNative> native = s_panel.as<ISwapChainPanelNative>();
        HRESULT hr = native->SetSwapChain(static_cast<IDXGISwapChain *>(swapchain));
        fprintf(stderr, "[UWP] SwapChainPanel.SetSwapChain 0x%08lX\n", (unsigned long)hr);
        if (SUCCEEDED(hr) && swapchain) s_swap_ok = true;   /* the 45 s check reads it */
    };
    if (s_dispatcher.HasThreadAccess()) {
        apply();
        return;
    }
    HANDLE done = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    s_dispatcher.RunAsync(CoreDispatcherPriority::High, [&]() {
        try { apply(); } catch (...) { fprintf(stderr, "[UWP] SetSwapChain failed\n"); }
        SetEvent(done);
    });
    WaitForSingleObjectEx(done, 5000, FALSE);
    CloseHandle(done);
}

void update_size()
{
    if (!s_panel) return;
    float sx = s_panel.CompositionScaleX(), sy = s_panel.CompositionScaleY();
    double w = s_panel.ActualWidth(), h = s_panel.ActualHeight();
    if (w > 0 && h > 0) {
        g_uwp_window_w = (LONG)(w * (sx > 0 ? sx : 1));
        g_uwp_window_h = (LONG)(h * (sy > 0 ? sy : 1));
        /* SDL's present stretches the swap chain over the panel (in view
         * pixels): a SwapChainPanel shows it unscaled otherwise. */
        SDL_SetFloatProperty(SDL_GetGlobalProperties(), "SDL.uwp.panel.width", (float)w);
        SDL_SetFloatProperty(SDL_GetGlobalProperties(), "SDL.uwp.panel.height", (float)h);
    }
}

struct XamlApp : ApplicationT<XamlApp> {
    DispatcherTimer survive{nullptr};

    void OnLaunched(LaunchActivatedEventArgs const &)
    {
        /* Xbox: 1 view pixel = 1 screen pixel (otherwise 1080p is laid out as
         * 960x540), and use the whole TV picture. */
        try {
            winrt::Windows::UI::ViewManagement::ApplicationViewScaling::TrySetDisableLayoutScaling(true);
            winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView().SetDesiredBoundsMode(
                winrt::Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
        } catch (...) {
        }
        Window win = Window::Current();
        s_root = Grid();
        s_root.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Black()));
        s_panel = SwapChainPanel();
        s_root.Children().Append(s_panel);
        win.Content(s_root);
        win.Activate();
        s_dispatcher = win.Dispatcher();

        CoreWindow cw = win.CoreWindow();
        cw.KeyDown([](auto &&, KeyEventArgs const &a) { g_uwp_keys[(int)a.VirtualKey() & 0xFF] = 1; });
        cw.KeyUp([](auto &&, KeyEventArgs const &a) { g_uwp_keys[(int)a.VirtualKey() & 0xFF] = 0; });
        s_panel.SizeChanged([](auto &&, auto &&) { update_size(); });
        s_panel.CompositionScaleChanged([](auto &&, auto &&) { update_size(); });
        update_size();

        /* Gamepads -> SDL every frame; leave when the game has. */
        Media::CompositionTarget::Rendering([](auto &&, auto &&) {
            uwp_game_tick();
            int code = 0;
            if (uwp_game_exited(&code)) Application::Current().Exit();
        });

        /* 45 s in: if the game's picture reached the panel, the XAML start
         * works here and the marker goes. If not (a black screen, nothing
         * crashed), switch this install to CoreWindow mode and close, so the
         * next start shows the game. */
        survive = DispatcherTimer();
        survive.Interval(std::chrono::seconds(45));
        survive.Tick([this](auto &&, auto &&) {
            survive.Stop();
            DeleteFileW(marker_path().c_str());
            if (s_swap_ok) {
                fprintf(stderr, "[UWP] XAML host mode running\n");
                return;
            }
            fprintf(stderr, "[UWP] no swap chain on the SwapChainPanel after 45 s: CoreWindow mode from the next start\n");
            if (FILE *f = _wfopen((s_local + L"\\xaml-mode.txt").c_str(), L"wb")) {
                fputs("off (automatic: the game's picture never reached the XAML panel; delete this file to try again)\r\n", f);
                fclose(f);
            }
            Application::Current().Exit();
        });
        survive.Start();

        s_active = true;
        std::wstring install = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str();
        SDL_SetPointerProperty(SDL_GetGlobalProperties(), "SDL.window.uwp.setswapchain", (void *)set_swapchain);
        uwp_game_start(nullptr, s_local.c_str(), install.c_str());
    }
};

} // namespace

CoreDispatcher uwp_xaml_dispatcher() { return s_dispatcher; }
Grid uwp_xaml_root() { return s_root; }
bool uwp_xaml_active(void) { return s_active; }

extern "C" int uwp_xaml_wanted(const wchar_t *local_dir)
{
    std::wstring local(local_dir);
    std::wstring mode = local + L"\\xaml-mode.txt";
    char buf[64] = {};
    if (FILE *f = _wfopen(mode.c_str(), L"rb")) {
        fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        if (_strnicmp(buf, "off", 3) == 0) return 0;
    }
    std::wstring marker = local + L"\\xaml-starting.marker";
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        /* The last XAML start died early: CoreWindow mode from now on, until
         * the player deletes xaml-mode.txt. */
        DeleteFileW(marker.c_str());
        if (FILE *f = _wfopen(mode.c_str(), L"wb")) {
            fputs("off (automatic: the XAML host did not survive its first 45 seconds; delete this file to try again)\r\n", f);
            fclose(f);
        }
        return 0;
    }
    return 1;
}

int uwp_xaml_run(void)
{
    s_local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    if (FILE *f = _wfopen(marker_path().c_str(), L"wb")) fclose(f);
    Application::Start([](auto &&) { make<XamlApp>(); });
    return 0;
}

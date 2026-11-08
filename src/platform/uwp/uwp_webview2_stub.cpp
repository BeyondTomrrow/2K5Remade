/* uwp_webview2_stub.cpp -- PresentationHost factory for the UWP build.
 *
 * In XAML host mode the packages run in the XAML WebView
 * (uwp_webview_host.cpp); in plain CoreWindow mode there is no browser
 * engine and this stub is used.
 *
 * WebView2 does not exist on Xbox (and its loader is desktop-only), so the
 * HTML scorebug packages are unavailable there and the game falls back to its
 * native HUD, exactly as on Linux. A later stage can host the packages in a
 * XAML WebView (Windows.UI.Xaml.Controls.WebView, EdgeHTML on Xbox) with
 * CapturePreviewToStreamAsync for frames; see docs/UWP-PORT.md. Also supplies
 * the gcc builtins xboxrecomp's APU code expects (MSVC has none). */
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include "presentation/presentation_host.h"
#include "uwp_xaml.h"

namespace nfl2k5 {
namespace {
class UwpStubPresentationHost final : public PresentationHost {
public:
    bool start(const PresentationHostConfig &) override {
        fprintf(stderr, "[HTMLPRES] no WebView2 on UWP/Xbox; using the native HUD\n");
        return false;
    }
    void stop() override {}
    void post_state(const std::string &) override {}
    void post_event(const std::string &) override {}
    bool acquire_surface(PresentationSurface &) override { return false; }
    std::string status() const override { return "unavailable (UWP)"; }
};
} // namespace

std::unique_ptr<PresentationHost> create_webview2_presentation_host()
{
#ifdef NFL2K5_UWP_GAME
    /* XAML host mode: the EdgeHTML WebView host (uwp_webview_host.cpp). */
    if (auto h = uwp_create_webview_presentation_host()) return h;
#endif
    return std::make_unique<UwpStubPresentationHost>();
}
} // namespace nfl2k5

extern "C" void __sync_synchronize(void) { MemoryBarrier(); }
extern "C" int __builtin_popcount(unsigned int x) { return (int)__popcnt(x); }

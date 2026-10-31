/*
 * uwp_webview_host.cpp -- PresentationHost on the XAML WebView (EdgeHTML on
 * Xbox), for the UWP build's XAML host mode (uwp_xaml_app.cpp).
 *
 * Same contract as webview2_host.cpp (Windows, WebView2) and the Android
 * PresentationView: the page runs off screen at its canvas size and its
 * transparent frames come back as a BGRA premultiplied CPU surface with the
 * changed rows marked, for the game's HUD layer.
 *
 *   - files: every request for https://presentation.local/, teams.local,
 *     erateams.local and portraits.local is answered from the game folder in
 *     WebResourceRequested (no network); HTML gets the shim below injected
 *     after <head>, as on Android.
 *   - shim: window.chrome.webview (addEventListener/postMessage), so packages
 *     run unchanged; window.__nfl2k5_recv delivers the game's messages;
 *     window.__nfl2k5_busy says whether anything changed (MutationObserver,
 *     running animations) so frames are captured only then (plus once a
 *     second); page -> game messages go through window.external.notify.
 *   - frames: CapturePreviewToStreamAsync -> PNG -> BitmapDecoder (BGRA8,
 *     premultiplied), at most ~30 a second and only while the page changes.
 *   - fallback: a page that fails to load, throws before its first frame, or
 *     whose capture has no transparency (the engine cannot do it) reports
 *     status "error: ...", and nfl2k5_presentation.cpp goes back to the native
 *     scorebug for the session (logged).
 *
 * Everything WebView runs on the XAML UI thread; the game calls in from its
 * own thread, so state shared with UI callbacks lives in a shared_ptr.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.UI.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "uwp_xaml.h"
#include "uwp_css_compat.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wwh = winrt::Windows::Web::Http;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wss = winrt::Windows::Storage::Streams;

winrt::Windows::UI::Core::CoreDispatcher uwp_xaml_dispatcher();   /* uwp_xaml_app.cpp */
wuxc::Grid uwp_xaml_root();

namespace {

/* The same shim the Android host injects (PresentationView.java), with the
 * page -> game channel on window.external.notify, plus an error report so a
 * package that needs something EdgeHTML lacks falls back cleanly. */
const char *k_shim =
    "<script>(function(){var L=[];window.NFL2K5_HOST='game';"
    "function N(s){try{window.external.notify(String(s));}catch(e){}}"
    "window.chrome=window.chrome||{};window.chrome.webview={"
    "addEventListener:function(t,f){if(t==='message')L.push(f);},"
    "removeEventListener:function(t,f){var i=L.indexOf(f);if(i>=0)L.splice(i,1);},"
    "postMessage:function(s){N(s);}};"
    "window.__nfl2k5_recv=function(d){for(var i=0;i<L.length;i++){try{L[i]({data:d});}catch(e){}}};"
    "window.addEventListener('error',function(e){N('__nfl2k5_error '+(e&&e.message?e.message:'script error')+"
    "(e&&e.filename?' ('+e.filename+':'+e.lineno+')':''));});"
    /* EdgeHTML 18 has no document.getAnimations(): there, CSS animations are
     * counted from their start/end events, and a change keeps the page
     * "busy" for 1.5 s so the CSS transitions it starts are captured too. */
    "var D=true,T=0,A=0;function M(){D=true;T=Date.now();}"
    "try{new MutationObserver(M).observe(document,{subtree:true,childList:true,"
    "attributes:true,characterData:true});}catch(e){}"
    "window.addEventListener('animationstart',function(){A++;},true);"
    "window.addEventListener('animationend',function(){if(A>0)A--;M();},true);"
    "window.addEventListener('animationcancel',function(){if(A>0)A--;},true);"
    "window.addEventListener('transitionend',M,true);"
    "window.__nfl2k5_busy=function(){var b=D;D=false;if(typeof document.getAnimations==='function'){"
    "try{var a=document.getAnimations();for(var i=0;i<a.length;i++)if(a[i].playState==='running'){b=true;break;}}"
    "catch(e){b=true;}}else if(A>0||Date.now()-T<1500)b=true;return b;};})();</script>";

std::wstring widen(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring full_path(const std::string &p)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(widen(p).c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
    return n ? std::wstring(buf, n) : widen(p);
}

std::wstring url_decode(const std::wstring &s)
{
    std::string bytes;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'%' && i + 2 < s.size()) {
            bytes += (char)wcstol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            bytes += narrow(std::wstring(1, s[i]));
        }
    }
    return widen(bytes);
}

const wchar_t *mime(const std::wstring &path)
{
    std::wstring p = path;
    for (auto &c : p) c = (wchar_t)towlower(c);
    auto ends = [&](const wchar_t *e) { size_t n = wcslen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
    if (ends(L".html") || ends(L".htm")) return L"text/html";
    if (ends(L".js")) return L"text/javascript";
    if (ends(L".css")) return L"text/css";
    if (ends(L".json")) return L"application/json";
    if (ends(L".png")) return L"image/png";
    if (ends(L".jpg") || ends(L".jpeg")) return L"image/jpeg";
    if (ends(L".webp")) return L"image/webp";
    if (ends(L".gif")) return L"image/gif";
    if (ends(L".svg")) return L"image/svg+xml";
    if (ends(L".ttf")) return L"font/ttf";
    if (ends(L".otf")) return L"font/otf";
    if (ends(L".woff")) return L"font/woff";
    if (ends(L".woff2")) return L"font/woff2";
    if (ends(L".mp3")) return L"audio/mpeg";
    if (ends(L".mp4")) return L"video/mp4";
    if (ends(L".webm")) return L"video/webm";
    return L"application/octet-stream";
}

/* State shared between the game thread (host object) and UI callbacks. */
struct Shared {
    nfl2k5::PresentationHostConfig config;
    std::map<std::wstring, std::wstring> hosts;   /* lower-case host -> folder */
    std::wstring entry_url;
    int css_w = 1920, css_h = 1080, out_w = 1920, out_h = 1080;
    float scale = 1.0f;

    std::atomic<bool> stopped{false};
    bool ready = false, asking = false, capturing = false, force = true;
    ULONGLONG last_draw = 0, last_full = 0;
    int frames_seen = 0;
    bool saw_alpha = false;
    wuxc::WebView web{nullptr};
    wux::DispatcherTimer timer{nullptr};

    mutable std::mutex status_lock;
    std::string status;

    std::mutex msg_lock;
    std::string pending_state;
    std::deque<std::string> pending_events;

    std::mutex frame_lock;
    std::vector<uint8_t> front, consumer, pend_rows, out_rows;
    int frame_w = 0, frame_h = 0;
    uint64_t front_serial = 0, consumer_serial = 0;

    void set_status(const std::string &s)
    {
        {
            std::lock_guard<std::mutex> g(status_lock);
            status = s;
        }
        fprintf(stderr, "[HTMLPRES] %s\n", s.c_str());
    }

    bool failed() const
    {
        std::lock_guard<std::mutex> g(status_lock);
        return status.compare(0, 6, "error:") == 0;
    }
};

using SharedPtr = std::shared_ptr<Shared>;

std::string take_js(Shared &s)
{
    std::string js, state;
    std::deque<std::string> events;
    {
        std::lock_guard<std::mutex> g(s.msg_lock);
        state.swap(s.pending_state);
        events.swap(s.pending_events);
    }
    if (!state.empty()) js += "window.__nfl2k5_recv({\"type\":\"state\",\"state\":" + state + "});";
    for (auto &e : events) js += "window.__nfl2k5_recv({\"type\":\"event\",\"event\":" + e + "});";
    return js;
}

void flush(const SharedPtr &s)
{
    if (!s->ready || !s->web || s->stopped) return;
    std::string js = take_js(*s);
    if (js.empty()) return;
    s->force = true;
    try {
        s->web.InvokeScriptAsync(L"eval", single_threaded_vector<hstring>({hstring(widen(js))}));
    } catch (...) {
    }
}

/* BGRA premultiplied frame in; only changed rows are copied and marked. */
void take_frame(Shared &s, const uint8_t *px, int w, int h)
{
    const size_t row = (size_t)w * 4;
    if (!s.saw_alpha) {
        for (size_t i = 3; i < row * h; i += 4 * 7)
            if (px[i] != 255) { s.saw_alpha = true; break; }
    }
    if (++s.frames_seen >= 3 && !s.saw_alpha) {
        s.set_status("error: the WebView capture is opaque (no transparency on this system)");
        return;
    }
    std::lock_guard<std::mutex> g(s.frame_lock);
    bool whole = s.frame_w != w || s.frame_h != h || s.front.size() != row * h;
    if (whole) {
        s.front.assign(row * h, 0);
        s.pend_rows.assign(h, 1);
        s.frame_w = w;
        s.frame_h = h;
    }
    bool any = whole;
    for (int y = 0; y < h; y++) {
        uint8_t *f = &s.front[(size_t)y * row];
        const uint8_t *src = px + (size_t)y * row;
        if (whole || memcmp(f, src, row)) {
            memcpy(f, src, row);
            s.pend_rows[y] = 1;
            any = true;
        }
    }
    if (!any) return;
    s.front_serial++;
    if (s.front_serial == 1) fprintf(stderr, "[HTMLPRES] first frame %dx%d\n", w, h);
}

fire_and_forget capture(SharedPtr s)
{
    if (!s->web || s->stopped || s->capturing) co_return;
    s->capturing = true;
    try {
        wss::InMemoryRandomAccessStream stream;
        co_await s->web.CapturePreviewToStreamAsync(stream);
        stream.Seek(0);
        wgi::BitmapDecoder dec = co_await wgi::BitmapDecoder::CreateAsync(stream);
        wgi::BitmapTransform xf;
        if ((int)dec.PixelWidth() != s->out_w || (int)dec.PixelHeight() != s->out_h) {
            xf.ScaledWidth((uint32_t)s->out_w);
            xf.ScaledHeight((uint32_t)s->out_h);
            xf.InterpolationMode(wgi::BitmapInterpolationMode::Fant);
        }
        wgi::PixelDataProvider data = co_await dec.GetPixelDataAsync(
            wgi::BitmapPixelFormat::Bgra8, wgi::BitmapAlphaMode::Premultiplied, xf,
            wgi::ExifOrientationMode::IgnoreExifOrientation, wgi::ColorManagementMode::DoNotColorManage);
        com_array<uint8_t> px = data.DetachPixelData();
        int w = xf.ScaledWidth() ? (int)xf.ScaledWidth() : (int)dec.PixelWidth();
        int h = xf.ScaledHeight() ? (int)xf.ScaledHeight() : (int)dec.PixelHeight();
        if (!s->stopped && px.size() >= (size_t)w * h * 4) take_frame(*s, px.data(), w, h);
    } catch (hresult_error const &e) {
        static int warned;
        if (warned++ < 3) fprintf(stderr, "[HTMLPRES] capture failed: %ls\n", e.message().c_str());
    } catch (...) {
    }
    s->capturing = false;
}

fire_and_forget ask_and_capture(SharedPtr s)
{
    if (!s->web || s->stopped) co_return;
    s->asking = true;
    hstring v = L"true";
    try {
        v = co_await s->web.InvokeScriptAsync(L"eval", single_threaded_vector<hstring>(
            {hstring(L"String(window.__nfl2k5_busy?__nfl2k5_busy():true)")}));
    } catch (...) {
    }
    s->asking = false;
    if (v != L"false") capture(s);
}

void tick(const SharedPtr &s)
{
    if (!s->ready || s->asking || s->capturing || s->stopped || s->failed()) return;
    ULONGLONG now = GetTickCount64();
    if (now - s->last_draw < 33) return;   /* ~30 frames a second at most */
    s->last_draw = now;
    if (s->force || now - s->last_full >= 1000) {
        s->force = false;
        s->last_full = now;
        capture(s);
    } else {
        ask_and_capture(s);
    }
}

/* Serves https://presentation.local/... etc. from the game folder. */
void serve(const SharedPtr &s, wuxc::WebViewWebResourceRequestedEventArgs const &args)
{
    Uri uri = args.Request().RequestUri();
    std::wstring host = uri.Host().c_str();
    for (auto &c : host) c = (wchar_t)towlower(c);
    auto it = s->hosts.find(host);
    if (it == s->hosts.end()) return;   /* not ours: the engine handles it */
    std::wstring path = url_decode(uri.Path().c_str());
    wwh::HttpResponseMessage resp(wwh::HttpStatusCode::NotFound);
    if (path.find(L"..") == std::wstring::npos) {
        std::wstring file = it->second + path;
        for (auto &c : file) if (c == L'/') c = L'\\';
        DWORD attr = GetFileAttributesW(file.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) file += L"\\index.html";
        std::ifstream in(file, std::ios::binary);
        if (in) {
            std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const wchar_t *type = mime(file);
            if (wcscmp(type, L"text/html") == 0 || wcscmp(type, L"text/css") == 0) uwp_css::expand_inset(body);
            if (wcscmp(type, L"text/html") == 0) {
                std::string lower = body;
                for (auto &c : lower) c = (char)tolower((unsigned char)c);
                size_t at = lower.find("<head");
                at = at == std::string::npos ? 0 : body.find('>', at) + 1;
                char zoom[160];
                snprintf(zoom, sizeof zoom, "<style>html{zoom:%.4f;overflow:hidden;background:transparent}</style>", s->scale);
                body.insert(at, std::string(k_shim) + zoom);
            }
            auto buf = winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
                array_view<const uint8_t>((const uint8_t *)body.data(), (const uint8_t *)body.data() + body.size()));
            wwh::HttpBufferContent content(buf);
            content.Headers().ContentType(wwh::Headers::HttpMediaTypeHeaderValue(hstring(type)));
            resp = wwh::HttpResponseMessage(wwh::HttpStatusCode::Ok);
            resp.Content(content);
        }
    }
    resp.Headers().Insert(L"Access-Control-Allow-Origin", L"*");
    resp.Headers().Insert(L"Cache-Control", L"no-cache");
    args.Response(resp);
}

void create_on_ui(const SharedPtr &s)
{
    wuxc::Grid root = uwp_xaml_root();
    if (!root) { s->set_status("error: no XAML host"); return; }
    double ppv = 1.0;
    try {
        ppv = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
    } catch (...) {
    }
    if (ppv <= 0) ppv = 1.0;
    wuxc::WebView web(wuxc::WebViewExecutionMode::SameThread);
    web.Width(s->out_w / ppv);
    web.Height(s->out_h / ppv);
    web.HorizontalAlignment(wux::HorizontalAlignment::Left);
    web.VerticalAlignment(wux::VerticalAlignment::Top);
    web.IsHitTestVisible(false);
    web.DefaultBackgroundColor(winrt::Windows::UI::Colors::Transparent());
    web.Settings().IsJavaScriptEnabled(true);
    std::weak_ptr<Shared> weak = s;
    web.WebResourceRequested([weak](auto &&, wuxc::WebViewWebResourceRequestedEventArgs const &a) {
        if (auto sp = weak.lock()) {
            try { serve(sp, a); } catch (...) { }
        }
    });
    web.ScriptNotify([weak](auto &&, wux::Controls::NotifyEventArgs const &a) {
        auto sp = weak.lock();
        if (!sp) return;
        std::string text = narrow(a.Value().c_str());
        if (text.compare(0, 15, "__nfl2k5_error ") == 0) {
            if (!sp->front_serial) sp->set_status("error: page script error before its first frame: " + text.substr(15));
            else fprintf(stderr, "[HTMLPRES] page script error: %s\n", text.substr(15).c_str());
            return;
        }
        fprintf(stderr, "[HTMLPRES] page: %s\n", text.c_str());
        if (sp->config.on_page_message && text.size() <= 256) sp->config.on_page_message(text);
    });
    web.NavigationCompleted([weak](auto &&, wuxc::WebViewNavigationCompletedEventArgs const &a) {
        auto sp = weak.lock();
        if (!sp || sp->stopped) return;
        if (!a.IsSuccess()) {
            sp->set_status("error: page failed to load (web error " + std::to_string((int)a.WebErrorStatus()) + ")");
            return;
        }
        sp->ready = true;
        sp->set_status("running");
        flush(sp);
    });
    /* Behind the game's SwapChainPanel: attached and laid out (so it runs
     * its animations) but never seen. */
    root.Children().InsertAt(0, web);
    s->web = web;
    s->timer = wux::DispatcherTimer();
    s->timer.Interval(std::chrono::milliseconds(16));
    s->timer.Tick([weak](auto &&, auto &&) {
        if (auto sp = weak.lock()) tick(sp);
    });
    s->timer.Start();
    s->set_status("loading");
    fprintf(stderr, "[HTMLPRES] loading %ls (%dx%d at %dx%d, EdgeHTML WebView)\n", s->entry_url.c_str(), s->css_w, s->css_h,
            s->out_w, s->out_h);
    web.Navigate(Uri(hstring(s->entry_url)));
}

void destroy_on_ui(const SharedPtr &s)
{
    if (s->timer) { s->timer.Stop(); s->timer = nullptr; }
    if (s->web) {
        try {
            wuxc::Grid root = uwp_xaml_root();
            uint32_t index = 0;
            if (root && root.Children().IndexOf(s->web, index)) root.Children().RemoveAt(index);
            s->web.NavigateToString(L"");
        } catch (...) {
        }
        s->web = nullptr;
    }
}

class UwpWebViewHost final : public nfl2k5::PresentationHost {
public:
    UwpWebViewHost() : s_(std::make_shared<Shared>()) {}
    ~UwpWebViewHost() override { stop(); }

    bool start(const nfl2k5::PresentationHostConfig &config) override
    {
        auto d = uwp_xaml_dispatcher();
        if (!d) { s_->set_status("error: no XAML dispatcher"); return false; }
        Shared &s = *s_;
        s.config = config;
        s.css_w = config.canvas_width > 0 ? config.canvas_width : 1920;
        s.css_h = config.canvas_height > 0 ? config.canvas_height : 1080;
        s.scale = config.raster_scale > 0.05f ? config.raster_scale : 1.0f;
        s.out_w = std::max(16, (int)(s.css_w * s.scale + 0.5f));
        s.out_h = std::max(16, (int)(s.css_h * s.scale + 0.5f));
        std::wstring root = full_path(config.root_dir);
        s.hosts[L"presentation.local"] = root;
        if (!config.teams_dir.empty()) s.hosts[L"teams.local"] = full_path(config.teams_dir);
        if (!config.eras_teams_dir.empty()) s.hosts[L"erateams.local"] = full_path(config.eras_teams_dir);
        if (!config.portraits_dir.empty()) s.hosts[L"portraits.local"] = full_path(config.portraits_dir);
        std::wstring pkg = full_path(config.package_dir), rel = pkg;
        if (pkg.size() > root.size() && _wcsnicmp(pkg.c_str(), root.c_str(), root.size()) == 0)
            rel = pkg.substr(root.size() + 1);
        for (auto &c : rel) if (c == L'\\') c = L'/';
        s.entry_url = L"https://presentation.local/" + rel + L"/" + widen(config.entry);
        s.set_status("starting");
        SharedPtr sp = s_;
        d.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [sp]() {
            try { create_on_ui(sp); }
            catch (hresult_error const &e) { sp->set_status("error: WebView unavailable: " + narrow(e.message().c_str())); }
            catch (...) { sp->set_status("error: WebView unavailable"); }
        });
        return true;
    }

    void stop() override
    {
        if (s_->stopped.exchange(true)) return;
        auto d = uwp_xaml_dispatcher();
        if (!d) return;
        SharedPtr sp = s_;
        d.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [sp]() { destroy_on_ui(sp); });
    }

    void post_state(const std::string &json) override
    {
        {
            std::lock_guard<std::mutex> g(s_->msg_lock);
            s_->pending_state = json;
        }
        kick();
    }

    void post_event(const std::string &json) override
    {
        {
            std::lock_guard<std::mutex> g(s_->msg_lock);
            s_->pending_events.push_back(json);
        }
        kick();
    }

    bool acquire_surface(nfl2k5::PresentationSurface &out) override
    {
        Shared &s = *s_;
        std::lock_guard<std::mutex> g(s.frame_lock);
        if (!s.front_serial || s.failed()) return false;
        out.dirty_y0 = out.dirty_y1 = 0;
        out.dirty_rows = nullptr;
        if (s.consumer_serial != s.front_serial) {
            size_t row = (size_t)s.frame_w * 4;
            if (s.consumer.size() != s.front.size()) {
                s.consumer = s.front;
                out.dirty_y0 = 0;
                out.dirty_y1 = s.frame_h;
            } else {
                s.out_rows.assign(s.frame_h, 0);
                int lo = INT_MAX, hi = 0;
                for (int y = 0; y < s.frame_h && y < (int)s.pend_rows.size(); y++)
                    if (s.pend_rows[y]) {
                        memcpy(&s.consumer[(size_t)y * row], &s.front[(size_t)y * row], row);
                        s.out_rows[y] = 1;
                        lo = std::min(lo, y);
                        hi = y + 1;
                    }
                if (hi > lo) { out.dirty_y0 = lo; out.dirty_y1 = hi; out.dirty_rows = s.out_rows.data(); }
            }
            std::fill(s.pend_rows.begin(), s.pend_rows.end(), (uint8_t)0);
            s.consumer_serial = s.front_serial;
        }
        out.pixels = s.consumer.data();
        out.width = s.frame_w;
        out.height = s.frame_h;
        out.stride = s.frame_w * 4;
        out.serial = s.consumer_serial;
        return true;
    }

    std::string status() const override
    {
        std::lock_guard<std::mutex> g(s_->status_lock);
        return s_->status;
    }

private:
    void kick()
    {
        auto d = uwp_xaml_dispatcher();
        if (!d || s_->stopped) return;
        SharedPtr sp = s_;
        d.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [sp]() { flush(sp); });
    }

    SharedPtr s_;
};

} // namespace

std::unique_ptr<nfl2k5::PresentationHost> uwp_create_webview_presentation_host(void)
{
    if (!uwp_xaml_active()) return nullptr;
    return std::make_unique<UwpWebViewHost>();
}

// Controller pictures and button icons (2026-10-09).
//
// Through the renderer's texture overrides (g_xbox_tex_override in
// external/xboxrecomp/src/kernel/nv2a_gpu_d3d11.inc.c) this replaces:
//
//   - the Duke controller picture on Options > Controller Setup (DXT1 256x256)
//     with the controller on that port -- Xbox Series, DualShock 4 or DualSense
//     (assets/controls/pad-*.png) -- and lights the button being remapped
//     (pad-*-hl-*.png, positions in pad-*.json; nfl2k5_controller_setup.c);
//   - the game's button icon sheet (paletted 128x128, every button prompt in
//     menus and the HUD) with Xbox Series, PlayStation or keyboard icons
//     (Kenney "Input Prompts", CC0: assets/controls/sheet-*.png, keys/);
//   - the small header sheet (paletted 64x64: the LT/RT paging arrows) the same way.
//
// Images are 4x the game's, so they stay sharp at high internal resolutions.
// "Original Xbox" prompts (F1 > Controls) keep the game's own icons.
// tools/controller-art builds the assets.
#include <windows.h>
#include <wincodec.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

extern "C" {
extern int (*g_xbox_tex_override_wants)(uint32_t fmt, uint32_t w, uint32_t h);
extern uint32_t (*g_xbox_tex_override_gen)(void);
extern const uint32_t *(*g_xbox_tex_override)(uint32_t fmt, uint32_t w, uint32_t h, uint32_t content,
                                              const uint32_t *orig, uint32_t *out_w, uint32_t *out_h);
int nfl2k5_controls_style(void);
int nfl2k5_controls_ps_kind(void);
int nfl2k5_controls_port_dev(int port);
int nfl2k5_controls_key(int game_button);
int nfl2k5_cs_highlight(void);
int nfl2k5_cs_screen_port(void);
uint32_t nfl2k5_cs_gen(void);
}

namespace {

const uint32_t kDukeHash = 0xCBDFAE7Fu;     // tex content hash of the Duke picture (DXT1 256x256)
const uint32_t kSheetHash = 0xBD8DD7ADu;    // decoded BGRA hash of the 128x128 button sheet
uint32_t g_small_hash = 0;                  // the 64x64 header sheet; found by its layout (below)

struct Img {
    int w = 0, h = 0;
    std::vector<uint32_t> px;               // BGRA, straight alpha
    bool ok() const { return w > 0; }
};

CRITICAL_SECTION g_lock;
std::string g_dir;
std::map<std::string, Img> g_files;
std::map<std::string, Img> g_made;          // composed results, by key
bool g_log, g_have_art;

uint32_t fnv(const uint32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

std::string find_dir()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string d = exe;
    d = d.substr(0, d.find_last_of("\\/") + 1);
    const char *tries[] = { "assets\\controls\\", "..\\..\\assets\\controls\\", "..\\assets\\controls\\" };
    for (const char *t : tries) {
        std::string p = d + t;
        if (GetFileAttributesA((p + "sheet-xbox.png").c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return "assets\\controls\\";
}

Img load_png(const std::string &path)
{
    Img im;
    IWICImagingFactory *wic = nullptr;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *fr = nullptr;
    IWICFormatConverter *cv = nullptr;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::wstring w(path.begin(), path.end());
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(w.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        cv->GetSize(&iw, &ih);
        im.px.resize((size_t)iw * ih);
        if (SUCCEEDED(cv->CopyPixels(nullptr, iw * 4, iw * ih * 4, (BYTE *)im.px.data()))) { im.w = (int)iw; im.h = (int)ih; }
        else im.px.clear();
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    if (!im.ok() && g_log) fprintf(stderr, "[ART] cannot load %s\n", path.c_str());
    return im;
}

const Img &file(const std::string &name)
{
    auto it = g_files.find(name);
    if (it != g_files.end()) return it->second;
    return g_files[name] = load_png(g_dir + name);
}

// --- pixel helpers (straight alpha BGRA) ---------------------------------

uint32_t lerp4(uint32_t a, uint32_t b, int t)      // t 0..256
{
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= (uint32_t)((ca * (256 - t) + cb * t) >> 8) << s;
    }
    return r;
}

// Bilinear scale (for upscaling the game's pixels).
Img scale(const uint32_t *src, int sw, int sh, int dw, int dh)
{
    Img o; o.w = dw; o.h = dh; o.px.resize((size_t)dw * dh);
    for (int y = 0; y < dh; y++) {
        float fy = (y + 0.5f) * sh / dh - 0.5f; if (fy < 0) fy = 0;
        int y0 = (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : y0, ty = (int)((fy - y0) * 256);
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * sw / dw - 0.5f; if (fx < 0) fx = 0;
            int x0 = (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : x0, tx = (int)((fx - x0) * 256);
            uint32_t a = lerp4(src[y0 * sw + x0], src[y0 * sw + x1], tx);
            uint32_t b = lerp4(src[y1 * sw + x0], src[y1 * sw + x1], tx);
            o.px[(size_t)y * dw + x] = lerp4(a, b, ty);
        }
    }
    return o;
}

// Area-average scale (for shrinking icons), premultiplied while averaging.
Img shrink(const Img &s, int dw, int dh)
{
    Img o; o.w = dw; o.h = dh; o.px.assign((size_t)dw * dh, 0);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            int sx0 = x * s.w / dw, sx1 = (x + 1) * s.w / dw, sy0 = y * s.h / dh, sy1 = (y + 1) * s.h / dh;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            double r = 0, g = 0, b = 0, a = 0; int n = 0;
            for (int yy = sy0; yy < sy1; yy++)
                for (int xx = sx0; xx < sx1; xx++) {
                    uint32_t p = s.px[(size_t)yy * s.w + xx];
                    double pa = (p >> 24) / 255.0;
                    b += (p & 255) * pa; g += ((p >> 8) & 255) * pa; r += ((p >> 16) & 255) * pa; a += pa; n++;
                }
            if (a <= 0) continue;
            o.px[(size_t)y * dw + x] = ((uint32_t)(a / n * 255 + 0.5) << 24) | ((uint32_t)(r / a + 0.5) << 16) |
                                       ((uint32_t)(g / a + 0.5) << 8) | (uint32_t)(b / a + 0.5);
        }
    return o;
}

void over(Img &d, const Img &s, int ox, int oy, int sx = 0, int sy = 0, int w = -1, int h = -1)
{
    if (w < 0) w = s.w;
    if (h < 0) h = s.h;
    for (int y = 0; y < h; y++) {
        int dy = oy + y; if (dy < 0 || dy >= d.h) continue;
        for (int x = 0; x < w; x++) {
            int dx = ox + x; if (dx < 0 || dx >= d.w) continue;
            uint32_t sp = s.px[(size_t)(sy + y) * s.w + sx + x], sa = sp >> 24;
            if (!sa) continue;
            uint32_t &dp = d.px[(size_t)dy * d.w + dx], da = dp >> 24;
            uint32_t oa = sa + da * (255 - sa) / 255;
            uint32_t r = 0;
            for (int c = 0; c < 24; c += 8) {
                uint32_t sc = (sp >> c) & 255, dc = (dp >> c) & 255;
                r |= ((sc * sa + dc * da * (255 - sa) / 255) / (oa ? oa : 1)) << c;
            }
            dp = r | (oa << 24);
        }
    }
}

void clear(Img &d, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h && y < d.h; y++)
        for (int x = x0; x < x0 + w && x < d.w; x++)
            if (x >= 0 && y >= 0) d.px[(size_t)y * d.w + x] = 0;
}

uint32_t gen();

// --- what to draw ----------------------------------------------------------

// Sheet cells (4x4): A Y X B / START BACK Lstick (!) / LT RT White Black / Dpad L3 R3 Rstick.
// Game button (nfl2k5_controls.c order: A B X Y LB RB LT RT Start Back LS RS) per cell, -1 none.
const int kCellButton[16] = { 0, 3, 2, 1, 8, 9, -1, -1, 6, 7, 4, 5, -2, 10, 11, -1 };

// Icon for one sheet cell in a style, as a 128x128 tile (or empty).
Img cell_icon(int style, int cell)
{
    Img t;
    if (style == 2) {                                    // keyboard: the bound key
        int b = kCellButton[cell];
        char name[32] = "";
        if (b == -2) snprintf(name, sizeof name, "keys\\arrows.png");
        else if (b >= 0 && nfl2k5_controls_key(b)) snprintf(name, sizeof name, "keys\\vk%02X.png", nfl2k5_controls_key(b));
        if (name[0]) {
            const Img &k = file(name);
            if (k.ok()) {
                t.w = t.h = 128; t.px.assign(128 * 128, 0);
                over(t, k, 64 - k.w / 2, 64 - k.h / 2);
                return t;
            }
        }
        style = 0;                                       // no key: the controller icon
    }
    const Img &sh = file(style != 1 ? "sheet-xbox.png" : nfl2k5_controls_ps_kind() == 1 ? "sheet-ps4.png" : "sheet-ps.png");
    if (!sh.ok()) return t;
    t.w = t.h = 128; t.px.assign(128 * 128, 0);
    over(t, sh, 0, 0, (cell % 4) * 128, (cell / 4) * 128, 128, 128);
    return t;
}

const uint32_t *button_sheet(const uint32_t *orig, int style, uint32_t *ow, uint32_t *oh)
{
    char key[64];
    snprintf(key, sizeof key, "sheet%d-%u", style, style >= 1 ? gen() : 0);
    auto it = g_made.find(key);
    if (it == g_made.end()) {
        Img o = scale(orig, 128, 128, 512, 512);
        for (int c = 0; c < 16; c++) {
            if (c == 7) continue;                        // (!) stays the game's
            Img t = cell_icon(style, c);
            if (!t.ok()) continue;
            clear(o, (c % 4) * 128, (c / 4) * 128, 128, 128);
            over(o, t, (c % 4) * 128, (c / 4) * 128);
        }
        it = g_made.emplace(key, std::move(o)).first;
    }
    *ow = (uint32_t)it->second.w; *oh = (uint32_t)it->second.h;
    return it->second.px.data();
}

// The 64x64 header sheet: arrows with L/R (LT/RT), White/Black and A/Y icons.
// Icon centres (in its 64x64 pixels) and the big-sheet cell drawn there.
struct SmallSpot { int cx, cy, half, cell; };
const SmallSpot kSmall[6] = { { 22, 23, 8, 8 }, { 42, 23, 8, 9 }, { 21, 40, 7, 10 }, { 43, 40, 7, 11 },
                              { 20, 55, 7, 0 }, { 43, 55, 7, 1 } };

const uint32_t *small_sheet(const uint32_t *orig, int style, uint32_t *ow, uint32_t *oh)
{
    char key[64];
    snprintf(key, sizeof key, "small%d-%u", style, style >= 1 ? gen() : 0);
    auto it = g_made.find(key);
    if (it == g_made.end()) {
        Img o = scale(orig, 64, 64, 256, 256);
        for (const SmallSpot &s : kSmall) {
            Img t = cell_icon(style, s.cell);
            if (!t.ok()) continue;
            int size = s.half * 2 * 4;
            Img sm = shrink(t, size, size);
            clear(o, (s.cx - s.half) * 4, (s.cy - s.half) * 4, size, size);
            over(o, sm, (s.cx - s.half) * 4, (s.cy - s.half) * 4);
        }
        it = g_made.emplace(key, std::move(o)).first;
    }
    *ow = (uint32_t)it->second.w; *oh = (uint32_t)it->second.h;
    return it->second.px.data();
}

// Highlight overlay positions: pad-<kind>.json {"hl": {"a": [x, y], ...}}.
bool hl_pos(const char *kind, const char *button, int *x, int *y)
{
    std::string path = g_dir + "pad-" + kind + ".json";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    size_t h = s.find("\"hl\"");
    if (h == std::string::npos) return false;
    std::string k = std::string("\"") + button + "\"";
    size_t p = s.find(k, h);
    if (p == std::string::npos) return false;
    p = s.find('[', p);
    return p != std::string::npos && sscanf(s.c_str() + p, "[ %d , %d", x, y) == 2;
}

const uint32_t *pad_picture(uint32_t *ow, uint32_t *oh)
{
    static const char *const kSlotName[8] = { "a", "b", "x", "y", "lt", "rt", "lb", "rb" };  // White = LB, Black = RB
    int port = nfl2k5_cs_screen_port();
    int dev = nfl2k5_controls_port_dev(port < 0 ? 0 : port);
    int style = nfl2k5_controls_style();
    // the picture follows the prompts' family (Xbox / PlayStation) when they
    // are set by hand; otherwise it is the controller on that port
    if (style == 1 && dev != 1 && dev != 2) dev = nfl2k5_controls_ps_kind();
    else if (style == 0 && dev > 0) dev = 0;
    else if (dev < 0) dev = 0;
    const char *kind = dev == 1 ? "ds4" : dev == 2 ? "dualsense" : "xbox";
    int hl = nfl2k5_cs_highlight();
    char key[64];
    snprintf(key, sizeof key, "pad-%s-%d", kind, hl);
    auto it = g_made.find(key);
    if (it == g_made.end()) {
        Img o = file(std::string("pad-") + kind + ".png");
        if (!o.ok()) return nullptr;
        if (hl >= 0) {
            int x, y;
            const Img &ov = file(std::string("pad-") + kind + "-hl-" + kSlotName[hl] + ".png");
            if (ov.ok() && hl_pos(kind, kSlotName[hl], &x, &y)) over(o, ov, x, y);
        }
        it = g_made.emplace(key, std::move(o)).first;
    }
    *ow = (uint32_t)it->second.w; *oh = (uint32_t)it->second.h;
    return it->second.px.data();
}

// The 64x64 header sheet is recognised by its layout the first time: grey
// arrows in the corners and the L icon's white strokes at (20..24, 18..28).
bool looks_like_small_sheet(const uint32_t *p)
{
    auto lum = [&](int x, int y) { uint32_t c = p[y * 64 + x]; return (int)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3; };
    auto alpha = [&](int x, int y) { return (int)(p[y * 64 + x] >> 24); };
    int white = 0;
    for (int y = 18; y < 29; y++) for (int x = 19; x < 25; x++) if (lum(x, y) > 200 && alpha(x, y) > 200) white++;
    return white >= 10 && alpha(2, 8) > 100 && alpha(61, 8) > 100;
}

// --- renderer hooks ---------------------------------------------------------

// The shapes of the textures replaced here (bit 1: they follow gen()); any
// other texture may come from a texture pack (nfl2k5_texture_packs.cpp).
extern "C" int nfl2k5_texpack_active(void);
extern "C" const uint32_t *nfl2k5_texpack_find(uint32_t fmt, uint32_t w, uint32_t h, uint32_t content,
                                               const uint32_t *orig, uint32_t *ow, uint32_t *oh);
extern "C" int nfl2k5_texpack_era_on(void);
extern "C" uint32_t nfl2k5_texpack_gen(void);
int wants(uint32_t fmt, uint32_t w, uint32_t h)
{
    if ((fmt == 0x0C && w == 256 && h == 256) || (fmt == 0x0B && ((w == 128 && h == 128) || (w == 64 && h == 64))))
        return 3;
    // My NFL Eras office (era context set): every paletted shape follows gen(),
    // so office textures upload again when the era or the context changes.
    if (fmt == 0x0B && nfl2k5_texpack_era_on()) return 3;
    return nfl2k5_texpack_active() ? 1 : 0;
}

uint32_t gen()
{
    // style, PlayStation kind, the setup screen's controller and highlight,
    // edits, key bindings. Asked for every candidate texture every frame, so
    // worked out at most every 15 ms (port_map polls XInput).
    static volatile DWORD at;
    static volatile uint32_t last;
    DWORD now = GetTickCount();
    if (at && now - at < 15) return last;
    int port = nfl2k5_cs_screen_port(), style = nfl2k5_controls_style();
    uint32_t g = (uint32_t)style * 1000003u ^ (uint32_t)nfl2k5_controls_ps_kind() * 7919u ^
                 (uint32_t)(nfl2k5_cs_highlight() + 2) * 15485863u ^ nfl2k5_cs_gen() * 2654435761u;
    if (port >= 0) g ^= (uint32_t)(nfl2k5_controls_port_dev(port) + 2) * 104729u;
    if (style == 2) for (int b = 0; b < 12; b++) g = g * 31u + (uint32_t)nfl2k5_controls_key(b);
    g ^= nfl2k5_texpack_gen() * 0x85EBCA6Bu;      // the Eras office era context
    if (g != last && getenv("NFL2K5_ERAS_TEXLOG")) fprintf(stderr, "[ART] gen %08X -> %08X (texpack gen %u)\n", last, g, nfl2k5_texpack_gen());
    last = g;
    at = now ? now : 1;
    return g;
}

const uint32_t *override(uint32_t fmt, uint32_t w, uint32_t h, uint32_t content, const uint32_t *orig,
                         uint32_t *ow, uint32_t *oh)
{
    const uint32_t *r = nfl2k5_texpack_find(fmt, w, h, content, orig, ow, oh);   // a texture pack first
    if (r) return r;
    int style = nfl2k5_controls_style();
    EnterCriticalSection(&g_lock);
    if (g_made.size() > 64) g_made.clear();
    if (!g_have_art) ;
    else if (fmt == 0x0C && content == kDukeHash) r = pad_picture(ow, oh);
    else if (fmt == 0x0B && orig && style != 3) {
        uint32_t dh = fnv(orig, (size_t)w * h);
        if (w == 128 && dh == kSheetHash) r = button_sheet(orig, style, ow, oh);
        else if (w == 64) {
            if (!g_small_hash && looks_like_small_sheet(orig)) {
                g_small_hash = dh;
                if (g_log) fprintf(stderr, "[ART] header sheet hash %08X\n", dh);
            }
            if (dh == g_small_hash) r = small_sheet(orig, style, ow, oh);
        }
        if (g_log && !r) fprintf(stderr, "[ART] %ux%u paletted texture hash %08X (not replaced)\n", w, h, dh);
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

}  // namespace

extern "C" void nfl2k5_controller_art_init(void)
{
    InitializeCriticalSection(&g_lock);
    g_log = getenv("NFL2K5_ART_LOG") != nullptr;
    g_dir = find_dir();
    g_have_art = GetFileAttributesA((g_dir + "sheet-xbox.png").c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!g_have_art)
        fprintf(stderr, "[ART] %s not found; the game's own controller pictures and icons stay\n", g_dir.c_str());
    g_xbox_tex_override_wants = wants;
    g_xbox_tex_override_gen = gen;
    g_xbox_tex_override = override;
    printf("[ART] controller pictures and button icons from %s\n", g_dir.c_str());
}

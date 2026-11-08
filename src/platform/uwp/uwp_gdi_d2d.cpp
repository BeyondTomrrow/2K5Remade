/*
 * uwp_gdi_d2d.cpp -- the GDI calls the PC sources draw with, on Direct2D +
 * DirectWrite, for the UWP build.
 *
 * gdi32 is not in the UWP API set. The F1 menu, the music toast and the
 * PlayStation prompt sheet draw into a memory DC holding a 32-bit DIB, then
 * read the DIB's bits after GdiFlush(). This file defines those import slots
 * (__imp_X, as uwp_win32_shim.c does) over its own objects: a DIB is a malloc'd
 * buffer; while a DC draws into it, a WIC bitmap copy is the render target, and
 * the bits are written back on GdiFlush, on selecting another bitmap, or when
 * the bitmap or DC is deleted. Only what those callers use is implemented:
 * memory DCs, solid brushes and pens, stock NULL_BRUSH/NULL_PEN, fonts by face
 * name/height/weight, TextOut / DrawText / GetTextExtentPoint32 (ANSI),
 * FillRect, Rectangle, RoundRect, Ellipse, MoveToEx/LineTo/Polyline, SetPixel.
 */
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {

enum Kind : uint32_t { K_DC = 1, K_BMP, K_FONT, K_BRUSH, K_PEN };
const uint32_t kMagic = 0x47444932;   /* "GDI2" */

struct Obj { uint32_t magic = kMagic; Kind kind; bool stock = false; explicit Obj(Kind k) : kind(k) {} virtual ~Obj() {} };

struct Bmp : Obj {
    int w = 0, h = 0;
    bool top_down = true;
    std::vector<uint8_t> bits;
    IWICBitmap *wic = nullptr;          /* set while being drawn into */
    ID2D1RenderTarget *rt = nullptr;
    Bmp() : Obj(K_BMP) {}
};
struct Font : Obj { std::wstring face; int height = 16; int weight = FW_NORMAL; bool italic = false; Font() : Obj(K_FONT) {} };
struct Brush : Obj { COLORREF col = 0; bool null = false; Brush() : Obj(K_BRUSH) {} };
struct Pen : Obj { COLORREF col = 0; int width = 1; bool null = false; Pen() : Obj(K_PEN) {} };
struct Dc : Obj {
    Bmp *bmp = nullptr;
    Font *font = nullptr;
    Brush *brush = nullptr;
    Pen *pen = nullptr;
    COLORREF text = 0;
    int bk_mode = OPAQUE;
    POINT pos = {0, 0};
    Dc() : Obj(K_DC) {}
};

std::recursive_mutex g_lock;
ID2D1Factory *g_d2d;
IDWriteFactory *g_dw;
IWICImagingFactory *g_wic;
std::vector<Bmp *> g_drawing;          /* bitmaps with an open render target */

Brush g_null_brush, g_white_brush, g_black_brush;
Pen g_null_pen, g_black_pen, g_white_pen;
Font g_system_font;
Bmp g_default_bmp;                     /* what a new DC has selected (1x1, never drawn) */

bool init()
{
    if (g_d2d && g_dw && g_wic) return true;
    if (!g_d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &g_d2d))) return false;
    if (!g_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown **>(&g_dw)))) return false;
    if (!g_wic) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
        if (hr == CO_E_NOTINITIALIZED) {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
        }
        if (FAILED(hr)) return false;
    }
    g_null_brush.null = true; g_null_brush.stock = true;
    g_white_brush.col = RGB(255, 255, 255); g_white_brush.stock = true;
    g_black_brush.stock = true;
    g_null_pen.null = true; g_null_pen.stock = true;
    g_black_pen.stock = true;
    g_white_pen.col = RGB(255, 255, 255); g_white_pen.stock = true;
    g_system_font.face = L"Segoe UI"; g_system_font.height = 16; g_system_font.stock = true;
    g_default_bmp.stock = true;
    return true;
}

template <class T> T *as(void *h, Kind k)
{
    Obj *o = static_cast<Obj *>(h);
    if (!o || o->magic != kMagic || o->kind != k) return nullptr;
    return static_cast<T *>(o);
}
Kind kind_of(void *h)
{
    Obj *o = static_cast<Obj *>(h);
    return (o && o->magic == kMagic) ? o->kind : (Kind)0;
}

D2D1_COLOR_F color(COLORREF c)
{
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f);
}

/* Row r of the DIB as the caller sees memory (top-down or bottom-up). */
uint8_t *row(Bmp *b, int r) { return b->bits.data() + (size_t)(b->top_down ? r : b->h - 1 - r) * b->w * 4; }

/* Write a bitmap's render target back into its bits and close it. */
void flush(Bmp *b)
{
    if (!b || !b->rt) return;
    b->rt->EndDraw();
    std::vector<uint8_t> tmp((size_t)b->w * b->h * 4);
    WICRect all = {0, 0, b->w, b->h};
    if (SUCCEEDED(b->wic->CopyPixels(&all, b->w * 4, (UINT)tmp.size(), tmp.data())))
        for (int r = 0; r < b->h; r++) {
            uint8_t *d = row(b, r), *s = tmp.data() + (size_t)r * b->w * 4;
            for (int x = 0; x < b->w; x++) {   /* GDI leaves alpha alone; callers make their own */
                d[x * 4 + 0] = s[x * 4 + 0]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4 + 2];
            }
        }
    b->rt->Release(); b->rt = nullptr;
    b->wic->Release(); b->wic = nullptr;
    for (size_t i = 0; i < g_drawing.size(); i++)
        if (g_drawing[i] == b) { g_drawing.erase(g_drawing.begin() + i); break; }
}

/* The render target for a DC's bitmap, opened from the bits on first use. */
ID2D1RenderTarget *target(Dc *dc)
{
    Bmp *b = dc ? dc->bmp : nullptr;
    if (!b || b->stock || b->w <= 0 || b->h <= 0 || !init()) return nullptr;
    if (b->rt) return b->rt;
    std::vector<uint8_t> tmp((size_t)b->w * b->h * 4);
    for (int r = 0; r < b->h; r++) memcpy(tmp.data() + (size_t)r * b->w * 4, row(b, r), (size_t)b->w * 4);
    if (FAILED(g_wic->CreateBitmapFromMemory(b->w, b->h, GUID_WICPixelFormat32bppBGR, b->w * 4,
                                              (UINT)tmp.size(), tmp.data(), &b->wic))) return nullptr;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    if (FAILED(g_d2d->CreateWicBitmapRenderTarget(b->wic, p, &b->rt))) { b->wic->Release(); b->wic = nullptr; return nullptr; }
    b->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    b->rt->BeginDraw();
    g_drawing.push_back(b);
    return b->rt;
}

ID2D1SolidColorBrush *solid(ID2D1RenderTarget *rt, COLORREF c)
{
    ID2D1SolidColorBrush *br = nullptr;
    rt->CreateSolidColorBrush(color(c), &br);
    return br;
}

/* A text format for the DC's font. GDI heights: positive = cell height
 * (ascent + descent), negative = em size. */
IDWriteTextFormat *format(Dc *dc, float *cell_h, float *baseline)
{
    Font *f = dc->font ? dc->font : &g_system_font;
    float em = (float)(f->height < 0 ? -f->height : f->height);
    if (em == 0) em = 16;
    DWRITE_FONT_WEIGHT wt = (DWRITE_FONT_WEIGHT)(f->weight ? f->weight : FW_NORMAL);
    DWRITE_FONT_STYLE st = f->italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    float asc = 0.9f, desc = 0.22f;                 /* fractions of em when metrics are missing */
    IDWriteFontCollection *coll = nullptr;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&coll))) {
        UINT32 idx = 0; BOOL exists = FALSE;
        coll->FindFamilyName(f->face.c_str(), &idx, &exists);
        IDWriteFontFamily *fam = nullptr; IDWriteFont *font = nullptr;
        if (exists && SUCCEEDED(coll->GetFontFamily(idx, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(wt, DWRITE_FONT_STRETCH_NORMAL, st, &font))) {
            DWRITE_FONT_METRICS m; font->GetMetrics(&m);
            asc = (float)m.ascent / m.designUnitsPerEm;
            desc = (float)m.descent / m.designUnitsPerEm;
        }
        if (font) font->Release();
        if (fam) fam->Release();
        coll->Release();
    }
    if (f->height > 0) em = em / (asc + desc);
    if (cell_h) *cell_h = em * (asc + desc);
    if (baseline) *baseline = em * asc;
    IDWriteTextFormat *tf = nullptr;
    g_dw->CreateTextFormat(f->face.empty() ? L"Segoe UI" : f->face.c_str(), nullptr, wt, st,
                           DWRITE_FONT_STRETCH_NORMAL, em, L"en-us", &tf);
    if (tf) {
        tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        /* one line exactly a GDI cell tall, baseline where GDI puts it */
        tf->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, em * (asc + desc), em * asc);
    }
    return tf;
}

std::wstring widen(const char *s, int n)
{
    if (!s) return std::wstring();
    if (n < 0) n = (int)strlen(s);
    std::wstring w(n, L'\0');
    int got = MultiByteToWideChar(CP_ACP, 0, s, n, &w[0], n);
    w.resize(got > 0 ? got : 0);
    return w;
}

IDWriteTextLayout *layout(Dc *dc, const std::wstring &w, float maxw, float maxh, float *cell_h)
{
    IDWriteTextFormat *tf = format(dc, cell_h, nullptr);
    if (!tf) return nullptr;
    IDWriteTextLayout *tl = nullptr;
    g_dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), tf, maxw, maxh, &tl);
    tf->Release();
    return tl;
}

void draw_layout(Dc *dc, ID2D1RenderTarget *rt, IDWriteTextLayout *tl, float x, float y)
{
    ID2D1SolidColorBrush *br = solid(rt, dc->text);
    if (br) { rt->DrawTextLayout(D2D1::Point2F(x, y), tl, br, D2D1_DRAW_TEXT_OPTIONS_NONE); br->Release(); }
}

/* Fill then outline a shape the way GDI does with the DC's brush and pen. */
template <class Fill, class Stroke>
void shape(Dc *dc, ID2D1RenderTarget *rt, Fill fill, Stroke stroke)
{
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    Brush *b = dc->brush ? dc->brush : &g_white_brush;
    Pen *p = dc->pen ? dc->pen : &g_black_pen;
    if (!b->null) { ID2D1SolidColorBrush *br = solid(rt, b->col); if (br) { fill(br); br->Release(); } }
    if (!p->null) { ID2D1SolidColorBrush *br = solid(rt, p->col); if (br) { stroke(br, (float)(p->width > 0 ? p->width : 1)); br->Release(); } }
}

} // namespace

/* ---------------------------------------------------------------------- */

#define GDI(ret, name, args) \
    static ret WINAPI gdi_##name args; \
    extern "C" void *__imp_##name = (void *)gdi_##name; \
    static ret WINAPI gdi_##name args

GDI(HDC, GetDC, (HWND))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    init();
    Dc *dc = new Dc();
    dc->bmp = &g_default_bmp;
    return (HDC)dc;
}

GDI(int, ReleaseDC, (HWND, HDC hdc))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    if (Dc *dc = as<Dc>(hdc, K_DC)) { flush(dc->bmp); delete dc; return 1; }
    return 0;
}

GDI(HDC, CreateCompatibleDC, (HDC))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    if (!init()) return nullptr;
    Dc *dc = new Dc();
    dc->bmp = &g_default_bmp;
    return (HDC)dc;
}

GDI(BOOL, DeleteDC, (HDC hdc))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc) return FALSE;
    flush(dc->bmp);
    delete dc;
    return TRUE;
}

GDI(HBITMAP, CreateDIBSection, (HDC, const BITMAPINFO *bi, UINT, void **bits, HANDLE, DWORD))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    if (bits) *bits = nullptr;
    if (!bi || bi->bmiHeader.biBitCount != 32 || bi->bmiHeader.biWidth <= 0 || bi->bmiHeader.biHeight == 0) return nullptr;
    Bmp *b = new Bmp();
    b->w = bi->bmiHeader.biWidth;
    b->h = bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight;
    b->top_down = bi->bmiHeader.biHeight < 0;
    b->bits.assign((size_t)b->w * b->h * 4, 0);
    if (bits) *bits = b->bits.data();
    return (HBITMAP)b;
}

GDI(HGDIOBJ, SelectObject, (HDC hdc, HGDIOBJ h))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc || !h) return nullptr;
    HGDIOBJ old = nullptr;
    switch (kind_of(h)) {
    case K_BMP: flush(dc->bmp); old = dc->bmp; dc->bmp = static_cast<Bmp *>(h); break;
    case K_FONT: old = dc->font ? (HGDIOBJ)dc->font : (HGDIOBJ)&g_system_font; dc->font = static_cast<Font *>(h); break;
    case K_BRUSH: old = dc->brush ? (HGDIOBJ)dc->brush : (HGDIOBJ)&g_white_brush; dc->brush = static_cast<Brush *>(h); break;
    case K_PEN: old = dc->pen ? (HGDIOBJ)dc->pen : (HGDIOBJ)&g_black_pen; dc->pen = static_cast<Pen *>(h); break;
    default: break;
    }
    return old;
}

GDI(BOOL, DeleteObject, (HGDIOBJ h))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Obj *o = static_cast<Obj *>(h);
    if (!o || o->magic != kMagic || o->kind == K_DC) return FALSE;
    if (o->stock) return TRUE;
    if (o->kind == K_BMP) flush(static_cast<Bmp *>(o));
    o->magic = 0;
    delete o;
    return TRUE;
}

GDI(HGDIOBJ, GetStockObject, (int i))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    init();
    switch (i) {
    case WHITE_BRUSH: return &g_white_brush;
    case BLACK_BRUSH: return &g_black_brush;
    case NULL_BRUSH: return &g_null_brush;
    case WHITE_PEN: return &g_white_pen;
    case BLACK_PEN: return &g_black_pen;
    case NULL_PEN: return &g_null_pen;
    default: return &g_system_font;
    }
}

GDI(HFONT, CreateFontA, (int h, int, int, int, int weight, DWORD italic, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR face))
{
    Font *f = new Font();
    f->height = h;
    f->weight = weight;
    f->italic = italic != 0;
    f->face = face && *face ? widen(face, -1) : L"Segoe UI";
    return (HFONT)f;
}

GDI(HPEN, CreatePen, (int style, int width, COLORREF c))
{
    Pen *p = new Pen();
    p->col = c;
    p->width = width;
    p->null = style == PS_NULL;
    return (HPEN)p;
}

GDI(HBRUSH, CreateSolidBrush, (COLORREF c))
{
    Brush *b = new Brush();
    b->col = c;
    return (HBRUSH)b;
}

GDI(int, SetBkMode, (HDC hdc, int mode))
{
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc) return 0;
    int old = dc->bk_mode;
    dc->bk_mode = mode;
    return old;
}

GDI(COLORREF, SetTextColor, (HDC hdc, COLORREF c))
{
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc) return CLR_INVALID;
    COLORREF old = dc->text;
    dc->text = c;
    return old;
}

GDI(BOOL, GetTextExtentPoint32A, (HDC hdc, LPCSTR s, int n, LPSIZE sz))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc || !sz || !init()) return FALSE;
    float cell = 0;
    IDWriteTextLayout *tl = layout(dc, widen(s, n), 100000.0f, 100000.0f, &cell);
    if (!tl) return FALSE;
    DWRITE_TEXT_METRICS m; tl->GetMetrics(&m);
    tl->Release();
    sz->cx = (LONG)(m.widthIncludingTrailingWhitespace + 0.5f);
    sz->cy = (LONG)(cell + 0.5f);
    return TRUE;
}

GDI(BOOL, TextOutA, (HDC hdc, int x, int y, LPCSTR s, int n))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return FALSE;
    float cell = 0;
    IDWriteTextLayout *tl = layout(dc, widen(s, n), 100000.0f, 100000.0f, &cell);
    if (!tl) return FALSE;
    draw_layout(dc, rt, tl, (float)x, (float)y);
    tl->Release();
    return TRUE;
}

GDI(int, DrawTextA, (HDC hdc, LPCSTR s, int n, LPRECT r, UINT fmt))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc || !r || !init()) return 0;
    float w = (float)(r->right - r->left), h = (float)(r->bottom - r->top), cell = 0;
    IDWriteTextLayout *tl = layout(dc, widen(s, n), w > 0 ? w : 1.0f, h > 0 ? h : 1.0f, &cell);
    if (!tl) return 0;
    tl->SetTextAlignment(fmt & DT_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER :
                         fmt & DT_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (!(fmt & DT_SINGLELINE)) tl->SetWordWrapping(fmt & DT_WORDBREAK ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if ((fmt & DT_SINGLELINE) && (fmt & DT_VCENTER)) tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    else if ((fmt & DT_SINGLELINE) && (fmt & DT_BOTTOM)) tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    IDWriteInlineObject *ellipsis = nullptr;
    if (fmt & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS)) {
        IDWriteTextFormat *tf = format(dc, nullptr, nullptr);
        if (tf && SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(tf, &ellipsis))) {
            DWRITE_TRIMMING t = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            tl->SetTrimming(&t, ellipsis);
        }
        if (tf) tf->Release();
    }
    DWRITE_TEXT_METRICS m; tl->GetMetrics(&m);
    int result = (int)(m.height + 0.5f);
    if (fmt & DT_CALCRECT) {
        r->right = r->left + (LONG)(m.widthIncludingTrailingWhitespace + 0.5f);
        r->bottom = r->top + result;
    } else if (ID2D1RenderTarget *rt = target(dc)) {
        rt->PushAxisAlignedClip(D2D1::RectF((float)r->left, (float)r->top, (float)r->right, (float)r->bottom),
                                D2D1_ANTIALIAS_MODE_ALIASED);
        draw_layout(dc, rt, tl, (float)r->left, (float)r->top);
        rt->PopAxisAlignedClip();
    }
    if (ellipsis) ellipsis->Release();
    tl->Release();
    return result;
}

GDI(int, FillRect, (HDC hdc, const RECT *r, HBRUSH hb))
{
    std::lock_guard<std::recursive_mutex> l(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    Brush *b = as<Brush>(hb, K_BRUSH);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt || !r || !b) return 0;
    if (b->null) return 1;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1SolidColorBrush *br = solid(rt, b->col);
    if (br) {
        rt->FillRectangle(D2D1::RectF((float)r->left, (float)r->top, (float)r->right, (float)r->bottom), br);
        br->Release();
    }
    return 1;
}

GDI(BOOL, Rectangle, (HDC hdc, int l, int t, int r, int b))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return FALSE;
    shape(dc, rt,
        [&](ID2D1Brush *br) { rt->FillRectangle(D2D1::RectF((float)l, (float)t, (float)r, (float)b), br); },
        [&](ID2D1Brush *br, float w) { rt->DrawRectangle(D2D1::RectF(l + w / 2, t + w / 2, r - w / 2, b - w / 2), br, w); });
    return TRUE;
}

GDI(BOOL, RoundRect, (HDC hdc, int l, int t, int r, int b, int ew, int eh))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return FALSE;
    shape(dc, rt,
        [&](ID2D1Brush *br) { rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF((float)l, (float)t, (float)r, (float)b), ew / 2.0f, eh / 2.0f), br); },
        [&](ID2D1Brush *br, float w) { rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l + w / 2, t + w / 2, r - w / 2, b - w / 2), ew / 2.0f, eh / 2.0f), br, w); });
    return TRUE;
}

GDI(BOOL, Ellipse, (HDC hdc, int l, int t, int r, int b))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return FALSE;
    float cx = (l + r) / 2.0f, cy = (t + b) / 2.0f, rx = (r - l) / 2.0f, ry = (b - t) / 2.0f;
    shape(dc, rt,
        [&](ID2D1Brush *br) { rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), br); },
        [&](ID2D1Brush *br, float w) { rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx - w / 2, ry - w / 2), br, w); });
    return TRUE;
}

GDI(BOOL, MoveToEx, (HDC hdc, int x, int y, LPPOINT old))
{
    Dc *dc = as<Dc>(hdc, K_DC);
    if (!dc) return FALSE;
    if (old) *old = dc->pos;
    dc->pos.x = x; dc->pos.y = y;
    return TRUE;
}

static void line(Dc *dc, ID2D1RenderTarget *rt, POINT a, POINT b)
{
    Pen *p = dc->pen ? dc->pen : &g_black_pen;
    if (p->null) return;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1SolidColorBrush *br = solid(rt, p->col);
    if (!br) return;
    rt->DrawLine(D2D1::Point2F(a.x + 0.5f, a.y + 0.5f), D2D1::Point2F(b.x + 0.5f, b.y + 0.5f), br,
                 (float)(p->width > 0 ? p->width : 1));
    br->Release();
}

GDI(BOOL, LineTo, (HDC hdc, int x, int y))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return FALSE;
    POINT to = {x, y};
    line(dc, rt, dc->pos, to);
    dc->pos = to;
    return TRUE;
}

GDI(BOOL, Polyline, (HDC hdc, const POINT *pts, int n))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt || !pts) return FALSE;
    for (int i = 1; i < n; i++) line(dc, rt, pts[i - 1], pts[i]);
    return TRUE;
}

GDI(COLORREF, SetPixel, (HDC hdc, int x, int y, COLORREF c))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    Dc *dc = as<Dc>(hdc, K_DC);
    ID2D1RenderTarget *rt = target(dc);
    if (!rt) return (COLORREF)-1;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    if (ID2D1SolidColorBrush *br = solid(rt, c)) {
        rt->FillRectangle(D2D1::RectF((float)x, (float)y, x + 1.0f, y + 1.0f), br);
        br->Release();
    }
    return c;
}

GDI(BOOL, GdiFlush, (void))
{
    std::lock_guard<std::recursive_mutex> lk(g_lock);
    while (!g_drawing.empty()) flush(g_drawing.back());
    return TRUE;
}

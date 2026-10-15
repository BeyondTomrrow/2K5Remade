// Texture packs (2026-10-09): replace any game texture with an image of any
// resolution, from a folder, without rebuilding anything.
//
//   mods\textures\<any folders>\<HASH>[anything].png / .jpg / .dds
//
// HASH is the texture's 8-hex-digit content hash: every byte of the texture
// as the game uploads it, plus its palette (tex_content_hash in
// nv2a_gpu_d3d11.inc.c). Find it by dumping: with texture_dump = 1 in
// nfl2k5_game.ini (or NFL2K5_TEXDUMP=1) every texture the game uses is written
// once to texture-dump\ in the per-user folder as <HASH>_<w>x<h>_fmt<NN>.png.
// Copy a dump into a pack folder, paint it at any size (4x, 8x...), keep the
// name. Larger images get a mip chain, so they stay sharp up close and calm far
// away. Folders are read in name order; the first file for a hash wins.
#include <windows.h>
#ifdef _WIN32
#include <wincodec.h>
#else
#include "platform/posix/image_bgra.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#pragma comment(lib, "windowscodecs.lib")
#endif

extern "C" char *xbox_user_path(const char *rel, char *out, size_t n);
extern "C" volatile long g_xbox_tex_dump_wanted;    // renderer: decode DXT for the dump

namespace {

struct Pic { uint32_t w = 0, h = 0; std::vector<uint32_t> px; };

CRITICAL_SECTION g_lock;
bool g_ready, g_dump;
std::map<uint32_t, std::string> g_files;            // hash -> image file
std::set<uint32_t> g_dumped, g_missing;
std::list<std::pair<uint32_t, Pic>> g_cache;        // most recent first
size_t g_cache_bytes;
const size_t kCacheMax = 768u << 20;                // decoded images kept for re-uploads
std::string g_dump_dir;
#ifdef _WIN32
IWICImagingFactory *g_wic;
#endif

bool hex8(const char *s, uint32_t *v)
{
    uint32_t r = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        r = r * 16 + (uint32_t)d;
    }
    *v = r;
    return true;
}

void scan(const std::string &dir, int depth)
{
    WIN32_FIND_DATAA fd;
    std::vector<std::string> names;
    HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do names.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        if (n == "." || n == "..") continue;
        std::string p = dir + n;
        DWORD a = GetFileAttributesA(p.c_str());
        if (a & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 6) scan(p + "\\", depth + 1); continue; }
        size_t dot = n.find_last_of('.');
        if (dot == std::string::npos || n.size() < 12) continue;
        std::string ext = n.substr(dot);
        for (char &c : ext) c = (char)tolower((unsigned char)c);
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".dds" && ext != ".bmp") continue;
        uint32_t hash;
        if (!hex8(n.c_str(), &hash)) continue;
        if (!g_files.count(hash)) g_files[hash] = p;
    }
}

#ifdef _WIN32
bool decode(const std::string &path, Pic *o)
{
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *fr = nullptr;
    IWICFormatConverter *cv = nullptr;
    bool ok = false;
    std::wstring w(path.begin(), path.end());
    if (g_wic && SUCCEEDED(g_wic->CreateDecoderFromFilename(w.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(g_wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        cv->GetSize(&iw, &ih);
        if (iw && ih && iw <= 16384 && ih <= 16384) {
            o->px.resize((size_t)iw * ih);
            ok = SUCCEEDED(cv->CopyPixels(nullptr, iw * 4, iw * ih * 4, (BYTE *)o->px.data()));
            o->w = iw; o->h = ih;
        }
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    return ok;
}

void write_png(const std::string &path, const uint32_t *px, uint32_t w, uint32_t h)
{
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    std::wstring wp(path.begin(), path.end());
    if (g_wic && SUCCEEDED(g_wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(wp.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
        SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h))) {
        WICPixelFormatGUID f = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(fr->SetPixelFormat(&f)) &&
            SUCCEEDED(fr->WritePixels(h, w * 4, w * h * 4, (BYTE *)px)))
            fr->Commit(), enc->Commit();
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
}

#else
bool decode(const std::string &path, Pic *o)
{
    uint32_t iw = 0, ih = 0;
    uint32_t *px = nfl2k5_image_load_bgra(path.c_str(), &iw, &ih);
    bool ok = false;
    if (px && iw && ih && iw <= 16384 && ih <= 16384) {
        o->px.assign(px, px + (size_t)iw * ih);
        o->w = iw; o->h = ih;
        ok = true;
    }
    if (px) nfl2k5_image_free(px);
    return ok;
}

void write_png(const std::string &path, const uint32_t *px, uint32_t w, uint32_t h)
{
    nfl2k5_image_write_png_bgra(path.c_str(), px, w, h);
}
#endif

}  // namespace

extern "C" int nfl2k5_texpack_active(void) { return g_ready && (g_dump || !g_files.empty()); }

extern "C" const uint32_t *nfl2k5_texpack_find(uint32_t fmt, uint32_t w, uint32_t h, uint32_t content,
                                               const uint32_t *orig, uint32_t *ow, uint32_t *oh)
{
    if (!nfl2k5_texpack_active() || !content) return nullptr;
    EnterCriticalSection(&g_lock);
    if (g_dump && orig && !g_dumped.count(content)) {
        char name[96];
        g_dumped.insert(content);
        snprintf(name, sizeof name, "%08X_%ux%u_fmt%02X.png", content, w, h, fmt);
        write_png(g_dump_dir + name, orig, w, h);
    }
    const uint32_t *r = nullptr;
    auto f = g_files.find(content);
    if (f != g_files.end() && !g_missing.count(content)) {
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
            if (it->first == content) {
                g_cache.splice(g_cache.begin(), g_cache, it);
                r = g_cache.front().second.px.data();
                *ow = g_cache.front().second.w; *oh = g_cache.front().second.h;
                break;
            }
        if (!r) {
            Pic p;
            if (decode(f->second, &p)) {
                g_cache_bytes += p.px.size() * 4;
                g_cache.emplace_front(content, std::move(p));
                while (g_cache_bytes > kCacheMax && g_cache.size() > 1) {
                    g_cache_bytes -= g_cache.back().second.px.size() * 4;
                    g_cache.pop_back();
                }
                r = g_cache.front().second.px.data();
                *ow = g_cache.front().second.w; *oh = g_cache.front().second.h;
                printf("[TEXPACK] %08X (%ux%u) -> %s (%ux%u)\n", content, w, h, f->second.c_str(), *ow, *oh);
            } else {
                g_missing.insert(content);
                fprintf(stderr, "[TEXPACK] cannot read %s\n", f->second.c_str());
            }
        }
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

extern "C" void nfl2k5_texture_packs_init(void)
{
    char ini[MAX_PATH], line[160], dd[MAX_PATH];
    InitializeCriticalSection(&g_lock);
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) g_wic = nullptr;
#endif
    FILE *f = fopen(xbox_user_path("nfl2k5_game.ini", ini, sizeof ini), "r");
    if (f) {
        while (fgets(line, sizeof line, f)) { int v; if (sscanf(line, " texture_dump = %d", &v) == 1) g_dump = v != 0; }
        fclose(f);
    }
    if (getenv("NFL2K5_TEXDUMP")) g_dump = atoi(getenv("NFL2K5_TEXDUMP")) != 0;
    scan("mods\\textures\\", 0);
    if (g_dump) {
        g_dump_dir = std::string(xbox_user_path("texture-dump", dd, sizeof dd)) + "\\";
        CreateDirectoryA(g_dump_dir.c_str(), nullptr);
        g_xbox_tex_dump_wanted = 1;
    }
    g_ready = true;
    if (!g_files.empty() || g_dump)
        printf("[TEXPACK] %zu replacement textures in mods\\textures%s\n", g_files.size(),
               g_dump ? "; dumping textures to texture-dump" : "");
}

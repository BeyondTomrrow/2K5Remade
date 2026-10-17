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
#include <wincodec.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

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
IWICImagingFactory *g_wic;

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

// ---- My NFL Eras office era context (docs/eras/ERAS-OFFICE-DESIGN.md 2.4) --------
//
// Set only by the Eras office (src/nfl2k5_eras_office.c) while its room shows,
// so Franchise, which shares every office texture hash, always draws retail.
// While it is set, a texture is looked up first in the era folders
//   mods\eras\offices\<season>\  <decade>\  default\
// by raw hash name (<HASH>*.png, the texture-dump workflow) or by friendly name
// (<name>.png; names -> hashes in mods\eras\offices\office-textures.ini), then
// in the era.ini [grade] lines of those folders: a colour grade of the game's own
// pixels (orig), so an era recolours retail textures without shipping any retail
// pixels. Then mods\textures as usual.
struct Grade {
    bool on = false;
    float mul[3] = { 1, 1, 1 }, add[3] = { 0, 0, 0 }, tint[3] = { 0, 0, 0 };
    float sat = 1, tint_amt = 0, contrast = 1, bright = 1;
};
struct EraDir {
    bool scanned = false;
    std::map<uint32_t, std::string> files;
    std::map<uint32_t, Grade> grades;
};
std::map<std::string, std::vector<uint32_t>> g_names;   // friendly name -> hashes
bool g_names_read;
std::map<std::string, EraDir> g_era_dirs;
std::vector<std::string> g_era;                         // the active folders, in search order; empty = off
// M9 (docs/eras/results/M9-era-looks.md): the Eras team-art folders, searched before g_era. On only
// while an Eras game is launched or an Eras depth chart is open (nfl2k5_eras_team_art_update):
//   mods\eras\teams\<season>\art\<HASH>.png   (card chips, the PHI/CHI style logos, the Bengals logo)
std::vector<std::string> g_teams;
volatile uint32_t g_era_gen = 1;
std::map<std::pair<std::string, uint32_t>, Pic> g_era_made;   // (folder, hash) -> graded or decoded image
size_t g_era_bytes;

std::string lower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
std::string trim_s(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

void read_names()
{
    if (g_names_read) return;
    g_names_read = true;
    FILE *f = fopen("mods\\eras\\offices\\office-textures.ini", "r");
    char line[2048];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        size_t c = l.find(';');
        if (c != std::string::npos) l = l.substr(0, c);
        size_t eq = l.find('=');
        if (eq == std::string::npos || l[0] == '[') continue;
        std::string k = lower(trim_s(l.substr(0, eq))), v = l.substr(eq + 1);
        for (size_t i = 0; i + 8 <= v.size(); i++) {
            uint32_t h;
            if ((i == 0 || v[i - 1] == ' ' || v[i - 1] == ',' || v[i - 1] == '\t') && hex8(v.c_str() + i, &h)) {
                if (std::find(g_names[k].begin(), g_names[k].end(), h) == g_names[k].end()) g_names[k].push_back(h);
                i += 7;
            }
        }
    }
    fclose(f);
}

bool parse_grade(const std::string &spec, Grade *g)
{
    std::string s = spec;
    for (char &c : s) if (c == ',') c = ' ';
    const char *p = s.c_str();
    char w[16];
    int n;
    while (sscanf(p, "%15s%n", w, &n) == 1) {
        p += n;
        std::string k = lower(w);
        float a = 0, b = 0, c = 0, d = 0;
        if (k == "sat" && sscanf(p, "%f%n", &a, &n) == 1) { g->sat = a; p += n; }
        else if (k == "contrast" && sscanf(p, "%f%n", &a, &n) == 1) { g->contrast = a; p += n; }
        else if (k == "bright" && sscanf(p, "%f%n", &a, &n) == 1) { g->bright = a; p += n; }
        else if (k == "mul" && sscanf(p, "%f %f %f%n", &a, &b, &c, &n) == 3) { g->mul[0] = a; g->mul[1] = b; g->mul[2] = c; p += n; }
        else if (k == "add" && sscanf(p, "%f %f %f%n", &a, &b, &c, &n) == 3) { g->add[0] = a; g->add[1] = b; g->add[2] = c; p += n; }
        else if (k == "tint" && sscanf(p, "%f %f %f %f%n", &a, &b, &c, &d, &n) == 4) {
            g->tint[0] = a; g->tint[1] = b; g->tint[2] = c; g->tint_amt = d; p += n;
        } else return false;
    }
    g->on = true;
    return true;
}

EraDir &era_dir(const std::string &name)
{
    EraDir &d = g_era_dirs[name];
    if (d.scanned) return d;
    d.scanned = true;
    read_names();
    // an office era name ("1980s"), or a full folder path (the M9 team-art folders)
    std::string dir = name.find('\\') != std::string::npos ? name + "\\" : "mods\\eras\\offices\\" + name + "\\";
    WIN32_FIND_DATAA fd;
    std::vector<std::string> files;
    HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    for (const std::string &n : files) {
        size_t dot = n.find_last_of('.');
        if (dot == std::string::npos) continue;
        std::string ext = lower(n.substr(dot)), base = lower(n.substr(0, dot));
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".dds") continue;
        uint32_t hv;
        if (n.size() >= 12 && hex8(n.c_str(), &hv)) { if (!d.files.count(hv)) d.files[hv] = dir + n; continue; }
        auto it = g_names.find(base);
        if (it != g_names.end()) for (uint32_t x : it->second) if (!d.files.count(x)) d.files[x] = dir + n;
    }
    FILE *f = fopen((dir + "era.ini").c_str(), "r");
    if (f) {
        char line[512];
        std::string sect;
        while (fgets(line, sizeof line, f)) {
            std::string l = line;
            size_t c = l.find(';');
            if (c != std::string::npos) l = l.substr(0, c);
            l = trim_s(l);
            if (l.empty()) continue;
            if (l[0] == '[') { sect = lower(l.substr(1, l.find(']') - 1)); continue; }
            size_t eq = l.find('=');
            if (sect != "grade" || eq == std::string::npos) continue;
            std::string k = lower(trim_s(l.substr(0, eq)));
            Grade g;
            if (!parse_grade(l.substr(eq + 1), &g)) { fprintf(stderr, "[TEXPACK] %sera.ini: bad grade '%s'\n", dir.c_str(), l.c_str()); continue; }
            std::vector<uint32_t> hs;
            uint32_t hv;
            if (k.size() == 8 && hex8(k.c_str(), &hv)) hs.push_back(hv);
            else if (g_names.count(k)) hs = g_names[k];
            else fprintf(stderr, "[TEXPACK] %sera.ini: unknown texture name '%s'\n", dir.c_str(), k.c_str());
            for (uint32_t x : hs) if (!d.grades.count(x)) d.grades[x] = g;
        }
        fclose(f);
    }
    fprintf(stderr, "[TEXPACK] era folder %s: %zu textures by file, %zu by grade\n", name.c_str(), d.files.size(), d.grades.size());
    return d;
}

inline uint8_t clamp8(float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }

void apply_grade(const Grade &g, const uint32_t *src, uint32_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t p = src[i];
        float c[3] = { (float)((p >> 16) & 255), (float)((p >> 8) & 255), (float)(p & 255) };   // r g b
        float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        for (int k = 0; k < 3; k++) {
            float v = l + (c[k] - l) * g.sat;
            if (g.tint_amt > 0) v = v * (1 - g.tint_amt) + l * 2.0f * g.tint[k] * g.tint_amt;
            v = (v - 128.0f) * g.contrast + 128.0f;
            v = v * g.mul[k] * g.bright + g.add[k] * 255.0f;
            c[k] = v;
        }
        dst[i] = (p & 0xFF000000u) | (uint32_t)clamp8(c[0]) << 16 | (uint32_t)clamp8(c[1]) << 8 | clamp8(c[2]);
    }
}

// Under g_lock. An era image for this texture, or nullptr.
const uint32_t *era_find(uint32_t content, const uint32_t *orig, uint32_t w, uint32_t h, uint32_t *ow, uint32_t *oh)
{
    std::vector<std::string> all = g_teams;
    all.insert(all.end(), g_era.begin(), g_era.end());
    for (const std::string &name : all) {
        EraDir &d = era_dir(name);
        auto f = d.files.find(content);
        auto g = d.grades.find(content);
        if (f == d.files.end() && (g == d.grades.end() || !orig)) continue;
        auto key = std::make_pair(name, content);
        auto it = g_era_made.find(key);
        if (it == g_era_made.end()) {
            Pic p;
            if (f != d.files.end()) {
                if (!decode(f->second, &p)) { fprintf(stderr, "[TEXPACK] cannot read %s\n", f->second.c_str()); d.files.erase(f); continue; }
                printf("[TEXPACK] era %s: %08X (%ux%u) -> %s (%ux%u)\n", name.c_str(), content, w, h, f->second.c_str(), p.w, p.h);
            } else {
                p.w = w; p.h = h;
                p.px.resize((size_t)w * h);
                apply_grade(g->second, orig, p.px.data(), p.px.size());
                printf("[TEXPACK] era %s: %08X (%ux%u) graded\n", name.c_str(), content, w, h);
            }
            if (g_era_bytes > (256u << 20)) { g_era_made.clear(); g_era_bytes = 0; }
            g_era_bytes += p.px.size() * 4;
            it = g_era_made.emplace(key, std::move(p)).first;
        }
        *ow = it->second.w; *oh = it->second.h;
        return it->second.px.data();
    }
    return nullptr;
}

}  // namespace

extern "C" int nfl2k5_texpack_era_on(void) { return !g_era.empty() || !g_teams.empty(); }

// M9: the Eras team-art context. season_dir ("1980") or NULL = off. Bumps the
// era generation on every change, so the textures upload again (retail art
// comes back the moment it is switched off).
extern "C" void nfl2k5_texpack_eras_teams(const char *season_dir)
{
    std::vector<std::string> v;
    if (season_dir && *season_dir) v.push_back(std::string("mods\\eras\\teams\\") + season_dir + "\\art");
    if (!g_ready) return;
    EnterCriticalSection(&g_lock);
    if (v != g_teams) {
        g_teams = v;
        g_era_gen++;
        fprintf(stderr, "[TEXPACK] eras team art %s (gen %u)\n", v.empty() ? "off" : v[0].c_str(), (unsigned)g_era_gen);
    }
    LeaveCriticalSection(&g_lock);
}
extern "C" uint32_t nfl2k5_texpack_gen(void) { return g_era_gen; }

// season_dir ("1980") and decade_dir ("1980s") may be NULL; both NULL = off.
extern "C" void nfl2k5_texpack_era(const char *season_dir, const char *decade_dir)
{
    std::vector<std::string> v;
    if (season_dir && *season_dir) v.push_back(season_dir);
    if (decade_dir && *decade_dir) v.push_back(decade_dir);
    if (!v.empty()) v.push_back("default");
    if (!g_ready) return;
    EnterCriticalSection(&g_lock);
    if (v != g_era) {
        g_era = v;
        g_era_gen++;
        if (getenv("NFL2K5_ERAS_LOG")) {
            std::string all;
            for (const std::string &x : v) all += (all.empty() ? "" : ",") + x;
            fprintf(stderr, "[TEXPACK] era context %s (gen %u)\n", v.empty() ? "off" : all.c_str(), (unsigned)g_era_gen);
        }
    }
    LeaveCriticalSection(&g_lock);
}

extern "C" int nfl2k5_texpack_active(void) { return g_ready && (g_dump || !g_files.empty() || !g_era.empty() || !g_teams.empty()); }

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
    if (!g_era.empty() || !g_teams.empty()) {
        r = era_find(content, orig, w, h, ow, oh);
        if (getenv("NFL2K5_ERAS_TEXLOG"))
            fprintf(stderr, "[TEXPACK] era gen %u: %08X %ux%u fmt %02X %s\n", (unsigned)g_era_gen, content, w, h, fmt, r ? "replaced" : "kept");
        if (r) { LeaveCriticalSection(&g_lock); return r; }
    }
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
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) g_wic = nullptr;
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

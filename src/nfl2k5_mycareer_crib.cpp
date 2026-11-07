/* The Crib's own items for the MyCareer home, read from the player's disc.
 *
 * mc_crib_model("crib_couch.iff#model2", team, path, n) gives the path of a glTF model of that
 * Crib item in <root>\cache\mycareer-crib (the per-user cache in the per-user layout), writing
 * it the first time from the disc archive (vc_53450030, the same volumes and disc overlay
 * portraits.cpp reads). Nothing is stored in the repository or a release: the cache is made on
 * the player's machine from the player's disc.
 *
 * Where the items live on the disc:
 *  - furniture: crib_<kind>.iff is a CACR record naming a streaming file (crib_<kind>.cdf, its
 *    slot count at +0x60); the .cdf is that many equal slots, each one SCNE resource
 *    ("<kind>_NN"; bobbleheads "NN_bobblehead_hi"). "#modelN" picks <kind>_NN.
 *  - room pieces: crib.iff holds the Crib room (SCNE "room": 79 shapes in Crib space, identity
 *    node matrices) and one SCNE per big piece (air_hockey, chopper, fish_tank, cap, ...).
 *    "crib.iff:<name>" is that scene, else the room's shape of that name, else the room's
 *    submeshes whose material has that name; "+" joins several.
 *  - looks: posters, banners, pennants, standees, rugs, photos and signs have no texture of
 *    their own; the Crib streams one by name from crib_16k/32k/64k.cdf (TXTR slots):
 *    "<kind>_NN" (generic looks) or "TT_<kind>_NN" (TT = the team). "#lookN" picks NN.
 *
 * Formats (read from the executable and the 2K Football Mod Tools' proved readers, MIT):
 * resources are a 0x20-byte span header (fourcc, stored, system bytes, video bytes, 0xFEEDBEEF
 * when VC-LZ compressed) and a body; pointers inside are one-based and self-relative; a shape
 * (0x100) has 16 vertex registers at +0x84 over 8 streams (+0xC4 strides, +0xD4 pointers),
 * submeshes (0x80: material, +0x78 NV2A push words); materials (0x80) a colour at +0x18 and a
 * texture at +0x30; textures here are P8, swizzled. Crib units are half centimetres; the
 * Crib's axes are right-handed, as glTF's (a picture reads the right way round from
 * -(dP/du x dP/dv)). Each model is written with its front towards glTF -z, which the home
 * (left-handed, z negated) sees as +z: a slot's face 0. */
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <memory>
#include <array>
#include <tuple>
#include <functional>

#ifdef MC_CRIB_CLI
static const char *xbox_user_dir(void) { return ""; }
static int xbox_pack_active(void) { return 0; }
static HANDLE xbox_pack_open(const WCHAR *) { return INVALID_HANDLE_VALUE; }
static void xbox_pack_close(HANDLE) {}
static int xbox_pack_read(HANDLE, void *, ULONG, const LARGE_INTEGER *, ULONG *) { return 0; }
static void xbox_overlay_attach(HANDLE, const WCHAR *) {}
static void xbox_overlay_detach(HANDLE) {}
static int xbox_overlay_read(HANDLE, void *, ULONG, const LARGE_INTEGER *, ULONG *) { return 0; }
#else
extern "C" const char *xbox_user_dir(void);
extern "C" void xbox_overlay_attach(HANDLE h, const WCHAR *path);
extern "C" void xbox_overlay_detach(HANDLE h);
extern "C" int xbox_overlay_read(HANDLE h, void *buf, ULONG len, const LARGE_INTEGER *off, ULONG *got);
extern "C" int xbox_pack_active(void);
extern "C" HANDLE xbox_pack_open(const WCHAR *win_path);
extern "C" void xbox_pack_close(HANDLE h);
extern "C" int xbox_pack_read(HANDLE h, void *buf, ULONG len, const LARGE_INTEGER *off, ULONG *got);
#endif

namespace {

constexpr float kUnit = 0.005f;                   /* metres per Crib unit */
std::recursive_mutex g_lock;

/* ---- the disc archive ------------------------------------------------------------------ */
struct Archive {
    std::string dir;
    bool flat = false, ok = false;
    unsigned count = 0;
    uint64_t start[36] = {}, size[36] = {};
    HANDLE h[36];
    bool overlay[36] = {};
    std::map<uint32_t, std::pair<uint64_t, uint32_t>> entries;     /* name id -> (offset, size) */
    Archive() { for (auto &x : h) x = INVALID_HANDLE_VALUE; }
    ~Archive() {
        for (unsigned i = 0; i < 36; i++)
            if (h[i] != INVALID_HANDLE_VALUE) { if (overlay[i]) xbox_overlay_detach(h[i]); xbox_pack_close(h[i]); CloseHandle(h[i]); }
    }
    bool read_volume(unsigned ordinal, uint64_t offset, void *out, size_t n) {
        if (ordinal >= 36 || n > MAXDWORD) return false;
        if (h[ordinal] == INVALID_HANDLE_VALUE) {
            std::string path = dir + "/" + "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[ordinal];
            for (char &c : path) if (c == '/') c = '\\';
            h[ordinal] = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            WCHAR wide[MAX_PATH];
            if (h[ordinal] == INVALID_HANDLE_VALUE && MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide, MAX_PATH))
                h[ordinal] = xbox_pack_open(wide);
            if (h[ordinal] == INVALID_HANDLE_VALUE) return false;
            if (!flat && MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide, MAX_PATH)) {
                xbox_overlay_attach(h[ordinal], wide);
                overlay[ordinal] = true;
            }
        }
        LARGE_INTEGER position; position.QuadPart = (LONGLONG)offset;
        ULONG got = 0;
        if (overlay[ordinal] && xbox_overlay_read(h[ordinal], out, (ULONG)n, &position, &got)) return got == n;
        if (xbox_pack_read(h[ordinal], out, (ULONG)n, &position, &got)) return got == n;
        DWORD rd = 0;
        return SetFilePointerEx(h[ordinal], position, nullptr, FILE_BEGIN) && ReadFile(h[ordinal], out, (DWORD)n, &rd, nullptr) && rd == n;
    }
    bool read(uint64_t position, void *out, size_t n) {
        uint8_t *o = (uint8_t *)out;
        while (n) {
            unsigned v = 0;
            while (v < count && position >= start[v] + size[v]) ++v;
            if (v >= count || position < start[v]) return false;
            uint64_t local = position - start[v];
            size_t take = (size_t)std::min<uint64_t>(n, size[v] - local);
            if (!read_volume(v, local, o, take)) return false;
            o += take; position += take; n -= take;
        }
        return true;
    }
    bool open(const std::string &d, bool is_flat) {
        dir = d; flat = is_flat;
        uint32_t header[39];
        if (!read_volume(0, 0, header, sizeof header) || !header[0] || header[0] > 100000 || header[1] || !header[2] || header[2] > 36)
            return false;
        count = header[2];
        uint64_t total = 0;
        for (unsigned i = 0; i < count; i++) { if (!header[3 + i]) return false; start[i] = total; size[i] = (uint64_t)header[3 + i] * 2048u; total += size[i]; }
        std::vector<uint32_t> e((size_t)header[0] * 3);
        if (!read_volume(0, sizeof header, e.data(), e.size() * 4)) return false;
        for (uint32_t i = 0; i < header[0]; i++) entries[e[i * 3]] = { (uint64_t)e[i * 3 + 2] * 2048u, e[i * 3 + 1] };
        ok = true;
        return true;
    }
};

std::vector<std::unique_ptr<Archive>> g_archives;
bool g_opened;

uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}

uint32_t name_id(const std::string &name)
{
    std::vector<uint8_t> w;
    for (char ch : name) { w.push_back((uint8_t)toupper((unsigned char)ch)); w.push_back(0); }
    return crc32(w.data(), w.size());
}

void open_archives()
{
    if (g_opened) return;
    g_opened = true;
    const char *pack = getenv("NFL2K5_PACK");
    if (pack && *pack) {
        std::string path = std::string(pack) + "/vc_53450030";
        if (GetFileAttributesA((path + "/0").c_str()) != INVALID_FILE_ATTRIBUTES) {
            auto a = std::make_unique<Archive>();
            if (a->open(path, true)) g_archives.push_back(std::move(a));
        }
    }
    const char *dirs[] = { "original/disc/vc_53450030", "original/vc_53450030" };
    for (const char *d : dirs)
        if (GetFileAttributesA((std::string(d) + "/0").c_str()) != INVALID_FILE_ATTRIBUTES) {
            auto a = std::make_unique<Archive>();
            if (a->open(d, false)) g_archives.push_back(std::move(a));     /* both: a partial overlay folder falls through */
        }
    if (g_archives.empty() && xbox_pack_active()) {
        auto a = std::make_unique<Archive>();
        if (a->open("original/vc_53450030", false)) g_archives.push_back(std::move(a));
    }
    fprintf(stderr, "[MCCRIB] %zu disc archive(s)\n", g_archives.size());
}

/* A whole file of the archive, or a range of it (size 0 = to its end). */
bool read_file(const std::string &name, std::vector<uint8_t> &out, uint64_t off = 0, uint32_t n = 0, uint32_t *total = nullptr)
{
    open_archives();
    uint32_t id = name_id(name);
    for (auto &a : g_archives) {
        auto it = a->entries.find(id);
        if (it == a->entries.end()) continue;
        if (total) *total = it->second.second;
        if (off > it->second.second) return false;
        uint32_t len = n ? std::min<uint32_t>(n, (uint32_t)(it->second.second - off)) : (uint32_t)(it->second.second - off);
        out.resize(len);
        if (a->read(it->second.first + off, out.data(), len)) return true;
    }
    return false;
}

/* ---- resources ------------------------------------------------------------------------- */
uint32_t u32(const uint8_t *b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
uint16_t u16(const uint8_t *b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }

bool vclz(const uint8_t *s, size_t n, std::vector<uint8_t> &out, uint32_t expect)
{
    if (n < 10) return false;
    uint32_t size = u32(s, 0);
    int ob = s[8];
    if (size != expect || ob < 1 || ob > 15) return false;
    uint32_t dmask = (1u << ob) - 1, lmask = (1u << (16 - ob)) - 1;
    out.assign(size, 0);
    size_t src = 9, dst = 0;
    uint8_t flags = s[src++], fm = 1;
    while (dst < size) {
        if (flags & fm) {
            if (src + 2 > n) return false;
            uint16_t code = u16(s, src); src += 2;
            uint32_t dist = code & dmask, len = ((code >> ob) & lmask) + 3;
            if (!dist || dist > dst || dst + len > size) return false;
            for (int i = (int)len - 1; i >= 0; i--) out[dst + i] = out[dst - dist + i];
            dst += len;
        } else {
            if (src >= n) return false;
            out[dst++] = s[src++];
        }
        fm <<= 1;
        if (!fm && dst < size) { if (src >= n) return false; flags = s[src++]; fm = 1; }
    }
    return true;
}

struct Res { char kind[5] = {}; uint32_t sys = 0; std::vector<uint8_t> b; };

/* One resource from a span at p (0x20 header + body); next gets the following span. */
bool res_decode(const uint8_t *p, size_t avail, Res &r, size_t *next = nullptr)
{
    if (avail < 0x20) return false;
    memcpy(r.kind, p, 4); r.kind[4] = 0;
    for (int i = 0; i < 4; i++) if (r.kind[i] < 0x20 || r.kind[i] > 0x7E) return false;
    uint32_t stored = u32(p, 4), sys = u32(p, 8), vid = u32(p, 12), magic = u32(p, 16);
    if (!stored || 0x20 + (size_t)stored > avail) return false;
    if (next) *next = 0x20 + (size_t)stored;
    r.sys = sys;
    if (magic == 0xFEEDBEEFu) return vclz(p + 0x20, stored, r.b, sys + vid);
    if ((size_t)sys + vid > stored) { r.b.assign(p + 0x20, p + 0x20 + stored); return true; }
    r.b.assign(p + 0x20, p + 0x20 + sys + vid);
    return true;
}

struct View {
    const uint8_t *b; size_t sys, size;
    long rel(size_t o) const { int32_t v; memcpy(&v, b + o, 4); return v ? (long)(o - 1 + v) : -1; }
    std::string wstr(long o) const {
        std::string s;
        if (o < 0) return s;
        for (size_t i = (size_t)o; i + 1 < sys; i += 2) { uint16_t c = u16(b, i); if (!c) break; s += (char)(c < 128 ? c : '?'); }
        return s;
    }
};

/* ---- textures -------------------------------------------------------------------------- */
struct Image { int w = 0, h = 0; std::vector<uint32_t> px; };    /* RGBA8, little-endian (R in the low byte) */

void unswizzle_index(int w, int h, std::vector<uint32_t> &map)
{
    uint32_t mx = 0, my = 0, bit = 1;
    for (int d = 1; d < w || d < h; d <<= 1) { if (d < w) { mx |= bit; bit <<= 1; } if (d < h) { my |= bit; bit <<= 1; } }
    map.resize((size_t)w * h);
    uint32_t sy = 0;
    for (int y = 0; y < h; y++) {
        uint32_t sx = 0;
        for (int x = 0; x < w; x++) { map[(size_t)y * w + x] = sx | sy; sx = (sx - mx) & mx; }
        sy = (sy - my) & my;
    }
}

uint32_t bgra(const uint8_t *c) { return (uint32_t)c[2] | (uint32_t)c[1] << 8 | (uint32_t)c[0] << 16 | (uint32_t)c[3] << 24; }

void rgb565(uint16_t v, uint8_t *o) { o[0] = (uint8_t)((v >> 11 & 31) * 255 / 31); o[1] = (uint8_t)((v >> 5 & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); }

/* The texture whose descriptor (unknown, pixels, palette, format, size, flags) is at desc. */
bool tex_decode(const View &v, size_t desc, Image &img)
{
    if (desc + 0x18 > v.sys) return false;
    uint32_t pix = u32(v.b, desc + 4), pal = u32(v.b, desc + 8), fmt = u32(v.b, desc + 12), sz = u32(v.b, desc + 16);
    uint32_t code = fmt >> 8 & 0xFF;
    int w, h;
    if (!sz) { w = 1 << (fmt >> 20 & 15); h = 1 << (fmt >> 24 & 15); }
    else if (code == 0x7F) { w = sz >> 16; h = sz & 0xFFFF; }
    else { w = sz & 0xFFFF; h = sz >> 16; }
    if ((fmt >> 4 & 15) != 2 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    const uint8_t *vid = v.b + v.sys;
    size_t vsize = v.size - v.sys, n = (size_t)w * h;
    img.w = w; img.h = h; img.px.assign(n, 0xFFFFFFFFu);
    std::vector<uint32_t> map;
    if (code == 0x0B || code == 0x7F) {
        if (pix + n > vsize || pal + 1024 > vsize) return false;
        if (code == 0x0B) unswizzle_index(w, h, map);
        for (size_t i = 0; i < n; i++) img.px[i] = bgra(vid + pal + 4u * vid[pix + (code == 0x0B ? map[i] : i)]);
        return true;
    }
    if (code == 0x06 || code == 0x07 || code == 0x02 || code == 0x05 || code == 0x04) {
        int bpp = code >= 0x06 ? 4 : 2;
        if (pix + n * bpp > vsize) return false;
        if (!sz) unswizzle_index(w, h, map);
        for (size_t i = 0; i < n; i++) {
            const uint8_t *c = vid + pix + (sz ? i : map[i]) * bpp;
            uint8_t o[4] = { 0, 0, 0, 255 };
            if (bpp == 4) { o[0] = c[2]; o[1] = c[1]; o[2] = c[0]; o[3] = code == 0x07 ? 255 : c[3]; }
            else {
                uint16_t x = u16(c, 0);
                if (code == 0x05) rgb565(x, o);
                else if (code == 0x02) { o[0] = (uint8_t)((x >> 10 & 31) * 255 / 31); o[1] = (uint8_t)((x >> 5 & 31) * 255 / 31); o[2] = (uint8_t)((x & 31) * 255 / 31); o[3] = x & 0x8000 ? 255 : 0; }
                else { o[0] = (uint8_t)((x >> 8 & 15) * 17); o[1] = (uint8_t)((x >> 4 & 15) * 17); o[2] = (uint8_t)((x & 15) * 17); o[3] = (uint8_t)((x >> 12) * 17); }
            }
            memcpy(&img.px[i], o, 4);
        }
        return true;
    }
    if (code == 0x0C || code == 0x0E || code == 0x0F) {
        int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = code == 0x0C ? 8 : 16;
        if (pix + (size_t)bw * bh * bs > vsize) return false;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t *blk = vid + pix + ((size_t)by * bw + bx) * bs, *cb = blk + (bs - 8);
                uint16_t c0 = u16(cb, 0), c1 = u16(cb, 2);
                uint32_t sel = u32(cb, 4);
                uint8_t pal4[4][4];
                rgb565(c0, pal4[0]); rgb565(c1, pal4[1]);
                for (int k = 0; k < 3; k++) {
                    if (c0 > c1 || bs == 16) { pal4[2][k] = (uint8_t)((2 * pal4[0][k] + pal4[1][k]) / 3); pal4[3][k] = (uint8_t)((pal4[0][k] + 2 * pal4[1][k]) / 3); }
                    else { pal4[2][k] = (uint8_t)((pal4[0][k] + pal4[1][k]) / 2); pal4[3][k] = 0; }
                }
                pal4[0][3] = pal4[1][3] = pal4[2][3] = 255; pal4[3][3] = (c0 > c1 || bs == 16) ? 255 : 0;
                uint8_t a8[16];
                for (int k = 0; k < 16; k++) a8[k] = 255;
                if (code == 0x0E) for (int k = 0; k < 16; k++) a8[k] = (uint8_t)(((blk[k / 2] >> (4 * (k & 1))) & 15) * 17);
                if (code == 0x0F) {
                    uint8_t a0 = blk[0], a1 = blk[1], ap[8] = { a0, a1 };
                    for (int k = 2; k < 8; k++) ap[k] = a0 > a1 ? (uint8_t)(((8 - k) * a0 + (k - 1) * a1) / 7) : k < 6 ? (uint8_t)(((6 - k) * a0 + (k - 1) * a1) / 5) : (k == 6 ? 0 : 255);
                    uint64_t bits = 0;
                    for (int k = 0; k < 6; k++) bits |= (uint64_t)blk[2 + k] << (8 * k);
                    for (int k = 0; k < 16; k++) a8[k] = ap[bits >> (3 * k) & 7];
                }
                for (int k = 0; k < 16; k++) {
                    int x = bx * 4 + (k & 3), y = by * 4 + (k >> 2);
                    if (x >= w || y >= h) continue;
                    uint8_t o[4];
                    memcpy(o, pal4[sel >> (2 * k) & 3], 4);
                    if (bs == 16) o[3] = a8[k];
                    memcpy(&img.px[(size_t)y * w + x], o, 4);
                }
            }
        return true;
    }
    fprintf(stderr, "[MCCRIB] texture format 0x%02X not read\n", code);
    return false;
}

/* ---- streaming files (CACR -> .cdf slots) ---------------------------------------------- */
struct Cdf { std::string file; uint32_t count = 0, stride = 0; std::vector<std::string> names; bool ok = false; };
std::map<std::string, Cdf> g_cdfs;

Cdf &cdf_open(const std::string &iff)
{
    Cdf &c = g_cdfs[iff];
    if (c.ok || !c.file.empty()) return c;
    std::vector<uint8_t> h;
    c.file = "-";
    if (!read_file(iff, h) || h.size() < 0x64 || memcmp(h.data(), "CACR", 4)) return c;
    std::string f;
    for (size_t i = 0x20; i + 1 < 0x60; i += 2) { uint16_t ch = u16(h.data(), i); if (!ch) break; f += (char)ch; }
    uint32_t total = 0;
    std::vector<uint8_t> probe;
    c.count = u32(h.data(), 0x60);
    if (!c.count || !read_file(f, probe, 0, 16, &total) || total % c.count) return c;
    c.file = f; c.stride = total / c.count;
    for (uint32_t i = 0; i < c.count; i++) {
        std::vector<uint8_t> s;
        std::string nm;
        if (read_file(f, s, (uint64_t)i * c.stride, 0xC0) && s.size() >= 0xC0)
            for (size_t k = 0x40; k + 1 < 0xC0; k += 2) { uint16_t ch = u16(s.data(), k); if (!ch) break; nm += (char)ch; }
        c.names.push_back(nm);
    }
    c.ok = true;
    return c;
}

bool cdf_slot(const std::string &iff, const std::string &name, Res &r)
{
    Cdf &c = cdf_open(iff);
    if (!c.ok) return false;
    for (uint32_t i = 0; i < c.count; i++)
        if (!_stricmp(c.names[i].c_str(), name.c_str())) {
            std::vector<uint8_t> s;
            return read_file(c.file, s, (uint64_t)i * c.stride, c.stride) && res_decode(s.data(), s.size(), r);
        }
    return false;
}

/* A named Crib look: TXTR from the streaming caches. */
bool look_texture(const std::string &name, Image &img)
{
    /* 64k last: its generic "<kind>_NN" are the catalogue's thumbnails (a blue card) */
    static const char *const caches[] = { "crib_32k.iff", "crib_16k.iff", "crib_64k.iff" };
    for (const char *c : caches) {
        Res r;
        if (!cdf_slot(c, name, r) || strcmp(r.kind, "TXTR")) continue;
        View v{ r.b.data(), r.sys, r.b.size() };
        long d = v.rel(0x14);
        if (d >= 0 && tex_decode(v, (size_t)d, img)) return true;
    }
    return false;
}

/* ---- crib.iff ---------------------------------------------------------------------------- */
std::vector<uint8_t> g_crib;
std::map<std::string, Res> g_crib_scenes;
bool g_crib_read;

void crib_read()
{
    if (g_crib_read) return;
    g_crib_read = true;
    if (!read_file("crib.iff", g_crib)) { fprintf(stderr, "[MCCRIB] crib.iff not found\n"); return; }
    size_t off = 0, next = 0;
    while (off + 0x20 <= g_crib.size()) {
        Res r;
        if (!memcmp(g_crib.data() + off, "SCNE", 4)) {
            if (!res_decode(g_crib.data() + off, g_crib.size() - off, r, &next)) break;
            View v{ r.b.data(), r.sys, r.b.size() };
            std::string nm = v.wstr(v.rel(0x10));
            if (!nm.empty()) g_crib_scenes[nm] = std::move(r);
        } else {
            uint32_t stored = u32(g_crib.data(), off + 4);
            if (!stored) break;
            next = 0x20 + (size_t)stored;
        }
        off += next;
    }
    fprintf(stderr, "[MCCRIB] crib.iff: %zu scenes\n", g_crib_scenes.size());
}

/* ---- geometry ------------------------------------------------------------------------------ */
struct Vtx { float p[3], n[3], uv[2]; };
struct Prim { std::string mat; uint32_t colour = 0xFFFFFFFFu; int tex = -1; bool look = false; std::vector<Vtx> v; };   /* a triangle list */
struct Model { std::vector<Prim> prims; std::vector<Image> tex; };

float nshort(int16_t v) { return v >= 0 ? v / 32767.0f : v / 32768.0f; }

void unpack3(uint32_t w, float *n)
{
    int x = w & 0x7FF, y = w >> 11 & 0x7FF, z = w >> 22 & 0x3FF;
    if (x & 0x400) x -= 0x800;
    if (y & 0x400) y -= 0x800;
    if (z & 0x200) z -= 0x400;
    n[0] = x / 1023.0f; n[1] = y / 1023.0f; n[2] = z / 511.0f;
}

bool skip_material(const std::string &m)
{
    std::string l = m;
    for (char &c : l) c = (char)tolower((unsigned char)c);
    return l.find("shadow") != std::string::npos || l.find("reflect") != std::string::npos || l.find("scissor") != std::string::npos;
}

/* Adds the shape at sh (or only its submeshes with material `only`) to the model. */
void add_shape(const View &v, size_t sh, Model &m, std::map<long, int> &texmap, const char *only)
{
    const uint8_t *b = v.b;
    long desc = v.rel(0x14);
    long ttab = v.rel((size_t)desc + 0x18), mtab = v.rel((size_t)desc + 0x20);
    uint32_t nmat = u32(b, (size_t)desc + 0x1C);
    uint32_t nv = u16(b, sh + 0x4C), nsub = u16(b, sh + 0x54);
    float pscale, poff[3], uvc[4];
    memcpy(&pscale, b + sh + 0x10, 4); memcpy(poff, b + sh + 0x20, 12); memcpy(uvc, b + sh + 0x30, 16);
    uint16_t strides[8]; long soff[8];
    for (int k = 0; k < 8; k++) { strides[k] = u16(b, sh + 0xC4 + k * 2); soff[k] = v.rel(sh + 0xD4 + k * 4); }
    uint32_t reg[16];
    for (int r = 0; r < 16; r++) reg[r] = u32(b, sh + 0x84 + r * 4);
    auto at = [&](int r, uint32_t i) -> const uint8_t * {
        uint32_t e = reg[r], s = e >> 8 & 0xFF;
        if (!e || (e & 0xFF) == 0x02 || s >= 8 || soff[s] < 0) return nullptr;
        size_t o = (size_t)soff[s] + (size_t)i * strides[s] + (e >> 16);
        return o + 16 <= v.sys ? b + o : nullptr;
    };
    std::vector<Vtx> vert(nv);
    for (uint32_t i = 0; i < nv; i++) {
        Vtx &x = vert[i];
        memset(&x, 0, sizeof x);
        const uint8_t *p = at(0, i);
        if (!p) return;
        if ((reg[0] & 0xFF) == 0x32) memcpy(x.p, p, 12);
        else if ((reg[0] & 0xFF) == 0x31) for (int a = 0; a < 3; a++) x.p[a] = nshort((int16_t)u16(p, a * 2)) * pscale + poff[a];
        else return;
        if ((p = at(2, i)) != nullptr && (reg[2] & 0xFF) == 0x16) unpack3(u32(p, 0), x.n);
        if ((p = at(6, i)) != nullptr) {
            if ((reg[6] & 0xFF) == 0x21) { x.uv[0] = nshort((int16_t)u16(p, 0)) * uvc[0] + uvc[2]; x.uv[1] = nshort((int16_t)u16(p, 2)) * uvc[1] + uvc[3]; }
            else if ((reg[6] & 0xFF) == 0x22) memcpy(x.uv, p, 8);
        }
    }
    long stab = v.rel(sh + 0x70);
    for (uint32_t k = 0; k < nsub && stab >= 0; k++) {
        size_t so = (size_t)stab + k * 0x80;
        uint16_t mi = u16(b, so);
        if (mi >= nmat) continue;
        size_t mo = (size_t)mtab + mi * 0x80;
        std::string mname = v.wstr(v.rel(mo));
        if (only && _stricmp(mname.c_str(), only)) continue;
        if (skip_material(mname)) continue;
        if ((u32(b, mo + 0x18) >> 24) < 0x80) continue;          /* see-through (case glass): not drawn by us */
        if (!_strnicmp(mname.c_str(), "z_glass", 7) || !_stricmp(mname.c_str(), "teamlogo")) continue;
        Prim pr;
        pr.mat = mname;
        pr.colour = u32(b, mo + 0x18);
        long t = v.rel(mo + 0x30);
        if (t >= 0 && ttab >= 0) {
            auto it = texmap.find(t);
            if (it == texmap.end()) {
                Image img;
                int id = -1;
                if (tex_decode(v, (size_t)t, img)) { id = (int)m.tex.size(); m.tex.push_back(std::move(img)); }
                it = texmap.emplace(t, id).first;
            }
            pr.tex = it->second;
        }
        long c = v.rel(so + 0x78);
        uint32_t words = u16(b, so + 0x7C);
        if (c < 0 || (size_t)c + words * 4 > v.sys) continue;
        size_t cur = (size_t)c, end = cur + words * 4;
        uint32_t mode = 0;
        std::vector<uint32_t> idx;
        auto flush = [&]() {
            auto tri = [&](uint32_t a, uint32_t bb, uint32_t cc) {
                if (a == bb || bb == cc || a == cc || a >= nv || bb >= nv || cc >= nv) return;
                pr.v.push_back(vert[a]); pr.v.push_back(vert[bb]); pr.v.push_back(vert[cc]);
            };
            size_t n = idx.size();
            if (mode == 5) for (size_t i = 0; i + 2 < n; i += 3) tri(idx[i], idx[i + 1], idx[i + 2]);
            else if (mode == 6) for (size_t i = 0; i + 2 < n; i++) { if (i & 1) tri(idx[i + 1], idx[i], idx[i + 2]); else tri(idx[i], idx[i + 1], idx[i + 2]); }
            else if (mode == 7 || mode == 10) for (size_t i = 1; i + 1 < n; i++) tri(idx[0], idx[i], idx[i + 1]);
            else if (mode == 8) for (size_t i = 0; i + 3 < n; i += 4) { tri(idx[i], idx[i + 1], idx[i + 2]); tri(idx[i], idx[i + 2], idx[i + 3]); }
            else if (mode == 9) for (size_t i = 0; i + 3 < n; i += 2) { tri(idx[i], idx[i + 1], idx[i + 2]); tri(idx[i + 1], idx[i + 3], idx[i + 2]); }
            idx.clear(); mode = 0;
        };
        while (cur < end) {
            uint32_t hd = u32(b, cur); cur += 4;
            uint32_t meth = hd & 0x1FFC, cnt = hd >> 18 & 0x7FF;
            if ((hd & 0xE0030003u) != 0 && (hd & 0xE0030003u) != 0x40000000u) break;
            if (cur + cnt * 4 > end) break;
            for (uint32_t q = 0; q < cnt; q++) {
                uint32_t a = u32(b, cur + q * 4);
                if (meth == 0x17FC) { flush(); mode = a; }
                else if (meth == 0x1800) { idx.push_back(a & 0xFFFF); idx.push_back(a >> 16); }
                else if (meth == 0x1808) idx.push_back(a);
                else if (meth == 0x1810) for (uint32_t z = 0; z <= (a >> 24); z++) idx.push_back((a & 0xFFFFFF) + z);
            }
            cur += cnt * 4;
        }
        flush();
        if (!pr.v.empty()) m.prims.push_back(std::move(pr));
    }
}

/* Every shape of a scene (named: only that shape; material: only those submeshes). */
int add_scene(const Res &r, Model &m, const char *shape, const char *material)
{
    View v{ r.b.data(), r.sys, r.b.size() };
    long desc = v.rel(0x14);
    if (desc < 0 || (size_t)desc + 0x54 > v.sys) return 0;
    uint32_t ns = u32(v.b, (size_t)desc + 0x2C);
    long st = v.rel((size_t)desc + 0x30);
    std::map<long, int> texmap;
    size_t before = m.prims.size();
    for (uint32_t i = 0; i < ns && st >= 0; i++) {
        size_t sh = (size_t)st + i * 0x100;
        if (shape && _stricmp(v.wstr(v.rel(sh + 0x40)).c_str(), shape)) continue;
        if (!shape && !material && skip_material(v.wstr(v.rel(sh + 0x40)))) continue;
        add_shape(v, sh, m, texmap, material);
    }
    return (int)(m.prims.size() - before);
}

/* ---- the item's look and pose ------------------------------------------------------------ */
/* the untextured material's look kind: poster_01 -> poster, rug_m_01 -> rug ... */
std::string look_kind(const std::string &mat)
{
    static const char *const k[][2] = {
        { "poster", "poster" }, { "banner", "banner" }, { "pennant", "pennant" }, { "standee", "standee" },
        { "rug", "rug" }, { "sign_parking", "sign_parking" }, { "sign_street", "sign_street" },
        { "sign_interstate", "sign_interstate" }, { "license", "license_plate" }, { "wall_art_player", "painting" },
        { "photo", "photo" }, { "team_photo", "photo" }, { "foam_finger", "foam_finger" }, { "helmet_mug", "helmet_mug" },
        { "jersey", "jersey" }, { "game_ball", "game_ball" }, { "bobblehead", "bobblehead" },
        { "cjersey", "photo" }, { "gb_skin", "game_ball" }, { "helmeta", "helmet" },
    };
    std::string l = mat;
    for (char &c : l) c = (char)tolower((unsigned char)c);
    for (auto &e : k) if (!l.compare(0, strlen(e[0]), e[0])) return e[1];
    return "";
}

bool find_look(const std::string &kind, int look, int team, Image &img, std::string *used)
{
    char nm[96];
    std::vector<std::string> tries;
    if (look >= 0) {                                /* the team's own picture first, then a generic one */
        snprintf(nm, sizeof nm, "%02d_%s_%02d", team, kind.c_str(), look); tries.push_back(nm);
        snprintf(nm, sizeof nm, "%s_%02d", kind.c_str(), look); tries.push_back(nm);
    }
    snprintf(nm, sizeof nm, "%02d_%s", team, kind.c_str()); tries.push_back(nm);
    snprintf(nm, sizeof nm, "%02d_%s_00", team, kind.c_str()); tries.push_back(nm);
    for (auto &t : tries) if (look_texture(t, img)) { if (used) *used = t; return true; }
    return false;
}

void bounds(const Model &m, float lo[3], float hi[3])
{
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (auto &p : m.prims) for (auto &x : p.v) for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], x.p[a]); hi[a] = std::max(hi[a], x.p[a]); }
}

/* Room pieces that are several copies around the Crib (pots, sconces, art): keep the piece
 * with the most triangles. Triangles sharing a corner are one piece; pieces whose boxes come
 * within 30 units of each other are one piece too. */
void keep_one_cluster(Model &m)
{
    struct Tri { int prim; size_t i; };
    std::vector<Tri> t;
    for (int p = 0; p < (int)m.prims.size(); p++)
        for (size_t i = 0; i + 2 < m.prims[p].v.size(); i += 3) t.push_back({ p, i });
    if (t.size() < 2 || t.size() > 60000) return;
    std::vector<int> up(t.size());
    for (size_t i = 0; i < up.size(); i++) up[i] = (int)i;
    std::function<int(int)> root = [&](int x) { while (up[x] != x) { up[x] = up[up[x]]; x = up[x]; } return x; };
    auto join = [&](int a, int b) { a = root(a); b = root(b); if (a != b) up[a] = b; };
    std::map<std::tuple<int, int, int>, int> corner;
    for (int i = 0; i < (int)t.size(); i++)
        for (int k = 0; k < 3; k++) {
            const float *q = m.prims[t[i].prim].v[t[i].i + k].p;
            auto key = std::make_tuple((int)lroundf(q[0]), (int)lroundf(q[1]), (int)lroundf(q[2]));
            auto it = corner.find(key);
            if (it == corner.end()) corner.emplace(key, i); else join(i, it->second);
        }
    for (int pass = 0; pass < 8; pass++) {
        std::map<int, std::array<float, 6>> box;
        for (int i = 0; i < (int)t.size(); i++) {
            auto it = box.find(root(i));
            if (it == box.end()) it = box.emplace(root(i), std::array<float, 6>{ 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f }).first;
            for (int k = 0; k < 3; k++) {
                const float *q = m.prims[t[i].prim].v[t[i].i + k].p;
                for (int a = 0; a < 3; a++) { it->second[a] = std::min(it->second[a], q[a]); it->second[3 + a] = std::max(it->second[3 + a], q[a]); }
            }
        }
        bool merged = false;
        std::vector<std::pair<int, std::array<float, 6>>> v(box.begin(), box.end());
        for (size_t i = 0; i < v.size(); i++)
            for (size_t j = i + 1; j < v.size(); j++) {
                bool close = true;
                for (int a = 0; a < 3; a++)
                    if (v[i].second[a] > v[j].second[3 + a] + 30.0f || v[j].second[a] > v[i].second[3 + a] + 30.0f) close = false;
                if (close && root(v[i].first) != root(v[j].first)) { join(v[i].first, v[j].first); merged = true; }
            }
        if (!merged) break;
    }
    std::map<int, int> count;
    for (int i = 0; i < (int)t.size(); i++) count[root(i)]++;
    if (count.size() < 2) return;
    int best = std::max_element(count.begin(), count.end(), [](auto &a, auto &b) { return a.second < b.second; })->first;
    std::vector<Prim> out(m.prims.size());
    for (size_t p = 0; p < m.prims.size(); p++) { out[p] = m.prims[p]; out[p].v.clear(); }
    for (int i = 0; i < (int)t.size(); i++)
        if (root(i) == best) for (int k = 0; k < 3; k++) out[t[i].prim].v.push_back(m.prims[t[i].prim].v[t[i].i + k]);
    m.prims.clear();
    for (auto &p : out) if (!p.v.empty()) m.prims.push_back(std::move(p));
}

bool ray_tri(const float o[3], const float d[3], const float *a, const float *b, const float *c, float &t)
{
    float e1[3], e2[3], p[3], q[3], s[3];
    for (int k = 0; k < 3; k++) { e1[k] = b[k] - a[k]; e2[k] = c[k] - a[k]; s[k] = o[k] - a[k]; }
    p[0] = d[1] * e2[2] - d[2] * e2[1]; p[1] = d[2] * e2[0] - d[0] * e2[2]; p[2] = d[0] * e2[1] - d[1] * e2[0];
    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1e-6f) return false;
    float inv = 1.0f / det, u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0 || u > 1) return false;
    q[0] = s[1] * e1[2] - s[2] * e1[1]; q[1] = s[2] * e1[0] - s[0] * e1[2]; q[2] = s[0] * e1[1] - s[1] * e1[0];
    float w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0 || u + w > 1) return false;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t > 0;
}

/* A flat picture (poster, banner, sign): the side its texture reads the right way round,
 * from the textured triangles' u and v directions (area-weighted), when the textured part is
 * flat enough to tell. Out: the front's direction (x, z); 0 when it cannot tell. */
int picture_front(const Model &m, float sg, float *fx, float *fz)
{
    struct F { double n[3], a, c[3]; };
    std::vector<F> f;
    double area = 0;
    for (auto &p : m.prims) {
        if (p.tex < 0) continue;
        for (size_t i = 0; i + 2 < p.v.size(); i += 3) {
            const Vtx &a = p.v[i], &b = p.v[i + 1], &c = p.v[i + 2];
            double e1[3], e2[3], du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1], du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
            for (int k = 0; k < 3; k++) { e1[k] = b.p[k] - a.p[k]; e2[k] = c.p[k] - a.p[k]; }
            double det = du1 * dv2 - du2 * dv1;
            double fa = sqrt(pow(e1[1] * e2[2] - e1[2] * e2[1], 2) + pow(e1[2] * e2[0] - e1[0] * e2[2], 2) + pow(e1[0] * e2[1] - e1[1] * e2[0], 2)) * 0.5;
            area += fa;
            if (fabs(det) < 1e-12) continue;
            double T[3], B[3];
            for (int k = 0; k < 3; k++) { T[k] = (e1[k] * dv2 - e2[k] * dv1) / det; B[k] = (e2[k] * du1 - e1[k] * du2) / det; }
            double N[3] = { T[1] * B[2] - T[2] * B[1], T[2] * B[0] - T[0] * B[2], T[0] * B[1] - T[1] * B[0] };
            double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]) * sg;
            if (l == 0) continue;
            f.push_back({ { N[0] / l, N[1] / l, N[2] / l }, fa, { (a.p[0] + b.p[0] + c.p[0]) / 3.0, (a.p[1] + b.p[1] + c.p[1]) / 3.0, (a.p[2] + b.p[2] + c.p[2]) / 3.0 } });
        }
    }
    if (f.empty() || area <= 0) return 0;
    /* the direction most of the picture's area reads from (a banner printed on both sides
     * has two; either will do -- its back goes against our wall) */
    double best = -1, n[3] = { 0, 0, 0 };
    for (auto &r : f) {
        double s2 = 0, acc[3] = { 0, 0, 0 }, front = 0, back = 0, nb = 0;
        for (auto &q : f) {
            double d = q.n[0] * r.n[0] + q.n[1] * r.n[1] + q.n[2] * r.n[2], h2 = q.c[0] * r.n[0] + q.c[1] * r.n[1] + q.c[2] * r.n[2];
            if (d > 0.7) { s2 += q.a; front += h2 * q.a; for (int k = 0; k < 3; k++) acc[k] += q.n[k] * q.a; }
            else if (d < -0.7) { back += h2 * q.a; nb += q.a; }
        }
        /* a two-sided picture is two sheets, each reading outwards: the right sheet is the one
         * in front along its own direction (the other one's back is behind it) */
        if (getenv("NFL2K5_MC_CRIB_DEBUG")) fprintf(stderr, "  n %.2f %.2f %.2f s2 %.0f front %.1f back %.1f nb %.0f\n", r.n[0], r.n[1], r.n[2], s2, s2 > 0 ? front / s2 : 0, nb > 0 ? back / nb : 0, nb);
        if (nb > 0 && s2 > 0 && back / nb > front / s2 + 0.05) continue;
        if (s2 > best) { best = s2; memcpy(n, acc, sizeof n); }
    }
    if (best <= 0) return 0;
    double h = sqrt(n[0] * n[0] + n[2] * n[2]);
    if (best < 0.4 * area || h < 0.6 * best) return 0;     /* not one flat, upright picture */
    *fx = (float)(n[0] / h); *fz = (float)(n[2] / h);
    return 1;
}

/* A room piece against a Crib wall: the angle (radians, about y) that turns its back to the
 * nearest wall, so its front faces +z; 0 when no wall is near. */
float wall_turn(const Model &m)
{
    auto it = g_crib_scenes.find("room");
    if (it == g_crib_scenes.end()) return 0.0f;
    Model shell;
    add_scene(it->second, shell, "r2_living_room", nullptr);
    add_scene(it->second, shell, "r1_lobby", nullptr);
    float lo[3], hi[3], o[3];
    bounds(m, lo, hi);
    for (int a = 0; a < 3; a++) o[a] = (lo[a] + hi[a]) * 0.5f;
    float ext = std::max(hi[0] - lo[0], hi[2] - lo[2]);
    float best = 1e30f, ang = 0.0f;
    for (int k = 0; k < 72; k++) {
        float a = k * 3.14159265f * 2.0f / 72.0f, d[3] = { sinf(a), 0.0f, cosf(a) }, t, tmin = 1e30f;
        for (auto &p : shell.prims)
            for (size_t i = 0; i + 2 < p.v.size(); i += 3)
                if (ray_tri(o, d, p.v[i].p, p.v[i + 1].p, p.v[i + 2].p, t) && t < tmin) tmin = t;
        if (tmin < best) { best = tmin; ang = a; }
    }
    if (best > ext * 0.75f + 60.0f) return 0.0f;           /* free-standing */
    /* the back (direction ang) should point to -z: turn by pi - ang */
    return 3.14159265f - ang;
}

void normals(Prim &p)
{
    bool have = false;
    for (auto &x : p.v) if (x.n[0] != 0 || x.n[1] != 0 || x.n[2] != 0) { have = true; break; }
    if (have) return;
    std::map<std::tuple<int, int, int>, std::array<float, 3>> acc;
    auto key = [](const float *q) { return std::make_tuple((int)lroundf(q[0] * 8), (int)lroundf(q[1] * 8), (int)lroundf(q[2] * 8)); };
    std::vector<std::array<float, 3>> fn(p.v.size() / 3);
    for (size_t i = 0; i + 2 < p.v.size(); i += 3) {
        const float *a = p.v[i].p, *b = p.v[i + 1].p, *c = p.v[i + 2].p;
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        std::array<float, 3> n = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        fn[i / 3] = n;
        for (int k = 0; k < 3; k++) { auto &s = acc[key(p.v[i + k].p)]; for (int a2 = 0; a2 < 3; a2++) s[a2] += n[a2]; }
    }
    for (size_t i = 0; i < p.v.size(); i++) {
        auto s = acc[key(p.v[i].p)];
        const auto &f = fn[i / 3];
        if (s[0] * f[0] + s[1] * f[1] + s[2] * f[2] <= 0) s = f;          /* a crease: the face's own */
        float l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (l > 0) for (int a = 0; a < 3; a++) p.v[i].n[a] = s[a] / l;
        else { p.v[i].n[0] = 0; p.v[i].n[1] = 1; p.v[i].n[2] = 0; }
    }
}

/* ---- output: glTF + PNG -------------------------------------------------------------------- */
void put32be(std::vector<uint8_t> &v, uint32_t x) { v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

void png_chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
    put32be(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32be(out, crc32(out.data() + start, out.size() - start));
}

bool write_png(const std::string &path, const Image &img)
{
    std::vector<uint8_t> raw, z, ihdr, out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (int y = 0; y < img.h; y++) {
        raw.push_back(0);
        const uint8_t *row = (const uint8_t *)&img.px[(size_t)y * img.w];
        raw.insert(raw.end(), row, row + (size_t)img.w * 4);
    }
    z = { 0x78, 0x01 };
    for (size_t i = 0; i < raw.size(); i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8)); z.push_back((uint8_t)(~n & 0xFF)); z.push_back((uint8_t)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32be(z, b << 16 | a);
    put32be(ihdr, (uint32_t)img.w); put32be(ihdr, (uint32_t)img.h);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
    png_chunk(out, "IHDR", ihdr);
    png_chunk(out, "IDAT", z);
    png_chunk(out, "IEND", {});
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    return fclose(f) == 0 && ok;
}

/* The model, in metres, x/z centred on its footprint and standing on y 0, turned by `turn`,
 * written as <dir>\<name>.gltf + .bin + <name>_tN.png. */
bool write_gltf(const std::string &dir, const std::string &name, Model &m, float turn)
{
    float lo[3], hi[3];
    bounds(m, lo, hi);
    float cx = (lo[0] + hi[0]) * 0.5f, cz = (lo[2] + hi[2]) * 0.5f, c = cosf(turn), s = sinf(turn);
    std::vector<uint8_t> bin;
    std::string js = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"NFL2K5 MyCareer Crib cache\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                     "\"nodes\":[{\"mesh\":0,\"name\":\"" + name + "\"}],";
    std::string meshes = "\"meshes\":[{\"primitives\":[", acc = "\"accessors\":[", views = "\"bufferViews\":[", mats = "\"materials\":[";
    std::string texs = "\"textures\":[", imgs = "\"images\":[";
    int na = 0, nvw = 0, nm = 0, nt = 0;
    std::map<int, int> texid;
    char t[512];
    for (auto &p : m.prims) {
        normals(p);
        float plo[3] = { 1e30f, 1e30f, 1e30f }, phi[3] = { -1e30f, -1e30f, -1e30f };
        size_t at = bin.size();
        for (auto &x : p.v) {
            float px = x.p[0] - cx, pz = x.p[2] - cz;
            /* turned so the front faces Crib +z, then half a turn more: the Crib's axes are
             * right-handed as glTF's, and our front is glTF -z (the room's +z, slot face 0) */
            float q[8] = { -(px * c + pz * s) * kUnit, (x.p[1] - lo[1]) * kUnit, -(-px * s + pz * c) * kUnit,
                           -(x.n[0] * c + x.n[2] * s), x.n[1], -(-x.n[0] * s + x.n[2] * c), x.uv[0], x.uv[1] };
            for (int a = 0; a < 3; a++) { plo[a] = std::min(plo[a], q[a]); phi[a] = std::max(phi[a], q[a]); }
            bin.insert(bin.end(), (uint8_t *)q, (uint8_t *)q + 32);
        }
        snprintf(t, sizeof t, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"byteStride\":32,\"target\":34962}", nvw ? "," : "", at, bin.size() - at);
        views += t;
        int vw = nvw++;
        snprintf(t, sizeof t, "%s{\"bufferView\":%d,\"byteOffset\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
                 "{\"bufferView\":%d,\"byteOffset\":12,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"},"
                 "{\"bufferView\":%d,\"byteOffset\":24,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC2\"}",
                 na ? "," : "", vw, p.v.size(), plo[0], plo[1], plo[2], phi[0], phi[1], phi[2], vw, p.v.size(), vw, p.v.size());
        acc += t;
        /* material: colour x texture */
        std::string tx;
        if (p.tex >= 0) {
            auto it = texid.find(p.tex);
            if (it == texid.end()) {
                char fn[96];
                snprintf(fn, sizeof fn, "%s_t%d.png", name.c_str(), p.tex);
                if (write_png(dir + "\\" + fn, m.tex[p.tex])) {
                    snprintf(t, sizeof t, "%s{\"uri\":\"%s\"}", nt ? "," : "", fn); imgs += t;
                    snprintf(t, sizeof t, "%s{\"source\":%d}", nt ? "," : "", nt); texs += t;
                    it = texid.emplace(p.tex, nt++).first;
                } else it = texid.emplace(p.tex, -1).first;
            }
            if (it->second >= 0) { snprintf(t, sizeof t, ",\"baseColorTexture\":{\"index\":%d}", it->second); tx = t; }
        }
        uint32_t col = p.colour;
        snprintf(t, sizeof t, "%s{\"name\":\"%s\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.4f,%.4f,%.4f,1]%s,\"metallicFactor\":0,\"roughnessFactor\":1},\"doubleSided\":true}",
                 nm ? "," : "", p.mat.c_str(), (col >> 16 & 255) / 255.0f, (col >> 8 & 255) / 255.0f, (col & 255) / 255.0f, tx.c_str());
        mats += t;
        snprintf(t, sizeof t, "%s{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"material\":%d,\"mode\":4}", nm ? "," : "", na, na + 1, na + 2, nm);
        meshes += t;
        na += 3; nm++;
    }
    if (!nm) return false;
    js += meshes + "]}]," + acc + "]," + views + "]," + mats + "],";
    if (nt) js += texs + "]," + imgs + "],\"samplers\":[{}],";
    snprintf(t, sizeof t, "\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%zu}]}", name.c_str(), bin.size());
    js += t;
    FILE *f = fopen((dir + "\\" + name + ".bin").c_str(), "wb");
    if (!f) return false;
    fwrite(bin.data(), 1, bin.size(), f); fclose(f);
    f = fopen((dir + "\\" + name + ".gltf").c_str(), "wb");
    if (!f) return false;
    fwrite(js.data(), 1, js.size(), f);
    return fclose(f) == 0;
}

std::string cache_dir()
{
    const char *ud = xbox_user_dir();
    const char *over = getenv("NFL2K5_MC_CRIB_CACHE");
    std::string d = over && *over ? over : ud && *ud ? std::string(ud) + "\\cache\\mycareer-crib" : std::string("cache\\mycareer-crib");
    for (size_t i = 1; i <= d.size(); i++)
        if (i == d.size() || d[i] == '\\' || d[i] == '/') CreateDirectoryA(d.substr(0, i).c_str(), nullptr);
    return d;
}

/* Builds one source ("crib_couch.iff#model2", "crib.iff:poster_p01#look5", ...). */
bool build(const std::string &source, int team, const std::string &dir, const std::string &name)
{
    std::string src = source, file, what;
    int model = -1, look = -1;
    size_t h;
    while ((h = src.rfind('#')) != std::string::npos) {
        std::string tag = src.substr(h + 1);
        if (!tag.compare(0, 5, "model")) model = atoi(tag.c_str() + 5);
        else if (!tag.compare(0, 4, "look")) look = atoi(tag.c_str() + 4);
        src.resize(h);
    }
    h = src.find(':');
    file = src.substr(0, h);
    if (h != std::string::npos) what = src.substr(h + 1);
    Model m;
    float turn = 0.0f;
    bool room_piece = false;
    if (!_stricmp(file.c_str(), "crib.iff")) {
        crib_read();
        size_t a = 0;
        while (a <= what.size()) {
            size_t e = what.find('+', a);
            std::string part = what.substr(a, e == std::string::npos ? std::string::npos : e - a);
            a = e == std::string::npos ? what.size() + 1 : e + 1;
            auto it = g_crib_scenes.find(part);
            if (it != g_crib_scenes.end() && part != "room") { add_scene(it->second, m, nullptr, nullptr); continue; }
            auto room = g_crib_scenes.find("room");
            if (room == g_crib_scenes.end()) continue;
            if (!add_scene(room->second, m, part.c_str(), nullptr)) add_scene(room->second, m, nullptr, part.c_str());
            room_piece = true;
        }
        if (m.prims.empty()) { fprintf(stderr, "[MCCRIB] %s: nothing of that name in crib.iff\n", source.c_str()); return false; }
    } else {
        /* crib_<kind>.iff: a streaming file of scenes, or scenes in the file itself */
        std::string kind = file.substr(0, file.rfind('.'));
        if (!kind.compare(0, 5, "crib_")) kind = kind.substr(5);
        Res r;
        char nm[64];
        bool got = false;
        if (cdf_open(file).ok) {
            int pick = model >= 0 ? model : look >= 0 ? look : 0;
            if (kind == "bobblehead") snprintf(nm, sizeof nm, "%02d_bobblehead_hi", pick);
            else snprintf(nm, sizeof nm, "%s_%02d", kind.c_str(), pick);
            got = cdf_slot(file, nm, r);
            if (got) add_scene(r, m, nullptr, nullptr);
        } else {
            std::vector<uint8_t> d;
            if (read_file(file, d)) {
                size_t off = 0, next = 0;
                while (off + 0x20 <= d.size()) {
                    Res q;
                    if (!res_decode(d.data() + off, d.size() - off, q, &next)) break;
                    if (!strcmp(q.kind, "SCNE")) { add_scene(q, m, nullptr, nullptr); got = true; break; }
                    off += next;
                }
            }
        }
        if (!got || m.prims.empty()) { fprintf(stderr, "[MCCRIB] %s: not found on the disc\n", source.c_str()); return false; }
    }
    /* looks: untextured materials of a look kind take the Crib's streamed picture */
    std::map<std::string, int> lookid;
    for (auto &p : m.prims) {
        if (p.tex >= 0) continue;
        std::string k = look_kind(p.mat);
        if (k.empty()) continue;
        auto it = lookid.find(k);
        if (it == lookid.end()) {
            Image img;
            std::string used;
            int id = -1;
            if (find_look(k, look, team, img, &used)) { id = (int)m.tex.size(); m.tex.push_back(std::move(img)); }
            fprintf(stderr, "[MCCRIB] %s: %s look %s\n", source.c_str(), p.mat.c_str(), id >= 0 ? used.c_str() : "not found");
            it = lookid.emplace(k, id).first;
        }
        if (it->second >= 0) { p.tex = it->second; p.look = true; p.colour = 0xFFFFFFFFu; }
    }
    {
        float fx, fz, lo[3], hi[3];
        if (room_piece) keep_one_cluster(m);
        bounds(m, lo, hi);
        bool thin = std::min(hi[0] - lo[0], hi[2] - lo[2]) < 0.25f * std::max(hi[0] - lo[0], hi[2] - lo[2]);
        /* the readable side of a picture to +z: -(dP/du x dP/dv) in the Crib's right-handed axes
         * (a two-sheet banner's sheets both read outwards only that way round) */
        float sg = getenv("NFL2K5_MC_CRIB_UVSIGN") ? (float)atof(getenv("NFL2K5_MC_CRIB_UVSIGN")) : -1.0f;
        if ((room_piece || thin) && picture_front(m, sg, &fx, &fz)) turn = atan2f(-fx, fz); else if (room_piece) turn = wall_turn(m);
    }
    if (!write_gltf(dir, name, m, turn)) return false;
    float lo[3], hi[3];
    bounds(m, lo, hi);
    size_t tris = 0;
    for (auto &p : m.prims) tris += p.v.size() / 3;
    fprintf(stderr, "[MCCRIB] %s -> %s: %zu parts, %zu triangles, %.2f x %.2f x %.2f m, turned %.0f deg\n", source.c_str(), name.c_str(),
            m.prims.size(), tris, (hi[0] - lo[0]) * kUnit, (hi[1] - lo[1]) * kUnit, (hi[2] - lo[2]) * kUnit, turn * 57.2958f);
    return true;
}

} // namespace

/* The cached glTF's path for a Crib source (made from the disc the first time); 0 when the
 * disc does not have it. team: the 0..31 team index of team-coloured looks. */
extern "C" int mc_crib_model(const char *source, int team, char *out, size_t n)
{
    std::lock_guard<std::recursive_mutex> g(g_lock);
    std::string name;
    for (const char *p = source; *p; p++) name += isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '_';
    while (!name.empty() && name.back() == '_') name.pop_back();
    char tb[16];
    snprintf(tb, sizeof tb, "_t%02d", team < 0 ? 0 : team);
    name += tb;                                     /* looks can depend on the team */
    std::string dir = cache_dir() + "\\" + name;
    std::string path = dir + "\\" + name + ".gltf";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES || getenv("NFL2K5_MC_CRIB_REBUILD")) {
        CreateDirectoryA(dir.c_str(), nullptr);
        DWORD t0 = GetTickCount();
        if (!build(source, team < 0 ? 0 : team, dir, name)) { RemoveDirectoryA(dir.c_str()); return 0; }
        fprintf(stderr, "[MCCRIB] cached %s in %lu ms\n", path.c_str(), (unsigned long)(GetTickCount() - t0));
    }
    snprintf(out, n, "%s", path.c_str());
    return 1;
}

#ifdef MC_CRIB_CLI
/* mc_crib_cli <root> <team> <source>... : builds into NFL2K5_MC_CRIB_CACHE or <root>\cache. */
int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: mc_crib_cli <root> <team> <source>...\n"); return 2; }
    SetCurrentDirectoryA(argv[1]);
    int ok = 0;
    for (int i = 3; i < argc; i++) {
        char p[MAX_PATH];
        if (mc_crib_model(argv[i], atoi(argv[2]), p, sizeof p)) { printf("%s\t%s\n", argv[i], p); ok++; }
        else printf("%s\tFAILED\n", argv[i]);
    }
    return ok ? 0 : 1;
}
#endif

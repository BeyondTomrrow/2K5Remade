/* Player portraits for HTML presentations: the same headshots the game's
 * depth chart and roster screens show, served as PNGs at
 * https://portraits.local/<photo id>.png (presentation host mapping).
 *
 * The disc keeps them as one aggregate resource (outer 3105) of 4,937
 * fixed 17,664-byte slots starting in pack vc_53450030/3 at 244,084,736 and
 * running on into pack 4 (Mod Studio's nfl2k5_player_portrait_compatibility
 * report). Each slot: a 32-byte TXTR wrapper, a 128-byte header whose name
 * at +0x40 is the photo id as UTF-16 ("0124"), then a 128x128 P8 texture --
 * 16,384 Morton-swizzled palette indices -- and 256 BGRA palette entries.
 * A roster record's photo id is the u16 at +0x06. Pictures come from the
 * player's disc, decoded on demand into cache/portraits; nothing is stored
 * in a package. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace {

constexpr uint64_t kSlotSize = 17664;
constexpr uint32_t kSlots = 4937;
constexpr uint64_t kBaseInPack3 = 244084736;
constexpr uint64_t kPack3Span = 71424000;          /* the aggregate's bytes in pack 3 */
constexpr char kCacheDir[] = "cache/portraits";

std::mutex g_lock;
std::unordered_map<int, uint32_t> g_slot;          /* photo id -> slot */
bool g_indexed;
std::string g_pack_dir;

std::string pack_dir()
{
    const char *dirs[] = { "original/disc/vc_53450030", "original/vc_53450030" };
    for (const char *d : dirs)
        if (GetFileAttributesA((std::string(d) + "/3").c_str()) != INVALID_FILE_ATTRIBUTES)
            return d;
    return "";
}

/* Read bytes at an offset of the aggregate, across the pack 3/4 boundary. */
bool read_agg(uint64_t off, void *out, size_t n)
{
    uint8_t *o = (uint8_t *)out;
    while (n) {
        bool in3 = off < kPack3Span;
        uint64_t pos = in3 ? kBaseInPack3 + off : off - kPack3Span;
        size_t take = in3 ? (size_t)std::min<uint64_t>(n, kPack3Span - off) : n;
        FILE *f = fopen((g_pack_dir + (in3 ? "/3" : "/4")).c_str(), "rb");
        if (!f) return false;
        bool ok = _fseeki64(f, (long long)pos, SEEK_SET) == 0 && fread(o, 1, take, f) == take;
        fclose(f);
        if (!ok) return false;
        o += take; off += take; n -= take;
    }
    return true;
}

void build_index()
{
    g_pack_dir = pack_dir();
    if (g_pack_dir.empty()) { fprintf(stderr, "[PORTRAIT] no vc_53450030 pack folder\n"); return; }
    std::string p3 = g_pack_dir + "/3";
    FILE *f = fopen(p3.c_str(), "rb");
    if (!f) return;
    for (uint32_t i = 0; i < kSlots; i++) {
        uint64_t off = i * kSlotSize;
        uint8_t h[80];
        if (off + 80 <= kPack3Span) {
            if (_fseeki64(f, (long long)(kBaseInPack3 + off), SEEK_SET) || fread(h, 1, 80, f) != 80) break;
        } else if (!read_agg(off, h, 80)) break;
        if (memcmp(h, "TXTR", 4) || memcmp(h + 44, "TXTR", 4)) continue;
        int id = 0, digits = 0;
        for (int k = 0; k < 8; k++) {
            uint16_t c = (uint16_t)(h[64 + k * 2] | h[65 + k * 2] << 8);
            if (c < '0' || c > '9') { if (c) digits = -1; break; }
            id = id * 10 + (c - '0');
            digits++;
        }
        if (digits > 0) g_slot[id] = i;
    }
    fclose(f);
    fprintf(stderr, "[PORTRAIT] %zu portraits indexed in %s\n", g_slot.size(), g_pack_dir.c_str());
}

uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0)
{
    c = ~c;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}

void put32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
    put32(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

/* RGBA -> PNG with stored (uncompressed) deflate blocks: 64 KB a portrait,
 * no codec needed. */
bool write_png(const std::string &path, const uint8_t *rgba, int w, int h)
{
    std::vector<uint8_t> raw, z, ihdr, out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgba + y * w * 4, rgba + (y + 1) * w * 4); }
    z = { 0x78, 0x01 };
    for (size_t i = 0; i < raw.size(); i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, b << 16 | a);
    put32(ihdr, w); put32(ihdr, h);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

} // namespace

/* Start indexing early so the first lineup does not wait for it. */
extern "C" void nfl2k5_portraits_warm(void)
{
    static std::once_flag once;
    std::call_once(once, [] {
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            std::lock_guard<std::mutex> g(g_lock);
            if (!g_indexed) { build_index(); g_indexed = true; }
            return 0;
        }, nullptr, 0, nullptr);
    });
}

/* The portrait's URL, writing its PNG the first time; "" when the disc has
 * no picture for that id (the game then shows its "nophoto" image). */
std::string nfl2k5_portrait_url(int photo_id)
{
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_indexed) { build_index(); g_indexed = true; }
    auto it = g_slot.find(photo_id);
    if (it == g_slot.end()) return "";
    char name[64];
    snprintf(name, sizeof name, "%s/%04d.png", kCacheDir, photo_id);
    if (GetFileAttributesA(name) == INVALID_FILE_ATTRIBUTES) {
        std::vector<uint8_t> s(16384 + 1024), rgba(128 * 128 * 4);
        if (!read_agg((uint64_t)it->second * kSlotSize + 160, s.data(), s.size())) return "";
        const uint8_t *pal = s.data() + 16384;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                uint32_t o = 0;
                for (int k = 0; k < 7; k++) o |= ((x >> k) & 1u) << (2 * k) | ((y >> k) & 1u) << (2 * k + 1);
                const uint8_t *c = pal + s[o] * 4;
                uint8_t *d = &rgba[(y * 128 + x) * 4];
                d[0] = c[2]; d[1] = c[1]; d[2] = c[0]; d[3] = c[3];
            }
        CreateDirectoryA("cache", nullptr);
        CreateDirectoryA(kCacheDir, nullptr);
        if (!write_png(name, rgba.data(), 128, 128)) return "";
    }
    snprintf(name, sizeof name, "https://portraits.local/%04d.png", photo_id);
    return name;
}

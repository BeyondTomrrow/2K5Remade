// Local music library (Cross-platform: C++17 std::filesystem + SDL3).
// <GameRoot>/Music/<folder>/<songs> become Xbox custom soundtracks in ST.DB.
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

extern "C" long nfl2k5_wma_transcode_utf8(const char *src, const char *dst);
extern "C" int nfl2k5_asf_strip_tags_utf8(const char *path);

namespace {

struct Song {
    fs::path src, cache;
    std::u16string title, artist;
    uint32_t ms = 0;
    bool ready = false;
};

struct Album {
    std::u16string name;
    std::vector<Song> songs;
};

fs::path g_music, g_out, g_cache;
std::vector<Album> g_albums;
/* The scanner runs below the game thread.  Do not let hooks dereference the
 * vector while it is still being grown; once the scan is published the
 * vector's shape is stable and only per-song readiness changes. */
static SDL_AtomicInt g_music_ready;
static SDL_AtomicInt g_music_stop;
static SDL_Thread *g_music_thread;

// Convert UTF-8 std::string to 2-byte UTF-16LE std::u16string for Xbox guest RAM & ST.DB
static std::u16string u8_to_u16(const std::string &s)
{
    std::u16string out;
    size_t i = 0, n = s.size();
    while (i < n) {
        uint32_t cp = 0;
        uint8_t c = (uint8_t)s[i++];
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i < n) {
            cp = ((c & 0x1F) << 6) | ((uint8_t)s[i++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 1 < n) {
            cp = ((c & 0x0F) << 12) | (((uint8_t)s[i] & 0x3F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF8) == 0xF0 && i + 2 < n) {
            cp = ((c & 0x07) << 18) | (((uint8_t)s[i] & 0x3F) << 12) |
                 (((uint8_t)s[i + 1] & 0x3F) << 6) | ((uint8_t)s[i + 2] & 0x3F);
            i += 3;
        } else {
            cp = '?';
        }
        if (cp <= 0xFFFF) {
            out.push_back((char16_t)cp);
        } else {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 + (cp >> 10)));
            out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

static bool is_audio(const fs::path &p)
{
    static const char *const exts[] = { ".mp3", ".wma", ".m4a", ".aac", ".flac", ".wav", ".ogg", ".mp4" };
    std::string ext = p.extension().string();
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    for (const char *x : exts) {
        if (ext == x) return true;
    }
    return false;
}

// Natural sort ("2 Track" before "10 Track") replacing StrCmpLogicalW
static bool natural_less(const fs::path &pa, const fs::path &pb)
{
    std::string a = pa.filename().string();
    std::string b = pb.filename().string();
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        if (std::isdigit((unsigned char)a[ia]) && std::isdigit((unsigned char)b[ib])) {
            unsigned long long va = 0, vb = 0;
            while (ia < a.size() && std::isdigit((unsigned char)a[ia]))
                va = va * 10 + (unsigned)(a[ia++] - '0');
            while (ib < b.size() && std::isdigit((unsigned char)b[ib]))
                vb = vb * 10 + (unsigned)(b[ib++] - '0');
            if (va != vb) return va < vb;
        } else {
            int ca = std::tolower((unsigned char)a[ia++]);
            int cb = std::tolower((unsigned char)b[ib++]);
            if (ca != cb) return ca < cb;
        }
    }
    return a.size() < b.size();
}

static std::vector<fs::path> list(const fs::path &dir, bool want_dirs)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        bool is_d = entry.is_directory(ec);
        if (is_d != want_dirs) continue;
        if (want_dirs || is_audio(entry.path()))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), natural_less);
    return out;
}

static std::string strip_track_prefix(std::string t)
{
    size_t i = 0;
    while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
    if (i && i < t.size()) {
        while (i < t.size() && (t[i] == ' ' || t[i] == '.' || t[i] == '-' || t[i] == '_')) i++;
        t = t.substr(i);
    }
    return t;
}

static std::u16string title_from_name(const fs::path &file)
{
    std::string t = strip_track_prefix(file.stem().string());
    size_t dash = t.find(" - ");
    if (dash != std::string::npos) t = t.substr(dash + 3);
    return u8_to_u16(t);
}

static std::u16string artist_from_name(const fs::path &file)
{
    std::string t = strip_track_prefix(file.stem().string());
    size_t dash = t.find(" - ");
    return dash == std::string::npos ? u"" : u8_to_u16(t.substr(0, dash));
}

// Parse duration (ms) directly from the ASF File Properties Object in the WMA header
static uint32_t read_asf_duration_ms(const fs::path &wma_path)
{
    static const uint8_t file_prop_guid[16] = {
        0xA1,0xDC,0xAB,0x8C,0x47,0xA9,0xCF,0x11,
        0x8E,0xE4,0x00,0xC0,0x0C,0x20,0x53,0x65
    };
    FILE *f = fopen(wma_path.string().c_str(), "rb");
    if (!f) return 0;

    uint8_t buf[4096];
    size_t got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (got < 30) return 0;

    uint64_t hsize = 0;
    uint32_t count = 0;
    memcpy(&hsize, buf + 16, 8);
    memcpy(&count, buf + 24, 4);
    if (hsize > got) hsize = got;

    uint64_t pos = 30;
    for (uint32_t i = 0; i < count && pos + 24 <= hsize; i++) {
        uint64_t osize = 0;
        memcpy(&osize, buf + pos + 16, 8);
        if (osize < 24 || pos + osize > hsize) break;
        if (memcmp(buf + pos, file_prop_guid, 16) == 0 && pos + 80 <= hsize) {
            uint64_t play_duration_100ns = 0, preroll_ms = 0;
            memcpy(&play_duration_100ns, buf + pos + 64, 8);
            memcpy(&preroll_ms, buf + pos + 72, 8);
            uint64_t total_ms = play_duration_100ns / 10000ull;
            return (uint32_t)(total_ms > preroll_ms ? total_ms - preroll_ms : total_ms);
        }
        pos += osize;
    }
    return 0;
}

static void read_tags(Song &s)
{
    s.title = title_from_name(s.src);
    s.artist = artist_from_name(s.src);
    if (fs::exists(s.cache))
        s.ms = read_asf_duration_ms(s.cache);
}

static fs::path cache_name(const fs::path &src)
{
    std::error_code ec;
    uint64_t fsize = (uint64_t)fs::file_size(src, ec);
    auto ftime = fs::last_write_time(src, ec).time_since_epoch().count();
    uint64_t mtime = (uint64_t)ftime;

    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) {
            h ^= ((const uint8_t *)p)[i];
            h *= 1099511628211ull;
        }
    };
    std::string lower = src.string();
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);
    mix(lower.data(), lower.size());
    mix(&fsize, sizeof fsize);
    mix(&mtime, sizeof mtime);

    char buf[32];
    snprintf(buf, sizeof buf, "%016llx.wma", (unsigned long long)h);
    return g_cache / buf;
}

static void put_name(uint8_t *dst, const std::u16string &s)
{
    memset(dst, 0, 64);
    memcpy(dst, s.data(), std::min<size_t>(s.size(), 31) * sizeof(char16_t));
}

// Soundtracks in the last ST.DB written (the title numbers them 0..count-1).
static SDL_AtomicInt g_st_count;

// ST.DB from the songs converted so far; replaces the file in one step.
static void write_db()
{
    std::vector<uint8_t> db(0x200 * 101, 0);
    std::vector<std::vector<uint8_t>> groups;
    uint32_t count = 0;
    for (size_t a = 0; a < g_albums.size() && count < 100; a++) {
        const Album &al = g_albums[a];
        std::vector<size_t> ready;
        for (size_t k = 0; k < al.songs.size(); k++)
            if (al.songs[k].ready) ready.push_back(k);
        if (ready.empty()) continue;
        if (ready.size() > 84 * 6) ready.resize(84 * 6);
        uint32_t sid = (uint32_t)a, total = 0;
        uint8_t *st = &db[0x200 * (1 + count)];
        uint32_t ngroups = 0;
        for (size_t g = 0; g < ready.size(); g += 6, ngroups++) {
            std::vector<uint8_t> blk(0x200, 0);
            uint32_t hdr[4] = { 0x31073, sid, (uint32_t)(g / 6), 0 };
            memcpy(&blk[0], hdr, 16);
            for (size_t j = 0; j < 6 && g + j < ready.size(); j++) {
                const Song &s = al.songs[ready[g + j]];
                uint32_t id = (sid << 16) | (uint32_t)ready[g + j];
                memcpy(&blk[0x10 + 4 * j], &id, 4);
                memcpy(&blk[0x28 + 4 * j], &s.ms, 4);
                put_name(&blk[0x40 + 64 * j], s.title);
                total += s.ms;
            }
            uint32_t gid = (uint32_t)groups.size();
            memcpy(st + 0xC + 4 * ngroups, &gid, 4);
            groups.push_back(blk);
        }
        uint32_t rec[3] = { 0x21371, sid, (uint32_t)ready.size() };
        memcpy(st, rec, 12);
        memcpy(st + 0x15C, &total, 4);
        put_name(st + 0x160, al.name);
        memcpy(&db[0xC + 4 * count], &sid, 4);
        count++;
    }
    uint32_t head[3] = { 1, count, (uint32_t)g_albums.size() };
    SDL_SetAtomicInt(&g_st_count, (int)count);
    memcpy(&db[0], head, 12);
    for (auto &g : groups) db.insert(db.end(), g.begin(), g.end());

    fs::path tmp = g_out / "ST.DB.new", dst = g_out / "ST.DB";
    FILE *f = fopen(tmp.string().c_str(), "wb");
    if (!f) return;
    fwrite(db.data(), 1, db.size(), f);
    fclose(f);

    std::error_code ec;
    for (int i = 0; i < 50; i++) {
        fs::rename(tmp, dst, ec);
        if (!ec) break;
        SDL_Delay(20);
    }
}

static void link_song(size_t a, size_t k)
{
    std::string cache_str = g_albums[a].songs[k].cache.string();
    nfl2k5_asf_strip_tags_utf8(cache_str.c_str());

    char dir_buf[32], file_buf[32];
    snprintf(dir_buf, sizeof dir_buf, "%04x", (unsigned)a);
    snprintf(file_buf, sizeof file_buf, "%08x.WMA", (unsigned)((a << 16) | k));

    fs::path d = g_out / dir_buf;
    fs::path f = d / file_buf;
    std::error_code ec;
    fs::create_directories(d, ec);
    fs::remove(f, ec);
    fs::create_hard_link(g_albums[a].songs[k].cache, f, ec);
    if (ec) {
        fs::copy_file(g_albums[a].songs[k].cache, f, fs::copy_options::overwrite_existing, ec);
    }
}

static int SDLCALL worker(void *)
{
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    size_t total = 0, todo = 0;
    std::error_code ec;
    std::vector<fs::path> album_dirs = list(g_music, true);
#ifdef NFL2K5_UWP
    /* UWP/Xbox: more song folders (LocalState\Music, a USB folder named in
     * LocalState\music.txt), ';'-separated UTF-8, from uwp_game.cpp. */
    if (const char *extra = getenv("NFL2K5_EXTRA_MUSIC")) {
        std::string all(extra);
        for (size_t p = 0; p <= all.size();) {
            size_t e = all.find(';', p);
            if (e == std::string::npos) e = all.size();
            if (e > p) {
                fs::path more = fs::u8path(all.substr(p, e - p));
                if (!fs::equivalent(more, g_music, ec))
                    for (const fs::path &d : list(more, true)) album_dirs.push_back(d);
            }
            p = e + 1;
        }
    }
#endif
    for (const fs::path &dir : album_dirs) {
        if (SDL_GetAtomicInt(&g_music_stop)) break;
        Album al;
        al.name = u8_to_u16(dir.filename().string());
        for (const fs::path &f : list(dir, false)) {
            if (SDL_GetAtomicInt(&g_music_stop)) break;
            Song s;
            s.src = f;
            s.cache = cache_name(s.src);
            s.ready = fs::exists(s.cache, ec);
            read_tags(s);
            if (!s.ready) todo++;
            al.songs.push_back(s);
            total++;
        }
        if (!al.songs.empty()) g_albums.push_back(al);
    }
    fprintf(stderr, "[MUSIC] library: %zu playlists, %zu songs, %zu to convert\n", g_albums.size(), total, todo);
    /* Publish only after all Album/Song entries have been appended. */
    SDL_SetAtomicInt(&g_music_ready, 1);
    for (size_t a = 0; a < g_albums.size(); a++) {
        for (size_t k = 0; k < g_albums[a].songs.size(); k++) {
            if (SDL_GetAtomicInt(&g_music_stop)) break;
            if (g_albums[a].songs[k].ready) link_song(a, k);
        }
    }
    write_db();

    for (size_t a = 0; a < g_albums.size(); a++) {
        for (size_t k = 0; k < g_albums[a].songs.size(); k++) {
            if (SDL_GetAtomicInt(&g_music_stop)) break;
            Song &s = g_albums[a].songs[k];
            if (s.ready) continue;
            fs::path part = s.cache;
            part.replace_extension(".part.wma");
            long r = nfl2k5_wma_transcode_utf8(s.src.string().c_str(), part.string().c_str());
            fs::rename(part, s.cache, ec);
            if (r || ec) {
                fprintf(stderr, "[MUSIC] could not convert %s (0x%08lX)\n", s.src.string().c_str(), (unsigned long)r);
                fs::remove(part, ec);
                continue;
            }
            if (!s.ms) s.ms = read_asf_duration_ms(s.cache);
            s.ready = true;
            link_song(a, k);
            write_db();
        }
    }
    fprintf(stderr, "[MUSIC] library ready\n");
    return 0;
}

} // namespace

static void nfl2k5_pack_music_hooks(void);   /* SOFTDRINK: the playlist wins (end of file) */
static void nfl2k5_retail_next_interp_hook(void);   /* R3 when the retail player is interpreted (end of file) */

// root: the game root (holds Music/); save_dir: where Soundtracks/ lives.
static std::string g_start_root, g_start_save;

extern "C" void nfl2k5_local_music_start(const char *root, const char *save_dir)
{
    g_start_root = root;
    g_start_save = save_dir;
    static bool hooked;                               /* once: a rescan starts here again */
    if (!hooked) {
        hooked = true;
        nfl2k5_pack_music_hooks();
        nfl2k5_retail_next_interp_hook();
    }
    SDL_SetAtomicInt(&g_music_ready, 0);
    SDL_SetAtomicInt(&g_music_stop, 0);
    std::error_code ec;
    g_music = fs::path(root) / "Music";
    g_out   = fs::path(save_dir) / "Soundtracks";
    g_cache = g_out / "cache";
    fs::create_directories(save_dir, ec);
    fs::create_directories(g_out, ec);
    fs::create_directories(g_cache, ec);
    fs::create_directories(g_music, ec);

    /* Kept (not detached) so shutdown can stop it and wait. */
    g_music_thread = SDL_CreateThread(worker, "local_music", nullptr);
}

extern "C" void nfl2k5_local_music_shutdown(void)
{
    SDL_SetAtomicInt(&g_music_stop, 1);
    if (g_music_thread) {
        SDL_WaitThread(g_music_thread, nullptr);
        g_music_thread = nullptr;
    }
    SDL_SetAtomicInt(&g_music_ready, 0);
}

/* Songs were added while the game runs (Android: Phone Settings > Add Songs):
 * scan again, convert the new ones and write ST.DB once more. */
extern "C" void nfl2k5_local_music_rescan(void)
{
    if (g_start_root.empty()) return;
    std::string root = g_start_root, save = g_start_save;
    nfl2k5_local_music_shutdown();
    nfl2k5_local_music_start(root.c_str(), save.c_str());
}

/* ── Now playing / skip ──────────────────────────────────────────────────
 * The game's soundtrack player (sub_00328130) opens song ids we handed out:
 * (playlist << 16) | track. Opening shows NOW PLAYING; R3 skips by moving
 * the song's end point to the start, so the player ends it exactly as it
 * ends a song or a clip preview and the game's own playlist moves on,
 * shuffle and repeat included. Two earlier tries hung: starving the decoder
 * read as an I/O error, and raising the "finished" flag directly skipped the
 * stream discontinuity, so entering a game mode waited on the stream forever
 * (2026-10-01). L3 shows NOW PLAYING again. */
extern "C" int nfl2k5_in_match(void);
extern "C" ptrdiff_t xbox_GetMemoryOffset(void);
extern "C" uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

static SDL_AtomicInt g_cur_id = { -1 };
static volatile uint32_t g_player;
static SDL_AtomicInt g_retail_next = { 0 };

static struct {
    uint32_t title_buf, sub_buf;
    uint32_t hdr, saved_title, saved_sub;
    uint32_t fsm_index, fsm_stack[5];
    Uint64 until;
    bool active;
} g_banner;
static std::u16string g_banner_sub_text;
static size_t g_banner_scroll_pos;
static Uint64 g_banner_scroll_at;

static uint8_t *guest(void) { return (uint8_t *)xbox_GetMemoryOffset(); }
static volatile uint32_t &g32(uint32_t va) { return *(volatile uint32_t *)(guest() + va); }

static void put_guest_u16str(uint32_t va, const std::u16string &w, size_t max_chars)
{
    size_t n = std::min(w.size(), max_chars - 1);
    uint16_t *d = (uint16_t *)(guest() + va);
    for (size_t i = 0; i < n; i++) d[i] = (uint16_t)w[i];
    d[n] = 0;
}

static std::u16string get_guest_u16str(uint32_t va, size_t max_chars = 64)
{
    std::u16string out;
    if (!va || va >= 0x04000000u) return out;
    const uint16_t *s = (const uint16_t *)(guest() + va);
    for (size_t i = 0; i < max_chars && s[i]; i++) out.push_back((char16_t)s[i]);
    return out;
}

static bool fsm_is_banner_origin(void)
{
    if (g32(0xA84B18u + 0x100u) != g_banner.fsm_index) return false;
    for (size_t i = 0; i < 5; i++)
        if (g32(0xA84B18u + (uint32_t)i * 8u) != g_banner.fsm_stack[i]) return false;
    return true;
}

static bool native_title_is_main_menu(uint32_t hdr)
{
    std::u16string title = get_guest_u16str(g32(hdr + 0x598u));
    std::u16string key;
    for (char16_t c : title) {
        if (c >= u'a' && c <= u'z') c -= u'a' - u'A';
        if ((c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')) key += c;
    }
    return key == u"MAINMENU";
}

static bool on_main_menu(void)
{
    if (nfl2k5_in_match()) return false;
    uint32_t hdr = g32(0xA84B18u + 0x10Cu);
    if (!hdr || hdr >= 0x04000000u) return false;
    if (g_banner.active && hdr == g_banner.hdr &&
        g32(hdr + 0x598u) == g_banner.title_buf)
        return fsm_is_banner_origin();
    return native_title_is_main_menu(hdr);
}

static std::u16string banner_sub_frame(void)
{
    const size_t width = 28;
    if (g_banner_sub_text.size() <= width) return g_banner_sub_text;
    const std::u16string loop = g_banner_sub_text + u"     ";
    std::u16string frame;
    frame.reserve(width);
    for (size_t i = 0; i < width; i++)
        frame.push_back(loop[(g_banner_scroll_pos + i) % loop.size()]);
    return frame;
}

static bool banner_show(const std::u16string &title, const std::u16string &sub)
{
    if (!on_main_menu()) return false;
    uint32_t hdr = g32(0xA84B18u + 0x10Cu);
    if (!hdr || hdr >= 0x04000000u || !g32(hdr + 0x598u)) return false;
    if (!g_banner.title_buf) {
        g_banner.title_buf = xbox_HeapAlloc(64 * 2, 4);
        g_banner.sub_buf = xbox_HeapAlloc(96 * 2, 4);
        if (!g_banner.title_buf || !g_banner.sub_buf) return false;
    }
    if (!g_banner.active || g_banner.hdr != hdr) {
        g_banner.hdr = hdr;
        g_banner.saved_title = g32(hdr + 0x598u);
        g_banner.saved_sub = g32(hdr + 0x59Cu);
        g_banner.fsm_index = g32(0xA84B18u + 0x100u);
        for (size_t i = 0; i < 5; i++)
            g_banner.fsm_stack[i] = g32(0xA84B18u + (uint32_t)i * 8u);
    }
    g_banner_sub_text = sub;
    g_banner_scroll_pos = 0;
    g_banner_scroll_at = SDL_GetTicks() + 900;
    put_guest_u16str(g_banner.title_buf, title, 64);
    put_guest_u16str(g_banner.sub_buf, banner_sub_frame(), 96);
    g32(hdr + 0x598u) = g_banner.title_buf;
    g32(hdr + 0x59Cu) = g_banner.sub_buf;
    g32(0xA83F14u) = 1;
    g_banner.active = true;
    g_banner.until = SDL_GetTicks() + 5000;
    return true;
}

static void banner_tick(void)
{
    if (!g_banner.active) return;
    Uint64 now = SDL_GetTicks();
    if (on_main_menu() && now < g_banner.until) {
        if (g_banner_sub_text.size() > 28 && now >= g_banner_scroll_at) {
            g_banner_scroll_pos++;
            g_banner_scroll_at = now + 260;
            put_guest_u16str(g_banner.sub_buf, banner_sub_frame(), 96);
            g32(0xA83F14u) = 1;
        }
        return;
    }
    g_banner.active = false;
    uint32_t hdr = g_banner.hdr;
    bool restored = false;
    if (hdr && hdr < 0x04000000u) {
        if (g32(hdr + 0x598u) == g_banner.title_buf) {
            g32(hdr + 0x598u) = g_banner.saved_title;
            restored = true;
        }
        if (g32(hdr + 0x59Cu) == g_banner.sub_buf) {
            g32(hdr + 0x59Cu) = g_banner.saved_sub;
            restored = true;
        }
    }
    if (restored) g32(0xA83F14u) = 1;
}

static bool show_now_playing(uint32_t id)
{
    if (SDL_GetAtomicInt(&g_music_ready) == 0) return false;
    size_t a = id >> 16, k = id & 0xFFFF;
    if (a >= g_albums.size() || k >= g_albums[a].songs.size()) return false;
    const Song &song = g_albums[a].songs[k];
    std::u16string sub = song.title;
    const std::u16string &by = song.artist.empty() ? g_albums[a].name : song.artist;
    if (!by.empty()) sub += u" - " + by;
    return banner_show(u"Now Playing", sub);
}

extern "C" void xbox_MenuInvalidate(void);

extern "C" void nfl2k5_local_music_opening(uint32_t song_id, uint32_t player)
{
    g_player = player;
    SDL_SetAtomicInt(&g_cur_id, (int)song_id);
    show_now_playing(song_id);
    xbox_MenuInvalidate();             /* F1 > Music Player: Now playing */
}

extern "C" void nfl2k5_local_music_closed(void)
{
    SDL_SetAtomicInt(&g_cur_id, -1);
}

struct RetailTrack { const char *title, *artist; };

static const RetailTrack g_crib_tracks[] = {
    {"Bounce", "Big J"}, {"Take Off", "Big J"},
    {"Crank It Up", "Big J"}, {"It Just Won't Be", "Big J"},
    {"Make The Stop", "Opus 1"}, {"Slow Down", "Opus 1"},
    {"Sportin'", "Opus 1"}, {"Two Three", "Opus 1"},
    {"2 Much", "Mike Reagan"}, {"Come On", "Mike Reagan"},
    {"Hands Up", "Brad Cross"}, {"Yo Groove", "Brad Cross"},
    {"Aw Yaz", "Brad Cross"}, {"Code Breaker", "Opus 1"},
    {"Dance It", "Opus 1"}, {"Tizaziz", "Brad Cross"},
    {"Can't Sit Still", "The Danger"}, {"Fly Eater", "The Danger"},
    {"I Ain't Used", "The Danger"}, {"Locket", "The Danger"},
    {"Outtake 1", "Dan & Steve"}, {"Outtake 2", "Dan & Steve"},
    {"Outtake 3", "Dan & Steve"}, {"Outtake 4", "Dan & Steve"},
    {"Outtake 5", "Dan & Steve"}, {"Outtake 6", "Dan & Steve"},
    {"Outtake 7", "Dan & Steve"}, {"Outtake 8", "Dan & Steve"},
    {"Outtake 9", "Dan & Steve"}, {"Outtake 10", "Dan & Steve"},
    {"Outtake 11", "Dan & Steve"}, {"Outtake 12", "Dan & Steve"},
    {"The Best", "Raw Intel/RIC"}, {"Get In Line", "Raw Intel/RIC"},
    {"Can't Go Wrong", "Raw Intel/RIC"}, {"Deep And Wide", "Aceyalone"},
    {"Ace Cowboy", "Aceyalone"}, {"Superstar", "The Good Brothers"},
    {"Try Me", "J. Boogie"}, {"Golden Nectar", "J. Boogie"},
    {"Le Sengre", "J. Boogie"}, {"All Pleasure", "Recliner"},
    {"Irish Bullfight", "Recliner"}, {"Gothic Voices", "Concept"},
    {"Angel Of Truth", "Concept"}, {"Evolution!", "Concept"},
    {"Drumbox", "People Under the Stairs"},
    {"Outrun", "People Under the Stairs"},
    {"Clean Living", "Rjd2"}, {"Pull Out Your Cut", "MR. LIF"},
    {"Like Smak", "Raw Intel/RIC"}, {"The God In Me", "Aceyalone"},
    {"Knock Me Down Girl", "Slicker"}, {"Oceanic Lullaby", "J. Boogie"},
    {"Making A Friend", "Recliner"}, {"Eternal Life", "Concept"},
    {"Disco Rout", "Legowelt"},
    {"Sound In A Dark Room", "Telefon Tel Aviv"},
    {"The Pharaoh", "The Danger"},
};

static constexpr size_t kNumCribTracks = sizeof(g_crib_tracks) / sizeof(g_crib_tracks[0]);

enum RetailBank { RETAIL_NONE, RETAIL_MENU, RETAIL_CRIB };

static bool retail_current(RetailBank &bank, uint32_t &mode, uint32_t &index)
{
    mode = g32(0xC3CC08u);
    if (mode >= 2 || !g32(0xC3AC90u)) return false;
    uint32_t name = g32(0xAC9EC0u + mode * 0x10u + 0x0Cu);
    index = g32(0xC5D39Cu + mode * 4u);
    if (name == 0xE92A34u && index < kNumCribTracks) {
        bank = RETAIL_CRIB;
        return true;
    }
    if (name == 0xE92D4Cu && index < 7) {
        bank = RETAIL_MENU;
        return true;
    }
    return false;
}

static bool show_retail_now_playing(RetailBank bank, uint32_t index)
{
    if (bank == RETAIL_CRIB) {
        const RetailTrack &song = g_crib_tracks[index];
        std::string s = std::string(song.title) + " - " + song.artist;
        return banner_show(u"Now Playing", u8_to_u16(s));
    } else if (bank == RETAIL_MENU) {
        char sub[96];
        snprintf(sub, sizeof sub, "Original Menu Music %02u - NFL 2K5 Original Soundtrack", index + 1);
        return banner_show(u"Now Playing", u8_to_u16(sub));
    }
    return false;
}

extern "C" int nfl2k5_retail_music_take_next(void)
{
    if (SDL_SetAtomicInt(&g_retail_next, 0) == 0) return 0;
    fprintf(stderr, "[MUSIC] R3: the retail player advances (stop + next on the guest thread)\n");
    return 1;
}

static SDL_AtomicInt g_skip_request;   /* F1 > Music Player: Next Song, an R3 press */

extern "C" void nfl2k5_local_music_buttons(int l3, int r3)
{
    static int prev_l3, prev_r3;
    static RetailBank last_bank = RETAIL_NONE;
    static RetailBank shown_bank = RETAIL_NONE;
    static uint32_t last_mode = ~0u, last_index = ~0u;
    static uint32_t shown_index = ~0u;
    static int shown_custom = -1;
    int cur = SDL_GetAtomicInt(&g_cur_id);
    banner_tick();
    uint32_t player = g_player;
    RetailBank retail_bank = RETAIL_NONE;
    uint32_t retail_mode = 0, retail_index = 0;
    bool retail_live = retail_current(retail_bank, retail_mode, retail_index);
    if (retail_live && (retail_bank != last_bank || retail_mode != last_mode ||
                        retail_index != last_index)) {
        last_bank = retail_bank;
        last_mode = retail_mode;
        last_index = retail_index;
        fprintf(stderr, "[MUSIC] retail %s track %u\n",
                retail_bank == RETAIL_CRIB ? "cribmusic" : "femusic", retail_index);
    } else if (!retail_live) {
        last_bank = RETAIL_NONE;
        last_mode = last_index = ~0u;
        shown_bank = RETAIL_NONE;
        shown_index = ~0u;
    }
    if (cur < 0 && retail_live && !nfl2k5_in_match() &&
        (shown_bank != retail_bank || shown_index != retail_index) &&
        show_retail_now_playing(retail_bank, retail_index)) {
        shown_bank = retail_bank;
        shown_index = retail_index;
    }
    if (cur >= 0 && cur != shown_custom && !nfl2k5_in_match() &&
        show_now_playing((uint32_t)cur))
        shown_custom = cur;
    else if (cur < 0)
        shown_custom = -1;

    if (nfl2k5_in_match() && !retail_live) {
        prev_l3 = l3;
        prev_r3 = r3;
        return;
    }
    if (cur >= 0 && player && r3 && !prev_r3) {
        volatile uint32_t *end_pos =
            (volatile uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + player + 0x1004Cu);
        *end_pos = 1;
        fprintf(stderr, "[MUSIC] R3: skip the playing song %08lX (end point pulled in)\n", (unsigned long)cur);
    } else if (retail_live && r3 && !prev_r3) {
        SDL_SetAtomicInt(&g_retail_next, 1);
        fprintf(stderr, "[MUSIC] R3: next %s track requested (now %u)\n",
                retail_bank == RETAIL_CRIB ? "cribmusic" : "femusic", retail_index);
    } else if (r3 && !prev_r3) {
        fprintf(stderr, "[MUSIC] R3: nothing to skip (no playlist song, no retail track; mode %08X list %08X)\n",
                g32(0xC3CC08u), g32(0xC3AC90u));
    }
    if (cur >= 0 && l3 && !prev_l3)
        show_now_playing((uint32_t)cur);
    else if (cur < 0 && retail_live && l3 && !prev_l3)
        show_retail_now_playing(retail_bank, retail_index);
    else if (l3 && !prev_l3 && getenv("NFL2K5_BANNER_TEST"))
        banner_show(u"Now Playing", u"Ramblin' Man From Gramblin - Sam Spence & Da Riffs");
    prev_l3 = l3;
    prev_r3 = r3;
}


/* ── SOFTDRINK: the player's playlist wins over the pack's menu shuffle ────
 * SOFTDRINK 2K28 (v0.6 and v0.6.1, the same bytes) replaces the game's menu
 * music with a shuffle of the retail femusic + cribmusic banks, and on the way
 * shuts out the player's own Jukebox playlist everywhere: its data makes the
 * UserList row (retail: the playlist) a cribmusic bank row, its music-mode switch
 * (0x014DFD71 for retail 0x000F6510) sends the Crib and the menus to its shuffle,
 * its context switch (0x014DFD00 for retail 0x002804C0) does the same for
 * UserList, and its song-finished callback (0x014DFBF6 for retail 0x0027F040)
 * restarts the current song, so a song never moved on -- R3 skip included.
 * When the player has a playable playlist selected (the Jukebox list, Crib Music
 * Crib/Menus for the menus), these interpreter hooks run the retail code instead
 * (its bytes after each 5-byte jump are intact) and the pack's own "playlist
 * playing" flag (0x014DFCE4) keeps its shuffle out. With no playlist the pack's
 * shuffle plays as before. docs/MUSIC-FIX-20261021.md.
 * nfl2k5_game.ini: pack_shuffle_first = 1 gives the pack's shuffle priority
 * again; NFL2K5_PACK_SHUFFLE_FIRST=0/1 overrides it for one run;
 * NFL2K5_PACK_MUSIC_LOG=1 logs every request. */
#if defined(_MSC_VER)
#define LOCALMUSIC_TLS __declspec(thread)
#else
#define LOCALMUSIC_TLS __thread
#endif
extern "C" void recomp_interp_add_hook(uint32_t va, uint32_t from, uint32_t (*fn)(void));
extern "C" LOCALMUSIC_TLS uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esp, g_esi;
extern "C" char *xbox_user_path(const char *rel, char *out, size_t n);

static int g_pack_shuffle_first;

static bool guest_wide_eq(uint32_t va, const char16_t *s)
{
    if (!va || va >= 0x08000000u) return false;
    const uint16_t *p = (const uint16_t *)(guest() + va);
    for (; *s; s++, p++) if (*p != (uint16_t)*s) return false;
    return *p == 0;
}

/* SOFTDRINK's data turns the UserList row into a second cribmusic bank row
 * ({UserList, 1, 1, "cribmusic"} at 0xAC9ED0; retail {UserList, 0, 0, 0}), so
 * retail sub_00280450 plays a bank there instead of the playlist. A playlist is
 * selected when the game holds a current entry in its list (0xC3CBFC, list head
 * 0xC3CBE8). */
static uint32_t g_pick_nodes;          /* F1 > Music Player: guest playlist entries, kMaxPick of them */
static constexpr uint32_t kPickNode = 0x18u, kMaxPick = 504u;

static bool pack_playlist_selected(void)
{
    uint32_t cur = g32(0xC3CBFCu);
    if (g_pick_nodes && cur >= g_pick_nodes && cur < g_pick_nodes + kPickNode * kMaxPick) return true;
    if (g32(0xC3CBF8u) == 0xC3CBE8u || !cur || cur == 0xC3CBE8u || cur >= 0x08000000u) return false;
    /* Its current entry must be playable: albums 0..0x11 are the game's own banks,
     * 0x12+ the custom soundtracks (retail 0x0028013A checks the same against the
     * soundtrack count). A playlist saved with songs that are gone from Music
     * leaves the pack's shuffle alone instead of silence. */
    uint32_t album = g32(cur);
    return album < 0x12u || album - 0x12u < (uint32_t)SDL_GetAtomicInt(&g_st_count);
}

static void pack_music_restore_userlist_row(void)
{
    if (g32(0xAC9ED8u) == 0) return;
    g32(0xAC9ED4u) = 0;
    g32(0xAC9ED8u) = 0;                               /* no bank: the playlist */
    g32(0xAC9EDCu) = 0;
    fprintf(stderr, "[MUSIC] pack music: the UserList row plays the player's playlist again (was a cribmusic bank row)\n");
}

static void pack_music_log(const char *what, uint32_t mode)
{
    fprintf(stderr, "[MUSIC] pack music %s (mode %u, row %u, row1 bank %08X, song %08X, flags %u/%u/%u, "
                    "player %08X/%08X, state %u/%u, pack %u/%u)\n",
            what, mode, g32(0xC3CC08u), g32(0xAC9ED8u), g32(0xC3CBFCu),
            g32(0xB9E298u), g32(0xB9E29Cu), g32(0xB9E2A4u), g32(0xC3CC10u), g32(0xC3CC14u),
            g32(0xC3AC90u), g32(0xC5D3A4u), g32(0x014F24C4u), g32(0x014F24E4u));
}

/* 0x00280507, the end of the retail context switch reached from the two hooks
 * above: "jmp 0x00280450" (start the row). Under the pack that start went to its
 * shuffle again (traced 2026-10-22: pack stop, then pack shuffle start, never the
 * playlist start), so for the UserList row with a playlist do what 0x00280450 does
 * for a bankless row: start the current playlist entry at 0x0027FF90 (a tail
 * call: the caller's return address is on top). The pack's 0x014DFCE4 hook there
 * marks its "playlist playing" flag and stays out. */
static uint32_t ih_pack_music_start(void)
{
    if (g_pack_shuffle_first || g32(0xC3CC08u) != 1u || g32(0xAC9ED8u) != 0 ||
        !pack_playlist_selected() || !g32(0xC3CC10u))
        return 0;
    pack_music_log("start: the player's playlist (0x27FF90)", 1);
    g32(0xC3AC90u) = 1;
    g_ecx = g32(0xC3CBFCu);
    return 0x0027FF90u;
}

/* 0x014DF9C5, the pack's "start my shuffle" (called on screen pushes / pops and
 * when a paused menu resumes, e.g. 0x014DFE59). With the UserList row active and a
 * playlist selected, resume the playlist instead -- only when the pack would really
 * start its shuffle (enabled 0x014F24C4, nothing playing: 0x014F24C8 / CC / E4 / E8). */
static uint32_t ih_pack_music_shuffle(void)
{
    if (getenv("NFL2K5_PACK_MUSIC_LOG")) pack_music_log("trace: pack shuffle start", 0);
    if (g_pack_shuffle_first || g32(0xC3CC08u) != 1u || g32(0xAC9ED8u) != 0 || !pack_playlist_selected() ||
        !g32(0x014F24C4u) || g32(0x014F24C8u) || g32(0x014F24CCu) || g32(0x014F24E4u) || g32(0x014F24E8u) ||
        !g32(0xC3CC10u))
        return 0;
    pack_music_log("resume: the player's playlist instead of the pack's shuffle (0x27FF90)", 1);
    g32(0xC3AC90u) = 1;
    g_ecx = g32(0xC3CBFCu);
    return 0x0027FF90u;                               /* runs as this function: returns to its caller */
}

/* 0x014DFE72, the pack's replacement for the retail "next song" routine
 * (0x0027F040, called when a song ends -- also after an R3 skip, which pulls the
 * song's end in). For a playlist it only stopped and asked for a restart of the
 * current entry, so a song that ended, or was skipped, played again from the top
 * ("when i skip a song in softdrink plays a song cant skip", 2026-10-22). With the
 * UserList row and a playlist, run the retail routine (intact after the 5-byte
 * jump; its first instruction is mov eax, [0xC3CC08]): it moves to the next entry
 * with the game's own shuffle / repeat rules. */
static uint32_t ih_pack_music_advance(void)
{
    if (g_pack_shuffle_first || g32(0xC3CC08u) != 1u || g32(0xAC9ED8u) != 0 || !pack_playlist_selected())
        return 0;
    pack_music_log("next: the playlist's next song (retail 0x27F040)", 1);
    g_eax = g32(0xC3CC08u);
    return 0x0027F045u;
}

/* 0x014DFBF6: the pack registers this instead of 0x0027F040 as the custom song's
 * "finished" callback (retail 0x0028016D push 0x27F040). It stops and restarts the
 * current entry, so a finished or skipped song played again. For the player's
 * playlist hand it to the next-song routine (0x27F040 -> the hook above). */
static uint32_t ih_pack_music_song_end(void)
{
    if (g_pack_shuffle_first || g32(0xC3CC08u) != 1u || g32(0xAC9ED8u) != 0 || !pack_playlist_selected())
        return 0;
    pack_music_log("song end: on to the playlist's next song", 1);
    return 0x0027F040u;
}

static uint32_t ih_pack_music_trace_play(void) { pack_music_log("trace: playlist start (0x27FF90)", g_ecx); return 0; }
static uint32_t ih_pack_music_trace_stop(void) { pack_music_log("trace: pack stop", 0); return 0; }

/* 0x014DFD00, the pack's "set music context" (retail 0x002804C0). */
static uint32_t ih_pack_music_context(void)
{
    if (getenv("NFL2K5_PACK_MUSIC_LOG"))
        pack_music_log(guest_wide_eq(g_ecx, u"UserList") ? "context request UserList" :
                       guest_wide_eq(g_ecx, u"Frontend") ? "context request Frontend" : "context request (none)", 0xFFFFFFFFu);
    if (g_pack_shuffle_first || !guest_wide_eq(g_ecx, u"UserList") || !pack_playlist_selected()) return 0;
    pack_music_log("context UserList: the player's playlist", 0xFFFFFFFFu);
    pack_music_restore_userlist_row();
    g_esp -= 4;                                       /* retail 0x002804C0: push ebx; mov ebx, ecx */
    g32(g_esp) = g_ebx;
    g_ebx = g_ecx;
    return 0x002804C7u;                               /* ebx != 0: past its test/je */
}

/* 0x014DFD71, the pack's music mode switch (retail 0x000F6510: 0 off, 1 menus,
 * 2 loading, 3 the Crib, 4 in game). The pack sends the menus and the Crib to its
 * Frontend shuffle; retail asks for the UserList row in the Crib, and in the
 * menus / loading / game when Crib Music says so (0xB9E298 / 0xB9E29C / 0xB9E2A4).
 * Those cases go to the retail handlers (intact after the pack's 5-byte jump). */
static uint32_t ih_pack_music_mode(void)
{
    uint32_t mode = g_ecx;
    if (getenv("NFL2K5_PACK_MUSIC_LOG")) pack_music_log("mode request", mode);
    if (g_pack_shuffle_first || mode > 4 || !pack_playlist_selected()) return 0;
    bool user = mode == 3 || (mode == 1 && g32(0xB9E298u)) || (mode == 2 && g32(0xB9E29Cu)) ||
                (mode == 4 && g32(0xB9E2A4u));
    if (!user) return 0;
    pack_music_log("mode: the player's playlist (retail handler)", mode);
    pack_music_restore_userlist_row();
    g_esp -= 4;                                       /* retail 0x000F6510: push esi; mov esi, ecx */
    g32(g_esp) = g_esi;
    g_esi = g_ecx;
    return g32(0x000F65B4u + mode * 4u);              /* jmp [esi*4 + 0xF65B4] */
}

static void nfl2k5_pack_music_hooks(void)
{
    static const uint8_t jmp_sig[5] = { 0xE9, 0x3B, 0xF8, 0x25, 0x01 };   /* 0x2804C0: jmp 0x014DFD00 */
    static const uint8_t ctx_sig[16] = { 0x85, 0xC9, 0x74, 0x22, 0x56, 0x89, 0xCE, 0xBA,
                                         0xC0, 0xC0, 0xE6, 0x00, 0xE8, 0xDF, 0x0F, 0xB5 };
    static const uint8_t retail_tail[8] = { 0x56, 0x57, 0x33, 0xFF, 0xBE, 0xC0, 0x9E, 0xAC };   /* 0x2804C7 */
    static const uint8_t mode_sig[5] = { 0xE9, 0x5C, 0x98, 0x3E, 0x01 };   /* 0xF6510: jmp 0x014DFD71 */
    static const uint8_t mode_ctx_sig[8] = { 0x83, 0xF9, 0x04, 0x77, 0x6E, 0x56, 0x89, 0xCE };
    static const uint8_t next_sig[5] = { 0xE9, 0x2D, 0x0E, 0x26, 0x01 };   /* 0x27F040: jmp 0x014DFE72 */
    static const uint8_t next_ctx_sig[8] = { 0xE8, 0xD9, 0xF9, 0xFF, 0xFF, 0x83, 0x3D, 0xE4 };
    static const uint8_t end_ctx_sig[7] = { 0x83, 0x3D, 0xE4, 0x24, 0x4F, 0x01, 0x00 };   /* cmp [0x14F24E4], 0 */
    uint8_t *m = guest();
    if (!m || memcmp(m + 0x2804C0u, jmp_sig, 5) || memcmp(m + 0x014DFD00u, ctx_sig, 16) ||
        memcmp(m + 0x2804C7u, retail_tail, 8) || memcmp(m + 0x000F6510u, mode_sig, 5) ||
        memcmp(m + 0x014DFD71u, mode_ctx_sig, 8) || m[0x000F651Au] != 0x33 || g32(0x000F65C4u) != 0x000F6578u ||
        memcmp(m + 0x0027F040u, next_sig, 5) || memcmp(m + 0x014DFE72u, next_ctx_sig, 8) ||
        g32(0x0027F045u) != 0x8B04E0C1u || g32(0x0028016Eu) != 0x014DFBF6u ||
        memcmp(m + 0x014DFBF6u, end_ctx_sig, 7))
        return;                                       /* not SOFTDRINK's music code */
    g_pack_shuffle_first = 0;
    char ip[1024], line[160];
    FILE *f = fopen(xbox_user_path("nfl2k5_game.ini", ip, sizeof ip), "r");
    bool listed = false;
    if (f) {
        int v;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, " pack_shuffle_first = %d", &v) == 1) { g_pack_shuffle_first = v != 0; listed = true; }
        fclose(f);
    }
    if (!listed && (f = fopen(ip, "a")) != NULL) {    /* make the switch visible in the file */
        fprintf(f, "; pack_shuffle_first: 1 = a mod pack's own menu music (SOFTDRINK's shuffle) plays even when\n"
                   "; a Crib Jukebox playlist is selected; 0 = the playlist wins (Crib Music: Crib/Menus).\n"
                   "pack_shuffle_first = 0\n");
        fclose(f);
    }
    if (const char *e = getenv("NFL2K5_PACK_SHUFFLE_FIRST")) g_pack_shuffle_first = atoi(e) != 0;
    recomp_interp_add_hook(0x014DFD00u, 0, ih_pack_music_context);
    recomp_interp_add_hook(0x014DFD71u, 0, ih_pack_music_mode);
    recomp_interp_add_hook(0x00280507u, 0x00280506u, ih_pack_music_start);
    recomp_interp_add_hook(0x014DF9C5u, 0, ih_pack_music_shuffle);
    recomp_interp_add_hook(0x014DFE72u, 0, ih_pack_music_advance);
    recomp_interp_add_hook(0x014DFBF6u, 0, ih_pack_music_song_end);
    if (getenv("NFL2K5_PACK_MUSIC_LOG")) {           /* diagnostics: the pack's own music entry points */
        recomp_interp_add_hook(0x014DFCE4u, 0, ih_pack_music_trace_play);
        recomp_interp_add_hook(0x014DFC41u, 0, ih_pack_music_trace_stop);
    }
    fprintf(stderr, "[MUSIC] SOFTDRINK menu music: %s (nfl2k5_game.ini pack_shuffle_first = %d)\n",
            g_pack_shuffle_first ? "the pack's shuffle has priority" : "the player's Jukebox playlist wins",
            g_pack_shuffle_first);
}

/* ── R3 for the original-disc music when the retail player runs interpreted ──
 * The retail music frame routine 0x00280620 is reached only by the tail jump at the end of
 * sub_000F6420. A mod pack that changes sub_000F6420 (SOFTDRINK 2K28 v0.6/v0.6.1 does) runs it
 * in the interpreter, and the interpreter follows the jump into 0x00280620 itself -- so the
 * generated code's LOCALMUSIC_RETAIL_NEXT hook never ran there, and R3 queued a skip that
 * nothing took: "plays a song, can't skip" (AUDIO-FIX-20261021: the log showed "R3: next
 * cribmusic track requested" three times and no "advances"). The same skip in the interpreter:
 * at 0x00280620, take the request, then "call" the stop (0x0027FEC0) and the advance
 * (0x0027F040) -- the pack's own versions when it replaced them -- by returning into the stop
 * with the advance and 0x00280620 as the return addresses; the second arrival restores the
 * scratch registers the native hook keeps (eax, ecx, edx) and lets the routine run. */
static uint32_t ih_retail_music_next(void)
{
    static LOCALMUSIC_TLS int restore;
    static LOCALMUSIC_TLS uint32_t keep_eax, keep_ecx, keep_edx;
    if (restore) {
        restore = 0;
        g_eax = keep_eax; g_ecx = keep_ecx; g_edx = keep_edx;
        return 0;
    }
    if (!nfl2k5_retail_music_take_next()) return 0;
    keep_eax = g_eax; keep_ecx = g_ecx; keep_edx = g_edx;
    restore = 1;
    g_esp -= 4; g32(g_esp) = 0x00280620u;             /* the advance returns here (restore, then run) */
    g_esp -= 4; g32(g_esp) = 0x0027F040u;             /* the stop's ret goes on into the advance */
    return 0x0027FEC0u;
}

static void nfl2k5_retail_next_interp_hook(void)
{
    recomp_interp_add_hook(0x00280620u, 0, ih_retail_music_next);
}

/* ── F1 > Music Player ─────────────────────────────────────────────────────
 * Works without the Crib, so also under a pack that took the Jukebox away.
 * Sources: the game's own music, the VIP's Jukebox playlist, each Music folder.
 * A folder plays through entries of the game's playlist form {album, track, ..,
 * next at +0x10} kept here, looped; the VIP's own list is never touched, the
 * game's "current entry" 0xC3CBFC just points into them. The choice is carried
 * out on the guest thread by nfl2k5_music_frame_service (front-end frames). */
extern "C" void sub_000CF0F0(void);
extern "C" void sub_0027F550(void);
extern "C" void sub_0027FEC0(void);
extern "C" void sub_002804C0(void);
extern "C" void sub_00280450(void);
extern "C" void xbox_MenuAddPage(const void *page);

static SDL_AtomicInt g_pick_request;   /* 0 none, else 1 + source */
static SDL_AtomicInt g_pick_song;      /* 0 all songs, else 1 + song */
static int g_ui_source, g_ui_song;
static int g_pick_row = -1, g_pick_wait, g_pick_hold;
static bool g_pick_told;
extern "C" void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
static uint32_t g_keep_menus, g_keep_loading;
static bool g_keep_valid;

static std::vector<size_t> music_folders(void)
{
    std::vector<size_t> out;
    if (SDL_GetAtomicInt(&g_music_ready) == 0) return out;
    for (size_t a = 0; a < g_albums.size() && out.size() < 100; a++)
        for (const Song &s : g_albums[a].songs)
            if (s.ready) { out.push_back(a); break; }
    return out;
}

static std::vector<size_t> folder_songs(size_t a)
{
    std::vector<size_t> out;
    for (size_t k = 0; k < g_albums[a].songs.size() && out.size() < kMaxPick; k++)
        if (g_albums[a].songs[k].ready) out.push_back(k);
    return out;
}

static void text_of(const std::u16string &s, char *out, size_t n)
{
    size_t i = 0;
    if (n > 49) n = 49;                /* the row's value column */
    for (; i < s.size() && i + 1 < n; i++) out[i] = s[i] < 0x80 ? (char)s[i] : '?';
    out[i] = 0;
    if (i < s.size() && i > 3) strcpy(out + i - 3, "...");
}

static void music_guest_call(void (*fn)(void), uint32_t ecx)
{
    g_ecx = ecx;
    g_esp -= 4;
    g32(g_esp) = 0xFEC0F000u;          /* any return address: fn returns to here in C */
    fn();
}

extern "C" void nfl2k5_music_frame_service(void)
{
    /* In a match, the attract demo or the title there is no menu music to
     * replace, and starting a song there is lost or starts nothing: a pick
     * waits for the menus, Next song is dropped. The Main Menu stays on the
     * screen stack through a match; the match's own screen 0x4E7EC0 sits above
     * it until the End Of Game menu is left. The title and demo have no Main
     * Menu yet. */
    uint32_t fsm_depth = g32(0x00A84B18u + 0x100u);
    bool under_main_menu = false, match_screen = false;
    for (uint32_t i = 0; i <= fsm_depth && i < 0x20u; i++) {
        uint32_t screen = g32(0x00A84B18u + i * 8u);
        if (screen == 0x00515660u) under_main_menu = true;
        if (screen == 0x004E7EC0u) match_screen = true;
    }
    /* A mod pack's interpreter pushes screens past the [FSM] push log. */
    static const bool log_top = getenv("NFL2K5_FSM_LOG") != nullptr;
    static uint32_t last_top = 0;
    uint32_t top_screen = fsm_depth < 0x20u ? g32(0x00A84B18u + fsm_depth * 8u) : 0u;
    if (log_top && top_screen != last_top) {
        last_top = top_screen;
        fprintf(stderr, "[FSM] top depth %u desc %08X\n", fsm_depth, top_screen);
        fflush(stderr);
    }
    if (!under_main_menu || match_screen) {
        if (SDL_SetAtomicInt(&g_skip_request, 0))
            xbox_PresentToast("MUSIC PLAYER", "Next song works in the menus", "", 3000);
        if (SDL_GetAtomicInt(&g_pick_request) && !g_pick_told) {
            g_pick_told = true;
            xbox_PresentToast("MUSIC PLAYER", "Your pick plays back in the menus", "", 3000);
        }
        return;
    }
    g_pick_told = false;
    if (g_pick_row >= 0 && g_pick_hold > 0) {
        g_pick_hold--;                                       /* a custom song's stream closes first */
    } else if (g_pick_row >= 0) {
        /* the bank player takes a moment to stop, and 0x280450 starts nothing
         * while it is busy (sub_000CF0F0) */
        uint32_t keep_eax = g_eax, keep_ebx = g_ebx, keep_esi = g_esi, keep_ecx = g_ecx, keep_edx = g_edx;
        g_edx = 0;
        music_guest_call(sub_000CF0F0, 0xC3CC10u);
        if (!g_eax || ++g_pick_wait > 240) {
            uint32_t row = (uint32_t)g_pick_row;
            g_pick_row = -1;
            g32(0xC3CC08u) = row ^ 1u;                       /* as a change of row: that path starts it, pack or not */
            music_guest_call(sub_002804C0, g32(0xAC9EC0u + row * 0x10u));   /* that row, by its name */
            if (!g32(0xC5D3A4u)) music_guest_call(sub_00280450, 0);  /* start it, if that did not */
            fprintf(stderr, "[MUSIC] F1 Music Player: row %u started after %d frames (player %u)\n",
                    row, g_pick_wait, g32(0xC5D3A4u));
        }
        g_eax = keep_eax; g_ebx = keep_ebx; g_esi = keep_esi; g_ecx = keep_ecx; g_edx = keep_edx;
    }
    if (SDL_SetAtomicInt(&g_skip_request, 0)) {
        uint32_t entry = g32(0xC3CBFCu);
        if (SDL_GetAtomicInt(&g_cur_id) >= 0 && entry && entry != 0xC3CBE8u) {
            /* a playlist song: straight to the next entry */
            uint32_t next = g32(entry + 0x10u);
            uint32_t keep_eax = g_eax, keep_ebx = g_ebx, keep_esi = g_esi, keep_ecx = g_ecx, keep_edx = g_edx;
            if (next == 0xC3CBE8u) next = g32(0xC3CBF8u);
            g32(0xC3CBFCu) = next;
            fprintf(stderr, "[MUSIC] F1 Music Player: next entry %08X\n", next);
            music_guest_call(sub_0027FEC0, 0);
            g_pick_row = 1;                                  /* and on, as a pick does */
            g_pick_wait = 0;
            g_pick_hold = 45;
            g_eax = keep_eax; g_ebx = keep_ebx; g_esi = keep_esi; g_ecx = keep_ecx; g_edx = keep_edx;
        } else {                                     /* the game's own music: an R3 press and release */
            nfl2k5_local_music_buttons(0, 1);
            nfl2k5_local_music_buttons(0, 0);
        }
    }
    int request = SDL_SetAtomicInt(&g_pick_request, 0);
    if (!request) return;
    int source = request - 1, song = SDL_GetAtomicInt(&g_pick_song);
    uint32_t first = g32(0xC3CBF8u), cur = 0, row = 1;
    uint32_t keep_eax = g_eax, keep_ebx = g_ebx, keep_esi = g_esi, keep_ecx = g_ecx, keep_edx = g_edx;
    if (!g_keep_valid) {
        g_keep_menus = g32(0xB9E298u);
        g_keep_loading = g32(0xB9E29Cu);
        g_keep_valid = true;
    }
    bool no_list = !first || first == 0xC3CBE8u || !g32(0xC3CC04u);   /* 0xC3CC04: entries in the list */
    if (source == 0) {
        cur = no_list ? 0 : first;
        row = 0;
        g32(0xB9E298u) = g_keep_menus;
        g32(0xB9E29Cu) = g_keep_loading;
    } else if (source == 1) {
        if (no_list) {
            banner_show(u"Music Player", u"The Jukebox playlist is empty");
            return;
        }
        cur = first;
    } else {
        std::vector<size_t> folders = music_folders();
        if ((size_t)(source - 2) >= folders.size()) return;
        std::vector<size_t> songs = folder_songs(folders[(size_t)(source - 2)]);
        if (songs.empty()) return;
        if (!g_pick_nodes) g_pick_nodes = xbox_HeapAlloc(kPickNode * kMaxPick, 16);
        if (!g_pick_nodes) return;
        for (size_t i = 0; i < songs.size(); i++) {
            uint32_t node = g_pick_nodes + (uint32_t)i * kPickNode;
            for (uint32_t o = 0; o < kPickNode; o += 4) g32(node + o) = 0;
            g32(node + 0x00) = 0x12u + (uint32_t)(source - 2);   /* the ST.DB's numbering */
            g32(node + 0x04) = (uint32_t)i;
            g_edx = (uint32_t)i;                             /* +8: the name check 0x27FF90 compares */
            music_guest_call(sub_0027F550, g32(node + 0x00));
            g32(node + 0x08) = g_eax;
            g32(node + 0x10) = g_pick_nodes + (uint32_t)((i + 1) % songs.size()) * kPickNode;
        }
        cur = g_pick_nodes + (uint32_t)(song > 0 && (size_t)(song - 1) < songs.size() ? song - 1 : 0) * kPickNode;
    }
    if (row) { g32(0xB9E298u) = 1; g32(0xB9E29Cu) = 1; }     /* Crib Music: Crib/Menus for now */
    if (cur) g32(0xC3CBFCu) = cur;
    fprintf(stderr, "[MUSIC] F1 Music Player: source %d song %d, entry %08X, row %u\n", source, song, cur, row);
    music_guest_call(sub_0027FEC0, 0);                       /* stop what plays */
    g_pick_row = (int)row;                                   /* and start when the player has let go */
    g_pick_wait = 0;
    g_eax = keep_eax; g_ebx = keep_ebx; g_esi = keep_esi; g_ecx = keep_ecx; g_edx = keep_edx;
}

static void now_playing_text(char *out, size_t n)
{
    int cur = SDL_GetAtomicInt(&g_cur_id);
    RetailBank bank = RETAIL_NONE;
    uint32_t mode = 0, index = 0;
    size_t a = (size_t)cur >> 16, k = (size_t)cur & 0xFFFF;
    if (cur >= 0 && SDL_GetAtomicInt(&g_music_ready) && a < g_albums.size() && k < g_albums[a].songs.size()) {
        const Song &s = g_albums[a].songs[k];
        std::u16string line = s.title;
        const std::u16string &by = s.artist.empty() ? g_albums[a].name : s.artist;
        if (!by.empty()) line += u" - " + by;
        text_of(line, out, n);
    } else if (retail_current(bank, mode, index)) {
        if (bank == RETAIL_CRIB) snprintf(out, n, "%s - %s", g_crib_tracks[index].title, g_crib_tracks[index].artist);
        else snprintf(out, n, "Original Menu Music %02u", index + 1);
    } else {
        snprintf(out, n, "Nothing");
    }
}

enum { MP_NOW, MP_NEXT, MP_SOURCE, MP_SONG, MP_PLAY, MP_ROWS };

static void mp_label(int row, char *name, char *val, size_t n)
{
    std::vector<size_t> folders = music_folders();
    val[0] = 0;
    if (g_ui_source >= (int)folders.size() + 2) g_ui_source = 0;
    switch (row) {
    case MP_NOW: strcpy(name, "Now playing"); now_playing_text(val, n); break;
    case MP_NEXT: strcpy(name, "Next song"); break;
    case MP_SOURCE:
        strcpy(name, "Playlist");
        if (g_ui_source == 0) snprintf(val, n, "Game music");
        else if (g_ui_source == 1) snprintf(val, n, "Jukebox playlist");
        else text_of(g_albums[folders[(size_t)(g_ui_source - 2)]].name, val, n);
        break;
    case MP_SONG:
        strcpy(name, "Song");
        if (g_ui_source < 2 || g_ui_song == 0) snprintf(val, n, "All songs");
        else {
            size_t a = folders[(size_t)(g_ui_source - 2)];
            std::vector<size_t> songs = folder_songs(a);
            if ((size_t)(g_ui_song - 1) < songs.size()) text_of(g_albums[a].songs[songs[(size_t)(g_ui_song - 1)]].title, val, n);
        }
        break;
    case MP_PLAY: strcpy(name, "Play"); break;
    default: name[0] = 0; break;
    }
}

static void mp_change(int row, int dir)
{
    std::vector<size_t> folders = music_folders();
    if (row == MP_SOURCE) {
        int count = (int)folders.size() + 2;
        g_ui_source = (g_ui_source + count + dir) % count;
        g_ui_song = 0;
    } else if (row == MP_SONG && g_ui_source >= 2 && (size_t)(g_ui_source - 2) < folders.size()) {
        int count = (int)folder_songs(folders[(size_t)(g_ui_source - 2)]).size() + 1;
        g_ui_song = (g_ui_song + count + dir) % count;
    }
    xbox_MenuInvalidate();
}

static void mp_activate(int row)
{
    if (row < 0) return;
    if (row == MP_NEXT) SDL_SetAtomicInt(&g_skip_request, 1);
    else if (row == MP_PLAY) {
        SDL_SetAtomicInt(&g_pick_song, g_ui_song);
        SDL_SetAtomicInt(&g_pick_request, g_ui_source + 1);
    } else mp_change(row, 1);
    xbox_MenuInvalidate();
}

static int mp_count(void) { return MP_ROWS; }

/* xbox_MenuAddPage's XboxMenuPage (nv2a_gpu_present.inc.c) */
static const struct {
    const char *title;
    int (*count)(void);
    void (*label)(int row, char *name, char *val, size_t n);
    void (*change)(int row, int dir);
    void (*activate)(int row);
    int (*busy)(void);
} k_music_page = { "MUSIC PLAYER", mp_count, mp_label, mp_change, mp_activate, nullptr };

extern "C" void nfl2k5_music_player_init(void)
{
    xbox_MenuAddPage(&k_music_page);
}

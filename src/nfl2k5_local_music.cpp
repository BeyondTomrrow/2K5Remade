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
    size_t total = 0, todo = 0;
    std::error_code ec;
    for (const fs::path &dir : list(g_music, true)) {
        Album al;
        al.name = u8_to_u16(dir.filename().string());
        for (const fs::path &f : list(dir, false)) {
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
    for (size_t a = 0; a < g_albums.size(); a++)
        for (size_t k = 0; k < g_albums[a].songs.size(); k++)
            if (g_albums[a].songs[k].ready) link_song(a, k);
    write_db();

    for (size_t a = 0; a < g_albums.size(); a++) {
        for (size_t k = 0; k < g_albums[a].songs.size(); k++) {
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

extern "C" void nfl2k5_local_music_start(const char *root, const char *save_dir)
{
    std::error_code ec;
    g_music = fs::path(root) / "Music";
    g_out   = fs::path(save_dir) / "Soundtracks";
    g_cache = g_out / "cache";
    fs::create_directories(save_dir, ec);
    fs::create_directories(g_out, ec);
    fs::create_directories(g_cache, ec);
    fs::create_directories(g_music, ec);

    SDL_Thread *t = SDL_CreateThread(worker, "local_music", nullptr);
    if (t) SDL_DetachThread(t);
}

/* ── Now playing / skip ────────────────────────────────────────────────── */
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
    size_t a = id >> 16, k = id & 0xFFFF;
    if (a >= g_albums.size() || k >= g_albums[a].songs.size()) return false;
    const Song &song = g_albums[a].songs[k];
    std::u16string sub = song.title;
    const std::u16string &by = song.artist.empty() ? g_albums[a].name : song.artist;
    if (!by.empty()) sub += u" - " + by;
    return banner_show(u"Now Playing", sub);
}

extern "C" void nfl2k5_local_music_opening(uint32_t song_id, uint32_t player)
{
    g_player = player;
    SDL_SetAtomicInt(&g_cur_id, (int)song_id);
    show_now_playing(song_id);
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
    return SDL_SetAtomicInt(&g_retail_next, 0) != 0;
}

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
    } else if (retail_live && r3 && !prev_r3) {
        SDL_SetAtomicInt(&g_retail_next, 1);
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
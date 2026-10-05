// Local music library: <GameRoot>/Music/<folder>/<songs> become Xbox custom
// soundtracks, so the game's own screens (Crib Jukebox, Stadium Music, Create
// Clip) list and play them through the game's own player. FOLDER = PLAYLIST;
// nothing to import or configure.
//
// The dashboard kept soundtracks as MUSIC\ST.DB plus MUSIC\<id>\<song>.WMA;
// the path layer maps that folder to <saves>/Soundtracks (kernel_path.c).
// ST.DB layout, as the title's XAPI reader (sub_003797DB..sub_00379BBD)
// reads it -- 0x200-byte blocks:
//   block 0       header: +0 version 1, +4 count, +8 next id, +0xC ids[100]
//   block 1+i     soundtrack i: magic 0x21371, id, song count,
//                 group ids[84], +0x15C total ms, +0x160 name WCHAR[32]
//   block 101+g   song group g: magic 0x31073, soundtrack id, group index, 0,
//                 +0x10 song ids[6], +0x28 ms[6], +0x40 names WCHAR[6][32]
//   song file     MUSIC\%04x\%08x.WMA, (song id >> 16, song id)
//
// Songs are converted once to WMA (nfl2k5_wma_encode.cpp) into a cache keyed
// by path, size and time, then hard-linked under their ids. Conversion runs
// on a background thread; ST.DB is rewritten after each song, so a library
// shows up at once and fills in while the game runs (the title re-reads ST.DB
// on every song-info call).
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "ole32.lib")

extern "C" long nfl2k5_wma_transcode(const wchar_t *src, const wchar_t *dst);
extern "C" int nfl2k5_asf_strip_tags(const wchar_t *path);

namespace {

struct Song {
    std::wstring src, cache, title, artist;
    uint32_t ms = 0;
    bool ready = false;
};
struct Album {
    std::wstring name;
    std::vector<Song> songs;
};

std::wstring g_music, g_out, g_cache;
std::vector<Album> g_albums;

std::wstring widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    for (auto &c : w) if (c == L'/') c = L'\\';
    return w;
}

bool is_audio(const std::wstring &name)
{
    static const wchar_t *ext[] = { L".mp3", L".wma", L".m4a", L".aac", L".flac", L".wav", L".ogg", L".mp4" };
    const wchar_t *e = PathFindExtensionW(name.c_str());
    for (auto x : ext) if (!_wcsicmp(e, x)) return true;
    return false;
}

std::vector<std::wstring> list(const std::wstring &dir, bool dirs)
{
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        bool d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (fd.cFileName[0] == L'.' || d != dirs) continue;
        if (dirs || is_audio(fd.cFileName)) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::wstring &a, const std::wstring &b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    });
    return out;
}

// "07 Artist - Title" -> "Title"; "Title" stays.
std::wstring title_from_name(const std::wstring &file)
{
    std::wstring t = file.substr(0, file.size() - wcslen(PathFindExtensionW(file.c_str())));
    size_t i = 0;
    while (i < t.size() && iswdigit(t[i])) i++;
    if (i && i < t.size()) {
        while (i < t.size() && (t[i] == L' ' || t[i] == L'.' || t[i] == L'-' || t[i] == L'_')) i++;
        t = t.substr(i);
    }
    size_t dash = t.find(L" - ");
    if (dash != std::wstring::npos) t = t.substr(dash + 3);
    return t;
}

// "07 Artist - Title" -> "Artist". Files without an artist keep it blank.
std::wstring artist_from_name(const std::wstring &file)
{
    std::wstring t = file.substr(0, file.size() - wcslen(PathFindExtensionW(file.c_str())));
    size_t i = 0;
    while (i < t.size() && iswdigit(t[i])) i++;
    if (i && i < t.size()) {
        while (i < t.size() && (t[i] == L' ' || t[i] == L'.' || t[i] == L'-' || t[i] == L'_')) i++;
        t = t.substr(i);
    }
    size_t dash = t.find(L" - ");
    return dash == std::wstring::npos ? L"" : t.substr(0, dash);
}

// Title and length from the file's tags (Windows' own property handlers).
void read_tags(Song &s)
{
    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(s.src.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Title, &v)) && v.vt == VT_LPWSTR && v.pwszVal && *v.pwszVal)
            s.title = v.pwszVal;
        PropVariantClear(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Music_Artist, &v))) {
            if (v.vt == VT_LPWSTR && v.pwszVal && *v.pwszVal)
                s.artist = v.pwszVal;
            else if (v.vt == (VT_VECTOR | VT_LPWSTR) && v.calpwstr.cElems &&
                     v.calpwstr.pElems && v.calpwstr.pElems[0])
                s.artist = v.calpwstr.pElems[0];
        }
        PropVariantClear(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Media_Duration, &v)) && v.vt == VT_UI8)
            s.ms = (uint32_t)(v.uhVal.QuadPart / 10000);
        PropVariantClear(&v);
        ps->Release();
    }
    if (s.title.empty()) s.title = title_from_name(PathFindFileNameW(s.src.c_str()));
    if (s.artist.empty()) s.artist = artist_from_name(PathFindFileNameW(s.src.c_str()));
}

std::wstring cache_name(const std::wstring &src)
{
    WIN32_FILE_ATTRIBUTE_DATA a = {};
    GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &a);
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t *)p)[i]; h *= 1099511628211ull; }
    };
    std::wstring lower = src;
    CharLowerBuffW(&lower[0], (DWORD)lower.size());
    mix(lower.data(), lower.size() * 2);
    mix(&a.nFileSizeLow, 4); mix(&a.nFileSizeHigh, 4); mix(&a.ftLastWriteTime, 8);
    wchar_t buf[32];
    swprintf_s(buf, L"%016llx.wma", (unsigned long long)h);
    return g_cache + L"\\" + buf;
}

void put_name(uint8_t *dst, const std::wstring &s)
{
    memset(dst, 0, 64);
    memcpy(dst, s.data(), std::min<size_t>(s.size(), 31) * 2);
}

// ST.DB from the songs converted so far; replaces the file in one step.
void write_db()
{
    std::vector<uint8_t> db(0x200 * 101, 0);
    std::vector<std::vector<uint8_t>> groups;
    uint32_t count = 0;
    for (size_t a = 0; a < g_albums.size() && count < 100; a++) {
        const Album &al = g_albums[a];
        std::vector<size_t> ready;
        for (size_t k = 0; k < al.songs.size(); k++) if (al.songs[k].ready) ready.push_back(k);
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

    std::wstring tmp = g_out + L"\\ST.DB.new", dst = g_out + L"\\ST.DB";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, db.data(), (DWORD)db.size(), &w, nullptr);
    CloseHandle(f);
    for (int i = 0; i < 50 && !MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING); i++)
        Sleep(20);   // the title may have ST.DB open for a moment
}

void link_song(size_t a, size_t k)
{
    nfl2k5_asf_strip_tags(g_albums[a].songs[k].cache.c_str());   /* caches from older builds */
    wchar_t dir[32], file[32];
    swprintf_s(dir, L"\\%04x", (unsigned)a);
    swprintf_s(file, L"\\%08x.WMA", (unsigned)((a << 16) | k));
    std::wstring d = g_out + dir, f = d + file;
    CreateDirectoryW(d.c_str(), nullptr);
    DeleteFileW(f.c_str());
    if (!CreateHardLinkW(f.c_str(), g_albums[a].songs[k].cache.c_str(), nullptr))
        CopyFileW(g_albums[a].songs[k].cache.c_str(), f.c_str(), FALSE);
}

DWORD WINAPI worker(LPVOID)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    size_t total = 0, todo = 0;
    for (auto &dir : list(g_music, true)) {
        Album al;
        al.name = dir;
        for (auto &f : list(g_music + L"\\" + dir, false)) {
            Song s;
            s.src = g_music + L"\\" + dir + L"\\" + f;
            s.cache = cache_name(s.src);
            read_tags(s);
            s.ready = GetFileAttributesW(s.cache.c_str()) != INVALID_FILE_ATTRIBUTES;
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
            std::wstring part = s.cache.substr(0, s.cache.size() - 4) + L".part.wma";
            long r = nfl2k5_wma_transcode(s.src.c_str(), part.c_str());
            if (r || !MoveFileExW(part.c_str(), s.cache.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                fprintf(stderr, "[MUSIC] could not convert %ls (0x%08lX)\n", s.src.c_str(), (unsigned long)r);
                DeleteFileW(part.c_str());
                continue;
            }
            if (!s.ms) {
                Song probe;
                probe.src = s.cache;
                read_tags(probe);
                s.ms = probe.ms;
            }
            s.ready = true;
            link_song(a, k);
            write_db();
        }
    }
    fprintf(stderr, "[MUSIC] library ready\n");
    CoUninitialize();
    return 0;
}

} // namespace

// root: the game root (holds Music/); save_dir: where Soundtracks/ lives.
extern "C" void nfl2k5_local_music_start(const char *root, const char *save_dir)
{
    g_music = widen(root) + L"\\Music";
    g_out = widen(save_dir) + L"\\Soundtracks";
    g_cache = g_out + L"\\cache";
    CreateDirectoryW(widen(save_dir).c_str(), nullptr);
    CreateDirectoryW(g_out.c_str(), nullptr);
    CreateDirectoryW(g_cache.c_str(), nullptr);
    CreateDirectoryW(g_music.c_str(), nullptr);
    HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (t) {
        SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(t);
    }
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

static volatile LONG g_cur_id = -1;
static volatile uint32_t g_player;
static volatile LONG g_retail_next;

/* NOW PLAYING in the game's own ESPN header bar. When a menu opens,
 * sub_000F3D60 stores its title at header + 0x598 (header = MEM32(menu FSM
 * 0xA84B18 + 0x10C)); + 0x59C is the subtitle in the grey strip (the
 * playbook screens put "Offense" there); setting 0xA83F14 makes the game
 * rebuild the bar from both on its own thread, with its own animation. We
 * swap in "Now Playing" / "<song> - <playlist>" and put the screen's own
 * title back after a few seconds -- unless the player has moved to another
 * screen meanwhile, which retitles the bar itself. Outside the menus (no
 * header) the overlay banner is used instead. */
extern "C" uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

static struct {
    uint32_t title_buf, sub_buf;           /* guest UTF-16 buffers */
    uint32_t hdr, saved_title, saved_sub;
    uint32_t fsm_index, fsm_stack[5];
    DWORD until;
    bool active;
} g_banner;
static std::wstring g_banner_sub_text;
static size_t g_banner_scroll_pos;
static DWORD g_banner_scroll_at;

static uint8_t *guest(void) { return (uint8_t *)xbox_GetMemoryOffset(); }
static volatile uint32_t &g32(uint32_t va) { return *(volatile uint32_t *)(guest() + va); }

static void put_guest_wstr(uint32_t va, const std::wstring &w, size_t max_chars)
{
    size_t n = std::min(w.size(), max_chars - 1);
    uint16_t *d = (uint16_t *)(guest() + va);
    for (size_t i = 0; i < n; i++) d[i] = (uint16_t)w[i];
    d[n] = 0;
}

static std::wstring get_guest_wstr(uint32_t va, size_t max_chars = 64)
{
    std::wstring out;
    if (!va || va >= 0x04000000u) return out;
    const uint16_t *s = (const uint16_t *)(guest() + va);
    for (size_t i = 0; i < max_chars && s[i]; i++) out.push_back((wchar_t)s[i]);
    return out;
}

static bool fsm_is_banner_origin(void)
{
    if (g32(0xA84B18u + 0x100u) != g_banner.fsm_index) return false;
    for (size_t i = 0; i < _countof(g_banner.fsm_stack); i++)
        if (g32(0xA84B18u + (uint32_t)i * 8u) != g_banner.fsm_stack[i]) return false;
    return true;
}

static bool native_title_is_main_menu(uint32_t hdr)
{
    std::wstring title = get_guest_wstr(g32(hdr + 0x598u));
    std::wstring key;
    for (wchar_t c : title) {
        if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) key += c;
    }
    return key == L"MAINMENU";
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

static std::wstring banner_sub_frame(void)
{
    const size_t width = 28;
    if (g_banner_sub_text.size() <= width) return g_banner_sub_text;
    const std::wstring loop = g_banner_sub_text + L"     ";
    std::wstring frame;
    frame.reserve(width);
    for (size_t i = 0; i < width; i++)
        frame.push_back(loop[(g_banner_scroll_pos + i) % loop.size()]);
    return frame;
}

static bool banner_show(const std::wstring &title, const std::wstring &sub)
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
        for (size_t i = 0; i < _countof(g_banner.fsm_stack); i++)
            g_banner.fsm_stack[i] = g32(0xA84B18u + (uint32_t)i * 8u);
    }
    g_banner_sub_text = sub;
    g_banner_scroll_pos = 0;
    g_banner_scroll_at = GetTickCount() + 900; /* brief readable hold before moving */
    put_guest_wstr(g_banner.title_buf, title, 64);
    put_guest_wstr(g_banner.sub_buf, banner_sub_frame(), 96);
    g32(hdr + 0x598u) = g_banner.title_buf;
    g32(hdr + 0x59Cu) = g_banner.sub_buf;
    g32(0xA83F14u) = 1;
    g_banner.active = true;
    g_banner.until = GetTickCount() + 5000;
    return true;
}

static void banner_tick(void)
{
    if (!g_banner.active) return;
    /* Restore MAIN MENU after five seconds. Entering a mode restores the
     * original heading immediately so NOW PLAYING never follows it. */
    if (on_main_menu() && (LONG)(GetTickCount() - g_banner.until) < 0) {
        DWORD now = GetTickCount();
        if (g_banner_sub_text.size() > 28 &&
            (LONG)(now - g_banner_scroll_at) >= 0) {
            g_banner_scroll_pos++;
            g_banner_scroll_at = now + 260;
            put_guest_wstr(g_banner.sub_buf, banner_sub_frame(), 96);
            g32(0xA83F14u) = 1;
        }
        return;
    }
    g_banner.active = false;
    /* Restore each field through the original header object. Team Select can
     * replace the current header while still retaining the main menu's grey
     * subtitle pointer, which previously left the marquee behind it. */
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
    std::wstring sub = song.title;
    const std::wstring &by = song.artist.empty() ? g_albums[a].name : song.artist;
    if (!by.empty()) sub += L" - " + by;
    /* Keep the header clean. The game's grey subtitle strip supplies its
     * own marquee for the complete, untruncated song title and artist. */
    return banner_show(L"Now Playing", sub);
}

extern "C" void nfl2k5_local_music_opening(uint32_t song_id, uint32_t player)
{
    g_player = player;
    InterlockedExchange(&g_cur_id, (LONG)song_id);
    show_now_playing(song_id);
}

extern "C" void nfl2k5_local_music_closed(void)
{
    InterlockedExchange(&g_cur_id, -1);
}

struct RetailTrack { const wchar_t *title, *artist; };

/* Retail cribmusic order transcribed from the game's own catalog.  The first
 * 20 are licensed/library tracks, 20-31 are the Dan & Steve outtakes, and
 * 32-58 include the songs shown in the game's jukebox. */
static const RetailTrack g_crib_tracks[] = {
    {L"Bounce", L"Big J"}, {L"Take Off", L"Big J"},
    {L"Crank It Up", L"Big J"}, {L"It Just Won't Be", L"Big J"},
    {L"Make The Stop", L"Opus 1"}, {L"Slow Down", L"Opus 1"},
    {L"Sportin'", L"Opus 1"}, {L"Two Three", L"Opus 1"},
    {L"2 Much", L"Mike Reagan"}, {L"Come On", L"Mike Reagan"},
    {L"Hands Up", L"Brad Cross"}, {L"Yo Groove", L"Brad Cross"},
    {L"Aw Yaz", L"Brad Cross"}, {L"Code Breaker", L"Opus 1"},
    {L"Dance It", L"Opus 1"}, {L"Tizaziz", L"Brad Cross"},
    {L"Can't Sit Still", L"The Danger"}, {L"Fly Eater", L"The Danger"},
    {L"I Ain't Used", L"The Danger"}, {L"Locket", L"The Danger"},
    {L"Outtake 1", L"Dan & Steve"}, {L"Outtake 2", L"Dan & Steve"},
    {L"Outtake 3", L"Dan & Steve"}, {L"Outtake 4", L"Dan & Steve"},
    {L"Outtake 5", L"Dan & Steve"}, {L"Outtake 6", L"Dan & Steve"},
    {L"Outtake 7", L"Dan & Steve"}, {L"Outtake 8", L"Dan & Steve"},
    {L"Outtake 9", L"Dan & Steve"}, {L"Outtake 10", L"Dan & Steve"},
    {L"Outtake 11", L"Dan & Steve"}, {L"Outtake 12", L"Dan & Steve"},
    {L"The Best", L"Raw Intel/RIC"}, {L"Get In Line", L"Raw Intel/RIC"},
    {L"Can't Go Wrong", L"Raw Intel/RIC"}, {L"Deep And Wide", L"Aceyalone"},
    {L"Ace Cowboy", L"Aceyalone"}, {L"Superstar", L"The Good Brothers"},
    {L"Try Me", L"J. Boogie"}, {L"Golden Nectar", L"J. Boogie"},
    {L"Le Sengre", L"J. Boogie"}, {L"All Pleasure", L"Recliner"},
    {L"Irish Bullfight", L"Recliner"}, {L"Gothic Voices", L"Concept"},
    {L"Angel Of Truth", L"Concept"}, {L"Evolution!", L"Concept"},
    {L"Drumbox", L"People Under the Stairs"},
    {L"Outrun", L"People Under the Stairs"},
    {L"Clean Living", L"Rjd2"}, {L"Pull Out Your Cut", L"MR. LIF"},
    {L"Like Smak", L"Raw Intel/RIC"}, {L"The God In Me", L"Aceyalone"},
    {L"Knock Me Down Girl", L"Slicker"}, {L"Oceanic Lullaby", L"J. Boogie"},
    {L"Making A Friend", L"Recliner"}, {L"Eternal Life", L"Concept"},
    {L"Disco Rout", L"Legowelt"},
    {L"Sound In A Dark Room", L"Telefon Tel Aviv"},
    {L"The Pharaoh", L"The Danger"},
};

enum RetailBank { RETAIL_NONE, RETAIL_MENU, RETAIL_CRIB };

static bool retail_current(RetailBank &bank, uint32_t &mode, uint32_t &index)
{
    /* The retail player has two context rows at AC9EC0.  +0C is the active
     * AUSB bank name pointer; C5D39C holds one current index per row. */
    mode = g32(0xC3CC08u);
    if (mode >= 2 || !g32(0xC3AC90u)) return false;
    uint32_t name = g32(0xAC9EC0u + mode * 0x10u + 0x0Cu);
    index = g32(0xC5D39Cu + mode * 4u);
    if (name == 0xE92A34u && index < _countof(g_crib_tracks)) {
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
        return banner_show(L"Now Playing",
                           std::wstring(song.title) + L" - " + song.artist);
    } else if (bank == RETAIL_MENU) {
        wchar_t sub[96];
        swprintf(sub, _countof(sub),
                 L"Original Menu Music %02u - NFL 2K5 Original Soundtrack", index + 1);
        return banner_show(L"Now Playing", sub);
    }
    return false;
}

/* Consumed inside the retail music frame routine.  The generated-code hook
 * stops the current stream and invokes the game's own 27F040 advance routine
 * on the guest thread, preserving its shuffle/repeat and cleanup rules. */
extern "C" int nfl2k5_retail_music_take_next(void)
{
    return InterlockedExchange(&g_retail_next, 0) != 0;
}

/* Host pad buttons, every poll: L3 and R3 act on their press. */
extern "C" void nfl2k5_local_music_buttons(int l3, int r3)
{
    static int prev_l3, prev_r3;
    static RetailBank last_bank = RETAIL_NONE;
    static RetailBank shown_bank = RETAIL_NONE;
    static uint32_t last_mode = ~0u, last_index = ~0u;
    static uint32_t shown_index = ~0u;
    static LONG shown_custom = -1;
    LONG cur = g_cur_id;
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
    /* A track often begins on the title screen, where the menu header does
     * not exist yet. Retry until the first real menu can display its name. */
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
    /* Retail menu music can remain active while the scorebug-based match
     * detector cools down after attract mode. */
    if (nfl2k5_in_match() && !retail_live) {
        prev_l3 = l3;
        prev_r3 = r3;
        return;
    }
    if (cur >= 0 && player && r3 && !prev_r3) {
        /* Pull the song's end point (player + 0x1004C) in to the start.
         * sub_00328460 compares it with the play position before every
         * decode and, once past it, ends the song the way it ends a clip
         * preview: last packet, stream discontinuity, then "finished". */
        volatile uint32_t *end_pos =
            (volatile uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + player + 0x1004Cu);
        *end_pos = 1;
    }
    else if (retail_live && r3 && !prev_r3) {
        InterlockedExchange(&g_retail_next, 1);
    }
    if (cur >= 0 && l3 && !prev_l3)
        show_now_playing((uint32_t)cur);
    else if (cur < 0 && retail_live && l3 && !prev_l3)
        show_retail_now_playing(retail_bank, retail_index);
    else if (l3 && !prev_l3 && getenv("NFL2K5_BANNER_TEST"))   /* debug: banner without a song */
        banner_show(L"Now Playing", L"Ramblin' Man From Gramblin - Sam Spence & Da Riffs");
    prev_l3 = l3;
    prev_r3 = r3;
}


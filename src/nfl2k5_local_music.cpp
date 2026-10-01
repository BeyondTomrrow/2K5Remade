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
    std::wstring src, cache, title;
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
        if (SUCCEEDED(ps->GetValue(PKEY_Media_Duration, &v)) && v.vt == VT_UI8)
            s.ms = (uint32_t)(v.uhVal.QuadPart / 10000);
        PropVariantClear(&v);
        ps->Release();
    }
    if (s.title.empty()) s.title = title_from_name(PathFindFileNameW(s.src.c_str()));
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

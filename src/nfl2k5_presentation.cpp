/* Broadcast presentations: custom scorebugs, their animations and the
 * broadcast theme music, defined by mods/presentations/<name>/presentation.json.
 *
 * The scorebug is drawn with Direct2D/DirectWrite into a premultiplied BGRA
 * image at display resolution and composited by the presenter's HUD layer
 * (nv2a_gpu_present.inc.c, xbox_PresentSetHudCallback); it is redrawn only
 * when something on it changes or an animation runs. Match state is read
 * from the game's globals (addresses from 2K5 Mod Studio's reverse
 * engineering, see docs below). The game's own ESPN bug is hidden through a
 * hook after its per-frame update (tools/apply-gen-patches.py,
 * SCOREBUG_NATIVE_HOOK), which also tells us when the game wants a scorebug
 * on screen at all (not during replays, cut scenes, menus).
 *
 * Team names, colours and scorebug logos come from mods/teams/<ABBR>/team.json
 * and mods/teams/<ABBR>/logos/scorebug.png.
 *
 * Settings (package, intro and outro theme, animations, theme volume) are in
 * mods/presentation.ini, edited in game at Coach Match Up > Presentation
 * (src/nfl2k5_video_menu.c).
 *
 * Test switches: NFL2K5_PRES_TEST=1 draws the bug with made-up state
 * everywhere; NFL2K5_PRES_EVENT=<animation> replays that animation every few
 * seconds; NFL2K5_PRES_LOG=1 logs the raw match state. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

extern "C" {
extern ptrdiff_t g_xbox_mem_offset;
typedef struct { int bb_w, bb_h; float game_x, game_y, game_w, game_h; } XboxHudFrame;
typedef struct { const void *pixels; int w, h, stride, x, y, changed, dw, dh; } XboxHudImage;
typedef int (*XboxHudCallback)(const XboxHudFrame *f, XboxHudImage *img);
void xbox_PresentSetHudCallback(XboxHudCallback cb);
void xbox_AudioSetGameGain(float gain);
void xbox_AudioMuteGameMusic(int mute);
void xbox_AudioSetCommentaryGain(float gain);
void nfl2k5_input_press_a(unsigned ms);
float xbox_AudioGameLevel(void);
void xbox_AudioSetCaptureMix(void (*mix)(int16_t *stereo, int frames));
}

/* ======================================================================
 * JSON (enough for presentation.json / team.json)
 * ====================================================================== */
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal *get(const char *k) const {
        if (t != Obj) return nullptr;
        for (auto &kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    double num(const char *k, double d) const { const JVal *v = get(k); return v && v->t == Num ? v->n : d; }
    std::string str(const char *k, const char *d = "") const { const JVal *v = get(k); return v && v->t == Str ? v->s : d; }
    bool flag(const char *k, bool d) const { const JVal *v = get(k); return v && v->t == Bool ? v->b : d; }
};

class JParser {
public:
    explicit JParser(const std::string &text) : p(text.c_str()), e(text.c_str() + text.size()) {}
    bool parse(JVal &out) { ws(); return value(out); }
private:
    const char *p, *e;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
    bool lit(const char *w) { size_t n = strlen(w); if ((size_t)(e - p) < n || memcmp(p, w, n)) return false; p += n; return true; }
    bool string(std::string &out) {
        if (p >= e || *p != '"') return false;
        p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 1; i <= 4 && p + i < e; i++) {
                        char c = p[i]; cp <<= 4;
                        cp |= (unsigned)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
                    }
                    p += 4;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: out += *p; break;
                }
                p++;
            } else {
                out += *p++;
            }
        }
        if (p >= e) return false;
        p++;
        return true;
    }
    bool value(JVal &v) {
        ws();
        if (p >= e) return false;
        if (*p == '{') {
            v.t = JVal::Obj; p++; ws();
            if (p < e && *p == '}') { p++; return true; }
            for (;;) {
                std::string k; JVal item;
                ws(); if (!string(k)) return false;
                ws(); if (p >= e || *p != ':') return false; p++;
                if (!value(item)) return false;
                v.o.emplace_back(std::move(k), std::move(item));
                ws(); if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; return true; }
                return false;
            }
        }
        if (*p == '[') {
            v.t = JVal::Arr; p++; ws();
            if (p < e && *p == ']') { p++; return true; }
            for (;;) {
                JVal item;
                if (!value(item)) return false;
                v.a.push_back(std::move(item));
                ws(); if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; return true; }
                return false;
            }
        }
        if (*p == '"') { v.t = JVal::Str; return string(v.s); }
        if (lit("true")) { v.t = JVal::Bool; v.b = true; return true; }
        if (lit("false")) { v.t = JVal::Bool; v.b = false; return true; }
        if (lit("null")) { v.t = JVal::Null; return true; }
        char *end = nullptr;
        v.n = strtod(p, &end);
        if (end == p) return false;
        v.t = JVal::Num; p = end;
        return true;
    }
};

static bool read_file(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static bool load_json(const std::string &path, JVal &out)
{
    std::string text;
    if (!read_file(path, text)) return false;
    JParser jp(text);
    if (!jp.parse(out)) { fprintf(stderr, "[PRES] JSON error in %s\n", path.c_str()); return false; }
    return true;
}

static std::wstring widen(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static bool file_exists(const std::string &p)
{
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string upper(std::string s) { for (auto &c : s) c = (char)toupper((unsigned char)c); return s; }

/* ======================================================================
 * Guest memory and match state
 *
 * Globals (2K5 Mod Studio: nfl2k5_overtime.py, nfl2k5_scorebug_runtime.py,
 * nfl2k5_uniform_choice.py):
 *   E6028C -> clock object, +0x10 game clock (float seconds)
 *   E602B0 period length (float seconds)      E602C4 period 1..4, 5+ = OT
 *   E602B4 phase: 0 pregame/toss, 1 safety kick, 2 kickoff, 3 PAT, 4 scrimmage
 *   E5FC20 home team object, E5FC60 away; +8 -> score object {points, timeouts}
 *   E60280 team object with the ball
 *   E602EC -> down state: +4 down, +0x18 ball, +0x28 line to gain
 *   B307D0 / B30810 home / away playbook file name, UTF-16 "SF-pb.iff": the
 *                   team code before the '-' (the abbreviation pointers
 *                   B3096C / B30B60 are empty during a match)
 *   A95A00 native scorebug shown (written by its update, 0xFC9C0)
 * ====================================================================== */
/* Low memory (image, heap) or the contiguous window at 0x80000000 where
 * the game's pools and scenes live. */
static bool guest_va_ok(uint32_t va)
{
    return (va >= 0x10000u && va <= 0x0FFFFFF0u) || (va >= 0x80000000u && va <= 0x8FFFFFF0u);
}

static bool gread(uint32_t va, void *out, size_t n)
{
    if (!guest_va_ok(va)) return false;
    __try {
        memcpy(out, (const void *)((uintptr_t)va + g_xbox_mem_offset), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uint32_t rd32(uint32_t va) { uint32_t v = 0; gread(va, &v, 4); return v; }
static float rdf(uint32_t va) { float v = 0; gread(va, &v, 4); return v; }

struct TeamState { int score = 0, timeouts = 0; std::string abbr; };
struct GameState {
    bool valid = false;
    int period = 0, phase = -1, down = 0, poss = 0;   /* poss: 0 none, 1 away, 2 home */
    float clock = 0, period_len = 0, ball = 0, line = 0;
    TeamState t[2];                                     /* 0 away, 1 home */
};

enum { AWAY = 0, HOME = 1 };
static const uint32_t k_team_obj[2] = { 0x00E5FC60u, 0x00E5FC20u };
static const uint32_t k_playbook_name[2] = { 0x00B30810u, 0x00B307D0u };

static std::string read_team_code(uint32_t va)
{
    uint16_t w[8] = { 0 };
    std::string out;
    if (!gread(va, w, sizeof w)) return out;
    for (int i = 0; i < 7 && w[i] && w[i] != '-'; i++) {
        if (w[i] < 32 || w[i] > 126) return "";
        out += (char)w[i];
    }
    return out;
}

static bool read_state(GameState &g)
{
    g = GameState();
    uint32_t clk = rd32(0x00E6028Cu);
    if (!clk) return false;
    g.clock = rdf(clk + 0x10);
    g.period = (int)rd32(0x00E602C4u);
    g.period_len = rdf(0x00E602B0u);
    g.phase = (int)rd32(0x00E602B4u);
    /* Period 0 is the pregame show: the match is loaded, the clock set. */
    if (g.period < 0 || g.period > 12 || !(g.clock >= 0.0f && g.clock < 4000.0f)) return false;
    for (int s = 0; s < 2; s++) {
        uint32_t sc = rd32(k_team_obj[s] + 8);
        if (!sc) return false;
        g.t[s].score = (int)rd32(sc);
        g.t[s].timeouts = (int)rd32(sc + 4);
        g.t[s].abbr = read_team_code(k_playbook_name[s]);
        if (g.t[s].score < 0 || g.t[s].score > 250 || g.t[s].timeouts < 0 || g.t[s].timeouts > 9) return false;
    }
    uint32_t p = rd32(0x00E60280u);
    g.poss = p == k_team_obj[HOME] ? 2 : p == k_team_obj[AWAY] ? 1 : 0;
    uint32_t ds = rd32(0x00E602ECu);
    if (ds) {
        g.down = (int)rd32(ds + 4);
        g.ball = rdf(ds + 0x18);
        g.line = rdf(ds + 0x28);
    }
    g.valid = true;
    return true;
}

/* ======================================================================
 * Teams (mods/teams)
 * ====================================================================== */
struct TeamInfo {
    std::string abbr, city, name, logo;
    D2D1_COLOR_F primary{ 0.3f, 0.3f, 0.34f, 1 }, secondary{ 0.8f, 0.8f, 0.8f, 1 }, text{ 1, 1, 1, 1 };
};

static bool parse_hex(const std::string &s, D2D1_COLOR_F &c)
{
    if (s.size() < 7 || s[0] != '#') return false;
    unsigned v = 0, a = 255;
    if (sscanf(s.c_str() + 1, "%6x", &v) != 1) return false;
    if (s.size() >= 9) sscanf(s.c_str() + 7, "%2x", &a);
    c = D2D1::ColorF(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a / 255.0f);
    return true;
}

static std::map<std::string, TeamInfo> s_teams;   /* by upper-case abbreviation or alias */

static void load_teams()
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"mods\\teams\\*", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        char dir8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, dir8, sizeof dir8, nullptr, nullptr);
        std::string dir = std::string("mods/teams/") + dir8;
        JVal j;
        if (!load_json(dir + "/team.json", j)) continue;
        TeamInfo t;
        t.abbr = j.str("abbreviation", dir8);
        t.city = j.str("city");
        t.name = j.str("name", t.abbr.c_str());
        if (const JVal *c = j.get("colors")) {
            parse_hex(c->str("primary"), t.primary);
            parse_hex(c->str("secondary"), t.secondary);
            parse_hex(c->str("text", "#FFFFFF"), t.text);
        }
        std::string logo = "logos/scorebug.png";
        if (const JVal *l = j.get("logos")) logo = l->str("scorebug", logo.c_str());
        if (file_exists(dir + "/" + logo)) t.logo = dir + "/" + logo;
        s_teams[upper(t.abbr)] = t;
        s_teams[upper(dir8)] = t;
        if (const JVal *al = j.get("aliases"))
            for (auto &a : al->a) if (a.t == JVal::Str) s_teams[upper(a.s)] = t;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    fprintf(stderr, "[PRES] %zu team entries from mods/teams\n", s_teams.size());
}

static TeamInfo team_info(const std::string &abbr)
{
    auto it = s_teams.find(upper(abbr));
    if (it != s_teams.end()) return it->second;
    TeamInfo t;
    t.abbr = t.name = abbr.empty() ? "---" : abbr;
    return t;
}

/* ======================================================================
 * Packages and settings
 * ====================================================================== */
struct Song { std::string title, file; };
struct Package {
    std::string name, dir;
    JVal root;
    std::vector<Song> intro, outro;
};

static std::vector<Package> s_pkgs;           /* [0] is the game's own ESPN presentation */
static std::atomic<int> s_sel_pkg{ 0 }, s_sel_intro{ 1 }, s_sel_outro{ 1 }, s_sel_anim{ 1 }, s_sel_vol{ 8 };

static void load_packages()
{
    Package def;
    def.name = "ESPN (Original)";
    s_pkgs.push_back(def);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"mods\\presentations\\*", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        char dir8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, dir8, sizeof dir8, nullptr, nullptr);
        Package p;
        p.dir = std::string("mods/presentations/") + dir8;
        if (!load_json(p.dir + "/presentation.json", p.root)) continue;
        p.name = p.root.str("name", dir8);
        if (const JVal *m = p.root.get("music")) {
            for (int k = 0; k < 2; k++)
                if (const JVal *list = m->get(k ? "outro" : "intro"))
                    for (auto &s : list->a) {
                        Song song{ s.str("title"), s.str("file") };
                        if (song.title.empty()) song.title = song.file;
                        (k ? p.outro : p.intro).push_back(song);
                    }
        }
        fprintf(stderr, "[PRES] package \"%s\" (%zu intro, %zu outro themes)\n",
                p.name.c_str(), p.intro.size(), p.outro.size());
        s_pkgs.push_back(std::move(p));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static const char *k_ini = "mods\\presentation.ini";

static void save_settings()
{
    FILE *f = fopen(k_ini, "w");
    if (!f) return;
    int pk = s_sel_pkg;
    fprintf(f, "; Broadcast presentation (Coach Match Up > Presentation edits this)\n");
    fprintf(f, "package = %s\nintro = %d\noutro = %d\nanimations = %d\ntheme_volume = %d\n",
            pk > 0 && pk < (int)s_pkgs.size() ? s_pkgs[pk].name.c_str() : "ESPN (Original)",
            (int)s_sel_intro, (int)s_sel_outro, (int)s_sel_anim, (int)s_sel_vol * 10);
    fclose(f);
}

static void load_settings()
{
    FILE *f = fopen(k_ini, "r");
    char line[512];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char key[64], val[400];
        if (line[0] == ';' || sscanf(line, " %63[a-z_] = %399[^\r\n]", key, val) != 2) continue;
        if (!strcmp(key, "package")) {
            for (size_t i = 0; i < s_pkgs.size(); i++) if (s_pkgs[i].name == val) s_sel_pkg = (int)i;
        } else if (!strcmp(key, "intro")) s_sel_intro = atoi(val);
        else if (!strcmp(key, "outro")) s_sel_outro = atoi(val);
        else if (!strcmp(key, "animations")) s_sel_anim = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "theme_volume")) s_sel_vol = std::max(0, std::min(10, atoi(val) / 10));
    }
    fclose(f);
}

/* Menu API (src/nfl2k5_video_menu.c). Index 0 of a theme list is "Off". */
extern "C" int nfl2k5_pres_package_count(void) { return (int)s_pkgs.size(); }
extern "C" const char *nfl2k5_pres_package_name(int i) { return i >= 0 && i < (int)s_pkgs.size() ? s_pkgs[i].name.c_str() : ""; }
extern "C" int nfl2k5_pres_get(int what)
{
    switch (what) {
    case 0: return s_sel_pkg;
    case 1: return s_sel_intro;
    case 2: return s_sel_outro;
    case 3: return s_sel_anim;
    case 4: return s_sel_vol;
    }
    return 0;
}
static const std::vector<Song> &songs(int which)
{
    static const std::vector<Song> none;
    int pk = s_sel_pkg;
    if (pk <= 0 || pk >= (int)s_pkgs.size()) return none;
    return which == 1 ? s_pkgs[pk].intro : s_pkgs[pk].outro;
}
extern "C" int nfl2k5_pres_values(int what)
{
    switch (what) {
    case 0: return (int)s_pkgs.size();
    case 1: case 2: return 1 + (int)songs(what).size();
    case 3: return 2;
    case 4: return 11;
    }
    return 1;
}
extern "C" void nfl2k5_pres_value_name(int what, int idx, char *buf, size_t n)
{
    buf[0] = 0;
    switch (what) {
    case 0: snprintf(buf, n, "%s", nfl2k5_pres_package_name(idx)); break;
    case 1: case 2: {
        const auto &l = songs(what);
        if (idx <= 0 || idx > (int)l.size()) snprintf(buf, n, "Off");
        else snprintf(buf, n, "%s", l[idx - 1].title.c_str());
        break;
    }
    case 3: snprintf(buf, n, "%s", idx ? "On" : "Off"); break;
    case 4: snprintf(buf, n, "%d%%", idx * 10); break;
    }
}
extern "C" void nfl2k5_pres_set(int what, int v)
{
    int n = nfl2k5_pres_values(what);
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    switch (what) {
    case 0:
        s_sel_pkg = v;
        s_sel_intro = std::min(1, nfl2k5_pres_values(1) - 1);    /* first theme of the new package */
        s_sel_outro = std::min(1, nfl2k5_pres_values(2) - 1);
        break;
    case 1: s_sel_intro = v; break;
    case 2: s_sel_outro = v; break;
    case 3: s_sel_anim = v; break;
    case 4: s_sel_vol = v; break;
    }
    save_settings();
}

/* ======================================================================
 * Theme music (Media Foundation decode -> XAudio2)
 * ====================================================================== */
struct Music {
    IXAudio2 *xa = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    IXAudio2SourceVoice *voice = nullptr;
    std::vector<int16_t> pcm;
    std::mutex lock;
    std::atomic<int> state{ 0 };     /* 0 idle, 1 decoding, 2 playing, 3 fading */
    std::atomic<int> generation{ 0 };
    float volume = 0.9f, fade_time = 2.5f, duck = 1.0f;
    float cur_vol = 0;              /* volume now, for the recording tap */
    float talk_duck = 0.35f;        /* theme level while the game is loud (announcers) */
    float talk_level = 0.02f;       /* game RMS counted as "talking" */
    float duck_now = 1.0f;
    int mute_game_music = 1;       /* the theme replaces the game's own music */
    size_t cap_pos = 0;             /* recording tap cursor into pcm */
    double fade_start = 0;
} s_music;

static double now_s()
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

/* MF_SOURCE_READER_ENABLE_AUDIO_PROCESSING (Windows 8+; the SDK hides it
 * behind the project's older target version): lets the reader resample. */
static const GUID k_mf_audio_processing =
    { 0xfb394f3d, 0xccf1, 0x42ee, { 0xbb, 0xb3, 0xf9, 0xb8, 0x45, 0xd5, 0x68, 0x1d } };

static bool decode_mp3(const std::string &path, std::vector<int16_t> &out)
{
    IMFSourceReader *rd = nullptr;
    IMFAttributes *attr = nullptr;
    IMFMediaType *mt = nullptr;
    bool ok = false;
    MFCreateAttributes(&attr, 1);
    attr->SetUINT32(k_mf_audio_processing, TRUE);
    if (SUCCEEDED(MFCreateSourceReaderFromURL(widen(path).c_str(), attr, &rd))) {
        MFCreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
        if (SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt))) {
            for (;;) {
                DWORD flags = 0;
                IMFSample *smp = nullptr;
                if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &smp))) break;
                if (smp) {
                    IMFMediaBuffer *buf = nullptr;
                    if (SUCCEEDED(smp->ConvertToContiguousBuffer(&buf))) {
                        BYTE *data; DWORD len;
                        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                            size_t at = out.size();
                            out.resize(at + len / 2);
                            memcpy(&out[at], data, len & ~1u);
                            buf->Unlock();
                        }
                        buf->Release();
                    }
                    smp->Release();
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            }
            ok = !out.empty();
        }
        mt->Release();
        rd->Release();
    }
    attr->Release();
    return ok;
}

static void music_stop_now()
{
    std::lock_guard<std::mutex> g(s_music.lock);
    if (s_music.voice) { s_music.voice->Stop(); s_music.voice->FlushSourceBuffers(); s_music.voice->DestroyVoice(); s_music.voice = nullptr; }
    s_music.state = 0;
    xbox_AudioSetGameGain(1.0f);
    xbox_AudioMuteGameMusic(0);
}

static void music_play(const std::string &path, float volume, float duck, float fade)
{
    music_stop_now();
    int gen = ++s_music.generation;
    s_music.state = 1;
    s_music.volume = volume;
    s_music.duck = duck;
    s_music.fade_time = fade;
    std::thread([path, gen]() {
        std::vector<int16_t> pcm;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool ok = decode_mp3(path, pcm);
        CoUninitialize();
        std::lock_guard<std::mutex> g(s_music.lock);
        if (gen != s_music.generation || s_music.state != 1) return;
        if (!ok) { fprintf(stderr, "[PRES] could not decode %s\n", path.c_str()); s_music.state = 0; return; }
        if (!s_music.xa) {
            if (FAILED(XAudio2Create(&s_music.xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
                FAILED(s_music.xa->CreateMasteringVoice(&s_music.master))) { s_music.state = 0; return; }
        }
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 48000;
        wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 48000 * 4;
        if (FAILED(s_music.xa->CreateSourceVoice(&s_music.voice, &wf))) { s_music.state = 0; return; }
        s_music.pcm.swap(pcm);
        XAUDIO2_BUFFER b = {};
        b.AudioBytes = (UINT32)(s_music.pcm.size() * 2);
        b.pAudioData = (const BYTE *)s_music.pcm.data();
        b.Flags = XAUDIO2_END_OF_STREAM;
        s_music.voice->SubmitSourceBuffer(&b);
        s_music.voice->SetVolume(s_music.volume);
        s_music.cur_vol = s_music.volume;
        s_music.cap_pos = 0;
        s_music.voice->Start();
        s_music.state = 2;
        xbox_AudioSetGameGain(s_music.duck);
        xbox_AudioMuteGameMusic(s_music.mute_game_music);
        fprintf(stderr, "[PRES] t=%lu theme playing: %s (%.0f s)\n", GetTickCount(), path.c_str(), s_music.pcm.size() / 96000.0);
    }).detach();
}

static void music_fade()
{
    if (s_music.state == 2) { s_music.fade_start = now_s(); s_music.state = 3; }
    else if (s_music.state == 1) { ++s_music.generation; s_music.state = 0; }
}

static void music_tick()
{
    int st = s_music.state;
    if (st < 2) return;
    std::lock_guard<std::mutex> g(s_music.lock);
    if (!s_music.voice) return;
    XAUDIO2_VOICE_STATE vs;
    s_music.voice->GetState(&vs, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (vs.BuffersQueued == 0) {       /* finished */
        s_music.voice->DestroyVoice(); s_music.voice = nullptr;
        s_music.state = 0;
        xbox_AudioSetGameGain(1.0f);
        xbox_AudioMuteGameMusic(0);
        return;
    }
    {
        /* Broadcast bed: the theme dips under the announcers. Game level
         * above talk_level pulls it down to talk_duck (fast), and it comes
         * back up over about a second in the pauses. */
        float lvl = xbox_AudioGameLevel();
        float want = lvl > s_music.talk_level ? s_music.talk_duck : 1.0f;
        s_music.duck_now += (want - s_music.duck_now) * (want < s_music.duck_now ? 0.25f : 0.03f);
    }
    if (st == 2) {
        s_music.cur_vol = s_music.volume * s_music.duck_now;
        s_music.voice->SetVolume(s_music.cur_vol);
    }
    if (st == 3) {
        float k = (float)((now_s() - s_music.fade_start) / std::max(0.05f, s_music.fade_time));
        if (k >= 1.0f) {
            s_music.voice->Stop(); s_music.voice->FlushSourceBuffers();
            s_music.voice->DestroyVoice(); s_music.voice = nullptr;
            s_music.state = 0;
            xbox_AudioSetGameGain(1.0f);
            xbox_AudioMuteGameMusic(0);
            return;
        }
        s_music.cur_vol = s_music.volume * s_music.duck_now * (1.0f - k);
        s_music.voice->SetVolume(s_music.cur_vol);
        xbox_AudioSetGameGain(s_music.duck + (1.0f - s_music.duck) * k);
    }
}

/* RECOMP_AUDIO_WAV recordings get the theme too (apu_xaudio2.c tap). */
static void capture_mix(int16_t *buf, int frames)
{
    std::lock_guard<std::mutex> g(s_music.lock);
    if (!s_music.voice || s_music.state < 2) return;
    float v = s_music.cur_vol;
    for (int i = 0; i < frames * 2 && s_music.cap_pos < s_music.pcm.size(); i++, s_music.cap_pos++) {
        int x = buf[i] + (int)(s_music.pcm[s_music.cap_pos] * v);
        buf[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}

/* ======================================================================
 * Pregame open video (presentation.json "pregame_video"): replaces the
 * game's studio segment (Berman) with the package's own open. Decoded by
 * Media Foundation on a thread in real time to BGRA frames; shown full screen
 * through the HUD layer; its audio plays through the theme voice.
 * ====================================================================== */
static const GUID k_mf_advanced_video =
    { 0x0f81da2c, 0xb537, 0x4672, { 0xa8, 0xb2, 0xa6, 0x81, 0xb1, 0x73, 0x07, 0xa3 } };

struct Video {
    std::mutex lock;
    std::vector<uint8_t> frame;      /* BGRA, top-down, w*h*4 */
    int w = 0, h = 0;
    bool fresh = false;
    std::atomic<int> state{ 0 };     /* 0 idle, 1 playing, 2 finished */
    std::atomic<int> generation{ 0 };
} s_video;

static void video_thread(std::string path, int gen)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMFAttributes *attr = nullptr;
    IMFSourceReader *rd = nullptr;
    MFCreateAttributes(&attr, 1);
    attr->SetUINT32(k_mf_advanced_video, TRUE);
    if (SUCCEEDED(MFCreateSourceReaderFromURL(widen(path).c_str(), attr, &rd))) {
        IMFMediaType *mt = nullptr, *cur = nullptr;
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        MFCreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt)) &&
            SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
            UINT32 w = 0, h = 0;
            INT32 stride = 0;
            MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
            if (FAILED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&stride))) stride = (INT32)w * 4;
            double t0 = now_s();
            fprintf(stderr, "[PRES] pregame video %ux%u: %s\n", w, h, path.c_str());
            while (gen == s_video.generation) {
                DWORD flags = 0;
                LONGLONG ts = 0;
                IMFSample *smp = nullptr;
                if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &smp))) break;
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (smp) smp->Release(); break; }
                if (!smp) continue;
                double due = t0 + ts / 1e7;
                while (now_s() < due && gen == s_video.generation) Sleep(2);
                IMFMediaBuffer *buf = nullptr;
                if (SUCCEEDED(smp->ConvertToContiguousBuffer(&buf))) {
                    BYTE *data; DWORD len;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        std::lock_guard<std::mutex> g(s_video.lock);
                        s_video.w = (int)w; s_video.h = (int)h;
                        s_video.frame.resize((size_t)w * h * 4);
                        for (UINT32 y = 0; y < h; y++) {
                            const BYTE *src = stride >= 0 ? data + (size_t)y * stride : data + (size_t)(h - 1 - y) * (size_t)(-stride);
                            BYTE *dst = &s_video.frame[(size_t)y * w * 4];
                            memcpy(dst, src, (size_t)w * 4);
                            for (UINT32 x = 0; x < w; x++) dst[x * 4 + 3] = 255;
                        }
                        s_video.fresh = true;
                        buf->Unlock();
                    }
                    buf->Release();
                }
                smp->Release();
            }
            cur->Release();
        }
        mt->Release();
        rd->Release();
    } else {
        fprintf(stderr, "[PRES] could not open video %s\n", path.c_str());
    }
    attr->Release();
    CoUninitialize();
    if (gen == s_video.generation) s_video.state = 2;
}

static void video_play(const std::string &path)
{
    int gen = ++s_video.generation;
    s_video.state = 1;
    std::thread(video_thread, path, gen).detach();
}

/* ======================================================================
 * Events
 * ====================================================================== */
struct Event { std::string name; int team; std::string ended_quarter; };
struct ActiveAnim { Event ev; const JVal *def = nullptr; double t0 = 0, duration = 0; };

static std::vector<Event> s_queue;
static ActiveAnim s_anim;
static bool s_anim_on;

static const char *ordinal(int period)
{
    static const char *q[] = { "", "1ST", "2ND", "3RD", "4TH" };
    return period >= 1 && period <= 4 ? q[period] : "OT";
}

static void detect_events(const GameState &p, const GameState &g)
{
    for (int s = 0; s < 2; s++) {
        int d = g.t[s].score - p.t[s].score;
        if (d > 0) {
            const char *name = d >= 6 ? "touchdown" : d == 3 ? "field_goal" : d == 1 ? "extra_point"
                             : d == 2 ? (p.phase == 3 ? "two_point" : "safety") : nullptr;
            if (name) s_queue.push_back({ name, s, "" });
        }
        if (g.t[s].timeouts < p.t[s].timeouts && g.period == p.period)
            s_queue.push_back({ "timeout", s, "" });
    }
    if (g.period == p.period + 1 && p.period >= 1) {
        s_queue.push_back({ p.period == 2 ? "halftime" : "end_of_quarter", g.poss ? g.poss - 1 : HOME,
                            std::string(ordinal(p.period)) + (p.period <= 4 ? " QUARTER" : "") });
    }
    if ((g.period == 2 || g.period == 4) && g.period == p.period && p.clock > 120.0f && g.clock <= 120.0f)
        s_queue.push_back({ "two_minute_warning", g.poss ? g.poss - 1 : HOME, "" });
    if (g.phase == 4 && p.phase == 4 && p.down > 1 && g.down == 1 && g.poss == p.poss &&
        g.t[0].score == p.t[0].score && g.t[1].score == p.t[1].score && g.poss)
        s_queue.push_back({ "first_down", g.poss - 1, "" });
}

/* ======================================================================
 * Rendering
 * ====================================================================== */
struct Renderer {
    ID2D1Factory *d2d = nullptr;
    IDWriteFactory *dw = nullptr;
    IWICImagingFactory *wic = nullptr;
    IWICBitmap *bmp = nullptr;
    ID2D1RenderTarget *rt = nullptr;
    int w = 0, h = 0;
    std::map<std::string, ID2D1Bitmap *> images;
    std::map<std::string, IDWriteTextFormat *> formats;
    std::vector<uint8_t> pixels;
    bool ok = false, tried = false;
} R;

static bool renderer_init()
{
    if (R.tried) return R.ok;
    R.tried = true;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &R.d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&R.dw))) return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&R.wic)))) return false;
    R.ok = true;
    return true;
}

static void renderer_resize(int w, int h)
{
    if (R.rt && R.w == w && R.h == h) return;
    for (auto &kv : R.images) if (kv.second) kv.second->Release();
    R.images.clear();
    if (R.rt) { R.rt->Release(); R.rt = nullptr; }
    if (R.bmp) { R.bmp->Release(); R.bmp = nullptr; }
    R.w = w; R.h = h;
    if (FAILED(R.wic->CreateBitmap((UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &R.bmp))) return;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(R.d2d->CreateWicBitmapRenderTarget(R.bmp, p, &R.rt))) { R.rt = nullptr; return; }
    R.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    R.pixels.assign((size_t)w * h * 4, 0);
}

static ID2D1Bitmap *image(const std::string &path)
{
    auto it = R.images.find(path);
    if (it != R.images.end()) return it->second;
    ID2D1Bitmap *out = nullptr;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *fr = nullptr;
    IWICFormatConverter *cv = nullptr;
    if (SUCCEEDED(R.wic->CreateDecoderFromFilename(widen(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(R.wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        R.rt->CreateBitmapFromWicBitmap(cv, nullptr, &out);
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    R.images[path] = out;
    return out;
}

static IDWriteTextFormat *text_format(const std::string &family, float size, int weight, bool condensed, bool italic)
{
    char key[256];
    snprintf(key, sizeof key, "%s|%.2f|%d|%d|%d", family.c_str(), size, weight, condensed, italic);
    auto it = R.formats.find(key);
    if (it != R.formats.end()) return it->second;
    IDWriteTextFormat *f = nullptr;
    R.dw->CreateTextFormat(widen(family).c_str(), nullptr, (DWRITE_FONT_WEIGHT)weight,
                           italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                           condensed ? DWRITE_FONT_STRETCH_CONDENSED : DWRITE_FONT_STRETCH_NORMAL,
                           size, L"en-us", &f);
    if (f) {
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    R.formats[key] = f;
    return f;
}

/* Values a template or colour can name. */
struct Ctx {
    const GameState *g = nullptr;
    TeamInfo team[2];
    const Package *pkg = nullptr;
    const Event *ev = nullptr;
    float opacity = 1.0f;
};

static std::string down_distance(const GameState &g)
{
    switch (g.phase) {
    case 0: return "";
    case 1: return "FREE KICK";
    case 2: return "KICKOFF";
    case 3: return "PAT";
    }
    if (g.down < 1 || g.down > 4) return "";
    static const char *d[] = { "", "1ST", "2ND", "3RD", "4TH" };
    float yards = fabsf(g.line - g.ball) / 91.44f;     /* field units are centimetres */
    char buf[48];
    if (fabsf(g.line) >= 4572.0f - 1.0f) snprintf(buf, sizeof buf, "%s & GOAL", d[g.down]);
    else if (yards < 0.5f) snprintf(buf, sizeof buf, "%s & INCHES", d[g.down]);
    else snprintf(buf, sizeof buf, "%s & %d", d[g.down], (int)(yards + 0.5f));
    return buf;
}

static bool is_final(const GameState &g)
{
    if (g.t[0].score == g.t[1].score) return false;
    return g.period >= 4 && g.clock <= 0.0f;
}

static std::string var(const Ctx &c, const std::string &name)
{
    const GameState &g = *c.g;
    auto team_var = [&](int s, const std::string &k) -> std::string {
        const TeamInfo &t = c.team[s];
        if (k == "abbr") return g.t[s].abbr.empty() ? t.abbr : upper(g.t[s].abbr);
        if (k == "score") return std::to_string(g.t[s].score);
        if (k == "timeouts") return std::to_string(g.t[s].timeouts);
        if (k == "name") return t.name;
        if (k == "city") return t.city;
        if (k == "logo") return t.logo;
        return "";
    };
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        std::string who = name.substr(0, dot), k = name.substr(dot + 1);
        int s = who == "away" ? AWAY : who == "home" ? HOME : (who == "team" && c.ev) ? c.ev->team : -1;
        return s >= 0 ? team_var(s, k) : "";
    }
    if (name == "down_distance") return down_distance(g);
    if (name == "quarter") {
        if (g.period == 0) return "";
        if (is_final(g)) return g.period > 4 ? "F/OT" : "FINAL";
        if (g.period == 2 && g.clock <= 0.0f) return "HALF";
        return ordinal(g.period);
    }
    if (name == "clock") {
        int secs = (int)ceilf(std::max(0.0f, g.clock) - 0.001f);
        char buf[16];
        snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
        return buf;
    }
    if (name == "ended_quarter") return c.ev ? c.ev->ended_quarter : "";
    if (name == "network") return c.pkg ? c.pkg->root.str("network") : "";
    return "";
}

static std::string expand(const Ctx &c, const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '{') {
            size_t j = s.find('}', i);
            if (j != std::string::npos) { out += var(c, s.substr(i + 1, j - i - 1)); i = j; continue; }
        }
        out += s[i];
    }
    return out;
}

static D2D1_COLOR_F color(const Ctx &c, const std::string &spec, float extra_alpha = 1.0f)
{
    D2D1_COLOR_F col = D2D1::ColorF(1, 1, 1, 1);
    std::string base = spec, mods;
    size_t bar = spec.find('|');
    if (bar != std::string::npos) { base = spec.substr(0, bar); mods = spec.substr(bar + 1); }
    if (base.size() > 2 && base[0] == '{') {
        std::string n = base.substr(1, base.size() - 2);
        size_t dot = n.find('.');
        std::string who = n.substr(0, dot), k = dot == std::string::npos ? "" : n.substr(dot + 1);
        int s = who == "away" ? AWAY : who == "home" ? HOME : (who == "team" && c.ev) ? c.ev->team : HOME;
        const TeamInfo &t = c.team[s];
        col = k == "secondary" ? t.secondary : k == "text" ? t.text : t.primary;
    } else {
        parse_hex(base, col);
    }
    while (!mods.empty()) {
        size_t nb = mods.find('|');
        std::string m = mods.substr(0, nb);
        mods = nb == std::string::npos ? "" : mods.substr(nb + 1);
        float v = 0;
        size_t colon = m.find(':');
        if (colon != std::string::npos) v = (float)atof(m.c_str() + colon + 1);
        if (!m.compare(0, 6, "darken")) { col.r *= 1 - v; col.g *= 1 - v; col.b *= 1 - v; }
        else if (!m.compare(0, 7, "lighten")) { col.r += (1 - col.r) * v; col.g += (1 - col.g) * v; col.b += (1 - col.b) * v; }
        else if (!m.compare(0, 5, "alpha")) col.a *= v;
    }
    col.a *= c.opacity * extra_alpha;
    return col;
}

static bool shown(const Ctx &c, const std::string &cond)
{
    if (cond.empty() || cond == "always") return true;
    const GameState &g = *c.g;
    if (cond == "possession:away") return g.phase == 4 && g.poss == 1;
    if (cond == "possession:home") return g.phase == 4 && g.poss == 2;
    if (cond == "scrimmage") return g.phase == 4;
    if (cond == "not_scrimmage") return g.phase != 4;
    if (cond == "final") return is_final(g);
    if (cond == "not_final") return !is_final(g);
    return true;
}

static bool slot_rect(const Ctx &c, const std::string &at, float r[4])
{
    const JVal *sb = c.pkg ? c.pkg->root.get("scorebug") : nullptr;
    const JVal *slots = sb ? sb->get("team_slots") : nullptr;
    if (!slots || at.compare(0, 5, "team.") || !c.ev) return false;
    const JVal *side = slots->get(c.ev->team == AWAY ? "away" : "home");
    const JVal *box = side ? side->get(at.substr(5).c_str()) : nullptr;
    if (!box || box->a.size() < 4) return false;
    for (int i = 0; i < 4; i++) r[i] = (float)box->a[i].n;
    return true;
}

/* Draw one element (units; the render target's transform maps them). */
static void draw_element(const Ctx &c, const JVal &e, float alpha, float dx, float dy, float clip_w_frac)
{
    if (!shown(c, e.str("show"))) return;
    std::string type = e.str("type");
    float x = (float)e.num("x", 0), y = (float)e.num("y", 0), w = (float)e.num("w", 0), h = (float)e.num("h", 0);
    {
        float r[4];
        std::string at = e.str("at");
        if (!at.empty() && slot_rect(c, at, r)) { x = r[0]; y = r[1]; w = r[2]; h = r[3]; }
    }
    x += dx; y += dy;
    ID2D1RenderTarget *rt = R.rt;
    bool clipped = clip_w_frac < 1.0f;
    if (clipped) rt->PushAxisAlignedClip(D2D1::RectF(x, y - 100, x + w * clip_w_frac, y + h + 100), D2D1_ANTIALIAS_MODE_ALIASED);

    if (type == "box") {
        float rad = (float)e.num("radius", 0);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), rad, rad);
        ID2D1Brush *brush = nullptr;
        if (const JVal *gr = e.get("gradient")) {
            D2D1_GRADIENT_STOP st[2] = { { 0, color(c, gr->str("top"), alpha) }, { 1, color(c, gr->str("bottom"), alpha) } };
            ID2D1GradientStopCollection *coll = nullptr;
            if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, &coll))) {
                ID2D1LinearGradientBrush *lb = nullptr;
                rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h)), coll, &lb);
                brush = lb;
                coll->Release();
            }
        } else {
            ID2D1SolidColorBrush *sb = nullptr;
            rt->CreateSolidColorBrush(color(c, e.str("fill", "#FFFFFF"), alpha), &sb);
            brush = sb;
        }
        if (brush) { rt->FillRoundedRectangle(rr, brush); brush->Release(); }
        std::string stroke = e.str("stroke");
        if (!stroke.empty()) {
            ID2D1SolidColorBrush *sb = nullptr;
            rt->CreateSolidColorBrush(color(c, stroke, alpha), &sb);
            if (sb) { rt->DrawRoundedRectangle(rr, sb, (float)e.num("stroke_width", 1)); sb->Release(); }
        }
    } else if (type == "text") {
        std::wstring txt = widen(expand(c, e.str("text")));
        if (!txt.empty()) {
            std::string fam = e.str("font", c.pkg ? c.pkg->root.str("font", "Bahnschrift").c_str() : "Bahnschrift");
            IDWriteTextFormat *f = text_format(fam, (float)e.num("size", 24), (int)e.num("weight", 700),
                                               e.str("stretch") == "condensed", e.flag("italic", false));
            if (f) {
                std::string al = e.str("align", "center");
                f->SetTextAlignment(al == "left" ? DWRITE_TEXT_ALIGNMENT_LEADING : al == "right" ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                                                 : DWRITE_TEXT_ALIGNMENT_CENTER);
                D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
                std::string sh = e.str("shadow");
                if (!sh.empty()) {
                    ID2D1SolidColorBrush *sb = nullptr;
                    rt->CreateSolidColorBrush(color(c, sh, alpha), &sb);
                    if (sb) {
                        D2D1_RECT_F s2 = D2D1::RectF(box.left + 1.5f, box.top + 1.5f, box.right + 1.5f, box.bottom + 1.5f);
                        rt->DrawText(txt.c_str(), (UINT32)txt.size(), f, s2, sb, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        sb->Release();
                    }
                }
                ID2D1SolidColorBrush *b = nullptr;
                rt->CreateSolidColorBrush(color(c, e.str("color", "#FFFFFF"), alpha), &b);
                if (b) { rt->DrawText(txt.c_str(), (UINT32)txt.size(), f, box, b, D2D1_DRAW_TEXT_OPTIONS_CLIP); b->Release(); }
            }
        }
    } else if (type == "image") {
        std::string src = expand(c, e.str("src"));
        if (!src.empty()) {
            std::string path = src;
            if (c.pkg && !file_exists(path)) path = c.pkg->dir + "/" + src;
            ID2D1Bitmap *bm = file_exists(path) ? image(path) : nullptr;
            if (bm) {
                D2D1_SIZE_F s = bm->GetSize();
                float k = std::min(w / s.width, h / s.height);
                float iw = s.width * k, ih = s.height * k;
                rt->DrawBitmap(bm, D2D1::RectF(x + (w - iw) / 2, y + (h - ih) / 2, x + (w + iw) / 2, y + (h + ih) / 2),
                               alpha * c.opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
    } else if (type == "timeouts") {
        int side = e.str("team") == "away" ? AWAY : HOME;
        int count = (int)e.num("count", 3), have = c.g->t[side].timeouts;
        float bw = (float)e.num("bar_w", 30), bh = (float)e.num("bar_h", 6), gap = (float)e.num("gap", 10);
        for (int i = 0; i < count; i++) {
            ID2D1SolidColorBrush *b = nullptr;
            rt->CreateSolidColorBrush(color(c, i < have ? e.str("on", "#F5C518") : e.str("off", "#FFFFFF33"), alpha), &b);
            if (b) {
                float bx = x + i * (bw + gap);
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, y, bx + bw, y + bh), bh / 2, bh / 2), b);
                b->Release();
            }
        }
    }
    if (clipped) rt->PopAxisAlignedClip();
}

static void draw_layer(const Ctx &c, const JVal &L, double t, double duration)
{
    double start = L.num("start", 0), end = L.num("end", duration);
    double local = t - start;
    if (local < 0 || t > end) return;
    float alpha = 1, dy = 0, clip = 1;
    float h = (float)L.num("h", 40);
    if (const JVal *en = L.get("enter")) {
        double tm = en->num("time", 0.3);
        float p = (float)std::min(1.0, local / std::max(0.01, tm));
        p = 1 - (1 - p) * (1 - p);   /* ease out */
        std::string ty = en->str("type", "fade");
        if (ty == "fade") alpha *= p;
        else if (ty == "slide_up") { dy += (1 - p) * (h + 10); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "slide_down") { dy -= (1 - p) * (h + 10); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "wipe_x") clip = p;
    }
    if (const JVal *ex = L.get("exit")) {
        double tm = ex->num("time", 0.3);
        float q = (float)std::min(1.0, (end - t) / std::max(0.01, tm));
        std::string ty = ex->str("type", "fade");
        if (ty == "fade") alpha *= q;
        else if (ty == "slide_down") { dy += (1 - q) * (h + 10); alpha *= q; }
        else if (ty == "slide_up") { dy -= (1 - q) * (h + 10); alpha *= q; }
        else if (ty == "wipe_x") clip = std::min(clip, q);
    }
    if (const JVal *bl = L.get("blink")) {
        double period = bl->num("period", 0.5), until = bl->num("until", duration);
        if (local < until && fmod(local, period) >= period / 2) return;
        if (local >= until) return;
    }
    /* A layer that lives above the bar (a banner) never shows over it while
     * it slides. */
    bool above = L.num("y", 0) + L.num("h", 0) <= 0.0;
    if (above) R.rt->PushAxisAlignedClip(D2D1::RectF(-100, -1000, 2000, 0), D2D1_ANTIALIAS_MODE_ALIASED);
    draw_element(c, L, alpha, 0, dy, clip);
    if (above) R.rt->PopAxisAlignedClip();
}

/* ======================================================================
 * Frame logic (presenter thread)
 * ====================================================================== */
static std::atomic<int> s_native_visible{ 0 };
static std::atomic<DWORD> s_native_tick{ 0 };
static int s_loaded_pkg = -1;
static GameState s_prev;
static bool s_prev_valid;
static float s_show = 0;            /* fade in/out of the whole bug */
static double s_last_t;
static std::string s_last_sig;
static bool s_game_started, s_outro_played, s_intro_active;
static double s_invalid_since;
static bool s_test, s_log;
static std::string s_test_event;

static bool custom_active() { int p = s_sel_pkg; return p > 0 && p < (int)s_pkgs.size(); }

static void gwrite32(uint32_t va, uint32_t v)
{
    if (!guest_va_ok(va)) return;
    __try { *(volatile uint32_t *)((uintptr_t)va + g_xbox_mem_offset) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void gwritef(uint32_t va, float f)
{
    if (!guest_va_ok(va)) return;
    __try { *(volatile float *)((uintptr_t)va + g_xbox_mem_offset) = f; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool s_text_hidden;

/* Called by the game right after its scorebug update (SCOREBUG_NATIVE_HOOK). */
extern "C" void nfl2k5_scorebug_hide(void)
{
    if (!custom_active() && s_text_hidden) {
        static const uint32_t k_text_colors2[] = {
            0xA95894, 0xA95898, 0xA958BC, 0xA958C0, 0xA958E4, 0xA958E8, 0xA9590C, 0xA95910,
            0xA95934, 0xA95938, 0xA95958, 0xA95990, 0xA959D8, 0xA95A48, 0xA95AB8, 0xA95B28,
            0xA95B98, 0xA95C08 };
        for (uint32_t a : k_text_colors2) {
            uint32_t c = rd32(a);
            if (!(c >> 24)) gwrite32(a, c | 0xFF000000u);
        }
        s_text_hidden = false;
    }
    if (custom_active()) {
        uint32_t scene = rd32(0x00A95528u), n = scene ? rd32(scene + 0x1C) : 0, arr = scene ? rd32(scene + 0x20) : 0;
        for (uint32_t i = 0; arr && i < n && i < 256; i++) {
            uint32_t fl = rd32(arr + i * 0x80u + 8u);
            if (!(fl & 1u)) gwrite32(arr + i * 0x80u + 8u, fl | 1u);
        }
        /* The bug's text fields are drawn separately from its meshes; their
         * colours live in the game's text table (0xA95880..0xA95C08):
         * clearing the alpha byte hides them. Originals kept for switching
         * back to the ESPN presentation. */
        static const uint32_t k_text_colors[] = {
            0xA95894, 0xA95898, 0xA958BC, 0xA958C0, 0xA958E4, 0xA958E8, 0xA9590C, 0xA95910,
            0xA95934, 0xA95938, 0xA95958, 0xA95990, 0xA959D8, 0xA95A48, 0xA95AB8, 0xA95B28,
            0xA95B98, 0xA95C08 };
        for (uint32_t a : k_text_colors) {
            uint32_t c = rd32(a);
            if (c >> 24) gwrite32(a, c & 0x00FFFFFFu);
        }
        s_text_hidden = true;
        if (s_log) {
            static DWORD last;
            if (GetTickCount() - last > 5000) {
                last = GetTickCount();
                fprintf(stderr, "[PRES] score_bug scene %08X nodes %u at %08X:", scene, n, arr);
                for (uint32_t i = 0; arr && i < n && i < 12; i++) {
                    uint32_t nm = rd32(arr + i * 0x80u);
                    uint16_t w[12] = { 0 };
                    char a[13] = { 0 };
                    gread(nm, w, sizeof w);
                    for (int k = 0; k < 11 && w[k]; k++) a[k] = (char)(w[k] < 128 ? w[k] : '?');
                    fprintf(stderr, " %s(%X)", a, rd32(arr + i * 0x80u + 8u));
                }
                fprintf(stderr, "\n");
            }
        }
    }
}

extern "C" void nfl2k5_scorebug_native_hook(void)
{
    s_native_visible = rd32(0x00A95A00u) != 0;
    s_native_tick = GetTickCount();
    nfl2k5_scorebug_hide();

}

/* SCOREBUG_NATIVE_HIDE2: with a custom package on, the ESPN bug's root
 * matrix (4x4 floats, translation at +0x30) is rewritten after the game
 * places it. NFL2K5_HIDE_MODE: 1 off screen, 2 zero scale, 3 both. */
extern "C" void nfl2k5_scorebug_native_place(uint32_t m)
{
    static int mode = -1;
    if (mode < 0) { const char *e = getenv("NFL2K5_HIDE_MODE"); mode = e ? atoi(e) : 0; }
    if (!custom_active() || !m) return;
    if (mode & 2) { gwritef(m + 0x00, 0.0f); gwritef(m + 0x14, 0.0f); gwritef(m + 0x28, 0.0f); }
    if (mode & 1) { gwritef(m + 0x30, -20000.0f); gwritef(m + 0x34, -20000.0f); }
}

extern "C" int nfl2k5_scorebug_skip_update(void)
{
    static int on = -1;
    if (on < 0) on = getenv("NFL2K5_HIDE_SKIP") != nullptr;
    return on && custom_active();
}

extern "C" int nfl2k5_scorebug_native_hidden(void)
{
    static unsigned long calls;
    static DWORD last;
    calls++;
    if (s_log && GetTickCount() - last > 5000) {
        last = GetTickCount();
        fprintf(stderr, "[PRES] native bug placement calls %lu, hidden %d\n", calls, (int)custom_active());
    }
    return custom_active();
}

/* Popups the game shows (src/main.c): the intro theme ends at the coin toss. */
static std::atomic<int> s_coin_toss{ 0 };
extern "C" void nfl2k5_presentation_popup(const char *title)
{
    if (title && !_strnicmp(title, "Coin Toss", 9)) s_coin_toss = 1;
}

static bool s_video_then_theme;   /* the open covers the loading show; the theme follows */
static bool s_video_hold;         /* keep the open's last frame up until the pregame starts */
static bool s_was_loading;

static void start_intro_theme(const Package *pkg, const JVal *m, float vol, float duck, float fade)
{
    int i = s_sel_intro;
    if (pkg && i >= 1 && i <= (int)pkg->intro.size()) {
        music_play(pkg->dir + "/" + pkg->intro[i - 1].file, vol * (m ? (float)m->num("volume", 0.9) : 0.9f), duck, fade);
        s_intro_active = true;
    }
}

static void music_logic(const GameState &g, bool native_on)
{
    const Package *pkg = custom_active() ? &s_pkgs[s_sel_pkg] : nullptr;
    double t = now_s();
    {
        /* Match loading (the studio show, Berman): mode 0xA9288C is 1 or 3
         * and 0xA92888 (done) is 0 while the match itself (clock) is not up
         * yet. A package with a pregame_video plays it over that show. */
        uint32_t mode = rd32(0x00A9288Cu), done = rd32(0x00A92888u);
        bool loading = (mode == 1 || mode == 3) && !done && !g.valid;
        static bool played_this_load;
        if (mode == 0) played_this_load = false;          /* back in the front end */
        if (loading && !s_was_loading && !s_game_started && pkg && !played_this_load && !s_video_then_theme) {
            played_this_load = true;
            const JVal *pv = pkg->root.get("pregame_video");
            std::string vfile = pv ? pv->str("file") : "";
            if (!vfile.empty() && file_exists(pkg->dir + "/" + vfile)) {
                std::string path = pkg->dir + "/" + vfile;
                float v0 = s_sel_vol / 10.0f;
                video_play(path);
                music_play(path, v0 * (float)pv->num("volume", 1.0), 0.0f, 0.3f);
                xbox_AudioSetGameGain(0.0f);
                xbox_AudioSetCommentaryGain(0.0f);
                s_video_then_theme = true;
                s_video_hold = true;
            }
        }
        s_was_loading = loading;
    }
    if (!g.valid) {
        if (s_invalid_since == 0) s_invalid_since = t;
        if (t - s_invalid_since > 4.0) {        /* left the match */
            if (s_game_started || s_outro_played) music_fade();
            if (!s_video_then_theme) {
                if (s_video.state) { ++s_video.generation; s_video.state = 0; }
                xbox_AudioSetCommentaryGain(1.0f);
            }
            s_game_started = s_outro_played = s_intro_active = false;
        }
        return;
    }
    s_invalid_since = 0;
    float vol = s_sel_vol / 10.0f;
    const JVal *m = pkg ? pkg->root.get("music") : nullptr;
    float duck = m ? (float)m->num("duck_game_audio", 1.0) : 1.0f;
    if (m) {
        s_music.talk_duck = (float)m->num("duck_under_announcers", 0.35);
        s_music.talk_level = (float)m->num("announcer_level", 0.02);
        s_music.mute_game_music = m->flag("mute_game_music", true) ? 1 : 0;
    }
    float fade = m ? (float)m->num("fade_out", 2.5) : 2.5f;
    bool fresh = g.period <= 1 && g.t[0].score == 0 && g.t[1].score == 0 && g.phase <= 2 &&
                 g.clock >= g.period_len - 0.5f;
    if (!s_game_started && fresh) {
        s_game_started = true;
        s_coin_toss = 0;
        s_outro_played = false;
        if (s_video_then_theme) {
            /* The open covered the loading show; the pregame starts now. */
            ++s_video.generation;
            s_video.state = 0;
            s_video_hold = false;
            s_video_then_theme = false;
            xbox_AudioSetCommentaryGain(1.0f);
            xbox_AudioSetGameGain(1.0f);
        }
        start_intro_theme(pkg, m, vol, duck, fade);
    }
    /* The intro plays over the pregame show and fades at the coin toss (or
     * when play starts, if there was none). */
    if (s_intro_active && (s_coin_toss || native_on || g.clock < g.period_len - 1.0f)) {
        music_fade();
        s_intro_active = false;
    }
    if (s_game_started && !s_outro_played && is_final(g)) {
        s_outro_played = true;
        int i = s_sel_outro;
        if (pkg && i >= 1 && i <= (int)pkg->outro.size())
            music_play(pkg->dir + "/" + pkg->outro[i - 1].file, vol * (m ? (float)m->num("volume", 0.9) : 0.9f), duck, fade);
    }
}

static int hud_callback(const XboxHudFrame *f, XboxHudImage *img)
{
    double t = now_s();
    float dt = (float)std::min(0.1, s_last_t ? t - s_last_t : 0.0);
    s_last_t = t;
    music_tick();

    GameState g;
    read_state(g);
    if (s_test) {
        g.valid = true; g.period = 1; g.clock = 791; g.period_len = 900; g.phase = 4; g.down = 1;
        g.ball = 0; g.line = 914.4f; g.poss = 2;
        g.t[0].score = 0; g.t[0].timeouts = 3; g.t[0].abbr = "KC";
        g.t[1].score = 0; g.t[1].timeouts = 3; g.t[1].abbr = "NE";
    }
    bool native_on = s_native_visible && GetTickCount() - s_native_tick < 300;
    if (s_test) native_on = true;

    if (s_log && !g.valid) {
        static DWORD last_raw;
        if (GetTickCount() - last_raw > 5000) {
            last_raw = GetTickCount();
            uint32_t clk = rd32(0x00E6028Cu);
            fprintf(stderr, "[PRES] raw: clk %08X (%.1f) period %u len %.1f phase %u score %08X/%08X poss %08X down %08X abbr %s/%s\n",
                    clk, clk ? rdf(clk + 0x10) : 0.0f, rd32(0x00E602C4u), rdf(0x00E602B0u), rd32(0x00E602B4u),
                    rd32(k_team_obj[AWAY] + 8), rd32(k_team_obj[HOME] + 8), rd32(0x00E60280u), rd32(0x00E602ECu),
                    read_team_code(k_playbook_name[AWAY]).c_str(), read_team_code(k_playbook_name[HOME]).c_str());
        }
    }
    if (s_log && g.valid) {
        static DWORD last;
        if (GetTickCount() - last > 3000) {
            last = GetTickCount();
            fprintf(stderr, "[PRES] q%d %.1f/%.0f phase %d down %d ball %.1f line %.1f poss %d  %s %d(%d) - %s %d(%d)  native %d\n",
                    g.period, g.clock, g.period_len, g.phase, g.down, g.ball, g.line, g.poss,
                    g.t[0].abbr.c_str(), g.t[0].score, g.t[0].timeouts, g.t[1].abbr.c_str(), g.t[1].score, g.t[1].timeouts,
                    (int)native_on);
        }
    }

    music_logic(g, native_on);

    if (g.valid && s_prev_valid && s_sel_anim && custom_active()) detect_events(s_prev, g);
    if (!g.valid || !s_prev_valid) s_queue.clear();
    s_prev = g;
    s_prev_valid = g.valid;

    if (s_video.state == 1 || (s_video.state == 2 && s_video_hold)) {
        /* Full screen, letterboxed over black: the frame is placed in a
         * black canvas with the backbuffer's aspect, drawn over the whole
         * backbuffer. */
        static std::vector<uint8_t> canvas;
        static int cw, ch;
        static bool dirty;
        std::lock_guard<std::mutex> gl(s_video.lock);
        if (s_video.w <= 0 || f->bb_w <= 0) return 0;
        int W = s_video.w, H = (int)((double)s_video.w * f->bb_h / f->bb_w);
        if (H < s_video.h) { H = s_video.h; W = (int)((double)s_video.h * f->bb_w / f->bb_h); }
        if (W != cw || H != ch) { cw = W; ch = H; canvas.assign((size_t)W * H * 4, 0); dirty = true; }
        if (s_video.fresh) {
            int ox = (W - s_video.w) / 2, oy = (H - s_video.h) / 2;
            for (int y = 0; y < s_video.h; y++)
                memcpy(&canvas[((size_t)(y + oy) * W + ox) * 4], &s_video.frame[(size_t)y * s_video.w * 4], (size_t)s_video.w * 4);
            for (size_t k = 3; k < canvas.size(); k += 4) canvas[k] = 255;
            s_video.fresh = false;
            dirty = true;
        }
        img->pixels = canvas.data();
        img->w = W; img->h = H; img->stride = W * 4;
        img->x = 0; img->y = 0; img->dw = f->bb_w; img->dh = f->bb_h;
        img->changed = dirty;
        dirty = false;
        return 1;
    }
    if (!custom_active() || !renderer_init()) return 0;
    const Package &pkg = s_pkgs[s_sel_pkg];
    const JVal *sb = pkg.root.get("scorebug");
    if (!sb) return 0;

    if (!s_test_event.empty() && !s_anim_on && s_queue.empty()) {
        static double next;
        if (t >= next) { s_queue.push_back({ s_test_event, HOME, "1ST QUARTER" }); next = t + 7.0; }
    }

    /* Whole-bug visibility with a short fade. */
    float fade = (float)sb->num("fade_time", 0.25);
    float target = (g.valid && native_on) ? 1.0f : 0.0f;
    if (s_show < target) s_show = std::min(target, s_show + dt / std::max(0.01f, fade));
    else if (s_show > target) s_show = std::max(target, s_show - dt / std::max(0.01f, fade));
    if (s_show <= 0.0f) { s_anim_on = false; return 0; }

    /* Next animation. */
    const JVal *anims = pkg.root.get("animations");
    if (s_anim_on && t - s_anim.t0 > s_anim.duration) s_anim_on = false;
    while (!s_anim_on && !s_queue.empty()) {
        Event ev = s_queue.front();
        s_queue.erase(s_queue.begin());
        const JVal *def = anims ? anims->get(ev.name.c_str()) : nullptr;
        if (!def) continue;
        s_anim.ev = ev; s_anim.def = def; s_anim.t0 = t; s_anim.duration = def->num("duration", 3);
        s_anim_on = true;
        fprintf(stderr, "[PRES] animation %s (%s)\n", ev.name.c_str(), ev.team == AWAY ? "away" : "home");
    }

    /* Layout: the bug is canvas.width units across placement.width of the
     * game picture, centred on placement.center_*; the image adds room above
     * for timeouts and banners. */
    const JVal *cv = sb->get("canvas"), *pl = sb->get("placement");
    float cw = cv ? (float)cv->num("width", 1000) : 1000, ch = cv ? (float)cv->num("height", 56) : 56;
    float top = cv ? (float)cv->num("overflow_top", 70) : 70;
    float u = f->game_w * (pl ? (float)pl->num("width", 0.92) : 0.92f) / cw;
    float cx = f->game_x + f->game_w * (pl ? (float)pl->num("center_x", 0.5) : 0.5f);
    float cy = f->game_y + f->game_h * (pl ? (float)pl->num("center_y", 0.915) : 0.915f);
    const int pad = 4;
    int W = (int)ceilf(cw * u) + pad * 2, H = (int)ceilf((ch + top) * u) + pad * 2;
    int X = (int)floorf(cx - cw * u / 2) - pad, Y = (int)floorf(cy - ch * u / 2 - top * u) - pad;
    if (W < 8 || H < 8 || W > 16384 || H > 4096) return 0;

    Ctx c;
    c.g = &g;
    c.pkg = &pkg;
    c.team[AWAY] = team_info(g.t[AWAY].abbr);
    c.team[HOME] = team_info(g.t[HOME].abbr);
    c.opacity = s_show;

    /* Redraw only when something visible changed. */
    char sigbuf[160];
    snprintf(sigbuf, sizeof sigbuf, "%d|%d|%.3f|%s|%s|%s|%s|%d%d|%d%d|%d", W, H, s_show, var(c, "clock").c_str(),
             var(c, "down_distance").c_str(), var(c, "quarter").c_str(), (g.t[0].abbr + g.t[1].abbr).c_str(),
             g.t[0].score, g.t[1].score, g.t[0].timeouts, g.t[1].timeouts, g.poss * 10 + g.phase);
    std::string sig = sigbuf;
    bool changed = s_anim_on || sig != s_last_sig || R.w != W || R.h != H;
    s_last_sig = sig;

    if (changed) {
        renderer_resize(W, H);
        if (!R.rt) return 0;
        R.rt->BeginDraw();
        R.rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        R.rt->SetTransform(D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation((float)pad, (float)pad + top * u));
        if (const JVal *els = sb->get("elements"))
            for (auto &e : els->a) draw_element(c, e, 1.0f, 0, 0, 1.0f);
        if (s_anim_on && s_anim.def) {
            Ctx ac = c;
            ac.ev = &s_anim.ev;
            if (const JVal *layers = s_anim.def->get("layers"))
                for (auto &L : layers->a) draw_layer(ac, L, t - s_anim.t0, s_anim.duration);
        }
        R.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(R.rt->EndDraw())) { renderer_resize(0, 0); return 0; }
        IWICBitmapLock *lk = nullptr;
        WICRect rc = { 0, 0, W, H };
        if (SUCCEEDED(R.bmp->Lock(&rc, WICBitmapLockRead, &lk))) {
            UINT stride = 0, size = 0;
            BYTE *data = nullptr;
            lk->GetStride(&stride);
            lk->GetDataPointer(&size, &data);
            for (int row = 0; row < H; row++)
                memcpy(&R.pixels[(size_t)row * W * 4], data + (size_t)row * stride, (size_t)W * 4);
            lk->Release();
        }
    }
    img->pixels = R.pixels.data();
    img->w = W; img->h = H; img->stride = W * 4;
    img->x = X; img->y = Y;
    img->changed = changed;
    return 1;
}

/* Called once at startup (nfl2k5_video_menu_install). */
extern "C" void nfl2k5_presentation_init(void)
{
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    load_teams();
    load_packages();
    load_settings();
    s_test = getenv("NFL2K5_PRES_TEST") != nullptr;
    s_log = getenv("NFL2K5_PRES_LOG") != nullptr;
    if (const char *ev = getenv("NFL2K5_PRES_EVENT")) s_test_event = ev;
    xbox_PresentSetHudCallback(hud_callback);
    xbox_AudioSetCaptureMix(capture_mix);
    fprintf(stderr, "[PRES] %zu presentations, selected \"%s\"\n", s_pkgs.size(), nfl2k5_pres_package_name(s_sel_pkg));
}

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
#include "nfl2k5_broadcast.h"
#include "nfl2k5_presentation.h"
#include "presentation/presentation_host.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>
#include <algorithm>
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
typedef struct { const void *pixels; int w, h, stride, x, y, changed, dw, dh; void *shared; } XboxHudImage;
/* Set by the presenter when it cannot open a shared HUD texture: use pixels. */
extern "C" int xbox_hud_shared_failed;
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
    explicit JParser(const std::string &text) : base(text.c_str()), p(text.c_str()), e(text.c_str() + text.size()) {}
    bool parse(JVal &out) { ws(); return value(out); }
    /* Where parsing stopped: 1-based line and column of the offending character. */
    void where(int *line, int *col) const {
        const char *q = base; *line = 1; *col = 1;
        for (; q < p && q < e; q++) { if (*q == '\n') { (*line)++; *col = 1; } else (*col)++; }
    }
private:
    const char *base, *p, *e;
    /* Whitespace and, as hand-edited packages have them, // and slash-star comments. */
    void ws() {
        for (;;) {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p + 1 < e && p[0] == '/' && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
            if (p + 1 < e && p[0] == '/' && p[1] == '*') {
                p += 2;
                while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) p++;
                p = p + 1 < e ? p + 2 : e;
                continue;
            }
            break;
        }
    }
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
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
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
                ws(); if (p < e && *p == ',') { p++; ws(); if (p < e && *p == '}') { p++; return true; } continue; }
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
                ws(); if (p < e && *p == ',') { p++; ws(); if (p < e && *p == ']') { p++; return true; } continue; }
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
    /* Editors add a byte-order mark: UTF-8 (EF BB BF) or UTF-16 (FF FE). */
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text.erase(0, 3);
    } else if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE) {
        const wchar_t *w = (const wchar_t *)(text.data() + 2);
        int wn = (int)((text.size() - 2) / 2);
        int n = WideCharToMultiByte(CP_UTF8, 0, w, wn, nullptr, 0, nullptr, nullptr);
        std::string u8((size_t)(n > 0 ? n : 0), '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, wn, &u8[0], n, nullptr, nullptr);
        text.swap(u8);
    }
    JParser jp(text);
    if (!jp.parse(out)) {
        int line, col;
        jp.where(&line, &col);
        fprintf(stderr, "[PRES] JSON error in %s at line %d, column %d\n", path.c_str(), line, col);
        return false;
    }
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
 *   E60294 -> scrimmage-countdown object, +0x10 play clock (float seconds)
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
    int period = 0, phase = -1, down = 0, poss = 0, play_clock = -1;   /* poss: 0 none, 1 away, 2 home */
    float clock = 0, period_len = 0, ball = 0, line = 0;
    TeamState t[2];                                     /* 0 away, 1 home */
};

enum { AWAY = 0, HOME = 1 };
static const uint32_t k_team_obj[2] = { 0x00E5FC60u, 0x00E5FC20u };
static const uint32_t k_playbook_name[2] = { 0x00B30810u, 0x00B307D0u };
static std::atomic<ULONGLONG> s_flag_until{ 0 };

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

static void eras_side_abbr(int side, std::string &abbr);   /* M9, below the team lookup */

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
        eras_side_abbr(s, g.t[s].abbr);     /* M9: an Eras game names its staged teams, never the playbook's 2004 club */
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
    /* This is a distinct countdown from the period clock: native captures
     * show E60294+10 descending once per second during phase 4 and resetting
     * after the snap.  The television clock rounds partial seconds upward. */
    uint32_t play_timer = rd32(0x00E60294u);
    float play_seconds = play_timer ? rdf(play_timer + 0x10) : -1.0f;
    if (g.phase == 4 && play_seconds >= 0.0f && play_seconds <= 99.0f)
        g.play_clock = (int)std::ceil(play_seconds);
    g.valid = true;
    return true;
}

/* ======================================================================
 * Teams (mods/teams)
 * ====================================================================== */
struct TeamInfo {
    std::string abbr, city, name, logo, record;
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
        t.record = j.str("record");
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

/* ======================================================================
 * My NFL Eras identity (M9, docs/eras/results/M9-era-looks.md)
 *
 * While an Eras game is launched (src/nfl2k5_eras_launch.c calls
 * nfl2k5_presentation_eras_begin / _end), each side's team comes from
 *   teams\<season>\<eras id>\team.json (+ logos\scorebug.png) of the Eras roots
 * keyed by the staged Eras team of that side, never by the 2004 abbreviation
 * the playbook name carries (BAL is the Colts there, HOU the Oilers). The
 * Presentation API gets season / decade / week, and the Eras presentations.ini
 * may pick a package for the season; the user's own selection is put back at
 * the end and is never saved over. Outside Eras nothing here is used.
 * ====================================================================== */
struct ErasPres {
    bool on = false;
    int season = 0, week = 0;
    std::string id[2];
    TeamInfo team[2];          /* [AWAY], [HOME] */
    std::string playbook[2];   /* what the playbook name said (logged once) */
    bool logged = false;
};
static std::mutex s_eras_lock;
static ErasPres s_eras;
static std::atomic<int> s_eras_on{ 0 };

/* The pack roots (src/nfl2k5_plugin_host.c): generated first, then the pack; '/' separators here.
 * None while no module is on, so nothing is mapped or found then. */
extern "C" int nfl2k5_pack_root_count(void);
extern "C" char *nfl2k5_pack_root_path(int i, const char *rel, char *out, size_t n);
static std::string eras_root_rel(int i, const std::string &rel)
{
    char p[MAX_PATH];
    std::string s = nfl2k5_pack_root_path(i, rel.c_str(), p, sizeof p);
    for (char &c : s) if (c == '\\') c = '/';
    return s;
}
/* The folder served as https://erateams.local/: the first root with a teams folder
 * (the logos the asset builder made); "" when there is none. */
static std::string eras_teams_root_find()
{
    for (int r = 0; r < nfl2k5_pack_root_count(); r++) {
        std::string d = eras_root_rel(r, "teams");
        if (GetFileAttributesA(d.c_str()) != INVALID_FILE_ATTRIBUTES) return d;
    }
    return "";
}
static const std::string &eras_teams_root()
{
    static const std::string root = eras_teams_root_find();   /* once: the roots are fixed at boot */
    return root;
}

static bool eras_team_load(int season, const std::string &id, TeamInfo &t)
{
    std::string rel = "teams/" + std::to_string(season) + "/" + id, dir;
    JVal j;
    t = TeamInfo();
    for (int r = 0; r < nfl2k5_pack_root_count() && dir.empty(); r++)
        if (file_exists(eras_root_rel(r, rel + "/team.json"))) dir = eras_root_rel(r, rel);
    if (dir.empty() || !load_json(dir + "/team.json", j)) return false;
    t.abbr = j.str("abbreviation", id.c_str());
    t.city = j.str("city");
    t.name = j.str("name", t.abbr.c_str());
    t.record = j.str("record");
    if (const JVal *c = j.get("colors")) {
        parse_hex(c->str("primary"), t.primary);
        parse_hex(c->str("secondary"), t.secondary);
        parse_hex(c->str("text", "#FFFFFF"), t.text);
    }
    std::string logo = "logos/scorebug.png";
    if (const JVal *l = j.get("logos")) logo = l->str("scorebug", logo.c_str());
    std::string served = eras_teams_root();     /* the logo from the served folder (built art) first */
    if (!served.empty() && file_exists(served + "/" + std::to_string(season) + "/" + id + "/" + logo))
        t.logo = served + "/" + std::to_string(season) + "/" + id + "/" + logo;
    else if (file_exists(dir + "/" + logo)) t.logo = dir + "/" + logo;
    return true;
}

static void eras_side_abbr(int side, std::string &abbr)
{
    if (!s_eras_on.load()) return;
    std::lock_guard<std::mutex> lock(s_eras_lock);
    if (!s_eras.on || s_eras.team[side].abbr.empty()) return;
    if (!s_eras.logged && !abbr.empty()) {
        s_eras.playbook[side] = abbr;
        if (!s_eras.playbook[AWAY].empty() && !s_eras.playbook[HOME].empty()) {
            s_eras.logged = true;
            fprintf(stderr, "[PRES] eras: playbook codes away %s / home %s -> Eras %s (%s %s) / %s (%s %s)\n",
                    s_eras.playbook[AWAY].c_str(), s_eras.playbook[HOME].c_str(),
                    s_eras.id[AWAY].c_str(), s_eras.team[AWAY].city.c_str(), s_eras.team[AWAY].name.c_str(),
                    s_eras.id[HOME].c_str(), s_eras.team[HOME].city.c_str(), s_eras.team[HOME].name.c_str());
        }
    }
    abbr = s_eras.team[side].abbr;
}

/* The Eras team whose (Eras) abbreviation this is, while an Eras game runs. */
static bool eras_team_info(const std::string &abbr, TeamInfo &out)
{
    if (!s_eras_on.load()) return false;
    std::lock_guard<std::mutex> lock(s_eras_lock);
    if (!s_eras.on) return false;
    for (int s = 0; s < 2; s++)
        if (!s_eras.team[s].abbr.empty() && upper(s_eras.team[s].abbr) == upper(abbr)) { out = s_eras.team[s]; return true; }
    return false;
}

static TeamInfo team_info(const std::string &abbr)
{
    TeamInfo eras;
    if (eras_team_info(abbr, eras)) return eras;
    auto it = s_teams.find(upper(abbr));
    if (it != s_teams.end()) return it->second;
    TeamInfo t;
    t.abbr = t.name = abbr.empty() ? "---" : abbr;
    return t;
}

static std::mutex s_broadcast_state_lock;
static Nfl2k5BroadcastState s_broadcast_state{};

static void copy_broadcast_text(char *dst, size_t cap, const std::string &src)
{
    if (!dst || !cap) return;
    strncpy_s(dst, cap, src.c_str(), _TRUNCATE);
}

static void publish_broadcast_state(const GameState &g)
{
    Nfl2k5BroadcastState out{};
    out.valid = g.valid ? 1 : 0;
    out.possession = g.poss; out.quarter = g.period; out.game_clock = g.clock;
    out.play_clock = g.play_clock; out.down = g.down; out.phase = g.phase;
    out.distance = (int)lroundf(fabsf(g.line - g.ball) / 91.44f);
    int yard = (int)lroundf(std::max(0.0f, std::min(100.0f, g.ball / 91.44f)));
    out.ball_on = std::min(yard, 100 - yard);
    out.flag_state = GetTickCount64() < s_flag_until ? 1 : 0;
    for (int side = 0; side < 2; side++) {
        Nfl2k5BroadcastTeamState &d = side == AWAY ? out.away : out.home;
        TeamInfo t = team_info(g.t[side].abbr);
        copy_broadcast_text(d.abbreviation, sizeof d.abbreviation, upper(g.t[side].abbr));
        copy_broadcast_text(d.city, sizeof d.city, t.city);
        copy_broadcast_text(d.name, sizeof d.name, t.name);
        copy_broadcast_text(d.logo_path, sizeof d.logo_path, t.logo);
        copy_broadcast_text(d.record, sizeof d.record, t.record);
        d.score = g.t[side].score; d.timeouts = g.t[side].timeouts;
    }
    std::lock_guard<std::mutex> lock(s_broadcast_state_lock);
    s_broadcast_state = out;
}

extern "C" int nfl2k5_broadcast_get_state(Nfl2k5BroadcastState *state)
{
    if (!state) return 0;
    std::lock_guard<std::mutex> lock(s_broadcast_state_lock);
    *state = s_broadcast_state;
    return state->valid;
}

/* ======================================================================
 * Packages and settings
 * ====================================================================== */
struct Song { std::string title, file; };
struct Package {
    std::string name, dir;
    JVal root;                    /* presentation.json, or mod.json for an HTML package */
    std::vector<Song> intro, outro;
    /* HTML/CSS/JS package (mod.json "type": "html"): drawn by the
     * PresentationHost instead of the JSON element renderer. */
    bool html = false;
    std::string entry;
    int canvas_w = 1920, canvas_h = 1080;
    /* mod.json "replaces": game graphics the package draws itself, which
     * the game then leaves out (e.g. "lineup": the after-kickoff starters). */
    bool replaces_lineup = false;
    bool replaces_player_cards = false;
    bool replaces_pregame_cards = false;
    /* mod.json "canvas": { "fit": "fill_width" }: the page is as wide as the
     * game picture (e.g. 2580x1080 on 21:9) instead of a centred 16:9
     * canvas, so full-width graphics reach the screen edges. */
    bool fill_width = false;
};

static std::vector<Package> s_pkgs;           /* [0] is the game's own ESPN presentation */
static std::atomic<int> s_sel_pkg{ 0 }, s_sel_intro{ 1 }, s_sel_outro{ 1 }, s_sel_anim{ 1 }, s_sel_vol{ 8 };
static void pregame_replacement_reset();
static std::atomic<int> s_sel_commentary{ 0 };
static std::string s_commentary_dir = "E:\\cfbsound";

/* The package may carry its fonts. Registering them privately makes the
 * broadcast reproducible without installing anything into Windows. */
static void register_package_fonts(const Package &p)
{
    std::wstring mask = widen(p.dir + "/fonts/*.ttf");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = widen(p.dir + "/fonts/") + fd.cFileName;
        int added = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
        if (added) fprintf(stderr, "[PRES] private font %ls\n", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

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
        if (dir8[0] == '_') continue;     /* shared folders such as _runtime */
        Package p;
        p.dir = std::string("mods/presentations/") + dir8;
        if (!load_json(p.dir + "/presentation.json", p.root)) {
            /* An HTML package describes itself in mod.json; its look lives in
             * the entry page, not in JSON. */
            if (!load_json(p.dir + "/mod.json", p.root) || p.root.str("type") != "html") continue;
            if (!p.root.flag("enabled", true)) continue;
            p.html = true;
            p.entry = p.root.str("entry", "index.html");
            if (const JVal *r = p.root.get("replaces"))
                for (auto &v : r->a)
                    if (v.t == JVal::Str) {
                        if (v.s == "lineup") p.replaces_lineup = true;
                        if (v.s == "player_cards") p.replaces_player_cards = true;
                        if (v.s == "pregame_cards") p.replaces_pregame_cards = true;
                    }
            if (const JVal *c = p.root.get("canvas")) {
                p.canvas_w = (int)c->num("width", 1920);
                p.canvas_h = (int)c->num("height", 1080);
                p.fill_width = c->str("fit") == "fill_width";
            }
            if (!file_exists(p.dir + "/" + p.entry)) {
                fprintf(stderr, "[PRES] HTML package %s: entry %s missing, skipped\n", dir8, p.entry.c_str());
                continue;
            }
        }
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
        fprintf(stderr, "[PRES] package \"%s\" (%s, %zu intro, %zu outro themes)\n",
                p.name.c_str(), p.html ? "html" : "json", p.intro.size(), p.outro.size());
        register_package_fonts(p);
        s_pkgs.push_back(std::move(p));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static const char *k_ini = "mods\\presentation.ini";
extern "C" char *xbox_user_path(const char *rel, char *out, size_t n);   /* per-user folder (kernel_path.c) */

/* M9: the player's own selection while an Eras game uses the package from
 * mods\eras\presentations.ini (-1: no Eras package is on). Saved settings
 * always carry the player's own selection. */
static std::atomic<int> s_eras_saved_pkg{ -1 };
static int s_eras_saved_intro = 1, s_eras_saved_outro = 1;

static void save_settings()
{
    char ipath[MAX_PATH];
    FILE *f = fopen(xbox_user_path(k_ini, ipath, sizeof ipath), "w");
    if (!f) return;
    int pk = s_sel_pkg;
    int intro = s_sel_intro, outro = s_sel_outro;
    if (s_eras_saved_pkg >= 0) { pk = s_eras_saved_pkg; intro = s_eras_saved_intro; outro = s_eras_saved_outro; }
    fprintf(f, "; Broadcast presentation (Coach Match Up > Presentation edits this)\n");
    fprintf(f, "package = %s\nintro = %d\noutro = %d\nanimations = %d\ntheme_volume = %d\ncustom_commentary = %d\ncommentary_dir = %s\n",
            pk > 0 && pk < (int)s_pkgs.size() ? s_pkgs[pk].name.c_str() : "ESPN (Original)",
            intro, outro, (int)s_sel_anim, (int)s_sel_vol * 10,
            (int)s_sel_commentary, s_commentary_dir.c_str());
    fclose(f);
}

static void load_settings()
{
    char ipath[MAX_PATH];
    FILE *f = fopen(xbox_user_path(k_ini, ipath, sizeof ipath), "r");
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
        else if (!strcmp(key, "custom_commentary")) s_sel_commentary = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "commentary_dir")) s_commentary_dir = val;
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
    case 5: return s_sel_commentary;
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
    case 5: return 2;
    }
    return 1;
}
/* The game sizes a settings screen's value column from its longest value;
 * when that no longer fits the box, it clips every value on the screen
 * (2026-09-30: "Classic Broadcast Scorebug v27", 30 characters, blanked the
 * whole Presentation screen). 23 characters is known to fit. */
static void menu_text(char *buf, size_t n, const std::string &s)
{
    const size_t k_max = 23;
    if (s.size() <= k_max) snprintf(buf, n, "%s", s.c_str());
    else snprintf(buf, n, "%s...", s.substr(0, k_max - 3).c_str());
}

extern "C" void nfl2k5_pres_value_name(int what, int idx, char *buf, size_t n)
{
    buf[0] = 0;
    switch (what) {
    case 0:
        /* A package may give a shorter "menu_name" for this screen. */
        if (idx > 0 && idx < (int)s_pkgs.size() && !s_pkgs[idx].root.str("menu_name").empty())
            menu_text(buf, n, s_pkgs[idx].root.str("menu_name"));
        else
            menu_text(buf, n, nfl2k5_pres_package_name(idx));
        break;
    case 1: case 2: {
        const auto &l = songs(what);
        if (idx <= 0 || idx > (int)l.size()) snprintf(buf, n, "Off");
        else menu_text(buf, n, l[idx - 1].title);
        break;
    }
    case 3: snprintf(buf, n, "%s", idx ? "On" : "Off"); break;
    case 4: snprintf(buf, n, "%d%%", idx * 10); break;
    case 5: snprintf(buf, n, "%s", idx ? "On" : "Off"); break;
    }
}
extern "C" void nfl2k5_pres_set(int what, int v)
{
    int n = nfl2k5_pres_values(what);
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    switch (what) {
    case 0:
        pregame_replacement_reset();
        s_eras_saved_pkg = -1;     /* M9: the player's own pick replaces an Eras package and is saved */
        s_sel_pkg = v;
        s_sel_intro = std::min(1, nfl2k5_pres_values(1) - 1);    /* first theme of the new package */
        s_sel_outro = std::min(1, nfl2k5_pres_values(2) - 1);
        break;
    case 1: s_sel_intro = v; break;
    case 2: s_sel_outro = v; break;
    case 3: s_sel_anim = v; break;
    case 4: s_sel_vol = v; break;
    case 5: s_sel_commentary = v; break;
    }
    save_settings();
}

/* ---- M9: My NFL Eras package choice and identity ----------------------- */

/* presentations.ini of the Eras roots: "<season> = pkg", "<first>-<last> = pkg" or
 * "<decade>s = pkg"; the most specific key wins. The package is named by its
 * folder under mods\presentations or by its display name. -1 = none installed. */
static int eras_ini_package(int season, std::string &why)
{
    FILE *f = nullptr;
    for (int r = 0; r < nfl2k5_pack_root_count() && !f; r++) f = fopen(eras_root_rel(r, "presentations.ini").c_str(), "r");
    char line[512];
    int best = -1, best_rank = 0;
    if (!f) { why = "no Eras presentations.ini"; return -1; }
    why = "no line for the season";
    while (fgets(line, sizeof line, f)) {
        char key[64], val[400];
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        if (sscanf(line, " %63[^= \t] = %399[^\r\n;]", key, val) != 2) continue;
        std::string v = val;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        int a = 0, b = 0, rank = 0;
        char tail[8] = "";
        if (sscanf(key, "%d-%d", &a, &b) == 2) rank = season >= a && season <= b ? 2 : 0;
        else if (sscanf(key, "%d%7s", &a, tail) == 2 && (tail[0] == 's' || tail[0] == 'S') && !tail[1]) rank = season / 10 * 10 == a ? 1 : 0;
        else if (sscanf(key, "%d", &a) == 1 && std::to_string(a) == key) rank = season == a ? 3 : 0;
        if (rank <= best_rank) continue;
        int found = -1;
        for (size_t i = 1; i < s_pkgs.size(); i++) {
            const std::string &d = s_pkgs[i].dir;
            std::string folder = d.substr(d.find_last_of('/') + 1);
            if (_stricmp(folder.c_str(), v.c_str()) == 0 || _stricmp(s_pkgs[i].name.c_str(), v.c_str()) == 0) { found = (int)i; break; }
        }
        if (found < 0) { why = "package '" + v + "' (" + key + ") is not installed"; continue; }
        best = found; best_rank = rank; why = std::string(key) + " = " + v;
    }
    fclose(f);
    return best;
}

extern "C" void nfl2k5_presentation_eras_begin(int season, int week, const char *home_id, const char *away_id)
{
    ErasPres e;
    e.on = true;
    e.season = season;
    e.week = week;
    e.id[HOME] = home_id ? home_id : "";
    e.id[AWAY] = away_id ? away_id : "";
    for (int s = 0; s < 2; s++)
        if (!eras_team_load(season, e.id[s], e.team[s]))
            fprintf(stderr, "[PRES] eras: no teams/%d/%s/team.json in the Eras roots; the %s side uses the normal lookup\n",
                    season, e.id[s].c_str(), s == AWAY ? "away" : "home");
    {
        std::lock_guard<std::mutex> lock(s_eras_lock);
        s_eras = e;
    }
    s_eras_on = 1;
    std::string why;
    int pk = eras_ini_package(season, why);
    if (pk > 0 && pk != s_sel_pkg) {
        if (s_eras_saved_pkg < 0) {
            s_eras_saved_pkg = (int)s_sel_pkg;
            s_eras_saved_intro = s_sel_intro;
            s_eras_saved_outro = s_sel_outro;
        }
        pregame_replacement_reset();
        s_sel_pkg = pk;
        s_sel_intro = std::min(1, nfl2k5_pres_values(1) - 1);
        s_sel_outro = std::min(1, nfl2k5_pres_values(2) - 1);
    }
    fprintf(stderr, "[PRES] eras game: season %d week %d, away %s (%s %s, logo %s), home %s (%s %s, logo %s); package \"%s\" (%s)\n",
            season, week, e.id[AWAY].c_str(), e.team[AWAY].city.c_str(), e.team[AWAY].name.c_str(),
            e.team[AWAY].logo.empty() ? "none" : e.team[AWAY].logo.c_str(),
            e.id[HOME].c_str(), e.team[HOME].city.c_str(), e.team[HOME].name.c_str(),
            e.team[HOME].logo.empty() ? "none" : e.team[HOME].logo.c_str(),
            nfl2k5_pres_package_name(s_sel_pkg), pk > 0 ? why.c_str() : ("player's selection kept: " + why).c_str());
}

extern "C" void nfl2k5_presentation_eras_end(void)
{
    if (!s_eras_on.load()) return;
    s_eras_on = 0;
    {
        std::lock_guard<std::mutex> lock(s_eras_lock);
        s_eras = ErasPres();
    }
    int saved = s_eras_saved_pkg.exchange(-1);
    if (saved >= 0) {
        pregame_replacement_reset();
        s_sel_pkg = saved;
        s_sel_intro = s_eras_saved_intro;
        s_sel_outro = s_eras_saved_outro;
    }
    fprintf(stderr, "[PRES] eras game over: normal team lookup; package \"%s\"%s\n",
            nfl2k5_pres_package_name(s_sel_pkg), saved >= 0 ? " (the player's selection again)" : "");
}

/* season / decade / week for the Presentation API (null outside an Eras game) */
static std::string eras_api_fields()
{
    int season = 0, week = 0;
    if (s_eras_on.load()) {
        std::lock_guard<std::mutex> lock(s_eras_lock);
        if (s_eras.on) { season = s_eras.season; week = s_eras.week; }
    }
    if (!season) return ",\"league\":null,\"season\":null,\"decade\":null,\"week\":null";
    return ",\"league\":\"eras\",\"season\":" + std::to_string(season) + ",\"decade\":\"" + std::to_string(season / 10 * 10) +
           "s\",\"week\":" + (week > 0 ? std::to_string(week) : std::string("null"));
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

/* ======================================================================
 * Optional situation commentary
 *
 * E:\cfbsound is a flat library whose filenames describe the situation
 * (PRE_4TH_GOAL, POST_INT, Q4, GETS_1ST, and so on).  Keep only an index in
 * memory and decode one selected WAV on demand.  The game's commentary
 * centre channel is muted independently, leaving crowd, music and effects
 * intact.  This works with ESPN Original as well as custom scorebugs.
 * ====================================================================== */
struct CommentaryClip {
    std::string path, key;
};
struct CommentaryAudio {
    IXAudio2 *xa = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    IXAudio2SourceVoice *voice = nullptr;
    std::vector<int16_t> pcm;
    std::vector<CommentaryClip> clips;
    std::mutex lock;
    std::atomic<int> state{ 0 };       /* 0 idle, 1 decoding, 2 playing */
    std::atomic<int> generation{ 0 };
    std::atomic<int> indexed{ 0 };
    size_t last_pick = (size_t)-1;
    DWORD last_request = 0;
} s_commentary;
static std::string s_commentary_test;
static std::atomic<ULONGLONG> s_commentary_pass_serial{ 0 };
static std::atomic<DWORD> s_commentary_last_pass_tick{ 0 };

static std::string narrow(const std::wstring &w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static DWORD WINAPI commentary_index_thread(void *)
{
    std::wstring dir = widen(s_commentary_dir), mask = dir + L"\\*.wav";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(mask.c_str(), &fd);
    std::vector<CommentaryClip> found;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name = narrow(fd.cFileName);
            found.push_back({ narrow(dir + L"\\" + fd.cFileName), upper(name) });
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    size_t count = 0;
    {
        std::lock_guard<std::mutex> g(s_commentary.lock);
        s_commentary.clips.swap(found);
        count = s_commentary.clips.size();
    }
    s_commentary.indexed = 1;
    fprintf(stderr, "[COMMENTARY] indexed %zu clips from %s\n",
            count, s_commentary_dir.c_str());
    return 0;
}

static bool key_has(const std::string &key, const char *token)
{
    return token && *token && key.find(token) != std::string::npos;
}

static int commentary_context_score(const std::string &key, const GameState &g, int prior_down)
{
    int score = key_has(key, "_GEN") ? 8 : 0;
    char q[8]; snprintf(q, sizeof q, "Q%d", g.period);
    if (g.period >= 1 && g.period <= 4) {
        if (key_has(key, q)) score += 7;
        for (int i = 1; i <= 4; i++) {
            char other[8]; snprintf(other, sizeof other, "Q%d", i);
            if (i != g.period && key_has(key, other)) score -= 15;
        }
    }
    bool goal = g.phase == 4 && fabsf(g.line) >= 4571.0f;
    if (goal && key_has(key, "GOAL")) score += 10;
    if (!goal && key_has(key, "GOAL")) score -= 16;
    for (int d = 1; d <= 4; d++) {
        char now_down[16], was_down[16];
        snprintf(now_down, sizeof now_down, "NOW_%d%s", d, d == 1 ? "ST" : d == 2 ? "ND" : d == 3 ? "RD" : "TH");
        snprintf(was_down, sizeof was_down, "WAS_%d%s", d, d == 1 ? "ST" : d == 2 ? "ND" : d == 3 ? "RD" : "TH");
        if (key_has(key, now_down)) score += d == g.down ? 10 : -18;
        if (key_has(key, was_down)) score += d == prior_down ? 10 : -18;
    }
    if (g.period == 4 && g.clock <= 5.5f) {
        if (key_has(key, "LESS_5_SEC") || key_has(key, "5SEC") || key_has(key, "5_SEC")) score += 14;
    } else if (key_has(key, "LESS_5_SEC") || key_has(key, "5SEC")) score -= 20;
    /* These describe college-only context that NFL 2K5 cannot verify. */
    static const char *avoid[] = { "CDM_", "HEIS_", "TOP25", "RANK", "RECRUIT", "HFA_",
                                   "RIVAL", "BOWL_", "SNOW", "RAIN", "WEEK_", "SEAS_" };
    for (const char *a : avoid) if (key_has(key, a)) score -= 12;
    return score;
}

static void commentary_play(const std::string &path, const std::string &key)
{
    if (s_commentary.state != 0) return;
    int gen = ++s_commentary.generation;
    s_commentary.state = 1;
    std::thread([path, key, gen]() {
        std::vector<int16_t> pcm;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool ok = decode_mp3(path, pcm);
        CoUninitialize();
        std::lock_guard<std::mutex> g(s_commentary.lock);
        if (gen != s_commentary.generation || s_commentary.state != 1) return;
        if (!ok) {
            fprintf(stderr, "[COMMENTARY] could not decode %s\n", path.c_str());
            s_commentary.state = 0;
            return;
        }
        if (!s_commentary.xa &&
            (FAILED(XAudio2Create(&s_commentary.xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
             FAILED(s_commentary.xa->CreateMasteringVoice(&s_commentary.master)))) {
            s_commentary.state = 0;
            return;
        }
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 48000;
        wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 48000 * 4;
        if (FAILED(s_commentary.xa->CreateSourceVoice(&s_commentary.voice, &wf))) {
            s_commentary.state = 0;
            return;
        }
        s_commentary.pcm.swap(pcm);
        XAUDIO2_BUFFER b = {};
        b.AudioBytes = (UINT32)(s_commentary.pcm.size() * sizeof(int16_t));
        b.pAudioData = (const BYTE *)s_commentary.pcm.data();
        b.Flags = XAUDIO2_END_OF_STREAM;
        s_commentary.voice->SubmitSourceBuffer(&b);
        s_commentary.voice->SetVolume(1.0f);
        s_commentary.voice->Start();
        s_commentary.state = 2;
        fprintf(stderr, "[COMMENTARY] playing %s (%.1f s)\n", key.c_str(),
                s_commentary.pcm.size() / 96000.0);
    }).detach();
}

/* marker2 may be null, or it may narrow a broad family such as POST + INT. */
static void commentary_request(const char *marker1, const char *marker2, const GameState &g, int prior_down = 0)
{
    if (!s_sel_commentary || !s_commentary.indexed || s_commentary.state != 0) return;
    DWORD now = GetTickCount();
    if (now - s_commentary.last_request < 1800) return;
    int best = -100000;
    std::vector<size_t> choices;
    {
        std::lock_guard<std::mutex> lock(s_commentary.lock);
        for (size_t i = 0; i < s_commentary.clips.size(); i++) {
            const std::string &key = s_commentary.clips[i].key;
            if (!key_has(key, marker1) || (marker2 && !key_has(key, marker2))) continue;
            int score = commentary_context_score(key, g, prior_down);
            if (key_has(key, "2MAN")) score += 1;
            if (i == s_commentary.last_pick) score -= 30;
            if (score > best) { best = score; choices.clear(); choices.push_back(i); }
            else if (score == best) choices.push_back(i);
        }
        if (choices.empty()) {
            fprintf(stderr, "[COMMENTARY] no clip for %s%s%s\n", marker1,
                    marker2 ? " + " : "", marker2 ? marker2 : "");
            return;
        }
        size_t pick = choices[(GetTickCount64() / 1000u + choices.size()) % choices.size()];
        s_commentary.last_pick = pick;
        s_commentary.last_request = now;
        commentary_play(s_commentary.clips[pick].path, s_commentary.clips[pick].key);
    }
}

static void commentary_tick(const GameState &g, bool match_active)
{
    static GameState prev;
    static bool prev_valid;
    static bool muted;
    static bool pending_run;
    static DWORD pending_run_due;
    static ULONGLONG pending_pass_serial;
    static GameState pending_before, pending_after;
    if (!s_sel_commentary || !match_active || !g.valid) {
        if (muted) xbox_AudioSetCommentaryGain(1.0f);
        muted = false;
        prev_valid = false;
        pending_run = false;
        if (!s_sel_commentary && s_commentary.state != 0) {
            std::lock_guard<std::mutex> lock(s_commentary.lock);
            ++s_commentary.generation;
            if (s_commentary.voice) {
                s_commentary.voice->Stop();
                s_commentary.voice->DestroyVoice();
                s_commentary.voice = nullptr;
            }
            s_commentary.pcm.clear();
            s_commentary.state = 0;
        }
        return;
    }
    if (!muted) xbox_AudioSetCommentaryGain(0.0f);
    muted = true;
    if (s_commentary.state == 2) {
        std::lock_guard<std::mutex> lock(s_commentary.lock);
        XAUDIO2_VOICE_STATE vs = {};
        if (s_commentary.voice) s_commentary.voice->GetState(&vs, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (!s_commentary.voice || vs.BuffersQueued == 0) {
            if (s_commentary.voice) { s_commentary.voice->DestroyVoice(); s_commentary.voice = nullptr; }
            s_commentary.pcm.clear();
            s_commentary.state = 0;
        }
    }
    if (!s_commentary_test.empty() && s_commentary.indexed) {
        commentary_request(s_commentary_test.c_str(), nullptr, g);
        s_commentary_test.clear();
    }
    if (!prev_valid) {
        prev = g; prev_valid = true;
        return;
    }
    DWORD now = GetTickCount();
    if (pending_run && (int32_t)(now - pending_run_due) >= 0) {
        pending_run = false;
        if (s_commentary_pass_serial == pending_pass_serial && s_commentary.state == 0) {
            bool got_first = pending_before.down > 1 && pending_after.down == 1 &&
                             pending_before.poss == pending_after.poss;
            commentary_request("POST_RUN_", got_first ? "GETS_1ST" : nullptr,
                               pending_after, pending_before.down);
        }
    }
    for (int side = 0; side < 2; side++) {
        int points = g.t[side].score - prev.t[side].score;
        if (points >= 6) {
            bool pass_score = now - s_commentary_last_pass_tick.load() < 1800;
            commentary_request(pass_score ? "TD_ANALYSIS_PASS_" : "TD_ANALYSIS_RUSH_", nullptr, g);
        }
        else if (points == 3) commentary_request("FG_SCORED_", nullptr, g);
        else if (points == 2) commentary_request("POST_2PT", nullptr, g);
    }
    /* At five seconds the library has no safe standalone clock call.  The
     * normal PRE_* request below still chooses a fourth-quarter clip without
     * accidentally matching filenames containing 45SEC. */
    if (g.period > prev.period && prev.period == 2)
        commentary_request("HALFTIME_", nullptr, g);
    bool new_down = g.phase == 4 && g.down >= 1 && g.down <= 4 &&
                    (prev.phase != 4 || g.down != prev.down || g.poss != prev.poss || g.line != prev.line);
    if (new_down) {
        bool first_scrimmage = prev.phase != 4 || !prev.poss || g.poss != prev.poss;
        bool completed_play = prev.phase == 4 && prev.poss && g.poss &&
                              (g.down != prev.down || g.poss != prev.poss || fabsf(g.ball - prev.ball) > 10.0f);
        if (g.down == 4 && fabsf(g.line) >= 4571.0f) {
            pending_run = false;
            commentary_request("PRE_4TH_GOAL", nullptr, g);
        } else if (first_scrimmage) {
            commentary_request("PRE_1ST_DOWN_GEN", nullptr, g);
        } else if (completed_play &&
                   g.t[0].score == prev.t[0].score && g.t[1].score == prev.t[1].score) {
            /* QB stats arrive on the guest thread just after the spot changes.
             * Give that sample a moment to identify pass/incompletion.  If no
             * attempt changed, the completed play was a run. */
            pending_run = true;
            pending_run_due = now + 650;
            pending_pass_serial = s_commentary_pass_serial;
            pending_before = prev;
            pending_after = g;
        }
    }
    prev = g;
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
/* Front-end voice clips (My NFL Eras draft show, docs/eras/results/M11-draftshow.md): four
 * channels (a chime's tail can ring under the call; channel 3 is the show's looping music bed,
 * M12), each one clip at a time, decoded by Media Foundation to 48 kHz stereo and played on this
 * file's own XAudio2 engine; mixed into the RECOMP_AUDIO_WAV / video-clip recordings like the
 * theme. The caller ducks the game's own audio (menu music) with nfl2k5_presentation_voice_duck,
 * and a channel's level (the bed under the call) with nfl2k5_presentation_voice_gain. */
#define VOICE_CHANNELS 4
struct VoiceChannel {
    IXAudio2SourceVoice *voice = nullptr;
    std::vector<int16_t> pcm;
    size_t cap_pos = 0;
    float vol = 1.0f;
    bool loop = false;                    /* the whole clip again and again (the bed) */
    std::atomic<int> state{ 0 };          /* 0 idle, 1 decoding, 2 playing */
    std::atomic<int> generation{ 0 };
    double length = 0;
};
static struct {
    std::mutex lock;
    IXAudio2 *xa = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    VoiceChannel ch[VOICE_CHANNELS];
} s_voice;

static void voice_release_locked(VoiceChannel &c)
{
    if (c.voice) { c.voice->Stop(); c.voice->FlushSourceBuffers(); c.voice->DestroyVoice(); c.voice = nullptr; }
    c.pcm.clear();
    c.cap_pos = 0;
}

/* ch < 0: every channel */
extern "C" void nfl2k5_presentation_voice_stop(int ch)
{
    std::lock_guard<std::mutex> g(s_voice.lock);
    for (int i = 0; i < VOICE_CHANNELS; i++)
        if (ch < 0 || ch == i) { ++s_voice.ch[i].generation; voice_release_locked(s_voice.ch[i]); s_voice.ch[i].state = 0; }
}

/* Starts decoding and playing `path` (WAV or MP3) on channel `ch` at `volume`; 0, or -1.
 * loop: the clip repeats without a break until stopped (XAudio2 loops the one buffer; the
 * recording copy wraps the same way). */
extern "C" int nfl2k5_presentation_voice_play_ex(int ch, const char *path, float volume, int loop)
{
    if (!path || !*path || ch < 0 || ch >= VOICE_CHANNELS) return -1;
    nfl2k5_presentation_voice_stop(ch);
    VoiceChannel &c = s_voice.ch[ch];
    int gen = ++c.generation;
    c.state = 1;
    c.loop = loop != 0;
    std::string p = path;
    std::thread([p, gen, volume, ch, loop]() {
        std::vector<int16_t> pcm;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool ok = decode_mp3(p, pcm);
        CoUninitialize();
        std::lock_guard<std::mutex> g(s_voice.lock);
        VoiceChannel &c = s_voice.ch[ch];
        if (gen != c.generation) return;
        if (!ok) { fprintf(stderr, "[PRES] voice: could not decode %s\n", p.c_str()); c.state = 0; return; }
        if (!s_voice.xa) {
            if (FAILED(XAudio2Create(&s_voice.xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
                FAILED(s_voice.xa->CreateMasteringVoice(&s_voice.master))) { c.state = 0; return; }
            if (getenv("RECOMP_AUDIO_MUTE")) s_voice.master->SetVolume(0.0f);   /* background tests: still recorded */
        }
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 48000;
        wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 48000 * 4;
        if (FAILED(s_voice.xa->CreateSourceVoice(&c.voice, &wf))) { c.state = 0; return; }
        c.pcm.swap(pcm);
        XAUDIO2_BUFFER b = {};
        b.AudioBytes = (UINT32)(c.pcm.size() * 2);
        b.pAudioData = (const BYTE *)c.pcm.data();
        b.Flags = XAUDIO2_END_OF_STREAM;
        if (loop) { b.LoopBegin = 0; b.LoopLength = 0; b.LoopCount = XAUDIO2_LOOP_INFINITE; }
        c.voice->SubmitSourceBuffer(&b);
        c.vol = volume;
        c.voice->SetVolume(volume);
        c.cap_pos = 0;
        c.length = c.pcm.size() / 96000.0;
        c.voice->Start();
        c.state = 2;
        fprintf(stderr, "[PRES] voice t=%lu ch %d playing %s (%.2f s, volume %.2f%s)\n", GetTickCount(), ch, p.c_str(), c.length, volume,
                loop ? ", looping" : "");
    }).detach();
    return 0;
}

extern "C" int nfl2k5_presentation_voice_play(int ch, const char *path, float volume)
{
    return nfl2k5_presentation_voice_play_ex(ch, path, volume, 0);
}

/* The channel's level now (speakers and the recording copy): the caller ramps it. */
extern "C" void nfl2k5_presentation_voice_gain(int ch, float gain)
{
    if (ch < 0 || ch >= VOICE_CHANNELS) return;
    std::lock_guard<std::mutex> g(s_voice.lock);
    VoiceChannel &c = s_voice.ch[ch];
    gain = gain < 0.0f ? 0.0f : gain > 4.0f ? 4.0f : gain;
    if (c.vol == gain) return;
    c.vol = gain;
    if (c.voice && c.state == 2) c.voice->SetVolume(gain);
}

/* 0 idle (finished), 1 decoding, 2 playing; elapsed / length in seconds while playing. */
extern "C" int nfl2k5_presentation_voice_state(int ch, double *elapsed, double *length)
{
    if (ch < 0 || ch >= VOICE_CHANNELS) return 0;
    std::lock_guard<std::mutex> g(s_voice.lock);
    VoiceChannel &c = s_voice.ch[ch];
    if (c.state == 2 && c.voice) {
        XAUDIO2_VOICE_STATE vs;
        c.voice->GetState(&vs, 0);
        if (vs.BuffersQueued == 0) {
            fprintf(stderr, "[PRES] voice t=%lu ch %d finished (%.2f s)\n", GetTickCount(), ch, c.length);
            /* the recording copy keeps its samples until the capture has mixed them all */
            c.voice->DestroyVoice(); c.voice = nullptr;
            c.state = 0;
        } else {
            if (elapsed) *elapsed = vs.SamplesPlayed / 48000.0;
            if (length) *length = c.length;
        }
    }
    return c.state;
}

/* The game's own audio (menu music, effects) under the voice: 1 = full. */
extern "C" void nfl2k5_presentation_voice_duck(float gain)
{
    xbox_AudioSetGameGain(gain < 0.0f ? 0.0f : gain > 1.0f ? 1.0f : gain);
}

/* Runs on the audio thread for every 5.3 ms block (the audio report ring
 * records it always, AUDIO-REPORT-HOTKEY-20261022), so it never waits: a
 * lock held elsewhere (a voice being created or destroyed) skips the mix for
 * that block instead of stalling the speakers. */
static void capture_mix(int16_t *buf, int frames)
{
    {
        std::unique_lock<std::mutex> gv(s_voice.lock, std::try_to_lock);
        if (gv.owns_lock())
        for (auto &c : s_voice.ch) {
            if (c.pcm.empty() || c.state == 1) continue;
            float v = c.vol;
            for (int i = 0; i < frames * 2 && c.cap_pos < c.pcm.size(); i++, c.cap_pos++) {
                int x = buf[i] + (int)(c.pcm[c.cap_pos] * v);
                buf[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
                if (c.loop && c.cap_pos + 1 >= c.pcm.size() && c.voice) c.cap_pos = (size_t)-1;   /* wraps to 0 */
            }
            if (c.cap_pos >= c.pcm.size() && !c.voice) { c.pcm.clear(); c.cap_pos = 0; }
        }
    }
    std::unique_lock<std::mutex> g(s_music.lock, std::try_to_lock);
    if (!g.owns_lock() || !s_music.voice || s_music.state < 2) return;
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
struct PlayerStat {
    int kind = NFL2K5_STAT_QB;
    std::string name;
    int completions = 0, attempts = 0, passing_yards = 0, passing_touchdowns = 0, interceptions = 0;
    int carries = 0, rushing_yards = 0, rushing_touchdowns = 0;
    int receptions = 0, receiving_yards = 0, receiving_touchdowns = 0;
    int tackles = 0, sacks = 0, defensive_interceptions = 0;
    int field_goals_made = 0, field_goals_attempted = 0, longest_field_goal = 0;
};
struct PlayerIdentity {
    std::string name, position, college, portrait;
    int number = -1;
};
std::string nfl2k5_portrait_url(int photo_id);   /* src/presentation/portraits.cpp */
std::string nfl2k5_portraits_dir();              /* src/presentation/portraits.cpp */
struct Event {
    std::string name;
    int team = HOME;
    std::string ended_quarter;
    PlayerStat player;
    double requested_duration = 0;
    PlayerIdentity player_identity;
    int photo_id = -1;
};
struct ActiveAnim { Event ev; const JVal *def = nullptr; double t0 = 0, duration = 0; };

static std::vector<Event> s_queue;
static std::mutex s_queue_lock;
static ActiveAnim s_anim;
static bool s_anim_on;

struct LiveQbSample {
    PlayerStat player;
    uint64_t revision = 0;
};
static std::mutex s_live_qb_lock;
static LiveQbSample s_live_qb[2];
static PlayerIdentity s_live_qb_identity[2];
static uint64_t s_live_qb_revision;
/* The following lifecycle state belongs exclusively to the HUD thread. */
static Event s_live_qb_event;
static uint64_t s_live_qb_display_revision;
static bool s_live_qb_visible;
static uint32_t s_live_qb_match_clock;
static std::string s_live_qb_match_teams;
static bool s_live_qb_match_valid;
static int s_live_qb_match_period;

static void queue_event(Event ev)
{
    std::lock_guard<std::mutex> lock(s_queue_lock);
    s_queue.push_back(std::move(ev));
}

static bool pop_event(Event &ev)
{
    std::lock_guard<std::mutex> lock(s_queue_lock);
    if (s_queue.empty()) return false;
    ev = std::move(s_queue.front());
    s_queue.erase(s_queue.begin());
    return true;
}

static bool queue_empty()
{
    std::lock_guard<std::mutex> lock(s_queue_lock);
    return s_queue.empty();
}

/* Roster records use full names ("Quincy Carter"); FOX's compact stat
 * treatment uses the initial plus surname ("Q. Carter").  Keep a string
 * that is already abbreviated intact, so the presentation boundary can also
 * accept a title-provided display name without applying the conversion twice. */
static std::string broadcast_player_name(const char *full_name)
{
    std::string name = full_name ? full_name : "";
    size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = name.find_last_not_of(" \t\r\n");
    name = name.substr(first, last - first + 1);
    if (name.size() >= 2 && name[1] == '.') return name;
    size_t split = name.find_last_of(" \t");
    if (split == std::string::npos || split == 0 || split + 1 >= name.size())
        return name;
    return std::string(1, name[0]) + ". " + name.substr(split + 1);
}

static void clear_events()
{
    std::lock_guard<std::mutex> lock(s_queue_lock);
    s_queue.clear();
}

static const char *ordinal(int period)
{
    static const char *q[] = { "", "1ST", "2ND", "3RD", "4TH" };
    return period >= 1 && period <= 4 ? q[period] : "OT";
}

static bool is_final(const GameState &g);

static void detect_events(const GameState &p, const GameState &g)
{
    for (int s = 0; s < 2; s++) {
        int d = g.t[s].score - p.t[s].score;
        if (d > 0) {
            const char *name = d >= 6 ? "touchdown" : d == 3 ? "field_goal" : d == 1 ? "extra_point"
                             : d == 2 ? (p.phase == 3 ? "two_point" : "safety") : nullptr;
            if (name) queue_event({ name, s, "" });
        }
        if (g.t[s].timeouts < p.t[s].timeouts && g.period == p.period)
            queue_event({ "timeout", s, "" });
    }
    if (g.period == p.period + 1 && p.period >= 1) {
        const char *boundary = p.period == 1 ? "end_q1" : p.period == 2 ? "halftime" :
                               p.period == 3 ? "end_q3" : "quarter_start";
        queue_event({ boundary, g.poss ? g.poss - 1 : HOME,
                      std::string(ordinal(p.period)) + (p.period <= 4 ? " QUARTER" : "") });
    }
    if (!is_final(p) && is_final(g))
        queue_event({ g.period > 4 ? "final_overtime" : "final", g.poss ? g.poss - 1 : HOME, "FINAL" });
    if ((g.period == 2 || g.period == 4) && g.period == p.period && p.clock > 120.0f && g.clock <= 120.0f)
        queue_event({ "two_minute_warning", g.poss ? g.poss - 1 : HOME, "" });
    if (g.phase == 4 && p.phase == 4 && p.down > 1 && g.down == 1 && g.poss == p.poss &&
        g.t[0].score == p.t[0].score && g.t[1].score == p.t[1].score && g.poss)
        queue_event({ "first_down", g.poss - 1, "" });
}

static const char *event_name(Nfl2k5BroadcastEvent event)
{
    static const char *names[] = {
        "game_start", "drive_start", "first_down", "touchdown", "field_goal", "extra_point",
        "two_point", "turnover", "interception", "fumble", "sack", "penalty", "timeout",
        "injury", "replay_begin", "replay_end", "end_of_quarter", "halftime", "quarter_start",
        "game_end"
    };
    return event >= NFL2K5_EVENT_GAME_START && event <= NFL2K5_EVENT_GAME_END ? names[event] : nullptr;
}

extern "C" void nfl2k5_broadcast_event(Nfl2k5BroadcastEvent event, int team)
{
    const char *name = event_name(event);
    if (name) queue_event({ name, team == AWAY ? AWAY : HOME, "" });
}

static PlayerStat copy_player_stat(const Nfl2k5PlayerStat &s)
{
    PlayerStat out;
    out.kind = s.kind; out.name = broadcast_player_name(s.player_name);
#define COPY_STAT(F) out.F = s.F
    COPY_STAT(completions); COPY_STAT(attempts); COPY_STAT(passing_yards); COPY_STAT(passing_touchdowns); COPY_STAT(interceptions);
    COPY_STAT(carries); COPY_STAT(rushing_yards); COPY_STAT(rushing_touchdowns);
    COPY_STAT(receptions); COPY_STAT(receiving_yards); COPY_STAT(receiving_touchdowns);
    COPY_STAT(tackles); COPY_STAT(sacks); COPY_STAT(defensive_interceptions);
    COPY_STAT(field_goals_made); COPY_STAT(field_goals_attempted); COPY_STAT(longest_field_goal);
#undef COPY_STAT
    return out;
}

extern "C" void nfl2k5_broadcast_player_stat(const Nfl2k5PlayerStat *s, int team)
{
    if (!s || !s->player_name) return;
    Event ev;
    ev.name = "player_stat";
    ev.team = team == AWAY ? AWAY : HOME;
    ev.requested_duration = s->display_seconds > 0 ? s->display_seconds : 5.0;
    ev.player = copy_player_stat(*s);
    ev.player_identity.name = s->full_name ? s->full_name : "";
    if (s->kind == NFL2K5_STAT_QB) ev.player_identity.position = "QB";
    ev.player_identity.number = s->jersey_number;
    ev.photo_id = s->photo_id;
    if (s->photo_id >= 0) ev.player_identity.portrait = nfl2k5_portrait_url(s->photo_id);
    queue_event(std::move(ev));
}

extern "C" void nfl2k5_broadcast_live_qb_sample(const Nfl2k5PlayerStat *s, int team)
{
    if (!s || s->kind != NFL2K5_STAT_QB || !s->player_name || team < AWAY || team > HOME)
        return;
    PlayerStat next = copy_player_stat(*s);
    if (next.name.empty()) return;
    bool pass_attempt = false, complete = false, intercepted = false, pass_td = false;
    {
        std::lock_guard<std::mutex> lock(s_live_qb_lock);
        LiveQbSample &sample = s_live_qb[team];
        PlayerIdentity &identity = s_live_qb_identity[team];
        if (sample.revision && sample.player.name != next.name) identity = PlayerIdentity();
        if (s->full_name && *s->full_name) identity.name = s->full_name;
        if (identity.position.empty()) identity.position = "QB";
        if (s->jersey_number >= 0) identity.number = s->jersey_number;
        if (s->photo_id >= 0) identity.portrait = nfl2k5_portrait_url(s->photo_id);
        const PlayerStat &old = sample.player;
        if (sample.revision && old.name == next.name && next.attempts > old.attempts) {
            pass_attempt = true;
            complete = next.completions > old.completions;
            intercepted = next.interceptions > old.interceptions;
            pass_td = next.passing_touchdowns > old.passing_touchdowns;
        }
        if (!sample.revision || old.name != next.name || old.completions != next.completions ||
            old.attempts != next.attempts || old.passing_yards != next.passing_yards ||
            old.passing_touchdowns != next.passing_touchdowns || old.interceptions != next.interceptions) {
            sample.player = next;
            sample.revision = ++s_live_qb_revision;
        }
    }
    if (pass_attempt) {
        ++s_commentary_pass_serial;
        GameState g;
        if (read_state(g) && g.valid && g.phase == 4 && g.poss == team + 1) {
            if (intercepted)
                commentary_request("POST_INT_", nullptr, g);
            else if (pass_td)
                commentary_request("TD_ANALYSIS_PASS_", nullptr, g);
            else if (complete) {
                bool got_first = g.down == 1;
                commentary_request("POST_PASS_COMP_", got_first ? "GETS_1ST" : nullptr, g);
            } else
                commentary_request("POST_PASS_INCOMP_", nullptr, g);
        }
    }
}

extern "C" void nfl2k5_broadcast_live_qb_reset(void)
{
    std::lock_guard<std::mutex> lock(s_live_qb_lock);
    s_live_qb[AWAY] = LiveQbSample();
    s_live_qb[HOME] = LiveQbSample();
    s_live_qb_identity[AWAY] = PlayerIdentity();
    s_live_qb_identity[HOME] = PlayerIdentity();
}

/* Opcode 133 supplies a title/kind, but no selected player or historical
 * dataset. Only the specifically QB-related title below can use the live
 * QB sample. Generic, receiving and career cards retain their native card
 * until the actual card player/scope is extracted. */
extern "C" int nfl2k5_presentation_playercard(uint32_t kind, const char *native_title)
{
    int k = s_sel_pkg.load();
    if (k <= 0 || k >= (int)s_pkgs.size() || !s_pkgs[k].html ||
        !s_pkgs[k].replaces_player_cards)
        return 0;
    std::string title = native_title ? native_title : "";
    std::string folded = title;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    if (folded != "consecutive incompletions") return 0;
    GameState g;
    if (!read_state(g) || g.poss < 1 || g.poss > 2) return 0;
    int side = g.poss - 1;
    LiveQbSample sample;
    PlayerIdentity identity;
    {
        std::lock_guard<std::mutex> ql(s_live_qb_lock);
        sample = s_live_qb[side];
        identity = s_live_qb_identity[side];
    }
    if (!g.valid || !sample.revision || sample.player.name.empty()) return 0;
    Event ev;
    ev.name = "player_stat";
    ev.team = side;
    ev.player = sample.player;
    ev.player_identity = identity;
    ev.requested_duration = 6.0;
    queue_event(std::move(ev));
    fprintf(stderr, "[PRES] replaces ESPN player card kind %u title '%s' as %s for side %d\n",
            kind, title.c_str(), "PLAYER_STAT", side);
    return 1;
}

/* The automatic QB is a persistent scorebug component, not a timed insert.
 * Its copied sample follows possession and remains visible until the live
 * camera is hidden. It never occupies the broadcast animation queue. */
static void service_live_qb(const GameState &g, bool visible, const JVal *anims)
{
    uint32_t clock = g.valid ? rd32(0x00E6028Cu) : 0;
    std::string teams = g.t[AWAY].abbr + "|" + g.t[HOME].abbr;
    bool new_match = g.valid && s_live_qb_match_valid &&
                     (clock != s_live_qb_match_clock || teams != s_live_qb_match_teams ||
                      (g.period == 0 && s_live_qb_match_period > 0));
    if ((!g.valid && s_live_qb_match_valid) || new_match) {
        nfl2k5_broadcast_live_qb_reset();
        s_live_qb_display_revision = 0;
    }
    s_live_qb_match_valid = g.valid;
    s_live_qb_match_clock = clock;
    s_live_qb_match_teams = std::move(teams);
    s_live_qb_match_period = g.period;
    s_live_qb_visible = false;
    if (!g.valid || !visible || !anims || !anims->get("player_stat") ||
        g.poss < 1 || g.poss > 2)
        return;
    int side = g.poss - 1;
    LiveQbSample sample;
    {
        std::lock_guard<std::mutex> lock(s_live_qb_lock);
        sample = s_live_qb[side];
    }
    if (!sample.revision || sample.player.name.empty())
        return;
    if (sample.revision != s_live_qb_display_revision || s_live_qb_event.team != side) {
        s_live_qb_event.name = "player_stat";
        s_live_qb_event.team = side;
        s_live_qb_event.player = std::move(sample.player);
        s_live_qb_display_revision = sample.revision;
        const PlayerStat &p = s_live_qb_event.player;
        fprintf(stderr, "[LIVE-QB] %s %d/%d %d YDS TD=%d INT=%d side=%d possession=%d persistent=1\n",
                p.name.c_str(), p.completions, p.attempts, p.passing_yards,
                p.passing_touchdowns, p.interceptions, side, g.poss);
    }
    s_live_qb_visible = true;
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

static std::string ball_position(const GameState &g)
{
    if (g.phase != 4 || !g.poss || !std::isfinite(g.ball)) return "";
    /* The down-state distances are centimetres measured from one goal line.
     * Present them in the conventional FOX "PHI 37" form.  Which side of
     * midfield owns the yard line follows possession and field direction;
     * team abbreviations keep the result useful even before the title's
     * drive-direction flag is mapped. */
    int yard = (int)lroundf(std::max(0.0f, std::min(100.0f, g.ball / 91.44f)));
    int shown_yard = yard <= 50 ? yard : 100 - yard;
    if (shown_yard <= 0) return "GOAL";
    /* Team-side labelling additionally needs the title's current drive
     * direction (coin toss and quarter changes); do not guess it. */
    return "BALL " + std::to_string(shown_yard);
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
        if (k == "record") return t.record;
        return "";
    };
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        std::string who = name.substr(0, dot), k = name.substr(dot + 1);
        if (who == "player" && c.ev) {
            const PlayerStat &p = c.ev->player;
            if (k == "name") return upper(p.name);
#define PLAYER_INT(KEY, FIELD) if (k == KEY) return std::to_string(p.FIELD)
            PLAYER_INT("completions", completions); PLAYER_INT("attempts", attempts);
            PLAYER_INT("passing_yards", passing_yards); PLAYER_INT("passing_touchdowns", passing_touchdowns);
            PLAYER_INT("interceptions", interceptions); PLAYER_INT("carries", carries);
            PLAYER_INT("rushing_yards", rushing_yards); PLAYER_INT("rushing_touchdowns", rushing_touchdowns);
            PLAYER_INT("receptions", receptions); PLAYER_INT("receiving_yards", receiving_yards);
            PLAYER_INT("receiving_touchdowns", receiving_touchdowns); PLAYER_INT("tackles", tackles);
            PLAYER_INT("sacks", sacks); PLAYER_INT("defensive_interceptions", defensive_interceptions);
            PLAYER_INT("field_goals_made", field_goals_made); PLAYER_INT("field_goals_attempted", field_goals_attempted);
            PLAYER_INT("longest_field_goal", longest_field_goal);
#undef PLAYER_INT
            if (k == "line") {
                char b[160];
                switch (p.kind) {
                case NFL2K5_STAT_QB:
                    /* FOX's compact passer insert leads with completion/attempt
                     * and yards.  Touchdowns and interceptions are contextual:
                     * leave either field out until the title reports a nonzero
                     * total, rather than displaying invented zero-value labels. */
                    snprintf(b, sizeof b, "%d/%d  %d YDS", p.completions, p.attempts,
                             p.passing_yards);
                    if (p.passing_touchdowns)
                        snprintf(b + strlen(b), sizeof b - strlen(b), "  %d TD",
                                 p.passing_touchdowns);
                    if (p.interceptions)
                        snprintf(b + strlen(b), sizeof b - strlen(b), "  %d INT",
                                 p.interceptions);
                    break;
                case NFL2K5_STAT_RB: snprintf(b, sizeof b, "%d CAR  %d YDS  %d TD", p.carries, p.rushing_yards, p.rushing_touchdowns); break;
                case NFL2K5_STAT_RECEIVER: snprintf(b, sizeof b, "%d REC  %d YDS  %d TD", p.receptions, p.receiving_yards, p.receiving_touchdowns); break;
                case NFL2K5_STAT_DEFENSE: snprintf(b, sizeof b, "%d TKL  %d SACK  %d INT", p.tackles, p.sacks, p.defensive_interceptions); break;
                default: snprintf(b, sizeof b, "%d/%d FG  LONG %d", p.field_goals_made, p.field_goals_attempted, p.longest_field_goal); break;
                }
                return b;
            }
            return "";
        }
        int s = who == "away" ? AWAY : who == "home" ? HOME : (who == "team" && c.ev) ? c.ev->team : -1;
        return s >= 0 ? team_var(s, k) : "";
    }
    if (name == "down_distance") return down_distance(g);
    if (name == "ball_on") return ball_position(g);
    if (name == "play_clock") {
        if (g.play_clock < 0) return "";
        char buf[8];
        snprintf(buf, sizeof buf, ":%02d", g.play_clock);
        return buf;
    }
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
    if (name == "flag_state") return GetTickCount64() < s_flag_until ? "FLAG" : "";
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
    size_t and_at = cond.find(" && ");
    if (and_at != std::string::npos)
        return shown(c, cond.substr(0, and_at)) && shown(c, cond.substr(and_at + 4));
    const GameState &g = *c.g;
    if (cond == "possession:away") return g.phase == 4 && g.poss == 1;
    if (cond == "possession:home") return g.phase == 4 && g.poss == 2;
    /* The player-stat event carries its own side.  Keeping this separate
     * from current possession lets a completed-play insert remain on the
     * correct side during the handoff to a kickoff or a turnover. */
    if (cond == "event_team:away") return c.ev && c.ev->team == AWAY;
    if (cond == "event_team:home") return c.ev && c.ev->team == HOME;
    if (cond == "play_clock_available") return c.g && c.g->play_clock >= 0;
    if (cond == "scrimmage") return g.phase == 4;
    if (cond == "not_scrimmage") return g.phase != 4;
    if (cond == "final") return is_final(g);
    if (cond == "not_final") return !is_final(g);
    if (cond == "flag") return GetTickCount64() < s_flag_until;
    if (cond == "no_flag") return GetTickCount64() >= s_flag_until;
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
        std::string shadow = e.str("shadow");
        if (!shadow.empty()) {
            float sox = (float)e.num("shadow_x", 2), soy = (float)e.num("shadow_y", 3);
            ID2D1SolidColorBrush *sh = nullptr;
            rt->CreateSolidColorBrush(color(c, shadow, alpha), &sh);
            if (sh) {
                D2D1_ROUNDED_RECT sr = D2D1::RoundedRect(D2D1::RectF(x + sox, y + soy, x + w + sox, y + h + soy), rad, rad);
                rt->FillRoundedRectangle(sr, sh); sh->Release();
            }
        }
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
    } else if (type == "polygon") {
        const JVal *pts = e.get("points");
        if (pts && pts->a.size() >= 3 && R.d2d) {
            ID2D1PathGeometry *geo = nullptr;
            ID2D1GeometrySink *sink = nullptr;
            if (SUCCEEDED(R.d2d->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
                auto point = [&](const JVal &p) {
                    return D2D1::Point2F(x + (p.a.size() > 0 ? (float)p.a[0].n : 0),
                                        y + (p.a.size() > 1 ? (float)p.a[1].n : 0));
                };
                sink->BeginFigure(point(pts->a[0]), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < pts->a.size(); i++) sink->AddLine(point(pts->a[i]));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close(); sink->Release();
                ID2D1SolidColorBrush *b = nullptr;
                rt->CreateSolidColorBrush(color(c, e.str("fill", "#FFFFFF"), alpha), &b);
                if (b) { rt->FillGeometry(geo, b); b->Release(); }
            }
            if (geo) geo->Release();
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
        float radius = (float)e.num("radius", bh / 2);
        for (int i = 0; i < count; i++) {
            ID2D1SolidColorBrush *b = nullptr;
            rt->CreateSolidColorBrush(color(c, i < have ? e.str("on", "#F5C518") : e.str("off", "#FFFFFF33"), alpha), &b);
            if (b) {
                float bx = x + i * (bw + gap);
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, y, bx + bw, y + bh), radius, radius), b);
                b->Release();
            }
        }
    }
    if (clipped) rt->PopAxisAlignedClip();
}

static float motion_ease(float p, const std::string &name)
{
    p = std::max(0.0f, std::min(1.0f, p));
    if (name == "linear") return p;
    if (name == "ease_in" || name == "quad_in") return p * p;
    if (name == "ease_in_out" || name == "smoothstep") return p * p * (3.0f - 2.0f * p);
    if (name == "cubic_in") return p * p * p;
    if (name == "cubic_out") { float q = 1.0f - p; return 1.0f - q * q * q; }
    if (name == "cubic_in_out")
        return p < 0.5f ? 4.0f * p * p * p : 1.0f - powf(-2.0f * p + 2.0f, 3.0f) / 2.0f;
    /* FOX elements decelerate quickly and settle without overshoot. */
    return 1.0f - (1.0f - p) * (1.0f - p);
}

struct MotionPose {
    float x = 0, y = 0, sx = 1, sy = 1, rotation = 0, opacity = 1;
    float crop_x = 1, crop_y = 1;
};

static float key_value(const JVal &k, const char *name, float fallback)
{
    const JVal *v = k.get(name);
    return v && v->t == JVal::Num ? (float)v->n : fallback;
}

/* Optional per-layer keyframes are expressed in seconds.  Every omitted
 * property inherits from the preceding keyframe, keeping motion definitions
 * compact and editable. */
static MotionPose motion_pose(const JVal &L, double local)
{
    MotionPose out;
    const JVal *keys = L.get("keyframes");
    if (!keys || keys->a.empty()) return out;
    MotionPose prev;
    double prev_t = keys->a.front().num("time", 0);
    auto apply = [](MotionPose p, const JVal &k) {
        p.x = key_value(k, "x", p.x); p.y = key_value(k, "y", p.y);
        p.sx = key_value(k, "scale_x", key_value(k, "scale", p.sx));
        p.sy = key_value(k, "scale_y", key_value(k, "scale", p.sy));
        p.rotation = key_value(k, "rotation", p.rotation);
        p.opacity = key_value(k, "opacity", p.opacity);
        p.crop_x = key_value(k, "crop_x", p.crop_x);
        p.crop_y = key_value(k, "crop_y", p.crop_y);
        return p;
    };
    prev = apply(prev, keys->a.front());
    if (local <= prev_t) return prev;
    for (size_t i = 1; i < keys->a.size(); i++) {
        const JVal &k = keys->a[i];
        double next_t = k.num("time", prev_t);
        MotionPose next = apply(prev, k);
        if (local <= next_t) {
            float p = next_t > prev_t ? (float)((local - prev_t) / (next_t - prev_t)) : 1.0f;
            p = motion_ease(p, k.str("ease", "linear"));
#define LERP_FIELD(F) out.F = prev.F + (next.F - prev.F) * p
            LERP_FIELD(x); LERP_FIELD(y); LERP_FIELD(sx); LERP_FIELD(sy);
            LERP_FIELD(rotation); LERP_FIELD(opacity); LERP_FIELD(crop_x); LERP_FIELD(crop_y);
#undef LERP_FIELD
            return out;
        }
        prev = next; prev_t = next_t;
    }
    return prev;
}

static void draw_layer(const Ctx &c, const JVal &L, double t, double duration)
{
    double start = L.num("start", 0), end = L.num("end", duration);
    double local = t - start;
    if (local < 0 || t > end) return;
    float alpha = 1, dx = 0, dy = 0, clip = 1;
    float w = (float)L.num("w", 80);
    float h = (float)L.num("h", 40);
    if (const JVal *en = L.get("enter")) {
        double tm = en->num("time", 0.3);
        float p = (float)std::min(1.0, local / std::max(0.01, tm));
        p = motion_ease(p, en->str("ease", "ease_out"));
        std::string ty = en->str("type", "fade");
        if (ty == "fade") alpha *= p;
        else if (ty == "slide_left") { dx -= (1 - p) * (w + 20); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "slide_right") { dx += (1 - p) * (w + 20); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "slide_up") { dy += (1 - p) * (h + 10); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "slide_down") { dy -= (1 - p) * (h + 10); alpha *= std::min(1.0f, p * 1.5f); }
        else if (ty == "wipe_x") clip = p;
    }
    if (const JVal *ex = L.get("exit")) {
        double tm = ex->num("time", 0.3);
        float q = (float)std::min(1.0, (end - t) / std::max(0.01, tm));
        std::string ty = ex->str("type", "fade");
        if (ty == "fade") alpha *= q;
        else if (ty == "slide_left") { dx -= (1 - q) * (w + 20); alpha *= q; }
        else if (ty == "slide_right") { dx += (1 - q) * (w + 20); alpha *= q; }
        else if (ty == "slide_down") { dy += (1 - q) * (h + 10); alpha *= q; }
        else if (ty == "slide_up") { dy -= (1 - q) * (h + 10); alpha *= q; }
        else if (ty == "wipe_x") clip = std::min(clip, q);
    }
    if (const JVal *bl = L.get("blink")) {
        double period = bl->num("period", 0.5), until = bl->num("until", duration);
        if (local < until && fmod(local, period) >= period / 2) return;
        if (local >= until) return;
    }
    MotionPose pose = motion_pose(L, local);
    dx += pose.x; dy += pose.y; alpha *= pose.opacity;
    clip = std::min(clip, pose.crop_x);
    D2D1_MATRIX_3X2_F old;
    R.rt->GetTransform(&old);
    float x = (float)L.num("x", 0) + dx, y = (float)L.num("y", 0) + dy;
    float ax = x + (float)L.num("anchor_x", 0.5) * w;
    float ay = y + (float)L.num("anchor_y", 0.5) * h;
    if (pose.sx != 1 || pose.sy != 1 || pose.rotation != 0) {
        D2D1_MATRIX_3X2_F local_xf = D2D1::Matrix3x2F::Scale(pose.sx, pose.sy, D2D1::Point2F(ax, ay)) *
                                      D2D1::Matrix3x2F::Rotation(pose.rotation, D2D1::Point2F(ax, ay));
        R.rt->SetTransform(local_xf * old);
    }
    bool crop_y = pose.crop_y < 1.0f;
    if (crop_y)
        R.rt->PushAxisAlignedClip(D2D1::RectF(x - 100, y, x + w + 100, y + h * pose.crop_y), D2D1_ANTIALIAS_MODE_ALIASED);
    /* A layer that lives above the bar (a banner) never shows over it while
     * it slides. */
    bool above = L.num("y", 0) + L.num("h", 0) <= 0.0;
    if (above) R.rt->PushAxisAlignedClip(D2D1::RectF(-100, -1000, 2000, 0), D2D1_ANTIALIAS_MODE_ALIASED);
    draw_element(c, L, alpha, dx, dy, clip);
    if (above) R.rt->PopAxisAlignedClip();
    if (crop_y) R.rt->PopAxisAlignedClip();
    R.rt->SetTransform(old);
}

/* ======================================================================
 * Frame logic (presenter thread)
 * ====================================================================== */
static std::atomic<int> s_native_visible{ 0 };
static std::atomic<DWORD> s_native_tick{ 0 };
/* Game-state globals survive a trip back to the frontend.  They are useful
 * data, but they are not proof that a match is still on screen.  This tick
 * is refreshed only by observable match activity (the native bug, a live
 * clock change, play selection, or the in-game pause controller). */
static std::atomic<DWORD> s_match_tick{ 0 };
static int s_loaded_pkg = -1;
static GameState s_prev;
static bool s_prev_valid;
enum BugLifecycle { BUG_HIDDEN, BUG_FADE_IN, BUG_LIVE, BUG_FADE_OUT };
static BugLifecycle s_bug_lifecycle = BUG_HIDDEN;
static float s_show = 0;            /* eased opacity of the whole bug */
static float s_life_progress = 0;   /* uninterrupted 0..1 transition */
static double s_last_t;
static std::string s_last_sig;
static bool s_game_started, s_outro_played, s_intro_active;
static double s_invalid_since;
static bool s_test, s_log, s_probe;
static std::string s_test_event;

static bool custom_active() { int p = s_sel_pkg; return p > 0 && p < (int)s_pkgs.size(); }

static void update_bug_lifecycle(const JVal *sb, bool wanted, float dt)
{
    const JVal *life = sb ? sb->get("lifecycle") : nullptr;
    const JVal *in = life ? life->get("fade_in") : nullptr;
    const JVal *out = life ? life->get("fade_out") : nullptr;
    float in_time = in ? (float)in->num("duration", 0.25) : (float)(sb ? sb->num("fade_time", 0.25) : 0.25);
    float out_time = out ? (float)out->num("duration", 0.2) : (float)(sb ? sb->num("fade_time", 0.2) : 0.2);
    if (wanted) {
        if (s_bug_lifecycle == BUG_HIDDEN || s_bug_lifecycle == BUG_FADE_OUT)
            s_bug_lifecycle = BUG_FADE_IN;
        if (s_bug_lifecycle == BUG_FADE_IN) {
            s_life_progress = std::min(1.0f, s_life_progress + dt / std::max(0.01f, in_time));
            s_show = motion_ease(s_life_progress, in ? in->str("ease", "cubic_out") : "cubic_out");
            if (s_life_progress >= 1.0f) { s_bug_lifecycle = BUG_LIVE; s_show = 1.0f; }
        }
    } else {
        if (s_bug_lifecycle == BUG_LIVE || s_bug_lifecycle == BUG_FADE_IN)
            s_bug_lifecycle = BUG_FADE_OUT;
        if (s_bug_lifecycle == BUG_FADE_OUT) {
            s_life_progress = std::max(0.0f, s_life_progress - dt / std::max(0.01f, out_time));
            s_show = motion_ease(s_life_progress, out ? out->str("ease", "cubic_in") : "cubic_in");
            if (s_life_progress <= 0.0f) { s_bug_lifecycle = BUG_HIDDEN; s_show = 0.0f; }
        }
    }
}

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

/* In a match, pause menu and replays included: the game's own scorebug
 * updated in the last 30 s. */
extern "C" int nfl2k5_in_match(void)
{
    DWORD t = s_match_tick;
    return t && GetTickCount() - t < 30000;
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
/* Presentation-only ownership: unlike the intro-theme flag above, this
 * follows native show/close and does not remain set for the whole match. */
static std::atomic<int> s_coin_toss_native_active{ 0 };
extern "C" void nfl2k5_presentation_popup(const char *title)
{
    if (!title) return;
    std::string t = upper(title);
    if (t.find("COIN TOSS") != std::string::npos) s_coin_toss = 1;
    int team = s_prev.poss ? s_prev.poss - 1 : HOME;
    GameState cg;
    read_state(cg);
    if (t.find("PENALTY") != std::string::npos || t.find("FLAG") != std::string::npos) {
        s_flag_until = GetTickCount64() + 8000;
        queue_event({ "penalty", team, "" });
    } else if (t.find("INTERCEPTION") != std::string::npos) {
        queue_event({ "interception", team, "" });
        commentary_request("POST_INT_", nullptr, cg);
    } else if (t.find("FUMBLE") != std::string::npos) {
        queue_event({ "fumble", team, "" });
        commentary_request("POST_FUM", nullptr, cg);
    } else if (t.find("SACK") != std::string::npos) {
        queue_event({ "sack", team, "" });
        commentary_request("POST_SACK_", nullptr, cg);
    }
    else if (t.find("TURNOVER") != std::string::npos) queue_event({ "turnover", team, "" });
    else if (t.find("INJURY") != std::string::npos) queue_event({ "injury", team, "" });
    else if (t.find("REPLAY") != std::string::npos) queue_event({ "replay_begin", team, "" });
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
        /* A package may define a pregame scorebug entrance.  It is queued
         * after the match state becomes valid, not while a full-screen open
         * video is holding the HUD. */
        queue_event({ "pregame", HOME, "" });
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

/* ======================================================================
 * HTML presentation bridge (Presentation API v1, docs/PRESENTATION-SYSTEM.md)
 *
 * The game side reports football STATE and EVENTS as JSON; an HTML package
 * running in the PresentationHost decides how they look. Nothing here is
 * specific to a network. The host's transparent frame goes out through the
 * same HUD image as the JSON renderer, so no graphics backend is involved.
 * ====================================================================== */
static std::string json_str(const std::string &s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

static std::string json_color(const D2D1_COLOR_F &c)
{
    char b[16];
    snprintf(b, sizeof b, "\"#%02X%02X%02X\"", (int)lroundf(c.r * 255), (int)lroundf(c.g * 255), (int)lroundf(c.b * 255));
    return b;
}

/* mods/teams/SF/logos/scorebug.png -> https://teams.local/SF/logos/scorebug.png */
static std::string team_asset_url(const std::string &path)
{
    const std::string prefix = "mods/teams/", eras = eras_teams_root() + "/";
    if (path.compare(0, prefix.size(), prefix) == 0) return "https://teams.local/" + path.substr(prefix.size());
    /* M9: <Eras teams root>/1980/colts/logos/scorebug.png -> https://erateams.local/1980/colts/logos/scorebug.png */
    if (eras.size() > 1 && path.compare(0, eras.size(), eras) == 0) return "https://erateams.local/" + path.substr(eras.size());
    return "";
}

static const char *side_name(int side) { return side == AWAY ? "away" : "home"; }

struct HtmlContext { bool scorebug_visible, play_selection, paused, native_scorebug; };

/* The original overall accessor is sampled on the guest popup/script path.
 * The presenter only consumes these owned strings and roster identities. */
struct FeaturedRosterSnapshot {
    uint32_t clock_object = 0, roster[2] = { 0, 0 };
    unsigned count[2] = { 0, 0 };
    std::string abbr[2], player[2];
};
static std::mutex s_featured_lock;
static FeaturedRosterSnapshot s_featured;

static bool fresh_pregame(const GameState &g)
{
    return g.valid && g.period == 0 && g.phase == 0 &&
           g.t[AWAY].score == 0 && g.t[HOME].score == 0 &&
           std::isfinite(g.period_len) && g.period_len > 0.0f && g.period_len < 4000.0f &&
           std::isfinite(g.clock) && std::fabs(g.clock - g.period_len) <= 0.1f;
}

extern "C" void nfl2k5_presentation_featured_players(uint32_t clock_object,
        const uint32_t rosters[2], const unsigned counts[2],
        const Nfl2k5FeaturedPlayer players[2])
{
    GameState g;
    if (!rosters || !counts || !players || !read_state(g) || !fresh_pregame(g) ||
        clock_object != rd32(0x00E6028Cu)) return;
    FeaturedRosterSnapshot out;
    out.clock_object = clock_object;
    for (int side = AWAY; side <= HOME; ++side) {
        const Nfl2k5FeaturedPlayer &p = players[side];
        uint8_t count = 0;
        out.abbr[side] = upper(team_info(g.t[side].abbr).abbr);
        if (!rosters[side] || rosters[side] != rd32(k_team_obj[side] + 0x1Cu) ||
            !gread(rosters[side] + 0x11Cu, &count, sizeof count) ||
            count != counts[side] || !count || count > 71 ||
            !memchr(p.full_name, 0, sizeof p.full_name) || !p.full_name[0] ||
            !memchr(p.position, 0, sizeof p.position) ||
            p.overall < 0 || p.overall > 100 || g.t[side].abbr.empty()) continue;
        out.roster[side] = rosters[side]; out.count[side] = count;
        out.player[side] = "{\"fullName\":" + json_str(p.full_name) +
            ",\"name\":" + json_str(p.full_name) + ",\"position\":" + json_str(p.position) +
            ",\"overall\":" + std::to_string(p.overall);
        if (p.number >= 0 && p.number <= 99)
            out.player[side] += ",\"number\":" + std::to_string(p.number);
        if (p.photo_id >= 0 && p.photo_id <= 65535) {
            std::string portrait = nfl2k5_portrait_url(p.photo_id);
            out.player[side] += ",\"photoId\":" + std::to_string(p.photo_id);
            if (!portrait.empty()) out.player[side] += ",\"portrait\":" + json_str(portrait);
        }
        out.player[side] += ",\"source\":\"native\"}";
    }
    std::lock_guard<std::mutex> lock(s_featured_lock);
    s_featured = std::move(out);
}

static std::string featured_players_json(const GameState &g, int side)
{
    if (!fresh_pregame(g)) return "[]";
    std::lock_guard<std::mutex> lock(s_featured_lock);
    if (!s_featured.clock_object || s_featured.clock_object != rd32(0x00E6028Cu) ||
        s_featured.player[side].empty()) return "[]";
    for (int s = AWAY; s <= HOME; ++s) {
        if (s_featured.abbr[s] != upper(team_info(g.t[s].abbr).abbr)) return "[]";
    }
    uint8_t count = 0;
    if (s_featured.roster[side] != rd32(k_team_obj[side] + 0x1Cu) ||
        !gread(s_featured.roster[side] + 0x11Cu, &count, sizeof count) ||
        count != s_featured.count[side]) return "[]";
    return "[" + s_featured.player[side] + "]";
}

static std::string html_state_json(const GameState &g, const HtmlContext &cx)
{
    std::string j = "{\"apiVersion\":1";
    j += ",\"valid\":" + std::string(g.valid ? "true" : "false");
    const char *status = !g.valid ? "inactive" : g.period == 0 ? "pregame" : is_final(g) ? "final" : "in_progress";
    j += ",\"gameStatus\":" + json_str(status);
    j += ",\"phase\":" + std::to_string(g.phase);
    j += ",\"quarter\":" + std::to_string(g.period);
    char num[64];
    snprintf(num, sizeof num, "%.2f", g.clock); j += ",\"gameClock\":" + std::string(num);
    snprintf(num, sizeof num, "%.0f", g.period_len); j += ",\"periodLength\":" + std::string(num);
    j += ",\"playClock\":" + std::to_string(g.play_clock);
    bool scrimmage = g.valid && g.phase == 4 && g.down >= 1 && g.down <= 4;
    j += ",\"down\":" + std::to_string(scrimmage ? g.down : 0);
    j += ",\"distance\":" + (scrimmage ? std::to_string((int)lroundf(fabsf(g.line - g.ball) / 91.44f)) : std::string("null"));
    j += ",\"goalToGo\":" + std::string(scrimmage && fabsf(g.line) >= 4572.0f - 1.0f ? "true" : "false");
    j += ",\"downDistanceText\":" + json_str(down_distance(g));
    /* Which half of the field the ball is in needs the drive direction,
     * which is not mapped yet: side and redZone stay null, not guessed. */
    int yard = std::isfinite(g.ball) ? (int)lroundf(std::max(0.0f, std::min(100.0f, g.ball / 91.44f))) : -1;
    j += ",\"ballPosition\":{\"yardLine\":" + (scrimmage && yard >= 0 ? std::to_string(std::min(yard, 100 - yard)) : std::string("null"));
    j += ",\"side\":null,\"text\":" + json_str(ball_position(g)) + "}";
    j += ",\"redZone\":null";
    j += ",\"possession\":" + (g.poss == 1 ? std::string("\"away\"") : g.poss == 2 ? std::string("\"home\"") : std::string("null"));
    j += eras_api_fields();   /* M9: league, season, decade, week */
    for (int side = 0; side < 2; side++) {
        TeamInfo t = team_info(g.t[side].abbr);
        j += ",\"" + std::string(side_name(side)) + "\":{";
        j += "\"abbreviation\":" + json_str(upper(g.t[side].abbr));
        j += ",\"city\":" + json_str(t.city) + ",\"name\":" + json_str(t.name);
        j += ",\"logo\":" + json_str(team_asset_url(t.logo));
        j += ",\"record\":" + json_str(t.record);
        j += ",\"score\":" + std::to_string(g.t[side].score);
        j += ",\"timeouts\":" + std::to_string(g.t[side].timeouts);
        j += ",\"primaryColor\":" + json_color(t.primary) + ",\"secondaryColor\":" + json_color(t.secondary);
        j += ",\"featuredPlayers\":" + featured_players_json(g, side);
        j += "}";
    }
    j += ",\"context\":{\"scorebugVisible\":" + std::string(cx.scorebug_visible ? "true" : "false");
    j += ",\"playSelection\":" + std::string(cx.play_selection ? "true" : "false");
    j += ",\"paused\":" + std::string(cx.paused ? "true" : "false");
    j += ",\"nativeScorebug\":" + std::string(cx.native_scorebug ? "true" : "false");
    const char *pregame_stage = !g.valid ? nullptr : g.period >= 1 && g.phase == 2 ? "kickoff" :
                                s_coin_toss_native_active.load() ? "coin_toss" : nullptr;
    j += ",\"pregameStage\":" + (pregame_stage ? json_str(pregame_stage) : std::string("null"));
    j += ",\"flag\":" + std::string(GetTickCount64() < s_flag_until ? "true" : "false") + "}";
    return j + "}";
}

static std::string html_event_json(const char *name, int side, const std::string &extra = "")
{
    std::string j = "{\"name\":" + json_str(name);
    j += ",\"team\":" + (side == AWAY || side == HOME ? json_str(side_name(side)) : std::string("null"));
    char t[32];
    snprintf(t, sizeof t, "%.3f", now_s());
    j += ",\"time\":" + std::string(t) + extra;
    return j + "}";
}

static std::string html_player_extra(const PlayerStat &p, const PlayerIdentity &id, int photo_id)
{
    std::string j = ",\"player\":{";
    j += "\"name\":" + json_str(p.name.empty() ? broadcast_player_name(id.name.c_str()) : p.name);
    j += ",\"fullName\":" + json_str(id.name);
    /* A native non-QB card has no live-QB identity record.  Preserve that
     * as empty so the package can choose its family from populated stats
     * instead of incorrectly labelling every player a quarterback. */
    j += ",\"position\":" + json_str(id.position);
    j += ",\"number\":" + (id.number >= 0 ? std::to_string(id.number) : std::string("null"));
    j += ",\"college\":" + json_str(id.college);
    j += ",\"portrait\":" + json_str(id.portrait);
    if (photo_id >= 0) j += ",\"photoId\":" + std::to_string(photo_id);
    j += ",\"completions\":" + std::to_string(p.completions);
    j += ",\"attempts\":" + std::to_string(p.attempts);
    j += ",\"passingYards\":" + std::to_string(p.passing_yards);
    j += ",\"passingTouchdowns\":" + std::to_string(p.passing_touchdowns);
    j += ",\"interceptions\":" + std::to_string(p.interceptions);
    /* Keep the complete stat payload here.  HTML packages decide which
     * family applies to a player; limiting this bridge to QB fields made an
     * SNF replacement impossible for ESPN's RB/WR/defense/kicker cards. */
    j += ",\"kind\":" + std::to_string(p.kind);
    j += ",\"carries\":" + std::to_string(p.carries);
    j += ",\"rushingYards\":" + std::to_string(p.rushing_yards);
    j += ",\"rushingTouchdowns\":" + std::to_string(p.rushing_touchdowns);
    j += ",\"receptions\":" + std::to_string(p.receptions);
    j += ",\"receivingYards\":" + std::to_string(p.receiving_yards);
    j += ",\"receivingTouchdowns\":" + std::to_string(p.receiving_touchdowns);
    j += ",\"tackles\":" + std::to_string(p.tackles);
    j += ",\"sacks\":" + std::to_string(p.sacks);
    j += ",\"defensiveInterceptions\":" + std::to_string(p.defensive_interceptions);
    j += ",\"fieldGoalsMade\":" + std::to_string(p.field_goals_made);
    j += ",\"fieldGoalsAttempted\":" + std::to_string(p.field_goals_attempted);
    j += ",\"longestFieldGoal\":" + std::to_string(p.longest_field_goal) + "}";
    return j;
}

/* The JSON renderer's event names -> Presentation API names. Quarter
 * boundaries are derived from state instead (html_state_events). */
static const char *html_event_name(const std::string &n)
{
    static const std::pair<const char *, const char *> map[] = {
        { "touchdown", "TOUCHDOWN" }, { "field_goal", "FIELD_GOAL" }, { "extra_point", "EXTRA_POINT" },
        { "two_point", "TWO_POINT_CONVERSION" }, { "safety", "SAFETY" }, { "timeout", "TIMEOUT" },
        { "first_down", "FIRST_DOWN" }, { "two_minute_warning", "TWO_MINUTE_WARNING" },
        { "final", "GAME_ENDED" }, { "final_overtime", "GAME_ENDED" }, { "game_end", "GAME_ENDED" },
        { "penalty", "PENALTY" }, { "interception", "INTERCEPTION" }, { "fumble", "FUMBLE" },
        { "turnover", "TURNOVER" }, { "sack", "SACK" }, { "injury", "INJURY" },
        { "replay_begin", "REPLAY_STARTED" }, { "replay_end", "REPLAY_ENDED" },
        { "game_start", "GAME_STARTED" }, { "drive_start", "DRIVE_STARTED" }, { "player_stat", "PLAYER_STAT" },
        { "player_feature", "PLAYER_FEATURE" },
    };
    for (auto &m : map) if (n == m.first) return m.second;
    return nullptr;     /* end_q1/end_q3/halftime/quarter_start: from state */
}

struct HtmlPresentation {
    std::unique_ptr<nfl2k5::PresentationHost> host;
    int package = -1;
    float scale = 1.0f;
    int canvas_w = 0;                 /* page width in use (fill_width packages follow the picture) */
    std::string last_state;
    double last_state_t = 0;
    GameState prev;
    bool prev_valid = false, started_game = false;
};
static HtmlPresentation s_html;

static void html_post(const std::string &event_json)
{
    if (s_html.host) s_html.host->post_event(event_json);
    if (s_log) fprintf(stderr, "[HTMLPRES] event %s\n", event_json.c_str());
}

/* Drive tracking for "scoring drive" graphics: plays, yards and time of
 * possession of the current drive, from the live state. A drive starts with
 * a new possession and is measured from its first scrimmage snap; each new
 * down/spot is a play. The game's own Drive Summary (popup text) replaces
 * these numbers when it appears (DRIVE_SUMMARY). */
struct DriveTrack {
    int side = -1;                  /* AWAY / HOME */
    bool scrimmage = false;         /* first snap seen */
    float start_ball = 0, last_ball = 0, last_line = 0, start_clock = 0;
    int start_period = 0, last_down = 0, plays = 0;
};
static DriveTrack s_drive;

static int drive_seconds(const GameState &g)
{
    if (!s_drive.scrimmage) return 0;
    float len = g.period_len > 0 ? g.period_len : 900.0f;
    float used = g.period == s_drive.start_period ? s_drive.start_clock - g.clock
               : s_drive.start_clock + (g.period - s_drive.start_period - 1) * len + (len - g.clock);
    return std::max(0, (int)lroundf(used));
}

/* ,"drive":{...} for a scoring event by `side` (touchdown: to the goal line;
 * field goal: to the kick spot). */
static std::string drive_json(const GameState &g, int side, bool touchdown)
{
    if (s_drive.side != side || !s_drive.scrimmage) return "";
    float end = s_drive.last_ball;
    if (touchdown) end = s_drive.last_ball >= s_drive.start_ball ? 9144.0f : 0.0f;
    int yards = (int)lroundf(fabsf(end - s_drive.start_ball) / 91.44f);
    int plays = s_drive.plays + 1;                      /* the scoring play */
    return ",\"drive\":{\"plays\":" + std::to_string(plays) + ",\"yards\":" + std::to_string(yards) +
           ",\"timeOfPossession\":" + std::to_string(drive_seconds(g)) + ",\"source\":\"tracked\"}";
}

static void drive_update(const GameState &p, const GameState &g)
{
    if (!g.valid || !g.poss) return;
    int side = g.poss - 1;
    if (side != s_drive.side) {                         /* new possession: new drive */
        s_drive = DriveTrack();
        s_drive.side = side;
    }
    if (g.phase != 4) return;
    if (!s_drive.scrimmage) {
        s_drive.scrimmage = true;
        s_drive.start_ball = s_drive.last_ball = g.ball;
        s_drive.last_line = g.line; s_drive.last_down = g.down;
        s_drive.start_clock = g.clock; s_drive.start_period = g.period;
        return;
    }
    if (p.phase == 4 && (g.ball != s_drive.last_ball || g.down != s_drive.last_down || g.line != s_drive.last_line)) {
        s_drive.plays++;
        s_drive.last_ball = g.ball; s_drive.last_line = g.line; s_drive.last_down = g.down;
    }
}

/* Popup text from src/main.c. Drive Summary: labels and values in order,
 * e.g. "Drive Summary|Plays|4|Yards|49|Time|0:52". */
static std::mutex s_popup_lock;
struct PendingPopupEvent {
    std::string name, extra, message;
    bool drive = false;
    int side = -1;
    uint32_t clock_object = 0, roster[2] = { 0, 0 };
    std::string abbr[2];
};
static std::vector<PendingPopupEvent> s_popup_events;

enum { PREGAME_COIN = 0, PREGAME_FACT = 1 };
/* Sole enable point: leave false until the post-crowd camera cue and a
 * verified visual-only native hook are proved, retaining layout/update/choice callbacks.
 * Draft source must not draw SNF cards over still-visible native artwork. */
static constexpr bool k_native_pregame_draw_hook_ready = false;
struct NativePregameOwner {
    bool active = false, acknowledged = false;
    uint32_t token = 0, generation = 0, clock_object = 0, layout = 0, player = 0;
    uint32_t roster[2] = { 0, 0 }, rows = 0, columns = 0;
    int package = -1, slot = -1, side = -1;
    std::string abbr[2], title, message, full_name;
    std::vector<std::string> cells;
};
static std::mutex s_pregame_owner_lock;
static NativePregameOwner s_pregame_owner[2];
static std::atomic<uint32_t> s_pregame_token{ 0 }, s_pregame_host_generation{ 1 };
static std::atomic<int> s_pregame_host_package{ -1 }, s_pregame_page_permitted{ 0 };

extern "C" int nfl2k5_presentation_pregame_metadata_needed(void)
{
    if (!k_native_pregame_draw_hook_ready || !s_pregame_page_permitted.load()) return 0;
    int pk = s_sel_pkg.load();
    if (pk != s_pregame_host_package.load() || pk <= 0 || pk >= (int)s_pkgs.size() ||
        !s_pkgs[pk].html || !s_pkgs[pk].replaces_pregame_cards) return 0;
    GameState g;
    return read_state(g) && fresh_pregame(g) ? 1 : 0;
}

static void pregame_clear_owners()
{
    std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
    s_pregame_owner[PREGAME_COIN] = NativePregameOwner();
    s_pregame_owner[PREGAME_FACT] = NativePregameOwner();
}

static void pregame_replacement_reset()
{
    s_pregame_page_permitted = 0;
    s_pregame_host_package = -1;
    ++s_pregame_host_generation;
    pregame_clear_owners();
}

static uint32_t pregame_begin_owner(int kind, uint32_t ctx, int slot,
        const GameState &g, int side, const std::string &title, const std::string &message,
        uint32_t player = 0, const std::string &full_name = "",
        const std::vector<std::string> &cells = {})
{
    NativePregameOwner owner;
    owner.active = true; owner.slot = slot; owner.side = side;
    owner.title = title; owner.message = message; owner.player = player;
    owner.full_name = full_name; owner.cells = cells;
    owner.layout = rd32(ctx + 0x48u);
    if (kind == PREGAME_FACT) {
        owner.rows = rd32(ctx + 0x22Cu); owner.columns = rd32(ctx + 0x230u);
    }
    owner.clock_object = rd32(0x00E6028Cu);
    for (int s = AWAY; s <= HOME; ++s) {
        owner.roster[s] = rd32(k_team_obj[s] + 0x1Cu);
        owner.abbr[s] = upper(team_info(g.t[s].abbr).abbr);
    }
    owner.package = s_sel_pkg.load(); owner.generation = s_pregame_host_generation.load();
    int pk = owner.package;
    if (k_native_pregame_draw_hook_ready && side >= AWAY && side <= HOME && fresh_pregame(g) && owner.layout &&
        s_pregame_page_permitted.load() && s_pregame_host_package.load() == pk &&
        pk > 0 && pk < (int)s_pkgs.size() && s_pkgs[pk].html && s_pkgs[pk].replaces_pregame_cards) {
        do { owner.token = ++s_pregame_token; } while (!owner.token);
    }
    uint32_t token = owner.token;
    std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
    s_pregame_owner[PREGAME_COIN] = NativePregameOwner();
    s_pregame_owner[PREGAME_FACT] = NativePregameOwner();
    s_pregame_owner[kind] = std::move(owner);
    return token;
}

/* This callback runs on the page's host thread and never calls guest code.
 * Exact protocol, positive uint32 tokens and host generations prevent a
 * delayed acknowledgment from another page from hiding a native card. */
static void pregame_page_message(int package, uint32_t generation, const std::string &message)
{
    if (package != s_sel_pkg.load() || package != s_pregame_host_package.load() ||
        generation != s_pregame_host_generation.load()) return;
    const char *show = "SNF_PREGAME_SHOW:", *hide = "SNF_PREGAME_HIDE:";
    bool visible;
    size_t prefix;
    if (message.compare(0, strlen(show), show) == 0) { visible = true; prefix = strlen(show); }
    else if (message.compare(0, strlen(hide), hide) == 0) { visible = false; prefix = strlen(hide); }
    else return;
    if (message.size() <= prefix || message.size() - prefix > 10) return;
    uint64_t value = 0;
    for (size_t i = prefix; i < message.size(); ++i) {
        unsigned char ch = (unsigned char)message[i];
        if (ch < '0' || ch > '9') return;
        value = value * 10u + ch - '0';
    }
    if (!value || value > UINT32_MAX) return;
    std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
    for (auto &owner : s_pregame_owner)
        if (owner.active && owner.token == (uint32_t)value && owner.package == package &&
            owner.generation == generation)
            owner.acknowledged = visible && s_pregame_page_permitted.load();
}

static int coin_toss_team(const GameState &g, const std::string &message);

extern "C" void nfl2k5_presentation_coin_toss(const char *title, const char *message,
                                             float timeout, int slot, int what, uint32_t ctx)
{
    if (!title || upper(title) != "COIN TOSS" || (what != 0 && what != 1)) return;
    PendingPopupEvent ev;
    if (what == 1) {
        {
            std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
            auto &owner = s_pregame_owner[PREGAME_COIN];
            if (owner.active && (owner.title != title || owner.message != (message ? message : "") ||
                (owner.slot >= 0 && owner.slot != slot))) return;
            owner = NativePregameOwner();
        }
        if (!s_coin_toss_native_active.exchange(0)) return; /* native fade has two close calls */
        ev.name = "COIN_TOSS_HIDE";
        ev.extra = ",\"source\":\"native\",\"nativeVisible\":false,\"slot\":" + std::to_string(slot);
    } else {
        s_coin_toss_native_active = 1;
        s_coin_toss = 1;
        ev.name = "COIN_TOSS";
        ev.message = message ? message : "";
        /* FLT_MAX waits for a native choice; -1 lets the title derive its
         * layout duration after this hook. Neither is a fixed lifetime. */
        std::string duration = "null";
        if (std::isfinite(timeout) && timeout >= 0.0f && timeout < 1.0e30f) {
            char value[32];
            snprintf(value, sizeof value, "%.3f", timeout);
            duration = value;
        }
        ev.extra = ",\"title\":" + json_str(title) + ",\"text\":" + json_str(ev.message) +
                   ",\"duration\":" + duration + ",\"source\":\"native\",\"nativeVisible\":true" +
                   ",\"slot\":" + std::to_string(slot);
        GameState g;
        if (ctx && read_state(g) && g.valid) {
            uint32_t token = pregame_begin_owner(PREGAME_COIN, ctx, slot, g,
                    coin_toss_team(g, ev.message), title, ev.message);
            if (token) ev.extra += ",\"replacementToken\":" + std::to_string(token);
        }
    }
    if (!nfl2k5_presentation_pregame_metadata_needed()) return;
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back(std::move(ev));
}

/* A native coin-toss message names its team explicitly. Result-only text
 * ("It is heads.") supplies no team; possession does not identify it. */
static int coin_toss_team(const GameState &g, const std::string &message)
{
    int found = -1;
    for (int side = AWAY; side <= HOME; ++side) {
        const std::string name = team_info(g.t[side].abbr).name;
        if (!name.empty() && message.size() > name.size() &&
            message.compare(0, name.size(), name) == 0 && message[name.size()] == ' ') {
            if (found != -1) return -1;
            found = side;
        }
    }
    return found;
}
extern "C" void nfl2k5_presentation_popup_text(const char *title, const char *text)
{
    if (!title || !text) return;
    std::string t = upper(title);
    if (t.find("DRIVE SUMMARY") == std::string::npos) return;
    std::vector<std::string> tok;
    std::string cur;
    for (const char *c = text;; c++) {
        if (*c == '\x1f' || !*c) { if (!cur.empty()) tok.push_back(cur); cur.clear(); if (!*c) break; }
        else cur += *c;
    }
    auto value_after = [&](const char *label, bool time) -> std::string {
        for (size_t i = 0; i + 1 < tok.size(); i++)
            if (upper(tok[i]) == label)
                for (size_t k = i + 1; k < tok.size() && k < i + 6; k++) {
                    const std::string &v = tok[k];
                    bool ok = !v.empty();
                    for (char ch : v) if (!(isdigit((unsigned char)ch) || (time && ch == ':') || ch == '-')) ok = false;
                    if (ok) return v;
                }
        return "";
    };
    std::string plays = value_after("PLAYS", false), yards = value_after("YARDS", false), time = value_after("TIME", true);
    if (s_log) {
        std::string joined;
        for (auto &x : tok) joined += (joined.empty() ? "" : "|") + x;
        fprintf(stderr, "[PRES] drive summary popup: %s -> plays %s yards %s time %s\n",
                joined.c_str(), plays.c_str(), yards.c_str(), time.c_str());
    }
    if (plays.empty() && yards.empty() && time.empty()) return;
    int secs = 0;
    if (!time.empty()) {
        size_t colon = time.find(':');
        secs = colon == std::string::npos ? atoi(time.c_str()) : atoi(time.substr(0, colon).c_str()) * 60 + atoi(time.substr(colon + 1).c_str());
    }
    std::string j = ",\"drive\":{\"plays\":" + (plays.empty() ? std::string("null") : plays) +
                    ",\"yards\":" + (yards.empty() ? std::string("null") : yards) +
                    ",\"timeOfPossession\":" + std::to_string(secs) + ",\"source\":\"game\"}";
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back({ "DRIVE_SUMMARY", j, "", true });
}

/* The game's after-kickoff lineup ticker (sub_000FFBA0; gen patch
 * LINEUP_TICKER at its end, 2026-10-02). It shows the offense of the team
 * with the ball, a few plays later the other team's defense, and the same
 * after the change of possession. sp is its frame: 11 roster record
 * pointers at sp+0x1C, lineup position codes at sp+0x48 (text: the pointer
 * table at 0x4F2688); the names it formats are at 0xBA3274 + i*0x40. A
 * record keeps its college behind a field-relative pointer at +0x00
 * (target = field + i32 - 1; the college's name pointer likewise), the
 * portrait id at +0x06 and the jersey number in bits 3..9 of +0x20. */
static std::string gtext(uint32_t va, int max = 40)
{
    std::string s;
    uint8_t b[2];
    if (!gread(va, b, 2)) return s;
    bool wide = b[1] == 0;
    for (int i = 0; i < max; i++) {
        uint32_t c = 0;
        if (!gread(va + (wide ? i * 2 : i), &c, wide ? 2 : 1) || !c) break;
        if (c < 32 || c > 126) return "";
        s += (char)c;
    }
    /* A stray pointer lands in fill patterns ("DEADBEEF...", Peerless
     * Price's college, 2026-10-02) or runs on past any real name. */
    if (s.find("DEADBEEF") != std::string::npos || s.find("EADBEEFD") != std::string::npos || (int)s.size() >= max)
        return "";
    return s;
}

/* A roster pointer field: on disc field-relative (field + i32 - 1); the
 * running game may hold it fixed up. Each form is tried in turn. */
static uint32_t grel(uint32_t field, int form)
{
    int32_t v = (int32_t)rd32(field);
    if (!v) return 0;
    return form == 0 ? field + v - 1 : form == 1 ? field + v : (uint32_t)v;
}

static std::string grel_text(uint32_t field)
{
    for (int f = 0; f < 3; f++) {
        std::string s = gtext(grel(field, f));
        if (s.size() >= 2) return s;
    }
    return "";
}

static std::atomic<int> s_player_fact_native_active{ 0 };

extern "C" void nfl2k5_presentation_player_fact_hide(int slot)
{
    if (slot != 1 || !s_player_fact_native_active.exchange(0)) return;
    {
        std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
        s_pregame_owner[PREGAME_FACT] = NativePregameOwner();
    }
    PendingPopupEvent ev;
    ev.name = "PLAYER_FACT_HIDE";
    ev.extra = ",\"source\":\"native\",\"nativeVisible\":false,\"slot\":1";
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back(std::move(ev));
}

extern "C" void nfl2k5_presentation_player_fact(uint32_t ctx)
{
    if (!nfl2k5_presentation_pregame_metadata_needed()) return;
    GameState g;
    /* sub_001C16B0 identifies a single-player table with these exact fields.
     * The selected player at +0x14 owns its fact; no cached QB is substituted. */
    if (!ctx || !read_state(g) || !fresh_pregame(g) ||
        rd32(ctx) != 1 || rd32(ctx + 0x10u) != 1 || rd32(ctx + 0x48u) != 1 ||
        rd32(ctx + 0x228u) != 1) return;
    uint32_t player = rd32(ctx + 0x14u);
    if (!player) return;
    int side = -1;
    uint32_t rosters[2] = { 0, 0 };
    for (int s = AWAY; s <= HOME; ++s) {
        uint8_t count = 0;
        rosters[s] = rd32(k_team_obj[s] + 0x1Cu);
        if (!rosters[s] || !gread(rosters[s] + 0x11Cu, &count, sizeof count) ||
            !count || count > 71) return;
        for (unsigned i = 0; i < count; ++i)
            if (rd32(rosters[s] + i * 4u) == player) {
                if (side != -1) return; /* Ambiguous identity is unavailable. */
                side = s;
            }
    }
    if (side < AWAY || side > HOME || g.t[side].abbr.empty()) return;
    uint8_t position = 0xFF;
    uint16_t photo = 0;
    if (!gread(player + 0x35u, &position, sizeof position) || position > 16 ||
        !gread(player + 6u, &photo, sizeof photo)) return;
    std::string first = grel_text(player + 0x10u), last = grel_text(player + 0x14u);
    std::string full_name = first + (first.empty() ? "" : " ") + last;
    std::string pos = gtext(rd32(0x004F2688u + position * 4u), 8);
    int number = (int)((rd32(player + 0x20u) >> 3) & 0x7Fu);
    if (first.empty() || last.empty() || pos.empty() || number > 99) return;
    /* sub_0025F8A0 defines the dimensions; sub_0025F8B0 locates each native
     * UTF-16 cell at +0x23C+i*0x88. sub_001BEF40 initializes at most 24 cells.
     * Copy the actual formatted strings rather than calculating career totals. */
    uint32_t rows = rd32(ctx + 0x22Cu), columns = rd32(ctx + 0x230u);
    uint32_t cells = rd32(ctx + 0xEF4u);
    if (!rows || !columns || rows > 24 || columns > 24 || !cells || cells > 24 ||
        (uint64_t)rows * columns != cells) return;
    std::string caption, text;
    std::vector<std::string> native_cells;
    for (uint32_t i = 0; i < cells; ++i) {
        uint16_t native_cell[64];
        if (!gread(ctx + 0x23Cu + i * 0x88u, native_cell, sizeof native_cell)) return;
        if (!native_cell[0]) { native_cells.push_back(""); continue; }
        std::string cell = gtext(ctx + 0x23Cu + i * 0x88u, 64);
        if (cell.empty()) return;
        native_cells.push_back(cell);
        std::string folded = upper(cell);
        if (folded == "CAREER" || folded.compare(0, 7, "CAREER ") == 0) {
            if (!caption.empty()) return;
            caption = cell.substr(0, 6);
            if (cell.size() > 7) text += (text.empty() ? "" : " ") + cell.substr(7);
        } else if (cell != full_name) {
            if (text.size() + cell.size() + 1 > 1024) return;
            text += (text.empty() ? "" : " ") + cell;
        }
    }
    if (caption.empty() || text.empty()) return;
    float timeout = -1.0f;
    gread(ctx + 8u, &timeout, sizeof timeout);
    std::string duration = "null";
    if (std::isfinite(timeout) && timeout >= 0.0f && timeout < 1.0e30f) {
        char value[32]; snprintf(value, sizeof value, "%.3f", timeout); duration = value;
    }
    PendingPopupEvent ev;
    ev.name = "PLAYER_FACT"; ev.side = side;
    ev.clock_object = rd32(0x00E6028Cu);
    for (int s = AWAY; s <= HOME; ++s) {
        ev.roster[s] = rosters[s]; ev.abbr[s] = upper(team_info(g.t[s].abbr).abbr);
    }
    ev.extra = ",\"caption\":" + json_str(caption) + ",\"text\":" + json_str(text) +
        ",\"duration\":" + duration + ",\"source\":\"native\",\"nativeVisible\":true,\"slot\":1" +
        ",\"player\":{\"id\":" + std::to_string(player) + ",\"name\":" + json_str(full_name) +
        ",\"fullName\":" + json_str(full_name) + ",\"position\":" + json_str(pos) +
        ",\"number\":" + std::to_string(number) + ",\"photoId\":" + std::to_string(photo) +
        ",\"portrait\":" + json_str(nfl2k5_portrait_url(photo)) + "}";
    uint32_t token = pregame_begin_owner(PREGAME_FACT, ctx, 1, g, side,
            caption, text, player, full_name, native_cells);
    if (token) ev.extra += ",\"replacementToken\":" + std::to_string(token);
    s_player_fact_native_active = 1;
    std::lock_guard<std::mutex> lock(s_popup_lock);
    s_popup_events.push_back(std::move(ev));
}

extern "C" int nfl2k5_presentation_pregame_draw_replaced(uint32_t ctx)
{
    if (!k_native_pregame_draw_hook_ready) return 0;
    /* Only the two actual native slots can be queried. This read-only
     * function does not bypass layout readiness or native update callbacks. */
    int slot = ctx == 0x00B61B50u ? 0 : ctx == 0x00B62A70u ? 1 : -1;
    GameState g;
    if (slot < 0 || !s_pregame_page_permitted.load() || !read_state(g) || !fresh_pregame(g) ||
        rd32(ctx + 4u) != 1 || rd32(ctx + 0xCu) != 0) return 0;
    int pk = s_sel_pkg.load();
    uint32_t generation = s_pregame_host_generation.load();
    if (pk != s_pregame_host_package.load() || pk <= 0 || pk >= (int)s_pkgs.size() ||
        !s_pkgs[pk].html || !s_pkgs[pk].replaces_pregame_cards) return 0;
    std::lock_guard<std::mutex> lock(s_pregame_owner_lock);
    for (int kind = PREGAME_COIN; kind <= PREGAME_FACT; ++kind) {
        auto &owner = s_pregame_owner[kind];
        if (!owner.active || !owner.token || !owner.acknowledged || owner.package != pk ||
            owner.generation != generation || (owner.slot >= 0 && owner.slot != slot) ||
            !owner.layout || owner.layout != rd32(ctx + 0x48u) ||
            owner.clock_object != rd32(0x00E6028Cu)) continue;
        bool owned = owner.side >= AWAY && owner.side <= HOME;
        for (int s = AWAY; s <= HOME && owned; ++s)
            owned = owner.roster[s] == rd32(k_team_obj[s] + 0x1Cu) &&
                    owner.abbr[s] == upper(team_info(g.t[s].abbr).abbr);
        if (!owned) continue;
        if (kind == PREGAME_COIN) {
            if (gtext(ctx + 0x4Cu, 64) != owner.title ||
                gtext(ctx + 0xCCu, 128) != owner.message) continue;
        } else {
            if (slot != 1 || rd32(ctx + 0x10u) != 1 || rd32(ctx + 0x228u) != 1 ||
                rd32(ctx + 0x14u) != owner.player || rd32(ctx + 0x22Cu) != owner.rows ||
                rd32(ctx + 0x230u) != owner.columns || rd32(ctx + 0xEF4u) != owner.cells.size() ||
                grel_text(owner.player + 0x10u) + " " + grel_text(owner.player + 0x14u) != owner.full_name)
                continue;
            bool cells_match = true;
            for (size_t i = 0; i < owner.cells.size(); ++i)
                if (gtext(ctx + 0x23Cu + (uint32_t)i * 0x88u, 64) != owner.cells[i]) {
                    cells_match = false; break;
                }
            if (!cells_match) continue;
        }
        owner.slot = slot; /* Resolve auto-selected coin slot by exact payload. */
        return 1;
    }
    return 0;
}

extern "C" void nfl2k5_portraits_warm(void);
extern "C" void xbox_PresentRecord(int seconds);   /* nv2a_gpu_present.inc.c, video clips */
static std::mutex s_lineup_lock;
struct PendingLineup {
    bool offense = false;
    std::string players;
    PlayerIdentity quarterback;
};
static std::vector<PendingLineup> s_lineups;

/* gen patch LINEUP_NATIVE_HIDE: the game skips showing its lineup ticker
 * (the starters are still read and sent as LINEUP) while the selected HTML
 * package says it draws the lineup itself. */
extern "C" int nfl2k5_lineup_hide_native(void)
{
    int k = s_sel_pkg.load();
    fprintf(stderr, "[PRES] game shows its lineup ticker (package %d, html %d)\n", k, s_html.host ? 1 : 0);
    return k > 0 && k < (int)s_pkgs.size() && s_pkgs[k].html && s_pkgs[k].replaces_lineup && s_html.host ? 1 : 0;
}

/* While a package's lineup intro plays, the game's own player cards (popup
 * slot 1: rookie cards, career stats) would cover it; main.c's popup hook
 * asks this and closes them at once. */
static volatile DWORD s_lineup_until;
extern "C" int nfl2k5_lineup_active(void)
{
    DWORD until = s_lineup_until;
    return until && (int32_t)(until - GetTickCount()) > 0;
}

extern "C" void nfl2k5_lineup_ticker(uint32_t sp)
{
    std::string players;
    bool offense = false;
    PlayerIdentity quarterback;
    int n = 0;
    for (int i = 0; i < 11; i++) {
        uint32_t rec = rd32(sp + 0x1C + i * 4), code = rd32(sp + 0x48 + i * 4);
        if (!rec) continue;
        std::string pos = code < 64 ? gtext(rd32(0x4F2688 + code * 4), 8) : "";
        std::string name = gtext(0xBA3274 + i * 0x40, 32);
        std::string first = grel_text(rec + 0x10), last = grel_text(rec + 0x14);
        std::string college;
        for (int f = 0; f < 3 && college.size() < 2; f++)
            if (uint32_t c = grel(rec, f)) college = grel_text(c);
        uint16_t photo = 0;
        gread(rec + 6, &photo, 2);
        int number = (int)((rd32(rec + 0x20) >> 3) & 0x7F);
        if (name.empty()) name = first + (first.empty() ? "" : " ") + last;
        if (pos == "QB") {
            offense = true;
            quarterback.name = name;
            quarterback.position = pos;
            quarterback.college = college;
            quarterback.number = number;
            quarterback.portrait = nfl2k5_portrait_url(photo);
        }
        players += std::string(n++ ? "," : "") + "{\"position\":" + json_str(pos) + ",\"number\":" + std::to_string(number) +
                   ",\"name\":" + json_str(name) + ",\"first\":" + json_str(first) + ",\"last\":" + json_str(last) +
                   ",\"college\":" + json_str(college) + ",\"photoId\":" + std::to_string(photo) +
                   ",\"portrait\":" + json_str(nfl2k5_portrait_url(photo)) + "}";
    }
    fprintf(stderr, "[PRES] lineup %s: %s\n", offense ? "offense" : "defense", players.c_str());
    if (n) xbox_PresentRecord(36);        /* NFL2K5_REC=<prefix>: record the intro (11 x 2.6 s) */
    if (n && nfl2k5_lineup_hide_native())  /* the package plays it: keep the game's cards out of its way */
        s_lineup_until = GetTickCount() + (DWORD)n * 2600u + 1500u;
    if (!n) return;
    std::lock_guard<std::mutex> lock(s_lineup_lock);
    s_lineups.push_back({ offense, players, quarterback });
}

/* Generic events that follow from two consecutive states. */
static void html_state_events(const GameState &p, const GameState &g)
{
    drive_update(p, g);
    {
        /* LINEUP: offense = the team with the ball, defense = the other. */
        std::vector<PendingLineup> pending;
        { std::lock_guard<std::mutex> lock(s_lineup_lock); pending.swap(s_lineups); }
        for (auto &l : pending) {
            int side = g.poss ? (l.offense ? g.poss - 1 : 2 - g.poss) : -1;
            if (l.offense && side >= AWAY && side <= HOME && !l.quarterback.name.empty()) {
                std::lock_guard<std::mutex> ql(s_live_qb_lock);
                s_live_qb_identity[side] = l.quarterback;
            }
            html_post(html_event_json("LINEUP", side, std::string(",\"unit\":\"") + (l.offense ? "offense" : "defense") +
                                      "\",\"players\":[" + l.players + "]"));
        }
    }
    {
        std::vector<PendingPopupEvent> pending;
        { std::lock_guard<std::mutex> lock(s_popup_lock); pending.swap(s_popup_events); }
        for (auto &ev : pending) {
            if (ev.name == "PLAYER_FACT") {
                bool owned = fresh_pregame(g) && ev.clock_object == rd32(0x00E6028Cu);
                for (int s = AWAY; s <= HOME && owned; ++s)
                    owned = ev.roster[s] == rd32(k_team_obj[s] + 0x1Cu) &&
                            ev.abbr[s] == upper(team_info(g.t[s].abbr).abbr);
                if (!owned) continue;
            }
            int side = ev.drive ? (s_drive.side >= 0 ? s_drive.side : -1) :
                       ev.side >= AWAY && ev.side <= HOME ? ev.side :
                       g.valid ? coin_toss_team(g, ev.message) : -1;
            html_post(html_event_json(ev.name.c_str(), side, ev.extra));
        }
    }
    /* Teardown/kickoff invalidates stale ownership without inventing a
     * native close event. COIN_TOSS_HIDE comes only from the close hook. */
    if (!g.valid || (g.period >= 1 && g.phase != 0)) {
        s_coin_toss_native_active = 0;
        s_player_fact_native_active = 0;
        pregame_clear_owners();
        std::lock_guard<std::mutex> lock(s_featured_lock);
        s_featured = FeaturedRosterSnapshot();
    }
    if (!g.valid) return;
    if (!s_html.started_game && g.period >= 1) {
        s_html.started_game = true;
        html_post(html_event_json("GAME_STARTED", -1));
    }
    if (!s_html.prev_valid) return;
    for (int s = 0; s < 2; s++)
        if (g.t[s].score != p.t[s].score)
            html_post(html_event_json("SCORE_CHANGED", s, ",\"points\":" + std::to_string(g.t[s].score - p.t[s].score) +
                                      ",\"score\":" + std::to_string(g.t[s].score)));
    if (g.poss != p.poss && g.poss)
        html_post(html_event_json("POSSESSION_CHANGED", g.poss - 1));
    if (g.phase == 4 && (g.down != p.down || p.phase != 4) && g.down >= 1 && g.down <= 4)
        html_post(html_event_json("DOWN_CHANGED", g.poss ? g.poss - 1 : -1, ",\"down\":" + std::to_string(g.down)));
    if (g.period > p.period && p.period >= 1) {
        html_post(html_event_json("QUARTER_ENDED", -1, ",\"quarter\":" + std::to_string(p.period)));
        if (p.period == 2) html_post(html_event_json("HALFTIME", -1));
        if (g.period == 5) html_post(html_event_json("OVERTIME", -1));
        html_post(html_event_json("QUARTER_STARTED", -1, ",\"quarter\":" + std::to_string(g.period)));
    }
}

static void html_stop()
{
    pregame_replacement_reset();
    if (s_html.host) {
        s_html.host->stop();
        s_html.host.reset();
        fprintf(stderr, "[HTMLPRES] host stopped\n");
    }
    s_html.package = -1;
    s_html.last_state.clear();
}

/* Draws the active HTML package. The page's 1920x1080 canvas maps onto the
 * largest 16:9 rectangle inside the game picture (broadcast safe): it keeps
 * its proportions at any resolution and is not stretched across ultrawide. */
static int html_hud(const XboxHudFrame *f, XboxHudImage *img, const GameState &g, const HtmlContext &cx)
{
    int pk = s_sel_pkg;
    const Package &pkg = s_pkgs[pk];
    float gw = f->game_w, gh = f->game_h;
    int cw = pkg.canvas_w;
    if (pkg.fill_width && gh > 0)
        cw = std::max(pkg.canvas_w, std::min(pkg.canvas_w * 3, (int)lroundf(pkg.canvas_h * gw / gh)));
    float dh = std::min(gh, gw * pkg.canvas_h / cw), dw = dh * cw / pkg.canvas_h;
    /* Rasterise at about the size it is shown (1080p: 1, 1440p: 1.33,
     * 4K: 2). The window can still be settling when the host starts, so a
     * shown size that stays well off the current scale for a second
     * restarts the page at the right one. */
    float want_scale = std::max(1.0f, std::min(2.0f, dh / pkg.canvas_h));
    static double scale_off_since;
    if (s_html.package == pk && (fabsf(want_scale - s_html.scale) > 0.2f || abs(cw - s_html.canvas_w) > 8)) {
        if (!scale_off_since) scale_off_since = now_s();
        else if (now_s() - scale_off_since > 1.0) { html_stop(); scale_off_since = 0; }
    } else scale_off_since = 0;
    if (s_html.package != pk) {
        html_stop();
        nfl2k5::PresentationHostConfig c;
        c.package_dir = pkg.dir;
        c.entry = pkg.entry;
        c.root_dir = "mods/presentations";
        c.teams_dir = "mods/teams";
        c.eras_teams_dir = eras_teams_root();   /* M9: https://erateams.local/ ("" = not mapped) */
        c.portraits_dir = nfl2k5_portraits_dir();
        nfl2k5_portraits_warm();
        c.canvas_width = cw;
        s_html.canvas_w = cw;
        c.canvas_height = pkg.canvas_h;
        c.raster_scale = want_scale;
        s_html.scale = want_scale;
        if (pkg.replaces_pregame_cards) {
            uint32_t generation = s_pregame_host_generation.load();
            s_pregame_host_package = pk;
            c.on_page_message = [pk, generation](const std::string &message) {
                pregame_page_message(pk, generation, message);
            };
        }
        s_html.host = nfl2k5::create_webview2_presentation_host();
        s_html.host->start(c);
        s_html.package = pk;
        s_html.prev_valid = false;
        s_html.started_game = false;
        fprintf(stderr, "[HTMLPRES] package \"%s\" at raster scale %.2f\n", pkg.name.c_str(), c.raster_scale);
    }

    bool page_permitted = fresh_pregame(g) && !cx.play_selection && !cx.paused;
    s_pregame_page_permitted = page_permitted ? 1 : 0;
    if (!page_permitted) pregame_clear_owners();

    /* State: whenever it changes, and at least twice a second. */
    std::string st = html_state_json(g, cx);
    double t = now_s();
    if (st != s_html.last_state || t - s_html.last_state_t > 0.5) {
        s_html.host->post_state(st);
        s_html.last_state = st;
        s_html.last_state_t = t;
    }

    /* Test hook: NFL2K5_PRES_EVENT=TOUCHDOWN,FIRST_DOWN,... cycles them every
     * 5 s. Upper-case names go out as Presentation API events as written;
     * lower-case ones go through the normal event queue (touchdown, ...). */
    if (!s_test_event.empty() && g.valid) {
        static double next;
        static size_t idx;
        if (t >= next) {
            std::vector<std::string> names;
            size_t a = 0, b;
            while ((b = s_test_event.find(',', a)) != std::string::npos) { names.push_back(s_test_event.substr(a, b - a)); a = b + 1; }
            names.push_back(s_test_event.substr(a));
            const std::string &n = names[idx++ % names.size()];
            int team = g.poss ? g.poss - 1 : HOME;
            if (!n.empty() && isupper((unsigned char)n[0]))
                html_post(html_event_json(n.c_str(), team, ",\"quarter\":" + std::to_string(g.period) + ",\"test\":true"));
            else
                queue_event({ n, team, "" });
            next = t + 5.0;
        }
    }

    html_state_events(s_html.prev, g);
    s_html.prev = g;
    s_html.prev_valid = g.valid;

    /* Live QB samples are data sources, not presentation triggers.  FOX may
     * choose a persistent QB treatment in its own package.  HTML packages
     * receive player graphics only when the title launches the corresponding
     * ESPN card/popup and queues player_stat/player_feature below. */
    Event ev;
    while (pop_event(ev))
        if (const char *name = html_event_name(ev.name)) {
            std::string extra;
            if (ev.name == "touchdown") extra = drive_json(g, ev.team, true);
            else if (ev.name == "field_goal") extra = drive_json(g, ev.team, false);
            else if (ev.name == "player_stat" || ev.name == "player_feature") {
                extra = html_player_extra(ev.player, ev.player_identity, ev.photo_id);
                if (std::isfinite(ev.requested_duration) && ev.requested_duration > 0)
                    extra += ",\"displaySeconds\":" + std::to_string(ev.requested_duration);
            }
            html_post(html_event_json(name, ev.team, extra));
        }

    nfl2k5::PresentationSurface surf;
    nfl2k5::PresentationSharedSurface shared;
    static uint64_t shown;
    if (!xbox_hud_shared_failed && s_html.host->acquire_shared(shared)) {
        /* GPU to GPU: the presenter draws the browser's texture itself. */
        surf.width = shared.width; surf.height = shared.height;
        surf.stride = shared.width * 4; surf.serial = shared.serial;
        img->shared = shared.handle;
        img->pixels = shared.handle;          /* non-null: "there is an image" */
    } else {
        if (!s_html.host->acquire_surface(surf)) return 0;
        img->pixels = surf.pixels;
    }
    img->w = surf.width; img->h = surf.height; img->stride = surf.stride;
    img->x = (int)lroundf(f->game_x + (gw - dw) / 2);
    img->y = (int)lroundf(f->game_y + (gh - dh) / 2);
    img->dw = (int)lroundf(dw); img->dh = (int)lroundf(dh);
    img->changed = surf.serial != shown;
    shown = surf.serial;
    return 1;
}

/* Front-end card mode (My NFL Eras draft show, docs/eras/results/M11-draftshow.md): a page the
 * game starts on demand outside a match, with events posted from C. Its own host, separate
 * from the match package's; the match gate above is not involved. Any thread may call
 * begin/end/post; the host is driven from the HUD callback. */
static struct {
    std::mutex lock;
    bool want = false;
    std::string root, dir, entry;
    std::vector<std::string> events;
    unsigned gen = 0;
} s_fe;
static std::unique_ptr<nfl2k5::PresentationHost> s_fe_host;
static unsigned s_fe_host_gen;
static float s_fe_scale;

extern "C" void nfl2k5_presentation_frontend_begin(const char *root_dir, const char *package_dir, const char *entry)
{
    std::lock_guard<std::mutex> g(s_fe.lock);
    s_fe.want = true;
    s_fe.root = root_dir ? root_dir : "";
    s_fe.dir = package_dir ? package_dir : "";
    s_fe.entry = entry && *entry ? entry : "index.html";
    s_fe.events.clear();
    s_fe.gen++;
    fprintf(stderr, "[HTMLPRES] front-end page requested: %s/%s\n", s_fe.dir.c_str(), s_fe.entry.c_str());
}

extern "C" void nfl2k5_presentation_frontend_end(void)
{
    std::lock_guard<std::mutex> g(s_fe.lock);
    if (s_fe.want) fprintf(stderr, "[HTMLPRES] front-end page released\n");
    s_fe.want = false;
    s_fe.events.clear();
}

/* One Presentation API event object, e.g. {"name":"DRAFT_PICK","pick":1,...}. Queued until the
 * page's host exists, then delivered in order. */
extern "C" void nfl2k5_presentation_post_json(const char *json)
{
    if (!json || !*json) return;
    std::lock_guard<std::mutex> g(s_fe.lock);
    if (!s_fe.want) return;
    if (s_fe.events.size() < 256) s_fe.events.push_back(json);
    if (s_log) fprintf(stderr, "[HTMLPRES] front-end event %s\n", json);
}

static int frontend_hud(const XboxHudFrame *f, XboxHudImage *img)
{
    bool want;
    std::string root, dir, entry;
    std::vector<std::string> events;
    unsigned gen;
    {
        std::lock_guard<std::mutex> g(s_fe.lock);
        want = s_fe.want;
        gen = s_fe.gen;
        if (want) { root = s_fe.root; dir = s_fe.dir; entry = s_fe.entry; }
    }
    if (!want || (s_fe_host && s_fe_host_gen != gen)) {
        if (s_fe_host) {
            s_fe_host->stop();
            s_fe_host.reset();
            fprintf(stderr, "[HTMLPRES] front-end host stopped\n");
        }
        if (!want) return 0;
    }
    float gw = f->game_w, gh = f->game_h;
    float dh = std::min(gh, gw * 1080.0f / 1920.0f), dw = dh * 1920.0f / 1080.0f;
    if (!s_fe_host) {
        nfl2k5::PresentationHostConfig c;
        c.root_dir = root;
        c.package_dir = dir;
        c.entry = entry;
        c.eras_teams_dir = eras_teams_root();
        c.canvas_width = 1920;
        c.canvas_height = 1080;
        c.raster_scale = s_fe_scale = std::max(1.0f, std::min(2.0f, dh / 1080.0f));
        s_fe_host = nfl2k5::create_webview2_presentation_host();
        s_fe_host->start(c);
        s_fe_host_gen = gen;
        fprintf(stderr, "[HTMLPRES] front-end host started: %s/%s at raster scale %.2f\n", dir.c_str(), entry.c_str(), c.raster_scale);
    }
    {
        std::lock_guard<std::mutex> g(s_fe.lock);
        events.swap(s_fe.events);
    }
    for (auto &e : events) s_fe_host->post_event(e);
    nfl2k5::PresentationSurface surf;
    nfl2k5::PresentationSharedSurface shared;
    static uint64_t shown;
    if (!xbox_hud_shared_failed && s_fe_host->acquire_shared(shared)) {
        surf.width = shared.width; surf.height = shared.height;
        surf.stride = shared.width * 4; surf.serial = shared.serial;
        img->shared = shared.handle;
        img->pixels = shared.handle;
    } else {
        if (!s_fe_host->acquire_surface(surf)) return 0;
        img->pixels = surf.pixels;
    }
    img->w = surf.width; img->h = surf.height; img->stride = surf.stride;
    img->x = (int)lroundf(f->game_x + (gw - dw) / 2);
    img->y = (int)lroundf(f->game_y + (gh - dh) / 2);
    img->dw = (int)lroundf(dw); img->dh = (int)lroundf(dh);
    img->changed = surf.serial != shown;
    shown = surf.serial;
    return 1;
}

static int hud_callback(const XboxHudFrame *f, XboxHudImage *img)
{
    double t = now_s();
    float dt = (float)std::min(0.1, s_last_t ? t - s_last_t : 0.0);
    s_last_t = t;
    music_tick();
    if (frontend_hud(f, img)) return 1;

    GameState g;
    read_state(g);
    if (s_test) {
        g.valid = true; g.period = 1; g.clock = 791; g.period_len = 900; g.phase = 4; g.down = 1;
        g.ball = 0; g.line = 914.4f; g.poss = 2;
        g.t[0].score = 0; g.t[0].timeouts = 3; g.t[0].abbr = "KC";
        g.t[1].score = 0; g.t[1].timeouts = 3; g.t[1].abbr = "NE";
    }
    publish_broadcast_state(g);
    if (s_probe && g.valid && g.phase == 4) {
        static DWORD last_probe;
        DWORD now = GetTickCount();
        if (now - last_probe >= 1000) {
            last_probe = now;
            /* Candidate countdown/state globals are emitted as both raw
             * words and floats.  A verified producer must visibly decrease
             * on consecutive samples before FOX consumes it. */
            fprintf(stderr, "[PRES-PROBE] E60260..FC");
            for (uint32_t a = 0x00E60260u; a <= 0x00E602FCu; a += 4)
                fprintf(stderr, " %08X=%08X/%.3g", a, rd32(a), rdf(a));
            /* E6028C/E60290/E60294 are adjacent timer-like objects created
             * together.  The stock HUD uses E6028C+10 for the game clock;
             * inspect the same compact header in its siblings before binding
             * any of them as FOX's play clock. */
            for (uint32_t root : { 0x00E6028Cu, 0x00E60290u, 0x00E60294u }) {
                uint32_t object = rd32(root);
                fprintf(stderr, " [%08X->%08X", root, object);
                for (uint32_t off = 0; object && off <= 0x18; off += 4)
                    fprintf(stderr, " +%02X=%08X/%.3g", off,
                            rd32(object + off), rdf(object + off));
                fprintf(stderr, "]");
            }
            fprintf(stderr, "\n");
        }
    }
    /* Audio test switch: NFL2K5_AUDIO_SLIDERS=v0,v1,... overrides the game's
     * volume sliders (floats at E601B4.., TV broadcast first) without
     * touching the saved settings; NFL2K5_PRES_LOG prints their values. */
    {
        static DWORD last;
        if (GetTickCount() - last > 1000) {
            last = GetTickCount();
            if (const char *sl = getenv("NFL2K5_AUDIO_SLIDERS")) {
                const char *c = sl;
                for (uint32_t a = 0x00E601B4u; *c && a <= 0x00E601CCu; a += 4) {
                    char *end;
                    float f = strtof(c, &end);
                    if (end == c) break;
                    gwritef(a, f);
                    c = *end == ',' ? end + 1 : end;
                }
            }
            if (s_log) {
                fprintf(stderr, "[PRES] sliders E601B4..:");
                for (uint32_t a = 0x00E601B4u; a <= 0x00E601CCu; a += 4) fprintf(stderr, " %.2f", rdf(a));
                fprintf(stderr, "\n");
            }
        }
    }
    bool native_on = s_native_visible && GetTickCount() - s_native_tick < 300;
    /* NFL 2K5's play-call overlay has its own four-state controller.  The
     * state is written by sub_00071B50 and consumed every frame by
     * sub_000721D0; zero means the overlay is down, while 1..3 cover its
     * visible and transition states.  The stock ESPN bug is part of that
     * screen, but modern broadcast bugs should leave the play cards clear. */
    bool playcall_on = rd32(0x00B38C30u) != 0;
    /* navigation_pause_notitle's enter (sub_00278310) sets AC7444;
     * its leave (sub_00270600) clears it, and sub_00270BA0 exposes the
     * same value. Gamecast is a nested pause screen, so a retained phase 4
     * is not sufficient evidence of a live camera. */
    bool pause_on = rd32(0x00AC7444u) != 0;
    if (playcall_on) native_on = false;
    if (s_test) native_on = true;

    /* A valid GameState alone is insufficient: the title/frontend leaves
     * the previous game's clock, teams and phase in memory.  Require fresh
     * evidence from a moving game/play clock or an in-game controller. */
    static bool activity_prev_valid;
    static float activity_prev_clock;
    static int activity_prev_play_clock = -1;
    static std::string activity_prev_away, activity_prev_home;
    DWORD activity_now = GetTickCount();
    bool same_game = activity_prev_valid && g.valid &&
                     activity_prev_away == g.t[AWAY].abbr &&
                     activity_prev_home == g.t[HOME].abbr;
    bool clock_moved = same_game &&
        (fabsf(g.clock - activity_prev_clock) > 0.01f ||
         (g.play_clock >= 0 && activity_prev_play_clock >= 0 &&
          g.play_clock != activity_prev_play_clock));
    if (g.valid && (native_on || playcall_on || pause_on || clock_moved || s_test))
        s_match_tick = activity_now;
    if (!g.valid) s_match_tick = 0;
    activity_prev_valid = g.valid;
    activity_prev_clock = g.clock;
    activity_prev_play_clock = g.play_clock;
    activity_prev_away = g.t[AWAY].abbr;
    activity_prev_home = g.t[HOME].abbr;
    DWORD match_tick = s_match_tick;
    bool match_active = match_tick && activity_now - match_tick < 2500;

    commentary_tick(g, match_active);

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
            fprintf(stderr, "[PRES] q%d %.1f/%.0f phase %d down %d ball %.1f line %.1f poss %d  %s %d(%d) - %s %d(%d)  native %d playcall %u\n",
                    g.period, g.clock, g.period_len, g.phase, g.down, g.ball, g.line, g.poss,
                    g.t[0].abbr.c_str(), g.t[0].score, g.t[0].timeouts, g.t[1].abbr.c_str(), g.t[1].score, g.t[1].timeouts,
                    (int)native_on, rd32(0x00B38C30u));
        }
    }

    music_logic(g, native_on);

    if (g.valid && s_prev_valid && s_sel_anim && custom_active()) detect_events(s_prev, g);
    if (!g.valid || !s_prev_valid) clear_events();
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
    /* Presentation HTML belongs to an active match.  Starting its WebView in
     * the title/front-end path kept an overlay process alive before gameplay
     * and made Press Start contend with presentation startup. */
    if (match_active && custom_active() && s_pkgs[s_sel_pkg].html) {
        HtmlContext cx;
        cx.play_selection = playcall_on;
        cx.paused = pause_on;
        cx.native_scorebug = native_on;
        cx.scorebug_visible = match_active && g.valid && !playcall_on && !pause_on && (g.phase == 4 || native_on);
        return html_hud(f, img, g, cx);
    }
    if (s_html.host) html_stop();       /* switched back to a JSON package or ESPN */
    if (!custom_active() || !renderer_init()) return 0;
    const Package &pkg = s_pkgs[s_sel_pkg];
    const JVal *sb = pkg.root.get("scorebug");
    if (!sb) return 0;

    if (!s_test_event.empty() && !s_anim_on && queue_empty()) {
        static double next;
        if (t >= next) { queue_event({ s_test_event, HOME, "1ST QUARTER" }); next = t + 7.0; }
    }

    /* Explicit, interruptible HIDDEN -> FADE_IN -> LIVE -> FADE_OUT state.
     * A transition reverses from its current progress, so a sudden return to
     * live play never drops or flashes stale game information. */
    /* The stock ESPN scorebug signal is useful for cuts and replays, but it
     * is not raised consistently by NFL 2K5's live scrimmage cameras. FOX
     * therefore follows verified live game phase too, while the play-call
     * controller remains the authoritative hide signal. */
    bool fox_live = match_active && g.valid && !playcall_on && !pause_on && (g.phase == 4 || native_on);
    update_bug_lifecycle(sb, fox_live, dt);

    /* Next animation. */
    const JVal *anims = pkg.root.get("animations");
    if (s_anim_on && t - s_anim.t0 > s_anim.duration) s_anim_on = false;
    bool qb_live = fox_live && g.phase == 4 && g.period > 0 && g.clock > 0.0f &&
                   g.down >= 1 && g.down <= 4;
    service_live_qb(g, qb_live, anims);
    Event ev;
    while (!s_anim_on && pop_event(ev)) {
        const JVal *def = anims ? anims->get(ev.name.c_str()) : nullptr;
        if (!def && anims && (ev.name == "end_q1" || ev.name == "end_q3")) def = anims->get("end_of_quarter");
        if (!def && anims && (ev.name == "final" || ev.name == "final_overtime")) def = anims->get("game_end");
        if (!def) continue;
        s_anim.ev = ev; s_anim.def = def; s_anim.t0 = t;
        s_anim.duration = ev.requested_duration > 0 ? ev.requested_duration : def->num("duration", 3);
        s_anim_on = true;
        fprintf(stderr, "[PRES] animation %s (%s)\n", ev.name.c_str(), ev.team == AWAY ? "away" : "home");
    }
    bool fullscreen = s_anim_on && s_anim.def && s_anim.def->flag("fullscreen", false);
    if (s_show <= 0.0f && !fullscreen) return 0;

    /* Layout: the bug is canvas.width units across placement.width of the
     * game picture, centred on placement.center_*; the image adds room above
     * for timeouts and banners. */
    const JVal *cv = fullscreen ? s_anim.def->get("canvas") : sb->get("canvas");
    const JVal *pl = sb->get("placement");
    float cw = cv ? (float)cv->num("width", 1000) : 1000, ch = cv ? (float)cv->num("height", 56) : 56;
    float top = fullscreen ? 0.0f : cv ? (float)cv->num("overflow_top", 70) : 70;
    /* FOX's possession tab and its contextual insert can extend past the
     * scorebug's logo edges.  Reserve that transparent space in the HUD
     * image rather than clipping the broadcast graphic at canvas.width. */
    float left = fullscreen ? 0.0f : cv ? (float)cv->num("overflow_left", 0) : 0;
    float right = fullscreen ? 0.0f : cv ? (float)cv->num("overflow_right", 0) : 0;
    float u = fullscreen ? std::min(f->game_w / cw, f->game_h / ch)
                         : f->game_w * (pl ? (float)pl->num("width", 0.92) : 0.92f) / cw;
    float cx = f->game_x + f->game_w * (fullscreen ? 0.5f : pl ? (float)pl->num("center_x", 0.5) : 0.5f);
    float cy = f->game_y + f->game_h * (fullscreen ? 0.5f : pl ? (float)pl->num("center_y", 0.915) : 0.915f);
    const int pad = fullscreen ? 0 : 4;
    int W = fullscreen ? f->game_w : (int)ceilf((cw + left + right) * u) + pad * 2;
    int H = fullscreen ? f->game_h : (int)ceilf((ch + top) * u) + pad * 2;
    int X = fullscreen ? f->game_x : (int)floorf(cx - cw * u / 2 - left * u) - pad;
    int Y = fullscreen ? f->game_y : (int)floorf(cy - ch * u / 2 - top * u) - pad;
    if (W < 8 || H < 8 || W > 16384 || H > 4096) return 0;

    Ctx c;
    c.g = &g;
    c.pkg = &pkg;
    c.team[AWAY] = team_info(g.t[AWAY].abbr);
    c.team[HOME] = team_info(g.t[HOME].abbr);
    c.opacity = fullscreen ? 1.0f : s_show;

    /* Redraw only when something visible changed. */
    char sigbuf[160];
    snprintf(sigbuf, sizeof sigbuf, "%d|%d|%.3f|%s|%s|%s|%s|%d%d|%d%d|%d", W, H, s_show, var(c, "clock").c_str(),
             var(c, "down_distance").c_str(), var(c, "quarter").c_str(), (g.t[0].abbr + g.t[1].abbr).c_str(),
             g.t[0].score, g.t[1].score, g.t[0].timeouts, g.t[1].timeouts, g.poss * 10 + g.phase);
    std::string sig = sigbuf;
    /* A finished/cancelled card must clear its pixels even if the match
     * clock is stopped and none of the scorebug fields changed. */
    sig += s_anim_on ? "|animation" : "|noanimation";
    bool draw_live_qb = s_live_qb_visible && !fullscreen;
    sig += draw_live_qb ? "|qb:" + std::to_string(s_live_qb_display_revision) +
                         ":" + std::to_string(s_live_qb_event.team) : "|noqb";
    bool changed = s_anim_on || sig != s_last_sig || R.w != W || R.h != H;
    s_last_sig = sig;

    if (changed) {
        renderer_resize(W, H);
        if (!R.rt) return 0;
        R.rt->BeginDraw();
        R.rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        float life_y = 0, life_scale = 1;
        if (const JVal *life = sb->get("lifecycle")) {
            const JVal *def = life->get(s_bug_lifecycle == BUG_FADE_OUT ? "fade_out" : "fade_in");
            if (def) {
                life_y = (float)def->num("from_y", 0) * (1.0f - s_life_progress);
                float from_scale = (float)def->num("from_scale", 1);
                life_scale = from_scale + (1.0f - from_scale) * s_life_progress;
            }
        }
        D2D1_MATRIX_3X2_F base;
        if (fullscreen) {
            float ox = (W - cw * u) * 0.5f, oy = (H - ch * u) * 0.5f;
            base = D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation(ox, oy);
        } else {
            base = D2D1::Matrix3x2F::Scale(u, u) *
                D2D1::Matrix3x2F::Translation((float)pad + left * u, (float)pad + (top + life_y) * u);
        }
        if (!fullscreen && life_scale != 1.0f)
            base = D2D1::Matrix3x2F::Scale(life_scale, life_scale, D2D1::Point2F(cw * 0.5f, ch * 0.5f)) * base;
        R.rt->SetTransform(base);
        if (!fullscreen || !s_anim.def->flag("hide_scorebug", true))
            if (const JVal *els = sb->get("elements"))
                for (auto &e : els->a) draw_element(c, e, 1.0f, 0, 0, 1.0f);
        if (draw_live_qb) {
            Ctx qc = c;
            qc.ev = &s_live_qb_event;
            /* Share the package's unboxed text geometry and typography,
             * without its temporary-insert entrance/expiry keyframes. The
             * enclosing scorebug lifecycle still controls opacity. */
            const JVal *def = anims->get("player_stat");
            if (const JVal *layers = def->get("layers"))
                for (auto &L : layers->a) draw_element(qc, L, 1.0f, 0, 0, 1.0f);
        }
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
/* For the presenter (nv2a_gpu_present.inc.c): a match is on screen. */
extern "C" int (*xbox_present_in_match_fn)(void);
static int in_match(void)
{
    /* The play-call cards and pause screens are 4:3 panels: drawn at F = 1
     * they fill the picture instead of sitting in a middle strip. */
    GameState g;
    if (rd32(0x00B38C30u) != 0 || rd32(0x00AC7444u) != 0) return 0;
    return read_state(g) ? 1 : 0;
}

extern "C" void nfl2k5_presentation_init(void)
{
    xbox_present_in_match_fn = in_match;
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    load_teams();
    load_packages();
    load_settings();
    {
        HANDLE t = CreateThread(nullptr, 0, commentary_index_thread, nullptr, 0, nullptr);
        if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); }
    }
    s_test = getenv("NFL2K5_PRES_TEST") != nullptr;
    s_log = getenv("NFL2K5_PRES_LOG") != nullptr;
    s_probe = getenv("NFL2K5_PRES_PROBE") != nullptr;
    if (const char *ev = getenv("NFL2K5_PRES_EVENT")) s_test_event = ev;
    if (const char *ev = getenv("NFL2K5_COMMENTARY_TEST")) {
        s_commentary_test = upper(ev);
        s_sel_commentary = 1;
    }
    xbox_PresentSetHudCallback(hud_callback);
    xbox_AudioSetCaptureMix(capture_mix);
    fprintf(stderr, "[PRES] %zu presentations, selected \"%s\"\n", s_pkgs.size(), nfl2k5_pres_package_name(s_sel_pkg));
}

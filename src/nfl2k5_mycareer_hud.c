/* MyCareer HUD: a MyPlayer stat bar with a live grade, and a backdrop for the Supersim
 * screen, as our own layer over SOFTDRINK v0.9's MyCareer (branch mycareer-hud).
 *
 * Nothing of the pack is changed. Three interpreter hooks (the pack's code is always
 * interpreted), each checked against the pack's exact bytes at install:
 *
 *  - 0x014EA522, inside the pack's in-game overlay (0x014EA1D5). The pack reaches this point
 *    only when its own conditions all hold: a MyCareer game, live play (no play-call screen,
 *    no pause), MyPlayer on the field, its "MyPlayer stat line" setting On. There it has just
 *    built "F. Last QB: CMP/ATT .." and is about to draw it at the top right. We draw our bar
 *    instead and continue at the function's epilogue (0x014EA57C). ESI = MyPlayer's record.
 *  - 0x014E7BB6, the pack's Supersim text (called first by the same overlay). When the pack
 *    would draw (its flag 0x014F314C, the clock and both team objects) we draw a panel sized
 *    to the pack's own lines, then the pack draws its text on top, unchanged.
 *  - 0x014EA768, the pack's screen handler: finds the pack's MyCareer Settings list (the rows
 *    whose labels are the pack's own "MyPlayer stat line" strings) and gives it one more row,
 *    "Stat bar look", in a copy of the rows; 0x014E72E9 (the end of the pack's label refresh)
 *    keeps the copy's labels in step.
 *
 * Drawing is the game's own: its immediate-mode UI primitives (sub_0002D2A0 begin, 2CBE0
 * colour, 2CB90 uv, 2CA70 vertex, 2CA00 end, the path sub_0026DE30's rects take) and its text
 * (sub_0006BC30, fonts at 0xA90ECC). It is all guest NV2A work, so it is the same on D3D11,
 * D3D12 and Vulkan, on Windows, Linux and Android. Shapes get a one-unit alpha fringe so their
 * edges are smooth at any render scale.
 *
 * Stats: the title's current-match accessor sub_000CB240 (player record, selector, scope 0),
 * the one the pack's own line and Gamecast use (selectors: data/eras/stat_selectors_2k5.json).
 * OL: team plays and yards, sacks allowed = the opponent's sacks (sub_000CB2D0, team record).
 * The grade: docs/mycareer/HUD-GRADE.md.
 *
 * Setting: <root>\nfl2k5_mycareer_hud.ini, style = 1 (2K5) or 2 (modern); default 1.
 * NFL2K5_MC_HUD=0 turns all of it off; NFL2K5_MC_HUD_STYLE overrides; NFL2K5_MC_HUD_LOG=1;
 * NFL2K5_MC_HUD_DIAG=fonts draws the font table (tests); NFL2K5_MC_HUD_AX = x unit scale. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "interp/x86_interp.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern int nfl2k5_guest_player_stat_scope(uint32_t player, uint32_t selector, uint32_t scope, double *out);
extern int nfl2k5_guest_team_stat(uint32_t team, uint32_t selector, uint32_t scope, double *out);

#define HUD_FN_BASE   0xFED20000u
#define HUD_RET       0xFED2FFF0u
#define ROW_BYTES     0x34u
#define SND_SELECT    0x00E7FD70u
#define FONTS         0x00A90ECCu

/* SOFTDRINK v0.9 */
#define SD_MAGIC      0x014BC000u          /* "MCHOMEX1" */
#define SD_LINE_DRAW  0x014EA522u          /* lea edx,[esp+20h]: the stat line, ready to draw */
#define SD_LINE_EXIT  0x014EA57Cu          /* add esp,120h: the overlay's epilogue */
#define SD_SIM_DRAW   0x014E7BB6u          /* cmp [14F314Ch],0: the Supersim text */
#define SD_HANDLER    0x014EA768u
#define SD_REFRESH_END 0x014E72E9u         /* ret of the Settings label refresh (0x014E7245) */
#define SD_SIM_FLAG   0x014F314Cu
#define SD_LBL_LINE_ON  0x014EAF04u        /* L"MyPlayer stat line: On" */
#define SD_LBL_LINE_OFF 0x014EAF32u

/* ---- state ------------------------------------------------------------------- */

enum { G_QB, G_RB, G_WR, G_TE, G_DL, G_LB, G_DB, G_OL, G_K, G_P, GROUPS };

typedef struct {
    double v[12];
    int ok;
} Stats;

static struct {
    int on, log, style, diag;
    float ax;                      /* x units per square unit: 1 (the HUD space is shown square) */
    float left;                    /* the visible left edge in HUD units */
    uint32_t mem, used, size;
    uint32_t state, vtx, wbuf[32], simbuf, label, camsave;
    int wnext;
    char ini[300];
    /* the bar */
    uint32_t player;
    int group, side, jersey;
    char name[48], pos[8];
    Stats st;
    double tgt, shown;             /* grade score: target from the stats, shown eases to it */
    int letter, letter_prev;
    float pulse;                   /* seconds since the letter changed */
    DWORD last_read, last_draw;
    uint32_t sig;                  /* stats signature: a change = a new play */
    int plays;
    /* the Settings row */
    uint32_t set_desc, set_rows_old, set_rows_new;
    int set_n;
    DWORD set_scan;
} s;

#define HLOG(...) do { if (s.log) { fprintf(stderr, "[MCHUD] " __VA_ARGS__); fflush(stderr); } } while (0)

/* ---- guest helpers ------------------------------------------------------------- */

static uint32_t h_alloc(uint32_t n, uint32_t align)
{
    uint32_t p = (s.mem + s.used + align - 1u) & ~(align - 1u), i;
    if (p + n > s.mem + s.size) return 0;
    s.used = p + n - s.mem;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, HUD_RET);
    fn();
    return g_eax;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* A UTF-16 copy of s in the next scratch buffer (64 chars). */
static uint32_t wstr(const char *t)
{
    uint32_t p = s.wbuf[s.wnext++ & 31], i;
    for (i = 0; t[i] && i < 63; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)t[i];
    MEM16(p + 2u * i) = 0;
    return p;
}

static int readable(uint32_t a) { return a >= 0x10000u && a < 0x10000000u; }

static int guest_text(uint32_t a, char *out, size_t n)
{
    size_t i;
    int wide;
    out[0] = 0;
    if (!readable(a)) return 0;
    wide = MEM8(a + 1u) == 0;
    for (i = 0; i + 1 < n; i++) {
        unsigned c = wide ? MEM16(a + 2u * (uint32_t)i) : MEM8(a + (uint32_t)i);
        if (!c) break;
        if (c < 32 || c > 126) { out[0] = 0; return 0; }
        out[i] = (char)c;
    }
    out[i] = 0;
    return i != 0;
}

/* A record name field: a pointer, or (added rosters) relative to the field itself. */
static void record_text(uint32_t field, char *out, size_t n)
{
    uint32_t v = MEM32(field);
    if (guest_text(v, out, n)) return;
    if (guest_text(field + v - 1u, out, n)) return;
    guest_text(field + v, out, n);
}

/* ---- the game's immediate-mode UI drawing ------------------------------------- */

static void im_begin(uint32_t tex)
{
    uint32_t st = s.state;
    gcall(sub_000306A0, st, 0);
    MEM32(st + 0x30u) = tex;
    MEM32(st + 0x64u) = (MEM32(st + 0x64u) & ~8u) | 7u;
    MEM32(st + 0x70u) = (MEM32(st + 0x70u) & 0xFFFF00FFu) | 2u;
    MEM32(st + 0x74u) = 0x03030302u;
    MEM32(st + 0x78u) = (MEM32(st + 0x78u) & 0xFFFF8006u) | 0x8006u;
    MEM32(st + 0x60u) &= ~0x100000u & 0xFF5FFFFFu;
    PUSH32(g_esp, 0); PUSH32(g_esp, st); PUSH32(g_esp, 5u);   /* NV2A TRIANGLES */
    gcall(sub_0002D2A0, 0, 0);
}

static void im_v(float x, float y, uint32_t argb)
{
    gcall(sub_0002CBE0, argb, 0);
    MEMF(s.vtx) = x; MEMF(s.vtx + 4u) = y; MEMF(s.vtx + 8u) = 20.0f; MEMF(s.vtx + 12u) = 1.0f;
    gcall(sub_0002CA70, s.vtx, 0);
}

static void im_end(void) { gcall(sub_0002CA00, 0, 0); }

static void tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2)
{
    im_v(x0, y0, c0); im_v(x1, y1, c1); im_v(x2, y2, c2);
}

static uint32_t clear_a(uint32_t c) { return c & 0x00FFFFFFu; }
static uint32_t with_a(uint32_t c, unsigned a) { return (c & 0x00FFFFFFu) | (a << 24); }
static uint32_t lerpc(uint32_t a, uint32_t b, float t)
{
    uint32_t r = 0;
    int k;
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    for (k = 0; k < 32; k += 8) {
        float x = (float)((a >> k) & 255u), y = (float)((b >> k) & 255u);
        r |= (uint32_t)(x + (y - x) * t + 0.5f) << k;
    }
    return r;
}

/* A convex polygon (screen units), each vertex its own colour, with an outward alpha fringe
 * `feather` units wide: smooth edges at any render scale, soft shadows with a wide one. */
#define PMAX 64
static void poly(const float *px, const float *py, const uint32_t *pc, int n, float feather)
{
    float cx = 0, cy = 0, nx[PMAX], ny[PMAX];
    uint32_t cc;
    int i;
    if (n < 3 || n > PMAX) return;
    for (i = 0; i < n; i++) { cx += px[i]; cy += py[i]; }
    cx /= (float)n; cy /= (float)n;
    cc = pc[0];
    { float r = 0, g = 0, b = 0, a = 0;
      for (i = 0; i < n; i++) { a += (float)(pc[i] >> 24); r += (float)((pc[i] >> 16) & 255u); g += (float)((pc[i] >> 8) & 255u); b += (float)(pc[i] & 255u); }
      cc = ((uint32_t)(a / n + 0.5f) << 24) | ((uint32_t)(r / n + 0.5f) << 16) | ((uint32_t)(g / n + 0.5f) << 8) | (uint32_t)(b / n + 0.5f); }
    im_begin(0);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        tri(cx, cy, cc, px[i], py[i], pc[i], px[j], py[j], pc[j]);
    }
    if (feather > 0.0f) {
        for (i = 0; i < n; i++) {        /* outward edge normals, in square units (x / ax) */
            int j = (i + 1) % n;
            float dx = (px[j] - px[i]) / s.ax, dy = py[j] - py[i], l = sqrtf(dx * dx + dy * dy);
            float ex = l > 0 ? dy / l : 0, ey = l > 0 ? -dx / l : 0;
            float mx = (px[i] + px[j]) * 0.5f - cx, my = (py[i] + py[j]) * 0.5f - cy;
            if (ex * mx / s.ax + ey * my < 0) { ex = -ex; ey = -ey; }
            nx[i] = ex * feather * s.ax; ny[i] = ey * feather;
        }
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n, h = (i + n - 1) % n;
            tri(px[i], py[i], pc[i], px[j], py[j], pc[j], px[j] + nx[i], py[j] + ny[i], clear_a(pc[j]));
            tri(px[i], py[i], pc[i], px[j] + nx[i], py[j] + ny[i], clear_a(pc[j]), px[i] + nx[i], py[i] + ny[i], clear_a(pc[i]));
            tri(px[i], py[i], pc[i], px[i] + nx[h], py[i] + ny[h], clear_a(pc[i]), px[i] + nx[i], py[i] + ny[i], clear_a(pc[i]));
        }
    }
    im_end();
}

/* Rounded rectangle, x/y/w/h in screen units, radius in square units, vertical gradient. */
static void rrect(float x, float y, float w, float h, float r, uint32_t top, uint32_t bot, float feather)
{
    float px[PMAX], py[PMAX];
    uint32_t pc[PMAX];
    int n = 0, c, k, seg = r > 0.5f ? 6 : 0;
    static const float cxs[4] = { 1, 0, 0, 1 }, cys[4] = { 0, 0, 1, 1 };   /* TR, TL, BL, BR */
    if (r > h * 0.5f) r = h * 0.5f;
    for (c = 0; c < 4; c++) {
        float ccx = x + (cxs[c] ? w - r * s.ax : r * s.ax), ccy = y + (cys[c] ? h - r : r);
        for (k = 0; k <= seg; k++) {
            float a = (float)(c * 90 + (seg ? k * 90 / seg : 45)) * 3.14159265f / 180.0f;
            float vx = ccx + cosf(a) * r * s.ax, vy = ccy - sinf(a) * r;
            if (!seg) { vx = x + (cxs[c] ? w : 0); vy = y + (cys[c] ? h : 0); }
            px[n] = vx; py[n] = vy; pc[n] = lerpc(top, bot, (vy - y) / h); n++;
            if (!seg) break;
        }
    }
    poly(px, py, pc, n, feather);
}

static void rect(float x, float y, float w, float h, uint32_t c) { rrect(x, y, w, h, 0, c, c, 0); }

/* Hexagon, pointy top, centre and radius in square units (x scaled by ax). */
static void hexagon(float cx, float cy, float r, uint32_t top, uint32_t bot, float feather)
{
    float px[6], py[6];
    uint32_t pc[6];
    int k;
    for (k = 0; k < 6; k++) {
        float a = (90.0f + 60.0f * (float)k) * 3.14159265f / 180.0f;
        px[k] = cx + cosf(a) * r * s.ax; py[k] = cy - sinf(a) * r;
        pc[k] = lerpc(top, bot, (py[k] - (cy - r)) / (2.0f * r));
    }
    poly(px, py, pc, 6, feather);
}

/* A thick segment with round joints, for the grade letters (square units, scaled at x). */
static void seg_line(float x0, float y0, float x1, float y1, float hw, uint32_t c)
{
    float dx = (x1 - x0) / s.ax, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny, px[4], py[4];
    uint32_t pc[4] = { c, c, c, c };
    if (l <= 0.0f) return;
    nx = -dy / l * hw * s.ax; ny = dx / l * hw;
    px[0] = x0 + nx; py[0] = y0 + ny; px[1] = x1 + nx; py[1] = y1 + ny;
    px[2] = x1 - nx; py[2] = y1 - ny; px[3] = x0 - nx; py[3] = y0 - ny;
    poly(px, py, pc, 4, 0.7f);
}

static void dot(float x, float y, float r, uint32_t c)
{
    float px[12], py[12];
    uint32_t pc[12];
    int k;
    for (k = 0; k < 12; k++) {
        float a = (float)k * 30.0f * 3.14159265f / 180.0f;
        px[k] = x + cosf(a) * r * s.ax; py[k] = y + sinf(a) * r; pc[k] = c;
    }
    poly(px, py, pc, 12, 0.7f);
}

/* Glyphs in a 10 x 14 box, y down: polylines; a negative x ends a line. */
static void glyph_lines(char ch, float *pts, int *np)
{
    int n = 0, k;
#define P(a, b) (pts[2 * n] = (a), pts[2 * n + 1] = (b), n++)
#define BRK() P(-1, -1)
    switch (ch) {
    case 'A': P(0, 14); P(5, 0); P(10, 14); BRK(); P(2.3f, 9.2f); P(7.7f, 9.2f); break;
    case 'B': P(0, 14); P(0, 0); P(5.8f, 0); P(8.2f, 0.9f); P(9.0f, 3.2f); P(8.2f, 5.6f); P(5.8f, 6.6f); P(0, 6.6f); BRK();
              P(5.8f, 6.6f); P(8.8f, 7.6f); P(10, 10.2f); P(8.9f, 13.0f); P(6.2f, 14); P(0, 14); break;
    case 'C': for (k = 0; k <= 12; k++) { float a = (40.0f + 280.0f * (float)k / 12.0f) * 3.14159265f / 180.0f; P(5.2f + 5.0f * cosf(a), 7.0f - 7.0f * sinf(a)); } break;
    case 'D': P(0, 0); P(0, 14); BRK(); P(0, 0); P(3.6f, 0);
              for (k = 0; k <= 10; k++) { float a = (90.0f - 180.0f * (float)k / 10.0f) * 3.14159265f / 180.0f; P(3.6f + 6.2f * cosf(a), 7.0f - 7.0f * sinf(a)); }
              P(0, 14); break;
    case 'F': P(0, 14); P(0, 0); P(9.4f, 0); BRK(); P(0, 6.6f); P(7.4f, 6.6f); break;
    case '+': P(0, 4); P(6, 4); BRK(); P(3, 1); P(3, 7); break;
    case '-': P(0, 4); P(6, 4); break;
    }
#undef P
#undef BRK
    *np = n;
}

/* A grade ("A-") centred at cx, cy: letter `size` units tall, sign as a superscript. */
static void grade_text(const char *g, float cx, float cy, float size, uint32_t c)
{
    float pts[80], k = size / 14.0f, hw = size * 0.105f, lw = 10.0f * k * s.ax, sw = g[1] ? 6.0f * k * 0.8f * s.ax : 0;
    float x0 = cx - (lw + (sw ? sw + 2.6f * s.ax : 0)) * 0.5f, y0 = cy - size * 0.5f;
    int n, i, part;
    for (part = 0; part < 2 && g[part]; part++) {
        float sc = part ? k * 0.8f : k, ox = part ? x0 + lw + 2.6f * s.ax : x0, oy = part ? y0 - 1.0f : y0;
        glyph_lines(g[part], pts, &n);
        for (i = 0; i < n; i++) {
            if (pts[2 * i] < 0) continue;
            dot(ox + pts[2 * i] * sc * s.ax, oy + pts[2 * i + 1] * sc, part ? hw * 0.85f : hw, c);
            if (i + 1 < n && pts[2 * i + 2] >= 0)
                seg_line(ox + pts[2 * i] * sc * s.ax, oy + pts[2 * i + 1] * sc,
                         ox + pts[2 * i + 2] * sc * s.ax, oy + pts[2 * i + 3] * sc, part ? hw * 0.85f : hw, c);
        }
    }
}

/* ---- the game's text --------------------------------------------------------- */

static uint32_t font(int i) { return MEM32(FONTS + (uint32_t)i * 4u); }

static float text_w(int f, uint32_t w)
{
    uint32_t fo = font(f);
    if (!fo || !w) return 0;
    return (float)(int32_t)gcall(sub_00049410, fo, w);
}

/* align: 0 centre, -1 left edge at x, 1 right edge at x. Returns the width. */
static float text(const char *t, int f, float x, float y, uint32_t argb, int align)
{
    uint32_t fo = font(f), w = wstr(t);
    float tw;
    if (!fo) return 0;
    tw = text_w(f, w);
    if (align < 0) x += tw * 0.5f;
    else if (align > 0) x -= tw * 0.5f;
    PUSH32(g_esp, argb); PUSH32(g_esp, fo); PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    PUSH32(g_esp, 0x43700000u); PUSH32(g_esp, 0x41A00000u);
    PUSH32(g_esp, fbits(y)); PUSH32(g_esp, fbits(x));
    gcall(sub_0006BC30, w, 0);
    return tw;
}

/* ---- the setting ---------------------------------------------------------------- */

#ifdef _WIN32
#define HUD_SEP "\\"
#else
#define HUD_SEP "/"
#endif

/* [hud] style=1|2 in <root>/nfl2k5_mycareer_hud.ini: plain stdio, the same on every platform. */
static void ini_load(void)
{
    char root[260] = "";
    const char *e = getenv("NFL2K5_MC_HUD_STYLE"), *r = getenv("NFL2K5_ROOT");
    FILE *f;
    if (r && *r && strlen(r) < sizeof root) snprintf(root, sizeof root, "%s", r);
    else {
        GetModuleFileNameA(NULL, root, sizeof root);
        { char *b = strrchr(root, '\\'); if (!b) b = strrchr(root, '/'); if (b) *b = 0; }
    }
    snprintf(s.ini, sizeof s.ini, "%s" HUD_SEP "nfl2k5_mycareer_hud.ini", root);
    s.style = 1;
    f = fopen(s.ini, "r");
    if (f) {
        char line[128];
        int v;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, " style = %d", &v) == 1 || sscanf(line, " style=%d", &v) == 1) s.style = v;
        fclose(f);
    }
    if (e && *e) s.style = atoi(e);
    if (s.style != 2) s.style = 1;
    /* Measured in game: the HUD's 640 x 480 is shown square (a 40 x 40 rect is square) and
     * centred, and its viewport clips to x 0..640 also with the widescreen patch. */
    s.ax = 1.0f;
    s.left = 0.0f;
    { const char *a = getenv("NFL2K5_MC_HUD_AX"); if (a && *a) s.ax = (float)atof(a); }
    { const char *a = getenv("NFL2K5_MC_HUD_LEFT"); if (a && *a) s.left = (float)atof(a); }
    if (s.ax < 0.3f || s.ax > 2.0f) s.ax = 1.0f;
}

static void ini_save(void)
{
    FILE *f = fopen(s.ini, "w");
    if (!f) return;
    fprintf(f, "[hud]\n; stat bar and Supersim look: 1 = 2K5, 2 = modern\nstyle=%d\n", s.style);
    fclose(f);
}

static void label_refresh(void)
{
    char b[64];
    uint32_t i;
    snprintf(b, sizeof b, "Stat bar look: %s", s.style == 2 ? "Modern" : "2K5");
    for (i = 0; b[i]; i++) MEM16(s.label + 2u * i) = (uint16_t)(unsigned char)b[i];
    MEM16(s.label + 2u * i) = 0;
}

static void hud_fn(void)
{
    uint32_t ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp;
    g_esp += 4;
    esp = g_esp;
    s.style = s.style == 2 ? 1 : 2;
    ini_save();
    label_refresh();
    gcall(sub_0014D390, SND_SELECT, 0);
    HLOG("style -> %d\n", s.style);
    g_esp = esp;
    g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp;
}

recomp_func_t nfl2k5_mc_hud_lookup(uint32_t address)
{
    if (!s.on || address != HUD_FN_BASE) return NULL;
    return hud_fn;
}

/* The pack's Settings list: rows (0x34 bytes, label at +4, type 3 ends) holding the pack's
 * "MyPlayer stat line" label. Copied with our row before the end. */
static int rows_count(uint32_t rows, int *has_line)
{
    int n;
    *has_line = 0;
    for (n = 0; n < 24; n++) {
        uint32_t r = rows + (uint32_t)n * ROW_BYTES, t = MEM32(r);
        if (t == 3) return n;
        if (t > 16) return -1;
        if (MEM32(r + 4u) == SD_LBL_LINE_ON || MEM32(r + 4u) == SD_LBL_LINE_OFF) *has_line = 1;
    }
    return -1;
}

#define SD_SETTINGS   0x015051B0u          /* the pack's Settings list (FSM log: "Settings" over "Settings + Save") */
static void settings_find(void)
{
    uint32_t desc = SD_SETTINGS, rows = MEM32(desc + 0x10u), rn, i;
    int line, n;
    if (!readable(rows) || rows == s.set_rows_new) return;
    if (s.set_rows_new && rows == s.set_rows_old) {      /* the pack put its own rows back: ours again */
        for (n = 0; n < s.set_n; n++)
            MEM32(s.set_rows_new + ROW_BYTES * (uint32_t)n + 4u) = MEM32(rows + ROW_BYTES * (uint32_t)n + 4u);
        MEM32(desc + 0x10u) = s.set_rows_new;
        return;
    }
    if (s.set_rows_new) return;
    n = rows_count(rows, &line);
    if (n <= 0 || !line) {
        DWORD now = GetTickCount();
        if (now - s.set_scan > 5000u) { s.set_scan = now; HLOG("Settings rows at %08X not ready (n %d, line %d)\n", rows, n, line); }
        return;
    }
    rn = h_alloc(ROW_BYTES * (uint32_t)(n + 2), 4);
    if (!rn) return;
    for (i = 0; i < ROW_BYTES * (uint32_t)n; i += 4) MEM32(rn + i) = MEM32(rows + i);          /* the pack's rows */
    for (i = 0; i < ROW_BYTES; i += 4)                                                          /* its end row, one later */
        MEM32(rn + ROW_BYTES * (uint32_t)(n + 1) + i) = MEM32(rows + ROW_BYTES * (uint32_t)n + i);
    /* our row: a copy of the pack's last row (its look), our label and action */
    for (i = 0; i < ROW_BYTES; i += 4) MEM32(rn + ROW_BYTES * (uint32_t)n + i) = MEM32(rows + ROW_BYTES * (uint32_t)(n - 1) + i);
    MEM32(rn + ROW_BYTES * (uint32_t)n + 0x00u) = 9;
    MEM32(rn + ROW_BYTES * (uint32_t)n + 0x04u) = s.label;
    MEM32(rn + ROW_BYTES * (uint32_t)n + 0x08u) = 0;
    MEM32(rn + ROW_BYTES * (uint32_t)n + 0x28u) = HUD_FN_BASE;
    s.set_desc = desc; s.set_rows_old = rows; s.set_rows_new = rn; s.set_n = n;
    MEM32(desc + 0x10u) = rn;
    fprintf(stderr, "[MCHUD] Settings list: %d rows at %08X, our row added (copy at %08X)\n", n, rows, rn);
    for (n = 0; n <= s.set_n; n++) {
        char b[64];
        guest_text(MEM32(rn + ROW_BYTES * (uint32_t)n + 4u), b, sizeof b);
        HLOG("  row %d type %u '%s' act %08X\n", n, MEM32(rn + ROW_BYTES * (uint32_t)n), b, MEM32(rn + ROW_BYTES * (uint32_t)n + 0x28u));
    }
}

static uint32_t ih_handler(void)
{
    if (s.log) {   /* each new screen on top: its descriptor and first rows (finding the Settings list) */
        static uint32_t seen[32];
        static int nseen;
        uint32_t fsm = g_ecx, idx = MEM32(fsm + 0x100u), top, rows, k;
        int i, known = 0;
        if (idx <= 0x1Fu && readable(fsm)) {
            top = MEM32(fsm + idx * 8u);
            for (i = 0; i < nseen; i++) known |= seen[i] == top;
            if (!known && nseen < 32 && readable(top)) {
                seen[nseen++] = top;
                rows = MEM32(top + 0x10u);
                HLOG("screen %08X: words %08X %08X %08X %08X %08X rows %08X\n", top, MEM32(top), MEM32(top + 4u),
                     MEM32(top + 8u), MEM32(top + 0xCu), MEM32(top + 0x28u), rows);
                for (k = 0; readable(rows) && k < 7; k++) {
                    char b[48];
                    guest_text(MEM32(rows + k * ROW_BYTES + 4u), b, sizeof b);
                    HLOG("  row %u: type %u label %08X '%s' link %08X act %08X\n", k, MEM32(rows + k * ROW_BYTES),
                         MEM32(rows + k * ROW_BYTES + 4u), b, MEM32(rows + k * ROW_BYTES + 8u), MEM32(rows + k * ROW_BYTES + 0x28u));
                    if (MEM32(rows + k * ROW_BYTES) == 3) break;
                }
            }
        }
    }
    settings_find();
    return 0;
}

static uint32_t ih_refresh_end(void)
{
    int k;
    settings_find();                       /* the labels are set now: the list can be found before it shows */
    if (s.set_rows_new)
        for (k = 0; k < s.set_n; k++)
            MEM32(s.set_rows_new + ROW_BYTES * (uint32_t)k + 4u) = MEM32(s.set_rows_old + ROW_BYTES * (uint32_t)k + 4u);
    return 0;
}

/* ---- stats and the grade --------------------------------------------------------- */

static int group_of(unsigned p)
{
    switch (p) {
    case 0: return G_QB;
    case 1: return G_K;
    case 2: return G_P;
    case 3: return G_WR;
    case 7: case 8: return G_RB;
    case 9: return G_TE;
    case 4: case 5: case 6: return G_DB;
    case 10: case 11: return G_LB;
    case 12: case 13: case 14: return G_OL;
    default: return G_DL;
    }
}

static const char *const k_pos[17] = { "QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "LB", "LB", "C", "G", "T", "DT", "DE" };

/* selectors per group (player accessor unless the group is OL) */
static const uint8_t k_sel[GROUPS][10] = {
    /* QB  */ { 0x04, 0x23, 0x4C, 0x40, 0x16, 0x03, 0x50, 0x3F, 0x30, 0 },   /* 0x30: sacks taken (getter 0x24E470, by the passing getters) */
    /* RB  */ { 0x03, 0x50, 0x3F, 0x2E, 0x4F, 0x3E, 0x0F, 0 },
    /* WR  */ { 0x2E, 0x4F, 0x3E, 0x1B, 0x0F, 0 },
    /* TE  */ { 0x2E, 0x4F, 0x3E, 0x1B, 0x0F, 0 },
    /* DL  */ { 0x32, 0xB8, 0x17, 0x22, 0x11, 0x34, 0 },
    /* LB  */ { 0x32, 0xB8, 0x17, 0x22, 0x11, 0x34, 0 },
    /* DB  */ { 0x32, 0xB8, 0x17, 0x22, 0x11, 0x34, 0 },
    /* OL  */ { 0x03, 0x23, 0x50, 0x4C, 0 },                  /* team; + opponent 0xB8 */
    /* K   */ { 0xC8, 0xC7, 0x58, 0x59, 0x1C, 0 },
    /* P   */ { 0x2B, 0x4E, 0x2D, 0 },
};

static uint32_t team_record(int side) { return MEM32((side ? 0x00E5FC20u : 0x00E5FC60u) + 0x1Cu); }

static int find_side(uint32_t player)
{
    int side, i;
    for (side = 0; side < 2; side++) {
        uint32_t t = team_record(side);
        unsigned cnt;
        if (!readable(t)) continue;
        cnt = MEM8(t + 0x11Cu);
        for (i = 0; i < (int)cnt && i < 71; i++) if (MEM32(t + 4u * (uint32_t)i) == player) return side;
    }
    return -1;
}

static float game_fraction(void)
{
    uint32_t q = MEM32(0x00E602C4u), clk = MEM32(0x00E6028Cu);
    float len = MEMF(0x00E602B0u), left = readable(clk) ? MEMF(clk + 0x10u) : 0.0f, f;
    if (!(len > 1.0f && len < 4000.0f) || q < 1 || q > 12) return 0.05f;
    if (!(left >= 0.0f && left <= len)) left = len;
    f = ((float)(q > 4 ? 4 : q) - 1.0f + (q > 4 ? 1.0f : (len - left) / len)) / 4.0f;
    return f < 0.05f ? 0.05f : f > 1.0f ? 1.0f : f;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The grade score (0..100, C = 66): docs/mycareer/HUD-GRADE.md. */
static double grade_target(void)
{
    const double *v = s.st.v, prior = 66.0;
    double e = prior, n = 0, k = 8, f = game_fraction();
    switch (s.group) {
    case G_QB: {
        double att = v[1], car = v[5], plays = att + car + v[8];
        double a = plays > 0 ? (v[2] + 20 * v[3] - 45 * v[4] + 0.8 * v[6] + 20 * v[7]) / plays : 0;
        double pct = att > 0 ? v[0] / att : 0.6;
        e = 40 + 6.5 * a + 30 * (pct - 0.6);
        n = plays; k = 10;
        break; }
    case G_RB: {
        double t = v[0] + v[3];
        double a = t > 0 ? (v[1] + v[4] + 20 * (v[2] + v[5]) - 30 * v[6]) / t : 0;
        e = 40 + 7 * a; n = t; k = 8;
        break; }
    case G_WR: case G_TE: {
        double p = v[1] + 20 * v[2] + 4 * v[0] - 30 * v[4], exp = (s.group == G_WR ? 70.0 : 45.0) * f;
        e = 66 + 25 * (p / exp - 1); n = 2 * v[0] + 6 * f; k = 8;
        break; }
    case G_DL: case G_LB: case G_DB: {
        double p = v[0] + 4 * v[1] + 6 * v[2] + 2 * v[3] + 4 * v[4] + 1.5 * v[5];
        double exp = (s.group == G_DL ? 4.0 : s.group == G_LB ? 7.0 : 5.5) * f;
        e = 66 + 25 * (p / exp - 1); n = v[0] + 2 * (v[1] + v[2] + v[3] + v[4]) + 6 * f; k = 6;
        break; }
    case G_OL: {
        double plays = v[0] + v[1] + v[4], ypp = plays > 0 ? (v[2] + v[3]) / plays : 0;
        double sr = plays > 0 ? v[4] / plays : 0;
        e = 40 + 5.5 * ypp - 150 * sr; n = plays / 3; k = 8;
        break; }
    case G_K: {
        double fga = v[1], xpa = v[3];
        double pct = (v[0] + 0.5 * v[2]) / (fga + 0.5 * xpa > 0 ? fga + 0.5 * xpa : 1);
        e = 30 + 62 * pct + (v[4] >= 50 ? 6 : v[4] >= 40 ? 3 : 0); n = fga + 0.5 * xpa; k = 3;
        break; }
    case G_P: {
        double avg = v[0] > 0 ? v[1] / v[0] : 0;
        e = 66 + 2.5 * (avg - 41) + 4 * (v[0] > 0 ? v[2] / v[0] * 3 : 0); n = v[0]; k = 3;
        break; }
    }
    e = clampd(e, 20, 100);
    return (prior * k + e * n) / (k + n);
}

static const char *const k_letters[13] = { "F", "D-", "D", "D+", "C-", "C", "C+", "B-", "B", "B+", "A-", "A", "A+" };
static int letter_of(double g)
{
    static const double cut[12] = { 48, 52, 56, 60, 64, 70, 74, 78, 82, 86, 90, 96 };
    int i = 0;
    while (i < 12 && g >= cut[i]) i++;
    return i;
}

static void read_stats(uint32_t player)
{
    const uint8_t *sel;
    int i;
    Stats st;
    uint32_t sig = 2166136261u;
    memset(&st, 0, sizeof st);
    if (player != s.player) {
        unsigned p = MEM8(player + 0x35u), jw = MEM32(player + 0x20u);
        char first[32], last[40];
        s.player = player;
        s.group = group_of(p > 16 ? 16 : p);
        snprintf(s.pos, sizeof s.pos, "%s", k_pos[p > 16 ? 16 : p]);
        s.jersey = ((jw >> 3) & 0x7Fu) <= 99u ? (int)((jw >> 3) & 0x7Fu) : -1;
        record_text(player + 0x10u, first, sizeof first);
        record_text(player + 0x14u, last, sizeof last);
        snprintf(s.name, sizeof s.name, "%c. %s", first[0] ? first[0] : ' ', last);
        for (i = 0; s.name[i]; i++) s.name[i] = (char)toupper((unsigned char)s.name[i]);
        s.side = find_side(player);
        s.tgt = s.shown = 66.0; s.letter = s.letter_prev = letter_of(66.0); s.sig = 0; s.plays = 0;
        HLOG("MyPlayer %s #%d %s group %d side %d\n", s.name, s.jersey, s.pos, s.group, s.side);
    }
    sel = k_sel[s.group];
    st.ok = 1;
    for (i = 0; sel[i] && i < 10; i++) {
        double v = 0;
        int ok = s.group == G_OL ? (s.side >= 0 && nfl2k5_guest_team_stat(team_record(s.side), sel[i], 0, &v))
                                 : nfl2k5_guest_player_stat_scope(player, sel[i], 0, &v);
        if (!ok) { st.ok = 0; v = 0; }
        st.v[i] = v;
    }
    if (s.group == G_OL && s.side >= 0) {      /* sacks allowed: the opponent's sacks */
        double v = 0;
        if (nfl2k5_guest_team_stat(team_record(!s.side), 0xB8, 0, &v)) st.v[4] = v; else st.ok = 0;
    }
    for (i = 0; i < 12; i++) { uint32_t q = (uint32_t)(int)(st.v[i] * 2.0); sig = (sig ^ q) * 16777619u; }
    s.st = st;
    if (sig != s.sig) {                         /* a play changed the line: regrade */
        int was = s.letter;
        if (s.sig) s.plays++;
        s.sig = sig;
        s.tgt = grade_target();
        s.letter = letter_of(s.tgt);
        if (s.letter != was) { s.letter_prev = was; s.pulse = 0.0f; }
        HLOG("stats %s: %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f -> grade %.1f %s (f %.2f)\n", s.pos, st.v[0], st.v[1], st.v[2],
             st.v[3], st.v[4], st.v[5], st.v[6], st.v[7], st.v[8], s.tgt, k_letters[s.letter], game_fraction());
    }
}

/* The cells: label, value text. */
static int boxes(char lab[4][8], char val[4][12])
{
    const double *v = s.st.v;
#define B(i, l, ...) (snprintf(lab[i], 8, "%s", l), snprintf(val[i], 12, __VA_ARGS__))
    switch (s.group) {
    case G_QB: B(0, "CMP/ATT", "%.0f/%.0f", v[0], v[1]); B(1, "YDS", "%.0f", v[2]); B(2, "TD", "%.0f", v[3]); B(3, "INT", "%.0f", v[4]); return 4;
    case G_RB: B(0, "CAR", "%.0f", v[0]); B(1, "YDS", "%.0f", v[1]); B(2, "TD", "%.0f", v[2] + v[5]); B(3, "REC", "%.0f", v[3]); return 4;
    case G_WR: case G_TE: B(0, "REC", "%.0f", v[0]); B(1, "YDS", "%.0f", v[1]); B(2, "TD", "%.0f", v[2]); B(3, "LNG", "%.0f", v[3]); return 4;
    case G_DL: case G_LB: case G_DB:
        B(0, "TKL", "%.0f", v[0]); B(1, "SCK", v[1] == floor(v[1]) ? "%.0f" : "%.1f", v[1]); B(2, "INT", "%.0f", v[2]); B(3, "PD", "%.0f", v[3]); return 4;
    case G_OL: B(0, "PLAYS", "%.0f", v[0] + v[1] + v[4]); B(1, "TM YDS", "%.0f", v[2] + v[3]); B(2, "SCK ALW", "%.0f", v[4]); return 3;
    case G_K: B(0, "FG", "%.0f/%.0f", v[0], v[1]); B(1, "LNG", "%.0f", v[4]); B(2, "XP", "%.0f/%.0f", v[2], v[3]); return 3;
    case G_P: B(0, "PUNTS", "%.0f", v[0]); B(1, "AVG", "%.1f", v[0] > 0 ? v[1] / v[0] : 0.0); B(2, "IN20", "%.0f", v[2]); return 3;
    }
#undef B
    return 0;
}

static uint32_t grade_colour(int l)
{
    if (l >= 10) return 0xFF2FD27Eu;           /* A: green */
    if (l >= 7) return 0xFF39A8FFu;            /* B: blue */
    if (l >= 4) return 0xFFFFC233u;            /* C: amber */
    if (l >= 1) return 0xFFFF8A33u;            /* D: orange */
    return 0xFFFF4848u;                        /* F: red */
}

/* Fonts (measured in game, HUD units; y passed = baseline): F0 regular 12.5 tall, F2 condensed
 * bold 16.5, F3 small 11.5. The HUD's 640 x 480 is shown 4:3-square (one unit is as wide as it
 * is tall) and centred; at 16:9 its visible x runs from about -80 to 720, y to about 448. */
#define F_LABEL 3
#define F_VALUE 2
#define F_NAME  2

/* Cell widths: the wider of label and value, plus padding. */
static float cells(int nb, char lab[4][8], char val[4][12], float pad, float cw[4])
{
    float sum = 0;
    int i;
    for (i = 0; i < nb; i++) {
        float a = text_w(F_LABEL, wstr(lab[i])), b = text_w(F_VALUE, wstr(val[i]));
        cw[i] = (a > b ? a : b) + pad;
        if (cw[i] < 34.0f) cw[i] = 34.0f;
        sum += cw[i];
    }
    return sum;
}

/* ---- style 2: modern ------------------------------------------------------------ */

static void draw_modern(void)
{
    char lab[4][8], val[4][12], b[64];
    float cw[4];
    int nb = boxes(lab, val), i;
    float R = 34.0f, H = 64.0f, Bot = 372.0f, T = Bot - H;
    float hx = s.left + 12.0f + R * 0.866f, hy = T + H * 0.5f;
    float x0 = hx + R * 0.866f + 12.0f;                       /* content */
    float CW = cells(nb, lab, val, 16.0f, cw), nw, W;
    uint32_t gc = grade_colour(s.letter);
    float pulse = s.pulse < 0.7f ? 1.0f - s.pulse / 0.7f : 0.0f;
    nw = text_w(F_NAME, wstr(s.name)) + 10.0f + text_w(F_LABEL, wstr(s.pos)) + 14.0f + 40.0f;
    W = (CW > nw ? CW : nw) + (x0 - hx) + 10.0f;
    /* shadow, panel, top hairline */
    rrect(hx - 2.0f, T + 4.0f, W + 4.0f, H, 9.0f, 0x58000000u, 0x58000000u, 8.0f);
    rrect(hx, T, W, H, 8.0f, 0xF21D2534u, 0xF20A0E16u, 0.8f);
    rrect(x0 - 4.0f, T, W - (x0 - hx) - 6.0f, 1.4f, 0, 0x80FFFFFFu, 0x10FFFFFFu, 0.4f);
    /* name, position chip, number */
    {
        float tw = text(s.name, F_NAME, x0, T + 20.0f, 0xFFFFFFFFu, -1);
        float cx = x0 + tw + 8.0f, pw = text_w(F_LABEL, wstr(s.pos)) + 12.0f;
        rrect(cx, T + 6.5f, pw, 15.0f, 4.0f, lerpc(gc, 0xFFFFFFFFu, 0.1f), lerpc(gc, 0xFF000000u, 0.2f), 0.6f);
        text(s.pos, F_LABEL, cx + pw * 0.5f, T + 18.5f, 0xFF0A0E16u, 0);
        if (s.jersey >= 0) { snprintf(b, sizeof b, "#%d", s.jersey); text(b, F_LABEL, cx + pw + 7.0f, T + 18.5f, 0xFF8A97AAu, -1); }
    }
    rrect(x0 - 2.0f, T + 26.0f, W - (x0 - hx) - 10.0f, 1.0f, 0, 0x26FFFFFFu, 0x26FFFFFFu, 0.3f);
    /* the stat cells: value over label */
    {
        float cx = x0 - 2.0f;
        for (i = 0; i < nb; i++) {
            if (i) rect(cx, T + 32.0f, 1.0f, 26.0f, 0x22FFFFFFu);
            text(val[i], F_VALUE, cx + cw[i] * 0.5f, T + 47.0f, 0xFFFFFFFFu, 0);
            text(lab[i], F_LABEL, cx + cw[i] * 0.5f, T + 59.0f, 0xFF8A97AAu, 0);
            cx += cw[i];
        }
    }
    /* the grade: glow when it changes, a soft drop, white rim, the grade's colour, the letter */
    if (pulse > 0.0f)
        hexagon(hx, hy, R + 3.0f + 7.0f * (1.0f - pulse), with_a(gc, (unsigned)(170 * pulse)), with_a(gc, (unsigned)(170 * pulse)), 6.0f);
    hexagon(hx + 1.0f, hy + 3.0f, R, 0x70000000u, 0x70000000u, 5.0f);
    hexagon(hx, hy, R, 0xFFFFFFFFu, 0xFFC4CDD9u, 0.8f);
    hexagon(hx, hy, R - 3.5f, lerpc(gc, 0xFFFFFFFFu, 0.22f), lerpc(gc, 0xFF000000u, 0.28f), 0.6f);
    {   /* a sheen on the upper half */
        float r = R - 3.5f;
        float px[5] = { hx - r * 0.866f, hx, hx + r * 0.866f, hx + r * 0.866f, hx - r * 0.866f };
        float py[5] = { hy - r * 0.5f, hy - r, hy - r * 0.5f, hy, hy };
        uint32_t pc[5] = { 0x2EFFFFFFu, 0x38FFFFFFu, 0x2EFFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu };
        poly(px, py, pc, 5, 0.0f);
    }
    grade_text(k_letters[s.letter], hx, hy + 1.0f, 26.0f, 0xFF0A0E16u);
}

/* ---- style 1: as ESPN NFL 2K5 would have shipped it -------------------------------- */

static void draw_native(void)
{
    char lab[4][8], val[4][12], b[64];
    float cw[4];
    int nb = boxes(lab, val), i;
    float hh = 17.0f, bh = 36.0f, Bot = 372.0f, T = Bot - hh - bh - 1.5f, L = s.left + 12.0f;
    float gw = text_w(F_LABEL, wstr("GRADE")) + 14.0f;
    float CW = cells(nb, lab, val, 14.0f, cw), W, nwid;
    uint32_t gc = grade_colour(s.letter);
    snprintf(b, sizeof b, s.jersey >= 0 ? "%d  %s" : "%s", s.jersey >= 0 ? s.jersey : 0, s.name);
    if (s.jersey < 0) snprintf(b, sizeof b, "%s", s.name);
    nwid = text_w(F_LABEL, wstr(b)) + text_w(F_LABEL, wstr(s.pos)) + 30.0f;
    W = CW + gw + 2.0f;
    if (W < nwid) { cw[nb - 1] += nwid - W; W = nwid; }
    /* header: the game's crimson bar with a gold rule */
    rect(L + 3.0f, T + 3.0f, W, hh + bh + 1.5f, 0x70000000u);
    rrect(L, T, W, hh, 0, 0xFFD01A24u, 0xFF8E0D14u, 0);
    rect(L, T, W, 1.0f, 0x60FFFFFFu);
    rect(L, T + hh, W, 1.5f, 0xFFF2C200u);
    text(b, F_LABEL, L + 6.0f, T + 13.5f, 0xFFFFFFFFu, -1);
    text(s.pos, F_LABEL, L + W - 6.0f, T + 13.5f, 0xFFF2C200u, 1);
    /* body: charcoal, the cells, the grade cell */
    rrect(L, T + hh + 1.5f, W, bh, 0, 0xEE2C2C2Cu, 0xEE0E0E0Eu, 0);
    {
        float cx = L, by = T + hh + 1.5f;
        for (i = 0; i < nb; i++) {
            if (i) rect(cx, by + 4.0f, 1.0f, bh - 8.0f, 0x40FFFFFFu);
            text(lab[i], F_LABEL, cx + cw[i] * 0.5f, by + 13.0f, 0xFFBEBEBEu, 0);
            text(val[i], F_VALUE, cx + cw[i] * 0.5f, by + 32.0f, 0xFFFFFFFFu, 0);
            cx += cw[i];
        }
        rect(cx + 1.0f, by, gw + 1.0f, bh, 0xFF050505u);
        rect(cx + 1.0f, by, 1.0f, bh, 0x60FFFFFFu);
        text("GRADE", F_LABEL, cx + 2.0f + gw * 0.5f, by + 13.0f, 0xFFF2C200u, 0);
        grade_text(k_letters[s.letter], cx + 2.0f + gw * 0.5f, by + 24.5f, 15.0f, gc);
    }
    rect(L, T + hh + 1.5f + bh - 1.5f, W, 1.5f, 0xFF8E0D14u);
}

static void diag_fonts(void)
{
    int f;
    float y = 40.0f;
    rect(20.0f, 50.0f, 40.0f, 40.0f, 0xC0FF00FFu);           /* 40 x 40 units: the aspect */
    for (f = 0; f < 9; f++) {
        char b[64];
        float w;
        if (!font(f)) continue;
        snprintf(b, sizeof b, "F%d Ag CMP/ATT 0123456789", f);
        rect(70.0f, y, 400.0f, 0.6f, 0xFFFF00FFu);            /* the y passed */
        rect(80.0f, y - 20.0f, 0.6f, 24.0f, 0xFF00FFFFu);     /* the left x */
        w = text(b, f, 80.0f, y, 0xFFFFFFFFu, -1);
        rect(80.0f + w, y - 20.0f, 0.6f, 24.0f, 0xFF00FF00u); /* the measured width */
        y += 36.0f;
    }
}

/* ---- the hooks ----------------------------------------------------------------- */

static uint32_t ih_line(void)
{
    uint32_t player = g_esi, eax = g_eax, ebx = g_ebx, ecx = g_ecx, edx = g_edx, edi = g_edi, ebp = g_ebp, esp = g_esp;
    DWORD now = GetTickCount();
    float dt = s.last_draw && now - s.last_draw < 500u ? (float)(now - s.last_draw) / 1000.0f : 1.0f / 60.0f;
    if (!readable(player) || MEM8(player + 0x35u) > 16u) return 0;
    if (MEM32(SD_SIM_FLAG)) return SD_LINE_EXIT;   /* Supersim on screen: its panel has the bottom, no bar over it */
    s.last_draw = now;
    s.pulse += dt;
    if (player != s.player || now - s.last_read >= 200u) { s.last_read = now; read_stats(player); }
    s.wnext = 0;                    /* the pack's HUD camera stays */
    if (s.diag) diag_fonts();
    if (s.style == 2) draw_modern(); else draw_native();
    g_eax = eax; g_ebx = ebx; g_ecx = ecx; g_edx = edx; g_esi = player; g_edi = edi; g_ebp = ebp; g_esp = esp;
    return SD_LINE_EXIT;
}

/* The pack's play line, wrapped as 0x014E7B06 wraps it: up to 4 lines of 48 characters. */
static int sim_lines(float *maxw)
{
    int lines = 0, n;
    uint32_t b = s.simbuf;
    char t[520];
    const char *p;
    *maxw = 0;
    if ((int32_t)MEM32(0x00E53804u) <= 0) {
        guest_text(0x0150599Cu, t, sizeof t);
        *maxw = text_w(0, wstr(t));
        return 1;
    }
    MEM16(b) = 0;
    gcall(sub_00150620, b, MEM32(0x00E53804u) - 1u);
    if (!guest_text(b, t, sizeof t)) return 1;
    p = t;
    while (*p && lines < 4) {
        int cut;
        char l[64];
        n = (int)strlen(p);
        cut = n > 48 ? 48 : n;
        if (n > 48) { int k = 48; while (k > 0 && p[k] != ' ') k--; if (k > 0) cut = k; }
        snprintf(l, sizeof l, "%.*s", cut, p);
        { float w = text_w(0, wstr(l)); if (w > *maxw) *maxw = w; }
        p += cut;
        while (*p == ' ') p++;
        lines++;
    }
    return lines ? lines : 1;
}

/* The pack draws its header at baseline 290 and its lines at 314 + 22 n, centred on 320, in F0
 * (12.5 tall); the panel goes round them. */
static uint32_t ih_sim(void)
{
    uint32_t eax = g_eax, ebx = g_ebx, ecx = g_ecx, edx = g_edx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    float maxw, w, x, y0 = 270.0f, h, hdrw;
    int lines;
    char hdr[64];
    if (!MEM32(SD_SIM_FLAG) || !MEM32(0x00E6028Cu) || !MEM32(0x00E5FC28u) || !MEM32(0x00E5FC68u)) return 0;
    s.wnext = 0;
    lines = sim_lines(&maxw);
    snprintf(hdr, sizeof hdr, "%u - %u   Q%u %u:%02u   8x   B: Cancel", 00u, 00u, 1u, 10u, 0u);
    hdrw = text_w(0, wstr(hdr));
    if (maxw < hdrw) maxw = hdrw;
    w = maxw + 48.0f;
    if (s.style != 2) {             /* the 2K5 look has a tab at the header's left: keep the header clear of it */
        float tab = text_w(F_LABEL, wstr("SUPERSIM")) + 16.0f;
        if (w < hdrw + 2.0f * (tab + 14.0f)) w = hdrw + 2.0f * (tab + 14.0f);
    }
    if (w < 320.0f) w = 320.0f;
    x = 320.0f - w * 0.5f;
    h = (314.0f + 22.0f * (float)(lines - 1) + 10.0f) - y0;
    if (s.style == 2) {
        float t = (float)(GetTickCount() % 1400u) / 1400.0f;
        int k;
        rrect(x - 3.0f, y0 + 5.0f, w + 6.0f, h, 10.0f, 0x58000000u, 0x58000000u, 9.0f);
        rrect(x, y0, w, h, 9.0f, 0xEE1D2534u, 0xEE0A0E16u, 0.8f);
        rrect(x + 14.0f, y0, w - 28.0f, 1.4f, 0, 0x80FFFFFFu, 0x10FFFFFFu, 0.4f);
        rrect(x + 18.0f, y0 + 26.5f, w - 36.0f, 1.0f, 0, 0x26FFFFFFu, 0x26FFFFFFu, 0.3f);
        /* the tag: SUPERSIM on a gold pill with fast-forward chevrons that sweep */
        {
            float lw = text_w(F_LABEL, wstr("SUPERSIM")), tw = lw + 34.0f, tx = x + 16.0f, ty = y0 - 9.0f;
            rrect(tx, ty, tw, 17.0f, 6.0f, 0xFFFFD84Au, 0xFFE8A40Au, 0.7f);
            text("SUPERSIM", F_LABEL, tx + 9.0f, ty + 13.0f, 0xFF10141Cu, -1);
            for (k = 0; k < 2; k++) {
                float cx = tx + lw + 14.0f + (float)k * 5.5f;
                float a = 0.35f + 0.65f * (float)fabs(sin((t - k * 0.18f) * 3.14159265));
                float px[3] = { cx, cx + 5.0f, cx }, py[3] = { ty + 3.5f, ty + 8.5f, ty + 13.5f };
                uint32_t c = with_a(0xFF10141Cu, (unsigned)(255 * a)), pc[3] = { c, c, c };
                poly(px, py, pc, 3, 0.6f);
            }
        }
        /* a progress rail on the bottom edge: how much of the game has gone */
        {
            float f = game_fraction(), rx = x + 18.0f, rw = w - 36.0f;
            rrect(rx, y0 + h - 5.0f, rw, 2.0f, 1.0f, 0x30FFFFFFu, 0x30FFFFFFu, 0.4f);
            rrect(rx, y0 + h - 5.0f, rw * f, 2.0f, 1.0f, 0xFFFFD84Au, 0xFFE8A40Au, 0.4f);
        }
    } else {
        rect(x + 3.0f, y0 + 3.0f, w, h, 0x70000000u);
        rrect(x, y0, w, 26.0f, 0, 0xF03A3A3Au, 0xF0141414u, 0);
        rect(x, y0, w, 1.0f, 0x60FFFFFFu);
        rect(x, y0 + 26.0f, w, 1.5f, 0xFFD01A24u);
        rrect(x, y0 + 27.5f, w, h - 27.5f, 0, 0xE6262626u, 0xE60C0C0Cu, 0);
        rect(x, y0 + h - 1.5f, w, 1.5f, 0xFF8E0D14u);
        {   /* the 2K5 tab on the left of the header */
            float lw = text_w(F_LABEL, wstr("SUPERSIM")) + 16.0f;
            rrect(x, y0, lw, 26.0f, 0, 0xFFD01A24u, 0xFF8E0D14u, 0);
            text("SUPERSIM", F_LABEL, x + 8.0f, y0 + 18.0f, 0xFFFFFFFFu, -1);
        }
    }
    g_eax = eax; g_ebx = ebx; g_ecx = ecx; g_edx = edx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    return 0;
}

/* ---- install ------------------------------------------------------------------- */

static int bytes_at(uint32_t va, const char *hex)
{
    size_t i, n = strlen(hex) / 2;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        if (MEM8(va + (uint32_t)i) != (uint8_t)v) return 0;
    }
    return 1;
}

void nfl2k5_mc_hud_install(void)
{
    const char *e = getenv("NFL2K5_MC_HUD");
    int i;
    if (e && *e == '0') return;
    if (MEM32(SD_MAGIC) != 0x4F48434Du || MEM32(SD_MAGIC + 4u) != 0x3158454Du ||
        !bytes_at(SD_LINE_DRAW, "8d542420") || !bytes_at(SD_LINE_EXIT, "81c420010000") ||
        !bytes_at(SD_SIM_DRAW, "833d4c314f0100") || !bytes_at(SD_REFRESH_END, "c3") ||
        !bytes_at(0x014E7245u, "bab6ad4e01") || !bytes_at(0x014E7C6Fu, "686cae4e01"))
        return;                                    /* not SOFTDRINK v0.9 */
    { const char *l = getenv("NFL2K5_MC_HUD_LOG"); s.log = l && *l && *l != '0'; }
    { const char *d = getenv("NFL2K5_MC_HUD_DIAG"); s.diag = d && !strcmp(d, "fonts"); }
    ini_load();
    s.size = 8192u;
    s.mem = xbox_HeapAlloc(s.size, 16);
    if (!s.mem) return;
    s.state = h_alloc(0x80, 16);
    s.vtx = h_alloc(16, 16);
    s.simbuf = h_alloc(0x400, 4);
    s.camsave = h_alloc(0x2C0, 16);
    s.label = h_alloc(96, 4);
    for (i = 0; i < 32; i++) s.wbuf[i] = h_alloc(128, 4);
    if (!s.wbuf[31]) { fprintf(stderr, "[MCHUD] off: out of guest memory\n"); return; }
    label_refresh();
    s.on = 1;
    recomp_interp_add_hook(SD_LINE_DRAW, 0, ih_line);
    recomp_interp_add_hook(SD_SIM_DRAW, 0, ih_sim);
    recomp_interp_add_hook(SD_HANDLER, 0, ih_handler);
    recomp_interp_add_hook(SD_REFRESH_END, 0, ih_refresh_end);
    fprintf(stderr, "[MCHUD] on: SOFTDRINK v0.9 MyCareer found; stat bar style %d (%s), x scale %.2f, setting %s\n",
            s.style, s.style == 2 ? "modern" : "2K5", s.ax, s.ini);
}

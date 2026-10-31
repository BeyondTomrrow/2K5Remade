/* ── On-screen controls (Android) ─────────────────────────────────────────
 *
 * A pad for players without a controller: a floating left stick (touch
 * anywhere in the lower left), a d-pad, A/B/X/Y, LT/RT, White/Black (LB/RB)
 * and Back/Start. Touches arrive through an SDL event watch (SDL's Android
 * thread) and are merged into port 0 by the input layer (nfl2k5_input_hle.c,
 * nfl2k5_touch_pad_merge). The controls are drawn through the presenter's HUD
 * hook (the HTML scorebug's on Windows, unused on Android), as one BGRA image
 * redrawn only when something changes. They hide while a real controller is
 * connected and nothing has been touched for a while. */
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* nv2a_gpu_present.inc.c's HUD hook */
typedef struct { int bb_w, bb_h; float game_x, game_y, game_w, game_h; } XboxHudFrame;
typedef struct { const void *pixels; int w, h, stride, x, y, changed, dw, dh; void *shared; int dirty_y0, dirty_y1; const unsigned char *dirty_rows; } XboxHudImage;
typedef int (*XboxHudCallback)(const XboxHudFrame *f, XboxHudImage *img);
void xbox_PresentSetTopHudCallback(XboxHudCallback cb);   /* drawn over the broadcast HUD */

/* The pad as the game's XINPUT_GAMEPAD has it (wButtons bits, analog buttons
 * A B X Y Black White LT RT). */
enum { C_NONE, C_STICK, C_DPAD, C_A, C_B, C_X, C_Y, C_BLACK, C_WHITE, C_LT, C_RT, C_BACK, C_START, C_COUNT };

typedef struct { float x, y, r; } Circle;     /* image pixels */
typedef struct { float x, y, w, h; } Rect;

static SDL_Mutex *s_lock;
static struct {
    SDL_FingerID id;
    int control;
    float x, y;                                /* image pixels */
} s_fingers[10];
static float s_stick_cx, s_stick_cy, s_stick_dx, s_stick_dy;   /* where the stick was put down, its offset */
static int s_stick_active;
static unsigned s_dpad;                        /* wButtons bits 0..3 */
static int s_pressed[C_COUNT];
static int s_back_key;                   /* the system Back button/gesture: the game's B */
static Uint64 s_last_touch;
static volatile int s_gen = 1;                 /* bumped by every change: redraw */

static int s_W = 960, s_H = 540;               /* image size (the screen's aspect) */
static uint32_t *s_img;
static int s_img_gen;

/* ---- layout (image pixels; H-relative so it holds on any aspect) ------- */
static Circle stick_home(void) { Circle c = { s_H * 0.33f, s_H * 0.72f, s_H * 0.17f }; return c; }
static Circle dpad_c(void) { Circle c = { s_H * 0.25f, s_H * 0.36f, s_H * 0.13f }; return c; }
static Circle face(int k)
{
    float cx = s_W - s_H * 0.31f, cy = s_H * 0.70f, o = s_H * 0.135f;
    Circle c = { cx, cy, s_H * 0.075f };
    switch (k) {
    case C_A: c.y += o; break;
    case C_B: c.x += o; break;
    case C_X: c.x -= o; break;
    default:  c.y -= o; break;                 /* Y */
    }
    return c;
}
static Rect btn_rect(int k)
{
    float w = s_H * 0.20f, h = s_H * 0.11f, t = s_H * 0.04f;
    Rect r = { 0, t, w, h };
    switch (k) {
    case C_LT:    r.x = s_H * 0.04f; break;
    case C_WHITE: r.x = s_H * 0.04f + w + s_H * 0.03f; break;
    case C_RT:    r.x = s_W - s_H * 0.04f - w; break;
    case C_BLACK: r.x = s_W - s_H * 0.04f - 2 * w - s_H * 0.03f; break;
    case C_BACK:  r.w = s_H * 0.17f; r.h = s_H * 0.09f; r.x = s_W * 0.5f - r.w - s_H * 0.03f; break;
    default:      r.w = s_H * 0.17f; r.h = s_H * 0.09f; r.x = s_W * 0.5f + s_H * 0.03f; break;   /* Start */
    }
    return r;
}

static int in_circle(Circle c, float x, float y, float grow) { float dx = x - c.x, dy = y - c.y, r = c.r * grow; return dx * dx + dy * dy <= r * r; }
static int in_rect(Rect r, float x, float y, float g)
{
    float mx = r.w * (g - 1) * 0.5f, my = r.h * (g - 1) * 0.5f;
    return x >= r.x - mx && x <= r.x + r.w + mx && y >= r.y - my && y <= r.y + r.h + my;
}

/* Which control a touch at (x, y) is on; buttons get a 25% margin. */
static int hit(float x, float y)
{
    int k;
    for (k = C_A; k <= C_Y; k++) if (in_circle(face(k), x, y, 1.35f)) return k;
    for (k = C_BLACK; k <= C_START; k++) if (in_rect(btn_rect(k), x, y, 1.25f)) return k;
    if (in_circle(dpad_c(), x, y, 1.2f)) return C_DPAD;
    if (x < s_W * 0.45f && y > s_H * 0.45f) return C_STICK;
    return C_NONE;
}

static unsigned dpad_bits(float x, float y)
{
    Circle c = dpad_c();
    float dx = x - c.x, dy = y - c.y, dead = c.r * 0.25f;
    unsigned b = 0;
    if (dy < -dead && fabsf(dy) > fabsf(dx) * 0.4f) b |= 0x01;
    if (dy > dead && fabsf(dy) > fabsf(dx) * 0.4f) b |= 0x02;
    if (dx < -dead && fabsf(dx) > fabsf(dy) * 0.4f) b |= 0x04;
    if (dx > dead && fabsf(dx) > fabsf(dy) * 0.4f) b |= 0x08;
    return b;
}

/* Every press and the stick from the fingers down now (lock held). */
static void recompute(void)
{
    int i, p[C_COUNT];
    unsigned dp = 0;
    memset(p, 0, sizeof p);
    s_stick_active = 0;
    for (i = 0; i < 10; i++) {
        int c = s_fingers[i].control;
        if (!c) continue;
        if (c == C_STICK) {
            Circle h = stick_home();
            float dx = s_fingers[i].x - s_stick_cx, dy = s_fingers[i].y - s_stick_cy, d = sqrtf(dx * dx + dy * dy);
            if (d > h.r) { dx *= h.r / d; dy *= h.r / d; }
            s_stick_dx = dx / h.r; s_stick_dy = dy / h.r;
            s_stick_active = 1;
        } else if (c == C_DPAD) {
            dp |= dpad_bits(s_fingers[i].x, s_fingers[i].y);
        } else if (c >= C_A && c <= C_Y) {
            /* sliding across the face buttons presses the one under the finger */
            int k, on = 0;
            for (k = C_A; k <= C_Y; k++)
                if (in_circle(face(k), s_fingers[i].x, s_fingers[i].y, 1.35f)) { p[k] = 1; on = 1; }
            if (!on) p[c] = in_circle(face(c), s_fingers[i].x, s_fingers[i].y, 2.0f);
        } else {
            p[c] = 1;
        }
    }
    if (!s_stick_active) { s_stick_dx = s_stick_dy = 0; }
    if (memcmp(p, s_pressed, sizeof p) || dp != s_dpad || s_stick_active) {
        memcpy(s_pressed, p, sizeof p);
        s_dpad = dp;
        s_gen++;
    }
}

static bool SDLCALL on_event(void *ud, SDL_Event *e)
{
    int i, slot = -1;
    float x, y;
    (void)ud;
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && e->key.scancode == SDL_SCANCODE_AC_BACK) {
        /* Back (trapped: SDL_ANDROID_TRAP_BACK_BUTTON) closed the app; it is B instead */
        s_back_key = e->type == SDL_EVENT_KEY_DOWN;
        return true;
    }
    if (e->type != SDL_EVENT_FINGER_DOWN && e->type != SDL_EVENT_FINGER_MOTION &&
            e->type != SDL_EVENT_FINGER_UP && e->type != SDL_EVENT_FINGER_CANCELED)
        return true;
    x = e->tfinger.x * (float)s_W;
    y = e->tfinger.y * (float)s_H;
    SDL_LockMutex(s_lock);
    s_last_touch = SDL_GetTicks();
    for (i = 0; i < 10; i++) if (s_fingers[i].control && s_fingers[i].id == e->tfinger.fingerID) slot = i;
    if (e->type == SDL_EVENT_FINGER_DOWN) {
        if (slot < 0) for (i = 0; i < 10 && slot < 0; i++) if (!s_fingers[i].control) slot = i;
        if (slot >= 0) {
            int c = hit(x, y);
            s_fingers[slot].id = e->tfinger.fingerID;
            s_fingers[slot].control = c;
            s_fingers[slot].x = x; s_fingers[slot].y = y;
            if (c == C_STICK) { s_stick_cx = x; s_stick_cy = y; }
            if (!c) s_fingers[slot].control = 0;
        }
    } else if (slot >= 0) {
        if (e->type == SDL_EVENT_FINGER_MOTION) { s_fingers[slot].x = x; s_fingers[slot].y = y; }
        else s_fingers[slot].control = 0;
    }
    recompute();
    SDL_UnlockMutex(s_lock);
    return true;
}

/* ---- drawing ------------------------------------------------------------ */

/* 5x7 capitals for the labels */
static const struct { char c; uint8_t rows[7]; } k_font[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } }, { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } }, { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } }, { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } }, { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } }, { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } }, { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
};

/* src-over with a premultiplied colour (a 0..1) */
static void blend(int x, int y, float r, float g, float b, float a)
{
    uint32_t *p, d;
    float dr, dg, db, da;
    if (x < 0 || y < 0 || x >= s_W || y >= s_H || a <= 0) return;
    p = &s_img[y * s_W + x];
    d = *p;
    db = (d & 255) / 255.0f; dg = ((d >> 8) & 255) / 255.0f; dr = ((d >> 16) & 255) / 255.0f; da = (d >> 24) / 255.0f;
    dr = r * a + dr * (1 - a); dg = g * a + dg * (1 - a); db = b * a + db * (1 - a); da = a + da * (1 - a);
    *p = (uint32_t)(da * 255 + 0.5f) << 24 | (uint32_t)(dr * 255 + 0.5f) << 16 | (uint32_t)(dg * 255 + 0.5f) << 8 | (uint32_t)(db * 255 + 0.5f);
}

static void fill_circle(Circle c, float r, float g, float b, float a, float ring)
{
    int x, y;
    for (y = (int)(c.y - c.r - 1); y <= (int)(c.y + c.r + 1); y++)
        for (x = (int)(c.x - c.r - 1); x <= (int)(c.x + c.r + 1); x++) {
            float d = sqrtf((x + 0.5f - c.x) * (x + 0.5f - c.x) + (y + 0.5f - c.y) * (y + 0.5f - c.y));
            float cov = c.r + 0.5f - d;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            if (ring > 0 && d < c.r - ring) cov *= 0.0f;
            blend(x, y, r, g, b, a * cov);
        }
}

static void fill_rect(Rect rc, float r, float g, float b, float a)
{
    int x, y;
    float rad = rc.h * 0.5f;
    for (y = (int)rc.y; y < (int)(rc.y + rc.h); y++)
        for (x = (int)rc.x; x < (int)(rc.x + rc.w); x++) {
            float cx = x + 0.5f < rc.x + rad ? rc.x + rad : x + 0.5f > rc.x + rc.w - rad ? rc.x + rc.w - rad : x + 0.5f;
            float dx = x + 0.5f - cx, dy = y + 0.5f - (rc.y + rad);
            if (dx * dx + dy * dy <= rad * rad) blend(x, y, r, g, b, a);
        }
}

static void text(const char *s, float cx, float cy, float px, float a)
{
    int n = (int)strlen(s), i, row, col;
    float x0 = cx - (n * 6 - 1) * px * 0.5f, y0 = cy - 3.5f * px;
    for (i = 0; i < n; i++) {
        const uint8_t *g = NULL;
        size_t k;
        for (k = 0; k < sizeof k_font / sizeof k_font[0]; k++) if (k_font[k].c == s[i]) g = k_font[k].rows;
        if (!g) continue;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) {
                    int x, y;
                    for (y = (int)(y0 + row * px); y < (int)(y0 + (row + 1) * px); y++)
                        for (x = (int)(x0 + (i * 6 + col) * px); x < (int)(x0 + (i * 6 + col + 1) * px); x++)
                            blend(x, y, 1, 1, 1, a);
                }
    }
}

static void redraw(void)
{
    static const char *const face_label[] = { "A", "B", "X", "Y" };
    static const float face_rgb[4][3] = { { 0.2f, 0.8f, 0.2f }, { 0.9f, 0.2f, 0.2f }, { 0.2f, 0.45f, 0.95f }, { 0.95f, 0.8f, 0.1f } };
    static const char *const btn_label[] = { "BK", "WH", "LT", "RT", "BACK", "START" };   /* Black, White */
    float base = 0.30f, on = 0.70f, lab = s_H / 540.0f * 3.0f;
    int k;
    memset(s_img, 0, (size_t)s_W * s_H * 4);
    /* stick: its base where it was put down, else home */
    {
        Circle h = stick_home(), b = h, kn;
        if (s_stick_active) { b.x = s_stick_cx; b.y = s_stick_cy; }
        fill_circle(b, 1, 1, 1, 0.18f, 0);
        fill_circle(b, 1, 1, 1, 0.35f, s_H * 0.008f);
        kn.x = b.x + s_stick_dx * h.r; kn.y = b.y + s_stick_dy * h.r; kn.r = h.r * 0.45f;
        fill_circle(kn, 1, 1, 1, s_stick_active ? 0.6f : 0.35f, 0);
    }
    /* d-pad: four petals */
    {
        Circle c = dpad_c();
        static const float dir[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        for (k = 0; k < 4; k++) {
            Circle p = { c.x + dir[k][0] * c.r * 0.62f, c.y + dir[k][1] * c.r * 0.62f, c.r * 0.38f };
            fill_circle(p, 1, 1, 1, (s_dpad >> k) & 1 ? on : base, 0);
        }
    }
    for (k = C_A; k <= C_Y; k++) {
        const float *rgb = face_rgb[k - C_A];
        Circle c = face(k);
        fill_circle(c, rgb[0], rgb[1], rgb[2], s_pressed[k] ? 0.85f : 0.40f, 0);
        text(face_label[k - C_A], c.x, c.y, lab * 1.3f, 0.9f);
    }
    for (k = C_BLACK; k <= C_START; k++) {
        Rect r = btn_rect(k);
        fill_rect(r, 1, 1, 1, s_pressed[k] ? on : base);
        text(btn_label[k - C_BLACK], r.x + r.w * 0.5f, r.y + r.h * 0.5f, lab * (k >= C_BACK ? 0.7f : 0.9f), 0.9f);
    }
}

static int s_hidden_logged;
static int hud(const XboxHudFrame *f, XboxHudImage *img)
{
    int gen, H;
    if (!f || f->bb_w <= 0 || f->bb_h <= 0) return 0;
    /* hidden while a controller is in and the screen has not been touched for 10 s */
    if (SDL_HasGamepad() && SDL_GetTicks() - s_last_touch > 10000) {
        if (!s_hidden_logged++) SDL_Log("NFL2K5 touch controls hidden: controller connected");
        return 0;
    }
    s_hidden_logged = 0;
    H = (int)((float)s_W * f->bb_h / f->bb_w + 0.5f);
    SDL_LockMutex(s_lock);
    if (!s_img || H != s_H) {
        free(s_img);
        s_H = H > 120 ? H : 120;
        s_img = (uint32_t *)malloc((size_t)s_W * s_H * 4);
        s_img_gen = 0;
        s_gen++;
    }
    gen = s_gen;
    img->changed = gen != s_img_gen;
    if (s_img && img->changed) redraw();
    s_img_gen = gen;
    SDL_UnlockMutex(s_lock);
    if (!s_img) return 0;
    img->pixels = s_img;
    img->w = s_W; img->h = s_H; img->stride = s_W * 4;
    img->x = 0; img->y = 0; img->dw = f->bb_w; img->dh = f->bb_h;
    return 1;
}

void nfl2k5_touch_pad_init(void)
{
    if (s_lock) return;
    s_lock = SDL_CreateMutex();
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   /* touches are not mouse clicks (A) */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_AddEventWatch(on_event, NULL);
    xbox_PresentSetTopHudCallback(hud);
}

/* Port 0's pad (nfl2k5_input_hle.c): the touch controls added in. */
void nfl2k5_touch_pad_merge(uint16_t *buttons, uint8_t analog[8], int16_t *lx, int16_t *ly)
{
    static const int amap[] = { C_A, C_B, C_X, C_Y, C_BLACK, C_WHITE, C_LT, C_RT };
    int i;
    if (!s_lock) return;
    SDL_LockMutex(s_lock);
    for (i = 0; i < 8; i++) if (s_pressed[amap[i]]) analog[i] = 255;
    if (s_back_key) analog[1] = 255;
    *buttons |= (uint16_t)s_dpad;
    if (s_pressed[C_START]) *buttons |= 0x10;
    if (s_pressed[C_BACK]) *buttons |= 0x20;
    if (s_stick_active) {
        float x = s_stick_dx, y = -s_stick_dy;
        if (fabsf(x) > 0.12f) *lx = (int16_t)(x * 32767.0f);
        if (fabsf(y) > 0.12f) *ly = (int16_t)(y * 32767.0f);
    }
    SDL_UnlockMutex(s_lock);
}
#endif

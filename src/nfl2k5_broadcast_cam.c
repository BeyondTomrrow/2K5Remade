/* Pro Broadcast camera system. See nfl2k5_broadcast_cam.h.
 *
 * ===== HONEST STATUS =====
 * Built on the Sandbox live camera-override path (the per-frame world camera
 * hook). Each selectable mode frames the action differently and moves with
 * critically-damped, frame-rate-independent operator motion. The override is
 * gated to on-field play only (flow 11..18); the play-call screen (flow 5),
 * halftime, period breaks and game-over hand control back to the native
 * camera/presentation. Mode 0 (Off) leaves the original cameras untouched.
 *
 * The field-scale framing numbers below are first-pass and tuned by eye against
 * real broadcast footage; expect to refine them from the captured native-eye
 * values ([SANDBOX] captured live viewport ... with NFL2K5_BROADCAST_DEBUG=1).
 *
 * Controls: pick the "Pro Broadcast" angle on the in-game Camera screen, or
 * force it with NFL2K5_PRO_BROADCAST=1..6 (1=Broadcast ... 6=Free Cam). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_broadcast_cam.h"
#include "nfl2k5_sandbox_world.h"
#include "nfl2k5_sandbox_actors.h"

#define IN_GAME  0x00A83A18u   /* == 3 while a match is live */
#define FLOW     0x00E602B8u   /* on-field play when 11..18; 5 = play-call menu */
#define DRIVE    0x00E602ECu   /* live drive struct: ball spot at +0x18 */
#define CLOCK    0x00E6028Cu   /* game clock object (the actors list keys off it) */
#define PI_F     3.14159265358979323846f

static const char *const k_mode_names[NFL2K5_BCAST_MODE_COUNT] = {
    "Off", "Broadcast", "Quarterback", "Handheld", "End Zone", "SkyCam", "Free Cam", "My Camera"
};

static int s_init, s_mode;
static int s_armed, s_in;
static int s_have;                     /* smoothed state primed */
static double s_last_ms;
static float s_aimx, s_aimy, s_aimz;   /* smoothed aim target (world cm) */
static float s_eyex, s_eyey, s_eyez;   /* smoothed eye position (world cm) */
static float s_yaw, s_pitch, s_fov;    /* smoothed camera orientation */
static int s_last_mode = -1;
/* Live pad snapshot (Free Cam only). */
static uint32_t s_btn; static int s_lx, s_ly, s_rx, s_ry, s_tl, s_tr;
/* Debug: NFL2K5_BROADCAST_CYCLE=<seconds> auto-advances the angle (1..6) so a
 * single run previews every camera. 0 = off (normal selection). */
static int s_cycle_sec; static double s_cycle_next_ms;

static void parse_env(void)
{
    const char *v;
    if (s_init) return;
    s_init = 1;
    { const char *c = getenv("NFL2K5_BROADCAST_CYCLE"); s_cycle_sec = c ? atoi(c) : 0; }
    if (s_cycle_sec > 0) s_mode = NFL2K5_BCAST_BROADCAST;
    v = getenv("NFL2K5_PRO_BROADCAST");
    if (v && *v) {
        int m = atoi(v);
        if (m == 0 && *v != '0') m = NFL2K5_BCAST_BROADCAST;   /* "1"/"on"/truthy */
        if (m < 0) m = 0;
        if (m >= NFL2K5_BCAST_MODE_COUNT) m = NFL2K5_BCAST_MODE_COUNT - 1;
        s_mode = m;
        if (s_mode)
            fprintf(stderr, "[BROADCAST] forced angle: %s (live override is EXPERIMENTAL)\n",
                    k_mode_names[s_mode]);
    }
}

int  nfl2k5_broadcast_cam_mode(void)        { parse_env(); return s_mode; }
void nfl2k5_broadcast_cam_set_mode(int m)   { parse_env(); if (m < 0) m = 0;
    if (m >= NFL2K5_BCAST_MODE_COUNT) m = NFL2K5_BCAST_MODE_COUNT - 1; s_mode = m; }
int  nfl2k5_broadcast_cam_mode_count(void)  { return NFL2K5_BCAST_MODE_COUNT; }
const char *nfl2k5_broadcast_cam_mode_name(int m)
{ return (m >= 0 && m < NFL2K5_BCAST_MODE_COUNT) ? k_mode_names[m] : "?"; }

int  nfl2k5_broadcast_cam_enabled(void)       { parse_env(); return s_mode != 0; }
void nfl2k5_broadcast_cam_set_enabled(int on) { parse_env(); s_mode = on ? NFL2K5_BCAST_BROADCAST : 0; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- My Camera (2026-10-30) ----------------------------------------------
 * The player places the camera with Free Cam and saves it with Back + D-pad
 * Up. What is kept is where the camera sits relative to the ball -- its
 * offset, with the along-the-field part in the offense's direction so the
 * shot flips with possession -- and its zoom. In play the camera holds that
 * offset from the ball and always aims at it, so a deep ball or a long run
 * stays in the picture. Saved in nfl2k5_camera.ini in the user folder. */
#define BTN_UP   0x0001u
#define BTN_BACK 0x0020u
typedef struct { int have; float dx, dy, dz, fov; } MyCam;
static MyCam s_my;
static int s_my_loaded;
static float s_dir = 1.f;              /* +1 / -1: the way the offense is going (along z) */
void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
char *xbox_user_path(const char *rel, char *out, size_t n);

static void mycam_load(void)
{
    char path[MAX_PATH];
    FILE *f;
    if (s_my_loaded) return;
    s_my_loaded = 1;
    if ((f = fopen(xbox_user_path("nfl2k5_camera.ini", path, sizeof path), "r")) != NULL) {
        MyCam m;
        memset(&m, 0, sizeof m);
        if (fscanf(f, " offset = %f %f %f fov = %f", &m.dx, &m.dy, &m.dz, &m.fov) == 4 &&
                isfinite(m.dx) && isfinite(m.dy) && isfinite(m.dz) && m.fov > 5.f && m.fov < 120.f) {
            m.have = 1;
            s_my = m;
        }
        fclose(f);
    }
    if (!s_my.have) {          /* until one is saved: high behind the offense */
        s_my.have = 1; s_my.dx = 0.f; s_my.dy = 900.f; s_my.dz = -1600.f; s_my.fov = 50.f;
    }
}

static void mycam_save(const MyCam *m)
{
    char path[MAX_PATH];
    FILE *f = fopen(xbox_user_path("nfl2k5_camera.ini", path, sizeof path), "w");
    if (!f) return;
    fprintf(f, "offset = %.1f %.1f %.1f fov = %.1f\n"
               "; My Camera (Pro Broadcast): the camera's position from the ball in cm\n"
               "; (across, up, along the field in the offense's direction) and its field of view.\n"
               "; Set it in a game: Free Cam, fly to the spot, Back + D-pad Up.\n",
            m->dx, m->dy, m->dz, m->fov);
    fclose(f);
}

/* The way the offense is going: from the ball spot toward the line to gain. */
static void update_dir(void)
{
    uint32_t ds = MEM32(DRIVE);
    if (ds) {
        float ball = MEMF(ds + 0x18u), line = MEMF(ds + 0x28u);
        if (isfinite(ball) && isfinite(line) && fabsf(line - ball) > 1.f)
            s_dir = line > ball ? 1.f : -1.f;
    }
}


/* Frame-rate-independent exponential approach toward a target. rate is 1/tau
 * in 1/seconds: bigger = snappier, smaller = more operator lag. */
static float approach(float cur, float tgt, float rate, float dt)
{
    float a = 1.f - expf(-rate * dt);
    return cur + (tgt - cur) * a;
}

/* The point to frame: the centroid of the players on the field (so the camera
 * follows the play as it develops), or the ball spot before the snap / as a
 * fallback. Returns 0 if nothing usable. */
static int action_target(float *tx, float *ty, float *tz)
{
    uint32_t clk = MEM32(CLOCK), ds;
    Nfl2k5SandboxActor a[NFL2K5_SANDBOX_MAX_ACTORS];
    int n = clk ? nfl2k5_sandbox_actors_list(clk, a, NFL2K5_SANDBOX_MAX_ACTORS) : 0;
    if (n > 0) {
        double sx = 0, sy = 0, sz = 0;
        int i;
        for (i = 0; i < n; i++) { sx += a[i].x; sy += a[i].y; sz += a[i].z; }
        *tx = (float)(sx / n); *ty = (float)(sy / n); *tz = (float)(sz / n);
        if (isfinite(*tx) && isfinite(*tz)) return 1;
    }
    ds = MEM32(DRIVE);
    if (ds) {
        float b = MEMF(ds + 0x18u);
        if (isfinite(b) && fabsf(b) < 20000.f) { *tx = 0.f; *ty = 80.f; *tz = b; return 1; }
    }
    return 0;
}

/* The football's position (world cm): a float triple (x, y, z) at 0x00BE5ED0,
 * found with nfl2k5_ballscan.c (2026-10-30): at every snap it sits on the
 * ball spot, it moves smoothly with the carrier (<1.2 m a frame) and comes to
 * rest where the next play is spotted. NFL2K5_BALL_ADDR=<hex> overrides it
 * (0 = the framing point above instead). */
static uint32_t s_ball_addr = 0x00BE5ED0u;
static int ball_target(float *tx, float *ty, float *tz)
{
    static int looked;
    if (!looked) {
        const char *e = getenv("NFL2K5_BALL_ADDR");
        looked = 1;
        if (e && *e) s_ball_addr = (uint32_t)strtoul(e, NULL, 16);   /* "0" turns it off */
    }
    if (s_ball_addr) {
        float x = MEMF(s_ball_addr), y = MEMF(s_ball_addr + 4u), z = MEMF(s_ball_addr + 8u);
        if (isfinite(x) && isfinite(y) && isfinite(z) && fabsf(x) < 6000.f && fabsf(z) < 8000.f &&
                y > -100.f && y < 6000.f) {
            *tx = x; *ty = y; *tz = z;
            return 1;
        }
    }
    return action_target(tx, ty, tz);
}

static void release(void)
{
    if (s_armed) { nfl2k5_sandbox_world_leave(); s_armed = 0; }
    s_have = 0;
}

/* Per-mode shot: target eye position, aim lift, fov curve and operator rates. */
typedef struct {
    float ex, ey, ez;          /* target eye (world cm)           */
    float aim_dy;              /* lift the aim above the target    */
    float fov_base, fov_k, fov_lo, fov_hi;
    float r_eye, r_aim, r_yaw, r_pitch, r_zoom;   /* 1/tau seconds  */
    int   sway;                /* handheld shoulder sway            */
} Shot;

static void shot_for(int mode, float tx, float ty, float tz, Shot *s)
{
    memset(s, 0, sizeof *s);
    s->aim_dy = 120.f;
    s->fov_lo = 28.f; s->fov_hi = 60.f;
    switch (mode) {
    default:
    case NFL2K5_BCAST_BROADCAST:   /* high sideline, steady tripod */
        s->ex = -5000.f; s->ey = 2800.f; s->ez = clampf(tz * 0.40f, -2500.f, 2500.f);
        s->aim_dy = 60.f;
        s->fov_base = 34.f; s->fov_k = 0.0030f; s->fov_lo = 26.f; s->fov_hi = 44.f;
        s->r_eye = 0.6f; s->r_aim = 2.5f; s->r_yaw = 3.5f; s->r_pitch = 2.0f; s->r_zoom = 1.2f;
        break;
    case NFL2K5_BCAST_QB:          /* cart-style sideline, horizontal track */
        s->ex = -2900.f; s->ey = 850.f; s->ez = clampf(tz, -5200.f, 5200.f);
        s->fov_base = 42.f; s->fov_k = 0.0030f; s->fov_lo = 32.f; s->fov_hi = 52.f;
        s->r_eye = 2.2f; s->r_aim = 2.8f; s->r_yaw = 4.5f; s->r_pitch = 2.0f; s->r_zoom = 1.5f;
        break;
    case NFL2K5_BCAST_HANDHELD:    /* low, near the action, subtle sway */
        s->ex = -2500.f; s->ey = 560.f; s->ez = clampf(tz * 0.75f, -4200.f, 4200.f);
        s->fov_base = 46.f; s->fov_k = 0.0030f; s->fov_lo = 36.f; s->fov_hi = 58.f;
        s->r_eye = 1.5f; s->r_aim = 3.0f; s->r_yaw = 4.0f; s->r_pitch = 2.5f; s->r_zoom = 1.8f;
        s->sway = 1;
        break;
    case NFL2K5_BCAST_ENDZONE:     /* behind the nearer end zone */
        s->ex = clampf(tx * 0.35f, -900.f, 900.f);
        s->ey = 1500.f; s->ez = (tz >= 0.f ? 6300.f : -6300.f);
        s->aim_dy = 150.f;
        s->fov_base = 40.f; s->fov_k = 0.0025f; s->fov_lo = 30.f; s->fov_hi = 50.f;
        s->r_eye = 1.2f; s->r_aim = 2.2f; s->r_yaw = 2.5f; s->r_pitch = 1.8f; s->r_zoom = 1.0f;
        break;
    case NFL2K5_BCAST_SKYCAM:      /* overhead cable cam, fluid */
        s->ex = clampf(tx * 0.40f, -900.f, 900.f);
        s->ey = 2200.f; s->ez = tz - 1200.f;
        s->aim_dy = 40.f;
        s->fov_base = 52.f; s->fov_k = 0.0025f; s->fov_lo = 40.f; s->fov_hi = 60.f;
        s->r_eye = 1.2f; s->r_aim = 1.8f; s->r_yaw = 2.2f; s->r_pitch = 1.5f; s->r_zoom = 1.0f;
        break;
    }
}

/* Free Cam: fly the Sandbox free camera with the pad while the game plays. */
static void service_freecam(float dt)
{
    float lx = s_lx / 32767.f, ly = s_ly / 32767.f, rx = s_rx / 32767.f, ry = s_ry / 32767.f;
    float up = (s_tr - s_tl) / 255.f;
    const float dz = 0.18f;   /* stick dead zone */
    if (fabsf(lx) < dz) lx = 0; if (fabsf(ly) < dz) ly = 0;
    if (fabsf(rx) < dz) rx = 0; if (fabsf(ry) < dz) ry = 0;
    if (!s_armed) {
        if (!nfl2k5_sandbox_world_enter()) return;
        nfl2k5_sandbox_world_set_timescale(1.f);   /* keep the game live while flying */
        nfl2k5_sandbox_world_set_fly_speed(2500.f);
        s_armed = 1;
    }
    /* move_x, move_y, look_x, look_y, elevate, zoom */
    nfl2k5_sandbox_world_input(lx, ly, rx, -ry, up, 0.f);
    nfl2k5_sandbox_world_tick(dt);
    {   /* Back + D-pad Up: keep this spot as My Camera */
        static uint32_t prev;
        uint32_t combo = BTN_BACK | BTN_UP;
        if ((s_btn & combo) == combo && (prev & combo) != combo) {
            Nfl2k5SandboxWorldState w;
            float tx, ty, tz;
            nfl2k5_sandbox_world_get_state(&w);
            if (w.camera_ready && ball_target(&tx, &ty, &tz)) {
                MyCam m;
                update_dir();
                m.have = 1;
                m.dx = (w.x - tx) * s_dir; m.dy = w.y - ty; m.dz = (w.z - tz) * s_dir;
                m.fov = clampf(w.fov, 15.f, 90.f);
                s_my = m; s_my_loaded = 1;
                mycam_save(&m);
                fprintf(stderr, "[BROADCAST] My Camera saved: offset %.0f,%.0f,%.0f fov %.0f\n",
                        m.dx, m.dy, m.dz, m.fov);
                xbox_PresentToast("MY CAMERA", "Camera position saved",
                                  "Pick \"My Camera\" on the Camera screen to play with it", 3500);
            }
        }
        prev = s_btn;
    }
}

/* My Camera in play: hold the saved offset from the ball, aim at the ball. */
static void service_mycam(float dt)
{
    float tx, ty, tz, ex, ey, ez, dx, dy, dz, len;
    Nfl2k5SandboxWorldState w;
    mycam_load();
    if (!s_armed) {
        if (!nfl2k5_sandbox_world_enter()) return;
        s_armed = 1;
    }
    if (!ball_target(&tx, &ty, &tz)) return;
    update_dir();
    if (!s_have) {
        s_aimx = tx; s_aimy = ty; s_aimz = tz;
        s_eyex = tx + s_my.dx * s_dir; s_eyey = ty + s_my.dy; s_eyez = tz + s_my.dz * s_dir;
        s_have = 1;
    }
    /* The aim keeps up with a thrown ball; the camera body trails it a little,
     * like an operator on a cable or a cart. Swapping ends (possession) glides. */
    s_aimx = approach(s_aimx, tx, 9.f, dt);
    s_aimy = approach(s_aimy, ty, 7.f, dt);
    s_aimz = approach(s_aimz, tz, 9.f, dt);
    s_eyex = approach(s_eyex, s_aimx + s_my.dx * s_dir, 3.5f, dt);
    s_eyey = approach(s_eyey, clampf(s_aimy + s_my.dy, 60.f, 20000.f), 3.5f, dt);
    s_eyez = approach(s_eyez, s_aimz + s_my.dz * s_dir, 3.5f, dt);
    ex = s_eyex; ey = s_eyey; ez = s_eyez;
    dx = s_aimx - ex; dy = s_aimy - ey; dz = s_aimz - ez;
    len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(len > 1.f)) return;
    memset(&w, 0, sizeof w);
    w.ready = 1; w.camera_ready = 1; w.timescale = 1.f;
    w.x = ex; w.y = ey; w.z = ez;
    w.yaw = atan2f(dx, dz) * 180.f / PI_F;
    w.pitch = clampf(asinf(clampf(dy / len, -1.f, 1.f)) * 180.f / PI_F, -85.f, 85.f);
    w.fov = s_my.fov;
    nfl2k5_sandbox_world_restore(&w);
}

void nfl2k5_broadcast_cam_service_pad(uint32_t buttons, int lx, int ly, int rx, int ry, int tl, int tr)
{
    uint32_t flow;
    float tx, ty, tz, ex, ey, ez, dx, dy, dz, len, tyaw, tpitch, tfov, dt;
    double now;
    Nfl2k5SandboxWorldState w;
    Shot sh;

    s_btn = buttons; s_lx = lx; s_ly = ly; s_rx = rx; s_ry = ry; s_tl = tl; s_tr = tr;
    {   extern void nfl2k5_ballscan_service(void);   /* NFL2K5_BALLSCAN=1 test only */
        nfl2k5_ballscan_service(); }

    parse_env();
    if (!s_mode) { if (s_in) { release(); s_in = 0; } return; }
    if (MEM32(IN_GAME) != 3u) { if (s_in) { release(); s_in = 0; } return; }
    s_in = 1;
    if (s_cycle_sec > 0) {   /* debug: walk every angle, one per interval */
        double nowc = (double)GetTickCount64();
        if (s_cycle_next_ms == 0.0) s_cycle_next_ms = nowc + s_cycle_sec * 1000.0;
        else if (nowc >= s_cycle_next_ms) {
            s_mode = (s_mode >= NFL2K5_BCAST_MODE_COUNT - 1) ? NFL2K5_BCAST_BROADCAST : s_mode + 1;
            s_cycle_next_ms = nowc + s_cycle_sec * 1000.0;
            fprintf(stderr, "[BROADCAST] cycle -> %d %s\n", s_mode, k_mode_names[s_mode]);
        }
    }
    if (s_mode != s_last_mode) { release(); s_last_mode = s_mode; }   /* re-prime on a mode switch */

    /* On-field play only (pre-snap settle through the whistle). Everything else
     * (play-call menu flow 5, replays, halftime, period breaks, game over)
     * hands control back to the native camera/presentation. */
    flow = MEM32(FLOW);
    if (!(flow >= 11u && flow <= 18u)) { if (s_armed) release(); return; }

    now = (double)GetTickCount64();
    dt = s_last_ms > 0 ? (float)((now - s_last_ms) / 1000.0) : 0.016f;
    s_last_ms = now;
    if (!(dt > 0.f) || dt > 0.1f) dt = 0.016f;

    if (s_mode == NFL2K5_BCAST_FREECAM) { service_freecam(dt); return; }
    if (s_mode == NFL2K5_BCAST_CUSTOM) { service_mycam(dt); return; }

    if (!s_armed) {
        if (!nfl2k5_sandbox_world_enter()) return;   /* arms the per-frame camera hook */
        s_armed = 1;
    }
    if (!action_target(&tx, &ty, &tz)) return;
    shot_for(s_mode, tx, ty, tz, &sh);

    /* Prime smoothing on the first live frame so it doesn't swing in from zero. */
    if (!s_have) {
        s_aimx = tx; s_aimy = ty + sh.aim_dy; s_aimz = tz;
        s_eyex = sh.ex; s_eyey = sh.ey; s_eyez = sh.ez;
        dx = s_aimx - s_eyex; dy = s_aimy - s_eyey; dz = s_aimz - s_eyez;
        len = sqrtf(dx*dx + dy*dy + dz*dz);
        s_yaw = len > 1.f ? atan2f(dx, dz) * 180.f / PI_F : 0.f;
        s_pitch = len > 1.f ? asinf(clampf(dy / len, -1.f, 1.f)) * 180.f / PI_F : -10.f;
        s_fov = sh.fov_base;
        s_have = 1;
    }

    /* Soft operator tracking of the framed point. */
    s_aimx = approach(s_aimx, tx, sh.r_aim, dt);
    s_aimy = approach(s_aimy, ty + sh.aim_dy, sh.r_aim * 0.8f, dt);
    s_aimz = approach(s_aimz, tz, sh.r_aim, dt);

    /* Eye glides toward the mode's framing position (track / cart / cable). */
    s_eyex = approach(s_eyex, sh.ex, sh.r_eye, dt);
    s_eyey = approach(s_eyey, sh.ey, sh.r_eye, dt);
    s_eyez = approach(s_eyez, sh.ez, sh.r_eye * (s_mode == NFL2K5_BCAST_QB ? 1.4f : 1.0f), dt);
    ex = s_eyex; ey = s_eyey; ez = s_eyez;

    dx = s_aimx - ex; dy = s_aimy - ey; dz = s_aimz - ez;
    len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (!(len > 1.f)) return;
    tyaw = atan2f(dx, dz) * 180.f / PI_F;
    tpitch = asinf(clampf(dy / len, -1.f, 1.f)) * 180.f / PI_F;
    tfov = clampf(sh.fov_base - (len - 3800.f) * sh.fov_k, sh.fov_lo, sh.fov_hi);

    /* Pan (yaw) reacts fastest, tilt (pitch) slower, zoom (fov) slowest — like a
     * real operator. Critically damped by the first-order approach (no overshoot). */
    s_yaw = approach(s_yaw, tyaw, sh.r_yaw, dt);
    s_pitch = approach(s_pitch, tpitch, sh.r_pitch, dt);
    s_fov = approach(s_fov, tfov, sh.r_zoom, dt);

    if (sh.sway) {   /* handheld shoulder-mounted micro-motion */
        float t = (float)(now / 1000.0);
        s_yaw  += 0.45f * sinf(t * 1.3f) + 0.25f * sinf(t * 0.7f + 1.1f);
        s_pitch += 0.30f * sinf(t * 1.1f + 0.5f);
    }

    memset(&w, 0, sizeof w);
    w.ready = 1; w.camera_ready = 1; w.timescale = 1.f;
    w.x = ex; w.y = ey; w.z = ez;
    w.yaw = s_yaw; w.pitch = clampf(s_pitch, -85.f, 85.f); w.fov = s_fov;
    {
        int rr = nfl2k5_sandbox_world_restore(&w);
        if (getenv("NFL2K5_BROADCAST_DEBUG")) {
            static DWORD last; DWORD t = GetTickCount();
            if (t - last > 1000) {
                last = t;
                fprintf(stderr, "[BROADCAST] %s flow=%u tgt=%.0f,%.0f,%.0f eye=%.0f,%.0f,%.0f "
                        "yaw=%.1f pitch=%.1f fov=%.1f restore=%d\n",
                        k_mode_names[s_mode], (unsigned)flow, tx, ty, tz,
                        ex, ey, ez, s_yaw, s_pitch, s_fov, rr);
            }
        }
    }
}

void nfl2k5_broadcast_cam_service(void)
{
    nfl2k5_broadcast_cam_service_pad(0, 0, 0, 0, 0, 0, 0);
}

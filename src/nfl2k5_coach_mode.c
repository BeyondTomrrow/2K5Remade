/* Coach Mode (Quick Game / Situation): the user CALLS PLAYS for their role's
 * side(s); the CPU runs every body on the field and calls the other side's
 * plays. A sideline camera watches live play, and the live game can be
 * fast-forwarded. Everything is gated behind nfl2k5_coach_enabled(); off, this
 * whole file is inert and ordinary games behave exactly as before.
 *
 * ================= WHAT IS AND IS NOT VERIFIED (read me) ===================
 * Written against this recomp's conventions and modelled on the experimental
 * MyCareer adapter (.tmp-2k-football-mod-tools/tools/mycareer_mode/runtime.c:
 * mode_human / the hand-back latch / mode_ff_frame). It COMPILES into the game
 * and is gated, but it was NOT witnessed running in-game in this session:
 *
 *  - PLAY-CALL HAND-OFF (highest risk, experimental): we do NOT patch the
 *    native read of the side object's "human" bit. Instead, from the per-frame
 *    guest service we directly raise that bit (+0x30) and point controller
 *    port 0 at the user's side DURING the call/formation phase (E602B4==4),
 *    then clear both before the snap so the CPU executes the body. Whether the
 *    native pre-snap code latches body ownership earlier than our clear, and
 *    therefore whether the CPU cleanly runs a user-chosen play without handing
 *    the body to the controller, is UNVERIFIED. MyCareer needed an elaborate
 *    settle/re-arm latch (its +2740 / 0xacb90 / 0x9fbe0) that we did not port.
 *
 *  - SIDELINE CAMERA: reuses the Sandbox world camera override, but driven
 *    during LIVE play at full timescale (the Sandbox only ever ran it paused).
 *    Running the per-frame camera write every frame at speed is UNVERIFIED and
 *    may hitch or fight the native camera. Disable with NFL2K5_COACH_CAMERA=0.
 *    Do not use Coach Mode and Sandbox at the same time (both drive the world).
 *
 *  - FAST-FORWARD: re-runs the native per-manager frame (sub_0006E6A0) up to 8x
 *    per real frame while a live play is on screen, modelled on mode_ff_frame.
 *    Re-entrant native calls mid-frame are UNVERIFIED here; audio is crudely
 *    muted (master gain only) rather than MyCareer's full voice invalidation.
 *
 *  - CPU-vs-CPU WATCH base (most reliable piece): on kickoff we force every
 *    controller port to CPU, so even if the hand-off does nothing the user
 *    still watches a CPU-vs-CPU game from the sideline and can fast-forward.
 *
 * MINIMAL ENTRY (no native menu screen yet): environment variables, read once
 *   NFL2K5_COACH_MODE=1            turn Coach Mode on
 *   NFL2K5_COACH_ROLE=hc|oc|dc     role (default hc)
 *   NFL2K5_COACH_TEAM=home|away    which team is the user's (default home)
 *   NFL2K5_COACH_CAMERA=0          disable the sideline camera
 * Controller: hold BOTH triggers during a live play to fast-forward.
 * The nfl2k5_coach_set_* setters let a future menu drive the same state. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_coach_mode.h"
#include "nfl2k5_sandbox_world.h"

/* ---- guest layout (retail XBE virtual addresses) ------------------------- */
#define IN_GAME       0x00A83A18u   /* == 3 while a match is live */
#define PHASE         0x00E602B4u   /* == 4 on the play-call / formation screen */
#define FLOW          0x00E602B8u   /* 14 live play, 13 pre-snap settle */
#define OFF_SIDE      0x00E60280u   /* -> offense side object (== a side base below) */
#define DEF_SIDE      0x00E60284u   /* -> defense side object */
#define SIDE_HOME     0x00E5FC20u   /* home side base */
#define SIDE_AWAY     0x00E5FC60u   /* away side base */
#define SIDE_HUMAN    0x30u         /* side object + 0x30: human-controlled bit */
#define PORT_SIDE     0x00E5FE88u   /* + port * 0x1C: 1 home, 2 away, 0 CPU */
#define PORT_STRIDE   0x1Cu
#define PORTS         8
#define DRIVE         0x00E602ECu   /* live drive struct (ball spot at +0x18) */
#define AUDIO_GAIN    0x00A70830u   /* master output gain (muted during FF) */

extern int nfl2k5_eras_launch_active(void);

/* ---- owned state --------------------------------------------------------- */
static int s_init;                  /* env parsed */
static int s_enabled;
static int s_role = NFL2K5_COACH_HC;
static int s_team_home = 1;         /* user team is home by default */
static int s_camera = 1;
static volatile int s_ff;           /* fast-forward armed (read by the FF hook) */

static int s_in_game;               /* latched: a match is live and set up */
static uint32_t s_saved_sides[PORTS];
static int s_sides_saved;
static int s_handoff;               /* the call-screen human hand-off is latched */
static uint32_t s_handoff_base;     /* which side base we raised */
static int s_camera_armed;

/* ---- native-call helpers (mirror src/nfl2k5_eras_launch.c gcall) --------- */
static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, 0x0BADC0DEu);
    fn();
    return g_eax;
}
/* Re-invoke the per-manager frame: ECX=manager, EDX=0, one stack arg (delta),
 * which the callee cleans (the native call site adjusts no esp afterwards). */
static void gcall_manager_frame(uint32_t manager, uint32_t delta)
{
    g_ecx = manager; g_edx = 0;
    PUSH32(g_esp, delta);
    PUSH32(g_esp, 0x0BADC0DEu);
    sub_0006E6A0();
}

static void parse_env(void)
{
    const char *v;
    if (s_init) return;
    s_init = 1;
    v = getenv("NFL2K5_COACH_MODE");  s_enabled = v && *v && *v != '0';
    v = getenv("NFL2K5_COACH_ROLE");
    if (v) {
        if      (!_stricmp(v, "oc") || !_stricmp(v, "offense")) s_role = NFL2K5_COACH_OC;
        else if (!_stricmp(v, "dc") || !_stricmp(v, "defense")) s_role = NFL2K5_COACH_DC;
        else s_role = NFL2K5_COACH_HC;
    }
    v = getenv("NFL2K5_COACH_TEAM");  if (v && !_stricmp(v, "away")) s_team_home = 0;
    v = getenv("NFL2K5_COACH_CAMERA"); if (v && *v == '0') s_camera = 0;
    if (s_enabled)
        fprintf(stderr, "[COACH] enabled: role=%s team=%s camera=%s "
                "(play-call hand-off + sideline camera are EXPERIMENTAL / unverified in-game)\n",
                s_role == NFL2K5_COACH_OC ? "OC" : s_role == NFL2K5_COACH_DC ? "DC" : "HC",
                s_team_home ? "home" : "away", s_camera ? "on" : "off");
}

int  nfl2k5_coach_enabled(void)        { parse_env(); return s_enabled; }
void nfl2k5_coach_set_enabled(int on)  { parse_env(); s_enabled = !!on; }
int  nfl2k5_coach_role(void)           { parse_env(); return s_role; }
void nfl2k5_coach_set_role(int role)   { parse_env(); if (role>=0 && role<=2) s_role = role; }
int  nfl2k5_coach_team_home(void)      { parse_env(); return s_team_home; }
void nfl2k5_coach_set_team_home(int h) { parse_env(); s_team_home = !!h; }
int  nfl2k5_coach_camera(void)         { parse_env(); return s_camera; }
void nfl2k5_coach_set_camera(int on)   { parse_env(); s_camera = !!on; }
void nfl2k5_coach_set_fast_forward(int on) { parse_env(); s_ff = !!on; }

/* ---- sideline camera (reuse the Sandbox world override) ------------------ */
static void camera_apply_sideline(void)
{
    Nfl2k5SandboxWorldState w;
    uint32_t ds;
    float spot = 0.f, dx, dy, dz, len;
    extern int nfl2k5_broadcast_cam_enabled(void);
    if (!s_camera || nfl2k5_broadcast_cam_enabled()) {   /* Pro Broadcast owns the camera when on */
        if (s_camera_armed) { nfl2k5_sandbox_world_leave(); s_camera_armed = 0; }
        return;
    }
    if (!s_camera_armed) {
        if (!nfl2k5_sandbox_world_enter()) return;   /* arms the per-frame camera hook */
        s_camera_armed = 1;
    }
    ds = MEM32(DRIVE);
    if (ds) { float b = MEMF(ds + 0x18u); if (isfinite(b) && fabsf(b) < 20000.f) spot = b; }
    memset(&w, 0, sizeof w);
    w.ready = 1; w.camera_ready = 1; w.timescale = 1.f;   /* full speed, NOT paused */
    w.fov = 60.f;
    w.x = -3400.f; w.y = 1600.f; w.z = spot - 1500.f;
    dx = 0.f - w.x; dy = 80.f - w.y; dz = spot - w.z;
    len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (!(len > 1.f)) return;
    w.yaw = atan2f(dx, dz) * 180.f / 3.14159265358979323846f;
    w.pitch = asinf(dy / len) * 180.f / 3.14159265358979323846f;
    nfl2k5_sandbox_world_restore(&w);
}
static void camera_release(void)
{
    if (s_camera_armed) { nfl2k5_sandbox_world_leave(); s_camera_armed = 0; }
}

/* ---- enter / leave a match ---------------------------------------------- */
static void on_enter_game(void)
{
    int s;
    for (s = 0; s < PORTS; s++) s_saved_sides[s] = MEM32(PORT_SIDE + PORT_STRIDE * (uint32_t)s);
    s_sides_saved = 1;
    /* Force CPU bodies on every port: the user watches, never drives a body.
     * Even if the hand-off below does nothing, this yields a CPU-vs-CPU game. */
    for (s = 0; s < PORTS; s++) gcall(sub_00077200, (uint32_t)s, 0u);
    s_handoff = 0; s_handoff_base = 0;
    fprintf(stderr, "[COACH] match start: all ports forced to CPU (watch mode)\n");
}
static void on_leave_game(void)
{
    int s;
    if (s_sides_saved) {
        for (s = 0; s < PORTS; s++) gcall(sub_00077200, (uint32_t)s, s_saved_sides[s]);
        s_sides_saved = 0;
    }
    s_handoff = 0; s_handoff_base = 0;
    camera_release();
    s_ff = 0;
}

/* ---- the play-call hand-off --------------------------------------------- */
/* Raise the user's side to "human" on the call screen, then hand back to the
 * CPU before the snap. Returns the side base we should own this play, or 0. */
static uint32_t role_side_base(void)
{
    uint32_t user_base = s_team_home ? SIDE_HOME : SIDE_AWAY;
    uint32_t off = MEM32(OFF_SIDE), def = MEM32(DEF_SIDE);
    int user_has_ball;
    if (off != user_base && def != user_base) return 0;  /* can't map the user's team */
    user_has_ball = (off == user_base);
    switch (s_role) {
        case NFL2K5_COACH_OC: return user_has_ball ? user_base : 0;
        case NFL2K5_COACH_DC: return user_has_ball ? 0 : user_base;
        default:              return user_base;   /* HC: always the user's team */
    }
}
static void handoff_service(void)
{
    uint32_t phase = MEM32(PHASE);
    uint32_t user_side = s_team_home ? 1u : 2u;
    if (phase == 4u) {
        uint32_t base = role_side_base();
        if (base && !s_handoff) {
            MEM32(base + SIDE_HUMAN) = 1u;                 /* native call screen comes up */
            gcall(sub_00077200, 0u, user_side);            /* port 0 drives the menu */
            s_handoff = 1; s_handoff_base = base;
            fprintf(stderr, "[COACH] call screen: handed selection to the user (side %08X)\n", base);
        }
    } else if (s_handoff) {
        /* Call accepted / leaving the screen: hand the body back to the CPU. */
        MEM32(s_handoff_base + SIDE_HUMAN) = 0u;
        gcall(sub_00077200, 0u, 0u);
        fprintf(stderr, "[COACH] handed execution back to the CPU (side %08X)\n", s_handoff_base);
        s_handoff = 0; s_handoff_base = 0;
    }
}

/* ---- per-frame service (guest thread, port-0 XInput poll) ---------------- */
void nfl2k5_coach_service(uint16_t buttons, const uint8_t analog[8])
{
    uint32_t in_game;
    (void)buttons;
    parse_env();
    if (!s_enabled || nfl2k5_eras_launch_active()) { if (s_in_game) { on_leave_game(); s_in_game = 0; } return; }

    in_game = MEM32(IN_GAME);
    if (in_game != 3u) {
        if (s_in_game) { on_leave_game(); s_in_game = 0; }
        return;
    }
    if (!s_in_game) { on_enter_game(); s_in_game = 1; }

    handoff_service();
    camera_apply_sideline();

    /* Fast-forward while BOTH triggers are held (the coach never drives a body,
     * so the triggers are free). analog[4]=left trigger, analog[5]=right. */
    s_ff = (analog && analog[4] > 160 && analog[5] > 160) ? 1 : 0;
}

/* ---- fast-forward: extra per-manager frames (COACH_FF_FRAME gen patch) ---- */
void nfl2k5_coach_ff_extra(uint32_t manager, uint32_t delta)
{
    uint32_t saved_gain;
    int i;
    if (!s_enabled || !s_ff || !manager) return;
    if (MEM32(IN_GAME) != 3u || MEM32(FLOW) != 14u) return;   /* only accelerate live play */
    saved_gain = MEM32(AUDIO_GAIN);
    MEM32(AUDIO_GAIN) = 0u;                                    /* crude mute during the burst */
    for (i = 0; i < 7; i++) {
        gcall_manager_frame(manager, delta);                  /* one more native frame */
        gcall(sub_000A2120, 0u, 0u);                          /* skip presentation waits */
        if (MEM32(IN_GAME) != 3u || MEM32(FLOW) != 14u) break;
    }
    MEM32(AUDIO_GAIN) = saved_gain;
}

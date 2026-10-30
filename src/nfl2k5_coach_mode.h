/* Coach Mode: the user calls plays for their role's side(s) while the CPU
 * executes every body on the field and calls the other side's plays. The
 * coach watches from a sideline camera and can fast-forward live play.
 *
 * All behaviour is gated behind nfl2k5_coach_enabled(); with Coach Mode off
 * every entry point here is a no-op and normal games are unaffected.
 *
 * Design notes and the honest "what is / isn't verified" list live in the
 * implementation (src/nfl2k5_coach_mode.c). The play-call hand-off and the
 * live sideline camera are EXPERIMENTAL and were not witnessed in-game. */
#ifndef NFL2K5_COACH_MODE_H
#define NFL2K5_COACH_MODE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Roles. */
enum {
    NFL2K5_COACH_HC = 0,   /* Head Coach: calls for the user's team on every play */
    NFL2K5_COACH_OC = 1,   /* Offensive Coordinator: only when the user's team has the ball */
    NFL2K5_COACH_DC = 2    /* Defensive Coordinator: only when the user's team is on defense */
};

/* Config (a menu may call these; env vars provide the minimal test path). */
int  nfl2k5_coach_enabled(void);
void nfl2k5_coach_set_enabled(int on);
int  nfl2k5_coach_role(void);
void nfl2k5_coach_set_role(int role);          /* NFL2K5_COACH_HC/OC/DC */
int  nfl2k5_coach_team_home(void);             /* 1 = user team is home, 0 = away */
void nfl2k5_coach_set_team_home(int home);
int  nfl2k5_coach_camera(void);                /* sideline camera on/off */
void nfl2k5_coach_set_camera(int on);
void nfl2k5_coach_set_fast_forward(int on);    /* programmatic fast-forward toggle */

/* Per-frame guest-thread service (called from the port-0 XInput poll).
 * buttons/analog are the live pad so the coach can toggle fast-forward from
 * the controller; input is only read, never consumed. */
void nfl2k5_coach_service(uint16_t buttons, const uint8_t analog[8]);

/* Fast-forward: extra native per-manager-frame iterations, run from the main
 * game loop via the COACH_FF_FRAME gen patch. A no-op unless fast-forward is
 * armed and a live play is on screen. manager/delta come from the call site. */
void nfl2k5_coach_ff_extra(uint32_t manager, uint32_t delta);

#ifdef __cplusplus
}
#endif
#endif

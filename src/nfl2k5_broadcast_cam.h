/* Pro Broadcast camera system: a selectable set of automated TV cameras that
 * replace the game camera during live play, each with its own realistic
 * human-operator motion (critically-damped, fast pan / slower tilt / slowest
 * zoom, subtle reaction lag). It reuses the Sandbox world camera override,
 * driven during LIVE unpaused play, and suspends for the play-call screen,
 * replays, halftime/period breaks and pauses so the native camera/presentation
 * own those. Mode 0 (Off) leaves the original cameras untouched.
 *
 * Modes (see k_mode_names): Off, Broadcast (high sideline), Quarterback,
 * Handheld, End Zone, SkyCam, Free Cam. Selected on the in-game Camera screen
 * as the "Pro Broadcast" angle, or forced with NFL2K5_PRO_BROADCAST (0..6). */
#ifndef NFL2K5_BROADCAST_CAM_H
#define NFL2K5_BROADCAST_CAM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    NFL2K5_BCAST_OFF = 0,
    NFL2K5_BCAST_BROADCAST,   /* high sideline, the main TV shot */
    NFL2K5_BCAST_QB,          /* cart-style quarterback follow   */
    NFL2K5_BCAST_HANDHELD,    /* low sideline, subtle sway        */
    NFL2K5_BCAST_ENDZONE,     /* behind the end zone              */
    NFL2K5_BCAST_SKYCAM,      /* overhead cable cam               */
    NFL2K5_BCAST_FREECAM,     /* Sandbox free camera, pad-flown   */
    NFL2K5_BCAST_MODE_COUNT
};

int  nfl2k5_broadcast_cam_mode(void);              /* 0..NFL2K5_BCAST_MODE_COUNT-1 */
void nfl2k5_broadcast_cam_set_mode(int mode);
int  nfl2k5_broadcast_cam_mode_count(void);
const char *nfl2k5_broadcast_cam_mode_name(int mode);

/* Legacy on/off shim (On == Broadcast). */
int  nfl2k5_broadcast_cam_enabled(void);
void nfl2k5_broadcast_cam_set_enabled(int on);

/* Per-frame guest-thread service (called from the port-0 XInput poll, like
 * Coach Mode). Reads game state and writes the camera; never consumes input.
 * pad_buttons/left_x..right_y are the live pad, used only by Free Cam. */
void nfl2k5_broadcast_cam_service(void);
void nfl2k5_broadcast_cam_service_pad(uint32_t buttons,
                                      int left_x, int left_y,
                                      int right_x, int right_y,
                                      int trig_l, int trig_r);

#ifdef __cplusplus
}
#endif
#endif

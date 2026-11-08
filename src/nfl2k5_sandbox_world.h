/* Optional Sandbox gameplay camera/time controls. All mutations run on the
 * guest thread. get_state copies host-owned data; it never reads guest RAM. */
#ifndef NFL2K5_SANDBOX_WORLD_H
#define NFL2K5_SANDBOX_WORLD_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Nfl2k5SandboxWorldState {
    int ready, camera_ready;
    float timescale, x, y, z, yaw, pitch, fov;
} Nfl2k5SandboxWorldState;
int nfl2k5_sandbox_world_bind(void);
int nfl2k5_sandbox_world_enter(void);
void nfl2k5_sandbox_world_leave(void);
void nfl2k5_sandbox_world_tick(float host_dt);
void nfl2k5_sandbox_world_input(float move_x, float move_y, float look_x,
                                float look_y, float elevate, float zoom);
void nfl2k5_sandbox_world_set_timescale(float scale);
void nfl2k5_sandbox_world_set_camera(int enabled);
void nfl2k5_sandbox_world_set_fly_speed(float centimetres_per_second);
void nfl2k5_sandbox_world_step(void);
void nfl2k5_sandbox_world_get_state(Nfl2k5SandboxWorldState *out);
int nfl2k5_sandbox_world_restore(const Nfl2k5SandboxWorldState *state);
void nfl2k5_sandbox_world_status(char *out, size_t size);
/* Generated-code hooks: delta is the local update argument, not a host clock.
 * camera_object comes from the real viewport camera selection, before draw. */
float nfl2k5_sandbox_world_frame_delta(float original_delta);
void nfl2k5_sandbox_world_camera(uint32_t camera_object);
int nfl2k5_sandbox_world_selftest(void);
#ifdef __cplusplus
}
#endif
#endif

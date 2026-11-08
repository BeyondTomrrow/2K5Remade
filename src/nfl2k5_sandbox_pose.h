/* Optional visual posing. Guest execution thread only. This never moves a
 * simulation actor, changes a live bone palette, or edits a saved roster. */
#ifndef NFL2K5_SANDBOX_POSE_H
#define NFL2K5_SANDBOX_POSE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_SANDBOX_MAX_POSES 142
/* Validation cap only; the actual loadable count is nfl2k5_sandbox_pose_clip_count(). */
#define NFL2K5_SANDBOX_POSE_CLIP_CAP 64
typedef struct Nfl2k5SandboxPose {
    uint32_t id, identity;
    float x, y, z;       /* world centimetre offsets, each -500..500 */
    float yaw;           /* degrees, -180..180; +X turns toward +Z */
    float scale;         /* uniform visual scale, 0.25..3 */
    uint32_t clip;       /* 0 = none; 1..count selects a baked draftposes.bin clip */
    float clip_time;     /* seconds into the frozen clip, 0..600 (scrub target) */
} Nfl2k5SandboxPose;

int nfl2k5_sandbox_pose_bind(void);
int nfl2k5_sandbox_pose_valid(const Nfl2k5SandboxPose *pose);
/* Baked clip picker (mods/eras/draft/files/draftposes.bin, read-only, host cache).
 * count is 0 when the file is absent/invalid, which disables the picker. The
 * name/seconds accessors take a 0-based index 0..count-1. */
int nfl2k5_sandbox_pose_clip_count(void);
const char *nfl2k5_sandbox_pose_clip_name(int index0);
float nfl2k5_sandbox_pose_clip_seconds(int index0);
/* get returns a matching player's identity with zero offsets/scale 1 when
 * that player has no edit. list/count contain nondefault edits only. */
int nfl2k5_sandbox_pose_get(uint32_t clock, int id, Nfl2k5SandboxPose *out);
int nfl2k5_sandbox_pose_set(uint32_t clock, const Nfl2k5SandboxPose *pose);
int nfl2k5_sandbox_pose_list(uint32_t clock, Nfl2k5SandboxPose *out, int capacity);
int nfl2k5_sandbox_pose_count(uint32_t clock);
const char *nfl2k5_sandbox_pose_status(uint32_t clock, int id);
/* clock=0 invalidates the session. No live actor data is written. */
void nfl2k5_sandbox_pose_clear(uint32_t clock);

/* Call immediately before 8FFA4's original sub_21900 (ordinary body path),
 * with packet=[ESI+2C], object=ECX, palette=EDX. The return replaces EDX.
 * Caller MUST call end_draw at common 8FFB6 before discarding that packet.
 * Disabled, invalid or unsupported paths return the original palette. */
uint32_t nfl2k5_sandbox_pose_begin_draw(uint32_t packet, uint32_t object,
                                      uint32_t palette);
void nfl2k5_sandbox_pose_end_draw(void);
int nfl2k5_sandbox_pose_selftest(void);

#ifdef __cplusplus
}
#endif
#endif

/* Match-local player inspection and ratings. Call only on the guest execution
 * thread; the presentation thread must use owned snapshots. */
#ifndef NFL2K5_SANDBOX_ACTORS_H
#define NFL2K5_SANDBOX_ACTORS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_SANDBOX_MAX_ACTORS 64
#define NFL2K5_SANDBOX_RATINGS 28
#define NFL2K5_SANDBOX_PLAYER_ID_STRIDE 71
typedef struct Nfl2k5SandboxActor {
    uint32_t record, node, geometry, payload;
    int side, index, jersey, position; /* home=0, away=1; roster index */
    char name[160];
    /* Centre of the native player pose (bone matrix 12), world centimetres.
     * These are inspection/focus coordinates, not a writable actor transform. */
    float x, y, z;
} Nfl2k5SandboxActor;

typedef struct Nfl2k5SandboxActorProperties {
    uint32_t id;       /* side*71 + roster index; each copied array holds 65 */
    uint32_t identity; /* hash of UTF-16 first/last name and jersey, no pointers */
    uint8_t ratings[NFL2K5_SANDBOX_RATINGS];
} Nfl2k5SandboxActorProperties;

int nfl2k5_sandbox_actors_list(uint32_t expected_clock,
                             Nfl2k5SandboxActor *out, int capacity);
int nfl2k5_sandbox_actor_get(uint32_t expected_clock, uint32_t roster_record,
                            Nfl2k5SandboxActor *out);
int nfl2k5_sandbox_actor_get_properties(uint32_t expected_clock, int id,
                                      Nfl2k5SandboxActorProperties *out);
/* Normal edits: changed ratings must be 0..99. The style slots (21,23,25)
 * are read-only: their selector semantics are not fully traced. Existing
 * out-of-range mod bytes can be preserved unchanged. */
int nfl2k5_sandbox_actor_set_properties(uint32_t expected_clock,
                                       const Nfl2k5SandboxActorProperties *in);
/* Trusted internal recovery ONLY: exact bytes from a captured baseline/undo.
 * This permits raw values >99 and must not be exposed to page commands. */
int nfl2k5_sandbox_actor_restore_properties(uint32_t expected_clock,
                                           const Nfl2k5SandboxActorProperties *in);
int nfl2k5_sandbox_actor_valid_properties(const Nfl2k5SandboxActorProperties *in);
const char *nfl2k5_sandbox_actor_rating_name(int index);
int nfl2k5_sandbox_actor_properties_selftest(void);
#ifdef __cplusplus
}
#endif
#endif

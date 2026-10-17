/* My NFL Eras: playing a league game (src/nfl2k5_eras_launch.c,
 * docs/eras/PHASE2-PLAN.md M4). Game thread only. */
#ifndef NFL2K5_ERAS_LAUNCH_H
#define NFL2K5_ERAS_LAUNCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "Play Next Game": launch the league's next fixture from a menu action
 * (ecx = the front-end FSM). 0 = Coach Match Up was pushed; otherwise nothing
 * is left changed and msg gets a short reason for the menu row. */
int nfl2k5_eras_launch(uint32_t fsm, char *msg, size_t n);
int nfl2k5_eras_launch_active(void);

/* The clean-up after a launched game (or a launch backed out of): commit or
 * log the result, release USER1/USER2, restore the settings snapshot. Does
 * nothing when no launch is active; safe to call more than once. A league that
 * says a game was launched but no launch is active in this process (a crash
 * between launch and clean-up) is noted in eras.log and its flag cleared. */
void nfl2k5_eras_after_game(const char *why);

/* Front-end FSM pop (src/main.c nfl2k5_diag_fsm): the clean-up runs once the
 * League screen, the hub or the Main Menu is on top again. */
void nfl2k5_eras_fsm_pop(uint32_t fsm, uint32_t top);

/* Gen patch ERAS_RETURN (sub_0034C1F0, the pop to the Main Menu after a
 * game): the League descriptor instead, when an Eras game was launched and
 * that descriptor is on the FSM stack; otherwise edx unchanged. */
uint32_t nfl2k5_eras_return_desc(uint32_t edx);

#ifdef __cplusplus
}
#endif

#endif

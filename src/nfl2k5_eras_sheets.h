/* My NFL Eras: Franchise's own Schedule screen, and Standings on the same
 * retail sheet engine (src/nfl2k5_eras_sheets.c,
 * docs/eras/research/FRANCHISE-SCREEN-COPY.md option (c), docs/eras/results/M7-schedule.md).
 * Game thread only; --retail only (served functions). */
#ifndef NFL2K5_ERAS_SHEETS_H
#define NFL2K5_ERAS_SHEETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_ERAS_SHEETS_BASE 0xFEE80000u   /* served functions (inside the menu's 0xFEE00000 range) */

/* Builds the descriptors and tables (once, at menu install). 0 = ready. */
int nfl2k5_eras_sheets_install(void);
/* The office rows use the copies unless NFL2K5_ERAS_SCHEDULE / NFL2K5_ERAS_STANDINGS = list. */
int nfl2k5_eras_sheets_schedule_on(void);
int nfl2k5_eras_sheets_standings_on(void);
/* Push the screens (deferred, as the game's own push); conference 0 AFC, 1 NFC. */
void nfl2k5_eras_sheets_push_schedule(uint32_t fsm);
void nfl2k5_eras_sheets_push_standings(uint32_t fsm, int conference);
/* The season block is filled with Eras data (only while one of the screens is on top). */
int nfl2k5_eras_sheets_swapped(void);

typedef void (*nfl2k5_eras_sheets_fn)(void);
nfl2k5_eras_sheets_fn nfl2k5_eras_sheets_lookup(uint32_t address);

#ifdef __cplusplus
}
#endif

#endif

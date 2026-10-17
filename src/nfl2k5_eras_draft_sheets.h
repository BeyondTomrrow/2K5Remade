/* My NFL Eras: the draft screens on Franchise's own sheet engine
 * (src/nfl2k5_eras_draft_sheets.c, docs/eras/results/M13-draft.md): The Big Board / the
 * candidates (the user's pick), and the draft order / recap, with the draft show for the picks as
 * they happen. Game thread only; --retail only (served functions). */
#ifndef NFL2K5_ERAS_DRAFT_SHEETS_H
#define NFL2K5_ERAS_DRAFT_SHEETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_ERAS_DRAFT_SHEETS_BASE 0xFEE90000u   /* served functions (inside the menu's 0xFEE00000 range) */

int nfl2k5_eras_draft_sheets_install(void);       /* once, at menu install; 0 = ready */
int nfl2k5_eras_draft_sheets_ready(void);
/* Open the draft (nfl2k5_eras_draft_open) and push the screen. 0 = pushed, else msg says why. */
int nfl2k5_eras_draft_sheets_push(uint32_t fsm, char *msg, unsigned n);

typedef void (*nfl2k5_eras_draft_sheets_fn)(void);
nfl2k5_eras_draft_sheets_fn nfl2k5_eras_draft_sheets_lookup(uint32_t address);

#ifdef __cplusplus
}
#endif
#endif

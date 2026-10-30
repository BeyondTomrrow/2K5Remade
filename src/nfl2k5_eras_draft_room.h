/* My NFL Eras: the draft screens as copies of Franchise's own draft screens
 * (src/nfl2k5_eras_draft_room.c, docs/eras/results/M15-draft-screens.md): The Big Board
 * (0x53BE3C), the Draft room candidates with the pick clock (0x5603E4) and the Draft Order
 * (the in-draft Draft Summary 0x560168), on the retail table handler, layouts and card bindings,
 * fed with Eras-owned prospect records in the retail player-record layout. The M13 sheet-engine
 * screens (nfl2k5_eras_draft_sheets.c) stay as the fallback. Game thread only; --retail only. */
#ifndef NFL2K5_ERAS_DRAFT_ROOM_H
#define NFL2K5_ERAS_DRAFT_ROOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_ERAS_DRAFT_ROOM_BASE 0xFEEA0000u   /* served functions (inside the menu's 0xFEE00000 range) */

int nfl2k5_eras_draft_room_install(void);         /* once, at menu install; 0 = ready */
int nfl2k5_eras_draft_room_ready(void);
/* Open the draft and push The Big Board (or the M13 sheets when the rookie_pane layout is not
 * resident or NFL2K5_ERAS_DRAFT_SCREENS=sheet). 0 = pushed, else msg says why. */
int nfl2k5_eras_draft_room_push(uint32_t fsm, char *msg, unsigned n);

typedef void (*nfl2k5_eras_draft_room_fn)(void);
nfl2k5_eras_draft_room_fn nfl2k5_eras_draft_room_lookup(uint32_t address);

#ifdef __cplusplus
}
#endif
#endif

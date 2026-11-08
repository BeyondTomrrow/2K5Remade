/* My NFL Eras: the office room (src/nfl2k5_eras_office.c,
 * docs/eras/ERAS-OFFICE-DESIGN.md). A copy of the Franchise office's machinery
 * with Eras' own package, state and camera stops; the menu screen that shows
 * it is in src/nfl2k5_eras_menu.c. Game thread only. */
#ifndef NFL2K5_ERAS_OFFICE_H
#define NFL2K5_ERAS_OFFICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Office rows that move the camera (each era has its own stop per kind). */
enum { OFFICE_STOP_PLAY, OFFICE_STOP_SCHEDULE, OFFICE_STOP_STANDINGS, OFFICE_STOP_TEAM,
       OFFICE_STOP_SETTINGS, OFFICE_STOP_KINDS };

/* Office looks; OFFICE_LOOK_AUTO = the league season's decade. */
enum { OFFICE_LOOK_AUTO = 0, OFFICE_LOOK_1970S, OFFICE_LOOK_1980S, OFFICE_LOOK_1990S,
       OFFICE_LOOK_2000S, OFFICE_LOOK_2010S, OFFICE_LOOK_2020S, OFFICE_LOOKS };

int nfl2k5_eras_office_init(void);          /* guest memory, strings; at menu install */
int nfl2k5_eras_office_ready(void);         /* the package files are in the archive (pack G) */
int nfl2k5_eras_office_load(void);          /* ERASOFFICE + ERASSKIN (synchronous); 0 = loaded */
void nfl2k5_eras_office_unload(void);

/* The screen's messages (ecx = FSM). */
void nfl2k5_eras_office_create(uint32_t fsm);    /* msg 1 */
void nfl2k5_eras_office_refresh(uint32_t fsm);   /* msg 3: the package is still there (reload if not) */
void nfl2k5_eras_office_shown(uint32_t fsm);     /* msg 4 */
void nfl2k5_eras_office_covered(void);           /* msg 5 */
int  nfl2k5_eras_office_frame(uint32_t fsm);     /* msg 6: returns a pending token when the camera arrived, else 0 */
void nfl2k5_eras_office_draw(uint32_t fsm);      /* msg 7 */
void nfl2k5_eras_office_back(void);              /* msg 10 */
void nfl2k5_eras_office_destroy(void);           /* msg 2 */

/* A row was chosen: sound, camera move to the era's stop for `kind`, and
 * `token` comes back from nfl2k5_eras_office_frame on arrival. Returns 0 when
 * there is no room (the caller acts at once). */
int nfl2k5_eras_office_go(int kind, int token);
int nfl2k5_eras_office_busy(void);               /* the camera is moving */
void nfl2k5_eras_office_return(void);            /* play the return move now (a refused launch) */
void nfl2k5_eras_office_leaving(void);           /* a match is launched: texture context and ambience off */
void nfl2k5_eras_office_free_for_match(void);    /* leaving() and unload the packages (before the launch) */
void nfl2k5_eras_office_back_from_match(void);   /* load them again (ERAS_RETURN / msg 3); no-op if not freed */

/* The look: the setting (saved in league.ini by the menu) and the live one
 * (LT/RT browsing, this session only). */
void nfl2k5_eras_office_set_look(int setting);
int nfl2k5_eras_office_look(void);               /* the effective look, OFFICE_LOOK_1970S.. */
int nfl2k5_eras_office_auto_look(void);          /* what Auto means for the league's season */
const char *nfl2k5_eras_office_look_name(int look);   /* "Auto", "1970s" .. */
int nfl2k5_eras_office_look_changed(void);       /* LT/RT changed the look since the last call */

#ifdef __cplusplus
}
#endif

#endif

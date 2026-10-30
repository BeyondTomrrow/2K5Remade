/* My NFL Eras: the draft show (src/nfl2k5_eras_draftshow.c, docs/eras/results/M11-draftshow.md).
 * A draft-day sequence on Franchise's Draft room stage (an Eras copy, never Franchise's own),
 * with the commissioner at the trophy-set podium, the suited player walking up, the pick card
 * and the "pick is in" audio. The menu screen that shows it is EM_DRAFT in
 * src/nfl2k5_eras_menu.c. Game thread only. */
#ifndef NFL2K5_ERAS_DRAFTSHOW_H
#define NFL2K5_ERAS_DRAFTSHOW_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int nfl2k5_eras_draftshow_ready(void);          /* erasdraft.iff is in the archive and draftposes.bin exists */
/* The screen's messages (ecx = FSM): 1 create, 2 destroy, 3 refresh, 4 shown, 5 covered,
 * 6 frame, 7 draw, 10 back. */
void nfl2k5_eras_draftshow_event(int msg, uint32_t fsm);
void nfl2k5_eras_draftshow_skip(void);           /* the row's A press: next pick */
/* The "Draft Show" setting: 0 Off, 1 First Round, 2 Your Picks. NFL2K5_ERAS_DRAFTSHOW (off / first /
 * mine) when set, else the league's league.ini draft_show (M12), else First Round. set_mode writes
 * league.ini (0, or -1 when no league is loaded or the switch is set). */
int nfl2k5_eras_draftshow_mode(void);
const char *nfl2k5_eras_draftshow_mode_name(int mode);
int nfl2k5_eras_draftshow_mode_forced(void);   /* 1 while NFL2K5_ERAS_DRAFTSHOW overrides the setting */
int nfl2k5_eras_draftshow_set_mode(int mode);
/* M13: the real draft's picks (src/nfl2k5_eras_draft_sheets.c). Picks added here are what the next
 * show plays (in order, already filtered by the setting); when the queue is done the show pops its
 * own screen. With an empty queue the screen plays picks_<year>.csv as before (the preview row). */
void nfl2k5_eras_draftshow_queue_clear(void);
int nfl2k5_eras_draftshow_queue_add(int round, int pick, int roundpick, const char *team_id, const char *player,
                                    const char *pos, const char *college);
int nfl2k5_eras_draftshow_queue_count(void);
int nfl2k5_eras_draftshow_queued(void);        /* a queued show is running */

#ifdef __cplusplus
}
#endif
#endif

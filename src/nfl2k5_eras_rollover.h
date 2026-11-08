/* My NFL Eras: the season rollover (M14): the league moves from its season to the next one. */
#ifndef NFL2K5_ERAS_ROLLOVER_H
#define NFL2K5_ERAS_ROLLOVER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0 = the office may offer "Start the <season+1> season"; else `msg` says why not (the Super Bowl not
 * played, the draft not complete, no package for the next season). */
int nfl2k5_eras_rollover_ready(char *msg, size_t n);
/* Archive the season under seasons\<season>\, build the next season's rosters (Historical: the
 * package's real rosters; Alternate History: the league's rosters with the draft's rookies, the real
 * retirements and the next-season community ratings from progression.csv), then switch the league.
 * 0 on success; `msg` gets a short reason otherwise. */
int nfl2k5_eras_rollover(char *msg, size_t n);
/* The season the next rollover would start (season + 1). */
int nfl2k5_eras_rollover_season(void);

#ifdef __cplusplus
}
#endif

#endif

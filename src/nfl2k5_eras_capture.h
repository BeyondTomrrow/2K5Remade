/* My NFL Eras: the result of a played game, read at the final whistle
 * (src/nfl2k5_eras_capture.c, docs/eras/PHASE2-PLAN.md M1). */
#ifndef NFL2K5_ERAS_CAPTURE_H
#define NFL2K5_ERAS_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NFL2K5_ERAS_CAPTURE_IDLE = 0,   /* nothing armed, nothing captured */
    NFL2K5_ERAS_CAPTURE_ARMED,      /* waiting for the final whistle */
    NFL2K5_ERAS_CAPTURE_FINAL,      /* captured; the files are written off the guest thread (see written) */
    NFL2K5_ERAS_CAPTURE_ABANDONED   /* match exit came first: no result */
};

typedef struct {
    int status;                     /* NFL2K5_ERAS_CAPTURE_* */
    char game_id[64];
    int home_points, away_points;
    int final_period;               /* 4 regulation, 5+ overtime */
    int end_state;                  /* flow state before 0x0A (0x15 clock expiry) */
    int mode;                       /* [0xE5FF80] at the whistle */
    volatile long written;          /* FINAL only: 0 still writing, 1 box and results row written, -1 failed */
} Nfl2k5ErasResult;

/* Arm the capture for one game (guest thread, before the match starts).
 * box_path: the box score JSON (never overwritten); results_csv: one line
 * game_id,home_pts,away_pts,final_period,end_state,status is appended. */
void nfl2k5_eras_capture_arm(const char *game_id, const char *box_path, const char *results_csv);
void nfl2k5_eras_capture_disarm(void);
/* A label written into the armed game's box score ("roster_source"), e.g.
 * "unverified community roster"; arm clears it. */
void nfl2k5_eras_capture_set_roster_source(const char *label);
/* The last armed game: its status and, once final, its score. */
void nfl2k5_eras_capture_result(Nfl2k5ErasResult *out);

/* Gen patches ERAS_GAME_FINAL / ERAS_MATCH_EXIT and their interpreter hooks. */
void nfl2k5_eras_game_final(uint32_t home_points, uint32_t away_points);
void nfl2k5_eras_match_exit(void);
/* Boot: NFL2K5_ERAS_CAPTURE=1 arms every game (test switch), NFL2K5_ERAS_LOG=1 logs [ERAS]. */
void nfl2k5_eras_capture_init(void);

#ifdef __cplusplus
}
#endif

#endif

/* My NFL Eras: the draft (src/nfl2k5_eras_draft.c, docs/eras/results/M13-draft.md).
 *
 * The class (mods\eras\draft\class_<year>.csv, tools/eras/drafts/make_draft_class.py: every real
 * pick with his rookie ratings and his real peak as potential; the never-rated picks estimated and
 * flagged), the order (Historical: the real slots; Alternate History: the league's standings, worst
 * first), the picks (CPU by the real pick or by need and value; the user's from the candidates
 * screen), all saved under the league folder (draft\<year>\class.csv, picks.csv), and the rookies
 * written into the next season's roster files (rosters\<year>\<id>.csv, depth\<year>\<id>.csv).
 * Game thread only. The season package is never written. */
#ifndef NFL2K5_ERAS_DRAFT_H
#define NFL2K5_ERAS_DRAFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_DRAFT_MAX_PROSPECTS 400
#define NFL2K5_DRAFT_MAX_SLOTS     400
#define NFL2K5_DRAFT_RATINGS       28

typedef struct {
    int pick, round;                 /* the real draft slot */
    int team;                        /* the real drafting team (Eras index), -1 if not in the league */
    char first[32], last[32], pos[6], college[40], source[16], flags[72];
    int pos_code, height, weight, skin, face, hand, number;
    int ovr, peak_ovr, peak_season, years_to_peak, games, av;
    uint8_t r[NFL2K5_DRAFT_RATINGS], p[NFL2K5_DRAFT_RATINGS];
    int sleeper;                     /* Alternate History: hidden star potential (never shown) */
    int taken_slot;                  /* -1 while available */
} ErasProspect;

typedef struct {
    int round, pick;                 /* round, overall pick number */
    int team;                        /* the team on the clock (Eras index) */
    int prospect;                    /* -1 until the pick is made */
    char by[6];                      /* "real", "cpu", "user" */
} ErasDraftSlot;

/* The draft year of the loaded league (the season after it). */
int nfl2k5_eras_draft_year(void);
/* Open the draft of the loaded league: the class (the league's copy, written on the first open with
 * the sleepers drawn), the order and the picks made so far. 0 = open; else nothing to show (msg says why). */
int nfl2k5_eras_draft_open(char *msg, unsigned n);
int nfl2k5_eras_draft_is_open(void);
void nfl2k5_eras_draft_close(void);

int nfl2k5_eras_draft_prospect_count(void);
const ErasProspect *nfl2k5_eras_draft_prospect(int i);
int nfl2k5_eras_draft_slot_count(void);
const ErasDraftSlot *nfl2k5_eras_draft_slot(int i);
int nfl2k5_eras_draft_round_count(void);
int nfl2k5_eras_draft_round_first(int round);     /* slot index of the round's first pick, -1 */
int nfl2k5_eras_draft_next_slot(void);            /* the first slot without a pick; -1 = complete */
int nfl2k5_eras_draft_user_on_clock(void);        /* the next slot belongs to the user's team */

/* Available prospects of a position group (0 all, 1 QB, 2 RB, 3 WR, 4 TE, 5 OL, 6 DL, 7 LB, 8 DB,
 * 9 K/P), by overall (the rookie ratings; potential is never used here), then by the real slot. */
int nfl2k5_eras_draft_available(int group, int *out, int max);
const char *nfl2k5_eras_draft_group_name(int group);
int nfl2k5_eras_draft_group_of(int pos_code);

/* Make the next pick for the CPU team on the clock (returns the slot index, -1 if none), or the
 * user's pick of prospect `prospect` (0 = made). Each pick is appended to picks.csv at once. */
int nfl2k5_eras_draft_cpu_pick(void);
int nfl2k5_eras_draft_user_pick(int prospect);
/* CPU picks until the user's team is on the clock or the draft is complete; the slots made go to
 * out[] (up to max). Returns the count. */
int nfl2k5_eras_draft_sim(int *out, int max);

/* The rookies into the next season's roster files, once the draft is complete (idempotent: the
 * marker draft\<year>\rosters_written). Returns the number of teams written, -1 if not complete. */
int nfl2k5_eras_draft_rosters_written(void);
int nfl2k5_eras_draft_write_rosters(void);

#ifdef __cplusplus
}
#endif
#endif

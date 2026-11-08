/* My NFL Eras: the playoffs (M14, docs/eras/results/M14-season-end.md). */
#ifndef NFL2K5_ERAS_PLAYOFFS_H
#define NFL2K5_ERAS_PLAYOFFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_PO_SEEDS 5        /* 1978-1989 format: 3 division winners + 2 wild cards per conference */

typedef struct {
    int team;                    /* league team index */
    int seed;                    /* 1..5 */
    int division_winner;
    char how[96];                /* the tiebreak that placed him (or "record") */
    int clinched;                /* bit 1 playoff berth, 2 division title, 4 home field (in-season picture) */
} ErasSeed;

/* The playoff picture "if the playoffs started today" (or the final field once the regular season
 * is complete): the five seeds of a conference (0 AFC, 1 NFC) in order, plus up to `max_hunt` more
 * conference teams in standings order ("in the hunt", seed 0). Returns the count written. */
int nfl2k5_eras_playoff_picture(int conference, ErasSeed *out, int max, int max_hunt);

/* Called from nfl2k5_eras_refresh: reads the league's playoffs.csv into the game list and, when a
 * round is complete, seeds / builds the next one (seeds.csv, playoffs.csv, eras.log). Returns 1 when
 * games were appended. */
int nfl2k5_eras_playoffs_update(void);

/* The package's postseason.csv row of a round (1 wild card .. 4 Super Bowl): date (YYYY-MM-DD),
 * label ("SUPER BOWL XV"), the Super Bowl's in-game site and designated home conference ("AFC"/"NFC").
 * Returns 0 when the row exists. */
int nfl2k5_eras_postseason_row(int round, char *date, size_t dn, char *label, size_t ln, char *site, size_t sn, char *home_conf, size_t hn);

/* Round names for titles: "Wild Card", "Divisional", "Conference Championship", "Super Bowl". */
const char *nfl2k5_eras_round_name(int round);
/* The Super Bowl's name for the season ("Super Bowl XV"); "" when postseason.csv lacks it. */
const char *nfl2k5_eras_super_bowl_name(void);
/* The champion (team index) once the Super Bowl is played, else -1. */
int nfl2k5_eras_champion(void);

/* The tiebreak procedure as text lines for the doc / log: writes the seeding explanation of both
 * conferences into `out` (multi-line). */
int nfl2k5_eras_playoffs_explain(char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif

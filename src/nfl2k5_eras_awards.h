/* My NFL Eras: season awards and league leaders from the league's own box scores (M14). */
#ifndef NFL2K5_ERAS_AWARDS_H
#define NFL2K5_ERAS_AWARDS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NFL2K5_AW_MAX_PLAYERS 2400
#define NFL2K5_AW_MAX_LEADERS 16

/* one player's regular-season totals summed over the league's box JSONs */
typedef struct {
    char first[24], last[24];
    int team, pos, games;
    int pass_yds, pass_td, pass_int, pass_comp, pass_att;
    int rush_yds, rush_td, rush_att;
    int rec, rec_yds, rec_td;
    int tackles, sacks, def_int, forced_fum;
    int fgm, fga, xpm;
    int points;                  /* the award scale (nfl2k5_eras_awards_rule) */
    int dpoints;
} ErasPlayerSeason;

typedef struct {
    const char *award;           /* "Most Valuable Player" ... */
    const ErasPlayerSeason *p;   /* NULL = no candidate (no box scores) */
    int coach_team;              /* Coach of the Year: team index (-1 otherwise) */
    char stat[64];               /* the line under the name */
} ErasAward;

/* Rebuild from box\<game_id>.json of every played regular-season game (cached per results count).
 * Returns the number of players summed, 0 when no box score exists. */
int nfl2k5_eras_awards_build(void);
int nfl2k5_eras_awards_count(void);
const ErasAward *nfl2k5_eras_award(int i);
/* The rule text (one line) for the doc and the screen. */
const char *nfl2k5_eras_awards_rule(void);
/* Leaders: category 0 passing yards, 1 rushing yards, 2 receiving yards, 3 touchdowns (rush + rec),
 * 4 tackles, 5 sacks, 6 interceptions, 7 field goals. Returns the count written (<= max). */
int nfl2k5_eras_leaders(int category, const ErasPlayerSeason **out, int max);
const char *nfl2k5_eras_leader_category(int category);
int nfl2k5_eras_leader_value(int category, const ErasPlayerSeason *p);
int nfl2k5_eras_leader_categories(void);
/* awards.csv in the league folder (written once the Super Bowl is played; idempotent). */
int nfl2k5_eras_awards_write(void);

#ifdef __cplusplus
}
#endif

#endif

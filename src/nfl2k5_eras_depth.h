/* My NFL Eras: depth charts and head coaches of staged teams
 * (src/nfl2k5_eras_depth.c, docs/eras/results/M6-depth.md). Game thread only. */
#ifndef NFL2K5_ERAS_DEPTH_H
#define NFL2K5_ERAS_DEPTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where a team's depth chart comes from. */
enum { ERAS_DEPTH_LIST = 0, ERAS_DEPTH_SEED = 1, ERAS_DEPTH_LEAGUE = 2 };

/* After nfl2k5_teams_stage(side, ...): the league's depth chart for `team`
 * (<league>\depth\<id>.csv), else the package seed (data\eras\runtime\<season>\
 * depth\<id>.csv), written into the staged players' rank / side fields.
 * Returns ERAS_DEPTH_*. */
int nfl2k5_eras_depth_apply(int side, int team);
/* Which file applies to the team, touching nothing. */
int nfl2k5_eras_depth_source(int team);
/* The starter (rank 0) at a position code from the file that applies, "" if none. */
void nfl2k5_eras_depth_starter(int team, int pos, char *out, size_t n);
/* The league's own depth chart moved aside (never deleted): the seed applies again. 1 = moved. */
int nfl2k5_eras_depth_reset(int team);

/* Depth Chart editing: stage `team` into a spare slot, apply its depth chart and
 * push the game's own Depth Chart screen (0x532D14) for it. 0 = pushed; else msg
 * says why (nothing changed). */
int nfl2k5_eras_depth_edit(uint32_t fsm, int team, char *msg, size_t n);
int nfl2k5_eras_depth_editing(void);
/* Back from the game's screen: the edited order into the league's depth file
 * (only when it changed), then the slot is released. Idempotent. Returns the
 * number of players whose rank/side changed (0 = nothing written), -1 when no
 * edit was open. */
int nfl2k5_eras_depth_edit_end(const char *why);

/* Head coaches (data\eras\runtime\<season>\coaches.csv): the staged team of
 * `side` gets its own coach record named for the team's coach of game number
 * `game_no` (1-16). Restore before the slot is released. */
int nfl2k5_eras_coach_apply(int side, int team, int game_no);
void nfl2k5_eras_coach_restore(int side);
/* The team's game number (1-based) of fixture `game` in schedule order. */
int nfl2k5_eras_team_game_number(int team, int game);

#ifdef __cplusplus
}
#endif

#endif

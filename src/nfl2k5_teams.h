/* Team slots: staging a My NFL Eras team into USER1/USER2 (src/nfl2k5_teams.c)
 * and the roster writer (src/nfl2k5_team_files.c). docs/eras/PHASE2-PLAN.md M2. */
#ifndef NFL2K5_TEAMS_H
#define NFL2K5_TEAMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reusable name arena, one per spare slot (side 0 USER1 = home, 1 USER2 = away):
 * 64 rows x {first, last} x 32 UTF-16 chars, then city, nickname, abbreviation. */
#define NFL2K5_ARENA_ROWS   64u
#define NFL2K5_ARENA_CHARS  32u
#define NFL2K5_ARENA_NAME(arena, row, k)  ((arena) + ((uint32_t)(row) * 2u + (uint32_t)(k)) * NFL2K5_ARENA_CHARS * 2u)
#define NFL2K5_ARENA_IDENT(arena, i)      (NFL2K5_ARENA_NAME(arena, NFL2K5_ARENA_ROWS, 0) + (uint32_t)(i) * NFL2K5_ARENA_CHARS * 2u)
#define NFL2K5_ARENA_BYTES  (NFL2K5_ARENA_ROWS * 2u * NFL2K5_ARENA_CHARS * 2u + 3u * NFL2K5_ARENA_CHARS * 2u)

uint32_t nfl2k5_team_arena(int side);
uint32_t nfl2k5_team_arena_str(uint32_t at, const char *s);
int nfl2k5_team_roster_file(const char *path, uint32_t slot, uint32_t arena);
void nfl2k5_team_roster(const char *rost_path, uint32_t slot);

/* M8 (docs/eras/results/M8-roster-size.md): a staged team carries 42..53 players,
 * the CSV's rows. 42 is the game's own minimum ("You must keep 42 players on
 * your team or you will not be able to take the field", Create a Team's "You
 * must have a larger player roster" below 42); 53 is what the disc templates
 * hold. Template players past the CSV's rows are removed with the game's own
 * remove-from-team (sub_000C3A90) and their records go back to the spare pool. */
#define NFL2K5_ROSTER_MIN   42

/* nfl2k5_teams_stage results */
enum {
    NFL2K5_STAGE_OK = 0,
    NFL2K5_STAGE_NO_ROSTER = -1,    /* the game's roster is not loaded yet */
    NFL2K5_STAGE_NO_SLOT = -2,      /* no USER1/USER2 slot for this side */
    NFL2K5_STAGE_USER_TEAM = -3,    /* the slot holds a Create-a-Team team: refused, nothing touched */
    NFL2K5_STAGE_TEMPLATE = -4,     /* the template (historic entry) is out of range or did not load */
    NFL2K5_STAGE_ROSTER_CSV = -5,   /* the roster file is missing, outside 42..53 rows, or short at a position */
    NFL2K5_STAGE_BAD_ARGS = -6
};

/* Stage one side: historic entry `template_entry` loaded into the side's spare
 * slot, the identity strings applied, then the roster CSV written over its
 * players. On any failure after loading, the slot is released again. Preserves
 * every guest register (callable from a menu action or a gen patch). Returns
 * NFL2K5_STAGE_*; nfl2k5_teams_staged_slot gives the slot. */
int nfl2k5_teams_stage(int side, int template_entry, const char *roster_csv, const char *city,
                       const char *nickname, const char *abbr);
uint32_t nfl2k5_teams_staged_slot(int side);
/* What nfl2k5_teams_stage would say about the side's slot, touching nothing:
 * NFL2K5_STAGE_OK, _USER_TEAM (a Create-a-Team team), _NO_SLOT or _NO_ROSTER. */
int nfl2k5_teams_stage_check(int side);
uint32_t nfl2k5_teams_spare_slot(int side);   /* USER1 (0) / USER2 (1), whatever it holds; 0 if none */
/* The n-th club (identity below 32, with players, not a spare slot) in table
 * order; 0 if none. */
uint32_t nfl2k5_teams_resident(int n);
/* A stadium record of the roster by its name (case and trailing spaces
 * ignored); 0 if none. *id_out gets its id byte (+0x7C). */
uint32_t nfl2k5_teams_stadium_find(const char *name, int *id_out);
uint32_t nfl2k5_teams_stadium_pack(const char *name, int *id_out);
void nfl2k5_teams_stadium_pack_restore(void);
/* Back to an empty created slot (category 2, identity 90/91), only if this
 * side's slot holds the team nfl2k5_teams_stage staged; a user-created team,
 * or a historic team Team Select loaded, is never touched. */
void nfl2k5_teams_release(int side);
/* A message for the menu's help bar. */
const char *nfl2k5_teams_stage_message(int code);

/* M2 test T2.2, from the result capture at the final whistle: every player of
 * the match team record (0xB30864 home, 0xB30A58 away) against the staged arena
 * rows. Logs and returns the mismatches (-1 when that side was not staged). */
int nfl2k5_teams_identity_check(int side, uint32_t match_team);
/* M8 test: the staged slot of `side` against its roster CSV, read again from the
 * file (names, position, jersey per row; player count). Logs; returns the
 * mismatches, or -1 when that side is not staged. */
int nfl2k5_teams_verify_csv(int side, const char *roster_csv);

#ifdef __cplusplus
}
#endif

#endif

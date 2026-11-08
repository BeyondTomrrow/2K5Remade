#ifndef NFL2K5_BROADCAST_H
#define NFL2K5_BROADCAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable boundary between NFL 2K5 state extraction and a selectable
 * broadcast package.  Packages consume these values; they never alter the
 * title's football simulation. */
typedef enum Nfl2k5BroadcastEvent {
    NFL2K5_EVENT_GAME_START,
    NFL2K5_EVENT_DRIVE_START,
    NFL2K5_EVENT_FIRST_DOWN,
    NFL2K5_EVENT_TOUCHDOWN,
    NFL2K5_EVENT_FIELD_GOAL,
    NFL2K5_EVENT_EXTRA_POINT,
    NFL2K5_EVENT_TWO_POINT_ATTEMPT,
    NFL2K5_EVENT_TURNOVER,
    NFL2K5_EVENT_INTERCEPTION,
    NFL2K5_EVENT_FUMBLE,
    NFL2K5_EVENT_SACK,
    NFL2K5_EVENT_PENALTY,
    NFL2K5_EVENT_TIMEOUT,
    NFL2K5_EVENT_INJURY,
    NFL2K5_EVENT_REPLAY_BEGIN,
    NFL2K5_EVENT_REPLAY_END,
    NFL2K5_EVENT_QUARTER_END,
    NFL2K5_EVENT_HALFTIME,
    NFL2K5_EVENT_QUARTER_START,
    NFL2K5_EVENT_GAME_END
} Nfl2k5BroadcastEvent;

typedef enum Nfl2k5StatKind {
    NFL2K5_STAT_QB,
    NFL2K5_STAT_RB,
    NFL2K5_STAT_RECEIVER,
    NFL2K5_STAT_DEFENSE,
    NFL2K5_STAT_KICKER
} Nfl2k5StatKind;

typedef struct Nfl2k5BroadcastTeamState {
    char abbreviation[8];
    char city[48];
    char name[48];
    char logo_path[260];
    char record[24];
    int score;
    int timeouts;
} Nfl2k5BroadcastTeamState;

typedef struct Nfl2k5BroadcastState {
    int valid;
    Nfl2k5BroadcastTeamState away, home;
    int possession;             /* 0 none, 1 away, 2 home */
    int quarter;
    float game_clock;
    int play_clock;             /* -1 outside a valid live scrimmage countdown */
    int down;
    int distance;
    int ball_on;
    int phase;
    int flag_state;
} Nfl2k5BroadcastState;

typedef struct Nfl2k5PlayerStat {
    Nfl2k5StatKind kind;
    const char *player_name;
    int completions, attempts, passing_yards, passing_touchdowns, interceptions;
    int carries, rushing_yards, rushing_touchdowns;
    int receptions, receiving_yards, receiving_touchdowns;
    int tackles, sacks, defensive_interceptions;
    int field_goals_made, field_goals_attempted, longest_field_goal;
    float display_seconds;
    /* Optional roster identity carried with a live stat sample.  These are
     * copied by the presentation layer before the guest callback returns. */
    const char *full_name;
    int photo_id;               /* -1 unknown; zero is a valid photo ID */
    int jersey_number;          /* -1 unknown; zero is a valid jersey */
} Nfl2k5PlayerStat;

void nfl2k5_broadcast_event(Nfl2k5BroadcastEvent event, int team);
void nfl2k5_broadcast_player_stat(const Nfl2k5PlayerStat *stat, int team);
/* Guest-context extraction publishes each team's current quarterback here.
 * The call copies the name and values before returning; no guest pointers
 * survive in the presentation cache. Team is 0 away, 1 home. The presenter
 * chooses possession and keeps the sample visible throughout live gameplay,
 * hiding it with the scorebug on play selection, pause and broadcast cuts.
 * A new-match/roster teardown may explicitly invalidate both cached samples. */
void nfl2k5_broadcast_live_qb_sample(const Nfl2k5PlayerStat *stat, int team);
void nfl2k5_broadcast_live_qb_reset(void);
int nfl2k5_broadcast_get_state(Nfl2k5BroadcastState *state);

/* Read-only current-match snapshots (scope 0). Values are doubles so the
 * title's 0.5-sack selector does not lose half credits. Only a set bit proves
 * a field was read successfully; unset values are NAN, never invented zero.
 * Indices are this interface's fields, NOT guest selector numbers. */
typedef enum Nfl2k5LiveStatField {
    NFL2K5_LIVE_PASS_COMPLETIONS,
    NFL2K5_LIVE_PASS_ATTEMPTS,
    NFL2K5_LIVE_PASS_YARDS,
    NFL2K5_LIVE_PASS_TOUCHDOWNS,
    NFL2K5_LIVE_PASS_INTERCEPTIONS,
    NFL2K5_LIVE_RUSH_ATTEMPTS,
    NFL2K5_LIVE_RUSH_YARDS,
    NFL2K5_LIVE_RUSH_TOUCHDOWNS,
    NFL2K5_LIVE_RECEPTIONS,
    NFL2K5_LIVE_RECEIVE_YARDS,
    NFL2K5_LIVE_RECEIVE_TOUCHDOWNS,
    NFL2K5_LIVE_TACKLES,
    NFL2K5_LIVE_SACKS,
    NFL2K5_LIVE_DEF_INTERCEPTIONS,
    NFL2K5_LIVE_FIELD_GOALS_MADE,
    NFL2K5_LIVE_FIELD_GOALS_ATTEMPTED,
    NFL2K5_LIVE_LONGEST_FIELD_GOAL,
    NFL2K5_LIVE_FIRST_DOWNS,
    NFL2K5_LIVE_POSSESSIONS,
    NFL2K5_LIVE_TOTAL_YARDS,
    NFL2K5_LIVE_TIME_OF_POSSESSION,
    NFL2K5_LIVE_FORCED_FUMBLES,
    NFL2K5_LIVE_FUMBLES_RECOVERED,
    NFL2K5_LIVE_TACKLES_FOR_LOSS,
    NFL2K5_LIVE_LONGEST_RUSH,
    NFL2K5_LIVE_LONGEST_RECEPTION,
    NFL2K5_LIVE_LONGEST_PASS,
    NFL2K5_LIVE_TURNOVERS,
    NFL2K5_LIVE_PENALTIES,
    NFL2K5_LIVE_PASS_YARDS_ALLOWED,
    NFL2K5_LIVE_RUSH_YARDS_ALLOWED,
    NFL2K5_LIVE_TOTAL_YARDS_ALLOWED,
    NFL2K5_LIVE_FIELD_COUNT
} Nfl2k5LiveStatField;

#define NFL2K5_LIVE_FIELD_BIT(field) (UINT64_C(1) << (unsigned)(field))

typedef enum Nfl2k5TimeOfPossessionUnit {
    NFL2K5_TOP_UNVERIFIED = 0,
    NFL2K5_TOP_SECONDS = 1
} Nfl2k5TimeOfPossessionUnit;

typedef struct Nfl2k5LiveStatOwner {
    uint32_t clock_object;
    uint32_t rosters[2];       /* away, home: bound team-data records */
    char abbreviations[2][16]; /* original playbook team code; empty if unreadable */
    uint32_t scope;            /* always 0: this match, never season/career */
    uint32_t period;
    uint64_t sampled_at_ms;    /* GetTickCount64, freshness not football time */
} Nfl2k5LiveStatOwner;

typedef struct Nfl2k5LivePlayerSample {
    Nfl2k5LiveStatOwner owner;
    int side;                 /* 0 away, 1 home */
    uint32_t player_record;
    uint32_t roster_index;
    uint32_t native_position; /* title's actual position ID, not StatKind */
    char name[48], full_name[80], position[8];
    int jersey_number, photo_id; /* -1 unknown */
    uint64_t valid_fields;
    double values[NFL2K5_LIVE_FIELD_COUNT];
} Nfl2k5LivePlayerSample;

typedef struct Nfl2k5LiveTeamSample {
    Nfl2k5LiveStatOwner owner;
    int side;                 /* 0 away, 1 home */
    uint32_t stat_block;
    Nfl2k5TimeOfPossessionUnit time_of_possession_unit;
    uint64_t valid_fields;
    double values[NFL2K5_LIVE_FIELD_COUNT];
} Nfl2k5LiveTeamSample;

/* Guest collector uses existing input/script service points only. These host
 * callbacks must copy the immutable stack record before returning; never
 * retain its pointer, invoke guest code or block on the guest CPU. The
 * optional-package gate avoids sampling unrelated presentation packages. */
int nfl2k5_broadcast_live_stats_enabled(void);
void nfl2k5_broadcast_live_player_sample(const Nfl2k5LivePlayerSample *sample);
void nfl2k5_broadcast_live_team_sample(const Nfl2k5LiveTeamSample *sample);
void nfl2k5_broadcast_live_stats_reset(void);

#ifdef __cplusplus
}
#endif
#endif

#ifndef NFL2K5_BROADCAST_H
#define NFL2K5_BROADCAST_H

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
    int photo_id;
    int jersey_number;
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

#ifdef __cplusplus
}
#endif
#endif

#ifndef NFL2K5_PRESENTATION_H
#define NFL2K5_PRESENTATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The existing popup hooks copy native text while the guest owns it.
 * what: 0 show, 1 begin native close. slot may be -1 before the title's
 * show routine allocates it. This boundary never changes popup state,
 * choice handling, timers or guest callbacks. */
void nfl2k5_presentation_coin_toss(const char *title, const char *message,
                                  float timeout, int slot, int what,
                                  uint32_t popup_context);

/* Owned copies of the highest-overall player on each loaded match roster.
 * The rating calculation runs only in an existing guest callback, never in
 * the presenter. Side order is away, home; missing data stays unavailable. */
typedef struct Nfl2k5FeaturedPlayer {
    char full_name[80];
    char position[8];
    int number, photo_id, overall;
} Nfl2k5FeaturedPlayer;
void nfl2k5_pregame_guest_roster_snapshot(void);
int nfl2k5_presentation_pregame_metadata_needed(void);
void nfl2k5_presentation_featured_players(uint32_t clock_object,
        const uint32_t rosters[2], const unsigned counts[2],
        const Nfl2k5FeaturedPlayer players[2]);

/* Copy a native single-player fact or stat table, including its actual
 * timeout. These functions do not close, hide or otherwise alter it. */
void nfl2k5_presentation_player_fact(uint32_t popup_context);
void nfl2k5_presentation_player_fact_hide(int slot);

/* Draw-only query for an existing native popup slot. It returns true only
 * after this package's page acknowledges the exact owned replacement.
 * No native draw hook is installed merely by declaring this boundary. */
int nfl2k5_presentation_pregame_draw_replaced(uint32_t popup_context);
void nfl2k5_presentation_native_popup_frame(uint32_t popup_context);
void nfl2k5_presentation_lineup_frame(uint32_t ticker_context);
int nfl2k5_presentation_lineup_draw_replaced(uint32_t ticker_context);
void nfl2k5_presentation_lineup_hide(uint32_t completed);
void nfl2k5_presentation_timeout_accepted(uint32_t team);
void nfl2k5_presentation_timeout_bug_frame(int visible);
void nfl2k5_presentation_timeout_committed(void);

/* Nestable, thread-local scope around a popup's ordinary scene-text draw.
 * Only the outgoing font colour's alpha is changed for an acknowledged
 * replacement; native text formatting and stored node alpha stay intact. */
void nfl2k5_presentation_native_text_begin(uint32_t popup_context);
void nfl2k5_presentation_native_text_end(void);
uint32_t nfl2k5_presentation_native_text_color(uint32_t argb);

/* Observations at the original automatic-replay FSM's stores.
 * Called only from guest execution. phase is the raw state (0..5), or 7
 * for the distinct replay-to-menu cancellation path. No camera, event,
 * transition, timing or guest state is changed by this observer. It releases
 * any page-owned visual transition when the native state completes/cancels. */
void nfl2k5_presentation_replay_observe(unsigned phase, uint32_t guest_pc);
/* Post-initialization visual ownership request, before original ambients.
 * returning: 0 entrance, 1 accepted return. No guest state is changed. */
void nfl2k5_presentation_replay_ready(unsigned returning, uint32_t guest_pc);
/* Only at screen119E0's private replay transition call sites. The delta
 * adapts the visual transition's clock to the acknowledged package motion;
 * the title retains midpoint/camera/actor/end/cleanup decisions. */
float nfl2k5_presentation_replay_delta(float original_delta);
int nfl2k5_presentation_replay_draw_replaced(void);
void nfl2k5_presentation_replay_handoff(void);
/* Observation only, after original successful match initialization and at
 * original exit. Keeps the HTML host warm across quiet match cameras. */
void nfl2k5_presentation_match_lifecycle(int entered);
/* Exact successful8ACF0 results from the review/ruling constructors.
 * slot=-1 is rejection; this observer never changes native review state. */
void nfl2k5_presentation_review_accepted(int slot, unsigned ruling);
/* Exact original sack decision; actor identities are candidates until the
 * native stat/graphic credit is verified. Diagnostic only. */
void nfl2k5_presentation_sack_observe(uint32_t actor_a, uint32_t actor_b, uint32_t guest_pc);
void nfl2k5_presentation_sack_event_record(uint32_t event_record, uint32_t guest_pc);
void nfl2k5_presentation_selected_player(unsigned selector, uint32_t player_record, uint32_t guest_pc);

#ifdef __cplusplus
}
#endif
#endif

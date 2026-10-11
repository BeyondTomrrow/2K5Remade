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

/* Copy only a native single-player CAREER table, including its actual
 * timeout. These functions do not close, hide or otherwise alter it. */
void nfl2k5_presentation_player_fact(uint32_t popup_context);
void nfl2k5_presentation_player_fact_hide(int slot);

/* Draw-only query for an existing native popup slot. It returns true only
 * after this package's page acknowledges the exact owned replacement.
 * No native draw hook is installed merely by declaring this boundary. */
int nfl2k5_presentation_pregame_draw_replaced(uint32_t popup_context);

#ifdef __cplusplus
}
#endif
#endif

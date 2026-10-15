/*
 * POSIX stand-in for src/nfl2k5_presentation.cpp.
 *
 * The real broadcast presentation (FOX/CBS/NBC packages, scorebugs, player
 * cards, broadcast music) renders with Direct2D/DirectWrite, decodes with
 * Media Foundation and plays through XAudio2, none of which exist on Linux.
 * Until it is ported, every entry point behaves as "no package installed":
 * the game keeps its original ESPN presentation and native scorebug.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "nfl2k5_broadcast.h"
#include "nfl2k5_presentation.h"

extern "C" {

int nfl2k5_broadcast_get_state(Nfl2k5BroadcastState *state)
{
    if (state) memset(state, 0, sizeof *state);
    return 0;
}
int nfl2k5_pres_package_count(void) { return 0; }
const char *nfl2k5_pres_package_name(int) { return ""; }
int nfl2k5_pres_get(int) { return 0; }
int nfl2k5_pres_values(int) { return 1; }
void nfl2k5_pres_value_name(int, int, char *buf, size_t n)
{
    if (buf && n) { strncpy(buf, "ESPN (original)", n - 1); buf[n - 1] = 0; }
}
void nfl2k5_pres_set(int, int) {}
void nfl2k5_broadcast_event(Nfl2k5BroadcastEvent, int) {}
void nfl2k5_broadcast_player_stat(const Nfl2k5PlayerStat *, int) {}
void nfl2k5_broadcast_live_qb_sample(const Nfl2k5PlayerStat *, int) {}
void nfl2k5_broadcast_live_qb_reset(void) {}
int nfl2k5_presentation_playercard(uint32_t, const char *) { return 0; }
void nfl2k5_scorebug_hide(void) {}
int nfl2k5_in_match(void) { return 0; }
void nfl2k5_scorebug_native_hook(void) {}
void nfl2k5_scorebug_native_place(uint32_t) {}
int nfl2k5_scorebug_skip_update(void) { return 0; }
int nfl2k5_scorebug_native_hidden(void) { return 0; }
void nfl2k5_presentation_popup(const char *) {}
int nfl2k5_presentation_pregame_metadata_needed(void) { return 0; }
void nfl2k5_presentation_popup_text(const char *, const char *) {}
void nfl2k5_presentation_player_fact_hide(int) {}
void nfl2k5_presentation_player_fact(uint32_t) {}
int nfl2k5_presentation_pregame_draw_replaced(uint32_t) { return 0; }
int nfl2k5_lineup_hide_native(void) { return 0; }
int nfl2k5_lineup_active(void) { return 0; }
void nfl2k5_lineup_ticker(uint32_t) {}
void nfl2k5_presentation_init(void) {}
void nfl2k5_presentation_coin_toss(const char *, const char *, float, int, int, uint32_t) {}
void nfl2k5_presentation_featured_players(uint32_t, const uint32_t[2], const unsigned[2],
                                          const Nfl2k5FeaturedPlayer[2]) {}

}

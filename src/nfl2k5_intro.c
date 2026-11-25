/*
 * nfl2k5_intro.c - boot intro movie policy.
 *
 * The boot intro movies are played by the title's own Sofdec MPEG decoder
 * (the movie player sub_00178150, gen patch NFL2K5-GENPATCH:SKIP_INTRO).
 * Decoding runs on the recompiled guest CPU, far below real time. The movie
 * player then steps through its five decoder frame buffers faster than they
 * fill, so most presented frames are buffers the decoder has not written yet
 * -- the screen flashes black between decoded frames.
 *
 * Windows does not hit this: the runtime hands movies to Media Foundation
 * instead of letting the guest decoder run, so there skipping stays opt-in.
 * Elsewhere skipping is the default and NFL2K5_SKIP_INTRO=0 re-enables
 * playback.
 */

#include <stdlib.h>

int nfl2k5_skip_boot_intro(void)
{
    const char *v = getenv("NFL2K5_SKIP_INTRO");

#if defined(_WIN32)
    return v && *v && *v != '0';
#else
    return !(v && *v == '0');
#endif
}

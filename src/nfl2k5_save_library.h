#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Keep the Xbox UDATA working tree mirrored into human-readable folders.
 * The worker also imports extracted Xbox save folders dropped into those
 * folders.  The game continues to use UDATA, so its save logic is unchanged. */
void nfl2k5_save_library_start(const char *save_root);

#ifdef __cplusplus
}
#endif


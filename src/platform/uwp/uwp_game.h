/* uwp_game.h -- the game inside the UWP shell (uwp_game.cpp). */
#ifndef NFL2K5_UWP_GAME_H
#define NFL2K5_UWP_GAME_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* core_window: the CoreWindow's IUnknown*. Starts the game thread. */
void uwp_game_start(void *core_window, const wchar_t *local_dir, const wchar_t *install_dir);
/* Test mode (unpackaged AppContainer run): an HWND instead of a CoreWindow. */
void uwp_game_start_hwnd(void *hwnd, const wchar_t *local_dir, const wchar_t *install_dir);
/* Called from the CoreWindow thread every few ms: gamepads -> SDL. */
void uwp_game_tick(void);
/* Nonzero once nfl2k5_real_main has returned. */
int uwp_game_exited(int *code);

#ifdef __cplusplus
}
#endif

#endif

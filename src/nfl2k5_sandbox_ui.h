#ifndef NFL2K5_SANDBOX_UI_H
#define NFL2K5_SANDBOX_UI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All three calls belong to the presenter thread. Input reaches the core's
 * bounded command queue; the WebView thread never accesses guest memory. */
void nfl2k5_sandbox_ui_tick(void);
/* Presenter supplies the actual game-picture bounds in framebuffer client
 * pixels, including letterboxing. Input translation checks exact game focus. */
void nfl2k5_sandbox_ui_viewport(float x, float y, float width, float height);
/* While true, do not also translate mouse buttons to gamepad A/B/Start. */
int nfl2k5_sandbox_ui_pointer_active(void);
int nfl2k5_sandbox_ui_surface(const uint8_t **bgra, int *width, int *height, int *stride);
void nfl2k5_sandbox_ui_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif

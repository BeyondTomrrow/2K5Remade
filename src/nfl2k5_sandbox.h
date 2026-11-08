#ifndef NFL2K5_SANDBOX_H
#define NFL2K5_SANDBOX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void nfl2k5_sandbox_begin(void);
void nfl2k5_sandbox_end(void);
void nfl2k5_sandbox_match_lifecycle(int entered);
/* Guest polling thread: 1 tools own input, 2 emit a native pause edge. */
int nfl2k5_sandbox_input(uint16_t buttons, const uint8_t analog[8],
    int16_t lx, int16_t ly, int16_t rx, int16_t ry);
int nfl2k5_sandbox_active(void);
int nfl2k5_sandbox_ui_open(void);
int nfl2k5_sandbox_ui_visible(void);
int nfl2k5_sandbox_hide_hud(void);
const char *nfl2k5_sandbox_json(void);
void nfl2k5_sandbox_command(const char *command, int value);
void nfl2k5_sandbox_command_for_generation(const char *command, int value, unsigned generation);
void nfl2k5_sandbox_shutdown(void);
int nfl2k5_sandbox_selftest(void);
#ifdef __cplusplus
}
#endif
#endif

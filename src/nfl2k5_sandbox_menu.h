#ifndef NFL2K5_SANDBOX_MENU_H
#define NFL2K5_SANDBOX_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nfl2k5_sandbox_menu_install(void);
void (*nfl2k5_sandbox_menu_lookup(uint32_t address))(void);
void nfl2k5_sandbox_menu_fsm_pop(uint32_t fsm, uint32_t top);
void nfl2k5_sandbox_menu_match_exit(void);
int nfl2k5_sandbox_menu_request_reselect(void);
int nfl2k5_sandbox_menu_quit_for_reselect(void);
void nfl2k5_sandbox_menu_service(void);
void nfl2k5_sandbox_menu_launch_accepted(void);
int nfl2k5_sandbox_menu_open(void);
int nfl2k5_sandbox_menu_validate(void);
int nfl2k5_sandbox_menu_installed(void);
uint32_t nfl2k5_sandbox_menu_header(void);

#ifdef __cplusplus
}
#endif
#endif

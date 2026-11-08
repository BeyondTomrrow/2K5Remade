/* uwp_probe.h -- run-time capability checks for the UWP/Xbox port (uwp_probe.c). */
#ifndef NFL2K5_UWP_PROBE_H
#define NFL2K5_UWP_PROBE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UWP_PROBE_MAX 32

typedef struct uwp_probe_result {
    int ok;
    char name[48];
    char detail[200];
} uwp_probe_result;

extern uwp_probe_result g_uwp_probe[UWP_PROBE_MAX];
extern int g_uwp_probe_count;

/* Runs every probe; writes <local_dir>\uwp-probe.txt. outside_path is a file
 * outside the package to try opening (informational), or NULL. */
void uwp_probe_run(const wchar_t *local_dir, const wchar_t *install_dir, const wchar_t *outside_path);

/* Adds one result from the C++ side (D3D12, gamepads, memory limit). */
void uwp_probe_note(const wchar_t *local_dir, const char *name, int ok, const char *detail);

#ifdef __cplusplus
}
#endif

#endif

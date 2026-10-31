/* uwp_xaml.h -- the XAML host mode of the UWP game (uwp_xaml_app.cpp) and the
 * EdgeHTML WebView presentation host that needs it (uwp_webview_host.cpp). */
#ifndef NFL2K5_UWP_XAML_H
#define NFL2K5_UWP_XAML_H

#ifdef __cplusplus
#include <memory>
#include "presentation/presentation_host.h"

/* Runs the app as a XAML application (SwapChainPanel for the game, room for
 * WebViews). Returns when the app exits. */
int uwp_xaml_run(void);

/* True once the XAML host is up (the WebView presentation host can start). */
bool uwp_xaml_active(void);

/* The HTML presentation host on the XAML WebView, or nullptr when the app
 * runs in plain CoreWindow mode. */
std::unique_ptr<nfl2k5::PresentationHost> uwp_create_webview_presentation_host(void);

extern "C" {
#endif

/* LocalState\xaml-mode.txt / crash marker logic: nonzero = start in XAML mode. */
int uwp_xaml_wanted(const wchar_t *local_dir);

#ifdef __cplusplus
}
#endif

#endif

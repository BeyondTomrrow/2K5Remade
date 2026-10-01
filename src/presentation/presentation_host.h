/* PresentationHost: runs an HTML/CSS/JavaScript broadcast package off screen
 * and hands back its transparent frames.
 *
 *   HTML/CSS/JS package  ->  PresentationHost  ->  PresentationSurface
 *                        ->  game HUD layer (renderer-independent CPU image)
 *                        ->  whichever backend presents the game
 *
 * Nothing here knows about a network, a scorebug design or a graphics API.
 * The game side posts state and events as JSON strings (the Presentation
 * API, docs/PRESENTATION-SYSTEM.md); the package decides how they look.
 *
 * The only implementation today is WebView2 (webview2_host.cpp). Another
 * engine or platform implements this same interface. */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace nfl2k5 {

/* One finished frame of the presentation canvas: BGRA, premultiplied alpha,
 * top-down rows. `serial` increases whenever the pixels change, so a consumer
 * re-uploads only new frames. */
struct PresentationSurface {
    const uint8_t *pixels = nullptr;
    int width = 0, height = 0, stride = 0;
    uint64_t serial = 0;
};

struct PresentationHostConfig {
    std::string package_dir;          /* e.g. mods/presentations/HTML_Test */
    std::string entry = "index.html"; /* page inside package_dir */
    std::string root_dir;             /* folder served as https://presentation.local/ */
    std::string teams_dir;            /* folder served as https://teams.local/ */
    int canvas_width = 1920;          /* logical CSS pixels */
    int canvas_height = 1080;
    float raster_scale = 1.0f;        /* device pixels per CSS pixel */
};

class PresentationHost {
public:
    virtual ~PresentationHost() = default;

    /* Starts the engine and loads the page; returns at once. Frames appear
     * when the page has rendered. */
    virtual bool start(const PresentationHostConfig &config) = 0;
    virtual void stop() = 0;

    /* Presentation API messages, already serialised as JSON objects. They are
     * delivered in order; state is coalesced (only the newest is kept if the
     * page is busy), events never are. */
    virtual void post_state(const std::string &json) = 0;
    virtual void post_event(const std::string &json) = 0;

    /* The newest frame, valid until the next call. Returns false until the
     * first frame exists. */
    virtual bool acquire_surface(PresentationSurface &out) = 0;

    /* Readable status for logs ("loading", "running", or an error). */
    virtual std::string status() const = 0;
};

std::unique_ptr<PresentationHost> create_webview2_presentation_host();

} // namespace nfl2k5

/* Independent transparent Sandbox browser. It shares the presentation engine,
 * never the selected broadcast package or its page/input state. */
#include "nfl2k5_sandbox_ui.h"
#include "presentation/presentation_host.h"
#include "nfl2k5_json.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

extern "C" const char *nfl2k5_sandbox_json(void);
extern "C" void nfl2k5_sandbox_command_for_generation(const char *command, int value, unsigned generation);
extern "C" int nfl2k5_sandbox_active(void);
extern "C" int nfl2k5_sandbox_ui_open(void);
extern "C" int nfl2k5_sandbox_ui_visible(void);
extern "C" void *xbox_FramebufferWindowHwnd(void);

namespace {
std::unique_ptr<nfl2k5::PresentationHost> s_host;
std::string s_last_state, s_last_status;
ULONGLONG s_last_post, s_last_attempt;
bool s_was_active;
struct Viewport { float x = 0, y = 0, w = 0, h = 0; } s_viewport;
std::atomic<bool> s_pointer_active{false};
bool s_pointer_engaged, s_pointer_release;
unsigned s_pointer_buttons, s_pointer_generation, s_pointer_sequence;
int s_pointer_x = -1, s_pointer_y = -1;
ULONGLONG s_pointer_post, s_pointer_click;
int s_click_x, s_click_y;

/* Only the host-owned metadata prefix is examined, never player row strings.
 * The core's JSON already escapes strings and publishes this numeric field. */
bool snapshot_generation(const std::string &state, unsigned &generation)
{
    size_t key = state.find("\"generation\":");
    size_t rows = state.find("\"rows\":");
    if (key == std::string::npos || (rows != std::string::npos && key > rows)) return false;
    const char *first = state.c_str() + key + strlen("\"generation\":");
    while (*first == ' ' || *first == '\t') ++first;
    if (*first < '0' || *first > '9') return false;
    char *end = nullptr;
    unsigned long long n = std::strtoull(first, &end, 10);
    if (n > 0xffffffffull || !end || (*end != ',' && *end != '}')) return false;
    generation = (unsigned)n;
    return true;
}

void pointer_reset(bool notify)
{
    if (notify && s_pointer_engaged && s_host) {
        char event[160];
        snprintf(event, sizeof event, "{\"type\":\"sandboxPointer\",\"generation\":%u,\"sequence\":%u,\"visible\":false}",
                 s_pointer_generation, ++s_pointer_sequence);
        s_host->post_event(event);
    }
    s_pointer_active = false;
    s_pointer_engaged = false;
    s_pointer_release = true;
    s_pointer_buttons = 0;
    s_pointer_x = s_pointer_y = -1;
    s_pointer_click = 0;
}

/* Sample every poll, independently of cursor/movement posting. Otherwise a
 * release inside the movement throttle leaves the previous button held and
 * swallows the next press. Opening/refocusing still requires a full release. */
unsigned pointer_button_edges(unsigned buttons)
{
    if (s_pointer_release && !buttons) s_pointer_release = false;
    unsigned pressed = s_pointer_release ? 0 : buttons & ~s_pointer_buttons;
    s_pointer_buttons = buttons;
    return pressed;
}

void pointer_tick(const std::string &state, ULONGLONG now)
{
    HWND window = (HWND)xbox_FramebufferWindowHwnd();
    unsigned gen = 0;
    POINT p;
    bool eligible = s_host && nfl2k5_sandbox_active() && nfl2k5_sandbox_ui_open() &&
        nfl2k5_sandbox_ui_visible() && window && GetForegroundWindow() == window &&
        s_viewport.w > 1 && s_viewport.h > 1 && snapshot_generation(state, gen) &&
        s_host->status() == "running";
    if (!eligible) { pointer_reset(true); return; }
    if (!s_pointer_engaged || s_pointer_generation != gen) {
        pointer_reset(false);
        s_pointer_engaged = true;
        s_pointer_generation = gen;
    }
    /* Capture ownership immediately, even while waiting for held buttons to
     * release: the same click must not leak through as native gamepad A. */
    s_pointer_active = true;
    unsigned buttons = (GetAsyncKeyState(VK_LBUTTON) & 0x8000 ? 1u : 0u) |
                       (GetAsyncKeyState(VK_RBUTTON) & 0x8000 ? 2u : 0u) |
                       (GetAsyncKeyState(VK_MBUTTON) & 0x8000 ? 4u : 0u);
    unsigned pressed = pointer_button_edges(buttons);
    if (!GetCursorPos(&p) || !ScreenToClient(window, &p)) return;
    float x = (p.x - s_viewport.x) * 1920.f / s_viewport.w;
    float y = (p.y - s_viewport.y) * 1080.f / s_viewport.h;
    bool inside = std::isfinite(x) && std::isfinite(y) && x >= 0 && x < 1920 && y >= 0 && y < 1080;
    if (!pressed && now - s_pointer_post < 33) return;
    if (!pressed && s_pointer_x == (inside ? (int)x : -1) && s_pointer_y == (inside ? (int)y : -1)) {
        return;
    }
    unsigned twice = 0;
    if (inside && (pressed & 1u)) {
        int dx = (int)x - s_click_x, dy = (int)y - s_click_y;
        if (s_pointer_click && now - s_pointer_click <= GetDoubleClickTime() && dx*dx + dy*dy <= 64) twice = 1;
        s_pointer_click = twice ? 0 : now;
        s_click_x = (int)x; s_click_y = (int)y;
    }
    char event[256];
    snprintf(event, sizeof event,
        "{\"type\":\"sandboxPointer\",\"generation\":%u,\"sequence\":%u,\"visible\":%s,\"x\":%d,\"y\":%d,\"pressed\":%u,\"doubleClick\":%s}",
        gen, ++s_pointer_sequence, inside ? "true" : "false", inside ? (int)x : -1, inside ? (int)y : -1,
        inside ? pressed : 0u, twice ? "true" : "false");
    s_host->post_event(event);
    s_pointer_post = now;
    s_pointer_x = inside ? (int)x : -1;
    s_pointer_y = inside ? (int)y : -1;
}

void page_message(const std::string &text)
{
    if (!nfl2k5_sandbox_active() || text.empty() || text.size() > 160) return;
    NjDoc d;
    if (nj_parse(&d, text.data(), text.size()) || d.nodes[0].type != NJ_OBJECT || d.count > 12) return;
    const char *cmd = nj_str(&d, 0, "cmd", "");
    size_t len = strlen(cmd);
    if (!len || len > 32) return;
    for (size_t i = 0; i < len; ++i)
        if ((cmd[i] < 'a' || cmd[i] > 'z') && cmd[i] != '_') return;
    double value = nj_num(&d, 0, "value", 0);
    if (!std::isfinite(value) || value < -1000000 || value > 1000000 || value != std::floor(value)) return;
    /* The core validates command names, ranges and current match generation. */
    double generation = nj_num(&d, 0, "generation", -1);
    if (!std::isfinite(generation) || generation < 0 || generation > 4294967295.0 || generation != std::floor(generation)) return;
    nfl2k5_sandbox_command_for_generation(cmd, (int)value, (unsigned)generation);
}

bool start_host()
{
    char root[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(sizeof root, root);
    if (!n || n >= sizeof root) return false;
    std::string package = std::string(root) + "\\mods\\sandbox\\ui";
    std::string entry = package + "\\index.html";
    if (GetFileAttributesA(entry.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[SANDBOX] Missing UI asset: %s\n", entry.c_str());
        return false;
    }
    nfl2k5::PresentationHostConfig cfg;
    cfg.root_dir = root;
    cfg.package_dir = package;
    cfg.entry = "index.html";
    cfg.canvas_width = 1920;
    cfg.canvas_height = 1080;
    cfg.on_page_message = page_message;
    auto host = nfl2k5::create_webview2_presentation_host();
    if (!host || !host->start(cfg)) return false;
    s_host = std::move(host);
    fprintf(stderr, "[SANDBOX] Browser started: %s\n", package.c_str());
    return true;
}
}

extern "C" void nfl2k5_sandbox_ui_tick(void)
{
    bool active = nfl2k5_sandbox_active() != 0;
    if (!active && !s_was_active) return;
    s_was_active = active;
    ULONGLONG now = GetTickCount64();
    if (!s_host) {
        if (!active || (s_last_attempt && now - s_last_attempt < 10000)) return;
        s_last_attempt = now;
        if (!start_host()) return;
    }
    const char *json = nfl2k5_sandbox_json();
    std::string state = json && json[0] ? json : "{\"active\":false}";
    if (state != s_last_state || now - s_last_post >= 500) {
        s_host->post_state(state);
        s_last_state = std::move(state);
        s_last_post = now;
    }
    pointer_tick(s_last_state, now);
    std::string status = s_host->status();
    if (status != s_last_status) {
        fprintf(stderr, "[SANDBOX] UI: %s\n", status.c_str());
        s_last_status = std::move(status);
    }
}

extern "C" void nfl2k5_sandbox_ui_viewport(float x, float y, float width, float height)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 1 || height <= 1 || width > 100000 || height > 100000) {
        s_viewport = {}; pointer_reset(true); return;
    }
    s_viewport = { x, y, width, height };
}

extern "C" int nfl2k5_sandbox_ui_pointer_active(void)
{
    HWND window = (HWND)xbox_FramebufferWindowHwnd();
    return s_pointer_active.load() && nfl2k5_sandbox_active() && nfl2k5_sandbox_ui_open() &&
        window && GetForegroundWindow() == window ? 1 : 0;
}

extern "C" int nfl2k5_sandbox_ui_surface(const uint8_t **bgra, int *width, int *height, int *stride)
{
    if (!bgra || !width || !height || !stride) return 0;
    *bgra = nullptr; *width = *height = *stride = 0;
    if (!s_host || !nfl2k5_sandbox_active() || !nfl2k5_sandbox_ui_visible()) return 0;
    nfl2k5::PresentationSurface surface;
    if (!s_host->acquire_surface(surface) || !surface.pixels || surface.width <= 0 || surface.height <= 0) return 0;
    *bgra = surface.pixels; *width = surface.width; *height = surface.height; *stride = surface.stride;
    return 1;
}

extern "C" void nfl2k5_sandbox_ui_shutdown(void)
{
    pointer_reset(false);
    if (s_host) { s_host->stop(); s_host.reset(); }
    s_last_state.clear(); s_last_status.clear();
    s_was_active = false; s_last_post = s_last_attempt = 0;
}

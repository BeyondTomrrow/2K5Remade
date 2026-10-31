/*
 * uwp_game.cpp -- runs the recompiled game inside the UWP shell (stage 2).
 *
 * The CoreWindow's thread (uwp_main.cpp) must keep dispatching window events,
 * so the game runs on a thread of its own with the 16 MB stack the desktop
 * exe links (/STACK:16777216), exactly as the Android port does. main.c is
 * compiled unchanged with main renamed to nfl2k5_real_main (uwp/CMakeLists).
 *
 * Bridges:
 *   - window: the CoreWindow is published as the SDL global property
 *     "SDL.window.uwp.corewindow"; the UWP-minimal SDL's D3D12 backend makes
 *     its swap chain on it (local SDL patch, tools/uwp/build-sdl-uwp.ps1).
 *   - input: Windows.Gaming.Input gamepads become SDL virtual joysticks of
 *     type GAMEPAD, so the game's SDL gamepad code works unchanged.
 *   - files: NFL2K5_ROOT = <LocalState>, or the folder named in
 *     <LocalState>\root.txt (e.g. E:\NFL2K5 on an Xbox USB drive, which
 *     must grant ALL APPLICATION PACKAGES access).
 *   - log: stdout/stderr go to <LocalState>\nfl2k5.log.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "uwp_game.h"

extern "C" int nfl2k5_real_main(int argc, char **argv);

using namespace winrt;
using namespace winrt::Windows::Gaming::Input;

namespace {

std::atomic<bool> s_exited{false};
std::atomic<int> s_exit_code{0};
std::string s_root;

std::string utf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

DWORD WINAPI game_thread(LPVOID)
{
    static char arg0[] = "NFL2K5.exe";
    char *argv[] = {arg0, nullptr};
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    fprintf(stderr, "[UWP] game thread: NFL2K5_ROOT=%s\n", s_root.c_str());
    int rc = nfl2k5_real_main(1, argv);
    fprintf(stderr, "[UWP] game returned %d\n", rc);
    fflush(stderr);
    s_exit_code = rc;
    s_exited = true;
    return 0;
}

/* ---- Windows.Gaming.Input -> SDL virtual gamepads ----------------------- */

struct Pad {
    Gamepad gp{nullptr};
    SDL_JoystickID id = 0;
    SDL_Joystick *js = nullptr;
};
std::vector<Pad> s_pads;

bool SDLCALL pad_rumble(void *userdata, Uint16 low, Uint16 high)
{
    Pad *p = static_cast<Pad *>(userdata);
    if (!p || !p->gp) return false;
    try {
        GamepadVibration v{};
        v.LeftMotor = low / 65535.0;
        v.RightMotor = high / 65535.0;
        p->gp.Vibration(v);
    } catch (...) {
        return false;
    }
    return true;
}

bool SDLCALL pad_trigger_rumble(void *userdata, Uint16 left, Uint16 right)
{
    Pad *p = static_cast<Pad *>(userdata);
    if (!p || !p->gp) return false;
    try {
        GamepadVibration v = p->gp.Vibration();
        v.LeftTrigger = left / 65535.0;
        v.RightTrigger = right / 65535.0;
        p->gp.Vibration(v);
    } catch (...) {
        return false;
    }
    return true;
}

void attach_pad(Gamepad const &gp)
{
    s_pads.reserve(16);
    if (s_pads.size() >= 16) return;
    s_pads.push_back(Pad{});
    Pad &p = s_pads.back();
    p.gp = gp;
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id = 0x045E;    /* Microsoft */
    d.product_id = 0x0B12;   /* Xbox Series controller: SDL picks the Xbox layout */
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.button_mask = (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_BACK) |
                    (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                    (1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                    (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) |
                    (1u << SDL_GAMEPAD_BUTTON_LEFT_STICK) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_STICK) |
                    (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) | (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                    (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT) |
                    (1u << SDL_GAMEPAD_BUTTON_GUIDE);
    d.name = "Xbox Controller (Windows.Gaming.Input)";
    d.userdata = &p;
    d.Rumble = pad_rumble;
    d.RumbleTriggers = pad_trigger_rumble;
    p.id = SDL_AttachVirtualJoystick(&d);
    if (p.id) p.js = SDL_OpenJoystick(p.id);
    fprintf(stderr, "[UWP] gamepad attached as SDL joystick %u (%s)\n", (unsigned)p.id, p.js ? "open" : SDL_GetError());
}

Sint16 axis(double v)
{
    double s = v * 32767.0;
    if (s > 32767.0) s = 32767.0;
    if (s < -32768.0) s = -32768.0;
    return (Sint16)s;
}

void poll_pads()
{
    if (!SDL_WasInit(SDL_INIT_JOYSTICK)) return;
    auto pads = Gamepad::Gamepads();
    /* attach new pads */
    for (uint32_t i = 0; i < pads.Size(); i++) {
        Gamepad gp = pads.GetAt(i);
        bool known = false;
        for (auto &p : s_pads) known |= p.gp == gp;
        if (!known) attach_pad(gp);
    }
    /* detach gone pads */
    for (auto &p : s_pads) {
        if (!p.gp) continue;
        bool present = false;
        for (uint32_t i = 0; i < pads.Size(); i++) present |= pads.GetAt(i) == p.gp;
        if (!present) {
            if (p.js) SDL_CloseJoystick(p.js);
            if (p.id) SDL_DetachVirtualJoystick(p.id);
            p = Pad{};
        }
    }
    for (auto &p : s_pads) {
        if (!p.js) continue;
        GamepadReading r = p.gp.GetCurrentReading();
        auto down = [&](GamepadButtons b) { return (r.Buttons & b) == b; };
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_LEFTX, axis(r.LeftThumbstickX));
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_LEFTY, axis(-r.LeftThumbstickY));
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_RIGHTX, axis(r.RightThumbstickX));
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_RIGHTY, axis(-r.RightThumbstickY));
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, axis(r.LeftTrigger * 2.0 - 1.0));
        SDL_SetJoystickVirtualAxis(p.js, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axis(r.RightTrigger * 2.0 - 1.0));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_SOUTH, down(GamepadButtons::A));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_EAST, down(GamepadButtons::B));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_WEST, down(GamepadButtons::X));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_NORTH, down(GamepadButtons::Y));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, down(GamepadButtons::LeftShoulder));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, down(GamepadButtons::RightShoulder));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_LEFT_STICK, down(GamepadButtons::LeftThumbstick));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_RIGHT_STICK, down(GamepadButtons::RightThumbstick));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_START, down(GamepadButtons::Menu));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_BACK, down(GamepadButtons::View));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_DPAD_UP, down(GamepadButtons::DPadUp));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_DPAD_DOWN, down(GamepadButtons::DPadDown));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_DPAD_LEFT, down(GamepadButtons::DPadLeft));
        SDL_SetJoystickVirtualButton(p.js, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, down(GamepadButtons::DPadRight));
    }
}

/* First line of <local>\<name> (UTF-8), or empty. */
std::wstring read_line_file(const std::wstring &local, const wchar_t *name)
{
    std::wstring path = local + L"\\" + name;
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return {};
    char buf[1024] = {};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    if (s.empty()) return {};
    int wn = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), wn);
    return w;
}

} // namespace

static void game_start(const char *prop, void *window, const wchar_t *local_dir, const wchar_t *install_dir)
{
    std::wstring local(local_dir);
    std::wstring log = local + L"\\nfl2k5.log";
    _wfreopen(log.c_str(), L"w", stderr);
    _wfreopen((local + L"\\nfl2k5-stdout.log").c_str(), L"w", stdout);
    setvbuf(stderr, nullptr, _IONBF, 0);

    std::wstring root = read_line_file(local, L"root.txt");
    if (root.empty()) root = local;
    s_root = utf8(root);
    _putenv_s("NFL2K5_ROOT", s_root.c_str());
    if (!getenv("RECOMP_RENDERER")) _putenv_s("RECOMP_RENDERER", "d3d12");   /* gx on SDL_GPU D3D12 */
    /* The UWP SDL has only the dummy video driver (the window is ours), and
     * SDL never picks that one by itself. */
    if (!getenv("SDL_VIDEO_DRIVER")) _putenv_s("SDL_VIDEO_DRIVER", "dummy");
    /* Saves, settings and logs always go to LocalState, even when root.txt
     * points at a read-only game folder: per-user mode, with LOCALAPPDATA set
     * to LocalState, puts the user folder at LocalState\NFL2K5-PC (main.c). */
    if (!getenv("NFL2K5_PORTABLE")) _putenv_s("NFL2K5_PORTABLE", "0");
    _wputenv_s(L"LOCALAPPDATA", local_dir);
    {   /* Song folders besides <root>\Music: LocalState\Music and the folder
         * named in LocalState\music.txt (e.g. a USB drive). */
        std::wstring music = local + L"\\Music";
        CreateDirectoryW(music.c_str(), nullptr);
        std::string extra = utf8(music);
        std::wstring more = read_line_file(local, L"music.txt");
        if (!more.empty()) extra += ";" + utf8(more);
        _putenv_s("NFL2K5_EXTRA_MUSIC", extra.c_str());
    }
    _putenv_s("NFL2K5_INSTALL_DIR", utf8(install_dir).c_str());
    _wputenv_s(L"NFL2K5_UWP_LOCALSTATE", local_dir);   /* SHGetFolderPathW shim */
    fprintf(stderr, "[UWP] NFL2K5 UWP build; install %s\n", utf8(install_dir).c_str());

    /* SDL reads this when its D3D12 backend makes the swap chain. */
    SDL_SetPointerProperty(SDL_GetGlobalProperties(), prop, window);

    HANDLE t = CreateThread(nullptr, 16u << 20, game_thread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) {
        SetThreadDescription(t, L"NFL2K5 game");
        CloseHandle(t);
    } else {
        fprintf(stderr, "[UWP] could not start the game thread (%lu)\n", GetLastError());
        s_exited = true;
    }
}

extern "C" void uwp_game_start(void *core_window, const wchar_t *local_dir, const wchar_t *install_dir)
{
    game_start("SDL.window.uwp.corewindow", core_window, local_dir, install_dir);
}

extern "C" void uwp_game_start_hwnd(void *hwnd, const wchar_t *local_dir, const wchar_t *install_dir)
{
    game_start("SDL.window.uwp.hwnd", hwnd, local_dir, install_dir);
}

extern "C" void uwp_game_tick(void)
{
    try {
        poll_pads();
    } catch (...) {
    }
}

extern "C" int uwp_game_exited(int *code)
{
    if (code) *code = s_exit_code;
    return s_exited ? 1 : 0;
}

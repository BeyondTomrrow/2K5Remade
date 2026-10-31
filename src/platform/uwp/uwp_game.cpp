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
#include <csignal>
#include <cstring>
#include <exception>
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

/* ---- CoreWindow keys -> SDL keyboard state ------------------------------
 * The game reads the keyboard with SDL_GetKeyboardState, but the UWP SDL has
 * only the dummy video driver, which never sees a key. The window's KeyDown/
 * KeyUp land in g_uwp_keys (Windows virtual-key codes); changes go to SDL's
 * internal key entry point (the static UWP SDL links it in). */
extern "C" volatile unsigned char g_uwp_keys[256];   /* uwp_win32_shim.c */
extern "C" bool SDL_SendKeyboardKey(Uint64 timestamp, SDL_KeyboardID keyboardID, int rawcode, SDL_Scancode scancode, bool down);

SDL_Scancode vk_to_scancode(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0') return SDL_SCANCODE_0;
    if (vk >= VK_F1 && vk <= VK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return (SDL_Scancode)(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD1));
    switch (vk) {
    case VK_RETURN: return SDL_SCANCODE_RETURN;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_SHIFT: case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_CONTROL: case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_MENU: case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_END: return SDL_SCANCODE_END;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
    case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
    case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
    case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
    case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
    case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
    case VK_OEM_2: return SDL_SCANCODE_SLASH;
    case VK_OEM_3: return SDL_SCANCODE_GRAVE;
    case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
    case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
    case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
    case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
    }
    return SDL_SCANCODE_UNKNOWN;
}

void forward_keys()
{
    static unsigned char sent[256];
    if (!SDL_WasInit(SDL_INIT_EVENTS)) return;
    for (int vk = 1; vk < 256; vk++) {
        unsigned char down = g_uwp_keys[vk] ? 1 : 0;
        if (down == sent[vk]) continue;
        sent[vk] = down;
        SDL_Scancode sc = vk_to_scancode(vk);
        if (sc != SDL_SCANCODE_UNKNOWN) SDL_SendKeyboardKey(0, 0 /* global keyboard */, vk, sc, down != 0);
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

/* Return addresses of the calling thread as module+offset, for the PDB
 * beside build-uwp\game\NFL2K5.exe. */
void log_backtrace()
{
    void *frames[48];
    USHORT n = RtlCaptureStackBackTrace(1, 48, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)frames[i], &mod) && mod) {
            GetModuleFileNameA(mod, name, sizeof name);
            const char *base = strrchr(name, '\\');
            fprintf(stderr, "[UWP]   #%u %s+0x%llX\n", i, base ? base + 1 : name,
                    (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)mod));
        } else {
            fprintf(stderr, "[UWP]   #%u %p\n", i, frames[i]);
        }
    }
    fflush(stderr);
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
    {   /* LocalState\env.txt: NAME=VALUE lines, the environment variables a
         * desktop test would set (a packaged app gets no command line or
         * environment from its launcher). '#' starts a comment line. */
        FILE *f = _wfopen((local + L"\\env.txt").c_str(), L"rb");
        char line[1024];
        while (f && fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
            char *eq = strchr(line, '=');
            if (!n || line[0] == '#' || !eq || eq == line) continue;
            *eq = 0;
            _putenv_s(line, eq + 1);
            fprintf(stderr, "[UWP] env.txt: %s=%s\n", line, eq + 1);
        }
        if (f) fclose(f);
    }
    /* A packaged app that dies leaves no console and often no WER report:
     * say in the log which way it went. */
    atexit([] { fprintf(stderr, "[UWP] exit() called\n"); fflush(stderr); });
    signal(SIGABRT, [](int) { fprintf(stderr, "[UWP] abort()\n"); fflush(stderr); });
    std::set_terminate([] { fprintf(stderr, "[UWP] std::terminate (uncaught C++ exception)\n"); log_backtrace(); abort(); });
    /* The release CRT ends the process with a fail-fast (no exception, no
     * report) when a checked function gets a bad argument (an _s function's
     * buffer too small, a NULL FILE*...). */
    _set_invalid_parameter_handler([](const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t) {
        fprintf(stderr, "[UWP] CRT invalid parameter: the CRT would fail-fast here\n");
        log_backtrace();
        TerminateProcess(GetCurrentProcess(), 0xC0000417);
    });
    /* The fourth boot movie is never decoded here (grey, then black, for good;
     * cause not found yet, 2026-10-31): skip the intro unless env.txt says
     * NFL2K5_SKIP_INTRO=0. */
    if (!getenv("NFL2K5_SKIP_INTRO")) _putenv_s("NFL2K5_SKIP_INTRO", "1");
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
    static bool audio_logged;
    if (!audio_logged && SDL_WasInit(SDL_INIT_AUDIO)) {
        audio_logged = true;
        const char *d = SDL_GetCurrentAudioDriver();
        fprintf(stderr, "[UWP] SDL audio driver: %s\n", d ? d : "(none)");
    }
    try {
        poll_pads();
    } catch (...) {
    }
    forward_keys();
}

extern "C" int uwp_game_exited(int *code)
{
    if (code) *code = s_exit_code;
    return s_exited ? 1 : 0;
}

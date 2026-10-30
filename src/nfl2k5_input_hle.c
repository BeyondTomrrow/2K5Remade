/*
 * XInput HLE for NFL 2K5 (Unified SDL3 Backend).
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "xinput_xbox.h"
#include "nfl2k5_sandbox.h"
#include "nfl2k5_sandbox_ui.h"

#define NFL2K5_XPP_GAMEPAD_TYPE 0x004DA614u
#define NFL2K5_XINPUT_HANDLE    0x7FFE0000u  /* | port */

extern void *xbox_FramebufferWindowHwnd(void);
extern unsigned nfl2k5_controls_ports(void);
extern int nfl2k5_controls_get(int port, XBOX_GAMEPAD *g, int use_pads);
extern void nfl2k5_controls_rumble(int port, unsigned left, unsigned right);

static Uint64 s_first_poll_ms;

static int input_hle_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("NFL2K5_INPUT_HLE");
        on = !v || (*v && *v != '0');
    }
    return on;
}

static int is_our_handle(uint32_t h)
{
    return (h & 0xFFFF0000u) == NFL2K5_XINPUT_HANDLE;
}

static WORD auto_press(BYTE analog[8])
{
    static char spec[256];
    static int loaded;
    WORD buttons = 0;
    double t;
    const char *p;

    if (!loaded) {
        const char *v = getenv("NFL2K5_AUTO_PRESS");
        loaded = 1;
        if (v) strncpy(spec, v, sizeof spec - 1);
    }
    if (!spec[0] || !s_first_poll_ms)
        return 0;
    t = (double)(SDL_GetTicks() - s_first_poll_ms) / 1000.0;
    for (p = spec; *p; ) {
        char name[16] = {0};
        double at = 0;
        int n = 0;
        if (sscanf(p, "%15[a-z]@%lf%n", name, &at, &n) < 2 || n <= 0)
            break;
        if (t >= at && t < at + 0.3) {
            if (!strcmp(name, "start")) buttons |= 0x10;
            else if (!strcmp(name, "back")) buttons |= 0x20;
            else if (!strcmp(name, "up")) buttons |= 0x01;
            else if (!strcmp(name, "down")) buttons |= 0x02;
            else if (!strcmp(name, "left")) buttons |= 0x04;
            else if (!strcmp(name, "right")) buttons |= 0x08;
            else if (!strcmp(name, "a")) analog[0] = 255;
            else if (!strcmp(name, "b")) analog[1] = 255;
            else if (!strcmp(name, "x")) analog[2] = 255;
            else if (!strcmp(name, "y")) analog[3] = 255;
        }
        p += n;
        if (*p == ',') p++;
    }
    return buttons;
}

static Uint64 g_last_script_press;

/* NFL2K5_NO_HOST_PAD=1 ignores host controllers for 15 s after each scripted
 * press (see host_gamepad). NFL2K5_NO_HOST_PAD=always never reads or rumbles
 * them: unattended background tests (RECOMP_BACKGROUND) while someone plays
 * another game on this PC with a pad -- the 15 s rule let that pad drive the
 * hidden test game between scripted presses (2026-10-15). */
static int host_pad_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *v = getenv("NFL2K5_NO_HOST_PAD");
        mode = !v ? 0 : SDL_strcasecmp(v, "always") == 0 ? 2 : 1;
    }
    return mode;
}

static int host_pads_ignored(void)
{
    int mode = host_pad_mode();
    if (mode == 2) return 1;
    return mode == 1 && g_last_script_press && SDL_GetTicks() - g_last_script_press < 15000;
}

static WORD file_press(BYTE analog[8])
{
    static char queue[64][8];
    static int head, tail;
    static Uint64 next_check, step_start;
    static int pressing;
    static const char *path;
    static int looked;

    if (!looked) { path = getenv("NFL2K5_PRESS_FILE"); looked = 1; }
    Uint64 now = SDL_GetTicks();
    WORD buttons = 0;

    if (!path || !*path)
        return 0;
    if (now >= next_check) {
        FILE *f;
        next_check = now + 100;
        f = fopen(path, "r");
        if (f) {
            char buf[512], *tok, *ctx = NULL;
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            remove(path);
            for (tok = SDL_strtok_r(buf, " ,\r\n\t", &ctx); tok; tok = SDL_strtok_r(NULL, " ,\r\n\t", &ctx)) {
                if (strlen(tok) < 8 && (tail + 1) % 64 != head) {
                    strcpy(queue[tail], tok);
                    tail = (tail + 1) % 64;
                }
            }
        }
    }
    if (head == tail)
        return 0;
    g_last_script_press = now;
    if (!step_start)
        step_start = now;
    /* NFL2K5_PRESS_HOLD_MS: longer presses for a slow test machine (an emulated
     * ARM64 VM draws a frame a second; a 200 ms press fell between polls) */
    static int hold = -1;
    if (hold < 0) { const char *h = getenv("NFL2K5_PRESS_HOLD_MS"); hold = h && atoi(h) > 0 ? atoi(h) : 200; }
    if (!pressing && now - step_start >= (Uint64)(hold + 50)) {
        pressing = 1;
        step_start = now;
    }
    if (pressing) {
        const char *name = queue[head];
        if (!strcmp(name, "start")) buttons |= 0x10;
        else if (!strcmp(name, "back")) buttons |= 0x20;
        else if (!strcmp(name, "back+r3")) buttons |= 0xA0;      /* the audio report combo */
        else if (!strcmp(name, "up")) buttons |= 0x01;
        else if (!strcmp(name, "down")) buttons |= 0x02;
        else if (!strcmp(name, "left")) buttons |= 0x04;
        else if (!strcmp(name, "right")) buttons |= 0x08;
        else if (!strcmp(name, "a")) analog[0] = 255;
        else if (!strcmp(name, "b")) analog[1] = 255;
        else if (!strcmp(name, "x")) analog[2] = 255;
        else if (!strcmp(name, "y")) analog[3] = 255;
        else if (!strcmp(name, "black") || !strcmp(name, "rb")) analog[4] = 255;
        else if (!strcmp(name, "white") || !strcmp(name, "lb")) analog[5] = 255;
        else if (!strcmp(name, "lt")) analog[6] = 255;
        else if (!strcmp(name, "rt")) analog[7] = 255;
        else if (!strcmp(name, "l3")) buttons |= 0x40;
        else if (!strcmp(name, "r3")) buttons |= 0x80;
        if (now - step_start >= (Uint64)hold) {
            pressing = 0;
            step_start = now;
            head = (head + 1) % 64;
        }
    }
    return buttons;
}

static volatile Uint64 s_inject_a_until;
void nfl2k5_input_press_a(unsigned ms) { s_inject_a_until = SDL_GetTicks() + ms; }

static int window_focused(void)
{
    SDL_Window *win = (SDL_Window *)xbox_FramebufferWindowHwnd();
    if (!win) return 0;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static int mouse_hle_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("NFL2K5_MOUSE");
        on = !v || (*v && *v != '0');
    }
    return on;
}

static void host_mouse_gamepad(XBOX_GAMEPAD *g)
{
    SDL_Window *win = (SDL_Window *)xbox_FramebufferWindowHwnd();
    float mx = 0.0f, my = 0.0f;
    int ww = 0, wh = 0, numkeys = 0;
    const bool *kbd;
    SDL_MouseButtonFlags mb;

#if defined(_WIN32)
    /* Sandbox's HTML panel owns the pointer: do not also send A/B/Start. */
    if (nfl2k5_sandbox_ui_pointer_active())
        return;
#endif

    if (!mouse_hle_on() || !win || !window_focused())
        return;

    mb = SDL_GetMouseState(&mx, &my);
    if (mb & SDL_BUTTON_LMASK) g->bAnalogButtons[0] = 255;
    if (mb & SDL_BUTTON_RMASK) g->bAnalogButtons[1] = 255;
    if (mb & SDL_BUTTON_MMASK) g->wButtons |= 0x10;

    kbd = SDL_GetKeyboardState(&numkeys);
    if (!kbd || !(kbd[SDL_SCANCODE_LSHIFT] || kbd[SDL_SCANCODE_RSHIFT]))
        return;

    SDL_GetWindowSize(win, &ww, &wh);
    if (ww <= 0 || wh <= 0)
        return;

    float cx = ww * 0.5f, cy = wh * 0.5f;
    float dx = mx - cx, dy = cy - my;
    if (fabsf(dx) > cx * 0.15f)
        g->sThumbLX = (SHORT)((dx * 32767.0f) / cx);
    if (fabsf(dy) > cy * 0.15f)
        g->sThumbLY = (SHORT)((dy * 32767.0f) / cy);
}

/* Android: the on-screen controls (src/platform/android/touch_pad.c). */
static void host_touch_gamepad(XBOX_GAMEPAD *g)
{
#if defined(__ANDROID__)
    extern void nfl2k5_touch_pad_merge(uint16_t *buttons, uint8_t analog[8], int16_t *lx, int16_t *ly);
    uint16_t b = g->wButtons;
    int16_t lx = g->sThumbLX, ly = g->sThumbLY;
    nfl2k5_touch_pad_merge(&b, g->bAnalogButtons, &lx, &ly);
    g->wButtons = b; g->sThumbLX = lx; g->sThumbLY = ly;
#else
    (void)g;
#endif
}

static void host_keyboard_gamepad(XBOX_GAMEPAD *g)
{
    int numkeys = 0;
    const bool *kbd;
    if (!window_focused()) return;
    kbd = SDL_GetKeyboardState(&numkeys);
    if (!kbd) return;

#define KEY(sc) ((int)(sc) < numkeys && kbd[sc])
    if (KEY(SDL_SCANCODE_RETURN))    g->wButtons |= 0x10;
    if (KEY(SDL_SCANCODE_BACKSPACE)) g->wButtons |= 0x20;
    if (KEY(SDL_SCANCODE_UP))        g->wButtons |= 0x01;
    if (KEY(SDL_SCANCODE_DOWN))      g->wButtons |= 0x02;
    if (KEY(SDL_SCANCODE_LEFT))      g->wButtons |= 0x04;
    if (KEY(SDL_SCANCODE_RIGHT))     g->wButtons |= 0x08;
    if (KEY(SDL_SCANCODE_Z) || KEY(SDL_SCANCODE_SPACE)) g->bAnalogButtons[0] = 255;
    if (KEY(SDL_SCANCODE_X)) g->bAnalogButtons[1] = 255;
    if (KEY(SDL_SCANCODE_C)) g->bAnalogButtons[2] = 255;
    if (KEY(SDL_SCANCODE_V)) g->bAnalogButtons[3] = 255;
    if (KEY(SDL_SCANCODE_Q)) g->bAnalogButtons[6] = 255;
    if (KEY(SDL_SCANCODE_E)) g->bAnalogButtons[7] = 255;
    if (KEY(SDL_SCANCODE_A)) g->sThumbLX = -32767;
    if (KEY(SDL_SCANCODE_D)) g->sThumbLX = 32767;
    if (KEY(SDL_SCANCODE_W)) g->sThumbLY = 32767;
    if (KEY(SDL_SCANCODE_S)) g->sThumbLY = -32767;
#undef KEY
}

/* Back + R3: save the last 90 s of audio and the diagnostics (F9 on the
 * keyboard; docs/AUDIO-REPORT-HOTKEY-20261022.md). While the combination is
 * held neither button reaches the game, and R3 does not skip the song. */
static void audio_report_combo(XBOX_GAMEPAD *g)
{
    static int held;
    extern void xbox_AudioReportRequest(void);
    if ((g->wButtons & 0xA0) == 0xA0) {
        if (!held) xbox_AudioReportRequest();
        held = 1;
    } else if (!(g->wButtons & 0xA0)) {
        held = 0;
    }
    if (held) g->wButtons &= (WORD)~0xA0;
}

static void host_gamepad_port(int port, XBOX_GAMEPAD *g)
{
    extern volatile long g_xbox_input_blocked;
    memset(g, 0, sizeof *g);
    if (g_xbox_input_blocked)
        return;
    nfl2k5_controls_get(port, g, !host_pads_ignored());
    if (port == 0) {
        host_keyboard_gamepad(g);
        host_mouse_gamepad(g);
        host_touch_gamepad(g);
        g->wButtons |= auto_press(g->bAnalogButtons);
        g->wButtons |= file_press(g->bAnalogButtons);
    }
    {
        extern void nfl2k5_cs_input(int port, XBOX_GAMEPAD *g);
        nfl2k5_cs_input(port, g);
    }
    if (port != 0)
        return;
    audio_report_combo(g);
    {   /* L3: show what is playing; R3: next song (local music library). */
        extern void nfl2k5_local_music_buttons(int l3, int r3);
        nfl2k5_local_music_buttons((g->wButtons & 0x40) != 0, (g->wButtons & 0x80) != 0);
    }
    if (SDL_GetTicks() < s_inject_a_until) g->bAnalogButtons[0] = 255;
}

static void host_gamepad(XBOX_GAMEPAD *g)
{
    XBOX_INPUT_STATE pad;
    extern volatile long g_xbox_input_blocked;
    static int legacy = -1;
    if (legacy < 0) legacy = getenv("NFL2K5_LEGACY_INPUT") != NULL;
    if (!legacy) { host_gamepad_port(0, g); return; }
    memset(g, 0, sizeof *g);
    if (g_xbox_input_blocked)
        return;
    /* NFL2K5_NO_HOST_PAD=1 ignores host controllers while a script is
     * driving: scripted runs on a machine with a pad attached got stray d-pad
     * presses that walked the menu cursor away from what the script selected
     * (2026-09-25). Only for 15 s after the last scripted press, though --
     * ignoring the pad for the whole run left the user on the keyboard in
     * every game a script had launched (2026-10-01). */
    if (!host_pads_ignored() && xbox_InputGetState(0, &pad) == 0)
        *g = pad.Gamepad;
    host_keyboard_gamepad(g);
    host_mouse_gamepad(g);
    host_touch_gamepad(g);
    g->wButtons |= auto_press(g->bAnalogButtons);
    g->wButtons |= file_press(g->bAnalogButtons);
    audio_report_combo(g);
    {   /* L3: show what is playing; R3: next song (local music library). */
        extern void nfl2k5_local_music_buttons(int l3, int r3);
        nfl2k5_local_music_buttons((g->wButtons & 0x40) != 0, (g->wButtons & 0x80) != 0);
    }
    if (SDL_GetTicks() < s_inject_a_until) g->bAnalogButtons[0] = 255;
}

static void put_gamepad(uint32_t va, const XBOX_GAMEPAD *g)
{
    int i;
    MEM16(va) = g->wButtons;
    for (i = 0; i < 8; i++)
        MEM8(va + 2u + (uint32_t)i) = g->bAnalogButtons[i];
    MEM16(va + 10u) = (uint16_t)g->sThumbLX;
    MEM16(va + 12u) = (uint16_t)g->sThumbLY;
    MEM16(va + 14u) = (uint16_t)g->sThumbRX;
    MEM16(va + 16u) = (uint16_t)g->sThumbRY;
}

static unsigned s_reported_ports;

int nfl2k5_hle_XGetDevices(void)
{
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    g_eax = getenv("NFL2K5_LEGACY_INPUT") ? 1u : nfl2k5_controls_ports();
    s_reported_ports = g_eax;
    g_esp += 8u;
    return 1;
}

int nfl2k5_hle_XGetDeviceChanges(void)
{
    static SDL_AtomicInt reported;
    uint32_t ins_va, rem_va;
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    ins_va = MEM32(g_esp + 8u);
    rem_va = MEM32(g_esp + 12u);
    if (getenv("NFL2K5_LEGACY_INPUT")) {
        MEM32(ins_va) = SDL_CompareAndSwapAtomicInt(&reported, 0, 1) ? 1u : 0u;
        MEM32(rem_va) = 0u;
    } else {
        unsigned now = nfl2k5_controls_ports();
        if (SDL_CompareAndSwapAtomicInt(&reported, 0, 1)) s_reported_ports = 0;
        MEM32(ins_va) = now & ~s_reported_ports;
        MEM32(rem_va) = s_reported_ports & ~now;
        if (now != s_reported_ports)
            fprintf(stderr, "  [XINPUT] ports %X -> %X\n", s_reported_ports, now);
        s_reported_ports = now;
    }
    g_eax = (MEM32(ins_va) || MEM32(rem_va)) ? 1u : 0u;
    g_esp += 16u;
    return 1;
}

int nfl2k5_hle_XInputOpen(void)
{
    uint32_t port;
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    port = MEM32(g_esp + 8u);
    g_eax = (port == 0 || (port < 4 && (nfl2k5_controls_ports() >> port) & 1u)) ? (NFL2K5_XINPUT_HANDLE | port) : 0u;
    if (!s_first_poll_ms)
        s_first_poll_ms = SDL_GetTicks();
    fprintf(stderr, "  [XINPUT] XInputOpen(port %u) -> 0x%08X\n", port, g_eax);
    g_esp += 20u;
    return 1;
}

int nfl2k5_hle_XInputClose(void)
{
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    g_esp += 8u;
    return 1;
}

int nfl2k5_hle_XInputGetCapabilities(void)
{
    uint32_t caps;
    XBOX_GAMEPAD all;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    caps = MEM32(g_esp + 8u);
    MEM8(caps) = 1;
    MEM16(caps + 2u) = 0;
    memset(&all, 0xFF, sizeof all);
    put_gamepad(caps + 4u, &all);
    MEM16(caps + 26u) = 0xFFFFu;
    MEM16(caps + 28u) = 0xFFFFu;
    g_eax = 0;
    g_esp += 12u;
    return 1;
}

int nfl2k5_hle_XInputGetState(void)
{
    extern void nfl2k5_live_qb_guest_tick(void);
    static uint32_t packet;
    static XBOX_GAMEPAD last;
    XBOX_GAMEPAD g;
    uint32_t state;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    nfl2k5_live_qb_guest_tick();
    {
        extern volatile DWORD g_xbox_game_tid;
        g_xbox_game_tid = (DWORD)SDL_GetCurrentThreadID();
    }
    state = MEM32(g_esp + 8u);
    if (!s_first_poll_ms)
        s_first_poll_ms = SDL_GetTicks();
    {
        uint32_t port = MEM32(g_esp + 4u) & 3u;
        static uint32_t packets[4];
        static XBOX_GAMEPAD lasts[4];
        if (port != 0) {
            host_gamepad_port((int)port, &g);
            if (memcmp(&g, &lasts[port], sizeof g)) { packets[port]++; lasts[port] = g; }
            MEM32(state) = packets[port];
            put_gamepad(state + 4u, &g);
            g_eax = 0;
            g_esp += 12u;
            return 1;
        }
    }
    host_gamepad(&g);
#if defined(_WIN32)
    {   /* Coach Mode: per-frame play-call hand-off, sideline camera and the
         * fast-forward toggle. Reads the pad only; never consumes input. A
         * no-op unless NFL2K5_COACH_MODE is on (src/nfl2k5_coach_mode.c). */
        extern void nfl2k5_coach_service(uint16_t, const uint8_t[8]);
        nfl2k5_coach_service(g.wButtons, g.bAnalogButtons);
    }
    {   /* Pro Broadcast camera: per-frame automated TV cameras during live play
         * (Free Cam flies with the pad). No-op unless an angle is selected
         * (src/nfl2k5_broadcast_cam.c). */
        extern void nfl2k5_broadcast_cam_service_pad(uint32_t, int, int, int, int, int, int);
        nfl2k5_broadcast_cam_service_pad(g.wButtons, g.sThumbLX, g.sThumbLY,
                                         g.sThumbRX, g.sThumbRY,
                                         g.bAnalogButtons[6], g.bAnalogButtons[7]);
    }
    {   /* ESPN 25th Anniversary moment audio: play a clip when a SOFTDRINK
         * moment (Beast Quake, SB LVIII OT) is launched (nfl2k5_moment_audio.c). */
        extern void nfl2k5_moment_audio_service(void);
        nfl2k5_moment_audio_service();
    }
    {
        int sandbox_input = nfl2k5_sandbox_input(g.wButtons, g.bAnalogButtons,
            g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY);
        if (sandbox_input) memset(&g, 0, sizeof g);
        if (sandbox_input == 2) g.wButtons = 0x10u;
    }
#endif
    if (memcmp(&g, &last, sizeof g)) {
        packet++;
        if (g.wButtons != last.wButtons || g.bAnalogButtons[0] != last.bAnalogButtons[0])
            fprintf(stderr, "  [XINPUT] buttons=0x%04X A=%u packet=%u\n",
                    g.wButtons, g.bAnalogButtons[0], packet);
        last = g;
    }
    MEM32(state) = packet;
    put_gamepad(state + 4u, &g);
    g_eax = 0;
    g_esp += 12u;
    return 1;
}

int nfl2k5_hle_XInputSetState(void)
{
    uint32_t fb;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    fb = MEM32(g_esp + 8u);
    /* XINPUT_FEEDBACK: header (dwStatus, hEvent, Reserved[58]) then
     * XINPUT_RUMBLE { wLeftMotorSpeed, wRightMotorSpeed } at +66. */
    if (host_pad_mode() != 2)
        nfl2k5_controls_rumble((int)(MEM32(g_esp + 4u) & 3u), MEM16(fb + 66u), MEM16(fb + 68u));
    MEM32(fb) = 0;                                  /* XINPUT_FEEDBACK_HEADER.dwStatus */
    g_eax = 0;
    g_esp += 12u;
    return 1;
}
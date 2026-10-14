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
    if (!pressing && now - step_start >= 250) {
        pressing = 1;
        step_start = now;
    }
    if (pressing) {
        const char *name = queue[head];
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
        else if (!strcmp(name, "black") || !strcmp(name, "rb")) analog[4] = 255;
        else if (!strcmp(name, "white") || !strcmp(name, "lb")) analog[5] = 255;
        else if (!strcmp(name, "lt")) analog[6] = 255;
        else if (!strcmp(name, "rt")) analog[7] = 255;
        else if (!strcmp(name, "l3")) buttons |= 0x40;
        else if (!strcmp(name, "r3")) buttons |= 0x80;
        if (now - step_start >= 200) {
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

static void host_gamepad_port(int port, XBOX_GAMEPAD *g)
{
    extern volatile long g_xbox_input_blocked;
    static int no_host_pad = -1;
    memset(g, 0, sizeof *g);
    if (g_xbox_input_blocked)
        return;
    if (no_host_pad < 0) no_host_pad = getenv("NFL2K5_NO_HOST_PAD") != NULL;
    nfl2k5_controls_get(port, g, !(no_host_pad && g_last_script_press && SDL_GetTicks() - g_last_script_press < 15000));
    if (port == 0) {
        host_keyboard_gamepad(g);
        host_mouse_gamepad(g);
        g->wButtons |= auto_press(g->bAnalogButtons);
        g->wButtons |= file_press(g->bAnalogButtons);
    }
    {
        extern void nfl2k5_cs_input(int port, XBOX_GAMEPAD *g);
        nfl2k5_cs_input(port, g);
    }
    if (port != 0)
        return;
    {
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
    static int no_host_pad = -1;
    if (no_host_pad < 0) no_host_pad = getenv("NFL2K5_NO_HOST_PAD") != NULL;
    if (!(no_host_pad && g_last_script_press &&
          SDL_GetTicks() - g_last_script_press < 15000) &&
        xbox_InputGetState(0, &pad) == 0)
        *g = pad.Gamepad;
    host_keyboard_gamepad(g);
    host_mouse_gamepad(g);
    g->wButtons |= auto_press(g->bAnalogButtons);
    g->wButtons |= file_press(g->bAnalogButtons);
    {
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
    nfl2k5_controls_rumble((int)(MEM32(g_esp + 4u) & 3u), MEM16(fb + 66u), MEM16(fb + 68u));
    MEM32(fb) = 0;
    g_eax = 0;
    g_esp += 12u;
    return 1;
}
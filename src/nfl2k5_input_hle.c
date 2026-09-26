/*
 * XInput HLE for NFL 2K5 (2026-09-23).
 *
 * The title polls its controllers through XAPI in the XPP section:
 *   0x4DBC0A XGetDevices(type)             0x4DBC2C XGetDeviceChanges(type, &ins, &rem)
 *   0x4DBC99 XInputOpen(type, port, slot, poll)       0x4DBCEF XInputClose(h)
 *   0x4DBCFB XInputGetCapabilities(h, &caps)          0x4DBED3 XInputGetState(h, &state)
 *   0x4DBF46 XInputSetState(h, &feedback)
 * all driven from the scheduler pump sub_00039380 against the gamepad device
 * type at 0x4DA614 (identified live on xemu). Underneath is a USB stack this
 * runtime does not emulate, so no pad ever arrives -- and the attract state
 * (FSM 0xA84B18, state 0x4F6870, tick sub_000F5AB0) waits for START forever.
 *
 * Instead a gamepad on port 0 is reported and fed from the host: an XInput pad
 * (xbox_InputGetState) plus the keyboard while the game window has focus,
 *   Enter START  Backspace BACK  arrows D-pad  Z/Space A  X B  C X  V Y
 *   Q/E triggers  W/A/S/D left stick
 * Mouse input is also controller input while the game window has focus:
 *   left click=A, right click=B, middle click=START; Shift + movement drives
 *   the left stick relative to the client-area centre. Set NFL2K5_MOUSE=0 to
 *   turn that mapping off. Xbox menus do not expose pointer hit-testing, so
 *   a click confirms the currently focused control rather than selecting a
 *   screen coordinate directly.
 * and NFL2K5_AUTO_PRESS="start@20,a@35" holds buttons for 300 ms at those
 * seconds after the first poll, for unattended runs (the bring-up harness runs
 * the window hidden). Other device types (memory units, headsets) still go to
 * the original code. NFL2K5_INPUT_HLE=0 turns all of it off.
 *
 * Each gen patch (tools/apply-gen-patches.py) calls one of the nfl2k5_hle_*
 * functions at its XAPI function's entry and returns early when it was handled.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "xinput_xbox.h"

#define NFL2K5_XPP_GAMEPAD_TYPE 0x004DA614u
#define NFL2K5_XINPUT_HANDLE    0x7FFE0000u  /* | port */

static DWORD s_first_poll_ms;

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

/* Scripted presses: "start@20,a@35.5". */
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
    t = (GetTickCount() - s_first_poll_ms) / 1000.0;
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

/* Presses from a command file: NFL2K5_PRESS_FILE=<path>. Write button names
 * ("a b x y start back up down left right", separated by spaces or commas)
 * into it; they are pressed in order, 200 ms each with 250 ms between, and the
 * file is deleted once read. Lets a script drive the menus without the window
 * having focus, which a background process cannot reliably take. */
static WORD file_press(BYTE analog[8])
{
    static char queue[64][8];
    static int head, tail;
    static DWORD next_check, step_start;
    static int pressing;
    const char *path = getenv("NFL2K5_PRESS_FILE");
    DWORD now = GetTickCount();
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
            for (tok = strtok_s(buf, " ,\r\n\t", &ctx); tok; tok = strtok_s(NULL, " ,\r\n\t", &ctx)) {
                if (strlen(tok) < 8 && (tail + 1) % 64 != head) {
                    strcpy(queue[tail], tok);
                    tail = (tail + 1) % 64;
                }
            }
        }
    }
    if (head == tail)
        return 0;
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
        if (now - step_start >= 200) {
            pressing = 0;
            step_start = now;
            head = (head + 1) % 64;
        }
    }
    return buttons;
}

static int window_focused(void)
{
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

/* The title receives only an XInput-style gamepad, never host mouse events.
 * Translate mouse movement to its left stick at this boundary rather than
 * trying to guess the layout of each individual Xbox UI screen. Stick motion
 * is held behind Shift so a parked cursor cannot make menus drift. */
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
    HWND hwnd = GetForegroundWindow();
    POINT pt;
    RECT rc;
    LONG cx, cy, dx, dy;

    if (!mouse_hle_on() || !hwnd || !window_focused())
        return;

    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        g->bAnalogButtons[0] = 255;                 /* A / confirm */
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        g->bAnalogButtons[1] = 255;                 /* B / back */
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
        g->wButtons |= 0x10;                         /* START */

    if (!(GetAsyncKeyState(VK_SHIFT) & 0x8000)
            || !GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)
            || !GetClientRect(hwnd, &rc))
        return;
    cx = (rc.right - rc.left) / 2;
    cy = (rc.bottom - rc.top) / 2;
    if (cx <= 0 || cy <= 0)
        return;
    dx = pt.x - cx;
    dy = cy - pt.y;                                  /* Xbox Y is up */
    /* Keep the centre 15% quiet, then scale the remaining range to a stick. */
    if (labs(dx) > cx * 15 / 100)
        g->sThumbLX = (SHORT)((dx * 32767L) / cx);
    if (labs(dy) > cy * 15 / 100)
        g->sThumbLY = (SHORT)((dy * 32767L) / cy);
}

static void host_gamepad(XBOX_GAMEPAD *g)
{
    XBOX_INPUT_STATE pad;
    /* The video settings page (F1) has the controller while it is open. */
    extern volatile long g_xbox_input_blocked;
    memset(g, 0, sizeof *g);
    if (g_xbox_input_blocked)
        return;
    /* NFL2K5_NO_HOST_PAD=1 ignores host controllers: scripted runs on a
     * machine with a pad attached got stray d-pad presses that walked the
     * menu cursor away from what the script selected (2026-09-25). */
    if (!getenv("NFL2K5_NO_HOST_PAD") && xbox_InputGetState(0, &pad) == 0)
        *g = pad.Gamepad;
    if (window_focused()) {
#define KEY(vk) (GetAsyncKeyState(vk) & 0x8000)
        if (KEY(VK_RETURN)) g->wButtons |= 0x10;
        if (KEY(VK_BACK))   g->wButtons |= 0x20;
        if (KEY(VK_UP))     g->wButtons |= 0x01;
        if (KEY(VK_DOWN))   g->wButtons |= 0x02;
        if (KEY(VK_LEFT))   g->wButtons |= 0x04;
        if (KEY(VK_RIGHT))  g->wButtons |= 0x08;
        if (KEY('Z') || KEY(VK_SPACE)) g->bAnalogButtons[0] = 255;
        if (KEY('X')) g->bAnalogButtons[1] = 255;
        if (KEY('C')) g->bAnalogButtons[2] = 255;
        if (KEY('V')) g->bAnalogButtons[3] = 255;
        if (KEY('Q')) g->bAnalogButtons[6] = 255;
        if (KEY('E')) g->bAnalogButtons[7] = 255;
        if (KEY('A')) g->sThumbLX = -32767;
        if (KEY('D')) g->sThumbLX = 32767;
        if (KEY('W')) g->sThumbLY = 32767;
        if (KEY('S')) g->sThumbLY = -32767;
#undef KEY
        host_mouse_gamepad(g);
    }
    g->wButtons |= auto_press(g->bAnalogButtons);
    g->wButtons |= file_press(g->bAnalogButtons);
}

/* An XINPUT_GAMEPAD in guest memory: 22 bytes, packed. */
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

/* Each returns 1 when it handled the call (eax set, stdcall args popped). */
int nfl2k5_hle_XGetDevices(void)
{
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    g_eax = 1u;                                     /* port 0 */
    g_esp += 8u;                                    /* ret 4 */
    return 1;
}

int nfl2k5_hle_XGetDeviceChanges(void)
{
    static LONG reported;
    uint32_t ins_va, rem_va;
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    ins_va = MEM32(g_esp + 8u);
    rem_va = MEM32(g_esp + 12u);
    MEM32(ins_va) = InterlockedExchange(&reported, 1) ? 0u : 1u;
    MEM32(rem_va) = 0u;
    g_eax = MEM32(ins_va) ? 1u : 0u;
    g_esp += 16u;                                   /* ret 0xC */
    return 1;
}

int nfl2k5_hle_XInputOpen(void)
{
    uint32_t port;
    if (!input_hle_on() || MEM32(g_esp + 4u) != NFL2K5_XPP_GAMEPAD_TYPE)
        return 0;
    port = MEM32(g_esp + 8u);
    g_eax = port == 0 ? (NFL2K5_XINPUT_HANDLE | port) : 0u;
    if (!s_first_poll_ms)
        s_first_poll_ms = GetTickCount();
    fprintf(stderr, "  [XINPUT] XInputOpen(port %u) -> 0x%08X\n", port, g_eax);
    g_esp += 20u;                                   /* ret 0x10 */
    return 1;
}

int nfl2k5_hle_XInputClose(void)
{
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    g_esp += 8u;                                    /* ret 4 */
    return 1;
}

int nfl2k5_hle_XInputGetCapabilities(void)
{
    uint32_t caps;
    XBOX_GAMEPAD all;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    caps = MEM32(g_esp + 8u);
    MEM8(caps) = 1;                                 /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
    MEM16(caps + 2u) = 0;
    memset(&all, 0xFF, sizeof all);                 /* every control present */
    put_gamepad(caps + 4u, &all);
    MEM16(caps + 26u) = 0xFFFFu;                    /* both rumble motors */
    MEM16(caps + 28u) = 0xFFFFu;
    g_eax = 0;
    g_esp += 12u;                                   /* ret 8 */
    return 1;
}

int nfl2k5_hle_XInputGetState(void)
{
    static uint32_t packet;
    static XBOX_GAMEPAD last;
    XBOX_GAMEPAD g;
    uint32_t state;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    state = MEM32(g_esp + 8u);
    if (!s_first_poll_ms)
        s_first_poll_ms = GetTickCount();
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
    g_esp += 12u;                                   /* ret 8 */
    return 1;
}

int nfl2k5_hle_XInputSetState(void)
{
    uint32_t fb;
    if (!is_our_handle(MEM32(g_esp + 4u)))
        return 0;
    fb = MEM32(g_esp + 8u);
    MEM32(fb) = 0;                                  /* XINPUT_FEEDBACK_HEADER.dwStatus */
    g_eax = 0;
    g_esp += 12u;                                   /* ret 8 */
    return 1;
}

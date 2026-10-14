/*
 * Controllers, keyboard and mouse for NFL 2K5 (Unified SDL3 Backend).
 *
 * Replaces Win32 XInput + SetupAPI/HID PlayStation reader with SDL3.
 * Preserves controls.ini compatibility, F1 menu pages, deadzones, and
 * automatic Xbox / DualShock 4 / DualSense prompt detection.
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xinput_xbox.h"

extern char *xbox_user_path(const char *rel, char *out, size_t n);
extern void *xbox_FramebufferWindowHwnd(void);

/* Virtual-Key constants kept for controls.ini backward compatibility */
#ifndef VK_LBUTTON
#define VK_LBUTTON  0x01
#define VK_RBUTTON  0x02
#define VK_MBUTTON  0x04
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_PRIOR    0x21
#define VK_NEXT     0x22
#define VK_END      0x23
#define VK_HOME     0x24
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_INSERT   0x2D
#define VK_DELETE   0x2E
#define VK_F1       0x70
#endif

enum { C_A, C_B, C_X, C_Y, C_LB, C_RB, C_LT, C_RT, C_START, C_BACK, C_LS, C_RS,
       C_DUP, C_DDOWN, C_DLEFT, C_DRIGHT, C_COUNT };

static const char *const k_ctl_names[C_COUNT] = {
    "A", "B", "X", "Y", "LB (White)", "RB (Black)", "LT", "RT", "Start", "Back",
    "Left stick click", "Right stick click", "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
static const char *const k_ctl_keys[C_COUNT] = {
    "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "ls", "rs", "dup", "ddown", "dleft", "dright" };
static const char *const k_ps_names[C_COUNT] = {
    "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Options", "Share/Create",
    "L3", "R3", "D-pad up", "D-pad down", "D-pad left", "D-pad right" };

enum { K_LUP = C_COUNT, K_LDOWN, K_LLEFT, K_LRIGHT, K_COUNT };
static const char *const k_key_names[K_COUNT] = {
    "A", "B", "X", "Y", "LB (White)", "RB (Black)", "LT", "RT", "Start", "Back",
    "Left stick click", "Right stick click", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Left stick up", "Left stick down", "Left stick left", "Left stick right" };
static const char *const k_key_ini[K_COUNT] = {
    "key_a", "key_b", "key_x", "key_y", "key_lb", "key_rb", "key_lt", "key_rt", "key_start", "key_back",
    "key_ls", "key_rs", "key_dup", "key_ddown", "key_dleft", "key_dright",
    "key_lup", "key_ldown", "key_lleft", "key_lright" };
static const int k_key_default[K_COUNT] = {
    'Z', 'X', 'C', 'V', 'R', 'F', 'Q', 'E', VK_RETURN, VK_BACK, 'T', 'G',
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'S', 'A', 'D' };

typedef struct {
    int vibration;
    int dz_left, dz_right, dz_trig;
    int mouse;
    int prompts;
    int map[C_COUNT];
    int keys[K_COUNT];
} Controls;

static Controls      s_cfg;
static SDL_Mutex    *s_lock;
static SDL_AtomicInt s_inited;

/* Map Windows VK codes used in controls.ini to SDL_Scancode */
static SDL_Scancode vk_to_scancode(int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9')
        return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0')
        return SDL_SCANCODE_0;
    if (vk >= 0x70 && vk <= 0x7B) /* VK_F1..VK_F12 */
        return (SDL_SCANCODE_F1 + (vk - 0x70));

    switch (vk) {
        case VK_BACK:   return SDL_SCANCODE_BACKSPACE;
        case VK_TAB:    return SDL_SCANCODE_TAB;
        case VK_RETURN: return SDL_SCANCODE_RETURN;
        case VK_SHIFT:  return SDL_SCANCODE_LSHIFT;
        case VK_CONTROL:return SDL_SCANCODE_LCTRL;
        case VK_MENU:   return SDL_SCANCODE_LALT;
        case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
        case VK_SPACE:  return SDL_SCANCODE_SPACE;
        case VK_PRIOR:  return SDL_SCANCODE_PAGEUP;
        case VK_NEXT:   return SDL_SCANCODE_PAGEDOWN;
        case VK_END:    return SDL_SCANCODE_END;
        case VK_HOME:   return SDL_SCANCODE_HOME;
        case VK_LEFT:   return SDL_SCANCODE_LEFT;
        case VK_UP:     return SDL_SCANCODE_UP;
        case VK_RIGHT:  return SDL_SCANCODE_RIGHT;
        case VK_DOWN:   return SDL_SCANCODE_DOWN;
        case VK_INSERT: return SDL_SCANCODE_INSERT;
        case VK_DELETE: return SDL_SCANCODE_DELETE;
        default:        return SDL_SCANCODE_UNKNOWN;
    }
}

static int vk_pressed(int vk)
{
    int numkeys = 0;
    const bool *state;
    SDL_Scancode sc;

    if (!vk) return 0;
    state = SDL_GetKeyboardState(&numkeys);
    if (!state) return 0;

    if (vk == VK_SHIFT)
        return state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];
    if (vk == VK_CONTROL)
        return state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL];
    if (vk == VK_MENU)
        return state[SDL_SCANCODE_LALT] || state[SDL_SCANCODE_RALT];

    sc = vk_to_scancode(vk);
    return (sc != SDL_SCANCODE_UNKNOWN && (int)sc < numkeys) ? state[sc] : 0;
}

static void cfg_defaults(Controls *c)
{
    int i;
    c->vibration = 1;
    c->dz_left = 12; c->dz_right = 12; c->dz_trig = 5;
    c->mouse = 1;
    c->prompts = 0;
    for (i = 0; i < C_COUNT; i++) c->map[i] = i;
    for (i = 0; i < K_COUNT; i++) c->keys[i] = k_key_default[i];
}

static void cfg_save(void)
{
    char p[512];
    FILE *f = fopen(xbox_user_path("controls.ini", p, sizeof p), "w");
    int i;
    if (!f) return;
    fprintf(f, "; NFL 2K5 controls (F1 > Controls pages edit these)\n"
               "vibration = %d\ndeadzone_left = %d\ndeadzone_right = %d\ndeadzone_trigger = %d\nmouse = %d\n"
               "; prompts: 0 automatic (the last controller used), 1 Xbox, 2 PlayStation, 3 keyboard, 4 original Xbox\nprompts = %d\n"
               "; map_<game button> = <controller button>: a b x y lb rb lt rt start back ls rs dup ddown dleft dright\n",
            s_cfg.vibration, s_cfg.dz_left, s_cfg.dz_right, s_cfg.dz_trig, s_cfg.mouse, s_cfg.prompts);
    for (i = 0; i < C_COUNT; i++) fprintf(f, "map_%s = %s\n", k_ctl_keys[i], k_ctl_keys[s_cfg.map[i]]);
    fprintf(f, "; key_<game button> = Windows virtual-key code (decimal), 0 = none\n");
    for (i = 0; i < K_COUNT; i++) fprintf(f, "%s = %d\n", k_key_ini[i], s_cfg.keys[i]);
    fclose(f);
}

static void cfg_load(void)
{
    char p[512], line[160];
    FILE *f;
    cfg_defaults(&s_cfg);
    f = fopen(xbox_user_path("controls.ini", p, sizeof p), "r");
    if (!f) { cfg_save(); return; }
    while (fgets(line, sizeof line, f)) {
        char k[64], v[64];
        int i, n;
        if (line[0] == ';' || sscanf(line, " %63[a-z_] = %63s", k, v) != 2) continue;
        n = atoi(v);
        if (!strcmp(k, "vibration")) s_cfg.vibration = n != 0;
        else if (!strcmp(k, "deadzone_left")) s_cfg.dz_left = n;
        else if (!strcmp(k, "deadzone_right")) s_cfg.dz_right = n;
        else if (!strcmp(k, "deadzone_trigger")) s_cfg.dz_trig = n;
        else if (!strcmp(k, "mouse")) s_cfg.mouse = n != 0;
        else if (!strcmp(k, "prompts")) s_cfg.prompts = n >= 0 && n <= 4 ? n : 0;
        else if (!strncmp(k, "map_", 4)) {
            for (i = 0; i < C_COUNT; i++) if (!strcmp(k + 4, k_ctl_keys[i])) break;
            if (i < C_COUNT) { int j; for (j = 0; j < C_COUNT; j++) if (!strcmp(v, k_ctl_keys[j])) s_cfg.map[i] = j; }
        } else
            for (i = 0; i < K_COUNT; i++) if (!strcmp(k, k_key_ini[i])) s_cfg.keys[i] = n;
    }
    fclose(f);
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
    s_cfg.dz_left = CLAMP(s_cfg.dz_left, 0, 50);
    s_cfg.dz_right = CLAMP(s_cfg.dz_right, 0, 50);
    s_cfg.dz_trig = CLAMP(s_cfg.dz_trig, 0, 50);
#undef CLAMP
}

typedef struct {
    int connected;
    char kind[32];
    int ps;
    int dev; /* 0 Xbox, 1 DualShock 4, 2 DualSense */
    unsigned char btn[C_COUNT];
    short lx, ly, rx, ry;
} RawPad;

static SDL_Gamepad *s_pads[4] = { NULL };
static Uint64       s_last_scan_ms = 0;

static int classify_gamepad(SDL_Gamepad *pad, char *kind_out, size_t kind_len, int *is_ps)
{
    SDL_GamepadType type = SDL_GetGamepadType(pad);
    Uint16 vid = SDL_GetGamepadVendor(pad);
    Uint16 pid = SDL_GetGamepadProduct(pad);

    if (type == SDL_GAMEPAD_TYPE_PS5 || (vid == 0x054C && (pid == 0x0CE6 || pid == 0x0DF2))) {
        *is_ps = 1;
        snprintf(kind_out, kind_len, "%s", pid == 0x0DF2 ? "DualSense Edge" : "DualSense");
        return 2;
    }
    if (type == SDL_GAMEPAD_TYPE_PS4 || (vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0))) {
        *is_ps = 1;
        snprintf(kind_out, kind_len, "DualShock 4");
        return 1;
    }
    *is_ps = 0;
    snprintf(kind_out, kind_len, "Xbox controller");
    return 0;
}

static void sdl_pads_refresh(void)
{
    Uint64 now = SDL_GetTicks();
    int count = 0;
    SDL_JoystickID *ids;

    if (s_last_scan_ms && now - s_last_scan_ms < 1000)
        return;
    s_last_scan_ms = now;

    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);

    SDL_LockMutex(s_lock);
    for (int i = 0; i < 4; i++) {
        if (s_pads[i] && !SDL_GamepadConnected(s_pads[i])) {
            SDL_CloseGamepad(s_pads[i]);
            s_pads[i] = NULL;
        }
    }

    ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; i++) {
            if (SDL_GetGamepadFromID(ids[i]) != NULL)
                continue;
            for (int slot = 0; slot < 4; slot++) {
                if (!s_pads[slot]) {
                    s_pads[slot] = SDL_OpenGamepad(ids[i]);
                    if (s_pads[slot]) {
                        char kind[32];
                        int is_ps = 0;
                        classify_gamepad(s_pads[slot], kind, sizeof kind, &is_ps);
                        fprintf(stderr, "[CONTROLS] %s connected on slot %d\n", kind, slot);
                    }
                    break;
                }
            }
        }
        SDL_free(ids);
    }
    SDL_UnlockMutex(s_lock);
}

static void read_sdl_pad(int slot, RawPad *s)
{
    SDL_Gamepad *pad;
    memset(s, 0, sizeof *s);

    SDL_UpdateGamepads();
    SDL_LockMutex(s_lock);
    pad = (slot >= 0 && slot < 4) ? s_pads[slot] : NULL;
    if (!pad || !SDL_GamepadConnected(pad)) {
        SDL_UnlockMutex(s_lock);
        return;
    }

    s->connected = 1;
    s->dev = classify_gamepad(pad, s->kind, sizeof s->kind, &s->ps);

    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))          s->btn[C_A] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))           s->btn[C_B] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))           s->btn[C_X] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH))          s->btn[C_Y] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  s->btn[C_LB] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) s->btn[C_RB] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))          s->btn[C_START] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))           s->btn[C_BACK] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK))     s->btn[C_LS] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK))    s->btn[C_RS] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))        s->btn[C_DUP] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      s->btn[C_DDOWN] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      s->btn[C_DLEFT] = 255;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     s->btn[C_DRIGHT] = 255;

    s->btn[C_LT] = (unsigned char)(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
    s->btn[C_RT] = (unsigned char)(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);

    s->lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    s->ly = (short)(-1 - SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    s->rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    s->ry = (short)(-1 - SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
    SDL_UnlockMutex(s_lock);
}

static void deadzone(short *x, short *y, int pct)
{
    double fx = *x, fy = *y, m = sqrt(fx * fx + fy * fy), dz = 32767.0 * pct / 100.0, k;
    if (m <= dz || m < 1.0) { *x = 0; *y = 0; return; }
    k = (m - dz) / (32767.0 - dz) / m * 32767.0;
    if (m * k > 32767.0) k = 32767.0 / m;
    *x = (short)(fx * k); *y = (short)(fy * k);
}

static SDL_AtomicInt s_last_ps, s_last_dev;
static void note_use(const RawPad *s)
{
    for (int i = 0; i < C_COUNT; i++) {
        if (s->btn[i] > 128) {
            SDL_SetAtomicInt(&s_last_ps, s->ps);
            SDL_SetAtomicInt(&s_last_dev, s->dev);
            return;
        }
    }
}

int nfl2k5_controls_style(void)
{
    switch (s_cfg.prompts) {
        case 1: return 0;
        case 2: return 1;
        case 3: return 2;
        case 4: return 3;
    }
    return SDL_GetAtomicInt(&s_last_dev) == 3 ? 2 : SDL_GetAtomicInt(&s_last_ps) ? 1 : 0;
}

int nfl2k5_controls_ps_kind(void)
{
    int last = SDL_GetAtomicInt(&s_last_dev);
    if (last == 1 || last == 2) return last;

    for (int i = 0; i < 4; i++) {
        RawPad s;
        read_sdl_pad(i, &s);
        if (s.connected && s.ps) return s.dev;
    }
    return 2;
}

static int prompt_style(void)
{
    return nfl2k5_controls_style() == 1;
}

int nfl2k5_controls_key(int game_button)
{
    return game_button >= 0 && game_button < K_COUNT ? s_cfg.keys[game_button] : 0;
}

static void to_gamepad(const RawPad *s, XBOX_GAMEPAD *g)
{
    int i, trig = s_cfg.dz_trig * 255 / 100;
    unsigned char v[C_COUNT];
    memset(g, 0, sizeof *g);
    for (i = 0; i < C_COUNT; i++) v[i] = s->btn[s_cfg.map[i]];
    if (v[C_LT] <= trig) v[C_LT] = 0;
    if (v[C_RT] <= trig) v[C_RT] = 0;
    g->bAnalogButtons[0] = v[C_A]; g->bAnalogButtons[1] = v[C_B];
    g->bAnalogButtons[2] = v[C_X]; g->bAnalogButtons[3] = v[C_Y];
    g->bAnalogButtons[4] = v[C_RB]; g->bAnalogButtons[5] = v[C_LB];
    g->bAnalogButtons[6] = v[C_LT]; g->bAnalogButtons[7] = v[C_RT];
    if (v[C_DUP] > 128) g->wButtons |= 0x01;
    if (v[C_DDOWN] > 128) g->wButtons |= 0x02;
    if (v[C_DLEFT] > 128) g->wButtons |= 0x04;
    if (v[C_DRIGHT] > 128) g->wButtons |= 0x08;
    if (v[C_START] > 128) g->wButtons |= 0x10;
    if (v[C_BACK] > 128) g->wButtons |= 0x20;
    if (v[C_LS] > 128) g->wButtons |= 0x40;
    if (v[C_RS] > 128) g->wButtons |= 0x80;
    g->sThumbLX = s->lx; g->sThumbLY = s->ly; g->sThumbRX = s->rx; g->sThumbRY = s->ry;
    deadzone(&g->sThumbLX, &g->sThumbLY, s_cfg.dz_left);
    deadzone(&g->sThumbRX, &g->sThumbRY, s_cfg.dz_right);
}

static int window_focused(void)
{
    SDL_Window *win = (SDL_Window *)xbox_FramebufferWindowHwnd();
    if (!win) return 0;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static void keyboard_into(XBOX_GAMEPAD *g)
{
#define KEY(i) (s_cfg.keys[i] && vk_pressed(s_cfg.keys[i]))
    static const int analog[8] = { C_A, C_B, C_X, C_Y, C_RB, C_LB, C_LT, C_RT };
    int i;
    for (i = 0; i < 8; i++) if (KEY(analog[i])) g->bAnalogButtons[i] = 255;
    for (i = 0; i < K_COUNT; i++) if (KEY(i)) { SDL_SetAtomicInt(&s_last_dev, 3); SDL_SetAtomicInt(&s_last_ps, 0); break; }
    if (KEY(C_DUP)) g->wButtons |= 0x01;
    if (KEY(C_DDOWN)) g->wButtons |= 0x02;
    if (KEY(C_DLEFT)) g->wButtons |= 0x04;
    if (KEY(C_DRIGHT)) g->wButtons |= 0x08;
    if (KEY(C_START)) g->wButtons |= 0x10;
    if (KEY(C_BACK)) g->wButtons |= 0x20;
    if (KEY(C_LS)) g->wButtons |= 0x40;
    if (KEY(C_RS)) g->wButtons |= 0x80;
    if (KEY(K_LLEFT)) g->sThumbLX = -32767;
    if (KEY(K_LRIGHT)) g->sThumbLX = 32767;
    if (KEY(K_LUP)) g->sThumbLY = 32767;
    if (KEY(K_LDOWN)) g->sThumbLY = -32767;
#undef KEY
}

static void mouse_into(XBOX_GAMEPAD *g)
{
    SDL_Window *win = (SDL_Window *)xbox_FramebufferWindowHwnd();
    float mx = 0.0f, my = 0.0f;
    int ww = 0, wh = 0;
    SDL_MouseButtonFlags mb;

    if (!s_cfg.mouse || !win || !window_focused()) return;

    mb = SDL_GetMouseState(&mx, &my);
    if (mb & SDL_BUTTON_LMASK) g->bAnalogButtons[0] = 255;
    if (mb & SDL_BUTTON_RMASK) g->bAnalogButtons[1] = 255;
    if (mb & SDL_BUTTON_MMASK) g->wButtons |= 0x10;

    if (!vk_pressed(VK_SHIFT)) return;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;

    float cx = ww * 0.5f, cy = wh * 0.5f;
    float dx = mx - cx, dy = cy - my;
    if (fabsf(dx) > cx * 0.15f) g->sThumbLX = (SHORT)((dx * 32767.0f) / cx);
    if (fabsf(dy) > cy * 0.15f) g->sThumbLY = (SHORT)((dy * 32767.0f) / cy);
}

static void controls_init(void)
{
    if (!SDL_CompareAndSwapAtomicInt(&s_inited, 0, 1)) return;
    s_lock = SDL_CreateMutex();
    cfg_load();
    {
        extern void nfl2k5_controls_menu_init(void);
        nfl2k5_controls_menu_init();
    }
    sdl_pads_refresh();
}

unsigned nfl2k5_controls_ports(void)
{
    unsigned m = 1; /* Port 0 always active (keyboard) */
    controls_init();
    sdl_pads_refresh();
    for (int i = 1; i < 4; i++) {
        RawPad s;
        read_sdl_pad(i, &s);
        if (s.connected) m |= (1u << i);
    }
    return m;
}

int nfl2k5_controls_get(int port, XBOX_GAMEPAD *g, int use_pads)
{
    RawPad s;
    controls_init();
    memset(g, 0, sizeof *g);
    sdl_pads_refresh();

    if (use_pads && port >= 0 && port < 4) {
        read_sdl_pad(port, &s);
        if (s.connected) { note_use(&s); to_gamepad(&s, g); }
    }
    if (port == 0 && window_focused()) {
        keyboard_into(g);
        mouse_into(g);
    }
    return port == 0 || s.connected;
}

int nfl2k5_controls_port_dev(int port)
{
    RawPad s;
    controls_init();
    if (port < 0 || port > 3) return -1;
    read_sdl_pad(port, &s);
    return s.connected ? s.dev : -1;
}

void nfl2k5_controls_rumble(int port, unsigned left, unsigned right)
{
    controls_init();
    if (!s_cfg.vibration) left = right = 0;
    if (port < 0 || port > 3) return;

    SDL_LockMutex(s_lock);
    if (s_pads[port] && SDL_GamepadConnected(s_pads[port])) {
        SDL_RumbleGamepad(s_pads[port], (Uint16)left, (Uint16)right, 1000);
    }
    SDL_UnlockMutex(s_lock);
}

static int menu_pad(XBOX_GAMEPAD *g)
{
    memset(g, 0, sizeof *g);
    for (int i = 0; i < 4; i++) {
        RawPad s;
        XBOX_GAMEPAD one;
        read_sdl_pad(i, &s);
        if (!s.connected) continue;
        to_gamepad(&s, &one);
        g->wButtons |= one.wButtons;
        for (int k = 0; k < 8; k++) {
            if (one.bAnalogButtons[k] > g->bAnalogButtons[k])
                g->bAnalogButtons[k] = one.bAnalogButtons[k];
        }
    }
    return 1;
}

/* ---- F1 pages ------------------------------------------------------------- */
typedef struct XboxMenuPage {
    const char *title;
    int (*count)(void);
    void (*label)(int row, char *name, char *val, size_t n);
    void (*change)(int row, int dir);
    void (*activate)(int row);
    int (*busy)(void);
} XboxMenuPage;
extern void xbox_MenuAddPage(const XboxMenuPage *p);
extern void xbox_MenuInvalidate(void);
extern int (*g_xbox_menu_pad)(XBOX_GAMEPAD *g);

static SDL_AtomicInt s_capture_pad = { -1 }, s_capture_key = { -1 };
static Uint64 s_capture_start;

static int SDLCALL capture_thread(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_Delay(15);
        int cap_pad = SDL_GetAtomicInt(&s_capture_pad);
        if (cap_pad >= 0) {
            int idle = (SDL_GetTicks() - s_capture_start) < 300;
            for (int i = 0; i < 4 && !idle; i++) {
                RawPad s;
                read_sdl_pad(i, &s);
                if (!s.connected) continue;
                for (int c = 0; c < C_COUNT; c++) {
                    if (s.btn[c] > 200) {
                        s_cfg.map[cap_pad] = c;
                        SDL_SetAtomicInt(&s_capture_pad, -1);
                        cfg_save();
                        xbox_MenuInvalidate();
                        break;
                    }
                }
                if (SDL_GetAtomicInt(&s_capture_pad) < 0) break;
            }
            if (SDL_GetTicks() - s_capture_start > 6000) {
                SDL_SetAtomicInt(&s_capture_pad, -1);
                xbox_MenuInvalidate();
            }
        }

        int cap_key = SDL_GetAtomicInt(&s_capture_key);
        if (cap_key >= 0) {
            if (SDL_GetTicks() - s_capture_start < 300) continue;
            for (int vk = 8; vk < 256; vk++) {
                if (vk == VK_F1 || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                    vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
                if (vk_pressed(vk)) {
                    if (vk != VK_ESCAPE) { s_cfg.keys[cap_key] = vk; cfg_save(); }
                    SDL_SetAtomicInt(&s_capture_key, -1);
                    xbox_MenuInvalidate();
                    break;
                }
            }
            if (SDL_GetTicks() - s_capture_start > 6000) {
                SDL_SetAtomicInt(&s_capture_key, -1);
                xbox_MenuInvalidate();
            }
        }
    }
    return 0;
}

static void vk_name(int vk, char *out, size_t n)
{
    if (!vk) { snprintf(out, n, "(none)"); return; }
    SDL_Scancode sc = vk_to_scancode(vk);
    const char *name = sc != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(sc) : NULL;
    if (name && *name)
        snprintf(out, n, "%s", name);
    else
        snprintf(out, n, "key %d", vk);
}

static const char *pad_kind(void)
{
    static char buf[160];
    buf[0] = 0;
    for (int i = 0; i < 4; i++) {
        RawPad s;
        read_sdl_pad(i, &s);
        if (!s.connected) continue;
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%d: %s", buf[0] ? ", " : "", i + 1, s.kind);
    }
    if (!buf[0]) snprintf(buf, sizeof buf, "none (keyboard on port 1)");
    return buf;
}

static int any_ps(void)
{
    for (int i = 0; i < 4; i++) {
        RawPad s;
        read_sdl_pad(i, &s);
        if (s.connected && s.ps) return 1;
    }
    return 0;
}

enum { R_DEVICES, R_VIB, R_DZL, R_DZR, R_DZT, R_MOUSE, R_PROMPTS, R_RESET, R_COUNT };
static int p1_count(void) { return R_COUNT; }
static void p1_label(int r, char *name, char *val, size_t n)
{
    switch (r) {
    case R_DEVICES: strcpy(name, "Controllers"); snprintf(val, n, "%s", pad_kind()); break;
    case R_VIB:     strcpy(name, "Vibration"); snprintf(val, n, "%s", s_cfg.vibration ? "On" : "Off"); break;
    case R_DZL:     strcpy(name, "Left stick dead zone"); snprintf(val, n, "%d%%", s_cfg.dz_left); break;
    case R_DZR:     strcpy(name, "Right stick dead zone"); snprintf(val, n, "%d%%", s_cfg.dz_right); break;
    case R_DZT:     strcpy(name, "Trigger dead zone"); snprintf(val, n, "%d%%", s_cfg.dz_trig); break;
    case R_MOUSE:   strcpy(name, "Mouse as controller"); snprintf(val, n, "%s", s_cfg.mouse ? "On" : "Off"); break;
    case R_PROMPTS: { static const char *m[5] = { "Automatic", "Xbox", "PlayStation", "Keyboard", "Original Xbox" };
                      static const char *now[4] = { "  (Xbox now)", "  (PlayStation now)", "  (keyboard now)", "" };
                      strcpy(name, "Button prompts");
                      snprintf(val, n, "%s%s", m[s_cfg.prompts % 5],
                               s_cfg.prompts == 0 ? now[nfl2k5_controls_style() & 3] : ""); } break;
    case R_RESET:   strcpy(name, "Reset controls to defaults"); break;
    }
}
static void p1_change(int r, int dir)
{
    switch (r) {
    case R_VIB:   s_cfg.vibration = !s_cfg.vibration; break;
    case R_DZL:   s_cfg.dz_left += 2 * dir; break;
    case R_DZR:   s_cfg.dz_right += 2 * dir; break;
    case R_DZT:   s_cfg.dz_trig += 2 * dir; break;
    case R_MOUSE: s_cfg.mouse = !s_cfg.mouse; break;
    case R_PROMPTS: s_cfg.prompts = (s_cfg.prompts + 5 + dir) % 5; break;
    default: return;
    }
    if (s_cfg.dz_left < 0) s_cfg.dz_left = 0;
    if (s_cfg.dz_left > 50) s_cfg.dz_left = 50;
    if (s_cfg.dz_right < 0) s_cfg.dz_right = 0;
    if (s_cfg.dz_right > 50) s_cfg.dz_right = 50;
    if (s_cfg.dz_trig < 0) s_cfg.dz_trig = 0;
    if (s_cfg.dz_trig > 50) s_cfg.dz_trig = 50;
    cfg_save();
    xbox_MenuInvalidate();
}
static void p1_activate(int r)
{
    if (r == R_RESET) { cfg_defaults(&s_cfg); cfg_save(); xbox_MenuInvalidate(); }
    else if (r >= 0) p1_change(r, 1);
}
static const XboxMenuPage k_p1 = { "CONTROLS", p1_count, p1_label, p1_change, p1_activate, NULL };

static int p2_count(void) { return C_COUNT + 1; }
static void p2_label(int r, char *name, char *val, size_t n)
{
    int ps = any_ps();
    if (r == C_COUNT) { strcpy(name, "Default layout"); val[0] = 0; return; }
    snprintf(name, 64, "Game %s", k_ctl_names[r]);
    if (SDL_GetAtomicInt(&s_capture_pad) == r) snprintf(val, n, "press a controller button...");
    else snprintf(val, n, "%s%s", ps ? k_ps_names[s_cfg.map[r]] : k_ctl_names[s_cfg.map[r]],
                  s_cfg.map[r] == r ? "" : "  (changed)");
}
static void p2_change(int r, int dir)
{
    if (r < 0 || r >= C_COUNT) return;
    s_cfg.map[r] = (s_cfg.map[r] + C_COUNT + dir) % C_COUNT;
    cfg_save();
    xbox_MenuInvalidate();
}
static void p2_activate(int r)
{
    if (r == -1) { SDL_SetAtomicInt(&s_capture_pad, -1); return; }
    if (r == C_COUNT) { for (int i = 0; i < C_COUNT; i++) s_cfg.map[i] = i; cfg_save(); xbox_MenuInvalidate(); return; }
    s_capture_start = SDL_GetTicks();
    SDL_SetAtomicInt(&s_capture_pad, r);
    xbox_MenuInvalidate();
}
static int p2_busy(void) { return SDL_GetAtomicInt(&s_capture_pad) >= 0; }
static const XboxMenuPage k_p2 = { "CONTROLLER BUTTONS", p2_count, p2_label, p2_change, p2_activate, p2_busy };

static int p3_count(void) { return K_COUNT + 1; }
static void p3_label(int r, char *name, char *val, size_t n)
{
    if (r == K_COUNT) { strcpy(name, "Default keys"); val[0] = 0; return; }
    snprintf(name, 64, "%s", k_key_names[r]);
    if (SDL_GetAtomicInt(&s_capture_key) == r) snprintf(val, n, "press a key...  (Esc: cancel)");
    else vk_name(s_cfg.keys[r], val, n);
}
static void p3_activate(int r)
{
    if (r == -1) { SDL_SetAtomicInt(&s_capture_key, -1); return; }
    if (r == K_COUNT) { for (int i = 0; i < K_COUNT; i++) s_cfg.keys[i] = k_key_default[i]; cfg_save(); xbox_MenuInvalidate(); return; }
    s_capture_start = SDL_GetTicks();
    SDL_SetAtomicInt(&s_capture_key, r);
    xbox_MenuInvalidate();
}
static void p3_change(int r, int dir) { (void)dir; if (r >= 0 && r < K_COUNT) { s_cfg.keys[r] = 0; cfg_save(); xbox_MenuInvalidate(); } }
static int p3_busy(void) { return SDL_GetAtomicInt(&s_capture_key) >= 0; }
static const XboxMenuPage k_p3 = { "KEYBOARD", p3_count, p3_label, p3_change, p3_activate, p3_busy };

void nfl2k5_controls_menu_init(void)
{
    SDL_Thread *t;
    xbox_MenuAddPage(&k_p1);
    xbox_MenuAddPage(&k_p2);
    xbox_MenuAddPage(&k_p3);
    g_xbox_menu_pad = menu_pad;
    {
        extern int (*g_xbox_prompt_style)(void);
        g_xbox_prompt_style = prompt_style;
    }
    t = SDL_CreateThread(capture_thread, "ctl_capture", NULL);
    if (t) SDL_DetachThread(t);
}

void nfl2k5_controls_start(void) { controls_init(); }
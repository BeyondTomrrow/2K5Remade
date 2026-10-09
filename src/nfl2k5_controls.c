/*
 * Controllers, keyboard and mouse for NFL 2K5 (2026-10-09).
 *
 * The game polls up to four Xbox gamepads through XInput HLE
 * (src/nfl2k5_input_hle.c). This layer decides what is on each port:
 *
 *   - Xbox controllers through XInput (user indices 0..3), and
 *   - PlayStation controllers -- DualShock 4 (054C:05C4/09CC/0BA0) and
 *     DualSense / DualSense Edge (054C:0CE6/0DF2) -- read straight from their
 *     HID reports (USB and Bluetooth), since XInput does not see them.
 *
 * Ports are filled in that order; the keyboard and mouse add to port 0.
 * Per-user settings (controls.ini in the per-user folder, see kernel_path.c
 * xbox_user_path): vibration, left/right stick and trigger dead zones, the
 * mouse, the button mapping (any game button from any controller button),
 * and keyboard bindings. They are edited on the F1 page (Controls,
 * Controller Buttons, Keyboard pages) or in the file.
 *
 * Button prompt glyphs: the game draws Xbox button icons from its own
 * textures; replacing them for PlayStation controllers is not implemented,
 * and the Controls page says so ("Unavailable") rather than offering it.
 */
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xinput_xbox.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

extern char *xbox_user_path(const char *rel, char *out, size_t n);

/* ---- the game's buttons ---------------------------------------------------
 * Indices into a mapping table: what each game button reads from. Sources use
 * the same names on the physical controller (Xbox layout; a PlayStation pad's
 * Cross/Circle/Square/Triangle are A/B/X/Y, L1/R1 are LB/RB). */
enum { C_A, C_B, C_X, C_Y, C_LB, C_RB, C_LT, C_RT, C_START, C_BACK, C_LS, C_RS,
       C_DUP, C_DDOWN, C_DLEFT, C_DRIGHT, C_COUNT };
static const char *const k_ctl_names[C_COUNT] = {
    "A", "B", "X", "Y", "LB (Black)", "RB (White)", "LT", "RT", "Start", "Back",
    "Left stick click", "Right stick click", "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
static const char *const k_ctl_keys[C_COUNT] = {     /* controls.ini names */
    "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "ls", "rs", "dup", "ddown", "dleft", "dright" };
static const char *const k_ps_names[C_COUNT] = {
    "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Options", "Share/Create",
    "L3", "R3", "D-pad up", "D-pad down", "D-pad left", "D-pad right" };

/* Keyboard: the game buttons plus the left stick's four directions. */
enum { K_LUP = C_COUNT, K_LDOWN, K_LLEFT, K_LRIGHT, K_COUNT };
static const char *const k_key_names[K_COUNT] = {
    "A", "B", "X", "Y", "LB (Black)", "RB (White)", "LT", "RT", "Start", "Back",
    "Left stick click", "Right stick click", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Left stick up", "Left stick down", "Left stick left", "Left stick right" };
static const char *const k_key_ini[K_COUNT] = {
    "key_a", "key_b", "key_x", "key_y", "key_lb", "key_rb", "key_lt", "key_rt", "key_start", "key_back",
    "key_ls", "key_rs", "key_dup", "key_ddown", "key_dleft", "key_dright",
    "key_lup", "key_ldown", "key_lleft", "key_lright" };
/* The bindings the game shipped with in this port (see nfl2k5_input_hle.c history). */
static const int k_key_default[K_COUNT] = {
    'Z', 'X', 'C', 'V', 'R', 'F', 'Q', 'E', VK_RETURN, VK_BACK, 'T', 'G',
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'S', 'A', 'D' };

typedef struct {
    int vibration;                  /* 0/1 */
    int dz_left, dz_right, dz_trig; /* percent */
    int mouse;                      /* mouse buttons/stick on port 0 */
    int map[C_COUNT];               /* game button <- controller button */
    int keys[K_COUNT];              /* virtual-key codes, 0 = unbound */
} Controls;
static Controls s_cfg;
static CRITICAL_SECTION s_lock;
static LONG s_inited;

static void cfg_defaults(Controls *c)
{
    int i;
    c->vibration = 1;
    c->dz_left = 12; c->dz_right = 12; c->dz_trig = 5;
    c->mouse = 1;
    for (i = 0; i < C_COUNT; i++) c->map[i] = i;
    for (i = 0; i < K_COUNT; i++) c->keys[i] = k_key_default[i];
}

static void cfg_save(void)
{
    char p[MAX_PATH];
    FILE *f = fopen(xbox_user_path("controls.ini", p, sizeof p), "w");
    int i;
    if (!f) return;
    fprintf(f, "; NFL 2K5 controls (F1 > Controls pages edit these)\n"
               "vibration = %d\ndeadzone_left = %d\ndeadzone_right = %d\ndeadzone_trigger = %d\nmouse = %d\n"
               "; map_<game button> = <controller button>: a b x y lb rb lt rt start back ls rs dup ddown dleft dright\n",
            s_cfg.vibration, s_cfg.dz_left, s_cfg.dz_right, s_cfg.dz_trig, s_cfg.mouse);
    for (i = 0; i < C_COUNT; i++) fprintf(f, "map_%s = %s\n", k_ctl_keys[i], k_ctl_keys[s_cfg.map[i]]);
    fprintf(f, "; key_<game button> = Windows virtual-key code (decimal), 0 = none\n");
    for (i = 0; i < K_COUNT; i++) fprintf(f, "%s = %d\n", k_key_ini[i], s_cfg.keys[i]);
    fclose(f);
}

static void cfg_load(void)
{
    char p[MAX_PATH], line[160];
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

/* ---- a controller's raw state, in Xbox layout ----------------------------- */
typedef struct {
    int connected;
    char kind[32];                  /* "Xbox controller", "DualSense", ... */
    int ps;                         /* PlayStation layout (for labels) */
    unsigned char btn[C_COUNT];     /* 0..255 per control (sticks clicks and d-pad digital) */
    short lx, ly, rx, ry;
} RawPad;

/* ---- PlayStation controllers over HID ------------------------------------- */
#define MAX_PS 4
typedef struct {
    HANDLE h;
    int used;
    int dualsense, edge, bt;
    char path[512];
    volatile LONG gen;
    RawPad state;
    unsigned char out_left, out_right;  /* rumble asked for */
    int rumble_sent_l, rumble_sent_r;
    HANDLE thread;
} PsPad;
static PsPad s_ps[MAX_PS];

static void ps_parse(PsPad *d, const unsigned char *r, DWORD n)
{
    RawPad s;
    const unsigned char *p;
    int hat;
    memset(&s, 0, sizeof s);
    s.connected = 1;
    s.ps = 1;
    snprintf(s.kind, sizeof s.kind, "%s%s", d->dualsense ? (d->edge ? "DualSense Edge" : "DualSense") : "DualShock 4",
             d->bt ? " (Bluetooth)" : "");
    if (n < 10) return;
    if (d->dualsense && r[0] == 0x31 && n >= 12) p = r + 2;         /* DualSense BT full report */
    else if (d->dualsense && r[0] == 0x01 && n >= 11 && !d->bt) p = r + 1;  /* DualSense USB */
    else if (!d->dualsense && r[0] == 0x11 && n >= 12) p = r + 3;   /* DS4 BT full report */
    else if (r[0] == 0x01) p = r + 1;                               /* DS4 USB, or BT simple report */
    else return;
    s.lx = (short)((p[0] - 128) * 256 + (p[0] >= 128 ? 255 : 0));
    s.ly = (short)((127 - p[1]) * 256 + (p[1] <= 127 ? 255 : 0));
    s.rx = (short)((p[2] - 128) * 256 + (p[2] >= 128 ? 255 : 0));
    s.ry = (short)((127 - p[3]) * 256 + (p[3] <= 127 ? 255 : 0));
    {
        unsigned char b0, b1, l2, r2;
        if (d->dualsense && !(r[0] == 0x01 && d->bt)) { l2 = p[4]; r2 = p[5]; b0 = p[7]; b1 = p[8]; }
        else { b0 = p[4]; b1 = p[5]; l2 = p[7]; r2 = p[8]; }
        hat = b0 & 0x0F;
        if (b0 & 0x10) s.btn[C_X] = 255;           /* square */
        if (b0 & 0x20) s.btn[C_A] = 255;           /* cross */
        if (b0 & 0x40) s.btn[C_B] = 255;           /* circle */
        if (b0 & 0x80) s.btn[C_Y] = 255;           /* triangle */
        if (b1 & 0x01) s.btn[C_LB] = 255;
        if (b1 & 0x02) s.btn[C_RB] = 255;
        if (b1 & 0x10) s.btn[C_BACK] = 255;        /* share / create */
        if (b1 & 0x20) s.btn[C_START] = 255;       /* options */
        if (b1 & 0x40) s.btn[C_LS] = 255;
        if (b1 & 0x80) s.btn[C_RS] = 255;
        s.btn[C_LT] = l2; s.btn[C_RT] = r2;
    }
    if (hat == 0 || hat == 1 || hat == 7) s.btn[C_DUP] = 255;
    if (hat >= 1 && hat <= 3) s.btn[C_DRIGHT] = 255;
    if (hat >= 3 && hat <= 5) s.btn[C_DDOWN] = 255;
    if (hat >= 5 && hat <= 7) s.btn[C_DLEFT] = 255;
    EnterCriticalSection(&s_lock);
    d->state = s;
    LeaveCriticalSection(&s_lock);
    InterlockedIncrement(&d->gen);
}

/* Rumble, USB only: Bluetooth output reports need a CRC and a mode switch. */
static void ps_rumble(PsPad *d)
{
    unsigned char out[64];
    DWORD w;
    OVERLAPPED ov;
    if (d->bt || (d->out_left == d->rumble_sent_l && d->out_right == d->rumble_sent_r)) return;
    memset(out, 0, sizeof out);
    if (d->dualsense) {
        out[0] = 0x02; out[1] = 0x03;               /* compatible vibration, haptics select */
        out[3] = d->out_right; out[4] = d->out_left;
    } else {
        out[0] = 0x05; out[1] = 0x01;               /* rumble enable */
        out[4] = d->out_right; out[5] = d->out_left;
    }
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!WriteFile(d->h, out, d->dualsense ? 48 : 32, &w, &ov) && GetLastError() == ERROR_IO_PENDING)
        WaitForSingleObject(ov.hEvent, 50);
    CloseHandle(ov.hEvent);
    d->rumble_sent_l = d->out_left; d->rumble_sent_r = d->out_right;
}

static DWORD WINAPI ps_reader(void *arg)
{
    PsPad *d = (PsPad *)arg;
    unsigned char buf[128];
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (;;) {
        DWORD n = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(d->h, buf, sizeof buf, &n, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) break;
            if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0) { CancelIo(d->h); continue; }
            if (!GetOverlappedResult(d->h, &ov, &n, FALSE)) break;
        }
        ps_parse(d, buf, n);
        ps_rumble(d);
    }
    CloseHandle(ov.hEvent);
    EnterCriticalSection(&s_lock);
    CloseHandle(d->h);
    d->h = NULL;
    d->state.connected = 0;
    d->used = 0;
    LeaveCriticalSection(&s_lock);
    return 0;
}

static void ps_scan(void)
{
    GUID hid;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA ifd;
    DWORD i;
    HidD_GetHidGuid(&hid);
    set = SetupDiGetClassDevsA(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    ifd.cbSize = sizeof ifd;
    for (i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &ifd); i++) {
        char buf[1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)buf;
        HIDD_ATTRIBUTES at;
        HANDLE h;
        int k, known = 0, ds = 0, edge = 0;
        det->cbSize = sizeof *det;
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, sizeof buf, NULL, NULL)) continue;
        for (k = 0; k < MAX_PS; k++) if (s_ps[k].used && !_stricmp(s_ps[k].path, det->DevicePath)) known = 1;
        if (known) continue;
        h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        at.Size = sizeof at;
        if (!HidD_GetAttributes(h, &at) || at.VendorID != 0x054C ||
            !(at.ProductID == 0x05C4 || at.ProductID == 0x09CC || at.ProductID == 0x0BA0 ||
              at.ProductID == 0x0CE6 || at.ProductID == 0x0DF2)) { CloseHandle(h); continue; }
        ds = at.ProductID == 0x0CE6 || at.ProductID == 0x0DF2;
        edge = at.ProductID == 0x0DF2;
        {
            PHIDP_PREPARSED_DATA pp;
            HIDP_CAPS caps;
            /* Gamepads are the top-level collection Usage Page 1, Usage 5. */
            if (!HidD_GetPreparsedData(h, &pp)) { CloseHandle(h); continue; }
            HidP_GetCaps(pp, &caps);
            HidD_FreePreparsedData(pp);
            if (caps.UsagePage != 1 || caps.Usage != 5) { CloseHandle(h); continue; }
            EnterCriticalSection(&s_lock);
            for (k = 0; k < MAX_PS && s_ps[k].used; k++) {}
            if (k == MAX_PS) { LeaveCriticalSection(&s_lock); CloseHandle(h); continue; }
            memset(&s_ps[k], 0, sizeof s_ps[k]);
            s_ps[k].used = 1;
            s_ps[k].h = h;
            s_ps[k].dualsense = ds; s_ps[k].edge = edge;
            /* Bluetooth: a larger input report than over USB. */
            s_ps[k].bt = caps.InputReportByteLength > 64;
            strncpy(s_ps[k].path, det->DevicePath, sizeof s_ps[k].path - 1);
            s_ps[k].rumble_sent_l = s_ps[k].rumble_sent_r = -1;
            LeaveCriticalSection(&s_lock);
            fprintf(stderr, "[CONTROLS] %s connected (%04X:%04X, %s)\n", ds ? (edge ? "DualSense Edge" : "DualSense") : "DualShock 4",
                    at.VendorID, at.ProductID, s_ps[k].bt ? "Bluetooth" : "USB");
            {
                extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
                xbox_PresentToast("CONTROLLER", ds ? (edge ? "DualSense Edge connected" : "DualSense connected") : "DualShock 4 connected",
                                  "F1 > Controls to set it up", 4000);
            }
            s_ps[k].thread = CreateThread(NULL, 0, ps_reader, &s_ps[k], 0, NULL);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

static DWORD WINAPI scan_thread(void *arg)
{
    (void)arg;
    for (;;) { ps_scan(); Sleep(2000); }
}

/* ---- ports ---------------------------------------------------------------- */
typedef struct { int src; int index; } PortSrc;     /* src 0 none, 1 XInput user index, 2 PS slot */

/* Which XInput slots have a controller. Asking an empty slot is slow (XInput
 * looks for the device each time), so empty slots are re-checked once a
 * second rather than on every poll. */
static volatile LONG s_xi_conn[4];
static DWORD s_xi_checked;

static void xinput_refresh(void)
{
    DWORD now = GetTickCount();
    int i;
    if (s_xi_checked && now - s_xi_checked < 1000) return;
    s_xi_checked = now;
    for (i = 0; i < 4; i++) {
        XINPUT_STATE x;
        InterlockedExchange(&s_xi_conn[i], XInputGetState((DWORD)i, &x) == ERROR_SUCCESS);
    }
}

static void xinput_raw(int user, RawPad *s)
{
    XINPUT_STATE x;
    memset(s, 0, sizeof *s);
    if (XInputGetState((DWORD)user, &x) != ERROR_SUCCESS) { InterlockedExchange(&s_xi_conn[user & 3], 0); return; }
    s->connected = 1;
    snprintf(s->kind, sizeof s->kind, "Xbox controller");
#define B(bit, c) if (x.Gamepad.wButtons & (bit)) s->btn[c] = 255
    B(XINPUT_GAMEPAD_A, C_A); B(XINPUT_GAMEPAD_B, C_B); B(XINPUT_GAMEPAD_X, C_X); B(XINPUT_GAMEPAD_Y, C_Y);
    B(XINPUT_GAMEPAD_LEFT_SHOULDER, C_LB); B(XINPUT_GAMEPAD_RIGHT_SHOULDER, C_RB);
    B(XINPUT_GAMEPAD_START, C_START); B(XINPUT_GAMEPAD_BACK, C_BACK);
    B(XINPUT_GAMEPAD_LEFT_THUMB, C_LS); B(XINPUT_GAMEPAD_RIGHT_THUMB, C_RS);
    B(XINPUT_GAMEPAD_DPAD_UP, C_DUP); B(XINPUT_GAMEPAD_DPAD_DOWN, C_DDOWN);
    B(XINPUT_GAMEPAD_DPAD_LEFT, C_DLEFT); B(XINPUT_GAMEPAD_DPAD_RIGHT, C_DRIGHT);
#undef B
    s->btn[C_LT] = x.Gamepad.bLeftTrigger;
    s->btn[C_RT] = x.Gamepad.bRightTrigger;
    s->lx = x.Gamepad.sThumbLX; s->ly = x.Gamepad.sThumbLY;
    s->rx = x.Gamepad.sThumbRX; s->ry = x.Gamepad.sThumbRY;
}

/* Which controller is on which game port: Xbox controllers first (in their
 * XInput order), then PlayStation controllers in the order they arrived. */
static int port_map(PortSrc ports[4])
{
    int n = 0, i;
    memset(ports, 0, sizeof(PortSrc) * 4);
    xinput_refresh();
    for (i = 0; i < 4 && n < 4; i++)
        if (s_xi_conn[i]) { ports[n].src = 1; ports[n].index = i; n++; }
    EnterCriticalSection(&s_lock);
    for (i = 0; i < MAX_PS && n < 4; i++)
        if (s_ps[i].used && s_ps[i].state.connected) { ports[n].src = 2; ports[n].index = i; n++; }
    LeaveCriticalSection(&s_lock);
    return n;
}

static void raw_for(const PortSrc *p, RawPad *s)
{
    memset(s, 0, sizeof *s);
    if (p->src == 1) xinput_raw(p->index, s);
    else if (p->src == 2) { EnterCriticalSection(&s_lock); *s = s_ps[p->index].state; LeaveCriticalSection(&s_lock); }
}

/* Radial dead zone, rescaled so the edge of the zone reads as zero. */
static void deadzone(short *x, short *y, int pct)
{
    double fx = *x, fy = *y, m = sqrt(fx * fx + fy * fy), dz = 32767.0 * pct / 100.0, k;
    if (m <= dz || m < 1.0) { *x = 0; *y = 0; return; }
    k = (m - dz) / (32767.0 - dz) / m * 32767.0;
    if (m * k > 32767.0) k = 32767.0 / m;
    *x = (short)(fx * k); *y = (short)(fy * k);
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
    g->bAnalogButtons[4] = v[C_LB]; g->bAnalogButtons[5] = v[C_RB];
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
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static void keyboard_into(XBOX_GAMEPAD *g)
{
#define KEY(i) (s_cfg.keys[i] && (GetAsyncKeyState(s_cfg.keys[i]) & 0x8000))
    static const int analog[8] = { C_A, C_B, C_X, C_Y, C_LB, C_RB, C_LT, C_RT };
    int i;
    for (i = 0; i < 8; i++) if (KEY(analog[i])) g->bAnalogButtons[i] = 255;
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
    HWND hwnd = GetForegroundWindow();
    POINT pt;
    RECT rc;
    LONG cx, cy, dx, dy;
    if (!s_cfg.mouse || !hwnd) return;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) g->bAnalogButtons[0] = 255;    /* A / confirm */
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) g->bAnalogButtons[1] = 255;    /* B / back */
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) g->wButtons |= 0x10;            /* START */
    /* Shift + mouse position: the left stick, relative to the window centre. */
    if (!(GetAsyncKeyState(VK_SHIFT) & 0x8000) || !GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt) ||
        !GetClientRect(hwnd, &rc)) return;
    cx = (rc.right - rc.left) / 2; cy = (rc.bottom - rc.top) / 2;
    if (cx <= 0 || cy <= 0) return;
    dx = pt.x - cx; dy = cy - pt.y;
    if (labs(dx) > cx * 15 / 100) g->sThumbLX = (SHORT)((dx * 32767L) / cx);
    if (labs(dy) > cy * 15 / 100) g->sThumbLY = (SHORT)((dy * 32767L) / cy);
}

static void controls_init(void)
{
    if (InterlockedCompareExchange(&s_inited, 1, 0) != 0) return;
    InitializeCriticalSection(&s_lock);
    cfg_load();
    {
        extern void nfl2k5_controls_menu_init(void);
        nfl2k5_controls_menu_init();
    }
    {
        HANDLE t = CreateThread(NULL, 0, scan_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

/* Bitmask of game ports with a controller: port 0 always (keyboard). */
unsigned nfl2k5_controls_ports(void)
{
    PortSrc p[4];
    int n, i;
    unsigned m = 1;
    controls_init();
    n = port_map(p);
    for (i = 1; i < n; i++) m |= 1u << i;
    return m;
}

/* The game's view of port `port`. use_host: 0 = only keyboard/mouse (pads
 * ignored while a script drives). Returns nonzero when the port is live. */
int nfl2k5_controls_get(int port, XBOX_GAMEPAD *g, int use_pads)
{
    PortSrc p[4];
    RawPad s;
    controls_init();
    memset(g, 0, sizeof *g);
    port_map(p);
    if (use_pads && port >= 0 && port < 4 && p[port].src) {
        raw_for(&p[port], &s);
        if (s.connected) to_gamepad(&s, g);
    }
    if (port == 0 && window_focused()) {
        keyboard_into(g);
        mouse_into(g);
    }
    return port == 0 || p[port & 3].src != 0;
}

/* XInputSetState from the game: motor speeds 0..65535. */
void nfl2k5_controls_rumble(int port, unsigned left, unsigned right)
{
    PortSrc p[4];
    controls_init();
    if (!s_cfg.vibration) left = right = 0;
    port_map(p);
    if (port < 0 || port > 3) return;
    if (p[port].src == 1) {
        XINPUT_VIBRATION v;
        v.wLeftMotorSpeed = (WORD)left; v.wRightMotorSpeed = (WORD)right;
        XInputSetState((DWORD)p[port].index, &v);
    } else if (p[port].src == 2) {
        PsPad *d = &s_ps[p[port].index];
        d->out_left = (unsigned char)(left >> 8);
        d->out_right = (unsigned char)(right >> 8);
    }
}

/* For the F1 page (any controller drives it). */
static int menu_pad(XBOX_GAMEPAD *g)
{
    PortSrc p[4];
    RawPad s;
    int n = port_map(p), i;
    memset(g, 0, sizeof *g);
    for (i = 0; i < n; i++) {
        XBOX_GAMEPAD one;
        raw_for(&p[i], &s);
        if (!s.connected) continue;
        to_gamepad(&s, &one);
        g->wButtons |= one.wButtons;
        { int k; for (k = 0; k < 8; k++) if (one.bAnalogButtons[k] > g->bAnalogButtons[k]) g->bAnalogButtons[k] = one.bAnalogButtons[k]; }
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

/* Binding capture: waiting for a controller button (page 2) or a key (page 3). */
static volatile LONG s_capture_pad = -1, s_capture_key = -1;
static DWORD s_capture_start;

static DWORD WINAPI capture_thread(void *arg)
{
    (void)arg;
    for (;;) {
        Sleep(15);
        if (s_capture_pad >= 0) {
            PortSrc p[4];
            int n = port_map(p), i, c, idle = GetTickCount() - s_capture_start < 300;
            for (i = 0; i < n && !idle; i++) {
                RawPad s;
                raw_for(&p[i], &s);
                for (c = 0; c < C_COUNT; c++)
                    if (s.btn[c] > 200) {
                        s_cfg.map[s_capture_pad] = c;
                        InterlockedExchange(&s_capture_pad, -1);
                        cfg_save();
                        xbox_MenuInvalidate();
                        break;
                    }
                if (s_capture_pad < 0) break;
            }
            if (GetTickCount() - s_capture_start > 6000) { InterlockedExchange(&s_capture_pad, -1); xbox_MenuInvalidate(); }
        }
        if (s_capture_key >= 0) {
            int vk;
            if (GetTickCount() - s_capture_start < 300) continue;
            for (vk = 8; vk < 256; vk++) {
                if (vk == VK_F1 || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                    vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
                if (GetAsyncKeyState(vk) & 0x8000) {
                    if (vk != VK_ESCAPE) { s_cfg.keys[s_capture_key] = vk; cfg_save(); }
                    InterlockedExchange(&s_capture_key, -1);
                    xbox_MenuInvalidate();
                    break;
                }
            }
            if (GetTickCount() - s_capture_start > 6000) { InterlockedExchange(&s_capture_key, -1); xbox_MenuInvalidate(); }
        }
    }
}

static void vk_name(int vk, char *out, size_t n)
{
    UINT sc;
    LONG lp;
    if (!vk) { snprintf(out, n, "(none)"); return; }
    sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    lp = (LONG)(sc << 16);
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END)
        lp |= 1 << 24;                                /* extended key: arrow names */
    if (!GetKeyNameTextA(lp, out, (int)n)) snprintf(out, n, "key %d", vk);
}

static const char *pad_kind(void)
{
    PortSrc p[4];
    int n = port_map(p), i;
    static char buf[160];
    buf[0] = 0;
    for (i = 0; i < n; i++) {
        RawPad s;
        raw_for(&p[i], &s);
        if (!s.connected) continue;
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%d: %s", buf[0] ? ", " : "", i + 1, s.kind);
    }
    if (!buf[0]) snprintf(buf, sizeof buf, "none (keyboard on port 1)");
    return buf;
}

static int any_ps(void)
{
    int i, r = 0;
    EnterCriticalSection(&s_lock);
    for (i = 0; i < MAX_PS; i++) if (s_ps[i].used && s_ps[i].state.connected) r = 1;
    LeaveCriticalSection(&s_lock);
    return r;
}

/* Page 1: Controls */
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
    case R_PROMPTS: strcpy(name, "PlayStation button prompts"); snprintf(val, n, "Unavailable"); break;
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

/* Page 2: Controller Buttons */
static int p2_count(void) { return C_COUNT + 1; }
static void p2_label(int r, char *name, char *val, size_t n)
{
    int ps = any_ps();
    if (r == C_COUNT) { strcpy(name, "Default layout"); val[0] = 0; return; }
    snprintf(name, 64, "Game %s", k_ctl_names[r]);
    if (s_capture_pad == r) snprintf(val, n, "press a controller button...");
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
    if (r == -1) { InterlockedExchange(&s_capture_pad, -1); return; }
    if (r == C_COUNT) { int i; for (i = 0; i < C_COUNT; i++) s_cfg.map[i] = i; cfg_save(); xbox_MenuInvalidate(); return; }
    s_capture_start = GetTickCount();
    InterlockedExchange(&s_capture_pad, r);
    xbox_MenuInvalidate();
}
static int p2_busy(void) { return s_capture_pad >= 0; }
static const XboxMenuPage k_p2 = { "CONTROLLER BUTTONS", p2_count, p2_label, p2_change, p2_activate, p2_busy };

/* Page 3: Keyboard */
static int p3_count(void) { return K_COUNT + 1; }
static void p3_label(int r, char *name, char *val, size_t n)
{
    if (r == K_COUNT) { strcpy(name, "Default keys"); val[0] = 0; return; }
    snprintf(name, 64, "%s", k_key_names[r]);
    if (s_capture_key == r) snprintf(val, n, "press a key...  (Esc: cancel)");
    else vk_name(s_cfg.keys[r], val, n);
}
static void p3_activate(int r)
{
    if (r == -1) { InterlockedExchange(&s_capture_key, -1); return; }
    if (r == K_COUNT) { int i; for (i = 0; i < K_COUNT; i++) s_cfg.keys[i] = k_key_default[i]; cfg_save(); xbox_MenuInvalidate(); return; }
    s_capture_start = GetTickCount();
    InterlockedExchange(&s_capture_key, r);
    xbox_MenuInvalidate();
}
static void p3_change(int r, int dir) { (void)dir; if (r >= 0 && r < K_COUNT) { s_cfg.keys[r] = 0; cfg_save(); xbox_MenuInvalidate(); } }
static int p3_busy(void) { return s_capture_key >= 0; }
static const XboxMenuPage k_p3 = { "KEYBOARD", p3_count, p3_label, p3_change, p3_activate, p3_busy };

void nfl2k5_controls_menu_init(void)
{
    HANDLE t;
    xbox_MenuAddPage(&k_p1);
    xbox_MenuAddPage(&k_p2);
    xbox_MenuAddPage(&k_p3);
    g_xbox_menu_pad = menu_pad;
    t = CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* Started at boot (src/main.c) so devices and pages are ready before the
 * game first polls. */
void nfl2k5_controls_start(void) { controls_init(); }

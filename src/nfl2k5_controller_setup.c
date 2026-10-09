/*
 * Controller Setup: a fourth layout, "Custom Control" (2026-10-09).
 *
 * The game's Options > Controller Setup screen (handlers 0x3573A0..0x357530,
 * state at 0xAEA000..0xAEA034) pages with LT/RT through three layouts --
 * Default, Alternate A, Alternate B -- and stores the choice per controller
 * (0x77240: [0xE5FE90 + port*0x1C]). Two tables drive a layout:
 *
 *   - labels: 0x580E30 + layout*0x98, 19 rows x {offense, defense} UTF-16
 *     strings with button tokens ("Spin |CIRCLE|"), drawn at the positions
 *     in 0x580AA0 (0x30 per row); names at 0x580FF8.
 *   - gameplay: 0xA99EC0 + (layout*21 + situation)*0x6C, 27 dwords per
 *     situation: the action on each button slot (masks at 0x4FAAC0; slots
 *     0..7 are A B X Y LT RT White Black). The game looks an action up and
 *     tests that slot's button (0x120730, 0x120960, 0x1209B0, 0x120A20).
 *
 * Custom Control is the Default layout with slots 0..7 permuted, per
 * controller port. Generated-code patches (tools/apply-gen-patches.py,
 * CONTROLLER_SETUP_*) page through four layouts, name and label layout 3 from
 * here, and make the per-port layout read (0x77230) answer, for a port on
 * Custom, a layout index whose table lands on this port's custom table in
 * guest memory -- so the game itself plays the custom layout, unchanged.
 *
 * On the Custom page, X opens the editor: the D-pad picks a button (its row
 * and the button on the controller picture light up), A then the new button
 * swaps the two actions, Y restores the Default layout, B finishes. The
 * picture is the controller on that port (src/nfl2k5_controller_art.cpp).
 * Layouts are kept in controller_layouts.ini in the per-user folder.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xinput_xbox.h"

extern void *xbox_GetMemoryOffset(void);
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern char *xbox_user_path(const char *rel, char *out, size_t n);

#define G8(va)  (((uint8_t *)xbox_GetMemoryOffset()) + (uint32_t)(va))
#define G32(va) (*(volatile uint32_t *)G8(va))

#define CS_STATE_SIDE   0xAEA000u   /* 0 offense, 1 defense */
#define CS_STATE_PIC    0xAEA028u   /* the controller picture; 0 = screen not shown */
#define CS_STATE_PORT   0xAEA030u
#define CS_STATE_LAYOUT 0xAEA034u
#define CS_LABELS       0x580E30u
#define CS_ROWPOS       0x580AA0u
#define CS_NAMES        0x580FF8u
#define CS_HELP_OFF     0x52C6E0u   /* help bar text: "for Offensive Assignments" */
#define CS_HELP_DEF     0x52C6ECu   /* "for Defensive Assignments" */
#define CS_PLAY         0xA99EC0u
#define CS_PLAY_LAYOUT  (21u * 0x6Cu)
#define CS_BLANK        0xEB4320u   /* the tables' empty string */
#define CS_SIDE_LABEL   0xEB4798u   /* "Offense:" under the rows */

/* Slots 0..7: A B X Y LT RT White Black. Their label row, token, and the
 * order the editor steps through them (top to bottom on screen). */
static const int k_row[8] = { 9, 10, 11, 12, 0, 8, 13, 14 };
static const char *const k_tok[8] = { "|CROSS|", "|CIRCLE|", "|SQUARE|", "|TRIANGLE|", "|L1|", "|R1|", "|L2|", "|R2|" };
static const int k_order[8] = { 4, 5, 0, 1, 2, 3, 6, 7 };

static int s_ok;
static uint32_t s_names, s_labels, s_pool, s_help, s_tables;
static int32_t s_k0;
static uint8_t s_perm[4][8];
/* Ports whose chosen layout is Custom. The game keeps the choice in memory
 * only (it resets at boot), so this remembers it (controller_layouts.ini)
 * and the per-port layout read follows it. Read by generated code. */
volatile long nfl2k5_cs_custom_ports;
static volatile LONG s_play_dirty = 0xF, s_label_port = -1;
static uint32_t s_orig_help_off, s_orig_help_def;

/* editor */
static volatile LONG s_editing, s_listen, s_sel;   /* s_sel: a slot */
static WORD s_prev_buttons;
static BYTE s_prev_analog[8];
static int s_armed, s_stick_prev, s_hold;
static volatile LONG s_gen;

static void wput(uint32_t va, const char *s)
{
    uint16_t *d = (uint16_t *)G8(va);
    while (*s) *d++ = (uint16_t)(unsigned char)*s++;
    *d = 0;
}

static void wget(uint32_t va, char *out, size_t n)
{
    const uint16_t *s = (const uint16_t *)G8(va);
    size_t i = 0;
    if (!va) { out[0] = 0; return; }
    while (s[i] && i + 1 < n) { out[i] = s[i] < 128 ? (char)s[i] : '?'; i++; }
    out[i] = 0;
}

/* "Spin |CIRCLE|" -> "Spin" */
static void action_text(uint32_t va, char *out, size_t n)
{
    char t[128], *o = out;
    const char *p = t;
    wget(va, t, sizeof t);
    while (*p && (size_t)(o - out) + 1 < n) {
        if (*p == '|') { const char *e = strchr(p + 1, '|'); if (!e) break; p = e + 1; continue; }
        *o++ = *p++;
    }
    *o = 0;
    while (o > out && o[-1] == ' ') *--o = 0;
    o = out;
    while (*o == ' ') o++;
    memmove(out, o, strlen(o) + 1);
}

static void layouts_save(void)
{
    char p[MAX_PATH];
    FILE *f = fopen(xbox_user_path("controller_layouts.ini", p, sizeof p), "w");
    int port, i;
    if (!f) return;
    fprintf(f, "; Controller Setup > Custom Control, per controller port: for the buttons\n"
               "; A B X Y LT RT White(LB) Black(RB), which Default-layout button's action each one has\n"
               "; (0 A, 1 B, 2 X, 3 Y, 4 LT, 5 RT, 6 White, 7 Black).\n");
    fprintf(f, "; use_custom_portN = 1: that port plays Custom Control (chosen on the setup screen)\n");
    for (port = 0; port < 4; port++) {
        fprintf(f, "use_custom_port%d = %d\n", port + 1, (int)((nfl2k5_cs_custom_ports >> port) & 1));
        fprintf(f, "custom_port%d =", port + 1);
        for (i = 0; i < 8; i++) fprintf(f, " %d", s_perm[port][i]);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void layouts_load(void)
{
    char p[MAX_PATH], line[200];
    FILE *f;
    int port, i;
    for (port = 0; port < 4; port++) for (i = 0; i < 8; i++) s_perm[port][i] = (uint8_t)i;
    f = fopen(xbox_user_path("controller_layouts.ini", p, sizeof p), "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int v[8], seen = 0, use;
        if (sscanf(line, " use_custom_port%d = %d", &port, &use) == 2) {
            if (port >= 1 && port <= 4 && use) nfl2k5_cs_custom_ports |= 1L << (port - 1);
            continue;
        }
        if (sscanf(line, " custom_port%d = %d %d %d %d %d %d %d %d", &port, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 9)
            continue;
        if (port < 1 || port > 4) continue;
        for (i = 0; i < 8; i++) { if (v[i] < 0 || v[i] > 7 || (seen & (1 << v[i]))) break; seen |= 1 << v[i]; }
        if (i < 8) continue;                         /* not a permutation */
        for (i = 0; i < 8; i++) s_perm[port - 1][i] = (uint8_t)v[i];
    }
    fclose(f);
}

/* The custom gameplay tables: Default's, with slots 0..7 permuted. */
static void build_play(int port)
{
    const uint32_t *d = (const uint32_t *)G8(CS_PLAY);
    uint32_t *t = (uint32_t *)G8(s_tables + (uint32_t)port * CS_PLAY_LAYOUT);
    int ctx, s;
    for (ctx = 0; ctx < 21; ctx++)
        for (s = 0; s < 27; s++)
            t[ctx * 27 + s] = d[ctx * 27 + (s < 8 ? s_perm[port][s] : s)];
}

/* Layout 3's labels for this port: Default's, each button showing the
 * action it now has. */
static void build_labels(int port)
{
    uint32_t *lab = (uint32_t *)G8(s_labels + 3 * 0x98);
    const uint32_t *def = (const uint32_t *)G8(CS_LABELS);
    int p, side;
    memcpy(lab, def, 0x98);
    for (p = 0; p < 8; p++)
        for (side = 0; side < 2; side++) {
            char act[96], txt[140];
            uint32_t buf = s_pool + (uint32_t)(p * 2 + side) * 256;
            action_text(def[k_row[s_perm[port][p]] * 2 + side], act, sizeof act);
            if (!act[0]) { lab[k_row[p] * 2 + side] = CS_BLANK; continue; }
            if (k_row[p] == 0) snprintf(txt, sizeof txt, "%s %s", k_tok[p], act);   /* left column */
            else snprintf(txt, sizeof txt, "%s %s", act, k_tok[p]);
            wput(buf, txt);
            lab[k_row[p] * 2 + side] = buf;
        }
}

static int screen_up(void) { return s_ok && G32(CS_STATE_PIC) != 0; }
static int screen_port(void) { return (int)(G32(CS_STATE_PORT) & 3u); }

/* ---- called from generated code (apply-gen-patches.py) ------------------- */

int nfl2k5_cs_pages(void) { return s_ok ? 4 : 3; }

uint32_t nfl2k5_cs_name(void) { return s_ok ? G32(s_names + 12) : 0; }

uint32_t nfl2k5_cs_labels(void)
{
    int port;
    if (!s_ok) return 0;
    port = screen_port();
    build_labels(port);                               /* cheap; follows edits at once */
    s_label_port = port;
    return s_labels + 3 * 0x98;
}

/* 0x77230: the layout of player `port`. ret = the caller's return address. */
uint32_t nfl2k5_cs_port_layout(uint32_t port, uint32_t stored, uint32_t ret)
{
    int custom = stored == 3 || (port < 4 && ((nfl2k5_cs_custom_ports >> port) & 1));
    if (ret == 0x003573B1u)                           /* the setup screen: its page */
        return s_ok && custom ? 3u : stored <= 2 ? stored : 0u;
    if (!s_ok || !custom) return stored <= 2 ? stored : 0u;
    port &= 3;
    if (s_play_dirty & (1 << port)) {
        static LONG told;
        if (!(InterlockedOr(&told, 1 << port) & (1 << port)))
            printf("[CTRLSETUP] player %u plays Custom Control (layout index %d)\n", port, s_k0 + (int)port);
        build_play((int)port);
        InterlockedAnd(&s_play_dirty, ~(1 << port));
    }
    return (uint32_t)(s_k0 + (int32_t)port);
}

/* The setup screen applied a layout to a port (0x357370). */
void nfl2k5_cs_applied(uint32_t port, uint32_t layout)
{
    long was = nfl2k5_cs_custom_ports;
    if (!s_ok || port > 3) return;
    if (layout == 3) InterlockedOr(&nfl2k5_cs_custom_ports, 1L << port);
    else InterlockedAnd(&nfl2k5_cs_custom_ports, ~(1L << port));
    if (nfl2k5_cs_custom_ports != was) layouts_save();
}

/* Row colour (0x3576D3): the button being edited lights up. */
uint32_t nfl2k5_cs_row_color(uint32_t rowpos, uint32_t color)
{
    int row;
    if (!s_editing || !screen_up() || G32(CS_STATE_LAYOUT) != 3) return color;
    row = (int)((rowpos - CS_ROWPOS) / 0x30);
    if (row != k_row[s_sel & 7]) return color;
    if (s_listen && (GetTickCount() / 160) % 2) return 0xFFFFFFFFu;
    return 0xFFFFD23Cu;
}

/* ---- for the controller picture (nfl2k5_controller_art.cpp) --------------- */

/* The slot to light on the picture (-1 none), and a counter that changes
 * whenever the picture should change. */
int nfl2k5_cs_highlight(void)
{
    if (!s_editing || !screen_up()) return -1;
    if (s_listen && (GetTickCount() / 160) % 2) return -1;
    return (int)(s_sel & 7);
}
int nfl2k5_cs_screen_port(void) { return screen_up() ? screen_port() : -1; }

/* ---- help bar text ------------------------------------------------------ */

static void help_update(void)
{
    /* The menu keeps the help texts' pointers from when the screen opened, so
     * they point at these buffers from boot and only the text changes. */
    static int shown = -1;
    int want;
    char t[200];
    if (!s_ok) return;
    want = !screen_up() ? 0 : G32(CS_STATE_LAYOUT) != 3 ? 0 : !s_editing ? 1 : s_listen ? 3 : 2;
    if (want == shown && want < 2) return;
    if (want == 0) {
        char o[96];
        wget(s_orig_help_off, o, sizeof o); wput(s_help, o);
        wget(s_orig_help_def, o, sizeof o); wput(s_help + 512, o);
    } else if (want == 1) {
        wput(s_help, "for Offense   |SQUARE| Customize");
        wput(s_help + 512, "for Defense   |SQUARE| Customize");
    } else {
        const uint32_t *def = (const uint32_t *)G8(CS_LABELS);
        char act[96];
        action_text(def[k_row[s_perm[screen_port()][s_sel & 7]] * 2 + (G32(CS_STATE_SIDE) & 1)], act, sizeof act);
        /* the menu shows one of the two, after "Press (A)" or "Press (Y)" */
        if (want == 2) {
            wput(s_help, "to Change  |DPAD| Choose  |TRIANGLE| Default  |CIRCLE| Done");
            wput(s_help + 512, "for Default  |DPAD| Choose  |CROSS| Change  |CIRCLE| Done");
        } else {
            snprintf(t, sizeof t, "or any button for %s  |START| Back", act[0] ? act : "it");
            wput(s_help, t);
            wput(s_help + 512, t);
        }
    }
    shown = want;
    *(uint16_t *)G8(CS_SIDE_LABEL) = want >= 2 ? 0 : 'O';
}

/* ---- the editor: takes the screen's port while it runs ------------------- */

static int slot_down(const XBOX_GAMEPAD *g, const BYTE prev[8], int slot)
{
    static const int ai[8] = { 0, 1, 2, 3, 6, 7, 5, 4 };   /* slot -> analog button (White 5, Black 4) */
    return g->bAnalogButtons[ai[slot]] > 128 && prev[ai[slot]] <= 128;
}

static int any_slot_held(const XBOX_GAMEPAD *g)
{
    int i;
    for (i = 0; i < 8; i++) if (g->bAnalogButtons[i] > 128) return 1;
    return 0;
}

void nfl2k5_cs_input(int port, XBOX_GAMEPAD *g)
{
    WORD pressed;
    int stick, i;
    if (!s_ok) return;
    help_update();
    if (!screen_up() || port != screen_port() || G32(CS_STATE_LAYOUT) != 3) {
        if (s_editing && (!screen_up() || G32(CS_STATE_LAYOUT) != 3)) {
            s_editing = 0; s_listen = 0; InterlockedIncrement(&s_gen);
        }
        if (port == screen_port()) { s_prev_buttons = g->wButtons; memcpy(s_prev_analog, g->bAnalogButtons, 8); }
        return;
    }
    if (s_hold) {                                    /* the press the editor took: until released */
        if (!any_slot_held(g) && !(g->wButtons & 0x3F)) s_hold = 0;
        s_prev_buttons = g->wButtons; memcpy(s_prev_analog, g->bAnalogButtons, 8);
        memset(g, 0, sizeof *g);
        return;
    }
    pressed = (WORD)(g->wButtons & ~s_prev_buttons);
    stick = g->sThumbLY > 20000 ? 1 : g->sThumbLY < -20000 ? -1 : 0;
    if (!s_editing) {
        if (g->bAnalogButtons[2] > 128 && s_prev_analog[2] <= 128) {   /* X: customize */
            s_editing = 1; s_listen = 0; s_sel = k_order[0]; s_hold = 1;
            G32(CS_STATE_SIDE) = 0;
            InterlockedIncrement(&s_gen);
        }
        s_prev_buttons = g->wButtons; memcpy(s_prev_analog, g->bAnalogButtons, 8);
        g->bAnalogButtons[2] = 0;                    /* X belongs to the editor here */
        return;
    }
    if (!s_listen) {
        int step = 0, k;
        if ((pressed & 0x01) || (stick == 1 && s_stick_prev != 1)) step = -1;
        if ((pressed & 0x02) || (stick == -1 && s_stick_prev != -1)) step = 1;
        if (step) {
            for (k = 0; k < 8 && k_order[k] != s_sel; k++) {}
            s_sel = k_order[(k + 8 + step) % 8];
            InterlockedIncrement(&s_gen);
        }
        if (slot_down(g, s_prev_analog, 0)) { s_listen = 1; s_armed = 0; s_hold = 1; InterlockedIncrement(&s_gen); }
        else if (slot_down(g, s_prev_analog, 3)) {                       /* Y: Default layout */
            for (i = 0; i < 8; i++) s_perm[port][i] = (uint8_t)i;
            InterlockedOr(&s_play_dirty, 1 << port);
            layouts_save();
            s_hold = 1;
            InterlockedIncrement(&s_gen);
        } else if (slot_down(g, s_prev_analog, 1)) {                     /* B: done */
            s_editing = 0;
            s_hold = 1;
            layouts_save();
            InterlockedIncrement(&s_gen);
        }
    } else {
        if (!s_armed) s_armed = !any_slot_held(g);
        else {
            for (i = 0; i < 8; i++)
                if (slot_down(g, s_prev_analog, i)) {
                    uint8_t t = s_perm[port][s_sel];
                    s_perm[port][s_sel] = s_perm[port][i];
                    s_perm[port][i] = t;
                    s_sel = i;                       /* the action moved there; keep it lit */
                    s_listen = 0;
                    s_hold = 1;
                    InterlockedOr(&s_play_dirty, 1 << port);
                    layouts_save();
                    InterlockedIncrement(&s_gen);
                    break;
                }
        }
        if (pressed & 0x30) { s_listen = 0; s_hold = 1; InterlockedIncrement(&s_gen); }   /* Start/Back: cancel */
    }
    s_prev_buttons = g->wButtons; memcpy(s_prev_analog, g->bAnalogButtons, 8);
    s_stick_prev = stick;
    memset(g, 0, sizeof *g);                         /* the game sees nothing while editing */
}

uint32_t nfl2k5_cs_gen(void) { return (uint32_t)s_gen; }

/* ---- boot ---------------------------------------------------------------- */

/* After the XBE is mapped (src/main.c). Checks the tables are the retail ones. */
void nfl2k5_controller_setup_init(void)
{
    uint32_t mem;
    int64_t diff;
    if (G32(CS_LABELS) != 0xEB42ECu || G32(CS_NAMES) != 0xEB4694u || G32(CS_NAMES + 8) != 0xEB46DCu ||
        G32(CS_HELP_OFF) != 0xE9CE68u || G32(CS_HELP_DEF) != 0xE9CE9Cu) {
        fprintf(stderr, "[CTRLSETUP] tables are not the retail ones; Custom Control off\n");
        return;
    }
    s_orig_help_off = G32(CS_HELP_OFF);
    s_orig_help_def = G32(CS_HELP_DEF);
    s_names = xbox_HeapAlloc(16 + 64, 16);
    s_labels = xbox_HeapAlloc(4 * 0x98, 16);
    s_pool = xbox_HeapAlloc(16 * 256, 16);
    s_help = xbox_HeapAlloc(3 * 512, 16);
    mem = xbox_HeapAlloc(5 * CS_PLAY_LAYOUT, 16);
    if (!s_names || !s_labels || !s_pool || !s_help || !mem) {
        fprintf(stderr, "[CTRLSETUP] no guest memory; Custom Control off\n");
        return;
    }
    memcpy(G8(s_names), G8(CS_NAMES), 12);
    wput(s_names + 16, "Custom Control");
    G32(s_names + 12) = s_names + 16;
    memcpy(G8(s_labels), G8(CS_LABELS), 3 * 0x98);
    /* layout k's table is at CS_PLAY + k * CS_PLAY_LAYOUT: pick the first k
     * at or after `mem` (the game multiplies with wrap-around, like the CPU) */
    diff = (int64_t)mem - (int64_t)CS_PLAY;
    s_k0 = (int32_t)((diff + CS_PLAY_LAYOUT - 1) / (int64_t)CS_PLAY_LAYOUT);
    s_tables = CS_PLAY + (uint32_t)s_k0 * CS_PLAY_LAYOUT;
    layouts_load();
    {
        char o[96];
        wget(s_orig_help_off, o, sizeof o); wput(s_help, o);
        wget(s_orig_help_def, o, sizeof o); wput(s_help + 512, o);
        G32(CS_HELP_OFF) = s_help;
        G32(CS_HELP_DEF) = s_help + 512;
    }
    s_ok = 1;
    printf("[CTRLSETUP] Custom Control ready: tables at %08X (layout index %d)\n", s_tables, s_k0);
}

/* ESPN NFL 2K5 Host Entry Point (Title 53450030)
 * Cross-platform (Windows & Linux) clean host implementation.
 */

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <bcrypt.h>
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/xbox_winnt.h"
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#define _putenv_s(k, v) setenv((k), (v), 1)
#define _stricmp strcasecmp
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "kernel.h"
#include "xbox_memory_layout.h"
#include "platform/guest_memory.h"
#include "apu.h"
#include "recomp_icall_feedback.h"
#include "nfl2k5_presentation.h"
#include "nfl2k5_widescreen_patch.h"
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>      /* main() is SDL_main: SDLActivity calls it */
#include <android/log.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#endif
#if defined(__aarch64__) && !defined(_WIN32)
int xbox_arm64_mmio_fault(void *raw_ctx, uintptr_t host, uint32_t va);   /* apu/arm64_mmio.c */
#endif

#define NFL2K5_RETAIL_XBE_SHA256 "73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9"
#ifndef NFL2K5_XBE_SHA256
#define NFL2K5_XBE_SHA256 NFL2K5_WIDESCREEN_XBE_SHA256
#endif
#define NFL2K5_BASE_CMP(h) (strcmp((h), NFL2K5_RETAIL_XBE_SHA256) && strcmp((h), NFL2K5_WIDESCREEN_XBE_SHA256))

/* ============================================================================
 * External Runtime & Recompiler Symbols
 * ============================================================================ */

typedef void (*guest_function)(void);
extern guest_function recomp_lookup(uint32_t address);
extern int            recomp_dispatch_init(void);
extern void           xbox_path_init(const char *game_dir, const char *save_dir);
extern void           xbox_set_user_dir(const char *dir);
extern char          *xbox_user_path(const char *rel, char *out, size_t n);
extern int            xbox_overlay_load(const char *dir, const char *disc);
extern void           xbox_ggl_enter(void);
extern int            xbox_ggl_release_all(void);
extern int            xbox_VideoFpsLimit(void);
extern void         (*g_xbox_fps_limit_changed)(void);

extern MCPXAPUState  *g_apu_state;
extern bool           apu_hook_handle_mmio(PCONTEXT, uintptr_t, uint32_t, int);
extern int          (*g_xbox_apu_irq_line)(void);
extern int            mcpx_apu_irq_line(void);
extern void         (*g_apu_irq_wake)(void);
extern void           xbox_kernel_irq_wake(void);
extern void         (*g_apu_pitch_hook)(unsigned voice, int was, int now);

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;

/* Mod pack & interpreter hooks */
int      nfl2k5_pack_build = 0;
static int g_interp_pack   = 0;
unsigned nfl2k5_interp_setup(const uint8_t *base, size_t base_len);
unsigned nfl2k5_interp_force(uint32_t lo, uint32_t hi);
uint32_t nfl2k5_interp_mark_va(uint32_t va);
extern int  nfl2k5_modpacks_boot_dispatch(const char *root);
extern void nfl2k5_scorebug_interp_hooks(void);
extern void nfl2k5_controller_setup_init(void);
extern void nfl2k5_save_library_start(const char *save_root);
extern void nfl2k5_local_music_start(const char *root, const char *save_dir);
extern void nfl2k5_controls_start(void);
extern void nfl2k5_texture_packs_init(void);
extern void nfl2k5_controller_art_init(void);
extern void nfl2k5_save_library_menu_init(void);
extern void nfl2k5_video_menu_install(void);
extern int  nfl2k5_lineup_active(void);
extern int  nfl2k5_presentation_playercard(uint32_t kind, const char *title);
extern void nfl2k5_presentation_popup(const char *title);
extern void nfl2k5_presentation_popup_text(const char *title, const char *text);

/* Feature modules (src/nfl2k5_plugin_host.c, docs/eras/MOD-PACK-DESIGN.md),
 * Main Menu "Exit", moment audio and the front end's FSM clean-up hooks */
extern void nfl2k5_plugins_boot(const char *xbe_sha256, uint32_t base_kind, const char *xbe_pack_name, uint32_t flags);
extern void nfl2k5_hook_menus_ready(void);
extern void nfl2k5_hook_fsm_pop(uint32_t fsm, uint32_t top);
extern void nfl2k5_teams_fsm_pop(uint32_t fsm, uint32_t top);
extern void nfl2k5_exit_menu_install(void);
extern void nfl2k5_moment_audio_install(const char *root);
extern void nfl2k5_local_music_shutdown(void);
extern int  nfl2k5_walk_diag_on;

static uint32_t main_stack_begin = 0;
static uint32_t main_stack_end   = 0;

/* ============================================================================
 * Cross-Platform Helpers (SHA-256, Path Discovery, Audio MMIO, Crash Handler)
 * ============================================================================ */

static uint32_t read32(const unsigned char *data, size_t offset)
{
    uint32_t value;
    memcpy(&value, data + offset, sizeof(value));
    return value;
}

static int compute_sha256_hex(const unsigned char *buf, size_t len, char hex_out[65])
{
    unsigned char digest[32];
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE sha = NULL;
    if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return 0;
    NTSTATUS status = BCryptHash(sha, NULL, 0, (PUCHAR)buf, (ULONG)len, digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(sha, 0);
    if (status < 0)
        return 0;
#else
    SHA256(buf, len, digest);
#endif
    for (unsigned i = 0; i < 32; ++i)
        snprintf(hex_out + i * 2, 3, "%02x", digest[i]);
    hex_out[64] = '\0';
    return 1;
}

static bool dir_exists(const char *path)
{
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

static bool file_exists(const char *path)
{
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
#endif
}

static const char *nfl2k5_project_root(void)
{
    static char root[MAX_PATH];
    static int initialized = 0;
    if (initialized)
        return root;
    initialized = 1;

    const char *override = getenv("NFL2K5_ROOT");
    if (override && override[0]) {
        snprintf(root, sizeof(root), "%s", override);
        return root;
    }

#if defined(_WIN32)
    if (GetModuleFileNameA(NULL, root, (DWORD)sizeof(root))) {
        char *slash = strrchr(root, '\\');
        if (slash) {
            *slash = '\0';
            for (;;) {
                char original[MAX_PATH];
                snprintf(original, sizeof(original), "%s\\original", root);
                if (dir_exists(original))
                    return root;
                slash = strrchr(root, '\\');
                if (!slash || slash == root + 2)
                    break;
                *slash = '\0';
            }
        }
    }
#else
    ssize_t rlen = readlink("/proc/self/exe", root, sizeof(root) - 1);
    if (rlen > 0) {
        root[rlen] = '\0';
        char *slash = strrchr(root, '/');
        if (slash) {
            *slash = '\0';
            for (;;) {
                char original[MAX_PATH];
                snprintf(original, sizeof(original), "%s/original", root);
                if (dir_exists(original))
                    return root;
                slash = strrchr(root, '/');
                if (!slash || slash == root)
                    break;
                *slash = '\0';
            }
        }
    }
#endif
    snprintf(root, sizeof(root), "%s", NFL2K5_PROJECT_ROOT);
    return root;
}

#if defined(_WIN32)
static LONG CALLBACK audio_mmio(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_apu_state)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t host = ep->ExceptionRecord->ExceptionInformation[1];
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    if (host < offset || host - offset < 0xFE800000ULL || host - offset >= 0xFE880000ULL)
        return EXCEPTION_CONTINUE_SEARCH;
    if (ep->ExceptionRecord->ExceptionInformation[0] > 1)
        return EXCEPTION_CONTINUE_SEARCH;
    return apu_hook_handle_mmio(ep->ContextRecord, host, (uint32_t)(host - offset),
                                ep->ExceptionRecord->ExceptionInformation[0] == 1)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK crash_report(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;

    fflush(stderr);
    fprintf(stderr, "[CRASH] exception=0x%08lX host=0x%llX\n",
            code, (unsigned long long)ep->ContextRecord->Rip);
    fprintf(stderr, "[CRASH] eax=%08X ecx=%08X edx=%08X esp=%08X ebx=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi);
    fflush(stderr);
    RECOMP_ICALL_FEEDBACK_DUMP();
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static struct sigaction g_prev_segv_sa;
static struct sigaction g_prev_trap_sa;
static bool g_posix_mmio_installed;

static void posix_audio_mmio_sigaction(int sig, siginfo_t *si, void *ucontext)
{
    if (xbox_ac97_handle_posix_signal(sig, si ? si->si_code : 0,
                                    si ? (uintptr_t)si->si_addr : 0, ucontext))
        return;
    if (sig == SIGSEGV && g_apu_state && si && si->si_addr) {
        uintptr_t host = (uintptr_t)si->si_addr;
        uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
        if (host >= offset && host - offset >= 0xFE800000ULL && host - offset < 0xFE880000ULL) {
#if defined(__x86_64__)
            ucontext_t *uc = (ucontext_t *)ucontext;
            int is_write = (uc->uc_mcontext.gregs[REG_ERR] & 0x2) != 0;
            if (apu_hook_handle_mmio((PCONTEXT)uc, host, (uint32_t)(host - offset), is_write))
                return;
#elif defined(__aarch64__)
            if (xbox_arm64_mmio_fault(ucontext, host, (uint32_t)(host - offset)))
                return;
#endif
        }
    }
    const struct sigaction *previous = sig == SIGTRAP ? &g_prev_trap_sa : &g_prev_segv_sa;
    if (previous->sa_handler == SIG_IGN) return;
    if (previous->sa_handler != SIG_DFL && (previous->sa_flags & SA_SIGINFO)) {
        previous->sa_sigaction(sig, si, ucontext);
    } else if (previous->sa_handler != SIG_DFL) {
        previous->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static bool install_audio_mmio_handler(void)
{
    if (g_posix_mmio_installed) return true;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posix_audio_mmio_sigaction;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;   /* the thread's signal stack where it has one */
    if (sigaction(SIGSEGV, &sa, &g_prev_segv_sa) != 0) return false;
    if (sigaction(SIGTRAP, &sa, &g_prev_trap_sa) != 0) {
        sigaction(SIGSEGV, &g_prev_segv_sa, NULL);
        return false;
    }
    g_posix_mmio_installed = true;
    xbox_ac97_set_posix_handler_ready(true);
    return true;
}
#endif

static DWORD WINAPI nfl2k5_stderr_flusher(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(500);
        fflush(stderr);
    }
    return 0;
}

/* NFL2K5_CROWD_PROBE=1 (AUDIO-REPORT-HOTKEY-20261022): each pitch change of the
 * crowd-reaction stream (voice 0x44) with the wall clock, to compare the swell
 * against time with xemu (tools/audio/crowd_clock.py). */
static void crowd_probe(unsigned voice, int was, int now)
{
    LARGE_INTEGER f, c;
    if (voice != 0x44)
        return;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    fprintf(stderr, "[CROWD] qpc=%.1f p=%d->%d hz=%.0f\n", (double)c.QuadPart * 1000.0 / (double)f.QuadPart,
            was, now, 48000.0 * exp2(now / 4096.0));
}

/* ============================================================================
 * Gameplay Hooks: Frame Pacing, Expanded Limits, Broadcast HUD & Sim Fallback
 * ============================================================================ */

static uint32_t s_frame_req = 1;

static uint32_t nfl2k5_frame_interval_for(uint32_t requested)
{
    static int forced = -1;
    if (forced < 0) {
        const char *e = getenv("NFL2K5_FRAME_INTERVAL");
        forced = e ? atoi(e) : 0;
    }
    if (forced == 1 || forced == 2)
        return (uint32_t)forced;
    if (requested == 0 || requested > 2)
        return requested;
    int limit = xbox_VideoFpsLimit();
    if (limit < 0)
        return requested;
    return limit == 30 ? 2u : 1u;
}

static void nfl2k5_frame_interval_refresh(void)
{
    *(volatile uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + 0xA6A9ACu) =
        nfl2k5_frame_interval_for(s_frame_req);
}

uint32_t nfl2k5_frame_interval(uint32_t requested, uint32_t caller)
{
    (void)caller;
    if (!g_xbox_fps_limit_changed)
        g_xbox_fps_limit_changed = nfl2k5_frame_interval_refresh;
    s_frame_req = requested;
    return nfl2k5_frame_interval_for(requested);
}

/* Optional short-quarter harness for reproducing quarter/halftime bugs. The
 * retail game is untouched unless NFL2K5_QUARTER_SECONDS is set. The title
 * rewrites its period length when a match is created, so keep the override
 * alive until the game has entered a valid period. */
static DWORD WINAPI nfl2k5_short_quarter_thread(LPVOID arg)
{
    float seconds = *(const float *)arg;
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t last_period = 0xFFFFFFFFu;
    for (;;) {
        uint32_t period = *(const uint32_t *)(m + 0x00E602C4u);
        if (period != last_period) {
            fprintf(stderr, "[TEST] period %u -> %u (t=%lu)\n", last_period, period, (unsigned long)GetTickCount());
            last_period = period;
        }
        if (period >= 1u && period <= 4u) {
            float *length = (float *)(m + 0x00E602B0u);
            /* the length is read when a quarter starts; the running clock
             * (clock object [E6028C] + 0x10, seconds) is what counts down */
            uint32_t clock_object = *(const uint32_t *)(m + 0x00E6028Cu);
            if (clock_object && clock_object < 0x08000000u) {
                float *clock = (float *)((uint8_t *)m + clock_object + 0x10u);
                if (*clock > seconds && *clock < 4000.0f)
                    *clock = seconds;
            }
            if (*length != seconds) {
                *length = seconds;
                fprintf(stderr, "[TEST] quarter length override: %.0f seconds (period %u)\n", seconds, period);
            }
        }
        Sleep(100);
    }
    return 0;
}

static void nfl2k5_expand_limits(void)
{
    static const struct { uint32_t va; const char *retail; } sites[] = {
        { 0x3191C2, "\x8a\x90\x70\x1b\x53\x00" },
        { 0x3192C9, "\x3a\x90\x70\x1b\x53\x00" },
        { 0x319340, "\x0f\xb6\x88\x70\x1b\x53\x00" },
        { 0x31926D, "\xb9\x43\x00\x00\x00" },
        { 0x319289, "\x83\xc0\x42" },
        { 0x31928D, "\xb9\x43\x00\x00\x00" },
        { 0x3192D7, "\x83\xf8\x43" },
        { 0x31934C, "\x83\xf8\x43" },
        { 0x31A9CE, "\xc7\x05\xf0\xf7\xc8\x00\x42\x00\x00\x00" },
        { 0x31A9FD, "\x83\xf8\x43" },
        { 0x2480CB, "\x83\xf8\x1e" },
    };
    static const uint8_t more_stadiums[15] = {
        32, 36, 39, 40, 41, 42, 43, 44, 45, 48, 95, 96, 97, 98, 99
    };
    static const size_t lens[] = { 6, 6, 7, 5, 3, 5, 3, 3, 10, 3, 3 };
    uint8_t *m = (uint8_t *)xbox_GetMemoryOffset();
    int on = 1;

    char ip[MAX_PATH], line[128];
    FILE *f = fopen(xbox_user_path("nfl2k5_game.ini", ip, sizeof(ip)), "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            int v;
            if (sscanf(line, " expanded_limits = %d", &v) == 1)
                on = v != 0;
        }
        fclose(f);
    } else if ((f = fopen(ip, "w")) != NULL) {
        fprintf(f, "; NFL 2K5 game options\nexpanded_limits = 1\n");
        fclose(f);
    }
    if (getenv("NFL2K5_EXPANDED_LIMITS"))
        on = atoi(getenv("NFL2K5_EXPANDED_LIMITS")) != 0;
    if (!on)
        return;

    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
        if (memcmp(m + sites[i].va, sites[i].retail, lens[i]) != 0)
            return;
    }

    uint32_t list = xbox_HeapAlloc(96, 16);
    if (!list)
        return;
    memcpy(m + list, m + 0x531B70, 67);
    memcpy(m + list + 67, more_stadiums, sizeof(more_stadiums));
    memcpy(m + 0x3191C2 + 2, &list, 4);
    memcpy(m + 0x3192C9 + 2, &list, 4);
    memcpy(m + 0x319340 + 3, &list, 4);
    m[0x31926D + 1] = 82;
    m[0x31928D + 1] = 82;
    m[0x319289 + 2] = 81;
    m[0x3192D7 + 2] = 82;
    m[0x31934C + 2] = 82;
    m[0x31A9FD + 2] = 82;
    m[0x31A9CE + 6] = 81;
    m[0x2480CB + 2] = 95;
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
        nfl2k5_interp_mark_va(sites[i].va);
    printf("[LIMITS] Expanded: Create-a-Team stadiums 67 -> 82, Franchise seasons 30 -> 95\n");
}

static void pc_inline_utf16(const uint8_t *m, uint32_t address, char *out, size_t capacity)
{
    size_t i = 0;
    if (!out || !capacity || address >= 0x4000000u - 2)
        return;
    while (i + 1 < capacity && address + i * 2u < 0x4000000u - 2) {
        uint16_t ch = *(const uint16_t *)(m + address + i * 2u);
        if (!ch)
            break;
        if (ch < 32 || ch > 126) {
            i = 0;
            break;
        }
        out[i++] = (char)ch;
    }
    out[i] = 0;
}

void nfl2k5_playercard_probe(uint32_t ctx)
{
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    if (ctx < 0x10000u || ctx >= 0x4000000u - 0x100)
        return;
    uint32_t kind = *(const uint32_t *)(m + ctx + 0x48u);
    char native_title[96];
    pc_inline_utf16(m, ctx + 0x4Cu, native_title, sizeof(native_title));
    if (nfl2k5_presentation_playercard(kind, native_title))
        *(float *)((uint8_t *)m + ctx + 8u) = 0.01f;
}

void nfl2k5_diag_popup(uint32_t obj, int what)
{
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    if (what == 0) {
        char title[64], msg[128];
        int i;
        if (obj < 0x10000u || obj > 0x4000000u - 0xF20u)
            return;
        /* A presentation package is playing its lineup intro: the game's
         * player card (slot 1) closes at once instead of covering it, unless
         * the package still needs the card's pregame metadata. */
        if (*(const int32_t *)(m + obj) == 1 && nfl2k5_lineup_active() &&
            !nfl2k5_presentation_pregame_metadata_needed())
            *(float *)((uint8_t *)m + obj + 8u) = 0.01f;

        for (i = 0; i < 63 && *(const uint16_t *)(m + obj + 0x4Cu + i * 2u); i++)
            title[i] = (char)*(const uint16_t *)(m + obj + 0x4Cu + i * 2u);
        title[i] = 0;
        for (i = 0; i < 127 && *(const uint16_t *)(m + obj + 0xCCu + i * 2u); i++)
            msg[i] = (char)*(const uint16_t *)(m + obj + 0xCCu + i * 2u);
        msg[i] = 0;

        nfl2k5_presentation_coin_toss(title, msg, *(const float *)(m + obj + 8u),
                                      *(const int32_t *)(m + obj), 0, obj);
        if (*(const int32_t *)(m + obj) == 1) {
            if (nfl2k5_presentation_pregame_metadata_needed())
                nfl2k5_pregame_guest_roster_snapshot();
            nfl2k5_presentation_player_fact(obj);
        }
        nfl2k5_presentation_popup(title);

        char all[1024];
        size_t n = 0;
        uint32_t off = 0;
        while (off + 2 <= 0xF20u && n + 70 < sizeof(all)) {
            uint32_t start = off, len = 0;
            while (off + 2 <= 0xF20u) {
                uint16_t ch = *(const uint16_t *)(m + obj + off);
                if (ch < 32 || ch > 126)
                    break;
                len++;
                off += 2;
            }
            if (len >= 1 && len < 64 && (start & 1u) == 0) {
                if (n)
                    all[n++] = '\x1f';
                for (uint32_t k = 0; k < len; k++)
                    all[n++] = (char)*(const uint16_t *)(m + obj + start + k * 2u);
            }
            off += 2;
        }
        all[n] = 0;
        nfl2k5_presentation_popup_text(title, all);
    } else if (what == 1) {
        uint32_t sl = 0xB61B50u + obj * 0xF20u;
        if (obj < 2 && *(const uint32_t *)(m + sl + 4u)) {
            char title[64], msg[128];
            pc_inline_utf16(m, sl + 0x4Cu, title, sizeof(title));
            pc_inline_utf16(m, sl + 0xCCu, msg, sizeof(msg));
            nfl2k5_presentation_coin_toss(title, msg, *(const float *)(m + sl + 8u), (int)obj, 1, sl);
            nfl2k5_presentation_player_fact_hide((int)obj);
        }
    }
}

/* Game state machines (sub_0006E390 push, sub_0006E400 pop; idx at +0x100,
 * state descriptors at +idx*8). On a pop the M2 staging test releases its
 * slots at the Main Menu, and feature modules (My NFL Eras) clean up. */
void nfl2k5_diag_fsm(uint32_t obj, uint32_t desc, int push)
{
    (void)desc;
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t idx = *(const uint32_t *)(m + obj + 0x100u);
    if (!push && idx < 0x20u) {
        uint32_t top = *(const uint32_t *)(m + obj + idx * 8u);
        nfl2k5_teams_fsm_pop(obj, top);
        nfl2k5_hook_fsm_pop(obj, top);
    }
}

/* Optional source-only pregame audit (NFL2K5_DIAG_SNF_PREGAME). The existing
 * command-start hook is after 5A6F0, so ctx's cursor points at the next
 * operands. Record them and the actual camera-state identities without
 * classifying a warmup stage. */
static int snf_pregame_read(uint32_t address, void *out, size_t size)
{
    uint64_t end = (uint64_t)address + size;
    /* Script contexts and vignette instances also live in the contiguous
     * guest heap window. Match the presentation reader's two ranges. */
    if (!((address >= 0x10000u && end <= 0x10000000u) ||
          (address >= 0x80000000u && end <= 0x90000000u)))
        return 0;
    __try {
        memcpy(out, (const uint8_t *)xbox_GetMemoryOffset() + address, size);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void snf_pregame_script_trace(uint32_t ctx, uint32_t op, uint32_t start)
{
    static int enabled = -1;
    static volatile LONG printed;
    uint32_t period, phase, clock_object, cursor, frame = 0, type = 0;
    uint32_t state_c8 = 0, state_cc = 0, camera_c4 = 0, camera_c8 = 0;
    uint32_t camera_mode = 0, camera_pending = 0;
    float clock, length;
    uint8_t args[8];
    if (enabled < 0)
        enabled = getenv("NFL2K5_DIAG_SNF_PREGAME") != NULL;
    if (!enabled || !start || op > 255u ||
        !snf_pregame_read(0x00E602C4u, &period, sizeof(period)) || period != 0 ||
        !snf_pregame_read(0x00E602B4u, &phase, sizeof(phase)) || phase != 0 ||
        !snf_pregame_read(0x00E6028Cu, &clock_object, sizeof(clock_object)) || !clock_object ||
        !snf_pregame_read(clock_object + 0x10u, &clock, sizeof(clock)) ||
        !snf_pregame_read(0x00E602B0u, &length, sizeof(length)) ||
        !(clock >= 0.0f && clock < 4000.0f && length > 0.0f && length < 4000.0f) ||
        clock - length > 0.1f || length - clock > 0.1f ||
        !snf_pregame_read(ctx, &cursor, sizeof(cursor)) ||
        !snf_pregame_read(cursor, args, sizeof(args)) ||
        InterlockedIncrement(&printed) > 2000)
        return;
    snf_pregame_read(0x00A6A9B4u, &frame, sizeof(frame));
    snf_pregame_read(0x00A8F7B8u + op * 12u, &type, sizeof(type));
    snf_pregame_read(0x00E602C8u, &state_c8, sizeof(state_c8));
    snf_pregame_read(0x00E602CCu, &state_cc, sizeof(state_cc));
    snf_pregame_read(0x00B616C4u, &camera_c4, sizeof(camera_c4));
    snf_pregame_read(0x00B616C8u, &camera_c8, sizeof(camera_c8));
    snf_pregame_read(0x00B616C0u, &camera_mode, sizeof(camera_mode));
    snf_pregame_read(0x00B61700u, &camera_pending, sizeof(camera_pending));
    fprintf(stderr, "[SNF-PREGAME] t=%llu frame=%u ctx=%08X op=%u type=%u cursor=%08X args=%02X%02X%02X%02X %02X%02X%02X%02X state=%08X/%08X camera=%08X/%08X mode=%u pending=%u\n",
            (unsigned long long)GetTickCount64(), frame, ctx, op, type, cursor,
            args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
            state_c8, state_cc, camera_c4, camera_c8, camera_mode, camera_pending);
    for (uint32_t channel = 0; channel < 2u; ++channel) {
        uint32_t slot = 0x00B607F0u + channel * 0x100u;
        uint32_t scene = 0, flags = 0, instance = 0, ready = 0;
        snf_pregame_read(slot, &flags, sizeof(flags));
        snf_pregame_read(slot + 0x10u, &scene, sizeof(scene));
        snf_pregame_read(slot + 0x18u, &instance, sizeof(instance));
        snf_pregame_read(slot + 0xF0u, &ready, sizeof(ready));
        fprintf(stderr, "[SNF-PREGAME-SCENE] t=%llu channel=%u id=%u flags=%08X instance=%08X ready=%u\n",
                (unsigned long long)GetTickCount64(), channel, scene, flags, instance, ready);
    }
}

/* Presentation script commands (DIAG_SCRIPTOP): each command a script context
 * starts, and the first poll of a command, with the {type, start, poll} entry
 * at 0xA8F7B8 + op*12. Verbose logging or NFL2K5_PRESENTATION_TRACE only. */
void nfl2k5_diag_script(uint32_t ctx, uint32_t op, uint32_t start)
{
    static int presentation_trace = -1;
    static uint32_t ctxs[16], last[16];
    static volatile LONG printed;
    snf_pregame_script_trace(ctx, op, start);
    if (presentation_trace < 0)
        presentation_trace = getenv("NFL2K5_PRESENTATION_TRACE") != NULL;
    if (!xbox_verbose() && !presentation_trace)   /* thousands of lines a match */
        return;
    if (presentation_trace && !start)             /* focused audit records command starts */
        return;
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t key = op | (start ? 0x100u : 0u), e = 0xA8F7B8u + op * 12u;
    int i;
    for (i = 0; i < 16 && ctxs[i] && ctxs[i] != ctx; i++) {}
    if (i == 16)
        i = 15;
    if (ctxs[i] == ctx && last[i] == key && !start)
        return;
    ctxs[i] = ctx;
    last[i] = key;
    if (InterlockedIncrement(&printed) > 20000)
        return;
    fprintf(stderr, "  [SCRIPT] ctx=%08X op=%3u %s type=%u start=%08X poll=%08X\n", ctx, op,
            start ? "start" : "poll ", *(const uint32_t *)(m + e), *(const uint32_t *)(m + e + 4u),
            *(const uint32_t *)(m + e + 8u));
}

uint32_t nfl2k5_sim_pick_empty(uint32_t team, uint32_t want, uint32_t arg2, uint32_t mask)
{
    (void)want; (void)arg2; (void)mask;
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t count = *(const uint32_t *)(m + team + 0x38u);
    uint32_t base  = *(const uint32_t *)(m + team + 0x60u);
    uint32_t pick = 0, loose = 0;
    for (uint32_t i = 0; base && i < count && i < 80; i++) {
        uint32_t e  = base + i * 0x60u;
        uint32_t fl = *(const uint32_t *)(m + e + 4u);
        if ((fl & 0x80000000u) && !(fl & 0x400000u) && !pick)
            pick = e;
        if ((fl & 0x80000000u) && !loose)
            loose = e;
    }
    if (!pick) pick = loose;
    if (!pick && base && count) pick = base;
    return pick;
}

/* ============================================================================
 * Required Linker Stubs for Generated Code & Kernel Bridge Diagnostics
 * ============================================================================ */

volatile long nfl2k5_28f70_site_hits[17];
volatile long nfl2k5_748a0_markers[8];

void nfl2k5_execwatch_print(void) {}
void nfl2k5_hw_watch_rescan(void) {}
void recomp_coverage_hit(uint32_t va) { (void)va; }
void recomp_flags_fallback_hit(uint32_t address) { (void)address; }
void recomp_icall_hist(uint32_t va) { (void)va; }
void recomp_unresolved_hit(uint32_t address) { (void)address; }
void recomp_todo_hit(uint32_t address) { (void)address; }
void nfl2k5_diag_texzero(uint32_t tex, uint32_t ret) { (void)tex; (void)ret; }
void nfl2k5_diag_reg(uint32_t fn, uint32_t edx_, uint32_t a0, uint32_t a1, uint32_t obj) { (void)fn; (void)edx_; (void)a0; (void)a1; (void)obj; }
void nfl2k5_diag_res43(uint32_t obj, uint32_t load, uint32_t fre, uint32_t where) { (void)obj; (void)load; (void)fre; (void)where; }
void nfl2k5_diag_res(uint32_t res, int load, uint32_t ret, uint32_t r2, uint32_t r3) { (void)res; (void)load; (void)ret; (void)r2; (void)r3; }
void nfl2k5_diag_stream(uint32_t dst, uint32_t src, uint32_t len, uint32_t eax_, uint32_t arg) { (void)dst; (void)src; (void)len; (void)eax_; (void)arg; }
void nfl2k5_diag_pbreset(uint32_t dev) { (void)dev; }
void nfl2k5_diag_pbbegin(void) {}
void nfl2k5_diag_music(int what, uint32_t handle_ptr, uint32_t esp_) { (void)what; (void)handle_ptr; (void)esp_; }
void nfl2k5_diag_music_end(uint32_t mgr, uint32_t rec, uint32_t delta, uint32_t chan) { (void)mgr; (void)rec; (void)delta; (void)chan; }
void nfl2k5_diag_music_fill(uint32_t mgr, uint32_t rec, uint32_t delta, uint32_t chan) { (void)mgr; (void)rec; (void)delta; (void)chan; }
void nfl2k5_diag_music_cursor(uint32_t slot_off, uint32_t cursor, uint32_t limit) { (void)slot_off; (void)cursor; (void)limit; }
void nfl2k5_diag_wma(const char *what, uint32_t v) { (void)what; (void)v; }
void nfl2k5_diag_wma3(const char *what, uint32_t a, uint32_t b, uint32_t c) { (void)what; (void)a; (void)b; (void)c; }
void nfl2k5_diag_txtr(uint32_t res, uint32_t album) { (void)res; (void)album; }
void nfl2k5_diag_sched(uint32_t idx) { (void)idx; }
void nfl2k5_diag_commvalid(uint32_t entry, uint32_t ret) { (void)entry; (void)ret; }
void nfl2k5_diag_playfail(uint32_t entry, uint32_t esi_, uint32_t ebp_, uint32_t ebx_, uint32_t edi_, uint32_t s10, uint32_t s14, uint32_t s18, uint32_t eax_) { (void)entry; (void)esi_; (void)ebp_; (void)ebx_; (void)edi_; (void)s10; (void)s14; (void)s18; (void)eax_; }
void nfl2k5_diag_playstep(int which, uint32_t eax_, uint32_t esi_, uint32_t ebp_) { (void)which; (void)eax_; (void)esi_; (void)ebp_; }
void nfl2k5_diag_slot(const char *where, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { (void)where; (void)a; (void)b; (void)c; (void)d; }

/* ============================================================================
 * Main Application Entry Point
 * ============================================================================ */

#if defined(__ANDROID__)
/* stderr goes nowhere on Android: the log lines go to logcat (tag NFL2K5)
 * and to <root>/nfl2k5.log, where they can be read off the device. */
static int android_log_pipe[2];
static void *android_log_thread(void *arg)
{
    FILE *file = (FILE *)arg;
    char buf[1024];
    ssize_t n;
    while ((n = read(android_log_pipe[0], buf, sizeof buf - 1)) > 0) {
        buf[n] = 0;
        __android_log_write(ANDROID_LOG_INFO, "NFL2K5", buf);
        if (file) { fwrite(buf, 1, (size_t)n, file); fflush(file); }
    }
    return NULL;
}

/* The broadcast presentation packages ship inside the APK (android/build_apk.py
 * puts mods/presentations under assets/presentations with a manifest whose
 * first line is a version stamp). They are copied into <root>/mods/presentations
 * when that stamp changes, and a first mods/presentation.ini picks NBC SNF. */
static void android_mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; }
    mkdir(tmp, 0775);
}

static void android_unpack_presentations(const char *root)
{
    size_t len = 0;
    char *manifest = (char *)SDL_LoadFile("presentations/manifest.txt", &len);
    char dir[1024], stamp_path[1024], old[64] = "";
    FILE *f;
    if (!manifest) return;
    snprintf(dir, sizeof dir, "%s/mods/presentations", root);
    snprintf(stamp_path, sizeof stamp_path, "%s/.apk-version", dir);
    if ((f = fopen(stamp_path, "r"))) { if (!fgets(old, sizeof old, f)) old[0] = 0; fclose(f); }
    char *line = strtok(manifest, "\n");
    if (line && strcmp(line, old) && strncmp(line, old, strlen(line))) {
        char stamp[64];
        unsigned n = 0, bad = 0;
        snprintf(stamp, sizeof stamp, "%s", line);
        android_mkdirs(dir);
        while ((line = strtok(NULL, "\n"))) {
            char src[1024], dst[1024];
            size_t sz = 0;
            void *data;
            snprintf(src, sizeof src, "presentations/%s", line);
            snprintf(dst, sizeof dst, "%s/%s", dir, line);
            if (strrchr(dst, '/')) { *strrchr(dst, '/') = 0; android_mkdirs(dst); dst[strlen(dst)] = '/'; }
            data = SDL_LoadFile(src, &sz);
            if (data && (f = fopen(dst, "wb"))) { fwrite(data, 1, sz, f); fclose(f); n++; }
            else bad++;
            SDL_free(data);
        }
        if ((f = fopen(stamp_path, "w"))) { fprintf(f, "%s\n", stamp); fclose(f); }
        fprintf(stderr, "[ANDROID] broadcast packages unpacked: %u files%s\n", n, bad ? " (some failed)" : "");
    }
    SDL_free(manifest);
    {
        char ini[1024];
        snprintf(ini, sizeof ini, "%s/mods/presentation.ini", root);
        if (access(ini, F_OK) != 0 && (f = fopen(ini, "w"))) {
            fprintf(f, "; Broadcast presentation (Coach Match Up > Presentation edits this)\n"
                       "package = NBC Sunday Night Football\nintro = 0\noutro = 0\nanimations = 1\n"
                       "theme_volume = 100\ncustom_commentary = 0\n");
            fclose(f);
        }
    }
}

/* Every 30 s: the threads that used the most CPU, as percent of one core.
 * What drains a phone battery is CPU time, and this names who spends it. */
static void *android_cpu_report_thread(void *arg)
{
    enum { MAXT = 160 };
    struct { int tid; unsigned long long t; char name[20]; } prev[MAXT], cur[MAXT];
    int np = 0;
    long hz = sysconf(_SC_CLK_TCK);
    (void)arg;
    for (;;) {
        DIR *d = opendir("/proc/self/task");
        struct dirent *e;
        int nc = 0, i, j, k;
        struct { int pct10; const char *name; } top[6];
        int ntop = 0, total10 = 0;
        if (!d) return NULL;
        while ((e = readdir(d)) && nc < MAXT) {
            char path[64], buf[512], *p;
            unsigned long long ut = 0, st = 0;
            FILE *f;
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            snprintf(path, sizeof path, "/proc/self/task/%s/stat", e->d_name);
            if (!(f = fopen(path, "r"))) continue;
            buf[0] = 0;
            if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
            fclose(f);
            cur[nc].tid = atoi(e->d_name);
            cur[nc].name[0] = 0;
            if ((p = strchr(buf, '(')) && strrchr(buf, ')')) {
                size_t l = (size_t)(strrchr(buf, ')') - p - 1);
                if (l >= sizeof cur[nc].name) l = sizeof cur[nc].name - 1;
                memcpy(cur[nc].name, p + 1, l); cur[nc].name[l] = 0;
                p = strrchr(buf, ')') + 2;
                /* fields after the name: state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
                sscanf(p, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st);
            }
            cur[nc].t = ut + st;
            nc++;
        }
        closedir(d);
        for (i = 0; i < nc; i++) {
            unsigned long long was = cur[i].t;
            int pct10;
            for (j = 0; j < np; j++) if (prev[j].tid == cur[i].tid) { was = prev[j].t; break; }
            if (j == np && np) was = 0;            /* new thread: all of its time is in this window */
            pct10 = (int)((cur[i].t - was) * 1000ull / (unsigned long long)(hz * 30));
            total10 += pct10;
            for (k = ntop; k > 0 && top[k - 1].pct10 < pct10; k--) if (k < 6) top[k] = top[k - 1];
            if (k < 6) { top[k].pct10 = pct10; top[k].name = cur[i].name; if (ntop < 6) ntop++; }
        }
        if (np) {
            char line[512];
            int o = snprintf(line, sizeof line, "[CPU] last 30 s: %d.%d%% of one core in total;", total10 / 10, total10 % 10);
            for (k = 0; k < ntop && o < (int)sizeof line - 40; k++)
                o += snprintf(line + o, sizeof line - o, " %s %d.%d%%", top[k].name, top[k].pct10 / 10, top[k].pct10 % 10);
            fprintf(stderr, "%s\n", line);
        }
        memcpy(prev, cur, sizeof cur[0] * (size_t)nc);
        np = nc;
        sleep(30);
    }
    return NULL;
}

/* A crash writes where it happened into the log: the signal, the faulting
 * address and the call stack as offsets in libmain.so (symbolised on the PC
 * with llvm-symbolizer and the unstripped library), then dies as before. The
 * game's own SIGSEGV handler, installed later, passes faults it does not
 * handle on to this one. */
#include <dlfcn.h>
#include <unwind.h>
static struct sigaction s_crash_prev[32];

static void crash_line(const char *s) { (void)!write(2, s, strlen(s)); }

static void crash_pc(uintptr_t pc)
{
    char line[512];
    Dl_info di;
    if (dladdr((void *)pc, &di) && di.dli_fname)
        snprintf(line, sizeof line, "[CRASH]   %s+0x%lx (%s)\n", strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname,
                 (unsigned long)(pc - (uintptr_t)di.dli_fbase), di.dli_sname ? di.dli_sname : "?");
    else
        snprintf(line, sizeof line, "[CRASH]   %p\n", (void *)pc);
    crash_line(line);
}

static _Unwind_Reason_Code crash_frame(struct _Unwind_Context *c, void *arg)
{
    int *n = (int *)arg;
    uintptr_t pc = _Unwind_GetIP(c);
    if (pc) crash_pc(pc);
    return ++*n < 40 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void crash_handler(int sig, siginfo_t *si, void *uctx)
{
    char line[256];
    int n = 0;
    ucontext_t *uc = (ucontext_t *)uctx;
    snprintf(line, sizeof line, "[CRASH] signal %d (code %d) at address %p, thread %d\n", sig,
             si ? si->si_code : 0, si ? si->si_addr : NULL, (int)gettid());
    crash_line(line);
#if defined(__aarch64__)
    if (uc) {
        crash_line("[CRASH] pc:\n");
        crash_pc((uintptr_t)uc->uc_mcontext.pc);
        crash_line("[CRASH] lr:\n");
        crash_pc((uintptr_t)uc->uc_mcontext.regs[30]);
    }
#else
    (void)uc;
#endif
    crash_line("[CRASH] stack:\n");
    _Unwind_Backtrace(crash_frame, &n);
    if (si && sig == SIGSEGV) {
        /* what the address space looks like around the fault: whether the
         * page is mapped at all, and what the neighbours are */
        FILE *mf = fopen("/proc/self/maps", "r");
        uintptr_t a = (uintptr_t)si->si_addr;
        char ml[512];
        crash_line("[CRASH] maps within 64 MB of the address:\n");
        while (mf && fgets(ml, sizeof ml, mf)) {
            unsigned long lo = 0, hi = 0;
            if (sscanf(ml, "%lx-%lx", &lo, &hi) == 2 && hi + (64ul << 20) > a && lo < a + (64ul << 20)) {
                crash_line("[CRASH]   ");
                crash_line(ml);
            }
        }
        if (mf) fclose(mf);
    }
    sigaction(sig, &s_crash_prev[sig], NULL);
    raise(sig);
}

static void android_crash_reporter(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction sa;
    unsigned i;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, &s_crash_prev[sigs[i]]);
}

/* The game folder: the first of these holding original/default.xbe. The
 * app's own folder needs no permission (adb push or a file manager);
 * /sdcard/NFL2K5 needs "All files access" on Android 11+. */
static void android_pick_root(void)
{
    const char *cands[4];
    char probe[1024];
    int n = 0;
    if (getenv("NFL2K5_ROOT")) return;
    cands[n++] = SDL_GetAndroidExternalStoragePath();
    cands[n++] = "/sdcard/NFL2K5";
    cands[n++] = "/storage/emulated/0/NFL2K5";
    extern int nfl2k5_android_xbe_file_ok(const char *path);   /* platform/android/xiso_extract.c */
    for (int i = 0; i < n; i++) {
        if (!cands[i]) continue;
        snprintf(probe, sizeof probe, "%s/original/default.xbe", cands[i]);
        if (access(probe, R_OK) == 0 && !nfl2k5_android_xbe_file_ok(probe)) {
            /* another version of the game (it closed at once, 2026-10-30):
             * a right disc image found below replaces it */
            fprintf(stderr, "[BOOT] %s is not the retail USA executable\n", probe);
            continue;
        }
        if (access(probe, R_OK) == 0) {
            setenv("NFL2K5_ROOT", cands[i], 1);
            setenv("NFL2K5_PORTABLE", "1", 1);   /* settings and saves beside the game */
            return;
        }
    }
    {
        /* Only the disc image on the phone: unpack it (first start only). */
        extern int nfl2k5_android_unpack_iso(const char *root);   /* platform/android/xiso_extract.c */
        int r = nfl2k5_android_unpack_iso("/sdcard/NFL2K5");
        if (r > 0) {
            setenv("NFL2K5_ROOT", "/sdcard/NFL2K5", 1);
            setenv("NFL2K5_PORTABLE", "1", 1);
            return;
        }
        if (r < 0) exit(1);                       /* it said why */
    }
    {
        char msg[1536];
        snprintf(msg, sizeof msg,
                 "Game files not found.\n\nPut your NFL 2K5 disc image (.iso) in Download or in "
                 "Internal storage > NFL2K5, and make sure NFL 2K5 has All files access "
                 "(Settings > Apps > NFL 2K5 > Permissions). The first start unpacks it.\n\n"
                 "Or copy default.xbe and the vc_53450030 folder into Internal storage > NFL2K5 > original.");
        extern void nfl2k5_android_notice(const char *title, const char *msg);
        nfl2k5_android_notice("NFL 2K5: game files not found", msg);
        exit(1);
    }
}
#endif

#if defined(__ANDROID__)
/* The recompiled game runs on the thread that calls main, with the deep call
 * chains of the guest's code: Windows links a 16 MB main-thread stack, and
 * SDL's Java activity starts this one with Java's default (about 1 MB). The
 * game gets a thread of its own with room to spare. */
static int s_main_argc, s_main_ret;
static char **s_main_argv;
int nfl2k5_real_main(int argc, char **argv);
static void *big_stack_main(void *unused)
{
    stack_t ss;
    (void)unused;
    ss.ss_sp = malloc(64 * 1024);           /* the crash reporter's stack */
    ss.ss_size = 64 * 1024;
    ss.ss_flags = 0;
    if (ss.ss_sp) sigaltstack(&ss, NULL);
    s_main_ret = nfl2k5_real_main(s_main_argc, s_main_argv);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_attr_t at;
    pthread_t t;
    s_main_argc = argc;
    s_main_argv = argv;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64u << 20);
    if (pthread_create(&t, &at, big_stack_main, NULL) == 0) {
        pthread_attr_destroy(&at);
        pthread_join(t, NULL);
        return s_main_ret;
    }
    pthread_attr_destroy(&at);
    return nfl2k5_real_main(argc, argv);
}
#undef main
#define main nfl2k5_real_main
#endif

int main(int argc, char **argv)
{
#if defined(_WIN32)
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
#endif
#if defined(__ANDROID__)
    {
        /* the log first, so a start that stops at the game files says why */
        char lp[1024];
        FILE *lf;
        pthread_t t;
        mkdir("/sdcard/NFL2K5", 0755);
        snprintf(lp, sizeof lp, "%s/nfl2k5.log", getenv("NFL2K5_ROOT") ? getenv("NFL2K5_ROOT") : "/sdcard/NFL2K5");
        lf = fopen(lp, "w");
        if (!lf) {
            snprintf(lp, sizeof lp, "%s/nfl2k5.log", SDL_GetAndroidExternalStoragePath());
            lf = fopen(lp, "w");
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        if (lf) {
            /* straight into the file: a log thread lost the last lines
             * before a crash (2026-10-30), the ones that say where */
            dup2(fileno(lf), 1);
            dup2(fileno(lf), 2);
        } else if (pipe(android_log_pipe) == 0) {
            dup2(android_log_pipe[1], 1);
            dup2(android_log_pipe[1], 2);
            pthread_create(&t, NULL, android_log_thread, lf);
            pthread_detach(t);
        }
        fprintf(stderr, "[ANDROID] log %s\n", lp);
        android_crash_reporter();
        {
            /* Android's own log of the run before this one -- the system's crash
             * report (a stack overflow or an abort inside the GPU driver, which
             * the handler above cannot catch) is in it -- kept beside ours. */
            FILE *in = popen("logcat -d -v threadtime -t 6000 2>&1", "r");
            if (in) {
                char pl[1024], buf[4096];
                size_t n;
                FILE *out;
                snprintf(pl, sizeof pl, "%s", lp);
                if (strrchr(pl, '/')) strcpy(strrchr(pl, '/') + 1, "previous-run-system.log");
                out = fopen(pl, "w");
                while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (out) fwrite(buf, 1, n, out);
                pclose(in);
                if (out) { fclose(out); fprintf(stderr, "[ANDROID] system log of the previous run: %s\n", pl); }
            }
        }
    }
    android_pick_root();
    fprintf(stderr, "[ANDROID] game folder %s\n", getenv("NFL2K5_ROOT"));
    setenv("RECOMP_RENDERER", "vulkan", 0);   /* the GPU renderer (gx) on Vulkan; "cpu" for software */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");   /* SDL picked portrait from the 640x480 window */
    if (getenv("NFL2K5_GAME_MODE"))
        fprintf(stderr, "[ANDROID] game mode: %s\n", getenv("NFL2K5_GAME_MODE"));
    {   /* Compiled pipelines kept across runs (SDL Vulkan, local patch): the
         * Adreno driver spends ~0.7 s on each new one, which froze the game
         * whenever a new scene appeared. */
        char cache[1024];
        const char *root = getenv("NFL2K5_ROOT") ? getenv("NFL2K5_ROOT") : "/sdcard/NFL2K5";
        snprintf(cache, sizeof cache, "%s/cache", root);
        mkdir(cache, 0775);
        snprintf(cache, sizeof cache, "%s/cache/vk_pipelines.bin", root);
        setenv("SDL_GPU_VULKAN_PIPELINE_CACHE", cache, 0);
        android_unpack_presentations(root);
        snprintf(cache, sizeof cache, "%s/Music", root);   /* Music/<playlist>/<songs>: custom soundtracks */
        mkdir(cache, 0775);
    }
    {
        extern void nfl2k5_touch_pad_init(void);   /* on-screen controls (platform/android/touch_pad.c) */
        extern void nfl2k5_android_settings_init(void);   /* Phone Settings (platform/android/android_settings.c) */
        nfl2k5_android_settings_init();
        nfl2k5_touch_pad_init();
    }
    {
        pthread_t t;
        if (pthread_create(&t, NULL, android_cpu_report_thread, NULL) == 0) pthread_detach(t);
    }
#endif

    const char *root = nfl2k5_project_root();

    /* --retail (or NFL2K5_RETAIL=1): the clean ESPN NFL 2K5, whatever mod pack
     * Features > Mod Packs has selected; the selection itself is left as it is.
     * --vanilla (or NFL2K5_VANILLA=1): no XBE pack and no feature packs (My NFL
     * Eras) either (docs/eras/MOD-PACK-DESIGN.md 3). Both are consumed here. */
    int force_retail = 0, vanilla = 0;
    {
        const char *rv = getenv("NFL2K5_RETAIL"), *vv = getenv("NFL2K5_VANILLA");
        if (rv && *rv == '1') force_retail = 1;
        if (vv && *vv == '1') vanilla = force_retail = 1;
        for (int a = 1; a < argc; a++) {
            if (strcmp(argv[a], "--retail") == 0 || strcmp(argv[a], "--vanilla") == 0) {
                force_retail = 1;
                if (argv[a][2] == 'v')
                    vanilla = 1;
                for (int b = a; b < argc - 1; b++)
                    argv[b] = argv[b + 1];
                argc--;
                a--;
            }
        }
        if (force_retail) _putenv_s("NFL2K5_RETAIL", "1");   /* the same choice for anything later */
        if (vanilla)      _putenv_s("NFL2K5_VANILLA", "1");
    }

#if defined(_WIN32)
    if (!SetCurrentDirectoryA(root)) {
        fprintf(stderr, "[BOOT] Could not use runtime root %s (error %lu)\n",
                root, (unsigned long)GetLastError());
    }
#else
    if (chdir(root) != 0) {
        fprintf(stderr, "[BOOT] Could not chdir to runtime root %s\n", root);
    }
#endif

    /* Configure per-user settings directory unless running in portable mode */
    {
        char marker[MAX_PATH];
        const char *port = getenv("NFL2K5_PORTABLE");
        snprintf(marker, sizeof(marker), "%s/portable.txt", root);
        if ((port && *port == '0') ||
            (!(port && *port == '1') && !file_exists(marker) && _stricmp(root, NFL2K5_PROJECT_ROOT) != 0)) {
#if defined(_WIN32)
            const char *la = getenv("LOCALAPPDATA");
            if (la && *la) {
                char ud[MAX_PATH];
                snprintf(ud, sizeof(ud), "%s\\NFL2K5-PC", la);
                xbox_set_user_dir(ud);
            }
#else
            const char *home = getenv("HOME");
            if (home && *home) {
                char ud[MAX_PATH];
                snprintf(ud, sizeof(ud), "%s/.local/share/NFL2K5-PC", home);
                mkdir(ud, 0755);
                xbox_set_user_dir(ud);
            }
#endif
        }
    }

#if defined(_WIN32)
    {
        DWORD type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
        if (type != FILE_TYPE_DISK && type != FILE_TYPE_PIPE) {
            char lp[MAX_PATH];
            CreateDirectoryA("logs", NULL);
            if (freopen(xbox_user_path("logs\\session.log", lp, sizeof(lp)), "w", stderr))
                setvbuf(stderr, NULL, _IOLBF, 1 << 16);
        }
    }
#endif

    if (force_retail)
        fprintf(stderr, vanilla ? "[BOOT] --vanilla: clean ESPN NFL 2K5; no mod pack, no feature packs\n"
                                : "[BOOT] --retail: clean ESPN NFL 2K5; the selected mod pack is not loaded\n");
    if (!force_retail && !getenv("NFL2K5_NO_PACK_DISPATCH")) {
        if (nfl2k5_modpacks_boot_dispatch(root)) {
#if defined(_WIN32)
            SetThreadExecutionState(ES_CONTINUOUS);
#endif
            return 0;
        }
    }

    char disc_dir[MAX_PATH], disc_xbe[MAX_PATH], original_dir[MAX_PATH], original_xbe[MAX_PATH], save_dir[MAX_PATH];
    snprintf(disc_dir, sizeof(disc_dir), "%s/original/disc", root);
    snprintf(disc_xbe, sizeof(disc_xbe), "%s/default.xbe", disc_dir);
    snprintf(original_dir, sizeof(original_dir), "%s/original", root);
    snprintf(original_xbe, sizeof(original_xbe), "%s/default.xbe", original_dir);
    snprintf(save_dir, sizeof(save_dir), "%s/saves", root);

    const char *game_dir = file_exists(disc_xbe) ? disc_dir : original_dir;
    const char *xbe_path = file_exists(disc_xbe) ? disc_xbe : original_xbe;

    static char pack_xbe[MAX_PATH], pack_dir[MAX_PATH];
    {
        char active[MAX_PATH] = "", cand[MAX_PATH];
        const char *env = getenv("NFL2K5_INTERP_PACK");
        if (force_retail) {
            active[0] = 0;
        } else if (env && *env) {
            snprintf(active, sizeof(active), "%s", env);
        } else {
            char ini[MAX_PATH];
            snprintf(ini, sizeof(ini), "%s/mods/active-pack.ini", root);
            FILE *af = fopen(ini, "rb");
            if (af) {
                if (fgets(active, sizeof(active), af))
                    active[strcspn(active, "\r\n \t")] = 0;
                fclose(af);
            }
        }
        if (active[0] && !strpbrk(active, "/\\:")) {
            snprintf(cand, sizeof(cand), "%s/mods/packs/%s/default.xbe", root, active);
            if (file_exists(cand)) {
                snprintf(pack_dir, sizeof(pack_dir), "%s/mods/packs/%s", root, active);
                snprintf(pack_xbe, sizeof(pack_xbe), "%s", cand);
                xbe_path = pack_xbe;
                g_interp_pack = 1;
                _putenv_s("NFL2K5_PACK", pack_dir);
                printf("[BOOT] Mod pack (portable): %s\n", pack_dir);
            }
        }
    }

    const int validate_only = (argc == 2 && strcmp(argv[1], "--validate") == 0);
    if (argc > 1 && !validate_only && !(argc == 2 && strcmp(argv[1], "--run") == 0)) {
        fprintf(stderr, "Usage: NFL2K5 [--retail | --vanilla] [--validate | --run]\n");
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    RECOMP_ICALL_FEEDBACK_INIT();

    {
        static char stderr_buf[1 << 16];
        setvbuf(stderr, stderr_buf, _IOFBF, sizeof(stderr_buf));
        CloseHandle(CreateThread(NULL, 0, nfl2k5_stderr_flusher, NULL, 0, NULL));
    }

    if (!validate_only) {
        if (!getenv("RECOMP_AC97_READY"))           _putenv_s("RECOMP_AC97_READY", "1");
        if (!getenv("RECOMP_SKIP_THREAD_PRIORITY")) _putenv_s("RECOMP_SKIP_THREAD_PRIORITY", "1");
        if (!getenv("RECOMP_FB_WINDOW"))            _putenv_s("RECOMP_FB_WINDOW", "1");
        if (!getenv("RECOMP_PB_EXEC"))              _putenv_s("RECOMP_PB_EXEC", "1");
        if (!getenv("RECOMP_GGL"))                  _putenv_s("RECOMP_GGL", "1");
        if (!getenv("RECOMP_VBLANK"))               _putenv_s("RECOMP_VBLANK", "1");
        if (!getenv("RECOMP_GPU"))                  _putenv_s("RECOMP_GPU", "1");
        if (!getenv("RECOMP_GPU_VP"))               _putenv_s("RECOMP_GPU_VP", "1");
        if (!getenv("RECOMP_GPU_RAWVB"))            _putenv_s("RECOMP_GPU_RAWVB", "1");
        if (!getenv("RECOMP_GPU_ASYNC"))            _putenv_s("RECOMP_GPU_ASYNC", "1");
        if (!getenv("XBOX_LOG_LEVEL"))              _putenv_s("XBOX_LOG_LEVEL", "0");
        if (!getenv("RECOMP_KERNEL_LOG_BUDGET"))    _putenv_s("RECOMP_KERNEL_LOG_BUDGET", "0");
    }

#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash_report);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#endif

    printf("ESPN NFL 2K5 Native Recompilation\n");
    printf("[BOOT] Mounted game disc root: %s\n", game_dir);
    {   /* warm the file cache with the packed game data (kernel_file.c) */
        extern void xbox_PrefetchGameData(const char *root);
        xbox_PrefetchGameData(game_dir);
    }

    FILE *file = fopen(xbe_path, "rb");
    if (!file) {
        fprintf(stderr, "Missing %s\n", xbe_path);
        return 2;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length < 0x178) {
        fclose(file);
        return 2;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(data);
        return 2;
    }
    fclose(file);

    char digest_hex[65];
    const char *expected_sha = NFL2K5_XBE_SHA256;
    if (!compute_sha256_hex(data, (size_t)length, digest_hex)) {
        free(data);
        return 2;
    }

    /* A staged native pack is already translated from this exact image.
     * Comparing it to the retail disc would mark its native functions dirty
     * against the wrong baseline, mixing two incompatible translations. */
    if (g_interp_pack && !strcmp(digest_hex, expected_sha)) {
        g_interp_pack = 0;
        printf("[BOOT] Selected pack matches this native build; using its translated code.\n");
    }

    if (!strcmp(expected_sha, NFL2K5_WIDESCREEN_XBE_SHA256) && !strcmp(digest_hex, NFL2K5_RETAIL_XBE_SHA256)) {
        for (unsigned r = 0; r < sizeof(k_widescreen_runs) / sizeof(k_widescreen_runs[0]); r++) {
            if ((uint64_t)k_widescreen_runs[r].offset + k_widescreen_runs[r].length <= (uint64_t)length)
                memcpy(data + k_widescreen_runs[r].offset, k_widescreen_runs[r].bytes, k_widescreen_runs[r].length);
        }
        compute_sha256_hex(data, (size_t)length, digest_hex);
        printf("[BOOT] Built-in widescreen patch applied to the retail executable\n");
    }

    unsigned char *base_data = NULL;
    long base_len = 0;
    if (g_interp_pack) {
        char base_path[MAX_PATH];
        snprintf(base_path, sizeof(base_path), "%s/default.xbe", game_dir);
        FILE *bf = fopen(base_path, "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END);
            base_len = ftell(bf);
            rewind(bf);
            base_data = base_len > 0 ? (unsigned char *)malloc((size_t)base_len) : NULL;
            if (base_data && fread(base_data, 1, (size_t)base_len, bf) != (size_t)base_len) {
                free(base_data);
                base_data = NULL;
            }
            fclose(bf);
        }
        if (!base_data) {
            fprintf(stderr, "[BOOT] Missing %s (needed to run a mod pack)\n", base_path);
            free(data);
            return 2;
        }
        if (!strcmp(expected_sha, NFL2K5_WIDESCREEN_XBE_SHA256)) {
            for (unsigned r = 0; r < sizeof(k_widescreen_runs) / sizeof(k_widescreen_runs[0]); r++) {
                if ((uint64_t)k_widescreen_runs[r].offset + k_widescreen_runs[r].length <= (uint64_t)base_len)
                    memcpy(base_data + k_widescreen_runs[r].offset, k_widescreen_runs[r].bytes, k_widescreen_runs[r].length);
            }
        }
        printf("[BOOT] Pack executable %s\n", digest_hex);
    } else if (strcmp(digest_hex, expected_sha) != 0) {
        fprintf(stderr, "[BOOT] XBE hash mismatch. Expected %s, got %s.\n", expected_sha, digest_hex);
        free(data);
        return 2;
    }

    const uint32_t base = read32(data, 0x104);
    const uint32_t cert = read32(data, 0x118);
    if (memcmp(data, "XBEH", 4) != 0 || cert < base ||
        (uint64_t)(cert - base) + 12 > (uint64_t)length ||
        read32(data, cert - base + 8) != 0x53450030 ||
        (!NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) && !g_interp_pack && (read32(data, 0x128) ^ 0xA8FC57AB) != 0x00016BD1)) {
        fprintf(stderr, "XBE does not match this NFL 2K5 integration.\n");
        free(data);
        return 2;
    }

    if (NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) != 0 || g_interp_pack) {
        xbox_SetTotalRam(XBOX_DEVKIT_RAM);
        xbox_SetMapSize(XBOX_DEVKIT_RAM);
        printf("[BOOT] Mod pack: 128 MB Xbox RAM\n");
    }

#ifndef _WIN32
    if (!install_audio_mmio_handler()) {
        fprintf(stderr, "Failed to install POSIX MMIO signal handlers\n");
        free(data);
        return 1;
    }
#endif
    if (!xbox_MemoryLayoutInit(data, (size_t)length)) {
        free(data);
        return 3;
    }

    if (g_interp_pack) {
        nfl2k5_interp_setup(base_data, (size_t)base_len);
        free(base_data);
    }

    nfl2k5_scorebug_interp_hooks();

    {   /* Feature modules: My NFL Eras registers its hooks here, or nothing does. */
        const char *pk = g_interp_pack ? strrchr(pack_dir, '/') : NULL;
        uint32_t kind = g_interp_pack ? 3u                                          /* NF_BASE_INTERP_PACK */
                      : NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) != 0 ? 4u                  /* NF_BASE_OTHER */
                      : !strcmp(NFL2K5_XBE_SHA256, NFL2K5_WIDESCREEN_XBE_SHA256) ? 2u : 1u;   /* widescreen / retail */
        nfl2k5_plugins_boot(digest_hex, kind, pk ? pk + 1 : NULL, (force_retail ? 2u : 0u) | (vanilla ? 1u : 0u));
    }

    if (!g_interp_pack && NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) == 0)
        nfl2k5_expand_limits();
    nfl2k5_controller_setup_init();

    if (!strcmp(digest_hex, "f67e6fc1cb76c9ea1f1ad504e4a6491ff126483d66a9da8ddb92950bbaf740ba")) {
        printf("[BOOT] SOFTDRINK v0.6 dispatcher compatibility: %u function\n",
               nfl2k5_interp_force(0x0006E4E5u, 0x0006E5F5u));
        /* These save-all detours branch after POPFD. The native lift only
         * restores DF and emits an unresolved, always-false conditional.
         * Interpret the wrapper and the copied continuation together so
         * the original comparisons survive the mod's temporary work. */
        unsigned flag_paths = nfl2k5_interp_force(0x014DE0BEu, 0x014DE142u);
        flag_paths += nfl2k5_interp_force(0x000B6F30u, 0x000B7044u);
        flag_paths += nfl2k5_interp_force(0x014E35C3u, 0x014E3613u);
        flag_paths += nfl2k5_interp_force(0x000A27F0u, 0x000A2835u);
        printf("[BOOT] SOFTDRINK v0.6 saved-flags compatibility: %u functions\n", flag_paths);
    }

    nfl2k5_pack_build = NFL2K5_BASE_CMP(NFL2K5_XBE_SHA256) != 0 || g_interp_pack;
    if (nfl2k5_pack_build) {
        uint8_t  *guest      = (uint8_t *)(uintptr_t)xbox_GetMemoryOffset();
        uint32_t *row_kind   = (uint32_t *)(guest + 0x00501494u);
        uint32_t *row_text   = (uint32_t *)(guest + 0x00501498u);
        uint32_t *row_flags  = (uint32_t *)(guest + 0x0050149Cu);
        uint32_t *row_action = (uint32_t *)(guest + 0x005014BCu);
        if (*row_kind == 9u && *row_text == 0x014E8698u && *row_flags == 0u && *row_action == 0x014E6550u) {
            static const uint16_t mycareer[] = { 'M','y','C','a','r','e','e','r',0 };
            memcpy(guest + 0x00E7D5C4u, mycareer, sizeof(mycareer));
            *row_text = 0x00E7D5C4u;
        }
    }

    xbox_kernel_init();
    nfl2k5_save_library_start(save_dir);
    xbox_path_init(game_dir, save_dir);

    {
        const char *pack = getenv("NFL2K5_PACK");
        if (pack && *pack)
            xbox_overlay_load(pack, game_dir);
    }

    /* M4 halftime-hang study: gen patches WALK_DIAG_* in sub_000561F0 */
    nfl2k5_walk_diag_on = getenv("NFL2K5_WALK_DIAG") != NULL;
    {
        const char *short_q = getenv("NFL2K5_QUARTER_SECONDS");
        static float short_q_seconds;
        if (short_q && *short_q) {
            short_q_seconds = (float)strtod(short_q, NULL);
            if (short_q_seconds >= 10.0f && short_q_seconds <= 900.0f)
                CloseHandle(CreateThread(NULL, 0, nfl2k5_short_quarter_thread, &short_q_seconds, 0, NULL));
            else
                fprintf(stderr, "[BOOT] Ignoring NFL2K5_QUARTER_SECONDS=%s (use 10..900).\n", short_q);
        }
    }

    nfl2k5_local_music_start(root, save_dir);
    /* ESPN 25th Anniversary moment audio: the MP3s in <root>/moment-audio, played
     * when a SOFTDRINK moment is launched (src/nfl2k5_moment_audio.c). */
    nfl2k5_moment_audio_install(root);
    nfl2k5_controls_start();
    nfl2k5_texture_packs_init();
    nfl2k5_controller_art_init();
    nfl2k5_save_library_menu_init();
    xbox_kernel_bridge_init();

    if (!validate_only && getenv("RECOMP_AC97_READY")) {
        g_apu_state = mcpx_apu_init_standalone((uint8_t *)(uintptr_t)xbox_GetMemoryOffset());
        g_xbox_apu_irq_line = mcpx_apu_irq_line;
        g_apu_irq_wake      = xbox_kernel_irq_wake;
        if (getenv("NFL2K5_CROWD_PROBE"))   /* crowd-reaction pitch vs the game's sequencer clock */
            g_apu_pitch_hook = crowd_probe;
#if defined(_WIN32)
        if (!g_apu_state || !AddVectoredExceptionHandler(1, audio_mmio)) {
            fprintf(stderr, "[BOOT] Failed to initialize APU compatibility.\n");
            return 3;
        }
#else
        if (!g_apu_state || !install_audio_mmio_handler()) {
            fprintf(stderr, "[BOOT] Failed to initialize POSIX APU MMIO handler.\n");
            return 3;
        }
#endif
    }

    uint32_t stack_base = xbox_HeapAlloc(XBOX_STACK_SIZE, 4096);
    if (!stack_base) {
        fprintf(stderr, "[BOOT] Main stack allocation failed.\n");
        return 3;
    }
    g_esp            = stack_base + XBOX_STACK_SIZE - 16;
    main_stack_begin = stack_base;
    main_stack_end   = stack_base + XBOX_STACK_SIZE;
    *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + g_fs_base + 8) = stack_base;

    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] Using fallback function lookup.\n");

    nfl2k5_video_menu_install();
    nfl2k5_exit_menu_install();    /* "Exit" in the Main Menu (src/nfl2k5_exit_menu.c, M16) */
    nfl2k5_hook_menus_ready();     /* feature modules' menus ("My NFL Eras", src/nfl2k5_eras_menu.c) */

    guest_function entry = recomp_lookup(0x00016BD1);
    if (!entry) {
        fprintf(stderr, "[BOOT] Recompiled entry point missing.\n");
        return 4;
    }
    printf("[BOOT] XBE loaded; memory, kernel and recompiled entry 0x00016BD1 ready.\n");

    if (validate_only) {
        extern int nfl2k5_sandbox_selftest(void);
        extern int nfl2k5_sandbox_world_selftest(void);
        extern int nfl2k5_sandbox_menu_validate(void);
        extern int nfl2k5_sandbox_actor_properties_selftest(void);
        extern int nfl2k5_sandbox_pose_selftest(void);
        extern int nfl2k5_test_memmove(uint32_t buffer);
        extern int nfl2k5_test_interp_flags(uint32_t buffer);
        if (!nfl2k5_sandbox_selftest() || !nfl2k5_sandbox_world_selftest() ||
            !nfl2k5_sandbox_menu_validate() || !nfl2k5_sandbox_actor_properties_selftest() ||
            !nfl2k5_sandbox_pose_selftest())
            return 10;
        uint32_t test_buffer = xbox_HeapAlloc(4096, 16);
        if (!test_buffer || !nfl2k5_test_memmove(test_buffer))
            return 8;
        if (!nfl2k5_test_interp_flags(test_buffer))
            return 9;
        printf("[TEST] Interpreter native flag handoff and PUSHFD/POPFD preservation passed (8 cases).\n");
        uint32_t a = xbox_ContiguousAlloc(8192, 4096);
        uint32_t b = xbox_ContiguousAlloc(4096, 4096);
        if (!a || !b || xbox_ContiguousBlockSize(a) != 8192 ||
            !xbox_ContiguousFree(a) || xbox_ContiguousBlockSize(a) != 0)
            return 7;
        uint32_t reused = xbox_ContiguousAlloc(8192, 4096);
        if (reused != a || !xbox_ContiguousFree(b) || !xbox_ContiguousFree(reused) ||
            xbox_ContiguousBlockSize(b) != 0 || xbox_ContiguousBlockSize(reused) != 0)
            return 7;
        printf("[TEST] Validation passed.\n");
        /* Validation is a short-lived diagnostic process, while normal game
         * launches deliberately run forever. Background guest/APU threads
         * cannot safely be drained through the full game teardown sequence;
         * terminate after the checks instead of releasing guest memory
         * underneath those threads. */
        fflush(stdout);
        fflush(stderr);
        ExitProcess(0);
    } else {
        if (!getenv("RECOMP_DISABLE_FENCE_MIRROR")) {
            if (xbox_Nv2aMirrorFence(0x004409A8, 0x2C, 0x30) != 0)
                return 6;
        }
        xbox_WatchdogStart();
        printf("[BOOT] Entering recompiled NFL 2K5 code.\n");
        xbox_ggl_enter();
#if defined(_WIN32)
        __try {
            entry();
        } __except (crash_report(GetExceptionInformation())) {
            xbox_ggl_release_all();
            fprintf(stderr, "[BOOT] Guest crashed; exiting with failure.\n");
            fflush(stderr);
            ExitProcess(1);
        }
#else
        entry();
#endif
        printf("[BOOT] Guest entry returned.\n");
    }

    /* The local-music indexer owns a host thread. Join it before releasing
     * guest memory, or the worker can dereference the torn-down address space. */
    nfl2k5_local_music_shutdown();
    xbox_kernel_bridge_shutdown();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(data);
#if defined(_WIN32)
    SymCleanup(GetCurrentProcess());
#endif
    return 0;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)instance; (void)previous; (void)command_line; (void)show;
    return main(__argc, __argv);
}
#endif

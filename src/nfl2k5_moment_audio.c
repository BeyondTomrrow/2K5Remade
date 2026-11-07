/* ESPN 25th Anniversary — moment audio.
 *
 * The SOFTDRINK pack adds historical "moments" to the ESPN 25th Anniversary
 * list (e.g. "BEAST QUAKE", "SUPER BOWL LVIII: OVERTIME"). Those moments have
 * no native narration. This plays a user-supplied clip from <root>\moment-audio\
 * while a moment's info screen is shown, on the self-contained XAudio2 voice in
 * nfl2k5_presentation.cpp.
 *
 * SITU record table: base pointer at guest 0xC8F0D4, count at 0xC8F0D8, records
 * 0x6C bytes, field +0 = title (UTF-16). The info screen sets mode 0xE5FF80 = 8
 * and the selected index at 0xBF1858. We match by TITLE so it survives the pack
 * renumbering the list.
 *
 * Traces to logs\moment-trace.txt (so a self-launched run is captured too).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>
#include "recomp_funcs.h"   /* MEM16/MEM32 + XBOX_PTR */

/* Play through the proven draft-show voice engine (nfl2k5_presentation.cpp),
 * which is tapped into the game's own audio output. Channel 0, one clip. */
extern int  nfl2k5_presentation_voice_play(int ch, const char *path, float volume);
extern void nfl2k5_presentation_voice_stop(int ch);
#define MOMENT_CH 0
static void nfl2k5_moment_audio_play(const char *path) { nfl2k5_presentation_voice_play(MOMENT_CH, path, 1.0f); }
static void nfl2k5_moment_audio_stop(void)            { nfl2k5_presentation_voice_stop(MOMENT_CH); }

#define MODE     0x00E5FF80u   /* == 8 on the moment info screen */
#define SEL_IDX  0x00BF1858u   /* selected moment INDEX */
#define TBL_BASE 0x00C8F0D4u   /* MEM32 here = SITU record table base */
#define TBL_CNT  0x00C8F0D8u   /* loaded moment count */
#define REC_SIZE 0x6Cu         /* 108-byte SITU record */

static const struct { const char *match, *file; } k_moments[] = {
    { "BEAST QUAKE", "beast_quake" },
    { "BOWL LVIII",  "super_bowl_lviii" },   /* "SUPER BOWL LVIII: OVERTIME" (not XLVIII) */
};

static char s_dir[MAX_PATH];
static int  s_armed;
static int  s_dumped;
/* Debounce: 0xBF1858 flickers to stray values for a frame or two, which would
 * machine-gun play/stop. Act only once the index holds steady for SETTLE frames. */
#define SETTLE 8
static uint32_t s_cand_idx = 0xFFFFFFFFu;   /* candidate being timed          */
static int      s_cand_n;                   /* frames it has held             */
static uint32_t s_acted_idx = 0xFFFFFFFFu;  /* index we last acted on         */
static int      s_playing;                  /* a moment clip of ours is playing */

static FILE *s_trace;
static void mlog(const char *fmt, ...)
{
    va_list ap;
    if (!s_trace) s_trace = fopen("logs\\moment-trace.txt", "a");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    if (s_trace) { va_start(ap, fmt); vfprintf(s_trace, fmt, ap); va_end(ap); fflush(s_trace); }
}

/* Guest RAM is mapped in two ranges: the low heap and the 0x80000000 region
 * (the SITU table lives at 0x81xxxxxx). Accept both. */
static int guest_ok(uint32_t a)
{
    return (a >= 0x10000u && a < 0x10000000u) || (a >= 0x80000000u && a < 0x90000000u);
}

/* Read a guest UTF-16 string into an uppercase ASCII buffer for matching. */
static void read_title_upper(uint32_t wptr, char *out, size_t n)
{
    size_t i = 0;
    out[0] = 0;
    if (!guest_ok(wptr)) return;
    for (; i + 1 < n; i++) {
        uint16_t c = MEM16(wptr + (uint32_t)(i * 2u));
        if (!c) break;
        out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : (c < 128u ? (char)c : '?');
    }
    out[i] = 0;
}

static int file_exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

static int resolve_clip(const char *dir, const char *key, char *out, size_t n)
{
    static const char *const ext[] = { ".mp3", ".wma", ".wav", ".m4a", ".ogg" };
    int e;
    for (e = 0; e < (int)(sizeof ext / sizeof ext[0]); e++) {
        snprintf(out, n, "%s/%s%s", dir, key, ext[e]);
        if (file_exists(out)) return 1;
    }
    return 0;
}
static int find_clip(const char *key, char *out, size_t n)
{
    if (s_dir[0] && resolve_clip(s_dir, key, out, n)) return 1;
    return resolve_clip("moment-audio", key, out, n);
}

/* One-time: dump the whole SITU table so the real index->title map is visible. */
static void dump_table(uint32_t base, uint32_t count)
{
    uint32_t i;
    char t[128];
    mlog("[MOMENT-AUDIO] ---- SITU table base=%08X count=%u ----\n", base, count);
    for (i = 0; i < count && i < 128u; i++) {
        uint32_t rec = base + i * REC_SIZE;
        read_title_upper(MEM32(rec + 0x00u), t, sizeof t);
        mlog("[MOMENT-AUDIO]   [%2u] %08X \"%s\"\n", i, rec, t);
    }
}

/* Per-frame, from the input HLE service chain. */
void nfl2k5_moment_audio_service(void)
{
    uint32_t mode, idx, base, count, rec;
    char title[128], path[MAX_PATH];
    int i;
    static int s_first = 1, s_debug = -1;
    if (!s_armed) return;
    /* getenv scans the whole environment: this runs on every XInputGetState
     * (several a frame, more while the game waits), so it is read once.
     * It was ~8% of the game thread's time (2026-10-31 profile). */
    if (s_debug < 0) s_debug = getenv("NFL2K5_MOMENT_DEBUG") != NULL;
    if (s_first) { s_first = 0; mlog("[MOMENT-AUDIO] service running\n"); }
    mode  = MEM32(MODE);
    idx   = MEM32(SEL_IDX);
    base  = MEM32(TBL_BASE);
    count = MEM32(TBL_CNT);

    if (s_debug) {
        static DWORD hb; DWORD t = GetTickCount();
        if (t - hb > 2000u) { hb = t;
            mlog("[MOMENT-AUDIO] hb mode=%u base=%08X cnt=%u idx=%u\n", mode, base, count, idx); }
        if (!s_dumped && guest_ok(base)) { s_dumped = 1; dump_table(base, count && count <= 256u ? count : 64u); }
    }

    /* Off the info screen (backed out): stop our clip so it doesn't keep talking. */
    if (mode != 8u || !guest_ok(base) || idx >= 256u ||
        (count >= 1u && count <= 256u && idx >= count)) {
        s_cand_idx = 0xFFFFFFFFu; s_cand_n = 0; s_acted_idx = 0xFFFFFFFFu;
        if (s_playing) { nfl2k5_moment_audio_stop(); s_playing = 0; }
        return;
    }
    /* Debounce the flickering index. */
    if (idx != s_cand_idx) { s_cand_idx = idx; s_cand_n = 1; return; }
    if (s_cand_n <= SETTLE) { s_cand_n++; if (s_cand_n <= SETTLE) return; }
    if (idx == s_acted_idx) return;       /* already handled this settled moment */
    s_acted_idx = idx;

    rec = base + idx * REC_SIZE;
    read_title_upper(MEM32(rec + 0x00u), title, sizeof title);
    if (s_debug)
        mlog("[MOMENT-AUDIO] settled idx=%u rec=%08X \"%s\"\n", idx, rec, title);
    for (i = 0; i < (int)(sizeof k_moments / sizeof k_moments[0]); i++) {
        if (title[0] && strstr(title, k_moments[i].match) &&
            find_clip(k_moments[i].file, path, sizeof path)) {
            mlog("[MOMENT-AUDIO] play \"%s\" -> %s\n", title, path);
            nfl2k5_moment_audio_play(path);
            s_playing = 1;
            return;
        }
    }
    if (s_playing) { nfl2k5_moment_audio_stop(); s_playing = 0; }   /* swapped to a non-matching moment */
}

void nfl2k5_moment_audio_install(const char *root)
{
    if (root && *root) snprintf(s_dir, sizeof s_dir, "%s/moment-audio", root);
    else               s_dir[0] = 0;
    s_armed = 1;
    mlog("[MOMENT-AUDIO] armed; clips from %s\n", s_dir[0] ? s_dir : "moment-audio");
}

/* ── Phone settings (Android) ─────────────────────────────────────────────
 *
 * The rows of Options > Phone Settings (src/nfl2k5_video_menu.c): the touch
 * controls (src/platform/android/touch_pad.c), the battery saver
 * (nv2a_gpu_present.inc.c), pausing the game while the app is in the
 * background, and adding songs from the phone (NFL2K5Activity.pickSongs).
 * Saved in <root>/android.ini. */
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void xbox_ggl_enter(void);
extern void xbox_ggl_leave(void);
extern void xbox_ggl_set_priority(int prio);
extern int xbox_ggl_enabled(void);
extern volatile int g_xbox_battery_saver;           /* nv2a_gpu_present.inc.c */
extern void nfl2k5_local_music_rescan(void);        /* nfl2k5_local_music.cpp */
extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
extern void nfl2k5_touch_pad_changed(void);         /* touch_pad.c: redraw */
extern void (*g_xbox_fps_limit_changed)(void);      /* src/main.c: re-reads the frame-rate cap */

static struct {
    int touch;          /* 0 automatic (hidden while a controller is used), 1 on, 2 off */
    int touch_size;     /* 0 small, 1 medium, 2 large */
    int touch_opacity;  /* 0 25%, 1 50%, 2 75%, 3 100% */
    int battery_saver;  /* 1x internal resolution and 30 FPS */
    int pause_bg;       /* freeze the game while the app is in the background */
} s_set = { 0, 1, 2, 0, 1 };

static void ini_path(char *out, size_t n)
{
    const char *root = getenv("NFL2K5_ROOT");
    snprintf(out, n, "%s/android.ini", root ? root : "/sdcard/NFL2K5");
}

static void save(void)
{
    char p[1024];
    FILE *f;
    ini_path(p, sizeof p);
    if (!(f = fopen(p, "w"))) return;
    fprintf(f, "; Options > Phone Settings\n"
               "; touch: 0 automatic (hidden while a controller is in use), 1 on, 2 off\n"
               "touch = %d\n; touch_size: 0 small, 1 medium, 2 large\ntouch_size = %d\n"
               "; touch_opacity: 0 25%%, 1 50%%, 2 75%%, 3 100%%\ntouch_opacity = %d\n"
               "battery_saver = %d\npause_in_background = %d\n",
            s_set.touch, s_set.touch_size, s_set.touch_opacity, s_set.battery_saver, s_set.pause_bg);
    fclose(f);
}

static void load(void)
{
    char p[1024], line[128], key[64];
    int v;
    FILE *f;
    ini_path(p, sizeof p);
    if (!(f = fopen(p, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "touch")) s_set.touch = v >= 0 && v <= 2 ? v : 0;
        else if (!strcmp(key, "touch_size")) s_set.touch_size = v >= 0 && v <= 2 ? v : 1;
        else if (!strcmp(key, "touch_opacity")) s_set.touch_opacity = v >= 0 && v <= 3 ? v : 2;
        else if (!strcmp(key, "battery_saver")) s_set.battery_saver = v != 0;
        else if (!strcmp(key, "pause_in_background")) s_set.pause_bg = v != 0;
    }
    fclose(f);
}

/* ---- touch controls (touch_pad.c) ---- */
int nfl2k5_touch_mode(void) { return s_set.touch; }
void nfl2k5_touch_set_mode(int m) { s_set.touch = m; save(); nfl2k5_touch_pad_changed(); }
float nfl2k5_touch_scale(void) { static const float k[] = { 0.85f, 1.0f, 1.18f }; return k[s_set.touch_size % 3]; }
float nfl2k5_touch_alpha(void) { static const float k[] = { 0.45f, 0.75f, 1.0f, 1.35f }; return k[s_set.touch_opacity % 4]; }

/* ---- pausing in the background -------------------------------------------
 * Android stops drawing and SDL pauses the sound, but the game itself ran on
 * (and spent the battery) while the app was out of sight. A thread takes the
 * global guest lock at the top priority and keeps it until the app is back:
 * every guest thread, the timer's vblank included, stops at its next kernel
 * call, the way the console's game freezes when nothing schedules it. */
static SDL_Mutex *s_pause_lock;
static SDL_Condition *s_pause_cond;
static int s_paused_wanted, s_pause_running;

static int pause_thread(void *arg)
{
    (void)arg;
    xbox_ggl_set_priority(1000);
    xbox_ggl_enter();
    fprintf(stderr, "[ANDROID] game paused (app in the background)\n");
    SDL_LockMutex(s_pause_lock);
    while (s_paused_wanted) SDL_WaitCondition(s_pause_cond, s_pause_lock);
    s_pause_running = 0;
    SDL_UnlockMutex(s_pause_lock);
    xbox_ggl_leave();
    fprintf(stderr, "[ANDROID] game resumed\n");
    return 0;
}

static bool SDLCALL on_app_event(void *ud, SDL_Event *e)
{
    (void)ud;
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND && s_set.pause_bg && xbox_ggl_enabled()) {
        SDL_LockMutex(s_pause_lock);
        s_paused_wanted = 1;
        if (!s_pause_running) {
            SDL_Thread *t = SDL_CreateThread(pause_thread, "app_pause", NULL);
            if (t) { s_pause_running = 1; SDL_DetachThread(t); }
        }
        SDL_UnlockMutex(s_pause_lock);
    } else if (e->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        SDL_LockMutex(s_pause_lock);
        s_paused_wanted = 0;
        SDL_BroadcastCondition(s_pause_cond);
        SDL_UnlockMutex(s_pause_lock);
    }
    return true;
}

/* ---- adding songs ---------------------------------------------------------
 * Android's file picker (NFL2K5Activity.pickSongs) copies the chosen songs
 * into Music/Phone Songs; the library then rescans and converts them. */
static void JNICALL jni_songs_added(JNIEnv *env, jclass cls, jint n)
{
    char line[64];
    (void)env; (void)cls;
    fprintf(stderr, "[ANDROID] %d song(s) added to Music/Phone Songs\n", (int)n);
    if (n <= 0) return;
    snprintf(line, sizeof line, "%d song%s added to \"Phone Songs\"", (int)n, n == 1 ? "" : "s");
    xbox_PresentToast("MUSIC", line, "Converting: they appear in the soundtrack list shortly", 5000);
    nfl2k5_local_music_rescan();
}

/* Attaching to the VM renames the thread "Thread-N"; keep its own name. */
static JNIEnv *jni_env(void)
{
    char name[17] = { 0 };
    JNIEnv *env;
    prctl(PR_GET_NAME, name, 0, 0, 0);
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (name[0]) prctl(PR_SET_NAME, name, 0, 0, 0);
    return env;
}

static jclass activity_class(JNIEnv *env)
{
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c = act ? (*env)->GetObjectClass(env, act) : NULL;
    if (act) (*env)->DeleteLocalRef(env, act);
    return c;
}

void nfl2k5_android_pick_songs(void)
{
    JNIEnv *env = jni_env();
    jclass c;
    jmethodID m;
    if (!env || !(c = activity_class(env))) return;
    m = (*env)->GetStaticMethodID(env, c, "pickSongs", "()V");
    if (m) (*env)->CallStaticVoidMethod(env, c, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
}

/* ---- Options > Phone Settings rows (src/nfl2k5_video_menu.c) ---- */
enum { PS_TOUCH, PS_SIZE, PS_OPACITY, PS_BATTERY, PS_PAUSE, PS_SONGS, PS_COUNT };
static const char *const k_labels[PS_COUNT] = { "Touch Controls", "Touch Button Size", "Touch Opacity",
                                                "Battery Saver", "Pause When Leaving", "Add Songs" };
int nfl2k5_phone_rows(void) { return PS_COUNT; }
const char *nfl2k5_phone_label(int r) { return r >= 0 && r < PS_COUNT ? k_labels[r] : ""; }
int nfl2k5_phone_toggle(int r) { return r == PS_BATTERY || r == PS_PAUSE; }
int nfl2k5_phone_values(int r)
{
    switch (r) {
    case PS_TOUCH: case PS_SIZE: return 3;
    case PS_OPACITY: return 4;
    case PS_SONGS: return 2;
    default: return 2;
    }
}
int nfl2k5_phone_get(int r)
{
    switch (r) {
    case PS_TOUCH: return s_set.touch;
    case PS_SIZE: return s_set.touch_size;
    case PS_OPACITY: return s_set.touch_opacity;
    case PS_BATTERY: return s_set.battery_saver;
    case PS_PAUSE: return s_set.pause_bg;
    default: return 0;
    }
}
void nfl2k5_phone_set(int r, int v)
{
    switch (r) {
    case PS_TOUCH: s_set.touch = v; nfl2k5_touch_pad_changed(); break;
    case PS_SIZE: s_set.touch_size = v; nfl2k5_touch_pad_changed(); break;
    case PS_OPACITY: s_set.touch_opacity = v; nfl2k5_touch_pad_changed(); break;
    case PS_BATTERY: s_set.battery_saver = v != 0; g_xbox_battery_saver = s_set.battery_saver;
                    if (g_xbox_fps_limit_changed) g_xbox_fps_limit_changed();
                    break;
    case PS_PAUSE: s_set.pause_bg = v != 0; break;
    case PS_SONGS: nfl2k5_android_pick_songs(); return;   /* an action: opens the picker */
    default: return;
    }
    save();
}
void nfl2k5_phone_value_name(int r, int v, char *b, size_t n)
{
    static const char *const touch[] = { "Automatic", "On", "Off" };
    static const char *const size[] = { "Small", "Medium", "Large" };
    static const char *const opacity[] = { "25%", "50%", "75%", "100%" };
    switch (r) {
    case PS_TOUCH: snprintf(b, n, "%s", touch[(unsigned)v % 3u]); break;
    case PS_SIZE: snprintf(b, n, "%s", size[(unsigned)v % 3u]); break;
    case PS_OPACITY: snprintf(b, n, "%s", opacity[(unsigned)v % 4u]); break;
    case PS_SONGS: snprintf(b, n, "%s", "Choose from phone"); break;
    default: snprintf(b, n, "%s", v ? "On" : "Off"); break;
    }
}

void nfl2k5_android_settings_init(void)
{
    JNIEnv *env;
    jclass c;
    static const JNINativeMethod natives[] = {
        { (char *)"nativeSongsAdded", (char *)"(I)V", (void *)jni_songs_added },
    };
    load();
    g_xbox_battery_saver = s_set.battery_saver;
    s_pause_lock = SDL_CreateMutex();
    s_pause_cond = SDL_CreateCondition();
    SDL_AddEventWatch(on_app_event, NULL);
    env = jni_env();
    if (env && (c = activity_class(env))) {
        if ((*env)->RegisterNatives(env, c, natives, 1) != 0) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, c);
    }
}
#endif

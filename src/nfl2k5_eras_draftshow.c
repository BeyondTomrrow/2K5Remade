/* My NFL Eras: the draft show (docs/eras/results/M11-draftshow.md, route of
 * docs/eras/research/DRAFT-SHOW-FEASIBILITY.md).
 *
 * The stage is Franchise's Draft room (SCNE draft_menu) and the podium the Super Bowl trophy
 * set's (SCNE cutscene); the commissioner and the suited player are SCNE commish01 and its
 * re-dressed copy draftply1. All four are Eras' own copies in erasdraft.iff (built from the
 * player's disc by tools/eras/draftshow/build-draftshow-pack.py, served from pack G by the
 * office's extra-pack hook), loaded under the context L"ERASDRAFT" and always looked up with
 * it; Franchise's fr.iff, FRANCHISE1 and its globals are never touched.
 *
 * Drawing is the game's own: on message 7 the camera object is built from a camera record we
 * fill ourselves (eye / target / focal per shot, blended), then sub_0002DE60 draws the room,
 * the podium and each actor. The actors are the game's skinned meshes drawn by the game's
 * skinned renderer (sub_00021860 -> sub_000243D0 -> palette sub_00022C00): a node's +0x14 is
 * its array of current joint matrices (25 for commish01, row-vector affine, identity in the
 * file), and the palette is T(-transform[+0x40]) * current[i] (Mod Tools
 * nfl_pose_matrix_apply.py). We write current[i] = local[i] * current[parent] each frame
 * from the poses sampled out of the retail clips (anthem idle, coin-toss walk and handshake,
 * helmet raise), baked by the pack builder into draftposes.bin, so the actors get the
 * game's shading with no renderer of our own.
 *
 * Audio: mods\eras\draft\audio (supplied by the user): the chime, "With the Nth pick", then
 * the team, played on the presentation layer's voice channels (XAudio2; RECOMP_AUDIO_MUTE
 * silences the speakers only, the recordings keep the mix). The show's music is a looping bed
 * on a fourth channel (M12: draft\audio\bed.* supplied by the user, else draft\files\audio\bed.wav
 * cut from the game's own menu music by tools\eras\draftshow\make_bed.py) with the game's own
 * menu music muted while the show runs, so the retail track's fade-out / restart gap never falls
 * inside a pick; without a bed file the menu music plays as before. The cards are an HTML page
 * (mods\eras\draft\show) shown by the presentation layer's front-end mode
 * (nfl2k5_presentation_frontend_begin / post_json).
 *
 * The suited player (M12): a coach head from erasdraft.iff's dhdNN scenes (sideline head-coach
 * packages) drawn attached to his thorax / neck / head joints, his own face and glasses hidden,
 * his hands and neck tinted to the head's skin; the head is chosen per pick from the tone group
 * in draft\looks_1981.csv (a hash of the player's name picks within the group, never the previous
 * pick's head); the suit (charcoal / navy / black, by the same hash) and the tie (the drafting
 * team's colour) are palette edits of his coat and trousers textures in guest memory. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_eras.h"
#include "nfl2k5_eras_draftshow.h"
#include "nfl2k5_json.h"


extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void nfl2k5_presentation_frontend_begin(const char *root_dir, const char *package_dir, const char *entry);
extern void nfl2k5_presentation_frontend_end(void);
extern void nfl2k5_presentation_post_json(const char *json);
extern void xbox_PresentRecord(int seconds);   /* NFL2K5_REC=<prefix>: presented frames (with the overlay) to MP4 */
extern void xbox_AudioMuteGameMusic(int mute); /* the game's own music stream in the mixdown (apu_dsp.c) */

#define FOURCC_SCNE   0x454E4353u
#define PAD_WORDS     0x00B37A78u     /* the game's button word per pad (stride 0x244) */
#define BTN_SKIP      (0x100u | 0x400u | 0x800u | 0x10u)   /* A, X, Y, START (B / BACK pop the screen) */
#define FSM_DT        0x104u
#define RET_ADDR      0xFEE0FFF0u
#define NJ            25              /* joints of commish01 (the player skeleton) */
#define NHJ           29              /* joints of a coach head (coachHeadGrp1: thorax, neck, head, the face) */
#define MAXPICKS      400
#define MAXCLIPS      12
#define MAXHEADS      16

/* ---- staging (scene units: draft_menu is about 6 units per centimetre) -------------------- *
 * Kept in step with tools/eras/draftshow/preview_shots.py. */
#define SCALE        6.0f
#define STAGE_Y      474.0f
static const float k_podium[2] = { -1750.0f, -4900.0f };
static const float k_commish[2] = { -1750.0f, -5130.0f };
static const float k_player_from[2] = { 650.0f, -5330.0f };
static const float k_player_to[2] = { -1180.0f, -5130.0f };

typedef struct { float eye[3], tgt[3], focal; } Shot;
enum { SH_WIDE, SH_BOARD, SH_PODIUM, SH_TWO, SH_WALK, SH_HOLD, SH_PUSH, SHOTS };
static const Shot k_shot[SHOTS] = {
    { { 0.0f, 1500.0f, 2600.0f }, { 0.0f, 1250.0f, -5600.0f }, 35.0f },        /* wide: the whole stage */
    { { 0.0f, 2000.0f, -2300.0f }, { 0.0f, 2050.0f, -6074.0f }, 35.0f },       /* the NFL ENTRY DRAFT board */
    { { -1750.0f, 1520.0f, -3300.0f }, { -1750.0f, 1400.0f, -5130.0f }, 45.0f }, /* commissioner at the podium, straight on */
    { { -1250.0f, 1450.0f, -2500.0f }, { -1450.0f, 1250.0f, -5130.0f }, 35.0f }, /* two-shot */
    { { 300.0f, 1600.0f, -1500.0f }, { -600.0f, 1200.0f, -5200.0f }, 35.0f },  /* the walk-up */
    { { -1430.0f, 1380.0f, -2500.0f }, { -1430.0f, 1330.0f, -5130.0f }, 35.0f }, /* the photo: jersey held up */
    { { -1250.0f, 1320.0f, -3350.0f }, { -1250.0f, 1290.0f, -5130.0f }, 42.0f }, /* push-in on the chest-height hold */
};

static const char *const k_hide_room[] = {
    "table_scissor", "monitor_left_group", "monitor_center_group", "monitor_right_group", "LM_screen_group",
    "CM_screen_group", "RM_screen_group", "PDA_group", "PDA_shadow", "LM_shadow", "CM_shadow", "RM_shadow",
    "GM_group", "vip01", "vip02",
};

/* ---- state ------------------------------------------------------------------------------- */

enum { SC_ROOM, SC_SET, SC_COMMISH, SC_PLAYER, SCENES };
static const char *const k_scene[SCENES] = { "draft_menu", "cutscene", "commish01", "draftply1" };

typedef struct { char name[32]; int frames; float fps; float *data; } Clip;   /* data: frames * 103 */
typedef struct {
    int round, pick; char team[24], pfr[8], player[48], pos[8], college[48];
    char tone[16];                /* looks csv: light / medium / dark ("" = any) */
    int head, suit;               /* chosen at pick_begin: head index, suit variant 0 charcoal 1 navy 2 black */
    unsigned char no_number, no_team;   /* the call's clips that were missing (summary at the end) */
} Pick;
typedef struct { char name[16]; int outer; int rgb[3]; char group[12]; } Head;   /* draftheads.csv */

enum { PH_OFF, PH_INTRO, PH_CLOCK, PH_CHIME, PH_NUMBER, PH_TEAM, PH_CARD, PH_WALK, PH_SHAKE, PH_HOLD, PH_OUT, PH_END };
static const char *const k_phase[] = { "off", "intro", "on the clock", "chime", "number", "team", "card",
                                       "walk", "shake", "hold", "out", "end" };

typedef struct {
    int clip;                 /* index into s_d.clips, -1 = bind pose */
    float t, from, to, speed; /* clip time and segment */
    int loop, freeze;
    float x, z, yaw;          /* placement (scene units, degrees) */
    float q[NJ][4], root_y;   /* the pose drawn */
    float fq[NJ][4], fade;    /* crossfade from this pose (fade 1 -> 0) */
    float hand[2][3];         /* world lhand, rhand after the last actor_write */
    float ik;                 /* chest-height hold weight 0..1 (chest_hold) */
    float Rw[NJ][3][3], Pw[NJ][3];   /* model-space joint rotations / positions of the last actor_write (the head follows) */
} Actor;

typedef struct { int pick; char team[24]; uint8_t *px; } Jersey;   /* px: 16384 indices + 1024 palette */

static struct {
    int init, log, ready_checked, ready, mode;
    uint32_t mem, used, size;
    uint32_t camobj, camsave, camrec, slot, w_ctx, w_file, w_scene[SCENES];
    int loaded;
    uint32_t scene[SCENES];
    int shown;
    /* data */
    Clip clips[MAXCLIPS];
    int nclips;
    Pick picks[MAXPICKS];
    int npicks;
    int jparent[NJ];
    float jlocal[NJ][3];
    int joints_ok;
    /* the heads (M12) */
    Head heads[MAXHEADS];
    int nheads;
    uint32_t w_head[MAXHEADS], head_scene[MAXHEADS], head_node[MAXHEADS];
    int hjparent[NHJ];
    float hjlocal[NHJ][3];
    int hjoints_ok, head_cur;
    /* the suited player's pristine palettes (coat, pants, hand, neckmaterial), 1024 bytes BGRA each */
    uint8_t pal_save[4][1024];
    int pal_ok;
    /* the music bed */
    int bed_on, music_muted;
    float bed_gain, bed_target;
    int summary_done;
    /* show */
    int phase, cur, last, first;    /* pick indices into picks[] */
    float pt;                       /* time in phase */
    float cam_from[7], cam_to[7], cam_t, cam_len;
    Actor commish, player;
    int player_on, jersey_on;
    int vstep, team_clip, clock_skip;   /* the call's audio steps */
    float vt;
    Jersey jerseys[64];
    int njerseys;
    uint32_t jersey_node;
    uint32_t pad_prev[4];
    int pad_armed;
    double clock;
    int frames;
} s_d;

#define DS_LOG(...) do { fprintf(stderr, "[ERAS] draftshow " __VA_ARGS__); } while (0)

static int clip_find(const char *name)
{
    int i;
    for (i = 0; i < s_d.nclips; i++) if (!strcmp(s_d.clips[i].name, name)) return i;
    return -1;
}

/* ---- guest helpers (as in src/nfl2k5_eras_office.c) --------------------------------------- */

static uint32_t d_alloc(uint32_t n, uint32_t align)
{
    uint32_t p = (s_d.mem + s_d.used + align - 1u) & ~(align - 1u), i;
    if (p + n > s_d.mem + s_d.size) return 0;
    s_d.used = p + n - s_d.mem;
    for (i = 0; i < n; i += 4) MEM32(p + i) = 0;
    return p;
}

static uint32_t d_wstr(const char *s)
{
    uint32_t n = (uint32_t)strlen(s), p = d_alloc(2u * (n + 1u), 4), i;
    if (!p) return 0;
    for (i = 0; i <= n; i++) MEM16(p + 2u * i) = (uint16_t)(unsigned char)s[i];
    return p;
}

static uint32_t gcall(void (*fn)(void), uint32_t ecx, uint32_t edx)
{
    g_ecx = ecx; g_edx = edx;
    PUSH32(g_esp, RET_ADDR);
    fn();
    return g_eax;
}

static int weq(uint32_t w, const char *s)
{
    uint32_t i;
    if (!w) return 0;
    for (i = 0; i < 80; i++) {
        uint16_t c = MEM16(w + 2u * i);
        if (tolower(c < 128 ? c : 0) != tolower((unsigned char)s[i])) return 0;
        if (!c) return 1;
    }
    return 0;
}

static uint32_t lookup(uint32_t ctx, uint32_t fourcc, uint32_t wname)
{
    PUSH32(g_esp, wname);
    return gcall(sub_000449E0, ctx, fourcc);
}

static uint32_t find_rec(uint32_t scene, uint32_t cnt_off, uint32_t ptr_off, uint32_t stride, const char *name)
{
    uint32_t n = MEM32(scene + cnt_off), a = MEM32(scene + ptr_off), i;
    if (n > 256 || !a) return 0;
    for (i = 0; i < n; i++)
        if (weq(MEM32(a + i * stride), name)) return a + i * stride;
    return 0;
}
static uint32_t find_mat(uint32_t scene, const char *name) { return find_rec(scene, 0x1C, 0x20, 0x80, name); }
static uint32_t find_node(uint32_t scene, const char *name) { return find_rec(scene, 0x24, 0x28, 0x60, name); }

static void putf(uint32_t a, float f) { memcpy(&MEM32(a), &f, 4); }

/* ---- files -------------------------------------------------------------------------------- */

static int load_poses(void)
{
    char path[MAX_PATH], magic[8];
    FILE *f;
    uint32_t n, i;
    nfl2k5_eras_path("draft\\files\\draftposes.bin", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) { DS_LOG("no %s (run tools\\eras\\draftshow\\build-draftshow-pack.py)\n", path); return -1; }
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "DSPOSE01", 8) || fread(&n, 4, 1, f) != 1 || n > MAXCLIPS) { fclose(f); return -1; }
    for (i = 0; i < n; i++) {
        Clip *c = &s_d.clips[i];
        uint32_t frames, res;
        float fps;
        if (fread(c->name, 1, 32, f) != 32 || fread(&frames, 4, 1, f) != 1 || fread(&fps, 4, 1, f) != 1 ||
            fread(&res, 4, 1, f) != 1 || frames == 0 || frames > 2000) { fclose(f); return -1; }
        c->name[31] = 0;
        c->frames = (int)frames;
        c->fps = fps > 1.0f ? fps : 15.0f;
        c->data = (float *)malloc((size_t)frames * 103u * sizeof(float));
        if (!c->data || fread(c->data, sizeof(float) * 103u, frames, f) != frames) { fclose(f); return -1; }
    }
    fclose(f);
    s_d.nclips = (int)n;
    DS_LOG("poses: %u clips from %s\n", n, path);
    return 0;
}

static void field(char **p, char *out, size_t n)
{
    size_t k = 0;
    while (**p && **p != ',' && **p != '\r' && **p != '\n') { if (k + 1 < n) out[k++] = **p; (*p)++; }
    out[k] = 0;
    if (**p == ',') (*p)++;
}

static int load_picks(int year)
{
    char path[MAX_PATH], rel[64], line[256];
    FILE *f;
    snprintf(rel, sizeof rel, "draft\\picks_%d.csv", year);
    nfl2k5_eras_path(rel, path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) { DS_LOG("no %s (tools\\eras\\draftshow\\make_draft_picks.py)\n", path); return -1; }
    s_d.npicks = 0;
    fgets(line, sizeof line, f);                      /* header */
    while (s_d.npicks < MAXPICKS && fgets(line, sizeof line, f)) {
        Pick *k = &s_d.picks[s_d.npicks];
        char a[16], b[16], *p = line;
        field(&p, a, sizeof a); field(&p, b, sizeof b);
        field(&p, k->team, sizeof k->team); field(&p, k->pfr, sizeof k->pfr);
        field(&p, k->player, sizeof k->player); field(&p, k->pos, sizeof k->pos); field(&p, k->college, sizeof k->college);
        k->round = atoi(a); k->pick = atoi(b);
        if (k->round > 0 && k->pick > 0) s_d.npicks++;
    }
    fclose(f);
    DS_LOG("picks: %d from %s\n", s_d.npicks, path);
    /* draft\looks_<year>.csv: pick, player, skin_group (light / medium / dark), community_skin, source */
    snprintf(rel, sizeof rel, "draft\\looks_%d.csv", year);
    nfl2k5_eras_path(rel, path, sizeof path);
    if (!fopen_s(&f, path, "rb") && f) {
        int n = 0, i;
        fgets(line, sizeof line, f);
        while (fgets(line, sizeof line, f)) {
            char a[16], b[64], g[16], *p = line;
            field(&p, a, sizeof a); field(&p, b, sizeof b); field(&p, g, sizeof g);
            for (i = 0; i < s_d.npicks; i++)
                if (s_d.picks[i].round == 1 && s_d.picks[i].pick == atoi(a) && g[0]) { snprintf(s_d.picks[i].tone, sizeof s_d.picks[i].tone, "%s", g); n++; }
        }
        fclose(f);
        DS_LOG("looks: %d tone groups from %s\n", n, path);
    }
    return s_d.npicks ? 0 : -1;
}

/* draft\files\draftheads.csv (the pack builder): name, outer, r, g, b, group */
static int load_heads(void)
{
    char path[MAX_PATH], line[128];
    FILE *f;
    nfl2k5_eras_path("draft\\files\\draftheads.csv", path, sizeof path);
    s_d.nheads = 0;
    if (fopen_s(&f, path, "rb") || !f) { DS_LOG("no %s: the player keeps the commissioner's head\n", path); return -1; }
    fgets(line, sizeof line, f);
    while (s_d.nheads < MAXHEADS && fgets(line, sizeof line, f)) {
        Head *h = &s_d.heads[s_d.nheads];
        char a[16], r[8], g[8], b[8], *p = line;
        field(&p, h->name, sizeof h->name); field(&p, a, sizeof a);
        field(&p, r, sizeof r); field(&p, g, sizeof g); field(&p, b, sizeof b); field(&p, h->group, sizeof h->group);
        h->outer = atoi(a); h->rgb[0] = atoi(r); h->rgb[1] = atoi(g); h->rgb[2] = atoi(b);
        if (h->name[0]) s_d.nheads++;
    }
    fclose(f);
    DS_LOG("heads: %d from %s\n", s_d.nheads, path);
    return s_d.nheads ? 0 : -1;
}

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)tolower((unsigned char)*s)) * 16777619u;
    return h;
}

/* The head for pick i: from its tone group (any group when it has none), by the hash of the
 * player's name, never the previous pick's head. -1 without heads. */
static int head_choose(int i)
{
    int pool[MAXHEADS], n = 0, k, prev = i > s_d.first ? s_d.picks[i - 1].head : -1;
    const Pick *p = &s_d.picks[i];
    if (!s_d.nheads) return -1;
    for (k = 0; k < s_d.nheads; k++)
        if (!p->tone[0] || !_stricmp(p->tone, s_d.heads[k].group) || (!_stricmp(p->tone, "dark_medium") && !strcmp(s_d.heads[k].group, "medium")))
            pool[n++] = k;
    if (!n) for (k = 0; k < s_d.nheads; k++) pool[n++] = k;
    k = (int)(name_hash(p->player) % (uint32_t)n);
    if (pool[k] == prev && n > 1) k = (k + 1) % n;
    return pool[k];
}

/* ---- audio (the presentation layer's voice channel: one clip at a time) --------------------- *
 * Files: draft\audio\normalized\<rel>.wav first (loudness-matched copies, -18 LUFS, 48 kHz PCM;
 * a clip flagged "suspect_short" in normalized\index.json counts as missing), then the pack
 * builder's PCM copy draft\files\audio\<rel>.wav, then the user's MP3. Played on XAudio2 and
 * mixed into the game's recordings; RECOMP_AUDIO_MUTE silences the speakers only. */

extern int nfl2k5_presentation_voice_play(int ch, const char *path, float volume);
extern int nfl2k5_presentation_voice_play_ex(int ch, const char *path, float volume, int loop);
extern int nfl2k5_presentation_voice_state(int ch, double *elapsed, double *length);
extern void nfl2k5_presentation_voice_stop(int ch);
extern void nfl2k5_presentation_voice_duck(float gain);
extern void nfl2k5_presentation_voice_gain(int ch, float gain);

#define VOL_VOICE   1.0f
#define VOL_CHIME   0.8f
#define VOL_BED     1.0f      /* the bed file is cut at the menu music's own level (make_bed.py: -31 dBFS RMS) */
#define DUCK_GAME   0.25f
#define DUCK_RAMP   0.35f     /* seconds for the bed / music to move between full and ducked */
#define CLIP_GAP    0.25f
#define CHIME_CALL  3.4f      /* the call starts here: the chime's loud body is 0-3.3 s, its tail rings under the voice */
enum { CH_CHIME, CH_NUMBER, CH_TEAM, CH_BED };   /* the presentation layer's voice channels */

static struct {
    int muted, active[3];
    char name[3][64];
    char *index;              /* normalized\index.json text */
    int index_tried;
} s_au;

static void au_stop(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (s_au.active[i] && nfl2k5_presentation_voice_state(i, NULL, NULL)) DS_LOG("audio: %s stopped (skip)\n", s_au.name[i]);
        s_au.active[i] = 0;
        nfl2k5_presentation_voice_stop(i);           /* the bed (CH_BED) plays on */
    }
}

/* ---- the music bed (M12) --------------------------------------------------------------------- *
 * A looping bed on CH_BED replaces the game's menu music while the show runs (the retail track
 * ends and restarts with a ~2-4 s gap that used to fall inside picks). Files: draft\audio\bed.wav
 * / .mp3 (the user's), else draft\files\audio\bed.wav (tools\eras\draftshow\make_bed.py). Under
 * the call the bed (or, without a bed, the game's music) goes to DUCK_GAME over DUCK_RAMP s. */
static void bed_start(void)
{
    static const char *const k_bed[] = { "draft\\audio\\bed.wav", "draft\\audio\\bed.mp3", "draft\\files\\audio\\bed.wav" };
    char path[MAX_PATH];
    int i;
    if (s_d.bed_on || getenv("NFL2K5_ERAS_DRAFTSHOW_NOBED")) return;
    for (i = 0; i < 3; i++) {
        if (!nfl2k5_eras_exists(k_bed[i])) continue;
        nfl2k5_eras_path(k_bed[i], path, sizeof path);
        if (nfl2k5_presentation_voice_play_ex(CH_BED, path, VOL_BED, 1)) continue;
        s_d.bed_on = 1;
        s_d.bed_gain = s_d.bed_target = 1.0f;
        xbox_AudioMuteGameMusic(1);
        s_d.music_muted = 1;
        DS_LOG("audio: music bed %s (looping; the game's menu music muted while the show runs)\n", k_bed[i]);
        return;
    }
    DS_LOG("audio: no music bed (draft\\audio\\bed.wav or make_bed.py): the game's menu music plays, its track gap included\n");
}

static void bed_stop(void)
{
    if (s_d.bed_on) { nfl2k5_presentation_voice_stop(CH_BED); DS_LOG("audio: music bed stopped\n"); }
    s_d.bed_on = 0;
    if (s_d.music_muted) xbox_AudioMuteGameMusic(0);
    s_d.music_muted = 0;
    nfl2k5_presentation_voice_duck(1.0f);
    s_d.bed_gain = s_d.bed_target = 1.0f;
}

/* the music under the call: the bed when there is one, else the game's own audio */
static void duck(float gain)
{
    s_d.bed_target = gain;
    if (!s_d.bed_on) nfl2k5_presentation_voice_duck(gain);
}

static void bed_tick(float dt)
{
    float k;
    if (!s_d.bed_on || s_d.bed_gain == s_d.bed_target) return;
    k = dt / DUCK_RAMP;
    if (k > 1.0f) k = 1.0f;
    s_d.bed_gain += (s_d.bed_target - s_d.bed_gain) * k;
    if (fabsf(s_d.bed_target - s_d.bed_gain) < 0.01f) s_d.bed_gain = s_d.bed_target;
    nfl2k5_presentation_voice_gain(CH_BED, VOL_BED * s_d.bed_gain);
}

static int au_suspect(const char *rel)
{
    char key[96], *p, *end;
    size_t i;
    if (!s_au.index_tried) {
        char path[MAX_PATH];
        FILE *f;
        long n;
        s_au.index_tried = 1;
        nfl2k5_eras_path("draft\\audio\\normalized\\index.json", path, sizeof path);
        if (!fopen_s(&f, path, "rb") && f) {
            fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
            s_au.index = (char *)malloc((size_t)n + 1);
            if (s_au.index) { n = (long)fread(s_au.index, 1, (size_t)n, f); s_au.index[n] = 0; }
            fclose(f);
        }
    }
    if (!s_au.index) return 0;
    snprintf(key, sizeof key, "\"%s\"", rel);
    for (i = 0; key[i]; i++) if (key[i] == '\\') key[i] = '/';
    p = strstr(s_au.index, key);
    if (!p) return 0;
    end = strchr(p, '}');
    p = strstr(p, "suspect_short");
    return p && end && p < end && strstr(p, "true") && strstr(p, "true") < end;
}

/* rel under draft\audio (e.g. "numbers\number_01.mp3"); 0 = started, -1 = missing */
static int au_play(int ch, const char *rel, float volume)
{
    char r2[MAX_PATH], path[MAX_PATH], base[MAX_PATH];
    size_t n;
    snprintf(base, sizeof base, "%s", rel);
    n = strlen(base);
    if (n > 4 && !_stricmp(base + n - 4, ".mp3")) base[n - 4] = 0;
    if (au_suspect(rel)) { DS_LOG("audio: %s is flagged suspect_short: treated as missing\n", rel); return -1; }
    snprintf(r2, sizeof r2, "draft\\audio\\normalized\\%s.wav", base);
    if (!nfl2k5_eras_exists(r2)) snprintf(r2, sizeof r2, "draft\\files\\audio\\%s.wav", base);
    if (!nfl2k5_eras_exists(r2)) snprintf(r2, sizeof r2, "draft\\audio\\%s", rel);
    if (!nfl2k5_eras_exists(r2)) { DS_LOG("audio: %s missing\n", rel); return -1; }
    nfl2k5_eras_path(r2, path, sizeof path);
    snprintf(s_au.name[ch], sizeof s_au.name[ch], "%s", rel);
    { const char *m = getenv("RECOMP_AUDIO_MUTE"); s_au.muted = m && *m && *m != '0'; }
    if (nfl2k5_presentation_voice_play(ch, path, volume)) return -1;
    s_au.active[ch] = 1;
    DS_LOG("audio: %s <- %s, volume %.2f%s (t %.2f s)\n", rel, r2, volume, s_au.muted ? ", speakers muted (recorded)" : "", s_d.clock);
    return 0;
}

/* 2 playing, 1 decoding, 0 finished / idle */
static int au_state(int ch, double *elapsed)
{
    int st = nfl2k5_presentation_voice_state(ch, elapsed, NULL);
    if (!st && s_au.active[ch]) { s_au.active[ch] = 0; DS_LOG("audio: %s finished (t %.2f s)\n", s_au.name[ch], s_d.clock); }
    return st;
}

/* clips.json: "teams_1980": { "<eras id>": "teams/..mp3" }; read once into a small table */
static char s_team_clip[40][2][64];
static int s_team_clips = -1;
static void team_clips_load(void)
{
    char path[MAX_PATH], *text, *p, *end;
    FILE *f;
    long n;
    s_team_clips = 0;
    nfl2k5_eras_path("draft\\audio\\clips.json", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) { DS_LOG("audio: no clips.json\n"); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return; }
    n = (long)fread(text, 1, (size_t)n, f); text[n] = 0;
    fclose(f);
    p = strstr(text, "\"teams_1980\"");
    end = p ? strchr(p, '}') : NULL;
    while (p && end && s_team_clips < 40) {
        char *k1 = strchr(p == strstr(text, "\"teams_1980\"") ? p + 12 : p + 1, '"'), *k2, *v1, *v2;
        if (!k1 || k1 > end) break;
        k2 = strchr(k1 + 1, '"'); if (!k2) break;
        v1 = strchr(k2 + 1, '"'); if (!v1 || v1 > end) break;
        v2 = strchr(v1 + 1, '"'); if (!v2) break;
        snprintf(s_team_clip[s_team_clips][0], 64, "%.*s", (int)(k2 - k1 - 1), k1 + 1);
        snprintf(s_team_clip[s_team_clips][1], 64, "%.*s", (int)(v2 - v1 - 1), v1 + 1);
        for (char *c = s_team_clip[s_team_clips][1]; *c; c++) if (*c == '/') *c = '\\';
        s_team_clips++;
        p = v2;
    }
    free(text);
    DS_LOG("audio: %d team clips in clips.json\n", s_team_clips);
}

static const char *team_clip(const char *id)
{
    int i;
    if (s_team_clips < 0) team_clips_load();
    for (i = 0; i < s_team_clips; i++) if (!strcmp(s_team_clip[i][0], id)) return s_team_clip[i][1];
    return NULL;
}

/* ---- overlay events ------------------------------------------------------------------------ */

static void jesc(char *out, size_t n, const char *s)
{
    size_t k = 0;
    for (; *s && k + 2 < n; s++) {
        if (*s == '"' || *s == '\\') out[k++] = '\\';
        out[k++] = *s;
    }
    out[k] = 0;
}

/* the team.json of mods/eras/teams/1980/<id> (Eras roots): city, name, abbreviation, colours (cached) */
typedef struct { char id[24], city[32], name[32], abbr[8], primary[12], secondary[12]; } TeamLook;
static TeamLook s_looks[40];
static int s_nlooks;
static const TeamLook *team_look(const char *id)
{
    static NjDoc doc;
    char rel[96], path[MAX_PATH], text[4096];
    TeamLook *t;
    FILE *f;
    size_t n;
    int i, c;
    for (i = 0; i < s_nlooks; i++) if (!strcmp(s_looks[i].id, id)) return &s_looks[i];
    if (s_nlooks >= 40) return NULL;
    t = &s_looks[s_nlooks++];
    memset(t, 0, sizeof *t);
    snprintf(t->id, sizeof t->id, "%s", id);
    snprintf(t->name, sizeof t->name, "%s", id);
    snprintf(rel, sizeof rel, "teams\\1980\\%s\\team.json", id);
    nfl2k5_eras_path(rel, path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) return t;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (nj_parse(&doc, text, n)) return t;
    snprintf(t->city, sizeof t->city, "%s", nj_str(&doc, 0, "city", ""));
    snprintf(t->name, sizeof t->name, "%s", nj_str(&doc, 0, "name", id));
    snprintf(t->abbr, sizeof t->abbr, "%s", nj_str(&doc, 0, "abbreviation", ""));
    c = nj_get(&doc, 0, "colors");
    snprintf(t->primary, sizeof t->primary, "%s", c >= 0 ? nj_str(&doc, c, "primary", "#333333") : "#333333");
    snprintf(t->secondary, sizeof t->secondary, "%s", c >= 0 ? nj_str(&doc, c, "secondary", "#999999") : "#999999");
    return t;
}

static void post_pick(const char *name, int idx)
{
    char j[1024], pl[96], co[96];
    const Pick *k;
    const TeamLook *t;
    if (idx < 0 || idx >= s_d.npicks) return;
    k = &s_d.picks[idx];
    t = team_look(k->team);
    jesc(pl, sizeof pl, k->player);
    jesc(co, sizeof co, k->college);
    snprintf(j, sizeof j, "{\"name\":\"%s\",\"year\":1981,\"season\":1980,\"round\":%d,\"pick\":%d,\"roundPick\":%d,"
             "\"team\":\"%s\",\"teamCity\":\"%s\",\"teamName\":\"%s\",\"teamAbbr\":\"%s\",\"primary\":\"%s\","
             "\"secondary\":\"%s\",\"player\":\"%s\",\"position\":\"%s\",\"college\":\"%s\",\"audio\":%s}",
             name, k->round, k->pick, idx - s_d.first + 1, k->team, t ? t->city : "", t ? t->name : k->team,
             t ? t->abbr : "", t ? t->primary : "#333333", t ? t->secondary : "#999999", pl, k->pos, co,
             s_au.muted ? "\"muted\"" : "\"on\"");
    nfl2k5_presentation_post_json(j);
}

static void post_board(const char *name)
{
    char j[12288];
    size_t n;
    int i;
    n = (size_t)snprintf(j, sizeof j, "{\"name\":\"%s\",\"year\":1981,\"season\":1980,\"picks\":[", name);
    for (i = s_d.first; i <= s_d.last && i < s_d.npicks && n + 200 < sizeof j; i++) {
        char pl[96];
        const Pick *k = &s_d.picks[i];
        jesc(pl, sizeof pl, k->player);
        {
            const TeamLook *t = team_look(k->team);
            n += (size_t)snprintf(j + n, sizeof j - n, "%s{\"pick\":%d,\"team\":\"%s\",\"teamName\":\"%s\",\"player\":\"%s\",\"position\":\"%s\",\"made\":%s}",
                                  i > s_d.first ? "," : "", k->pick, k->team, t ? t->name : k->team, i < s_d.cur ? pl : "",
                                  i < s_d.cur ? k->pos : "", i < s_d.cur ? "true" : "false");
        }
    }
    snprintf(j + n, sizeof j - n, "],\"current\":%d}", s_d.cur < s_d.npicks ? s_d.picks[s_d.cur].pick : 0);
    nfl2k5_presentation_post_json(j);
}

/* ---- poses and matrices ------------------------------------------------------------------- */

static void qnorm(float *q)
{
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-6f) { q[0] /= l; q[1] /= l; q[2] /= l; q[3] /= l; } else { q[0] = 1; q[1] = q[2] = q[3] = 0; }
}

/* the pose of clip c at time t (seconds): nlerp between baked frames */
static void sample(const Clip *c, float t, float q[NJ][4], float traj[3])
{
    float f = t * c->fps, a;
    int i0, i1, j, k;
    if (f < 0) f = 0;
    if (f > (float)(c->frames - 1)) f = (float)(c->frames - 1);
    i0 = (int)f; i1 = i0 + 1 < c->frames ? i0 + 1 : i0; a = f - (float)i0;
    {
        const float *r0 = c->data + (size_t)i0 * 103u, *r1 = c->data + (size_t)i1 * 103u;
        for (j = 0; j < NJ; j++) {
            float d = 0, s;
            for (k = 0; k < 4; k++) d += r0[j * 4 + k] * r1[j * 4 + k];
            s = d < 0 ? -1.0f : 1.0f;
            for (k = 0; k < 4; k++) q[j][k] = r0[j * 4 + k] * (1 - a) + s * r1[j * 4 + k] * a;
            qnorm(q[j]);
        }
        for (k = 0; k < 3; k++) traj[k] = r0[100 + k] * (1 - a) + r1[100 + k] * a;
    }
}

static void qmat(const float *q, float m[3][3])
{
    float w = q[0], x = q[1], y = q[2], z = q[3];
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - w * z);     m[0][2] = 2 * (x * z + w * y);
    m[1][0] = 2 * (x * y + w * z);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - w * x);
    m[2][0] = 2 * (x * z - w * y);     m[2][1] = 2 * (y * z + w * x);     m[2][2] = 1 - 2 * (x * x + y * y);
}

static void mmul(float a[3][3], float b[3][3], float o[3][3])
{
    int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) o[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
}

static void mvec(float a[3][3], const float v[3], float o[3])
{
    int i;
    for (i = 0; i < 3; i++) o[i] = a[i][0] * v[0] + a[i][1] * v[1] + a[i][2] * v[2];
}

/* The joint table of the commissioner's mesh, from the loaded shape's transform records
 * (+0x40 absolute, +0x50 local, +0x64 parent). */
static void joints_read(uint32_t scene)
{
    uint32_t node = MEM32(scene + 0x28), shape, tf, n, i;
    s_d.joints_ok = 0;
    if (!node || MEM32(scene + 0x24) < 1) return;
    shape = MEM32(node + 8);
    if (!shape) return;
    n = MEM16(shape + 0x50);
    tf = MEM32(shape + 0x64);
    if (n != NJ || !tf) { DS_LOG("commish01: %u transforms (want %d), table %08X\n", n, NJ, tf); return; }
    for (i = 0; i < NJ; i++) {
        uint32_t r = tf + i * 0x70u;
        memcpy(s_d.jlocal[i], &MEM32(r + 0x50), 12);
        s_d.jparent[i] = (int)MEM32(r + 0x64);
        if (s_d.jparent[i] >= (int)i || s_d.jparent[i] < -1) { DS_LOG("joint %u parent %d: bad table\n", i, s_d.jparent[i]); return; }
    }
    s_d.joints_ok = 1;
    DS_LOG("commish01: 25 joints read (node %08X shape %08X matrices %08X)\n", node, shape, MEM32(node + 0x14));
}

/* ---- the chest-height jersey hold: a pose authored on the 25-joint skeleton ---------------- *
 * Both hands brought to chest height in front of the body, 42 cm apart, elbows down and out
 * (two-bone IK on humerus / elbow, wrists and hands carried along), blended in by `w`. The
 * jersey then hangs from the hands across the chest, facing the camera. Model space (cm). */
static float v3dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float v3norm(float *a) { float l = sqrtf(v3dot(a, a)); if (l > 1e-6f) { a[0] /= l; a[1] /= l; a[2] /= l; } return l; }

static void rot_between(const float *a, const float *b, float R[3][3])
{
    float k[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    float c = v3dot(a, b), s2 = v3dot(k, k), K[3][3], K2[3][3], f;
    int i, j;
    memset(R, 0, 36); R[0][0] = R[1][1] = R[2][2] = 1;
    if (s2 < 1e-10f) return;
    K[0][0] = 0; K[0][1] = -k[2]; K[0][2] = k[1];
    K[1][0] = k[2]; K[1][1] = 0; K[1][2] = -k[0];
    K[2][0] = -k[1]; K[2][1] = k[0]; K[2][2] = 0;
    mmul(K, K, K2);
    f = (1.0f - c) / s2;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) R[i][j] += K[i][j] + K2[i][j] * f;
}

static void rot_apply(float Rw[NJ][3][3], float Pw[NJ][3], float R[3][3], const float *pivot, const int *js, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        float M[3][3], d[3], t[3];
        int j = js[i];
        mmul(R, Rw[j], M);
        memcpy(Rw[j], M, sizeof M);
        for (k = 0; k < 3; k++) d[k] = Pw[j][k] - pivot[k];
        mvec(R, d, t);
        for (k = 0; k < 3; k++) Pw[j][k] = pivot[k] + t[k];
    }
}

static void chest_hold(float Rw[NJ][3][3], float Pw[NJ][3], float w)
{
    static const int arm[2][4] = { { 14, 15, 16, 17 }, { 19, 20, 21, 22 } };   /* humerus elbow wrist hand */
    int s, k;
    if (w > 1.0f) w = 1.0f;
    for (s = 0; s < 2; s++) {
        float side = s == 0 ? 1.0f : -1.0f;                   /* the left arm is on +x (facing +z) */
        float S[3], E[3], H[3], T[3], u[3], v[3], p[3] = { side * 0.6f, -1.0f, -0.35f }, e2[3], h1[3], a1[3], b1[3];
        float lu, ll, d, ca, sa, R[3][3];
        const int *j = arm[s];
        memcpy(S, Pw[j[0]], 12); memcpy(E, Pw[j[1]], 12); memcpy(H, Pw[j[3]], 12);
        /* the target: between the thorax and the neck, 30 cm in front, 21 cm to the side; 4 cm
         * lower than M11 so the wrists stay behind the jersey's top (M12 item 4) */
        T[0] = Pw[10][0] + side * 21.0f;
        T[1] = (Pw[10][1] + Pw[11][1]) * 0.5f - 6.0f;
        T[2] = Pw[10][2] + 30.0f;
        for (k = 0; k < 3; k++) T[k] = H[k] + (T[k] - H[k]) * w;      /* blend from the clip's hand */
        for (k = 0; k < 3; k++) { a1[k] = E[k] - S[k]; b1[k] = H[k] - E[k]; u[k] = T[k] - S[k]; }
        lu = sqrtf(v3dot(a1, a1)); ll = sqrtf(v3dot(b1, b1));
        d = v3norm(u);
        if (d > lu + ll - 0.5f) d = lu + ll - 0.5f;
        if (d < fabsf(lu - ll) + 1.0f) d = fabsf(lu - ll) + 1.0f;
        ca = (lu * lu + d * d - ll * ll) / (2.0f * lu * d);
        if (ca > 1) ca = 1; if (ca < -1) ca = -1;
        sa = sqrtf(1.0f - ca * ca);
        v3norm(p);
        { float pd = v3dot(p, u); for (k = 0; k < 3; k++) v[k] = p[k] - pd * u[k]; }
        v3norm(v);
        for (k = 0; k < 3; k++) e2[k] = S[k] + lu * (ca * u[k] + sa * v[k]);
        /* the upper arm onto the new elbow */
        { float x[3], y[3]; for (k = 0; k < 3; k++) { x[k] = E[k] - S[k]; y[k] = e2[k] - S[k]; }
          v3norm(x); v3norm(y); rot_between(x, y, R); }
        rot_apply(Rw, Pw, R, S, j, 4);
        /* the forearm onto the target */
        memcpy(h1, Pw[j[3]], 12);
        { float x[3], y[3]; for (k = 0; k < 3; k++) { x[k] = h1[k] - e2[k]; y[k] = S[k] + u[k] * d - e2[k]; }
          v3norm(x); v3norm(y); rot_between(x, y, R); }
        rot_apply(Rw, Pw, R, e2, j + 1, 3);
    }
}

/* Write the actor's current joint matrices into its scene's node (+0x14), row-vector form:
 * rows 0..2 = the columns of the world rotation, row 3 = the position. */
static void actor_write(uint32_t scene, const Actor *a)
{
    float Rw[NJ][3][3], Pw[NJ][3], W[3][3], yaw = a->yaw * 3.14159265f / 180.0f, cy = cosf(yaw), sy = sinf(yaw);
    float off[3];
    uint32_t node = MEM32(scene + 0x28), mats;
    int i, r, c;
    if (!node || !s_d.joints_ok) return;
    mats = MEM32(node + 0x14);
    if (!mats) return;
    W[0][0] = cy * SCALE; W[0][1] = 0; W[0][2] = sy * SCALE;
    W[1][0] = 0;          W[1][1] = SCALE; W[1][2] = 0;
    W[2][0] = -sy * SCALE; W[2][1] = 0; W[2][2] = cy * SCALE;
    off[0] = a->x; off[1] = STAGE_Y + a->root_y * SCALE; off[2] = a->z;
    for (i = 0; i < NJ; i++) {
        float R[3][3];
        qmat(a->q[i], R);
        if (s_d.jparent[i] < 0) {
            memcpy(Rw[i], R, sizeof R);
            Pw[i][0] = s_d.jlocal[i][0]; Pw[i][1] = s_d.jlocal[i][1]; Pw[i][2] = s_d.jlocal[i][2];
        } else {
            int p = s_d.jparent[i];
            float t[3];
            mmul(Rw[p], R, Rw[i]);
            mvec(Rw[p], s_d.jlocal[i], t);
            Pw[i][0] = Pw[p][0] + t[0]; Pw[i][1] = Pw[p][1] + t[1]; Pw[i][2] = Pw[p][2] + t[2];
        }
    }
    if (a->ik > 0.0f) chest_hold(Rw, Pw, a->ik);
    memcpy(((Actor *)a)->Rw, Rw, sizeof Rw);
    memcpy(((Actor *)a)->Pw, Pw, sizeof Pw);
    for (i = 0; i < NJ; i++) {
        float M[3][3], T[3];
        uint32_t m = mats + (uint32_t)i * 64u;
        mmul(W, Rw[i], M);
        mvec(W, Pw[i], T);
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) putf(m + (uint32_t)(r * 16 + c * 4), M[c][r]);
            putf(m + (uint32_t)(r * 16 + 12), 0.0f);
        }
        putf(m + 48, T[0] + off[0]); putf(m + 52, T[1] + off[1]); putf(m + 56, T[2] + off[2]); putf(m + 60, 1.0f);
        if (i == 17 || i == 22) {                       /* lhand, rhand */
            float *h = ((Actor *)a)->hand[i == 17 ? 0 : 1];
            h[0] = T[0] + off[0]; h[1] = T[1] + off[1]; h[2] = T[2] + off[2];
        }
    }
}

/* ---- the player's head: a coach head attached to the suited body (M12) -------------------- *
 * A coach package's coachHeadGrp1 is a 29-joint skinned mesh in thorax space: joint 0 is the
 * thorax (its root), 1 the neck, 2 the head (the same local offsets as the body's joints 10,
 * 11, 12), the rest the face. Its current matrices are the body's thorax / neck / head world
 * transforms, and the face joints ride rigidly under theirs, so the game's skinned renderer
 * draws the head where the suited body's own (hidden) face would be. */
static void head_joints_read(uint32_t scene, int node_index)
{
    uint32_t node = MEM32(scene + 0x28) + (uint32_t)node_index * 0x60u, shape, tf, n, i;
    s_d.hjoints_ok = 0;
    if (!MEM32(scene + 0x28) || MEM32(scene + 0x24) <= (uint32_t)node_index) return;
    shape = MEM32(node + 8);
    if (!shape) return;
    n = MEM16(shape + 0x50);
    tf = MEM32(shape + 0x64);
    if (n != NHJ || !tf) { DS_LOG("head: %u transforms (want %d), table %08X\n", n, NHJ, tf); return; }
    for (i = 0; i < NHJ; i++) {
        uint32_t r = tf + i * 0x70u;
        memcpy(s_d.hjlocal[i], &MEM32(r + 0x50), 12);
        s_d.hjparent[i] = (int)MEM32(r + 0x64);
        if (s_d.hjparent[i] >= (int)i || s_d.hjparent[i] < -1) { DS_LOG("head joint %u parent %d: bad table\n", i, s_d.hjparent[i]); return; }
    }
    s_d.hjoints_ok = 1;
    DS_LOG("head: %d joints read (node %08X shape %08X matrices %08X)\n", NHJ, node, shape, MEM32(node + 0x14));
}

static void head_write(uint32_t node, const Actor *a)
{
    static const int body_of[3] = { 10, 11, 12 };        /* thorax, neck, head */
    float R[NHJ][3][3], P[NHJ][3], W[3][3], off[3], yaw = a->yaw * 3.14159265f / 180.0f, cy = cosf(yaw), sy = sinf(yaw);
    uint32_t mats = node ? MEM32(node + 0x14) : 0;
    int i, r, c;
    if (!mats || !s_d.hjoints_ok) return;
    W[0][0] = cy * SCALE; W[0][1] = 0; W[0][2] = sy * SCALE;
    W[1][0] = 0;          W[1][1] = SCALE; W[1][2] = 0;
    W[2][0] = -sy * SCALE; W[2][1] = 0; W[2][2] = cy * SCALE;
    off[0] = a->x; off[1] = STAGE_Y + a->root_y * SCALE; off[2] = a->z;
    for (i = 0; i < NHJ; i++) {
        if (i < 3) {
            memcpy(R[i], a->Rw[body_of[i]], sizeof R[i]);
            memcpy(P[i], a->Pw[body_of[i]], sizeof P[i]);
        } else {
            int p = s_d.hjparent[i] < 0 ? 0 : s_d.hjparent[i];
            float t[3];
            memcpy(R[i], R[p], sizeof R[i]);
            mvec(R[p], s_d.hjlocal[i], t);
            P[i][0] = P[p][0] + t[0]; P[i][1] = P[p][1] + t[1]; P[i][2] = P[p][2] + t[2];
        }
    }
    for (i = 0; i < NHJ; i++) {
        float M[3][3], T[3];
        uint32_t m = mats + (uint32_t)i * 64u;
        mmul(W, R[i], M);
        mvec(W, P[i], T);
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) putf(m + (uint32_t)(r * 16 + c * 4), M[c][r]);
            putf(m + (uint32_t)(r * 16 + 12), 0.0f);
        }
        putf(m + 48, T[0] + off[0]); putf(m + 52, T[1] + off[1]); putf(m + 56, T[2] + off[2]); putf(m + 60, 1.0f);
    }
}

/* ---- the player's suit, tie and skin: palette edits in guest memory (M12) ----------------- *
 * draftply1's textures are P8 with a 1024-byte BGRA palette; the pristine palettes are saved at
 * load and each pick's look is written from them (the commissioner's scene is untouched). */
extern uint32_t xbox_ContiguousAllocatedBytes(void);
static uint32_t phys_va(uint32_t x)
{
    if (x & 0x40000000u) return x & 0x03FFFFFFu;                  /* XBOX_PHYS_LOW_TAG */
    if (x < xbox_ContiguousAllocatedBytes()) return 0x80000000u + x;   /* XBOX_CONTIG_BASE */
    return x;
}

static const char *const k_pal_mat[4] = { "coat", "pants", "hand", "neckmaterial" };

static uint32_t pal_addr(int which)
{
    uint32_t m = s_d.scene[SC_PLAYER] ? find_mat(s_d.scene[SC_PLAYER], k_pal_mat[which]) : 0, t = m ? MEM32(m + 0x30) : 0;
    return t ? phys_va(MEM32(t + 8)) : 0;
}

static void pal_snapshot(void)
{
    int w;
    uint32_t i;
    s_d.pal_ok = 1;
    for (w = 0; w < 4; w++) {
        uint32_t p = pal_addr(w);
        if (!p) { DS_LOG("palette: no %s texture\n", k_pal_mat[w]); s_d.pal_ok = 0; return; }
        for (i = 0; i < 1024; i++) s_d.pal_save[w][i] = MEM8(p + i);
    }
}

static void rgb_to_hls(float r, float g, float b, float *h, float *l, float *s)
{
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
    *l = (mx + mn) * 0.5f;
    if (d < 1e-6f) { *h = 0; *s = 0; return; }
    *s = *l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r) *h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) *h = (b - r) / d + 2.0f;
    else *h = (r - g) / d + 4.0f;
    *h /= 6.0f;
}

static uint8_t clamp8(float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }

static int hex_rgb(const char *s, float rgb[3])
{
    unsigned v;
    if (!s || s[0] != '#' || strlen(s) < 7 || sscanf_s(s + 1, "%6x", &v) != 1) return -1;
    rgb[0] = (float)((v >> 16) & 255); rgb[1] = (float)((v >> 8) & 255); rgb[2] = (float)(v & 255);
    return 0;
}

/* which 0 coat / 1 pants: suit 0 charcoal (as baked) / 1 navy / 2 black; the tie (saturated red
 * entries of the coat) in the team colour, its shading kept. */
static void pal_suit(int which, int suit, const float *tie)
{
    uint32_t p = pal_addr(which), i;
    float tie_l = 0;
    int tie_n = 0;
    if (!p || !s_d.pal_ok) return;
    for (i = 0; i < 256; i++) {
        const uint8_t *e = s_d.pal_save[which] + 4 * i;
        float h, l, s;
        rgb_to_hls(e[2] / 255.0f, e[1] / 255.0f, e[0] / 255.0f, &h, &l, &s);
        if (which == 0 && s > 0.35f && (h < 0.06f || h > 0.92f)) { tie_l += l; tie_n++; }
    }
    if (tie_n) tie_l /= (float)tie_n;
    for (i = 0; i < 256; i++) {
        const uint8_t *e = s_d.pal_save[which] + 4 * i;
        float r = e[2], g = e[1], b = e[0], h, l, s;
        rgb_to_hls(r / 255.0f, g / 255.0f, b / 255.0f, &h, &l, &s);
        if (which == 0 && tie && s > 0.35f && (h < 0.06f || h > 0.92f)) {
            float k = tie_l > 0.02f ? l / tie_l : 1.0f;             /* the entry's shade relative to the tie's mean */
            r = tie[0] * k; g = tie[1] * k; b = tie[2] * k;
        } else if (s < 0.16f && l < 0.62f) {                        /* the cloth (grey after the builder), the belt */
            float v = l * 255.0f;
            if (suit == 1) { r = v * 0.62f; g = v * 0.72f; b = v * 1.45f + 10.0f; }
            else if (suit == 2) { r = v * 0.5f; g = v * 0.5f; b = v * 0.52f; }
        }
        MEM8(p + 4 * i + 0) = clamp8(b); MEM8(p + 4 * i + 1) = clamp8(g); MEM8(p + 4 * i + 2) = clamp8(r);
        MEM8(p + 4 * i + 3) = e[3];
    }
}

/* which 2 hand / 3 neck: the skin entries scaled from their mean to the head's skin colour */
static void pal_skin(int which, const int *rgb)
{
    uint32_t p = pal_addr(which), i;
    float mean[3] = { 0, 0, 0 }, k[3];
    int n = 0, c;
    if (!p || !s_d.pal_ok || !rgb) return;
    for (i = 0; i < 256; i++) {
        const uint8_t *e = s_d.pal_save[which] + 4 * i;
        if (e[2] > e[1] && e[1] >= e[0] * 0.9f && e[2] > 60 && e[2] < 250) { mean[0] += e[2]; mean[1] += e[1]; mean[2] += e[0]; n++; }
    }
    if (n < 8) return;
    for (c = 0; c < 3; c++) { mean[c] /= (float)n; k[c] = mean[c] > 1 ? (float)rgb[c] / mean[c] : 1.0f; }
    for (i = 0; i < 256; i++) {
        const uint8_t *e = s_d.pal_save[which] + 4 * i;
        float r = e[2], g = e[1], b = e[0];
        if (e[2] > e[1] && e[1] >= e[0] * 0.9f && e[2] > 60) { r *= k[0]; g *= k[1]; b *= k[2]; }
        MEM8(p + 4 * i + 0) = clamp8(b); MEM8(p + 4 * i + 1) = clamp8(g); MEM8(p + 4 * i + 2) = clamp8(r);
        MEM8(p + 4 * i + 3) = e[3];
    }
}

/* The look of pick i: head, suit, tie, skin (the choice is deterministic: the player's name). */
static void look_apply(int i)
{
    Pick *k = &s_d.picks[i];
    const TeamLook *t = team_look(k->team);
    float tie[3] = { 160, 30, 40 }, h, l, s;
    const int *skin = NULL;
    static const float k_cloth[3][3] = { { 62, 62, 62 }, { 40, 48, 100 }, { 30, 30, 32 } };   /* charcoal, navy, black as drawn */
    k->head = head_choose(i);
    k->suit = (int)((name_hash(k->player) >> 8) % 3u);
    if (i > s_d.first && k->suit == s_d.picks[i - 1].suit) k->suit = (k->suit + 1) % 3;   /* never the previous pick's suit */
    if (t && !hex_rgb(t->primary, tie)) {
        const float *c = k_cloth[k->suit];
        float d = fabsf(tie[0] - c[0]) + fabsf(tie[1] - c[1]) + fabsf(tie[2] - c[2]);
        rgb_to_hls(tie[0] / 255.0f, tie[1] / 255.0f, tie[2] / 255.0f, &h, &l, &s);
        /* a white / grey primary, or one that would vanish against the suit (navy on navy): the secondary */
        if ((l > 0.82f || s < 0.12f || d < 90.0f) && !hex_rgb(t->secondary, tie)) {}
        rgb_to_hls(tie[0] / 255.0f, tie[1] / 255.0f, tie[2] / 255.0f, &h, &l, &s);
        if (l > 0.82f) { tie[0] *= 0.7f; tie[1] *= 0.7f; tie[2] *= 0.7f; }
    }
    if (k->head >= 0) skin = s_d.heads[k->head].rgb;
    s_d.head_cur = k->head;
    pal_suit(0, k->suit, tie);
    pal_suit(1, k->suit, NULL);
    if (skin) { pal_skin(2, skin); pal_skin(3, skin); }
    DS_LOG("pick %d look: %s, head %s (%s%s), suit %s, tie #%02X%02X%02X%s\n", k->pick, k->player,
           k->head >= 0 ? s_d.heads[k->head].name : "commissioner's", k->head >= 0 ? s_d.heads[k->head].group : "-",
           k->tone[0] ? "" : ", no tone in looks csv: any", k->suit == 1 ? "navy" : k->suit == 2 ? "black" : "charcoal",
           (unsigned)clamp8(tie[0]), (unsigned)clamp8(tie[1]), (unsigned)clamp8(tie[2]), s_d.pal_ok ? "" : " (palettes not found: unchanged)");
}

/* ---- the jersey prop (the set's STAGE quad, reshaped by the pack builder) ------------------- */

static int load_jerseys(void)
{
    char path[MAX_PATH], magic[8];
    FILE *f;
    uint32_t n, i;
    nfl2k5_eras_path("draft\\files\\draftjerseys.bin", path, sizeof path);
    if (fopen_s(&f, path, "rb") || !f) { DS_LOG("no %s: no jersey prop\n", path); return -1; }
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "DSJRSY01", 8) || fread(&n, 4, 1, f) != 1 || n > 64) { fclose(f); return -1; }
    for (i = 0; i < n; i++) {
        Jersey *j = &s_d.jerseys[i];
        j->px = (uint8_t *)malloc(16384 + 1024);
        if (!j->px || fread(&j->pick, 4, 1, f) != 1 || fread(j->team, 1, 24, f) != 24 || fread(j->px, 1, 17408, f) != 17408) { fclose(f); return -1; }
        j->team[23] = 0;
    }
    fclose(f);
    s_d.njerseys = (int)n;
    DS_LOG("jerseys: %u from %s\n", n, path);
    return 0;
}

/* Write pick `pick`'s jersey over the platform texture (P8 128x128, one level). */
static void jersey_apply(int pick)
{
    uint32_t m, t, px, pal, i;
    const Jersey *j = NULL;
    int k;
    if (!s_d.scene[SC_SET]) return;
    for (k = 0; k < s_d.njerseys; k++) if (s_d.jerseys[k].pick == pick) j = &s_d.jerseys[k];
    m = find_mat(s_d.scene[SC_SET], "platform");
    t = m ? MEM32(m + 0x30) : 0;
    if (!j || !t) { DS_LOG("jersey %d: %s\n", pick, j ? "no platform texture" : "none baked"); s_d.jersey_on = 0; return; }
    px = phys_va(MEM32(t + 4));
    pal = phys_va(MEM32(t + 8));
    for (i = 0; i < 16384; i += 4) MEM32(px + i) = *(const uint32_t *)(j->px + i);
    for (i = 0; i < 1024; i += 4) MEM32(pal + i) = *(const uint32_t *)(j->px + 16384 + i);
    DS_LOG("jersey %d (%s): texture %08X data %08X -> %08X palette %08X -> %08X\n", pick, j->team, t,
           MEM32(t + 4), px, MEM32(t + 8), pal);
}

static float s_jersey_drop = -0.05f;
/* Place the quad between the player's hands, facing the way he faces. */
static void jersey_place(const Actor *a)
{
    float r[3], up[3] = { 0, 1, 0 }, n[3], c[3], w, l, h = 70.0f * SCALE;
    uint32_t m;
    int i;
    if (!s_d.jersey_node || !(m = MEM32(s_d.jersey_node + 0x14))) return;
    for (i = 0; i < 3; i++) { r[i] = a->hand[0][i] - a->hand[1][i]; c[i] = (a->hand[0][i] + a->hand[1][i]) * 0.5f; }
    r[1] = 0;
    l = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (l < 1.0f) { r[0] = cosf(a->yaw * 3.14159265f / 180.0f); r[2] = -sinf(a->yaw * 3.14159265f / 180.0f); l = 1.0f; }
    w = l * 1.15f; if (w < 52.0f * SCALE) w = 52.0f * SCALE; if (w > 70.0f * SCALE) w = 70.0f * SCALE;
    r[0] /= l; r[2] /= l;
    n[0] = r[1] * up[2] - r[2] * up[1]; n[1] = r[2] * up[0] - r[0] * up[2]; n[2] = r[0] * up[1] - r[1] * up[0];
    c[1] -= h * s_jersey_drop;                            /* 0: centred on the hands (overhead); 0.38: hung from them */
    for (i = 0; i < 3; i++) {
        putf(m + (uint32_t)i * 4u, r[i] * w);
        putf(m + 16u + (uint32_t)i * 4u, up[i] * h);
        putf(m + 32u + (uint32_t)i * 4u, n[i] + n[i] * 3.0f);   /* forward of the hands a little */
        putf(m + 48u + (uint32_t)i * 4u, c[i] + n[i] * 4.0f * SCALE);
    }
    putf(m + 12, 0); putf(m + 28, 0); putf(m + 44, 0); putf(m + 60, 1.0f);
}

static void actor_clip(Actor *a, const char *clip, float from, float to, float speed, int loop, float fade)
{
    int c = clip_find(clip);
    if (fade > 0) { memcpy(a->fq, a->q, sizeof a->q); a->fade = 1.0f; }
    a->clip = c;
    a->from = from; a->to = to; a->t = from; a->speed = speed; a->loop = loop; a->freeze = 0;
    if (c < 0) DS_LOG("clip %s missing\n", clip);
}

static void actor_tick(Actor *a, float dt)
{
    float q[NJ][4], traj[3];
    int j, k;
    if (a->clip < 0 || a->clip >= s_d.nclips) return;
    if (!a->freeze) {
        a->t += dt * a->speed;
        if (a->t > a->to) {
            if (a->loop && a->to > a->from) a->t = a->from + fmodf(a->t - a->from, a->to - a->from);
            else a->t = a->to;
        }
    }
    sample(&s_d.clips[a->clip], a->t, q, traj);
    /* the root height relative to the clip's first frame (the trophy clip stands on a 75 cm stage) */
    a->root_y = 107.0f + traj[1] - s_d.clips[a->clip].data[101];
    if (a->fade > 0) {
        float w = a->fade;
        for (j = 0; j < NJ; j++) {
            float d = 0, s;
            for (k = 0; k < 4; k++) d += q[j][k] * a->fq[j][k];
            s = d < 0 ? -1.0f : 1.0f;
            for (k = 0; k < 4; k++) q[j][k] = q[j][k] * (1 - w) + s * a->fq[j][k] * w;
            qnorm(q[j]);
        }
        a->fade -= dt / 0.3f;
    }
    memcpy(a->q, q, sizeof q);
}

/* ---- camera ------------------------------------------------------------------------------- */

static void shot_vec(int s, float v[7])
{
    memcpy(v, k_shot[s].eye, 12); memcpy(v + 3, k_shot[s].tgt, 12); v[6] = k_shot[s].focal;
}

static void cam_go(int s, float len)
{
    float cur[7];
    float a = s_d.cam_len > 0 ? s_d.cam_t / s_d.cam_len : 1.0f;
    int i;
    if (a > 1) a = 1;
    a = a * a * (3 - 2 * a);
    for (i = 0; i < 7; i++) cur[i] = s_d.cam_from[i] + (s_d.cam_to[i] - s_d.cam_from[i]) * a;
    memcpy(s_d.cam_from, cur, sizeof cur);
    shot_vec(s, s_d.cam_to);
    s_d.cam_t = 0;
    s_d.cam_len = len;
    if (len <= 0) memcpy(s_d.cam_from, s_d.cam_to, sizeof cur);
}

static void cam_write(void)
{
    float a = s_d.cam_len > 0 ? s_d.cam_t / s_d.cam_len : 1.0f, v[7], f[3], r[3], u[3], l;
    uint32_t c = s_d.camrec;
    int i;
    if (a > 1) a = 1;
    a = a * a * (3 - 2 * a);
    for (i = 0; i < 7; i++) v[i] = s_d.cam_from[i] + (s_d.cam_to[i] - s_d.cam_from[i]) * a;
    /* back = eye - target; right = up_world x back; up = back x right */
    f[0] = v[0] - v[3]; f[1] = v[1] - v[4]; f[2] = v[2] - v[5];
    l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (l < 1e-3f) l = 1; f[0] /= l; f[1] /= l; f[2] /= l;
    r[0] = f[2]; r[1] = 0; r[2] = -f[0];
    l = sqrtf(r[0] * r[0] + r[2] * r[2]); if (l < 1e-3f) l = 1; r[0] /= l; r[2] /= l;
    u[0] = f[1] * r[2] - f[2] * r[1]; u[1] = f[2] * r[0] - f[0] * r[2]; u[2] = f[0] * r[1] - f[1] * r[0];
    putf(c + 0x10, r[0]); putf(c + 0x14, r[1]); putf(c + 0x18, r[2]); putf(c + 0x1C, 0);
    putf(c + 0x20, u[0]); putf(c + 0x24, u[1]); putf(c + 0x28, u[2]); putf(c + 0x2C, 0);
    putf(c + 0x30, f[0]); putf(c + 0x34, f[1]); putf(c + 0x38, f[2]); putf(c + 0x3C, 0);
    putf(c + 0x40, v[0]); putf(c + 0x44, v[1]); putf(c + 0x48, v[2]); putf(c + 0x4C, 1);
    putf(c + 0x50, v[6]);
}

/* ---- package ------------------------------------------------------------------------------ */

/* The Draft Show setting: NFL2K5_ERAS_DRAFTSHOW when set, else league.ini draft_show (M12). */
static int mode_parse(const char *e)
{
    return !e || !*e ? 1 : !_stricmp(e, "off") || !strcmp(e, "0") ? 0 : !_stricmp(e, "mine") || !strcmp(e, "2") ? 2 : 1;
}

int nfl2k5_eras_draftshow_mode_forced(void)
{
    const char *e = getenv("NFL2K5_ERAS_DRAFTSHOW");
    return e && *e;
}

static int mode_now(void)
{
    return nfl2k5_eras_draftshow_mode_forced() ? mode_parse(getenv("NFL2K5_ERAS_DRAFTSHOW")) : mode_parse(nfl2k5_eras_draft_show_saved());
}

static int init(void)
{
    int i;
    if (s_d.init) return s_d.mem ? 0 : -1;
    s_d.init = 1;
    { const char *e = getenv("NFL2K5_ERAS_LOG"); s_d.log = e && *e && *e != '0'; }
    s_d.mode = mode_now();
    s_d.size = 16u * 1024u;
    s_d.mem = xbox_HeapAlloc(s_d.size, 16);
    if (!s_d.mem) return -1;
    s_d.camobj = d_alloc(0x2C0, 16);
    s_d.camsave = d_alloc(0x2C0, 16);
    s_d.camrec = d_alloc(0x60, 16);
    s_d.slot = d_alloc(0x40, 16);
    s_d.w_ctx = d_wstr("ERASDRAFT");
    s_d.w_file = d_wstr("erasdraft.iff");
    for (i = 0; i < SCENES; i++) s_d.w_scene[i] = d_wstr(k_scene[i]);
    if (!s_d.nheads) load_heads();
    for (i = 0; i < s_d.nheads; i++) s_d.w_head[i] = d_wstr(s_d.heads[i].name);
    MEM32(s_d.camrec) = d_wstr("erasdraftcam");
    putf(s_d.camrec + 0x50, 35.0f);
    if (!s_d.w_scene[SCENES - 1]) { s_d.mem = 0; return -1; }
    return 0;
}

int nfl2k5_eras_draftshow_ready(void)
{
    if (init()) return 0;
    if (!s_d.ready_checked) {
        s_d.ready_checked = 1;
        s_d.ready = nfl2k5_eras_exists("office\\files\\erasdraft.iff") && nfl2k5_eras_exists("draft\\files\\draftposes.bin");
        if (!s_d.ready) DS_LOG("not ready: erasdraft.iff / draftposes.bin missing (python -I tools\\eras\\draftshow\\build-draftshow-pack.py)\n");
    }
    return s_d.ready;
}

int nfl2k5_eras_draftshow_mode(void) { init(); s_d.mode = mode_now(); return s_d.mode; }
const char *nfl2k5_eras_draftshow_mode_name(int m) { return m == 0 ? "Off" : m == 2 ? "Your Picks" : "First Round"; }
int nfl2k5_eras_draftshow_set_mode(int m)
{
    static const char *const k_key[3] = { "off", "first", "mine" };
    init();
    if (m < 0 || m > 2) m = 1;
    if (nfl2k5_eras_draftshow_mode_forced()) { DS_LOG("mode: NFL2K5_ERAS_DRAFTSHOW is set: the setting is not changed\n"); return -1; }
    if (nfl2k5_eras_set_draft_show_saved(k_key[m])) return -1;
    s_d.mode = m;
    DS_LOG("mode: %s (league.ini draft_show = %s)\n", nfl2k5_eras_draftshow_mode_name(m), k_key[m]);
    return 0;
}

static int load(void)
{
    uint32_t save_ebx = g_ebx, save_esi = g_esi, save_edi = g_edi, save_ebp = g_ebp, heap;
    DWORD t0 = GetTickCount();
    int i;
    if (s_d.loaded) return 0;
    PUSH32(g_esp, 0); PUSH32(g_esp, 0);
    heap = gcall(sub_00038FB0, 0, 0);
    PUSH32(g_esp, heap);
    PUSH32(g_esp, s_d.slot);
    gcall(sub_00043F50, s_d.w_ctx, s_d.w_file);
    gcall(sub_000432D0, 0, 0);
    for (i = 0; i < SCENES; i++) s_d.scene[i] = lookup(s_d.w_ctx, FOURCC_SCNE, s_d.w_scene[i]);
    for (i = 0; i < s_d.nheads; i++) s_d.head_scene[i] = lookup(s_d.w_ctx, FOURCC_SCNE, s_d.w_head[i]);
    g_ebx = save_ebx; g_esi = save_esi; g_edi = save_edi; g_ebp = save_ebp;
    DS_LOG("load: ERASDRAFT draft_menu %08X cutscene %08X commish01 %08X draftply1 %08X; %lu ms\n",
           s_d.scene[0], s_d.scene[1], s_d.scene[2], s_d.scene[3], (unsigned long)(GetTickCount() - t0));
    if (!s_d.scene[SC_ROOM]) {
        gcall(sub_000432F0, s_d.w_ctx, 0);
        return -1;
    }
    s_d.loaded = 1;
    /* the stage: hide the war room's desk, monitors, PDA and static figures */
    for (i = 0; i < (int)(sizeof k_hide_room / sizeof k_hide_room[0]); i++) {
        uint32_t n = find_node(s_d.scene[SC_ROOM], k_hide_room[i]);
        if (n) MEM32(n + 0x0C) |= 0x10u;
        else DS_LOG("room: no node %s\n", k_hide_room[i]);
    }
    /* the trophy set: only the podium, scaled to the room and placed stage left */
    if (s_d.scene[SC_SET]) {
        uint32_t sc = s_d.scene[SC_SET], n = MEM32(sc + 0x24), a = MEM32(sc + 0x28), k;
        for (k = 0; k < n && k < 64; k++) {
            uint32_t node = a + k * 0x60u, m = MEM32(node + 0x14);
            if (weq(MEM32(node), "STAGE")) { s_d.jersey_node = node; MEM32(node + 0x0C) |= 0x14u; continue; }
            if (!weq(MEM32(node), "podium")) { MEM32(node + 0x0C) |= 0x10u; continue; }
            if (!m) { DS_LOG("podium: no node matrix\n"); continue; }
            for (i = 0; i < 16; i++) putf(m + (uint32_t)i * 4u, (i % 5 == 0) ? SCALE : 0.0f);
            putf(m + 60, 1.0f);
            /* the podium's own centre in the set is about (-17.5, 75, 10) cm */
            putf(m + 48, k_podium[0] + 17.5f * SCALE);
            putf(m + 52, STAGE_Y - 75.0f * SCALE);
            putf(m + 56, k_podium[1] - 10.1f * SCALE);
            MEM32(node + 0x0C) |= 0x4u;              /* no frustum test on the moved node */
        }
    }
    /* the actors: skip the frustum test (their joint matrices move them); the player has no glasses */
    for (i = SC_COMMISH; i <= SC_PLAYER; i++)
        if (s_d.scene[i] && MEM32(s_d.scene[i] + 0x28)) MEM32(MEM32(s_d.scene[i] + 0x28) + 0x0C) |= 0x4u;
    if (s_d.scene[SC_PLAYER]) {
        uint32_t g = find_mat(s_d.scene[SC_PLAYER], "glasses");
        if (g) MEM32(g + 8) |= 1u;
        pal_snapshot();
    }
    /* the heads: only the head node of each coach package, drawn with no frustum test */
    s_d.hjoints_ok = 0;
    for (i = 0; i < s_d.nheads; i++) {
        char nm[32];
        uint32_t sc = s_d.head_scene[i], n;
        s_d.head_node[i] = 0;
        if (!sc) { DS_LOG("head %s: not in erasdraft.iff (rebuild the pack)\n", s_d.heads[i].name); continue; }
        snprintf(nm, sizeof nm, "%sBodyGrp1", s_d.heads[i].name);
        if ((n = find_node(sc, nm))) MEM32(n + 0x0C) |= 0x10u;
        snprintf(nm, sizeof nm, "%sLodGrp1", s_d.heads[i].name);
        if ((n = find_node(sc, nm))) MEM32(n + 0x0C) |= 0x10u;
        snprintf(nm, sizeof nm, "%sHeadGrp1", s_d.heads[i].name);
        s_d.head_node[i] = find_node(sc, nm);
        if (!s_d.head_node[i]) { DS_LOG("head %s: no node %s\n", s_d.heads[i].name, nm); continue; }
        MEM32(s_d.head_node[i] + 0x0C) |= 0x4u;
        if (!s_d.hjoints_ok) head_joints_read(sc, (int)((s_d.head_node[i] - MEM32(sc + 0x28)) / 0x60u));
    }
    if (s_d.hjoints_ok && s_d.scene[SC_PLAYER]) {
        /* the suited body's own face goes: its head is one of the coach heads */
        uint32_t f = find_mat(s_d.scene[SC_PLAYER], "face");
        if (f) MEM32(f + 8) |= 1u;
        DS_LOG("heads: %d coach heads ready; the player's face material hidden\n", s_d.nheads);
    }
    s_d.head_cur = -1;
    if (s_d.scene[SC_COMMISH]) joints_read(s_d.scene[SC_COMMISH]);
    gcall(sub_00066B20, s_d.camobj, 0);
    return 0;
}

static void unload(void)
{
    if (!s_d.loaded) return;
    gcall(sub_000432F0, s_d.w_ctx, 0);
    s_d.loaded = 0;
    memset(s_d.scene, 0, sizeof s_d.scene);
    memset(s_d.head_scene, 0, sizeof s_d.head_scene);
    memset(s_d.head_node, 0, sizeof s_d.head_node);
    s_d.hjoints_ok = s_d.pal_ok = 0;
    DS_LOG("unloaded ERASDRAFT\n");
}

/* One line at the end naming the picks whose call had no number / team clip (M12 item 7). */
static void summary_log(void)
{
    char line[1024];
    size_t n = 0;
    int i, missing = 0;
    if (s_d.summary_done) return;
    s_d.summary_done = 1;
    line[0] = 0;
    for (i = s_d.first; i <= s_d.last && i < s_d.npicks; i++) {
        const Pick *k = &s_d.picks[i];
        if (!k->no_number && !k->no_team) continue;
        missing++;
        if (n + 80 < sizeof line)
            n += (size_t)snprintf(line + n, sizeof line - n, "%s%d %s (%s%s%s)", n ? ", " : "", k->pick, k->team,
                                  k->no_number ? "number" : "", k->no_number && k->no_team ? " + " : "", k->no_team ? "team" : "");
    }
    if (missing) DS_LOG("audio summary: %d pick(s) had no clip to record: %s\n", missing, line);
    else DS_LOG("audio summary: every pick shown had its number and team clips\n");
}

/* ---- the show ----------------------------------------------------------------------------- */

static int user_pick(int i)
{
    int u = nfl2k5_eras_user_team();
    const ErasTeam *t = u >= 0 ? nfl2k5_eras_team(u) : NULL;
    return t && i >= 0 && i < s_d.npicks && !strcmp(s_d.picks[i].team, t->id);
}

static void phase(int ph)
{
    s_d.phase = ph;
    s_d.pt = 0;
    DS_LOG("pick %d: %s (t %.2f s)\n", s_d.cur < s_d.npicks ? s_d.picks[s_d.cur].pick : 0, k_phase[ph], s_d.clock);
}

static void pick_begin(void)
{
    /* Your Picks: the other picks go straight onto the board */
    while (s_d.mode == 2 && s_d.cur <= s_d.last && !user_pick(s_d.cur)) s_d.cur++;
    if (s_d.cur > s_d.last) {
        phase(PH_END);
        post_board("DRAFT_ROUND_SUMMARY");
        cam_go(SH_WIDE, 3.0f);
        summary_log();
        return;
    }
    s_d.player_on = 0;
    actor_clip(&s_d.commish, "idle", 0.0f, 7.0f, 1.0f, 1, 0.3f);
    s_d.jersey_on = 0;
    jersey_apply(s_d.picks[s_d.cur].pick);
    look_apply(s_d.cur);
    post_board("DRAFT_BOARD");
    post_pick("DRAFT_ON_CLOCK", s_d.cur);
    cam_go(SH_BOARD, 1.5f);
    phase(PH_CLOCK);
}

static void show_start(void)
{
    const char *e;
    int k, round = 1;
    s_d.first = 0;
    for (k = 0; k < s_d.npicks && s_d.picks[k].round == round; k++) {}
    s_d.last = k - 1;
    if ((e = getenv("NFL2K5_ERAS_DRAFTSHOW_PICKS")) && atoi(e) > 0 && s_d.first + atoi(e) - 1 < s_d.last)
        s_d.last = s_d.first + atoi(e) - 1;                       /* test switch: only the first N picks */
    s_d.cur = s_d.first;
    s_d.mode = mode_now();
    s_d.summary_done = 0;
    for (k = 0; k < s_d.npicks; k++) { s_d.picks[k].no_number = s_d.picks[k].no_team = 0; s_d.picks[k].head = -1; }
    if ((e = getenv("NFL2K5_ERAS_DRAFTSHOW_SKIP")) && !_stricmp(e, "mine")) {   /* "Skip to my pick" */
        for (k = s_d.first; k <= s_d.last && !user_pick(k); k++) {}
        if (k <= s_d.last) s_d.cur = k;
    }
    if ((e = getenv("NFL2K5_ERAS_DRAFTSHOW_START")) && atoi(e) > 0) s_d.cur = s_d.first + atoi(e) - 1;
    memset(&s_d.commish, 0, sizeof s_d.commish);
    memset(&s_d.player, 0, sizeof s_d.player);
    s_d.commish.x = k_commish[0]; s_d.commish.z = k_commish[1]; s_d.commish.yaw = 0;
    actor_clip(&s_d.commish, "idle", 0.0f, 7.0f, 1.0f, 1, 0);
    s_d.player.clip = -1;
    shot_vec(SH_WIDE, s_d.cam_from);
    shot_vec(SH_WIDE, s_d.cam_to);
    s_d.cam_t = s_d.cam_len = 0;
    s_d.clock = 0;
    DS_LOG("show: mode %s, picks %d..%d of %d (round 1, 1981)\n", nfl2k5_eras_draftshow_mode_name(s_d.mode),
           s_d.picks[s_d.cur].pick, s_d.picks[s_d.last].pick, s_d.npicks);
    nfl2k5_presentation_post_json("{\"name\":\"DRAFT_SHOW_START\",\"year\":1981,\"season\":1980,\"round\":1}");
    { const char *r = getenv("NFL2K5_ERAS_DRAFTSHOW_REC"); if (r && atoi(r) > 0) xbox_PresentRecord(atoi(r)); }
    phase(PH_INTRO);
}

static void next_pick(void)
{
    au_stop();
    duck(1.0f);
    s_d.cur++;
    nfl2k5_presentation_post_json("{\"name\":\"DRAFT_CLEAR\"}");
    pick_begin();
}

static void tick(float dt)
{
    const Pick *k = s_d.cur < s_d.npicks ? &s_d.picks[s_d.cur] : NULL;
    s_d.pt += dt;
    s_d.cam_t += dt;
    s_d.clock += dt;
    switch (s_d.phase) {
    case PH_INTRO:
        if (s_d.pt > 3.5f) pick_begin();
        break;
    case PH_CLOCK:
        /* watched picks: a short beat with the clock running fast (any button ends it) */
        if (s_d.pt > 3.0f || s_d.clock_skip) {
            s_d.clock_skip = 0;
            duck(DUCK_GAME);                                    /* the music under the call */
            au_stop();
            s_d.vstep = au_play(CH_CHIME, "pick_is_in_chime.mp3", VOL_CHIME) ? 2 : 0;
            post_pick("DRAFT_PICK_IN", s_d.cur);
            cam_go(SH_PODIUM, 3.0f);
            phase(PH_CHIME);
        }
        break;
    case PH_CHIME:
        /* "With the Nth pick" at 3.4 s into the chime, over its tail (caption at its first sample) */
        if (s_d.vstep == 0) {
            double el = 0;
            int st = au_state(CH_CHIME, &el);
            if (st == 0 || (st == 2 && el >= CHIME_CALL)) { s_d.vstep = 2; s_d.vt = CLIP_GAP; }
        }
        if (s_d.vstep == 2 && (s_d.vt += dt) >= CLIP_GAP) {
            char rel[48];
            snprintf(rel, sizeof rel, "numbers\\number_%02d.mp3", k ? k->pick : 1);
            s_d.vstep = au_play(CH_NUMBER, rel, VOL_VOICE) ? 3 : 1;          /* 3: no clip: straight to the team */
            phase(PH_NUMBER);
            if (s_d.vstep == 3) { DS_LOG("audio: no number clip for pick %d: chime, then the team\n", k ? k->pick : 0); if (k) ((Pick *)k)->no_number = 1; }
        }
        break;
    case PH_NUMBER:
        if (s_d.vstep == 1 && au_state(CH_NUMBER, NULL) == 2) { post_pick("DRAFT_ANNOUNCE", s_d.cur); s_d.vstep = 4; }
        if ((s_d.vstep == 4 || s_d.vstep == 1) && s_d.pt > 0.2f && au_state(CH_NUMBER, NULL) == 0) { s_d.vstep = 5; s_d.vt = 0; }
        if (s_d.vstep == 3) { post_pick("DRAFT_ANNOUNCE", s_d.cur); s_d.vstep = 5; s_d.vt = CLIP_GAP; }
        if (s_d.vstep == 5 && (s_d.vt += dt) >= CLIP_GAP) {
            const char *clip = k ? team_clip(k->team) : NULL;
            s_d.team_clip = clip && !au_play(CH_TEAM, clip, VOL_VOICE);
            if (!s_d.team_clip) { DS_LOG("audio: no team clip for %s: caption held, then the card\n", k ? k->team : "?"); post_pick("DRAFT_TEAM", s_d.cur); if (k) ((Pick *)k)->no_team = 1; }
            s_d.vstep = 0;
            phase(PH_TEAM);
        }
        break;
    case PH_TEAM:
        /* "...the <team> select" at the team clip's first sample; the card when it ends */
        if (s_d.team_clip && s_d.vstep == 0 && au_state(CH_TEAM, NULL) == 2) { post_pick("DRAFT_TEAM", s_d.cur); s_d.vstep = 1; }
        if ((s_d.team_clip && s_d.pt > 0.2f && au_state(CH_TEAM, NULL) == 0) || (!s_d.team_clip && s_d.pt > 0.9f)) {
            if (s_d.team_clip && s_d.vstep == 0) post_pick("DRAFT_TEAM", s_d.cur);
            post_pick("DRAFT_PICK_CARD", s_d.cur);
            cam_go(SH_WIDE, 2.0f);
            phase(PH_CARD);
        }
        break;
    case PH_CARD:
        if (s_d.pt > 2.5f) {
            /* the player walks on from stage right */
            memset(&s_d.player, 0, sizeof s_d.player);
            s_d.player.x = k_player_from[0]; s_d.player.z = k_player_from[1];
            s_d.player.yaw = (float)(atan2(k_player_to[0] - k_player_from[0], k_player_to[1] - k_player_from[1]) * 180.0 / 3.14159265);
            actor_clip(&s_d.player, "walk", 0.4f, 2.4f, 1.5f, 1, 0);
            duck(1.0f);                                        /* the card beat is over: the music back up */
            s_d.player_on = 1;
            cam_go(SH_WALK, 1.5f);
            phase(PH_WALK);
        }
        break;
    case PH_WALK: {
        float dx = k_player_to[0] - s_d.player.x, dz = k_player_to[1] - s_d.player.z;
        float d = sqrtf(dx * dx + dz * dz), step = 54.0f * SCALE * dt;   /* 54 cm/s: the clip at 1.5x */
        if (d <= step || s_d.pt > 12.0f) {
            s_d.player.x = k_player_to[0]; s_d.player.z = k_player_to[1];
            s_d.player.yaw = -90.0f;                 /* face the commissioner */
            actor_clip(&s_d.player, "shake2", 1.0f, 2.6f, 1.0f, 0, 0.3f);
            s_d.commish.yaw = 60.0f;
            actor_clip(&s_d.commish, "shake2", 1.0f, 2.6f, 1.0f, 0, 0.3f);
            nfl2k5_presentation_post_json("{\"name\":\"DRAFT_HANDSHAKE\"}");
            cam_go(SH_TWO, 1.0f);
            phase(PH_SHAKE);
        } else {
            s_d.player.x += dx / d * step; s_d.player.z += dz / d * step;
        }
        break;
    }
    case PH_SHAKE:
        if (s_d.pt > 1.8f) {
            /* both turn to the cameras; the player raises the jersey overhead (helmet-raise clip, frozen) */
            s_d.player.yaw = -10.0f;
            s_d.commish.yaw = 15.0f;
            actor_clip(&s_d.player, "raise", 3.0f, 3.3f, 1.0f, 0, 0.35f);
            s_jersey_drop = -0.05f;
            actor_clip(&s_d.commish, "idle", 0.0f, 7.0f, 1.0f, 1, 0.35f);
            post_pick("DRAFT_PHOTO", s_d.cur);
            s_d.jersey_on = s_d.njerseys > 0;
            cam_go(SH_HOLD, 1.2f);
            phase(PH_HOLD);
        }
        break;
    case PH_HOLD:
        /* 0-2.2 s overhead (helmet-raise clip frozen at 3.25 s); then the chest-height hold for the
         * cameras: the MVP-trophy clip at 2.75 s (both hands in front at chin height, 64 cm apart),
         * the jersey hung from the hands */
        if (s_d.pt > 2.2f && s_d.pt - dt <= 2.2f) {
            actor_clip(&s_d.player, "idle", 1.0f, 6.0f, 1.0f, 1, 0.0f);
            s_jersey_drop = 0.40f;                   /* M12: the top edge 7 cm above the hands (was 3.5) */
            cam_go(SH_PUSH, 1.6f);
            DS_LOG("pick %d: chest-height hold (authored pose)\n", k ? k->pick : 0);
        }
        if (s_d.pt > 2.2f && s_d.player.ik < 1.0f) s_d.player.ik += dt / 0.45f;
        if (s_d.pt > 6.0f) { nfl2k5_presentation_post_json("{\"name\":\"DRAFT_CLEAR\"}"); phase(PH_OUT); }
        break;
    case PH_OUT:
        if (s_d.pt > 0.8f) {
            s_d.commish.yaw = 0;
            next_pick();
        }
        break;
    case PH_END:
        break;
    }
    bed_tick(dt);
    actor_tick(&s_d.commish, dt);
    if (s_d.player_on) actor_tick(&s_d.player, dt);
}

static void skip(void)
{
    if (s_d.phase == PH_END || s_d.phase == PH_OFF) return;
    if (s_d.phase == PH_CLOCK) { s_d.clock_skip = 1; DS_LOG("skip: on the clock ends\n"); return; }
    DS_LOG("skip pick %d\n", s_d.cur < s_d.npicks ? s_d.picks[s_d.cur].pick : 0);
    if (s_d.phase == PH_INTRO) { pick_begin(); return; }
    s_d.commish.yaw = 0;
    next_pick();
}

/* START: on to the user's next pick (the others go onto the board), or the round's end */
static void skip_to_mine(void)
{
    int k;
    if (s_d.phase == PH_END || s_d.phase == PH_OFF) return;
    for (k = s_d.cur + 1; k <= s_d.last && !user_pick(k); k++) {}
    DS_LOG("START: from pick %d to %s\n", s_d.picks[s_d.cur].pick, k <= s_d.last ? "the user's next pick" : "the round's end");
    au_stop();
    duck(1.0f);
    s_d.commish.yaw = 0;
    s_d.cur = k - 1;
    next_pick();
}

void nfl2k5_eras_draftshow_skip(void) { skip(); }

static void shown(void)
{
    char root[MAX_PATH], pkg[MAX_PATH];
    int i;
    if (!s_d.loaded) return;
    s_d.shown = 1;
    nfl2k5_eras_path("draft", root, sizeof root);
    nfl2k5_eras_path("draft\\show", pkg, sizeof pkg);
    nfl2k5_presentation_frontend_begin(root, pkg, "index.html");
    for (i = 0; i < 4; i++) s_d.pad_prev[i] = MEM32(PAD_WORDS + (uint32_t)i * 0x244u);
    bed_start();
    if (s_d.phase == PH_OFF) show_start();
}

void nfl2k5_eras_draftshow_event(int msg, uint32_t fsm)
{
    switch (msg) {
    case 1:                                      /* create */
        if (!nfl2k5_eras_draftshow_ready()) break;
        if (!s_d.nclips && load_poses()) break;
        if (!s_d.npicks && load_picks(1981)) break;
        if (!s_d.njerseys) load_jerseys();
        s_d.phase = PH_OFF;
        if (load() == 0) DS_LOG("create: stage loaded\n");
        break;
    case 2:                                      /* destroy */
        au_stop();
        bed_stop();
        if (s_d.phase != PH_OFF) summary_log();
        nfl2k5_presentation_frontend_end();
        s_d.shown = 0;
        s_d.phase = PH_OFF;
        unload();
        break;
    case 4: shown(); break;
    case 5:                                      /* covered */
        au_stop();
        bed_stop();
        nfl2k5_presentation_frontend_end();
        s_d.shown = 0;
        break;
    case 6: {                                    /* frame */
        float dt;
        int i;
        if (!s_d.shown || !s_d.loaded) break;
        dt = MEMF(fsm + FSM_DT);
        if (!(dt > 0.0f && dt < 0.25f)) dt = 1.0f / 60.0f;
        for (i = 0; i < 4; i++) {
            uint32_t w = MEM32(PAD_WORDS + (uint32_t)i * 0x244u), edge = w & ~s_d.pad_prev[i];
            s_d.pad_prev[i] = w;
            if (edge & 0x10u) { skip_to_mine(); break; }
            if (edge & BTN_SKIP) { skip(); break; }
        }
        tick(dt);
        if ((++s_d.frames % 300) == 0 && s_d.log)
            DS_LOG("frame %d: phase %s, commish clip %d t %.2f, player %d\n", s_d.frames, k_phase[s_d.phase],
                   s_d.commish.clip, s_d.commish.t, s_d.player_on);
        break;
    }
    case 7:                                      /* draw */
        if (!s_d.shown || !s_d.loaded) break;
        cam_write();
        gcall(sub_0002AD80, s_d.camsave, 0);
        gcall(sub_0013E710, s_d.camobj, s_d.camrec);
        gcall(sub_0002AC80, s_d.camobj, 0);
        gcall(sub_0002DE60, s_d.scene[SC_ROOM], 0);
        if (s_d.scene[SC_COMMISH] && !getenv("NFL2K5_ERAS_DRAFTSHOW_NOACTORS")) {
            actor_write(s_d.scene[SC_COMMISH], &s_d.commish);
            gcall(sub_0002DE60, s_d.scene[SC_COMMISH], 0);
        }
        if (s_d.player_on && s_d.scene[SC_PLAYER] && !getenv("NFL2K5_ERAS_DRAFTSHOW_NOACTORS")) {
            actor_write(s_d.scene[SC_PLAYER], &s_d.player);
            gcall(sub_0002DE60, s_d.scene[SC_PLAYER], 0);
            /* his head: the pick's coach head on the body's thorax / neck / head joints */
            if (s_d.head_cur >= 0 && s_d.head_cur < s_d.nheads && s_d.head_node[s_d.head_cur] && s_d.hjoints_ok) {
                head_write(s_d.head_node[s_d.head_cur], &s_d.player);
                gcall(sub_0002DE60, s_d.head_scene[s_d.head_cur], 0);
            }
        }
        /* the podium, and the jersey between the player's hands (after his matrices) */
        if (s_d.jersey_node) {
            if (s_d.jersey_on && s_d.player_on) { jersey_place(&s_d.player); MEM32(s_d.jersey_node + 0x0C) &= ~0x10u; }
            else MEM32(s_d.jersey_node + 0x0C) |= 0x10u;
        }
        if (s_d.scene[SC_SET]) gcall(sub_0002DE60, s_d.scene[SC_SET], 0);
        gcall(sub_0002AC80, s_d.camsave, 0);
        break;
    }
}

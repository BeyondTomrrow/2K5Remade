/* Native rendering evidence and limits: docs/SANDBOX-PROP-TRACE.md.
 * sub_21900 stores object+14; sub_21860 immediately consumes that palette.
 * Only a PRIVATE copy is transformed. Original player/bone/AI data stays
 * untouched; culling, shadows and nameplates retain their original placement.
 * Caller owns guest-thread execution and the balanced draw begin/end hooks. */
#include "nfl2k5_sandbox_pose.h"
#include "nfl2k5_sandbox_actors.h"
#include "nfl2k5_sandbox.h"
#include "xbox_memory_layout.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
#define POSE_MATRIX_COUNT 25u
#define POSE_MATRIX_FLOATS 16u
#define POSE_PALETTE_BYTES (POSE_MATRIX_COUNT * 0x40u)
#define POSE_PI 3.14159265358979323846f

static Nfl2k5SandboxPose s_edits[NFL2K5_SANDBOX_MAX_POSES];
static uint32_t s_clock, s_palettes;
static DWORD s_prepared[NFL2K5_SANDBOX_MAX_POSES];
static unsigned char s_reported[NFL2K5_SANDBOX_MAX_POSES];
static struct {
    uint32_t object, shape, previous, replacement, clock;
} s_draw;

static int read_guest(uint32_t at, void *out, size_t size)
{
    uint64_t end = (uint64_t)at + size;
    if (!out || !size || at < 0x10000u || end > 0x08000000u) return 0;
    __try {
        memcpy(out, (const void *)((uintptr_t)xbox_GetMemoryOffset() + at), size);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int write_guest(uint32_t at, const void *data, size_t size)
{
    uint64_t end = (uint64_t)at + size;
    if (!data || !size || at < 0x10000u || end > 0x08000000u) return 0;
    __try {
        memcpy((void *)((uintptr_t)xbox_GetMemoryOffset() + at), data, size);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uint32_t u32(uint32_t at)
{
    uint32_t value = 0; read_guest(at, &value, sizeof value); return value;
}
static int current(uint32_t clock)
{
    uint32_t period;
    if (!clock || !nfl2k5_sandbox_active() || u32(0xA83A18u) != 3u ||
        u32(0xE6028Cu) != clock) return 0;
    period = u32(0xE602C4u);
    return period >= 1u && period <= 8u;
}
static int valid_id(uint32_t id)
{
    return id < NFL2K5_SANDBOX_MAX_POSES &&
           id % NFL2K5_SANDBOX_PLAYER_ID_STRIDE < 65u;
}
int nfl2k5_sandbox_pose_valid(const Nfl2k5SandboxPose *pose)
{
    return pose && valid_id(pose->id) && pose->identity &&
        isfinite(pose->x) && fabsf(pose->x) <= 500 &&
        isfinite(pose->y) && fabsf(pose->y) <= 500 &&
        isfinite(pose->z) && fabsf(pose->z) <= 500 &&
        isfinite(pose->yaw) && fabsf(pose->yaw) <= 180 &&
        isfinite(pose->scale) && pose->scale >= 0.25f && pose->scale <= 3 &&
        pose->clip <= (uint32_t)NFL2K5_SANDBOX_POSE_CLIP_CAP &&
        isfinite(pose->clip_time) && pose->clip_time >= 0 && pose->clip_time <= 600;
}
static int nondefault(const Nfl2k5SandboxPose *pose)
{
    return pose->x != 0 || pose->y != 0 || pose->z != 0 ||
           pose->yaw != 0 || pose->scale != 1 || pose->clip != 0;
}
int nfl2k5_sandbox_pose_bind(void)
{
    /* Distinct stable addresses prevent the native renderer's palette-pointer
     * cache from confusing two edited actors. Allocated once, session data
     * is cleared separately; never free a palette the GPU may still read. */
    if (!s_palettes)
        s_palettes = xbox_HeapAlloc(NFL2K5_SANDBOX_MAX_POSES * POSE_PALETTE_BYTES, 16u);
    return s_palettes != 0;
}
void nfl2k5_sandbox_pose_end_draw(void)
{
    /* The normal hook has already executed the original sub_21900 and draw.
     * Restore only the same model in the still-owned scene, and only if the
     * field still contains OUR replacement. Never overwrite a native change. */
    if (s_draw.object && current(s_draw.clock) &&
        u32(s_draw.object + 8u) == s_draw.shape &&
        u32(s_draw.object + 0x14u) == s_draw.replacement) {
        if (!write_guest(s_draw.object + 0x14u, &s_draw.previous, 4))
            fprintf(stderr, "[SANDBOX-POSE] could not restore model palette %08X\n", s_draw.object);
    }
    memset(&s_draw, 0, sizeof s_draw);
}
void nfl2k5_sandbox_pose_clear(uint32_t clock)
{
    nfl2k5_sandbox_pose_end_draw();
    memset(s_edits, 0, sizeof s_edits);
    memset(s_prepared,0,sizeof s_prepared);memset(s_reported,0,sizeof s_reported);
    s_clock = current(clock) ? clock : 0;
}
static int session(uint32_t clock)
{
    if (!current(clock)) return 0;
    if (s_clock != clock) nfl2k5_sandbox_pose_clear(clock);
    return s_clock == clock;
}
int nfl2k5_sandbox_pose_get(uint32_t clock, int id, Nfl2k5SandboxPose *out)
{
    Nfl2k5SandboxActorProperties properties;
    Nfl2k5SandboxPose value;
    if (!out || id < 0 || !valid_id((uint32_t)id) || !session(clock) ||
        !nfl2k5_sandbox_actor_get_properties(clock, id, &properties)) return 0;
    memset(&value, 0, sizeof value);
    value.id = properties.id; value.identity = properties.identity; value.scale = 1;
    if (s_edits[id].identity == properties.identity) value = s_edits[id];
    else memset(&s_edits[id], 0, sizeof s_edits[id]);
    *out = value;
    return 1;
}
int nfl2k5_sandbox_pose_set(uint32_t clock, const Nfl2k5SandboxPose *pose)
{
    Nfl2k5SandboxActorProperties properties;
    if (!nfl2k5_sandbox_pose_valid(pose) || !session(clock) ||
        !nfl2k5_sandbox_actor_get_properties(clock, (int)pose->id, &properties) ||
        pose->identity != properties.identity) return 0;
    if (nondefault(pose)) s_edits[pose->id] = *pose;
    else memset(&s_edits[pose->id], 0, sizeof s_edits[pose->id]);
    return 1;
}
static int edit_current(uint32_t clock, int id)
{
    Nfl2k5SandboxActorProperties properties;
    if (!s_edits[id].identity) return 0;
    if (!nfl2k5_sandbox_actor_get_properties(clock, id, &properties) ||
        s_edits[id].identity != properties.identity) {
        memset(&s_edits[id], 0, sizeof s_edits[id]); return 0;
    }
    return 1;
}
int nfl2k5_sandbox_pose_list(uint32_t clock, Nfl2k5SandboxPose *out, int capacity)
{
    int count = 0;
    if (!out || capacity <= 0 || !session(clock)) return 0;
    if (capacity > NFL2K5_SANDBOX_MAX_POSES) capacity = NFL2K5_SANDBOX_MAX_POSES;
    for (int id = 0; id < NFL2K5_SANDBOX_MAX_POSES && count < capacity; ++id)
        if (edit_current(clock, id)) out[count++] = s_edits[id];
    return current(clock) ? count : 0;
}
int nfl2k5_sandbox_pose_count(uint32_t clock)
{
    int count = 0;
    if (!session(clock)) return 0;
    for (int id = 0; id < NFL2K5_SANDBOX_MAX_POSES; ++id)
        if (edit_current(clock, id)) ++count;
    return current(clock) ? count : 0;
}
const char *nfl2k5_sandbox_pose_status(uint32_t clock,int id)
{
    if(id<0||!valid_id((uint32_t)id)||!current(clock)||s_clock!=clock)return "Unavailable";
    if(!s_edits[id].identity)return "Native model placement";
    if(s_prepared[id]&&GetTickCount()-s_prepared[id]<1000)return "Private pose palette prepared";
    return "Waiting for a supported mesh draw";
}
static int record_id(uint32_t record)
{
    const uint32_t bases[] = { 0xB30C4Cu, 0xB321A0u };
    for (int side = 0; side < 2; ++side) {
        uint32_t delta;
        if (record < bases[side]) continue;
        delta = record - bases[side];
        if (delta < 65u * 0x54u && delta % 0x54u == 0)
            return side * NFL2K5_SANDBOX_PLAYER_ID_STRIDE + (int)(delta / 0x54u);
    }
    return -1;
}
static int palette_valid(const float *palette)
{
    for (unsigned bone = 0; bone < POSE_MATRIX_COUNT; ++bone) {
        const float *m = palette + bone * POSE_MATRIX_FLOATS;
        for (unsigned i = 0; i < POSE_MATRIX_FLOATS; ++i)
            if (!isfinite(m[i]) || fabsf(m[i]) > 1000000) return 0;
        if (fabsf(m[3]) > 0.05f || fabsf(m[7]) > 0.05f ||
            fabsf(m[11]) > 0.05f || fabsf(m[15] - 1) > 0.05f) return 0;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                if (fabsf(m[row * 4u + column]) > 20) return 0;
    }
    return 1;
}
static void turn(float *x, float *y, float *z, float c, float s, float scale)
{
    float ox = *x, oz = *z;
    *x = scale * (c * ox - s * oz);
    *y *= scale;
    *z = scale * (s * ox + c * oz);
}
static void transform_palette(float *out, const float *source,
                               const Nfl2k5SandboxPose *pose)
{
    const float *chest = source + 12u * POSE_MATRIX_FLOATS;
    /* The editor uses a flat Y=0 pivot plane. Chest XZ keeps the actor
     * centred while scaling from that plane. This does not query ground
     * collision or promise exact foot placement on an elevated surface. */
    float px = chest[12], pz = chest[14];
    float angle = pose->yaw * POSE_PI / 180.0f, c = cosf(angle), s = sinf(angle);
    memcpy(out, source, POSE_PALETTE_BYTES);
    for (unsigned bone = 0; bone < POSE_MATRIX_COUNT; ++bone) {
        float *m = out + bone * POSE_MATRIX_FLOATS;
        for (unsigned row = 0; row < 3; ++row)
            turn(&m[row * 4u], &m[row * 4u + 1u], &m[row * 4u + 2u], c, s, pose->scale);
        m[12] -= px; m[14] -= pz;
        turn(&m[12], &m[13], &m[14], c, s, pose->scale);
        m[12] += px + pose->x; m[13] += pose->y; m[14] += pz + pose->z;
    }
}
/* ---- Baked animation clip picker (draftposes.bin) ----------------------
 * Read-only host cache of the baked clips. Sampling a frozen frame and
 * reconstructing the selected player's own 25-bone bind skeleton yields a
 * private preview palette; the simulation controller and the live palette are
 * never touched. Smallest route compatible with the existing begin/end draw
 * substitution (docs/SANDBOX-ANIMATION-TRACE.md). */
#define POSE_CLIP_MAX 32
#define POSE_JOINTS 25
#define POSE_FRAME_FLOATS (POSE_JOINTS * 4 + 3) /* 103: 25 quats + root xyz */
typedef struct { char name[32]; uint32_t frames; float fps; const float *samples; } PoseClip;
static struct { int loaded; int count; float *data; PoseClip clip[POSE_CLIP_MAX]; } s_clips;
static const struct { const char *name; int parent; } k_skel[POSE_JOINTS] = {
    {"root",-1},{"lfemur",0},{"ltibia",1},{"lfoot",2},{"ltoes",3},
    {"rfemur",0},{"rtibia",5},{"rfoot",6},{"rtoes",7},{"waist",0},
    {"thorax",9},{"neck",10},{"head",11},{"lcollar",10},{"lhumerus",13},
    {"lelbow",14},{"lwrist",15},{"lhand",16},{"rcollar",10},{"rhumerus",18},
    {"relbow",19},{"rwrist",20},{"rhand",21},{"lshoulderpad",10},{"rshoulderpad",10}
};
static void clips_load(void)
{
    char path[MAX_PATH];
    unsigned char *buf;
    uint32_t count, c, total = 0, frames_each[POSE_CLIP_MAX];
    float *data, *dst, fps_each[POSE_CLIP_MAX];
    long size;
    size_t off;
    FILE *f = NULL;
    PoseClip tmp[POSE_CLIP_MAX];
    DWORD n;
    int pi;
    char base[MAX_PATH];
    /* Self-contained Sandbox pack data first; the Eras draft-show asset is only
     * a fallback when that pack is also installed (coordinator decision
     * 2026-10-23: Sandbox ships standalone and must not require Eras). */
    static const char *const rels[2] = {
        "\\mods\\sandbox\\files\\sandboxposes.bin",
        "\\mods\\eras\\draft\\files\\draftposes.bin"
    };
    if (s_clips.loaded) return;
    s_clips.loaded = -1; /* tried; absent/invalid disables the picker */
    n = GetCurrentDirectoryA(sizeof base, base);
    if (!n || n >= sizeof base) return;
    for (pi = 0; pi < 2 && !f; ++pi) {
        if (snprintf(path, sizeof path, "%s%s", base, rels[pi]) >= (int)sizeof path) continue;
        f = fopen(path, "rb");
    }
    if (!f) return;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return; }
    size = ftell(f);
    if (size < 12 || size > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return; }
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf) { fclose(f); return; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return; }
    fclose(f);
    if (memcmp(buf, "DSPOSE01", 8)) { free(buf); return; }
    memcpy(&count, buf + 8, 4);
    if (count == 0 || count > POSE_CLIP_MAX) { free(buf); return; }
    memset(tmp, 0, sizeof tmp);
    off = 12;
    for (c = 0; c < count; ++c) {
        uint32_t frames; float fps; size_t bytes;
        if (off + 44 > (size_t)size) { free(buf); return; }
        memcpy(tmp[c].name, buf + off, 31); tmp[c].name[31] = 0;
        memcpy(&frames, buf + off + 32, 4); memcpy(&fps, buf + off + 36, 4);
        off += 44; /* 32 name + frames + fps + reserved */
        if (frames == 0 || frames > 4096 || !isfinite(fps) || fps < 1 || fps > 240) { free(buf); return; }
        bytes = (size_t)frames * POSE_FRAME_FLOATS * 4;
        if (off + bytes > (size_t)size) { free(buf); return; }
        frames_each[c] = frames; fps_each[c] = fps; off += bytes; total += frames;
    }
    data = (float *)malloc((size_t)total * POSE_FRAME_FLOATS * sizeof(float));
    if (!data) { free(buf); return; }
    off = 12; dst = data;
    for (c = 0; c < count; ++c) {
        uint32_t frames = frames_each[c], fr; size_t floats, i;
        off += 44; floats = (size_t)frames * POSE_FRAME_FLOATS;
        tmp[c].samples = dst; tmp[c].frames = frames; tmp[c].fps = fps_each[c];
        for (i = 0; i < floats; ++i) {
            float v; memcpy(&v, buf + off + i * 4, 4);
            if (!isfinite(v)) { free(data); free(buf); return; }
            dst[i] = v;
        }
        for (fr = 0; fr < frames; ++fr) {
            const float *q = dst + (size_t)fr * POSE_FRAME_FLOATS; int j;
            for (j = 0; j < POSE_JOINTS; ++j) {
                const float *qq = q + j * 4;
                float nsq = qq[0]*qq[0] + qq[1]*qq[1] + qq[2]*qq[2] + qq[3]*qq[3];
                if (nsq < 0.9f || nsq > 1.1f) { free(data); free(buf); return; }
            }
        }
        off += floats * 4; dst += floats;
    }
    free(buf);
    memcpy(s_clips.clip, tmp, sizeof tmp);
    s_clips.data = data; s_clips.count = (int)count; s_clips.loaded = 1;
    fprintf(stderr, "[SANDBOX-POSE] loaded %d baked animation clips from draftposes.bin\n", (int)count);
}
int nfl2k5_sandbox_pose_clip_count(void) { clips_load(); return s_clips.loaded == 1 ? s_clips.count : 0; }
const char *nfl2k5_sandbox_pose_clip_name(int i)
{
    clips_load();
    return (s_clips.loaded == 1 && i >= 0 && i < s_clips.count) ? s_clips.clip[i].name : "";
}
float nfl2k5_sandbox_pose_clip_seconds(int i)
{
    clips_load();
    if (s_clips.loaded != 1 || i < 0 || i >= s_clips.count) return 0;
    return s_clips.clip[i].fps > 0 ? (float)(s_clips.clip[i].frames - 1) / s_clips.clip[i].fps : 0;
}
static int clip_sample(int i, float time, float q[POSE_JOINTS][4], float traj[3])
{
    const PoseClip *c; const float *a, *b; float t, frac; uint32_t f0, f1; int j, k;
    clips_load();
    if (s_clips.loaded != 1 || i < 0 || i >= s_clips.count) return 0;
    c = &s_clips.clip[i];
    if (c->frames == 0 || c->fps <= 0) return 0;
    t = time * c->fps;
    if (!isfinite(t) || t < 0) t = 0;
    if (t > (float)(c->frames - 1)) t = (float)(c->frames - 1);
    f0 = (uint32_t)t; f1 = (f0 + 1 < c->frames) ? f0 + 1 : f0; frac = t - (float)f0;
    a = c->samples + (size_t)f0 * POSE_FRAME_FLOATS;
    b = c->samples + (size_t)f1 * POSE_FRAME_FLOATS;
    for (j = 0; j < POSE_JOINTS; ++j) {
        const float *qa = a + j * 4, *qb = b + j * 4;
        float dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
        float s = dot < 0 ? -1.f : 1.f, nrm;
        float w = qa[0] + (qb[0]*s - qa[0]) * frac, x = qa[1] + (qb[1]*s - qa[1]) * frac;
        float y = qa[2] + (qb[2]*s - qa[2]) * frac, z = qa[3] + (qb[3]*s - qa[3]) * frac;
        nrm = sqrtf(w*w + x*x + y*y + z*z);
        if (!(nrm > 1e-4f)) return 0;
        q[j][0] = w / nrm; q[j][1] = x / nrm; q[j][2] = y / nrm; q[j][3] = z / nrm;
    }
    for (k = 0; k < 3; ++k) { float va = a[POSE_JOINTS*4 + k], vb = b[POSE_JOINTS*4 + k]; traj[k] = va + (vb - va) * frac; }
    return 1;
}
static int read_cstr(uint32_t at, char *out, int cap)
{
    int i;
    if (cap < 1) return 0;
    out[0] = 0;
    for (i = 0; i < cap - 1; ++i) {
        unsigned char ch = 0;
        if (!read_guest(at + (uint32_t)i, &ch, 1)) return 0;
        out[i] = (char)ch;
        if (!ch) return 1;
    }
    out[cap - 1] = 0;
    return 0; /* unterminated within cap: reject */
}
static int read_bind(uint32_t shape, float local[POSE_JOINTS][3], int parent[POSE_JOINTS])
{
    uint16_t count = 0; uint32_t table = 0; int i;
    if (!read_guest(shape + 0x50u, &count, 2) || count != POSE_JOINTS ||
        !read_guest(shape + 0x64u, &table, 4) || table < 0x10000u || table > 0x07FFFFFFu) return 0;
    for (i = 0; i < POSE_JOINTS; ++i) {
        uint32_t t = table + (uint32_t)i * 0x70u, nameptr = 0; int32_t par = 0; char nm[32];
        if (!read_guest(t + 0x50u, local[i], sizeof local[i]) ||
            !read_guest(t + 0x64u, &par, 4) || !read_guest(t + 0x60u, &nameptr, 4)) return 0;
        if (!isfinite(local[i][0]) || !isfinite(local[i][1]) || !isfinite(local[i][2]) ||
            fabsf(local[i][0]) > 100000 || fabsf(local[i][1]) > 100000 || fabsf(local[i][2]) > 100000) return 0;
        if (par != k_skel[i].parent || (i == 0 ? par != -1 : (par < 0 || par >= i))) return 0;
        if (!read_cstr(nameptr, nm, sizeof nm) || strcmp(nm, k_skel[i].name)) return 0;
        parent[i] = par;
    }
    return 1;
}
static void quat_mat(const float q[4], float m[9])
{
    float w = q[0], x = q[1], y = q[2], z = q[3];
    m[0] = 1 - 2*(y*y + z*z); m[1] = 2*(x*y - w*z);     m[2] = 2*(x*z + w*y);
    m[3] = 2*(x*y + w*z);     m[4] = 1 - 2*(x*x + z*z); m[5] = 2*(y*z - w*x);
    m[6] = 2*(x*z - w*y);     m[7] = 2*(y*z + w*x);     m[8] = 1 - 2*(x*x + y*y);
}
static void mat_mul(const float a[9], const float b[9], float c[9])
{
    int r, cc, k;
    for (r = 0; r < 3; ++r) for (cc = 0; cc < 3; ++cc) {
        float s = 0; for (k = 0; k < 3; ++k) s += a[r*3 + k] * b[k*3 + cc]; c[r*3 + cc] = s;
    }
}
static void mat_vec(const float a[9], const float v[3], float o[3])
{
    int r; for (r = 0; r < 3; ++r) o[r] = a[r*3]*v[0] + a[r*3 + 1]*v[1] + a[r*3 + 2]*v[2];
}
/* Reconstruct a current-joint palette for a frozen clip frame, anchored so the
 * head joint lands on the live player's head (field position). Column-vector
 * hierarchy math from draftshow actor_write, transposed into the row-vector
 * 4x4 the renderer expects (translation at 12/13/14). Returns 0 => caller keeps
 * the original palette (no corruption). */
static int build_clip_palette(uint32_t shape, int clip0, float time,
                              const float *live, float *out)
{
    float q[POSE_JOINTS][4], traj[3], local[POSE_JOINTS][3];
    float wrot[POSE_JOINTS][9], wpos[POSE_JOINTS][3], dx, dy, dz;
    int parent[POSE_JOINTS], i;
    if (!clip_sample(clip0, time, q, traj) || !read_bind(shape, local, parent)) return 0;
    for (i = 0; i < POSE_JOINTS; ++i) {
        float rloc[9]; quat_mat(q[i], rloc);
        if (i == 0) { memcpy(wrot[0], rloc, sizeof rloc); memcpy(wpos[0], local[0], sizeof wpos[0]); }
        else {
            int p = parent[i]; float off[3];
            mat_mul(wrot[p], rloc, wrot[i]);
            mat_vec(wrot[p], local[i], off);
            wpos[i][0] = wpos[p][0] + off[0]; wpos[i][1] = wpos[p][1] + off[1]; wpos[i][2] = wpos[p][2] + off[2];
        }
    }
    dx = live[12*16 + 12] - wpos[12][0];
    dy = live[12*16 + 13] - wpos[12][1];
    dz = live[12*16 + 14] - wpos[12][2];
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz)) return 0;
    for (i = 0; i < POSE_JOINTS; ++i) {
        float *m = out + i * 16; const float *R = wrot[i];
        m[0] = R[0]; m[1] = R[3]; m[2] = R[6]; m[3] = 0;
        m[4] = R[1]; m[5] = R[4]; m[6] = R[7]; m[7] = 0;
        m[8] = R[2]; m[9] = R[5]; m[10] = R[8]; m[11] = 0;
        m[12] = wpos[i][0] + dx; m[13] = wpos[i][1] + dy; m[14] = wpos[i][2] + dz; m[15] = 1;
    }
    (void)traj; /* in-place preview: root trajectory intentionally not applied */
    return 1;
}
uint32_t nfl2k5_sandbox_pose_begin_draw(uint32_t packet, uint32_t object,
                                      uint32_t palette)
{
    uint32_t words[6], model[6], private_palette, clock = u32(0xE6028Cu);
    uint16_t counts[2];
    int id;
    Nfl2k5SandboxActor actor;
    float source[POSE_MATRIX_COUNT * POSE_MATRIX_FLOATS];
    float base[POSE_MATRIX_COUNT * POSE_MATRIX_FLOATS];
    float edited[POSE_MATRIX_COUNT * POSE_MATRIX_FLOATS];
    nfl2k5_sandbox_pose_end_draw();
    if (!current(clock) || s_clock != clock ||
        !read_guest(packet, words, sizeof words) || words[1] != palette ||
        (words[4] & 0x01000000u) != 0 || (id = record_id(words[0])) < 0 ||
        !s_edits[id].identity || !edit_current(clock, id) ||
        !nfl2k5_sandbox_actor_get(clock, words[0], &actor) ||
        actor.payload != palette || !read_guest(object, model, sizeof model) ||
        !model[2] || !read_guest(model[2] + 0x50u, counts, sizeof counts) ||
        !counts[0] || counts[0] > POSE_MATRIX_COUNT ||
        !read_guest(palette, source, sizeof source) || !palette_valid(source) ||
        !nfl2k5_sandbox_pose_bind()) return palette;
    /* A selected clip rebuilds the pose from the player's own bind skeleton;
     * any failure falls back to the live palette so nothing is corrupted. */
    if (s_edits[id].clip) {
        if (!build_clip_palette(model[2], (int)s_edits[id].clip - 1, s_edits[id].clip_time, source, base) ||
            !palette_valid(base)) return palette;
    } else {
        memcpy(base, source, sizeof base);
    }
    transform_palette(edited, base, &s_edits[id]);
    if (!palette_valid(edited) || !current(clock)) return palette;
    private_palette = s_palettes + (uint32_t)id * POSE_PALETTE_BYTES;
    if (!write_guest(private_palette, edited, sizeof edited)) return palette;
    s_draw.object = object; s_draw.shape = model[2]; s_draw.previous = model[5];
    s_draw.replacement = private_palette; s_draw.clock = clock;
    s_prepared[id]=GetTickCount();
    if(!s_reported[id]) {
        s_reported[id]=1;
        fprintf(stderr,"[SANDBOX-POSE] private palette prepared id=%d shape=%08X bones=%u; simulation and original palette unchanged\n",id,model[2],counts[0]);
    }
    return private_palette;
}

/* Independent point-transform equivalence checks. No guest memory, allocation,
 * native calls, or feature activation occurs in this selftest. */
static void point(const float *matrix, const float in[3], float out[3])
{
    for (unsigned i = 0; i < 3; ++i)
        out[i] = in[0] * matrix[i] + in[1] * matrix[4u + i] +
                 in[2] * matrix[8u + i] + matrix[12u + i];
}
int nfl2k5_sandbox_pose_selftest(void)
{
    Nfl2k5SandboxPose pose = {0, 1, 40, 25, -80, 90, 2};
    float source[POSE_MATRIX_COUNT * POSE_MATRIX_FLOATS] = {0};
    float edited[POSE_MATRIX_COUNT * POSE_MATRIX_FLOATS];
    const float p[3] = {3, 4, 5};
    if (!nfl2k5_sandbox_pose_valid(&pose)) return 0;
    pose.id = 65; if (nfl2k5_sandbox_pose_valid(&pose)) return 0;
    pose.id = 0; pose.scale = 0; if (nfl2k5_sandbox_pose_valid(&pose)) return 0;
    pose.scale = 2; pose.x = NAN; if (nfl2k5_sandbox_pose_valid(&pose)) return 0;
    pose.x = 40;
    for (unsigned i = 0; i < POSE_MATRIX_COUNT; ++i) {
        float *m = source + i * POSE_MATRIX_FLOATS;
        m[0] = m[5] = m[10] = m[15] = 1;
        m[12] = (float)i * 2; m[13] = (float)i; m[14] = 100 - (float)i;
    }
    if (!palette_valid(source)) return 0;
    transform_palette(edited, source, &pose);
    if (!palette_valid(edited)) return 0;
    for (unsigned i = 0; i < POSE_MATRIX_COUNT; ++i) {
        float before[3], after[3], expected[3];
        point(source + i * POSE_MATRIX_FLOATS, p, before);
        point(edited + i * POSE_MATRIX_FLOATS, p, after);
        expected[0] = 24 - 2 * (before[2] - 88) + 40;
        expected[1] = 2 * before[1] + 25;
        expected[2] = 88 + 2 * (before[0] - 24) - 80;
        for (unsigned k = 0; k < 3; ++k)
            if (!isfinite(after[k]) || fabsf(after[k] - expected[k]) > 0.0002f) return 0;
    }
    source[0] = NAN;
    if (palette_valid(source)) return 0;
    fprintf(stderr, "[SANDBOX-POSE] private palette math/bounds validation passed\n");
    return 1;
}

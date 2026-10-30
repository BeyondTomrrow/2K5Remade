/* Ball finder (test only): NFL2K5_BALLSCAN=1.
 *
 * Nothing in the port knows where the football is during a play -- only the
 * spot it is snapped from (drive struct +0x18). The camera that follows the
 * ball needs the live position, so this looks for it: at each snap it lists
 * every float triple in game memory that sits at the ball spot (|z - spot| <
 * 150 cm, near the middle of the field, on the ground), follows each through
 * the play, and at the whistle logs the ones that moved smoothly, far and
 * high -- a thrown or kicked ball, or one carried downfield.
 *
 * Coordinates are world centimetres, y up (as the player poses and the
 * camera eye are). Runs on the guest thread from the per-frame camera service. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"

#define FLOW   0x00E602B8u   /* on-field play when 11..18 */
#define DRIVE  0x00E602ECu   /* live drive struct: ball spot (z) at +0x18 */
#define SCAN_LO 0x00080000u  /* game image + data + heap below 64 MB */
#define SCAN_HI 0x03F00000u
#define MAX_CAND 400000

typedef struct {
    uint32_t a;
    float x0, y0, z0, px, py, pz;
    float path, ymax, maxjump;
    int dead;
} Cand;

static Cand *s_c;
static int s_n, s_live, s_frames, s_plays;

static int rd3(uint32_t a, float *x, float *y, float *z)
{
    *x = MEMF(a); *y = MEMF(a + 4u); *z = MEMF(a + 8u);
    return isfinite(*x) && isfinite(*y) && isfinite(*z);
}

static void snap(float spot)
{
    uint32_t a;
    s_n = 0;
    if (!s_c) s_c = (Cand *)calloc(MAX_CAND, sizeof *s_c);
    if (!s_c) return;
    for (a = SCAN_LO; a < SCAN_HI && s_n < MAX_CAND; a += 4u) {
        float x, y, z;
        if (!rd3(a, &x, &y, &z)) continue;
        if (fabsf(z - spot) > 150.f || fabsf(x) > 1300.f || y < 1.f || y > 250.f) continue;
        if (x == 0.f || z == spot) continue;            /* zero-filled data, copies of the spot */
        {
            Cand *c = &s_c[s_n++];
            memset(c, 0, sizeof *c);
            c->a = a; c->x0 = c->px = x; c->y0 = c->py = y; c->z0 = c->pz = z; c->ymax = y;
        }
    }
    s_frames = 0;
    fprintf(stderr, "[BALLSCAN] play %d snap: spot z=%.0f, %d candidates\n", s_plays, spot, s_n);
}

static void track(void)
{
    int i;
    for (i = 0; i < s_n; i++) {
        Cand *c = &s_c[i];
        float x, y, z, d;
        if (c->dead) continue;
        if (!rd3(c->a, &x, &y, &z)) { c->dead = 1; continue; }
        d = sqrtf((x - c->px) * (x - c->px) + (y - c->py) * (y - c->py) + (z - c->pz) * (z - c->pz));
        if (d > 400.f) { c->dead = 1; continue; }          /* not a moving object */
        c->path += d;
        if (d > c->maxjump) c->maxjump = d;
        if (y > c->ymax) c->ymax = y;
        c->px = x; c->py = y; c->pz = z;
    }
    s_frames++;
}

static int cmp(const void *a, const void *b)
{
    const Cand *p = (const Cand *)a, *q = (const Cand *)b;
    float sp = p->dead ? -1.f : p->path + p->ymax * 4.f, sq = q->dead ? -1.f : q->path + q->ymax * 4.f;
    return sp < sq ? 1 : sp > sq ? -1 : 0;
}

static void report(void)
{
    int i, shown = 0, alive = 0;
    for (i = 0; i < s_n; i++) alive += !s_c[i].dead;
    qsort(s_c, (size_t)s_n, sizeof *s_c, cmp);
    fprintf(stderr, "[BALLSCAN] play %d whistle after %d frames: %d of %d still continuous\n",
            s_plays, s_frames, alive, s_n);
    for (i = 0; i < s_n && shown < 12; i++) {
        Cand *c = &s_c[i];
        if (c->dead || c->path < 300.f) continue;
        fprintf(stderr, "[BALLSCAN]   %08X path %.0f ymax %.0f maxjump %.0f  start %.0f,%.0f,%.0f end %.0f,%.0f,%.0f\n",
                c->a, c->path, c->ymax, c->maxjump, c->x0, c->y0, c->z0, c->px, c->py, c->pz);
        shown++;
    }
}

void nfl2k5_ballscan_service(void)
{
    static int on = -1;
    uint32_t flow, ds;
    int live;
    if (on < 0) { const char *e = getenv("NFL2K5_BALLSCAN"); on = e && *e && *e != '0'; }
    if (!on) return;
    flow = MEM32(FLOW);
    {   static uint32_t lastflow = 0xFFFFFFFFu;
        if (flow != lastflow) {
            uint32_t d = MEM32(DRIVE);
            fprintf(stderr, "[BALLSCAN] flow %u -> %u t=%lu spot %.0f\n", (unsigned)lastflow, (unsigned)flow,
                    (unsigned long)GetTickCount(), d ? MEMF(d + 0x18u) : -1.f);
            lastflow = flow;
        } }
    live = flow >= 11u && flow <= 18u;
    {   /* plays follow each other inside 11..18: a new one starts at 11 */
        static uint32_t prevflow;
        if (flow == 11u && prevflow != 11u && s_live && s_n) { report(); s_n = 0; s_live = 0; }
        prevflow = flow;
    }
    if (live && !s_live) {
        ds = MEM32(DRIVE);
        if (ds) {
            float spot = MEMF(ds + 0x18u);
            /* not kickoffs (spot at midfield, z = 0): too much memory matches */
            if (isfinite(spot) && fabsf(spot) < 7000.f && fabsf(spot) > 100.f) { s_plays++; snap(spot); }
        }
    } else if (live && s_n) {
        track();
    } else if (!live && s_live && s_n) {
        report();
        s_n = 0;
    }
    s_live = live;
}

/* Sandbox is opt-in. No guest globals are patched and no thread is stopped.
 * See docs/SANDBOX-WORLD-NOTES.md for the traced native update/draw boundaries. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "xbox_memory_layout.h"
#include "recomp_funcs.h"
#include "nfl2k5_sandbox_world.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
#define SB_PI 3.14159265358979323846f
#define SB_CAMERA_BYTES 0x2B0u
static struct {
    int active, step, captured, camera_on;
    uint32_t clock, scratch, camera;
    float input[6];
    unsigned char original[SB_CAMERA_BYTES];
    unsigned char applied[SB_CAMERA_BYTES];
    Nfl2k5SandboxWorldState state;
} s_world;
static SRWLOCK s_snapshot_lock = SRWLOCK_INIT;
static Nfl2k5SandboxWorldState s_snapshot;
static uint32_t s_probe_clock;
static float s_fly_speed = 1200;

static float clampf(float f, float lo, float hi) { return f < lo ? lo : f > hi ? hi : f; }
static int address_ok(uint32_t a, size_t n)
{
    uint64_t end = (uint64_t)a + n;
    return (a >= 0x10000u && end <= 0x10000000u) ||
           (a >= 0x80000000u && end <= 0x90000000u);
}
static int read_guest(uint32_t a, void *out, size_t n)
{
    if (!address_ok(a, n)) return 0;
    __try { memcpy(out, (const void *)(uintptr_t)XBOX_PTR(a), n); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int write_guest(uint32_t a, const void *data, size_t n)
{
    if (!address_ok(a, n)) return 0;
    __try { memcpy((void *)(uintptr_t)XBOX_PTR(a), data, n); return 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uint32_t rd32(uint32_t a) { uint32_t v = 0; read_guest(a, &v, 4); return v; }
static float atf(const unsigned char *p, size_t off) { float f; memcpy(&f, p + off, 4); return f; }
/* Diagnostic evidence for mapping roster records to live render actors.
 * Nothing in this probe changes a player, matrix or animation. */
static void actor_probe(void)
{
    uint32_t roster[142],roster_n=0;
    const char *enabled=getenv("NFL2K5_SANDBOX_ACTOR_PROBE");
    if(!enabled || strcmp(enabled,"1") || !s_world.state.camera_ready ||
       !s_world.clock || s_probe_clock==s_world.clock) return;
    s_probe_clock=s_world.clock;
    for(unsigned side=0;side<2;++side) {
        uint32_t team=side?0xB30A58u:0xB30864u,count=rd32(team+0x11Cu)&0xFFu;
        if(count>71) count=0;
        for(uint32_t i=0;i<count;++i) {uint32_t p=rd32(team+4*i);if(p)roster[roster_n++]=p;}
    }
    fprintf(stderr,"[SANDBOX-ACTOR] clock=%08X roster_count=%u\n",s_world.clock,roster_n);
    for(unsigned list=0;list<2;++list) {
        uint32_t node=rd32(list?0xE60274u:0xE60268u),seen[64],n=0;
        while(node && n<64) {
            int duplicate=0;for(unsigned i=0;i<n;i++)if(seen[i]==node)duplicate=1;
            if(duplicate)break;seen[n++]=node;
            uint32_t words[20];if(!read_guest(node,words,sizeof words))break;
            fprintf(stderr,"[SANDBOX-ACTOR] list=%u node=%08X geom=%08X ctx=%08X/%08X flags=%08X\n",list,node,words[6],words[14],words[15],words[18]);
            const unsigned offsets[]={0x10u,0x14u,0x18u,0x38u,0x3Cu};
            for(unsigned k=0;k<sizeof offsets/sizeof offsets[0];k++) {
                uint32_t p=words[offsets[k]/4],fields[128];
                if(!p||!read_guest(p,fields,sizeof fields))continue;
                for(unsigned f=0;f<128;f++)for(unsigned r=0;r<roster_n;r++)if(fields[f]==roster[r])
                    fprintf(stderr,"[SANDBOX-ACTOR] roster_match node=%08X +%02X -> %08X +%03X = roster[%u] %08X\n",node,offsets[k],p,f*4,r,roster[r]);
            }
            node=words[12];
        }
    }
}
static void publish(void)
{
    AcquireSRWLockExclusive(&s_snapshot_lock);
    s_snapshot = s_world.state;
    ReleaseSRWLockExclusive(&s_snapshot_lock);
}
static int match_current(void)
{
    return s_world.active && s_world.clock && rd32(0x00A83A18u) == 3u &&
           rd32(0x00E6028Cu) == s_world.clock && rd32(0x00E602C4u) >= 1u &&
           rd32(0x00E602C4u) <= 8u;
}

int nfl2k5_sandbox_world_bind(void)
{
    /* Camera routines get their own bounded guest stack after the pose data;
     * never borrow a stale register-local caller's simulated stack. */
    if (!s_world.scratch) s_world.scratch = xbox_HeapAlloc(0x2000u, 16u);
    return s_world.scratch != 0;
}
int nfl2k5_sandbox_world_enter(void)
{
    uint32_t clock = rd32(0x00E6028Cu), period = rd32(0x00E602C4u);
    float seconds;
    if (rd32(0x00A83A18u) != 3u || !clock || period < 1 || period > 8 ||
        !read_guest(clock + 0x10u, &seconds, 4) || !isfinite(seconds) ||
        seconds < 0 || seconds > 7200 || !nfl2k5_sandbox_world_bind()) return 0;
    if (s_world.active && s_world.clock == clock) return 1;
    nfl2k5_sandbox_world_leave();
    s_world.active = 1; s_world.clock = clock;
    s_world.camera_on = 1;
    memset(&s_world.state, 0, sizeof s_world.state);
    s_world.state.ready = 1; s_world.state.timescale = 0;
    s_world.state.fov = 60;
    fprintf(stderr, "[SANDBOX] world entered clock=%08X (simulation paused)\n", clock);
    publish();
    return 1;
}
void nfl2k5_sandbox_world_leave(void)
{
    /* Only restore a still-owned live object. During teardown, leave released
     * memory alone; the normal camera pipeline will initialise the next one. */
    if (s_world.captured && match_current() && s_world.camera)
        write_guest(s_world.camera, s_world.original, sizeof s_world.original);
    s_world.active = s_world.step = s_world.captured = s_world.camera_on = 0;
    s_world.clock = s_world.camera = 0;
    memset(s_world.input, 0, sizeof s_world.input);
    memset(&s_world.state, 0, sizeof s_world.state);
    s_world.state.timescale = 1;
    publish();
}
void nfl2k5_sandbox_world_input(float mx, float my, float lx, float ly, float up, float zoom)
{
    float in[6] = { mx, my, lx, ly, up, zoom }; int i;
    for (i = 0; i < 6; ++i) s_world.input[i] = isfinite(in[i]) ? clampf(in[i], -1, 1) : 0;
}
void nfl2k5_sandbox_world_tick(float dt)
{
    float yaw, cp, sp, cy, sy, *v = s_world.input;
    if (!s_world.active) return;
    if (!match_current()) { nfl2k5_sandbox_world_leave(); return; }
    if (!isfinite(dt) || dt <= 0) return;
    dt = clampf(dt, 0, 0.1f);
    if (s_world.state.camera_ready) {
        actor_probe();
        s_world.state.yaw = fmodf(s_world.state.yaw + v[2] * dt * 100.0f, 360.0f);
        s_world.state.pitch = clampf(s_world.state.pitch + v[3] * dt * 80.0f, -85, 85);
        s_world.state.fov = clampf(s_world.state.fov - v[5] * dt * 45.0f, 15, 110);
        yaw = s_world.state.yaw * SB_PI / 180.0f;
        cp = cosf(s_world.state.pitch * SB_PI / 180.0f);
        sp = sinf(s_world.state.pitch * SB_PI / 180.0f); cy = cosf(yaw); sy = sinf(yaw);
        s_world.state.x = clampf(s_world.state.x + (-v[0] * cy + v[1] * sy * cp) * dt * s_fly_speed, -100000, 100000);
        s_world.state.y = clampf(s_world.state.y + (v[4] + v[1] * sp) * dt * s_fly_speed, -10000, 50000);
        s_world.state.z = clampf(s_world.state.z + (v[0] * sy + v[1] * cy * cp) * dt * s_fly_speed, -100000, 100000);
    }
    publish();
}
void nfl2k5_sandbox_world_set_fly_speed(float speed)
{
    if (isfinite(speed)) s_fly_speed = clampf(speed, 100, 20000);
}
void nfl2k5_sandbox_world_set_timescale(float scale)
{
    if (!s_world.active || !isfinite(scale)) return;
    s_world.state.timescale = clampf(scale, 0, 1); s_world.step = 0; publish();
}
void nfl2k5_sandbox_world_set_camera(int enabled)
{
    if (!s_world.active || s_world.camera_on == !!enabled) return;
    if (!enabled) {
        if (s_world.captured && match_current() && s_world.camera)
            write_guest(s_world.camera, s_world.original, sizeof s_world.original);
        s_world.captured = 0; s_world.camera = 0;
        s_world.state.camera_ready = 0;
    }
    s_world.camera_on = !!enabled;
    publish();
}
void nfl2k5_sandbox_world_step(void)
{
    if (!s_world.active) return;
    s_world.state.timescale = 0; s_world.step = 1; publish();
}
float nfl2k5_sandbox_world_frame_delta(float original)
{
    if (!s_world.active || !match_current() || !isfinite(original) || original < 0) return original;
    if (s_world.step && original > 0) { s_world.step = 0; return 1.0f / 60.0f; }
    return original * s_world.state.timescale;
}

/* A camera override must not perturb registers, flags, SSE or x87 values the
 * recompiled caller still owns. The called camera routines keep guest stack
 * conventions; the saved ESP restores the caller even on a caught fault. */
static int camera_apply(uint32_t camera)
{
    uint32_t regs[10] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi, g_seh_ebp, (uint32_t)g_df };
    uint32_t rf[6] = { g_rf_kind, g_rf_a, g_rf_b, (uint32_t)g_rf_as, (uint32_t)g_rf_bs, g_rf_cf };
    double fp[8]; int top = g_fp_top, cmp = g_fp_cmp, ok = 0;
    uint16_t cc = g_fp_cc, cw = g_fp_control_word;
    RecompXmm xm[8] = { g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7 };
    float yaw = s_world.state.yaw * SB_PI / 180.0f, pitch = s_world.state.pitch * SB_PI / 180.0f;
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    float pose[16] = { s_world.state.x,s_world.state.y,s_world.state.z,1,
                      sy*cp,sp,cy*cp,0, -sy*sp,cp,-cy*sp,0, -cy,0,sy,0 };
    uint32_t p = s_world.scratch;
    memcpy(fp, g_fp_stack, sizeof fp);
    if (!write_guest(p, pose, sizeof pose)) return 0;
    __try {
        /* 2BA10(ECX=object, EDX=eye, stack=forward/up/right). It builds
         * the native view and all dependent transforms. */
        g_esp = p + 0x1F00u;
        g_ecx = camera; g_edx = p;
        PUSH32(g_esp, p + 48u); PUSH32(g_esp, p + 32u); PUSH32(g_esp, p + 16u);
        PUSH32(g_esp, 0); sub_0002BA10();
        MEMF(camera + 0x270u) = 1.0f / tanf(s_world.state.fov * SB_PI / 360.0f);
        g_ecx = camera; PUSH32(g_esp, 0); sub_0002B510();
        ok = 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    g_eax=regs[0]; g_ecx=regs[1]; g_edx=regs[2]; g_ebx=regs[3]; g_esp=regs[4];
    g_ebp=regs[5]; g_esi=regs[6]; g_edi=regs[7]; g_seh_ebp=regs[8]; g_df=(int)regs[9];
    g_rf_kind=rf[0];g_rf_a=rf[1];g_rf_b=rf[2];g_rf_as=(int32_t)rf[3];g_rf_bs=(int32_t)rf[4];g_rf_cf=rf[5];
    memcpy(g_fp_stack,fp,sizeof fp); g_fp_top=top;g_fp_cmp=cmp;g_fp_cc=cc;g_fp_control_word=cw;
    g_xmm0=xm[0];g_xmm1=xm[1];g_xmm2=xm[2];g_xmm3=xm[3];
    g_xmm4=xm[4];g_xmm5=xm[5];g_xmm6=xm[6];g_xmm7=xm[7];
    return ok;
}
void nfl2k5_sandbox_world_camera(uint32_t camera)
{
    unsigned char original[SB_CAMERA_BYTES]; float f[4], n; int i;
    if (!s_world.camera_on || !match_current() || !camera || !read_guest(camera, original, sizeof original)) return;
    /* Own only the first actual viewport. A second split-screen camera must
     * not steal restoration ownership or get overwritten as a side effect. */
    if (s_world.camera && s_world.camera != camera) return;
    f[0]=-atf(original,0x48);f[1]=-atf(original,0x58);f[2]=-atf(original,0x68);
    n=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    f[3]=atf(original,0x270);
    if (!isfinite(n) || n<0.5f || n>2.0f || !isfinite(f[3]) || f[3]<0.05f || f[3]>100) return;
    for(i=0;i<3;++i) if(!isfinite(atf(original,0x80u+(unsigned)i*4u)) || fabsf(atf(original,0x80u+(unsigned)i*4u))>100000) return;
    if (!s_world.state.camera_ready) {
        s_world.state.x=atf(original,0x80);s_world.state.y=atf(original,0x84);s_world.state.z=atf(original,0x88);
        s_world.state.yaw=atan2f(f[0],f[2])*180.0f/SB_PI;
        s_world.state.pitch=asinf(clampf(f[1]/n,-1,1))*180.0f/SB_PI;
        s_world.state.fov=clampf(2*atanf(1/f[3])*180.0f/SB_PI,15,110);
        s_world.state.camera_ready=1;
        fprintf(stderr,"[SANDBOX] captured live viewport camera=%08X eye=%.1f,%.1f,%.1f\n",camera,s_world.state.x,s_world.state.y,s_world.state.z);
    }
    /* Frozen frames may offer our previous output again. Preserve the last
     * actual native camera, rather than replacing the restore point with it. */
    if (!s_world.captured || memcmp(original,s_world.applied,sizeof original))
        memcpy(s_world.original,original,sizeof original);
    s_world.camera=camera;s_world.captured=1;
    if (!camera_apply(camera)) {
        write_guest(camera,original,sizeof original);
        s_world.state.camera_ready=0;
        fprintf(stderr,"[SANDBOX] camera override failed; native camera restored\n");
    }
    else read_guest(camera,s_world.applied,sizeof s_world.applied);
    publish();
}
void nfl2k5_sandbox_world_get_state(Nfl2k5SandboxWorldState *out)
{
    if (!out) return; AcquireSRWLockShared(&s_snapshot_lock); *out=s_snapshot; ReleaseSRWLockShared(&s_snapshot_lock);
}
int nfl2k5_sandbox_world_restore(const Nfl2k5SandboxWorldState *v)
{
    if (!v || !match_current() || !isfinite(v->timescale) || !isfinite(v->x) || !isfinite(v->y) ||
        !isfinite(v->z) || !isfinite(v->yaw) || !isfinite(v->pitch) || !isfinite(v->fov) ||
        v->timescale<0 || v->timescale>1 || fabsf(v->x)>100000 || fabsf(v->z)>100000 ||
        v->y < -10000 || v->y>50000 || fabsf(v->yaw)>36000 || fabsf(v->pitch)>85 || v->fov<15 || v->fov>110) return 0;
    s_world.state=*v;s_world.state.ready=1;s_world.state.camera_ready=1;s_world.camera_on=1;s_world.step=0;publish();return 1;
}
void nfl2k5_sandbox_world_status(char *out,size_t n)
{
    Nfl2k5SandboxWorldState v; if(!out||!n)return;nfl2k5_sandbox_world_get_state(&v);
    snprintf(out,n,"%s",!v.ready?"Load a game in Situation Mode":!v.camera_ready?"Waiting for the live field camera":"Free camera ready");
}
int nfl2k5_sandbox_world_selftest(void)
{
    /* Check camera conventions/FOV independently of guest memory. This is
     * deliberately not a claim that the native hooks ran in a real match. */
    static const float angles[][2]={{0,0},{90,0},{-150,35},{179,-85}};
    unsigned i; float fov;
    for(i=0;i<sizeof angles/sizeof angles[0];++i) {
        float y=angles[i][0]*SB_PI/180,p=angles[i][1]*SB_PI/180;
        float f[3]={sinf(y)*cosf(p),sinf(p),cosf(y)*cosf(p)};
        float r[3]={-cosf(y),0,sinf(y)};
        float u[3]={-sinf(y)*sinf(p),cosf(p),-cosf(y)*sinf(p)};
        float dot=f[0]*r[0]+f[1]*r[1]+f[2]*r[2];
        float dot2=f[0]*u[0]+f[1]*u[1]+f[2]*u[2];
        float norm=f[0]*f[0]+f[1]*f[1]+f[2]*f[2];
        float determinant = r[0]*(u[1]*-f[2]-u[2]*-f[1]) -
                            r[1]*(u[0]*-f[2]-u[2]*-f[0]) +
                            r[2]*(u[0]*-f[1]-u[1]*-f[0]);
        if(fabsf(dot)>1e-5f||fabsf(dot2)>1e-5f||fabsf(norm-1)>1e-5f)return 0;
        if(fabsf(determinant-1)>1e-5f)return 0;
        if(fabsf(asinf(f[1])*180/SB_PI-angles[i][1])>0.001f)return 0;
    }
    for(fov=15;fov<=110;fov+=5) {
        float focal=1/tanf(fov*SB_PI/360);
        if(!isfinite(focal)||fabsf(2*atanf(1/focal)*180/SB_PI-fov)>0.001f)return 0;
    }
    return 1;
}

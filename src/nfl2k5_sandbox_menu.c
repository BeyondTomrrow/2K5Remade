/* Sandbox owns a COPY of Situation Mode's setup, not another implementation
 * of its game-state rules. Original Situation rows/events stay untouched.
 * Only the new Game Modes link and the cloned event lists reference us. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "recomp_funcs.h"
#include "xbox_memory_layout.h"
#include "nfl2k5_sandbox_menu.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern void nfl2k5_sandbox_begin(void);
extern void nfl2k5_sandbox_end(void);

#define SB_FN_BASE 0xFEF00000u
#define SB_ENTER (SB_FN_BASE + 0x00u)
#define SB_START (SB_FN_BASE + 0x04u)
#define SB_SHOWN (SB_FN_BASE + 0x08u)
#define SB_ROW 0x34u
#define SB_ACTION 0x24u
#define SB_GAME_MODES 0x005015CCu
#define SB_MAIN_MENU 0x00515660u
#define SB_SITUATION 0x00501298u
#define SB_SETTINGS 0x00E5FF80u
#define SB_SETTINGS_BYTES 0x2E0u

static uint32_t s_heap, s_used, s_header;
static RECOMP_TLS uint32_t s_called;
static int s_open, s_started, s_exited, s_reselect;
static int s_launch_accepted;
static int s_quit_pending, s_reopen_pending, s_servicing;
static unsigned char s_settings[SB_SETTINGS_BYTES];
static uint32_t s_selection[7], s_sides[8];

/* Every callback preserves the full published guest context. The original
 * menu action interpreter still owns the call's one guest return word. */
typedef struct {
    uint32_t r[9], rf[6];
    double fp[8];
    int fp_top, fp_cmp, df;
    uint16_t fp_cw, fp_cc;
    RecompXmm xmm[8];
} SbRegs;

static void regs_save(SbRegs *s)
{
    const uint32_t r[] = {g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_seh_ebp};
    const uint32_t rf[] = {g_rf_kind,g_rf_a,g_rf_b,g_rf_cf,(uint32_t)g_rf_as,(uint32_t)g_rf_bs};
    memcpy(s->r,r,sizeof r); memcpy(s->rf,rf,sizeof rf);
    memcpy(s->fp,g_fp_stack,sizeof s->fp);
    s->fp_top=g_fp_top; s->fp_cmp=g_fp_cmp; s->df=g_df;
    s->fp_cw=g_fp_control_word; s->fp_cc=g_fp_cc;
    s->xmm[0]=g_xmm0; s->xmm[1]=g_xmm1; s->xmm[2]=g_xmm2; s->xmm[3]=g_xmm3;
    s->xmm[4]=g_xmm4; s->xmm[5]=g_xmm5; s->xmm[6]=g_xmm6; s->xmm[7]=g_xmm7;
}

static void regs_restore(const SbRegs *s, uint32_t return_bytes)
{
    g_eax=s->r[0]; g_ecx=s->r[1]; g_edx=s->r[2]; g_ebx=s->r[3];
    g_esp=s->r[4]+return_bytes; g_ebp=s->r[5]; g_esi=s->r[6]; g_edi=s->r[7]; g_seh_ebp=s->r[8];
    g_rf_kind=s->rf[0]; g_rf_a=s->rf[1]; g_rf_b=s->rf[2]; g_rf_cf=s->rf[3];
    g_rf_as=(int32_t)s->rf[4]; g_rf_bs=(int32_t)s->rf[5];
    memcpy(g_fp_stack,s->fp,sizeof s->fp);
    g_fp_top=s->fp_top; g_fp_cmp=s->fp_cmp; g_df=s->df;
    g_fp_control_word=s->fp_cw; g_fp_cc=s->fp_cc;
    g_xmm0=s->xmm[0]; g_xmm1=s->xmm[1]; g_xmm2=s->xmm[2]; g_xmm3=s->xmm[3];
    g_xmm4=s->xmm[4]; g_xmm5=s->xmm[5]; g_xmm6=s->xmm[6]; g_xmm7=s->xmm[7];
}

static void copy_guest(uint32_t dst, uint32_t src, uint32_t bytes)
{
    for (uint32_t i=0;i<bytes;i+=4u) MEM32(dst+i)=MEM32(src+i);
}

static uint32_t alloc_guest(uint32_t bytes)
{
    uint32_t p;
    bytes=(bytes+3u)&~3u;
    if (s_used+bytes>32768u) return 0;
    p=s_heap+s_used; s_used+=bytes;
    for(uint32_t i=0;i<bytes;i+=4u) MEM32(p+i)=0;
    return p;
}

static uint32_t guest_text(const char *text)
{
    uint32_t p=alloc_guest(((uint32_t)strlen(text)+1u)*2u);
    if(p) for(uint32_t i=0;text[i];++i) MEM16(p+2u*i)=(uint16_t)(unsigned char)text[i];
    return p;
}

static int enabled(void)
{
    const char *e=getenv("NFL2K5_SANDBOX");
    int setting=-1;
    FILE *f=NULL;
    char line[256];
    if(e && *e) return strcmp(e,"0")!=0;
    if(!fopen_s(&f,"mods\\features.ini","r") && f) {
        while(fgets(line,sizeof line,f)) {
            char *p=line,*end;
            while(isspace((unsigned char)*p)) ++p;
            if(_strnicmp(p,"sandbox",7)!=0) continue;
            p+=7; while(isspace((unsigned char)*p)) ++p;
            if(*p++!='=') continue;
            setting=(int)strtol(p,&end,10);
            if(end!=p) setting=setting!=0;
        }
        fclose(f);
    }
    if(setting>=0) return setting;
    return GetFileAttributesA("mods\\sandbox\\pack.json")!=INVALID_FILE_ATTRIBUTES;
}

static void snapshot(void)
{
    for(uint32_t i=0;i<SB_SETTINGS_BYTES;++i) s_settings[i]=MEM8(SB_SETTINGS+i);
    for(uint32_t i=0;i<7;++i) s_selection[i]=MEM32(0xE5FE64u+4u*i);
    for(uint32_t i=0;i<8;++i) s_sides[i]=MEM32(0xE5FE88u+0x1Cu*i);
}

static void finish(const char *reason)
{
    if(!s_open) return;
    nfl2k5_sandbox_end();
    for(uint32_t i=0;i<SB_SETTINGS_BYTES;++i) MEM8(SB_SETTINGS+i)=s_settings[i];
    for(uint32_t i=0;i<7;++i) MEM32(0xE5FE64u+4u*i)=s_selection[i];
    for(uint32_t i=0;i<8;++i) MEM32(0xE5FE88u+0x1Cu*i)=s_sides[i];
    s_open=s_started=s_exited=s_reselect=s_quit_pending=s_reopen_pending=0;
    fprintf(stderr,"[SANDBOX] setup closed (%s); original match settings restored\n",reason);
}

static void push_setup(uint32_t fsm)
{
    g_ecx=fsm; g_edx=s_header;
    PUSH32(g_esp,SB_FN_BASE);
    sub_0006E390();
}

static void callback(void)
{
    SbRegs regs;
    uint32_t action=s_called,fsm=g_ecx;
    regs_save(&regs);
    if(action==SB_ENTER) {
        if(!s_open) { snapshot(); s_open=1; }
        s_started=s_exited=s_reselect=s_quit_pending=s_reopen_pending=s_launch_accepted=0;
        push_setup(fsm);
        fprintf(stderr,"[SANDBOX] opened independent Situation setup %08X\n",s_header);
    } else if(action==SB_START && s_open) {
        s_started=1; s_exited=0; s_launch_accepted=0;
        nfl2k5_sandbox_begin();
        fprintf(stderr,"[SANDBOX] armed by Situation's actual Start event\n");
    } else if(action==SB_SHOWN && s_started && !s_launch_accepted) {
        /* Backing out of Team Select lands here before any match starts. */
        nfl2k5_sandbox_end(); s_started=0;
    }
    regs_restore(&regs,4u);
}

void (*nfl2k5_sandbox_menu_lookup(uint32_t address))(void)
{
    if(!s_header || (address!=SB_ENTER && address!=SB_START && address!=SB_SHOWN)) return NULL;
    s_called=address;
    return callback;
}

int nfl2k5_sandbox_menu_installed(void) { return s_header!=0; }
uint32_t nfl2k5_sandbox_menu_header(void) { return s_header; }
int nfl2k5_sandbox_menu_open(void) { return s_open; }

void nfl2k5_sandbox_menu_launch_accepted(void)
{
    /* Native Team Select has accepted Start and is about to pop to Main
     * Menu before pushing the gameplay descriptor. Its shown callbacks
     * during that pop must not be mistaken for backing out of setup. */
    if(s_open && s_started && MEM32(0x00ACF614u)==8u) {
        s_launch_accepted=1;
        fprintf(stderr,"[SANDBOX] native Team Select launch accepted\n");
    }
}

void nfl2k5_sandbox_menu_match_exit(void)
{
    if(!s_open) return;
    nfl2k5_sandbox_end(); s_started=0; s_exited=1;
    /* Do not restore selections while native match teardown still owns them. */
}

int nfl2k5_sandbox_menu_request_reselect(void)
{
    if(!s_open || !s_started) return 0;
    s_reselect=1;
    fprintf(stderr,"[SANDBOX] team/stadium reselection queued until native match exit\n");
    return 1;
}

int nfl2k5_sandbox_menu_quit_for_reselect(void)
{
    uint32_t count=MEM32(0xA84C18u);
    int loaded=0;
    if(!s_open || !s_started || MEM32(0xE5FF80u)!=8u || MEM32(0xA83A18u)!=3u || count>=32u || !MEM32(0xE6028Cu)) return 0;
    for(uint32_t i=0;i<=count;++i)
        if(MEM32(0xA84B18u+i*8u)==0x004E7EC0u) loaded=1;
    if(!loaded) return 0;
    /* Input polling may be nested inside menus that retain their own object
     * pointers. Execute the native action only after that poll returns. */
    s_reselect=1; s_quit_pending=1;
    return 1;
}

void nfl2k5_sandbox_menu_fsm_pop(uint32_t fsm,uint32_t top)
{
    (void)fsm;
    if(!s_open || (top!=SB_GAME_MODES && top!=SB_MAIN_MENU)) return;
    /* Situation's successful Team Select Start intentionally pops to Main
     * Menu before pushing gameplay (sub_2C0E70 flow 8). That is a launch,
     * not cancellation. Back first returns to our setup's shown callback,
     * which clears s_started; a genuine match exit marks s_exited. */
    if(s_started && !s_exited && s_launch_accepted) return;
    if(s_reselect && s_exited) {
        /* This hook runs INSIDE native pop-to-descriptor loops. Pushing a
         * screen here would make the outer loop pop it straight back off. */
        s_reopen_pending=1;
    } else finish("front-end return");
}

/* Call once at a top-level guest frame boundary shared by gameplay and the
 * frontend, never inside XInputGetState or the nested FSM-pop hook. */
void nfl2k5_sandbox_menu_service(void)
{
    SbRegs regs;
    uint32_t count,top;
    if(s_servicing || !s_open || (!s_quit_pending && !s_reopen_pending)) return;
    s_servicing=1;
    regs_save(&regs);
    if(s_quit_pending) {
        s_quit_pending=0;
        if(s_started && MEM32(0xA83A18u)==3u && MEM32(0xE5FF80u)==8u) {
            int accepted;
            g_ecx=0x00A84B18u; g_edx=0;
            PUSH32(g_esp,SB_FN_BASE);
            /* Same action as Pause -> Quit, including native confirmations
             * and profile guards. Never call match teardown directly. */
            sub_0006EBE0();
            accepted=s_exited || MEM32(0xA83A18u)==0u;
            if(!accepted) s_reselect=0;
            fprintf(stderr,"[SANDBOX] native Quit for team/stadium reload: %s\n",accepted?"accepted":"canceled");
        } else s_reselect=0;
    }
    /* Defer a new screen until the prior Quit/pop operation has unwound and
     * the original front end actually owns the top of the FSM stack. */
    count=MEM32(0xA84C18u);
    top=count<32u?MEM32(0xA84B18u+count*8u):0;
    if(s_open && s_reopen_pending && (top==SB_GAME_MODES || top==SB_MAIN_MENU)) {
        s_reopen_pending=s_reselect=s_exited=s_started=0;
        push_setup(0x00A84B18u);
    }
    regs_restore(&regs,0);
    s_servicing=0;
}

/* Copy every native event and action, inserting callbacks without replacing
 * the mode-specific setup, rendering, Start routing, or controller handlers. */
static uint32_t clone_events(uint32_t original,int *saw_start)
{
    uint32_t table=alloc_guest(33u*8u);
    if(!table || !original) return 0;
    for(uint32_t i=0;i<32;++i) {
        uint32_t msg=MEM32(original+8u*i),list=MEM32(original+8u*i+4u);
        if(!msg) return table;
        if(!list) return 0;
        uint32_t n=0;
        while(n<32u && MEM32(list+SB_ACTION*n)!=0u) ++n;
        if(n==32u) return 0;
        uint32_t added=(msg==11u || msg==3u)?1u:0u;
        uint32_t dst=alloc_guest(SB_ACTION*(n+added+1u));
        if(!dst) return 0;
        if(added) {
            MEM32(dst)=1u;
            MEM32(dst+4u)=msg==11u?SB_START:SB_SHOWN;
        }
        copy_guest(dst+added*SB_ACTION,list,SB_ACTION*(n+1u));
        MEM32(table+i*8u)=msg; MEM32(table+i*8u+4u)=dst;
        if(msg==11u) {
            /* An unknown package must not arm on a different event. */
            if(n!=1u || MEM32(list)!=1u || MEM32(list+4u)!=0x00147370u) return 0;
            *saw_start=1;
        }
    }
    return 0;
}

void nfl2k5_sandbox_menu_install(void)
{
    uint32_t old_rows,count=0,setup_rows,setup_count=0,rows,new_rows,label,title,events;
    int saw_start=0;
    if(s_header || !enabled()) return;
    /* Require the known generic settings schema. Mod-specific rewrites fail
     * closed instead of poking a guessed descriptor layout. */
    if(MEM32(SB_GAME_MODES+8u)!=0x000F3E90u || MEM32(SB_SITUATION+8u)!=0x000F3FC0u) {
        fprintf(stderr,"[SANDBOX] menu schema differs; leaving existing menus unchanged\n"); return;
    }
    old_rows=MEM32(SB_GAME_MODES+0x10u); setup_rows=MEM32(SB_SITUATION+0x10u);
    if(!old_rows || !setup_rows) return;
    while(count<30u && MEM32(old_rows+SB_ROW*count)!=3u) ++count;
    while(setup_count<24u && MEM32(setup_rows+SB_ROW*setup_count)!=3u) ++setup_count;
    if(count>=30u || !setup_count || setup_count>=24u) return;
    s_heap=xbox_HeapAlloc(32768u,16u); s_used=0;
    if(!s_heap) return;
    label=guest_text("Sandbox"); title=guest_text("Sandbox Setup");
    rows=alloc_guest(SB_ROW*(setup_count+1u));
    new_rows=alloc_guest(SB_ROW*(count+2u));
    events=clone_events(MEM32(SB_SITUATION+4u),&saw_start);
    uint32_t header=alloc_guest(SB_ROW);
    if(!label || !title || !rows || !new_rows || !events || !header || !saw_start) {
        fprintf(stderr,"[SANDBOX] independent setup construction failed; existing menus unchanged\n"); return;
    }
    copy_guest(rows,setup_rows,SB_ROW*(setup_count+1u));
    copy_guest(header,SB_SITUATION,SB_ROW);
    MEM32(header)=title; MEM32(header+4u)=events; MEM32(header+0x10u)=rows;
    copy_guest(new_rows,old_rows,SB_ROW*count);
    uint32_t row=new_rows+SB_ROW*count;
    MEM32(row)=9u; MEM32(row+4u)=label; MEM32(row+0x28u)=SB_ENTER;
    MEM32(row+SB_ROW)=3u;
    /* Commit only when the whole private graph has been constructed. */
    s_header=header;
    MEM32(SB_GAME_MODES+0x10u)=new_rows;
    MEM32(SB_GAME_MODES+0x28u)|=4u; /* native link-list scrolling for added rows */
    fprintf(stderr,"[SANDBOX] Game Modes entry installed; private setup %08X, %u rows\n",s_header,setup_count);
}

/* Runs against the actual loaded menu graph; no callbacks or game transition
 * occur. Suitable for --validate after optional menu installation. */
int nfl2k5_sandbox_menu_validate(void)
{
    uint32_t original_rows,rows,original_events,events,n=0;
    int starts=0,shown=0;
    if(!s_header) return 1; /* disabled feature is not a validation failure */
    /* Validate the public dispatcher, not only this module's private graph.
     * Otherwise an installed row can still be mistaken for guest machine
     * code when it is actually selected. */
    if(recomp_lookup_manual(SB_ENTER)!=callback ||
       recomp_lookup_manual(SB_START)!=callback ||
       recomp_lookup_manual(SB_SHOWN)!=callback) {
        fprintf(stderr,"[TEST] Sandbox menu callbacks are not routed to native code\n");
        return 0;
    }
    original_rows=MEM32(SB_SITUATION+0x10u); rows=MEM32(s_header+0x10u);
    original_events=MEM32(SB_SITUATION+4u); events=MEM32(s_header+4u);
    if(rows==original_rows || events==original_events || !rows || !events) return 0;
    while(n<24u) {
        for(uint32_t j=0;j<SB_ROW;j+=4u)
            if(MEM32(rows+n*SB_ROW+j)!=MEM32(original_rows+n*SB_ROW+j)) return 0;
        if(MEM32(rows+n*SB_ROW)==3u) break;
        ++n;
    }
    if(n>=24u) return 0;
    for(uint32_t i=0;i<32u;++i) {
        uint32_t msg=MEM32(original_events+i*8u),cloned=MEM32(events+i*8u);
        if(msg!=cloned) return 0;
        if(!msg) {
            if(!starts || !shown) return 0;
            fprintf(stderr,"[TEST] Sandbox private Situation rows/events and Start ordering passed\n");
            return 1;
        }
        uint32_t a=MEM32(original_events+i*8u+4u),b=MEM32(events+i*8u+4u),added=0;
        if(msg==11u || msg==3u) {
            if(MEM32(b)!=1u || MEM32(b+4u)!=(msg==11u?SB_START:SB_SHOWN)) return 0;
            if(msg==11u) ++starts; else ++shown;
            added=1u;
        }
        uint32_t k=0;
        for(;k<32u;++k) {
            for(uint32_t j=0;j<SB_ACTION;j+=4u)
                if(MEM32(a+k*SB_ACTION+j)!=MEM32(b+(k+added)*SB_ACTION+j)) return 0;
            if(MEM32(a+k*SB_ACTION)==0u) break;
        }
        if(k==32u) return 0;
    }
    return 0;
}

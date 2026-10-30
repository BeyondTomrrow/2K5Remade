"""Compile actual native camera-mark ownership code in an isolated MSVC harness.

Only guest RAM, read_state, package choices and host time are external inputs.
Owner, replay-lifetime predicate and state/event declarations come directly
from current production source. No game/build/Release/regeneration is used.
"""
from __future__ import annotations
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/nfl2k5_presentation.cpp'
spec = importlib.util.spec_from_file_location('period_owner_verifier', Path(__file__).with_name('verify-period-owner.py'))
period = importlib.util.module_from_spec(spec)
spec.loader.exec_module(period)

FIXTURE = r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static std::map<uint32_t,uint32_t> words;
static std::map<uint32_t,std::string> texts;
static unsigned guest_reads=0,state_reads=0;
static bool gread(uint32_t va,void *out,size_t n){++guest_reads;if(n!=4)return false;auto it=words.find(va);if(it==words.end())return false;memcpy(out,&it->second,4);return true;}
static std::string gtext(uint32_t va,int max){++guest_reads;auto it=texts.find(va);return it==texts.end()?std::string():it->second.substr(0,(size_t)max);}
static uint64_t fake_now=1000;
static uint64_t GetTickCount64(){return fake_now;}
struct PackageFixture{bool html=false,camera_marks_ack=false;};
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
'''

CASES = r'''
static constexpr uint32_t base=0x00B61B50u,resource=0x01820000u,scene=0x01830000u;
static HtmlContext cx{false,false,false,false,true};
static uint32_t ctx_for(int slot){return base+(uint32_t)slot*0xF20u;}
static void instance(int slot){
    uint32_t b=ctx_for(slot);words[b]=slot;words[b+4]=1;words[b+0xC]=0;words[b+0x48]=19;
    words[b+0xF08]=0;words[b+0xF1C]=1;words[b+0x1D8]=5;words[b+0x1DC]=resource;words[resource]=scene;
    words[b+0xF14]=0;words[b+0x40]=0;words[b+0x228]=0;words[b+0xEF4]=0;
    words[b+0x22C]=48;words[b+0x230]=7733313; // inactive subtype0 storage is not a table
    texts[b+0x4C]="";texts[b+0xCC]="";
}
static void setup(int slot=1){
    s_camera_mark_owner={};s_popup_events.clear();words.clear();texts.clear();
    s_sel_pkg=1;s_pregame_host_generation=7;s_camera_mark_host_package=1;s_camera_mark_page_permitted=1;
    s_pkgs[1]={true,true};s_pkgs[2]={true,true};fake_game={};fake_game.valid=true;
    fake_game.period=1;fake_game.t[0].abbr="DAL";fake_game.t[1].abbr="NE";
    words[0x00E6028Cu]=0x123400u;words[k_team_obj[0]+0x1Cu]=0x1812340;words[k_team_obj[1]+0x1Cu]=0x1813450;
    words[0x00BE5214u]=0x00AAD180u;words[0x00AAD984u]=0x00E86400u;words[0x00AAD988u]=0x00E861A8u;
    texts[0x00E86400u]="skycam";texts[0x00E861A8u]="OVERLAY";
    words[0x00AFA078u]=3;words[0x00A84C18u]=1;words[0x00A84B20u]=0x004E3F38u;
    cx={false,false,false,false,true};read_success=true;fake_now+=1000;instance(slot);
}
static std::string ack(const char *verb="SHOW",const std::string &token=""){
    return std::string("NFL2K5_CAMERA_MARK_")+verb+":"+(token.empty()?std::to_string(s_camera_mark_owner.token):token);
}
static void page(const std::string &message,int pk=1,uint32_t gen=7){
    unsigned before=guest_reads,sbefore=state_reads;camera_mark_page_message(pk,gen,message);
    CHECK(guest_reads==before);CHECK(state_reads==sbefore);
}
static PendingPopupEvent deliver(){
    size_t before=s_popup_events.size();camera_mark_hud_context(fake_game,cx);
    CHECK(s_camera_mark_owner.active&&s_camera_mark_owner.queued&&!s_camera_mark_owner.delivered);
    CHECK(s_popup_events.size()==before+1);auto ev=s_popup_events.back();
    CHECK(ev.name=="CAMERA_MARK_SHOW");CHECK(ev.camera_token==s_camera_mark_owner.token);
    CHECK(ev.camera_generation==7&&ev.camera_package==1);
    CHECK(ev.extra.find("\"nativeCameraToken\":\""+std::to_string(ev.camera_token)+"\"")!=std::string::npos);
    CHECK(ev.extra.find("\"source\":\"native\"")!=std::string::npos);
    CHECK(ev.extra.find("\"nativeVisible\":true")!=std::string::npos);
    CHECK(camera_mark_event_current(ev,fake_game));CHECK(s_camera_mark_owner.delivered&&!s_camera_mark_owner.queued);
    size_t n=s_popup_events.size();camera_mark_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n);
    return ev;
}
static void bind_and_ack(int slot=1){
    camera_mark_frame(ctx_for(slot));CHECK(s_camera_mark_owner.active&&!s_camera_mark_owner.acknowledged);
    CHECK(camera_mark_draw_replaced(ctx_for(slot))==0);deliver();page(ack());
    CHECK(s_camera_mark_owner.acknowledged);CHECK(camera_mark_draw_replaced(ctx_for(slot))==1);
}
static void test_actual_slots(){
    for(int slot:{0,1}){
        setup(slot);camera_mark_frame(ctx_for(slot));CHECK(s_camera_mark_owner.active&&s_camera_mark_owner.slot==slot);
        CHECK(s_popup_events.empty());page(ack());CHECK(!s_camera_mark_owner.acknowledged); // undelivered cannot acquire
        auto ev=deliver();CHECK(camera_mark_draw_replaced(ctx_for(slot))==0);page(ack());CHECK(camera_mark_draw_replaced(ctx_for(slot))==1);
        CHECK(camera_mark_draw_replaced(ctx_for(1-slot))==0);CHECK(camera_mark_draw_replaced(0x00B73C44u)==0);
        camera_mark_slot_closing(1-slot);CHECK(!s_camera_mark_owner.closing);
        camera_mark_slot_closing(slot);words[ctx_for(slot)+0xC]=1;CHECK(s_camera_mark_owner.closing);
        CHECK(s_popup_events.back().name=="CAMERA_MARK_HIDE");CHECK(camera_mark_event_current(s_popup_events.back(),fake_game));
        page(ack("HIDE"));CHECK(s_camera_mark_owner.acknowledged);CHECK(camera_mark_draw_replaced(ctx_for(slot))==1);
        size_t n=s_popup_events.size();camera_mark_slot_closing(slot);CHECK(s_popup_events.size()==n);
        words[ctx_for(slot)+4]=0;CHECK(camera_mark_draw_replaced(ctx_for(slot))==0);camera_mark_hud_context(fake_game,cx);
        CHECK(!s_camera_mark_owner.active);page(ack("SHOW",std::to_string(ev.camera_token)));CHECK(!s_camera_mark_owner.active);
    }
    setup();bind_and_ack();page(ack("HIDE"));CHECK(!s_camera_mark_owner.acknowledged);CHECK(camera_mark_draw_replaced(ctx_for(1))==0);
    page(ack());CHECK(camera_mark_draw_replaced(ctx_for(1))==1); // visible repaint can reacquire
    setup();camera_mark_frame(ctx_for(1));deliver();camera_mark_slot_closing(1);page(ack());CHECK(!s_camera_mark_owner.acknowledged);
}
static void mutate(int n){
    uint32_t b=ctx_for(1);
    switch(n){
    case 0:words[b]=0;break;case 1:words[b+4]=0;break;case 2:words[b+0xC]=1;break;
    case 3:words[b+0x48]=14;break;case 4:words[b+0xF08]=1;break;case 5:words[b+0xF1C]=0;break;
    case 6:words[b+0x1D8]=4;break;case 7:words[b+0x1D8]=6;break;case 8:words[b+0x1DC]=0;break;
    case 9:words[resource]=0;break;case 10:words[b+0x40]=1;break;case 11:words[b+0x228]=1;break;
    case 12:words[b+0xEF4]=1;break;case 13:texts[b+0x4C]="Something meaningful";break;
    case 14:texts[b+0xCC]="Meaningful message";break;case 15:words[0x00E6028Cu]=0;break;
    case 16:words[k_team_obj[0]+0x1Cu]=0;break;case 17:words[k_team_obj[1]+0x1Cu]=0;break;
    case 18:words[0x00BE5214u]=0;break;case 19:words[0x00AAD984u]=0x00E8635Cu;break;
    case 20:words[0x00AAD988u]=0x00E86370u;break;case 21:texts[0x00E86400u]="other";break;
    case 22:texts[0x00E861A8u]="OTHER";break;case 23:fake_game.valid=false;break;
    case 24:words[0x00AFA078u]=0;break;case 25:words[0x00AFA078u]=6;break;
    case 26:words[0x00A84C18u]=0;break;case 27:words[0x00A84C18u]=32;break;
    case 28:words[0x00A84B20u]=0;break;case 29:s_sel_pkg=2;break;case 30:s_camera_mark_host_package=2;break;
    case 31:s_pkgs[1].html=false;break;case 32:s_pkgs[1].camera_marks_ack=false;break;
    }
}
static void test_guarded_fallback(){
    for(int n=0;n<33;++n){setup();mutate(n);camera_mark_frame(ctx_for(1));CHECK(!s_camera_mark_owner.active);CHECK(s_popup_events.empty());CHECK(!camera_mark_draw_replaced(ctx_for(1)));}
    for(uint32_t b:{0u,0x00B73C44u,0x00B61B54u,ctx_for(2)}){setup();camera_mark_frame(b);CHECK(!s_camera_mark_owner.active);CHECK(s_popup_events.empty());}
    setup();read_success=false;camera_mark_frame(ctx_for(1));CHECK(!s_camera_mark_owner.active);
    // First ready draw still belongs to native art; a later actually latched draw can bind.
    setup();words[ctx_for(1)+0xF1C]=0;camera_mark_frame(ctx_for(1));CHECK(!s_camera_mark_owner.active);
    words[ctx_for(1)+0xF1C]=1;bind_and_ack();
    // Non-table bytes and native mutable timing never become invented identity.
    words[ctx_for(1)+8]=0;words[ctx_for(1)+0xF00]=0x3F000000;words[ctx_for(1)+0x22C]=0xFFFFFFFF;words[ctx_for(1)+0x230]=99;
    CHECK(camera_mark_draw_replaced(ctx_for(1))==1);
}
static void test_invalidation(){
    for(int n=0;n<33;++n){setup();bind_and_ack();mutate(n);CHECK(!camera_mark_draw_replaced(ctx_for(1)));camera_mark_hud_context(fake_game,cx);CHECK(!s_camera_mark_owner.active);}
    for(int n=0;n<7;++n){setup();bind_and_ack();uint32_t b=ctx_for(1);
        switch(n){case 0:words[b+0xF14]=0x00117300;break;case 1:words[b+0x1DC]=resource+4;words[resource+4]=scene;break;
        case 2:words[resource]=scene+4;break;case 3:words[0x00E6028Cu]++;break;case 4:words[k_team_obj[0]+0x1Cu]++;break;
        case 5:words[k_team_obj[1]+0x1Cu]++;break;case 6:s_pregame_host_generation=8;break;}
        CHECK(!camera_mark_draw_replaced(b));camera_mark_hud_context(fake_game,cx);CHECK(!s_camera_mark_owner.active);
    }
    for(int n=0;n<2;++n){setup();bind_and_ack();if(n)cx.play_selection=true;else cx.paused=true;camera_mark_hud_context(fake_game,cx);CHECK(!s_camera_mark_owner.active);CHECK(!s_camera_mark_page_permitted);}
    setup();bind_and_ack();read_success=false;CHECK(!camera_mark_draw_replaced(ctx_for(1)));
    setup();bind_and_ack();camera_mark_replacement_reset();CHECK(!s_camera_mark_owner.active);CHECK(s_camera_mark_host_package==-1&&!s_camera_mark_page_permitted);
}
static void test_protocol_and_reuse(){
    setup();camera_mark_frame(ctx_for(1));auto ev=deliver();
    for(std::string t:{"0","01","+1","-1","4294967296","1.0","1e2"," 1","1 ","","999999999999999999999"}){page("NFL2K5_CAMERA_MARK_SHOW:"+t);CHECK(!s_camera_mark_owner.acknowledged);}
    page(ack(),2);CHECK(!s_camera_mark_owner.acknowledged);page(ack(),1,8);CHECK(!s_camera_mark_owner.acknowledged);
    s_camera_mark_page_permitted=0;page(ack());CHECK(!s_camera_mark_owner.acknowledged);s_camera_mark_page_permitted=1;
    page(ack());CHECK(s_camera_mark_owner.acknowledged);auto token=s_camera_mark_owner.token;
    camera_mark_frame(ctx_for(1));CHECK(s_camera_mark_owner.token==token);CHECK(s_camera_mark_owner.acknowledged);
    // Actual resource identity change is a different instance even in the same slot.
    words[resource]=scene+4;camera_mark_frame(ctx_for(1));CHECK(s_camera_mark_owner.active&&s_camera_mark_owner.token!=token);
    CHECK(!s_camera_mark_owner.acknowledged);CHECK(!camera_mark_event_current(ev,fake_game));
    page(ack("SHOW",std::to_string(token)));CHECK(!s_camera_mark_owner.acknowledged);deliver();page(ack());CHECK(s_camera_mark_owner.acknowledged);
    page(ack("HIDE",std::to_string(token)));CHECK(s_camera_mark_owner.acknowledged);
    setup();camera_mark_frame(ctx_for(1));camera_mark_hud_context(fake_game,cx);ev=s_popup_events.back();camera_mark_slot_closing(1);
    CHECK(!camera_mark_event_current(ev,fake_game));page(ack());CHECK(!s_camera_mark_owner.acknowledged);
    setup();camera_mark_frame(ctx_for(1));camera_mark_hud_context(fake_game,cx);ev=s_popup_events.back();s_pregame_host_generation=8;
    CHECK(!camera_mark_event_current(ev,fake_game));ev.name="CAMERA_MARK_HIDE";CHECK(!camera_mark_event_current(ev,fake_game));
    setup();s_camera_mark_token=UINT32_MAX;camera_mark_frame(ctx_for(1));CHECK(s_camera_mark_owner.token==1);deliver();page(ack());CHECK(s_camera_mark_owner.acknowledged);
    setup();bind_and_ack();s_popup_events.clear();camera_mark_replacement_reset();CHECK(s_popup_events.empty());
}
int main(){test_actual_slots();test_guarded_fallback();test_invalidation();test_protocol_and_reuse();printf("PASS %u actual native camera-mark owner assertions\n",checks);}
'''

def main():
    source=SOURCE.read_text(encoding='utf-8')
    start=source.index('struct NativeCameraMarkOwner {')
    # Adjacent owners can live in an included implementation. Keep this
    # harness's actual camera owner intact without compiling unrelated code.
    reset=period.function(source,'camera_mark_replacement_reset','static void ')
    owner=source[start:source.index(reset,start)+len(reset)]
    states=source[source.index('struct TeamState {'):source.index('static const uint32_t k_playbook_name')]
    pending=source.index('static std::mutex s_popup_lock;')
    declarations=source[pending:source.index('enum { PREGAME_COIN',pending)]
    context=re.search(r'struct HtmlContext \{[^}]+\};',source).group()
    deps=states+context+'\n'+declarations+'\n'+'\n'.join([
        period.function(source,'rd32','static uint32_t '),period.function(source,'json_str','static std::string '),
        period.function(source,'native_replay_active','static bool '),
    ])+'\nstatic GameState fake_game;\nstatic bool read_success=true;\nstatic bool read_state(GameState &g){++state_reads;g=fake_game;return read_success;}\n'
    glue={name:period.function(source,name,prefix) for name,prefix in [
        ('nfl2k5_presentation_player_fact_hide','extern "C" void '),
        ('nfl2k5_presentation_native_popup_frame','extern "C" void '),
        ('nfl2k5_presentation_pregame_draw_replaced','extern "C" int '),
        ('pregame_replacement_reset','static void '),
    ]}
    assert glue['nfl2k5_presentation_player_fact_hide'].index('camera_mark_slot_closing(slot);')<glue['nfl2k5_presentation_player_fact_hide'].index('if (slot != 1')
    assert 'camera_mark_frame(ctx);' in glue['nfl2k5_presentation_native_popup_frame']
    assert 'if (camera_mark_draw_replaced(ctx)) return 1;' in glue['nfl2k5_presentation_pregame_draw_replaced']
    assert 'camera_mark_replacement_reset();' in glue['pregame_replacement_reset']
    for required in ['p.camera_marks_ack = p.root.flag("camera_marks_ack", false);','if (!camera_mark_event_current(ev, g)) continue;',
                     'if (pkg.camera_marks_ack) s_camera_mark_host_package = pk;','camera_mark_page_message(pk, generation, message);','camera_mark_hud_context(g, cx);']:
        assert required in source,required
    output=ROOT/'outputs/presentation-audit/camera-owner-test';output.mkdir(parents=True,exist_ok=True)
    cpp,exe=output/'camera-owner.cpp',output/'camera-owner.exe';cpp.write_text(FIXTURE+deps+owner+CASES,encoding='utf-8')
    compiler,environment=period.msvc_environment()
    command=[str(compiler),'/nologo','/EHsc','/std:c++17','/Fe:'+str(exe),'/Fo:'+str(output/'camera-owner.obj'),str(cpp)]
    built=subprocess.run(command,env=environment,cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'compile.log').write_text(built.stdout,encoding='utf-8')
    if built.returncode:print(built.stdout);raise SystemExit(built.returncode)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/'test.stdout.log').write_text(result.stdout,encoding='utf-8');(output/'test.stderr.log').write_text(result.stderr,encoding='utf-8')
    fingerprint=hashlib.sha256((owner+deps+repr(glue)).encode()).hexdigest();(output/'source.sha256').write_text(fingerprint+'\n',encoding='utf-8')
    print(result.stdout,end='')
    if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
    print('Actual extracted ownership source SHA-256: '+fingerprint)
    print('Source checks: actual-slot frame, draw-only owner query, both-slot close, page/HUD/reset/event/capability glue.')

if __name__=='__main__':main()

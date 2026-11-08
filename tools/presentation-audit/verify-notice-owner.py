"""Compile and verify the actual included native injury/penalty owner.

Only memory, package state, clock and host-facing dependencies are fixtures.
The ownership algorithm is read verbatim from native_notice.inc.cpp. No game
launch, guest mutation, generated-code edit or build/Release artifact is used.
"""
from __future__ import annotations
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'src/presentation/native_notice.inc.cpp'
SOURCE = ROOT / 'src/nfl2k5_presentation.cpp'
spec = importlib.util.spec_from_file_location('period_verifier', Path(__file__).with_name('verify-period-owner.py'))
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
static unsigned checks=0,guest_reads=0,state_reads=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static std::map<uint32_t,uint32_t> words;
static std::map<uint32_t,std::string> texts;
static bool gread(uint32_t va,void *out,size_t n){
    ++guest_reads;
    if(n==2){auto t=texts.find(va);if(t!=texts.end()){uint16_t c=t->second.empty()?0:(unsigned char)t->second[0];memcpy(out,&c,2);return true;}}
    auto w=words.find(va);if(w==words.end()||(n!=2&&n!=4))return false;
    memcpy(out,&w->second,n);return true;
}
static std::string gtext(uint32_t va,int max){
    ++guest_reads;auto t=texts.find(va);if(t==texts.end()||t->second.size()>=(size_t)max)return "";
    for(unsigned char c:t->second)if(c<32||c>126)return "";return t->second;
}
static uint64_t GetTickCount64(){return 123456u;}
struct PackageFixture{bool html=false,notice_cards_ack=false;};
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
static std::string native_confirm_prompt(){return "A";}
'''

CASES = r'''
static constexpr uint32_t base=0x00B61B50u;
static HtmlContext cx{};
static uint32_t context(int slot=0){return base+(uint32_t)slot*0xF20u;}
static uint32_t row(int slot,int i){return context(slot)+0x234u+(uint32_t)i*0x228u;}
static void cell(int slot,int i,int j,const std::string &text,uint32_t style=1){
    texts[row(slot,i)+4u+(uint32_t)j*0x88u]=text;
    words[row(slot,i)+0x84u+(uint32_t)j*0x88u]=style;
}
static void instance(int slot,const std::string &kind){
    uint32_t b=context(slot);words[b]=slot;words[b+4]=1;words[b+0xC]=0;
    words[b+0x40]=0;words[b+0x48]=14;words[b+0x228]=2;words[b+0x22C]=0;
    words[b+0xF08]=0;words[b+0xF1C]=1;words[b+0x1D8]=5;
    words[b+0x1DC]=0x200000u+(uint32_t)slot*0x100u;words[words[b+0x1DC]]=0x300000u;
    words[b+0x684]=kind=="choice"?2:0;words[b+0x688]=0;
    words[b+0xF14]=kind=="injury"?0x117770u:kind=="choice"?0:0x1180C0u;
    texts[b+0x4C]=kind=="injury"?"Injured Player":kind=="offset"?"Double Penalty":"Defensive Penalty";
    texts[b+0xCC]=kind=="injury"?"Cowboys QB #17":kind=="offset"?"Penalties are offset. Replay down.":"Against Dallas #20, Pass Interference";
    int rows=kind=="injury"?1:2;words[b+0x230]=rows;
    for(int i=0;i<rows;++i){
        int columns=kind=="choice"?3:kind=="offset"?2:1;
        words[row(slot,i)]=kind=="choice"?(i?0x117C30u:0x117C10u):0;
        words[row(slot,i)+0x224]=columns;
        cell(slot,i,0,kind=="injury"?"Quincy Carter":kind=="choice"?(i?"Decline":"Accept"):kind=="offset"?(i?"Dallas":"Pittsburgh"):(i?"1st and 10 at DAL 20":"Pittsburgh accepts."),kind=="offset"?2:1);
        if(columns>1)cell(slot,i,1,kind=="offset"?(i?"Offside":"Holding"):(i?"4th and 2":"1st and 10"));
        if(columns>2)cell(slot,i,2,i?"DAL 33":"PIT 45");
    }
}
static void setup(int slot=0,const std::string &kind="injury"){
    s_notice_owner={};s_popup_events.clear();words.clear();texts.clear();
    s_sel_pkg=1;s_pregame_host_generation=7;s_notice_host_package=1;s_notice_page_permitted=1;
    s_pkgs[1]={true,true};s_pkgs[2]={true,true};
    fake_game={};fake_game.valid=true;fake_game.phase=4;fake_game.period=2;
    fake_game.t[0].abbr="PIT";fake_game.t[1].abbr="DAL";cx={};read_success=true;
    words[0xE6028C]=0x123400u;words[k_team_obj[0]+0x1C]=0x400000u;words[k_team_obj[1]+0x1C]=0x410000u;
    instance(slot,kind);
}
static std::string ack(const char *verb="SHOW",const std::string &token=""){
    return std::string("NFL2K5_NOTICE_")+verb+":"+(token.empty()?std::to_string(s_notice_owner.token):token);
}
static void page(const std::string &message,int package=1,uint32_t gen=7){
    unsigned before=guest_reads,states=state_reads;native_notice_page_message(package,gen,message);
    CHECK(guest_reads==before);CHECK(state_reads==states);
}
static PendingPopupEvent deliver(){
    size_t n=s_popup_events.size();native_notice_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n+1);
    auto ev=s_popup_events.back();CHECK(ev.name=="BROADCAST_NOTICE"&&ev.notice_token==s_notice_owner.token);
    CHECK(native_notice_event_current(ev,fake_game));CHECK(s_notice_owner.delivered&&!s_notice_owner.queued);
    return ev;
}
static void bind_ack(int slot=0){
    native_notice_frame(context(slot));CHECK(s_notice_owner.active&&!s_notice_owner.acknowledged);
    CHECK(native_notice_draw_replaced(context(slot))==0);deliver();page(ack());CHECK(native_notice_draw_replaced(context(slot))==1);
}
static void lifecycle(){
    for(int slot:{0,1})for(const std::string kind:{"injury","result","offset","choice"}){
        setup(slot,kind);native_notice_frame(context(slot));CHECK(s_notice_owner.active&&s_notice_owner.slot==slot);
        uint32_t token=s_notice_owner.token;CHECK(s_popup_events.empty());page(ack());CHECK(!s_notice_owner.acknowledged);
        auto ev=deliver();CHECK(ev.extra.find("\"replacementToken\":"+std::to_string(token))!=std::string::npos);
        CHECK(ev.extra.find("\"phase\":4")!=std::string::npos);
        CHECK(ev.extra.find(kind=="choice"?"\"interactive\":true":"\"interactive\":false")!=std::string::npos);
        if(kind=="choice"){
            CHECK(ev.extra.find("\"cells\":[\"1st and 10\",\"PIT 45\"]")!=std::string::npos);
            CHECK(ev.extra.find("\"cells\":[\"4th and 2\",\"DAL 33\"]")!=std::string::npos);
            CHECK(ev.extra.find("\"button\":\"A\"")!=std::string::npos);
        }else CHECK(ev.extra.find("\"button\"")==std::string::npos);
        if(kind=="offset")CHECK(ev.extra.find("\"text\":\"Pittsburgh\",\"selected\":false,\"cells\":[\"Holding\"]")!=std::string::npos);
        page(ack());CHECK(native_notice_draw_replaced(context(slot))==1);CHECK(native_notice_draw_replaced(context(1-slot))==0);
        size_t n=s_popup_events.size();native_notice_frame(context(slot));native_notice_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n);
        native_notice_slot_closing(1-slot);CHECK(!s_notice_owner.closing);
        words[context(slot)+0xC]=1;
        if(kind=="choice")words[context(slot)+0xF14]=0x117C10u;
        native_notice_slot_closing(slot);CHECK(s_notice_owner.closing);CHECK(s_popup_events.back().name=="BROADCAST_NOTICE_HIDE");
        CHECK(native_notice_event_current(s_popup_events.back(),fake_game));page(ack("HIDE"));CHECK(s_notice_owner.acknowledged);
        CHECK(native_notice_draw_replaced(context(slot))==1);n=s_popup_events.size();native_notice_slot_closing(slot);CHECK(s_popup_events.size()==n);
        words[context(slot)+4]=0;CHECK(native_notice_draw_replaced(context(slot))==0);
        native_notice_hud_context(fake_game,cx);CHECK(!s_notice_owner.active);page(ack("SHOW",std::to_string(token)));CHECK(!s_notice_owner.active);
    }
}
static void malformed_instances(){
    for(int m=0;m<36;++m){
        setup();uint32_t b=base;
        switch(m){
            case 0:words[b]=1;break;case 1:words[b+4]=0;break;case 2:words[b+0xC]=1;break;
            case 3:words[b+0x48]=7;break;case 4:words[b+0x48]=19;break;case 5:words[b+0x228]=1;break;
            case 6:words[b+0x230]=0;break;case 7:words[b+0x230]=3;break;case 8:words[row(0,0)+0x224]=0;break;
            case 9:words[row(0,0)+0x224]=4;break;case 10:words[b+0x22C]=1;break;case 11:words[b+0x688]=1;break;
            case 12:words[b+0x684]=3;break;case 13:words[b+0xF14]=0x1172F0;break;case 14:words[b+0xF14]=0;break;
            case 15:words[row(0,0)]=1;break;case 16:texts[b+0x4C]="";break;case 17:texts[b+0xCC]="";break;
            case 18:texts[b+0x238]="";break;case 19:texts[b+0x238]=std::string(64,'A');break;case 20:texts[b+0x238]="bad\nrow";break;
            case 21:words[b+0xF08]=1;break;case 22:words[b+0xF1C]=0;break;case 23:words[b+0x1D8]=4;break;
            case 24:words[b+0x1DC]=0;break;case 25:words[words[b+0x1DC]]=0;break;case 26:words[b+0x40]=1;break;
            case 27:words[0xE6028C]=0;break;case 28:words[k_team_obj[0]+0x1C]=0;break;case 29:words[k_team_obj[1]+0x1C]=0;break;
            case 30:s_pkgs[1].notice_cards_ack=false;break;case 31:s_pkgs[1].html=false;break;case 32:s_notice_host_package=2;break;
            case 33:s_notice_page_permitted=0;break;case 34:fake_game.valid=false;break;case 35:read_success=false;break;
        }
        native_notice_frame(b);CHECK(!s_notice_owner.active&&s_popup_events.empty());CHECK(!native_notice_draw_replaced(b));
    }
    for(int m=0;m<9;++m){
        setup(0,"choice");
        switch(m){
            case 0:words[row(0,0)]=0x117C30;break;case 1:words[row(0,1)]=0x117C10;break;
            case 2:words[row(0,0)+0x224]=2;break;case 3:words[row(0,1)+0x224]=2;break;
            case 4:words[base+0xF14]=0x117C10;break;case 5:words[base+0xF14]=0x117C30;break;
            case 6:words[base+0x2B8]=0;break;case 7:words[base+0x2B8]=4;break;case 8:texts[base+0x2C0]="bad\nextra";break;
        }
        native_notice_frame(base);CHECK(!s_notice_owner.active);
    }
    setup(0,"choice");texts[base+0x2C0]="";native_notice_frame(base);CHECK(s_notice_owner.active);
    auto ev=deliver();CHECK(ev.extra.find("\"cells\":[\"\",\"PIT 45\"]")!=std::string::npos); // Preserve valid empty extra cells.
}
static void selected_row_and_callbacks(){
    setup(0,"choice");bind_ack();uint32_t token=s_notice_owner.token;
    words[base+0x22C]=1;native_notice_frame(base);CHECK(s_notice_owner.token==token&&s_notice_owner.selected==1);
    auto update=s_popup_events.back();CHECK(update.extra.find("\"text\":\"Decline\",\"selected\":true")!=std::string::npos);
    CHECK(native_notice_event_current(update,fake_game));CHECK(native_notice_draw_replaced(base)==1);
    page(ack("HIDE"));CHECK(native_notice_draw_replaced(base)==0);page(ack());CHECK(native_notice_draw_replaced(base)==1);
    words[base+0xF14]=0x117C30;words[base+0xC]=1;native_notice_slot_closing(0);CHECK(native_notice_draw_replaced(base)==1);
    words[base+0xF14]=0x117C10;CHECK(native_notice_draw_replaced(base)==0); // Wrong selected callback cannot inherit ownership.
    setup(0,"choice");native_notice_frame(base);native_notice_hud_context(fake_game,cx);auto stale=s_popup_events.back();
    words[base+0x22C]=1;native_notice_frame(base);CHECK(!native_notice_event_current(stale,fake_game));CHECK(s_notice_owner.queued);
    auto fresh=s_popup_events.back();CHECK(native_notice_event_current(fresh,fake_game));
    native_notice_slot_closing(0);page(ack());CHECK(!s_notice_owner.acknowledged); // No late acquisition during closing.
}
static void stale_and_reuse(){
    for(int m=0;m<17;++m){
        setup();bind_ack();
        switch(m){
            case 0:words[base+4]=0;break;case 1:words[base+0x48]=19;break;case 2:texts[base+0x4C]="Different";break;
            case 3:texts[base+0xCC]="Different";break;case 4:texts[base+0x238]="Different";break;
            case 5:words[base+0xF14]=0x1180C0;break;case 6:words[0xE6028C]++;break;
            case 7:words[k_team_obj[0]+0x1C]++;break;case 8:words[k_team_obj[1]+0x1C]++;break;
            case 9:words[base+0x1DC]++;break;case 10:words[words[base+0x1DC]]++;break;
            case 11:s_pregame_host_generation++;break;case 12:s_notice_host_package=2;break;
            case 13:fake_game.phase=2;break;case 14:fake_game.valid=false;break;
            case 15:s_pkgs[1].notice_cards_ack=false;break;case 16:s_pkgs[1].html=false;break;
        }
        CHECK(!native_notice_draw_replaced(base));native_notice_hud_context(fake_game,cx);CHECK(!s_notice_owner.active);
    }
    setup();bind_ack();auto oldtoken=s_notice_owner.token;words[base+0xC]=1;native_notice_slot_closing(0);
    words[base+0xC]=0;CHECK(!native_notice_draw_replaced(base));native_notice_frame(base);
    CHECK(s_notice_owner.active&&s_notice_owner.token!=oldtoken&&!s_notice_owner.acknowledged);
    page(ack("SHOW",std::to_string(oldtoken)));CHECK(!s_notice_owner.acknowledged);
    setup();bind_ack();cx.paused=true;native_notice_hud_context(fake_game,cx);CHECK(!s_notice_owner.active);
    setup();bind_ack();cx.play_selection=true;native_notice_hud_context(fake_game,cx);CHECK(!s_notice_owner.active);
    setup();bind_ack();native_notice_reset();CHECK(!s_notice_owner.active&&s_notice_host_package==-1&&!s_notice_page_permitted);
    setup();bind_ack();instance(1,"result");native_notice_frame(context(1));CHECK(s_notice_owner.ctx==base); // Stable first owner; no frame-by-frame slot stealing.
}
static void message_protocol(){
    setup();native_notice_frame(base);deliver();std::string t=std::to_string(s_notice_owner.token);
    for(const std::string value:std::vector<std::string>{"","0","00","01","-1","+1","1x","1 ","4294967296","99999999999","0"+t}){
        page(ack("SHOW",value));if(value.empty()){page(ack("HIDE"));continue;}CHECK(!s_notice_owner.acknowledged);
    }
    page(ack(),2);CHECK(!s_notice_owner.acknowledged);page(ack(),1,8);CHECK(!s_notice_owner.acknowledged);
    page(ack("SHOW",std::to_string(s_notice_owner.token+1)));CHECK(!s_notice_owner.acknowledged);
    s_notice_page_permitted=0;page(ack());CHECK(!s_notice_owner.acknowledged);s_notice_page_permitted=1;
    page(ack());CHECK(s_notice_owner.acknowledged);page(ack("HIDE"));CHECK(!s_notice_owner.acknowledged);
    PendingPopupEvent unrelated;CHECK(native_notice_event_current(unrelated,fake_game));
}
int main(){lifecycle();malformed_instances();selected_row_and_callbacks();stale_and_reuse();message_protocol();
    printf("PASS %u actual native injury/penalty notice owner assertions\n",checks);}
'''

def main():
    source = SOURCE.read_text(encoding='utf-8')
    module = MODULE.read_text(encoding='utf-8')
    states = source[source.index('struct TeamState {'):source.index('static const uint32_t k_playbook_name')]
    context = re.search(r'struct HtmlContext \{[^}]+\};', source).group()
    # Queue declaration is a host fixture; the tested owner itself is verbatim production source.
    queue = '''
struct PendingPopupEvent {std::string name,extra;uint32_t notice_token=0,notice_generation=0;int notice_package=-1;};
static std::mutex s_popup_lock;static std::vector<PendingPopupEvent> s_popup_events;
'''
    dependencies = '\n'.join([states, context, queue,
        period.function(source, 'rd32', 'static uint32_t '),
        period.function(source, 'json_str', 'static std::string '),
        'static GameState fake_game;static bool read_success=true;',
        'static bool read_state(GameState &g){++state_reads;g=fake_game;return read_success;}'])
    assert not re.search(r'\b(gwrite\w*|RECOMP_ABI_CALL|RECOMP_ICALL_SAFE|MEM32)\b', module)
    page = period.function(module, 'native_notice_page_message', 'static void ')
    assert not re.search(r'\b(gread|rd32|gtext|read_state|native_confirm_prompt)\s*\(', page)
    generated = (ROOT / 'src/recomp/gen/recomp_0008.c').read_text(encoding='utf-8')
    for fn,pc in [('sub_00117780','001178F9'),('sub_001180D0','001182DF'),('sub_00118310','001185CE'),('sub_001185F0','0011871E')]:
        body=period.function(generated,fn,'void ')
        assert f'PUSH32(esp, 0x{pc}u); RECOMP_ABI_CALL(0x0008ACF0u' in body
    renderer=(ROOT/'src/recomp/gen/recomp_0013.c').read_text(encoding='utf-8')
    update=period.function(renderer,'sub_001BE290','void ')
    assert 'MEM32(edx + esi + 0x234)' in update
    assert 'RECOMP_ABI_CALL(0x0008AC30u' in update and 'RECOMP_ABI_CALL(0x0008A340u' in update
    output=ROOT/'outputs/presentation-audit/notice-owner-test';output.mkdir(parents=True,exist_ok=True)
    cpp,exe=output/'notice-owner.cpp',output/'notice-owner.exe'
    cpp.write_text(FIXTURE+dependencies+module+CASES,encoding='utf-8')
    compiler,environment=period.msvc_environment()
    built=subprocess.run([str(compiler),'/nologo','/EHsc','/std:c++17','/Fe:'+str(exe),'/Fo:'+str(output/'notice-owner.obj'),str(cpp)],
        env=environment,cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'compile.log').write_text(built.stdout,encoding='utf-8')
    if built.returncode:print(built.stdout);raise SystemExit(built.returncode)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/'test.stdout.log').write_text(result.stdout,encoding='utf-8')
    (output/'test.stderr.log').write_text(result.stderr,encoding='utf-8')
    digest=hashlib.sha256(module.encode()).hexdigest();(output/'source.sha256').write_text(digest+'\n',encoding='utf-8')
    print(result.stdout,end='')
    if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
    print('Actual included owner SHA-256: '+digest)
    print('Verified native constructor/callback seams; browser callbacks perform zero guest reads. No game launch/build.')

if __name__=='__main__': main()

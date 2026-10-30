"""Verify actual native challenge/review owner, without building/running the game.

Only guest RAM, read_state, package choices and host time are fixtures. Owner,
state/event declarations and helpers are extracted from current production C++.
Standalone MSVC artifacts stay under outputs/presentation-audit/review-owner-test.
"""
from __future__ import annotations
import ast
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/nfl2k5_presentation.cpp'
spec=importlib.util.spec_from_file_location('period_owner_verifier',Path(__file__).with_name('verify-period-owner.py'))
period=importlib.util.module_from_spec(spec)
spec.loader.exec_module(period)

FIXTURE = r'''

#include <algorithm>
#include <atomic>
#include <cctype>
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
static bool gread(uint32_t va,void *out,size_t n) {
    ++guest_reads;
    if(n!=4) return false;
    auto it=words.find(va); if(it==words.end()) return false;
    memcpy(out,&it->second,4);return true;
}
static std::string gtext(uint32_t va,int max) {
    ++guest_reads;auto it=texts.find(va);
    return it==texts.end()?std::string():it->second.substr(0,(size_t)max);
}
static uint64_t fake_now=1000;
static uint64_t GetTickCount64(){return fake_now;}
static void xbox_PresentRecord(int seconds){CHECK(seconds==8);}
struct PackageFixture{bool html=false,review_cards_ack=false;};
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
'''
CASES = r'''
static constexpr uint32_t request=0x00BA4E78u,base=0x00B61B50u;
static HtmlContext cx{false,false,false,false};
static uint32_t ctx_for(int slot){return base+(uint32_t)slot*0xF20u;}
static void instance(int slot,bool ruling=false,const std::string &row="Spot Of Ball"){
    uint32_t b=ctx_for(slot),cb=ruling?0x00117300u:0x001172F0u;
    words[b]=slot;words[b+4]=1;words[b+0xC]=0;words[b+0x48]=14;
    words[b+0x228]=2;words[b+0x230]=1;words[b+0x234]=0;words[b+0x2B8]=1;words[b+0x458]=1;
    words[b+0xF14]=cb;words[b+0xF1C]=1;
    texts[b+0x4C]="Challenge";
    texts[b+0xCC]=ruling?"The ruling on the field is overturned.":"The booth is reviewing the play";
    texts[b+0x238]=row;
    words[request]=(uint32_t)slot;words[request+0xF14]=cb;
    for(uint32_t field:{0x4Cu,0xCCu,0x238u})texts[request+field]=texts[b+field];
}
static void setup(int slot=0,bool ruling=false,const std::string &row="Spot Of Ball"){
    s_review_owner={};s_popup_events.clear();words.clear();texts.clear();
    s_sel_pkg=1;s_pregame_host_generation=7;s_review_host_package=1;s_review_page_permitted=1;
    s_pkgs[1]={true,true};s_pkgs[2]={true,true};
    fake_game={};fake_game.valid=true;fake_game.period=2;fake_game.phase=3;
    fake_game.t[0].abbr="MIN";fake_game.t[1].abbr="DAL";
    words[0x00E6028Cu]=0x123400u;
    words[k_team_obj[0]+0x1Cu]=0x08123400u;words[k_team_obj[1]+0x1Cu]=0x08134500u;
    cx={false,false,false,false};read_success=true;fake_now+=1000;
    instance(slot,ruling,row);
}
static std::string ack(const char *verb="SHOW",const std::string &token=""){
    return std::string("NFL2K5_REVIEW_")+verb+":"+(token.empty()?std::to_string(s_review_owner.token):token);
}
static void page(const std::string &message,int pk=1,uint32_t gen=7){
    unsigned before=guest_reads,sbefore=state_reads;
    review_page_message(pk,gen,message);
    CHECK(guest_reads==before);CHECK(state_reads==sbefore);
}
static PendingPopupEvent deliver(){
    CHECK(s_review_owner.active&&!s_review_owner.queued&&!s_review_owner.delivered);
    size_t before=s_popup_events.size();review_hud_context(fake_game,cx);
    CHECK(s_review_owner.active&&s_review_owner.queued&&!s_review_owner.delivered);
    CHECK(s_popup_events.size()==before+1);
    auto ev=s_popup_events.back();CHECK(ev.name=="REVIEW_SHOW");
    CHECK(ev.review_token==s_review_owner.token&&ev.review_generation==7&&ev.review_package==1);
    CHECK(ev.extra.find("\"source\":\"native\"")!=std::string::npos);
    CHECK(ev.extra.find("\"nativeVisible\":true")!=std::string::npos);
    CHECK(ev.extra.find("\"nativeReviewToken\":\""+std::to_string(ev.review_token)+"\"")!=std::string::npos);
    CHECK(review_event_current(ev,fake_game));
    CHECK(s_review_owner.delivered&&!s_review_owner.queued&&!s_review_owner.acknowledged);
    size_t n=s_popup_events.size();review_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n);
    return ev;
}
static void accept_and_ack(int slot=0,bool ruling=false){
    nfl2k5_presentation_review_accepted(slot,ruling?1u:0u);
    CHECK(s_review_owner.active&&!s_review_owner.acknowledged);
    CHECK(review_draw_replaced(ctx_for(slot))==0);
    deliver();page(ack());CHECK(s_review_owner.acknowledged);
    CHECK(review_draw_replaced(ctx_for(slot))==1);
}
static void test_both_slots_stages(){
    for(int slot:{0,1})for(bool ruling:{false,true}){
        setup(slot,ruling,ruling?"":"Spot Of Ball");
        nfl2k5_presentation_review_accepted(slot,ruling?1u:0u);
        CHECK(s_review_owner.active&&s_review_owner.slot==slot&&s_review_owner.ruling==ruling);
        CHECK(s_popup_events.empty()); // accepted is not page-delivered/painted
        page(ack());CHECK(!s_review_owner.acknowledged); // premature ACK cannot hide art
        auto ev=deliver();
        CHECK(ev.extra.find(ruling?"\"reviewStage\":\"ruling\"":"\"reviewStage\":\"review\"")!=std::string::npos);
        CHECK(ev.extra.find(ruling?"\"reason\":\"\"":"\"reason\":\"Spot Of Ball\"")!=std::string::npos);
        CHECK(review_draw_replaced(ctx_for(slot))==0);
        page(ack());CHECK(s_review_owner.acknowledged);CHECK(review_draw_replaced(ctx_for(slot))==1);
        CHECK(review_draw_replaced(ctx_for(1-slot))==0);CHECK(review_draw_replaced(request)==0);
        review_slot_closing(1-slot);CHECK(!s_review_owner.closing);
        words[ctx_for(slot)+0xC]=1;review_slot_closing(slot);
        CHECK(s_review_owner.closing&&s_popup_events.back().name=="REVIEW_HIDE");
        CHECK(review_event_current(s_popup_events.back(),fake_game));
        page(ack("HIDE"));CHECK(s_review_owner.acknowledged);
        CHECK(review_draw_replaced(ctx_for(slot))==1); // preserve during native exit animation
        size_t n=s_popup_events.size();review_slot_closing(slot);CHECK(s_popup_events.size()==n);
        words[ctx_for(slot)+4]=0;CHECK(review_draw_replaced(ctx_for(slot))==0);
        review_hud_context(fake_game,cx);CHECK(!s_review_owner.active);
        page(ack("SHOW",std::to_string(ev.review_token)));CHECK(!s_review_owner.active);
    }
}
static void test_readiness_and_live_revocation(){
    setup();words[base+0xF1C]=0;
    nfl2k5_presentation_review_accepted(0,0);auto ev=deliver();page(ack());
    CHECK(s_review_owner.acknowledged);CHECK(review_draw_replaced(base)==0);
    words[base+0xF1C]=1;CHECK(review_draw_replaced(base)==1);
    page(ack("HIDE"));CHECK(!s_review_owner.acknowledged);CHECK(review_draw_replaced(base)==0);
    page(ack());CHECK(review_draw_replaced(base)==1);
    review_slot_closing(0);page(ack("HIDE"));CHECK(review_draw_replaced(base)==1);
    words[base+0xF1C]=0;CHECK(review_draw_replaced(base)==0); // no first-draw evidence now
    setup();nfl2k5_presentation_review_accepted(0,0);deliver();
    review_slot_closing(0);page(ack());CHECK(!s_review_owner.acknowledged);
    CHECK(review_draw_replaced(base)==0); // late ACK cannot acquire art during close
}
static void test_rejected_constructors(){
    for(int slot:{-1,2,100}){setup();nfl2k5_presentation_review_accepted(slot,0);CHECK(!s_review_owner.active&&s_popup_events.empty());}
    for(unsigned ruling:{2u,UINT32_MAX}){setup();nfl2k5_presentation_review_accepted(0,ruling);CHECK(!s_review_owner.active);}
    for(int mutation=0;mutation<31;++mutation){
        setup();
        switch(mutation){
          case 0:words[base]=1;break;
          case 1:words[base+4]=0;break;
          case 2:words[base+0xC]=1;break;
          case 3:words[base+0x48]=7;break;
          case 4:words[base+0x48]=19;break;
          case 5:words[base+0x228]=1;break;
          case 6:words[base+0x228]=3;break;
          case 7:words[base+0x230]=0;break;
          case 8:words[base+0x230]=2;break;
          case 9:words[base+0x234]=1;break;
          case 10:words[base+0x458]=0;break;
          case 11:words[base+0x458]=2;break;
          case 12:words[base+0xF14]=0;break;
          case 13:words[base+0xF14]=0x00117300u;break;
          case 14:words[request]=UINT32_MAX;break;
          case 15:words[request]=1;break;
          case 16:words[request+0xF14]=0x00117300u;break;
          case 17:texts[request+0x4C]="Unrelated";break;
          case 18:texts[request+0xCC]="Unrelated";break;
          case 19:texts[request+0x238]="Unrelated";break;
          case 20:texts[base+0x4C]="";texts[request+0x4C]="";break;
          case 21:texts[base+0xCC]="";texts[request+0xCC]="";break;
          case 22:words[k_team_obj[0]+0x1C]=0;break;
          case 23:words[k_team_obj[1]+0x1C]=0;break;
          case 24:fake_game.valid=false;break;
          case 25:read_success=false;break;
          case 26:s_sel_pkg=0;break;
          case 27:s_review_host_package=2;break;
          case 28:s_pkgs[1].html=false;break;
          case 29:s_pkgs[1].review_cards_ack=false;break;
          case 30:words[base+0x2B8]=0;break;
        }
        nfl2k5_presentation_review_accepted(0,0);
        CHECK(!s_review_owner.active&&s_popup_events.empty());CHECK(review_draw_replaced(base)==0);
    }
    setup();accept_and_ack();uint32_t token=s_review_owner.token;
    nfl2k5_presentation_review_accepted(-1,0);
    CHECK(s_review_owner.active&&s_review_owner.token==token&&review_draw_replaced(base)==1);
}
static void test_stale_record_guards(){
    for(int mutation=0;mutation<27;++mutation){
        setup();accept_and_ack();
        switch(mutation){
          case 0:words[base]=1;break;
          case 1:words[base+4]=0;break;
          case 2:words[base+0x48]=0;break;
          case 3:words[base+0x228]=1;break;
          case 4:words[base+0x230]=2;break;
          case 5:words[base+0x234]=1;break;
          case 6:words[base+0x458]=2;break;
          case 7:words[base+0xF14]=0x00117300u;break;
          case 8:texts[base+0x4C]="Unrelated";break;
          case 9:texts[base+0xCC]="Different review";break;
          case 10:texts[base+0x238]="Fumble Before Down";break;
          case 11:words[0x00E6028Cu]+=4;break;
          case 12:words[k_team_obj[0]+0x1C]+=0x100;break;
          case 13:words[k_team_obj[1]+0x1C]+=0x100;break;
          case 14:words[k_team_obj[0]+0x1C]=0;break;
          case 15:fake_game.valid=false;break;
          case 16:s_sel_pkg=2;break;
          case 17:s_pregame_host_generation=8;break;
          case 18:s_review_host_package=2;break;
          case 19:s_pkgs[1].html=false;break;
          case 20:s_pkgs[1].review_cards_ack=false;break;
          case 21:s_sel_pkg=0;break;
          case 22:s_sel_pkg=20;break;
          case 23:cx.paused=true;break;
          case 24:cx.play_selection=true;break;
          case 25:s_review_owner.ctx=base+4;break;
          case 26:words[base+0x2B8]=2;break;
        }
        if(mutation==23||mutation==24)review_hud_context(fake_game,cx);
        CHECK(review_draw_replaced(base)==0);
        review_hud_context(fake_game,cx);CHECK(!s_review_owner.active);
    }
    setup();accept_and_ack();read_success=false;
    CHECK(review_draw_replaced(base)==0); // draw-time read failure does not fabricate validity
    read_success=true;CHECK(review_draw_replaced(base)==1);
}
static void test_tokens_messages_and_reset(){
    setup();accept_and_ack();uint32_t token=s_review_owner.token;
    const std::vector<std::string> invalid={"","0","00","01","-1","+1","1 "," 1","1\n","1x","4294967296","999999999999999999999"};
    for(const auto &value:invalid){
        page(ack("HIDE",value.empty()?" ":value));CHECK(s_review_owner.acknowledged);
        page(ack("SHOW",value.empty()?" ":value));CHECK(s_review_owner.acknowledged);
    }
    page("NFL2K5_REVIEW_SHOW:");CHECK(s_review_owner.acknowledged);
    page("NFL2K5_REVIEW_HIDE:");CHECK(s_review_owner.acknowledged);
    page("NFL2K5_PERIOD_BOARD_HIDE:"+std::to_string(token));CHECK(s_review_owner.acknowledged);
    page(ack("HIDE"),2,7);CHECK(s_review_owner.acknowledged);
    page(ack("HIDE"),1,8);CHECK(s_review_owner.acknowledged);
    page(ack("HIDE",std::to_string(token+1)));CHECK(s_review_owner.acknowledged);
    s_review_page_permitted=0;page(ack());CHECK(!s_review_owner.acknowledged);
    s_review_page_permitted=1;page(ack());CHECK(s_review_owner.acknowledged);
    auto ev=s_popup_events.back();review_replacement_reset();
    CHECK(!s_review_owner.active&&s_review_host_package==-1&&s_review_page_permitted==0);
    CHECK(!review_event_current(ev,fake_game));CHECK(review_draw_replaced(base)==0);
    page(ack("SHOW",std::to_string(token)));CHECK(!s_review_owner.active);
    setup();s_review_token=UINT32_MAX-1;
    nfl2k5_presentation_review_accepted(0,0);CHECK(s_review_owner.token==UINT32_MAX);
    deliver();page(ack());CHECK(review_draw_replaced(base)==1);
    instance(1,true,"");nfl2k5_presentation_review_accepted(1,1);
    CHECK(s_review_owner.token==1&&s_review_owner.slot==1&&!s_review_owner.acknowledged);
    deliver();page(ack());CHECK(review_draw_replaced(ctx_for(1))==1);
    PendingPopupEvent unrelated;CHECK(review_event_current(unrelated,fake_game));
}
static void test_reuse_and_ruling_payload(){
    setup();nfl2k5_presentation_review_accepted(0,0);auto old=deliver();page(ack());
    uint32_t first=s_review_owner.token;
    instance(0,true,"");nfl2k5_presentation_review_accepted(0,1);
    CHECK(s_review_owner.active&&s_review_owner.token!=first&&s_review_owner.ruling);
    CHECK(s_review_owner.reason.empty()&&!s_review_owner.acknowledged);
    CHECK(s_popup_events.back().name=="REVIEW_HIDE");
    CHECK(s_popup_events.back().review_token==first);
    CHECK(review_event_current(s_popup_events.back(),fake_game));
    CHECK(!review_event_current(old,fake_game));
    page(ack("SHOW",std::to_string(first)));CHECK(!s_review_owner.acknowledged);
    auto ruling=deliver();CHECK(ruling.extra.find("\"reason\":\"\"")!=std::string::npos);
    page(ack());CHECK(review_draw_replaced(base)==1);
    page(ack("HIDE",std::to_string(first)));CHECK(s_review_owner.acknowledged);
    setup(1,true,"Dallas is charged a timeout.");nfl2k5_presentation_review_accepted(1,1);
    auto ev=deliver();CHECK(ev.extra.find("Dallas is charged a timeout.")!=std::string::npos);
    instance(1,true,"Dallas is not charged a timeout.");
    nfl2k5_presentation_review_accepted(1,1);CHECK(!review_event_current(ev,fake_game));
    CHECK(s_review_owner.reason=="Dallas is not charged a timeout.");
    deliver();page(ack());CHECK(review_draw_replaced(ctx_for(1))==1);
    // A queued show that starts closing before delivery never obtains ACK.
    setup();nfl2k5_presentation_review_accepted(0,0);review_hud_context(fake_game,cx);
    auto queued=s_popup_events.back();review_slot_closing(0);
    CHECK(!review_event_current(queued,fake_game));page(ack());CHECK(!s_review_owner.acknowledged);
    // Wrong package/generation drops queued events without browser side effects.
    setup();nfl2k5_presentation_review_accepted(0,0);review_hud_context(fake_game,cx);
    queued=s_popup_events.back();s_pregame_host_generation=8;
    CHECK(!review_event_current(queued,fake_game));
    queued.name="REVIEW_HIDE";CHECK(!review_event_current(queued,fake_game));
}
int main(){
    test_both_slots_stages();test_rejected_constructors();test_stale_record_guards();
    test_tokens_messages_and_reset();test_reuse_and_ruling_payload();
    test_readiness_and_live_revocation();
    printf("PASS %u actual native challenge/review owner assertions\n",checks);
}
'''

def main():
    source=SOURCE.read_text(encoding='utf-8')
    start=source.index('struct NativeReviewOwner {')
    owner=source[start:source.index("/* The original 'endofqrt' board",start)]
    states=source[source.index('struct TeamState {'):source.index('static const uint32_t k_playbook_name')]
    pending=source.index('static std::mutex s_popup_lock;')
    declarations=source[pending:source.index('enum { PREGAME_COIN',pending)]
    context=re.search(r'struct HtmlContext \{[^}]+\};',source).group()
    deps=states+context+'\n'+declarations+'\n'+'\n'.join([
        period.function(source,'rd32','static uint32_t '),
        period.function(source,'json_str','static std::string '),
    ])+'\nstatic GameState fake_game;\nstatic bool read_success=true;\n'+(
        'static bool read_state(GameState &g){++state_reads;g=fake_game;return read_success;}\n')
    generated=(ROOT/'src/recomp/gen/recomp_0004.c').read_text(encoding='utf-8')
    show=period.function(generated,'sub_0008ACF0','void ')
    draw=period.function(generated,'sub_0008A170','void ')
    auto=show[show.index('loc_0008ADB9: ;'):show.index('loc_0008ADE1: ;')]
    reuse=show[show.index('loc_0008ADEA: ;'):show.index('loc_0008AE12: ;')]
    assert auto.index('MEM32(edi) = esi;')<auto.index('loc_0008ADD5: ;')
    assert reuse.index('MEM32(edi) = esi;')<reuse.index('loc_0008AE06: ;')
    assert 'eax = eax | 0xFFFFFFFFu;' in show[show.index('loc_0008ADB2: ;'):show.index('loc_0008ADB9: ;')]
    ready=draw.index('PUSH32(esp, 0x0008A19Du); RECOMP_ABI_CALL(0x001BCE30u, sub_001BCE30);')
    exit_ready=draw.index('if (TEST_Z(_fa, _fb)) goto loc_0008A269;',ready)
    latch=draw.index('MEM32(esi + 0xF1C) = 1;')
    assert draw.index('MEM32(esi + 0xF08)')<ready<exit_ready<latch
    assert latch<draw.index('PUSH32(esp, 0x0008A1CFu); RECOMP_ABI_CALL(0x001BCB40u, sub_001BCB40);')
    constructors=(ROOT/'src/recomp/gen/recomp_0008.c').read_text(encoding='utf-8')
    patches=(ROOT/'tools/apply-gen-patches.py').read_text(encoding='utf-8')
    node=next(n.value for n in ast.parse(patches).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='PATCHES' for t in n.targets))
    names={'PRES_REVIEW_ACCEPTED','PRES_REVIEW_RULING_ACCEPTED'}
    hooks={item[0]:item for n in node.elts if isinstance(n,ast.Tuple) and isinstance(n.elts[0],ast.Constant) and n.elts[0].value in names for item in [ast.literal_eval(n)]}
    for name,fn,pc,stage in [('PRES_REVIEW_ACCEPTED','sub_00117310','0011741B',0),('PRES_REVIEW_RULING_ACCEPTED','sub_00117430','00117578',1)]:
        actual=period.function(constructors,fn,'void ')
        anchor=f'PUSH32(esp, 0x{pc}u); RECOMP_ABI_CALL(0x0008ACF0u, sub_0008ACF0); /* call 0x0008ACF0 */'
        assert actual.count(anchor)==1
        hook=hooks[name]
        assert hook[1:4]==(fn,anchor,'after')
        observer=f'nfl2k5_presentation_review_accepted((int)eax, {stage}u)'
        assert observer in hook[4]
        if 'NFL2K5-GENPATCH:'+name in actual:
            assert actual.index(anchor)<actual.index(observer)<actual.index('loc_'+pc+': ;')
    close_glue=period.function(source,'nfl2k5_presentation_player_fact_hide','extern "C" void ')
    assert close_glue.index('review_slot_closing(slot);')<close_glue.index('if (slot != 1')
    main_source=(ROOT/'src/main.c').read_text(encoding='utf-8')
    diag=period.function(main_source,'nfl2k5_diag_popup','void ')
    close_branch=diag[diag.index('} else if (what == 1) {'):]
    assert 'if (obj < 2 && *(const uint32_t *)(m + sl + 4u))' in close_branch
    assert 'nfl2k5_presentation_player_fact_hide((int)obj);' in close_branch
    output=ROOT/'outputs/presentation-audit/review-owner-test'
    output.mkdir(parents=True,exist_ok=True)
    cpp,exe=output/'review-owner.cpp',output/'review-owner.exe'
    cpp.write_text(FIXTURE+deps+owner+CASES,encoding='utf-8')
    compiler,environment=period.msvc_environment()
    command=[str(compiler),'/nologo','/EHsc','/std:c++17','/Fe:'+str(exe),'/Fo:'+str(output/'review-owner.obj'),str(cpp)]
    built=subprocess.run(command,env=environment,cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'compile.log').write_text(built.stdout,encoding='utf-8')
    if built.returncode:
        print(built.stdout);raise SystemExit(built.returncode)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/'test.stdout.log').write_text(result.stdout,encoding='utf-8')
    (output/'test.stderr.log').write_text(result.stderr,encoding='utf-8')
    fingerprint=hashlib.sha256((owner+states+declarations+draw+show+repr(hooks)+close_glue+close_branch).encode()).hexdigest()
    (output/'source.sha256').write_text(fingerprint+'\n',encoding='utf-8')
    print(result.stdout,end='')
    if result.returncode:
        print(result.stderr);raise SystemExit(result.returncode)
    print('Actual extracted ownership source SHA-256: '+fingerprint)
    print('Source checks: request slot assigned before copy, rejection returns -1, F1C store follows readiness, exact post-accept hooks and both-slot close glue.')
if __name__=='__main__':main()

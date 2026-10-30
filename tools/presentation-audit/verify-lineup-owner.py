"""Compile and test the actual isolated native lineup owner implementation.

Guest RAM/read_state, package choices and pending-event transport are fixtures.
The owner and real pause/replay/final predicates are extracted unchanged.
Never starts a game, runs a Release build or regenerates guest code.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/nfl2k5_presentation.cpp'
OWNER=ROOT/'src/presentation/native_lineup.inc.cpp'
spec=importlib.util.spec_from_file_location('period_owner_verifier',Path(__file__).with_name('verify-period-owner.py'))
period=importlib.util.module_from_spec(spec);spec.loader.exec_module(period)

FIXTURE=r'''
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
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static std::map<uint32_t,uint32_t> words;
static std::map<uint32_t,std::string> texts;
static bool gread(uint32_t va,void *out,size_t n){++guest_reads;auto it=words.find(va);if(it==words.end()||(n!=1&&n!=4))return false;memcpy(out,&it->second,n);return true;}
static std::string gtext(uint32_t va,int max){++guest_reads;auto it=texts.find(va);return it==texts.end()?std::string():it->second.substr(0,(size_t)max);}
static uint64_t fake_now=1000;
static uint64_t GetTickCount64(){return fake_now;}
struct PackageFixture{bool html=false,lineup_cards_ack=false;};
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1},s_native_match_lifecycle{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
// Actual integrated pending-event declarations are extracted below.
'''

CASES=r'''
static constexpr uint32_t ctx=0x01804000u,resource=0x01805000u,scene=0x01806000u,sp=0x0180F000u;
static constexpr uint32_t roster[2]={0x01807000u,0x01808000u};
static HtmlContext cx{true,false,false,false,false};
static std::string payload;
static uint32_t record(int side,unsigned index){return 0x01900000u+(uint32_t)side*0x10000u+index*0x100u;}
static void setup(int side=1,bool offense=true){
    s_native_lineup_owner={};s_popup_events.clear();words.clear();texts.clear();s_sel_pkg=1;s_pregame_host_generation=7;
    s_native_lineup_host_package=1;s_native_lineup_page_permitted=1;s_native_match_lifecycle=1;s_pkgs[1]={true,true};s_pkgs[2]={true,true};
    fake_game={};fake_game.valid=true;fake_game.period=1;fake_game.phase=4;fake_game.flow_state=0x0E;
    fake_game.t[0].abbr="DAL";fake_game.t[1].abbr="NE";cx={true,false,false,false,false};read_success=true;fake_now+=1000;
    words[0x00E6028Cu]=0x123400u;words[0x00BA3534u]=ctx;words[0x00BA3538u]=resource;words[resource]=scene;
    words[0x00BA3210u]=1;words[0x00BA3214u]=1;words[0x00BA3218u]=1;words[0x00BA3544u]=roster[side];words[0x00BDE77Cu]=ctx;
    for(int s=0;s<2;++s){words[k_team_obj[s]+0x1Cu]=roster[s];words[roster[s]+0x11Cu]=11;for(unsigned i=0;i<11;++i)words[roster[s]+i*4u]=record(s,i);}
    words[0x00E60280u]=k_team_obj[offense?side:1-side];
    words[ctx+0x150u]=0x000FFE70u;words[ctx+0x154u]=0;words[ctx+0x158u]=resource;words[ctx+0x15Cu]=0;words[ctx+0x160u]=0x00BA3550u;
    payload.clear();
    for(unsigned i=0;i<11;++i){
        words[sp+0x1Cu+i*4u]=record(side,i);words[sp+0x48u+i*4u]=i;words[record(side,i)+0x20u]=(10+i)<<3;
        words[0x004F2688u+i*4u]=0x01000000u+i*0x20u;texts[0x01000000u+i*0x20u]="POS"+std::to_string(i);
        texts[0x00BA3274u+i*0x40u]="Native Player "+std::to_string(i);
        if(i)payload+=",";payload+="{\"position\":\"POS"+std::to_string(i)+"\",\"number\":"+std::to_string(10+i)+",\"name\":\"Native Player "+std::to_string(i)+"\"}";
    }
}
static std::string ack(const char *verb="SHOW",const std::string &token=""){return std::string("NFL2K5_LINEUP_")+verb+":"+(token.empty()?std::to_string(s_native_lineup_owner.token):token);}
static void page(const std::string &message,int pk=1,uint32_t generation=7){unsigned reads=guest_reads,states=state_reads;native_lineup_page_message(pk,generation,message);CHECK(guest_reads==reads);CHECK(state_reads==states);}
static void prepare(){CHECK(native_lineup_snapshot(sp,payload,11));CHECK(s_native_lineup_owner.prepared&&!s_native_lineup_owner.active);CHECK(s_popup_events.empty());}
static PendingPopupEvent deliver(){
    size_t n=s_popup_events.size();native_lineup_hud_context(fake_game,cx);CHECK(s_native_lineup_owner.active&&s_native_lineup_owner.queued&&!s_native_lineup_owner.delivered);
    CHECK(s_popup_events.size()==n+1);auto event=s_popup_events.back();CHECK(event.name=="LINEUP");CHECK(event.lineup_token==s_native_lineup_owner.token);
    CHECK(event.lineup_generation==7&&event.lineup_package==1&&event.side==s_native_lineup_owner.side);
    CHECK(event.extra.find("\"nativeVisible\":true")!=std::string::npos);CHECK(event.extra.find("\"source\":\"native\"")!=std::string::npos);
    CHECK(event.extra.find("\"nativeLineupToken\":\""+std::to_string(event.lineup_token)+"\"")!=std::string::npos);
    CHECK(event.extra.find("\"players\":["+payload+"]")!=std::string::npos);CHECK(native_lineup_event_current(event,fake_game));
    CHECK(s_native_lineup_owner.delivered&&!s_native_lineup_owner.queued);n=s_popup_events.size();native_lineup_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n);return event;
}
static void bind(){native_lineup_frame(ctx);CHECK(s_native_lineup_owner.active&&!s_native_lineup_owner.acknowledged);CHECK(!native_lineup_draw_replaced(ctx));}
static void bind_and_ack(){prepare();bind();deliver();page(ack());CHECK(s_native_lineup_owner.acknowledged);CHECK(native_lineup_draw_replaced(ctx)==1);}
static void test_source_and_actual_render(){
    for(int side:{0,1})for(bool offense:{false,true}){
        setup(side,offense);prepare();CHECK(s_native_lineup_owner.side==side&&s_native_lineup_owner.offense==offense);
        CHECK(s_native_lineup_owner.selected_roster==roster[side]);CHECK(!native_lineup_draw_replaced(ctx));page(ack());CHECK(!s_native_lineup_owner.acknowledged);
        native_lineup_hud_context(fake_game,cx);CHECK(s_popup_events.empty()); // preparation is not native visibility
        native_lineup_frame(0x00B73C44u);CHECK(!s_native_lineup_owner.active);bind();
        page(ack());CHECK(!s_native_lineup_owner.acknowledged);auto e=deliver();CHECK(e.extra.find(offense?"\"unit\":\"offense\"":"\"unit\":\"defense\"")!=std::string::npos);
        CHECK(!native_lineup_draw_replaced(ctx));page(ack());CHECK(native_lineup_draw_replaced(ctx)==1);CHECK(!native_lineup_draw_replaced(ctx+4));
        // Later possession changes cannot reassign this captured native unit/team.
        words[0x00E60280u]=k_team_obj[1-side];fake_game.poss=side?1:2;
        CHECK(s_native_lineup_owner.offense==offense&&s_native_lineup_owner.side==side);CHECK(native_lineup_draw_replaced(ctx)==1);
        size_t n=s_popup_events.size();auto token=s_native_lineup_owner.token;native_lineup_frame(ctx);native_lineup_hud_context(fake_game,cx);
        CHECK(s_native_lineup_owner.token==token&&s_popup_events.size()==n);
        native_lineup_hide(1);CHECK(!s_native_lineup_owner.prepared&&!s_native_lineup_owner.active);CHECK(!native_lineup_draw_replaced(ctx));
        CHECK(s_popup_events.back().name=="LINEUP_HIDE");CHECK(native_lineup_event_current(s_popup_events.back(),fake_game));
        native_lineup_frame(ctx);CHECK(!s_native_lineup_owner.active);
    }
}
static void mutate(int n){
    switch(n){
    case 0:words[0x00E6028Cu]++;break;case 1:words[0x00BA3534u]++;break;case 2:words[0x00BA3538u]++;break;
    case 3:words[resource]++;break;case 4:words[0x00BA3210u]=0;break;case 5:words[0x00BA3544u]=roster[0];break;
    case 6:words[ctx+0x150u]=0;break;case 7:words[ctx+0x154u]=1;break;case 8:words[ctx+0x158u]=0;break;
    case 9:words[ctx+0x15Cu]=0x00BA3550u;break;case 10:words[ctx+0x160u]=0;break;
    case 11:words[k_team_obj[0]+0x1Cu]++;break;case 12:words[k_team_obj[1]+0x1Cu]++;break;
    case 13:fake_game.t[0].abbr="BUF";break;case 14:fake_game.t[1].abbr="KC";break;case 15:fake_game.valid=false;break;
    case 16:fake_game.period=0;break;case 17:fake_game.flow_state=0x0A;break;case 18:s_native_match_lifecycle=2;break;
    case 19:s_pregame_host_generation=8;break;case 20:s_pkgs[1].html=false;break;case 21:s_pkgs[1].lineup_cards_ack=false;break;
    case 22:s_sel_pkg=2;break;case 23:s_native_lineup_host_package=2;break;
    }
}
static void test_source_rejection(){
    for(unsigned n:{0u,1u,10u,12u,100u}){setup();CHECK(!native_lineup_snapshot(sp,payload,n));CHECK(!s_native_lineup_owner.prepared);}
    setup();CHECK(!native_lineup_snapshot(sp,"",11));setup();CHECK(!native_lineup_snapshot(sp,std::string(32769,'a'),11));
    for(int n=0;n<21;++n){setup();
        switch(n){case 0:words[sp+0x1C]=0;break;case 1:words[sp+0x1C]=record(0,0);break;
        case 2:words[sp+0x20]=words[sp+0x1C];break;case 3:words[sp+0x48]=64;break;
        case 4:texts[0x01000000u]="";break;case 5:texts[0x00BA3274u]="";break;
        case 6:words[record(1,0)+0x20u]=127u<<3;break;case 7:words[roster[1]+0x11C]=0;break;
        case 8:words[roster[1]+0x11C]=72;break;case 9:words[k_team_obj[0]+0x1C]=roster[1];break;
        case 10:words[0x00BA3544u]=0x00BADBAD;break;case 11:words[0x00E60280u]=0;break;
        case 12:words[k_team_obj[1]+0x1Cu]=0;break;case 13:words[resource]=0;break;
        case 14:words[0x00BA3538u]=0;break;case 15:read_success=false;break;
        case 16:fake_game.period=0;break;case 17:fake_game.flow_state=0x0A;break;
        case 18:s_pkgs[1].lineup_cards_ack=false;break;case 19:s_pkgs[1].html=false;break;case 20:s_sel_pkg=0;break;}
        CHECK(!native_lineup_snapshot(sp,payload,11));CHECK(!s_native_lineup_owner.prepared);CHECK(s_popup_events.empty());
    }
    setup();bind_and_ack();CHECK(!native_lineup_snapshot(sp,payload,10));CHECK(!s_native_lineup_owner.prepared);CHECK(!native_lineup_draw_replaced(ctx));
    CHECK(s_popup_events.back().name=="LINEUP_HIDE"); // failed new unit never leaves old roster painted
}
static void test_identity_and_visibility(){
    for(int n=0;n<24;++n){setup();bind_and_ack();mutate(n);CHECK(!native_lineup_draw_replaced(ctx));native_lineup_hud_context(fake_game,cx);CHECK(!s_native_lineup_owner.prepared);}
    for(int n=0;n<7;++n){setup();bind_and_ack();uint32_t token=s_native_lineup_owner.token;
        switch(n){case 0:words[0x00BA3214u]=0;break;case 1:words[0x00BA3218u]=0;break;case 2:words[0x00BDE77Cu]=0x00BDE618u;break;
        case 3:words[0x00AC7444u]=1;break;case 4:words[0x00A84C18u]=1;words[0x00A84B20u]=0x004E9078u;break;
        case 5:words[0x00A84C18u]=1;words[0x00A84B20u]=0x0052DC20u;break;
        case 6:words[0x00A84C18u]=1;words[0x00A84B20u]=0x004E3F38u;words[0x00AFA078u]=3;break;}
        CHECK(!native_lineup_draw_replaced(ctx));native_lineup_hud_context(fake_game,cx);CHECK(s_native_lineup_owner.prepared&&!s_native_lineup_owner.active);
        CHECK(s_popup_events.back().name=="LINEUP_HIDE"&&s_popup_events.back().lineup_token==token);
    }
    for(int n=0;n<3;++n){setup();bind_and_ack();if(n==0)cx.paused=true;else if(n==1)cx.play_selection=true;else cx.replay_active=true;
        native_lineup_hud_context(fake_game,cx);CHECK(s_native_lineup_owner.prepared&&!s_native_lineup_owner.active);CHECK(!s_native_lineup_page_permitted);}
    setup();prepare();words[0x00BA3218u]=0;native_lineup_frame(ctx);CHECK(!s_native_lineup_owner.active);words[0x00BA3218u]=1;bind();deliver();page(ack());CHECK(native_lineup_draw_replaced(ctx)==1);
    for(uint32_t off:{0x12Cu,0x130u,0x134u,0x138u,0x13Cu,0x140u,0x164u,0x168u})words[ctx+off]=99;
    CHECK(native_lineup_draw_replaced(ctx)==1); // original ring/animation/list fields remain mutable
    setup();bind_and_ack();read_success=false;CHECK(!native_lineup_draw_replaced(ctx));
}
static void test_hide_resume_tokens_and_reset(){
    setup();bind_and_ack();auto old=s_native_lineup_owner.token;auto source=s_native_lineup_owner.source_serial;
    words[0x00BA3214u]=0;native_lineup_hide(0);CHECK(s_native_lineup_owner.prepared&&!s_native_lineup_owner.active);
    CHECK(s_native_lineup_owner.source_serial==source);page(ack("SHOW",std::to_string(old)));CHECK(!s_native_lineup_owner.acknowledged);
    native_lineup_frame(ctx);CHECK(!s_native_lineup_owner.active);words[0x00BA3214u]=1;bind();CHECK(s_native_lineup_owner.token!=old);deliver();
    page(ack("SHOW",std::to_string(old)));CHECK(!s_native_lineup_owner.acknowledged);page(ack());CHECK(s_native_lineup_owner.acknowledged);
    page(ack("HIDE",std::to_string(old)));CHECK(s_native_lineup_owner.acknowledged);
    page(ack("HIDE"));CHECK(!s_native_lineup_owner.acknowledged);CHECK(!native_lineup_draw_replaced(ctx));page(ack());CHECK(native_lineup_draw_replaced(ctx)==1);
    native_lineup_hide(1);CHECK(!s_native_lineup_owner.prepared);native_lineup_frame(ctx);CHECK(!s_native_lineup_owner.active);
    setup();prepare();bind();deliver();for(std::string t:{"0","01","+1","-1","4294967296","1.0","1e2"," 1","1 ","","999999999999999999999"}){page("NFL2K5_LINEUP_SHOW:"+t);CHECK(!s_native_lineup_owner.acknowledged);}
    page(ack(),2);CHECK(!s_native_lineup_owner.acknowledged);page(ack(),1,8);CHECK(!s_native_lineup_owner.acknowledged);s_native_lineup_page_permitted=0;page(ack());CHECK(!s_native_lineup_owner.acknowledged);
    setup();prepare();bind();native_lineup_hud_context(fake_game,cx);auto event=s_popup_events.back();native_lineup_hide(0);CHECK(!native_lineup_event_current(event,fake_game));page(ack("SHOW",std::to_string(event.lineup_token)));CHECK(!s_native_lineup_owner.acknowledged);
    setup();prepare();bind();native_lineup_hud_context(fake_game,cx);event=s_popup_events.back();s_pregame_host_generation=8;CHECK(!native_lineup_event_current(event,fake_game));event.name="LINEUP_HIDE";CHECK(!native_lineup_event_current(event,fake_game));
    setup();prepare();source=s_native_lineup_owner.source_serial;CHECK(native_lineup_snapshot(sp,payload,11));CHECK(s_native_lineup_owner.source_serial!=source);
    native_lineup_release("stale-read",true,true,source);CHECK(s_native_lineup_owner.prepared); // stale copied work cannot discard new seed
    setup();s_native_lineup_source_serial=UINT32_MAX;prepare();CHECK(s_native_lineup_owner.source_serial==1);s_native_lineup_token=UINT32_MAX;bind();CHECK(s_native_lineup_owner.token==1);
    setup();bind_and_ack();s_popup_events.clear();native_lineup_reset();CHECK(!s_native_lineup_owner.prepared);CHECK(s_native_lineup_host_package==-1&&!s_native_lineup_page_permitted);CHECK(s_popup_events.empty());
    // Hot paths before actual acknowledgment do not rescan guest roster/state.
    setup();unsigned reads=guest_reads,states=state_reads;CHECK(!native_lineup_draw_replaced(ctx));CHECK(guest_reads==reads&&state_reads==states);
    prepare();reads=guest_reads;states=state_reads;CHECK(!native_lineup_draw_replaced(ctx));CHECK(guest_reads==reads&&state_reads==states);
    bind();reads=guest_reads;states=state_reads;CHECK(!native_lineup_draw_replaced(ctx));CHECK(guest_reads==reads&&state_reads==states);
}
int main(){test_source_and_actual_render();test_source_rejection();test_identity_and_visibility();test_hide_resume_tokens_and_reset();printf("PASS %u actual native lineup-owner assertions\n",checks);}
'''

def integration_checks(source: str) -> tuple[int, str]:
    """Read-only checks of actual applied anchors and shared root wiring."""
    count=0
    def check(condition, label):
        nonlocal count
        if not condition:
            raise AssertionError(label)
        count+=1
    patches=(ROOT/'tools/apply-gen-patches.py').read_text(encoding='utf-8')
    node=next(n.value for n in ast.parse(patches).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='PATCHES' for t in n.targets))
    names={'LINEUP_OWNER_FRAME','LINEUP_OWNER_MESH','LINEUP_OWNER_TEXT','LINEUP_OWNER_HIDE','LINEUP_OWNER_COMPLETE','LINEUP_OWNER_DESTROY','LINEUP_TICKER','LINEUP_NATIVE_HIDE'}
    hooks={item[0]:item for n in node.elts if isinstance(n,ast.Tuple) and isinstance(n.elts[0],ast.Constant) and n.elts[0].value in names for item in [ast.literal_eval(n)]}
    check(set(hooks)==names,'All six owner hooks and existing source/legacy hooks must exist')
    gen=(ROOT/'src/recomp/gen/recomp_0011.c').read_text(encoding='utf-8')
    gen7=(ROOT/'src/recomp/gen/recomp_0007.c').read_text(encoding='utf-8')
    draw=period.function(gen,'sub_001770C0','void ')
    entry=hooks['LINEUP_OWNER_FRAME']
    check(entry[1:4]==('sub_001770C0','loc_001770C0: ;','after'),'Observe actual render EAX at entry')
    check('nfl2k5_presentation_lineup_frame(eax)' in entry[4],'Entry passes actual ticker register')
    check(draw.index('nfl2k5_presentation_lineup_frame(eax)')<draw.index('PUSH32(esp, ebp);'),'Entry observer runs before guest prologue')
    for name,pc,callee in [('LINEUP_OWNER_MESH','00177128','00021860'),('LINEUP_OWNER_TEXT','001771A3','001513D0')]:
        hook=hooks[name]
        anchor=f'PUSH32(esp, 0x{pc}u); RECOMP_ABI_CALL(0x{callee}u, sub_{callee}); /* call 0x{callee} */'
        check(hook[1:4]==('sub_001770C0',anchor,'before'),name+' must exactly match full stripped source line including call comment')
        check(draw.count(anchor)==1 and anchor in [line.strip() for line in draw.splitlines()],name+' original call must remain unique')
        check('nfl2k5_presentation_lineup_draw_replaced(esi)' in hook[4],name+' passes owning ticker, not mesh/text node')
        check('goto loc_'+pc+';' in hook[4] and 'return;' not in hook[4] and 'esp +=' not in hook[4],name+' only skips its output call')
        marker=draw.index('/* NFL2K5-GENPATCH:'+name+' */')
        call=draw.index(anchor);continuation=draw.index('loc_'+pc+': ;')
        check(marker<call<continuation,name+' applied guard precedes PUSH return address and reaches normal continuation')
        check(hook[4].strip() in draw[marker:call],name+' installed guard matches actual patch')
    for pc,callee in [('001770DA','0002AD80'),('001770EE','0002AC80'),('00177153','0002AC80'),('001771C0','0002AC80')]:
        check(f'PUSH32(esp, 0x{pc}u); RECOMP_ABI_CALL(0x{callee}u, sub_{callee});' in draw,'Original camera save/set/restore '+pc+' retained')
    check(draw.index('loc_001771C0: ;')<draw.index('esi = MEM32(esi + 0x154);')<draw.index('PUSH32(esp, 0x001771CCu); RECOMP_ICALL_SAFE'),'Original post-draw callback remains after camera restore')
    for name,fn,pc,completed in [('LINEUP_OWNER_HIDE','sub_000FFDA0','000FFDA0',0),('LINEUP_OWNER_COMPLETE','sub_000FFE70','000FFE70',1),('LINEUP_OWNER_DESTROY','sub_000FFDC0','000FFDC0',1)]:
        hook=hooks[name];actual=period.function(gen7,fn,'void ')
        check(hook[1:4]==(fn,'loc_'+pc+': ;','after'),name+' exact native lifecycle boundary')
        check('nfl2k5_presentation_lineup_hide('+str(completed)+')' in hook[4],name+' temporary vs completed lifecycle meaning')
        check('/* NFL2K5-GENPATCH:'+name+' */' in actual and hook[4].strip() in actual,name+' actual applied observer')
    native_show=period.function(gen7,'sub_000FFD60','void ')
    native_hide=period.function(gen7,'sub_000FFDA0','void ')
    native_done=period.function(gen7,'sub_000FFE70','void ')
    native_destroy=period.function(gen7,'sub_000FFDC0','void ')
    check('sub_001775D0(); return; /* tail jmp 0x001775D0 */' in native_show,'Native active-queue enqueue remains')
    check('RECOMP_ABI_CALL(0x00177640u, sub_00177640)' in native_hide and 'MEM32(0xBA3214) = 0;' in native_hide,'Native hide/dequeue and shown flag remain')
    check('RECOMP_ABI_CALL(0x000FFDA0u, sub_000FFDA0)' in native_done and 'MEM32(0xBA3210) = eax;' in native_done and 'MEM32(0xBA3218) = eax;' in native_done,'Native completion callback and native flags remain')
    check('sub_001774D0(); return; /* tail jmp 0x001774D0 */' in native_destroy,'Native ticker resource release remains')
    source_hook=hooks['LINEUP_TICKER'];source_fn=period.function(gen7,'sub_000FFBA0','void ')
    check(source_hook[1:4]==('sub_000FFBA0','loc_000FFD54: ;','after'),'Player snapshot observes completed native formatter')
    check(source_fn.index('nfl2k5_lineup_ticker(esp)')<source_fn.index('POP32(esp, edi);'),'Snapshot consumes live saved stack before unwind')
    collector=period.function(source,'nfl2k5_lineup_ticker','extern "C" void ')
    check(collector.count('native_lineup_snapshot(sp, players, (unsigned)n);')==1,'Actual collector invokes source snapshot once')
    check(collector.index('players +=')<collector.index('native_lineup_snapshot(sp, players, (unsigned)n);')<collector.index('s_lineups.push_back'),'Snapshot receives actual built payload before legacy transport')
    for field in ['position','number','name','first','last','college','photoId','portrait']:
        check('\\"'+field+'\\"' in collector,'Live collector keeps '+field+' field')
    for required in ['bool lineup_cards_ack = false;','p.lineup_cards_ack = p.root.flag("lineup_cards_ack", false);',
                     'uint32_t lineup_token = 0, lineup_generation = 0;','int lineup_package = -1;',
                     '#include "presentation/native_lineup.inc.cpp"','native_lineup_reset();',
                     'if (!native_lineup_event_current(ev, g)) continue;',
                     'if (pkg.lineup_cards_ack) s_native_lineup_host_package = pk;',
                     'native_lineup_page_message(pk, generation, message);','native_lineup_hud_context(g, cx);']:
        check(required in source,'Shared integration '+required)
    for fn,prefix,body in [('nfl2k5_presentation_lineup_frame','extern "C" void ','native_lineup_frame(ctx);'),
                           ('nfl2k5_presentation_lineup_draw_replaced','extern "C" int ','return native_lineup_draw_replaced(ctx);'),
                           ('nfl2k5_presentation_lineup_hide','extern "C" void ','native_lineup_hide(completed);')]:
        check(body in period.function(source,fn,prefix),'Gen-call wrapper '+fn+' reaches actual owner')
    cache=source[source.index('/* LINEUP: offense ='):source.index('std::vector<PendingPopupEvent> pending;',source.index('/* LINEUP: offense ='))]
    gate=re.search(r'if\s*\(!s_pkgs\[s_sel_pkg\.load\(\)\]\.lineup_cards_ack\)\s*html_post\(html_event_json\("LINEUP"',cache)
    check(gate is not None,'Untokened legacy LINEUP event is not posted for owned packages')
    check(cache.index('s_live_qb_identity[side] = l.quarterback;')<gate.start(),'Existing QB identity cache remains before legacy post gate')
    manifest=json.loads((ROOT/'mods/presentations/CBS_2026/mod.json').read_text(encoding='utf-8'))
    check(manifest.get('lineup_cards_ack') is True and 'lineup' not in manifest.get('replaces',[]),'CBS enables painted lineup ownership without legacy blind enqueue skip')
    legacy=period.function(source,'nfl2k5_lineup_hide_native','extern "C" int ')
    check('s_pkgs[k].replaces_lineup' in legacy,'Legacy bypass still requires its separate manifest declaration')
    html=(ROOT/'mods/presentations/CBS_2026/scorebug.html').read_text(encoding='utf-8')
    router=(ROOT/'mods/presentations/CBS_2026/scorebug.js').read_text(encoding='utf-8')
    check('href="lineup.css"' in html and 'src="lineup.js"' in html,'Shipped page loads component and editable CSS')
    check('CBSLineup.updateState(s,prev)' in router and 'CBSLineup.handleEvent(e)' in router,'Shipped page routes state and events')
    return count,repr(hooks)+draw+collector+cache+legacy+json.dumps(manifest,sort_keys=True)+html+router

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--static-only',action='store_true',help='Read applied hooks/integration without launching a compiler');args=parser.parse_args()
    source=SOURCE.read_text(encoding='utf-8');owner=OWNER.read_text(encoding='utf-8')
    static_count,glue=integration_checks(source)
    print(f'PASS {static_count} actual applied lineup hook/integration checks')
    if args.static_only:
        print('Static checks only: no compiler, build or game process started.');return
    states=source[source.index('struct TeamState {'):source.index('static const uint32_t k_playbook_name')]
    context=re.search(r'struct HtmlContext \{[^}]+\};',source).group()
    pending=source.index('static std::mutex s_popup_lock;')
    declarations=source[pending:source.index('enum { PREGAME_COIN',pending)]
    dependencies=states+context+'\n'+declarations+'\n'+'\n'.join(period.function(source,name,prefix) for name,prefix in [
        ('rd32','static uint32_t '),('json_str','static std::string '),('is_final','static bool '),
        ('native_pause_active','static bool '),('native_replay_active','static bool '),
    ])+'\nstatic GameState fake_game;\nstatic bool read_success=true;\nstatic bool read_state(GameState &g){++state_reads;g=fake_game;return read_success;}\n'
    output=ROOT/'outputs/presentation-audit/lineup-owner-test';output.mkdir(parents=True,exist_ok=True)
    cpp,exe=output/'lineup-owner.cpp',output/'lineup-owner.exe';cpp.write_text(FIXTURE+dependencies+owner+CASES,encoding='utf-8')
    compiler,environment=period.msvc_environment();command=[str(compiler),'/nologo','/EHsc','/std:c++17','/Fe:'+str(exe),'/Fo:'+str(output/'lineup-owner.obj'),str(cpp)]
    built=subprocess.run(command,env=environment,cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'compile.log').write_text(built.stdout,encoding='utf-8')
    if built.returncode:print(built.stdout);raise SystemExit(built.returncode)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/'test.stdout.log').write_text(result.stdout,encoding='utf-8');(output/'test.stderr.log').write_text(result.stderr,encoding='utf-8')
    fingerprint=hashlib.sha256((owner+dependencies+glue).encode()).hexdigest();(output/'source.sha256').write_text(fingerprint+'\n',encoding='utf-8');print(result.stdout,end='')
    if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
    print('Actual extracted lineup owner source SHA-256: '+fingerprint)
    print('No native game, Release build, main CPP/header or generated code was changed by this verifier.')

if __name__=='__main__':main()

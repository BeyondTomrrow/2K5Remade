"""Exercise actual native replay ownership code without starting/building the game.

Extracts NativeReplayOwner through the Optional presentation policy boundary,
native_replay_active, the real pending-event/state declarations and guest-read
formatting helpers. Only guest RAM, game/package inputs and host time are mocked.
The standalone MSVC harness lives under outputs, never build/Release.
"""
from __future__ import annotations

import ast
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/nfl2k5_presentation.cpp"
spec = importlib.util.spec_from_file_location("period_owner_verifier", Path(__file__).with_name("verify-period-owner.py"))
period = importlib.util.module_from_spec(spec)
spec.loader.exec_module(period)


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("struct NativeReplayOwner {")
    end = source.index("/* Optional presentation policy,", start)
    owner = source[start:end]
    states = source[source.index("struct TeamState {"):source.index("static const uint32_t k_playbook_name")]
    pending_start = source.index("static std::mutex s_popup_lock;")
    declarations = source[pending_start:source.index("enum { PREGAME_COIN", pending_start)]
    context = re.search(r"struct HtmlContext \{[^}]+\};", source).group()
    active = period.function(source, "native_replay_active", "static bool ")
    pause = period.function(source, "native_pause_active", "static bool ")
    in_match = period.function(source, "in_match", "static int ")
    assert "native_pause_active()" in in_match
    assert "bool pause_on = native_pause_active();" in source
    assert "!native_pause_active()" in owner
    lifecycle = period.function(source, "nfl2k5_presentation_match_lifecycle", 'extern "C" void ')
    lifecycle_declarations = "\n".join(re.search(pattern, source).group() for pattern in [
        r"static std::atomic<DWORD> s_match_tick\{[^}]*\};",
        r"static std::atomic<int> s_native_match_lifecycle\{[^}]*\};",
    ])
    keepalive = re.search(r"if \((g\.valid && \(pregame_on[^\n]+)\)\s*\n\s*s_match_tick = activity_now;", source)
    if not keepalive or "replay_on" not in keepalive.group(1) or "native_match_on" not in keepalive.group(1):
        raise RuntimeError("Actual match keepalive condition lacks replay or native lifecycle evidence")
    activity = re.search(r"if \(g\.valid && \(pregame_on[^\n]+\)\)\s*\n\s*s_match_tick = activity_now;\s*\n\s*if \(!g\.valid[^\n]+s_match_tick = 0;", source)
    if not activity:
        raise RuntimeError("Actual match activity/exit reset sequence missing")
    keepalive_source = (
        "static bool actual_match_keepalive(const GameState &g, bool replay_on, bool native_match_on=false) {\n"
        "const bool pregame_on=false,native_on=false,native_card_on=false,playcall_on=false,"
        "pause_on=false,clock_moved=false,s_test=false;\nreturn " + keepalive.group(1) + ";\n}\n"
        "static void actual_match_activity(const GameState &g, bool replay_on=false) {\n"
        "const bool pregame_on=false,native_on=false,native_card_on=false,playcall_on=false,"
        "pause_on=false,clock_moved=false,s_test=false;\n"
        "bool native_match_on=s_native_match_lifecycle.load()==1;\n"
        "DWORD activity_now=(DWORD)GetTickCount64();\n" + activity.group() + "\n}\n"
    )
    # Read hook definitions as inert syntax, never import/execute the patcher.
    patch_source = (ROOT / "tools/apply-gen-patches.py").read_text(encoding="utf-8")
    patches_node = next(node.value for node in ast.parse(patch_source).body
                        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "PATCHES" for t in node.targets))
    lifecycle_hooks = {item[0]: item for node in patches_node.elts
                       if isinstance(node, ast.Tuple) and isinstance(node.elts[0], ast.Constant)
                       and node.elts[0].value in {"PRES_MATCH_INITIALIZED", "PRES_MATCH_EXIT"}
                       for item in [ast.literal_eval(node)]}
    enter_hook = lifecycle_hooks["PRES_MATCH_INITIALIZED"]
    exit_hook = lifecycle_hooks["PRES_MATCH_EXIT"]
    assert enter_hook[1:4] == ("sub_00064C70", "PUSH32(esp, 0x00064C82u); RECOMP_ABI_CALL(0x00064710u, sub_00064710); /* call 0x00064710 */", "after")
    assert exit_hook[1:4] == ("sub_000645D0", "loc_000645D0: ;", "after")
    assert "nfl2k5_presentation_match_lifecycle(1)" in enter_hook[4]
    assert "nfl2k5_presentation_match_lifecycle(0)" in exit_hook[4]
    generated_source = (ROOT / "src/recomp/gen/recomp_0002.c").read_text(encoding="utf-8")
    generated_enter = period.function(generated_source, "sub_00064C70", "void ")
    call_index = generated_enter.index(enter_hook[2])
    assert generated_enter.count(enter_hook[2]) == 1
    assert generated_enter.index("if (TEST_Z(_fa, _fb)) goto loc_00064C88;") < call_index
    assert call_index < generated_enter.index("loc_00064C82: ;") < generated_enter.index("loc_00064C88: ;")
    if "NFL2K5-GENPATCH:PRES_MATCH_INITIALIZED" not in generated_enter:
        generated_enter = generated_enter.replace(enter_hook[2], enter_hook[2] + "\n" + enter_hook[4], 1)
    assert generated_enter.index("nfl2k5_presentation_match_lifecycle(1)") < generated_enter.index("loc_00064C82: ;")
    generated_exit = period.function(generated_source, "sub_000645D0", "void ")
    assert generated_exit.count(exit_hook[2]) == 1
    fixture = r'''
#include <algorithm>
#include <atomic>
#include <cmath>
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
static unsigned guest_reads=0,state_reads=0;
static bool gread(uint32_t va,void *out,size_t n) {
    ++guest_reads;
    if(n%4) return false;
    for(size_t i=0;i<n;i+=4) {
        auto it=words.find(va+(uint32_t)i);
        if(it==words.end()) return false;
        memcpy((char*)out+i,&it->second,4);
    }
    return true;
}
static uint64_t fake_now=1000;
static uint64_t GetTickCount64(){ return fake_now; }
using DWORD=uint32_t;
struct PackageFixture { bool html=false,replay_wipes_ack=false; };
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};
static std::atomic<int> s_sel_anim{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
'''
    dependencies = states + context + "\n" + declarations + "\n" + "\n".join([
        period.function(source, "rd32", "static uint32_t "),
        period.function(source, "rdf", "static float "),
        period.function(source, "json_str", "static std::string "),
    ]) + "\nstatic GameState fake_game;\nstatic bool read_success=true;\n" + (
        "static bool read_state(GameState &g){ ++state_reads; g=fake_game; return read_success; }\n"
    ) + lifecycle_declarations + "\n" + lifecycle + "\n" + active + "\n" + pause + "\n" + in_match + "\n" + keepalive_source
    generated_fixture = r'''
// Execute the actual generated initialization branch with inert external
// calls. This proves failed64590 goes to64C88 without executing the hook;
// the hook itself remains the extracted production lifecycle function.
static uint32_t eax=0,ecx=0,esi=0,esp=0x1800000,g_ebp=0,g_seh_ebp=0;
static bool initialize_succeeds=false;
static unsigned initialized_calls=0,success_tail_calls=0,failure_tail_calls=0;
#define RECOMP_INTERP_GATE(address) ((void)0)
#define RECOMP_ABI_CALL(address,function) function()
#define PUSH32(stack,value) do { (stack)-=4; } while(0)
#define POP32(stack,value) do { (stack)+=4; } while(0)
#define MEM32(address) words[address]
#define TEST_Z(a,b) ((a)==0)
static void sub_00064590(){eax=initialize_succeeds?1:0;}
static void sub_00064710(){++initialized_calls;}
static void sub_00125700(){++success_tail_calls;}
static void sub_0006E400(){++failure_tail_calls;}
static void sub_000DCB20(){} static void sub_00072BC0(){} static void sub_00064620(){}
static void sub_00125C50(){} static void sub_000649C0(){} static void sub_000645D0(){}
static void sub_00128C60(){} static void sub_00127A70(){} static void sub_00064CD0(){}
'''
    cases = r'''
static constexpr uint32_t record=0x00AFA07Cu,phaseva=0x00AFA078u;
static constexpr uint32_t stack=0x00A84B18u,top=0x00A84C18u,descriptor=0x004E3F38u;
static HtmlContext cx{false,false,false,false};
static void putf(uint32_t va,float value){ uint32_t u;memcpy(&u,&value,4);words[va]=u; }
static bool closef(float a,float b){ return std::isfinite(a)&&fabsf(a-b)<0.00002f; }
static void setup(bool returning=false) {
    s_replay_owner={};s_popup_events.clear();words.clear();
    s_sel_pkg=1;s_sel_anim=1;s_pregame_host_generation=7;s_replay_host_package=1;
    s_pkgs[1]={true,true};s_pkgs[2]={true,true};
    fake_game={};fake_game.valid=true;fake_game.period=2;fake_game.flow_state=11;
    fake_game.t[0].abbr="KC";fake_game.t[1].abbr="DAL";
    read_success=true;cx={false,false,false,false};fake_now+=10000;
    words[phaseva]=returning?4:1;words[top]=1;words[stack]=0x004E7EC0u;words[stack+8]=descriptor;
    words[0x00E6028Cu]=0x01234500u;words[k_team_obj[0]+0x1Cu]=0x08123400u;words[k_team_obj[1]+0x1Cu]=0x08134500u;
    words[0x00AC7444u]=0;words[0x00B38C30u]=0;
    words[record]=0x81B21020u;words[record+4]=0x81B22020u;
    words[record+0x20]=0x0008B1E0u;words[record+0x24]=0x0008B110u;
    putf(record+0x10,1.5f);putf(record+0x14,1.667f);putf(record+0x18,1.0f);putf(record+0x1C,0.0f);
}
static std::string ack(const std::string &action,const std::string &token="") {
    return "NFL2K5_REPLAY_"+action+":"+(token.empty()?std::to_string(s_replay_owner.token):token);
}
static void page(const std::string &message,int package=1,uint32_t generation=7) {
    unsigned before=guest_reads,state_before=state_reads;
    replay_page_message(package,generation,message);
    CHECK(guest_reads==before);CHECK(state_reads==state_before);
}
static PendingPopupEvent prepare(bool returning=false) {
    replay_begin(returning?1u:0u);
    CHECK(s_replay_owner.active);CHECK(!s_replay_owner.delivered);
    CHECK(!s_replay_owner.ready&&!s_replay_owner.painted&&!s_replay_owner.covered&&!s_replay_owner.handed);
    CHECK(s_popup_events.size()==1&&s_popup_events.back().name=="NATIVE_REPLAY_PREPARE");
    CHECK(s_popup_events.back().extra.find("\"source\":\"native_automatic_replay\"")!=std::string::npos);
    CHECK(s_popup_events.back().extra.find(returning?"\"direction\":\"out\"":"\"direction\":\"in\"")!=std::string::npos);
    CHECK(nfl2k5_presentation_replay_draw_replaced()==0);
    return s_popup_events.back();
}
static PendingPopupEvent deliver(bool returning=false) {
    auto ev=prepare(returning);
    CHECK(replay_event_current(ev,fake_game));CHECK(s_replay_owner.delivered);
    return ev;
}
static void paint(bool returning=false) {
    deliver(returning);
    page(ack("READY"));CHECK(s_replay_owner.ready);
    CHECK(nfl2k5_presentation_replay_delta(.016f)==0);
    CHECK(s_replay_owner.start_sent&&s_popup_events.back().name=="NATIVE_REPLAY_START");
    page(ack("START"));CHECK(s_replay_owner.painted);
    CHECK(nfl2k5_presentation_replay_draw_replaced()==1);
}
static void test_cycle(bool returning) {
    setup(returning);auto ev=prepare(returning);
    page(ack("READY"));CHECK(!s_replay_owner.ready); // cannot acknowledge undelivered request
    CHECK(replay_event_current(ev,fake_game));
    page(ack("COVER"));CHECK(!s_replay_owner.covered);
    page(ack("DONE"));CHECK(!s_replay_owner.done);
    page(ack("START"));CHECK(!s_replay_owner.painted); // native must send START first
    page(ack("READY"));CHECK(s_replay_owner.ready);
    CHECK(nfl2k5_presentation_replay_delta(.016f)==0);
    CHECK(s_replay_owner.start_sent&&s_popup_events.back().name=="NATIVE_REPLAY_START");
    const size_t started_events=s_popup_events.size();
    CHECK(nfl2k5_presentation_replay_delta(.016f)==0);CHECK(s_popup_events.size()==started_events);
    page(ack("START"));CHECK(s_replay_owner.painted);CHECK(nfl2k5_presentation_replay_draw_replaced()==1);
    uint64_t painted=s_replay_owner.painted_at;
    fake_now+=returning?20:440;
    CHECK(closef(nfl2k5_presentation_replay_delta(.016f),.75f));
    fake_now=painted+(returning?40:880)+100;
    float midpoint=s_replay_owner.midpoint,end=s_replay_owner.end;
    float epsilon=std::min(.0001f,(end-midpoint)*.01f);
    CHECK(closef(nfl2k5_presentation_replay_delta(.016f),midpoint-epsilon));
    nfl2k5_presentation_replay_handoff();CHECK(!s_replay_owner.handed); // no COVER yet
    page(ack("COVER"));CHECK(s_replay_owner.covered);
    putf(record+0x1C,midpoint-epsilon);
    float delta=nfl2k5_presentation_replay_delta(.016f);
    CHECK(delta>0&&closef(delta,epsilon*2));
    putf(record+0x1C,midpoint+epsilon);words[phaseva]=returning?5:2;
    nfl2k5_presentation_replay_handoff();CHECK(s_replay_owner.handed);
    CHECK(s_popup_events.back().name=="NATIVE_REPLAY_HANDOFF");
    size_t event_count=s_popup_events.size();nfl2k5_presentation_replay_handoff();CHECK(s_popup_events.size()==event_count);
    fake_now=s_replay_owner.handed_at+(returning?505:85);
    CHECK(closef(nfl2k5_presentation_replay_delta(.016f),(end-midpoint)*.5f-epsilon));
    fake_now=s_replay_owner.handed_at+1050;
    putf(record+0x1C,end-epsilon);
    CHECK(closef(nfl2k5_presentation_replay_delta(.016f),0)); // cannot finish before page DONE
    page(ack("DONE"));CHECK(s_replay_owner.done);
    CHECK(closef(nfl2k5_presentation_replay_delta(.016f),epsilon*2));
    words[phaseva]=returning?0:3;
    CHECK(nfl2k5_presentation_replay_delta(.016f)==.016f);CHECK(!s_replay_owner.active);
    CHECK(s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");CHECK(!nfl2k5_presentation_replay_draw_replaced());
}
static void test_tokens_and_messages() {
    setup();auto original=deliver();std::string token=std::to_string(s_replay_owner.token);
    for(const std::string &suffix:std::vector<std::string>{"","0","00","0"+token,"+"+token,"-"+token,
        " "+token,token+" ",token+"x","4294967296","10000000000",std::to_string(s_replay_owner.token+1)}) {
        page("NFL2K5_REPLAY_READY:"+suffix);CHECK(!s_replay_owner.ready);
    }
    for(const std::string &message:std::vector<std::string>{"","READY:"+token,"NFL2K5_REPLAY_READY",
        "other:NFL2K5_REPLAY_READY:"+token,"NFL2K5_REPLAY_UNKNOWN:"+token}) {
        page(message);CHECK(!s_replay_owner.ready&&!s_replay_owner.painted&&!s_replay_owner.covered&&!s_replay_owner.done);
    }
    page(ack("READY"),2);CHECK(!s_replay_owner.ready);
    page(ack("READY"),1,8);CHECK(!s_replay_owner.ready);
    s_sel_pkg=2;page(ack("READY"));CHECK(!s_replay_owner.ready);s_sel_pkg=1;
    s_replay_host_package=2;page(ack("READY"));CHECK(!s_replay_owner.ready);s_replay_host_package=1;
    s_pregame_host_generation=8;page(ack("READY"));CHECK(!s_replay_owner.ready);s_pregame_host_generation=7;
    page(ack("READY"));CHECK(s_replay_owner.ready);
    auto wrong=original;wrong.replay_token++;CHECK(!replay_event_current(wrong,fake_game));
    wrong=original;wrong.replay_generation++;CHECK(!replay_event_current(wrong,fake_game));
    wrong=original;wrong.replay_package=2;CHECK(!replay_event_current(wrong,fake_game));
    PendingPopupEvent unrelated;CHECK(replay_event_current(unrelated,fake_game));
    replay_release("test");CHECK(!s_replay_owner.active);
    page("NFL2K5_REPLAY_READY:"+token);CHECK(!s_replay_owner.ready);
    CHECK(replay_event_current(s_popup_events.back(),fake_game)); // delivered CANCEL survives cleared owner
    setup();s_replay_token=UINT32_MAX;prepare();CHECK(s_replay_owner.token==1); // no zero token after wrap
    setup();s_replay_token=UINT32_MAX-1;deliver();CHECK(s_replay_owner.token==UINT32_MAX);
    page("NFL2K5_REPLAY_READY:4294967295");CHECK(s_replay_owner.ready);
}
static void test_fallbacks() {
    setup();prepare();fake_now=s_replay_owner.created_at+750;
    CHECK(nfl2k5_presentation_replay_delta(.02f)==0&&s_replay_owner.active);
    fake_now++;CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f&&!s_replay_owner.active);
    CHECK(s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");CHECK(!nfl2k5_presentation_replay_draw_replaced());
    setup();paint();fake_now=s_replay_owner.created_at+4500;
    CHECK(nfl2k5_presentation_replay_draw_replaced()==1);nfl2k5_presentation_replay_delta(.02f);CHECK(s_replay_owner.active);
    fake_now++;CHECK(!nfl2k5_presentation_replay_draw_replaced());
    CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f&&!s_replay_owner.active);
    setup();paint();page(ack("CANCEL"));CHECK(s_replay_owner.failed);
    CHECK(!nfl2k5_presentation_replay_draw_replaced());CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f&&!s_replay_owner.active);
    setup();paint();read_success=false;CHECK(!nfl2k5_presentation_replay_draw_replaced());CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f);
    setup();CHECK(nfl2k5_presentation_replay_delta(-1)==-1);CHECK(std::isnan(nfl2k5_presentation_replay_delta(NAN)));
    CHECK(!s_replay_owner.active);CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f);
}
static void test_record_and_package_guards() {
    for(int mismatch=0;mismatch<26;++mismatch) {
        setup();paint();
        switch(mismatch) {
            case 0: words[0x00E6028Cu]++;break;
            case 1: words[k_team_obj[0]+0x1C]++;break;
            case 2: words[k_team_obj[1]+0x1C]++;break;
            case 3: words[record]++;break;
            case 4: words[record+4]++;break;
            case 5: words[record+0x20]++;break;
            case 6: words[record+0x24]++;break;
            case 7: putf(record+0x10,1.6f);break;
            case 8: putf(record+0x14,1.9f);break;
            case 9: putf(record+0x18,2);break;
            case 10: words[0x00AC7444u]=1;break;
            case 11: words[0x00B38C30u]=1;break;
            case 12: words[phaseva]=3;break;
            case 13: words[stack+8]=0x004E9078u;break;
            case 14: words[top]=0;break;
            case 15: words[top]=32;break;
            case 16: fake_game.valid=false;break;
            case 17: s_sel_pkg=2;break;
            case 18: s_replay_host_package=2;break;
            case 19: s_pregame_host_generation=8;break;
            case 20: s_pkgs[1].html=false;break;
            case 21: s_pkgs[1].replay_wipes_ack=false;break;
            case 22: s_sel_pkg=0;break;
            case 23: s_sel_pkg=3;break;
            case 24: words[phaseva]=4;break; // cannot borrow entrance ownership for return
            case 25: s_sel_anim=0;break; // Presentation > Animations OFF preserves native presentation
        }
        CHECK(!nfl2k5_presentation_replay_draw_replaced());
        CHECK(nfl2k5_presentation_replay_delta(.016f)==.016f);CHECK(!s_replay_owner.active);
        CHECK(s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");
    }
    for(int mismatch=0;mismatch<9;++mismatch) {
        setup();
        switch(mismatch) {
            case 0: putf(record+0x10,0);break;
            case 1: putf(record+0x14,1.5f);break;
            case 2: putf(record+0x14,10.1f);break;
            case 3: putf(record+0x18,0);break;
            case 4: putf(record+0x1C,.1f);break;
            case 5: putf(record+0x10,NAN);break;
            case 6: putf(record+0x14,INFINITY);break;
            case 7: putf(record+0x18,NAN);break;
            case 8: read_success=false;break;
        }
        replay_begin(0);CHECK(!s_replay_owner.active);CHECK(s_popup_events.empty());
    }
    setup();words[record]=0;words[record+4]=0;prepare();CHECK(s_replay_owner.resource==0&&s_replay_owner.scene==0);
    setup();paint();cx.paused=true;replay_hud_context(fake_game,cx);CHECK(!s_replay_owner.active);
    setup();paint();cx.play_selection=true;replay_hud_context(fake_game,cx);CHECK(!s_replay_owner.active);
    setup();paint();fake_game.valid=false;replay_hud_context(fake_game,cx);CHECK(!s_replay_owner.active);
    setup();paint();auto old=s_replay_owner.token;putf(record+0x1C,0);replay_begin(0);
    CHECK(s_replay_owner.active&&s_replay_owner.token!=old);CHECK(!s_replay_owner.painted);
    CHECK(s_popup_events[s_popup_events.size()-2].name=="NATIVE_REPLAY_CANCEL");
}
static void test_keepalive_and_stack() {
    setup();CHECK(native_replay_active(fake_game));CHECK(actual_match_keepalive(fake_game,native_replay_active(fake_game)));
    words[stack]=descriptor;words[stack+8]=0x004E7EC0u;CHECK(native_replay_active(fake_game)); // active ancestor
    words[stack]=0x004E7EC0u;words[stack+8]=descriptor;CHECK(native_replay_active(fake_game)); // exact top index
    words[top]=2;words[stack+8]=0x004E7EC0u;words[stack+16]=descriptor;CHECK(native_replay_active(fake_game));
    words[stack+16]=0x004E7EC0u;CHECK(!native_replay_active(fake_game)); // raw state survived screen pop
    CHECK(!actual_match_keepalive(fake_game,native_replay_active(fake_game)));
    words[stack+24]=descriptor;CHECK(!native_replay_active(fake_game)); // stale descriptor beyond active top
    words[top]=31;words[stack+31*8]=descriptor;CHECK(native_replay_active(fake_game));
    words[top]=32;CHECK(!native_replay_active(fake_game));
    words[top]=0;CHECK(!native_replay_active(fake_game));
    setup();for(uint32_t phase:{0u,6u,7u,UINT32_MAX}){words[phaseva]=phase;CHECK(!native_replay_active(fake_game));}
    for(uint32_t phase=1;phase<=5;++phase){words[phaseva]=phase;CHECK(native_replay_active(fake_game));}
    fake_game.valid=false;CHECK(!native_replay_active(fake_game));CHECK(!actual_match_keepalive(fake_game,true));
}
static void seed_without_page(bool returning=false) {
    setup(returning);s_replay_host_package=-1;
    replay_begin(returning?1u:0u);
    CHECK(s_replay_owner.active&&s_replay_owner.awaiting_page);
    CHECK(!s_replay_owner.delivered&&!s_replay_owner.ready&&!s_replay_owner.painted);
    CHECK(!s_replay_owner.start_sent&&!s_replay_owner.covered&&!s_replay_owner.handed&&!s_replay_owner.failed);
    CHECK(s_popup_events.empty()); // no page to receive PREPARE yet
    CHECK(s_replay_owner.clock==words[0x00E6028Cu]);
    CHECK(s_replay_owner.roster[0]==words[k_team_obj[0]+0x1C]);
    CHECK(s_replay_owner.roster[1]==words[k_team_obj[1]+0x1C]);
    CHECK(s_replay_owner.resource==words[record]&&s_replay_owner.scene==words[record+4]);
    CHECK(closef(s_replay_owner.midpoint,1.5f)&&closef(s_replay_owner.end,1.667f));
    CHECK(!nfl2k5_presentation_replay_draw_replaced());
    CHECK(nfl2k5_presentation_replay_delta(.016f)==0);
    CHECK(s_popup_events.empty());
}
static void test_awaiting_page_seed() {
    for(bool returning:{false,true}) {
        seed_without_page(returning);
        const NativeReplayOwner seed=s_replay_owner;
        unsigned reads=guest_reads,state_before=state_reads;
        replay_page_reset(); // initial html_hud startup stops/resets old host
        CHECK(guest_reads==reads&&state_reads==state_before); // no guest access on reset/UI boundary
        CHECK(s_replay_host_package==-1);
        CHECK(s_replay_owner.active&&s_replay_owner.awaiting_page&&s_replay_owner.token==seed.token);
        CHECK(s_popup_events.empty());CHECK(!nfl2k5_presentation_replay_draw_replaced());
        s_pregame_host_generation=8;replay_page_reset();
        CHECK(s_replay_owner.token==seed.token&&s_replay_owner.awaiting_page);
        s_replay_host_package=2;replay_hud_context(fake_game,cx);
        CHECK(s_replay_owner.awaiting_page&&s_popup_events.empty()); // unrelated page cannot bind
        s_replay_host_package=1;s_pregame_host_generation=9;
        replay_hud_context(fake_game,cx);
        CHECK(s_replay_owner.active&&!s_replay_owner.awaiting_page);
        CHECK(s_replay_owner.generation==9&&s_replay_owner.token==seed.token);
        CHECK(s_popup_events.size()==1&&s_popup_events.back().name=="NATIVE_REPLAY_PREPARE");
        CHECK(s_popup_events.back().replay_generation==9&&s_popup_events.back().replay_package==1);
        CHECK(s_popup_events.back().extra.find(returning?"\"direction\":\"out\"":"\"direction\":\"in\"")!=std::string::npos);
        replay_hud_context(fake_game,cx);CHECK(s_popup_events.size()==1); // bind once
        CHECK(!nfl2k5_presentation_replay_draw_replaced());
        page(ack("READY"),1,9);CHECK(!s_replay_owner.ready); // PREPARE not delivered
        CHECK(replay_event_current(s_popup_events.back(),fake_game));
        page(ack("READY"),1,8);CHECK(!s_replay_owner.ready); // stale initial generation
        page(ack("READY"),1,9);CHECK(s_replay_owner.ready);
        CHECK(nfl2k5_presentation_replay_delta(.016f)==0);
        CHECK(s_popup_events.back().name=="NATIVE_REPLAY_START");
        page(ack("START"),1,9);CHECK(s_replay_owner.painted);
        CHECK(nfl2k5_presentation_replay_draw_replaced());
        replay_page_reset();CHECK(!s_replay_owner.active);CHECK(!nfl2k5_presentation_replay_draw_replaced());
        CHECK(s_replay_host_package==-1); // painted ownership must never survive page loss
    }
    seed_without_page();fake_now=s_replay_owner.created_at+750;replay_page_reset();
    CHECK(s_replay_owner.active&&s_replay_owner.awaiting_page);CHECK(s_popup_events.empty());
    CHECK(nfl2k5_presentation_replay_delta(.02f)==0);CHECK(!nfl2k5_presentation_replay_draw_replaced());
    fake_now++;CHECK(nfl2k5_presentation_replay_delta(.02f)==.02f);CHECK(!s_replay_owner.active);
    CHECK(s_popup_events.size()==1&&s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");
    s_replay_host_package=1;replay_hud_context(fake_game,cx);
    CHECK(s_popup_events.size()==1); // expired seed cannot issue a late PREPARE
    seed_without_page();fake_now=s_replay_owner.created_at+751;replay_page_reset();
    CHECK(!s_replay_owner.active&&s_popup_events.empty()); // reset expiry is quiet native fallback
    seed_without_page();s_sel_pkg=2;replay_page_reset();CHECK(!s_replay_owner.active&&s_popup_events.empty());
    setup();prepare();replay_page_reset();CHECK(!s_replay_owner.active); // existing unpainted page owner also expires
    setup();paint();replay_page_reset();CHECK(!s_replay_owner.active);CHECK(!nfl2k5_presentation_replay_draw_replaced());
    for(int mismatch=0;mismatch<16;++mismatch) {
        seed_without_page();
        switch(mismatch) {
            case 0: words[0x00E6028Cu]++;break;
            case 1: words[k_team_obj[0]+0x1C]++;break;
            case 2: words[k_team_obj[1]+0x1C]++;break;
            case 3: words[record]++;break;
            case 4: words[record+4]++;break;
            case 5: words[phaseva]=3;break;
            case 6: words[stack+8]=0x004E9078u;break;
            case 7: words[0x00AC7444u]=1;break;
            case 8: words[0x00B38C30u]=1;break;
            case 9: fake_game.valid=false;break;
            case 10: s_sel_pkg=2;break;
            case 11: s_sel_anim=0;break;
            case 12: s_pkgs[1].html=false;break;
            case 13: s_pkgs[1].replay_wipes_ack=false;break;
            case 14: cx.paused=true;break;
            case 15: cx.play_selection=true;break;
        }
        s_replay_host_package=1;s_pregame_host_generation=8;
        replay_hud_context(fake_game,cx);
        CHECK(!s_replay_owner.active);CHECK(!nfl2k5_presentation_replay_draw_replaced());
        CHECK(s_popup_events.size()==1&&s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");
        CHECK(s_popup_events.back().name!="NATIVE_REPLAY_PREPARE");
    }
    seed_without_page();s_replay_host_package=1;s_pregame_host_generation=8;
    replay_hud_context(fake_game,cx);CHECK(!s_replay_owner.awaiting_page);
    CHECK(replay_event_current(s_popup_events.back(),fake_game));
    page(ack("CANCEL"),1,8);CHECK(s_replay_owner.failed);
    CHECK(nfl2k5_presentation_replay_delta(.016f)==.016f&&!s_replay_owner.active);
    CHECK(!nfl2k5_presentation_replay_draw_replaced()); // page load/runtime failure before START
}
static void test_authoritative_match_lifecycle() {
    setup();s_native_match_lifecycle=0;s_match_tick=0;
    CHECK(!actual_match_keepalive(fake_game,false)); // valid stale globals alone are insufficient
    CHECK(!actual_match_keepalive(fake_game,false,false));
    unsigned reads=guest_reads,state_before=state_reads;
    nfl2k5_presentation_match_lifecycle(1);
    CHECK(s_native_match_lifecycle==1&&s_match_tick==0);
    CHECK(guest_reads==reads&&state_reads==state_before); // callback copies no guest data
    CHECK(actual_match_keepalive(fake_game,false,s_native_match_lifecycle==1));
    CHECK(!native_replay_active(GameState{}));
    for(unsigned quiet_ms:{0u,2501u,10000u,60000u,120000u}) {
        fake_now+=quiet_ms;actual_match_activity(fake_game,false);
        CHECK(s_match_tick==(DWORD)fake_now); // no clocks/cards/replay needed during actual match
    }
    reads=guest_reads;state_before=state_reads;
    nfl2k5_presentation_match_lifecycle(0);
    CHECK(s_native_match_lifecycle==2&&s_match_tick==0);
    CHECK(guest_reads==reads&&state_reads==state_before);
    CHECK(!actual_match_keepalive(fake_game,false,s_native_match_lifecycle==1));
    actual_match_activity(fake_game,false);CHECK(s_match_tick==0);
    actual_match_activity(fake_game,true);CHECK(s_match_tick==0); // stale replay cannot resurrect an exited match
    nfl2k5_presentation_match_lifecycle(1);CHECK(s_native_match_lifecycle==1&&s_match_tick==0);
    actual_match_activity(fake_game,false);CHECK(s_match_tick==(DWORD)fake_now);
    fake_game.valid=false;actual_match_activity(fake_game,true);CHECK(s_match_tick==0);
    fake_game.valid=true;
    for(int prior:{0,2}) {
        s_native_match_lifecycle=prior;s_match_tick=123;initialize_succeeds=false;
        initialized_calls=success_tail_calls=failure_tail_calls=0;
        sub_00064C70();
        CHECK(s_native_match_lifecycle==prior&&s_match_tick==123);
        CHECK(initialized_calls==0&&success_tail_calls==0&&failure_tail_calls==1);
    }
    s_native_match_lifecycle=0;s_match_tick=123;initialize_succeeds=true;
    initialized_calls=success_tail_calls=failure_tail_calls=0;
    sub_00064C70();
    CHECK(s_native_match_lifecycle==1&&s_match_tick==123);
    CHECK(initialized_calls==1&&success_tail_calls==1&&failure_tail_calls==0);
}
static void test_actual_pause_stack() {
    constexpr uint32_t paused=0x004E9078u,challenge=0x0052DC20u,game=0x004E7EC0u;
    setup();CHECK(!native_pause_active());CHECK(in_match()==1);
    // Legacy no-title navigation retains its old behavior independently.
    words[0x00AC7444u]=1;CHECK(native_pause_active());CHECK(in_match()==0);
    words[0x00AC7444u]=0;CHECK(!native_pause_active());
    // Inclusive index: a pause/reason menu at slot0 or the exact top is live.
    for(uint32_t menu:{paused,challenge}) {
        setup();words[top]=0;words[stack]=menu;
        CHECK(native_pause_active());unsigned states_before=state_reads;
        CHECK(in_match()==0);CHECK(state_reads==states_before);
        setup();words[stack+8]=menu;CHECK(native_pause_active());CHECK(in_match()==0);
        setup();words[top]=2;words[stack+8]=paused;words[stack+16]=menu;
        CHECK(native_pause_active());CHECK(in_match()==0);
        // A submenu remains paused because its pause ancestor remains live.
        words[stack+16]=0x005038C8u;CHECK(native_pause_active());CHECK(in_match()==0);
        // A popped descriptor must never survive in stale stack storage.
        words[top]=0;CHECK(!native_pause_active());CHECK(in_match()==1);
        words[stack+8]=menu;CHECK(!native_pause_active());
        words[top]=1;CHECK(native_pause_active());
        words[stack+8]=game;CHECK(!native_pause_active());CHECK(in_match()==1);
    }
    setup();words[top]=31;
    for(unsigned i=0;i<32;++i)words[stack+i*8]=game;
    words[stack+31*8]=paused;CHECK(native_pause_active());CHECK(in_match()==0);
    words[stack+31*8]=challenge;CHECK(native_pause_active());
    words[stack+31*8]=game;unsigned before=guest_reads;
    CHECK(!native_pause_active());CHECK(guest_reads-before==34); // legacy + top +32 slots
    for(uint32_t count:{32u,33u,1000u,UINT32_MAX}) {
        words[top]=count;words[stack]=paused;before=guest_reads;
        CHECK(!native_pause_active());CHECK(guest_reads-before==2); // no descriptor read out of bounds
    }
    // A real pause above an automatic replay invalidates ownership even when
    // callers have not yet refreshed HtmlContext.paused from the same helper.
    for(bool returning:{false,true})for(uint32_t menu:{paused,challenge}) {
        setup(returning);words[top]=2;words[stack+16]=menu;
        CHECK(native_replay_active(fake_game));CHECK(native_pause_active());
        replay_begin(returning?1u:0u);CHECK(!s_replay_owner.active);
        CHECK(s_popup_events.empty());CHECK(!nfl2k5_presentation_replay_draw_replaced());
        setup(returning);paint(returning);
        words[top]=2;words[stack+16]=menu;
        CHECK(native_replay_active(fake_game));CHECK(native_pause_active());
        CHECK(!nfl2k5_presentation_replay_draw_replaced());
        page(ack("CANCEL")); // page messages remain zero-guest-read even while paused
        CHECK(nfl2k5_presentation_replay_delta(.016f)==.016f);
        CHECK(!s_replay_owner.active&&s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");
        // Stale pause slot above top cannot cancel an otherwise valid replay.
        setup(returning);paint(returning);words[stack+16]=menu;
        CHECK(!native_pause_active());CHECK(nfl2k5_presentation_replay_draw_replaced());
    }
    // Awaiting-page seeds must not bind a new page after a nested real pause.
    seed_without_page();words[top]=2;words[stack+16]=paused;
    s_replay_host_package=1;s_pregame_host_generation=8;
    replay_hud_context(fake_game,cx);
    CHECK(!s_replay_owner.active&&s_popup_events.back().name=="NATIVE_REPLAY_CANCEL");
}
int main() {
    test_cycle(false);test_cycle(true);test_tokens_and_messages();test_fallbacks();
    test_record_and_package_guards();test_keepalive_and_stack();test_awaiting_page_seed();test_authoritative_match_lifecycle();
    test_actual_pause_stack();
    printf("PASS %u actual native replay-owner assertions\n",checks);
}
'''
    output = ROOT / "outputs/presentation-audit/replay-owner-test"
    output.mkdir(parents=True, exist_ok=True)
    cpp, exe = output / "replay-owner.cpp", output / "replay-owner.exe"
    cpp.write_text(fixture + dependencies + owner + generated_fixture + generated_enter + cases, encoding="utf-8")
    compiler, environment = period.msvc_environment()
    command = [str(compiler), "/nologo", "/EHsc", "/std:c++17", "/Fe:" + str(exe),
               "/Fo:" + str(output / "replay-owner.obj"), str(cpp)]
    built = subprocess.run(command, env=environment, cwd=output, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "compile.log").write_text(built.stdout, encoding="utf-8")
    if built.returncode:
        print(built.stdout)
        raise SystemExit(built.returncode)
    result = subprocess.run([str(exe)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (output / "test.stdout.log").write_text(result.stdout, encoding="utf-8")
    (output / "test.stderr.log").write_text(result.stderr, encoding="utf-8")
    fingerprint = hashlib.sha256((owner + active + pause + in_match + keepalive_source + lifecycle_declarations + lifecycle + generated_enter + repr(lifecycle_hooks)).encode()).hexdigest()
    (output / "source.sha256").write_text(fingerprint + "\n", encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr)
        raise SystemExit(result.returncode)
    print("Actual extracted ownership source SHA-256: " + fingerprint)


if __name__ == "__main__":
    main()

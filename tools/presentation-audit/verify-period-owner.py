"""Compile and exercise the actual native period-board ownership functions.

This test extracts production function bodies, owner/state/event declarations,
and formatting helpers directly from src/nfl2k5_presentation.cpp. Only external
inputs (guest reads, host package choices, read_state and clock) are fixtures.
It does not start the game, compile generated code or touch build/Release.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/nfl2k5_presentation.cpp"


def function(source: str, name: str, prefix: str) -> str:
    match = re.search(re.escape(prefix + name) + r"\([^;]*?\)\s*\{", source, re.S)
    if not match:
        raise RuntimeError("Actual function definition missing: " + name)
    start, end, depth = match.start(), match.end(), 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def msvc_environment() -> tuple[Path, dict[str, str]]:
    roots = [Path("F:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"),
             Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC")]
    root = next((p for p in roots if p.is_dir()), None)
    if root is None:
        raise RuntimeError("Existing MSVC installation not found")
    version_key = lambda p: tuple(map(int, p.name.split(".")))
    msvc = max(root.iterdir(), key=version_key)
    sdk = next(p for p in [Path("F:/Program Files (x86)/Windows Kits/10"),
                           Path("C:/Program Files (x86)/Windows Kits/10")]
               if (p / "Include").is_dir())
    version = max((sdk / "Include").iterdir(), key=version_key).name
    env = os.environ.copy()
    env["INCLUDE"] = ";".join(str(p) for p in [msvc / "include", sdk / "Include" / version / "ucrt",
                                              sdk / "Include" / version / "um", sdk / "Include" / version / "shared"])
    env["LIB"] = ";".join(str(p) for p in [msvc / "lib/x64", sdk / "Lib" / version / "ucrt/x64",
                                          sdk / "Lib" / version / "um/x64"])
    return msvc / "bin/Hostx64/x64/cl.exe", env


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    owner_start = source.index("struct NativePeriodBoardOwner {")
    owner_end = source.index("/* Automatic replay:", owner_start)
    owner_source = source[owner_start:owner_end]
    text_scope_declaration = re.search(
        r"static thread_local std::vector<unsigned char> s_native_popup_text_scope;", source)
    if not text_scope_declaration:
        raise RuntimeError("Actual native text thread-local scope declaration missing")
    text_source = text_scope_declaration.group() + "\n" + "\n".join([
        function(source, "nfl2k5_presentation_native_text_begin", 'extern "C" void '),
        function(source, "nfl2k5_presentation_native_text_end", 'extern "C" void '),
        function(source, "nfl2k5_presentation_native_text_color", 'extern "C" uint32_t '),
    ])
    states = source[source.index("struct TeamState {"):source.index("static const uint32_t k_playbook_name")]
    pending_start = source.index("static std::mutex s_popup_lock;")
    pending_end = source.index("enum { PREGAME_COIN", pending_start)
    declarations = source[pending_start:pending_end]
    html_context = re.search(r"struct HtmlContext \{[^}]+\};", source).group()
    fixture = r'''
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
static std::atomic<unsigned> checks{0};
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static std::map<uint32_t,uint32_t> words;
static std::map<uint32_t,std::string> texts;
static unsigned guest_reads=0;
static bool gread(uint32_t va, void *out, size_t n) {
    ++guest_reads;
    auto found=words.find(va);
    if(n!=4 || found==words.end()) return false;
    memcpy(out,&found->second,4); return true;
}
static uint64_t fake_now=1000;
static uint64_t GetTickCount64(){ return fake_now; }
static std::string gtext(uint32_t va,int max) {
    ++guest_reads;
    auto found=texts.find(va);
    return found==texts.end()?std::string():found->second.substr(0,(size_t)max);
}
struct PackageFixture { bool html=false,replaces_period_boards=false,period_boards_ack=false; };
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
'''
    dependencies = (states + html_context + "\n" + declarations +
                    function(source, "rd32", "static uint32_t ") + "\n" +
                    function(source, "upper", "static std::string ") + "\n" +
                    function(source, "json_str", "static std::string ") + "\n" +
                    function(source, "is_final", "static bool ") + "\n" +
                    "static GameState fake_game;\nstatic bool read_success=true;\n"
                    "static bool read_state(GameState &g){ g=fake_game; return read_success; }\n")
    # Text-scoping tests control its one external ownership predicate. The
    # actual period owner is exercised separately above; the extracted text
    # helpers and their thread_local stack are not substituted or rewritten.
    text_dependency = r'''
extern "C" int nfl2k5_presentation_pregame_draw_replaced(uint32_t ctx) {
    return ctx==0x0A11CEu ? 1 : 0;
}
'''
    text_cases = r'''
static void test_text_scope() {
    constexpr uint32_t owned=0x0A11CEu,unowned=0x0BADu;
    constexpr uint32_t rgba=0xAB123456u,transparent=0x00123456u;
    CHECK(s_native_popup_text_scope.empty());
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
    nfl2k5_presentation_native_text_end();
    nfl2k5_presentation_native_text_end();
    CHECK(s_native_popup_text_scope.empty());
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
    nfl2k5_presentation_native_text_begin(unowned);
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
    nfl2k5_presentation_native_text_begin(owned);
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
    for(uint32_t color:{0xFFFFFFFFu,0x00010203u,0xFF000000u,0x7FABCDE0u,0x00808080u}) {
        uint32_t result=nfl2k5_presentation_native_text_color(color);
        CHECK((result & 0x00FFFFFFu)==(color & 0x00FFFFFFu));
        CHECK((result & 0xFF000000u)==0);
    }
    nfl2k5_presentation_native_text_begin(unowned);
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent); // inherited suppression
    nfl2k5_presentation_native_text_end();
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
    nfl2k5_presentation_native_text_end();
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba); // restore unowned parent
    nfl2k5_presentation_native_text_end();
    CHECK(s_native_popup_text_scope.empty());
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
    nfl2k5_presentation_native_text_begin(owned);
    nfl2k5_presentation_native_text_begin(owned);
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
    nfl2k5_presentation_native_text_end();
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
    nfl2k5_presentation_native_text_end();
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);

    // Keep four different scopes resident concurrently; no sleeps or spin
    // polling. Any process-global stack causes a visible assertion failure.
    std::mutex barrier_lock; std::condition_variable barrier;
    unsigned ready=0; bool release=false;
    nfl2k5_presentation_native_text_begin(owned);
    std::vector<std::thread> workers;
    for(unsigned mode=0;mode<4;++mode) workers.emplace_back([&,mode]{
        CHECK(s_native_popup_text_scope.empty());
        CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
        if(mode==0) nfl2k5_presentation_native_text_begin(owned);
        if(mode==1) nfl2k5_presentation_native_text_begin(unowned);
        if(mode==2) {
            nfl2k5_presentation_native_text_begin(unowned);
            nfl2k5_presentation_native_text_begin(owned);
        }
        if(mode==3) nfl2k5_presentation_native_text_end();
        const bool suppressed=mode==0||mode==2;
        CHECK(nfl2k5_presentation_native_text_color(rgba)==(suppressed?transparent:rgba));
        {
            std::unique_lock<std::mutex> lock(barrier_lock);
            ++ready; barrier.notify_all();
            barrier.wait(lock,[&]{return release;});
        }
        CHECK(nfl2k5_presentation_native_text_color(rgba)==(suppressed?transparent:rgba));
        if(mode==2) {
            nfl2k5_presentation_native_text_end();
            CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
        }
        nfl2k5_presentation_native_text_end();
        CHECK(s_native_popup_text_scope.empty());
        CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
    });
    {
        std::unique_lock<std::mutex> lock(barrier_lock);
        CHECK(barrier.wait_for(lock,std::chrono::seconds(3),[&]{return ready==4;}));
        CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
        release=true; barrier.notify_all();
    }
    for(auto &worker:workers) worker.join();
    CHECK(nfl2k5_presentation_native_text_color(rgba)==transparent);
    nfl2k5_presentation_native_text_end();
    CHECK(s_native_popup_text_scope.empty());
    CHECK(nfl2k5_presentation_native_text_color(rgba)==rgba);
}
'''
    cases = r'''
static constexpr uint32_t slot=0x00B62A70u;
static HtmlContext cx{false,false,false,false};
static void setup(int quarter=1) {
    s_period_board_owner=NativePeriodBoardOwner();
    s_popup_events.clear(); words.clear(); texts.clear();
    s_sel_pkg=1; s_pregame_host_generation=7;
    s_pkgs[1]={true,true,true}; s_pkgs[2]={true,true,true};
    s_period_board_host_package=1; s_period_board_page_permitted=1;
    fake_game=GameState(); fake_game.valid=true; fake_game.period=quarter;
    fake_game.flow_state=0x15; fake_game.t[0].abbr="SEA"; fake_game.t[1].abbr="DAL";
    words[0x00E6028Cu]=0x123400; words[0x00E602C4u]=quarter; words[0x00E602B8u]=0x15; words[0x123410u]=0;
    texts[0xE85A28u]="Final";texts[0xE85A34u]="1st";texts[0xE85A3Cu]="HALF";texts[0xE85A48u]="2nd";
    texts[0xE85A50u]="3rd";texts[0xE85A58u]="4th";texts[0xE85A60u]="OT";texts[0xE85A68u]="OT%d"; words[k_team_obj[0]+0x1C]=0x234500;
    words[k_team_obj[1]+0x1C]=0x345600;
    words[slot+4]=1; words[slot+0x48]=7; words[slot+0xC]=0;
    texts[slot+0x4C]=""; cx={false,false,false,false}; read_success=true;
    fake_now+=1000;
}
static PendingPopupEvent bind_and_deliver() {
    period_board_frame(slot);
    CHECK(s_period_board_owner.active);
    CHECK(s_period_board_owner.queued);
    CHECK(s_popup_events.size()==1);
    CHECK(s_popup_events[0].name=="PERIOD_BOARD_SHOW");
    PendingPopupEvent event=s_popup_events[0];
    CHECK(period_board_event_current(event,fake_game));
    CHECK(s_period_board_owner.delivered && !s_period_board_owner.queued);
    return event;
}
static std::string ack(const char *verb="SHOW") {
    return std::string("NFL2K5_PERIOD_BOARD_")+verb+":"+std::to_string(s_period_board_owner.token);
}
static void acknowledge() {
    unsigned before=guest_reads;
    period_board_page_message(1,7,ack());
    CHECK(guest_reads==before); // browser callback must never touch guest RAM
    CHECK(s_period_board_owner.acknowledged);
    CHECK(period_board_draw_replaced(slot)==1);
}
int main() {
    setup(); period_board_frame(slot);
    CHECK(s_period_board_owner.active && s_period_board_owner.queued);
    CHECK(!s_period_board_owner.delivered);
    CHECK(period_board_draw_replaced(slot)==0); // native survives before page paints
    period_board_page_message(1,7,ack());
    CHECK(!s_period_board_owner.acknowledged); // delivery is mandatory
    auto first=s_popup_events[0];
    CHECK(first.extra.find("\"nativePeriodToken\":\""+std::to_string(first.period_token)+"\"")!=std::string::npos);
    CHECK(first.extra.find("\"quarter\":1")!=std::string::npos);
    CHECK(period_board_event_current(first,fake_game));
    CHECK(period_board_draw_replaced(slot)==0); // delivery alone isn't paint ACK
    std::string token=std::to_string(first.period_token);
    std::vector<std::string> invalid={"", "0", "00", "0"+token, "+"+token, "-"+token,
        " "+token, token+" ", token+"x", "4294967296", "10000000000", std::to_string(first.period_token+1)};
    for(const auto &suffix:invalid) {
        unsigned before=guest_reads;
        period_board_page_message(1,7,"NFL2K5_PERIOD_BOARD_SHOW:"+suffix);
        CHECK(!s_period_board_owner.acknowledged);
        CHECK(guest_reads==before);
    }
    period_board_page_message(2,7,ack()); CHECK(!s_period_board_owner.acknowledged);
    period_board_page_message(1,8,ack()); CHECK(!s_period_board_owner.acknowledged);
    s_sel_pkg=2; period_board_page_message(1,7,ack()); CHECK(!s_period_board_owner.acknowledged); s_sel_pkg=1;
    s_period_board_page_permitted=0; period_board_page_message(1,7,ack()); CHECK(!s_period_board_owner.acknowledged);
    s_period_board_page_permitted=1;
    acknowledge();
    period_board_frame(slot); CHECK(s_popup_events.size()==1); // same owner is not redelivered
    period_board_page_message(1,7,ack("HIDE")); CHECK(period_board_draw_replaced(slot)==0);
    acknowledge();
    fake_game.period=2; period_board_frame(slot);
    CHECK(s_period_board_owner.quarter==1); CHECK(period_board_draw_replaced(slot)==1);
    period_board_event(s_period_board_owner,false);
    CHECK(s_popup_events.back().extra.find("\"quarter\":1")!=std::string::npos);
    CHECK(s_popup_events.back().extra.find("\"quarter\":2")==std::string::npos);

    setup(); auto event=bind_and_deliver();
    auto wrong=event; wrong.period_token++; CHECK(!period_board_event_current(wrong,fake_game));
    wrong=event; wrong.period_generation++; CHECK(!period_board_event_current(wrong,fake_game));
    wrong=event; wrong.period_package=2; CHECK(!period_board_event_current(wrong,fake_game));
    PendingPopupEvent unrelated; CHECK(period_board_event_current(unrelated,fake_game));
    acknowledge();
    uint32_t oldtoken=s_period_board_owner.token;
    period_board_slot_closed(0); CHECK(!s_period_board_owner.closing);
    period_board_slot_closed(1); words[slot+0xC]=1;
    CHECK(s_period_board_owner.closing); CHECK(s_popup_events.back().name=="PERIOD_BOARD_HIDE");
    CHECK(period_board_draw_replaced(slot)==1); // suppression through native close animation
    period_board_page_message(1,7,ack("HIDE"));
    CHECK(s_period_board_owner.acknowledged); CHECK(period_board_draw_replaced(slot)==1);
    size_t event_count=s_popup_events.size(); period_board_slot_closed(1);
    CHECK(s_popup_events.size()==event_count); // close can't enqueue duplicate hides
    words[slot+0xC]=0; CHECK(period_board_draw_replaced(slot)==0); // reopened slot cannot inherit old ACK
    period_board_frame(slot);
    CHECK(s_period_board_owner.active && s_period_board_owner.token!=oldtoken);
    CHECK(!s_period_board_owner.acknowledged);
    period_board_page_message(1,7,"NFL2K5_PERIOD_BOARD_SHOW:"+std::to_string(oldtoken));
    CHECK(!s_period_board_owner.acknowledged);
    CHECK(!period_board_event_current(event,fake_game)); // stale SHOW invalidated

    for(int quarter:{0,256,-1}) {
        setup(quarter); period_board_frame(slot);
        CHECK(!s_period_board_owner.active); CHECK(s_popup_events.empty()); CHECK(!period_board_draw_replaced(slot));
    }
    setup(3); bind_and_deliver(); acknowledge(); CHECK(s_period_board_owner.quarter==3);
    setup(); fake_game.flow_state=0x19;words[0xE602B8u]=0x19; bind_and_deliver();acknowledge();
    CHECK(s_period_board_owner.native_caption=="Final"&&s_period_board_owner.presentation_kind=="final");
    setup(3); fake_game.flow_state=0x0A;words[0xE602B8u]=0x0A;bind_and_deliver();acknowledge();
    CHECK(s_period_board_owner.native_caption=="Final");
    setup(); read_success=false; period_board_frame(slot); CHECK(!s_period_board_owner.active);
    setup(); period_board_frame(0x00B61B50u); CHECK(!s_period_board_owner.active);
    setup(); words[slot+0xC]=1; period_board_frame(slot); CHECK(!s_period_board_owner.active);
    setup(); words[k_team_obj[0]+0x1C]=0; period_board_frame(slot); CHECK(!s_period_board_owner.active);

    // Each mismatch must immediately reject the draw, then release through HUD service.
    for(int mismatch=0;mismatch<13;++mismatch) {
        setup(); bind_and_deliver(); acknowledge(); uint32_t previous=s_period_board_owner.token;
        switch(mismatch) {
            case 0: words[0x00E6028Cu]++; break;
            case 1: words[k_team_obj[0]+0x1C]++; break;
            case 2: words[k_team_obj[1]+0x1C]++; break;
            case 3: fake_game.t[0].abbr="KC"; break;
            case 4: words[slot+4]=0; break;
            case 5: words[slot+0x48]=28; break;
            case 6: texts[slot+0x4C]="Reused slot"; break;
            case 7: fake_game.valid=false; break;
            case 8: s_pregame_host_generation=8; break;
            case 9: s_pkgs[1].html=false; break;
            case 10: s_pkgs[1].replaces_period_boards=false; break;
            case 11: s_pkgs[1].period_boards_ack=false; break;
            case 12: s_period_board_host_package=2; break;
        }
        CHECK(period_board_draw_replaced(slot)==0);
        period_board_hud_context(fake_game,cx); CHECK(!s_period_board_owner.active);
        CHECK(s_popup_events.back().name=="PERIOD_BOARD_HIDE"); CHECK(s_popup_events.back().period_token==previous);
    }
    setup(); bind_and_deliver(); acknowledge();
    read_success=false; CHECK(!period_board_draw_replaced(slot)); read_success=true;
    CHECK(!period_board_draw_replaced(0x00B61B50u));
    cx.paused=true; period_board_hud_context(fake_game,cx); CHECK(!s_period_board_owner.active);
    setup(); bind_and_deliver(); acknowledge();
    cx.play_selection=true; period_board_hud_context(fake_game,cx); CHECK(!s_period_board_owner.active);
    setup(); bind_and_deliver(); acknowledge();
    fake_game.flow_state=0x0A; period_board_hud_context(fake_game,cx); CHECK(s_period_board_owner.active);
    CHECK(s_period_board_owner.native_caption=="1st"); // original owner is frozen, not relabelled Final
    setup(); bind_and_deliver(); acknowledge();
    uint32_t before=s_period_board_owner.token;
    period_board_release("wrong-expected",true,before+1); CHECK(s_period_board_owner.active);
    period_board_replacement_reset(); CHECK(!s_period_board_owner.active);
    CHECK(s_period_board_host_package==-1 && !s_period_board_page_permitted);
    for(int q:{1,2,3,4,5,6,7,255}) {
        setup(q);auto ev=bind_and_deliver();acknowledge();
        const auto &o=s_period_board_owner;
        CHECK(o.quarter==q);
        CHECK(o.presentation_kind==(q==2?"halftime":q>4?"overtime":"quarter_end"));
        std::string caption=q==1?"1st":q==2?"HALF":q==3?"3rd":q==4?"4th":q==5?"OT":"OT"+std::to_string(q-4);
        CHECK(o.native_caption==caption);
        CHECK(ev.extra.find("\"nativeCaption\":\""+caption+"\"")!=std::string::npos);
        fake_game.period=q+1;words[0xE602C4u]=q+1;period_board_frame(slot);CHECK(o.native_caption==caption);
    }
    for(uint32_t bits:{0u,0x80000000u,0x3DCCCCCDu,0xBF800000u,0x7FC00000u}) {
        setup(2);words[0x123410u]=bits;bind_and_deliver();acknowledge();
        CHECK(s_period_board_owner.native_caption==(bits==0||bits==0x80000000u?"HALF":"2nd"));
    }
    setup(2);words.erase(0x123410u);period_board_frame(slot);CHECK(!s_period_board_owner.active);
    for(uint32_t flow:{0x0Au,0x19u})for(int q:{1,2,4,5,7}) {
        setup(q);words[0xE602B8u]=flow;fake_game.flow_state=flow;bind_and_deliver();acknowledge();
        CHECK(s_period_board_owner.native_caption=="Final"&&s_period_board_owner.presentation_kind=="final");
    }
    setup(4);bind_and_deliver();CHECK(s_period_board_owner.native_caption=="4th");
    setup(2);texts[0xE85A3Cu]="Unknown";period_board_frame(slot);CHECK(!s_period_board_owner.active);
    setup(6);texts[0xE85A68u]="Unknown";period_board_frame(slot);CHECK(!s_period_board_owner.active);
    test_text_scope();
    printf("PASS %u actual native period-board and text-scope assertions\n",checks.load());
}
'''
    output = ROOT / "outputs/presentation-audit/period-owner-test"
    output.mkdir(parents=True, exist_ok=True)
    cpp, exe = output / "period-owner.cpp", output / "period-owner.exe"
    cpp.write_text(fixture + dependencies + owner_source + text_dependency + text_source + text_cases + cases,
                   encoding="utf-8")
    compiler, environment = msvc_environment()
    command = [str(compiler), "/nologo", "/EHsc", "/std:c++17", "/Fe:" + str(exe),
               "/Fo:" + str(output / "period-owner.obj"), str(cpp)]
    compile_result = subprocess.run(command, env=environment, cwd=output, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "compile.log").write_text(compile_result.stdout, encoding="utf-8")
    if compile_result.returncode:
        print(compile_result.stdout)
        raise SystemExit(compile_result.returncode)
    result = subprocess.run([str(exe)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (output / "test.stdout.log").write_text(result.stdout, encoding="utf-8")
    (output / "test.stderr.log").write_text(result.stderr, encoding="utf-8")
    fingerprint = hashlib.sha256((owner_source + text_source).encode()).hexdigest()
    (output / "source.sha256").write_text(fingerprint + "\n", encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr)
        raise SystemExit(result.returncode)
    print("Actual extracted ownership source SHA-256: " + fingerprint)


if __name__ == "__main__":
    main()

"""Compile the actual shared pregame page callback; never build/run the game."""
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('period', Path(__file__).with_name('verify-period-owner.py'))
period = importlib.util.module_from_spec(spec)
spec.loader.exec_module(period)
source = (ROOT / 'src/nfl2k5_presentation.cpp').read_text(encoding='utf-8')
callback = period.function(source, 'pregame_page_message', 'static void ')
ready = period.function(source, 'native_coin_show_ready', 'static bool ')
show = period.function(source, 'nfl2k5_presentation_coin_toss', 'extern "C" void ')
assert show.index('!native_coin_show_ready(ctx, slot)') < show.index('s_coin_toss_native_active = 1;')
precedence = period.function(source, 'player_card_precedes_fact', 'static bool ')
assert 'player_card_precedes_fact(ctx)' in period.function(source, 'nfl2k5_presentation_player_fact', 'extern "C" void ')
declaration = re.search(r'struct NativePregameOwner \{.*?\n\};', source, re.S).group()
fixture = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <map>
#include <string>
#include <vector>
static bool presentation_trace_enabled(){return false;}
static uint64_t GetTickCount64(){return 0;}
static std::atomic<int> s_sel_pkg{1},s_pregame_host_package{1},s_pregame_page_permitted{1};
static std::atomic<uint32_t> s_pregame_host_generation{7};
static std::mutex s_pregame_owner_lock;
static std::map<uint32_t,uint32_t> ram;
static unsigned guest_reads=0;
static uint32_t rd32(uint32_t address){++guest_reads;return ram[address];}
struct PreferredPlayerOwner {bool pending=false,bound=false,closing=false;uint32_t ctx=0;};
static PreferredPlayerOwner s_player_card_owner;
static std::mutex s_player_card_lock;
static bool preferred_package_current=true;
static bool player_card_package_current(const PreferredPlayerOwner&){return preferred_package_current;}
'''
cases = r'''
static NativePregameOwner s_pregame_owner[2];
'''
tests = r'''
static int checks=0;
static void check(bool yes){++checks;if(!yes){fprintf(stderr,"check %d failed\n",checks);exit(1);}}
static void reset(){
 s_sel_pkg=1;s_pregame_host_package=1;s_pregame_page_permitted=1;s_pregame_host_generation=7;
 for(int i=0;i<2;++i){s_pregame_owner[i]={};auto&o=s_pregame_owner[i];o.active=true;o.token=17+i;o.package=1;o.generation=7;}
}
int main(){
 for(unsigned slot=0;slot<2;++slot){
  ram.clear();unsigned ctx=0xB61B50+slot*0xF20;
  ram[ctx]=slot;ram[ctx+4]=1;ram[ctx+0x48]=14;ram[ctx+0x1D8]=5;ram[ctx+0xF1C]=1;
  check(native_coin_show_ready(ctx,slot));
  for(unsigned offset:{4u,0x48u,0x1D8u,0xF1Cu}){auto saved=ram[ctx+offset];ram[ctx+offset]=0;check(!native_coin_show_ready(ctx,slot));ram[ctx+offset]=saved;}
  ram[ctx+0xC]=1;check(!native_coin_show_ready(ctx,slot));ram[ctx+0xC]=0;
  check(!native_coin_show_ready(ctx,1-slot));
 }
 guest_reads=0;check(!native_coin_show_ready(0xC38268,-1));check(!native_coin_show_ready(0xC38268,0));check(!native_coin_show_ready(0xB61B50,2));check(guest_reads==0);
 guest_reads=0;
 for(const char *prefix:{"NFL2K5_PREGAME_","SNF_PREGAME_"}){
  reset();pregame_page_message(1,7,std::string(prefix)+"SHOW:17");check(s_pregame_owner[0].acknowledged);check(!s_pregame_owner[1].acknowledged);
  pregame_page_message(1,7,std::string(prefix)+"HIDE:17");check(!s_pregame_owner[0].acknowledged);
  pregame_page_message(1,7,std::string(prefix)+"SHOW:18");check(s_pregame_owner[1].acknowledged);
  s_pregame_owner[1].closing=true;pregame_page_message(1,7,std::string(prefix)+"HIDE:18");check(s_pregame_owner[1].acknowledged);
  for(const char *bad:{"0","00017","+17","-17"," 17","17 ","17x","17\n","4294967296","18446744073709551617",""}){
   reset();pregame_page_message(1,7,std::string(prefix)+"SHOW:"+bad);check(!s_pregame_owner[0].acknowledged);check(!s_pregame_owner[1].acknowledged);
  }
  reset();pregame_page_message(2,7,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  pregame_page_message(1,8,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  s_sel_pkg=2;pregame_page_message(1,7,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  reset();s_pregame_host_package=2;pregame_page_message(1,7,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  reset();s_pregame_page_permitted=0;pregame_page_message(1,7,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  reset();s_pregame_owner[0].active=false;pregame_page_message(1,7,std::string(prefix)+"SHOW:17");check(!s_pregame_owner[0].acknowledged);
  reset();s_pregame_owner[0].token=UINT32_MAX;pregame_page_message(1,7,std::string(prefix)+"SHOW:4294967295");check(s_pregame_owner[0].acknowledged);
 }
 s_player_card_owner={true,true,false,0xB62A70};
 check(player_card_precedes_fact(0xB62A70));check(!player_card_precedes_fact(0xB61B50));
 preferred_package_current=false;check(!player_card_precedes_fact(0xB62A70));preferred_package_current=true;
 s_player_card_owner.pending=false;check(!player_card_precedes_fact(0xB62A70));s_player_card_owner.pending=true;
 s_player_card_owner.bound=false;check(!player_card_precedes_fact(0xB62A70));s_player_card_owner.bound=true;
 s_player_card_owner.closing=true;check(!player_card_precedes_fact(0xB62A70));s_player_card_owner.closing=false;
 reset();pregame_page_message(1,7,"NFL2K5_NOTICE_SHOW:17");check(!s_pregame_owner[0].acknowledged);
 pregame_page_message(1,7,"NFL2K5_PREGAME_SHOW:18junk");check(!s_pregame_owner[1].acknowledged);
 check(guest_reads==0);
 printf("PASS %d actual pregame protocol assertions\n",checks);return 0;
}
'''
output = ROOT / 'outputs/presentation-audit/pregame-protocol-test'
output.mkdir(exist_ok=True, parents=True)
cpp, exe = output / 'pregame-protocol.cpp', output / 'pregame-protocol.exe'
cpp.write_text(fixture + declaration + cases + callback + ready + precedence + tests, encoding='utf-8')
compiler, environment = period.msvc_environment()
build = subprocess.run([str(compiler), '/nologo', '/EHsc', '/std:c++17', str(cpp), '/Fe:' + str(exe)], cwd=output, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(output / 'compile.log').write_text(build.stdout, encoding='utf-8')
if build.returncode:
    print(build.stdout)
    raise SystemExit(build.returncode)
result = subprocess.run([str(exe)], text=True, capture_output=True)
(output / 'test.log').write_text(result.stdout + result.stderr, encoding='utf-8')
print(result.stdout, end='')
if result.returncode:
    print(result.stderr)
    raise SystemExit(result.returncode)
(output / 'source.sha256').write_text(hashlib.sha256((callback+ready).encode()).hexdigest() + '\n')
assert 'rd32(' not in callback and 'gread(' not in callback and 'read_state(' not in callback
print('Shared CBS/legacy SNF callback has no guest/state reads.')

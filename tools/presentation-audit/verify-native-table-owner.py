"""Verify the production native team-table owner without running the game.

Compiles src/presentation/native_table.inc.cpp verbatim with host/RAM fixtures.
Also checks the owned XBE's draw-enabled catalog flags; a serialized request is
not counted as proof that layout0 or layout27 displayed a subtype table.
"""
from __future__ import annotations
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/nfl2k5_presentation.cpp'
MODULE=ROOT/'src/presentation/native_table.inc.cpp'
spec=importlib.util.spec_from_file_location('period_verifier',Path(__file__).with_name('verify-period-owner.py'))
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
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static std::map<uint32_t,uint32_t> words;
static std::map<uint32_t,std::string> texts;
static bool gread(uint32_t va,void *out,size_t n){
 ++guest_reads;if(n==2){auto t=texts.find(va);if(t!=texts.end()){uint16_t c=t->second.empty()?0:(unsigned char)t->second[0];memcpy(out,&c,2);return true;}}
 auto w=words.find(va);if(w==words.end()||(n!=2&&n!=4))return false;memcpy(out,&w->second,n);return true;
}
static std::string gtext(uint32_t va,int max){++guest_reads;auto t=texts.find(va);if(t==texts.end()||t->second.size()>=(size_t)max)return "";
 for(unsigned char c:t->second)if(c<32||c>126)return "";return t->second;}
static uint64_t GetTickCount64(){return 123456;}
static bool trace_enabled=false;
static bool presentation_trace_enabled(){return trace_enabled;}
static unsigned recordings=0;
static void xbox_PresentRecord(int seconds){CHECK(seconds==8);++recordings;}
struct PackageFixture{bool html=false,native_tables_ack=false;};
static std::vector<PackageFixture> s_pkgs(3);
static std::atomic<int> s_sel_pkg{1};static std::atomic<uint32_t> s_pregame_host_generation{7};
'''

CASES=r'''
static constexpr uint32_t base=0x00B61B50u;
static HtmlContext cx{};
static uint32_t ctx(int slot=0){return base+(uint32_t)slot*0xF20u;}
static uint32_t desc(uint32_t layout){return 0xAAD180u+layout*0x6Cu;}
static uint32_t address(int slot,int cell){return ctx(slot)+0x234u+(uint32_t)cell*0x88u;}
static void putcell(int slot,int i,const std::string &text,bool pointer=false){
 uint32_t a=address(slot,i);words[a]=pointer?1:0;words[a+4]=1;
 if(pointer){words[a+8]=0x500000u+(uint32_t)i*0x100u+(uint32_t)slot*0x10000u;texts[words[a+8]]=text;}
 else texts[a+8]=text;
}
static void instance(int slot=0,uint32_t layout=3,int subjects=1,int rowcount=3,bool reverse=false){
 uint32_t b=ctx(slot);words[b]=slot;words[b+4]=1;words[b+0xC]=0;words[b+0x48]=layout;words[b+0x228]=1;
 words[b+0x40]=subjects;words[b+0x22C]=subjects+1;words[b+0x230]=rowcount;words[b+0xEF4]=(subjects+1)*rowcount;
 words[b+0xF08]=0;words[b+0xF1C]=1;words[b+0x1D8]=5;words[b+0xF14]=0x101010;
 words[b+0x1DC]=0x200000u+(uint32_t)slot*0x100u;words[words[b+0x1DC]]=0x300000;
 if(layout==27){
  uint32_t resource=words[b+0x1DC],records=0x600000u+(uint32_t)slot*0x10000u;
  words[resource+4]=32;words[resource+8]=records;
  const uint32_t hashes[]={0xD59992C8u,0xC72C3D26u,0x7F905A43u,0x9AD8040Fu,0x886DABE1u,0x30D1CC84u,0x83C3354Eu,0x91769AA0u,0x29CAFDC5u,0xA8EE668Du,0xBA5BC963u,0x02E7AE06u,0xB1F557CCu,0xA340F822u,0x1BFC9F47u,0xAE7481A6u};
  for(uint32_t i=0;i<32;++i)words[records+i*0x90u+0x80u]=hashes[i/2];
 }
 texts[b+0x4C]="Through Regulation";texts[b+0xCC]="";
 for(int i=0;i<subjects;++i){words[b+0x10+i*8]=2;words[b+0x14+i*8]=words[k_team_obj[(i+(reverse?1:0))%2]+0x1C];}
 for(int i=0;i<(subjects+1)*rowcount;++i)putcell(slot,i,i%(subjects+1)==0?"Label "+std::to_string(i/(subjects+1)):std::to_string(i),i%3==1);
}
static void setup(int slot=0,uint32_t layout=3,int subjects=1,int rows=3,bool reverse=false){
 s_native_table_owner={};s_popup_events.clear();words.clear();texts.clear();s_sel_pkg=1;s_pregame_host_generation=7;
 s_native_table_host_package=1;s_native_table_page_permitted=1;s_pkgs[1]={true,true};s_pkgs[2]={true,true};
 fake_game={};fake_game.valid=true;fake_game.period=4;fake_game.phase=4;fake_game.t[0].abbr="PIT";fake_game.t[1].abbr="DAL";
 cx={};read_success=true;words[0xE6028C]=0x123400;words[k_team_obj[0]+0x1C]=0x400000;words[k_team_obj[1]+0x1C]=0x410000;
 words[0xBE5214]=0xAAD180;
 for(uint32_t l:{3,4,27}){words[desc(l)]=l==3?0xE861DC:l==4?0xE861F8:0xE8656C;words[desc(l)+4]=l==27?0xE85B20:0xE861A8;words[desc(l)+0x24]=l==27?0:1;words[desc(l)+0x30]=0x50D268;}
 instance(slot,layout,subjects,rows,reverse);
}
static std::string ack(const char *verb="SHOW",const std::string &t=""){
 return std::string("NFL2K5_NATIVE_TABLE_")+verb+":"+(t.empty()?std::to_string(s_native_table_owner.token):t);}
static void page(const std::string &msg,int pk=1,uint32_t gen=7){unsigned reads=guest_reads,states=state_reads;
 native_table_page_message(pk,gen,msg);CHECK(guest_reads==reads);CHECK(state_reads==states);}
static PendingPopupEvent deliver(){size_t n=s_popup_events.size();native_table_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n+1);
 auto ev=s_popup_events.back();CHECK(ev.name=="NATIVE_TABLE_SHOW"&&ev.table_token==s_native_table_owner.token);
 CHECK(native_table_event_current(ev,fake_game));CHECK(s_native_table_owner.delivered&&!s_native_table_owner.queued);return ev;}
static void bind_ack(int slot=0){native_table_frame(ctx(slot));CHECK(s_native_table_owner.active&&!s_native_table_owner.acknowledged);
 CHECK(!native_table_draw_replaced(ctx(slot)));deliver();page(ack());CHECK(native_table_draw_replaced(ctx(slot))==1);}
static void lifecycle(){
 for(int slot:{0,1})for(uint32_t layout:{3,4})for(int subjects:{1,2})for(bool reverse:{false,true}){
  setup(slot,layout,subjects,subjects==1?3:6,reverse);native_table_frame(ctx(slot));CHECK(s_native_table_owner.active&&s_native_table_owner.slot==slot);
  uint32_t token=s_native_table_owner.token;CHECK(s_popup_events.empty());page(ack());CHECK(!s_native_table_owner.acknowledged);
  auto ev=deliver();CHECK(ev.extra.find("\"nativeTableToken\":\""+std::to_string(token)+"\"")!=std::string::npos);
  CHECK(ev.extra.find("\"title\":\"Through Regulation\"")!=std::string::npos);CHECK(ev.extra.find("FINAL")==std::string::npos);
  std::string headers=subjects==1?(reverse?"[\"\",\"DAL\"]":"[\"\",\"PIT\"]"):(reverse?"[\"\",\"DAL\",\"PIT\"]":"[\"\",\"PIT\",\"DAL\"]");
  CHECK(ev.extra.find("\"headers\":"+headers)!=std::string::npos);CHECK(s_native_table_owner.cells.size()==(size_t)(subjects+1)*(subjects==1?3:6));
  CHECK(ev.extra.find(subjects==1?"[\"Label 0\",\"1\"]":"[\"Label 0\",\"1\",\"2\"]")!=std::string::npos);
  page(ack());CHECK(native_table_draw_replaced(ctx(slot)));CHECK(!native_table_draw_replaced(ctx(1-slot)));
  size_t n=s_popup_events.size();native_table_frame(ctx(slot));native_table_hud_context(fake_game,cx);CHECK(s_popup_events.size()==n);
  fake_game.period=5;fake_game.phase=2;fake_game.flow_state=0xA;native_table_hud_context(fake_game,cx);
  CHECK(s_native_table_owner.active&&s_native_table_owner.title=="Through Regulation");CHECK(native_table_draw_replaced(ctx(slot)));
  native_table_slot_closing(1-slot);CHECK(!s_native_table_owner.closing);words[ctx(slot)+0xC]=1;native_table_slot_closing(slot);
  CHECK(s_native_table_owner.closing&&s_popup_events.back().name=="NATIVE_TABLE_HIDE");CHECK(native_table_event_current(s_popup_events.back(),fake_game));
  page(ack("HIDE"));CHECK(s_native_table_owner.acknowledged&&native_table_draw_replaced(ctx(slot)));
  n=s_popup_events.size();native_table_slot_closing(slot);CHECK(s_popup_events.size()==n);
  words[ctx(slot)+4]=0;CHECK(!native_table_draw_replaced(ctx(slot)));native_table_hud_context(fake_game,cx);CHECK(!s_native_table_owner.active);
  page(ack("SHOW",std::to_string(token)));CHECK(!s_native_table_owner.active);
 }
}
static void boundaries(){
 setup(0,4,1,12);putcell(0,1,"",true);putcell(0,2,"",false);bind_ack();CHECK(s_native_table_owner.cells.size()==24);
 CHECK(s_native_table_owner.cells[1].text.empty()&&s_native_table_owner.cells[2].text.empty());
 setup(0,4,2,8);bind_ack();CHECK(s_native_table_owner.cells.size()==24);
 setup(0,27,2,5);bind_ack();CHECK(s_native_table_owner.cells.size()==15); // Real independent 3x5 scene path, +24=0.
 words[desc(27)+0x24]=1;CHECK(!native_table_draw_replaced(base)); // Unknown modified layout is not the proven scene.
 setup(0,0,2,6);words[base+0x1DC]=0;native_table_frame(base);CHECK(!s_native_table_owner.active); // A requested layout0 is not display proof.
}
static void scene_grid(){
 for(int slot:{0,1})for(bool reverse:{false,true}){
  setup(slot,27,2,5,reverse);bind_ack(slot);CHECK(native_table_draw_replaced(ctx(slot)));
  words[ctx(slot)+0xC]=1;native_table_slot_closing(slot);CHECK(native_table_draw_replaced(ctx(slot)));
 }
 for(uint32_t i=0;i<32;++i){
  setup(0,27,2,5);uint32_t resource=words[base+0x1DC],records=words[resource+8];words[records+i*0x90+0x80]++;
  native_table_frame(base);CHECK(!s_native_table_owner.active); // Every cell/title text pair is bound, not just one convenient hash.
 }
 for(int m=0;m<6;++m){
  setup(0,27,2,5);
  switch(m){case 0:words[words[base+0x1DC]+4]=31;break;case 1:words[words[base+0x1DC]+8]=0;break;
   case 2:words[desc(27)+0x30]++;break;case 3:words[base+0x230]=4;words[base+0xEF4]=12;break;
   case 4:words[base+0x230]=6;words[base+0xEF4]=18;break;case 5:words[base+0x40]=1;words[base+0x22C]=2;words[base+0xEF4]=10;break;}
  native_table_frame(base);CHECK(!s_native_table_owner.active);
 }
}
static void malformed(){
 for(int m=0;m<38;++m){setup();
  switch(m){
   case 0:words[base]=1;break;case 1:words[base+4]=0;break;case 2:words[base+0xC]=1;break;case 3:words[base+0x48]=1;break;
   case 4:words[base+0x228]=2;break;case 5:words[base+0x40]=0;break;case 6:words[base+0x40]=3;break;
   case 7:words[base+0x22C]=3;break;case 8:words[base+0x230]=0;break;case 9:words[base+0x230]=13;words[base+0xEF4]=26;break;
   case 10:words[base+0xEF4]=5;break;case 11:words[base+0x10]=1;break;case 12:words[base+0x10]=3;break;
   case 13:words[base+0x14]=k_team_obj[0];break;case 14:words[base+0x14]=1;break;case 15:words[k_team_obj[1]+0x1C]=words[k_team_obj[0]+0x1C];break;
   case 16:texts[base+0x4C]="";break;case 17:texts[base+0xCC]="Additional actual message";break;
   case 18:words[address(0,0)]=2;break;case 19:words[address(0,1)+8]=0;break;case 20:texts[words[address(0,1)+8]]=std::string(64,'X');break;
   case 21:texts[address(0,0)+8]="bad\nlabel";break;case 22:words[base+0xF08]=1;break;case 23:words[base+0xF1C]=0;break;
   case 24:words[base+0x1D8]=4;break;case 25:words[base+0x1DC]=0;break;case 26:words[words[base+0x1DC]]=0;break;
   case 27:words[desc(3)+0x24]=0;break;case 28:words[desc(3)]++;break;case 29:words[desc(3)+4]++;break;case 30:words[0xBE5214]++;break;
   case 31:words[0xE6028C]=0;break;case 32:s_native_table_host_package=2;break;case 33:s_pkgs[1].native_tables_ack=false;break;
   case 34:s_pkgs[1].html=false;break;case 35:fake_game.valid=false;break;case 36:read_success=false;break;case 37:fake_game.t[0].abbr="";break;
  }
  native_table_frame(base);CHECK(!s_native_table_owner.active&&s_popup_events.empty());CHECK(!native_table_draw_replaced(base));
 }
 setup(0,3,2,3);words[base+0x1C]=words[base+0x14];native_table_frame(base);CHECK(!s_native_table_owner.active); // duplicate subject
}
static void stale(){
 for(int m=0;m<18;++m){setup();bind_ack();
  switch(m){case 0:words[base+4]=0;break;case 1:texts[base+0x4C]="Final";break;case 2:texts[address(0,0)+8]="Changed";break;
   case 3:texts[words[address(0,1)+8]]="Changed";break;case 4:words[address(0,1)+8]++;break;case 5:words[address(0,0)+4]++;break;
   case 6:words[base+0xF14]++;break;case 7:words[base+0x1DC]++;break;case 8:words[words[base+0x1DC]]++;break;
   case 9:words[0xE6028C]++;break;case 10:words[k_team_obj[0]+0x1C]++;break;case 11:words[k_team_obj[1]+0x1C]++;break;
   case 12:fake_game.t[0].abbr="KC";break;case 13:s_pregame_host_generation++;break;case 14:s_sel_pkg=2;break;
   case 15:s_native_table_host_package=2;break;case 16:s_pkgs[1].native_tables_ack=false;break;case 17:words[desc(3)+0x24]=0;break;}
  CHECK(!native_table_draw_replaced(base));native_table_hud_context(fake_game,cx);CHECK(!s_native_table_owner.active);
 }
 setup();bind_ack();uint32_t old=s_native_table_owner.token;words[base+0xC]=1;native_table_slot_closing(0);words[base+0xC]=0;
 CHECK(!native_table_draw_replaced(base));native_table_frame(base);CHECK(s_native_table_owner.active&&s_native_table_owner.token!=old&&!s_native_table_owner.acknowledged);
 page(ack("SHOW",std::to_string(old)));CHECK(!s_native_table_owner.acknowledged);
 setup();bind_ack();cx.paused=true;native_table_hud_context(fake_game,cx);CHECK(!s_native_table_owner.active);
 setup();bind_ack();cx.play_selection=true;native_table_hud_context(fake_game,cx);CHECK(!s_native_table_owner.active);
 setup();bind_ack();native_table_release("wrong expected",true,s_native_table_owner.token+1);CHECK(s_native_table_owner.active);
 native_table_reset();CHECK(!s_native_table_owner.active&&s_native_table_host_package==-1&&!s_native_table_page_permitted);
 setup();bind_ack();instance(1);native_table_frame(ctx(1));CHECK(s_native_table_owner.ctx==base);
 setup();native_table_frame(base);native_table_hud_context(fake_game,cx);auto pending=s_popup_events.back();
 texts[address(0,0)+8]="Reused";CHECK(!native_table_event_current(pending,fake_game));page(ack());CHECK(!s_native_table_owner.acknowledged);
}
static void protocol(){
 setup();native_table_frame(base);deliver();std::string t=std::to_string(s_native_table_owner.token);
 for(const std::string value:std::vector<std::string>{"0","00","01","-1","+1","1x","1 ","4294967296","99999999999","0"+t}){
  page(ack("SHOW",value));CHECK(!s_native_table_owner.acknowledged);}
 page("NFL2K5_NATIVE_TABLE_SHOW:");CHECK(!s_native_table_owner.acknowledged);
 page(ack(),2);CHECK(!s_native_table_owner.acknowledged);page(ack(),1,8);CHECK(!s_native_table_owner.acknowledged);
 page(ack("SHOW",std::to_string(s_native_table_owner.token+1)));CHECK(!s_native_table_owner.acknowledged);
 page(ack());CHECK(s_native_table_owner.acknowledged);page(ack("HIDE"));CHECK(!s_native_table_owner.acknowledged);
 words[base+0xC]=1;native_table_slot_closing(0);page(ack());CHECK(!s_native_table_owner.acknowledged);
 PendingPopupEvent unrelated;CHECK(native_table_event_current(unrelated,fake_game));
}
static void hot_path_and_recording(){
 setup();s_pkgs[1].native_tables_ack=false;unsigned reads=guest_reads,states=state_reads;
 native_table_frame(base);CHECK(guest_reads==reads&&state_reads==states);
 setup();reads=guest_reads;states=state_reads;CHECK(!native_table_draw_replaced(base));CHECK(guest_reads==reads&&state_reads==states);
 native_table_frame(base);reads=guest_reads;states=state_reads;CHECK(!native_table_draw_replaced(base));CHECK(guest_reads==reads&&state_reads==states);
 deliver();page(ack());reads=guest_reads;states=state_reads;CHECK(!native_table_draw_replaced(ctx(1)));CHECK(guest_reads==reads&&state_reads==states);
 setup();_putenv_s("NFL2K5_REC","notice-test");trace_enabled=true;unsigned before=recordings;
 native_table_frame(base);CHECK(recordings==before+1);native_table_frame(base);CHECK(recordings==before+1);
 setup();trace_enabled=false;before=recordings;native_table_frame(base);CHECK(recordings==before);
 _putenv_s("NFL2K5_REC","");trace_enabled=false;
}
int main(){lifecycle();boundaries();scene_grid();malformed();stale();protocol();hot_path_and_recording();printf("PASS %u actual native team-table owner assertions\n",checks);}
'''

def main():
    source=SOURCE.read_text(encoding='utf-8');module=MODULE.read_text(encoding='utf-8')
    states=source[source.index('struct TeamState {'):source.index('static const uint32_t k_playbook_name')]
    context=re.search(r'struct HtmlContext \{[^}]+\};',source).group()
    queue='''
struct PendingPopupEvent{std::string name,extra;uint32_t table_token=0,table_generation=0;int table_package=-1;};
static std::mutex s_popup_lock;static std::vector<PendingPopupEvent> s_popup_events;
'''
    deps='\n'.join([states,context,queue,period.function(source,'rd32','static uint32_t '),period.function(source,'json_str','static std::string '),
        'static GameState fake_game;static bool read_success=true;',
        'static bool read_state(GameState &g){++state_reads;g=fake_game;return read_success;}'])
    assert not re.search(r'\b(gwrite\w*|RECOMP_ABI_CALL|RECOMP_ICALL_SAFE|MEM32)\b',module)
    ui=period.function(module,'native_table_page_message','static void ')
    assert not re.search(r'\b(gread|rd32|gtext|read_state)\s*\(',ui)
    sys.path.insert(0,str(ROOT/'tools'));from xbe_peek import Xbe
    x=Xbe();assert x.u32(0xAAD180+0x24)==0 and x.u32(0xAADCE4+0x24)==0
    for layout in (3,4):assert x.u32(0xAAD180+layout*0x6C+0x24)==1
    generated=(ROOT/'src/recomp/gen/recomp_0004.c').read_text(encoding='utf-8')
    add=period.function(generated,'sub_0008A2E0','void ')
    assert 'MEM32(ecx + eax * 8 + 0x10) = 2;' in add and 'MEM32(ecx + eax * 8 + 0x14) = edx;' in add
    # Independently verify every supported scene-text hash against the user's
    # original halftime.iff, in memory only. No asset is written or bundled.
    sys.path.insert(0,str(ROOT/'tools/team-creator'));from game_assets import Archive,T
    halftime=Archive(ROOT/'original/disc').read('halftime.iff')
    target=[]
    for chunk in T.parse_chunks(halftime,allow_trailing=True):
        decoded,_=T.decode_chunk(halftime,chunk)
        if chunk.kind=='MRKS' and 'halftime_team_stats'.encode('utf-16le') in decoded:target.append((chunk,decoded))
    assert len(target)==1
    chunk,decoded=target[0];u32=lambda at:struct.unpack_from('<I',decoded,at)[0]
    rel=lambda at:at+struct.unpack_from('<i',decoded,at)[0]-1
    wrapper=rel(0x14);assert u32(wrapper+4)==32
    array=rel(wrapper+8)
    actual=[u32(array+i*0x90+0x80) for i in range(32)]
    literal=re.search(r'static const uint32_t expected\[\] = \{([^}]+)\}',module).group(1)
    expected=[int(v,16) for v in re.findall(r'0x([A-Fa-f0-9]+)u',literal)]
    assert actual==[h for h in expected for _ in (0,1)]
    callbacks={}
    for i in range(100):
        hash_,fn,param=(x.u32(0x50D268+i*12+j*4) for j in range(3))
        if not hash_:break
        callbacks[hash_]=(fn,param)
    for i,hash_ in enumerate(expected):
        fn,param=callbacks[hash_]
        assert (fn,param)==((0x1C0AF0,0) if i==15 else (0x1C0CD0,((2-i%3)<<16)|(4-i//3)))
    output=ROOT/'outputs/presentation-audit/native-table-owner-test';output.mkdir(parents=True,exist_ok=True)
    cpp,exe=output/'native-table-owner.cpp',output/'native-table-owner.exe';cpp.write_text(FIXTURE+deps+module+CASES,encoding='utf-8')
    compiler,environment=period.msvc_environment()
    built=subprocess.run([str(compiler),'/nologo','/EHsc','/std:c++17','/Fe:'+str(exe),'/Fo:'+str(output/'native-table-owner.obj'),str(cpp)],
        env=environment,cwd=output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'compile.log').write_text(built.stdout,encoding='utf-8')
    if built.returncode:print(built.stdout);raise SystemExit(built.returncode)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (output/'test.stdout.log').write_text(result.stdout,encoding='utf-8');(output/'test.stderr.log').write_text(result.stderr,encoding='utf-8')
    digest=hashlib.sha256(module.encode()).hexdigest();(output/'source.sha256').write_text(digest+'\n',encoding='utf-8')
    print(result.stdout,end='')
    if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
    print('Actual included owner SHA-256: '+digest)
    print('Owned XBE/halftime.iff verify the independent 32-text-record layout27 grid; layout0 stays native. Browser guest reads are zero. No game launch/build.')

if __name__=='__main__':main()

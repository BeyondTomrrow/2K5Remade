"""Compile the actual host trigger helpers in isolation and test boundaries.

No substituted copy of the implementation, guest code, game process or build
directory. Uses the project's existing MSVC installation.
"""
import os
import pathlib
import re
import subprocess

root=pathlib.Path(__file__).resolve().parents[2]
source=(root/'src/nfl2k5_presentation.cpp').read_text()
def function(name,prefix='static bool '):
    match=re.search(re.escape(prefix+name)+r'\([^;]*?\)\s*\{',source,re.S)
    if not match: raise RuntimeError('Actual definition missing: '+name)
    start=match.start();brace=match.end()-1; depth=1; end=brace+1
    while depth:
        if source[end]=='{': depth+=1
        elif source[end]=='}': depth-=1
        end+=1
    return source[start:end]
out=root/'outputs/cbs-2026/live-trigger-test'
out.mkdir(parents=True,exist_ok=True)
cpp=out/'trigger.cpp';exe=out/'trigger.exe'
globals_start=source.index('struct LiveStatCandidate {')
globals_end=source.index('extern "C" int nfl2k5_broadcast_live_stats_enabled',globals_start)
states=source[source.index('struct TeamState {'):source.index('\nenum { AWAY')]
cpp.write_text('#include <cmath>\n#include <cassert>\n#include <algorithm>\n#include <cstring>\n#include <cstdio>\n#include <cstdlib>\n#include <cstdint>\n#include <map>\n#include <vector>\n#include <mutex>\n#include <string>\n#include "nfl2k5_broadcast.h"\nstatic uint64_t GetTickCount64() { return 0; }\n'+
    states+function('is_final')+'\n'+function('period_ending')+'\n'+
    source[globals_start:globals_end]+function('live_owner_equal')+'\n'+
    function('live_value')+'\n'+function('live_increased')+'\n'+
    function('nfl2k5_broadcast_live_player_sample','extern "C" void ')+r'''
int main() {
    Nfl2k5LivePlayerSample a{},b{}; int f=NFL2K5_LIVE_RECEPTIONS;
    assert(!live_increased(a,b,f));
    b.valid_fields=NFL2K5_LIVE_FIELD_BIT(f);b.values[f]=0;
    assert(!live_increased(a,b,f)); // availability alone is not a play
    b.values[f]=1;assert(live_increased(a,b,f)); // first real positive stat
    a=b;assert(!live_increased(a,b,f));
    b.values[f]=2;assert(live_increased(a,b,f));
    b.values[f]=0;assert(!live_increased(a,b,f)); // regression/reset
    b.valid_fields=0;assert(!live_increased(a,b,f));
    b.valid_fields=NFL2K5_LIVE_FIELD_BIT(f);b.values[f]=NAN;
    assert(!live_increased(a,b,f));
    f=NFL2K5_LIVE_SACKS;a={};b={};
    a.valid_fields=b.valid_fields=NFL2K5_LIVE_FIELD_BIT(f);
    a.values[f]=0;b.values[f]=0.5;assert(live_increased(a,b,f));
    a.values[f]=0.5;assert(!live_increased(a,b,f));
    b.values[f]=1;assert(live_increased(a,b,f));
    Nfl2k5LivePlayerSample p{};
    p.owner.clock_object=0x123;p.owner.sampled_at_ms=1000;p.player_record=0x567;
    p.native_position=9;strcpy(p.name,"Real TE");
    nfl2k5_broadcast_live_player_sample(&p);
    assert(s_live_candidates.empty()); // unknown baseline is not a fake play
    p.valid_fields=NFL2K5_LIVE_FIELD_BIT(NFL2K5_LIVE_RECEPTIONS);
    p.values[NFL2K5_LIVE_RECEPTIONS]=1;p.owner.sampled_at_ms=2000;
    nfl2k5_broadcast_live_player_sample(&p);
    assert(s_live_candidates.size()==1); // first positive TE stat is eligible
    assert(s_live_candidates[0].sample.values[NFL2K5_LIVE_RECEPTIONS]==1);
    p.values[NFL2K5_LIVE_RECEPTIONS]=99;
    assert(s_live_candidates[0].sample.values[NFL2K5_LIVE_RECEPTIONS]==1); // copied
    p.values[NFL2K5_LIVE_RECEPTIONS]=1;p.owner.sampled_at_ms=40000;
    nfl2k5_broadcast_live_player_sample(&p);
    assert(s_live_candidates[0].queued_at==2000); // refresh is not a new event
    p.values[NFL2K5_LIVE_RECEPTIONS]=2;p.owner.sampled_at_ms=41000;
    nfl2k5_broadcast_live_player_sample(&p);
    assert(s_live_candidates[0].queued_at==41000); // new play revives expired candidate
    assert(s_live_candidates.size()==1); // no duplicate cards for one player
    p.owner.scope=11;p.values[NFL2K5_LIVE_RECEPTIONS]=77;
    nfl2k5_broadcast_live_player_sample(&p);
    assert(s_live_candidates[0].sample.values[NFL2K5_LIVE_RECEPTIONS]==2); // season rejected
    GameState game;game.valid=true;game.period=4;game.clock=0;game.t[0].score=14;game.t[1].score=20;
    game.flow_state=0x0E;assert(!is_final(game)); // live last play
    game.flow_state=0x15;assert(!is_final(game)); // period presentation before PAT
    game.flow_state=0x0A;assert(is_final(game));
    game.t[1].score=14;assert(is_final(game)); // actual tie final
    game.period=5;game.clock=437;game.t[0].score=20;assert(is_final(game)); // sudden-death OT
    game.valid=false;assert(!is_final(game));
    game.valid=true;game.period=0;assert(!is_final(game)); // teardown/new pregame
    game.period=4;game.flow_state=-1;assert(!is_final(game));
    game.period=1;game.clock=0;game.flow_state=0x0E;assert(!period_ending(game));
    game.flow_state=0x15;assert(period_ending(game));
    game.period=2;game.flow_state=0x09;assert(period_ending(game));
    game.clock=300;assert(!period_ending(game));
    game.clock=0;game.flow_state=0x0A;assert(!period_ending(game));
    game.flow_state=0x15;game.valid=false;assert(!period_ending(game));
    game.valid=true;game.period=0;assert(!period_ending(game));
    return 0;
}
''')
vs=pathlib.Path('F:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC')
msvc=max(vs.iterdir(),key=lambda p:tuple(map(int,p.name.split('.'))))
sdks=[pathlib.Path(d) for d in ['F:/Program Files (x86)/Windows Kits/10','C:/Program Files (x86)/Windows Kits/10']]
sdk=next(d for d in sdks if (d/'Include').exists())
version=max((sdk/'Include').iterdir(),key=lambda p:tuple(map(int,p.name.split('.')))).name
env=os.environ.copy();env['INCLUDE']=';'.join(str(p) for p in [msvc/'include',sdk/'Include'/version/'ucrt',sdk/'Include'/version/'um',sdk/'Include'/version/'shared'])
env['LIB']=';'.join(str(p) for p in [msvc/'lib/x64',sdk/'Lib'/version/'ucrt/x64',sdk/'Lib'/version/'um/x64'])
subprocess.run([str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/std:c++17','/I'+str(root/'src'),'/Fe:'+str(exe),'/Fo:'+str(out/'trigger.obj'),str(cpp)],env=env,cwd=out,check=True)
subprocess.run([str(exe)],check=True)
print('PASS 34 actual native assertions: cache/trigger boundaries, final and period-ending decisions')

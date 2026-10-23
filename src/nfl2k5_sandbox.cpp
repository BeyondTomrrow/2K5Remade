/* Optional Situation-derived editor. Guest memory is only accessed from its
 * normal polling/lifecycle thread. The presenter and page exchange owned
 * snapshots and bounded commands, never guest pointers. */
#include "nfl2k5_sandbox.h"
#include "nfl2k5_sandbox_menu.h"
#include "nfl2k5_sandbox_world.h"
#include "nfl2k5_sandbox_actors.h"
#include "nfl2k5_sandbox_pose.h"
#include "nfl2k5_sandbox_ui.h"
#include "xbox_memory_layout.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

extern "C" int nfl2k5_controls_style(void);
extern "C" char *xbox_user_path(const char *,char *,size_t);
extern "C" void xbox_PresentRecord(int);
namespace {
struct Cmd { std::string name; int value; unsigned generation; };
struct Scene {
    unsigned version=2;
    uint32_t home_identity=0,away_identity=0,stadium_identity=0;
    int home=0,away=0,period=1,down=1,home_to=3,away_to=3;
    float clock=900,ball=0,line=914.4f;
    Nfl2k5SandboxWorldState camera{};
    uint32_t player_count=0;
    Nfl2k5SandboxActorProperties players[142]{};
    uint32_t pose_count=0;
    Nfl2k5SandboxPose poses[142]{};
};
struct Row { std::string name,value,cmd,description; int arg=0; bool enabled=true; };
std::atomic<bool> armed{false},active{false},open{false},photo{false};
std::atomic<bool> match_loaded{false};
std::atomic<unsigned> generation{0};
std::mutex queue_lock,json_lock;
std::deque<Cmd> queue;
std::string json="{\"active\":false}";
int tab=0,selection=0,scene_slot=1,speed=0;
bool free_camera=true;
bool request_native_pause=false;
bool wait_for_release=false;
ULONGLONG last_tick=0,last_repeat=0;
uint32_t previous_buttons=0;
std::string message;
std::vector<Scene> undo_stack,redo_stack;
std::vector<Row> rows;
Nfl2k5SandboxWorldState camera_shots[2]{};
bool camera_shot_saved[2]={false,false};
int camera_motion=0,shot_seconds=10,fly_speed=1200;
int selected_player=-1;
bool player_panel=false;
float orbit_angle=0,orbit_radius=900,orbit_height=300,shot_elapsed=0;
float tracked_x=0,tracked_y=0,tracked_z=0;
ULONGLONG last_publish=0;
constexpr float pi=3.14159265358979323846f;
Nfl2k5SandboxActorProperties player_baselines[142]{};
bool player_baseline_valid[142]{};
uint32_t baseline_clock=0;
bool grab_mode=false,pose_snap=false;
float grab_pending_dt=0,camera_pending_dt=0;
Scene grab_before{};
std::string selected_name;

bool guest_copy(uint32_t at,void *out,size_t n,bool write=false) {
    uint64_t end=(uint64_t)at+n;
    if (!((at>=0x10000u&&end<=0x08000000u)||(at>=0x80000000u&&end<=0x88000000u))) return false;
    void *p=(void *)((uintptr_t)xbox_GetMemoryOffset()+at);
    __try { if(write) memcpy(p,out,n); else memcpy(out,p,n); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t rd(uint32_t at) { uint32_t v=0; guest_copy(at,&v,4); return v; }
float rf(uint32_t at) { float v=NAN; guest_copy(at,&v,4); return v; }
bool finite_range(float x,float lo,float hi) { return std::isfinite(x)&&x>=lo&&x<=hi; }
std::string quote(const std::string &s) {
    std::string out="\"";
    for(unsigned char c:s) { if(c=='"'||c=='\\') out+='\\'; if(c>=32)out+=(char)c; }
    return out+'"';
}
std::string number(float x) { char b[40]; snprintf(b,sizeof b,"%.2f",x);return b; }
std::string time_text(float x) { char b[24];int s=(int)std::ceil(x);snprintf(b,sizeof b,"%d:%02d",s/60,s%60);return b; }
bool snapshot(Scene &s) {
    uint32_t clk=rd(0xE6028Cu),ds=rd(0xE602ECu);
    uint32_t hs=rd(0xE5FC20u+8),as=rd(0xE5FC60u+8);
    if(!clk||!ds||!hs||!as)return false;
    s.clock=rf(clk+0x10);s.period=(int)rd(0xE602C4u);
    s.home=(int)rd(hs);s.away=(int)rd(as);s.home_to=(int)rd(hs+4);s.away_to=(int)rd(as+4);
    s.down=(int)rd(ds+4);s.ball=rf(ds+0x18);s.line=rf(ds+0x28);
    s.home_identity=rd(rd(0xE5FE68u)+0x118)&0xFFFFu;
    s.away_identity=rd(rd(0xE5FE6Cu)+0x118)&0xFFFFu;
    uint16_t stadium=0;guest_copy(rd(0xE5FE64u)+0x7C,&stadium,1);s.stadium_identity=stadium;
    nfl2k5_sandbox_world_get_state(&s.camera);
    s.player_count=0;memset(s.players,0,sizeof s.players);memset(s.poses,0,sizeof s.poses);
    if(baseline_clock&&baseline_clock!=clk)return false;
    for(int id=0;id<142;id++)if(player_baseline_valid[id]) {
        Nfl2k5SandboxActorProperties p{};
        if(!nfl2k5_sandbox_actor_get_properties(clk,id,&p)||p.identity!=player_baselines[id].identity)return false;
        if(memcmp(p.ratings,player_baselines[id].ratings,sizeof p.ratings))s.players[s.player_count++]=p;
    }
    s.pose_count=(uint32_t)nfl2k5_sandbox_pose_list(clk,s.poses,142);
    return finite_range(s.clock,0,3600)&&s.period>=0&&s.period<=5&&s.home>=0&&s.home<=250&&s.away>=0&&s.away<=250&&s.down>=1&&s.down<=4&&finite_range(s.ball,-20000,20000)&&finite_range(s.line,-20000,20000);
}
bool valid_scene(const Scene &s) {
    if(s.player_count>142||s.pose_count>142)return false;
    bool seen[142]{};
    for(unsigned i=0;i<s.player_count;i++) {
        auto &p=s.players[i];if(!nfl2k5_sandbox_actor_valid_properties(&p)||p.id>=142||seen[p.id])return false;seen[p.id]=true;
    }
    memset(seen,0,sizeof seen);
    for(unsigned i=0;i<s.pose_count;i++) {
        auto &p=s.poses[i];if(!nfl2k5_sandbox_pose_valid(&p)||p.id>=142||seen[p.id])return false;seen[p.id]=true;
    }
    return s.version==2&&s.period>=1&&s.period<=5&&s.down>=1&&s.down<=4&&
        s.home>=0&&s.home<=250&&s.away>=0&&s.away<=250&&s.home_to>=0&&s.home_to<=9&&s.away_to>=0&&s.away_to<=9&&
        finite_range(s.clock,0,3600)&&finite_range(s.ball,-20000,20000)&&finite_range(s.line,-20000,20000)&&
        finite_range(s.camera.x,-100000,100000)&&finite_range(s.camera.y,-10000,50000)&&finite_range(s.camera.z,-100000,100000)&&
        finite_range(s.camera.yaw,-36000,36000)&&finite_range(s.camera.pitch,-85,85)&&finite_range(s.camera.fov,15,110)&&
        finite_range(s.camera.timescale,0,1);
}
void restore_properties(const std::vector<Nfl2k5SandboxActorProperties> &values) {
    for(auto &p:values)nfl2k5_sandbox_actor_restore_properties(baseline_clock,&p);
}
bool apply_properties(const Scene &s,bool trusted,std::vector<Nfl2k5SandboxActorProperties> &previous) {
    uint32_t clk=rd(0xE6028C);if(baseline_clock&&baseline_clock!=clk)return false;
    std::vector<Nfl2k5SandboxActorProperties> next;std::vector<bool> raw;
    for(unsigned id=0;id<142;id++) {
        const Nfl2k5SandboxActorProperties *wanted=nullptr;
        for(unsigned i=0;i<s.player_count;i++)if(s.players[i].id==id)wanted=&s.players[i];
        if(!wanted&&!player_baseline_valid[id])continue;
        Nfl2k5SandboxActorProperties now{};
        if(!nfl2k5_sandbox_actor_get_properties(clk,(int)id,&now)||(wanted&&wanted->identity!=now.identity)||
           (player_baseline_valid[id]&&player_baselines[id].identity!=now.identity))return false;
        if(!player_baseline_valid[id]){player_baselines[id]=now;player_baseline_valid[id]=true;baseline_clock=clk;}
        previous.push_back(now);next.push_back(wanted?*wanted:player_baselines[id]);raw.push_back(trusted||!wanted);
    }
    for(size_t i=0;i<next.size();i++) {
        bool ok=raw[i]?nfl2k5_sandbox_actor_restore_properties(clk,&next[i]):nfl2k5_sandbox_actor_set_properties(clk,&next[i]);
        if(!ok){restore_properties(previous);return false;}
    }
    return true;
}
bool apply_poses(const Scene &s) {
    uint32_t clk=rd(0xE6028C);Nfl2k5SandboxPose old[142];int count=nfl2k5_sandbox_pose_list(clk,old,142);
    /* Validate every identity before clearing anything. */
    for(unsigned i=0;i<s.pose_count;i++) {
        Nfl2k5SandboxActorProperties p{};
        if(!nfl2k5_sandbox_actor_get_properties(clk,(int)s.poses[i].id,&p)||p.identity!=s.poses[i].identity)return false;
    }
    nfl2k5_sandbox_pose_clear(clk);
    for(unsigned i=0;i<s.pose_count;i++)if(!nfl2k5_sandbox_pose_set(clk,&s.poses[i])) {
        nfl2k5_sandbox_pose_clear(clk);for(int j=0;j<count;j++)nfl2k5_sandbox_pose_set(clk,&old[j]);return false;
    }
    return true;
}
bool apply_scene(const Scene &s,bool restore_camera=true,bool trusted=false) {
    Scene current;
    int old_speed=speed,old_motion=camera_motion;bool old_free=free_camera;
    bool old_baseline_valid[142];memcpy(old_baseline_valid,player_baseline_valid,sizeof old_baseline_valid);
    uint32_t old_baseline_clock=baseline_clock;
    auto rollback_baselines=[&](){memcpy(player_baseline_valid,old_baseline_valid,sizeof old_baseline_valid);baseline_clock=old_baseline_clock;};
    if(!active||!valid_scene(s)||!snapshot(current))return false;
    if(s.home_identity!=current.home_identity||s.away_identity!=current.away_identity||s.stadium_identity!=current.stadium_identity)return false;
    uint32_t clk=rd(0xE6028Cu),ds=rd(0xE602ECu),hs=rd(0xE5FC20u+8),as=rd(0xE5FC60u+8);
    /* Validate every destination before any writes. These are the live
     * Situation settings, not saved roster/profile records. */
    const uint32_t dest[]={clk+0x10,0xE602C4u,hs,as,hs+4,as+4,ds+4,ds+0x18,ds+0x28};
    uint32_t old[9],next[9];
    for(int i=0;i<9;i++)if(!guest_copy(dest[i],&old[i],4))return false;
    std::vector<Nfl2k5SandboxActorProperties> previous;
    if(!apply_properties(s,trusted,previous)){rollback_baselines();return false;}
    memcpy(&next[0],&s.clock,4);next[1]=s.period;next[2]=s.home;next[3]=s.away;next[4]=s.home_to;next[5]=s.away_to;next[6]=s.down;
    memcpy(&next[7],&s.ball,4);memcpy(&next[8],&s.line,4);
    for(int i=0;i<9;i++)if(!guest_copy(dest[i],&next[i],4,true)) {
        for(int j=i-1;j>=0;j--)guest_copy(dest[j],&old[j],4,true);restore_properties(previous);rollback_baselines();return false;
    }
    if(restore_camera&&s.camera.camera_ready) {
        if(!nfl2k5_sandbox_world_restore(&s.camera)) {
            for(int i=0;i<9;i++)guest_copy(dest[i],&old[i],4,true);restore_properties(previous);rollback_baselines();return false;
        }
        speed=(int)std::lround(s.camera.timescale*100.f);free_camera=true;camera_motion=0;
    }
    if(!apply_poses(s)) {
        for(int i=0;i<9;i++)guest_copy(dest[i],&old[i],4,true);restore_properties(previous);
        if(restore_camera&&current.camera.camera_ready)nfl2k5_sandbox_world_restore(&current.camera);
        speed=old_speed;free_camera=old_free;camera_motion=old_motion;
        rollback_baselines();
        return false;
    }
    return true;
}
void remember(const Scene &s) { if(undo_stack.size()==32)undo_stack.erase(undo_stack.begin());undo_stack.push_back(s);redo_stack.clear(); }
/* Player IDs in page commands are bounded side*71+index, never guest pointers.
 * Resolve and revalidate against this scene's native actor list every use. */
bool selected_actor(Nfl2k5SandboxActor &actor,int id=-1) {
    if(id<0)id=selected_player;
    if(id<0||id>=142)return false;
    uint32_t team=id>=71?0xB30A58u:0xB30864u;
    unsigned count=rd(team+0x11C)&255u,index=(unsigned)id%71u;
    if(count>71||index>=count)return false;
    uint32_t clock=rd(0xE6028C);
    if(!nfl2k5_sandbox_actor_get(clock,rd(team+index*4),&actor))return false;
    Nfl2k5SandboxPose pose{};
    if(nfl2k5_sandbox_pose_get(clock,id,&pose)) {actor.x+=pose.x;actor.y=actor.y*pose.scale+pose.y;actor.z+=pose.z;}
    return true;
}
bool aim_camera(Nfl2k5SandboxWorldState &w,float x,float y,float z) {
    float dx=x-w.x,dy=y-w.y,dz=z-w.z;
    float length=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!std::isfinite(length)||length<1.f)return false;
    w.yaw=std::atan2(dx,dz)*180.f/pi;
    w.pitch=std::clamp(std::asin(std::clamp(dy/length,-1.f,1.f))*180.f/pi,-85.f,85.f);
    return true;
}
bool set_camera(Nfl2k5SandboxWorldState &w) {
    /* A camera bookmark/take must never restore its old simulation speed. */
    w.timescale=speed/100.f;
    if(!w.camera_ready||!nfl2k5_sandbox_world_restore(&w))return false;
    free_camera=true;return true;
}
float camera_lerp_angle(float a,float b,float t) {
    float delta=std::fmod(b-a,360.f);
    if(delta>180)delta-=360;if(delta<-180)delta+=360;
    return a+delta*t;
}
void camera_tick(float dt) {
    if(!camera_motion||!free_camera){camera_pending_dt=0;return;}
    if(dt<=0)return;camera_pending_dt+=dt;if(camera_pending_dt<1.f/60.f)return;
    dt=std::min(camera_pending_dt,.1f);camera_pending_dt=0;
    Nfl2k5SandboxWorldState w{};nfl2k5_sandbox_world_get_state(&w);
    if(!w.camera_ready){camera_motion=0;return;}
    if(camera_motion==3) {
        shot_elapsed=std::min(shot_elapsed+dt,(float)shot_seconds);
        float t=shot_elapsed/shot_seconds;t=t*t*(3-2*t);
        auto &a=camera_shots[0];auto &b=camera_shots[1];
        w.x=a.x+(b.x-a.x)*t;w.y=a.y+(b.y-a.y)*t;w.z=a.z+(b.z-a.z)*t;
        w.yaw=camera_lerp_angle(a.yaw,b.yaw,t);w.pitch=a.pitch+(b.pitch-a.pitch)*t;w.fov=a.fov+(b.fov-a.fov)*t;
        if(shot_elapsed>=shot_seconds)camera_motion=0;
    } else {
        Nfl2k5SandboxActor a{};
        if(!selected_actor(a)){camera_motion=0;message="Selected player left the field; camera motion stopped";return;}
        if(camera_motion==1) {
            orbit_angle=std::fmod(orbit_angle+dt*.25f,2*pi);
            w.x=a.x+std::sin(orbit_angle)*orbit_radius;w.y=a.y+orbit_height;w.z=a.z+std::cos(orbit_angle)*orbit_radius;
        } else {
            w.x+=a.x-tracked_x;w.y+=a.y-tracked_y;w.z+=a.z-tracked_z;
            tracked_x=a.x;tracked_y=a.y;tracked_z=a.z;
        }
        aim_camera(w,a.x,a.y,a.z);
    }
    if(!set_camera(w))camera_motion=0;
}
void finish_grab(bool cancel) {
    if(!grab_mode)return;grab_mode=false;grab_pending_dt=0;
    if(cancel) {
        message=apply_scene(grab_before,true,true)?"Photo pose cancelled":"Scene changed; pose restore refused";
    } else {
        if(pose_snap) {
            Nfl2k5SandboxPose pose{};Nfl2k5SandboxActor a{};
            if(nfl2k5_sandbox_pose_get(rd(0xE6028C),selected_player,&pose)&&selected_actor(a)) {
                pose.x=std::clamp(pose.x+std::round(a.x/91.44f)*91.44f-a.x,-500.f,500.f);
                pose.z=std::clamp(pose.z+std::round(a.z/91.44f)*91.44f-a.z,-500.f,500.f);
                nfl2k5_sandbox_pose_set(rd(0xE6028C),&pose);
            }
        }
        remember(grab_before);message="Photo pose placed; simulation position stays unchanged";
    }
}
void grab_tick(float dt,int16_t lx,int16_t ly,int16_t rx,const uint8_t *a) {
    grab_pending_dt+=dt;if(grab_pending_dt<1.f/60.f)return;
    dt=std::min(grab_pending_dt,.1f);grab_pending_dt=0;
    Nfl2k5SandboxPose pose{};Nfl2k5SandboxWorldState w{};
    if(!nfl2k5_sandbox_pose_get(rd(0xE6028C),selected_player,&pose)){grab_mode=false;message="Player left this scene; photo posing stopped";return;}
    nfl2k5_sandbox_world_get_state(&w);
    auto axis=[](int v){return std::abs(v)<6000?0.f:v/32768.f;};
    float y=w.yaw*pi/180.f,mx=axis(lx),mz=axis(ly);
    pose.x=std::clamp(pose.x+(-mx*std::cos(y)+mz*std::sin(y))*dt*150.f,-500.f,500.f);
    pose.z=std::clamp(pose.z+(mx*std::sin(y)+mz*std::cos(y))*dt*150.f,-500.f,500.f);
    pose.y=std::clamp(pose.y+(a[7]-a[6])/255.f*dt*100.f,-500.f,500.f);
    pose.yaw=std::clamp(pose.yaw+axis(rx)*dt*90.f,-180.f,180.f);
    if(!nfl2k5_sandbox_pose_set(rd(0xE6028C),&pose)){grab_mode=false;message="Photo pose refused; scene changed";}
}
void restore_session() {
    if(baseline_clock)for(int id=0;id<142;id++)if(player_baseline_valid[id])
        nfl2k5_sandbox_actor_restore_properties(baseline_clock,&player_baselines[id]);
    memset(player_baseline_valid,0,sizeof player_baseline_valid);baseline_clock=0;
    nfl2k5_sandbox_pose_clear(0);grab_mode=false;selected_name.clear();
}
void history(bool redo) {
    auto &from=redo?redo_stack:undo_stack;auto &to=redo?undo_stack:redo_stack;
    Scene now;if(from.empty()||!snapshot(now)){message="Nothing to restore";return;}
    if(apply_scene(from.back(),true,true)){to.push_back(now);from.pop_back();message=redo?"Redone":"Undone";}
    else message="Scene changed; restore refused";
}
uint32_t hash_bytes(const void *p,size_t n) { uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^((const uint8_t*)p)[i])*16777619u;return h; }
struct DiskScene { char magic[8]; uint32_t size,hash; Scene state; };
bool scene_file(bool save) {
    if(scene_slot<1||scene_slot>5)return false;
    char rel[80],path[MAX_PATH],temp[MAX_PATH],backup[MAX_PATH];
    path[0]=0;snprintf(rel,sizeof rel,"sandbox/scene-%d.sbscene",scene_slot);
    if(!xbox_user_path(rel,path,sizeof path)||!path[0]||strlen(path)+5>=sizeof path)return false;
    std::string parent=path;auto slash=parent.find_last_of("/\\");if(slash!=std::string::npos){parent.resize(slash);CreateDirectoryA(parent.c_str(),NULL);}
    DiskScene d{};memcpy(d.magic,"2K5SCN2",8);d.size=sizeof(Scene);
    if(save) {
        if(!snapshot(d.state)||!valid_scene(d.state))return false;
        d.hash=hash_bytes(&d.state,sizeof d.state);
        snprintf(temp,sizeof temp,"%s.tmp",path);snprintf(backup,sizeof backup,"%s.bak",path);
        FILE *f=fopen(temp,"wb");if(!f)return false;
        bool ok=fwrite(&d,1,sizeof d,f)==sizeof d;ok=fclose(f)==0&&ok;
        if(!ok)return false;
        if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES&&!CopyFileA(path,backup,FALSE))return false;
        return MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    }
    FILE *f=fopen(path,"rb");if(!f)return false;
    /* Version 1 held only the camera/game-state prefix. Its original file
     * remains loadable; v2 stores bounded player IDs/identities, never pointers. */
    bool ok=fread(&d,1,16,f)==16;
    bool legacy=ok&&!memcmp(d.magic,"2K5SCN1",8)&&d.size==offsetof(Scene,player_count);
    ok=ok&&(legacy||(!memcmp(d.magic,"2K5SCN2",8)&&d.size==sizeof(Scene)));
    if(ok)ok=fread(&d.state,1,d.size,f)==d.size&&fgetc(f)==EOF;fclose(f);
    if(!ok||d.hash!=hash_bytes(&d.state,d.size))return false;
    if(legacy){if(d.state.version!=1)return false;d.state.version=2;}
    if(!valid_scene(d.state))return false;
    Scene old;if(!snapshot(old)||!apply_scene(d.state))return false;remember(old);return true;
}
void build_rows() {
    rows.clear();Scene s;bool ready=snapshot(s);
    auto add=[](const char*n,std::string v,const char*c,int a,const char*d,bool e=true){rows.push_back({n,v,c,d,a,e});};
    if(tab==0) {
        add("Choose teams / change stadium","Reload scene","reselect",0,"Uses the original team and stadium loaders; custom and historic teams stay optional.");
        add("Home score",ready?std::to_string(s.home):"Loading","home_score",1,"Left / right changes score",ready);
        add("Away score",ready?std::to_string(s.away):"Loading","away_score",1,"Left / right changes score",ready);
        add("Quarter",ready?std::to_string(s.period):"Loading","quarter",1,"1-4 regulation; 5 overtime",ready);
        add("Game clock",ready?time_text(s.clock):"Loading","clock",5,"Adjust in five-second steps",ready);
        add("Down",ready?std::to_string(s.down):"Loading","down",1,"Changes the live Situation down",ready);
        add("Ball spot",ready?number(s.ball/91.44f):"Loading","ball",1,"Field coordinate in yards; resume uses native game AI",ready);
        add("Yards to gain",ready?number(std::fabs(s.line-s.ball)/91.44f):"Loading","distance",1,"Adjusts line to gain in yards",ready);
        add("Home timeouts",ready?std::to_string(s.home_to):"Loading","home_timeouts",1,"Session only",ready);
        add("Away timeouts",ready?std::to_string(s.away_to):"Loading","away_timeouts",1,"Session only",ready);
    } else if(tab==1) {
        Nfl2k5SandboxActor selected{},actors[NFL2K5_SANDBOX_MAX_ACTORS];
        bool has_selected=selected_actor(selected);
        if(!player_panel) {
            add("On-field players","Choose a player","",0,"Select a native player to open camera, match-rating and experimental photo-pose tools",false);
            int count=nfl2k5_sandbox_actors_list(rd(0xE6028C),actors,NFL2K5_SANDBOX_MAX_ACTORS);
            std::sort(actors,actors+count,[](const Nfl2k5SandboxActor &a,const Nfl2k5SandboxActor &b){return a.side!=b.side?a.side<b.side:a.index<b.index;});
            for(int i=0;i<count;i++) {
                auto &a=actors[i];std::string label=(a.side?"Away: ":"Home: ")+std::string(a.name);
                rows.push_back({label,"#"+std::to_string(a.jersey),"focus_player","Select and frame this native on-field player",a.side*71+a.index,true});
            }
            add("Spawn / possess","Unavailable","",0,"These need additional engine integration. Select a player for camera, ratings, photo-pose and animation-clip tools.",false);
        } else {
            add("Player list","Back","player_list",0,"Choose another loaded field player");
            add("Selected player",has_selected?selected.name:"Player left the field","",0,"Match-only changes. Original roster and save files remain separate",false);
            add("Follow player",camera_motion==2?"On":"Off","follow_player",0,"Track this player's movement",has_selected);
            add("Orbit player",camera_motion==1?"On":"Off","orbit_player",0,"Moving camera shot, including when simulation is paused",has_selected);
            add("Close-up","Frame player","closeup_player",0,"Frame a player portrait",has_selected);
            Nfl2k5SandboxPose pose{};bool can_pose=has_selected&&nfl2k5_sandbox_pose_get(rd(0xE6028C),selected_player,&pose);
            add("Photo pose status",nfl2k5_sandbox_pose_status(rd(0xE6028C),selected_player),"",0,"A prepared palette confirms the renderer path ran; it is not a physics-position edit",false);
            add("Grab for photo posing","Experimental","grab_player",0,"Visual preview only: sticks move/turn, triggers raise/lower, A places, B cancels. AI and shadows keep their native placement",can_pose);
            add("Pose offset X",number(pose.x/100.f)+" m","pose_x",1,"Experimental visual shift; bounded to five metres",can_pose);
            add("Pose height",number(pose.y/100.f)+" m","pose_y",1,"Visual shift only; no ground collision",can_pose);
            add("Pose offset Z",number(pose.z/100.f)+" m","pose_z",1,"Experimental visual shift; bounded to five metres",can_pose);
            add("Pose rotation",number(pose.yaw)+" degrees","pose_yaw",1,"Rotate the private visual pose",can_pose);
            add("Pose scale",number(pose.scale)+"x","pose_scale",1,"Quarter-size through three times size; does not change collision or ratings",can_pose);
            int clip_count=nfl2k5_sandbox_pose_clip_count();
            std::string clip_label=pose.clip?std::string(nfl2k5_sandbox_pose_clip_name((int)pose.clip-1)):"Off";
            add("Animation clip",clip_count?clip_label:"Clips unavailable","pose_clip",1,"Freeze the player on a baked animation clip (visual preview); offset, rotation and scale still apply",can_pose&&clip_count>0);
            std::string frame_desc;
            if(can_pose&&clip_count>0&&pose.clip) {
                float secs=nfl2k5_sandbox_pose_clip_seconds((int)pose.clip-1);
                int frame=(int)std::lround(pose.clip_time*15.f);
                frame_desc="Scrub to a frame and hold it frozen ("+number(secs)+"s clip)";
                add("Clip frame",std::to_string(frame),"pose_clip_time",1,frame_desc.c_str(),true);
            }
            add("Snap photo poses",pose_snap?"Yard grid":"Off","pose_snap",0,"Snap X/Z to a one-yard grid when dropping a grabbed pose");
            add("Reset photo pose","Reset","pose_reset",0,"Restore native model placement and scale",can_pose);
            Nfl2k5SandboxActorProperties p{};
            bool can_edit=has_selected&&nfl2k5_sandbox_actor_get_properties(rd(0xE6028C),selected_player,&p);
            add("Reset match ratings","Restore original","reset_ratings",0,"Restore this player's ratings captured before the first Sandbox edit",can_edit&&selected_player>=0&&player_baseline_valid[selected_player]);
            for(int i=0;i<NFL2K5_SANDBOX_RATINGS;i++) {
                bool style=i==21||i==23||i==25;
                add(nfl2k5_sandbox_actor_rating_name(i),can_edit?std::to_string(p.ratings[i]):"Unavailable","rating",i+1,
                    style?"Style selector: read-only until its enum values are verified":"Match-only rating, 0-99. Some AI values may be cached until the next play",can_edit&&!style);
            }
        }
    } else if(tab==2) {
        Nfl2k5SandboxWorldState w{};nfl2k5_sandbox_world_get_state(&w);
        add("Simulation",speed?std::to_string(speed)+"%":"Paused","toggle_pause",0,"Pause keeps input, rendering and audio threads running");
        add("Simulation speed",std::to_string(speed)+"%","speed",25,"0 / 25 / 50 / 75 / 100 percent");
        add("Advance one frame","Step","step",0,"Advances one native simulation update while paused");
        add("Free camera",free_camera?"On":"Off","camera",0,"Left stick fly; right stick look; triggers rise / descend; bumpers zoom",w.ready!=0);
        add("Field of view",number(w.fov)+" degrees","fov",1,"15-110 degrees. Bumpers zoom while the browser is closed",w.camera_ready!=0);
        add("Camera speed",number(fly_speed/100.f)+" m/s","fly_speed",100,"Fine positioning through fast stadium travel; 1-200 metres per second");
        add("Camera: sideline","Wide shot","camera_preset",1,"Frame the line of scrimmage from the sideline",w.camera_ready!=0);
        add("Camera: end zone","Wide shot","camera_preset",2,"Frame the field from behind the end zone",w.camera_ready!=0);
        add("Camera: overhead","Tactical shot","camera_preset",3,"High field view centred on the ball spot",w.camera_ready!=0);
        add("Save camera A",camera_shot_saved[0]?"Replace shot":"Store shot","store_camera",0,"Session camera bookmark A. Scene files also preserve the current camera",w.camera_ready!=0);
        add("Save camera B",camera_shot_saved[1]?"Replace shot":"Store shot","store_camera",1,"Session camera bookmark B",w.camera_ready!=0);
        add("Recall camera A","Recall","recall_camera",0,"Return to shot A without changing score or game time",camera_shot_saved[0]);
        add("Recall camera B","Recall","recall_camera",1,"Return to shot B without changing score or game time",camera_shot_saved[1]);
        add("Camera take duration",std::to_string(shot_seconds)+" seconds","shot_seconds",1,"5-30 seconds for a smooth move between shots A and B");
        add("Play camera A to B",camera_motion==3?"Playing":"Play take","camera_take",0,"Smooth camera-only move; works while the game is paused",camera_shot_saved[0]&&camera_shot_saved[1]);
        add("Stop camera motion","Stop","stop_camera",0,"Cancel orbit, player follow or a recorded camera take",camera_motion!=0);
        add("Reset to game camera","Reset","reset_camera",0,"Recapture the normal native camera on the next frame",w.ready!=0);
        add("Photo mode",photo?"On":"Off","photo",0,"Hide Sandbox and broadcast HUD. Back / Circle returns.");
        add("Record 10 seconds","Capture","record",10,"Uses the existing game video capture");
    } else {
        add("Scene slot",std::to_string(scene_slot),"slot",1,"Five independent Sandbox scene files; original saves untouched");
        add("Save scene","Save","save_scene",scene_slot,"Saves camera, game state, match ratings and experimental photo poses",ready);
        add("Load scene","Load","load_scene",scene_slot,"Loads settings into the current stadium; does not load another stadium's assets",ready);
        add("Undo","Undo","undo",0,"Up to 32 state edits",!undo_stack.empty());
        add("Redo","Redo","redo",0,"Restores an undone state edit",!redo_stack.empty());
        add("Play normally","Release camera","exit_tools",0,"Returns camera and input to native gameplay; Menu / Options reopens Sandbox");
    }
    if(selection>=(int)rows.size())selection=std::max(0,(int)rows.size()-1);
}
void publish(bool force=false) {
    ULONGLONG now=GetTickCount64();
    if(!force&&last_publish&&now-last_publish<50)return;
    last_publish=now;
    build_rows();
    std::string out="{\"active\":"+std::string(active?"true":"false")+",\"menuOpen\":"+(open?"true":"false")+",\"photoMode\":"+(photo?"true":"false")+
        ",\"paused\":"+(speed==0?"true":"false")+",\"speed\":"+std::to_string(speed)+",\"style\":"+quote(nfl2k5_controls_style()==1?"playstation":"xbox")+
        ",\"generation\":"+std::to_string(generation.load())+",\"tab\":"+std::to_string(tab)+",\"selection\":"+std::to_string(selection)+",\"message\":"+quote(message)+",\"reticle\":"+(free_camera?"true":"false")+",\"grabMode\":"+(grab_mode?"true":"false")+",\"selectedPlayer\":"+quote(selected_name)+",\"rows\":[";
    for(size_t i=0;i<rows.size();i++) {if(i)out+=',';auto&r=rows[i];
        bool adjustable=r.cmd=="home_score"||r.cmd=="away_score"||r.cmd=="quarter"||r.cmd=="clock"||r.cmd=="down"||r.cmd=="ball"||r.cmd=="distance"||r.cmd=="home_timeouts"||r.cmd=="away_timeouts"||r.cmd=="speed"||r.cmd=="slot"||r.cmd=="fov"||r.cmd=="fly_speed"||r.cmd=="shot_seconds"||r.cmd=="rating"||r.cmd=="pose_x"||r.cmd=="pose_y"||r.cmd=="pose_z"||r.cmd=="pose_yaw"||r.cmd=="pose_scale"||r.cmd=="pose_clip"||r.cmd=="pose_clip_time";
        out+="{\"label\":"+quote(r.name)+",\"value\":"+quote(r.value)+",\"command\":"+quote(r.cmd)+",\"commandValue\":"+std::to_string(r.arg)+",\"description\":"+quote(r.description)+",\"enabled\":"+(r.enabled?"true":"false")+",\"adjustable\":"+(adjustable?"true":"false")+"}";}
    out+="]}";std::lock_guard<std::mutex>lock(json_lock);json=std::move(out);
}
void execute(const std::string &name,int value) {
    if(name=="navigate"){if(!rows.empty())selection=((selection+value)%(int)rows.size()+(int)rows.size())%(int)rows.size();return;}
    if(name=="tab"){tab=((tab+value)%4+4)%4;selection=0;return;}
    if(name=="select"||name=="adjust") {
        build_rows();if(selection<0||selection>=(int)rows.size())return;auto r=rows[selection];
        if(!r.enabled||r.cmd.empty()){message=r.description;return;}
        execute(r.cmd,name=="adjust"?(value<0?-r.arg:r.arg):r.arg);return;
    }
    if(name=="toggle_menu"){finish_grab(false);open=!open;photo=false;return;}
    if(name=="back"){if(open&&tab==1&&player_panel){player_panel=false;selection=0;return;}photo=false;open=false;return;}
    if(name=="player_list"){player_panel=false;selection=0;return;}
    if(name=="pose_snap"){pose_snap=!pose_snap;return;}
    if(name=="rating"||name=="reset_ratings") {
        uint32_t clk=rd(0xE6028C);Nfl2k5SandboxActorProperties p{};Scene before;
        if(selected_player<0||!nfl2k5_sandbox_actor_get_properties(clk,selected_player,&p)||!snapshot(before)){message="Selected player is unavailable";return;}
        if(name=="reset_ratings") {
            if(!player_baseline_valid[selected_player]){message="No match-rating edits to restore";return;}
            if(nfl2k5_sandbox_actor_restore_properties(clk,&player_baselines[selected_player])){remember(before);message="Original match ratings restored";}
            else message="Player identity changed; restore refused";
            return;
        }
        int index=std::abs(value)-1;
        if(index<0||index>=28||index==21||index==23||index==25)return;
        if(baseline_clock&&baseline_clock!=clk){message="Scene changed; rating edit refused";return;}
        if(!player_baseline_valid[selected_player]){player_baselines[selected_player]=p;player_baseline_valid[selected_player]=true;baseline_clock=clk;}
        p.ratings[index]=(uint8_t)std::clamp((int)p.ratings[index]+(value<0?-1:1),0,99);
        if(nfl2k5_sandbox_actor_set_properties(clk,&p)){remember(before);message="Match rating changed; original roster file untouched";}
        else message="Rating edit refused; player or style changed";
        return;
    }
    if(name=="grab_player"||name=="pose_x"||name=="pose_y"||name=="pose_z"||name=="pose_yaw"||name=="pose_scale"||name=="pose_reset"||name=="pose_clip"||name=="pose_clip_time") {
        uint32_t clk=rd(0xE6028C);Nfl2k5SandboxPose pose{};Scene before;
        if(!nfl2k5_sandbox_pose_get(clk,selected_player,&pose)||!snapshot(before)){message="Player is unavailable for photo posing";return;}
        if(name=="grab_player") {
            grab_before=before;grab_mode=true;grab_pending_dt=0;camera_motion=0;speed=0;open=false;photo=false;
            nfl2k5_sandbox_world_set_timescale(0);nfl2k5_sandbox_world_input(0,0,0,0,0,0);
            message="Experimental photo posing: sticks move/turn, triggers height, A places, B cancels";return;
        }
        float dir=value<0?-1.f:1.f;
        if(name=="pose_x")pose.x=std::clamp(pose.x+dir*10.f,-500.f,500.f);
        else if(name=="pose_y")pose.y=std::clamp(pose.y+dir*10.f,-500.f,500.f);
        else if(name=="pose_z")pose.z=std::clamp(pose.z+dir*10.f,-500.f,500.f);
        else if(name=="pose_yaw")pose.yaw=std::clamp(pose.yaw+dir*15.f,-180.f,180.f);
        else if(name=="pose_scale")pose.scale=std::clamp(pose.scale+dir*.05f,.25f,3.f);
        else if(name=="pose_clip") {
            int count=nfl2k5_sandbox_pose_clip_count();
            if(count<=0){message="Animation clips unavailable (draftposes.bin missing)";return;}
            int c=(int)pose.clip+(value<0?-1:1);
            if(c<0)c=count;else if(c>count)c=0;
            pose.clip=(uint32_t)c;pose.clip_time=0;
        }
        else if(name=="pose_clip_time") {
            if(!pose.clip)return;
            float secs=nfl2k5_sandbox_pose_clip_seconds((int)pose.clip-1);
            pose.clip_time=std::clamp(pose.clip_time+dir*(1.f/15.f),0.f,secs>0?secs:0.f);
        }
        else {pose.x=pose.y=pose.z=pose.yaw=0;pose.scale=1;pose.clip=0;pose.clip_time=0;}
        if(nfl2k5_sandbox_pose_set(clk,&pose)){remember(before);message="Experimental photo pose updated; AI, culling and shadows retain native positions";}
        else message="Photo pose refused; player changed";
        return;
    }
    if(name=="toggle_pause"){speed=speed?0:100;nfl2k5_sandbox_world_set_timescale(speed/100.f);return;}
    if(name=="speed"||name=="timescale"){speed=name=="speed"?(speed+value+125)%125:std::clamp(value,0,100);nfl2k5_sandbox_world_set_timescale(speed/100.f);return;}
    if(name=="step"){speed=0;nfl2k5_sandbox_world_step();return;}
    if(name=="camera"){camera_motion=0;free_camera=!free_camera;nfl2k5_sandbox_world_set_camera(free_camera?1:0);return;}
    if(name=="stop_camera"){camera_motion=0;message="Camera motion stopped";return;}
    if(name=="reset_camera"){camera_motion=0;free_camera=true;nfl2k5_sandbox_world_set_camera(0);nfl2k5_sandbox_world_set_camera(1);message="Recapturing native field camera";return;}
    if(name=="fly_speed"){fly_speed=std::clamp(fly_speed+(value<0?-100:100),100,20000);nfl2k5_sandbox_world_set_fly_speed((float)fly_speed);return;}
    if(name=="shot_seconds"){shot_seconds=std::clamp(shot_seconds+(value<0?-1:1),5,30);return;}
    if(name=="fov"||name=="camera_preset"||name=="store_camera"||name=="recall_camera"||name=="camera_take"||name=="focus_player"||name=="closeup_player"||name=="follow_player"||name=="orbit_player") {
        Nfl2k5SandboxWorldState w{};nfl2k5_sandbox_world_get_state(&w);
        if(!w.camera_ready){free_camera=true;nfl2k5_sandbox_world_set_camera(1);message="Waiting for the field camera; select again when ready";return;}
        Scene before;bool can_undo=snapshot(before);int previous_motion=camera_motion;camera_motion=0;
        if((name=="follow_player"&&previous_motion==2)||(name=="orbit_player"&&previous_motion==1)) {
            message="Player camera motion stopped";return;
        }
        if(name=="store_camera") {
            if(value<0||value>1)return;camera_shots[value]=w;camera_shot_saved[value]=true;
            message=value?"Camera B stored for this session":"Camera A stored for this session";return;
        }
        if(name=="recall_camera") {
            if(value<0||value>1||!camera_shot_saved[value])return;
            w=camera_shots[value];
        } else if(name=="camera_take") {
            if(!camera_shot_saved[0]||!camera_shot_saved[1])return;
            w=camera_shots[0];camera_motion=3;shot_elapsed=0;open=false;
        } else if(name=="fov") w.fov=std::clamp(w.fov+(value<0?-1.f:1.f),15.f,110.f);
        else if(name=="camera_preset") {
            float spot=can_undo?before.ball:0;
            if(value==1){w.x=-3400;w.y=1600;w.z=spot-1500;}
            else if(value==2){w.x=0;w.y=1000;w.z=-6200;}
            else if(value==3){w.x=0;w.y=6500;w.z=spot-600;}
            else return;
            w.fov=60;aim_camera(w,0,80,spot);
        } else {
            Nfl2k5SandboxActor a{};
            if(!selected_actor(a,name=="focus_player"?value:-1)){message="Player is no longer on the field";return;}
            selected_player=a.side*71+a.index;
            selected_name=a.name;
            if(name=="focus_player"){player_panel=true;selection=0;}
            if(name=="focus_player"||name=="closeup_player") {
                float distance=name=="closeup_player"?350.f:750.f;
                float angle=w.yaw*pi/180.f;
                w.x=a.x-std::sin(angle)*distance;w.y=a.y+(name=="closeup_player"?30.f:160.f);w.z=a.z-std::cos(angle)*distance;
                w.fov=name=="closeup_player"?38.f:55.f;aim_camera(w,a.x,a.y,a.z);
            } else if(name=="follow_player") {
                camera_motion=2;tracked_x=a.x;tracked_y=a.y;tracked_z=a.z;aim_camera(w,a.x,a.y,a.z);
            } else {
                camera_motion=1;orbit_angle=std::atan2(w.x-a.x,w.z-a.z);
                orbit_radius=std::clamp(std::hypot(w.x-a.x,w.z-a.z),350.f,5000.f);
                orbit_height=std::clamp(w.y-a.y,100.f,3000.f);aim_camera(w,a.x,a.y,a.z);
            }
            message="Camera selected: "+std::string(a.name);
        }
        if(set_camera(w)){if(can_undo)remember(before);if(name=="camera_take")message="Playing camera take; moving the sticks cancels";}
        else {camera_motion=0;message="Camera change refused; scene changed";}
        return;
    }
    if(name=="photo"){photo=!photo;open=false;return;}
    if(name=="record"){xbox_PresentRecord(10);message="Recording 10 seconds";return;}
    if(name=="slot"){scene_slot=(scene_slot+(value>0?1:4)-1)%5+1;return;}
    if(name=="save_scene"||name=="load_scene"){scene_slot=std::clamp(value,1,5);message=scene_file(name=="save_scene")?(name=="save_scene"?"Scene saved":"Scene restored"):"Scene unavailable or invalid; nothing restored";return;}
    if(name=="undo"||name=="redo"){history(name=="redo");return;}
    if(name=="exit_tools"){camera_motion=0;speed=100;free_camera=false;open=false;photo=false;nfl2k5_sandbox_world_set_timescale(1);nfl2k5_sandbox_world_set_camera(0);return;}
    if(name=="reselect") {
        camera_motion=0;speed=100;free_camera=false;open=false;photo=false;
        nfl2k5_sandbox_world_set_timescale(1);nfl2k5_sandbox_world_set_camera(0);
        if(nfl2k5_sandbox_menu_quit_for_reselect()) {
            message="Team / stadium change queued for the next scene boundary.";
        } else message="Scene is not ready for team / stadium changes";
        return;
    }
    Scene before;if(!snapshot(before)){message="Game state is still loading";return;}Scene s=before;
    int dir=value<0?-1:1;
    if(name=="home_score")s.home=std::clamp(s.home+dir,0,250);
    else if(name=="away_score")s.away=std::clamp(s.away+dir,0,250);
    else if(name=="quarter")s.period=std::clamp(s.period+dir,1,5);
    else if(name=="clock")s.clock=std::clamp(s.clock+dir*5.f,0.f,3600.f);
    else if(name=="down")s.down=std::clamp(s.down+dir,1,4);
    else if(name=="ball"){s.ball=std::clamp(s.ball+dir*91.44f,-4572.f,4572.f);s.line+=s.ball-before.ball;}
    else if(name=="distance"){float sign=s.line>=s.ball?1.f:-1.f;float distance=std::clamp(std::fabs(s.line-s.ball)+dir*91.44f,0.f,9144.f);s.line=s.ball+sign*distance;}
    else if(name=="home_timeouts")s.home_to=std::clamp(s.home_to+dir,0,3);
    else if(name=="away_timeouts")s.away_to=std::clamp(s.away_to+dir,0,3);
    else {message="Unknown or unavailable Sandbox action";return;}
    if(apply_scene(s,false)){remember(before);message="Scene updated";}else message="State changed during edit; edit refused";
}
/* Deterministic in-match self-test: NFL2K5_SANDBOX_AUTOTEST=1 drives each editor
 * control directly and logs a [SANDBOX-TEST] marker, so every control can be
 * frame-captured without walking native menus, the HTML overlay, or analog
 * sticks (which the press-file test harness cannot inject). Off by default. */
int at_step=0; ULONGLONG at_next=0; bool at_done=false;
bool autotest_on() { static int v=-1; if(v<0){const char*e=getenv("NFL2K5_SANDBOX_AUTOTEST");v=(e&&*e&&strcmp(e,"0"))?1:0;} return v!=0; }
void at_tick() {
    if(!autotest_on()||at_done)return;
    ULONGLONG now=GetTickCount64();
    if(at_next&&now<at_next)return;
    Nfl2k5SandboxWorldState w{};nfl2k5_sandbox_world_get_state(&w);
    if(!w.camera_ready){at_next=now+1000;return;} /* wait for the live field camera */
    at_next=now+3500; /* dwell so a ~1 Hz frame capture catches each step */
    uint32_t clk=rd(0xE6028C);
    switch(at_step) {
    case 0: open=false;photo=true;free_camera=true;camera_motion=0;execute("camera_preset",1);
        fprintf(stderr,"[SANDBOX-TEST] 0 sideline camera preset (field view, HUD hidden)\n");break;
    case 1: execute("camera_preset",2);fprintf(stderr,"[SANDBOX-TEST] 1 end-zone camera preset\n");break;
    case 2: execute("camera_preset",3);fprintf(stderr,"[SANDBOX-TEST] 2 overhead camera preset\n");break;
    case 3: {float x0=w.x;if(w.camera_ready){w.x+=500;nfl2k5_sandbox_world_restore(&w);}
        fprintf(stderr,"[SANDBOX-TEST] 3 camera move +500cm x=%.0f->%.0f\n",x0,w.x);}break;
    case 4: {float f0=w.fov;for(int i=0;i<12;i++)execute("fov",1);nfl2k5_sandbox_world_get_state(&w);
        fprintf(stderr,"[SANDBOX-TEST] 4 FOV +12deg %.0f->%.0f\n",f0,w.fov);}break;
    case 5: speed=100;nfl2k5_sandbox_world_set_timescale(1);fprintf(stderr,"[SANDBOX-TEST] 5 resume speed=%d%%\n",speed);break;
    case 6: execute("toggle_pause",0);fprintf(stderr,"[SANDBOX-TEST] 6 pause speed=%d%%\n",speed);break;
    case 7: execute("speed",25);fprintf(stderr,"[SANDBOX-TEST] 7 slow-mo speed=%d%%\n",speed);break;
    case 8: speed=0;nfl2k5_sandbox_world_set_timescale(0);nfl2k5_sandbox_world_step();fprintf(stderr,"[SANDBOX-TEST] 8 one-frame step (paused)\n");break;
    case 9: {Scene s0{};int b=snapshot(s0)?s0.home:-1;execute("home_score",1);execute("home_score",1);execute("home_score",1);
        Scene s1{};int a=snapshot(s1)?s1.home:-1;fprintf(stderr,"[SANDBOX-TEST] 9 game-state edit home_score %d->%d\n",b,a);}break;
    case 10:{Nfl2k5SandboxActor al[NFL2K5_SANDBOX_MAX_ACTORS];int c=nfl2k5_sandbox_actors_list(clk,al,NFL2K5_SANDBOX_MAX_ACTORS);
        if(c>0){int id=al[c/2].side*71+al[c/2].index;execute("focus_player",id);
            fprintf(stderr,"[SANDBOX-TEST] 10 select+frame player id=%d #%d %s (list=%d)\n",id,al[c/2].jersey,al[c/2].name,c);}
        else fprintf(stderr,"[SANDBOX-TEST] 10 no on-field actors listed\n");}break;
    case 11: execute("closeup_player",0);fprintf(stderr,"[SANDBOX-TEST] 11 close-up selected=%d\n",selected_player);break;
    case 12:{for(int i=0;i<10;i++)execute("pose_x",1);Nfl2k5SandboxPose p{};nfl2k5_sandbox_pose_get(clk,selected_player,&p);
        fprintf(stderr,"[SANDBOX-TEST] 12 grab/move player +X -> %.0fcm\n",p.x);}break;
    case 13:{for(int i=0;i<4;i++)execute("pose_yaw",1);Nfl2k5SandboxPose p{};nfl2k5_sandbox_pose_get(clk,selected_player,&p);
        fprintf(stderr,"[SANDBOX-TEST] 13 rotate player -> %.0fdeg\n",p.yaw);}break;
    case 14:{int cc=nfl2k5_sandbox_pose_clip_count();execute("pose_clip",1);Nfl2k5SandboxPose p{};nfl2k5_sandbox_pose_get(clk,selected_player,&p);
        fprintf(stderr,"[SANDBOX-TEST] 14 animation clip pick clip=%u (%s) of %d\n",p.clip,p.clip?nfl2k5_sandbox_pose_clip_name((int)p.clip-1):"-",cc);}break;
    case 15:{for(int i=0;i<15;i++)execute("pose_clip_time",1);Nfl2k5SandboxPose p{};nfl2k5_sandbox_pose_get(clk,selected_player,&p);
        fprintf(stderr,"[SANDBOX-TEST] 15 clip scrub+freeze time=%.2fs status=%s\n",p.clip_time,nfl2k5_sandbox_pose_status(clk,selected_player));}break;
    case 16:{bool ok=scene_file(true);fprintf(stderr,"[SANDBOX-TEST] 16 scene SAVE slot %d -> %s\n",scene_slot,ok?"OK":"FAIL");}break;
    case 17:{execute("pose_reset",0);execute("home_score",-1);execute("home_score",-1);bool ok=scene_file(false);
        Scene s{};int h=snapshot(s)?s.home:-1;fprintf(stderr,"[SANDBOX-TEST] 17 scene LOAD slot %d -> %s (home now %d)\n",scene_slot,ok?"OK":"FAIL",h);}break;
    case 18: open=false;photo=false;fprintf(stderr,"[SANDBOX-TEST] COMPLETE (showing live editor camera)\n");at_done=true;break;
    }
    at_step++;
}
}
extern "C" void nfl2k5_sandbox_begin() {
    restore_session();
    generation++;armed=true;active=false;match_loaded=false;open=false;photo=false;speed=0;free_camera=true;request_native_pause=false;wait_for_release=true;
    {std::lock_guard<std::mutex>lock(queue_lock);queue.clear();}undo_stack.clear();redo_stack.clear();selection=tab=0;previous_buttons=0;last_tick=0;
    selected_player=-1;player_panel=false;camera_motion=0;camera_shot_saved[0]=camera_shot_saved[1]=false;fly_speed=1200;shot_seconds=10;last_publish=0;message.clear();pose_snap=false;
    nfl2k5_sandbox_world_set_fly_speed((float)fly_speed);
    nfl2k5_sandbox_world_bind();nfl2k5_sandbox_pose_bind();fprintf(stderr,"[SANDBOX] Armed independent Situation scene\n");
}
extern "C" void nfl2k5_sandbox_end() {
    restore_session();
    if(armed||active)nfl2k5_sandbox_world_leave();generation++;armed=false;active=false;match_loaded=false;open=false;photo=false;request_native_pause=false;camera_motion=0;selected_player=-1;
    std::lock_guard<std::mutex> lock(queue_lock);queue.clear();
}
extern "C" void nfl2k5_sandbox_match_lifecycle(int entered) {
    match_loaded=entered!=0;
    if(!entered){if(active){restore_session();nfl2k5_sandbox_world_leave();active=false;armed=false;open=false;camera_motion=0;camera_shot_saved[0]=camera_shot_saved[1]=false;generation++;}return;}
    if(!armed)return;
}
extern "C" int nfl2k5_sandbox_active(){return active?1:0;}
extern "C" int nfl2k5_sandbox_ui_open(){return active&&open?1:0;}
extern "C" int nfl2k5_sandbox_ui_visible(){return active&&!photo?1:0;}
extern "C" int nfl2k5_sandbox_hide_hud(){return active?1:0;}
extern "C" const char *nfl2k5_sandbox_json(){static thread_local std::string copy;std::lock_guard<std::mutex>lock(json_lock);copy=json;return copy.c_str();}
extern "C" void nfl2k5_sandbox_command(const char *cmd,int value) {
    if(!active||!cmd||strlen(cmd)>32||value<-100000||value>100000)return;
    for(const char*c=cmd;*c;c++)if((*c<'a'||*c>'z')&&*c!='_')return;
    std::lock_guard<std::mutex>lock(queue_lock);if(queue.size()<64)queue.push_back({cmd,value,generation.load()});
}
extern "C" void nfl2k5_sandbox_command_for_generation(const char *cmd,int value,unsigned expected) {
    if(expected!=generation.load())return;
    if(!active||!cmd||strlen(cmd)>32||value<-100000||value>100000)return;
    for(const char*c=cmd;*c;c++)if((*c<'a'||*c>'z')&&*c!='_')return;
    std::lock_guard<std::mutex>lock(queue_lock);
    if(expected==generation.load()&&active&&queue.size()<64)queue.push_back({cmd,value,expected});
}
extern "C" int nfl2k5_sandbox_input(uint16_t buttons,const uint8_t a[8],int16_t lx,int16_t ly,int16_t rx,int16_t ry) {
    if(armed&&match_loaded&&!active&&nfl2k5_sandbox_world_enter()) {
        active=true;open=true;photo=false;speed=0;free_camera=true;last_tick=0;
        message="Sandbox: LB/RB or L1/R1 changes tabs. Close the browser to fly the camera.";
        nfl2k5_sandbox_world_set_timescale(0);nfl2k5_sandbox_world_set_camera(1);
    }
    if(!active)return 0;
    ULONGLONG now=GetTickCount64();float dt=last_tick?std::min(.05f,(now-last_tick)*.001f):0;last_tick=now;
    uint32_t b=buttons;for(int i=0;i<6;i++)if(a[i]>80)b|=1u<<(16+i);
    if(wait_for_release) {
        previous_buttons=b;
        if(!(b&0x3F003Fu))wait_for_release=false;
        nfl2k5_sandbox_world_input(0,0,0,0,0,0);
        nfl2k5_sandbox_world_tick(dt);publish();return 1;
    }
    uint32_t edge=b&~previous_buttons;previous_buttons=b;
    if(edge&0x10u)execute("toggle_menu",0);
    if((edge&0x20u)&&!grab_mode)execute("toggle_pause",0);
    if(photo&&(edge&(1u<<17)))execute("back",0);
    if(open) {
        if(edge&(1u<<16))execute("select",0);
        if(edge&(1u<<17))execute("back",0);
        if(edge&(1u<<20))execute("tab",1);
        if(edge&(1u<<21))execute("tab",-1);
        uint32_t direction=b&15u;
        if((edge&15u)||(direction&&now-last_repeat>180)) {
            if(direction&1)execute("navigate",-1);else if(direction&2)execute("navigate",1);
            else if(direction&4)execute("adjust",-1);else if(direction&8)execute("adjust",1);
            last_repeat=now;
        }
    } else if(grab_mode) {
        nfl2k5_sandbox_world_input(0,0,0,0,0,0);
        grab_tick(dt,lx,ly,rx,a);
        if(edge&(1u<<16))finish_grab(false);
        else if(edge&(1u<<17))finish_grab(true);
    } else if(free_camera) {
        auto axis=[](int v){return std::abs(v)<6000?0.f:v/32768.f;};
        if(axis(lx)||axis(ly)||axis(rx)||axis(ry)||(a[7]-a[6])||a[4]>80||a[5]>80)camera_motion=0;
        nfl2k5_sandbox_world_input(axis(lx),axis(ly),axis(rx),axis(ry),(a[7]-a[6])/255.f,(a[4]-a[5])/255.f);
        if(edge&(1u<<18))execute("toggle_pause",0);
        if(edge&(1u<<19))execute("step",0);
    }
    if(open||!free_camera)nfl2k5_sandbox_world_input(0,0,0,0,0,0);
    std::deque<Cmd> pending;{std::lock_guard<std::mutex>lock(queue_lock);pending.swap(queue);}
    for(auto&c:pending)if(c.generation==generation&&active)execute(c.name,c.value);
    at_tick();
    nfl2k5_sandbox_world_tick(dt);camera_tick(dt);publish(edge!=0||!pending.empty());
    if(request_native_pause){request_native_pause=false;return 2;}
    return active&&(open||free_camera||photo||grab_mode);
}
extern "C" void nfl2k5_sandbox_shutdown(){nfl2k5_sandbox_end();nfl2k5_sandbox_ui_shutdown();}
extern "C" int nfl2k5_sandbox_selftest() {
    Scene s{};s.camera.fov=60;
    if(!valid_scene(s))return 0;s.clock=NAN;if(valid_scene(s))return 0;s.clock=120;
    s.down=5;if(valid_scene(s))return 0;s.down=4;
    s.player_count=143;if(valid_scene(s))return 0;s.player_count=0;
    s.pose_count=1;s.poses[0]={0,1,0,0,0,0,0};if(valid_scene(s))return 0;s.pose_count=0;
    s.player_count=2;s.players[0].id=s.players[1].id=0;s.players[0].identity=s.players[1].identity=1;
    if(valid_scene(s))return 0;s.player_count=0;
    static_assert(offsetof(Scene,player_count)==88,"Legacy v1 scene prefix changed");
    Scene legacy{};memcpy(&legacy,&s,88);legacy.version=1;
    uint32_t legacy_hash=hash_bytes(&legacy,88);Scene recovered{};memcpy(&recovered,&legacy,88);
    if(hash_bytes(&recovered,88)!=legacy_hash)return 0;recovered.version=2;if(!valid_scene(recovered))return 0;
    DiskScene d{};memcpy(d.magic,"2K5SCN1",8);d.size=sizeof(Scene);d.state=s;d.hash=hash_bytes(&d.state,sizeof d.state);
    DiskScene copy{};memcpy(&copy,&d,sizeof d);if(copy.hash!=hash_bytes(&copy.state,sizeof copy.state))return 0;
    copy.state.home++;if(copy.hash==hash_bytes(&copy.state,sizeof copy.state))return 0;
    if(quote("a\"b\\c")!="\"a\\\"b\\\\c\"")return 0;
    Nfl2k5SandboxWorldState w{};
    if(!aim_camera(w,0,0,100)||std::fabs(w.yaw)>.001f||std::fabs(w.pitch)>.001f)return 0;
    if(!aim_camera(w,100,0,0)||std::fabs(w.yaw-90)>.001f)return 0;
    if(aim_camera(w,NAN,0,0))return 0;
    if(std::fabs(camera_lerp_angle(179,-179,.5f)-180)>.001f||std::fabs(camera_lerp_angle(-350,350,.5f)+360)>.001f)return 0;
    fprintf(stderr,"[TEST] Sandbox scene bounds, checksum corruption, JSON escaping and camera-shot math passed\n");return 1;
}

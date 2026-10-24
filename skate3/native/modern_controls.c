/* Archolos modern exploration controls, separate from the skating bridge.
 * Architecture informed by Kopselek's MIT Gothic Game Mixing controller;
 * see docs/MODERN_CONTROLS.md and THIRD_PARTY_NOTICES.md. No foreign game assets.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "MinHook.h"
#include "gothic_layout.h"
#include "modern_layout.h"
#include "modern_core.h"
typedef int (__attribute__((thiscall)) *QueryFn)(void*);
typedef void (__attribute__((thiscall)) *VectorFn)(void*, const ModernVec*);
typedef void (__attribute__((thiscall)) *WalkFn)(void*);
typedef void (__attribute__((thiscall)) *TurnFn)(void*,int);
typedef float (__attribute__((thiscall)) *InputFn)(void*,unsigned short);
typedef int (__attribute__((thiscall)) *ToggleFn)(void*,unsigned short);
typedef void (__attribute__((thiscall)) *CameraFn)(void*,void*,int*);
typedef int (__attribute__((fastcall)) *TraceFn)(void*,const ModernVec*,const ModernVec*,void*,int);
typedef void (__attribute__((thiscall)) *MouseFn)(void*,float*,float*,float*);
static WalkFn original_walk;
static TurnFn original_turn;
static InputFn original_input;
static ToggleFn original_toggle;
static CameraFn original_camera;
static int initialized, supported, enabled=1, camera_active, override_input, forward_input, forward_edge, previous_forward;
static void *tracked_player, *tracked_world, *tracked_camera;
static float yaw, pitch=12, handedness=1, distance=280, height=135, shoulder=25, sensitivity=0.25f, turn_rate=720;
static float configured_distance=-1;
static ModernVec focus;
static char config[MAX_PATH], log_path[MAX_PATH], state_path[MAX_PATH];
static DWORD last_settings, last_state;
static int toggle_held, invert_y, last_active=-1;
static unsigned walk_calls, camera_calls, obstacle_hits;

static int readable(const void* p,size_t n) {
    MEMORY_BASIC_INFORMATION m;
    return p && VirtualQuery(p,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
        (uintptr_t)p+n >= (uintptr_t)p && (uintptr_t)p+n <= (uintptr_t)m.BaseAddress+m.RegionSize;
}
static void* pointer(uintptr_t a) { return readable((void*)a,4)?*(void**)a:NULL; }
static int integer(void* p,unsigned o) { return *(int*)((char*)p+o); }
static ModernVec at(void* p) {
    const float* m=(const float*)((char*)p+G2A_VOB_MATRIX);
    return (ModernVec){m[2],m[6],m[10]};
}
static ModernVec position(void* p) {
    const float* m=(const float*)((char*)p+G2A_VOB_MATRIX);
    return (ModernVec){m[3],m[7],m[11]};
}
static void log_line(const char* text) {
    FILE* f=fopen(log_path,"a");
    if(f) { fprintf(f,"%lu %s\n",GetTickCount(),text); fclose(f); }
}
static float setting(const char* key,float fallback,float lo,float hi) {
    char value[64],def[32]; snprintf(def,sizeof(def),"%.9g",fallback);
    GetPrivateProfileStringA("Controls",key,def,value,sizeof(value),config);
    char* end; float number=strtof(value,&end);
    return end!=value && !*end && isfinite(number)?modern_clamp(number,lo,hi):fallback;
}
static void settings(void) {
    if(GetTickCount()-last_settings<1000 && last_settings) return;
    last_settings=GetTickCount();
    enabled=GetPrivateProfileIntA("Controls","Enabled",1,config)!=0;
    sensitivity=setting("MouseSensitivity",0.25f,0.01f,3);
    float requested_distance=setting("CameraDistance",280,100,600);
    if(requested_distance!=configured_distance) distance=configured_distance=requested_distance;
    height=setting("CameraHeight",135,60,200);
    shoulder=setting("ShoulderOffset",25,-80,80);
    turn_rate=setting("TurnRate",720,90,1800);
    invert_y=GetPrivateProfileIntA("Controls","InvertY",0,config)!=0;
}
static int camera_mode(void* ai) {
    if(!readable(ai,MC_CAMERA_SIZE) || integer(ai,MC_CAMERA_CUTSCENE) || integer(ai,MC_CAMERA_FIRST_PERSON)) return 0;
    char* mode=pointer((uintptr_t)ai+MC_CAMERA_MODE+8);
    int length=integer(ai,MC_CAMERA_MODE+12);
    if(length<1 || length>40 || !readable(mode,(size_t)length+1) || mode[length]) return 0;
    const char* allowed[]={"CAMMODNORMAL","CAMMODRUN","CAMMODJUMP","CAMMODJUMPUP","CAMMODFALL","CAMMODMELEE"};
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i) if(!_stricmp(mode,allowed[i])) return 1;
    return 0;
}
/* Re-evaluate at each hook. Never depend on last frame's player, focus or mode. */
static int context(void* ai,void** player,void** world,int movement) {
    if(!supported || !enabled) return 0;
    DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()) return 0;
    void* game=pointer(G2A_GAME); void* manager=pointer(G2A_MANAGER);
    *player=pointer(G2A_PLAYER);
    if(!readable(game,G2A_GAME_WORLD_ENTERED+4) || !manager || !readable(*player,MC_NPC_HUMAN_AI+4)) return 0;
    if(!((QueryFn)G2A_IS_RUNNING)(manager) || integer(game,G2A_GAME_LOADING) || integer(game,G2A_GAME_LEVEL_CHANGE) || integer(game,G2A_GAME_PAUSE)) return 0;
    *world=pointer((uintptr_t)game+G2A_SESSION_WORLD);
    if(!*world || pointer((uintptr_t)*player+G2A_VOB_HOME_WORLD)!=*world || !camera_mode(ai) ||
       pointer((uintptr_t)ai+MC_CAMERA_TARGET)!=*player || !readable(pointer((uintptr_t)ai+MC_CAMERA_VOB),G2A_VOB_FLAGS+1)) return 0;
    if(((QueryFn)G2A_NPC_DEAD)(*player) || ((QueryFn)MC_NPC_UNCONSCIOUS)(*player) ||
       pointer((uintptr_t)*player+MC_NPC_INTERACT_MOB)) return 0;
    unsigned body=(unsigned)((QueryFn)G2A_NPC_BODY)(*player);
    return modern_body_allowed(body,((QueryFn)G2A_NPC_WEAPON)(*player),movement);
}
static void reset(void) {
    camera_active=override_input=forward_input=forward_edge=previous_forward=0;
    tracked_player=tracked_world=tracked_camera=NULL;
}
static void seed(void* player,void* world,void* camera) {
    if(camera_active && tracked_player==player && tracked_world==world && tracked_camera==camera) return;
    ModernVec look=at(camera), fwd=modern_flat(at(player));
    const float* m=(const float*)((char*)player+G2A_VOB_MATRIX);
    handedness=(fwd.z*m[0]-fwd.x*m[8])>=0?1.0f:-1.0f;
    yaw=modern_yaw(modern_flat(look));
    pitch=modern_clamp(atan2f(-look.y,hypotf(look.x,look.z))*57.295779513f,-35,60);
    focus=modern_target(position(player),(const float*)((char*)player+G2A_VOB_BBOX),height);
    tracked_player=player; tracked_world=world; tracked_camera=camera; camera_active=1;
}
static int down(int vk) { return (GetAsyncKeyState(vk)&0x8000)!=0; }
static float dt(void) {
    float value=*(float*)(MC_TIMER+MC_TIMER_FRAME_MS)*0.001f;
    return isfinite(value)?modern_clamp(value,0,0.05f):0;
}
static void __attribute__((fastcall)) walk_hook(void* self,void* unused) {
    (void)unused;
    void *player=NULL,*world=NULL,*ai=pointer(MC_CAMERA_CURRENT);
    if(!context(ai,&player,&world,1) || pointer((uintptr_t)player+MC_NPC_HUMAN_AI)!=self ||
       ((QueryFn)G2A_NPC_IS_MOVLOCK)(player)) { previous_forward=0; original_walk(self); return; }
    /* Keep Gothic's interaction handling when its action modifier is held. */
    void* input=pointer(MC_INPUT);
    if(!input || original_input(input,5)!=0) { previous_forward=0; original_walk(self); return; }
    seed(player,world,pointer((uintptr_t)ai+MC_CAMERA_VOB));
    int up=down('W')-down('S'),right=down('D')-down('A');
    forward_input=0;
    if(up || right) {
        ModernVec want=modern_direction(modern_forward(yaw),handedness,up,right);
        ModernVec turned=modern_turn(modern_flat(at(player)),want,turn_rate,dt());
        ((VectorFn)MC_SET_HEADING)(player,&turned);
        /* Turn before stepping if the desired direction is behind Marvin. */
        forward_input=turned.x*want.x+turned.z*want.z>0.3f;
    }
    forward_edge=forward_input&&!previous_forward;
    previous_forward=forward_input;
    int saved=override_input; override_input=1;
    ++walk_calls; original_walk(self); override_input=saved;
}
static int remap(unsigned short key,float* value) {
    if(!override_input) return 0;
    switch(key) {
    case 3: *value=(float)forward_input; return 1;
    case 1: case 2: case 4: case 18: case 19: *value=0; return 1;
    default: return 0;
    }
}
static float __attribute__((fastcall)) input_hook(void* self,void* unused,unsigned short key) {
    (void)unused; float value;
    return remap(key,&value)?value:original_input(self,key);
}
static int __attribute__((fastcall)) toggle_hook(void* self,void* unused,unsigned short key) {
    (void)unused; float value;
    return remap(key,&value)?(key==3?forward_edge:0):original_toggle(self,key);
}
static void __attribute__((fastcall)) turn_hook(void* self,void* unused,int force) {
    (void)unused;
    if(!override_input) original_turn(self,force);
}
static void __attribute__((fastcall)) camera_hook(void* self,void* unused,void* vob,int* deleted) {
    (void)unused;
    original_camera(self,vob,deleted);
    void *player=NULL,*world=NULL;
    if(!context(self,&player,&world,0) || (deleted&&*deleted)) { reset(); return; }
    void* camera=pointer((uintptr_t)self+MC_CAMERA_VOB);
    seed(player,world,camera);
    float dx=0,dy=0,wheel=0;
    void* input=pointer(MC_INPUT);
    if(input && readable(*(void**)input,0x54)) {
        MouseFn mouse=*(MouseFn*)((char*)*(void**)input+0x50);
        mouse(input,&dx,&dy,&wheel); /* virtual dispatch preserves Union's mouse wrapper */
    }
    if(!isfinite(dx)||!isfinite(dy)||fabsf(dx)>2000||fabsf(dy)>2000) dx=dy=0;
    yaw=modern_angle(yaw+dx*sensitivity*0.01745329252f*handedness);
    pitch=modern_clamp(pitch+(invert_y?-dy:dy)*sensitivity,-35,60);
    if(isfinite(wheel) && wheel!=0) distance=modern_clamp(distance-modern_clamp(wheel,-1,1)*20,100,600);
    ModernVec target=modern_target(position(player),(const float*)((char*)player+G2A_VOB_BBOX),height);
    float delta=modern_length(modern_add(target,modern_scale(focus,-1)));
    float k=delta>400?1:1-expf(-dt()/0.06f);
    focus=modern_add(focus,modern_scale(modern_add(target,modern_scale(focus,-1)),k));
    ModernVec look=modern_look(yaw,pitch),right=modern_right(modern_forward(yaw),handedness);
    ModernVec ray=modern_add(modern_scale(look,-distance),modern_scale(right,shoulder));
    float len=modern_length(ray),fraction=1;
    /* Centre and four near-plane offsets keep a margin from walls/door frames. */
    ModernVec offsets[5]={{0,0,0},modern_scale(right,12),modern_scale(right,-12),{0,12,0},{0,-12,0}};
    for(unsigned i=0;i<5;++i) {
        ModernVec start=modern_add(focus,offsets[i]);
        if(((TraceFn)G2A_TRACE_NEAREST)(world,&start,&ray,player,1|32|256|2048)) {
            ModernVec hit=*(ModernVec*)((char*)world+G2A_WORLD_REPORT+G2A_REPORT_INTERSECTION);
            float travelled=modern_length(modern_add(hit,modern_scale(start,-1)));
            if(isfinite(travelled)) fraction=fminf(fraction,fmaxf(0,(travelled-15)/len));
        }
    }
    if(fraction<1) ++obstacle_hits;
    ModernVec destination=modern_add(focus,modern_scale(ray,fraction));
    ((VectorFn)MC_SET_POSITION)(camera,&destination);
    ((VectorFn)MC_SET_HEADING)(camera,&look);
    ++camera_calls;
}
static void initialize(void) {
    if(initialized) return; initialized=1;
    char root[MAX_PATH]; GetModuleFileNameA(NULL,root,sizeof(root));
    char* slash=strrchr(root,'\\'); if(!slash || _stricmp(slash+1,"Gothic2.exe")) return;
    *slash=0; slash=strrchr(root,'\\'); if(!slash) return; *slash=0;
    snprintf(config,sizeof(config),"%s\\.modding\\skate3-archolos\\modern-controls.ini",root);
    snprintf(log_path,sizeof(log_path),"%s\\.modding\\skate3-archolos\\modern-controls.log",root);
    snprintf(state_path,sizeof(state_path),"%s\\.modding\\skate3-archolos\\modern-controls-state.json",root);
    if((uintptr_t)GetModuleHandleA(NULL)!=G2A_IMAGE_BASE || !GetModuleHandleA("Shw32.dll")) return;
    const IMAGE_DOS_HEADER* dos=(const IMAGE_DOS_HEADER*)G2A_IMAGE_BASE;
    const IMAGE_NT_HEADERS32* pe=(const IMAGE_NT_HEADERS32*)((const char*)dos+dos->e_lfanew);
    if(pe->FileHeader.TimeDateStamp!=G2A_IMAGE_TIMESTAMP || pe->OptionalHeader.SizeOfImage!=G2A_IMAGE_SIZE) return;
    /* Refuse unknown pre-existing detours at the entry points we will own. */
    for(unsigned i=0;i<sizeof(mc_fingerprints)/sizeof(mc_fingerprints[0]);++i)
        if(memcmp((void*)mc_fingerprints[i].address,mc_fingerprints[i].code,12)) {
            log_line("An input/camera entry point differs; modern controls were not installed."); return;
        }
    if(MH_Initialize()!=MH_OK) { log_line("MinHook initialization failed."); return; }
    void* targets[]={(void*)MC_WALK,(void*)MC_TURN,(void*)MC_GET_STATE,(void*)MC_GET_TOGGLED,(void*)MC_CAMERA_DO_AI};
    void* detours[]={(void*)walk_hook,(void*)turn_hook,(void*)input_hook,(void*)toggle_hook,(void*)camera_hook};
    void** originals[]={(void**)&original_walk,(void**)&original_turn,(void**)&original_input,(void**)&original_toggle,(void**)&original_camera};
    unsigned made=0;
    for(;made<5;++made) if(MH_CreateHook(targets[made],detours[made],originals[made])!=MH_OK) break;
    if(made!=5) { for(unsigned i=0;i<made;++i) MH_RemoveHook(targets[i]); log_line("Could not create all control hooks; disabled."); return; }
    /* Queue only OUR hooks: never toggle another plugin's hooks. */
    for(unsigned i=0;i<5;++i) MH_QueueEnableHook(targets[i]);
    if(MH_ApplyQueued()!=MH_OK) {
        for(unsigned i=0;i<5;++i) MH_DisableHook(targets[i]);
        log_line("Could not enable all control hooks; disabled."); return;
    }
    supported=1; settings();
    log_line("Modern controls loaded: mouse orbit in exploration/melee, camera-relative exploration WASD, F6 toggles. Ranged/magic use Free Aim.");
}
__declspec(dllexport) void __cdecl Game_Entry(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_Init(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_PostLoop(void) {
    initialize(); if(!supported) return;
    settings();
    DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground==GetCurrentProcessId()) {
        int held=down(VK_F6);
        if(held&&!toggle_held) {
            enabled=!enabled;
            WritePrivateProfileStringA("Controls","Enabled",enabled?"1":"0",config);
            reset(); log_line(enabled?"F6: modern controls enabled.":"F6: original Gothic controls enabled.");
        }
        toggle_held=held;
    } else { toggle_held=0; reset(); }
    if(last_active!=camera_active) { last_active=camera_active; log_line(camera_active?"Modern camera active.":"Modern camera yielded to Gothic."); }
    if(GetTickCount()-last_state>500) {
        last_state=GetTickCount();
        FILE* f=fopen(state_path,"w");
        if(f) { fprintf(f,"{\"pid\":%lu,\"enabled\":%d,\"cameraActive\":%d,\"yaw\":%.7g,\"pitch\":%.7g,\"walkCalls\":%u,\"cameraCalls\":%u,\"obstacleHits\":%u}\n",GetCurrentProcessId(),enabled,camera_active,yaw,pitch,walk_calls,camera_calls,obstacle_hits); fclose(f); }
    }
}
__declspec(dllexport) void __cdecl Game_MenuLoop(void) { reset(); }
__declspec(dllexport) void __cdecl Game_Pause(void) { reset(); }
__declspec(dllexport) void __cdecl Game_SaveBegin(void) { reset(); }
__declspec(dllexport) void __cdecl Game_LoadBegin_NewGame(void) { reset(); }
__declspec(dllexport) void __cdecl Game_LoadBegin_SaveGame(void) { reset(); }
__declspec(dllexport) void __cdecl Game_LoadBegin_ChangeLevel(void) { reset(); }
__declspec(dllexport) void __cdecl Game_Exit(void) { reset(); }
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)reserved; if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module); return TRUE;
}

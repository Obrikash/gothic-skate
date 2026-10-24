/* Experimental local skating slice. Union event exports, never render detours.
 * The x64 worker starts in the background once a world runs. F8 captures the
 * collision around Marvin in-process, reloads it in the warm worker and mounts.
 * The worker's rider pose is retargeted onto Gothic's live model nodes. */
#include "worker_client.h"
#include "capture_core.h"
#include "retarget_core.h"
#include "ride_clock.h"
#include "gothic_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct Vec3 { float x, y, z; } Vec3;
typedef int (__attribute__((thiscall)) *QueryFn)(void*);
typedef void (__attribute__((thiscall)) *StateFn)(void*, int);
typedef void (__attribute__((fastcall)) *CollisionFn)(void*, int);
typedef void (__attribute__((thiscall)) *TransformFn)(void*, const float*);
typedef void (__attribute__((thiscall)) *LineFn)(void*, const Vec3*, const Vec3*, uint32_t, int);
typedef int (__attribute__((fastcall)) *TraceFn)(void*, const Vec3*, const Vec3*, void*, int);
typedef void (__attribute__((thiscall)) *StringCtorFn)(void*, const char*);
typedef void (__attribute__((thiscall)) *StringDtorFn)(void*);
typedef void (__attribute__((thiscall)) *PrintFn)(void*, int, const void*);
typedef void* (__attribute__((thiscall)) *ModelFn)(void*);
typedef int (__attribute__((thiscall)) *AniIdFn)(void*, const void*);
typedef void* (__attribute__((thiscall)) *AniFromIdFn)(void*, int);
typedef int (__attribute__((thiscall)) *AniActiveFn)(void*, void*);
typedef void (__attribute__((thiscall)) *StartAniFn)(void*, int, int);
typedef void (__attribute__((thiscall)) *StopAniFn)(void*, int);
typedef void (__attribute__((thiscall)) *MovLockFn)(void*, int);
typedef void* (__cdecl *CreateInstanceFn)(const void*);
typedef int (__attribute__((thiscall)) *ReleaseFn)(void*);
typedef void (__attribute__((thiscall)) *SetVisualFn)(void*, const void*);
typedef void (__attribute__((thiscall)) *RemoveFromWorldFn)(void*);
typedef void* (__attribute__((thiscall)) *AddVobFn)(void*, void*);
typedef void (__attribute__((thiscall)) *AdvanceFn)(void*);
static SkateClient worker;
static char directory[MAX_PATH], config[MAX_PATH], log_path[MAX_PATH], state_path[MAX_PATH];
/* The warm worker's lab: .modding/skate3-archolos/host-worker-lab, prepared by
   tools/prepare_warm_lab.py. In game, a LocalAppData lab was reported missing
   (Windows error 3) although it existed; this one launches and its captures
   reach the worker. */
static char lab[MAX_PATH], lab_assets[MAX_PATH];
static char notice[180];
static DWORD notice_time;
static int initialized, supported, phase, frozen, f8_held, space_held;
static void *mounted_player, *mounted_world;
static unsigned char saved_flags, saved_sleep;
static int saved_movlock;
static Vec3 origin;
static float floor_y, offset_y, heading, radius, matrix[16], safe_matrix[16], last_output[14];
static RideClock ride_clock;
static int unsafe_applied;
/* Filled ground support of the current capture; NaN = none. */
static float support_grid[GROUND_CELLS];
/* Ride frame (core-space origin, fixed for a ride) and the centre of the
   current capture. Rolling recaptures move the scan, never the frame, so the
   worker's positions stay valid. */
static Vec3 frame, scan;
static GroundCentre grid_centre;
static float height_reference;
static DWORD last_recapture_try;
static unsigned recaptures;
#define RECAPTURE_DISTANCE 7.0f /* m from the grid centre; the grid reaches 15 m */
/* Warm worker: started in the background, kept across mounts. */
static unsigned worker_failures, capture_area;
static unsigned submitted_area;
static int reload_pending;
static DWORD reload_started;
static DWORD worker_retry_at;
static int worker_announced, want_mount;
static DWORD want_since;
/* Rider: F7 cycles 0 skate (the worker's own Skate rider pose retargeted onto
   Marvin's skeleton every frame), 1 pose (a Gothic animation: [Rider]
   Ground/Air, advanced by the bridge), 2 off. Marvin's vob sleeps while riding,
   so Gothic's animation code never overwrites the node trafos written here. */
static int rider_ground = -1, rider_air = -1, rider_current = -1, rider_air_frames;
static int rider_method, f7_held;
static const char* const rider_method_names[] = {"skate", "pose", "off"};
static RetargetSetup skate_rig;
static char* skate_nodes[RETARGET_MAX_NODES];
static char* skate_node_list;
static int skate_node_count, skate_ready, skate_have_pose;
static char* skate_model;
static float rider_saved_local[RETARGET_MAX_NODES][16];
static int rider_enabled, rider_dirty, rider_pose_warned;
static float skate_pose[36][13], skate_board[13];
static unsigned rider_restarts;
static DWORD rider_window;
/* Board: a game mesh in an engine-allocated zCVob, never archived or traced. */
static void* board_vob;
static float board_lift, board_yaw_offset;
static DWORD last_status, release_until;
static float release_x;
static unsigned responses;
static Vec3 observed;
static void* observed_world;
static int menu_seen;
static ObjectBox object_boxes[4096];
static Rail rails[CAPTURE_RAIL_LIMIT];
/* Geometry snapshots cannot follow a moving ship/prop. Retain the exact
   contributing objects and dismount if their transforms change. Stage a new
   watch list until its collision files have been written successfully. */
typedef struct CollisionWatch { char* vob; void* visual; float transform[16]; } CollisionWatch;
static CollisionWatch capture_watch[512], collision_watch[512];
static unsigned capture_watch_count, collision_watch_count;
static int capture_watch_overflow;

static void log_line(const char* text) {
    FILE* file = fopen(log_path, "a");
    if (file) { fprintf(file, "%lu %s\n", GetTickCount(), text); fclose(file); }
}

static void inform(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
    notice_time = GetTickCount();
    log_line(text);
}

static int readable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info))) return 0;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
        (uintptr_t)pointer + size <= (uintptr_t)info.BaseAddress + info.RegionSize;
}

static void* pointer_at(uintptr_t address) {
    return readable((void*)address, 4) ? *(void**)address : NULL;
}

static int enabled(void) { return GetPrivateProfileIntA("Bridge", "Enabled", 1, config); }

static int active_world(void** player, void** world) {
    void* game = pointer_at(G2A_GAME);
    void* manager = pointer_at(G2A_MANAGER);
    *player = pointer_at(G2A_PLAYER);
    if (!game || !manager || !*player || !readable(game, G2A_GAME_WORLD_ENTERED + 4) ||
        !readable(*player, G2A_VOB_SLEEP_FLAGS + 1)) return 0;
    if (!((QueryFn)G2A_IS_RUNNING)(manager) || *(int*)((char*)game + G2A_GAME_LOADING) ||
        *(int*)((char*)game + G2A_GAME_LEVEL_CHANGE) || *(int*)((char*)game + G2A_GAME_PAUSE)) return 0;
    *world = *(void**)((char*)game + G2A_SESSION_WORLD);
    if (!*world || pointer_at((uintptr_t)*player + G2A_VOB_HOME_WORLD) != *world) return 0;
    if (((QueryFn)G2A_NPC_DEAD)(*player) || ((QueryFn)G2A_NPC_WEAPON)(*player)) return 0;
    /* Mount only ordinary standing/walking states, never dialog, inventory,
       swimming, combat, transformations or scripted interaction states. */
    unsigned body = (unsigned)((QueryFn)G2A_NPC_BODY)(*player);
    return (body & 127) <= 4 && !(body & (128 | 1024 | 2048 | 4096 | 8192));
}

static void status(void) {
    char temporary[MAX_PATH];
    snprintf(temporary, sizeof(temporary), "%s.new", state_path);
    FILE* file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "{\"pid\":%lu,\"phase\":%d,\"movementAuthority\":%s,\"responses\":%u,\"restored\":%s,\"workerReady\":%s,\"worldPointer\":%u,\"hostPosition\":[%.9g,%.9g,%.9g],\"output\":[",
        GetCurrentProcessId(), phase, frozen ? "true" : "false", responses, frozen ? "false" : "true",
        worker.shared && worker.shared->ready == 2 ? "true" : "false",
        (unsigned)(uintptr_t)observed_world, observed.x, observed.y, observed.z);
    for (unsigned i = 0; i < 14; ++i) fprintf(file, "%s%.9g", i ? "," : "", last_output[i]);
    fputs("]}\n", file);
    if (!fclose(file)) MoveFileExA(temporary, state_path, MOVEFILE_REPLACE_EXISTING);
}

static int animation_id(void* model, const char* name) {
    uint32_t string_storage[5]; /* zSTRING's verified x86 ABI, including vptr. */
    ((StringCtorFn)G2A_STRING_CTOR)(string_storage, name);
    int id = ((AniIdFn)G2A_MODEL_ANI_ID)(model, string_storage);
    ((StringDtorFn)G2A_STRING_DTOR)(string_storage);
    return id;
}

/* Bind the retarget to Marvin's live skeleton: node names, parents and bind
   trafos from each zCModelNodeInst's prototype. */
static int skate_bind(char* model) {
    static char names[RETARGET_MAX_NODES][64];
    static const char* name_list[RETARGET_MAX_NODES];
    static int parent[RETARGET_MAX_NODES];
    static float local[RETARGET_MAX_NODES][16];
    char line[200];
    if (!readable(model, G2A_MODEL_NODE_LIST + 12)) return 0;
    char* list = *(char**)(model + G2A_MODEL_NODE_LIST);
    int count = *(int*)(model + G2A_MODEL_NODE_LIST + 8);
    skate_ready = 0; rider_dirty = 0; skate_model = NULL;
    if (count < 1 || count > RETARGET_MAX_NODES || !readable(list, count * 4)) return 0;
    for (int i = 0; i < count; ++i) {
        char* node = ((char**)list)[i];
        char* proto = node && readable(node, G2A_NODEINST_TRAFO + 64) ? *(char**)(node + G2A_NODEINST_PROTO) : NULL;
        if (!proto || !readable(proto, G2A_NODE_TRAFO + 64)) return 0;
        const char* text = *(const char**)(proto + G2A_NODE_NAME + 8); /* zSTRING _Ptr, _Len */
        unsigned length = *(const unsigned*)(proto + G2A_NODE_NAME + 12);
        if (!text || length > 63 || !readable(text, length)) return 0;
        memcpy(names[i], text, length); names[i][length] = 0;
        name_list[i] = names[i];
        memcpy(local[i], proto + G2A_NODE_TRAFO, 64);
        memcpy(rider_saved_local[i], node + G2A_NODEINST_TRAFO, 64);
        skate_nodes[i] = node;
        char* parent_node = *(char**)(node + G2A_NODEINST_PARENT);
        parent[i] = -1;
        for (int k = 0; k < i && parent_node; ++k) if (skate_nodes[k] == parent_node) parent[i] = k;
        if (parent_node && parent[i] < 0) return 0;
    }
    skate_model = model; skate_node_list = list; skate_node_count = count;
    skate_ready = retarget_setup(&skate_rig, count, name_list, parent, local);
    snprintf(line, sizeof(line), "Skate rider retarget %s on %d model nodes.", skate_ready ? "bound" : "unavailable (Biped bones missing)", count);
    log_line(line);
    return skate_ready;
}

static int skate_binding_current(char* model) {
    if (model != skate_model || !readable(model, G2A_MODEL_NODE_LIST + 12) ||
        *(char**)(model + G2A_MODEL_NODE_LIST) != skate_node_list ||
        *(int*)(model + G2A_MODEL_NODE_LIST + 8) != skate_node_count ||
        !readable(skate_node_list, skate_node_count * 4)) return 0;
    for (int i = 0; i < skate_node_count; ++i)
        if (((char**)skate_node_list)[i] != skate_nodes[i] || !readable(skate_nodes[i], G2A_NODEINST_TRAFO + 64)) return 0;
    return 1;
}

/* Off/dismount must actually undo our direct skeleton writes. Restore only
   the exact live model/list/nodes captured at bind time (an armour rebuild
   invalidates those pointers). Gothic resumes its own animation afterward. */
static void rider_restore(char* model) {
    if (!rider_dirty) return;
    rider_dirty = 0;
    if (!skate_binding_current(model)) return;
    for (int i = 0; i < skate_node_count; ++i)
        memcpy(skate_nodes[i] + G2A_NODEINST_TRAFO, rider_saved_local[i], 64);
}

/* Write the retargeted Skate pose into Marvin's node trafos. */
static int skate_apply(char* model) {
    if (!skate_have_pose) return 0;
    if (!skate_binding_current(model)) {
        if (!skate_bind(model)) return 0; /* the model was rebuilt (armour change) */
    }
    if (!skate_ready) return 0;
    static float locals[RETARGET_MAX_NODES][16];
    const float rotation[9] = {matrix[0], matrix[1], matrix[2], matrix[4], matrix[5], matrix[6], matrix[8], matrix[9], matrix[10]};
    const float position[3] = {matrix[3], matrix[7], matrix[11]};
    const float ride_origin[3] = {origin.x, origin.y, origin.z};
    retarget_pose(&skate_rig, (const float (*)[13])skate_pose, rotation, position, ride_origin, locals);
    if (!retarget_locals_valid(skate_node_count, (const float (*)[16])locals)) return 0;
    if (rider_current >= 0) ((StopAniFn)G2A_MODEL_STOP_ANI)(model, rider_current);
    rider_current = -1; rider_dirty = 1;
    for (int i = 0; i < skate_node_count; ++i) memcpy(skate_nodes[i] + G2A_NODEINST_TRAFO, locals[i], 64);
    return 1;
}

static void rider_begin(void* player) {
    rider_ground = rider_air = rider_current = -1;
    rider_air_frames = 0; rider_restarts = 0; rider_window = GetTickCount();
    skate_have_pose = skate_ready = rider_dirty = rider_pose_warned = 0;
    skate_model = NULL;
    rider_enabled = GetPrivateProfileIntA("Rider", "Enabled", 1, config) != 0;
    void* model = ((ModelFn)G2A_NPC_MODEL)(player);
    if (!model || !rider_enabled) return;
    skate_bind((char*)model);
    char ground[64], air[64], line[200];
    GetPrivateProfileStringA("Rider", "Ground", "S_SLIDE", ground, sizeof(ground), config);
    /* Not S_JUMP/S_FALL*: oCAniCtrl_Human::CheckSpecialStates acts on its own
       jump state and can swap in S_FALLDN. Gothic starts S_SLIDE/S_SLIDEB only
       from zCAIPlayer::CheckFloorSliding, which needs physics, off here. */
    GetPrivateProfileStringA("Rider", "Air", "S_SLIDEB", air, sizeof(air), config);
    rider_ground = animation_id(model, ground);
    rider_air = animation_id(model, air);
    snprintf(line, sizeof(line), "Rider animations: ground %s id %d, air %s id %d.", ground, rider_ground, air, rider_air);
    log_line(line);
}

static void rider_update(void) {
    void* model = ((ModelFn)G2A_NPC_MODEL)(mounted_player);
    if (!model || !rider_enabled || rider_method == 2) return;
    if (rider_method == 0 && skate_apply((char*)model)) return;
    if (rider_method == 0) {
        rider_restore((char*)model);
        if (!rider_pose_warned) { log_line("Skate rider pose unavailable or invalid; using the Gothic pose."); rider_pose_warned = 1; }
    }
    /* output[13] is 1 in ground physics. Require 0.1 s airborne before the
       air pose so small bumps do not flicker. */
    rider_air_frames = last_output[13] > .5f ? 0 : rider_air_frames + 1;
    int want = rider_air >= 0 && rider_air_frames > 6 ? rider_air : rider_ground;
    if (want < 0) return;
    if (want != rider_current || !((AniActiveFn)G2A_MODEL_ANI_ACTIVE)(model, ((AniFromIdFn)G2A_MODEL_ANI_FROM_ID)(model, want))) {
        if (want == rider_current) ++rider_restarts;
        ((StartAniFn)G2A_MODEL_START_ANI)(model, want, 0);
        rider_current = want;
    }
    DWORD now = GetTickCount();
    /* Marvin's vob sleeps while riding, so Gothic does not advance his model;
       do it here so the Gothic pose actually plays. */
    ((AdvanceFn)G2A_MODEL_ADVANCE)(model);
    if (model == skate_model) rider_dirty = 1;
    if (now - rider_window >= 1000) {
        if (rider_restarts) {
            char line[120];
            snprintf(line, sizeof(line), "Rider animation was replaced and restarted %u times in the last second.", rider_restarts);
            log_line(line);
        }
        rider_restarts = 0; rider_window = now;
    }
}

static void rider_end(void* player) {
    void* model = ((ModelFn)G2A_NPC_MODEL)(player);
    if (model && rider_current >= 0) ((StopAniFn)G2A_MODEL_STOP_ANI)(model, rider_current);
    if (model) rider_restore((char*)model);
    rider_current = -1;
}

/* Apply an animation method to the mounted player (see rider_method). */
static void rider_apply_method(void* player, int method) {
    rider_end(player);
    rider_method = rider_enabled ? method : 2;
    /* Only the "pose" method plays a Gothic animation; the skate rider writes
       the skeleton directly. */
    char line[96];
    snprintf(line, sizeof(line), "Rider animation method: %s.", rider_method_names[rider_method]);
    log_line(line);
}

static void board_matrix(float* out) {
    float yaw = last_output[3] + board_yaw_offset;
    memset(out, 0, 16 * sizeof(float));
    out[0] = cosf(yaw); out[8] = sinf(yaw);
    out[5] = 1;
    out[2] = -sinf(yaw); out[10] = cosf(yaw);
    if (skate_have_pose) {
        /* The worker's deck (board[0]: rotation rows, Z-up) with columns width,
           length, up. The plank's local X is its length and Y its thickness, so
           kickflips and grinds turn the plank exactly as the deck turns. */
        float length[3] = {skate_board[1], skate_board[7], skate_board[4]}; /* column 1, y/z swapped */
        float up[3] = {skate_board[2], skate_board[8], skate_board[5]};     /* column 2, y/z swapped */
        float l = sqrtf(length[0] * length[0] + length[1] * length[1] + length[2] * length[2]);
        float d = (length[0] * up[0] + length[1] * up[1] + length[2] * up[2]) / (l > 1e-6f ? l * l : 1);
        for (int k = 0; k < 3; ++k) up[k] -= length[k] * d;
        float u = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (l > 1e-6f && u > 1e-6f) {
            for (int k = 0; k < 3; ++k) { length[k] /= l; up[k] /= u; }
            float side[3] = {length[1] * up[2] - length[2] * up[1], length[2] * up[0] - length[0] * up[2], length[0] * up[1] - length[1] * up[0]};
            out[0] = length[0]; out[4] = length[1]; out[8] = length[2];
            out[1] = up[0]; out[5] = up[1]; out[9] = up[2];
            out[2] = side[0]; out[6] = side[1]; out[10] = side[2];
        }
    }
    out[3] = origin.x + last_output[0] * 100;
    out[7] = origin.y + last_output[2] * 100;
    out[11] = origin.z + last_output[1] * 100;
    out[15] = 1;
}

static void board_begin(void* world) {
    char mesh[64], line[160];
    GetPrivateProfileStringA("Board", "Mesh", "KM_VOB_CRAFTINGBOARDS.3DS", mesh, sizeof(mesh), config);
    /* The plank's long axis is its local X; turn it onto the rider's forward. */
    board_yaw_offset = (float)GetPrivateProfileIntA("Board", "YawDegrees", 90, config) * 3.14159265f / 180;
    board_lift = 0;
    if (!mesh[0]) return;
    uint32_t class_name[5], visual[5]; /* zSTRING's verified x86 ABI, including vptr. */
    ((StringCtorFn)G2A_STRING_CTOR)(class_name, "zCVob");
    void* vob = ((CreateInstanceFn)G2A_CREATE_INSTANCE)(class_name);
    ((StringDtorFn)G2A_STRING_DTOR)(class_name);
    if (!vob || !readable(vob, G2A_VOB_ARCHIVE_FLAGS + 1)) { log_line("Board object could not be created; using the outline."); return; }
    *((unsigned char*)vob + G2A_VOB_ARCHIVE_FLAGS) |= G2A_VOB_DONT_ARCHIVE_BIT;
    *((unsigned char*)vob + G2A_VOB_FLAGS) |= G2A_VOB_IGNORED_BY_TRACE_BIT | G2A_VOB_SHOW_VISUAL_BIT;
    ((CollisionFn)G2A_SET_STATIC_COLLISION)(vob, 0);
    ((CollisionFn)G2A_SET_DYNAMIC_COLLISION)(vob, 0);
    ((StateFn)G2A_SET_PHYSICS)(vob, 0);
    ((StringCtorFn)G2A_STRING_CTOR)(visual, mesh);
    ((SetVisualFn)G2A_VOB_SET_VISUAL)(vob, visual);
    ((StringDtorFn)G2A_STRING_DTOR)(visual);
    float placed[16];
    board_matrix(placed);
    ((TransformFn)G2A_SET_TRANSFORM)(vob, placed);
    ((AddVobFn)G2A_WORLD_ADD_VOB)(world, vob);
    board_vob = vob;
    /* Stand on the deck: the plank is about 8 cm thick around its origin. */
    board_lift = (float)GetPrivateProfileIntA("Board", "LiftCentimeters", 4, config);
    snprintf(line, sizeof(line), "Board object %s added to the world (not archived, ignored by traces).", mesh);
    log_line(line);
}

static void board_update(void) {
    if (!board_vob) return;
    float placed[16];
    board_matrix(placed);
    ((TransformFn)G2A_SET_TRANSFORM)(board_vob, placed);
}

static void board_end(void) {
    if (!board_vob) return;
    void* game = pointer_at(G2A_GAME);
    void* world = game && readable(game, G2A_SESSION_WORLD + 4) ? *(void**)((char*)game + G2A_SESSION_WORLD) : NULL;
    /* Every save, load and level change dismounts first, so the world is still
       live here. If it is not, never touch the old world: leak the one vob. */
    if (world && pointer_at((uintptr_t)board_vob + G2A_VOB_HOME_WORLD) == world) {
        ((RemoveFromWorldFn)G2A_VOB_REMOVE_FROM_WORLD)(board_vob);
        ((ReleaseFn)G2A_OBJECT_RELEASE)(board_vob);
        log_line("Board object removed from the world.");
    } else log_line("Board object left behind: its world was no longer current.");
    board_vob = NULL;
}

static void stop(const char* reason) {
    if (want_mount) { want_mount = 0; if (!phase) inform(reason); }
    if (!phase && !frozen) return;
    board_end();
    /* A world/load event may already have invalidated the old NPC. Never
       dereference it unless the live player and world still match. */
    if (frozen && pointer_at(G2A_PLAYER) == mounted_player &&
        readable(mounted_player, G2A_VOB_SLEEP_FLAGS + 1) &&
        pointer_at((uintptr_t)mounted_player + G2A_VOB_HOME_WORLD) == mounted_world) {
        /* Never hand a player below the sampled floor back to Gothic physics. */
        if (unsafe_applied) {
            ((TransformFn)G2A_SET_TRANSFORM)(mounted_player, safe_matrix);
            log_line("Player returned to the last supported board position.");
        }
        rider_end(mounted_player);
        ((MovLockFn)G2A_NPC_SET_MOVLOCK)(mounted_player, saved_movlock);
        ((CollisionFn)G2A_SET_STATIC_COLLISION)(mounted_player, (saved_flags >> 6) & 1);
        ((CollisionFn)G2A_SET_DYNAMIC_COLLISION)(mounted_player, (saved_flags >> 7) & 1);
        ((StateFn)G2A_SET_PHYSICS)(mounted_player, (saved_flags >> 3) & 1);
        ((StateFn)G2A_SET_SLEEP)(mounted_player, saved_sleep);
        log_line("Original player collision, physics and sleeping state restored.");
    }
    if (phase == 3) {
        char summary[128];
        snprintf(summary, sizeof(summary), "Ride summary: %u outputs, %u rolling recaptures.", responses, recaptures);
        log_line(summary);
    }
    frozen = phase = 0;
    mounted_player = mounted_world = NULL;
    /* Keep the warm worker: unmount it now, or let the idle poll consume a
       response still in flight. A worker that failed is already stopped. */
    if (worker.shared && !worker.pending && worker.shared->ready == 2) skate_request(&worker, 3);
    inform(reason);
    status();
}

/* Lowest sampled support among the four corners of the board's cell, in
   wire coordinates: output[0] is Gothic +X, output[1] Gothic +Z, in meters. */
static int support_at(float x, float z, float* height) { return ground_support_at(support_grid, grid_centre, x, z, height); }

/* ---- In-process capture (ported from the tested Python tools) ---- */

#define BSP_RADIUS 3000.0f       /* cm: static world triangles kept around Marvin */
#define OBJECT_RADIUS 1600.0f    /* cm: matches the 14 m skating cap plus margin */

static int solid_material(const char* material);

static void watch_collision(char* vob) {
    if (capture_watch_count >= sizeof(capture_watch) / sizeof(capture_watch[0])) { capture_watch_overflow = 1; return; }
    CollisionWatch* watch = &capture_watch[capture_watch_count++];
    watch->vob = vob; watch->visual = *(void**)(vob + G2A_VOB_VISUAL);
    memcpy(watch->transform, vob + G2A_VOB_MATRIX, sizeof(watch->transform));
}

static int collision_objects_changed(void) {
    for (unsigned n = 0; n < collision_watch_count; ++n) {
        const CollisionWatch* watch = &collision_watch[n];
        const char* vob = watch->vob;
        if (!readable(vob, G2A_VOB_FLAGS + 1) || *(void* const*)(vob + G2A_VOB_HOME_WORLD) != mounted_world ||
            *(void* const*)(vob + G2A_VOB_VISUAL) != watch->visual ||
            !((unsigned char)vob[G2A_VOB_FLAGS] & G2A_VOB_DYNAMIC_COLLISION_BIT)) return 1;
        const float* m = (const float*)(vob + G2A_VOB_MATRIX);
        for (int k = 0; k < 12; ++k)
            if (!isfinite(m[k]) || fabsf(m[k] - watch->transform[k]) > (k % 4 == 3 ? 2.0f : .01f)) return 1;
    }
    return 0;
}

static unsigned capture_ground(void* player, void* world, float* sampled) {
    unsigned hits = 0;
    for (int z = -GROUND_RADIUS; z <= GROUND_RADIUS; ++z) for (int x = -GROUND_RADIUS; x <= GROUND_RADIUS; ++x) {
        Vec3 start = {scan.x + x * 100, scan.y + 20, scan.z + z * 100};
        Vec3 direction = {0, -1000, 0};
        int hit = ((TraceFn)G2A_TRACE_NEAREST)(world, &start, &direction, player,
            G2A_TRACE_IGNORE_CHARACTERS | G2A_TRACE_NORMAL | G2A_TRACE_IGNORE_NO_COLLISION);
        Vec3 contact = *(Vec3*)((char*)world + G2A_WORLD_REPORT + G2A_REPORT_INTERSECTION);
        float* cell = &sampled[(z + GROUND_RADIUS) * GROUND_SIZE + x + GROUND_RADIUS];
        *cell = NAN;
        if (hit && isfinite(contact.x) && isfinite(contact.y) && isfinite(contact.z)) { *cell = (contact.y - frame.y) / 100; ++hits; }
    }
    return hits;
}

static void core_point(const float* position, float* out) {
    out[0] = (position[0] - frame.x) / 100;
    out[1] = (position[1] - frame.y) / 100;
    out[2] = -(position[2] - frame.z) / 100;
}

static int capture_bsp(void* world, TriangleList* out, unsigned* polygons) {
    char* bsp = (char*)world + G2A_WORLD_BSP;
    unsigned count = *(unsigned*)(bsp + G2A_BSP_POLY_COUNT);
    char** list = *(char***)(bsp + G2A_BSP_POLYS);
    *polygons = count;
    if (count < 1 || count > 4000000 || !readable(list, count * sizeof(char*))) return 0;
    for (unsigned n = 0; n < count; ++n) {
        char* polygon = list[n];
        if (!polygon || polygon[G2A_POLYGON_FLAGS] & 3) continue; /* portals */
        unsigned vertices = (unsigned char)polygon[G2A_POLYGON_VERTEX_COUNT];
        if (vertices < 3 || vertices > 21) continue;
        char** vertex = *(char***)(polygon + G2A_POLYGON_VERTICES);
        const float* first = (const float*)(vertex[0] + G2A_VERTEX_POSITION);
        /* Cheap reject before reading every vertex; BSP polygons are far
           smaller than this margin. */
        if (fabsf(first[0] - scan.x) > BSP_RADIUS + 10000 || fabsf(first[2] - scan.z) > BSP_RADIUS + 10000) continue;
        float low[3] = {INFINITY, INFINITY, INFINITY}, high[3] = {-INFINITY, -INFINITY, -INFINITY}, points[21][3];
        for (unsigned v = 0; v < vertices; ++v) {
            const float* position = (const float*)(vertex[v] + G2A_VERTEX_POSITION);
            for (int axis = 0; axis < 3; ++axis) {
                low[axis] = fminf(low[axis], position[axis]); high[axis] = fmaxf(high[axis], position[axis]);
            }
            core_point(position, points[v]);
        }
        const float centre[3] = {scan.x, scan.y, scan.z};
        int outside = 0;
        for (int axis = 0; axis < 3; ++axis)
            outside |= low[axis] > centre[axis] + BSP_RADIUS || high[axis] < centre[axis] - BSP_RADIUS;
        if (outside || !isfinite(low[0] + high[0] + low[1] + high[1] + low[2] + high[2])) continue;
        if (!solid_material(*(const char* const*)(polygon + G2A_POLYGON_MATERIAL))) continue;
        for (unsigned v = 1; v + 1 < vertices; ++v) {
            /* The Z reflection reverses winding; keep the original face side. */
            const float *a = points[0], *b = points[v + 1], *c = points[v];
            float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            float nx = u[1] * w[2] - u[2] * w[1], ny = u[2] * w[0] - u[0] * w[2], nz = u[0] * w[1] - u[1] * w[0];
            if (nx * nx + ny * ny + nz * nz > 1e-12f) triangles_push(out, a, b, c);
        }
    }
    return 1;
}

/* Class names of a vob, most derived first. Every zCObject vtable starts with
   _GetClassDef, Archive, Unarchive (checked in .rdata for zCVob, oCMOB and
   oCNpc); in this build _GetClassDef is `mov eax, <classDef>; ret`. */
static int vob_class(void* vob, int* plain_vob, int* mob, int* door, int* mover) {
    void** vtable = *(void***)vob;
    const unsigned char* code = vtable ? (const unsigned char*)vtable[0] : NULL;
    *plain_vob = *mob = *door = *mover = 0;
    if (!code || !readable(code, 6) || code[0] != 0xB8 || code[5] != 0xC3) return 0;
    const char* definition = *(const char* const*)(code + 1);
    for (int depth = 0; definition && depth < 12; ++depth) {
        if (!readable(definition, G2A_CLASSDEF_BASE + 4)) return 0;
        const char* name = *(const char* const*)(definition + 8); /* zSTRING _Ptr */
        unsigned length = *(const unsigned*)(definition + 12);    /* zSTRING _Len */
        if (!name || length > 63 || !readable(name, length)) return 0;
        if (!depth) *plain_vob = length == 5 && !memcmp(name, "zCVob", 5);
        if (length == 5 && !memcmp(name, "oCMOB", 5)) *mob = 1;
        if (length == 9 && !memcmp(name, "oCMobDoor", 9)) *door = 1;
        if (length == 7 && !memcmp(name, "zCMover", 7)) *mover = 1;
        definition = *(const char* const*)(definition + G2A_CLASSDEF_BASE);
    }
    return 1;
}

/* The exact class definition an object reports, read from its _GetClassDef. */
static const void* class_definition(const void* object) {
    void* const* vtable = *(void* const* const*)object;
    const unsigned char* code = vtable && readable(vtable, 4) ? (const unsigned char*)vtable[0] : NULL;
    if (!code || !readable(code, 6) || code[0] != 0xB8 || code[5] != 0xC3) return NULL;
    return *(const void* const*)(code + 1);
}

/* Match Gothic's own level collision filter: water and noCollDet materials
   are not solid. Unknown material memory is omitted, never guessed solid. */
static int solid_material(const char* material) {
    return material && readable(material, G2A_MATERIAL_FLAGS + 1) &&
        *(const int*)(material + G2A_MATERIAL_GROUP) != G2A_MATERIAL_WATER &&
        !((unsigned char)material[G2A_MATERIAL_FLAGS] & G2A_MATERIAL_NO_COLLISION_BIT);
}

/* Read the verified progressive-mesh layout, then use the engine-free
   extraction core (also exercised through the real worker offline). */
static unsigned mesh_triangles(const char* vob, const char* mesh, TriangleList* out) {
    if (!readable(mesh, G2A_PMESH_SUBMESH_COUNT + 4)) return 0;
    const float* m = (const float*)(vob + G2A_VOB_MATRIX);
    const float* positions = *(const float* const*)(mesh + G2A_PMESH_POSITIONS);
    unsigned position_count = *(const unsigned*)(mesh + G2A_PMESH_POSITIONS + 4);
    const char* submeshes = *(const char* const*)(mesh + G2A_PMESH_SUBMESHES);
    unsigned submesh_count = *(const unsigned*)(mesh + G2A_PMESH_SUBMESH_COUNT), added = 0;
    if (!positions || position_count > 65536 || !readable(positions, position_count * 12) ||
        submesh_count > 256 || !readable(submeshes, submesh_count * G2A_SUBMESH_SIZE)) return 0;
    const float frame_point[3] = {frame.x, frame.y, frame.z}, scan_point[3] = {scan.x, scan.y, scan.z};
    for (unsigned s = 0; s < submesh_count && !out->overflow; ++s) {
        const char* submesh = submeshes + s * G2A_SUBMESH_SIZE;
        if (!solid_material(*(const char* const*)submesh)) continue;
        const uint16_t* triangles = *(const uint16_t* const*)(submesh + G2A_SUBMESH_TRIANGLES);
        unsigned triangle_count = *(const unsigned*)(submesh + G2A_SUBMESH_TRIANGLES + 4);
        const MeshWedge* wedges = *(const MeshWedge* const*)(submesh + G2A_SUBMESH_WEDGES);
        unsigned wedge_count = *(const unsigned*)(submesh + G2A_SUBMESH_WEDGES + 4);
        if (triangle_count > 200000 || wedge_count > 65536 || !readable(triangles, triangle_count * 6) ||
            !readable(wedges, wedge_count * sizeof(MeshWedge))) continue;
        added += progmesh_faces(positions, position_count, triangles, triangle_count, wedges, wedge_count,
            m, frame_point, scan_point, BSP_RADIUS, out);
    }
    return added;
}

/* Name of a class definition (zCClassDef::className zSTRING _Ptr/_Len). */
static void class_name(const void* definition, char* out, size_t size) {
    snprintf(out, size, "?");
    if (!definition || !readable(definition, 16)) return;
    const char* text = *(const char* const*)((const char*)definition + 8);
    unsigned length = *(const unsigned*)((const char*)definition + 12);
    if (text && length < size && readable(text, length)) { memcpy(out, text, length); out[length] = 0; }
}

/* Why static meshes near the scan did or did not become collision; logged with
   each capture, plus the largest nearby objects, so a session shows the cause. */
static struct { unsigned in_range, no_collision, other_class, empty; } mesh_diag;
static struct { char* vob; float volume; } largest[5];

static void note_largest(char* vob, const float* box) {
    float volume = (box[3] - box[0]) * (box[4] - box[1]) * (box[5] - box[2]);
    for (int k = 0; k < 5; ++k) {
        if (largest[k].vob && volume <= largest[k].volume) continue;
        memmove(&largest[k + 1], &largest[k], (4 - k) * sizeof(largest[0]));
        largest[k].vob = vob; largest[k].volume = volume;
        return;
    }
}

static void log_largest(void) {
    for (int k = 0; k < 5 && largest[k].vob; ++k) {
        char* vob = largest[k].vob;
        char* visual = *(char**)(vob + G2A_VOB_VISUAL);
        char vob_name[64], visual_name[64], line[260];
        class_name(class_definition(vob), vob_name, sizeof(vob_name));
        class_name(visual ? class_definition(visual) : NULL, visual_name, sizeof(visual_name));
        const float* box = (const float*)(vob + G2A_VOB_BBOX);
        snprintf(line, sizeof(line), "  nearby object %d: %s with %s visual, flags 0x%02X, size %.0f x %.0f x %.0f cm.",
                 k + 1, vob_name, visual_name, (unsigned char)vob[G2A_VOB_FLAGS], box[3] - box[0], box[4] - box[1], box[5] - box[2]);
        log_line(line);
    }
}

static unsigned capture_objects(void* world, float feet, unsigned* visited, TriangleList* out, unsigned* meshes, unsigned* mesh_triangle_count) {
    char* root = (char*)world + G2A_WORLD_VOB_TREE;
    char* node = *(char**)(root + 4); /* zCTree: parent, firstChild, next, prev, data */
    unsigned count = 0;
    *visited = *meshes = *mesh_triangle_count = 0;
    capture_watch_count = 0; capture_watch_overflow = 0;
    memset(&mesh_diag, 0, sizeof(mesh_diag));
    memset(largest, 0, sizeof(largest));
    while (node && ++*visited < 400000) {
        char* vob = *(char**)(node + 16);
        char* visual = vob ? *(char**)(vob + G2A_VOB_VISUAL) : NULL;
        if (!vob || vob == board_vob || !visual) goto next;
        const float* box = (const float*)(vob + G2A_VOB_BBOX);
        /* Reject distant objects BEFORE class-chain/memory queries. The world
           has 70,000 vobs; only about 100 intersect this capture. */
        if (!isfinite(box[0] + box[1] + box[2] + box[3] + box[4] + box[5]) ||
            box[0] > scan.x + BSP_RADIUS || box[3] < scan.x - BSP_RADIUS ||
            box[2] > scan.z + BSP_RADIUS || box[5] < scan.z - BSP_RADIUS ||
            box[1] > scan.y + BSP_RADIUS || box[4] < scan.y - BSP_RADIUS) goto next;
        note_largest(vob, box);
        int mesh = class_definition(visual) == (const void*)G2A_PMESH_CLASSDEF;
        if (mesh) ++mesh_diag.in_range;
        if (!((unsigned char)vob[G2A_VOB_FLAGS] & G2A_VOB_DYNAMIC_COLLISION_BIT)) {
            if (mesh) ++mesh_diag.no_collision;
            goto next;
        }
        int plain_vob, mob, door, mover;
        if (!vob_class(vob, &plain_vob, &mob, &door, &mover) || !(plain_vob || mover || (mob && !door))) {
            if (mesh) ++mesh_diag.other_class;
            goto next;
        }
        if (mesh) {
            unsigned added = mesh_triangles(vob, visual, out);
            if (added) { ++*meshes; *mesh_triangle_count += added; watch_collision(vob); } else ++mesh_diag.empty;
            goto next;
        }
        int nearby = box[0] <= scan.x + OBJECT_RADIUS && box[3] >= scan.x - OBJECT_RADIUS &&
                     box[2] <= scan.z + OBJECT_RADIUS && box[5] >= scan.z - OBJECT_RADIUS;
        float sx = box[3] - box[0], sy = box[4] - box[1], sz = box[5] - box[2];
        /* Other visuals (animated MOB models): an axis-aligned box. */
        if (nearby && fmaxf(sx, sz) <= 800 && sy <= 600 && fminf(sx, sz) >= 5 && sy >= 10 &&
            box[1] <= feet + 180 && box[4] >= feet + 5 &&
            !(box[0] <= observed.x && observed.x <= box[3] && box[2] <= observed.z && observed.z <= box[5]) &&
            count < sizeof(object_boxes) / sizeof(object_boxes[0])) {
            watch_collision(vob);
            ObjectBox* result = &object_boxes[count++];
            memcpy(result->low, box, 12); memcpy(result->high, box + 3, 12);
            const float* m = (const float*)(vob + G2A_VOB_MATRIX);
            result->aligned = m[5] > .985f && fmaxf(fabsf(m[0]), fabsf(m[8])) > .985f;
        }
next:
        /* Depth-first walk without a stack, via parent links. */
        if (*(char**)(node + 4)) node = *(char**)(node + 4);
        else {
            while (node && node != root && !*(char**)(node + 8)) node = *(char**)node;
            node = node && node != root ? *(char**)(node + 8) : NULL;
        }
    }
    return count;
}

static void placeholder_geometry(void) {
    /* The worker loads area 0 at startup; real captures use areas 1-127. */
    TriangleList floor_list = {0};
    const float a[3] = {-50, -100, -50}, b[3] = {50, -100, -50}, c[3] = {50, -100, 50}, d[3] = {-50, -100, 50};
    triangles_push(&floor_list, a, c, b);
    triangles_push(&floor_list, a, d, c);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\world.bmgeo", lab_assets);
    int written = write_geometry(path, &floor_list, 0);
    snprintf(path, sizeof(path), "%s\\world.bmrails", lab_assets);
    written = written && write_rails(path, NULL, 0, 0);
    triangles_free(&floor_list);
    if (!written) {
        char line[MAX_PATH + 80];
        snprintf(line, sizeof(line), "Could not write the placeholder into %s (Windows error %lu).", lab_assets, GetLastError());
        log_line(line);
    }
}

/* Capture around `scan` in the ride frame `frame`, write it as a new area and
   install its support grid. The first capture (initial) also checks the floor
   under Marvin against his feet; rolling recaptures only need support under
   the board. feet is the Gothic height of the feet or board bottom. */
static int capture_collision(void* player, void* world, int initial, float feet, float* floor_out) {
    DWORD started = GetTickCount();
    float sampled[GROUND_CELLS], support[GROUND_CELLS], below, floor = NAN;
    GroundCentre centre = {(scan.x - frame.x) / 100, (scan.z - frame.z) / 100};
    unsigned hits = capture_ground(player, world, sampled);
    DWORD traced_at = GetTickCount();
    ground_fill(sampled, support);
    if (!ground_support_at(support, centre, centre.x, centre.z, &below)) {
        if (initial) inform("No ground found under Marvin here; skating unavailable.");
        return 0;
    }
    TriangleList all = {0};
    unsigned polygons = 0, visited = 0;
    unsigned measured = ground_triangles(support, centre, &all);
    if (!capture_bsp(world, &all, &polygons)) { triangles_free(&all); if (initial) inform("World collision unavailable; skating unavailable."); return 0; }
    unsigned static_triangles = all.count - measured;
    DWORD bsp_at = GetTickCount();
    unsigned meshes = 0, mesh_count = 0;
    unsigned boxes = capture_objects(world, feet, &visited, &all, &meshes, &mesh_count);
    const float frame_point[3] = {frame.x, frame.y, frame.z};
    for (unsigned n = 0; n < boxes; ++n) box_faces(&object_boxes[n], frame_point, &all);
    unsigned rail_count = box_rails(object_boxes, boxes, frame_point, feet, rails, CAPTURE_RAIL_LIMIT);
    DWORD objects_at = GetTickCount();
    if (all.overflow || capture_watch_overflow) { triangles_free(&all); if (initial) inform("Too much collision here; skating unavailable."); return 0; }
    if (initial) {
        float feet_meters = (feet - frame.y) / 100;
        if (!floor_under_origin(&all, &floor) || fabsf(floor - feet_meters) > .25f) {
            triangles_free(&all);
            inform("Can't find a reliable floor under Marvin here; try open ground.");
            return 0;
        }
    } else floor = below;
    unsigned area = capture_area % 127 + 1; /* a new area makes the worker reload (select_world) */
    char geometry[MAX_PATH], rail_path[MAX_PATH];
    snprintf(geometry, sizeof(geometry), "%s\\world.bmgeo", lab_assets);
    snprintf(rail_path, sizeof(rail_path), "%s\\world.bmrails", lab_assets);
    int written = write_geometry(geometry, &all, area) && write_rails(rail_path, rails, rail_count, area);
    char line[360];
    snprintf(line, sizeof(line), "%s in %lu ms at (%.1f, %.1f) m: %u/%u ground hits, %u ground + %u static triangles (of %u BSP polygons), %u mesh objects (%u triangles), %u object boxes (%u tree nodes), %u rails, floor %.3f m, area %u.",
        initial ? "Captured" : "Recaptured", GetTickCount() - started, centre.x, centre.z, hits, GROUND_CELLS, measured, static_triangles,
        polygons, meshes, mesh_count, boxes, visited, rail_count, floor, area);
    log_line(line);
    snprintf(line, sizeof(line), "  capture stages: traces %lu ms, support/BSP %lu ms, objects/meshes %lu ms, write %lu ms.",
        traced_at - started, bsp_at - traced_at, objects_at - bsp_at, GetTickCount() - objects_at);
    log_line(line);
    snprintf(line, sizeof(line), "  mesh visuals within 30 m: %u; skipped: %u without object collision, %u other class, %u unreadable or out of range.",
        mesh_diag.in_range, mesh_diag.no_collision, mesh_diag.other_class, mesh_diag.empty);
    log_line(line);
    if (initial) log_largest();
    triangles_free(&all);
    if (!written) { if (initial) inform("Could not write the skating collision; skating unavailable."); else log_line("Recapture could not be written; keeping the previous collision."); return 0; }
    collision_watch_count = capture_watch_count;
    memcpy(collision_watch, capture_watch, collision_watch_count * sizeof(collision_watch[0]));
    capture_area = area;
    memcpy(support_grid, support, sizeof(support_grid));
    grid_centre = centre;
    *floor_out = floor;
    return 1;
}

static int capture_and_mount(void* player, void* world) {
    float floor;
    frame = scan = observed;
    if (!capture_collision(player, world, 1, ((const float*)((char*)player + G2A_VOB_BBOX))[1], &floor)) return 0;
    recaptures = 0;
    last_recapture_try = GetTickCount();
    origin = frame;
    floor_y = height_reference = floor;
    radius = fminf(14, GROUND_RADIUS - .25f) + 1;
    memcpy(matrix, (char*)player + G2A_VOB_MATRIX, sizeof(matrix));
    heading = atan2f(-matrix[2], matrix[10]);
    worker.shared->mount[0] = worker.shared->mount[1] = 0;
    worker.shared->mount[2] = floor_y + .15f;
    worker.shared->mount[3] = heading;
    worker.shared->mount_area = capture_area;
    submitted_area = capture_area; reload_pending = 0;
    if (!skate_request(&worker, 1)) { inform("Mount submission failed."); return 0; }
    mounted_player = player;
    mounted_world = world;
    responses = 0;
    ride_clock_reset(&ride_clock, GetTickCount());
    phase = 2;
    status();
    return 1;
}

static int worker_ready(void) { return worker.shared && worker.shared->ready == 2 && !worker.pending; }

static void worker_maintain(void) {
    DWORD now = GetTickCount();
    if (!worker.shared) {
        if (!enabled() || worker_failures >= 5 || (int)(now - worker_retry_at) < 0) return;
        char executable[MAX_PATH], line[MAX_PATH + 96];
        snprintf(executable, sizeof(executable), "%s\\_derpy_script_loader\\scripts\\BullyMotion\\SkatePhysicsWorker.exe", lab);
        if (GetFileAttributesA(executable) == INVALID_FILE_ATTRIBUTES) {
            /* Report, then still try: CreateProcess gives the decisive error. */
            snprintf(line, sizeof(line), "Lab check failed for %s (Windows error %lu).", executable, GetLastError());
            log_line(line);
        }
        placeholder_geometry();
        worker_announced = 0;
        if (!skate_start(&worker, lab)) {
            ++worker_failures; worker_retry_at = now + 10000;
            snprintf(line, sizeof(line), "%s; lab %s; attempt %u of 5.", worker.error, lab, worker_failures);
            log_line(line);
            return;
        }
        snprintf(line, sizeof(line), "Skating engine starting in the background from %s.", lab);
        log_line(line);
        return;
    }
    if (phase) return; /* the riding code polls while mounted or mounting */
    int ready = skate_poll(&worker);
    if (ready < 0) {
        ++worker_failures; worker_retry_at = now + 5000;
        log_line(worker.error);
        if (want_mount) { want_mount = 0; inform("Skating engine failed to start; it will retry."); }
        return;
    }
    if (ready && !worker_announced) {
        worker_announced = 1;
        char line[96];
        snprintf(line, sizeof(line), "Skating engine ready after %.1f s.", (now - worker.started) / 1000.0f);
        log_line(line);
        if (!want_mount) inform("Skating ready: press F8 to ride.");
    }
}

static const char* output_problem(const float* output) {
    for (unsigned i = 0; i < 14; ++i) if (!isfinite(output[i])) return "Worker output was not finite; dismounted.";
    if (hypotf(output[0] - grid_centre.x, output[1] - grid_centre.z) >= radius - 1) return "Reached the edge of the sampled area; dismounted.";
    /* The static BSP can lie well below an object floor. Below the sampled
       support of its own cell, the board fell through a collision gap. */
    float below;
    if (!support_at(output[0], output[1], &below)) return "Left the sampled ground; dismounted.";
    if (output[2] < below - .3f) return "Board fell below the sampled floor; dismounted.";
    if (output[2] > height_reference + 8) return "Board rose beyond the supported height; dismounted.";
    if (fabsf(output[6]) >= 25 || fabsf(output[7]) >= 25 || fabsf(output[8]) >= 25) return "Board velocity out of range; dismounted.";
    /* output[13] is category==100 (ground physics), not an off-board flag.
       A value of 1 is normal while riding on the ground. */
    return NULL;
}

static void apply_output(void) {
    memcpy(last_output, worker.shared->output, sizeof(last_output));
    /* The worker is idle until the next request: take its rider pose and deck now. */
    memcpy(skate_pose, worker.shared->pose, sizeof(skate_pose));
    memcpy(skate_board, worker.shared->board[0], sizeof(skate_board));
    skate_have_pose = retarget_input_valid((const float (*)[13])skate_pose, last_output);
    for (int k = 0; k < 12; ++k) if (!isfinite(skate_board[k])) skate_have_pose = 0;
    for (int k = 0; k < 9; ++k) if (fabsf(skate_board[k]) > 2) skate_have_pose = 0;
    float yaw = last_output[3];
    matrix[0] = cosf(yaw); matrix[4] = 0; matrix[8] = sinf(yaw);
    matrix[1] = 0; matrix[5] = 1; matrix[9] = 0;
    matrix[2] = -sinf(yaw); matrix[6] = 0; matrix[10] = cosf(yaw);
    matrix[3] = origin.x + last_output[0] * 100;
    matrix[7] = origin.y + last_output[2] * 100 + offset_y;
    matrix[11] = origin.z + last_output[1] * 100;
    ((TransformFn)G2A_SET_TRANSFORM)(mounted_player, matrix);
    float below, around;
    /* Restore only to a pose whose board is not below its cell's floor and
       whose surroundings are supported 1 m out, never at an edge (a deck edge
       hands Marvin to Gothic physics beside the railing). */
    unsafe_applied = !support_at(last_output[0], last_output[1], &below) || last_output[2] < below;
    for (int k = 0; k < 4 && !unsafe_applied; ++k)
        unsafe_applied = !support_at(last_output[0] + (k == 0) - (k == 1), last_output[1] + (k == 2) - (k == 3), &around);
    if (!unsafe_applied) memcpy(safe_matrix, matrix, sizeof(safe_matrix));
    ++responses;
}

static void draw_board(void) {
    /* An authored wireframe placeholder; not a Skate or Gothic model. */
    float sine = sinf(last_output[3]), cosine = cosf(last_output[3]);
    Vec3 corners[4];
    const float right[] = {-11, 11, 11, -11}, forward[] = {-40, -40, 40, 40};
    for (unsigned i = 0; i < 4; ++i) {
        corners[i].x = origin.x + last_output[0] * 100 + right[i] * cosine - forward[i] * sine;
        corners[i].y = origin.y + last_output[2] * 100 + 3;
        corners[i].z = origin.z + last_output[1] * 100 + right[i] * sine + forward[i] * cosine;
    }
    for (unsigned i = 0; i < 4; ++i)
        ((LineFn)G2A_LINE_3D)((void*)G2A_LINE_CACHE, &corners[i], &corners[(i + 1) % 4], 0xFF20E0FFu, 0);
}

static void initialize(void) {
    if (initialized) return;
    initialized = 1;
    char executable[MAX_PATH];
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    char* slash = strrchr(executable, '\\');
    if (!slash || _stricmp(slash + 1, "Gothic2.exe")) return;
    *slash = 0; slash = strrchr(executable, '\\');
    if (!slash) return;
    *slash = 0;
    snprintf(directory, sizeof(directory), "%s\\.modding\\skate3-archolos", executable);
    snprintf(config, sizeof(config), "%s\\host-bridge.ini", directory);
    snprintf(log_path, sizeof(log_path), "%s\\host-bridge.log", directory);
    snprintf(state_path, sizeof(state_path), "%s\\bridge-state.json", directory);
    snprintf(lab, sizeof(lab), "%s\\host-worker-lab", directory);
    snprintf(lab_assets, sizeof(lab_assets), "%s\\_derpy_script_loader\\scripts\\BullyMotion\\assets", lab);
    if ((uintptr_t)GetModuleHandleA(NULL) != G2A_IMAGE_BASE || !GetModuleHandleA("Shw32.dll")) return;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS32* pe = (const IMAGE_NT_HEADERS32*)((const char*)dos + dos->e_lfanew);
    if (pe->FileHeader.TimeDateStamp != G2A_IMAGE_TIMESTAMP || pe->OptionalHeader.SizeOfImage != G2A_IMAGE_SIZE) return;
    for (unsigned i = 0; i < sizeof(g2a_bridge_fingerprints) / sizeof(g2a_bridge_fingerprints[0]); ++i) {
        if (memcmp((void*)g2a_bridge_fingerprints[i].address, g2a_bridge_fingerprints[i].code, 12)) {
            char error[128]; snprintf(error, sizeof(error), "Host function changed at %08X; bridge disabled.", g2a_bridge_fingerprints[i].address);
            log_line(error); return;
        }
    }
    supported = 1;
    log_line("Skating bridge loaded: engine warms up in a running world; F8 captures and mounts.");
    status();
}

__declspec(dllexport) void __cdecl Game_Entry(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_Init(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_Loop(void) {
    if (!supported || !GetPrivateProfileIntA("Bridge", "ShowHud", 1, config)) return;
    void* view = pointer_at(G2A_SCREEN);
    if (!view) return;
    char text[220];
    if (phase == 2) snprintf(text, sizeof(text), "Mounting skating physics...");
    else if (phase == 3)
        snprintf(text, sizeof(text), "SKATING %.0f km/h | W push | S brake | A/D steer | SPACE ollie (+Q/E flip) | SHIFT/CTRL grab | F7 rider: %s | F8 exit",
                 hypotf(last_output[6], last_output[7]) * 3.6f, rider_method_names[rider_method]);
    else if (want_mount) snprintf(text, sizeof(text), "Skating engine starting (%lus)... mounts when ready. F8 cancels.", (GetTickCount() - worker.started) / 1000);
    else if (notice[0] && GetTickCount() - notice_time < 6000) snprintf(text, sizeof(text), "%s", notice);
    else return;
    uint32_t string_storage[5]; /* zSTRING's verified x86 ABI, including vptr. */
    ((StringCtorFn)G2A_STRING_CTOR)(string_storage, text);
    ((PrintFn)G2A_PRINT_CX)(view, 700, string_storage);
    ((StringDtorFn)G2A_STRING_DTOR)(string_storage);
}
__declspec(dllexport) void __cdecl Game_PostLoop(void) {
    initialize();
    if (!supported) return;
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    void *player = NULL, *world = NULL;
    if (foreground != GetCurrentProcessId() || !active_world(&player, &world)) { stop("World, focus or player state changed; dismounted."); return; }
    const float* transform = (const float*)((char*)player + G2A_VOB_MATRIX);
    observed = (Vec3){transform[3], transform[7], transform[11]};
    observed_world = world;
    if (phase && !enabled()) { stop("Skating disabled by configuration."); return; }
    worker_maintain();
    int f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    int edge = f8 && !f8_held;
    f8_held = f8;
    if (edge) {
        if (phase) { stop("F8: dismounted."); return; }
        if (want_mount) { stop("Skating cancelled."); return; }
        if (!enabled()) { inform("Skating is disabled in host-bridge.ini."); return; }
        if (worker_ready()) { capture_and_mount(player, world); return; }
        if (worker.shared) { want_mount = 1; want_since = GetTickCount(); log_line("F8 while the skating engine starts; mounting when ready."); return; }
        inform(worker_failures >= 5 ? "Skating engine unavailable; see host-bridge.log." : "Skating engine restarting; press F8 again shortly.");
        return;
    }
    if (want_mount && worker_ready()) { want_mount = 0; capture_and_mount(player, world); return; }
    if (!phase) {
        if (GetTickCount() - last_status > 2000) { last_status = GetTickCount(); status(); }
        return;
    }
    if (player != mounted_player || world != mounted_world || GetAsyncKeyState(VK_ESCAPE) & 0x8000 ||
        GetAsyncKeyState(VK_TAB) & 0x8000 || GetAsyncKeyState('I') & 0x8000) { stop("Player/context changed; dismounted."); return; }
    if (frozen && collision_objects_changed()) { stop("A nearby collision object moved or changed; dismounted."); return; }
    DWORD maintenance_ms = 0;
    unsigned pending = worker.pending;
    int ready = skate_poll(&worker);
    if (ready < 0) {
        log_line(worker.error);
        ++worker_failures; worker_retry_at = GetTickCount() + 5000;
        stop("Skating engine failed; dismounted. It restarts in the background.");
        return;
    }
    if (!ready) return;
    if (reload_pending && pending == 2) {
        maintenance_ms = GetTickCount() - reload_started;
        reload_pending = 0;
        if (maintenance_ms > 50) {
            char line[100];
            snprintf(line, sizeof(line), "Collision reload completed after %lu ms; simulation clock paused during reload.", maintenance_ms);
            log_line(line);
        }
    }
    const char* problem = output_problem(worker.shared->output);
    if (problem) { stop(problem); return; }
    if (phase == 2) {
        if (fabsf(observed.x - origin.x) > 100 || fabsf(observed.y - origin.y) > 100 || fabsf(observed.z - origin.z) > 100) {
            stop("Player moved while mounting; press F8 again."); return;
        }
        saved_flags = *(unsigned char*)((char*)player + G2A_VOB_FLAGS);
        saved_sleep = *(unsigned char*)((char*)player + G2A_VOB_SLEEP_FLAGS) & 3;
        memcpy(safe_matrix, (char*)player + G2A_VOB_MATRIX, sizeof(safe_matrix));
        unsafe_applied = 0;
        /* Preserve the host's origin-to-feet offset, then add board height. */
        offset_y = -floor_y * 100;
        ((StateFn)G2A_SET_SLEEP)(player, 0);
        ((CollisionFn)G2A_SET_STATIC_COLLISION)(player, 0);
        ((CollisionFn)G2A_SET_DYNAMIC_COLLISION)(player, 0);
        ((StateFn)G2A_SET_PHYSICS)(player, 0);
        /* oCAIHuman::Moving() skips _WalkCycle and PC_CheckSpecialStates under
           the movement lock, so W/A/D/Space no longer drive Gothic's walk,
           turn and jump. It calls _Stand() instead, which leaves the rider
           pose alone (it reacts only to Gothic's own walk/aim/stand states). */
        saved_movlock = ((QueryFn)G2A_NPC_IS_MOVLOCK)(player) ? 1 : 0;
        ((MovLockFn)G2A_NPC_SET_MOVLOCK)(player, 1);
        frozen = 1; phase = 3;
        ride_clock_reset(&ride_clock, GetTickCount());
        space_held = 0;
        release_until = GetTickCount();
        rider_begin(player);
        int method = GetPrivateProfileIntA("Rider", "Method", 0, config);
        rider_apply_method(player, method >= 0 && method <= 2 ? method : 0);
        f7_held = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        board_begin(world);
        offset_y += board_lift;
        log_line("Mounted: worker owns movement. W push, S brake, A/D steer, Space ollie (Q/E flip), Shift/Ctrl grab, F7 animation, F8 exit.");
    }
    if (pending) apply_output();
    int f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !f7_held) rider_apply_method(player, (rider_method + 1) % 3);
    f7_held = f7;
    rider_update();
    if (board_vob) board_update(); else draw_board();
    /* Rolling recapture: keep collision around the board as it travels. The
       worker is idle here; the next input step reloads the new area. */
    if (phase == 3 && hypotf(last_output[0] - grid_centre.x, last_output[1] - grid_centre.z) > RECAPTURE_DISTANCE &&
        GetTickCount() - last_recapture_try > 300) {
        last_recapture_try = GetTickCount();
        DWORD maintenance_started = GetTickCount();
        float local;
        scan = observed;
        if (capture_collision(player, world, 0, frame.y + last_output[2] * 100 - 9, &local)) {
            worker.shared->mount_area = capture_area;
            height_reference = local;
            ++recaptures;
        }
        maintenance_ms += GetTickCount() - maintenance_started;
    }
    DWORD now = GetTickCount();
    if (now - last_status > 500) { last_status = now; status(); }
    /* Fixed 60 Hz requests; busy responses never block the host main thread. */
    int steps = ride_clock_tick(&ride_clock, now, maintenance_ms);
    if (steps == -1) { stop("Host frame stalled; dismounted."); return; }
    if (steps == -2) { stop("Input queue exceeded its bound; dismounted."); return; }
    unsigned count = (unsigned)steps;
    if (!count) return;
    int space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    SkateInput input = {0}; input.dt = 1.0f / 60.0f;
    /* XInput bits: A (0x1000) pushes, B (0x2000) brakes, as BullySkate maps them. */
    input.buttons = (GetAsyncKeyState('W') & 0x8000 ? 0x1000 : 0) | (GetAsyncKeyState('S') & 0x8000 ? 0x2000 : 0);
    input.axes[0] = ((GetAsyncKeyState('D') & 0x8000) ? 1.0f : 0) - ((GetAsyncKeyState('A') & 0x8000) ? 1.0f : 0);
    /* Right stick: Space held pulls it down; release flicks it up for 0.1 s
       (the worker needs several ticks, as in the tested scripted ollie). Q/E at
       release add a full left/right deflection: kickflip/heelflip (offline:
       about 12.4 rad of deck roll either way; a 0.4 deflection does nothing). */
    if (!space && space_held) {
        release_until = now + 100;
        release_x = GetAsyncKeyState('Q') & 0x8000 ? -1.0f : GetAsyncKeyState('E') & 0x8000 ? 1.0f : 0;
    }
    if (space) input.axes[3] = -1.0f;
    else if ((int)(release_until - now) > 0) { input.axes[3] = 1.0f; input.axes[2] = release_x; }
    space_held = space;
    /* Grabs: left/right trigger. */
    input.axes[4] = GetAsyncKeyState(VK_SHIFT) & 0x8000 ? 1.0f : 0;
    input.axes[5] = GetAsyncKeyState(VK_CONTROL) & 0x8000 ? 1.0f : 0;
    worker.shared->input_count = count;
    for (unsigned i = 0; i < count; ++i) worker.shared->inputs[i] = input;
    if (worker.shared->mount_area != submitted_area) {
        submitted_area = worker.shared->mount_area;
        reload_started = GetTickCount(); reload_pending = 1;
    }
    if (!skate_request(&worker, 2)) stop("Input submission failed; dismounted.");
}

__declspec(dllexport) void __cdecl Game_MenuLoop(void) {
    initialize();
    if (supported && !menu_seen) { menu_seen = 1; log_line("Union menu callback received."); }
    stop("Menu opened; dismounted.");
}
__declspec(dllexport) void __cdecl Game_Pause(void) { stop("Paused; dismounted."); }
__declspec(dllexport) void __cdecl Game_SaveBegin(void) { stop("Save started; original player state restored."); }
__declspec(dllexport) void __cdecl Game_LoadBegin_NewGame(void) { stop("New game; dismounted."); }
__declspec(dllexport) void __cdecl Game_LoadBegin_SaveGame(void) { stop("Load started; dismounted."); }
__declspec(dllexport) void __cdecl Game_LoadBegin_ChangeLevel(void) { stop("Level change; dismounted."); }
__declspec(dllexport) void __cdecl Game_Exit(void) { stop("Game exiting."); skate_stop(&worker); }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

/* Read-only first slice: log the real host position and sample static ground.
 * Engine addresses/layouts are generated locally from the Gothic API headers.
 * This module does not move the player or start the physics worker. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gothic_layout.h"

typedef struct Vec3 { float x, y, z; } Vec3;
typedef int (__attribute__((thiscall)) *RunningFn)(void*);
typedef int (__attribute__((fastcall)) *TraceFn)(void*, const Vec3*, const Vec3*, void*, int);
static char root[MAX_PATH], log_path[MAX_PATH], snapshot_path[MAX_PATH];
static char config_path[MAX_PATH];
static DWORD last_capture;
static unsigned sequence;
static int initialized, enabled, terrain, menu_seen, world_seen;

static void log_line(const char* text) {
    FILE* file = fopen(log_path, "a");
    if (file) { fprintf(file, "%lu %s\n", GetTickCount(), text); fclose(file); }
}

static int readable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info))) return 0;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (uintptr_t)pointer + size <= (uintptr_t)info.BaseAddress + info.RegionSize;
}

static int read_pointer(uintptr_t address, void** output) {
    if (!readable((void*)address, sizeof(void*))) return 0;
    *output = *(void**)address;
    return *output != NULL;
}

static void capture(void* game) {
    terrain = GetPrivateProfileIntA("Probe", "CaptureTerrain", 0, config_path);
    void *player = NULL, *world = NULL, *manager = NULL;
    if (!read_pointer(G2A_PLAYER, &player) || !read_pointer(G2A_MANAGER, &manager)) return;
    if (!((RunningFn)G2A_IS_RUNNING)(manager)) return;
    if (!readable(game, G2A_GAME_WORLD_ENTERED + 4) ||
        *(int*)((char*)game + G2A_GAME_LOADING) || *(int*)((char*)game + G2A_GAME_LEVEL_CHANGE) ||
        *(int*)((char*)game + G2A_GAME_PAUSE)) return;
    world = *(void**)((char*)game + G2A_SESSION_WORLD);
    if (!readable(player, G2A_VOB_HOME_WORLD + 4) || !readable(world, G2A_WORLD_REPORT + 40) ||
        *(void**)((char*)player + G2A_VOB_HOME_WORLD) != world) return;
    const float* matrix = (float*)((char*)player + G2A_VOB_MATRIX);
    Vec3 position = {matrix[3], matrix[7], matrix[11]};
    if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z)) return;
    char temporary[MAX_PATH];
    snprintf(temporary, sizeof(temporary), "%s.new", snapshot_path);
    FILE* file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "{\"version\":1,\"sequence\":%u,\"pid\":%lu,\"worldPointer\":%u,\"units\":\"centimeters\",\"up\":\"Y\",\"position\":[%.9g,%.9g,%.9g],\"forward\":[%.9g,%.9g,%.9g],\"terrainEnabled\":%s,\"gridRadius\":10,\"spacing\":100,\"grid\":[", ++sequence, GetCurrentProcessId(), (unsigned)(uintptr_t)world, position.x, position.y, position.z, matrix[2], matrix[6], matrix[10], terrain ? "true" : "false");
    unsigned hits = 0;
    for (int z = -10; terrain && z <= 10; ++z) for (int x = -10; x <= 10; ++x) {
        Vec3 start = {position.x + x * 100.0f, position.y + 20.0f, position.z + z * 100.0f};
        Vec3 direction = {0, -1000.0f, 0};
        int hit = ((TraceFn)G2A_TRACE_NEAREST)(world, &start, &direction, player, G2A_TRACE_IGNORE_CHARACTERS | G2A_TRACE_NORMAL);
        Vec3 contact = *(Vec3*)((char*)world + G2A_WORLD_REPORT + G2A_REPORT_INTERSECTION);
        if (x != -10 || z != -10) fputc(',', file);
        if (hit && isfinite(contact.x) && isfinite(contact.y) && isfinite(contact.z)) {
            fprintf(file, "[%.9g,%.9g,%.9g]", contact.x, contact.y, contact.z); ++hits;
        } else fputs("null", file);
    }
    fprintf(file, "],\"hits\":%u}\n", hits);
    if (fclose(file) == 0) MoveFileExA(temporary, snapshot_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (sequence == 1) log_line("First host snapshot written through Union Game_PostLoop.");
}

static void initialize(void) {
    if (initialized) return;
    initialized = 1;
    char executable[MAX_PATH], config[MAX_PATH];
    GetModuleFileNameA(NULL, executable, MAX_PATH);
    char* separator = strrchr(executable, '\\');
    if (!separator || _stricmp(separator + 1, "Gothic2.exe")) return;
    *separator = 0;
    separator = strrchr(executable, '\\');
    if (!separator) return;
    *separator = 0;
    snprintf(root, sizeof(root), "%s", executable);
    snprintf(log_path, sizeof(log_path), "%s\\.modding\\skate3-archolos\\host-probe.log", root);
    snprintf(snapshot_path, sizeof(snapshot_path), "%s\\.modding\\skate3-archolos\\host-snapshot.json", root);
    snprintf(config, sizeof(config), "%s\\.modding\\skate3-archolos\\host-probe.ini", root);
    snprintf(config_path, sizeof(config_path), "%s", config);
    log_line("ArcholosSkate v2: Union callbacks only; no code patches or movement writes.");
    if (!GetPrivateProfileIntA("Probe", "Enabled", 0, config)) { log_line("Disabled by configuration."); return; }
    terrain = GetPrivateProfileIntA("Probe", "CaptureTerrain", 0, config);
    if (!GetModuleHandleA("Shw32.dll") || (uintptr_t)GetModuleHandleA(NULL) != G2A_IMAGE_BASE) { log_line("Unsupported host or missing Union."); return; }
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS32* image = (const IMAGE_NT_HEADERS32*)((const char*)dos + dos->e_lfanew);
    if (image->FileHeader.TimeDateStamp != G2A_IMAGE_TIMESTAMP || image->OptionalHeader.SizeOfImage != G2A_IMAGE_SIZE ||
        memcmp((void*)G2A_IS_RUNNING, g2a_expected_running, sizeof(g2a_expected_running)) ||
        memcmp((void*)G2A_TRACE_NEAREST, g2a_expected_trace, sizeof(g2a_expected_trace))) {
        log_line("Loaded host code differs from the inspected API build; capture disabled."); return;
    }
    enabled = 1;
    log_line(terrain ? "Position and terrain capture enabled." : "Position capture enabled; terrain calls disabled.");
}

/* Union resolves these documented C exports when loading an autorun plugin.
 * No detour is installed over Union's existing frame dispatcher. */
__declspec(dllexport) void __cdecl Game_Entry(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_Init(void) { initialize(); }
__declspec(dllexport) void __cdecl Game_MenuLoop(void) {
    initialize();
    if (enabled && !menu_seen) { menu_seen = 1; log_line("Union Game_MenuLoop received."); }
}
__declspec(dllexport) void __cdecl Game_PostLoop(void) {
    initialize();
    if (!enabled) return;
    if (!world_seen) { world_seen = 1; log_line("Union Game_PostLoop received."); }
    DWORD now = GetTickCount();
    if (now - last_capture < 2000) return;
    last_capture = now;
    void* game = NULL;
    if (read_pointer(G2A_GAME, &game)) capture(game);
}
__declspec(dllexport) void __cdecl Game_Exit(void) {
    if (enabled) log_line("Union Game_Exit received.");
    enabled = 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

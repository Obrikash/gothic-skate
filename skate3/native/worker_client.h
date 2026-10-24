/* Fixed-width wire contract for the pinned BullySkate v0.1.2 worker.
 * No game pointers cross this boundary. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

typedef struct SkateInput { float dt; uint32_t buttons; float axes[6]; } SkateInput;
typedef struct SkateShared {
    uint32_t ready, command, success;
    float mount[4], dt;
    uint32_t buttons;
    float axes[6];
    uint32_t actor_count;
    /* output[13] means ground-physics category==100; it is not off-board. */
    float actors[24][9], aspect, output[14], pose[36][13], board[2][13], camera[7], root[3];
    uint32_t has_camera;
    char error[2048];
    float preferences[10];
    uint32_t marker_flags, marker_sets, marker_returns;
    float marker_progress;
    uint32_t input_count;
    SkateInput inputs[8];
    uint32_t mount_area, actor_revision, vehicle_count, vehicle_revision;
    float vehicles[8][16];
    uint32_t interaction[4];
    float audio[64];
    uint32_t world_phase, difficulty, camera_type;
} SkateShared;
_Static_assert(sizeof(SkateShared) == 6184, "Pinned worker ABI size");
_Static_assert(offsetof(SkateShared, output) == 932, "Pinned worker output offset");
_Static_assert(offsetof(SkateShared, input_count) == 5112, "Pinned worker input offset");

typedef struct SkateClient {
    HANDLE mapping, command, response, process, job;
    SkateShared* shared;
    DWORD started, submitted, timeout;
    unsigned pending;
    char error[256];
} SkateClient;

int skate_start(SkateClient* client, const char* directory);
/* 1=complete, 0=pending, -1=failure; always polls with a zero timeout. */
int skate_poll(SkateClient* client);
int skate_request(SkateClient* client, unsigned command);
void skate_stop(SkateClient* client);

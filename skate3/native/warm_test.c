/* Offline check of the warm-worker flow the bridge uses; never loads Gothic.
 * One worker: area-0 placeholder start, then two captures as areas 1 and 2,
 * each mounted (command 1 reloads via select_world), pushed and unmounted (3). */
#include "worker_client.h"
#include "capture_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int await(SkateClient* client) {
    int status;
    while ((status = skate_poll(client)) == 0) Sleep(1);
    if (status < 0) fprintf(stderr, "%s\n", client->error);
    return status > 0;
}

/* Test-only wait: runtime shutdown stays nonblocking, but a temporary lab
   cannot be removed until Windows has released the terminated worker exe. */
static void stop_and_wait(SkateClient* client) {
    HANDLE process = NULL;
    if (client->process) DuplicateHandle(GetCurrentProcess(), client->process,
        GetCurrentProcess(), &process, SYNCHRONIZE, FALSE, 0);
    skate_stop(client);
    if (process) { WaitForSingleObject(process, 5000); CloseHandle(process); }
}

static int copy(const char* from, const char* to) {
    return CopyFileA(from, to, FALSE) != 0;
}

int main(int argc, char** argv) {
    if (argc != 7 && argc != 9) { fprintf(stderr, "usage: warm-test LAB FLOOR GEOMETRY1 RAILS1 GEOMETRY2 RAILS2 [GEOMETRY3 RAILS3]\n"); return 2; }
    char assets[MAX_PATH], geometry[MAX_PATH], rail_path[MAX_PATH];
    snprintf(assets, sizeof(assets), "%s\\_derpy_script_loader\\scripts\\BullyMotion\\assets", argv[1]);
    snprintf(geometry, sizeof(geometry), "%s\\world.bmgeo", assets);
    snprintf(rail_path, sizeof(rail_path), "%s\\world.bmrails", assets);
    TriangleList placeholder = {0};
    const float a[3] = {-50, -100, -50}, b[3] = {50, -100, -50}, c[3] = {50, -100, 50}, d[3] = {-50, -100, 50};
    triangles_push(&placeholder, a, c, b); triangles_push(&placeholder, a, d, c);
    if (!write_geometry(geometry, &placeholder, 0) || !write_rails(rail_path, NULL, 0, 0)) return 3;
    triangles_free(&placeholder);

    float floor = (float)atof(argv[2]);
    SkateClient client;
    DWORD started = GetTickCount();
    if (!skate_start(&client, argv[1])) { fprintf(stderr, "%s\n", client.error); return 4; }
    if (!await(&client)) return 5;
    printf("{\"readySeconds\":%.3f", (GetTickCount() - started) / 1000.0);
    for (int area = 1; area <= 2; ++area) {
        if (!copy(argv[area == 1 ? 3 : 5], geometry) || !copy(argv[area == 1 ? 4 : 6], rail_path)) { stop_and_wait(&client); return 6; }
        client.shared->mount[0] = client.shared->mount[1] = 0;
        client.shared->mount[2] = floor + .15f;
        client.shared->mount[3] = 0;
        client.shared->mount_area = (uint32_t)area;
        DWORD mount_started = GetTickCount();
        if (!skate_request(&client, 1) || !await(&client)) { stop_and_wait(&client); return 7; }
        DWORD mount_ms = GetTickCount() - mount_started;
        float rest = client.shared->output[2], distance = 0;
        for (unsigned tick = 0; tick < 240; ++tick) {
            SkateInput* input = &client.shared->inputs[0];
            *input = (SkateInput){0};
            input->dt = 1.0f / 60.0f;
            input->buttons = tick >= 30 && tick < 150 ? 0x1000 : 0;
            client.shared->input_count = 1;
            if (!skate_request(&client, 2) || !await(&client)) { stop_and_wait(&client); return 8; }
            distance = fmaxf(distance, hypotf(client.shared->output[0], client.shared->output[1]));
        }
        if (!skate_request(&client, 3) || !await(&client)) { stop_and_wait(&client); return 9; }
        printf(",\"area%d\":{\"mountMs\":%lu,\"restHeight\":%.4f,\"maxDistance\":%.3f,\"workerAlive\":%s}", area, mount_ms, rest, distance,
               WaitForSingleObject(client.process, 0) == WAIT_TIMEOUT ? "true" : "false");
    }
    /* Mid-ride swap, as the bridge's rolling recapture does: mount on area 1,
       push, and at tick 60 switch mount_area to area 3 (written as the area-2
       files, retagged by the caller). Command 2 runs select_world each step. */
    if (argc == 9) {
        if (!copy(argv[3], geometry) || !copy(argv[4], rail_path)) { stop_and_wait(&client); return 10; }
        client.shared->mount[0] = client.shared->mount[1] = 0;
        client.shared->mount[2] = floor + .15f;
        client.shared->mount[3] = 0;
        client.shared->mount_area = 1;
        if (!skate_request(&client, 1) || !await(&client)) { stop_and_wait(&client); return 11; }
        float before[3] = {0}, after[3] = {0}, distance = 0, jump = 0;
        DWORD swap_ms = 0;
        for (unsigned tick = 0; tick < 240; ++tick) {
            if (tick == 60) {
                memcpy(before, client.shared->output, 12);
                if (!copy(argv[7], geometry) || !copy(argv[8], rail_path)) { stop_and_wait(&client); return 12; }
                client.shared->mount_area = 3;
            }
            SkateInput* input = &client.shared->inputs[0];
            *input = (SkateInput){0};
            input->dt = 1.0f / 60.0f;
            input->buttons = tick >= 30 && tick < 150 ? 0x1000 : 0;
            client.shared->input_count = 1;
            DWORD step_started = GetTickCount();
            if (!skate_request(&client, 2) || !await(&client)) { stop_and_wait(&client); return 13; }
            if (tick == 60) {
                swap_ms = GetTickCount() - step_started;
                memcpy(after, client.shared->output, 12);
                jump = sqrtf((after[0] - before[0]) * (after[0] - before[0]) + (after[1] - before[1]) * (after[1] - before[1]) +
                             (after[2] - before[2]) * (after[2] - before[2]));
            }
            distance = fmaxf(distance, hypotf(client.shared->output[0], client.shared->output[1]));
        }
        printf(",\"midRideSwap\":{\"swapStepMs\":%lu,\"stepAcrossSwapMeters\":%.4f,\"positionAtSwap\":%.3f,\"maxDistance\":%.3f,\"workerAlive\":%s}",
               swap_ms, jump, hypotf(before[0], before[1]), distance, WaitForSingleObject(client.process, 0) == WAIT_TIMEOUT ? "true" : "false");
    }
    printf("}\n");
    stop_and_wait(&client);
    return 0;
}

/* Real x86-to-x64 protocol check. Never loads or modifies Gothic. */
#include "worker_client.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int await(SkateClient* client) {
    int status;
    while ((status = skate_poll(client)) == 0) Sleep(1);
    if (status < 0) fprintf(stderr, "%s\n", client->error);
    return status > 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) { fprintf(stderr, "usage: worker-smoke LAB FLOOR_HEIGHT [stationary|steer|push|brake [HEADING]]\n"); return 2; }
    int stationary = argc >= 4 && !strcmp(argv[3], "stationary");
    /* steer: push, then hold A, as in the first host ride that fell through. */
    int steer = argc >= 4 && !strcmp(argv[3], "steer");
    int brake = argc >= 4 && !strcmp(argv[3], "brake");
    float flick = argc >= 4 && !strcmp(argv[3], "flipL") ? -.7f : argc >= 4 && !strcmp(argv[3], "flipR") ? .7f : 0;
    if (flick && getenv("SMOKE_FLICK")) flick = (flick > 0 ? 1 : -1) * (float)atof(getenv("SMOKE_FLICK"));
    int grab = argc >= 4 && !strcmp(argv[3], "grabL") ? -1 : argc >= 4 && !strcmp(argv[3], "grabR") ? 1 : 0;
    SkateClient client;
    FILE* pose_file = NULL;
    if (!skate_start(&client, argv[1])) { fprintf(stderr, "%s\n", client.error); return 3; }
    int code = 4;
    if (!await(&client)) goto done;
    client.shared->mount[2] = (float)atof(argv[2]) + .15f;
    client.shared->mount[3] = argc == 5 ? (float)atof(argv[4]) : 0;
    if (!skate_request(&client, 1) || !await(&client)) goto done;
    float distance = 0, rest = 0, peak = -1e9f, lowest = 1e9f, fall = -1;
    float pitch_travel = 0, roll_travel = 0, last_pitch = 0, last_roll = 0, category_after = -1;
    /* SMOKE_POSE=file: per tick, output[14], pose[36][13] and board[2][13] as f32. */
    if (getenv("SMOKE_POSE")) pose_file = fopen(getenv("SMOKE_POSE"), "wb");
    unsigned contacts = 0;
    for (unsigned tick = 0; tick < 600; ++tick) {
        SkateInput* input = &client.shared->inputs[0];
        *input = (SkateInput){0};
        input->dt = 1.0f / 60.0f;
        input->buttons = !stationary && tick >= 90 && tick < (steer ? 330 : 150) ? 0x1000 : 0;
        /* brake: the same push, then hold B (0x2000) from tick 180. */
        if (brake && tick >= 180) input->buttons |= 0x2000;
        input->axes[0] = steer && tick >= 200 ? -1.0f : 0;
        input->axes[3] = tick >= 360 && tick < 390 ? -1 : (tick >= 390 && tick < 396 ? 1 : 0);
        /* flipL/flipR: the release flick goes up-left/up-right; grabL/grabR hold a trigger in the air. */
        if (tick >= 390 && tick < 396) input->axes[2] = flick;
        /* SMOKE_ARC: swing the stick through the flick side before going up (a quarter circle). */
        if (getenv("SMOKE_ARC") && tick >= 386 && tick < 390) { input->axes[2] = flick > 0 ? 1.0f : -1.0f; input->axes[3] = 0; }
        if (tick >= 400 && tick < 440) { input->axes[4] = grab < 0 ? 1.0f : 0; input->axes[5] = grab > 0 ? 1.0f : 0; }
        client.shared->input_count = 1;
        if (!skate_request(&client, 2) || !await(&client)) goto done;
        const float* output = client.shared->output;
        for (unsigned i = 0; i < 14; ++i) if (!isfinite(output[i])) goto done;
        distance = fmaxf(distance, hypotf(output[0], output[1]));
        if (tick == 80) rest = output[2];
        lowest = fminf(lowest, output[2]);
        if (argc >= 4 && !stationary) fprintf(stderr, "%u %.5f %.5f %.5f\n", tick, output[0], output[1], output[2]);
        if (fall < 0 && output[2] < (float)atof(argv[2]) - .3f) fall = hypotf(output[0], output[1]);
        if (tick >= 360 && tick < 480) peak = fmaxf(peak, output[2]);
        /* Deck rotation during the trick window: summed |change| of pitch and roll. */
        if (tick > 390 && tick < 480) { pitch_travel += fabsf(output[4] - last_pitch); roll_travel += fabsf(output[5] - last_roll); }
        last_pitch = output[4]; last_roll = output[5];
        if (tick == 470) category_after = output[13];
        if (pose_file) {
            fwrite(output, 4, 14, pose_file);
            fwrite(client.shared->pose, 4, 36 * 13, pose_file);
            fwrite(client.shared->board, 4, 2 * 13, pose_file);
        }
        contacts += output[10] > 0;
    }
    printf("{\"adapterBits\":32,\"workerBits\":64,\"sharedBytes\":%u,\"distanceMeters\":%.6f,\"restingHeightMeters\":%.6f,\"ollieGainMeters\":%.6f,\"contactTicks\":%u,\"lowestHeightMeters\":%.6f,\"fallDistanceMeters\":%.6f,\"pitchTravel\":%.4f,\"rollTravel\":%.4f,\"categoryAfter\":%.0f}\n",
        (unsigned)sizeof(SkateShared), distance, rest, peak - rest, contacts, lowest, fall, pitch_travel, roll_travel, category_after);
    code = (stationary ? distance < 1 && fabsf(rest - ((float)atof(argv[2]) + .087f)) < .15f : distance > .1f) && peak - rest > .1f && contacts ? 0 : 5;
done:
    if (pose_file) fclose(pose_file);
    skate_stop(&client);
    return code;
}

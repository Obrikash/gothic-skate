/* Offline oracle harness for retarget_core.c; never loads Gothic.
 * Input:  u32 node count; per node: char name[64], i32 parent, f32 local[16];
 *         u32 tick count; per tick: f32 marvin_r[9], marvin_p[3],
 *         ride_origin[3], board_position[3], pose[36][13].
 * Output: per tick, f32 local[count][16]. */
#include "retarget_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: retarget-test INPUT OUTPUT\n"); return 2; }
    FILE* input = fopen(argv[1], "rb");
    FILE* output = fopen(argv[2], "wb");
    if (!input || !output) return 3;
    uint32_t count;
    if (fread(&count, 4, 1, input) != 1 || count > RETARGET_MAX_NODES) return 4;
    static char names[RETARGET_MAX_NODES][64];
    static const char* name_list[RETARGET_MAX_NODES];
    static int parent[RETARGET_MAX_NODES];
    static float local[RETARGET_MAX_NODES][16];
    for (uint32_t i = 0; i < count; ++i) {
        if (fread(names[i], 1, 64, input) != 64 || fread(&parent[i], 4, 1, input) != 1 || fread(local[i], 4, 16, input) != 16) return 5;
        names[i][63] = 0;
        name_list[i] = names[i];
    }
    static RetargetSetup setup;
    if (!retarget_setup(&setup, (int)count, name_list, parent, local)) { fprintf(stderr, "setup failed\n"); return 6; }
    uint32_t ticks;
    if (fread(&ticks, 4, 1, input) != 1) return 7;
    for (uint32_t t = 0; t < ticks; ++t) {
        float marvin_r[9], marvin_p[3], ride_origin[3], board_position[3], pose[36][13];
        static float out[RETARGET_MAX_NODES][16];
        if (fread(marvin_r, 4, 9, input) != 9 || fread(marvin_p, 4, 3, input) != 3 ||
            fread(ride_origin, 4, 3, input) != 3 || fread(board_position, 4, 3, input) != 3 ||
            fread(pose, 4, 36 * 13, input) != 36 * 13) return 8;
        if (!retarget_input_valid((const float (*)[13])pose, board_position)) { fprintf(stderr, "valid worker pose rejected at tick %u\n", t); return 11; }
        retarget_pose(&setup, (const float (*)[13])pose, marvin_r, marvin_p, ride_origin, out);
        if (!retarget_locals_valid((int)count, (const float (*)[16])out)) return 12;
        if (!t) {
            float bad[36][13];
            memcpy(bad, pose, sizeof(bad)); bad[1][9] = NAN;
            if (retarget_input_valid((const float (*)[13])bad, board_position)) return 13;
            memcpy(bad, pose, sizeof(bad)); memcpy(bad[1] + 3, bad[1], 12);
            if (retarget_input_valid((const float (*)[13])bad, board_position)) return 14;
            memcpy(bad, pose, sizeof(bad)); bad[1][9] += 10;
            if (retarget_input_valid((const float (*)[13])bad, board_position)) return 15;
            /* Reproduce the original distant-skeleton failure: a missing
               ride origin must fail the final model-space bounds check. */
            float far[3] = {marvin_p[0] + 100000, marvin_p[1], marvin_p[2]};
            static float unsafe[RETARGET_MAX_NODES][16];
            retarget_pose(&setup, (const float (*)[13])pose, marvin_r, far, ride_origin, unsafe);
            if (retarget_locals_valid((int)count, (const float (*)[16])unsafe)) return 16;
        }
        if (fwrite(out, 4, count * 16, output) != count * 16) return 9;
    }
    fclose(input);
    return fclose(output) ? 10 : 0;
}

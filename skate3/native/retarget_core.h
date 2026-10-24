/* Engine-free retarget of the worker's skater pose onto Gothic's human
 * skeleton; a C port of tools/retarget_reference.py (its oracle).
 * Matrices are Gothic zMAT4 row-major 4x4 (translation in [3], [7], [11]);
 * rotations are row-major 3x3. Gothic model space: Y up, centimeters. */
#pragma once

#define RETARGET_MAX_NODES 96
#define RETARGET_RIG_BONES 36

typedef struct RetargetSetup {
    int count, ready;
    int parent[RETARGET_MAX_NODES];
    float local_r[RETARGET_MAX_NODES][9], local_p[RETARGET_MAX_NODES][3];
    float bind_r[RETARGET_MAX_NODES][9];
    int joint[RETARGET_MAX_NODES], child[RETARGET_MAX_NODES]; /* rig bones; -1 if none */
    int spine_slot[RETARGET_MAX_NODES];                        /* 0..3 in the spine chain, else -1 */
    float reference_r[RETARGET_MAX_NODES][9];
    float spine_fraction[4];
    int pelvis, legs[2][3];
} RetargetSetup;

/* names/parent/local describe Gothic's skeleton (parents before children).
 * Returns 0 if a required Biped bone is missing. */
int retarget_setup(RetargetSetup* setup, int count, const char* const* names, const int* parent, const float (*local)[16]);
/* pose: the worker's shared pose[36][13] (rotation rows, positions relative
 * to the fixed ride origin in Z-up meters). marvin_r/marvin_p: Marvin's vob
 * rotation (row-major 3x3) and absolute Gothic position (cm). ride_origin:
 * fixed ride origin in Gothic world cm, NOT the rolling capture centre.
 * Writes one zMAT4 per node: its local trafo. */
void retarget_pose(const RetargetSetup* setup, const float (*pose)[13], const float* marvin_r, const float* marvin_p, const float* ride_origin, float (*out_local)[16]);

/* Presentation failures must never publish NaNs or a distant skeleton. Wire
 * board_position is Z-up metres, like pose positions; locals are Gothic cm. */
int retarget_input_valid(const float (*pose)[13], const float board_position[3]);
int retarget_locals_valid(int count, const float (*local)[16]);

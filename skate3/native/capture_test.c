/* Offline oracle harness for capture_core.c; never loads or touches Gothic.
 * Input:  origin[3] f32, feet f32, centre x/z f32 (Gothic meters from origin),
 *         sampled[GROUND_CELLS] f32, box count u32,
 *         boxes {low[3], high[3], aligned} as f32[7].
 * Output: support grid, ground triangles, floor, box faces and rails, plus the
 *         BMGEO2/BMRL3 files the bridge would write (area 7). */
#include "capture_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int put_triangles(FILE* file, const TriangleList* list, unsigned from) {
    uint32_t count = list->count - from;
    return fwrite(&count, 4, 1, file) == 1 && (!count || fwrite(list->items + from, sizeof(Triangle), count, file) == count);
}

int main(int argc, char** argv) {
    if (argc != 5) { fprintf(stderr, "usage: capture-test INPUT OUTPUT GEOMETRY RAILS\n"); return 2; }
    FILE* input = fopen(argv[1], "rb");
    if (!input) return 3;
    float origin[3], feet, sampled[GROUND_CELLS];
    GroundCentre centre;
    uint32_t boxes = 0;
    if (fread(origin, 4, 3, input) != 3 || fread(&feet, 4, 1, input) != 1 || fread(&centre, 4, 2, input) != 2 ||
        fread(sampled, 4, GROUND_CELLS, input) != GROUND_CELLS || fread(&boxes, 4, 1, input) != 1 || boxes > 10000) return 4;
    ObjectBox* box = calloc(boxes ? boxes : 1, sizeof(ObjectBox));
    for (uint32_t n = 0; n < boxes; ++n) {
        float values[7];
        if (fread(values, 4, 7, input) != 7) return 5;
        memcpy(box[n].low, values, 12); memcpy(box[n].high, values + 3, 12); box[n].aligned = values[6] != 0;
    }
    fclose(input);

    float support[GROUND_CELLS], floor = NAN;
    TriangleList ground = {0}, faces = {0};
    ground_fill(sampled, support);
    ground_triangles(support, centre, &ground);
    int found = floor_under_origin(&ground, &floor);
    for (uint32_t n = 0; n < boxes; ++n) box_faces(&box[n], origin, &faces);
    Rail rails[CAPTURE_RAIL_LIMIT];
    uint32_t made = box_rails(box, boxes, origin, feet, rails, CAPTURE_RAIL_LIMIT);
    float probe[4] = {NAN, NAN, NAN, NAN};
    const float samples[4][2] = {{0, 0}, {-6.56f, -2.44f}, {3.3f, 1.7f}, {20, 0}};
    for (int k = 0; k < 4; ++k)
        if (!ground_support_at(support, centre, centre.x + samples[k][0], centre.z + samples[k][1], &probe[k])) probe[k] = NAN;

    FILE* output = fopen(argv[2], "wb");
    if (!output) return 6;
    int ok = fwrite(support, 4, GROUND_CELLS, output) == GROUND_CELLS && put_triangles(output, &ground, 0) &&
        fwrite(&found, 4, 1, output) == 1 && fwrite(&floor, 4, 1, output) == 1 && put_triangles(output, &faces, 0) &&
        fwrite(&made, 4, 1, output) == 1 && (!made || fwrite(rails, sizeof(Rail), made, output) == made) &&
        fwrite(probe, 4, 4, output) == 4;
    ok = !fclose(output) && ok;
    TriangleList all = {0};
    for (unsigned n = 0; n < ground.count; ++n) triangles_push(&all, ground.items[n].p[0], ground.items[n].p[1], ground.items[n].p[2]);
    for (unsigned n = 0; n < faces.count; ++n) triangles_push(&all, faces.items[n].p[0], faces.items[n].p[1], faces.items[n].p[2]);
    ok = ok && write_geometry(argv[3], &all, 7) && write_rails(argv[4], rails, made, 7);
    triangles_free(&ground); triangles_free(&faces); triangles_free(&all); free(box);
    return ok ? 0 : 7;
}

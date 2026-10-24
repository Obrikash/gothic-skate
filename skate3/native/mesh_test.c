/* Authored progressive-mesh fixtures through the same extraction/writer as
   the host; never loads Gothic. Writes an open floor and a walled floor for
   the real worker hot-reload test. */
#include "capture_core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "mesh check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static float normal_y(const Triangle* t) {
    return (t->p[1][2] - t->p[0][2]) * (t->p[2][0] - t->p[0][0]) -
           (t->p[1][0] - t->p[0][0]) * (t->p[2][2] - t->p[0][2]);
}

static unsigned quad(const float* p, const float* m, const float* origin, TriangleList* out) {
    MeshWedge wedges[4] = {0};
    for (int k = 0; k < 4; ++k) wedges[k].position = (uint16_t)k;
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    return progmesh_faces(p, 4, indices, 2, wedges, 4, m, origin, origin, 3000, out);
}

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    /* A real-world offset and 90-degree yaw catch missing transforms, wrong
       units and confusing the rolling scan centre with the fixed origin. */
    const float origin[3] = {-40233, -1789, -154343};
    const float m[16] = {0, 0, -1, -40233, 0, 1, 0, -1789, 1, 0, 0, -154343, 0, 0, 0, 1};
    const float floor[12] = {-5000, 0, -5000, -5000, 0, 5000, 5000, 0, 5000, 5000, 0, -5000};
    TriangleList open = {0}, wall = {0}, rejected = {0};
    CHECK(quad(floor, m, origin, &open) == 2);
    CHECK(normal_y(&open.items[0]) > 0 && normal_y(&open.items[1]) > 0);
    CHECK(fabsf(open.items[0].p[0][0] - 50) < .001f && fabsf(open.items[0].p[0][2] - 50) < .001f);
    CHECK(quad(floor, m, origin, &wall) == 2);
    for (int side = 0; side < 4; ++side) {
        float p[12];
        const float ends[4][2] = {{-5000, 0}, {5000, 0}, {5000, 300}, {-5000, 300}};
        for (int k = 0; k < 4; ++k) {
            p[k * 3 + (side < 2 ? 0 : 2)] = ends[k][0];
            p[k * 3 + 1] = ends[k][1];
            p[k * 3 + (side < 2 ? 2 : 0)] = side % 2 ? 150 : -150;
        }
        CHECK(quad(p, m, origin, &wall) == 4); /* each wall is two-sided */
    }
    CHECK(wall.count == 18);

    const float points[9] = {-100, 200, -100, 100, 200, 100, 100, 200, -100};
    MeshWedge wedges[3] = {0};
    for (int k = 0; k < 3; ++k) wedges[k].position = (uint16_t)k;
    uint16_t indices[3] = {0, 2, 1}; /* downward ceiling stays downward */
    CHECK(progmesh_faces(points, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected) == 1);
    CHECK(normal_y(&rejected.items[0]) < 0);
    triangles_free(&rejected);
    indices[1] = 3;
    CHECK(!progmesh_faces(points, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected));
    indices[1] = 2; wedges[1].position = 3;
    CHECK(!progmesh_faces(points, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected));
    wedges[1].position = 1;
    float bad[9]; memcpy(bad, points, sizeof(bad)); bad[0] = NAN;
    CHECK(!progmesh_faces(bad, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected));
    memset(bad, 0, sizeof(bad));
    CHECK(!progmesh_faces(bad, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected));
    float far[3] = {origin[0] + 100000, origin[1], origin[2]};
    CHECK(!progmesh_faces(points, 3, indices, 1, wedges, 3, m, origin, far, 3000, &rejected));
    /* Every vertex may be outside the scan while the triangle spans it. */
    CHECK(quad(floor, m, origin, &rejected) == 2);
    triangles_free(&rejected);
    rejected.count = CAPTURE_TRIANGLE_LIMIT;
    CHECK(!progmesh_faces(points, 3, indices, 1, wedges, 3, m, origin, origin, 3000, &rejected) && rejected.overflow);
    rejected.count = 0;
    CHECK(write_geometry(argv[1], &open, 1) && write_rails(argv[2], NULL, 0, 1));
    CHECK(write_geometry(argv[3], &wall, 2) && write_rails(argv[4], NULL, 0, 2));
    triangles_free(&open); triangles_free(&wall);
    puts("PASS mesh extraction: world transform, winding, two-sided walls, indices, NaN, degeneracy, bounds, overflow");
    return 0;
}

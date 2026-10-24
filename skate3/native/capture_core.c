#include "capture_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void triangles_push(TriangleList* list, const float a[3], const float b[3], const float c[3]) {
    if (list->count >= CAPTURE_TRIANGLE_LIMIT) { list->overflow = 1; return; }
    if (list->count == list->capacity) {
        unsigned capacity = list->capacity ? list->capacity * 2 : 4096;
        Triangle* grown = realloc(list->items, capacity * sizeof(Triangle));
        if (!grown) { list->overflow = 1; return; }
        list->items = grown; list->capacity = capacity;
    }
    Triangle* triangle = &list->items[list->count++];
    memcpy(triangle->p[0], a, 12); memcpy(triangle->p[1], b, 12); memcpy(triangle->p[2], c, 12);
}

void triangles_free(TriangleList* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void subtract(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i];
}

static void cross(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

_Static_assert(sizeof(MeshWedge) == 0x18, "Gothic wedge stride");

unsigned progmesh_faces(const float* positions, unsigned position_count,
    const uint16_t* triangles, unsigned triangle_count, const MeshWedge* wedges, unsigned wedge_count,
    const float m[16], const float origin[3], const float scan[3], float radius, TriangleList* out) {
    unsigned before = out->count;
    for (unsigned t = 0; t < triangle_count && !out->overflow; ++t) {
        float world[3][3], core[3][3];
        int valid = 1;
        for (int k = 0; k < 3 && valid; ++k) {
            unsigned wedge = triangles[t * 3 + k];
            unsigned index = wedge < wedge_count ? wedges[wedge].position : position_count;
            if (index >= position_count) { valid = 0; break; }
            const float* p = positions + index * 3;
            for (int r = 0; r < 3; ++r) {
                world[k][r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
                if (!isfinite(world[k][r])) valid = 0;
            }
        }
        if (!valid) continue;
        int outside = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float low = fminf(world[0][axis], fminf(world[1][axis], world[2][axis]));
            float high = fmaxf(world[0][axis], fmaxf(world[1][axis], world[2][axis]));
            outside |= low > scan[axis] + radius || high < scan[axis] - radius;
        }
        if (outside) continue;
        for (int k = 0; k < 3; ++k) {
            core[k][0] = (world[k][0] - origin[0]) / 100;
            core[k][1] = (world[k][1] - origin[1]) / 100;
            core[k][2] = -(world[k][2] - origin[2]) / 100;
        }
        /* Z reflection reverses winding. */
        const float *a = core[0], *b = core[2], *c = core[1];
        float u[3], v[3], n[3];
        subtract(b, a, u); subtract(c, a, v); cross(u, v, n);
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!isfinite(length) || length < 1e-6f) continue;
        triangles_push(out, a, b, c);
        if (fabsf(n[1]) / length <= .7f) triangles_push(out, a, c, b);
    }
    return out->count - before;
}

void ground_fill(const float* sampled, float* support) {
    memcpy(support, sampled, GROUND_CELLS * sizeof(float));
    for (int z = 0; z < GROUND_SIZE; ++z) for (int x = 0; x < GROUND_SIZE; ++x) {
        float near[8], low = INFINITY;
        int count = 0;
        for (int j = z - 1; j <= z + 1; ++j) for (int i = x - 1; i <= x + 1; ++i) {
            if (i < 0 || j < 0 || i >= GROUND_SIZE || j >= GROUND_SIZE || (i == x && j == z)) continue;
            float value = sampled[j * GROUND_SIZE + i];
            if (isfinite(value)) { near[count++] = value; if (value < low) low = value; }
        }
        if (count < 2) continue;
        float point = sampled[z * GROUND_SIZE + x];
        if (isfinite(point)) {
            int level = 0;
            for (int k = 0; k < count; ++k) level += near[k] - low <= GROUND_STEP;
            int obstacle = point - low > GROUND_STEP && level >= 2;
            int pit = low - point > GROUND_STEP;
            if (!obstacle && !pit) continue;
        }
        support[z * GROUND_SIZE + x] = low;
    }
}

static void ground_point(const float* support, GroundCentre centre, int x, int z, float* out) {
    out[0] = centre.x + (float)(x - GROUND_RADIUS);
    out[1] = support[z * GROUND_SIZE + x];
    out[2] = -(centre.z + (float)(z - GROUND_RADIUS));
}

unsigned ground_triangles(const float* support, GroundCentre centre, TriangleList* out) {
    unsigned added = 0;
    for (int z = 0; z < GROUND_SIZE - 1; ++z) for (int x = 0; x < GROUND_SIZE - 1; ++x) {
        float a[3], b[3], d[3], e[3];
        ground_point(support, centre, x, z, a); ground_point(support, centre, x + 1, z, b);
        ground_point(support, centre, x, z + 1, d); ground_point(support, centre, x + 1, z + 1, e);
        const float* triangles[2][3] = {{a, b, e}, {a, e, d}};
        for (int t = 0; t < 2; ++t) {
            const float** p = triangles[t];
            if (!isfinite(p[0][1]) || !isfinite(p[1][1]) || !isfinite(p[2][1])) continue;
            float high = fmaxf(p[0][1], fmaxf(p[1][1], p[2][1])), low = fminf(p[0][1], fminf(p[1][1], p[2][1]));
            if (high - low > GROUND_STEP) continue; /* no fake ramp across a large step */
            float u[3], v[3], normal[3];
            subtract(p[1], p[0], u); subtract(p[2], p[0], v); cross(u, v, normal);
            if (normal[1] < 1e-5f) continue;
            triangles_push(out, p[0], p[1], p[2]);
            ++added;
        }
    }
    return added;
}

int ground_support_at(const float* support, GroundCentre centre, float x, float z, float* height) {
    float fx = x - centre.x + GROUND_RADIUS, fz = z - centre.z + GROUND_RADIUS;
    int cx = (int)floorf(fx), cz = (int)floorf(fz), found = 0;
    if (cx < 0 || cz < 0 || cx >= GROUND_SIZE - 1 || cz >= GROUND_SIZE - 1) return 0;
    for (int j = cz; j <= cz + 1; ++j) for (int i = cx; i <= cx + 1; ++i) {
        float value = support[j * GROUND_SIZE + i];
        if (isfinite(value) && (!found++ || value < *height)) *height = value;
    }
    return found;
}

static void core(const float origin[3], float x, float y, float z, float* out) {
    out[0] = (x - origin[0]) / 100; out[1] = (y - origin[1]) / 100; out[2] = -(z - origin[2]) / 100;
}

void box_faces(const ObjectBox* box, const float origin[3], TriangleList* out) {
    float x0 = box->low[0], y0 = box->low[1], z0 = box->low[2], x1 = box->high[0], y1 = box->high[1], z1 = box->high[2];
    float centre[3];
    core(origin, (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2, centre);
    const float faces[5][4][3] = {
        {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, /* top; no bottom */
        {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}},
        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}},
        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}},
        {{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}},
    };
    for (int f = 0; f < 5; ++f) {
        float q[4][3];
        for (int k = 0; k < 4; ++k) core(origin, faces[f][k][0], faces[f][k][1], faces[f][k][2], q[k]);
        const float* triangles[2][3] = {{q[0], q[1], q[2]}, {q[0], q[2], q[3]}};
        for (int t = 0; t < 2; ++t) {
            const float** p = triangles[t];
            float u[3], v[3], normal[3], middle[3];
            subtract(p[1], p[0], u); subtract(p[2], p[0], v); cross(u, v, normal);
            for (int i = 0; i < 3; ++i) middle[i] = (p[0][i] + p[1][i] + p[2][i]) / 3 - centre[i];
            if (normal[0] * middle[0] + normal[1] * middle[1] + normal[2] * middle[2] < 0) triangles_push(out, p[0], p[2], p[1]);
            else triangles_push(out, p[0], p[1], p[2]);
        }
    }
}

unsigned box_rails(const ObjectBox* boxes, unsigned count, const float origin[3], float feet, Rail* rails, unsigned limit) {
    unsigned made = 0;
    for (unsigned n = 0; n < count && made < limit; ++n) {
        const ObjectBox* box = &boxes[n];
        float rise = box->high[1] - feet;
        if (!box->aligned || rise < 15 || rise > 120) continue;
        const float corners[4][2] = {{box->low[0], box->low[2]}, {box->high[0], box->low[2]},
                                     {box->high[0], box->high[2]}, {box->low[0], box->high[2]}};
        for (int k = 0; k < 4 && made < limit; ++k) {
            const float* a = corners[k];
            const float* b = corners[(k + 1) % 4];
            if (hypotf(b[0] - a[0], b[1] - a[1]) < 100) continue;
            Rail rail;
            core(origin, a[0], box->high[1], a[1], rail.a);
            core(origin, b[0], box->high[1], b[1], rail.b);
            int duplicate = 0; /* the same edge shared by two boxes */
            for (unsigned m = 0; m < made && !duplicate; ++m) {
                const Rail* other = &rails[m];
                for (int flip = 0; flip < 2 && !duplicate; ++flip) {
                    const float* p = flip ? other->b : other->a;
                    const float* q = flip ? other->a : other->b;
                    duplicate = fabsf(p[0] - rail.a[0]) < .01f && fabsf(p[2] - rail.a[2]) < .01f &&
                                fabsf(q[0] - rail.b[0]) < .01f && fabsf(q[2] - rail.b[2]) < .01f &&
                                fabsf(p[1] - rail.a[1]) < .01f;
                }
            }
            if (!duplicate) rails[made++] = rail;
        }
    }
    return made;
}

int floor_under_origin(const TriangleList* list, float* floor) {
    int found = 0;
    for (unsigned n = 0; n < list->count; ++n) {
        const float* a = list->items[n].p[0];
        float v[3], u[3];
        subtract(list->items[n].p[1], a, v); subtract(list->items[n].p[2], a, u);
        float determinant = v[0] * u[2] - u[0] * v[2];
        if (fabsf(determinant) < 1e-7f) continue;
        float s = (-a[0] * u[2] + u[0] * a[2]) / determinant;
        float t = (-v[0] * a[2] + a[0] * v[2]) / determinant;
        if (s >= -1e-5f && t >= -1e-5f && s + t <= 1.00001f) {
            float y = a[1] + s * v[1] + t * u[1];
            if (y > -10 && y <= .5f && (!found++ || y > *floor)) *floor = y;
        }
    }
    return found;
}

int write_geometry(const char* path, const TriangleList* list, unsigned area) {
    /* Batch complete wire records. Large ship captures contain ~35,000
       triangles; three stdio calls per triangle made recapture expensive. */
    typedef struct GeometryRecord { float p[9]; uint16_t unused, area; uint32_t model; } GeometryRecord;
    _Static_assert(sizeof(GeometryRecord) == 44, "Pinned BMGEO2 record size");
    GeometryRecord* batch = malloc(1024 * sizeof(*batch));
    if (!batch) return 0;
    FILE* file = fopen(path, "wb");
    if (!file) { free(batch); return 0; }
    uint32_t count = list->count;
    int ok = fwrite("BMGEO2\0\0", 1, 8, file) == 8 && fwrite(&count, 4, 1, file) == 1;
    for (unsigned n = 0; ok && n < list->count;) {
        unsigned size = list->count - n;
        if (size > 1024) size = 1024;
        for (unsigned k = 0; k < size; ++k) {
            memcpy(batch[k].p, list->items[n + k].p, 36);
            batch[k].unused = 0; batch[k].area = (uint16_t)area; batch[k].model = 0;
        }
        ok = fwrite(batch, sizeof(*batch), size, file) == size;
        n += size;
    }
    int closed = fclose(file) == 0;
    free(batch);
    return closed && ok;
}

int write_rails(const char* path, const Rail* rails, unsigned count, unsigned area) {
    FILE* file = fopen(path, "wb");
    if (!file) return 0;
    uint32_t total = count;
    int ok = fwrite("BMRL3\0\0\0", 1, 8, file) == 8 && fwrite(&total, 4, 1, file) == 1;
    for (unsigned n = 0; ok && n < count; ++n) {
        uint16_t tag = (uint16_t)area, flags = 0;
        uint32_t points = 2;
        ok = fwrite(&tag, 2, 1, file) == 1 && fwrite(&flags, 2, 1, file) == 1 && fwrite(&points, 4, 1, file) == 1 &&
             fwrite(rails[n].a, 4, 3, file) == 3 && fwrite(rails[n].b, 4, 3, file) == 3;
    }
    return !fclose(file) && ok;
}

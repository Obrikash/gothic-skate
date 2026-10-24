/* Engine-free capture geometry, ported from tools/arm_bridge.py and
 * tools/export_host_geometry.py so the bridge can capture on F8 in-process.
 * Core space: meters relative to Marvin, x' = dx, y' = dy, z' = -dz. */
#pragma once
#include <stdint.h>

#define GROUND_RADIUS 15
#define GROUND_SIZE (2 * GROUND_RADIUS + 1)
#define GROUND_CELLS (GROUND_SIZE * GROUND_SIZE)
#define GROUND_STEP .75f          /* meters: no fake ramp across a larger step */
#define CAPTURE_TRIANGLE_LIMIT 250000
#define CAPTURE_RAIL_LIMIT 512

typedef struct Triangle { float p[3][3]; } Triangle;
typedef struct TriangleList { Triangle* items; unsigned count, capacity; int overflow; } TriangleList;
/* Gothic world centimeters, as stored in zCVob::bbox3D. */
typedef struct ObjectBox { float low[3], high[3]; int aligned; } ObjectBox;
typedef struct Rail { float a[3], b[3]; } Rail;

void triangles_push(TriangleList* list, const float a[3], const float b[3], const float c[3]);
void triangles_free(TriangleList* list);

/* zCProgMeshProto's verified 0x18-byte wedge layout. The host validates the
 * backing memory; this function checks indices, coordinates and capture bounds.
 * The transform/origin/scan use Gothic world cm. Core Z reflects Gothic Z.
 * Keep floors/ceilings on their original side; walls block from both sides. */
typedef struct MeshWedge { float normal[3], uv[2]; uint16_t position, pad; } MeshWedge;
unsigned progmesh_faces(const float* positions, unsigned position_count,
    const uint16_t* triangles, unsigned triangle_count, const MeshWedge* wedges, unsigned wedge_count,
    const float transform[16], const float origin[3], const float scan[3], float radius, TriangleList* out);

/* sampled: GROUND_CELLS heights (core meters, NAN = no hit), rows along Gothic +Z.
 * support: the same grid with missing samples, obstacle tops and isolated pits
 * filled at the neighbouring floor, exactly as measured_support() in Python. */
void ground_fill(const float* sampled, float* support);
/* The grid's centre relative to the ride origin, in Gothic meters (+X, +Z):
 * 0, 0 for the first capture; the board's position for rolling recaptures. */
typedef struct GroundCentre { float x, z; } GroundCentre;
/* Two triangles per supported 1 m cell; returns how many were added. */
unsigned ground_triangles(const float* support, GroundCentre centre, TriangleList* out);
/* Lowest support among the four corners of the cell under (x, z) wire meters. */
int ground_support_at(const float* support, GroundCentre centre, float x, float z, float* height);

/* Top and four sides of a box, faces outward, in core meters. */
void box_faces(const ObjectBox* box, const float origin[3], TriangleList* out);
/* Rails along >= 1 m top edges of aligned boxes 15-120 cm above the feet. */
unsigned box_rails(const ObjectBox* boxes, unsigned count, const float origin[3], float feet, Rail* rails, unsigned limit);

/* Highest triangle under core (0, 0) with -10 < y <= 0.5, as floor_at_origin(). */
int floor_under_origin(const TriangleList* list, float* floor);

/* BMGEO2 / BMRL3 files for the pinned worker, every record tagged with area. */
int write_geometry(const char* path, const TriangleList* list, unsigned area);
int write_rails(const char* path, const Rail* rails, unsigned count, unsigned area);

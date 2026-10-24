"""Read nearby static BSP triangles from the inspected, offline Archolos build.

Uses PROCESS_VM_READ only. No injection, game calls, input or memory writes.
Derived collision and addresses stay in the private sibling .modding directory.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = ROOT / ".modding/skate3-archolos"
EXE_SHA = "0655b4811e008a353ce372fde463ef00b6101f3e89be850e2f14980140447c7f"


class Reader:
    def __init__(self, pid):
        self.kernel = c.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "OpenProcess": ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            "ReadProcessMemory": ([w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)], w.BOOL),
            "QueryFullProcessImageNameW": ([w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)], w.BOOL),
            "CloseHandle": ([w.HANDLE], w.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.kernel, name)
            function.argtypes, function.restype = arguments, result
        self.process = self.kernel.OpenProcess(0x10 | 0x1000, False, pid)
        if not self.process:
            raise c.WinError(c.get_last_error())
        self.cache = {}
        try:
            name, size = c.create_unicode_buffer(32768), w.DWORD(32768)
            if not self.kernel.QueryFullProcessImageNameW(self.process, 0, name, c.byref(size)):
                raise c.WinError(c.get_last_error())
            self.path = Path(name.value).resolve()
            if self.path != (ROOT / "system/Gothic2.exe").resolve() or hashlib.sha256(self.path.read_bytes()).hexdigest() != EXE_SHA:
                raise RuntimeError("PID is not the inspected local Archolos executable")
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.process:
            self.kernel.CloseHandle(self.process)
            self.process = None

    def read(self, address, size):
        if not 0x10000 <= address < 0x100000000 or not 0 < size <= 32 * 1024 * 1024:
            raise ValueError("Invalid x86 read range")
        buffer, received = c.create_string_buffer(size), c.c_size_t()
        if not self.kernel.ReadProcessMemory(self.process, address, buffer, size, c.byref(received)) or received.value != size:
            raise c.WinError(c.get_last_error())
        return buffer.raw

    def cached(self, address, size):
        page = address & ~4095
        if address + size > page + 4096:
            return self.read(address, size)
        if page not in self.cache:
            self.cache[page] = self.read(page, 4096)
        return self.cache[page][address - page:address - page + size]

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def floor_at_origin(triangles):
    heights = []
    for a, b, point in triangles:
        v, u = subtract(b, a), subtract(point, a)
        determinant = v[0] * u[2] - u[0] * v[2]
        if abs(determinant) < 1e-7:
            continue
        s = (-a[0] * u[2] + u[0] * a[2]) / determinant
        t = (-v[0] * a[2] + a[0] * v[2]) / determinant
        if s >= -1e-5 and t >= -1e-5 and s + t <= 1.00001:
            y = a[1] + s * v[1] + t * u[1]
            if -10 < y <= .5:
                heights.append(y)
    return max(heights) if heights else None


OBJECT_RADIUS = 1600  # cm around Marvin; matches the 14 m skating cap plus margin
OBJECT_NODE_LIMIT = 400_000


def class_chain(reader, vtable, cache):
    """Class names of a vob, most derived first, read from its zCClassDef chain.

    Every zCObject's first virtual is _GetClassDef, which in this build is
    `mov eax, <classDef>; ret` (checked on zCVob, oCMOB and oCNpc).
    """
    if vtable in cache:
        return cache[vtable]
    names = []
    code = reader.cached(reader.u32(vtable), 6)
    if code[0] == 0xB8 and code[5] == 0xC3:
        definition = struct.unpack_from("<I", code, 1)[0]
        while definition and len(names) < 12:
            text, length = struct.unpack("<II", reader.cached(definition + 8, 8))  # zSTRING _Ptr, _Len
            names.append(reader.read(text, length).decode("latin1") if 0 < length < 64 else "?")
            definition = reader.u32(definition + 0x3C)  # baseClassDef
    cache[vtable] = names
    return names


def object_boxes(reader, layout, world, origin, feet):
    """Axis-aligned boxes of nearby static objects with collision, from the world vob tree.

    Static BSP misses every placed object (crates, tables, fences, house meshes).
    Plain zCVob meshes and oCMOB objects with static collision count; doors,
    NPCs, items, lights, triggers and movers do not.
    """
    root = world + layout["G2A_WORLD_VOB_TREE"]
    stack, visited, cache, boxes, skipped = [root], 0, {}, [], {}
    while stack:
        node = stack.pop()
        visited += 1
        if visited > OBJECT_NODE_LIMIT:
            raise RuntimeError("Vob tree is larger than expected")
        parent, child, following, previous, vob = struct.unpack("<5I", reader.cached(node, 20))
        if following:
            stack.append(following)
        if child:
            stack.append(child)
        if not vob:
            continue
        header = reader.cached(vob, 4)
        flags = reader.cached(vob + layout["G2A_VOB_FLAGS"], 1)[0]
        if not flags & 0x40 or not reader.u32(vob + layout["G2A_VOB_VISUAL"]):
            continue
        box = struct.unpack("<6f", reader.cached(vob + layout["G2A_VOB_BBOX"], 24))
        low, high = box[:3], box[3:]
        if not all(math.isfinite(value) for value in box):
            continue
        if (low[0] > origin[0] + OBJECT_RADIUS or high[0] < origin[0] - OBJECT_RADIUS or
                low[2] > origin[2] + OBJECT_RADIUS or high[2] < origin[2] - OBJECT_RADIUS):
            continue
        names = class_chain(reader, struct.unpack("<I", header)[0], cache)
        kind = names[0] if names else "?"
        allowed = kind == "zCVob" or ("oCMOB" in names and "oCMobDoor" not in names)
        size = [high[i] - low[i] for i in range(3)]
        reason = None
        if not allowed:
            reason = "class " + kind
        elif max(size[0], size[2]) > 800 or size[1] > 600:
            reason = "too large"
        elif min(size[0], size[2]) < 5 or size[1] < 10:
            reason = "too small"
        elif low[1] > feet + 180 or high[1] < feet + 5:
            reason = "overhead or flush with the floor"
        elif low[0] <= origin[0] <= high[0] and low[2] <= origin[2] <= high[2]:
            reason = "contains Marvin"
        if reason:
            skipped[reason] = skipped.get(reason, 0) + 1
            continue
        # An unrotated, upright object has a tight AABB, so its top edges are real ledges.
        matrix = struct.unpack("<16f", reader.cached(vob + layout["G2A_VOB_MATRIX"], 64))
        aligned = matrix[5] > 0.985 and max(abs(matrix[0]), abs(matrix[8])) > 0.985
        boxes.append({"class": kind, "low": list(low), "high": list(high), "aligned": aligned})
    return boxes, {"visitedNodes": visited, "skipped": skipped}


def box_triangles(box, origin):
    """Top and four sides of a box in core meters, faces pointing outward (no bottom)."""
    low, high = box["low"], box["high"]

    def core(x, y, z):
        return ((x - origin[0]) / 100, (y - origin[1]) / 100, -(z - origin[2]) / 100)

    x0, y0, z0, x1, y1, z1 = low[0], low[1], low[2], high[0], high[1], high[2]
    centre = core((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    faces = [
        [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],  # top
        [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
        [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
        [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)],
        [(x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0)],
    ]
    triangles = []
    for face in faces:
        a, b, c_, d = (core(*point) for point in face)
        for triangle in [(a, b, c_), (a, c_, d)]:
            normal = cross(subtract(triangle[1], triangle[0]), subtract(triangle[2], triangle[0]))
            middle = [sum(point[i] for point in triangle) / 3 for i in range(3)]
            if sum(normal[i] * (middle[i] - centre[i]) for i in range(3)) < 0:
                triangle = (triangle[0], triangle[2], triangle[1])
            triangles.append(triangle)
    return triangles


def export(pid, radius):
    layout = {key: int(value, 16) for key, value in re.findall(
        r"#define (G2A_\w+) 0x([0-9A-F]+)u", (PRIVATE / "build/gothic_layout.h").read_text())}
    started = time.monotonic()
    reader = Reader(pid)
    try:
        game = reader.u32(layout["G2A_GAME"])
        player = reader.u32(layout["G2A_PLAYER"])
        if not game or not player:
            raise RuntimeError("Load Marvin into a world before exporting")
        world = reader.u32(game + layout["G2A_SESSION_WORLD"])
        if not world or reader.u32(game + layout["G2A_GAME_LOADING"]) or reader.u32(game + layout["G2A_GAME_LEVEL_CHANGE"]):
            raise RuntimeError("World is unavailable or changing")
        matrix = struct.unpack("<16f", reader.read(player + layout["G2A_VOB_MATRIX"], 64))
        origin = [matrix[3], matrix[7], matrix[11]]
        bounds = struct.unpack("<6f", reader.read(player + layout["G2A_VOB_BBOX"], 24))
        if not all(math.isfinite(value) and abs(value) < 1e8 for value in origin):
            raise RuntimeError("Invalid host position")
        bsp = world + layout["G2A_WORLD_BSP"]
        count = reader.u32(bsp + layout["G2A_BSP_POLY_COUNT"])
        polygon_array = reader.u32(bsp + layout["G2A_BSP_POLYS"])
        if not 1 <= count <= 2_000_000:
            raise RuntimeError(f"Unexpected BSP polygon count: {count}")
        polygon_pointers = struct.unpack(f"<{count}I", reader.read(polygon_array, count * 4))
        triangles, seen, ignored_portals = [], set(), 0
        for address in polygon_pointers:
            if address in seen:
                continue
            seen.add(address)
            polygon = reader.cached(address, 52)
            if polygon[layout["G2A_POLYGON_FLAGS"]] & 3:
                ignored_portals += 1
                continue
            vertices = polygon[layout["G2A_POLYGON_VERTEX_COUNT"]]
            if not 3 <= vertices <= 21:
                raise RuntimeError("Invalid BSP polygon vertex count")
            pointers = struct.unpack(f"<{vertices}I", reader.cached(
                struct.unpack_from("<I", polygon, layout["G2A_POLYGON_VERTICES"])[0], vertices * 4))
            points = [struct.unpack("<3f", reader.cached(pointer + layout["G2A_VERTEX_POSITION"], 12)) for pointer in pointers]
            if not all(math.isfinite(value) and abs(value) < 1e8 for point in points for value in point):
                raise RuntimeError("Non-finite or implausible world vertex")
            # AABB overlap retains large triangles spanning the selected area.
            if any(min(point[axis] for point in points) > origin[axis] + radius * 100 or
                   max(point[axis] for point in points) < origin[axis] - radius * 100 for axis in range(3)):
                continue
            converted = [((point[0] - origin[0]) / 100, (point[1] - origin[1]) / 100,
                          -(point[2] - origin[2]) / 100) for point in points]
            for index in range(1, vertices - 1):
                # Reflection reverses winding; preserve the original face side.
                triangle = (converted[0], converted[index + 1], converted[index])
                normal = cross(subtract(triangle[1], triangle[0]), subtract(triangle[2], triangle[0]))
                if sum(value * value for value in normal) > 1e-12:
                    triangles.append(triangle)
        if reader.u32(layout["G2A_GAME"]) != game or reader.u32(layout["G2A_PLAYER"]) != player or reader.u32(game + layout["G2A_SESSION_WORLD"]) != world:
            raise RuntimeError("Host world changed during capture; discard and retry after loading")
        if reader.u32(bsp + layout["G2A_BSP_POLYS"]) != polygon_array or reader.u32(bsp + layout["G2A_BSP_POLY_COUNT"]) != count:
            raise RuntimeError("Host BSP changed during capture")
        if not triangles:
            raise RuntimeError("No nearby static world triangles found")
        bsp_triangles = len(triangles)
        boxes, object_report = object_boxes(reader, layout, world, origin, bounds[1])
        for box in boxes:
            triangles.extend(box_triangles(box, origin))
        if reader.u32(game + layout["G2A_SESSION_WORLD"]) != world:
            raise RuntimeError("Host world changed during object capture; discard and retry")
        target = PRIVATE / "host-export"
        target.mkdir(exist_ok=True)
        data = b"BMGEO2\0\0" + struct.pack("<I", len(triangles))
        data += b"".join(struct.pack("<9fHHI", *(value for point in triangle for value in point), 0, 0, 0) for triangle in triangles)
        (target / "world.bmgeo").write_bytes(data)
        (target / "objects.json").write_text(json.dumps({"pid": pid, "worldPointer": world, "originCentimeters": origin,
                                                         "boxes": boxes, **object_report}, indent=1))
        report = {"kind": "read-only real Archolos static BSP + object box capture", "pid": pid, "worldPointer": world,
                  "originCentimeters": origin, "forward": [matrix[2], matrix[6], matrix[10]],
                  "playerBoundsCentimeters": list(bounds),
                  "radiusMeters": radius, "bspPolygons": count, "triangles": len(triangles),
                  "bspTriangles": bsp_triangles, "objectBoxes": len(boxes), "objectsSkipped": object_report["skipped"],
                  "ignoredPortals": ignored_portals, "floorAtOriginMeters": floor_at_origin(triangles),
                  "mapping": "core=(Gothic-origin)*(0.01,0.01,-0.01); reverse winding",
                  "sha256": hashlib.sha256(data).hexdigest(), "seconds": time.monotonic() - started,
                  "limitations": "static BSP plus axis-aligned boxes of nearby static objects; rotated objects are over-sized, moving objects and material-specific physics are excluded"}
        (target / "capture.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report, indent=2))
    finally:
        reader.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--radius", type=float, default=50)
    arguments = parser.parse_args()
    if not 2 <= arguments.radius <= 200:
        parser.error("radius must be 2 to 200 meters")
    export(arguments.pid, arguments.radius)

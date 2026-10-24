"""Prepare measured local collision, check it in the real x86 client, then arm F8.

The prototype combines static BSP with a small measured ground heightfield. It
does not reconstruct all Vob meshes, rails, moving objects or rider animation.
"""
import argparse
import configparser
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import time

from export_host_geometry import PRIVATE, Reader, cross, floor_at_origin, subtract
from probe_worker import LAB
from native_artifacts import latest


STEP = .75


def measured_support(snapshot, origin):
    """Triangulate sampled ground in core meters; return (triangles, support points).

    Archolos floors here are objects resting about 0.77 m above the static BSP.
    A missing sample or a dropped step triangle therefore opens a pit onto that
    hidden BSP floor. Fill such cells at the neighbouring floor height instead,
    including isolated deep samples. The board passes through obstacles at floor
    level, as with every Vob today.
    """
    radius, spacing = snapshot["gridRadius"], snapshot["spacing"]
    size, grid, centre = radius * 2 + 1, snapshot["grid"], snapshot["position"]

    def convert(point):
        if point is None:
            return None
        if len(point) != 3 or not all(math.isfinite(value) for value in point):
            raise RuntimeError("Invalid measured terrain point")
        return ((point[0] - origin[0]) / 100, (point[1] - origin[1]) / 100, -(point[2] - origin[2]) / 100)

    sampled = [convert(point) for point in grid]
    points = list(sampled)
    for z in range(size):
        for x in range(size):
            near = [sampled[j * size + i] for j in range(max(z - 1, 0), min(z + 2, size))
                    for i in range(max(x - 1, 0), min(x + 2, size)) if (i, j) != (x, z)]
            near = [point for point in near if point is not None]
            if len(near) < 2:
                continue
            low = min(point[1] for point in near)
            point = sampled[z * size + x]
            obstacle = point is not None and point[1] - low > STEP and sum(other[1] - low <= STEP for other in near) >= 2
            pit = point is not None and low - point[1] > STEP
            if point is not None and not obstacle and not pit:
                continue
            position = (centre[0] + (x - radius) * spacing, 0, centre[2] + (z - radius) * spacing)
            filled = convert(position)
            points[z * size + x] = (filled[0], low, filled[2])
    triangles = []
    for z in range(size - 1):
        for x in range(size - 1):
            a, b, d, e = (points[z * size + x], points[z * size + x + 1],
                          points[(z + 1) * size + x], points[(z + 1) * size + x + 1])
            for triangle in [(a, b, e), (a, e, d)]:
                if any(point is None for point in triangle):
                    continue
                # Do not bridge large vertical steps with a fake ramp.
                if max(point[1] for point in triangle) - min(point[1] for point in triangle) > STEP:
                    continue
                normal = cross(subtract(triangle[1], triangle[0]), subtract(triangle[2], triangle[0]))
                if normal[1] < 1e-5:
                    continue
                triangles.append(triangle)
    return triangles, points


RAIL_HEIGHT = (15, 120)  # cm above Marvin's feet: kerbs, crates, benches, ledges
RAIL_MIN_LENGTH = 100    # cm
RAIL_LIMIT = 512


def object_rails(objects, origin, feet):
    """Grind rails along the top edges of unrotated object boxes, in core meters.

    BMRL3 (BullySkate skate-runtime bully_map.rs parse_rails): 'BMRL3\\0\\0\\0',
    u32 count, then per rail u16 area, u16 flags (1 = closed), u32 points, f32x3.
    Area 0 is the worker's startup area, matching the collision records.
    """
    def core(x, y, z):
        return ((x - origin[0]) / 100, (y - origin[1]) / 100, -(z - origin[2]) / 100)

    rails, seen = [], set()
    for box in objects:
        low, high = box["low"], box["high"]
        if not box.get("aligned") or not RAIL_HEIGHT[0] <= high[1] - feet <= RAIL_HEIGHT[1]:
            continue
        corners = [(low[0], low[2]), (high[0], low[2]), (high[0], high[2]), (low[0], high[2])]
        for (ax, az), (bx, bz) in zip(corners, corners[1:] + corners[:1]):
            if math.hypot(bx - ax, bz - az) < RAIL_MIN_LENGTH:
                continue
            key = tuple(sorted([(round(ax), round(az)), (round(bx), round(bz))]) + [round(high[1])])
            if key in seen:
                continue
            seen.add(key)
            rails.append([core(ax, high[1], az), core(bx, high[1], bz)])
    rails = rails[:RAIL_LIMIT]
    data = b"BMRL3\0\0\0" + struct.pack("<I", len(rails))
    for rail in rails:
        data += struct.pack("<HHI", 0, 0, len(rail)) + b"".join(struct.pack("<3f", *point) for point in rail)
    return data, len(rails)


def support_bytes(snapshot, origin, points, pid, world):
    """Support heights for the bridge's per-cell fall guard (host-support.bin).

    Rows follow the sample (Gothic +Z), so a cell is indexed directly by the
    worker's wire output[0] and output[1]. Heights are core meters; NaN = none.
    """
    radius, spacing, centre = snapshot["gridRadius"], snapshot["spacing"] / 100, snapshot["position"]
    header = b"SKSUP1\0\0" + struct.pack("<IIifff", pid, world, radius, spacing,
                                         (centre[0] - origin[0]) / 100, (centre[2] - origin[2]) / 100)
    return header + struct.pack(f"<{len(points)}f", *[math.nan if point is None else point[1] for point in points])


def main(pid):
    found = latest("bridge-state.json", pid)
    if not found:
        raise RuntimeError("No bridge state for this process")
    _, state = found
    if state["pid"] != pid or state["phase"] != 0 or state["movementAuthority"]:
        raise RuntimeError("The bridge must be idle in this exact game process before arming")
    captured = PRIVATE / "host-export/capture.json"
    sampled = PRIVATE / "host-snapshot.json"
    ground_age = time.time() - sampled.stat().st_mtime
    if time.time() - captured.stat().st_mtime > 120 or ground_age > 60:
        raise RuntimeError("Capture is stale; export this world again with Marvin standing still")
    capture, snapshot = json.loads(captured.read_text()), json.loads(sampled.read_text())
    if capture["pid"] != pid or snapshot["pid"] != pid or capture["worldPointer"] != snapshot["worldPointer"]:
        raise RuntimeError("BSP and sampled ground must belong to the same current world")
    origin = capture["originCentimeters"]
    if not snapshot.get("requestId"):
        raise RuntimeError("Request fresh ground through the consolidated bridge first")
    if math.dist(origin, snapshot["position"]) > 10:
        raise RuntimeError("Marvin moved during capture; stand still and recapture")
    coverage = snapshot["gridRadius"] * snapshot["spacing"] / 100
    remaining = coverage - max(abs(origin[0] - snapshot["position"][0]), abs(origin[2] - snapshot["position"][2])) / 100
    allowed_distance = min(14.0, remaining - .25)
    if allowed_distance < 1:
        raise RuntimeError("Marvin is too close to the measured patch edge; a new ground sample is needed")
    grid, size = snapshot["grid"], snapshot["gridRadius"] * 2 + 1
    if len(grid) != size * size or not snapshot["terrainEnabled"]:
        raise RuntimeError("No complete measured terrain grid")
    raw = (PRIVATE / "host-export/world.bmgeo").read_bytes()
    if hashlib.sha256(raw).hexdigest() != capture["sha256"] or raw[:8] != b"BMGEO2\0\0":
        raise RuntimeError("Static geometry checksum mismatch")
    count = struct.unpack_from("<I", raw, 8)[0]
    if len(raw) != 12 + count * 44:
        raise RuntimeError("Static collision size mismatch")
    records = [struct.unpack_from("<9fHHI", raw, 12 + index * 44) for index in range(count)]
    triangles = [tuple(tuple(record[i:i+3]) for i in [0, 3, 6]) for record in records]

    measured, support = measured_support(snapshot, origin)
    reach = [point[1] for point in support if point is not None and math.hypot(point[0], point[2]) <= allowed_distance + 1]
    if not reach:
        raise RuntimeError("No measured support within the skating radius")
    min_support = min(reach)
    added = len(measured)
    triangles.extend(measured)
    records.extend((*[value for point in triangle for value in point], 0, 0, 0) for triangle in measured)
    floor = floor_at_origin(triangles)
    if floor is None or added < 2:
        raise RuntimeError("No measured support surface below Marvin")
    feet = (capture["playerBoundsCentimeters"][1] - origin[1]) / 100
    if abs(floor - feet) > .25:
        raise RuntimeError(f"Measured ground and player feet disagree: {floor=:.3f}, {feet=:.3f}")
    # Read-only identity check; every new load/restart requires a new capture.
    reader = Reader(pid)
    try:
        import re
        layout = {key: int(value, 16) for key, value in re.findall(
            r"#define (G2A_\w+) 0x([0-9A-F]+)u", (PRIVATE / "build/gothic_layout.h").read_text())}
        game = reader.u32(layout["G2A_GAME"])
        player = reader.u32(layout["G2A_PLAYER"])
        if reader.u32(game + layout["G2A_SESSION_WORLD"]) != capture["worldPointer"]:
            raise RuntimeError("The loaded world changed")
        matrix = struct.unpack("<16f", reader.read(player + layout["G2A_VOB_MATRIX"], 64))
        if math.dist(origin, [matrix[3], matrix[7], matrix[11]]) > 25:
            raise RuntimeError("Marvin moved away from the captured origin")
    finally:
        reader.close()
    destination = PRIVATE / "host-worker-lab"
    motion = Path("_derpy_script_loader/scripts/BullyMotion")
    target = destination / motion
    target.mkdir(parents=True, exist_ok=True)
    (target / "assets").mkdir(exist_ok=True)
    preparation = json.loads((LAB / "preparation-receipt.json").read_text())
    worker = LAB / motion / "SkatePhysicsWorker.exe"
    if hashlib.sha256(worker.read_bytes()).hexdigest() != preparation["workerSha256"]:
        raise RuntimeError("Pinned worker checksum mismatch")
    for source in (LAB / motion).iterdir():
        if source.is_file():
            shutil.copy2(source, target / source.name)
    for name in ["jimmy-bind.json", "world-models.txt"]:
        shutil.copy2(LAB / motion / "assets" / name, target / "assets" / name)
    objects = json.loads((PRIVATE / "host-export/objects.json").read_text())
    if objects["pid"] != pid or objects["worldPointer"] != capture["worldPointer"] or objects["originCentimeters"] != origin:
        raise RuntimeError("Object boxes do not belong to this capture")
    rail_data, rail_count = object_rails(objects["boxes"], origin, capture["playerBoundsCentimeters"][1])
    (target / "assets/world.bmrails").write_bytes(rail_data)
    data = b"BMGEO2\0\0" + struct.pack("<I", len(records))
    data += b"".join(struct.pack("<9fHHI", *record) for record in records)
    (target / "assets/world.bmgeo").write_bytes(data)
    (destination / "_derpy_script_loader/logs").mkdir(parents=True, exist_ok=True)
    # The standalone native client owns and cleans up its own worker child.
    tested = subprocess.run([str(PRIVATE / "build/worker-smoke.exe"), str(destination), str(floor), "stationary"],
                            check=True, capture_output=True, text=True, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    result = json.loads(tested.stdout.strip().splitlines()[-1])
    # The bridge refuses to mount without a support grid for this exact process/world.
    staged = PRIVATE / "host-support.bin.new"
    staged.write_bytes(support_bytes(snapshot, origin, support, pid, capture["worldPointer"]))
    staged.replace(PRIVATE / "host-support.bin")
    config = (f"[Bridge]\nEnabled=1\n[World]\nPID={pid}\nPointer={capture['worldPointer']}\n"
              f"OriginX={origin[0]:.9g}\nOriginY={origin[1]:.9g}\nOriginZ={origin[2]:.9g}\nFloor={floor:.9g}\nRadius={allowed_distance + 1:.6g}\n")
    # Keep the user's rider animation choices across re-arming.
    previous = configparser.ConfigParser()
    previous.optionxform = str
    previous.read(PRIVATE / "host-bridge.ini")
    if previous.has_section("Rider"):
        config += "[Rider]\n" + "".join(f"{key}={value}\n" for key, value in previous["Rider"].items())
    temporary = PRIVATE / "host-bridge.ini.new"
    temporary.write_text(config, encoding="ascii")
    temporary.replace(PRIVATE / "host-bridge.ini")
    report = {"pid": pid, "worldPointer": capture["worldPointer"], "bspTriangles": count,
              "measuredGroundTriangles": added, "objectBoxes": len(objects["boxes"]), "grindRails": rail_count,
              "floorMeters": floor, "minSupportMeters": min_support, "feetMeters": feet,
              "collisionSha256": hashlib.sha256(data).hexdigest(), "workerCheck": result,
              "allowedDistanceMeters": allowed_distance, "groundSampleAgeSeconds": ground_age,
              "movementAuthority": False, "nextAction": "F8 in game explicitly mounts; F8 again exits",
              "limitations": "local prototype within allowedDistanceMeters; sampled ground, static BSP, placeholder board, no rider retargeting"}
    (PRIVATE / "arm-receipt.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    main(parser.parse_args().pid)

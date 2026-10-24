"""Check the bridge's C capture core against the Python capture that ran in game.

Builds native/capture_test.c with the pinned Zig, feeds it the latest real ground
sample plus synthetic object boxes, and compares support grid, ground triangles,
floor, box faces, rails and the exact BMGEO2/BMRL3 bytes. Never touches Gothic.
"""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from arm_bridge import PRIVATE, measured_support, object_rails
from build_host_probe import PROJECT
from export_host_geometry import box_triangles, floor_at_origin

GRID = 15


def build(output):
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(PRIVATE / "compiler-cache/global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(PRIVATE / "compiler-cache/local")
    target = output / "capture-test.exe"
    subprocess.run([str(zig), "cc", "-target", "x86-windows-gnu", "-O2", "-Wall", "-Wextra",
                    str(PROJECT / "native/capture_test.c"), str(PROJECT / "native/capture_core.c"), "-o", str(target)],
                   env=environment, cwd=output, check=True)
    return target


def close(a, b, tolerance=1e-4):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def main():
    snapshot = json.loads((PRIVATE / "host-snapshot.json").read_text())
    if snapshot["gridRadius"] != GRID:
        raise SystemExit(f"Need a {2 * GRID + 1}x{2 * GRID + 1} ground sample; found radius {snapshot['gridRadius']}")
    position = snapshot["position"]
    results = []
    with tempfile.TemporaryDirectory(dir=PRIVATE / "build") as folder:
        folder = Path(folder)
        program = build(folder)
        # First capture (grid centred on the ride origin), then a rolling
        # recapture whose grid sits 4.2 m / 2.6 m from an origin 35 cm higher.
        for label, origin in [("centred", list(position)), ("rolling", [position[0] - 420, position[1] + 35, position[2] + 260])]:
            print(f"-- {label}")
            results.append(run_case(snapshot, origin, program, folder))
    if not all(results):
        raise SystemExit(1)


def run_case(snapshot, origin, program, folder):
    position = snapshot["position"]
    centre = ((position[0] - origin[0]) / 100, (position[2] - origin[2]) / 100)
    feet = position[1] - 99.9
    sampled = [math.nan if point is None else (point[1] - origin[1]) / 100 for point in snapshot["grid"]]
    boxes = [  # Gothic cm: a ledge, a crate, a rotated table, a duplicate edge, a tall wall
        {"low": [position[0] + 150, feet, position[2] - 100], "high": [position[0] + 210, feet + 40, position[2] + 100], "aligned": True},
        {"low": [position[0] - 300, feet, position[2] + 200], "high": [position[0] - 180, feet + 90, position[2] + 320], "aligned": True},
        {"low": [position[0] + 400, feet, position[2] + 400], "high": [position[0] + 560, feet + 75, position[2] + 520], "aligned": False},
        {"low": [position[0] + 210, feet, position[2] - 100], "high": [position[0] + 270, feet + 40, position[2] + 100], "aligned": True},
        {"low": [position[0] - 50, feet, position[2] - 600], "high": [position[0] + 450, feet + 300, position[2] - 560], "aligned": True},
    ]
    if True:
        data = struct.pack("<3ff", *origin, feet) + struct.pack("<2f", *centre) + struct.pack(f"<{len(sampled)}f", *sampled) + struct.pack("<I", len(boxes))
        data += b"".join(struct.pack("<7f", *box["low"], *box["high"], 1.0 if box["aligned"] else 0.0) for box in boxes)
        (folder / "input.bin").write_bytes(data)
        subprocess.run([str(program), str(folder / "input.bin"), str(folder / "output.bin"),
                        str(folder / "world.bmgeo"), str(folder / "world.bmrails")], check=True)
        out, cursor = (folder / "output.bin").read_bytes(), 0

        def take(fmt):
            nonlocal cursor
            values = struct.unpack_from(fmt, out, cursor)
            cursor += struct.calcsize(fmt)
            return values

        def triangles():
            count, = take("<I")
            return [take("<9f") for _ in range(count)]

        support = take(f"<{(2 * GRID + 1) ** 2}f")
        ground = triangles()
        found, floor = take("<if")
        faces = triangles()
        made, = take("<I")
        rails = [take("<6f") for _ in range(made)]
        probes = take("<4f")
        geometry = (folder / "world.bmgeo").read_bytes()
        rail_bytes = (folder / "world.bmrails").read_bytes()

    python_ground, python_points = measured_support(snapshot, origin)
    checks = []
    expected_support = [math.nan if p is None else p[1] for p in python_points]
    checks.append(("support grid", len(support) == len(expected_support) and all(
        (math.isnan(a) and math.isnan(b)) or abs(a - b) < 1e-5 for a, b in zip(support, expected_support))))
    flat = [tuple(v for point in t for v in point) for t in python_ground]
    # Python keeps the raw hit x/z (float jitter well under 1 mm); C uses the exact grid.
    worst = max((abs(x - y) for a, b in zip(ground, flat) for x, y in zip(a, b)), default=0)
    checks.append((f"ground triangles ({len(ground)} vs {len(flat)}, worst difference {worst * 1000:.3f} mm)",
                   len(ground) == len(flat) and worst < 1e-3))
    python_floor = floor_at_origin(python_ground)
    checks.append((f"floor ({floor:.4f} vs {python_floor})", (not found and python_floor is None) or (found and python_floor is not None and abs(floor - python_floor) < 1e-4)))
    python_faces = [tuple(v for point in t for v in point) for box in boxes for t in box_triangles(box, origin)]
    checks.append((f"box faces ({len(faces)})", len(faces) == len(python_faces) and all(close(a, b) for a, b in zip(faces, python_faces))))
    python_rail_bytes, python_rails = object_rails(boxes, origin, feet)
    checks.append((f"rails ({made} vs {python_rails})", made == python_rails))
    # Headers and tags must match exactly; coordinates within 1 mm (C computes in float32).
    rails_ok = len(rail_bytes) == len(python_rail_bytes) and rail_bytes[:12] == python_rail_bytes[:12]
    for n in range(python_rails if rails_ok else 0):
        record = 12 + n * 32
        rails_ok = rails_ok and rail_bytes[record:record + 8] == struct.pack("<HHI", 7, 0, 2) and close(
            struct.unpack_from("<6f", rail_bytes, record + 8), struct.unpack_from("<6f", python_rail_bytes, record + 8), 1e-3)
    checks.append(("BMRL3 layout (area 7)", rails_ok))
    records = [(*t, 0, 7, 0) for t in [*flat, *python_faces]]
    expected_geometry = b"BMGEO2\0\0" + struct.pack("<I", len(records)) + b"".join(struct.pack("<9fHHI", *r) for r in records)
    checks.append(("BMGEO2 layout (area 7)", len(geometry) == len(expected_geometry) and geometry[:12] == expected_geometry[:12] and all(
        close(struct.unpack_from("<9f", geometry, 12 + n * 44), struct.unpack_from("<9f", expected_geometry, 12 + n * 44), 1e-3) and
        geometry[12 + n * 44 + 36:12 + n * 44 + 44] == expected_geometry[12 + n * 44 + 36:12 + n * 44 + 44] for n in range(len(records)))))

    def python_probe(x, z):
        size = 2 * GRID + 1
        cx, cz = math.floor(x + GRID), math.floor(z + GRID)
        if cx < 0 or cz < 0 or cx >= size - 1 or cz >= size - 1:
            return math.nan
        values = [expected_support[j * size + i] for j in (cz, cz + 1) for i in (cx, cx + 1) if not math.isnan(expected_support[j * size + i])]
        return min(values) if values else math.nan
    expected_probes = [python_probe(x, z) for x, z in [(0, 0), (-6.56, -2.44), (3.3, 1.7), (20, 0)]]
    checks.append(("support lookups", all((math.isnan(a) and math.isnan(b)) or abs(a - b) < 1e-5 for a, b in zip(probes, expected_probes))))
    for name, passed in checks:
        print(("PASS " if passed else "FAIL ") + name)
    return all(passed for _, passed in checks)


if __name__ == "__main__":
    main()

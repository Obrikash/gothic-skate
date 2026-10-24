"""Check the bridge's C retarget against tools/retarget_reference.py on real worker poses.

Reads Gothic's human skeleton from the game's KM_Anims.mod (read-only), records
pose streams from the real worker in a temporary copy of the lab (push, steer,
kickflip, heelflip, grab, brake), runs native/retarget_test.c and compares every
node-local matrix with the Python reference. Never touches Gothic.
"""
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import retarget_reference as rr
from build_bridge import write_rig_bind
from build_host_probe import PRIVATE, PROJECT, ROOT

MODES = ["push", "steer", "flipL", "flipR", "grabL", "brake"]
# Gothic centimetres: origin-only previews missed the runtime frame mismatch.
# Include the reported ship area and another world location in every motion.
ORIGINS = [(0.0, 0.0, 0.0), (-40233.0, -1789.0, -154343.0), (100000.0, 5000.0, 200000.0)]


def main():
    nodes = rr.parse_mdh(rr.archive_file(ROOT / "Data/KM_Anims.mod", "HUMANS.MDH"))
    skeleton = rr.GothicSkeleton(nodes)
    rig_rot, rig_pos = rr.rig_bind(PRIVATE / "host-worker-lab/_derpy_script_loader/scripts/BullyMotion/assets/jimmy-bind.json")
    reference = rr.reference_rotations(skeleton, rig_pos)
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    environment = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(PRIVATE / "compiler-cache/global"),
                       ZIG_LOCAL_CACHE_DIR=str(PRIVATE / "compiler-cache/local"))
    with tempfile.TemporaryDirectory(dir=PRIVATE / "build") as folder:
        folder = Path(folder)
        write_rig_bind(folder)
        program = folder / "retarget-test.exe"
        subprocess.run([str(zig), "cc", "-target", "x86-windows-gnu", "-O2", "-Wall", "-Wextra", "-I", str(folder),
                        str(PROJECT / "native/retarget_test.c"), str(PROJECT / "native/retarget_core.c"), "-o", str(program)],
                       env=environment, check=True)
        lab = folder / "lab"
        shutil.copytree(PRIVATE / "host-worker-lab", lab, ignore=shutil.ignore_patterns("*.log"))
        # The lab holds the last in-game capture (areas 1-127); the worker starts on area 0.
        # Use the bridge's placeholder: a 100 m floor at -100 m.
        assets = lab / "_derpy_script_loader/scripts/BullyMotion/assets"
        floor = [(-50, -100, -50), (50, -100, 50), (50, -100, -50), (-50, -100, -50), (-50, -100, 50), (50, -100, 50)]
        records = [floor[0:3], floor[3:6]]
        (assets / "world.bmgeo").write_bytes(b"BMGEO2\0\0" + struct.pack("<I", 2) + b"".join(
            struct.pack("<9fHHI", *[v for point in tri for v in point], 0, 0, 0) for tri in records))
        (assets / "world.bmrails").write_bytes(b"BMRL3\0\0\0" + struct.pack("<I", 0))
        worst_all, ticks_all = 0.0, 0
        for mode in MODES:
            stream = folder / f"{mode}.bin"
            subprocess.run([str(PRIVATE / "build/worker-smoke.exe"), str(lab), "-100", mode, "0"],
                           env=dict(os.environ, SMOKE_POSE=str(stream), SMOKE_FLICK="1.0"), capture_output=True, timeout=180, check=True)
            ticks = [tick for n, tick in enumerate(rr.read_ticks(stream)) if n >= 60 and n % 3 == 0]
            if len(ticks) != 180:
                raise RuntimeError(f"Incomplete {mode} worker stream: {len(ticks)} ticks")
            data = struct.pack("<I", len(nodes))
            for node in nodes:
                data += node["name"].encode("latin1").ljust(64, b"\0")[:64]
                data += struct.pack("<i16f", -1 if node["parent"] is None else node["parent"], *node["matrix"])
            data += struct.pack("<I", len(ticks) * len(ORIGINS))
            expected = []
            for output, pose, board in ticks:
                marvin_rot = rr.marvin_rotation(output[3])
                marvin_pos = [output[0] * 100, output[2] * 100 + 102.1, output[1] * 100]
                g_rot, g_pos = rr.retarget(skeleton, reference, rig_rot, pose, marvin_rot, marvin_pos)
                for origin in ORIGINS:
                    # Same boundary as host_bridge: absolute vob position plus
                    # the fixed ride origin; worker pose stays ride-relative.
                    world_pos = [marvin_pos[k] + origin[k] for k in range(3)]
                    data += struct.pack("<9f3f3f3f", *[v for row in marvin_rot for v in row], *world_pos, *origin, *output[:3])
                    data += struct.pack(f"<{36 * 13}f", *[v for bone in pose for v in bone])
                    expected.append(rr.locals_from_globals(skeleton, g_rot, g_pos))
            (folder / "in.bin").write_bytes(data)
            subprocess.run([str(program), str(folder / "in.bin"), str(folder / "out.bin")], check=True)
            out = (folder / "out.bin").read_bytes()
            if len(out) != len(expected) * len(nodes) * 64 or not all(math.isfinite(v) for (v,) in struct.iter_unpack("<f", out)):
                raise RuntimeError("Invalid or incomplete native matrix output")
            worst_rotation = worst_translation = 0.0
            for t, locals_ in enumerate(expected):
                for i, (r, p) in enumerate(locals_):
                    m = struct.unpack_from("<16f", out, (t * len(nodes) + i) * 64)
                    got_r = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
                    worst_rotation = max(worst_rotation, max(abs(got_r[a][b] - r[a][b]) for a in range(3) for b in range(3)))
                    worst_translation = max(worst_translation, max(abs((m[3], m[7], m[11])[k] - p[k]) for k in range(3)))
            passed = worst_rotation < 2e-3 and worst_translation < 0.05
            print(f"{'PASS' if passed else 'FAIL'} {mode}: {len(ticks)} ticks x {len(ORIGINS)} world origins x {len(nodes)} nodes; worst rotation element {worst_rotation:.2e}, translation {worst_translation:.4f} cm; invalid poses rejected", flush=True)
            worst_all = max(worst_all, 0 if passed else 1)
            ticks_all += len(ticks)
    if worst_all:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

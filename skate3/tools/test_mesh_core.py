"""Exercise progressive-mesh extraction and drive its result in the real worker.

Uses authored fixtures and a temporary copy of the private lab. No game launch,
save edits, live memory writes or changes to the installed worker lab.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from build_host_probe import PRIVATE, PROJECT


def main():
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(PRIVATE / "compiler-cache/global"),
               ZIG_LOCAL_CACHE_DIR=str(PRIVATE / "compiler-cache/local"))
    with tempfile.TemporaryDirectory(dir=PRIVATE / "build") as folder:
        folder = Path(folder)
        for target, sources in [("mesh-test", ["mesh_test.c", "capture_core.c"]),
                                ("warm-test", ["warm_test.c", "worker_client.c", "capture_core.c"])]:
            subprocess.run([str(zig), "cc", "-target", "x86-windows-gnu", "-O2", "-Wall", "-Wextra",
                            *(str(PROJECT / "native" / s) for s in sources), "-lkernel32", "-o", str(folder / f"{target}.exe")],
                           env=env, check=True)
        files = [folder / name for name in ["open.bmgeo", "open.bmrails", "wall.bmgeo", "wall.bmrails"]]
        subprocess.run([str(folder / "mesh-test.exe"), *map(str, files)], check=True)
        lab = folder / "lab"
        shutil.copytree(PRIVATE / "host-worker-lab", lab, ignore=shutil.ignore_patterns("*.log"))
        result = subprocess.run([str(folder / "warm-test.exe"), str(lab), "0", *map(str, files)],
                                capture_output=True, text=True, check=True, timeout=120)
        state = json.loads(result.stdout)
        print(json.dumps(state, indent=2), flush=True)
        assert state["area1"]["workerAlive"] and state["area2"]["workerAlive"]
        assert state["area1"]["maxDistance"] > 5, "Open floor must permit travel"
        assert state["area2"]["maxDistance"] < 1.5, "Extracted mesh walls must stop the board"
        print("PASS real worker: extracted mesh floor supports riding; mesh walls block; one worker reloads both areas", flush=True)


if __name__ == "__main__":
    main()

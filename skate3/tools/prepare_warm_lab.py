"""Prepare the warm worker's lab in .modding/skate3-archolos/host-worker-lab.

The bridge writes each F8 capture into this lab and the x64 worker reads it.
A lab in %LOCALAPPDATA% was reported missing inside the game (Windows error 3)
although it existed; this lab launches in game and its captures reach the
worker. It holds only the pinned worker, its runtime and small fixtures; the
owned Skate data stays in the private worker-lab and is referenced by path.
"""
import hashlib
import json
from pathlib import Path
import shutil

from probe_worker import LAB

MOTION = Path("_derpy_script_loader/scripts/BullyMotion")
TARGET = LAB.parent / "host-worker-lab"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    preparation = json.loads((LAB / "preparation-receipt.json").read_text())
    worker = LAB / MOTION / "SkatePhysicsWorker.exe"
    if digest(worker) != preparation["workerSha256"]:
        raise RuntimeError("Pinned worker checksum mismatch")
    source_assets = (LAB / MOTION / "source-path.txt").read_text().strip()
    if not Path(source_assets).is_dir():
        raise RuntimeError(f"Owned Skate data folder is missing: {source_assets}")
    (TARGET / MOTION / "assets").mkdir(parents=True, exist_ok=True)
    (TARGET / "_derpy_script_loader/logs").mkdir(parents=True, exist_ok=True)
    for name in ["SkatePhysicsWorker.exe", "vcruntime140.dll"]:
        shutil.copy2(LAB / MOTION / name, TARGET / MOTION / name)
    (TARGET / MOTION / "source-path.txt").write_text(source_assets + "\n")
    for name in ["jimmy-bind.json", "world-models.txt"]:
        shutil.copy2(LAB / MOTION / "assets" / name, TARGET / MOTION / "assets" / name)
    receipt = {"lab": str(TARGET), "workerSha256": digest(TARGET / MOTION / "SkatePhysicsWorker.exe"),
               "sourceAssets": source_assets,
               "note": "world.bmgeo/world.bmrails are written by the bridge: area 0 placeholder at worker start, areas 1-127 per F8 capture"}
    (TARGET / "lab-receipt.json").write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()

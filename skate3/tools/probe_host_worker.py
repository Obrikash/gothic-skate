"""Exercise captured real host collision in a separate worker, without moving Marvin."""
import hashlib
import json
from pathlib import Path
import shutil

from probe_worker import LAB, run


def main():
    private = LAB.parent
    capture = json.loads((private / "host-export/capture.json").read_text())
    geometry = private / "host-export/world.bmgeo"
    if hashlib.sha256(geometry.read_bytes()).hexdigest() != capture["sha256"]:
        raise RuntimeError("Captured collision checksum mismatch")
    floor = capture["floorAtOriginMeters"]
    if floor is None:
        raise RuntimeError("No static ground below the captured host position")
    destination = private / "host-worker-lab"
    motion = Path("_derpy_script_loader/scripts/BullyMotion")
    target = destination / motion
    target.mkdir(parents=True, exist_ok=True)
    (target / "assets").mkdir(exist_ok=True)
    for source in (LAB / motion).iterdir():
        if source.is_file():
            shutil.copy2(source, target / source.name)
    for name in ["jimmy-bind.json", "world-models.txt", "world.bmrails"]:
        shutil.copy2(LAB / motion / "assets" / name, target / "assets" / name)
    shutil.copy2(geometry, target / "assets/world.bmgeo")
    shutil.copy2(LAB / "preparation-receipt.json", destination / "preparation-receipt.json")
    (destination / "_derpy_script_loader/logs").mkdir(parents=True, exist_ok=True)
    # Wire Z is core Y. The local host origin is captured in the receipt.
    run(destination, mount=(0, 0, floor + .15, 0), host_geometry=True)


if __name__ == "__main__":
    main()

"""Prepare the pinned upstream worker and authored fixtures in private staging."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = ROOT / ".modding/skate3-archolos"
ARCHIVE_SHA = "a7158ee985704ec9c306a4da1b9d314e718bae14b7168fa0f64bb6d76038eeb3"


def write_new(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise RuntimeError(f"Refusing to overwrite an existing different file: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def skeleton():
    # Authored diagnostic rig, not a game mesh or a recovered Bully skeleton.
    # Upstream's presentation function requires 36 bones and fixed leg slots.
    bones = [{"name": f"LabBone{i}", "parent": -1,
              "matrix": [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]]}
             for i in range(36)]
    layout = {
        0: ("Root", -1, (0, 0, 0)),
        1: ("Root Pelvis", 0, (0, 0, .9)),
        2: ("Root Spine", 1, (0, 0, 1.1)),
        3: ("Root L Thigh", 1, (.1, 0, .9)),
        4: ("Root L Calf", 3, (.1, 0, .5)),
        5: ("Root L Foot", 4, (.1, 0, .1)),
        6: ("Root R Thigh", 1, (-.1, 0, .9)),
        7: ("Root R Calf", 6, (-.1, 0, .5)),
        8: ("Root R Foot", 7, (-.1, 0, .1)),
        9: ("Root Spine1", 2, (0, 0, 1.3)),
        10: ("Root Spine2", 9, (0, 0, 1.5)),
        11: ("Root Neck", 10, (0, 0, 1.6)),
        12: ("Root Head", 11, (0, 0, 1.8)),
        19: ("Root L Clavicle", 10, (.1, 0, 1.5)),
        20: ("Root L UpperArm", 19, (.3, 0, 1.5)),
        21: ("Root L Forearm", 20, (.6, 0, 1.5)),
        22: ("Root L Hand", 21, (.8, 0, 1.5)),
        27: ("Root R Clavicle", 10, (-.1, 0, 1.5)),
        28: ("Root R UpperArm", 27, (-.3, 0, 1.5)),
        29: ("Root R Forearm", 28, (-.6, 0, 1.5)),
        30: ("Root R Hand", 29, (-.8, 0, 1.5)),
    }
    for i, (name, parent, position) in layout.items():
        bones[i].update(name=name, parent=parent)
        bones[i]["matrix"][3] = list(position)
    return bones


def prepare(game):
    archive = PRIVATE / "downloads/BullySkate-Windows-v0.1.2.zip"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        raise RuntimeError("Upstream release archive checksum mismatch")
    lab = PRIVATE / "worker-lab"
    game = game.resolve(strict=True)
    xex = game / "game/default.xex"
    if not xex.is_file() or xex.read_bytes()[:4] != b"XEX2":
        raise RuntimeError("Expected the user's extracted Skate 3 default.xex")
    with zipfile.ZipFile(archive) as outer:
        launcher = next(n for n in outer.namelist() if n.endswith("/BullySkateLauncher.exe") or n == "BullySkateLauncher.exe")
        import io
        # The managed launcher's resource is an ordinary embedded ZIP. Read
        # its data directly; do not run the host-specific Bully installer.
        with zipfile.ZipFile(io.BytesIO(outer.read(launcher))) as payload:
            wanted = {
                "runtime/SkatePhysicsWorker.exe": "_derpy_script_loader/scripts/BullyMotion/SkatePhysicsWorker.exe",
                "runtime/SkateAssetExtractor.exe": "tools/SkateAssetExtractor.exe",
                "runtime/expected-skate-assets.sha256": "expected-skate-assets.sha256",
            }
            for source, destination in wanted.items():
                write_new(lab / destination, payload.read(source))
            for name in payload.namelist():
                if name.startswith("runtime/worker-dependencies/") and not name.endswith("/"):
                    write_new(lab / "_derpy_script_loader/scripts/BullyMotion" / Path(name).name, payload.read(name))
    asset_root = lab / "source-assets"
    receipt = asset_root / "extraction-receipt.json"
    if not asset_root.exists():
        subprocess.run([str(lab / "tools/SkateAssetExtractor.exe"), "--xex", str(xex),
                        "--out", str(asset_root), "--manifest", str(lab / "expected-skate-assets.sha256")],
                       cwd=lab, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if not receipt.is_file():
        raise RuntimeError("No successful owned-game extraction receipt")
    extracted = json.loads(receipt.read_text(encoding="utf-8"))
    for relative, expected in extracted["files"].items():
        path = (asset_root / relative).resolve()
        if not path.is_relative_to(asset_root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Prepared asset failed verification: {relative}")
    assets = lab / "_derpy_script_loader/scripts/BullyMotion/assets"
    # +Y-up core-space ground, authored for an isolated simulation test only.
    vertices = [(-1000., 0., -1000.), (-1000., 0., 1000.), (1000., 0., 1000.), (1000., 0., -1000.)]
    triangles = [(vertices[0], vertices[1], vertices[2]), (vertices[0], vertices[2], vertices[3])]
    world = b"BMGEO2\0\0" + struct.pack("<I", len(triangles))
    for triangle in triangles:
        world += struct.pack("<9fHHI", *(v for point in triangle for v in point), 0, 0, 0)
    write_new(assets / "world.bmgeo", world)
    write_new(assets / "world.bmrails", b"BMRL3\0\0\0" + struct.pack("<I", 0))
    write_new(assets / "world-models.txt", b"BMMODELS1\n0 255\n")
    write_new(assets / "jimmy-bind.json", json.dumps(skeleton(), indent=2).encode())
    write_new(assets.parent / "source-path.txt", str(asset_root).encode())
    (lab / "_derpy_script_loader/logs").mkdir(parents=True, exist_ok=True)
    report = {"upstream": "Faiqie/BullySkate", "tag": "v0.1.2", "archiveSha256": ARCHIVE_SHA,
              "workerSha256": hashlib.sha256((assets.parent / "SkatePhysicsWorker.exe").read_bytes()).hexdigest(),
              "ownedAssetsVerified": len(extracted["files"]),
              "fixtures": "authored flat ground and diagnostic skeleton; not Archolos collision or a game rig"}
    write_new(lab / "preparation-receipt.json", json.dumps(report, indent=2).encode())
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skate-root", type=Path, required=True)
    prepare(parser.parse_args().skate_root)

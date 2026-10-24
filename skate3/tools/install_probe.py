"""Install or park only the owned read-only probe; preserve the working mods."""
import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = ROOT / ".modding/skate3-archolos"
TARGET = ROOT / "system/autorun/ArcholosSkateProbe.dll"
RECEIPT = PRIVATE / "probe-install.json"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def closed():
    result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Gothic2.exe", "/FO", "CSV", "/NH"],
                            check=True, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if b'"gothic2.exe"' in result.stdout.lower():
        raise RuntimeError("Close Archolos before installing or removing the probe")


def main(action, terrain=False):
    closed()
    if action == "remove":
        receipt = json.loads(RECEIPT.read_text())
        if TARGET.exists():
            if digest(TARGET) != receipt["sha256"]:
                raise RuntimeError("Probe DLL changed; refusing to move an unknown file")
            destination = PRIVATE / "disabled" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-ArcholosSkateProbe.dll")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(TARGET), str(destination))
        receipt["active"] = False
        RECEIPT.write_text(json.dumps(receipt, indent=2))
        print("Read-only probe removed; existing aiming files were not changed")
        return
    source = PRIVATE / "build/ArcholosSkateProbe.dll"
    build = json.loads((PRIVATE / "build/probe-build.json").read_text())
    if build.get("mechanism") != "union-callbacks-v2":
        raise RuntimeError("Refusing the retired render-detour probe; rebuild the Union callback version")
    if digest(source) != build["sha256"]:
        raise RuntimeError("Built probe checksum mismatch")
    config = PRIVATE / "host-probe.ini"
    if config.exists():
        existing = configparser.ConfigParser()
        existing.read(config)
        if (existing.sections() != ["Probe"] or existing.defaults() or
                any(key not in {"enabled", "captureterrain"} or value not in {"0", "1"}
                    for key, value in existing["Probe"].items())):
            raise RuntimeError("Unknown probe configuration needs review")
    if TARGET.exists():
        if not RECEIPT.exists() or digest(TARGET) != build["sha256"]:
            raise RuntimeError("An existing probe must be reviewed before replacement")
        print("The same probe is already installed")
        return
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup = PRIVATE / "backups" / ("before-host-probe-" + stamp)
    backup.mkdir(parents=True)
    if config.exists():
        shutil.copy2(config, backup / "previous-host-probe.ini")
    for relative in ["system", "Saves", "Saves_TheChroniclesOfMyrtana"]:
        source_dir = ROOT / relative
        if source_dir.is_dir():
            shutil.copytree(source_dir, backup / relative)
    manifest = [{"path": str(path.relative_to(backup)), "sha256": digest(path)}
                for path in backup.rglob("*") if path.is_file()]
    (backup / "manifest.json").write_text(json.dumps(manifest, indent=2))
    config.write_text(f"[Probe]\nEnabled=1\nCaptureTerrain={int(terrain)}\n", encoding="ascii")
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, TARGET)
    if digest(TARGET) != build["sha256"]:
        raise RuntimeError("Installed probe checksum mismatch")
    receipt = {"active": True, "path": str(TARGET), "sha256": digest(TARGET),
               "backup": str(backup), "backupFiles": len(manifest),
               "mechanism": build["mechanism"], "terrainEnabled": terrain,
               "kind": "read-only Union callback probe; no code patches or movement writes"}
    RECEIPT.write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "remove"])
    parser.add_argument("--terrain", action="store_true", help="Also call the host static-ground raycast (default: position only)")
    arguments = parser.parse_args()
    main(arguments.action, arguments.terrain)

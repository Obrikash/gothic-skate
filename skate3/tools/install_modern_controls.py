"""Back up and install/park only our modern-controls DLL and mouse smoothing setting."""
import argparse
from datetime import datetime, timezone
import json
import re
import shutil
from install_probe import ROOT, PRIVATE, closed, digest

TARGET = ROOT / "system/autorun/ArcholosModernControls.dll"
RECEIPT = PRIVATE / "modern-install.json"
CONFIG = PRIVATE / "modern-controls.ini"
INI = ROOT / "system/Gothic.ini"


def main(action):
    if action == "status":
        receipt = json.loads(RECEIPT.read_text()) if RECEIPT.exists() else {}
        print(json.dumps({"installed": TARGET.exists(), "knownHash": bool(TARGET.exists() and receipt.get("sha256") == digest(TARGET)), "receipt": receipt}, indent=2))
        return
    closed()
    if action == "remove":
        receipt = json.loads(RECEIPT.read_text())
        if TARGET.exists():
            if digest(TARGET) != receipt["sha256"]:
                raise RuntimeError("Modern-controls DLL changed; refusing to park it")
            destination = PRIVATE / "disabled" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-ArcholosModernControls.dll")
            destination.parent.mkdir(exist_ok=True)
            shutil.move(str(TARGET), str(destination))
        # Restore only our key, preserving settings changed by the player since installation.
        data = INI.read_bytes()
        pattern = rb"(?im)^(zSmoothMouse\s*=\s*)0([ \t]*\r?$)"
        data, count = re.subn(pattern, lambda m: m[1] + receipt["previousSmoothMouse"].encode("ascii") + m[2], data)
        if count == 1:
            INI.write_bytes(data)
        receipt["active"] = False
        RECEIPT.write_text(json.dumps(receipt, indent=2))
        print("Modern controls parked; original smoothing restored if still set to our value.")
        return
    if TARGET.exists():
        raise RuntimeError("Park the existing modern-controls DLL before installing a replacement")
    build = json.loads((PRIVATE / "build/modern-build.json").read_text())
    source = PRIVATE / "build/ArcholosModernControls.dll"
    if build["mechanism"] != "modern-exploration-v1" or digest(source) != build["sha256"]:
        raise RuntimeError("Modern-controls build receipt mismatch")
    if digest(ROOT / "system/Gothic2.exe") != build["hostSha256"]:
        raise RuntimeError("Host executable changed")
    data = INI.read_bytes()
    found = re.findall(rb"(?im)^zSmoothMouse\s*=\s*(\d+)[ \t]*\r?$", data)
    if len(found) != 1:
        raise RuntimeError("Expected exactly one mouse smoothing setting")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup = PRIVATE / "backups" / ("before-modern-controls-" + stamp)
    backup.mkdir(parents=True)
    for relative in ["system", "Saves", "Saves_TheChroniclesOfMyrtana"]:
        source_dir = ROOT / relative
        if source_dir.is_dir():
            shutil.copytree(source_dir, backup / relative)
    if CONFIG.exists():
        shutil.copy2(CONFIG, backup / "previous-modern-controls.ini")
    manifest = [{"path": str(p.relative_to(backup)), "sha256": digest(p)} for p in backup.rglob("*") if p.is_file()]
    (backup / "manifest.json").write_text(json.dumps(manifest, indent=2))
    if not CONFIG.exists():
        CONFIG.write_text("[Controls]\nEnabled=1\nMouseSensitivity=0.25\nInvertY=0\nCameraDistance=280\nCameraHeight=135\nShoulderOffset=25\nTurnRate=720\n", encoding="ascii")
    changed = re.sub(rb"(?im)^(zSmoothMouse\s*=\s*)\d+([ \t]*\r?$)", lambda m: m[1] + b"0" + m[2], data)
    try:
        shutil.copy2(source, TARGET)
        if digest(TARGET) != build["sha256"]:
            raise RuntimeError("Installed modern-controls checksum mismatch")
        INI.write_bytes(changed)
        receipt = {"active": True, "sha256": digest(TARGET), "backup": str(backup), "backupFiles": len(manifest), "previousSmoothMouse": found[0].decode(), "gameplayVerified": False}
        RECEIPT.write_text(json.dumps(receipt, indent=2))
    except Exception:
        INI.write_bytes(data)
        if TARGET.exists() and digest(TARGET) == build["sha256"]:
            destination = PRIVATE / "disabled" / (stamp + "-failed-modern-controls.dll")
            destination.parent.mkdir(exist_ok=True)
            shutil.move(str(TARGET), str(destination))
        raise
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "remove", "status"])
    main(parser.parse_args().action)

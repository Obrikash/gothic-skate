"""Install/park only our experimental DLL and prepare its warm worker lab."""
import argparse
import configparser
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil

from install_probe import ROOT, PRIVATE, closed, digest
from prepare_warm_lab import main as prepare_warm_lab

TARGET = ROOT / "system/autorun/ArcholosSkateBridge.dll"
RECEIPT = PRIVATE / "bridge-install.json"


def main(action):
    closed()
    if action == "remove":
        receipt = json.loads(RECEIPT.read_text())
        if TARGET.exists():
            if digest(TARGET) != receipt["sha256"]:
                raise RuntimeError("Refusing to move an unknown bridge DLL")
            destination = PRIVATE / "disabled" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-ArcholosSkateBridge.dll")
            destination.parent.mkdir(exist_ok=True)
            shutil.move(str(TARGET), str(destination))
        receipt["active"] = False
        RECEIPT.write_text(json.dumps(receipt, indent=2))
        print("Movement bridge parked; working aiming dependencies were not changed")
        return
    source = PRIVATE / "build/ArcholosSkateBridge.dll"
    build = json.loads((PRIVATE / "build/ArcholosSkateBridge.dll.json").read_text())
    if build["mechanism"] != "union-callbacks-movement-v1" or digest(source) != build["sha256"]:
        raise RuntimeError("Bridge build receipt mismatch")
    if TARGET.exists():
        raise RuntimeError("Park the existing bridge before installing a replacement")
    backup = PRIVATE / "backups" / ("before-movement-bridge-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    backup.mkdir(parents=True)
    for relative in ["system", "Saves", "Saves_TheChroniclesOfMyrtana"]:
        directory = ROOT / relative
        if directory.is_dir():
            shutil.copytree(directory, backup / relative)
    config = PRIVATE / "host-bridge.ini"
    if config.exists():
        shutil.copy2(config, backup / "previous-host-bridge.ini")
    manifest = [{"path": str(path.relative_to(backup)), "sha256": digest(path)} for path in backup.rglob("*") if path.is_file()]
    (backup / "manifest.json").write_text(json.dumps(manifest, indent=2))
    # The bridge captures on F8 itself; external arming keys are obsolete.
    # Enabled used to mean "armed"; it now switches the feature, on after
    # installing (set Enabled=0 afterwards to disable). Keep [Rider]/[Board].
    settings = configparser.ConfigParser()
    settings.optionxform = str
    if config.exists():
        settings.read(config)
    settings.remove_section("World")
    if not settings.has_section("Bridge"):
        settings.add_section("Bridge")
    settings["Bridge"].pop("SampleRequest", None)
    settings["Bridge"]["Enabled"] = "1"
    with config.open("w", encoding="ascii") as file:
        settings.write(file)
    prepare_warm_lab()
    TARGET.parent.mkdir(exist_ok=True)
    shutil.copy2(source, TARGET)
    if digest(TARGET) != build["sha256"]:
        raise RuntimeError("Installed bridge checksum mismatch")
    receipt = {"active": True, "path": str(TARGET), "sha256": digest(TARGET), "backup": str(backup),
               "backupFiles": len(manifest), "movementArmed": False, "gameplayVerified": False}
    RECEIPT.write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "remove"])
    main(parser.parse_args().action)

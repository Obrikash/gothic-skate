"""Validate, install, or restore the locally staged Archolos free-aim setup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from datetime import datetime, timezone

REPOSITORY = Path(__file__).resolve().parent.parent
GAME = REPOSITORY.parent
PROJECT = GAME / ".modding" / "archolos-free-aim"
BACKUP = PROJECT / "backups" / "before-union-20261024"
STATE = PROJECT / "install-state.json"
EXE_SHA = "0655B4811E008A353CE372FDE463EF00B6101F3E89BE850E2F14980140447C7F"
ALLOWED_TARGETS = {
    "system/msvcp100.dll", "system/msvcr100.dll", "system/Shw32.dll",
    "system/stdhost.exe", "system/Union.patch", "system/Vdfs32g.dll",
    "Data/Union.vdf", "Data/Plugins/UnionFreeAim.vdf", "system/Gothic.ini",
    "Data/SystemPack.vdf", "system/SystemPack.ini", "system/Paths.d",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def inside(base, relative):
    path = (base / relative).resolve()
    if not path.is_relative_to(base.resolve()):
        raise RuntimeError(f"Path outside expected folder: {relative}")
    return path


def game_path(relative):
    if relative not in ALLOWED_TARGETS:
        raise RuntimeError(f"Unexpected installation target: {relative}")
    return inside(GAME, relative)


def write_state(value):
    STATE.write_text(json.dumps(value, indent=2), encoding="utf-8")


def ensure_closed():
    result = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq Gothic2.exe", "/FO", "CSV", "/NH"],
        capture_output=True, check=True,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if b'"gothic2.exe"' in result.stdout.lower():
        raise RuntimeError("Close Gothic II / Archolos before installing or restoring.")


def validate():
    manifest = json.loads((REPOSITORY / "dependencies.json").read_text(encoding="utf-8-sig"))
    if sha(GAME / "system/Gothic2.exe") != EXE_SHA:
        raise RuntimeError("Game executable changed; inspect compatibility again.")
    originals = json.loads((BACKUP / "backup-manifest.json").read_text(encoding="utf-8-sig"))
    backed_up = {x["path"].replace("\\", "/").lower(): x for x in originals["files"]}
    seen = set()
    for entry in manifest["files"]:
        target = game_path(entry["target"])
        key = str(target).lower()
        if key in seen:
            raise RuntimeError("Duplicate install target")
        seen.add(key)
        source = inside(PROJECT / "staging", entry["source"])
        if sha(source) != entry["sha256"]:
            raise RuntimeError(f"Staged file changed: {source}")
        if target.exists():
            old = backed_up.get(entry["target"].lower())
            if not old or sha(target) != old["sha256"]:
                raise RuntimeError(f"Existing file changed since backup: {target}")
            if sha(inside(BACKUP, old["path"])) != old["sha256"]:
                raise RuntimeError(f"Backup is damaged: {target}")
    for relative in ["system/Gothic.ini", "system/SystemPack.ini", "system/Paths.d", "Data/SystemPack.vdf"]:
        old = backed_up[relative.lower()]
        if sha(game_path(relative)) != old["sha256"] or sha(inside(BACKUP, old["path"])) != old["sha256"]:
            raise RuntimeError(f"Original or backup changed: {relative}")
    return manifest


def configure_ini(path):
    # Latin-1 round-trips every byte of the existing legacy INI.
    text = path.read_bytes().decode("latin-1")
    section = re.search(r"(?im)^\[UNIONFREEAIM\][^\r\n]*\r?\n", text)
    values = {"enabled": "1", "cameraZoomEnabled": "1", "elevationEnabled": "0"}
    if section:
        end = re.search(r"(?m)^\[", text[section.end():])
        end = section.end() + end.start() if end else len(text)
        body = text[section.end():end]
        for key, value in values.items():
            pattern = rf"(?im)^{re.escape(key)}\s*=[^\r\n]*"
            if re.search(pattern, body):
                body = re.sub(pattern, f"{key}={value}", body)
            else:
                body = body.rstrip("\r\n") + f"\r\n{key}={value}\r\n"
        text = text[:section.end()] + body + text[end:]
    else:
        text = text.rstrip("\r\n") + "\r\n\r\n[UNIONFREEAIM]\r\n"
        text += "".join(f"{key}={value}\r\n" for key, value in values.items())
    path.write_bytes(text.encode("latin-1"))


def status():
    manifest = json.loads((REPOSITORY / "dependencies.json").read_text(encoding="utf-8-sig"))
    if not STATE.exists():
        print("No installation has been recorded. Run validate before installing.")
        return
    state = json.loads(STATE.read_text(encoding="utf-8"))
    print(f"Installation state: {state['status']}")
    if state["status"] != "installed":
        return
    for entry in manifest["files"]:
        target = game_path(entry["target"])
        if not target.is_file() or sha(target) != entry["sha256"]:
            raise RuntimeError(f"Installed file missing or changed: {target}")
    if sha(GAME / "system/Gothic2.exe") != EXE_SHA:
        raise RuntimeError("Game executable changed since preparation.")
    if (GAME / "Data/SystemPack.vdf").exists():
        raise RuntimeError("Old SystemPack volume is active alongside Union.")
    ini = (GAME / "system/Gothic.ini").read_bytes().decode("latin-1")
    section = re.search(r"(?ims)^\[UNIONFREEAIM\]\s*\n(.*?)(?=^\[|\Z)", ini)
    if not section or not re.search(r"(?im)^enabled\s*=\s*1\s*$", section[1]):
        raise RuntimeError("Free aiming is not enabled in Gothic.ini.")
    print(f"Verified {len(manifest['files'])} installed files; free aiming enabled; game executable unchanged.")


def restore():
    ensure_closed()
    state = json.loads(STATE.read_text(encoding="utf-8"))
    if state["status"] == "restored":
        print("The original setup has already been restored.")
        return
    # Union also writes these existing configuration files during its first start.
    # Include them when restoring installations recorded by the initial helper.
    known = {entry["target"] for entry in state["targets"]}
    backup_manifest = json.loads((BACKUP / "backup-manifest.json").read_text(encoding="utf-8-sig"))
    backup_hashes = {entry["path"].replace("\\", "/").lower(): entry["sha256"] for entry in backup_manifest["files"]}
    for relative in ["system/SystemPack.ini", "system/Paths.d"]:
        if relative not in known:
            state["targets"].append({"target": relative, "original": backup_hashes[relative.lower()]})
    # Validate every restore source before changing any installed file.
    for entry in state["targets"]:
        game_path(entry["target"])
        if entry["original"] and sha(inside(BACKUP, entry["target"])) != entry["original"]:
            raise RuntimeError(f"Backup checksum mismatch: {entry['target']}")
    recovery = PROJECT / "disabled" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    recovery.mkdir(parents=True)
    for entry in state["targets"]:
        target = game_path(entry["target"])
        if target.exists():
            parked = inside(recovery, entry["target"])
            parked.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(target), str(parked))
        if entry["original"]:
            source = inside(BACKUP, entry["target"])
            if sha(source) != entry["original"]:
                raise RuntimeError(f"Backup checksum mismatch: {source}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    state["status"] = "restored"
    state["restoredUtc"] = datetime.now(timezone.utc).isoformat()
    write_state(state)
    print(f"Original runtime and INI restored. Removed mod files are preserved in {recovery}")


def install():
    ensure_closed()
    if STATE.exists() and json.loads(STATE.read_text(encoding="utf-8"))["status"] != "restored":
        raise RuntimeError("Installation state already exists; inspect it before installing again.")
    manifest = validate()
    if STATE.exists():
        history = PROJECT / "disabled" / ("install-state-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".json")
        history.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(STATE), str(history))
    targets = [x["target"] for x in manifest["files"]] + ["system/Gothic.ini", "system/SystemPack.ini", "system/Paths.d", "Data/SystemPack.vdf"]
    state = {
        "status": "installing", "installedUtc": datetime.now(timezone.utc).isoformat(),
        "targets": [{"target": x, "original": sha(game_path(x)) if game_path(x).exists() else None} for x in targets],
    }
    write_state(state)
    try:
        disabled = PROJECT / "disabled" / ("SystemPack-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".vdf")
        disabled.parent.mkdir(parents=True, exist_ok=True)
        if disabled.exists():
            raise RuntimeError("Disabled SystemPack destination already exists.")
        shutil.move(str(GAME / "Data/SystemPack.vdf"), str(disabled))
        for entry in manifest["files"]:
            target = game_path(entry["target"])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(inside(PROJECT / "staging", entry["source"]), target)
            if sha(target) != entry["sha256"]:
                raise RuntimeError(f"Installed checksum mismatch: {target}")
        configure_ini(GAME / "system/Gothic.ini")
        if sha(GAME / "system/Gothic2.exe") != EXE_SHA:
            raise RuntimeError("Executable changed unexpectedly.")
        state["status"] = "installed"
        write_state(state)
    except BaseException:
        restore()
        raise
    print("Installed Union 1.0m and Union Free Aim. In-game verification is still required.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["status", "validate", "install", "restore"], default="status", nargs="?")
    args = parser.parse_args()
    if args.action == "status":
        status()
    elif args.action == "validate":
        data = validate()
        print(f"Ready: {len(data['files'])} staged files match their checksums and the original backup is intact.")
        for entry in data["files"]:
            print(entry["target"])
    elif args.action == "install":
        install()
    else:
        restore()

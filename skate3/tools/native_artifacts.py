"""Read our own game-side files, including Windows legacy UAC VirtualStore."""
import json
import os
from pathlib import Path

from export_host_geometry import ROOT, PRIVATE


def candidates(name):
    paths = [PRIVATE / name]
    local = os.environ.get("LOCALAPPDATA")
    if local:
        virtual_root = Path(local) / "VirtualStore" / ROOT.relative_to(ROOT.anchor)
        paths.append(virtual_root / ".modding/skate3-archolos" / name)
    return paths


def latest(name, pid, request=None):
    matches = []
    for path in candidates(name):
        try:
            data = json.loads(path.read_text())
            if data.get("pid") == pid and (request is None or data.get("requestId") == request):
                matches.append((path.stat().st_mtime, path, data))
        except (OSError, json.JSONDecodeError):
            continue
    if not matches:
        return None
    _, path, data = max(matches, key=lambda item: item[0])
    return path, data

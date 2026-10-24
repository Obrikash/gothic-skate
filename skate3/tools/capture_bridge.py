"""Request one ground sample from the consolidated, idle bridge."""
import argparse
import configparser
import json
import secrets
import time

from export_host_geometry import PRIVATE
from native_artifacts import latest


def main(pid):
    found = latest("bridge-state.json", pid)
    if not found:
        raise RuntimeError("No bridge state for this process")
    _, state = found
    if state["pid"] != pid or state["phase"] or state["movementAuthority"]:
        raise RuntimeError("This exact bridge must be idle before requesting a sample")
    path = PRIVATE / "host-bridge.ini"
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(path)
    if not config.has_section("Bridge"):
        config.add_section("Bridge")
    request = secrets.randbelow(2_000_000_000) + 1
    config["Bridge"]["Enabled"] = "0"
    config["Bridge"]["SampleRequest"] = str(request)
    temporary = path.with_suffix(".ini.new")
    with temporary.open("w", encoding="ascii") as file:
        config.write(file)
    temporary.replace(path)
    deadline = time.monotonic() + 60
    snapshot = PRIVATE / "host-snapshot.json"
    while time.monotonic() < deadline:
        found = latest("host-snapshot.json", pid, request)
        if found:
            source, result = found
            if source != snapshot:
                copied = snapshot.with_suffix(".copied.json")
                copied.write_bytes(source.read_bytes())
                copied.replace(snapshot)
            print(json.dumps({key: value for key, value in result.items() if key != "grid"}, indent=2))
            return
        time.sleep(.1)
    raise RuntimeError("No ground response: keep the loaded game active, with Marvin standing and weapons sheathed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    main(parser.parse_args().pid)

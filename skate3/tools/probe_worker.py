"""Exercise the real upstream skating worker in the isolated authored lab."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time

LAB = Path(__file__).resolve().parents[3] / ".modding/skate3-archolos/worker-lab"


class Input(c.LittleEndianStructure):
    _fields_ = [("dt", c.c_float), ("buttons", c.c_uint32), ("axes", c.c_float * 6)]


class Shared(c.LittleEndianStructure):
    _fields_ = [
        ("ready", c.c_uint32), ("command", c.c_uint32), ("success", c.c_uint32),
        ("mount", c.c_float * 4), ("dt", c.c_float), ("buttons", c.c_uint32), ("axes", c.c_float * 6),
        ("actor_count", c.c_uint32), ("actors", (c.c_float * 9) * 24), ("aspect", c.c_float),
        ("output", c.c_float * 14), ("pose", (c.c_float * 13) * 36), ("board", (c.c_float * 13) * 2),
        ("camera", c.c_float * 7), ("root", c.c_float * 3), ("has_camera", c.c_uint32),
        ("error", c.c_char * 2048), ("preferences", c.c_float * 10),
        ("marker_flags", c.c_uint32), ("marker_sets", c.c_uint32), ("marker_returns", c.c_uint32), ("marker_progress", c.c_float),
        ("input_count", c.c_uint32), ("inputs", Input * 8),
        ("mount_area", c.c_uint32), ("actor_revision", c.c_uint32), ("vehicle_count", c.c_uint32), ("vehicle_revision", c.c_uint32),
        ("vehicles", (c.c_float * 16) * 8), ("interaction", c.c_uint32 * 4), ("audio", c.c_float * 64),
        ("world_phase", c.c_uint32), ("difficulty", c.c_uint32), ("camera_type", c.c_uint32),
    ]


assert c.sizeof(Shared) == 6184, "Pinned worker ABI mismatch"


def run(lab=LAB, mount=(0, 0, 0, 0), host_geometry=False):
    receipt = json.loads((lab / "preparation-receipt.json").read_text())
    worker = lab / "_derpy_script_loader/scripts/BullyMotion/SkatePhysicsWorker.exe"
    if hashlib.sha256(worker.read_bytes()).hexdigest() != receipt["workerSha256"]:
        raise RuntimeError("Worker checksum mismatch")
    kernel = c.WinDLL("kernel32", use_last_error=True)
    signatures = {
        "CreateFileMappingW": ([w.HANDLE, c.c_void_p, w.DWORD, w.DWORD, w.DWORD, w.LPCWSTR], w.HANDLE),
        "MapViewOfFile": ([w.HANDLE, w.DWORD, w.DWORD, w.DWORD, c.c_size_t], c.c_void_p),
        "CreateEventW": ([c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR], w.HANDLE),
        "SetEvent": ([w.HANDLE], w.BOOL),
        "WaitForSingleObject": ([w.HANDLE, w.DWORD], w.DWORD),
        "UnmapViewOfFile": ([c.c_void_p], w.BOOL),
        "CloseHandle": ([w.HANDLE], w.BOOL),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(kernel, name)
        function.argtypes, function.restype = arguments, result
    pid = os.getpid()
    prefix = f"Local\\BullySkate-{pid}"
    handles, memory, process = [], None, None

    def handle(value):
        if not value:
            raise c.WinError(c.get_last_error())
        handles.append(value)
        return value

    try:
        mapping = handle(kernel.CreateFileMappingW(w.HANDLE(-1), None, 4, 0, c.sizeof(Shared), prefix + "-map"))
        memory = kernel.MapViewOfFile(mapping, 0xF001F, 0, 0, c.sizeof(Shared))
        if not memory:
            raise c.WinError(c.get_last_error())
        c.memset(memory, 0, c.sizeof(Shared))
        shared = Shared.from_address(memory)
        shared.ready, shared.aspect, shared.camera_type = 1, 16 / 9, 1
        shared.preferences[:] = [1, .7, .7, 0, 0, 0, 1, 2, 3, 67]
        command = handle(kernel.CreateEventW(None, False, False, prefix + "-command"))
        response = handle(kernel.CreateEventW(None, False, False, prefix + "-response"))
        with (lab / "probe-worker-stdio.log").open("wb") as log:
            process = subprocess.Popen([str(worker), "--parent", str(pid)], cwd=lab,
                                       stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            deadline = time.monotonic() + 45
            while shared.ready == 1 and time.monotonic() < deadline and process.poll() is None:
                time.sleep(.02)
            if shared.ready != 2:
                raise RuntimeError(f"Worker did not initialize: ready={shared.ready}, exit={process.poll()}, error={shared.error.decode(errors='replace')}")

            def request(code):
                shared.command, shared.success = code, 0
                if not kernel.SetEvent(command) or kernel.WaitForSingleObject(response, 5000) != 0:
                    raise RuntimeError(f"Worker response timed out: {process.poll()}")
                if not shared.success:
                    raise RuntimeError(shared.error.decode(errors="replace"))
                values = list(shared.output) + [x for bone in shared.pose for x in bone]
                if not all(math.isfinite(x) for x in values):
                    raise RuntimeError("Worker returned non-finite state")
                return list(shared.output)

            shared.mount[:] = mount
            mounted = request(1)
            trace = []
            for tick in range(600):
                # Xbox A is push; a right-stick down/up edge requests an ollie.
                sample = shared.inputs[0]
                sample.dt, sample.buttons = 1 / 60, 0x1000 if 90 <= tick < (150 if host_geometry else 330) else 0
                sample.axes[:] = [0, 0, 0, -1 if 360 <= tick < 390 else (1 if 390 <= tick < 396 else 0), 0, 0]
                shared.input_count = 1
                state = request(2)
                if tick % 10 == 0:
                    trace.append({"tick": tick, "output": state, "root": list(shared.root)})
            request(3)
            max_distance = max(math.hypot(x["output"][0] - mounted[0], x["output"][1] - mounted[1]) for x in trace)
            max_height = max(x["output"][2] for x in trace)
            grounded_samples = sum(x["output"][10] > 0 for x in trace)
            resting = [x["output"][2] for x in trace if 60 <= x["tick"] < 90]
            rest_height = sum(resting) / len(resting)
            ollie_gain = max(x["output"][2] for x in trace if 360 <= x["tick"] < 480) - rest_height
            if max_distance < .1 or grounded_samples < 1:
                raise RuntimeError(f"Physics probe did not establish motion and contact: distance={max_distance}, contacts={grounded_samples}")
            if ollie_gain < .1:
                raise RuntimeError(f"The stick input did not establish an ollie: gain={ollie_gain}")
            report = {"test": "real Rust worker on captured Archolos static collision" if host_geometry else "real Rust worker on authored flat collision", "archolosGameplayVerified": False,
                      "sharedBytes": c.sizeof(Shared), "ticksRequested": 600,
                      "maxPlanarDistanceMeters": max_distance, "maxBoardHeightMeters": max_height,
                      "ollieGainMeters": ollie_gain, "groundedSamples": grounded_samples, "trace": trace}
            (lab / "probe-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            print(json.dumps({k: v for k, v in report.items() if k != "trace"}, indent=2))
    finally:
        if process is not None and process.poll() is None:
            process.terminate()  # This exact child only; no game process is touched.
            process.wait(timeout=5)
        if memory:
            kernel.UnmapViewOfFile(memory)
        for item in reversed(handles):
            kernel.CloseHandle(item)


if __name__ == "__main__":
    run()

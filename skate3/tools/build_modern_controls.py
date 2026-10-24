"""Build the separate modern-controls DLL against the inspected Gothic 2 build."""
import hashlib
import json
import os
import re
import subprocess
from build_host_probe import PRIVATE, PROJECT, build
from disasm_host import Image


def main():
    build()  # shared host layout, all executable/ABI checks
    image = Image()
    api = next((PRIVATE / "gothic-api").iterdir()) / "ZenGin/Gothic_II_Addon"
    def method(file, signature):
        matches = re.findall(re.escape(signature) + r"\s+zCall\(\s*(0x[\da-fA-F]+)\s*\)", (api / "API" / file).read_text(encoding="utf-8-sig"))
        if len(matches) != 1:
            raise RuntimeError((signature, matches))
        return int(matches[0], 16)
    def offset(file, name):
        lines = [line for line in (api / "API" / file).read_text(encoding="utf-8-sig").splitlines() if re.search(r"\b" + name + r"\s*;", line)]
        if len(lines) != 1:
            raise RuntimeError((name, lines))
        return int(re.search(r"offset ([\dA-Fa-f]+)h", lines[0])[1], 16)
    def static(file, pattern):
        matches = re.findall(pattern + r"[^\n]*?(0x[\da-fA-F]+)", (api / file).read_text(encoding="utf-8-sig"))
        if len(matches) != 1:
            raise RuntimeError((pattern, matches))
        return int(matches[0], 16)
    layout = {
        "MC_WALK": method("oAiHuman.h", "void _WalkCycle()"),
        "MC_TURN": method("oAiHuman.h", "void PC_Turnings( int )"),
        "MC_GET_STATE": method("zInput_Win32.h", "virtual float GetState( unsigned short )"),
        "MC_GET_TOGGLED": method("zInput_Win32.h", "virtual int GetToggled( unsigned short )"),
        "MC_CAMERA_DO_AI": method("zAiCamera.h", "virtual void DoAI( zCVob*, int& )"),
        "MC_SET_HEADING": method("zVob.h", "void SetHeadingAtWorld( zVEC3 const& )"),
        "MC_SET_POSITION": method("zVob.h", "void SetPositionWorld( zVEC3 const& )"),
        "MC_NPC_UNCONSCIOUS": method("oNpc.h", "int IsUnconscious()"),
        "MC_NPC_HUMAN_AI": offset("oNpc.h", "human_ai"),
        "MC_NPC_INTERACT_MOB": offset("oNpc.h", "interactMob"),
        "MC_CAMERA_CURRENT": static("StaticsProperties.h", r"zCAICamera::current\s*="),
        "MC_INPUT": static("GlobalVariables.h", r"\bzinput\s*="),
        "MC_TIMER": static("GlobalVariables.h", r"\bztimer\s*="),
        "MC_TIMER_FRAME_MS": offset("zTools.h", "frameTimeFloat"),
        "MC_CAMERA_SIZE": 0x29C,
        **{"MC_CAMERA_" + key: offset("zAiCamera.h", field) for key, field in {
            "MODE": "curcammode", "TARGET": "target", "VOB": "camVob",
            "CUTSCENE": "inCutsceneMode", "FIRST_PERSON": "firstPerson"}.items()},
    }
    output = PRIVATE / "build"
    header = "".join(f"#define {key} 0x{value:X}u\n" for key, value in layout.items())
    header += "static const struct { unsigned address; unsigned char code[12]; } mc_fingerprints[] = {\n"
    for name in ["MC_WALK", "MC_TURN", "MC_GET_STATE", "MC_GET_TOGGLED", "MC_CAMERA_DO_AI", "MC_SET_POSITION", "MC_SET_HEADING", "MC_NPC_UNCONSCIOUS"]:
        header += "{0x%Xu,{%s}},\n" % (layout[name], ",".join(str(v) for v in image.read(layout[name], 12)))
    (output / "modern_layout.h").write_text(header + "};\n")
    minhook = PRIVATE / "minhook/minhook-1.3.4"
    environment = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(PRIVATE / "compiler-cache/global"), ZIG_LOCAL_CACHE_DIR=str(PRIVATE / "compiler-cache/local"))
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    sources = [PROJECT / "native/modern_controls.c", *(minhook / "src" / file for file in ["buffer.c", "hook.c", "trampoline.c", "hde/hde32.c"])]
    target = output / "ArcholosModernControls.dll"
    subprocess.run([str(zig), "cc", "-target", "x86-windows-gnu", "-shared", "-O2", "-Wall", "-Wextra", "-I", str(output), "-I", str(minhook / "include"), *map(str, sources), "-luser32", "-lkernel32", "-o", str(target)], env=environment, check=True)
    receipt = {"sha256": hashlib.sha256(target.read_bytes()).hexdigest(), "mechanism": "modern-exploration-v1", "gameplayVerified": False, "hostSha256": hashlib.sha256(image.data).hexdigest(), "layout": layout}
    (output / "modern-build.json").write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()

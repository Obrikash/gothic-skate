"""Build the x86 probe using pinned private tools and local Gothic API layouts."""
from pathlib import Path
import hashlib
import re
import subprocess
import json
import os
import struct

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = ROOT / ".modding/skate3-archolos"
PROJECT = Path(__file__).resolve().parents[1]


def build():
    exe = ROOT / "system/Gothic2.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != "0655b4811e008a353ce372fde463ef00b6101f3e89be850e2f14980140447c7f":
        raise RuntimeError("Host executable differs from the inspected build")
    api_root = next((PRIVATE / "gothic-api").iterdir()) / "ZenGin/Gothic_II_Addon"
    output = PRIVATE / "build"
    output.mkdir(exist_ok=True)

    def method(file, name):
        text = (api_root / "API" / file).read_text(encoding="utf-8-sig")
        found = re.findall(rf"[^\n]*\b{re.escape(name)}\([^\n]*zCall\(\s*(0x[0-9A-Fa-f]+)\s*\)", text)
        if len(found) != 1:
            raise RuntimeError(f"Expected one API method {name}, found {found}")
        return int(found[0], 16)

    def overload(file, signature, owner=None):
        """Resolve one exact overload, e.g. 'void StartAni( int, int )', optionally inside one class."""
        text = (api_root / "API" / file).read_text(encoding="utf-8-sig")
        if owner:
            body = re.search(rf"^  class {re.escape(owner)}\b[^\n]*\{{(.*?)^  \}};", text, re.M | re.S)
            if not body:
                raise RuntimeError(f"Cannot find API class {owner}")
            text = body[1]
        found = re.findall(rf"^\s*{re.escape(signature)}\s+zCall\(\s*(0x[0-9A-Fa-f]+)\s*\)", text, re.M)
        if len(found) != 1:
            raise RuntimeError(f"Expected one API overload {signature}, found {found}")
        return int(found[0], 16)

    def global_address(name):
        text = (api_root / "GlobalVariables.h").read_text(encoding="utf-8-sig")
        found = re.findall(rf"\b{name}\s*=.*?(0x[0-9A-Fa-f]+)", text)
        if len(found) != 1:
            raise RuntimeError(f"Expected one global {name}")
        return int(found[0], 16)

    def offset(file, name, owner=None):
        text = (api_root / "API" / file).read_text(encoding="utf-8-sig")
        if owner:
            body = re.search(rf"^  class {re.escape(owner)}\b[^\n;]*\{{(.*?)^  \}};", text, re.M | re.S)
            if not body:
                raise RuntimeError(f"Cannot find API class {owner}")
            text = body[1]
        found = re.findall(rf"\b{name}\s*;[^\n]*offset\s+([0-9A-Fa-f]+)h", text)
        if len(found) != 1:
            raise RuntimeError(f"Expected one offset for {name}, found {found}")
        return int(found[0], 16)

    world = (api_root / "API/zWorld.h").read_text(encoding="utf-8-sig")
    nearest = re.findall(r"TraceRayNearestHit\(.*?zCVob\*\s*,.*?zCall\(\s*(0x[0-9A-Fa-f]+)", world)
    if len(nearest) != 1:
        raise RuntimeError("Cannot resolve the exact nearest-hit overload")
    string_header = (api_root / "API/zString.h").read_text(encoding="utf-8-sig")
    string_constructor = re.findall(r"zSTRING_OnInit\( char const\*[^\n]*zCall\(\s*(0x[0-9A-Fa-f]+)", string_header)
    string_destructor = re.findall(r"~zSTRING\(\)[^\n]*zCall\(\s*(0x[0-9A-Fa-f]+)", string_header)
    if len(string_constructor) != 1 or len(string_destructor) != 1:
        raise RuntimeError("Cannot resolve the exact game-string ABI")
    layout = {
        "G2A_IMAGE_BASE": 0x400000,
        "G2A_IS_RUNNING": method("oGameManager.h", "IsGameRunning"),
        "G2A_GAME": global_address("ogame"),
        "G2A_PLAYER": global_address("player"), "G2A_MANAGER": global_address("gameMan"),
        "G2A_VOB_MATRIX": offset("zVob.h", "trafoObjToWorld"),
        "G2A_VOB_BBOX": offset("zVob.h", "bbox3D"),
        "G2A_VOB_HOME_WORLD": offset("zVob.h", "homeWorld"),
        "G2A_VOB_FLAGS": offset("zVob.h", "nextOnTimer") + 4,
        "G2A_SET_SLEEP": method("zVob.h", "SetSleepingMode"),
        "G2A_SET_PHYSICS": method("zVob.h", "SetPhysicsEnabled"),
        "G2A_SET_STATIC_COLLISION": method("zVob.h", "SetCollDetStat"),
        "G2A_SET_DYNAMIC_COLLISION": method("zVob.h", "SetCollDetDyn"),
        "G2A_SET_TRANSFORM": method("zVob.h", "SetTrafoObjToWorld"),
        "G2A_NPC_DEAD": method("oNpc.h", "IsDead"),
        "G2A_NPC_BODY": method("oNpc.h", "GetBodyState"),
        "G2A_NPC_WEAPON": method("oNpc.h", "GetWeaponMode"),
        "G2A_NPC_MODEL": method("oNpc.h", "GetModel"),
        "G2A_NPC_SET_MOVLOCK": method("oNpc.h", "SetMovLock"),
        "G2A_NPC_IS_MOVLOCK": method("oNpc.h", "IsMovLock"),
        "G2A_MODEL_ANI_ID": overload("zModel.h", "int GetAniIDFromAniName( zSTRING const& ) const"),
        "G2A_MODEL_ANI_FROM_ID": overload("zModel.h", "zCModelAni* GetAniFromAniID( int ) const"),
        "G2A_MODEL_ANI_ACTIVE": overload("zModel.h", "int IsAniActive( zCModelAni* )"),
        "G2A_MODEL_START_ANI": overload("zModel.h", "void StartAni( int, int )"),
        "G2A_MODEL_STOP_ANI": overload("zModel.h", "void StopAni( int )"),
        "G2A_CREATE_INSTANCE": overload("zObject.h", "static zCObject* CreateNewInstance( zSTRING const& )"),
        "G2A_OBJECT_RELEASE": overload("zObject.h", "int Release()", "zCObject"),
        "G2A_VOB_SET_VISUAL": overload("zVob.h", "virtual void SetVisual( zSTRING const& )", "zCVob"),
        "G2A_VOB_REMOVE_FROM_WORLD": method("zVob.h", "RemoveVobFromWorld"),
        "G2A_WORLD_ADD_VOB": overload("zWorld.h", "zCTree<zCVob>* AddVob( zCVob* )", "zCWorld"),
        "G2A_WORLD_ARCHIVE_VOBS": overload("zWorld.h", "void __fastcall ArcTraverseVobs( zCArchiver&, zCTree<zCVob>*, int )", "zCWorld"),
        # Bitfield bytes after MSVC packing: verified against code below.
        "G2A_VOB_ARCHIVE_FLAGS": offset("zVob.h", "m_poCollisionObjectClass") - 4,
        "G2A_VOB_DONT_ARCHIVE_BIT": 0x10, "G2A_VOB_IGNORED_BY_TRACE_BIT": 0x20, "G2A_VOB_SHOW_VISUAL_BIT": 0x01,
        "G2A_LINE_CACHE": global_address("zlineCache"),
        "G2A_LINE_3D": method("z3d.h", "Line3D"),
        "G2A_SCREEN": global_address("screen"),
        "G2A_PRINT_CX": method("zView.h", "PrintCX"),
        "G2A_STRING_CTOR": int(string_constructor[0], 16),
        "G2A_STRING_DTOR": int(string_destructor[0], 16),
        "G2A_GAME_LOADING": offset("oGame.h", "inLoadSaveGame"),
        "G2A_GAME_LEVEL_CHANGE": offset("oGame.h", "inLevelChange"),
        "G2A_GAME_PAUSE": offset("oGame.h", "singleStep"),
        "G2A_GAME_WORLD_ENTERED": offset("oGame.h", "m_bWorldEntered"),
        "G2A_SESSION_WORLD": offset("zSession.h", "world"),
        "G2A_WORLD_REPORT": offset("zWorld.h", "traceRayReport"),
        "G2A_REPORT_INTERSECTION": offset("zWorld.h", "foundIntersection"),
        "G2A_TRACE_NEAREST": int(nearest[0], 16), "G2A_TRACE_IGNORE_VOBS": 2, "G2A_TRACE_NORMAL": 128,
        "G2A_TRACE_IGNORE_CHARACTERS": 2048, "G2A_TRACE_IGNORE_NO_COLLISION": 1,
        "G2A_WORLD_BSP": offset("zWorld.h", "bspTree"),
        "G2A_BSP_POLYS": offset("zBsp.h", "treePolyList", "zCBspTree"),
        "G2A_BSP_POLY_COUNT": offset("zBsp.h", "numPolys", "zCBspTree"),
        "G2A_POLYGON_VERTICES": offset("z3d.h", "vertex", "zCPolygon"),
        "G2A_POLYGON_VERTEX_COUNT": offset("z3d.h", "polyNumVert", "zCPolygon"),
        "G2A_POLYGON_FLAGS": offset("z3d.h", "flags", "zCPolygon"),
        "G2A_VERTEX_POSITION": offset("z3d.h", "position", "zCVertex"),
        "G2A_MODEL_ADVANCE": overload("zModel.h", "void AdvanceAnis()", "zCModel"),
        "G2A_VOB_COLLISION_VOBS": method("zVob.h", "CollectCollisionContext_Vobs"),
        "G2A_VOB_COLLISION_LEVEL": method("zVob.h", "CollectCollisionContext_Static"),
        "G2A_VOB_DYNAMIC_COLLISION_BIT": 0x80,
        "G2A_MATERIAL_GROUP": offset("zMaterial.h", "matGroup", "zCMaterial"),
        "G2A_MATERIAL_FLAGS": offset("zMaterial.h", "m_bEnvironmentalMappingStrength", "zCMaterial") + 4,
        "G2A_MATERIAL_NO_COLLISION_BIT": 0x10,
        "G2A_MATERIAL_WATER": 5,
        "G2A_POLYGON_MATERIAL": offset("z3d.h", "material", "zCPolygon"),
        "G2A_PMESH_CLASSDEF_GETTER": overload("zProgMesh.h", "virtual zCClassDef* _GetClassDef() const", "zCProgMeshProto"),
        "G2A_PMESH_POSITIONS": offset("zProgMesh.h", "posList", "zCProgMeshProto"),
        "G2A_PMESH_SUBMESHES": offset("zProgMesh.h", "subMeshList", "zCProgMeshProto"),
        "G2A_PMESH_SUBMESH_COUNT": offset("zProgMesh.h", "numSubMeshes", "zCProgMeshProto"),
        # Nested zCSubMesh (sizeof 58h): triList +04h, wedgeList +0Ch (zCArrayAdapt {T*, int});
        # zTPMTriangle is u16 wedge[3]; zTPMWedge (sizeof 18h) has its u16 position at +14h.
        "G2A_SUBMESH_SIZE": 0x58, "G2A_SUBMESH_TRIANGLES": 0x04, "G2A_SUBMESH_WEDGES": 0x0C,
        "G2A_WEDGE_SIZE": 0x18, "G2A_WEDGE_POSITION": 0x14,
        # Model nodes: zCModel::nodeList (zCArray {array, numAlloc, numInArray}),
        # each zCModelNodeInst's local trafo (what RenderNodeList composes) and
        # its prototype's name and bind trafo.
        "G2A_MODEL_NODE_LIST": offset("zModel.h", "nodeList", "zCModel"),
        "G2A_NODEINST_PARENT": offset("zModel.h", "parentNode", "zCModelNodeInst"),
        "G2A_NODEINST_PROTO": offset("zModel.h", "protoNode", "zCModelNodeInst"),
        "G2A_NODEINST_TRAFO": offset("zModel.h", "trafo", "zCModelNodeInst"),
        "G2A_NODE_NAME": offset("zModel.h", "nodeName", "zCModelNode"),
        "G2A_NODE_TRAFO": offset("zModel.h", "trafo", "zCModelNode"),
        "G2A_WORLD_VOB_TREE": offset("zWorld.h", "globalVobTree", "zCWorld"),
        "G2A_VOB_VISUAL": offset("zVob.h", "visual", "zCVob"),
        "G2A_CLASSDEF_BASE": offset("zObject.h", "baseClassDef", "zCClassDef"),
    }
    binary = exe.read_bytes()
    pe = struct.unpack_from("<I", binary, 0x3C)[0]
    optional = pe + 24
    sections = struct.unpack_from("<H", binary, pe + 6)[0]
    section_table = optional + struct.unpack_from("<H", binary, pe + 20)[0]
    layout["G2A_IMAGE_TIMESTAMP"] = struct.unpack_from("<I", binary, pe + 8)[0]
    layout["G2A_IMAGE_SIZE"] = struct.unpack_from("<I", binary, optional + 56)[0]

    def code_at(address, size=12):
        rva = address - layout["G2A_IMAGE_BASE"]
        for index in range(sections):
            entry = section_table + index * 40
            virtual_size, start, raw_size, raw = struct.unpack_from("<4I", binary, entry + 8)
            if start <= rva < start + min(virtual_size, raw_size):
                return binary[raw + rva - start:raw + rva - start + size]
        raise RuntimeError("API function was not inside a backed host section")

    sleep_code = code_at(layout["G2A_SET_SLEEP"], 32)
    sleeping = re.findall(b"\x8a\x8b(.{4})\x83\xe1\x03", sleep_code, re.S)
    if len(sleeping) != 1:
        raise RuntimeError("Cannot verify the original sleeping-state field")
    layout["G2A_VOB_SLEEP_FLAGS"] = struct.unpack("<I", sleeping[0])[0]
    # Confirm the physics-bit access rather than inferring MSVC bitfield packing.
    flags = struct.pack("<I", layout["G2A_VOB_FLAGS"])
    if b"\x88\x91" + flags not in code_at(layout["G2A_SET_PHYSICS"], 48):
        raise RuntimeError("Cannot verify the original physics-state field")
    # Object/object collisions test the sign (0x80) of BOTH vobs' flag bytes.
    vob_collision = code_at(layout["G2A_VOB_COLLISION_VOBS"], 0xA0)
    if b"\x8a\x87" + flags not in vob_collision or b"\x8a\x96" + flags not in vob_collision:
        raise RuntimeError("Cannot verify the dynamic object collision flag reads")
    if b"\x84\xc0" not in vob_collision or b"\x84\xd2" not in vob_collision:
        raise RuntimeError("Cannot verify the sign tests of the object collision flags")
    level_collision = code_at(layout["G2A_VOB_COLLISION_LEVEL"], 0x200)
    # Group/flags are byte-sized displacements in this build's native filter.
    if b"\xbb" + struct.pack("<I", layout["G2A_MATERIAL_WATER"]) not in level_collision or \
       bytes([0x39, 0x58, layout["G2A_MATERIAL_GROUP"]]) not in level_collision or \
       bytes([0xF6, 0x40, layout["G2A_MATERIAL_FLAGS"], layout["G2A_MATERIAL_NO_COLLISION_BIT"]]) not in level_collision:
        raise RuntimeError("Cannot verify the native water/noCollDet material filter")
    # zCProgMeshProto::_GetClassDef is `mov eax, <classDef>; ret`: the exact
    # class a static mesh visual reports, compared without calling it.
    getter = code_at(layout["G2A_PMESH_CLASSDEF_GETTER"], 6)
    if getter[0] != 0xB8 or getter[5] != 0xC3:
        raise RuntimeError("zCProgMeshProto::_GetClassDef has an unexpected shape")
    layout["G2A_PMESH_CLASSDEF"] = struct.unpack_from("<I", getter, 1)[0]
    # The bridge advances Marvin's animations itself while his vob sleeps.
    if not code_at(layout["G2A_MODEL_ADVANCE"], 1):
        raise RuntimeError("zCModel::AdvanceAnis is not file-backed")
    # zCWorld::ArcTraverseVobs skips vobs whose dontWriteIntoArchive bit is set.
    archive_test = b"\xf6\x81" + struct.pack("<I", layout["G2A_VOB_ARCHIVE_FLAGS"]) + bytes([layout["G2A_VOB_DONT_ARCHIVE_BIT"]])
    if archive_test not in code_at(layout["G2A_WORLD_ARCHIVE_VOBS"], 0x100):
        raise RuntimeError("Cannot verify the dontWriteIntoArchive bit in the savegame writer")
    # The single-vob nearest-hit trace (the sampler's) wraps the zCArray overload,
    # which tests ignoredByTraceRay in the first flag byte.
    array_trace = overload("zWorld.h", "int __fastcall TraceRayNearestHit( zVEC3 const&, zVEC3 const&, zCArray<zCVob*> const*, int )", "zCWorld")
    wrapper = code_at(layout["G2A_TRACE_NEAREST"], 0x30)
    call = wrapper.find(b"\xe8")
    if call < 0 or layout["G2A_TRACE_NEAREST"] + call + 5 + struct.unpack_from("<i", wrapper, call + 1)[0] != array_trace:
        raise RuntimeError("Nearest-hit trace no longer wraps the zCArray overload")
    trace_test = re.compile(rb"\xf6[\x80-\x87]" + re.escape(flags) + bytes([layout["G2A_VOB_IGNORED_BY_TRACE_BIT"]]))
    if not trace_test.search(code_at(array_trace, 0x400)):
        raise RuntimeError("Cannot verify the ignoredByTraceRay bit in the nearest-hit trace")
    header = "\n".join(f"#define {key} 0x{value:X}u" for key, value in layout.items()) + "\n"
    for name, address in [("running", layout["G2A_IS_RUNNING"]), ("trace", layout["G2A_TRACE_NEAREST"])]:
        header += f"static const unsigned char g2a_expected_{name}[] = {{" + ",".join(f"0x{value:02x}" for value in code_at(address)) + "};\n"
    header += "struct G2aFingerprint { unsigned address; unsigned char code[12]; };\nstatic const struct G2aFingerprint g2a_bridge_fingerprints[] = {\n"
    for key in ["G2A_SET_SLEEP", "G2A_SET_PHYSICS", "G2A_SET_STATIC_COLLISION", "G2A_SET_DYNAMIC_COLLISION", "G2A_SET_TRANSFORM", "G2A_NPC_DEAD", "G2A_NPC_BODY", "G2A_NPC_WEAPON", "G2A_NPC_MODEL", "G2A_NPC_SET_MOVLOCK", "G2A_NPC_IS_MOVLOCK", "G2A_MODEL_ANI_ID", "G2A_MODEL_ANI_FROM_ID", "G2A_MODEL_ANI_ACTIVE", "G2A_MODEL_START_ANI", "G2A_MODEL_STOP_ANI", "G2A_MODEL_ADVANCE", "G2A_PMESH_CLASSDEF_GETTER", "G2A_CREATE_INSTANCE", "G2A_OBJECT_RELEASE", "G2A_VOB_SET_VISUAL", "G2A_VOB_REMOVE_FROM_WORLD", "G2A_WORLD_ADD_VOB", "G2A_LINE_3D", "G2A_PRINT_CX", "G2A_STRING_CTOR", "G2A_STRING_DTOR", "G2A_TRACE_NEAREST"]:
        header += "{" + f"0x{layout[key]:X}u," + "{" + ",".join(f"0x{value:02x}" for value in code_at(layout[key])) + "}},\n"
    header += "};\n"
    (output / "gothic_layout.h").write_text(header)
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    target = output / "ArcholosSkateProbe.dll"
    command = [str(zig), "cc", "-target", "x86-windows-gnu", "-shared", "-O2", "-Wall", "-Wextra",
               "-I", str(output), str(PROJECT / "native/host_probe.c"),
               "-luser32", "-lkernel32", "-o", str(target)]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(PRIVATE / "compiler-cache/global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(PRIVATE / "compiler-cache/local")
    subprocess.run(command, cwd=output, env=environment, check=True)
    report = {"kind": "read-only host probe", "mechanism": "union-callbacks-v2", "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
              "gothicApiCommit": api_root.parents[1].name.split("-")[-1], "compiler": "Zig 0.17.0", "installed": False}
    (output / "probe-build.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    build()

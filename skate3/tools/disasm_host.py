"""Disassemble Gothic2.exe functions, naming calls and globals from the pinned Gothic API.

Static and read-only: it reads the installed executable as data and never runs it.
Run with the private RE interpreter, which has capstone:
  ../../../.modding/skate3-archolos/re-venv/Scripts/python.exe disasm_host.py oCNpc::SetMovLock
Output is analysis of the user's own copy; keep it out of Git.
"""
import argparse
import hashlib
import re
import struct
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = ROOT / ".modding/skate3-archolos"
EXPECTED = "0655b4811e008a353ce372fde463ef00b6101f3e89be850e2f14980140447c7f"


class Image:
    def __init__(self):
        self.data = (ROOT / "system/Gothic2.exe").read_bytes()
        if hashlib.sha256(self.data).hexdigest() != EXPECTED:
            raise RuntimeError("Host executable differs from the inspected build")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        optional = pe + 24
        self.base = struct.unpack_from("<I", self.data, optional + 28)[0]
        table = optional + struct.unpack_from("<H", self.data, pe + 20)[0]
        self.sections = []
        for index in range(struct.unpack_from("<H", self.data, pe + 6)[0]):
            entry = table + index * 40
            name = self.data[entry:entry + 8].rstrip(b"\0").decode()
            virtual_size, start, raw_size, raw = struct.unpack_from("<4I", self.data, entry + 8)
            self.sections.append((name, start, min(virtual_size, raw_size), raw))

    def read(self, address, size):
        rva = address - self.base
        for _, start, length, raw in self.sections:
            if start <= rva < start + length:
                return self.data[raw + rva - start:raw + rva - start + min(size, start + length - rva)]
        raise ValueError(f"{address:08X} is not in a file-backed section")


def api_names():
    """Address -> 'Class::member' for every zCall method and global in the API headers."""
    api = next((PRIVATE / "gothic-api").iterdir()) / "ZenGin/Gothic_II_Addon"
    names = {}
    for header in (api / "API").glob("*.h"):
        owner = None
        for line in header.read_text(encoding="utf-8-sig", errors="replace").splitlines():
            found = re.match(r"^  (?:class|struct) (\w+)", line)
            if found:
                owner = found[1]
            call = re.search(r"([~\w]+)\(([^)]*)\)[^\n]*zCall\(\s*(0x[0-9A-Fa-f]+)\s*\)", line)
            if call:
                key = int(call[3], 16)
                label = f"{owner}::{call[1]}({' '.join(call[2].split())})" if owner else call[1]
                names.setdefault(key, label)
    for line in (api / "GlobalVariables.h").read_text(encoding="utf-8-sig", errors="replace").splitlines():
        found = re.search(r"(\w+)\s*=.*?(0x[0-9A-Fa-f]+)", line)
        if found:
            names.setdefault(int(found[2], 16), "global " + found[1])
    return names


def resolve(target, names):
    if re.fullmatch(r"(0x)?[0-9A-Fa-f]{6,8}", target):
        return int(target, 16)
    matches = [address for address, label in names.items() if label.split("(")[0] == target or label == target]
    if len(matches) != 1:
        raise SystemExit(f"{target}: {len(matches)} matches " + ", ".join(f"{a:08X} {names[a]}" for a in matches[:8]))
    return matches[0]


def disassemble(image, names, address, limit, stop_at_return=True):
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    code = image.read(address, limit * 16)
    lines, count = [], 0
    for instruction in engine.disasm(code, address):
        note = ""
        for value in re.findall(r"0x[0-9a-f]+", instruction.op_str):
            label = names.get(int(value, 16))
            if label:
                note = "  ; " + label
                break
        lines.append(f"{instruction.address:08X}  {instruction.mnemonic:<7} {instruction.op_str}{note}")
        count += 1
        if count >= limit or (stop_at_return and instruction.mnemonic in ("ret", "retn")):
            break
    return lines


def callers(image, names, target):
    """Direct E8 rel32 calls to target, each with the nearest preceding API function."""
    known = sorted(names)
    found = []
    for name, start, length, raw in image.sections:
        if name != ".text":
            continue
        block = image.data[raw:raw + length]
        position = block.find(b"\xe8")
        while position != -1:
            if position + 5 <= len(block):
                site = image.base + start + position
                if site + 5 + struct.unpack_from("<i", block, position + 1)[0] == target:
                    owner = max((address for address in known if address <= site), default=None)
                    found.append((site, owner))
            position = block.find(b"\xe8", position + 1)
    return found


def displacement_users(image, names, displacement):
    """Linear sweep of .text for memory operands [reg + displacement]."""
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    engine.detail = True
    known = sorted(names)
    found = []
    for name, start, length, raw in image.sections:
        if name != ".text":
            continue
        address, block = image.base + start, image.data[raw:raw + length]
        offset = 0
        while offset < len(block):
            decoded = False
            for instruction in engine.disasm(block[offset:offset + 4096], address + offset):
                decoded = True
                for operand in instruction.operands:
                    if operand.type == capstone.x86.X86_OP_MEM and operand.mem.disp == displacement and operand.mem.base:
                        owner = max((a for a in known if a <= instruction.address), default=None)
                        found.append((instruction.address, f"{instruction.mnemonic} {instruction.op_str}", owner))
                offset = instruction.address + instruction.size - address
            if not decoded:
                offset += 1
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("targets", nargs="+", help="Class::Method or a hex address")
    parser.add_argument("--limit", type=int, default=120, help="maximum instructions per function")
    parser.add_argument("--through-returns", action="store_true", help="do not stop at the first ret")
    parser.add_argument("--callers", action="store_true", help="list direct call sites instead")
    parser.add_argument("--field", action="store_true", help="targets are hex field offsets; list [reg+offset] users")
    arguments = parser.parse_args()
    image, names = Image(), api_names()
    if arguments.field:
        for target in arguments.targets:
            displacement = int(target, 16)
            print(f"== users of [reg+0x{displacement:X}] (any class with that offset)")
            for site, text, owner in displacement_users(image, names, displacement):
                print(f"{site:08X}  {text:<40} in ~{names.get(owner, '?')} (+0x{site - owner:X})")
        return
    for target in arguments.targets:
        address = resolve(target, names)
        if arguments.callers:
            print(f"== callers of {address:08X} {names.get(address, '')}")
            for site, owner in callers(image, names, address):
                # The owner is the nearest API symbol at or below the site, which may be imprecise.
                print(f"{site:08X}  in ~{owner:08X} {names.get(owner, '?')} (+0x{site - owner:X})")
            continue
        print(f"== {address:08X} {names.get(address, '')}")
        print("\n".join(disassemble(image, names, address, arguments.limit, not arguments.through_returns)))


if __name__ == "__main__":
    main()

"""Inspect a Windows PE file's architecture and imported DLLs without loading it."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Missing DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE header")
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic not in (0x10B, 0x20B):
        raise ValueError("Unknown PE optional header")
    directory = optional + (96 if magic == 0x10B else 112)
    import_rva = struct.unpack_from("<I", data, directory + 8)[0]
    table = optional + optional_size
    ranges = []
    for index in range(sections):
        entry = table + index * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from("<4I", data, entry + 8)
        ranges.append((rva, max(virtual_size, raw_size), raw))

    def offset(rva):
        for start, size, raw in ranges:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError(f"RVA is outside section table: {rva:#x}")

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    imports = []
    symbols = {}
    if import_rva:
        cursor = offset(import_rva)
        while True:
            descriptor = struct.unpack_from("<5I", data, cursor)
            if not any(descriptor):
                break
            dll = string(descriptor[3])
            imports.append(dll)
            symbols[dll] = []
            thunk = offset(descriptor[0] or descriptor[4])
            width = 4 if magic == 0x10B else 8
            while True:
                value = struct.unpack_from("<I" if width == 4 else "<Q", data, thunk)[0]
                if value == 0:
                    break
                ordinal_bit = 1 << (width * 8 - 1)
                symbols[dll].append(f"ordinal:{value & 0xffff}" if value & ordinal_bit else string(value + 2))
                thunk += width
            cursor += 20
    exports = []
    export_ordinals = []
    export_rva = struct.unpack_from("<I", data, directory)[0]
    if export_rva:
        export_offset = offset(export_rva)
        base, function_count = struct.unpack_from("<II", data, export_offset + 16)
        functions = offset(struct.unpack_from("<I", data, export_offset + 28)[0])
        export_ordinals = [base + i for i in range(function_count) if struct.unpack_from("<I", data, functions + i * 4)[0]]
        count = struct.unpack_from("<I", data, export_offset + 24)[0]
        names = offset(struct.unpack_from("<I", data, export_offset + 32)[0])
        exports = [string(struct.unpack_from("<I", data, names + i * 4)[0]) for i in range(count)]
    return {
        "path": str(Path(path).resolve()), "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "architecture": {0x14C: "x86", 0x8664: "x64"}.get(machine, hex(machine)),
        "imports": imports, "importedSymbols": symbols, "exports": exports,
        "exportOrdinals": export_ordinals,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    print(json.dumps(inspect(parser.parse_args().path), indent=2))

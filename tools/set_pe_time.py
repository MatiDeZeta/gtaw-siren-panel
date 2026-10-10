#!/usr/bin/env python3
"""Stamps a built .asi with the time of the commit it was built from.

/Brepro makes the MSVC linker write a hash of the output where the build time goes, so the same
source always gives the same bytes. Read as a date that hash lands anywhere, decades in the
future as often as not, and a PE header dated in the future is one of the things antivirus
models score as suspicious. This puts a real date there instead -- the commit's, which is just as
reproducible, since the same commit always has the same time.

It rewrites the COFF header's TimeDateStamp and the TimeDateStamp of every debug directory entry,
then recomputes the optional header's CheckSum if the linker had set one.

    tools/set_pe_time.py <file.asi> [unix-time]

Without a time it uses SOURCE_DATE_EPOCH, then the HEAD commit's time from git. With neither it
leaves the file alone and says so.
"""
import os
import struct
import subprocess
import sys


def commit_time():
    env = os.environ.get("SOURCE_DATE_EPOCH")
    if env and env.isdigit():
        return int(env)
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        out = subprocess.run(["git", "-C", here, "log", "-1", "--format=%ct"],
                             capture_output=True, text=True, check=True).stdout.strip()
        return int(out)
    except (OSError, subprocess.CalledProcessError, ValueError):
        return None


def pe_checksum(data, checksum_offset):
    total = 0
    padded = data + b"\0" * (len(data) % 2)
    for i in range(0, len(padded), 2):
        if i == checksum_offset or i == checksum_offset + 2:
            continue
        total += struct.unpack_from("<H", padded, i)[0]
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (total + len(data)) & 0xFFFFFFFF


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    stamp = int(sys.argv[2]) if len(sys.argv) > 2 else commit_time()
    if stamp is None:
        print(f"{path}: no commit time available; timestamp left as the linker wrote it")
        return 0

    data = bytearray(open(path, "rb").read())
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        print(f"{path}: not a PE file", file=sys.stderr)
        return 1
    coff = pe + 4
    n_sections, opt_size = struct.unpack_from("<H", data, coff + 2)[0], struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        print(f"{path}: not PE32+", file=sys.stderr)
        return 1

    struct.pack_into("<I", data, coff + 4, stamp)

    # Debug directory: data directory 6. Each 28-byte entry has its own TimeDateStamp at +4.
    debug_rva, debug_size = struct.unpack_from("<II", data, opt + 112 + 8 * 6)
    sections = []
    for i in range(n_sections):
        base = opt + opt_size + 40 * i
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr))
    patched = 0
    if debug_rva:
        for vaddr, size, rawptr in sections:
            if vaddr <= debug_rva < vaddr + size:
                off = rawptr + (debug_rva - vaddr)
                for e in range(debug_size // 28):
                    struct.pack_into("<I", data, off + 28 * e + 4, stamp)
                    patched += 1
                break

    checksum_offset = opt + 64
    if struct.unpack_from("<I", data, checksum_offset)[0]:
        struct.pack_into("<I", data, checksum_offset, pe_checksum(bytes(data), checksum_offset))

    open(path, "wb").write(bytes(data))
    print(f"{path}: timestamp set to {stamp} ({patched} debug entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

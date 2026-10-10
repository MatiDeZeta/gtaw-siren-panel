#!/usr/bin/env python3
"""Checks a built gtaw-siren-panel.asi is something FiveM will actually load.

The FX_ASI_BUILD stamps are the part worth verifying mechanically: asi-five looks them up with
FindResource(module, L"FX_ASI_BUILD", MAKEINTRESOURCE(GetGameBuild())), and if the one matching
the running game build is missing it declines to load the DLL and writes nothing anywhere. A
typo there looks exactly like "the plugin does nothing".

Usage: tests/verify_asi.py [path-to-.asi]
"""
import struct
import sys

# Builds the plugin claims support for. Keep in step with gtaw-siren-panel.rc.
EXPECTED_BUILDS = [2189, 2372, 2545, 2612, 2699, 2802, 2944, 3095, 3258,
                   3323, 3407, 3442, 3570, 3679, 3751, 3775, 3788, 3889]

checks = 0
failures = 0


def check(label, ok, detail=""):
    global checks, failures
    checks += 1
    if ok:
        print(f"  ok   {label}" + (f"  =  {detail}" if detail else ""))
    else:
        failures += 1
        print(f"  FAIL {label}" + (f"  =  {detail}" if detail else ""))


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_off:pe_off + 4] != b"PE\0\0":
            raise ValueError("no PE signature")

        coff = pe_off + 4
        (self.machine, self.n_sections, _, _, _,
         self.opt_size, self.characteristics) = struct.unpack_from("<HHIIIHH", data, coff)

        opt = coff + 20
        self.magic = struct.unpack_from("<H", data, opt)[0]
        if self.magic != 0x20B:
            raise ValueError("not PE32+ (64-bit)")

        self.dll_characteristics = struct.unpack_from("<H", data, opt + 70)[0]
        n_dirs = struct.unpack_from("<I", data, opt + 108)[0]
        dirs = opt + 112
        self.directories = [struct.unpack_from("<II", data, dirs + 8 * i) for i in range(n_dirs)]

        sec = opt + self.opt_size
        self.sections = []
        for i in range(self.n_sections):
            base = sec + 40 * i
            name = data[base:base + 8].rstrip(b"\0").decode("latin1")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
            self.sections.append((name, vaddr, vsize, rawptr, rawsize))

    def offset(self, rva):
        for _name, vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        return None


def resource_string(data, base, offset):
    length = struct.unpack_from("<H", data, base + offset)[0]
    raw = data[base + offset + 2: base + offset + 2 + length * 2]
    return raw.decode("utf-16-le", "replace")


def walk(data, base, offset, depth, path, out):
    """Walks the three-level resource tree, collecting (type, name) pairs."""
    n_named, n_id = struct.unpack_from("<HH", data, base + offset + 12)
    entries = base + offset + 16
    for i in range(n_named + n_id):
        name_field, data_field = struct.unpack_from("<II", data, entries + 8 * i)
        if name_field & 0x80000000:
            key = resource_string(data, base, name_field & 0x7FFFFFFF)
        else:
            key = name_field
        if data_field & 0x80000000:
            if depth < 2:
                walk(data, base, data_field & 0x7FFFFFFF, depth + 1, path + [key], out)
            else:
                out.append(tuple(path + [key]))
        else:
            out.append(tuple(path + [key]))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "gtaw-siren-panel.asi"
    with open(path, "rb") as fh:
        data = fh.read()

    pe = PE(data)

    print(f"\n== {path} ({len(data):,} bytes) ==")
    check("64-bit PE (PE32+)", pe.magic == 0x20B)
    check("machine is x86-64", pe.machine == 0x8664, hex(pe.machine))
    check("it is a DLL", bool(pe.characteristics & 0x2000))

    # asi-five refuses managed assemblies. Directory 14 is the CLR header; a native DLL has none.
    clr_rva, clr_size = pe.directories[14] if len(pe.directories) > 14 else (0, 0)
    check("not a managed assembly (no CLR header)", clr_rva == 0 and clr_size == 0)

    # Nothing is exported: the loader only needs DllMain, which is the entry point, not an export.
    exp_rva, _ = pe.directories[0]
    check("exports nothing", exp_rva == 0)

    import time
    stamp = struct.unpack_from("<I", data, struct.unpack_from("<I", data, 0x3C)[0] + 8)[0]
    check("build date is not in the future", stamp <= time.time() + 86400,
          time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(stamp)))

    print("\n== security characteristics ==")
    check("NX_COMPAT",      bool(pe.dll_characteristics & 0x0100))
    check("DYNAMIC_BASE",   bool(pe.dll_characteristics & 0x0040))
    check("HIGH_ENTROPY_VA", bool(pe.dll_characteristics & 0x0020))

    print("\n== FX_ASI_BUILD stamps ==")
    rsrc_rva, _ = pe.directories[2]
    base = pe.offset(rsrc_rva)
    if base is None:
        check("resource directory present", False)
    else:
        found = []
        walk(data, base, 0, 0, [], found)
        # Level 1 is the type, level 2 the name. The .rc puts the build in the type and the
        # literal FX_ASI_BUILD in the name.
        builds = sorted({entry[0] for entry in found
                         if len(entry) > 1 and entry[1] == "FX_ASI_BUILD"
                         and isinstance(entry[0], int)})
        check("resource directory present", True)
        check("FX_ASI_BUILD entries found", len(builds) > 0, str(len(builds)))

        missing = [b for b in EXPECTED_BUILDS if b not in builds]
        extra = [b for b in builds if b not in EXPECTED_BUILDS]
        check("every build in the .rc is stamped", not missing,
              "missing " + str(missing) if missing else "all " + str(len(EXPECTED_BUILDS)))
        check("no unexpected builds", not extra, str(extra) if extra else "none")
        if builds:
            print(f"       builds: {', '.join(str(b) for b in builds)}")

        has_version = any(entry[0] == 16 for entry in found)
        check("version resource present", has_version)

        print("\n== panel images, as resources ==")
        rcdata = {entry[1] for entry in found if len(entry) > 1 and entry[0] == 10}
        panel = sorted(n for n in rcdata if isinstance(n, str) and n.startswith("KB_"))
        check("panel layers present", len(panel) == 26, str(len(panel)))

    print(f"\n{checks - failures}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

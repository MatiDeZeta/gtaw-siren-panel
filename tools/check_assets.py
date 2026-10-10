#!/usr/bin/env python3
"""Checks the files gtaw-siren-panel.rc compiles into the .asi as resources.

The panel images are compiled into a binary that runs inside the game, so they are held to a
strict shape: each one must be a well-formed PNG, every chunk's CRC must match, nothing may
follow the IEND chunk, and it may carry pixels and nothing else -- no text, no timestamps, no colour profiles. The .rc, the
layer list in src/hud/images.cpp and the folder must also agree.

    tools/check_assets.py          check, and fail on anything out of shape
    tools/check_assets.py --fix    strip the metadata chunks in place, then check

--fix never repairs a damaged file; a bad CRC or trailing bytes still fail.
"""
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL = os.path.join(ROOT, "assets", "kabel")
RC = os.path.join(ROOT, "gtaw-siren-panel.rc")
LAYERS_CPP = os.path.join(ROOT, "src", "hud", "images.cpp")

SIGNATURE = b"\x89PNG\r\n\x1a\n"
# What a browser needs to draw the image as it was drawn. Anything else is dropped by --fix.
KEEP = {b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND", b"gAMA", b"sRGB"}

failures = 0


def fail(msg):
    global failures
    failures += 1
    print(f"  FAIL {msg}")


def chunks(raw):
    """Yields (kind, whole_chunk_bytes), raising on anything malformed."""
    if raw[:8] != SIGNATURE:
        raise ValueError("not a PNG")
    i = 8
    while i < len(raw):
        if i + 12 > len(raw):
            raise ValueError(f"truncated chunk at byte {i}")
        (length,) = struct.unpack(">I", raw[i:i + 4])
        kind = raw[i + 4:i + 8]
        end = i + 12 + length
        if end > len(raw):
            raise ValueError(f"truncated {kind!r} chunk")
        body = raw[i + 8:i + 8 + length]
        (crc,) = struct.unpack(">I", raw[end - 4:end])
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise ValueError(f"bad CRC on {kind!r} chunk")
        yield kind, raw[i:end]
        i = end
        if kind == b"IEND":
            if i != len(raw):
                raise ValueError(f"{len(raw) - i} bytes after IEND")
            return
    raise ValueError("no IEND chunk")


def check_png(path, fix):
    with open(path, "rb") as fh:
        raw = fh.read()
    try:
        parts = list(chunks(raw))
    except ValueError as e:
        fail(f"{os.path.relpath(path, ROOT)}: {e}")
        return
    extra = sorted({k.decode("latin1") for k, _ in parts if k not in KEEP})
    if extra and fix:
        with open(path, "wb") as fh:
            fh.write(SIGNATURE + b"".join(c for k, c in parts if k in KEEP))
        print(f"  fixed {os.path.relpath(path, ROOT)}: dropped {', '.join(extra)}")
    elif extra:
        fail(f"{os.path.relpath(path, ROOT)}: carries {', '.join(extra)}; run with --fix")
    else:
        print(f"  ok   {os.path.relpath(path, ROOT)}")


def main():
    fix = "--fix" in sys.argv[1:]

    with open(LAYERS_CPP, encoding="utf8") as fh:
        layers = re.findall(r'"([a-z0-9]+)"', fh.read().split("kPanelLayers[]", 1)[1])
    with open(RC, encoding="utf8") as fh:
        rc = fh.read()
    in_rc = dict(re.findall(r'^KB_([A-Z0-9]+)\s+RCDATA\s+"assets/kabel/([a-z0-9]+)\.png"',
                            rc, re.M))

    print("== the layer list, the .rc and the folder agree ==")
    on_disk = sorted(f[:-4] for f in os.listdir(PANEL) if f.endswith(".png"))
    for name in layers:
        if in_rc.get(name.upper()) != name:
            fail(f"layer {name} has no KB_{name.upper()} line in the .rc")
    for res, leaf in in_rc.items():
        if res.lower() != leaf or leaf not in layers:
            fail(f"KB_{res} in the .rc is not a layer the script draws")
    for leaf in on_disk:
        if leaf not in layers:
            fail(f"assets/kabel/{leaf}.png is not compiled in; remove it")
    if not failures:
        print(f"  ok   {len(layers)} layers")

    print("== every layer is a clean PNG ==")
    for name in layers:
        path = os.path.join(PANEL, name + ".png")
        if not os.path.exists(path):
            fail(f"assets/kabel/{name}.png is missing")
            continue
        check_png(path, fix)

    print("all clean" if not failures else f"{failures} problem(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

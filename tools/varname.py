#!/usr/bin/env python3
"""Instance variable names by id: varname.py <vm dir> [ids...] (or reads
"var <1000+id> <type> <bits>" dump lines on stdin and names them)."""
import struct
import sys
from pathlib import Path

d = (Path(sys.argv[1]) / "code.bin").read_bytes()
f = struct.unpack_from("<12I", d, 4)
nstrings, ninst, so, iname = f[0], f[4], f[5], f[9]
offs = struct.unpack_from(f"<{nstrings}I", d, so)
ids = struct.unpack_from(f"<{ninst}H", d, iname)


def name(i):
    o = offs[ids[i]]
    return d[o:d.index(b"\0", o)].decode()


if len(sys.argv) > 2:
    for a in sys.argv[2:]:
        print(a, name(int(a)))
else:
    for line in sys.stdin:
        p = line.split()
        if len(p) == 4 and p[0] == "var" and int(p[1]) >= 1000:
            bits = int(p[3], 16)
            val = struct.unpack("<f", struct.pack("<I", bits))[0] if p[2] == "1" else p[3]
            print(f"{name(int(p[1]) - 1000):24s} type {p[2]} {val}")
        else:
            print(line.rstrip())

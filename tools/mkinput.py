#!/usr/bin/env python3
"""Scripted input for `make TEST=<frames>` calculator builds: the host's
-i file ("frame key hold" lines, key = virtual key code) plus its -m/-M
Z mashing, as the AppVar UTIN.

Lines "frame warp <room index>" and "frame battle <group>" go to a room or
start a battle group at that frame.

Usage: mkinput.py <input.txt> <out.8xv> [mash every] [mash from]
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from cepack import write_8xv  # noqa: E402


def main():
    src, out = sys.argv[1:3]
    mash = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    mash_from = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    recs = []
    for line in open(src):
        p = line.split()
        if len(p) == 3 and p[1] in ("warp", "battle"):
            # frame warp <room index> / frame battle <group>: commands
            n = int(p[2])
            key = (251 if p[1] == "warp" else 253) + (n >= 256)
            recs.append(struct.pack("<IBB", int(p[0]), key, n & 255))
        elif len(p) == 3:
            recs.append(struct.pack("<IBB", int(p[0]), int(p[1]) & 255, int(p[2])))
    data = b"UTI1" + struct.pack("<HIH", mash, mash_from, len(recs)) + b"".join(recs)
    write_8xv(out, "UTIN", data, archived=False)


if __name__ == "__main__":
    main()

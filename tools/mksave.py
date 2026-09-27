#!/usr/bin/env python3
"""Save files from the host (its -d directory: file0, undertale.ini...) as
the calculator's save AppVars (UTS + a hash of the name, as ce/src/main.c
save_name makes them), archived, to send along with a pack.

Usage: mksave.py <host save dir> <out dir>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cepack import write_8xv  # noqa: E402

DIGITS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"


def save_name(name):
    h = 2166136261
    for c in name.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    out = "UTS"
    for _ in range(5):
        out += DIGITS[h & 31]
        h >>= 5
    return out


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for f in sorted(src.iterdir()):
        if f.is_file():
            var = save_name(f.name)
            write_8xv(out / f"{var}.8xv", var, f.read_bytes(), archived=True)
            print(f"{f.name} -> {var}")


if __name__ == "__main__":
    main()

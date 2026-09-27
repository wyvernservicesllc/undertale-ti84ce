#!/usr/bin/env python3
"""Run a `make BENCH=1` build without windows and print its timings.

Usage: cebench.py <rom> <UNDERTLE.8xp> <UNDERTLE.map> <ms> <files...>
Each line is 1000 runs, so ms = 48 CPU cycles per run.
"""
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    rom, prog, mapf, ms = sys.argv[1:5]
    files = sys.argv[5:]
    mp = Path(mapf).read_text()
    addr = int(re.search(r"(0x[0-9a-f]+)\s+_bench_out\b", mp).group(1), 16)
    tmp = Path(tempfile.mkdtemp())
    cfg = {"rom": rom, "transfer_files": [str(Path(prog).resolve())] + [str(Path(f).resolve()) for f in files],
           "target": {"name": "UNDERTLE", "isASM": False},
           "sequence": ["action|launch", f"delay|{ms}", "hash|b", "hash|ram"],
           "hashes": {"b": {"description": "b", "start": hex(addr), "size": 256, "expected_CRCs": ["0"]},
                      "ram": {"description": "r", "start": "0xD00000", "size": 0x65800, "expected_CRCs": ["0"]}}}
    (tmp / "t.json").write_text(json.dumps(cfg))
    subprocess.run([str(Path.home() / "CEdev/bin/cemu-autotester"), "-d", str(tmp / "t.json")],
                   capture_output=True, cwd=tmp)
    b = next(tmp.glob("failure_hashb_*")).read_bytes()
    ram = next(tmp.glob("failure_hashram_*")).read_bytes()
    n = struct.unpack_from("<I", b, 0)[0]
    for i in range(min(n, 31)):
        lab, ms_ = struct.unpack_from("<II", b, 4 + 8 * i)
        o = lab - 0xD00000
        label = ram[o:ram.index(b"\0", o)].decode() if 0 <= o < len(ram) else hex(lab)
        print(f"{label:14s} {ms_:6d} ms/1000 = {ms_ * 48:7d} cycles")


if __name__ == "__main__":
    main()

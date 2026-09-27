#!/usr/bin/env python3
"""Runs a `make FTEST=1` build (float routine check) without windows.
Usage: ceftest.py <rom> <8xp> <map> <UTFT.8xv> <files...>"""
import json, re, struct, subprocess, sys, tempfile
from pathlib import Path
rom, prog, mapf, vec = sys.argv[1:5]
files = sys.argv[5:]
addr = int(re.search(r"(0x[0-9a-f]+)\s+_bench_out\b", Path(mapf).read_text()).group(1), 16)
tmp = Path(tempfile.mkdtemp())
cfg = {"rom": rom, "transfer_files": [str(Path(prog).resolve())] + [str(Path(f).resolve()) for f in files + [vec]],
       "target": {"name": "UNDERTLE", "isASM": False},
       "sequence": ["action|launch", "delay|60000", "hash|b"],
       "hashes": {"b": {"description": "b", "start": hex(addr), "size": 64, "expected_CRCs": ["0"]}}}
(tmp / "t.json").write_text(json.dumps(cfg))
subprocess.run([str(Path.home() / "CEdev/bin/cemu-autotester"), "-d", str(tmp / "t.json")], capture_output=True, cwd=tmp)
b = next(tmp.glob("failure_hashb_*")).read_bytes()
n, bad, x, y, s, d, cyc, p = struct.unpack_from("<8I", b)
print(f"{n} records, {bad} wrong, {cyc / max(n, 1) / 3:.0f} cycles per operation")
if bad:
    f = lambda u: struct.unpack("<f", struct.pack("<I", u))[0]
    print(f"first: x {x:08x} ({f(x)}) y {y:08x} ({f(y)}): got x+y {s:08x} ({f(s)}), x-y {d:08x} ({f(d)}), x*y {p:08x} ({f(p)})")

#!/usr/bin/env python3
"""Run a `make TEST=<frames>` calculator build with no windows and print
the hash of the game state it ends with (and how many frames it ran), to
compare builds that should compute the same thing. SHOT=<png> also saves
the screen it ends on.

Usage: cetest.py <rom> <UNDERTLE.8xp> <UNDERTLE.map> <ms> <files...>
"""
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    rom, prog, mapf, ms = sys.argv[1:5]
    files = sys.argv[5:]
    addr = int(re.search(r"(0x[0-9a-f]+)\s+_test_result\b", Path(mapf).read_text()).group(1), 16)
    tm = re.search(r"(0x[0-9a-f]+)\s+_test_time\b", Path(mapf).read_text())
    tmp = Path(tempfile.mkdtemp())
    cfg = {
        "rom": rom, "transfer_files": [str(Path(prog).resolve())] + [str(Path(f).resolve()) for f in files],
        "target": {"name": "UNDERTLE", "isASM": False},
        "sequence": ["action|launch", f"delay|{ms}", "hash|t", "hash|vram", "hash|pal"] + (["hash|tm"] if tm else []),
        "hashes": {"t": {"description": "t", "start": hex(addr), "size": 4 * (275 + (600 if os.environ.get("DETAIL_INST") else 0)), "expected_CRCs": ["0"]},
                   "vram": {"description": "vram", "start": "0xD40000", "size": 153600, "expected_CRCs": ["0"]},
                   "pal": {"description": "pal", "start": "lcdPalette", "size": 512, "expected_CRCs": ["0"]},
                   "tm": {"description": "tm", "start": tm.group(1) if tm else "0", "size": 24, "expected_CRCs": ["0"]}},
    }
    (tmp / "t.json").write_text(json.dumps(cfg))
    run = subprocess.run([str(Path.home() / "CEdev/bin/cemu-autotester"), "-d", str(tmp / "t.json")],
                         capture_output=True, cwd=tmp, text=True)
    if os.environ.get("CETEST_LOG") or not list(tmp.glob("failure_hasht_*_dump.bin")):
        print("\n".join(l for l in run.stdout.split("\n") if "transfer progress" not in l)[-3000:] + run.stderr[-500:])
    raw = next(tmp.glob("failure_hasht_*_dump.bin")).read_bytes()
    if os.environ.get("SHOT") and raw[:4] != b"TEST":
        subprocess.run([sys.executable, str(Path(__file__).parent / "vramshot.py"), str(next(tmp.glob("failure_hashvram_*_dump.bin"))), str(next(tmp.glob("failure_hashpal_*_dump.bin"))), os.environ["SHOT"]])
    magic, h, frames, stack, gh, n = struct.unpack_from("<6I", raw)
    if magic != 0x54534554:
        sys.exit("test_result not found")
    shot = os.environ.get("SHOT")
    if shot:
        subprocess.run([sys.executable, str(Path(__file__).parent / "vramshot.py"),
                        str(next(tmp.glob("failure_hashvram_*_dump.bin"))),
                        str(next(tmp.glob("failure_hashpal_*_dump.bin"))), shot], check=True)
    if os.environ.get("DETAIL"):
        print(f"globals {gh:08x}")
        for i in range(min(n, 128)):
            iid, ih = struct.unpack_from("<II", raw, 24 + 8 * i)
            print(f"inst {iid} {ih:08x}")
        nd = struct.unpack_from("<I", raw, 4 * 274)[0] if os.environ.get("DETAIL_INST") else 0
        for k in range(0, nd, 3):
            key, t, bits = struct.unpack_from("<3I", raw, 4 * (275 + k))
            print(f"var {key} {t} {bits:08x}")
    tmf = list(tmp.glob("failure_hashtm_*_dump.bin"))
    if tmf:
        _, ticks, _, nfr, _, cyc = struct.unpack_from("<6I", tmf[0].read_bytes())
        if nfr:
            print(f"timing: {nfr} frames in {ticks / 32768:.2f} s = {nfr * 32768 / max(ticks, 1):.2f} fps"
                  f" ({cyc / 48e6:.2f} s of CPU cycles)")
    errs, ecode = struct.unpack_from("<Ii", raw, 4 * 262)
    if errs:
        print(f"{errs} VM errors, first in code {ecode}: {raw[4 * 264:4 * 271].split(bytes(1))[0].decode(errors='replace')}")
    if os.environ.get("RENDER"):
        ni, ns, sc, ox, oy = struct.unpack_from("<IIfff", raw, 4 * 256)
        vf = struct.unpack_from("<I", raw, 4 * 261)[0]
        print(f"render: {ni} image draws ({ns} skipped), scale {sc} offset {ox},{oy}; views on {vf & 255}, "
              f"view0 enabled {vf >> 8 & 255} visible {vf >> 16 & 255}, room {vf >> 24}")
    avail, peak, total = struct.unpack_from("<III", raw, 4 * 271)
    print(f"RAM free at start {avail}, heap: peak {peak} of {total} bytes")
    print(f"hash {h:08x} after {frames} frames, {stack} bytes of stack used" if frames else "not finished")


if __name__ == "__main__":
    main()

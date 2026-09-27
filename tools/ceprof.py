#!/usr/bin/env python3
"""Profile the VM on an emulated calculator (make PROFILE=1 build), with no
windows: runs cemu-autotester, dumps the vm_prof table from RAM and prints
CPU cycles per opcode.

Usage: ceprof.py <rom> <UNDERTLE.8xp> <UNDERTLE.map> <ms to play> <files...>
"""
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    rom, prog, mapf, ms = sys.argv[1:5]
    files = sys.argv[5:]
    addr = int(re.search(r"(0x[0-9a-f]+)\s+_vm_prof\b", Path(mapf).read_text()).group(1), 16)
    ops = re.findall(r"OP_(\w+) = (\d+),", (ROOT / "vm" / "gen_ids.h").read_text())
    names = {int(n): o for o, n in ops}
    names.update({48: "[begin step events]", 49: "[alarms]", 50: "[keyboard events]", 51: "[step events]",
                  52: "[motion+collisions+end]", 53: "[end step events]", 54: "[draw frame]", 55: "[other runtime]",
                  56: "[asm fast path]"})
    tmp = Path(tempfile.mkdtemp())
    cfg = {
        "rom": rom, "transfer_files": [str(Path(prog).resolve())] + [str(Path(f).resolve()) for f in files],
        "target": {"name": "UNDERTLE", "isASM": False},
        "sequence": ["action|launch", f"delay|{ms}", "hash|prof"],
        "hashes": {"prof": {"description": "prof", "start": hex(addr), "size": 4 + 64 * 8 + 288 * 8, "expected_CRCs": ["0"]}},
    }
    (tmp / "p.json").write_text(json.dumps(cfg))
    run = subprocess.run([str(Path.home() / "CEdev/bin/cemu-autotester"), "-d", str(tmp / "p.json")],
                         capture_output=True, cwd=tmp, text=True)
    dump = next(tmp.glob("failure_hashprof_*_dump.bin")).read_bytes()
    if dump[:4] != b"PROF":
        print("\n".join(l for l in run.stdout.split("\n") if "transfer progress" not in l)[-3000:] + run.stderr[-500:])
        sys.exit("profile table not found (wrong map file?)")
    cyc = struct.unpack_from("<64I", dump, 4)
    cnt = struct.unpack_from("<64I", dump, 4 + 64 * 4)
    fcyc = struct.unpack_from("<288I", dump, 4 + 64 * 8)
    fcnt = struct.unpack_from("<288I", dump, 4 + 64 * 8 + 288 * 4)
    fnames = {int(n): f for f, n in re.findall(r"F_(\w+) = (\d+),", (ROOT / "vm" / "gen_ids.h").read_text())}
    fnames[255] = "[code cache misses]"
    fnames.update({242: "[native writer]", 243: "[writer draw_text_full]", 244: "[writer random+round]"})
    fnames.update({256: "[render: clear]", 257: "[render: images]", 258: "[render: text]", 259: "[render: shapes]",
                   260: "[render: palette+swap]", 261: "[img_row]", 262: "[rle_copy]", 263: "[scaled img row decode]", 264: "[glyph setup]", 265: "[scaled img total]", 271: "[img setup]", 266: "(big scaled)", 270: "[collision events]", 272: "[rotated fast]", 273: "[rotated source decode]", 274: "[1:1 mirrored/dithered]", 275: "[1:1 fast rows]", 276: "[img: tiny]", 277: "[scaled: dithered/lut]", 279: "(rot alloc failed)", 280: "(rot miss small)", 281: "(rot miss big)", 282: "(rot miss small blend)", 283: "(rot miss big blend)", 278: "[scaled: plain]", 267: "[motion]", 268: "[collisions]", 269: "[glyph scaled loop]"})
    fnames.update({245: "[push self var]", 246: "[push global]", 247: "[push array]", 248: "[push builtin var]",
                   249: "[push other scope]", 250: "[pop array]", 251: "[pop self var]", 252: "[pop builtin var]",
                   253: "[pop global]", 254: "[pop other scope]"})
    # the table counts ticks of the 32768 Hz clock (the CPU-cycle timer is
    # unreliable in the emulator): shown as seconds and 48 MHz cycles
    cyc = [c * 48e6 / 32768 for c in cyc]
    fcyc = [c * 48e6 / 32768 for c in fcyc]
    total = sum(cyc)
    print(f"total {total / 48e6:.2f} s of CPU")
    for i in sorted(range(64), key=lambda i: -cyc[i]):
        if cnt[i]:
            print(f"{str(names.get(i, i)):26s} {cyc[i] / 48e6:7.3f}s {100 * cyc[i] / total:5.1f}%  n={cnt[i]:8d}  {cyc[i] / cnt[i]:8.0f} cycles each")
    print("built-in functions:")
    for i in sorted(range(288), key=lambda i: -fcyc[i])[:40]:
        if fcnt[i]:
            print(f"  {str(fnames.get(i, i)):26s} {fcyc[i] / 48e6:7.3f}s  n={fcnt[i]:7d}  {fcyc[i] / fcnt[i]:8.0f} cycles each")
    print("renderer counters:")
    for i in range(256, 288):
        if fcnt[i]:
            print(f"  {str(fnames.get(i, i)):26s} {fcyc[i] / 48e6:7.3f}s  n={fcnt[i]:7d}")


if __name__ == "__main__":
    main()

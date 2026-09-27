#!/usr/bin/env python3
"""Add what the game really uses to the chapter lists of tools/chapters.py:
the host visits every room of a chapter (walking about) and fights every
battle group its code can start, with -t traces; the sprites, backgrounds,
fonts and code in them join the list. Static analysis misses what code
names by number (an object in a variable, say).

Usage: chaptrace.py <host binary> <full pack dir> <input.txt> <walk.txt>
                    <vm dir> <GML dir> <chapter lists dir> <work dir>
"""
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cepack import CodeBin  # noqa: E402


def run(args):
    host, pack, inp, env, trace, frames = args
    if trace.exists():
        return trace
    e = dict(os.environ, **env)
    save = trace.with_suffix(".save")
    subprocess.run([host, pack, str(frames), "-i", inp, "-m", "20", "-M", "700", "-o", "/tmp/chaptrace_out",
                    "-f", "1000000", "-d", str(save), "-t", str(trace)], env=e, capture_output=True, timeout=600)
    return trace


def main():
    host, pack, inp, walk, vmdir, gmldir, lists, work = sys.argv[1:9]
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    cb = CodeBin(Path(vmdir) / "code.bin")
    names = [n for (_, _, n) in cb.code]
    groups_of = {}
    for f in Path(gmldir).glob("*.gml"):
        gs = {int(g) for g in re.findall(r"global\.battlegroup\s*=\s*(\d+)", f.read_text(errors="replace"))}
        if gs and f.stem in names:
            groups_of[names.index(f.stem)] = gs
    rooms = json.loads((Path(vmdir).parent / "game" / "game.json").read_text())["rooms"] \
        if (Path(vmdir).parent / "game" / "game.json").exists() else None
    jobs, per_chapter = [], {}
    for lf in sorted(Path(lists).glob("*.txt")):
        lines = lf.read_text().split("\n")
        room_ids = [int(l.split()[1]) for l in lines if l.startswith("room ")]
        codes = {int(l.split()[1]) for l in lines if l.startswith("code ")}
        groups = set()
        for c in codes:
            groups |= groups_of.get(c, set())
        mine = []
        for r in room_ids:
            t = work / f"room{r}.tr"
            jobs.append((host, pack, walk, {"WARP": f"700:{rooms[r]['name']}"}, t, 1400))
            mine.append(t)
        for g in sorted(groups):
            t = work / f"group{g}.tr"
            jobs.append((host, pack, inp, {"BATTLE": f"700:{g}"}, t, 2200))
            mine.append(t)
        per_chapter[lf] = mine
    uniq = {j[4]: j for j in jobs}
    with cf.ThreadPoolExecutor(8) as ex:
        list(ex.map(run, uniq.values()))
    for lf, traces in per_chapter.items():
        have = set(lf.read_text().split("\n"))
        add = set()
        for t in traces:
            if t.exists():
                for l in t.read_text().split("\n"):
                    p = l.split()
                    if len(p) >= 2 and p[0] in ("sprite", "bg", "font", "code"):
                        add.add(f"{p[0]} {p[1]}")
        new = sorted(add - have)
        with open(lf, "a") as f:
            f.write("\n".join(new) + "\n")
        print(f"{lf.stem}: +{len(new)} from {len(traces)} traces")


if __name__ == "__main__":
    main()

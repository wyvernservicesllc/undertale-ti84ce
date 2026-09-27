#!/bin/bash
# Windowless calculator screenshots: runs a cemu-autotester config that dumps
# VRAM + palette at each "hash|vram" step, then writes shot<N>.png grids.
#   tools/ceshot.sh <config.json> <out.png>
set -e
CFG=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DIR=$(dirname "$CFG")
HERE=$(cd "$(dirname "$0")" && pwd)
rm -f "$DIR"/failure_hash*_dump.bin
~/CEdev/bin/cemu-autotester -d "$CFG" > "$DIR/autotester.log" 2>&1 || true
python3 - "$DIR" "$2" "$HERE/vramshot.py" <<'PY'
import glob, re, subprocess, sys
from PIL import Image
d, out, tool = sys.argv[1:4]
v = sorted(glob.glob(d + "/failure_hashvram_num*_dump.bin"), key=lambda f: int(re.search(r"num(\d+)", f).group(1)))
ims = []
for f in v:
    n = int(re.search(r"num(\d+)", f).group(1))
    pal = f"{d}/failure_hashpal_num{n + 1}_dump.bin"
    png = f"{d}/shot{n}.png"
    subprocess.run(["python3", tool, f, pal, png], check=True)
    ims.append(Image.open(png).crop((0, 0, 650, 240)))
o = Image.new("RGB", (650, 250 * max(1, len(ims))))
for k, im in enumerate(ims):
    o.paste(im, (0, k * 250))
o.save(out)
print(len(ims), "shots")
PY

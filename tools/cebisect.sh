#!/bin/bash
# Compare the calculator's state hash with the host's after <frame> frames
# of the same scripted play:  tools/cebisect.sh <frame> <pack dir> <UTIN.8xv> <input.txt> [mash from]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
N=$1 PACKDIR=$2 UTIN=$3 IN=$4 FROM=${5:-1850}
cd "$HERE/ce"
export PATH=$HOME/CEdev/bin:$PATH
make clean >/dev/null
make TEST=$N TEST_INST=$DETAIL_INST >/dev/null 2>&1
CE=$(python3 "$HERE/tools/cetest.py" /Users/admin/Downloads/ti-84ce.rom bin/UNDERTLE.8xp bin/UNDERTLE.map $((N * 800 + 20000)) "$HERE/libs/clibs.8xg" "$UTIN" "$PACKDIR"/*.8xv)
rm -rf /tmp/cebisect_save; HOST=$(cd "$HERE/host" && HASH=$N ./host "$PACKDIR" $((N + 1)) -i "$IN" -m 25 -M $FROM -o /tmp/cebisect_run -f 100000000 -d /tmp/cebisect_save 2>/dev/null | grep -E "^(hash|inst|globals|var)")
echo "frame $N: calc: $CE | host: $HOST"

#!/bin/bash
# Puts together what goes on a calculator: the program, the C libraries
# and one folder of AppVars per chapter pack, plus the README.
#
#   tools/release.sh <folder with the cp_<n>_<name> pack builds> [out dir]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
PACKS=$1 OUT=${2:-$HERE/release}
if [ -z "$PACKS" ]; then
    echo "usage: $0 <pack builds folder> [out dir]" >&2
    exit 1
fi
rm -rf "$OUT"
mkdir -p "$OUT/program"
(cd "$HERE/ce" && make clean >/dev/null && make >/dev/null)
cp "$HERE"/ce/bin/UNDERTLE.8xp "$HERE"/ce/bin/UNDERTLE.8xp.*.8xv "$HERE/libs/clibs.8xg" "$OUT/program/"
cp "$HERE/README.md" "$OUT/"
for d in "$PACKS"/cp_*/; do
    d=${d%/}
    name=${d##*/cp_}
    n=${name%%_*}
    mkdir -p "$OUT/pack$(printf %02d "$n")_${name#*_}"
    cp "$d"/*.8xv "$OUT/pack$(printf %02d "$n")_${name#*_}/"
done
du -sh "$OUT"/* | sed "s|$OUT/||"

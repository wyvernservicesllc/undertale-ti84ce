#!/bin/bash
# Dump what the port needs from Undertale's data.win with UndertaleModTool's
# command-line build (UTMT CLI, native on macOS; no Wine needed for this step).
#
#   tools/extract_assets.sh <path to data.win> <UndertaleModCli> <output dir>
#
# Writes <output dir>/Sprites, CodeEntries, fonts and rooms for
# convert_assets.py.
set -e
DATA=$1 CLI=$2 OUT=$3
if [ -z "$OUT" ]; then
    echo "usage: $0 <data.win> <UndertaleModCli> <output dir>" >&2
    exit 1
fi
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/fonts" "$OUT/rooms"
OUT=$(cd "$OUT" && pwd)

"$CLI" dump "$DATA" -o "$OUT" -c gml_Script_textdata_en -c gml_Script_scr_namingscreen_check --sprites < /dev/null

# The CLI drops into a prompt after running its scripts, so stop it once
# everything is written.
rm -f "$OUT/rooms/done"
UT_FONT_DIR="$OUT/fonts" UT_ROOM_DIR="$OUT/rooms" "$CLI" load "$DATA" \
    -s "$HERE/export_fonts.csx" -s "$HERE/export_rooms.csx" < /dev/null &
PID=$!
for _ in $(seq 1 100); do
    [ -f "$OUT/fonts/glyphs_fnt_small.csv" ] && [ -f "$OUT/rooms/done" ] && break
    sleep 2
done
sleep 2
kill $PID 2>/dev/null || true
ls "$OUT/fonts/glyphs_fnt_maintext.csv" "$OUT/rooms/done" > /dev/null

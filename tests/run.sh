#!/bin/sh
# Headless tests: no windows. Each prints the game-state hash it ends on;
# the Mac build (host/) and the calculator build (ce/, in CEmu's
# autotester) must agree with each other and with the expected hash.
#
#   tests/run.sh host      Mac build: opening and Toriel battle
#   tests/run.sh ce        calculator build: the same two, in the emulator
#   tests/run.sh asgore    calculator build: Asgore battle, frame timing
#   tests/run.sh profile   calculator build: Asgore battle, CPU profile
#   tests/run.sh bench     calculator build: microbenchmarks
#
# Needs: CEdev v15 in ~/CEdev (with cemu-autotester in ~/CEdev/bin), a
# TI-84 Plus CE ROM image (ROM=path, default ~/Downloads/ti-84ce.rom),
# Python 3 with Pillow (screenshots only).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=${ROM:-$HOME/Downloads/ti-84ce.rom}
TMP=${TMPDIR:-/tmp}/ut_tests
export PATH=$HOME/CEdev/bin:$PATH
mkdir -p "$TMP"
T=$ROOT/tests
RUINS=$ROOT/release/pack01_ruins
CASTLE=$ROOT/release/pack09_castle

host_build() { (cd "$ROOT/host" && make host >/dev/null); }

# the opening, mashing Z from frame 1850 (host -m/-M = mkinput's last two)
intro_input() { python3 "$ROOT/tools/mkinput.py" "$T/inputs/intro.txt" "$TMP/in_intro/UTIN.8xv" 25 1850; }

case "$1" in
host)
    host_build
    cd "$ROOT/host"
    rm -rf "$TMP/save"
    echo "opening, frame 600 (expect e9d6d8d6):"
    HASH=600 ./host "$ROOT/data/chapters/1_ruins" 601 -i "$T/inputs/intro.txt" -m 25 -M 1850 \
        -o "$TMP/out" -f 100000000 -d "$TMP/save" 2>&1 | grep hash
    rm -rf "$TMP/save"
    echo "Toriel battle from frame 700, frame 1000 (expect 0366db9c):"
    HASH=1000 BATTLE=700:22 ./host "$ROOT/data/chapters/1_ruins" 1001 -i "$T/inputs/intro.txt" -m 25 -M 1850 \
        -o "$TMP/out" -f 100000000 -d "$TMP/save" 2>&1 | grep hash
    ;;
ce)
    mkdir -p "$TMP/in_intro" "$TMP/in_toriel"
    intro_input
    (cat "$T/inputs/intro.txt"; echo "700 battle 22") > "$TMP/toriel.txt"
    python3 "$ROOT/tools/mkinput.py" "$TMP/toriel.txt" "$TMP/in_toriel/UTIN.8xv" 25 1850
    cd "$ROOT/ce"
    make clean >/dev/null && make TEST=600 >/dev/null
    echo "opening, frame 600 (expect e9d6d8d6):"
    python3 "$ROOT/tools/cetest.py" "$ROM" bin/UNDERTLE.8xp bin/UNDERTLE.map 600000 bin/UTFAR.8xv \
        "$ROOT/libs/clibs.8xg" "$TMP/in_intro/UTIN.8xv" "$RUINS"/*.8xv
    make clean >/dev/null && make TEST=1000 >/dev/null
    echo "Toriel battle, frame 1000 (expect 0366db9c):"
    python3 "$ROOT/tools/cetest.py" "$ROM" bin/UNDERTLE.8xp bin/UNDERTLE.map 900000 bin/UTFAR.8xv \
        "$ROOT/libs/clibs.8xg" "$TMP/in_toriel/UTIN.8xv" "$RUINS"/*.8xv
    ;;
asgore)
    mkdir -p "$TMP/in_asgore"
    python3 "$ROOT/tools/mkinput.py" "$T/inputs/boss_6_castle.txt" "$TMP/in_asgore/UTIN.8xv"
    cd "$ROOT/ce"
    make clean >/dev/null && make TEST=450 TIME_FROM=350 >/dev/null
    echo "Asgore, frames 350-450 (expect hash bf5d40e2):"
    SHOT=${SHOT:-$TMP/asgore.png} python3 "$ROOT/tools/cetest.py" "$ROM" bin/UNDERTLE.8xp bin/UNDERTLE.map \
        900000 bin/UTFAR.8xv "$ROOT/libs/clibs.8xg" "$TMP/in_asgore/UTIN.8xv" "$T/saves/castle"/*.8xv \
        "$CASTLE"/*.8xv
    echo "screenshot: ${SHOT:-$TMP/asgore.png}"
    ;;
profile)
    mkdir -p "$TMP/in_asgore"
    python3 "$ROOT/tools/mkinput.py" "$T/inputs/boss_6_castle.txt" "$TMP/in_asgore/UTIN.8xv"
    cd "$ROOT/ce"
    make clean >/dev/null && make PROFILE=1 TEST=450 PROF_FROM=350 >/dev/null
    python3 "$ROOT/tools/ceprof.py" "$ROM" bin/UNDERTLE.8xp bin/UNDERTLE.map 900000 bin/UTFAR.8xv \
        "$ROOT/libs/clibs.8xg" "$TMP/in_asgore/UTIN.8xv" "$T/saves/castle"/*.8xv "$CASTLE"/*.8xv |
        grep -v "Will send"
    ;;
bench)
    cd "$ROOT/ce"
    make clean >/dev/null && make BENCH=1 >/dev/null
    python3 "$ROOT/tools/cebench.py" "$ROM" bin/UNDERTLE.8xp bin/UNDERTLE.map 120000 bin/UTFAR.8xv \
        "$ROOT/libs/clibs.8xg" "$RUINS"/*.8xv
    ;;
*)
    sed -n '2,17p' "$0"
    exit 1
    ;;
esac

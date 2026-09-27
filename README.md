# Undertale for the TI-84 Plus CE

The whole of Undertale on a TI-84 Plus CE. It runs the game's real code, not
a remake: the GameMaker bytecode in your copy's `data.win` is compiled into a
compact virtual machine (`vm/`) that runs on the calculator (`ce/`) and, for
testing, on a Mac (`host/`). Sprites, backgrounds and fonts are the original
pixels at the original 320x240. Colors are mapped to a 256-color palette.

## What works

- The whole game's logic, all 336 rooms and all 107 battle groups, every boss
  included, run without VM errors on the Mac build.
- On the (emulated) calculator, every one of the 12 packs loads from a save
  and runs its boss: Toriel, Papyrus, Undyne, Mettaton, Mettaton EX, Asgore,
  Omega Flowey, the True Lab amalgamates and Asriel. These were tested for a
  few hundred frames each, not played start to finish. The opening through
  Flowey's battle matches the Mac build frame for frame.

Limits on the calculator:

- **Speed.** A 48 MHz chip with no floating-point hardware runs Undertale's
  code at about 2 frames per second in battles and somewhat more when
  walking. The game advances one step per frame, so this is slow motion,
  not skipped action: every bullet is still there to dodge.
- **No sound.** The calculator has no speaker.
- **Effects.** Transparency is dithered, and anything below 25% opacity is
  left out. Rotated text is drawn upright. Some very large animations (Omega
  Flowey's) keep every 2nd to 5th frame.
- **Crowds of particles.** Monster dust and similar purely visual particles
  are capped at 24 of each at once, to fit the calculator's RAM.

## Chapter packs

The whole game is about 10 MB of data, and a calculator holds about 3 MB, so
it comes in 12 packs. Keep one pack on the calculator at a time:

| Pack | Covers |
| --- | --- |
| 1 | Intro, Ruins, Toriel, first Snowdin rooms |
| 2 | Snowdin forest |
| 3 | Snowdin town, Papyrus |
| 4 | Waterfall (first half) |
| 5 | Waterfall (second half) |
| 6 | Waterfall's end, Undyne, Hotland (first half) |
| 7 | Hotland (second half) |
| 8 | MTT Resort, Muffet, the Core, Mettaton EX |
| 9 | New Home, Asgore, neutral ending |
| 10 | Omega Flowey |
| 11 | True Lab |
| 12 | True ending, Asriel, credits |

When you reach the end of a pack, the game saves where you are and shows
"End of this pack". Delete that pack's `UTD` AppVars, send the next pack, run
the game and pick Continue. You come in at the door you walked through. Your
saves (`UTS...` AppVars) stay on the calculator.

After Asgore, the neutral route goes to pack 10 (Omega Flowey), then back to
pack 9 for the ending. The true route goes from pack 6 (Alphys's lab) to
pack 11, then packs 9 and 12.

## Installing

Send these with TI Connect CE (or TiLP):

1. `clibs.8xg`, the CE C libraries (skip if already installed).
2. The program: `UNDERTLE.8xp` and `UTFAR.8xv` (from `ce/bin` after `make`).
   The build in `release/program` is older: `UNDERTLE.8xp` with
   `UNDERTLE.8xp.0.8xv` and `UNDERTLE.8xp.1.8xv`, and no `UTFAR`.
3. Every `UTDnnn.8xv` of the pack you are playing, to Archive.

A pack takes up to about 2.35 MB of archive. A calculator straight out of the
box may need some of its preinstalled apps deleted to make room
(`2nd` `mem` `2` `A:Apps`).

Run `prgmUNDERTLE` from the home screen or a shell such as Cesium. On OS 5.5
and later, use [arTIfiCE](https://yvantt.github.io/arTIfiCE/) to run native
programs. The program needs about 8 KB of free RAM besides itself; clear RAM
variables if it says there is not enough.

## Controls

| Key | Undertale key |
| --- | --- |
| Arrows | Arrows |
| `2nd` | Z (confirm) |
| `enter` | Enter |
| `alpha` | X (cancel) |
| `del` | Shift |
| `mode` | C (menu) |
| `clear` | Esc |
| `graph` | Quit (saves are kept) |

## Building it yourself

You need your own copy of Undertale (Windows `data.win`), the CE C toolchain
(CEdev) and UndertaleModTool's command-line build. Then:

    tools/extract_assets.sh <data.win> <UndertaleModCli> <export dir>
    python3 tools/vmc.py <export dir> <vm dir>          # bytecode -> VM code
    python3 tools/gfxpack.py ...                         # graphics
    python3 tools/chapters.py <export dir> <vm dir> <GML dir> <lists dir>
    python3 tools/chaptrace.py host/host <full pack> ... # add traced assets
    python3 tools/cepack.py <vm dir> <export dir> <pack dir> \
        --sprites <list> --rooms <list> --code <list>    # one pack per list
    cd ce && make                                        # the program

`make` builds the program (`ce/bin/UNDERTLE.8xp`) and the code that runs
from the archive (`ce/bin/UTFAR.8xv`, see `tools/farcode.py`). The two
belong together: the program refuses a `UTFAR` from another build.

## Playing on a Mac

`host/` builds the same VM for macOS. `make play` builds a windowed version
with sound (SDL2 and SDL2_mixer from Homebrew):

    cd host && make play && ./play ../data/pack 0 -d save_play

`data/pack` is the full-game pack. `data/sounds` holds the music (the `.ogg`
files next to `data.win`) and the sound effects, which
`tools/export_sounds.csx` extracts from `data.win`.

## Status (2026-09-27)

The whole game runs, but slowly. Measured in CEmu (the emulated TI-84 Plus
CE, 48 MHz, flash revision M):

| Scene | fps |
| --- | --- |
| Opening (text, `tests/run.sh ce`) | 3.2 to 4.2 |
| Asgore battle (`tests/run.sh asgore`) | 0.72 (was 0.24 before this round) |

What changed in this round:

- **Code in the archive** (`tools/farcode.py`, `ce/src/farhelp.s`). The
  program must fit in about 150 KB of RAM. The files in `FAR_SOURCES`
  (`ce/makefile`) are compiled, made position-independent and stored in the
  AppVar `UTFAR`, which runs where the OS put it in flash. That freed about
  43 KB of RAM. Flash has an 8 KB cache: a hit costs 2 to 3 cycles per byte
  (RAM costs 4), a miss about 197. Large, branchy functions thrash it, so
  they are split up. The built-ins are now one function each, dispatched
  through a table (`vm/builtins.c`).
- **Fast trigonometry** (`vm/gmmath.c`, tables from `tools/gen_gmmath.py`):
  integer, table-driven, the same on the Mac and the calculator. `sin` fell
  from about 118,000 cycles to 8,000.
- **Renderer inner loops in assembly** (`ce/src/blit.s`): dithered spans,
  scaled and mirrored rows with blend, and rotated images. Rotated sources
  are cached in 256-byte-row arenas (`render_ce.c`, `rot_source`).
- **Measuring**: a frame timer (`make TIME_FROM=`), a profiler on the
  32 kHz clock (the CPU-cycle timer is unreliable in the emulator) and
  per-path renderer counters (`tools/ceprof.py`).

## To do

Target: 20 fps where possible, and the game at its real speed everywhere.
In order:

1. **Check the last change.** `malloc_aligned` (`ce/src/heap.c`) and the
   rotation arenas are built but untested. Run `tests/run.sh ce` (expect the
   same hashes) and `tests/run.sh asgore` (fps, and a screenshot for errors).
   In the last profile, `[rotated fast]` cost about 4 million cycles a draw
   because the arena allocation failed every frame; with `malloc_aligned` it
   should hit the cache.
2. **Redraw only what changed.** Today every frame clears and redraws all
   320x240 pixels (clearing alone is 250,000 cycles). Battles are mostly
   still black. Probably the largest single gain in battles.
3. **Compile the game's scripts to native code.** They are still
   interpreted from bytecode (about 35 to 40% of a battle frame). Plan:
   - a tool that turns each code entry's VM bytecode into C calling the
     runtime (constant scopes, built-in variables as fields, built-ins called
     directly);
   - check it on the Mac first: the hashes must match the interpreter's;
   - on the calculator, compile it as far code into the chapter packs, one
     function inside one 64 KB window. This needs more than one far-code
     AppVar (`UTFAR` is at 53 KB of about 65) and native pointers in the
     pack's code table;
   - watch the flash cache: keep each frame's hot code together.
4. **Keep the game at real speed:** run every logic step at 30 per second
   and skip drawing when behind. Test builds never skip.
5. **The VM's remaining costs:** an instance variable lookup costs about 4,000
   cycles (binary search), and a built-in call about 10,000 in overhead.
   Consider per-object variable slots.
6. **More renderer work.** In the Asgore profile, after rotation: plain
   scaled images (`[scaled: plain]`), per-image setup (`[img setup]`, about
   90,000 cycles, float math), the dialogue writer (`[native writer]`) and
   text.
7. **Measure more scenes:** an ordinary battle, walking in a room, Omega
   Flowey, Asriel. Add each to `tests/run.sh`.
8. **Rebuild `release/`** with the new program (`UNDERTLE.8xp` + `UTFAR.8xv`)
   and test every pack's boss again (inputs in `tests/inputs/boss_*.txt`;
   see below).
9. Real hardware: nothing has run on a real calculator yet.

## Testing headless

Nothing opens a window. `tests/run.sh`:

    tests/run.sh host      # Mac build: opening and Toriel battle hashes
    tests/run.sh ce        # the same on the emulated calculator
    tests/run.sh asgore    # Asgore battle: fps and a screenshot
    tests/run.sh profile   # Asgore battle: where the CPU time goes
    tests/run.sh bench     # microbenchmarks (cycles per operation)

Expected hashes:

| Test | Hash |
| --- | --- |
| Opening, frame 600 | e9d6d8d6 |
| Toriel battle, frame 1000 | 0366db9c |
| Asgore battle, frame 450 | bf5d40e2 |

Needs:

- CEdev v15 in `~/CEdev`, with `cemu-autotester` in `~/CEdev/bin`.
  `tools/farcode.py` also uses `~/CEdev`.
- A TI-84 Plus CE ROM image, not in this repository. It defaults to
  `~/Downloads/ti-84ce.rom`; set `ROM=...` to use another path.
- Python 3 with Pillow.
- For `make play` on the Mac: SDL2 and SDL2_mixer.

Emulated time runs about 30 times faster than real time: an Asgore run takes
a few minutes.

How it works:

- A `make TEST=<frames>` build plays scripted input from the AppVar `UTIN`
  (`tools/mkinput.py`: "frame key hold" lines, and "frame battle N" or
  "frame warp ROOM" commands). It stops after that many frames with a hash
  of the game state in RAM, which `tools/cetest.py` reads with the
  autotester. `SHOT=file.png` also saves the screen.
- The Mac build takes the same input text (`host/host <pack> <frames> -i`)
  and reads the packs' `.bin` files (`data/chapters/*`). `data/pack` is the
  whole game in one pack.
- `tests/inputs/boss_*.txt` start each boss battle ("300 battle N"). Their
  file names use an older 9-pack numbering; the battle numbers are current.
  Pack 1's is played from a new game and includes the opening. The others
  load a save; only the castle's save is here (`tests/saves/castle`).
- `tests/saves/castle` is a calculator save at New Home, for the Asgore
  test.
- The profiler's times are 32 kHz clock ticks scaled to 48 MHz cycles. The
  per-path renderer counters are listed at the end.

For crashes and hot spots there is a debugging patch for CEmu's core,
`tools/cemu_trace.patch`, against https://github.com/CE-Programming/CEmu at
fb10bfe. Build CEmu's `core` and `tests/autotester` with it. It records
every jump with its cycle count, and when the CPU reaches the address in
`PC_MARK` (hex, e.g. a function from `ce/bin/UNDERTLE.map`) it writes the
last million or so to `/tmp/pc_ring.txt` (source, target, cycles).

## Layout

- `vm/`: the GameMaker VM, runtime, built-ins and drawing. Shared by both
  builds.
- `ce/`: the calculator build. `src/` holds the platform layer and the
  assembly; `make` builds it.
- `host/`: the Mac build, headless (`host`) or windowed with sound (`play`).
- `tools/`: data extraction, the bytecode compiler (`vmc.py`), pack builder
  (`cepack.py`), far code (`farcode.py`) and test tools.
- `data/`: the extracted game (`pack`, `sounds`, `chapters`). `release/`: the
  last calculator release.
- `tests/`: headless test inputs, saves and `run.sh`.
- Top-level `src/`, `makefile`: the first prototype (opening only, by hand),
  replaced by the VM. Kept for reference.

## Testing tools

- `host/host <pack> <frames> [-i input] [-m every] [-M from] [-f frames] [-d save dir] [-t trace]`
  runs headless. `HASH=`, `WARP=frame:room`, `BATTLE=frame:group`,
  `SAVEAT=frame:script`, `LIST=` and `MEM=1` help compare and debug.
- `tools/cetest.py` runs a `make TEST=<frames>` build in CEmu's headless
  autotester and reports the state hash, VM errors and memory.
- `tools/ceprof.py` profiles a `make PROFILE=1` build. `tools/cebench.py`
  times the basic operations.

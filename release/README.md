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
2. `UNDERTLE.8xp`, `UNDERTLE.8xp.0.8xv` and `UNDERTLE.8xp.1.8xv`, the program.
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

## Playing on a Mac

`host/` builds the same VM for macOS. `make play` builds a windowed version
with sound (SDL2 and SDL2_mixer from Homebrew):

    cd host && make play && ./play ../data/pack 0 -d save_play

`data/pack` is the full-game pack. `data/sounds` holds the music (the `.ogg`
files next to `data.win`) and the sound effects, which
`tools/export_sounds.csx` extracts from `data.win`.

## Testing tools

- `host/host <pack> <frames> [-i input] [-m every] [-M from] [-f frames] [-d save dir] [-t trace]`
  runs headless. `HASH=`, `WARP=frame:room`, `BATTLE=frame:group`,
  `SAVEAT=frame:script`, `LIST=` and `MEM=1` help compare and debug.
- `tools/cetest.py` runs a `make TEST=<frames>` build in CEmu's headless
  autotester and reports the state hash, VM errors and memory.
- `tools/ceprof.py` profiles a `make PROFILE=1` build. `tools/cebench.py`
  times the basic operations.

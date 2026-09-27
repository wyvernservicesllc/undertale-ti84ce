#!/usr/bin/env python3
"""Drive CEmu's headless core (cemu-headless) with no windows.

  cehl.py setup <rom> <state out> <file.8x*>...
      boot the ROM, send the files to archive, save an emulator state
  cehl.py run <state> <script> <out dir>
      load a state and run a script, one command per line:
        run <ms>              advance emulated time
        key <name> [ms]       press a key (autotester names: 2nd, enter, up...)
        launch <PROGRAM>      [clear], [prgm], its letters, [enter]
        shot <name>           save <out dir>/<name>.png
        shots <name> <n> <ms> n screenshots, <ms> apart
        send <file>           send a file to archive

CEMU_HEADLESS points to the cemu-headless binary.
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

HEADLESS = os.environ.get("CEMU_HEADLESS", "cemu-headless")

LETTERS = {
    "A": "math", "B": "apps", "C": "prgm", "D": "inv", "E": "sin", "F": "cos", "G": "tan",
    "H": "pow", "I": "sq", "J": "comma", "K": "lpar", "L": "rpar", "M": "div", "N": "log",
    "O": "7", "P": "8", "Q": "9", "R": "mul", "S": "ln", "T": "4", "U": "5", "V": "6",
    "W": "sub", "X": "sto", "Y": "1", "Z": "2", "0": "0",
}


class Emu:
    def __init__(self, *args):
        self.p = subprocess.Popen([HEADLESS, *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("cemu-headless exited")
            if "CEMU_HEADLESS_READY" in line:
                break

    def cmd(self, c):
        self.p.stdin.write(c + "\n")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("cemu-headless exited")
            if line.startswith("OK") or line.startswith("ERR"):
                if line.startswith("ERR"):
                    print(f"{c}: {line.strip()}", file=sys.stderr)
                return line.strip()

    def shot(self, path):
        bmp = str(path) + ".bmp"
        self.cmd(f"screenshot {bmp}")
        Image.open(bmp).convert("RGB").save(path)
        os.unlink(bmp)

    def launch(self, name):
        self.cmd("key clear")
        self.cmd("run 200")
        self.cmd("key prgm")
        self.cmd("run 300")
        for ch in name.upper():
            self.cmd(f"key {LETTERS[ch]}")
            self.cmd("run 100")
        self.cmd("key enter")

    def quit(self):
        try:
            self.cmd("quit")
        except RuntimeError:
            pass
        self.p.wait()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == "setup":
        rom, state, files = sys.argv[2], sys.argv[3], sys.argv[4:]
        emu = Emu("--rom", rom)
        emu.cmd("run 6000")
        for f in files:
            r = emu.cmd(f"send-file archive {f}")
            emu.cmd("run 200")
            print(Path(f).name, r, flush=True)
        emu.cmd("run 2000")
        emu.cmd(f"save-state {state}")
        emu.quit()
    elif sys.argv[1] == "run":
        state, script, out = sys.argv[2], sys.argv[3], Path(sys.argv[4])
        out.mkdir(parents=True, exist_ok=True)
        emu = Emu("--image", state)
        for line in Path(script).read_text().split("\n"):
            p = line.split("#")[0].split()
            if not p:
                continue
            if p[0] == "run":
                emu.cmd(f"run {p[1]}")
            elif p[0] == "key":
                emu.cmd(" ".join(["key"] + p[1:]))
            elif p[0] == "launch":
                emu.launch(p[1])
            elif p[0] == "shot":
                emu.shot(out / f"{p[1]}.png")
            elif p[0] == "shots":
                for i in range(int(p[2])):
                    emu.shot(out / f"{p[1]}{i:03d}.png")
                    emu.cmd(f"run {p[3]}")
            elif p[0] == "send":
                emu.cmd(f"send-file archive {p[1]}")
        emu.quit()


if __name__ == "__main__":
    main()

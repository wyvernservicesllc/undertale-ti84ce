#!/usr/bin/env python3
"""Build a calculator data pack from the VM build (tools/vmc.py and
tools/gfxpack.py output).

Usage: python3 tools/cepack.py <vm dir> <export dir> <out dir> [options]
  --sprites FILE   only include these graphics: a trace file from the host
                   ("sprite N", "bg N", "font N" lines); default: all
  --name PREFIX    AppVar name prefix (default UTD), names are PREFIX000...
  --code FILE      only include these code entries ("code N" lines) and the
                   text they use; for small test packs. Other entries are
                   empty and do nothing when called.
  --rooms FILE     only include these rooms' instances and tiles ("room N"
                   lines, e.g. the same trace); other rooms are marked as
                   not in this pack (room flag 0x80)

The pack is one "far" address space of up to 64 KB windows; window i is
stored as AppVar PREFIX<i> (one per 64 KB flash sector). A far address is
24 bits: window = addr >> 16, offset = addr & 0xffff. Nothing crosses a
window; see class Far for how arrays are laid out.

Writes <out dir>/PREFIX000.bin ... (raw windows, for the host) and
PREFIX000.8xv ... (archived AppVars for the CE).

Layout (see vm/pack.h for the reader):
  header (window 0, 256 bytes): "UTP1", then u32 fields, see HEADER below
  code:   entry table (8 B: u16 first block, u16 nblocks, u16 nlocals,
          u16 pad), block table (4 B: u24 far address of the block blob),
          blocks of 1024 bytes before compression
  blob:   u8 method; 1: zx0 data; 0: u16 length + raw bytes
  text:   index (4 B: u16 block, u16 offset), block table (4 B far),
          key hashes (4 B, sorted) + ids (2 B) for run-time key lookups
  strings, tables and graphics as described in HEADER.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

WDATA = 65280        # bytes per window (AppVars hold at most 65505)
CHUNK = 32768        # big arrays are cut into chunks of this size
BLOCK = 1024  # keep in sync with CODE_BLOCK in vm/vm.h
TEXT_BLOCK = 2048
ROOT = Path(__file__).resolve().parent.parent
CONVBIN = Path.home() / "CEdev" / "bin" / "convbin"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vmc  # noqa: E402  (opcode list)

OP = vmc.OP

# header fields, in order (u32 each, after the magic)
HEADER = [
    "nwindows", "ncode", "code_entries", "nblocks", "code_blocks",
    "nscripts", "scripts", "nglobals", "global_names", "ninstvars",
    "ntext", "text_index", "ntextblocks", "text_blocks", "ntexthashes", "text_hashes", "text_ids",
    "nobjects", "objects", "events", "nsprites", "sprites", "frames",
    "nbgs", "bgs", "nfonts", "fonts", "glyphs", "npaths", "paths", "points",
    "nrooms", "rooms", "room_bgs", "room_views", "insts", "tiles", "tile_depths",
    "nroomorder", "roomorder", "palette",
]


class Far:
    """The pack's address space: 64 KB windows, each an AppVar of up to
    WDATA bytes. A far address is (window << 16) | offset.

    Items never cross a window. Arrays up to 32 KB go anywhere; bigger ones
    are cut into 32 KB chunks at the start of consecutive windows, so the
    runtime finds byte L of an array at base + ((L >> 15) << 16) + (L & 0x7fff).
    """

    def __init__(self, reserve=0):
        self.wins = [bytearray(256)]  # window 0 starts with the header
        # Windows 1 to reserve: their first CHUNK bytes wait for big arrays'
        # chunks (placed last, see main), everything else fills in after.
        self.res_next, self.res_end = 1, 1 + reserve
        self.chunks = 0
        for _ in range(reserve):
            self.wins.append(bytearray())

    def _new_window(self):
        self.wins.append(bytearray())
        return len(self.wins) - 1

    def _start(self, w):
        """Where ordinary data may begin in window w."""
        return CHUNK if self.res_next <= w < self.res_end else 0

    def put(self, data, align=1):
        """Place data in the first window with room; returns its far address."""
        data = bytes(data)
        if len(data) > WDATA:
            raise ValueError("item larger than a window")
        for w, buf in enumerate(self.wins):
            off = (max(len(buf), self._start(w)) + align - 1) // align * align
            if off + len(data) <= WDATA:
                buf += bytes(off - len(buf)) + data
                return w << 16 | off
        w = self._new_window()
        self.wins[w] += data
        return w << 16

    def put_array(self, records, size):
        """Records padded to `size` (a power of two)."""
        assert size & (size - 1) == 0
        out = bytearray()
        for r in records:
            if len(r) > size:
                raise ValueError(f"record of {len(r)} bytes > {size}")
            out += r + bytes(size - len(r))
        if len(out) <= CHUNK:
            return self.put(out, align=size) if out else self.put(b"", align=size)
        first = None
        n = (len(out) + CHUNK - 1) // CHUNK
        self.chunks += n
        use_res = self.res_next + n <= self.res_end
        for k in range(0, len(out), CHUNK):
            if use_res:
                w = self.res_next
                self.res_next += 1
                buf = self.wins[w]
                piece = out[k:k + CHUNK]
                if len(buf) < CHUNK:
                    buf += bytes(CHUNK - len(buf))
                buf[0:len(piece)] = piece
            else:
                w = self._new_window()
                self.wins[w] += out[k:k + CHUNK]
            if first is None:
                first = w
            elif w != first + k // CHUNK:
                raise RuntimeError("array chunks not consecutive")
        return first << 16

    def windows(self):
        return [bytes(b) for b in self.wins]

    @property
    def size(self):
        return sum(len(b) for b in self.wins)


def write_8xv(path, name, data, archived=True):
    """A TI-84 Plus CE AppVar file (.8xv)."""
    body = struct.pack("<H", len(data)) + data
    name_b = name.encode()[:8].ljust(8, b"\0")
    entry = struct.pack("<HHB", 0x0D, len(body), 0x15) + name_b + bytes([0, 0x80 if archived else 0]) \
        + struct.pack("<H", len(body)) + body
    comment = b"Undertale data pack".ljust(42, b"\0")
    out = b"**TI83F*\x1a\x0a\x00" + comment + struct.pack("<H", len(entry)) + entry
    out += struct.pack("<H", sum(entry) & 0xFFFF)
    Path(path).write_bytes(out)


def u24(v):
    return struct.pack("<I", v)[:3]


def blob_worker(data):
    """blob() in a pool worker, with its own temporary directory."""
    tmp = tempfile.mkdtemp()
    try:
        return blob(data, tmp)
    finally:
        for f in Path(tmp).iterdir():
            f.unlink()
        os.rmdir(tmp)


def blobs(datas):
    """Compress many blobs in parallel (convbin is one process per blob)."""
    from concurrent.futures import ProcessPoolExecutor
    with ProcessPoolExecutor() as ex:
        return list(ex.map(blob_worker, datas, chunksize=16))


def blob(data, tmp):
    """A compressed blob: u8 method, then 1 = zx0 data, or 0 = u16 length
    and the raw bytes (when zx0 doesn't make it smaller)."""
    try:
        c = zx0(data, tmp)
        if len(c) < len(data):
            return b"\1" + c
    except RuntimeError:
        pass
    return b"\0" + struct.pack("<H", len(data)) + data


def zx0(data, tmp):
    """Compress with convbin's zx0."""
    src = Path(tmp) / "in.bin"
    dst = Path(tmp) / "out.bin"
    if len(data) < 16:
        data = data + bytes(16 - len(data))  # convbin rejects tiny inputs
    src.write_bytes(data)
    r = subprocess.run([str(CONVBIN), "-j", "bin", "-k", "bin", "-c", "zx0", "-i", str(src), "-o", str(dst)],
                       capture_output=True)
    if r.returncode:
        raise RuntimeError(r.stdout.decode() + r.stderr.decode())
    return dst.read_bytes()


def fnv(s):
    h = 2166136261
    for b in s:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# ---------------------------------------------------------------- inputs

class CodeBin:
    def __init__(self, path):
        d = self.d = path.read_bytes()
        f = struct.unpack_from("<12I", d, 4)
        (self.nstrings, self.ncode, self.nscripts, self.nglobals, self.ninstvars, so, ct, st, gn, iname,
         self.ntext, ta) = f
        offs = struct.unpack_from(f"<{self.nstrings}I", d, so)
        self.strings = [d[o:d.index(b"\0", o)] for o in offs]
        ents = [struct.unpack_from("<IHH", d, ct + i * 8) for i in range(self.ncode)]
        starts = sorted(set(e[0] for e in ents))
        nxt = {s: (starts[i + 1] if i + 1 < len(starts) else len(d)) for i, s in enumerate(starts)}
        self.code = [(d[o:nxt[o]], nl, self.strings[sid].decode()) for o, nl, sid in ents]
        self.scripts = struct.unpack_from(f"<{self.nscripts}H", d, st)
        self.global_names = [self.strings[i] for i in struct.unpack_from(f"<{self.nglobals}H", d, gn)]
        tt = struct.unpack_from(f"<{self.ntext * 2}H", d, ta)
        self.text = [(self.strings[tt[i * 2]], self.strings[tt[i * 2 + 1]]) for i in range(self.ntext)]


def operand_size(code, pc):
    op = code[pc]
    if op in (OP["PUSH_I16"],):
        return 2
    if op in (OP["PUSH_I32"], OP["PUSH_REAL"]):
        return 4
    if op == OP["PUSH_STR"]:
        return 2
    if op in (OP["PUSH_VAR"], OP["POP_VAR"]):
        return 3 + (4 if code[pc + 1] & 15 == 5 else 0)
    if op == OP["DUP"]:
        return 1
    if op in (OP["JMP"], OP["BT"], OP["BF"], OP["PUSHENV"], OP["POPENV"]):
        return 3
    if op in (OP["CALL"], OP["CALL_SCRIPT"]):
        return 3
    return 0


def decode_packed(code):
    """Instructions of relaid-out (pack) code; stops at padding."""
    out = []
    pc = 0
    sizes = {OP["PUSH_FSTR"]: 3, OP["CALL_GETTEXT"]: 3}
    while pc < len(code):
        op = code[pc]
        if op == OP["NEXTBLK"]:
            pc = (pc // BLOCK + 1) * BLOCK
            continue
        n = sizes.get(op, None)
        if n is None:
            n = operand_size(code, pc)
        out.append((pc, op, code[pc + 1:pc + 1 + n]))
        pc += 1 + n
    return out


def decode(code):
    """[(offset, opcode, operand bytes)]"""
    out = []
    pc = 0
    while pc < len(code):
        n = operand_size(code, pc)
        out.append((pc, code[pc], code[pc + 1:pc + 1 + n]))
        pc += 1 + n
    return out


# ---------------------------------------------------------------- code

def relayout(code, string_addr, text_id_of, gettext_f, gettext_id_f):
    """Rewrite one code entry for the pack: far string operands, constant
    scr_gettext keys resolved to text ids, and instructions placed so none
    crosses a code block (blocks end with NEXTBLK)."""
    ins = decode(code)
    # peephole: PUSH_STR key; CALL vm_gettext argc -> CALL_GETTEXT id argc
    items = []
    i = 0
    while i < len(ins):
        off, op, opnd = ins[i]
        if (op == OP["PUSH_STR"] and i + 1 < len(ins) and ins[i + 1][1] == OP["CALL"]
                and struct.unpack("<H", ins[i + 1][2][:2])[0] == gettext_f):
            sid = struct.unpack("<H", opnd)[0]
            tid = text_id_of(sid)
            if tid is not None:
                argc = ins[i + 1][2][2]
                items.append((off, "GETTEXT", (tid, argc)))
                items.append((ins[i + 1][0], "NOP_REMOVED", None))
                i += 2
                continue
        items.append((off, op, opnd))
        i += 1

    def size(item):
        _, op, opnd = item
        if op == "GETTEXT":
            return 4
        if op == "NOP_REMOVED":
            return 0
        if op == OP["PUSH_STR"]:
            return 4
        if op == OP["PUSH_I16"]:
            return 5  # becomes PUSH_REAL: no int -> float conversion at run time
        return 1 + len(opnd)

    # assign new offsets; branch targets are old offsets -> map
    newoff = {}
    pos = 0
    for it in items:
        s = size(it)
        if s and pos % BLOCK + s > BLOCK - 1:
            pos = (pos // BLOCK + 1) * BLOCK
        newoff[it[0]] = pos
        pos += s
    newoff[len(code)] = pos  # branch to the end
    out = bytearray()
    for it in items:
        off, op, opnd = it
        s = size(it)
        if not s:
            continue
        target = newoff[off]
        if len(out) < target:
            out.append(OP["NEXTBLK"])
            out += bytes(target - len(out))
        if op == "GETTEXT":
            out.append(OP["CALL_GETTEXT"])
            out += struct.pack("<HB", opnd[0], opnd[1])
        elif op == OP["PUSH_STR"]:
            out.append(OP["PUSH_FSTR"])
            out += u24(string_addr(struct.unpack("<H", opnd)[0]))
        elif op == OP["PUSH_I16"]:
            out.append(OP["PUSH_REAL"])
            out += struct.pack("<f", float(struct.unpack("<h", opnd)[0]))
        elif op == OP["PUSH_I32"]:
            out.append(OP["PUSH_REAL"])
            out += struct.pack("<f", float(struct.unpack("<i", opnd)[0]))
        elif op in (OP["JMP"], OP["BT"], OP["BF"], OP["PUSHENV"], OP["POPENV"]):
            old = opnd[0] | opnd[1] << 8 | opnd[2] << 16
            out.append(op)
            out += u24(newoff[old])
        else:
            out.append(op)
            out += opnd
    out.append(OP["EXIT"])
    return bytes(out)


# ---------------------------------------------------------------- graphics

def rle_row(row):
    """Row tokens: 0x00-0x3f transparent run (n+1), 0x40-0x7f repeat run
    ((n&63)+1) of the next byte, 0x80-0xff literal run ((n&127)+1).
    Trailing transparency is left out."""
    end = len(row)
    while end and row[end - 1] == 0:
        end -= 1
    out = bytearray()
    i = 0
    while i < end:
        c = row[i]
        j = i
        if c == 0:
            while j < end and row[j] == 0 and j - i < 64:
                j += 1
            out.append(j - i - 1)
        else:
            while j < end and row[j] == c and j - i < 64:
                j += 1
            if j - i >= 3:
                out.append(0x40 | (j - i - 1))
                out.append(c)
            else:
                j = i
                while (j < end and row[j] != 0 and j - i < 128
                       and not (j + 2 < end and row[j] == row[j + 1] == row[j + 2])):
                    j += 1
                if j == i:
                    j = i + 1
                out.append(0x80 | (j - i - 1))
                out += row[i:j]
        i = j
    return bytes(out)


def put_image(far, pix, w, h):
    """An image: header u8 nbands, then per band u16 first row + u24 far
    address; each band is (u16 row offsets * (rows + 1), row data) and fits
    in a window. Returns the far address of the header."""
    rows = [rle_row(pix[y * w:(y + 1) * w]) for y in range(h)]
    bands = []
    y = 0
    while y < h:
        size = 2
        y0 = y
        while y < h and size + 2 + len(rows[y]) <= WDATA - 16:
            size += 2 + len(rows[y])
            y += 1
        if y == y0:
            raise ValueError("row too big")
        bands.append((y0, y))
    blobs = []
    for y0, y1 in bands:
        n = y1 - y0
        table = bytearray()
        data = bytearray()
        for r in range(y0, y1):
            table += struct.pack("<H", 2 * (n + 1) + len(data))
            data += rows[r]
        table += struct.pack("<H", 2 * (n + 1) + len(data))
        blobs.append(bytes(table + data))
    if len(bands) == 1 and 1 + 5 + len(blobs[0]) <= WDATA:
        # header and band together
        addr = far.put(b"\0" * 6 + blobs[0])
        hdr = bytes([1]) + struct.pack("<H", 0) + u24(addr + 6)
        far.wins[addr >> 16][(addr & 0xffff):(addr & 0xffff) + 6] = hdr
        return addr
    band_addrs = [far.put(b) for b in blobs]
    hdr = bytes([len(bands)]) + b"".join(struct.pack("<H", y0) + u24(a) for (y0, _), a in zip(bands, band_addrs))
    return far.put(hdr)


class GfxBin:
    def __init__(self, path):
        d = self.d = path.read_bytes()
        self.nspr, self.nfr, self.nbg, self.nfont = struct.unpack_from("<4I", d, 4)
        pal, st, ft, bt, fontt, mt = struct.unpack_from("<6I", d, 20)
        self.palette = d[pal:pal + 768]
        self.first = struct.unpack_from(f"<{self.nspr}I", d, st)
        self.frames = struct.unpack_from(f"<{self.nfr}I", d, ft)
        self.bgs = struct.unpack_from(f"<{self.nbg}I", d, bt)
        self.fonts = [struct.unpack_from("<HHI", d, fontt + i * 8) for i in range(self.nfont)]


# ---------------------------------------------------------------- main

def main():
    args = sys.argv[1:]
    if len(args) < 3:
        sys.exit(__doc__)
    vmdir, expdir, outdir = Path(args[0]), Path(args[1]), Path(args[2])
    opts = {}
    i = 3
    while i < len(args):
        opts[args[i]] = args[i + 1]
        i += 2
    prefix = opts.get("--name", "UTD")
    outdir.mkdir(parents=True, exist_ok=True)
    game = json.loads((expdir / "game.json").read_text())
    cb = CodeBin(vmdir / "code.bin")
    gfx = GfxBin(vmdir / "gfx.bin")
    gb = (vmdir / "game.bin").read_bytes()

    keep = None
    if "--sprites" in opts:
        keep = {"sprite": set(), "bg": set(), "font": set()}
        for line in Path(opts["--sprites"]).read_text().split("\n"):
            p = line.split()
            if len(p) == 2 and p[0] in keep:
                keep[p[0]].add(int(p[1]))
    pack_rooms = None
    if "--rooms" in opts:
        pack_rooms = set()
        for line in Path(opts["--rooms"]).read_text().split("\n"):
            p = line.split()
            if len(p) == 2 and p[0] == "room":
                pack_rooms.add(int(p[1]))
    if keep is not None:
        # Collision masks come from sprite pixels, so the sprite and mask of
        # every object placed in a room are always kept, even when invisible.
        objidx = {o["name"]: o for o in game["objects"]}
        sprites = {sp["name"]: sp["index"] for sp in game["sprites"]}
        for room in game["rooms"]:
            if pack_rooms is not None and room["index"] not in pack_rooms:
                continue
            for inst in room["instances"]:
                o = objidx.get(inst["obj"])
                while o:
                    for key in ("sprite", "mask"):
                        if o[key]:
                            keep["sprite"].add(sprites[o[key]])
                    o = objidx.get(o["parent"])
        # "dropsprite N": leave out even so (the intro's pictures, after the
        # first pack)
        for line in Path(opts["--sprites"]).read_text().split("\n"):
            p = line.split()
            if len(p) == 2 and p[0] == "dropsprite":
                keep["sprite"].discard(int(p[1]))

    far = Far(int(os.environ.get("CEPACK_RESERVE", "0")))
    marks = []

    def mark(name):
        marks.append((name, far.size))
    H = {}

    # --- strings used by code (literals) and names
    string_cache = {}

    def string_addr(sid):
        if sid not in string_cache:
            string_cache[sid] = far.put(cb.strings[sid] + b"\0")
        return string_cache[sid]

    # --- text
    text_ids = {k: i for i, (k, v) in enumerate(cb.text)}
    sid_to_text = {}
    for sid, s in enumerate(cb.strings):
        if s in text_ids:
            sid_to_text[sid] = text_ids[s]

    # builtin ids for gettext from gen_ids.h
    gen = (ROOT / "vm" / "gen_ids.h").read_text()

    def fid(name):
        import re
        m = re.search(rf"F_{name} = (\d+),", gen)
        return int(m.group(1))

    gettext_f = fid("vm_gettext")


    mark("code+strings")
    code_keep = None
    if "--code" in opts:
        code_keep = set()
        for line in Path(opts["--code"]).read_text().split("\n"):
            p = line.split()
            if len(p) == 2 and p[0] == "code":
                code_keep.add(int(p[1]))
    used_text = set()

    # --- code: relayout, split into blocks, compress
    entries = []
    blocks = []
    for ci, (bc, nlocals, name) in enumerate(cb.code):
        if code_keep is not None and ci not in code_keep:
            entries.append(struct.pack("<HHHH", len(blocks), 0, nlocals, 0))
            continue
        new = relayout(bc, string_addr, sid_to_text.get, gettext_f, None)
        if code_keep is not None:
            for off, op, opnd in decode_packed(new):
                if op == OP["CALL_GETTEXT"]:
                    used_text.add(struct.unpack("<H", opnd[:2])[0])
        first = len(blocks)
        for b in range(0, len(new), BLOCK):
            blocks.append(new[b:b + BLOCK])
        entries.append(struct.pack("<HHHH", first, len(blocks) - first, nlocals, 0))
    print(f"code: {len(entries)} entries, {len(blocks)} blocks")
    # the hottest code (by instructions run in a traced playthrough, per
    # byte) stays uncompressed: the calculator runs it straight from flash
    # instead of decompressing it into its small RAM cache
    raw_blocks = set()
    if "--hot" in opts:
        ops = {}
        for line in Path(opts["--hot"]).read_text().split("\n"):
            p = line.split()
            if len(p) == 3 and p[0] == "ops":
                ops[int(p[1])] = ops.get(int(p[1]), 0) + int(p[2])
        budget = int(opts.get("--hot-bytes", 128 * 1024))
        for ci in sorted(ops, key=lambda c: -ops[c] / max(1, struct.unpack("<HHHH", entries[c])[1])):
            first, n = struct.unpack("<HHHH", entries[ci])[:2]
            if n * (BLOCK + 3) > budget:
                continue
            budget -= n * (BLOCK + 3)
            raw_blocks.update(range(first, first + n))
    block_addrs = []
    comp_total = 0
    packed = iter(blobs([b for i, b in enumerate(blocks) if i not in raw_blocks]))
    for i, b in enumerate(blocks):
        c = b"\0" + struct.pack("<H", len(b)) + b if i in raw_blocks else next(packed)
        comp_total += len(c)
        block_addrs.append(far.put(c))
    print(f"  code: {comp_total} bytes, {len(raw_blocks)} blocks uncompressed")
    H["ncode"] = len(entries)
    H["code_entries"] = far.put_array(entries, 8)
    H["nblocks"] = len(blocks)
    H["code_blocks"] = far.put_array([u24(a) for a in block_addrs], 4)
    H["nscripts"] = cb.nscripts
    H["scripts"] = far.put_array([struct.pack("<H", s) for s in cb.scripts], 2)
    H["nglobals"] = cb.nglobals
    H["global_names"] = far.put_array([u24(far.put(n + b"\0")) for n in cb.global_names], 4)
    H["ninstvars"] = cb.ninstvars

    mark('# --- text: values in co')
    # Most scr_gettext keys are constants, resolved when packing; only keys
    # the game builds at run time ("item_name_" + string(n) ...) need a
    # hash for lookup. Find those prefixes in the disassembly.
    import re
    dyn = set()
    for f in (expdir / "asm").glob("*.asm"):
        lines = f.read_text(errors="replace").split("\n")
        for i, l in enumerate(lines):
            if "call.i scr_gettext" in l and i and not lines[i - 1].startswith("conv.s.v"):
                for c in lines[max(0, i - 12):i]:
                    m = re.match(r'push\.s "([^"]*)"@', c)
                    if m and len(m.group(1)) >= 3 and not m.group(1).startswith("_"):
                        dyn.add(m.group(1).encode())
    # --- text: values in compressed blocks
    tblocks = []
    tindex = []
    cur = bytearray()
    for ti_, (k, v) in enumerate(cb.text):
        if code_keep is not None and ti_ not in used_text and not any(k.startswith(p) for p in dyn):
            v = b""
        if len(cur) + len(v) + 1 > TEXT_BLOCK and cur:
            tblocks.append(bytes(cur))
            cur = bytearray()
        tindex.append(struct.pack("<HH", len(tblocks), len(cur)))
        cur += v + b"\0"
    if cur:
        tblocks.append(bytes(cur))
    taddrs = [far.put(b) for b in blobs(tblocks)]
    H["ntext"] = len(cb.text)
    H["text_index"] = far.put_array(tindex, 4)
    H["ntextblocks"] = len(tblocks)
    H["text_blocks"] = far.put_array([u24(a) for a in taddrs], 4)
    hashed = sorted((fnv(k), i) for i, (k, v) in enumerate(cb.text) if any(k.startswith(p) for p in dyn))
    print(f"text: {len(hashed)} keys looked up at run time")
    H["ntexthashes"] = len(hashed)
    H["text_hashes"] = far.put_array([struct.pack("<I", h) for h, _ in hashed], 4)
    H["text_ids"] = far.put_array([struct.pack("<H", i) for _, i in hashed], 2)
    print(f"text: {len(cb.text)} entries in {len(tblocks)} blocks")

    mark('# --- game tables (from ')
    # --- game tables (from game.bin)
    counts = struct.unpack_from("<12I", gb, 4)
    offs = struct.unpack_from("<12I", gb, 4 + 48)
    (nobj, nev, nspr, nbg, nfont, nglyph, npath, npts, nroom, ninst, ntile, norder) = counts
    (o_obj, o_ev, o_spr, o_bg, o_font, o_gl, o_path, o_pts, o_room, o_inst, o_tile, o_order) = offs

    def recs(off, size, n):
        return [gb[off + i * size:off + (i + 1) * size] for i in range(n)]

    def name_addr(sid):
        return far.put(cb.strings[sid] + b"\0") if sid < len(cb.strings) else 0

    mark('# objects: 16 B')
    # objects: 16 B = i16 sprite, mask, parent, u8 flags, i32 depth,
    #   u16 nevents, u24 first event index (no names: not needed at run time)
    objs = []
    for r in recs(o_obj, 20, nobj):
        spr, msk, par, flags, _, depth, nm, nevents, evoff = struct.unpack("<hhhBBiHHI", r)
        objs.append(struct.pack("<hhhBiH", spr, msk, par, flags, depth, nevents) + u24(evoff // 6))
    H["nobjects"] = nobj
    H["objects"] = far.put_array(objs, 16)
    H["events"] = far.put_array(recs(o_ev, 6, nev), 8)

    mark('# sprites: 32 B')
    # sprites: 32 B = u16 w, h, i16 ox, oy, l, r, t, b, u8 precise,
    #   u8 nmasks, u16 frames, u32 first frame, u24 name, u24 masks
    sprs = []
    frame_recs = []
    frame_cache = {}
    nframes_kept = 0
    for idx, r in enumerate(recs(o_spr, 24, nspr)):
        w, h, ox, oy, l, rr, t, b, prec, _, frames, nm, _ = struct.unpack("<HHhhhhhhBBHHH", r)
        # Precise masks usually equal the opaque pixels, which the runtime
        # derives; the others are stored (1 bpp rows, all frames together).
        mask_addr, nmasks = 0, 0
        if prec and (keep is None or idx in keep["sprite"]):
            spname = game["sprites"][idx]["name"]
            rowb = (w + 7) // 8
            masks = []
            while (expdir / "masks" / f"{spname}_{len(masks)}.bin").exists():
                masks.append((expdir / "masks" / f"{spname}_{len(masks)}.bin").read_bytes())
            differs = False
            for m, mb in enumerate(masks):
                off = gfx.frames[gfx.first[idx] + (m if m < frames else 0)]
                px = gfx.d[off:off + w * h]
                for y in range(h):
                    for x in range(w):
                        if ((mb[y * rowb + (x >> 3)] >> (7 - (x & 7))) & 1) != (px[y * w + x] != 0):
                            differs = True
                            break
                    if differs:
                        break
                if differs:
                    break
            if differs and len(masks) * rowb * h <= WDATA:
                mask_addr = far.put(b"".join(masks))
                nmasks = len(masks)
        sprs.append(struct.pack("<HHhhhhhhBBHI", w, h, ox, oy, l, rr, t, b, prec, nmasks, frames, len(frame_recs))
                    + u24(0) + u24(mask_addr))
        size0 = far.size
        # Very big animations (Omega Flowey's) keep every 2nd, 3rd or 4th frame:
        # the calculator shows a few frames a second, it would skip most
        # anyway. Identical frames are stored once.
        px = w * h * frames
        step = (5 if px > 3000000 else 4 if px > 1500000 else 3 if px > 800000 else 2 if px > 400000 else 1) \
            if frames >= 6 else 1
        for f in range(frames):
            if keep is None or idx in keep["sprite"]:
                off = gfx.frames[gfx.first[idx] + f - f % step]
                pix = gfx.d[off:off + w * h]
                key = (w, h, hash(pix))
                if key not in frame_cache:
                    frame_cache[key] = put_image(far, pix, w, h)
                    nframes_kept += 1
                frame_recs.append(u24(frame_cache[key]))
            else:
                frame_recs.append(u24(0))
        if os.environ.get("SPRSIZE") and far.size - size0 > 20000:
            print(f"  sprite {game['sprites'][idx]['name']}: {far.size - size0} bytes, {frames} frames")
    H["nsprites"] = nspr
    H["sprites"] = far.put_array(sprs, 32)
    H["frames"] = far.put_array(frame_recs, 4)
    print(f"sprites: {nframes_kept} frames packed")

    mark('bgs = []')
    bgs = []
    for idx, r in enumerate(recs(o_bg, 8, nbg)):
        w, h, nm, _ = struct.unpack("<HHHH", r)
        addr = 0
        if w and h and (keep is None or idx in keep["bg"]):
            off = gfx.bgs[idx]
            addr = put_image(far, gfx.d[off:off + w * h], w, h)
        bgs.append(struct.pack("<HH", w, h) + u24(addr) + b"\0" + u24(0))
    H["nbgs"] = nbg
    H["bgs"] = far.put_array(bgs, 16)

    mark('# fonts: 16 B')
    # fonts: 16 B = u16 first, last, nglyphs, pad, u32 first glyph, u24 name
    # glyphs: 16 B = u16 ch, u8 w, h, i8 shift, offset, pad, u24 bitmap
    #   (1 bpp rows, MSB first)
    fonts = []
    glyphs = []
    for idx, r in enumerate(recs(o_font, 12, nfont)):
        nm, first, last, ng, goff = struct.unpack("<HHHHI", r)
        sw, sh, soff = gfx.fonts[idx]
        use = keep is None or idx in keep["font"]
        start = len(glyphs)
        for g in range(ng):
            ch, x, y, w, h, shift, offset = struct.unpack_from("<HHHBBbb", gb, o_gl + goff + g * 10)
            if ch >= 0x3000 and not (keep is None):
                continue
            bm = 0
            if use and w and h:
                rowb = (w + 7) // 8
                bits = bytearray(rowb * h)
                for yy in range(h):
                    for xx in range(w):
                        if gfx.d[soff + (y + yy) * sw + x + xx]:
                            bits[yy * rowb + (xx >> 3)] |= 0x80 >> (xx & 7)
                bm = far.put(bytes(bits))
            glyphs.append(struct.pack("<HBBbbB", ch, w, h, shift, offset, 0) + u24(bm))
        fonts.append(struct.pack("<HHHHI", first, last, len(glyphs) - start, 0, start) + u24(name_addr(nm)))
    H["nfonts"] = nfont
    H["fonts"] = far.put_array(fonts, 16)
    H["glyphs"] = far.put_array(glyphs, 16)

    mark('H["npaths"] = npath')
    H["npaths"] = npath
    H["paths"] = far.put_array([struct.pack("<BBHI", *struct.unpack("<BBHI", r)[:3], struct.unpack("<BBHI", r)[3] // 12)
                                for r in recs(o_path, 8, npath)], 8)
    H["points"] = far.put_array(recs(o_pts, 12, npts), 16)

    mark('# rooms: 32 B')
    # rooms: 32 B = u16 w, h, speed, creation, u8 persistent, flags, pad2,
    #   u32 color, u16 ninst, ntiles, u32 first inst, u32 first tile, u24 name
    rooms = []
    rbgs = []
    rviews = []
    inst_recs = recs(o_inst, 32, ninst)
    tile_recs = recs(o_tile, 32, ntile)
    kept_insts = []
    kept_tiles = []
    for ri, r in enumerate(recs(o_room, 348, nroom)):
        w, h, speed, nm, creation, pers, flags, color, ni, nt, fi, ft = struct.unpack_from("<HHHHHBBIHHII", r)
        if pack_rooms is not None and ri not in pack_rooms:
            flags |= 0x80
            ni = nt = 0
        new_fi, new_ft = len(kept_insts), len(kept_tiles)
        kept_insts += inst_recs[fi:fi + ni]
        kept_tiles += tile_recs[ft:ft + nt]
        # backgrounds and views: 8 each per room in the pack, found by the
        # room's slot (in the record's pad field); slot 0 is empty
        slot = 0
        if not flags & 0x80:
            slot = len(rbgs) // 8 + (0 if rbgs else 1)
            if not rbgs:
                rbgs += [bytes(12)] * 8
                rviews += [bytes(28)] * 8
            for k in range(8):
                rbgs.append(r[28 + k * 12:28 + (k + 1) * 12])
            for k in range(8):
                rviews.append(r[124 + k * 28:124 + (k + 1) * 28])
        rooms.append(struct.pack("<HHHHBBHIHHII", w, h, speed, creation, pers, flags, slot, color, ni, nt, new_fi,
                                 new_ft) + u24(name_addr(nm)))
    H["nrooms"] = nroom
    H["rooms"] = far.put_array(rooms, 32)
    if not rbgs:
        rbgs += [bytes(12)] * 8
        rviews += [bytes(28)] * 8
    H["room_bgs"] = far.put_array(rbgs, 16)
    H["room_views"] = far.put_array(rviews, 32)
    mark("instances")
    # instances: 16 B = i16 obj, x, y, u16 creation, id - 100000,
    #   i16 xscale, yscale (8.8 fixed point)
    insts = []
    for r in kept_insts:
        obj, x, y, _, iid, sx, sy, color, angle, creation, _ = struct.unpack("<hhhHIffIfHH", r)
        insts.append(struct.pack("<hhhHHhh", obj, x, y, creation, iid - 100000,
                                 max(-32768, min(32767, round(sx * 256))), max(-32768, min(32767, round(sy * 256)))))
    H["insts"] = far.put_array(insts, 16)
    mark("tiles")
    # tiles: 16 B = u8 bg, u8 depth index, i16 x, y, u16 sx, sy, w, h
    depths = []
    tiles = []
    for r in kept_tiles:
        bg, x, y, sx, sy, w, h, _, depth, _, _, _ = struct.unpack("<hhhHHHHHiIff", r)
        if depth not in depths:
            depths.append(depth)
        tiles.append(struct.pack("<BBhhHHHH", bg, depths.index(depth), x, y, sx, sy, w, h))
    H["tiles"] = far.put_array(tiles, 16)
    H["tile_depths"] = far.put_array([struct.pack("<i", d) for d in depths], 4)
    mark('H["nroomorder"] = norder')
    H["nroomorder"] = norder
    H["roomorder"] = far.put_array(recs(o_order, 2, norder), 2)
    H["palette"] = far.put(gfx.palette)

    mark("end")
    for (a, pa), (b, pb) in zip(marks, marks[1:]):
        print(f"  {pb - pa:9d}  {a}")
    H["nwindows"] = len(far.wins)
    hdr = bytearray(b"UTP1")
    for k in HEADER:
        hdr += struct.pack("<I", H[k])
    assert len(hdr) <= 256
    far.wins[0][0:len(hdr)] = hdr
    if "CEPACK_RESERVE" not in os.environ and far.chunks:
        # again, with windows kept for the big arrays from the start, so
        # their other halves fill up instead of trailing half empty
        os.environ["CEPACK_RESERVE"] = str(far.chunks)
        os.execv(sys.executable, [sys.executable] + sys.argv)
    wins = far.windows()
    for f in outdir.glob(f"{prefix}*"):
        f.unlink()
    for n, w in enumerate(wins):
        name = f"{prefix}{n:03d}"
        (outdir / f"{name}.bin").write_bytes(w)
        write_8xv(outdir / f"{name}.8xv", name, w)
    print(f"pack: {far.size} bytes in {len(wins)} windows ({prefix}000-{prefix}{len(wins) - 1:03d})")


if __name__ == "__main__":
    main()

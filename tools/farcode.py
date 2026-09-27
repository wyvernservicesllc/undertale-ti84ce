#!/usr/bin/env python3
"""Code that runs from the calculator's archive (flash) instead of RAM.

The program must fit in about 150 KB of RAM. C files listed as "far" are
compiled apart, made position independent and stored in an AppVar that
runs where the OS put it in the archive: its jumps and calls to its own
code go through small helpers in the program (ce/src/farhelp.s) that add a
relative offset, since the eZ80 has no relative jump beyond 128 bytes. Its
calls into the program, and its variables, are absolute: the program is
always loaded at the same address, and the far code's variables live in a
RAM area the program sets aside (farcode_ram). The program calls far
functions through a table of `jp` instructions it fills in at startup.

  farcode.py prep <objdir> <srcdir> <clang flags...> -- <far .c files>
      compiles the far files and writes <srcdir>/farthunks.s: the call
      table, the RAM area and the addresses of everything in the program
      the far code uses (which also keeps them linked)
  farcode.py link <objdir> <program .obj> <out .8xv> <AppVar name>
      links the far code against the finished program
"""
import re
import subprocess
import sys
from pathlib import Path

CEDEV = Path.home() / "CEdev"
CLANG = CEDEV / "bin/ez80-clang"
AS = CEDEV / "binutils/bin/z80-none-elf-as"
LD = CEDEV / "binutils/bin/z80-none-elf-ld"
OBJCOPY = CEDEV / "binutils/bin/z80-none-elf-objcopy"
NM = CEDEV / "binutils/bin/z80-none-elf-nm"

CLANG_FLAGS = ["-S", "-nostdinc", "-fno-autolink", "-fno-addrsig", "-mllvm", "-z80-gas-style",
               "-fno-math-errno", "-fno-jump-tables", "-D__TICE__=1", "-isystem", str(CEDEV / "include")]

SYM_RE = r"[._A-Za-z$][\w.$]*"


def is_flash(sect):
    """Code and read-only data stay in the archive; variables go to RAM."""
    return sect.startswith(".text") or sect.startswith(".rodata")


def parse(asm_text, prefix):
    """Lines of one file, with its file-local symbols renamed (prefix) so
    files can be merged, and the kind of section each symbol is in."""
    lines = asm_text.split("\n")
    globs = set()
    for l in lines:
        m = re.match(r"\s*\.globl\s+(\S+)", l)
        if m:
            globs.add(m.group(1))
    defs = {}
    sect = ".text"
    for l in lines:
        m = re.match(r"\s*\.section\s+([^,\s]+)", l)
        if m:
            sect = m.group(1).strip('"')
        m = re.match(r"^(" + SYM_RE + r"):", l)
        if m:
            defs[m.group(1)] = "text" if is_flash(sect) else "data"
    local = {s for s in defs if s not in globs}
    ren = {s: (f".L{prefix}_{s[2:]}" if s.startswith(".L") else f"{s}.{prefix}") for s in local}

    def fix(s):
        return re.sub(SYM_RE, lambda m: ren.get(m.group(0), m.group(0)), s)

    out = []
    for l in lines:
        code, _, _ = l.partition(";")
        if not code.strip() or re.match(r"\s*\.(file|ident|assume)\b", code):
            continue
        out.append(fix(code.rstrip()))
    kinds = {ren.get(s, s): k for s, k in defs.items()}
    return out, kinds, globs


class Pic:
    def __init__(self):
        self.n = 0
        self.relocs = []

    def label(self):
        self.n += 1
        return f".Lpic{self.n}"

    def field(self, sym):
        end = self.label()
        return [f"\t.d24\t{sym}-{end}", f"{end}:"]

    def long_jump(self, cond, sym):
        if cond is None:
            return ["\tcall\t__pic_jp"] + self.field(sym)
        skip = self.label()
        return [f"\tcall\t{cond}, __pic_jpc", f"\tjr\t{skip}"] + self.field(sym) + [f"{skip}:"]

    def long_call(self, cond, sym):
        if cond is None:
            return ["\tcall\t__pic_call"] + self.field(sym)
        skip = self.label()
        return [f"\tcall\t{cond}, __pic_callc", f"\tjr\t{skip}"] + self.field(sym) + [f"{skip}:"]


def transform(lines, kinds, long_jr, pic):
    """Position-independent text: every jump, call and address of our own
    code becomes a helper call with a relative offset. `long_jr`: indices
    of jr lines too far after the expansion (found by assembling)."""
    out, errors = [], []
    in_text = True
    for i, l in enumerate(lines):
        m = re.match(r"\s*\.section\s+([^,\s]+)(.*)", l)
        if m:
            name = m.group(1).strip('"')
            in_text = is_flash(name)
            if in_text:
                l = '\t.section\t.text,"ax",@progbits'
            out.append(l)
            continue
        d = re.match(r"\s+\.?(d24|dl|long|dw|word)\s+(.*)", l)
        if d and any(kinds.get(x) == "text" for x in re.findall(SYM_RE, d.group(2))):
            if in_text or d.group(1) != "d24" or not re.match(SYM_RE + "$", d.group(2).strip()):
                errors.append(f"address of far code or data in data: {l.strip()}")
            else:
                # a pointer in RAM data: far_init adds where the code is
                pic.relocs.append(f"__far_rel_{len(pic.relocs)}")
                out += [f"{pic.relocs[-1]}:", l]
                continue
        m = re.match(r"\s+([a-z]+)\s+(.*)", l)
        if not m or not in_text:
            if False:
                for s in re.findall(SYM_RE, m.group(2)):
                    if kinds.get(s) == "text":
                        errors.append(f"address of code in data: {l.strip()}")
            out.append(l)
            continue
        op, args = m.group(1), m.group(2).strip()
        am = re.match(r"(?:(nz|z|nc|c|po|pe|p|m)\s*,\s*)?(" + SYM_RE + r")$", args)
        if op in ("jp", "call", "jr") and am and kinds.get(am.group(2)) == "text":
            cond, sym = am.group(1), am.group(2)
            if op == "jr" and i not in long_jr:
                out.append(l)
            elif op == "call":
                out += pic.long_call(cond, sym)
            else:
                out += pic.long_jump(cond, sym)
            out.append(f"\t; @{i}")
            continue
        refs = [s for s in re.findall(SYM_RE, args) if kinds.get(s) == "text"]
        if refs:
            lm = re.match(r"(hl|de|bc|ix|iy)\s*,\s*(" + SYM_RE + r")$", args)
            im = re.match(r"(hl|de|bc|ix|iy|a)\s*,\s*\((" + SYM_RE + r")\)$", args)
            if op == "ld" and lm:
                reg, sym = lm.groups()
                if reg == "hl":
                    out += ["\tcall\t__pic_lea"] + pic.field(sym)
                elif reg == "de":
                    out += ["\tpush\thl", "\tcall\t__pic_lea"] + pic.field(sym) + ["\tex\tde, hl", "\tpop\thl"]
                else:
                    out += ["\tpush\thl", "\tcall\t__pic_lea"] + pic.field(sym) + ["\tpush\thl", f"\tpop\t{reg}", "\tpop\thl"]
                continue
            if op == "ld" and im:
                reg, sym = im.groups()
                if reg == "hl":
                    out += ["\tcall\t__pic_lea"] + pic.field(sym) + ["\tld\thl, (hl)"]
                else:
                    out += ["\tpush\thl", "\tcall\t__pic_lea"] + pic.field(sym) + [f"\tld\t{reg}, (hl)", "\tpop\thl"]
                continue
            errors.append(f"can't make position independent: {l.strip()}")
        out.append(l)
    return out, errors


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    return r


def prep(objdir, srcdir, cflags, files):
    objdir = Path(objdir)
    objdir.mkdir(parents=True, exist_ok=True)
    merged, kinds, exports = [], {}, []
    for k, f in enumerate(files):
        s = objdir / (Path(f).stem + ".far.s")
        r = run([CLANG] + CLANG_FLAGS + cflags + [f, "-o", s])
        if r.returncode:
            sys.exit(r.stderr)
        lines, kd, globs = parse(s.read_text(), f"f{k}")
        merged += lines
        kinds.update(kd)
        exports += sorted(g for g in globs if kd.get(g) == "text")
    long_jr = set()
    for attempt in range(20):
        pic = Pic()
        text, errors = transform(merged, kinds, long_jr, pic)
        if errors:
            sys.exit("\n".join(errors))
        src = objdir / "far.s"
        src.write_text("\t.assume\tADL = 1\n" + "\n".join(text) + "\n")
        r = run([AS, "-march=ez80+full", src, "-o", objdir / "far.o"])
        if r.returncode == 0:
            break
        # jr out of range: find the source line (tagged "; @i" after it)
        src_lines = src.read_text().split("\n")
        bad = set()
        for em in re.finditer(r":(\d+): Error: (.*)", r.stderr):
            ln = int(em.group(1)) - 1
            if "range" not in em.group(2):
                sys.exit(r.stderr[:3000])
            for j in range(ln, min(ln + 3, len(src_lines))):
                t = re.match(r"\s*; @(\d+)", src_lines[j])
                if t:
                    bad.add(int(t.group(1)))
                    break
            else:
                # a jr we left alone: its tag follows directly
                sys.exit("unmatched error: " + src_lines[ln])
        if not bad:
            sys.exit(r.stderr[:3000])
        long_jr |= bad
    else:
        sys.exit("far code: could not fix jump ranges")
    # imports: symbols the far code uses that the program defines
    r = run([NM, "-u", objdir / "far.o"])
    imports = sorted({l.split()[-1] for l in r.stdout.split("\n") if l.strip()} | {"_farcode_ram"})
    sizes = section_sizes(objdir / "far.o")
    ram = sizes.get("data", 0) + sizes.get("bss", 0)
    (objdir / "farcode.exports").write_text("\n".join(exports) + "\n")
    (objdir / "farcode.relocs").write_text("\n".join(pic.relocs) + "\n")
    (objdir / "farcode.imports").write_text("\n".join(imports) + "\n")
    thunks = ["\t.assume\tADL = 1",
              "; generated by tools/farcode.py: calls into far code (filled in by far_init)",
              "\t.section\t.data._far_thunks,\"aw\",@progbits",
              "\t.global\t_far_thunks", "_far_thunks:"]
    for e in exports:
        thunks += [f"\t.global\t{e}", f"{e}:", "\tjp\t0"]
    thunks += ["\t.section\t.rodata._far_nthunks,\"a\",@progbits", "\t.global\t_far_nthunks", "_far_nthunks:",
               f"\t.d24\t{len(exports)}",
               # every program address the far code uses (keeps them linked;
               # far_init checks the far code was linked against the same)
               "\t.global\t_far_imports", "_far_imports:"] + [f"\t.d24\t{i}" for i in imports] + [
               "\t.global\t_far_nimports", "_far_nimports:", f"\t.d24\t{len(imports)}",
               "\t.section\t.bss._farcode_ram,\"aw\",@nobits", "\t.global\t_farcode_ram", "_farcode_ram:",
               f"\t.ds\t{max(ram, 1)}", ""]
    new = "\n".join(thunks)
    p = Path(srcdir) / "farthunks.s"
    if not p.exists() or p.read_text() != new:
        p.write_text(new)
    print(f"far code: {sizes.get('text', 0)} bytes of code, {ram} of RAM, {len(pic.relocs)} pointers, "
          f"{len(exports)} functions, {len(imports)} imports, {len(long_jr)} long jr")


def section_sizes(obj):
    r = run([CEDEV / "binutils/bin/z80-none-elf-objdump", "-h", obj])
    sizes = {}
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)", r.stdout, re.M):
        name, size = m.group(1), int(m.group(2), 16)
        kind = "text" if name.startswith(".text") else "bss" if name.startswith(".bss") else "data"
        sizes[kind] = sizes.get(kind, 0) + size
    return sizes


def fletcher(data):
    """The check far_init computes over the program's import table."""
    a = b = 0
    for x in data:
        a = (a + x) & 0xffffff
        b = (b + a) & 0xffffff
    return (a ^ (b << 8)) & 0xffffff


def link(objdir, prog_obj, out, name):
    objdir = Path(objdir)
    exports = (objdir / "farcode.exports").read_text().split()
    imports = (objdir / "farcode.imports").read_text().split()
    syms = {}
    for l in run([NM, prog_obj]).stdout.split("\n"):
        p = l.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    ram = syms["_farcode_ram"]
    script = objdir / "far.ld"
    script.write_text(f"""SECTIONS
{{
    . = 0;
    .text : {{ *(.text .text.*) }}
    . = {ram:#x};
    .data : {{ *(.rodata .rodata.* .data .data.*) }}
    .bss (NOLOAD) : {{ *(.bss .bss.* COMMON) }}
    __far_bss_end = .;
}}
""")
    elf = objdir / "far.elf"
    # far.o first: the program also defines the exported names (its call
    # table), and with muldefs the first definition wins
    r = run([LD, "-T", script, "-z", "muldefs", objdir / "far.o", "-R", prog_obj, "-o", elf])
    if r.returncode:
        sys.exit(r.stderr[:3000])
    for sect in ("text", "data"):
        run([OBJCOPY, "-O", "binary", "-j", "." + sect, elf, objdir / f"far.{sect}"])
    text = (objdir / "far.text").read_bytes()
    data = (objdir / "far.data").read_bytes() if (objdir / "far.data").exists() else b""
    fsyms = {}
    for l in run([NM, elf]).stdout.split("\n"):
        p = l.split()
        if len(p) == 3:
            fsyms[p[2]] = int(p[0], 16)
    bss = fsyms["__far_bss_end"] - ram - len(data)
    missing = [i for i in imports if i not in syms]
    if missing:
        sys.exit("far code: the program lacks " + " ".join(missing))
    check = fletcher(b"".join(syms[i].to_bytes(3, "little") for i in imports))
    relocs = (objdir / "farcode.relocs").read_text().split()
    head = b"UTFC" + b"".join(v.to_bytes(3, "little") for v in (
        check, len(imports), len(exports), len(text), len(data), bss, len(relocs)))
    own = {}
    for l in run([NM, objdir / "far.o"]).stdout.split("\n"):
        p = l.split()
        if len(p) == 3 and p[1] in "Tt":
            own[p[2]] = int(p[0], 16)
    if any(fsyms[e] != own[e] for e in exports):
        sys.exit("far code: exported functions moved in linking")
    head += b"".join(own[e].to_bytes(3, "little") for e in exports)
    head += b"".join((fsyms[r] - ram).to_bytes(3, "little") for r in relocs)
    blob = head + text + data
    if len(blob) > 65000:
        sys.exit(f"far code is {len(blob)} bytes: more than one AppVar holds")
    sys.path.insert(0, str(Path(__file__).parent))
    from cepack import write_8xv
    write_8xv(out, name, blob, archived=True)
    print(f"far code: {len(blob)} bytes in {name} ({len(text)} code, {len(data)} data, {bss} bss)")


def main():
    if sys.argv[1] == "prep":
        objdir, srcdir = sys.argv[2:4]
        rest = sys.argv[4:]
        k = rest.index("--")
        prep(objdir, srcdir, rest[:k], rest[k + 1:])
    elif sys.argv[1] == "link":
        link(*sys.argv[2:6])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

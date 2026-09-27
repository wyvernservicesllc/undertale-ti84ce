#!/usr/bin/env python3
"""Compile Undertale's disassembled GameMaker bytecode into the compact
bytecode the VM runs (vm/), plus the data tables it needs.

Usage: python3 tools/vmc.py <export dir> <out dir>

<export dir> is what tools/export_game.csx writes (asm/, game.json, ...).
Writes:
  <out dir>/code.bin    header, string table, code entries (see FORMAT)
  <out dir>/game.bin    objects, sprites, backgrounds, fonts, rooms
  vm/gen_ids.h          ids shared by the compiler and the runtime

FORMAT (all little endian, offsets from the start of the file):
  code.bin: "UTC1", u32 nstrings, u32 ncode, u32 nscripts, u32 nglobals,
            u32 ninstvars, u32 string_offsets_at, u32 code_table_at,
            u32 script_table_at, u32 globalnames_at, u32 instnames_at,
            u32 ntext, u32 text_at
    text table:   per entry u16 key string, u16 value string, sorted by key
                  (textdata_en, served natively instead of a ds_map)
    string table: u32 offset per string -> NUL terminated bytes
    code table:   per entry u32 offset, u16 nlocals, u16 name string id
    script table: per script u16 code id (0xffff = none)
  Instructions are one opcode byte followed by fixed operands; branch
  targets are u24 offsets inside the code entry.
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---- opcodes (keep in sync with vm/vm.h, which includes gen_ids.h) ----
OPS = [
    "NOP", "PUSH_I16", "PUSH_I32", "PUSH_REAL", "PUSH_STR", "PUSH_VAR", "POP_VAR",
    "POPZ", "DUP", "ADD", "SUB", "MUL", "DIV", "REM", "MOD", "AND", "OR", "XOR",
    "SHL", "SHR", "CMP_LT", "CMP_LE", "CMP_EQ", "CMP_NE", "CMP_GE", "CMP_GT",
    "NEG", "NOT", "JMP", "BT", "BF", "PUSHENV", "POPENV", "POPENV_DROP",
    "CALL", "CALL_SCRIPT", "RET", "EXIT", "CONV_INT", "PUSH_UNDEF",
    # only in packs (tools/cepack.py): continue at the next 4 KB code block;
    # push a string by far address; scr_gettext of a text id
    "NEXTBLK", "PUSH_FSTR", "CALL_GETTEXT",
]
OP = {n: i for i, n in enumerate(OPS)}

# Variable scopes in PUSH_VAR / POP_VAR. Low 4 bits: scope; 0x80 array
# access (index and instance on the stack); 0x40 the value is on top of the
# array reference (pop.i.v after dup).
SC_SELF, SC_OTHER, SC_GLOBAL, SC_LOCAL, SC_STACKTOP, SC_INST, SC_ALL, SC_NOONE = range(8)
SC_ARRAY = 0x80
SC_SWAP = 0x40

# Instance variables the runtime implements itself; they get the first ids.
BUILTIN_VARS = """
x y xprevious yprevious xstart ystart hspeed vspeed speed direction friction
gravity gravity_direction sprite_index image_index image_speed image_xscale
image_yscale image_angle image_alpha image_blend image_number image_single
sprite_width sprite_height sprite_xoffset sprite_yoffset mask_index depth
visible solid persistent object_index id alarm bbox_left bbox_right bbox_top
bbox_bottom path_index path_position path_speed path_endaction path_scale
path_orientation path_positionprevious timeline_index timeline_position
timeline_speed timeline_running
room room_width room_height room_speed room_first room_last room_persistent
view_xview view_yview view_wview view_hview view_current view_enabled
view_visible view_object view_hborder view_vborder view_angle view_xport
view_yport view_wport view_hport background_color background_showcolor
background_visible background_index background_x background_y
background_hspeed background_vspeed background_alpha background_blend
background_htiled background_vtiled background_xscale background_yscale
argument argument_count argument0 argument1 argument2 argument3 argument4
argument5 argument6 argument7 argument8 argument9 argument10 argument11
argument12 argument13 argument14 argument15
os_type current_time current_hour current_minute current_second current_day
current_month current_year current_weekday async_load application_surface
undefined mouse_x mouse_y working_directory keyboard_lastchar keyboard_key
fps instance_count score lives health
""".split()


def die(msg):
    sys.exit(f"vmc: {msg}")


class Strings:
    def __init__(self):
        self.ids = {}
        self.list = []

    def add(self, s):
        if s not in self.ids:
            self.ids[s] = len(self.list)
            self.list.append(s)
        return self.ids[s]


class Names:
    def __init__(self, first=()):
        self.ids = {}
        self.list = []
        for n in first:
            self.add(n)

    def add(self, n):
        if n not in self.ids:
            self.ids[n] = len(self.list)
            self.list.append(n)
        return self.ids[n]


def unescape(s):
    # UTMT escapes \", \\, \n, \r, \t in push.s literals.
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}.get(n, "\\" + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


STR_RE = re.compile(r'^"((?:[^"\\]|\\.)*)"@\d+$')
VAR_RE = re.compile(r'^(\[(array|stacktop)\])?([A-Za-z_]+|-?\d+)\.([A-Za-z_][A-Za-z_0-9]*)$')
CALL_RE = re.compile(r'^([A-Za-z_][A-Za-z_0-9]*)\(argc=(\d+)\)$')


TEXT_ADD_RE = re.compile(
    r'push\.s ("(?:[^"\\]|\\.)*"@\d+)\s+conv\.s\.v\s+push\.s ("(?:[^"\\]|\\.)*"@\d+)\s+conv\.s\.v\s+'
    r'pushglb\.v global\.text_data_en\s+call\.i ds_map_add\(argc=3\)')


def read_text_table(asm_text):
    """(key, value) pairs from textdata_en's ds_map_add calls."""
    pairs = {}
    for val, key in TEXT_ADD_RE.findall(asm_text):
        k = unescape(STR_RE.match(key).group(1))
        v = unescape(STR_RE.match(val).group(1))
        pairs.setdefault(k, v)  # ds_map_add keeps the first value
    return pairs


# Scripts the runtime implements natively (tools/vmc.py emits CALL to the
# built-in instead of CALL_SCRIPT).
NATIVE_SCRIPTS = {"scr_gettext": "vm_gettext"}
# Scripts replaced by a stub: the text tables are served from code.bin.
STUB_SCRIPTS = {
    "gml_Script_textdata_en": "text_data_en",
    "gml_Script_textdata_ja": "text_data_ja",
}


class Compiler:
    def __init__(self, game):
        self.strings = Strings()
        self.globals = Names()
        self.instvars = Names(BUILTIN_VARS)
        self.scripts = {s["name"]: s for s in game["scripts"]}
        self.script_index = {s["name"]: s["index"] for s in game["scripts"]}
        self.builtin_funcs = Names()
        self.code_ids = {}
        self.code_names = []

    def code_id(self, name):
        if name not in self.code_ids:
            self.code_ids[name] = len(self.code_names)
            self.code_names.append(name)
        return self.code_ids[name]

    def var_operand(self, text, is_pop, locals_):
        m = VAR_RE.match(text)
        if not m:
            die(f"bad variable operand {text!r}")
        kind, inst, name = m.group(2), m.group(3), m.group(4)
        flags = SC_ARRAY if kind == "array" else 0
        extra = b""
        if kind == "stacktop":
            scope = SC_STACKTOP
        elif inst == "self":
            scope = SC_SELF
        elif inst == "other":
            scope = SC_OTHER
        elif inst == "global":
            scope = SC_GLOBAL
        elif inst == "local":
            scope = SC_LOCAL
        elif inst == "builtin":
            scope = SC_SELF
        else:
            v = int(inst)
            if v == -1:
                scope = SC_SELF
            elif v == -2:
                scope = SC_OTHER
            elif v == -5:
                scope = SC_GLOBAL
            elif v == -7:
                scope = SC_LOCAL
            elif v == -3:
                scope = SC_ALL
            elif v == -4:
                scope = SC_NOONE
            else:
                scope = SC_INST
                extra = struct.pack("<i", v)
        if scope == SC_GLOBAL:
            vid = self.globals.add(name)
        elif scope == SC_LOCAL:
            if name not in locals_:
                locals_[name] = len(locals_)
            vid = locals_[name]
        else:
            vid = self.instvars.add(name)
        return bytes([scope | flags]) + struct.pack("<H", vid) + extra

    def compile_entry(self, name, text):
        lines = [l.strip() for l in text.splitlines()]
        locals_ = {}
        labels = {}
        fixups = []  # (position of u24, label)
        out = bytearray()
        for line in lines:
            if not line or line.startswith(";"):
                continue
            if line.startswith(".localvar"):
                parts = line.split()
                locals_.setdefault(parts[2], len(locals_))
                continue
            if line.startswith(":["):
                labels[line[2:-1]] = len(out)
                continue
            if line.startswith("/*") or line.startswith("*/") or "DISASSEMBLY FAILED" in line:
                die(f"{name}: disassembly failed")
            mnem, _, arg = line.partition(" ")
            arg = arg.strip()
            base = mnem.split(".")[0]
            types = mnem.split(".")[1:]

            def target(lbl):
                fixups.append((len(out), lbl))
                out.extend(b"\0\0\0")

            if base == "pushi" or (base == "push" and types[0] in ("e", "i", "l")):
                v = int(arg)
                if -32768 <= v <= 32767:
                    out.append(OP["PUSH_I16"])
                    out += struct.pack("<h", v)
                elif -2**31 <= v < 2**31:
                    out.append(OP["PUSH_I32"])
                    out += struct.pack("<i", v)
                else:
                    out.append(OP["PUSH_REAL"])
                    out += struct.pack("<f", float(v))
            elif base == "push" and types[0] == "d":
                out.append(OP["PUSH_REAL"])
                out += struct.pack("<f", float(arg))
            elif base == "push" and types[0] == "b":
                out.append(OP["PUSH_I16"])
                out += struct.pack("<h", 1 if arg in ("true", "1") else 0)
            elif base == "push" and types[0] == "s":
                m = STR_RE.match(arg)
                if not m:
                    die(f"{name}: bad string {arg!r}")
                out.append(OP["PUSH_STR"])
                out += struct.pack("<H", self.strings.add(unescape(m.group(1))))
            elif base in ("push", "pushglb", "pushloc", "pushbltn"):
                if arg == "self.undefined" or arg == "builtin.undefined":
                    out.append(OP["PUSH_UNDEF"])
                    continue
                out.append(OP["PUSH_VAR"])
                out += self.var_operand(arg, False, locals_)
            elif base == "pop":
                operand = bytearray(self.var_operand(arg, True, locals_))
                if types[0] == "i" and operand[0] & SC_ARRAY:
                    operand[0] |= SC_SWAP
                out.append(OP["POP_VAR"])
                out += operand
            elif base == "popz":
                out.append(OP["POPZ"])
            elif base == "dup":
                out.append(OP["DUP"])
                out.append(int(arg))
            elif base in ("add", "sub", "mul", "div", "rem", "mod", "and", "or", "xor", "shl", "shr"):
                out.append(OP[base.upper()])
            elif base == "cmp":
                out.append(OP["CMP_" + {"LT": "LT", "LTE": "LE", "EQ": "EQ", "NEQ": "NE", "GTE": "GE", "GT": "GT"}[arg]])
            elif base == "neg":
                out.append(OP["NEG"])
            elif base == "not":
                out.append(OP["NOT"])
            elif base == "conv":
                if types[1] in ("i", "l"):
                    out.append(OP["CONV_INT"])
            elif base in ("b", "bt", "bf"):
                if arg == "[end]":
                    if base == "b":
                        out.append(OP["EXIT"])
                        continue
                    out.append(OP["BT" if base == "bt" else "BF"])
                    target("end")
                else:
                    out.append(OP[{"b": "JMP", "bt": "BT", "bf": "BF"}[base]])
                    target(arg[1:-1])
            elif base == "pushenv":
                out.append(OP["PUSHENV"])
                target(arg[1:-1])
            elif base == "popenv":
                if arg == "<drop>":
                    out.append(OP["POPENV_DROP"])
                else:
                    out.append(OP["POPENV"])
                    target(arg[1:-1])
            elif base == "call":
                m = CALL_RE.match(arg)
                if not m:
                    die(f"{name}: bad call {arg!r}")
                fname, argc = m.group(1), int(m.group(2))
                if fname in NATIVE_SCRIPTS:
                    out.append(OP["CALL"])
                    out += struct.pack("<H", self.builtin_funcs.add(NATIVE_SCRIPTS[fname]))
                elif fname in self.scripts and self.scripts[fname]["code"]:
                    out.append(OP["CALL_SCRIPT"])
                    out += struct.pack("<H", self.code_id(self.scripts[fname]["code"]))
                else:
                    out.append(OP["CALL"])
                    out += struct.pack("<H", self.builtin_funcs.add(fname))
                out.append(argc)
            elif base == "ret":
                out.append(OP["RET"])
            elif base == "exit":
                out.append(OP["EXIT"])
            elif base == "chkindex":
                pass
            else:
                die(f"{name}: unknown instruction {line!r}")
        labels["end"] = len(out)
        out.append(OP["EXIT"])
        for pos, lbl in fixups:
            if lbl not in labels:
                die(f"{name}: missing label {lbl}")
            out[pos:pos + 3] = labels[lbl].to_bytes(3, "little")
        if len(out) >= 1 << 24:
            die(f"{name}: too big")
        return bytes(out), len(locals_)


def write_game_bin(game, comp, path):
    """Objects, sprites, backgrounds, fonts, paths and rooms as fixed-size
    records the runtime reads in place (see vm/data.h)."""
    idx = {}
    for kind in ("objects", "sprites", "backgrounds", "fonts", "rooms", "paths"):
        idx[kind] = {r["name"]: r["index"] for r in game[kind]}

    def ref(kind, name):
        return -1 if name is None else idx[kind][name]

    def code(name):
        return 0xFFFF if not name else comp.code_ids[name]

    def sid(s):
        return comp.strings.add(s or "")

    sections = []

    # objects: i16 sprite, i16 mask, i16 parent, u8 flags, u8 pad, i32 depth,
    # u16 name, u16 nevents, u32 events offset. events: u8 type, u8 pad,
    # u16 subtype, u16 code.
    obj = bytearray()
    ev = bytearray()
    for o in game["objects"]:
        events = [e for e in o["events"] if e["code"]]
        flags = (o["visible"] and 1) | (o["solid"] and 2) | (o["persistent"] and 4)
        obj += struct.pack("<hhhBBiHHI", ref("sprites", o["sprite"]), ref("sprites", o["mask"]),
                           ref("objects", o["parent"]), flags, 0, o["depth"], sid(o["name"]),
                           len(events), len(ev))
        for e in events:
            ev += struct.pack("<BBHH", e["type"], 0, e["subtype"], code(e["code"]))
    sections += [obj, ev]

    # sprites: u16 w, u16 h, i16 ox, i16 oy, i16 left, right, top, bottom,
    # u8 precise, u8 pad, u16 frames, u16 name, u16 pad
    spr = bytearray()
    for sp in game["sprites"]:
        spr += struct.pack("<HHhhhhhhBBHHH", sp["width"], sp["height"], sp["ox"], sp["oy"],
                           sp["left"], sp["right"], sp["top"], sp["bottom"],
                           1 if sp["sep"] == "Precise" else 0, 0, sp["frames"], sid(sp["name"]), 0)
    sections.append(spr)

    bgs = bytearray()
    for b in game["backgrounds"]:
        bgs += struct.pack("<HHHH", b["width"], b["height"], sid(b["name"]), 0)
    sections.append(bgs)

    # fonts: u16 name, u16 first, u16 last, u16 nglyphs, u32 glyph offset;
    # glyphs: u16 ch, u16 x, u16 y, u8 w, u8 h, i8 shift, i8 offset
    fnt = bytearray()
    gl = bytearray()
    for f in game["fonts"]:
        fnt += struct.pack("<HHHHI", sid(f["name"]), f["first"], f["last"], len(f["glyphs"]), len(gl))
        for g in f["glyphs"]:
            gl += struct.pack("<HHHBBbb", g["ch"], g["x"], g["y"], g["w"], g["h"],
                              max(-128, min(127, g["shift"])), max(-128, min(127, g["offset"])))
    sections += [fnt, gl]

    # paths: u8 smooth, u8 closed, u16 npoints, u32 offset; points f32 x, y, speed
    pth = bytearray()
    pts = bytearray()
    for pa in game["paths"]:
        pth += struct.pack("<BBHI", pa["smooth"], pa["closed"], len(pa["points"]), len(pts))
        for pt in pa["points"]:
            pts += struct.pack("<fff", pt["x"], pt["y"], pt["speed"])
    sections += [pth, pts]

    # rooms: u16 w, h, speed, name, creation code, u8 persistent,
    # u8 flags(1 views enabled, 2 draw bg color), u32 color,
    # u16 ninst, u16 ntiles, u32 inst offset, u32 tile offset,
    # 8 x background (u8 enabled, fg, htile, vtile, i16 bg, i16 x, i16 y,
    #   i8 hspeed, i8 vspeed),
    # 8 x view (u8 enabled, u8 pad, i16 x, y, w, h, px, py, pw, ph, bx, by,
    #   sx, sy, follow)
    # instances: i16 obj, i16 x, i16 y, u16 pad, u32 id, f32 sx, f32 sy,
    #   u32 color, f32 angle, u16 creation code, u16 pad
    # tiles: i16 bg, i16 x, i16 y, u16 sx, u16 sy, u16 w, u16 h, u16 pad, i32 depth,
    #   u32 id, f32 scx, f32 scy
    rm = bytearray()
    inst = bytearray()
    til = bytearray()
    for r in game["rooms"]:
        flags = (1 if "EnableViews" in r["views"] else 0) | (2 if r["drawColor"] else 0)
        rm += struct.pack("<HHHHHBBIHHII", r["width"], r["height"], r["speed"], sid(r["name"]),
                          code(r["creation"]), 1 if r["persistent"] else 0, flags, r["color"] & 0xFFFFFFFF,
                          len(r["instances"]), len(r["tiles"]), len(inst) // 32, len(til) // 32)
        bl = (r["backgrounds"] + [None] * 8)[:8]
        for b in bl:
            if b is None:
                rm += struct.pack("<BBBBhhhbb", 0, 0, 0, 0, -1, 0, 0, 0, 0)
            else:
                rm += struct.pack("<BBBBhhhbb", 1 if b["enabled"] else 0, 1 if b["fg"] else 0,
                                  1 if b["htile"] else 0, 1 if b["vtile"] else 0,
                                  ref("backgrounds", b["bg"]), b["x"], b["y"],
                                  max(-128, min(127, b["hspeed"])), max(-128, min(127, b["vspeed"])))
        vl = (r["viewList"] + [None] * 8)[:8]
        for v in vl:
            if v is None:
                rm += struct.pack("<BB13h", 0, 0, *([0] * 12), -1)
            else:
                rm += struct.pack("<BB13h", 1 if v["enabled"] else 0, 0, v["x"], v["y"], v["w"], v["h"],
                                  v["px"], v["py"], v["pw"], v["ph"], v["bx"], v["by"], v["sx"], v["sy"],
                                  ref("objects", v["follow"]))
        for i in r["instances"]:
            inst += struct.pack("<hhhHIffIfHH", ref("objects", i["obj"]), i["x"], i["y"], 0, i["id"],
                                i["sx"], i["sy"], i["color"] & 0xFFFFFFFF, i["angle"], code(i["creation"]), 0)
        for t in r["tiles"]:
            til += struct.pack("<hhhHHHHHiIff", ref("backgrounds", t["bg"]), t["x"], t["y"], t["sx"], t["sy"],
                               t["w"], t["h"], 0, t["depth"], t["id"], t["scx"], t["scy"])
    sections += [rm, inst, til]

    order = b"".join(struct.pack("<H", idx["rooms"][n]) for n in game["roomOrder"])
    sections.append(order)

    counts = [len(game["objects"]), len(ev) // 6, len(game["sprites"]), len(game["backgrounds"]),
              len(game["fonts"]), len(gl) // 10, len(game["paths"]), len(pts) // 12,
              len(game["rooms"]), len(inst) // 32, len(til) // 32, len(game["roomOrder"])]
    header_size = 4 + 4 * len(counts) + 4 * len(sections)
    out = bytearray(b"UTG1")
    out += struct.pack(f"<{len(counts)}I", *counts)
    pos = header_size
    offsets = []
    for sec in sections:
        pos = (pos + 3) & ~3
        offsets.append(pos)
        pos += len(sec)
    out += struct.pack(f"<{len(offsets)}I", *offsets)
    for off, sec in zip(offsets, sections):
        out += b"\0" * (off - len(out))
        out += sec
    path.write_bytes(out)
    print(f"game.bin: {len(out)} bytes")


def write_ids_header(comp, path):
    lines = ["/* Generated by tools/vmc.py. */", "#ifndef GEN_IDS_H", "#define GEN_IDS_H", ""]
    lines.append("enum {")
    for i, n in enumerate(OPS):
        lines.append(f"    OP_{n} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("/* Built-in instance variables (ids in the instance variable space). */")
    lines.append("enum {")
    for i, n in enumerate(BUILTIN_VARS):
        lines.append(f"    V_{n} = {i},")
    lines.append(f"    V_BUILTIN_COUNT = {len(BUILTIN_VARS)},")
    lines.append("};")
    lines.append("")
    lines.append("/* Built-in functions called by the game. */")
    lines.append("enum {")
    for i, n in enumerate(comp.builtin_funcs.list):
        lines.append(f"    F_{n} = {i},")
    lines.append(f"    F_COUNT = {len(comp.builtin_funcs.list)},")
    lines.append("};")
    lines.append("")
    lines.append("#ifdef VM_NAMES")
    lines.append("static const char *const builtin_func_names[] = {")
    for n in comp.builtin_funcs.list:
        lines.append(f'    "{n}",')
    lines.append("};")
    lines.append("#endif")
    lines.append("")
    lines.append("#endif")
    path.write_text("\n".join(lines) + "\n")


# Names that native reimplementations of hot code (vm/native.c) use.
NATIVE_NAMES = {
    "instvars": ["vtext", "myx", "myy", "writingx", "writingy", "writingxend", "vspacing", "stringpos",
                 "originalstring", "mycolor", "halt", "stringno", "mystring", "textspeed", "myfont", "shake",
                 "spacing", "htextscale", "vtextscale", "dogcheck"],
    "globals": ["language", "typer", "flag", "faceemotion", "facechoice", "facechange", "currentroom", "entrance",
                "interact", "facing"],
    "fonts": ["fnt_comicsans", "fnt_papyrus"],
    "codes": ["gml_Object_obj_base_writer_Draw_0", "gml_Script_SCR_TEXTTYPE", "gml_Script_scr_replace_buttons_pc",
              "gml_Script_scr_save", "gml_Script_scr_dogcheck"],
    # purely visual particles, created by the hundred (monster dust, Asgore's
    # background): runtime.c keeps only so many of each
    "particles": ["obj_glowparticle_1", "obj_whtpxlgrav", "obj_blkpxlgrav", "obj_blkpxltall", "obj_blkpxl0tall",
                  "obj_orangeparticle", "obj_snowhatparticle"],
}


def write_native_header(comp, game, path):
    lines = ["/* Generated by tools/vmc.py: ids for vm/native.c (-1: not in the game). */",
             "#ifndef GEN_NATIVE_H", "#define GEN_NATIVE_H", ""]
    for n in NATIVE_NAMES["instvars"]:
        lines.append(f"#define IV_{n} {comp.instvars.ids.get(n, -1)}")
    for n in NATIVE_NAMES["globals"]:
        lines.append(f"#define GV_{n} {comp.globals.ids.get(n, -1)}")
    fonts = [f["name"] for f in game["fonts"]]
    for n in NATIVE_NAMES["fonts"]:
        lines.append(f"#define FONT_{n} {fonts.index(n) if n in fonts else -1}")
    for n in NATIVE_NAMES["codes"]:
        lines.append(f"#define CODE_{n} {comp.code_ids.get(n, -1)}")
    objs = [o["name"] for o in game["objects"]]
    ids = [str(objs.index(n)) for n in NATIVE_NAMES["particles"] if n in objs]
    lines.append(f"#define PARTICLE_OBJECTS {', '.join(ids)}")
    lines.append(f"#define OBJ_obj_battler {objs.index('obj_battler')}")
    lines += ["", "#endif"]
    path.write_text("\n".join(lines) + "\n")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exp, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    game = json.loads((exp / "game.json").read_text())
    comp = Compiler(game)

    # Every code entry gets an id; scripts reference theirs by code id.
    asm_files = sorted((exp / "asm").glob("*.asm"))
    for f in asm_files:
        comp.code_id(f.stem)
    text = read_text_table((exp / "asm" / "gml_Script_textdata_en.asm").read_text())
    print(f"text table: {len(text)} entries")
    # Every built-in the game calls gets a stable id, even ones only the
    # stubbed scripts used, so the runtime's F_* names always exist.
    for f in asm_files:
        for fname in re.findall(r"call\.i ([A-Za-z_][A-Za-z_0-9]*)\(argc=", f.read_text(errors="replace")):
            if fname not in comp.scripts and fname not in NATIVE_SCRIPTS:
                comp.builtin_funcs.add(fname)
    comp.builtin_funcs.add("vm_gettext")
    comp.builtin_funcs.add("vm_texttable")
    blobs = {}
    for f in asm_files:
        if f.stem in STUB_SCRIPTS:
            # global.<name> = vm_texttable()
            stub = (f"call.i vm_texttable(argc=0)\npop.v.v global.{STUB_SCRIPTS[f.stem]}\n")
            blobs[f.stem] = comp.compile_entry(f.stem, stub)
        else:
            blobs[f.stem] = comp.compile_entry(f.stem, f.read_text(errors="replace"))
    comp.text = text

    # game.bin adds its names to the string table, so it goes first.
    write_game_bin(game, comp, outdir / "game.bin")

    # code.bin
    ncode = len(comp.code_names)
    names_sid = [comp.strings.add(n) for n in comp.code_names]
    for n in comp.globals.list + comp.instvars.list:
        comp.strings.add(n)
    text_keys = sorted(comp.text)
    text_table = b"".join(struct.pack("<HH", comp.strings.add(k), comp.strings.add(comp.text[k])) for k in text_keys)
    header_size = 4 + 12 * 4
    code_blob = bytearray()
    code_table = bytearray()
    for n, sid in zip(comp.code_names, names_sid):
        if n not in blobs:
            die(f"called code entry {n} has no asm")
        bc, nloc = blobs[n]
        code_table += struct.pack("<IHH", len(code_blob), nloc, sid)
        code_blob += bc
    script_table = bytearray()
    for s in game["scripts"]:
        cid = comp.code_ids.get(s["code"], 0xFFFF) if s["code"] else 0xFFFF
        script_table += struct.pack("<H", cid)
    global_names = b"".join(struct.pack("<H", comp.strings.ids[n]) for n in comp.globals.list)
    inst_names = b"".join(struct.pack("<H", comp.strings.ids[n]) for n in comp.instvars.list)

    str_data = bytearray()
    str_offsets = []
    for s in comp.strings.list:
        str_offsets.append(len(str_data))
        str_data += s.encode("utf-8") + b"\0"

    layout = bytearray()
    pos = header_size
    string_offsets_at = pos
    pos += 4 * len(str_offsets)
    string_data_at = pos
    pos += len(str_data)
    pos = (pos + 3) & ~3
    code_table_at = pos
    pos += len(code_table)
    script_table_at = pos
    pos += len(script_table)
    globalnames_at = pos
    pos += len(global_names)
    instnames_at = pos
    pos += len(inst_names)
    pos = (pos + 3) & ~3
    text_at = pos
    pos += len(text_table)
    pos = (pos + 3) & ~3
    code_at = pos

    out = bytearray(b"UTC1")
    out += struct.pack("<12I", len(comp.strings.list), ncode, len(game["scripts"]),
                       len(comp.globals.list), len(comp.instvars.list),
                       string_offsets_at, code_table_at, script_table_at, globalnames_at, instnames_at,
                       len(text_keys), text_at)
    out += b"".join(struct.pack("<I", string_data_at + o) for o in str_offsets)
    out += str_data
    out += b"\0" * (code_table_at - len(out))
    # code table offsets are relative to code_at
    ct = bytearray()
    for i in range(ncode):
        off, nloc, sid = struct.unpack_from("<IHH", code_table, i * 8)
        ct += struct.pack("<IHH", code_at + off, nloc, sid)
    out += ct + script_table + global_names + inst_names
    out += b"\0" * (text_at - len(out))
    out += text_table
    out += b"\0" * (code_at - len(out))
    out += code_blob
    (outdir / "code.bin").write_bytes(out)

    write_ids_header(comp, ROOT / "vm" / "gen_ids.h")
    write_native_header(comp, game, ROOT / "vm" / "gen_native.h")
    print(f"code.bin: {len(out)} bytes, {ncode} code entries, {len(comp.strings.list)} strings, "
          f"{len(comp.globals.list)} globals, {len(comp.instvars.list)} instance vars, "
          f"{len(comp.builtin_funcs.list)} builtin functions; bytecode {len(code_blob)} bytes")


if __name__ == "__main__":
    main()

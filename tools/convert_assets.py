#!/usr/bin/env python3
"""Turn the UndertaleModTool dump of data.win into CE-ready sources.

Usage: python3 tools/convert_assets.py <dump dir>

<dump dir> is what tools/extract_assets.sh writes: Sprites/, CodeEntries/,
fonts/ and rooms/. Outputs (all generated, safe to delete):

  src/gfx/*.png     intro panels cropped to the visible 200x110 window, the
                    long final panel, and the title logo; indexed to PALETTE
  src/gfx/pal.png   the fixed palette convimg must use (1 pixel per entry)
  src/font.h        fnt_maintext and fnt_small as 1bpp glyphs
  src/gfx/area1_tiles.png, frisk_*.png
                    room_area1's background cut into unique 20x20 tiles, and
                    the player's walking frames
  src/area1_data.h  room_area1's tilemap, walls, start position and door
  src/intro_data.h  the intro story strings and the menu strings (verbatim
                    from textdata_en), the special-name reactions from
                    scr_namingscreen_check, and where the title logo goes
"""
import json
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
GFX = SRC / "gfx"

# obj_introtangle masks everything except this window (x 60-260, y 30-140).
WIN = (60, 30, 260, 140)

# Every color used by the intro panels, the final panel and the title logo.
# Index 0 must stay black: the screen is cleared to it and it never fades.
PALETTE = [
    (0, 0, 0), (44, 23, 8), (74, 41, 11), (100, 50, 20), (133, 74, 29),
    (192, 130, 38), (103, 112, 88), (157, 180, 180), (192, 160, 130),
    (216, 225, 225), (243, 255, 225), (255, 255, 255), (132, 132, 132),
    (237, 28, 36),
    # 14: transparent key for sprites (convimg.yaml's transparent-index)
    (255, 0, 255),
]
TRANSPARENT = 14
# Colors of the overworld are appended after these by add_colors().

# textdata_en entries used by the instruction and naming screens.
MENU_KEYS = [
    "instructions_title", "instructions_confirm_label",
    "instructions_cancel_label", "instructions_menu_label",
    "instructions_quit_label", "instructions_hp0", "instructions_begin",
    "settings_name", "name_entry_title", "name_entry_quit",
    "name_entry_backspace", "name_entry_done", "name_entry_confirm",
    "name_entry_missing", "name_entry_hardmode", "name_entry_goback",
    "yes", "no",
]

# Messages the intro writer shows, in order (obj_introimage Alarm 2).
INTRO_KEYS = [70, 71, 72, 73, 74, 75, 76, 78, 79, 80, 81, 82, 83, 84, 85]


def add_colors(*images):
    for im in images:
        for r, g, b, a in im.convert("RGBA").getdata():
            if a >= 128 and (r, g, b) not in PALETTE:
                PALETTE.append((r, g, b))


def indexed(im, transparent=(0, 0, 0)):
    """Map an RGBA image onto PALETTE exactly and return a 'P' image."""
    im = im.convert("RGBA")
    lookup = {c: i for i, c in enumerate(PALETTE)}
    out = Image.new("P", im.size)
    pal = [v for c in PALETTE for v in c]
    out.putpalette(pal + [0] * (768 - len(pal)))
    px = []
    for r, g, b, a in im.getdata():
        c = (r, g, b) if a >= 128 else transparent
        if c not in lookup:
            sys.exit(f"color {c} is not in PALETTE")
        px.append(lookup[c])
    out.putdata(px)
    return out


def convert_images(dump):
    spr = dump / "Sprites"
    GFX.mkdir(parents=True, exist_ok=True)
    for i in range(11):
        im = Image.open(spr / f"spr_introimage_{i}.png").crop(WIN)
        indexed(im).save(GFX / f"intro{i}.png")
    # The final panel scrolls vertically through the window, so keep its
    # full height. It is split in two because a zx0 block must fit in RAM.
    last = Image.open(spr / "spr_introlast_0.png").crop((WIN[0], 0, WIN[2], 350))
    indexed(last.crop((0, 0, 200, 175))).save(GFX / "last0.png")
    indexed(last.crop((0, 175, 200, 350))).save(GFX / "last1.png")
    title = Image.open(spr / "spr_undertaletitle_0.png")
    box = title.convert("RGB").getbbox()
    indexed(title.crop(box)).save(GFX / "title.png")
    return box


def save_palette():
    pal = Image.new("RGB", (len(PALETTE), 1))
    pal.putdata(PALETTE)
    pal.save(GFX / "pal.png")


# Frisk's walking sprites: spr_mainchara<dir>, frames per direction.
FRISK = {"d": 4, "u": 4, "l": 2, "r": 2}

# Walls in room_area1, by object: size in pixels (from each object's Create
# code: obj_solidlong sets image_xscale = 400, obj_solidtall image_yscale =
# 400) and the collision event obj_mainchara runs for it.
WALLS = {
    "obj_solidsmall": ("WALL_SOLID", 20, 20),
    "obj_solidlong": ("WALL_SOLID", 8000, 20),
    "obj_solidtall": ("WALL_SOLID", 20, 8000),
    "obj_sdr": ("WALL_SDR", 20, 20),
    "obj_sdl": ("WALL_SDL", 20, 20),
    "obj_sur": ("WALL_SUR", 20, 20),
    "obj_sul": ("WALL_SUL", 20, 20),
}


def convert_area1(dump):
    rooms = dump / "rooms"
    room = json.loads((rooms / "room_area1.json").read_text())
    bg = Image.open(rooms / f"{room['backgrounds'][0]['name']}.png").convert("RGBA")
    frames = {d: [Image.open(dump / "Sprites" / f"spr_mainchara{d}_{i}.png") for i in range(n)]
              for d, n in FRISK.items()}
    add_colors(bg, *[f for fs in frames.values() for f in fs])

    # Background -> unique 20x20 tiles, stacked vertically, plus the map.
    tw, th = room["width"] // 20, room["height"] // 20
    tiles, tilemap = [], []
    for ty in range(th):
        for tx in range(tw):
            t = bg.crop((tx * 20, ty * 20, tx * 20 + 20, ty * 20 + 20))
            key = t.tobytes()
            for i, other in enumerate(tiles):
                if other.tobytes() == key:
                    tilemap.append(i)
                    break
            else:
                tilemap.append(len(tiles))
                tiles.append(t)
    strip = Image.new("RGBA", (20, 20 * len(tiles)))
    for i, t in enumerate(tiles):
        strip.paste(t, (0, i * 20))
    indexed(strip).save(GFX / "area1_tiles.png")

    for d, fs in frames.items():
        for i, f in enumerate(fs):
            indexed(f, PALETTE[TRANSPARENT]).save(GFX / f"frisk_{d}{i}.png")

    walls, start, door = [], None, None
    for o in room["objects"]:
        if o["object"] in WALLS:
            kind, w, h = WALLS[o["object"]]
            walls.append(f"    {{{kind}, {o['x']}, {o['y']}, {w}, {h}}}, /* {o['object']} */")
        elif o["object"] == "obj_mainchara":
            start = (o["x"], o["y"])
        elif o["object"] == "obj_doorA":
            door = (o["x"], o["y"])

    out = [
        "/* Generated by tools/convert_assets.py from data.win (room_area1). */",
        "#ifndef AREA1_DATA_H",
        "#define AREA1_DATA_H",
        "",
        f"#define AREA1_W {room['width']}",
        f"#define AREA1_H {room['height']}",
        f"#define AREA1_TILES_W {tw + 1} /* including the padding */",
        f"#define AREA1_TILES_H {th + 1}",
        f"#define AREA1_START_X {start[0]}",
        f"#define AREA1_START_Y {start[1]}",
        f"#define AREA1_DOOR_X {door[0]} /* obj_doorA, 20x20 */",
        f"#define AREA1_DOOR_Y {door[1]}",
        f"#define SPRITE_TRANSPARENT {TRANSPARENT}",
        "",
        "static uint8_t area1_tilemap[] = {",
    ]
    # gfx_Tilemap draws one tile past the screen on each axis while
    # scrolling, so pad the map with a column and a row of tile 0 (black).
    assert tilemap[0] == 0 and not tiles[0].convert("RGB").getbbox(), "tile 0 must be black"
    for ty in range(th + 1):
        row = tilemap[ty * tw:(ty + 1) * tw] if ty < th else [0] * tw
        out.append("    " + ", ".join(str(v) for v in row + [0]) + ",")
    out += ["};", "", "static const wall_t area1_walls[] = {"] + walls + ["};", ""]
    out.append("/* Precise masks of the diagonal walls: 20 rows of 3 bytes, MSB first. */")
    for name in ("sdr", "sdl", "sur", "sul"):
        data = (rooms / f"spr_{name}_mask.bin").read_bytes()
        out.append(f"static const uint8_t mask_{name}[60] = {{")
        for y in range(0, 60, 12):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in data[y:y + 12]) + ",")
        out.append("};")
    out += ["", "#endif", ""]
    (SRC / "area1_data.h").write_text("\n".join(out))
    print(f"area1: {len(tiles)} tiles, {len(walls)} walls, {len(PALETTE)} colors")


def load_font(dump, name):
    sheet = Image.open(dump / "fonts" / f"fnt_{name}.png").convert("RGBA")
    rows = (dump / "fonts" / f"glyphs_fnt_{name}.csv").read_text().splitlines()[1:]
    glyphs = {}
    for row in rows:
        ch, sx, sy, w, h, shift, off = map(int, row.split(";"))
        if not 32 <= ch < 127:
            continue
        bits = []
        for y in range(h):
            v = 0
            for x in range(w):
                if sheet.getpixel((sx + x, sy + y))[3] >= 96:
                    v |= 0x80 >> x
            bits.append(v)
        glyphs[ch] = (w, h, off, shift, bits)
    return glyphs


def emit_font(out, name, glyphs):
    data, table = [], []
    for ch in range(32, 127):
        w, h, off, shift, bits = glyphs.get(ch, (0, 0, 0, 0, []))
        table.append(f"{{{len(data)}, {w}, {h}, {off}, {shift}}}")
        data.extend(bits)
    out.append(f"static const uint8_t {name}_bits[] = {{")
    for i in range(0, len(data), 16):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    out.append("};")
    out.append(f"static const glyph_t {name}_glyphs[95] = {{")
    for i in range(0, 95, 5):
        out.append("    " + ", ".join(table[i:i + 5]) + ",")
    out.append("};")


def convert_fonts(dump):
    out = [
        "/* Generated by tools/convert_assets.py from data.win. */",
        "#ifndef FONT_H",
        "#define FONT_H",
        "#include <stdint.h>",
        "",
        "/* One glyph: offset into the bit rows, size, x offset and advance. Each",
        "   row is one byte, leftmost pixel in bit 7. */",
        "typedef struct { uint16_t start; uint8_t w, h, xoff, shift; } glyph_t;",
        "",
    ]
    emit_font(out, "fnt_maintext", load_font(dump, "maintext"))
    out.append("")
    emit_font(out, "fnt_small", load_font(dump, "small"))
    out += ["", "#endif", ""]
    (SRC / "font.h").write_text("\n".join(out))


def c_str(s):
    return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""


def special_names(dump):
    """The English branch of every name check in scr_namingscreen_check."""
    src = (dump / "CodeEntries" / "gml_Script_scr_namingscreen_check.gml").read_text()
    out = [
        "/* Names with their own reaction on the \"Is this name correct?\" screen.",
        "   allow = 0 means the name is refused. */",
        "typedef struct { const char *name; uint8_t allow; const char *msg; } special_name_t;",
        "static const special_name_t special_names[] = {",
    ]
    block = re.compile(
        r'if \(((?:l_char == "[^"]*"(?: \|\| )?)+)\)\s*\{\s*allow = (\d);.*?\belse\s*\{\s*spec_m = "([^"]*)";',
        re.S)
    for names, allow, msg in block.findall(src):
        for name in re.findall(r'l_char == "([^"]*)"', names):
            if name.isascii():
                out.append(f"    {{{c_str(name)}, {allow}, {c_str(msg)}}},")
    out += ["};", ""]
    return out


def convert_text(dump, title_box):
    src = (dump / "CodeEntries" / "gml_Script_textdata_en.gml").read_text()
    strings = {}
    for key, val in re.findall(r'ds_map_add\(global\.text_data_en, "([^"]+)", "((?:[^"\\]|\\.)*)"\)', src):
        strings[key] = val
    out = [
        "/* Generated by tools/convert_assets.py from data.win (textdata_en). */",
        "#ifndef INTRO_DATA_H",
        "#define INTRO_DATA_H",
        "",
        f"#define LOGO_X {title_box[0]}",
        f"#define LOGO_Y {title_box[1]}",
        "",
        "static const char *const intro_msg[] = {",
    ]
    for k in INTRO_KEYS:
        s = strings[f"obj_introimage_{k}"]
        out.append("    " + c_str(s) + ",")
    out += ["};", ""]
    for k in MENU_KEYS:
        out.append(f"#define STR_{k} " + c_str(strings[k]))
    out.append("")
    out += special_names(dump)
    out += ["#endif", ""]
    (SRC / "intro_data.h").write_text("\n".join(out))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    dump = Path(sys.argv[1])
    box = convert_images(dump)
    convert_area1(dump)
    save_palette()
    convert_fonts(dump)
    convert_text(dump, box)


if __name__ == "__main__":
    main()

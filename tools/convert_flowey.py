#!/usr/bin/env python3
"""Extract the first Flowey encounter art from the owner's Undertale dump.

Usage: python3 tools/convert_flowey.py <export_game dir> <sprite dump dir>
The sprite dump is the directory containing Sprites/*.png. The game export
contains backgrounds/bg_floweyglow.png and game.json.
"""
import json
import ast
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GFX = ROOT / "src/gfx"

ROOM_SPRITES = {
    "flowey": 2, "floweygrow": 9, "floweyshrink": 9,
    "floweysink": 6, "floweylaughoverworld": 3, "toriel_d": 4,
}
BATTLE_SPRITES = {
    "floweynice": 2, "floweyniceside": 2, "floweynicesideum": 2,
    "floweysassy": 2, "floweyevil": 2, "floweygrin": 2,
    "floweylaugh": 2, "floweypissed": 2, "floweywink": 1,
    "floweyside": 1, "floweysideshock": 1, "floweyhurt": 1,
    "winkstar": 1,
    "torielside1": 2, "torielcutscene": 2, "torielflame": 4,
    "face_torielhappytalk": 2,
}


def literal_text(expr):
    """Read GML's quoted string literals and literal concatenations only."""
    def value(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return value(node.left) + value(node.right)
        raise ValueError("not a literal text expression")
    return value(ast.parse(expr, mode="eval").body)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    export, dump = map(Path, sys.argv[1:])
    sprites = dump / "Sprites"
    game = json.loads((export / "game.json").read_text())
    palette = list(Image.open(GFX / "pal.png").convert("RGB").getdata())
    transparent = palette[14]
    bg = Image.open(export / "backgrounds/bg_floweyglow.png").convert("RGBA")
    src = {}
    for name, count in (ROOM_SPRITES | BATTLE_SPRITES).items():
        info = next(s for s in game["sprites"] if s["name"] == "spr_" + name)
        assert info["frames"] >= count, name
        for i in range(count):
            src[f"{name}{i}"] = Image.open(sprites / f"spr_{name}_{i}.png").convert("RGBA")
    src["bubble0"] = Image.open(sprites / "spr_blconwdshrt_0.png").convert("RGBA").resize(
        (120, 52), Image.Resampling.NEAREST)
    for i in range(2):
        src[f"battleheart{i}"] = Image.open(sprites / f"spr_heart_{i}.png").convert("RGBA").resize(
            (8, 8), Image.Resampling.NEAREST)
        src[f"battlebullet{i}"] = Image.open(sprites / f"spr_spinbullet_{i}.png").convert("RGBA").resize(
            (6, 6), Image.Resampling.NEAREST)
    for im in [bg, *src.values()]:
        for r, g, b, a in im.getdata():
            c = (r, g, b)
            if a >= 128 and c not in palette:
                palette.append(c)
    assert len(palette) < 250, len(palette)
    pal = Image.new("RGB", (len(palette), 1))
    pal.putdata(palette)
    pal.save(GFX / "pal.png")
    lookup = {c: i for i, c in enumerate(palette)}

    def indexed(im, key):
        out = Image.new("P", im.size)
        out.putpalette([v for c in palette for v in c] + [0] * (768 - 3 * len(palette)))
        out.putdata([lookup[(r, g, b) if a >= 128 else key] for r, g, b, a in im.getdata()])
        return out

    # Preserve GameMaker's background position (0,0) in a 320x420 room.
    canvas = Image.new("RGBA", (320, 420), (0, 0, 0, 255))
    canvas.paste(bg, (0, 0))
    tiles, lookup_tiles, tilemap = [], {}, []
    for y in range(0, 420, 20):
        for x in range(0, 320, 20):
            tile = canvas.crop((x, y, x + 20, y + 20))
            key = tile.tobytes()
            if key not in lookup_tiles:
                lookup_tiles[key] = len(tiles)
                tiles.append(tile)
            tilemap.append(lookup_tiles[key])
    strip = Image.new("RGBA", (20, 20 * len(tiles)))
    for i, tile in enumerate(tiles):
        strip.paste(tile, (0, 20 * i))
    indexed(strip, (0, 0, 0)).save(GFX / "flowey_tiles.png")
    for name, im in src.items():
        indexed(im, transparent).save(GFX / f"{name}.png")

    lines = ["/* Generated from room_area1_2 in data.win. */", "#ifndef FLOWEY_DATA_H",
             "#define FLOWEY_DATA_H", "", "#define FLOWEY_ROOM_W 320",
             "#define FLOWEY_ROOM_H 420", "#define FLOWEY_TILES_W 17",
             "#define FLOWEY_TILES_H 22", "static uint8_t flowey_tilemap[] = {"]
    for y in range(22):
        row = tilemap[y * 16:(y + 1) * 16] if y < 21 else [0] * 16
        lines.append("    " + ", ".join(map(str, row + [0])) + ",")
    room = next(r for r in game["rooms"] if r["name"] == "room_area1_2")
    walls = []
    for obj in room["instances"]:
        n = obj["obj"]
        if n in ("obj_solidsmall", "obj_solidtall", "obj_solidlong"):
            w, h = ((20, 8000) if n == "obj_solidtall" else
                    (8000, 20) if n == "obj_solidlong" else (20, 20))
            walls.append((obj["x"], obj["y"], w, h))
    lines += ["};", "", "static const int16_t flowey_walls[][4] = {"]
    lines += ["    {" + ", ".join(map(str, w)) + "}," for w in walls]
    lines += ["};", "", "#endif", ""]
    (ROOT / "src/flowey_data.h").write_text("\n".join(lines))

    def section(name, image_names, tiles=False):
        if tiles:
            return ["  - name: " + name, "    palette: global_palette", "    tilesets:",
                    "      tile-width: 20", "      tile-height: 20", "      images:",
                    "        - flowey_tiles.png"]
        return ["  - name: " + name, "    palette: global_palette",
                "    transparent-index: 14", "    images:"] + ["      - " + x + ".png" for x in image_names]

    room_a = [f"{n}{i}" for n in ("flowey", "floweygrow", "toriel_d")
              for i in range(ROOM_SPRITES[n])]
    room_b = [f"{n}{i}" for n in ("floweyshrink", "floweysink", "floweylaughoverworld")
              for i in range(ROOM_SPRITES[n])]
    battle_a = [f"{n}{i}" for n in ("floweynice", "floweyniceside", "floweynicesideum",
                "floweysassy", "floweyevil", "floweygrin", "floweylaugh",
                "floweypissed", "floweywink", "floweyside", "floweysideshock",
                "floweyhurt")
                for i in range(BATTLE_SPRITES[n])]
    battle_b = ["bubble0", "battleheart0", "battleheart1", "battlebullet0", "battlebullet1"]
    battle_b += [f"{n}{i}" for n in ("winkstar",
                "torielside1", "torielcutscene", "torielflame",
                "face_torielhappytalk")
                for i in range(BATTLE_SPRITES[n])]
    yaml = ["palettes:", "  - name: global_palette", "    fixed-entries:",
            "      - image: pal.png", "    images: automatic", "", "converts:"]
    yaml += section("flowey_tiles", [], True)
    yaml += section("flowey_room_a", room_a)
    yaml += section("flowey_room_b", room_b)
    yaml += section("flowey_battle_a", battle_a)
    yaml += section("flowey_battle_b", battle_b)
    yaml += ["", "outputs:"]
    for name, converts in (("UTFLOW0", ["flowey_tiles"]),
                           ("UTFLOW1", ["flowey_room_a"]),
                           ("UTFLOW2", ["flowey_room_b"]),
                           ("UTFLOW3", ["flowey_battle_a"]),
                           ("UTFLOW4", ["flowey_battle_b"])):
        yaml += ["  - type: appvar", "    name: " + name, "    source-format: c",
                 "    include-file: " + name.lower() + ".h", "    archived: true",
                 "    palettes:", "      - global_palette", "    converts:"]
        yaml += ["      - " + x for x in converts]
    (GFX / "flowey.yaml").write_text("\n".join(yaml) + "\n")
    textfile = dump / "CodeEntries/gml_Script_textdata_en.gml"
    messages = {}
    for line in textfile.read_text().splitlines():
        m = re.match(r'ds_map_add\(global\.text_data_en, "([^"]+)", (.*)\);', line)
        if m:
            try:
                messages[m[1]] = literal_text(m[2])
            except (SyntaxError, ValueError):
                pass
    groups = {
        "flowey_opening": [f"SCR_TEXT_{n}" for n in range(291, 298)],
        "flowey_soul": [f"SCR_TEXT_{n}" for n in range(3495, 3500)],
        "flowey_pellets": [f"SCR_TEXT_{n}" for n in range(3503, 3506)],
        "flowey_move": ["SCR_TEXT_3509"],
        "flowey_missed": ["SCR_TEXT_3524", "SCR_TEXT_3525"],
        "flowey_lastchance": ["SCR_TEXT_3528"],
        "flowey_hit": [f"SCR_TEXT_{n}" for n in range(3514, 3517)],
        "flowey_know": ["SCR_TEXT_3531", "SCR_TEXT_3532"],
        "flowey_die": ["SCR_TEXT_3520"],
        "toriel_rescue": [f"SCR_TEXT_{n}" for n in range(3535, 3540)] + ["SCR_TEXT_3541"],
        "toriel_room": ["SCR_TEXT_301"],
    }
    out = ["/* Dialogue extracted from the owner's data.win textdata_en. */",
           "#ifndef FLOWEY_TEXT_H", "#define FLOWEY_TEXT_H", ""]
    for group, keys in groups.items():
        out.append(f"static const char *const {group}[] = {{")
        for key in keys:
            assert key in messages, key
            out.append("    " + json.dumps(messages[key], ensure_ascii=True) + ",")
        out += ["};", ""]
    out += ["#endif", ""]
    (ROOT / "src/flowey_text.h").write_text("\n".join(out))
    print(f"Flowey: {len(tiles)} tiles, {len(palette)} shared colors, {len(src)} sprites")


if __name__ == "__main__":
    main()

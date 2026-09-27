#!/usr/bin/env python3
"""Split the game into chapter packs for the calculator: which rooms,
code, sprites, backgrounds and fonts each chapter needs, as list files for
tools/cepack.py (--sprites/--rooms/--code all take the same file).

Usage: chapters.py <export dir> <vm dir> <decompiled GML dir> <out dir>

What a chapter needs is found by following names from its rooms: a room's
instances, creation code and backgrounds; an object's events, sprite, mask
and parent; the objects, sprites, backgrounds, fonts and scripts a piece of
code names (decompiled GML, where assets appear by name), and the monsters
of every battle group it starts (global.battlegroup = N). Rooms a chapter
goes to that no chapter lists (shops, date rooms...) join it. Scripts that
name nearly everything (the battle group table) are not followed by name.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cepack import CodeBin  # noqa: E402

# chapter: (first room, last room) by index, plus extra rooms by name
CHAPTERS = [
    ("ruins", 0, 47, ["room_floweybattle", "room_storybattle"]),
    ("snowdin", 48, 64, ["room_icecave1", "room_ice_dog"]),
    ("snowdin2", 65, 81, ["room_shop1", "room_papdate"]),
    ("waterfall", 82, 105, ["room_water16A", "room_water_mushroom", "room_shop2"]),
    ("waterfall2", 106, 130, ["room_shop3"]),
    ("hotland", 131, 148, ["room_fire_labelevator", "room_quizholder", "room_adate"]),
    ("hotland2", 149, 168, ["room_shop4"]),
    ("core", 169, 215, ["room_bringitinguys", "room_shop5"]),
    ("castle", 216, 241, ["room_flowey_endchoice", "room_flowey_regret", "room_nothingness", "room_undertale"]),
    ("omega", 0, -1, ["room_floweyx", "room_f_start", "room_f_intro", "room_f_menu", "room_f_room", "room_f_phrase"]),
    ("truelab", 242, 264, []),
    ("ending", 239, 241, ["room_end_castroll", "room_end_highway", "room_end_beach", "room_end_metta",
                          "room_end_school", "room_end_mtebott", "room_creditsdodger", "room_end_myroom",
                          "room_end_theend", "room_asrielappears", "room_goodbyeasriel", "room_asrielmemory"]),
]
# in every pack
COMMON_ROOMS = ["room_start", "room_introstory", "room_introimage", "room_intromenu", "room_battle",
                "room_fastbattle", "room_gameover", "room_empty", "room_emptywhite", "room_emptyblack",
                "room_of_dog", "room_riverman_transition"]
# sprites only the first pack needs
INTRO_ONLY = ["spr_introimage", "spr_introlast"]
# named too widely to follow (handled otherwise)
HUBS = {"gml_Script_scr_battlegroup", "gml_Script_SCR_TEXT", "gml_Script_scr_draw_screen_border",
        "gml_Script_scr_preload_textures"}


def main():
    exp, vmdir, gmldir, out = map(Path, sys.argv[1:5])
    out.mkdir(parents=True, exist_ok=True)
    game = json.loads((exp / "game.json").read_text())
    cb = CodeBin(vmdir / "code.bin")
    code_id = {name: i for i, (_, _, name) in enumerate(cb.code)}

    objects = {o["name"]: o for o in game["objects"]}
    obj_idx = {o["name"]: o["index"] for o in game["objects"]}
    sprites = {s["name"]: s["index"] for s in game["sprites"]}
    bgs = {b["name"]: b["index"] for b in game["backgrounds"]}
    fonts = {f["name"]: f["index"] for f in game["fonts"]}
    rooms = {r["name"]: r for r in game["rooms"]}
    scripts = {s["name"]: s["code"] for s in game["scripts"] if s["code"]}
    names = set(objects) | set(sprites) | set(bgs) | set(fonts) | set(rooms) | set(scripts)

    # the names each code entry mentions, and the battle groups it starts
    word = re.compile(r"\b[A-Za-z_]\w*\b")
    group_re = re.compile(r"global\.battlegroup\s*=\s*(\d+)")
    # Objects count only where code makes one (with (x), instance_exists(x)
    # and the like work on what the rooms already have).
    create_re = re.compile(r"\b(?:instance_create|instance_change|action_create_object)\s*\(([^;]*)")
    refs, groups = {}, {}
    for f in gmldir.glob("*.gml"):
        text = f.read_text(errors="replace")
        made = set()
        for m in create_re.finditer(text):
            made |= set(word.findall(m.group(1))) & set(objects)
        refs[f.stem] = ((set(word.findall(text)) & names) - set(objects)) | made
        groups[f.stem] = {int(g) for g in group_re.findall(text)}

    # battle group -> monster objects (scr_battlegroup's cases)
    bgtext = (gmldir / "gml_Script_scr_battlegroup.gml").read_text()
    group_objs = {}
    parts = re.split(r"\n    case (\d+):", bgtext)
    for i in range(1, len(parts), 2):
        group_objs[int(parts[i])] = set(re.findall(r"instance_create\([^,]+, [^,]+, (\w+)\)", parts[i + 1]))
    bg_common = set(re.findall(r"\b(\w+)\b", parts[0])) & names  # before the switch: music etc.

    def expand(extra):
        out = []
        for n in extra:
            if n.startswith("@"):
                lo, hi = map(int, n[1:].split("-"))
                out += [r["name"] for r in game["rooms"][lo:hi + 1]]
            else:
                out.append(n)
        return out

    CH = [(c, a, b, expand(extra)) for c, a, b, extra in CHAPTERS]
    assigned = {}
    for ci, (_, a, b, extra) in enumerate(CH):
        for r in game["rooms"][a:b + 1]:
            assigned[r["name"]] = ci
        for n in extra:
            assigned[n] = ci

    def closure(root_rooms, root_objs):
        need = {"room": set(), "code": set(), "sprite": set(), "bg": set(), "font": set()}
        todo = [("room", r) for r in root_rooms] + [("obj", o) for o in root_objs]
        seen = set()
        via = {}
        new_rooms = []
        while todo:
            item = todo.pop()
            kind, n = item[0], item[1]
            if (kind, n) in seen:
                continue
            seen.add((kind, n))
            via[(kind, n)] = item[2] if len(item) > 2 else None
            cur = (kind, n)
            if kind == "room":
                r = rooms[n]
                need["room"].add(r["index"])
                if r["creation"]:
                    todo.append(("code", r["creation"], cur))
                for inst in r["instances"]:
                    todo.append(("obj", inst["obj"], cur))
                    if inst.get("creation"):
                        todo.append(("code", inst["creation"], cur))
                for b in r["backgrounds"]:
                    if b.get("bg"):
                        todo.append(("name", b["bg"], cur))
                for t in r["tiles"]:
                    if t.get("bg"):
                        todo.append(("name", t["bg"], cur))
            elif kind == "obj":
                o = objects.get(n)
                if not o:
                    continue
                for e in o["events"]:
                    todo.append(("code", e["code"], cur))
                for key in ("sprite", "mask"):
                    if o[key]:
                        todo.append(("name", o[key], cur))
                if o["parent"]:
                    todo.append(("obj", o["parent"], cur))
            elif kind == "code":
                if n in code_id:
                    need["code"].add(code_id[n])
                if n in HUBS:
                    continue
                for m in refs.get(n, ()):
                    todo.append(("name", m, cur))
                for g in groups.get(n, ()):
                    for o in group_objs.get(g, ()):
                        todo.append(("obj", o, cur))
            elif kind == "name":
                if re.search(r"_ja(_|$)", n):
                    continue  # Japanese version only
                if n in objects:
                    todo.append(("obj", n, cur))
                elif n in sprites:
                    need["sprite"].add(sprites[n])
                elif n in bgs:
                    need["bg"].add(bgs[n])
                elif n in fonts:
                    need["font"].add(fonts[n])
                elif n in scripts:
                    todo.append(("code", scripts[n], cur))
                elif n in rooms:
                    pass  # rooms belong to chapters (CHAPTERS, COMMON_ROOMS)
        closure.via = via
        return need, new_rooms

    # everything the battle group table and the common rooms need, always
    for ci, (cname, a, b, extra) in enumerate(CH):
        roots = [r["name"] for r in game["rooms"][a:b + 1]] + extra + COMMON_ROOMS
        while True:
            need, new_rooms = closure(roots, list(bg_common & set(objects)))
            if not new_rooms:
                break
            roots += new_rooms
        if ci > 0:
            # the intro plays at every start, but its pictures only matter
            # before a new game (the first pack); elsewhere it shows text
            for n in INTRO_ONLY:
                need["sprite"].discard(sprites.get(n, -1))
        need["code"].add(code_id["gml_Script_scr_battlegroup"])
        for n in bg_common:
            if n in sprites:
                need["sprite"].add(sprites[n])
        import os
        for q in os.environ.get("WHY", "").split(","):
            if q:
                k = ("name", q)
                chain = []
                while k in closure.via and len(chain) < 30:
                    chain.append(k[1])
                    k = closure.via[k]
                print(f"  {cname}: why {q}: " + " <- ".join(chain))
        lines = [f"{k} {v}" for k in need for v in sorted(need[k])]
        if ci > 0:
            lines += [f"dropsprite {sprites[n]}" for n in INTRO_ONLY]
        (out / f"{ci + 1}_{cname}.txt").write_text("\n".join(lines) + "\n")
        print(f"{cname}: {len(need['room'])} rooms, {len(need['code'])} code, {len(need['sprite'])} sprites, "
              f"{len(need['bg'])} bgs, {len(need['font'])} fonts")


if __name__ == "__main__":
    main()

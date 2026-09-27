#!/usr/bin/env python3
"""Pack every sprite frame, background, font sheet and collision mask into
gfx.bin with one shared 256-color palette (index 0 = transparent).

Usage: python3 tools/gfxpack.py <export dir> <sprite dir> <out dir>

<export dir>: tools/export_game.csx output (game.json, backgrounds/, fonts/,
masks/). <sprite dir>: `UndertaleModCli dump --sprites` output (Sprites/).

The palette is chosen in the CE's 15-bit color space (the LCD palette is
1555), with weighted k-means over every opaque pixel of the game.

gfx.bin (little endian):
  "UTX1", u32 nsprites, nframes, nbgs, nfonts,
  u32 palette_at, sprite_table_at, frame_table_at, bg_table_at,
      font_table_at, mask_table_at
  palette: 256 x (r, g, b) bytes
  sprite table: per sprite u32 first frame
  frame table: per frame u32 pixel offset (w*h bytes, sprite dims)
  bg table: per background u32 pixel offset (w*h bytes)
  font table: per font u16 w, u16 h, u32 offset (w*h bytes, 0/1 coverage)
  mask table: per sprite u32 offset (0 = none), u16 count, u16 pad;
      each mask is h rows of ceil(w/8) bytes, MSB first
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load_rgba(path, w=None, h=None):
    im = Image.open(path).convert("RGBA")
    if w is not None and im.size != (w, h):
        canvas = Image.new("RGBA", (w, h))
        canvas.paste(im, (0, 0))
        im = canvas
    return np.asarray(im, dtype=np.uint8)


def to15(a):
    return ((a[..., 0].astype(np.int32) >> 3) << 10) | ((a[..., 1].astype(np.int32) >> 3) << 5) | (a[..., 2].astype(np.int32) >> 3)


def build_palette(hist, ncolors=255, iters=12):
    """Weighted k-means over 15-bit colors; returns ncolors x 3 (0-31)."""
    keys = np.nonzero(hist)[0]
    w = np.sqrt(hist[keys].astype(np.float64))
    pts = np.stack([(keys >> 10) & 31, (keys >> 5) & 31, keys & 31], axis=1).astype(np.float64)
    # start from the most common colors
    order = np.argsort(-hist[keys])
    centers = pts[order[:ncolors]].copy()
    if len(centers) < ncolors:
        return centers
    for _ in range(iters):
        d = ((pts[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        lab = d.argmin(axis=1)
        for k in range(ncolors):
            m = lab == k
            if m.any():
                centers[k] = (pts[m] * w[m, None]).sum(axis=0) / w[m].sum()
    # snap centers to exact common colors where they're close, so flat
    # sprite colors stay exact
    d = ((pts[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
    lab = d.argmin(axis=1)
    for k in range(ncolors):
        m = np.nonzero(lab == k)[0]
        if len(m):
            best = m[hist[keys[m]].argmax()]
            centers[k] = pts[best]
    return np.round(centers)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    exp, sprdir, outdir = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    game = json.loads((exp / "game.json").read_text())

    images = []  # (kind, key, array)
    for sp in game["sprites"]:
        for f in range(sp["frames"]):
            p = sprdir / f"{sp['name']}_{f}.png"
            if p.exists():
                a = load_rgba(p, sp["width"], sp["height"])
            else:
                a = np.zeros((sp["height"], sp["width"], 4), np.uint8)
            images.append(("spr", (sp["index"], f), a))
    for b in game["backgrounds"]:
        p = exp / "backgrounds" / f"{b['name']}.png"
        if p.exists() and b["width"]:
            images.append(("bg", b["index"], load_rgba(p, b["width"], b["height"])))
        else:
            images.append(("bg", b["index"], np.zeros((max(1, b["height"]), max(1, b["width"]), 4), np.uint8)))

    hist = np.zeros(32768, np.int64)
    for _, _, a in images:
        op = a[..., 3] >= 128
        if op.any():
            hist += np.bincount(to15(a)[op], minlength=32768)
    print(f"{int((hist > 0).sum())} distinct 15-bit colors")
    centers = build_palette(hist)
    ncol = len(centers)
    # 15-bit color -> palette index (1-based) lookup
    allc = np.stack([(np.arange(32768) >> 10) & 31, (np.arange(32768) >> 5) & 31, np.arange(32768) & 31], axis=1).astype(np.float64)
    lut = np.empty(32768, np.uint8)
    for s in range(0, 32768, 4096):
        d = ((allc[s:s + 4096, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        lut[s:s + 4096] = d.argmin(axis=1) + 1

    def index(a):
        idx = lut[to15(a)]
        idx[a[..., 3] < 128] = 0
        return idx.astype(np.uint8)

    pixels = bytearray()
    frame_offsets = []
    sprite_first = []
    bg_offsets = []
    for kind, key, a in images:
        if kind == "spr":
            if key[1] == 0:
                sprite_first.append(len(frame_offsets))
            frame_offsets.append(len(pixels))
        else:
            bg_offsets.append(len(pixels))
        pixels += index(a).tobytes()
    # sprites with zero frames still need a table entry
    sprite_first = []
    n = 0
    for sp in game["sprites"]:
        sprite_first.append(n)
        n += sp["frames"]

    fonts = bytearray()
    font_table = []
    for f in game["fonts"]:
        p = exp / "fonts" / f"{f['name']}.png"
        a = load_rgba(p) if p.exists() else np.zeros((1, 1, 4), np.uint8)
        cov = (a[..., 3] >= 96).astype(np.uint8)
        font_table.append((a.shape[1], a.shape[0], len(pixels) + len(fonts)))
        fonts += cov.tobytes()
    pixels += fonts

    masks = bytearray()
    mask_table = []
    for sp in game["sprites"]:
        if sp["sep"] != "Precise":
            mask_table.append((0, 0))
            continue
        blobs = []
        m = 0
        while (exp / "masks" / f"{sp['name']}_{m}.bin").exists():
            blobs.append((exp / "masks" / f"{sp['name']}_{m}.bin").read_bytes())
            m += 1
        if not blobs:
            mask_table.append((0, 0))
            continue
        mask_table.append((len(masks) + 1, len(blobs)))  # +1: 0 means none
        for b in blobs:
            masks += b

    header = 4 + 4 * 4 + 6 * 4
    palette_at = header
    sprite_table_at = palette_at + 768
    frame_table_at = sprite_table_at + 4 * len(sprite_first)
    bg_table_at = frame_table_at + 4 * len(frame_offsets)
    font_table_at = bg_table_at + 4 * len(bg_offsets)
    mask_table_at = font_table_at + 8 * len(font_table)
    masks_at = mask_table_at + 8 * len(mask_table)
    pixels_at = masks_at + len(masks)

    out = bytearray(b"UTX1")
    out += struct.pack("<4I", len(sprite_first), len(frame_offsets), len(bg_offsets), len(font_table))
    out += struct.pack("<6I", palette_at, sprite_table_at, frame_table_at, bg_table_at, font_table_at, mask_table_at)
    pal = bytearray(3)  # index 0 transparent (black)
    for c in centers:
        pal += bytes(int(v) * 255 // 31 for v in c)
    pal += bytes(768 - len(pal))
    out += pal
    out += b"".join(struct.pack("<I", v) for v in sprite_first)
    out += b"".join(struct.pack("<I", pixels_at + v) for v in frame_offsets)
    out += b"".join(struct.pack("<I", pixels_at + v) for v in bg_offsets)
    out += b"".join(struct.pack("<HHI", w, h, pixels_at + o) for w, h, o in font_table)
    out += b"".join(struct.pack("<IHH", (masks_at + o - 1) if o else 0, c, 0) for o, c in mask_table)
    out += masks
    out += pixels
    (outdir / "gfx.bin").write_bytes(out)
    print(f"gfx.bin: {len(out)} bytes, {len(frame_offsets)} frames, {len(bg_offsets)} backgrounds, {ncol} colors")


if __name__ == "__main__":
    main()

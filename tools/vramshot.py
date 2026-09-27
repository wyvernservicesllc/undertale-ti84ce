#!/usr/bin/env python3
"""Turn cemu-autotester memory dumps (VRAM 0xD40000, 153600 bytes, and the
LCD palette, 512 bytes) into PNG screenshots.

Usage: vramshot.py <vram dump> <palette dump> <out.png>
Writes out.png with three panels: 8 bpp buffer 0, 8 bpp buffer 1 (both via
the 1555 palette) and the 16 bpp (RGB565) view the OS uses."""
import struct
import sys

from PIL import Image

vram = open(sys.argv[1], "rb").read()
pal = open(sys.argv[2], "rb").read()
colors = []
for i in range(256):
    c = struct.unpack_from("<H", pal, i * 2)[0]
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    colors.append((r * 255 // 31, g * 255 // 31, b * 255 // 31))
out = Image.new("RGB", (330 * 3, 240))
for k in range(2):
    im = Image.new("RGB", (320, 240))
    im.putdata([colors[p] for p in vram[k * 76800:(k + 1) * 76800]])
    out.paste(im, (k * 330, 0))
im = Image.new("RGB", (320, 240))
px = []
for i in range(76800):
    c = struct.unpack_from("<H", vram, i * 2)[0]
    px.append(((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
im.putdata(px)
out.paste(im, (660, 0))
out.save(sys.argv[3])

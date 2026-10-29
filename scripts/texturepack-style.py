#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Hand-tunes an exported image texture pack into the "Storybook 32x" example pack (decision E-303).

Usage: scripts/texturepack-style.py <exported pack dir> <output pack dir>

The input is a pack written by `qtcraft-headless --export-textures` (PNG files at 32 x 32). Every
texture gets a warm colour grade; full block faces get a dark outline (a drawn, storybook look that
is easy to tell apart from the procedural packs); a few textures get hand-placed details (flowers in
the grass, cracks in stone, pebbles in dirt, nails in planks). Only the Python standard library is
used: PNG files are decoded and encoded here (8-bit RGB/RGBA, not interlaced, as Qt writes them).
"""
import json
import os
import shutil
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
# The project's own textures are licensed like the code (decision E-303).
LICENSE = "GPL-3.0-or-later"

# Full block faces: they get the outline. Cut-out textures (plants, items, glass) keep their shape.
OUTLINED = {
    "grass_side", "grass_top", "dirt", "stone", "cobblestone", "sand", "gravel", "oak_log", "oak_log_top",
    "oak_planks", "birch_log", "birch_log_top", "birch_planks", "spruce_log", "spruce_log_top",
    "spruce_planks", "jungle_log", "jungle_log_top", "jungle_planks", "bricks", "stone_bricks", "coal_ore", "iron_ore", "gold_ore", "diamond_ore", "snow",
    "sandstone", "sandstone_top", "bedrock", "clay", "terracotta", "crafting_table_side",
    "crafting_table_top", "furnace_front", "furnace_side", "furnace_top", "chest_front", "chest_side",
    "chest_top", "bookshelf", "obsidian", "polished_stone", "mud_bricks", "packed_ice",
}


def read_png(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(SIGNATURE):
        raise ValueError(f"{path}: not a PNG")
    pos = len(SIGNATURE)
    idat = b""
    width = height = colour_type = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour_type not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA without interlacing")
        elif kind == b"IDAT":
            idat += body
    channels = 4 if colour_type == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                row[i] = (row[i] + left) & 0xFF
            elif kind == 2:
                row[i] = (row[i] + up) & 0xFF
            elif kind == 3:
                row[i] = (row[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                p = left + up - corner
                best = min((abs(p - left), 0, left), (abs(p - up), 1, up), (abs(p - corner), 2, corner))[2]
                row[i] = (row[i] + best) & 0xFF
        rows.append(row)
        previous = row
    pixels = []
    for row in rows:
        for x in range(width):
            px = list(row[x * channels:(x + 1) * channels])
            pixels.append(px + [255] if channels == 3 else px)
    return width, height, pixels


def write_png(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(bytes(pixels[y * width + x]))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as handle:
        handle.write(SIGNATURE)
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def clamp(value):
    return max(0, min(255, int(round(value))))


def grade(pixel):
    """Warm, a little more saturated, a little lifted shadows."""
    r, g, b, a = pixel
    grey = 0.3 * r + 0.59 * g + 0.11 * b
    r, g, b = (grey + (c - grey) * 1.2 for c in (r, g, b))
    return [clamp(r * 1.07 + 6), clamp(g * 1.02 + 4), clamp(b * 0.9), a]


def shade(pixel, factor):
    return [clamp(pixel[0] * factor), clamp(pixel[1] * factor), clamp(pixel[2] * factor), pixel[3]]


def outline(width, height, pixels):
    for y in range(height):
        for x in range(width):
            if x in (0, width - 1) or y in (0, height - 1):
                pixels[y * width + x] = shade(pixels[y * width + x], 0.55)
            elif x in (1, width - 2) or y in (1, height - 2):
                pixels[y * width + x] = shade(pixels[y * width + x], 0.85)


def dots(width, pixels, spots, colour, centre=None):
    for x, y in spots:
        pixels[y * width + x] = list(colour) + [255]
        if centre:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                pixels[(y + dy) * width + x + dx] = list(colour) + [255]
            pixels[y * width + x] = list(centre) + [255]


def details(name, width, pixels):
    """The hand-placed touches, in 32 x 32 coordinates."""
    if name == "grass_top":
        dots(width, pixels, [(7, 8), (22, 5), (14, 21), (26, 25)], (250, 250, 240), centre=(250, 210, 60))
        dots(width, pixels, [(4, 26), (18, 13)], (220, 70, 60), centre=(250, 210, 60))
    elif name == "stone":
        crack = [(5, 6), (6, 7), (7, 7), (8, 8), (9, 9), (9, 10), (10, 11), (20, 18), (21, 19), (22, 19), (23, 20), (24, 21), (24, 22)]
        for x, y in crack:
            pixels[y * width + x] = shade(pixels[y * width + x], 0.5)
    elif name == "dirt":
        for x, y in [(6, 5), (20, 9), (11, 18), (25, 24), (4, 27), (16, 28)]:
            for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                pixels[(y + dy) * width + x + dx] = [150, 140, 128, 255] if (dx, dy) != (0, 0) else [190, 182, 170, 255]
    elif name in ("oak_planks", "birch_planks", "spruce_planks", "jungle_planks"):
        for y in (3, 11, 19, 27):
            for x in (3, 28):
                pixels[y * width + x] = [70, 70, 78, 255]


PREVIEW = ["grass_side", "stone", "oak_planks", "cobblestone", "oak_log", "iron_ore", "bricks", "iron_pickaxe"]


def write_preview(pack):
    """4 x 2 tiles of the finished pack, like the preview qtcraft-headless writes."""
    tiles = [read_png(os.path.join(pack, "textures", "qtcraft", name + ".png")) for name in PREVIEW]
    size = tiles[0][0]
    pixels = [[0, 0, 0, 0]] * (size * 4 * size * 2)
    for index, (_, _, tile) in enumerate(tiles):
        left, top = (index % 4) * size, (index // 4) * size
        for y in range(size):
            for x in range(size):
                pixels[(top + y) * size * 4 + left + x] = tile[y * size + x]
    write_png(os.path.join(pack, "preview.png"), size * 4, size * 2, pixels)


def main():
    source, target = sys.argv[1], sys.argv[2]
    # Only ever replace a texture pack folder (or create a new one), never some other directory.
    if os.path.exists(target):
        if not os.path.isfile(os.path.join(target, "texturepack.json")):
            sys.exit(f"{target} exists and is no texture pack; not replacing it")
        shutil.rmtree(target)
    os.makedirs(os.path.join(target, "textures", "qtcraft"))
    folder = os.path.join(source, "textures", "qtcraft")
    for file_name in sorted(os.listdir(folder)):
        name = file_name[:-4]
        width, height, pixels = read_png(os.path.join(folder, file_name))
        pixels = [grade(p) if p[3] else p for p in pixels]
        if name in OUTLINED:
            outline(width, height, pixels)
        details(name, width, pixels)
        write_png(os.path.join(target, "textures", "qtcraft", file_name), width, height, pixels)
    write_preview(target)
    manifest = {
        "format": 1,
        "name": "Storybook",
        "description": "Example of a texture pack made of plain PNG files: warm colours, drawn outlines on blocks "
                       "and a few hand-placed details. Textures it does not have come from the next pack or the game.",
        "resolution": 32,
        "license": LICENSE,
        "credits": [{"author": "QtCraft",
                     "note": "Exported from the procedural textures at 32 x 32 and hand-tuned with scripts/texturepack-style.py"}],
        "preview": "preview.png",
    }
    with open(os.path.join(target, "texturepack.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=4)
        handle.write("\n")


if __name__ == "__main__":
    main()

#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Regenerates the files of the built-in texture packs in content/texturepacks (stage 28, E-303):
# the preview images of the procedural "QtCraft HD" packs and the whole image pack "Storybook 32x"
# (exported from the procedural textures at 32 x 32, then hand-tuned by texturepack-style.py).
# Needs a built qtcraft-headless; writes only below content/texturepacks.
# Usage: scripts/make-texture-packs.sh [build-dir]
set -eu

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$PROJECT_DIR/build/debug}
HEADLESS="$BUILD/src/headless/qtcraft-headless"
PACKS="$PROJECT_DIR/content/texturepacks"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-packs.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

PREVIEW="qtcraft:grass_side,qtcraft:stone,qtcraft:oak_planks,qtcraft:cobblestone,qtcraft:oak_log,qtcraft:iron_ore,qtcraft:bricks,qtcraft:iron_pickaxe"
# The example image pack: the common blocks of the surface, caves and first buildings, plants and
# the first tools, so the effect shows at once; everything else falls back to the next pack.
STORYBOOK="$PREVIEW,qtcraft:grass_top,qtcraft:dirt,qtcraft:sand,qtcraft:gravel,qtcraft:oak_log_top,qtcraft:oak_leaves,\
qtcraft:birch_log,qtcraft:birch_log_top,qtcraft:birch_planks,qtcraft:birch_leaves,qtcraft:spruce_log,qtcraft:spruce_log_top,\
qtcraft:spruce_planks,qtcraft:spruce_leaves,qtcraft:jungle_log,qtcraft:jungle_log_top,qtcraft:jungle_planks,qtcraft:jungle_leaves,qtcraft:water,qtcraft:glass,qtcraft:stone_bricks,qtcraft:coal_ore,qtcraft:gold_ore,\
qtcraft:diamond_ore,qtcraft:snow,qtcraft:ice,qtcraft:sandstone,qtcraft:sandstone_top,qtcraft:bedrock,qtcraft:clay,\
qtcraft:terracotta,qtcraft:tall_grass,qtcraft:poppy,qtcraft:dandelion,qtcraft:crafting_table_side,qtcraft:crafting_table_top,\
qtcraft:furnace_front,qtcraft:furnace_side,qtcraft:furnace_top,qtcraft:chest_front,qtcraft:chest_side,qtcraft:chest_top,\
qtcraft:torch,qtcraft:torch_item,qtcraft:torch_top,qtcraft:bookshelf,qtcraft:wooden_pickaxe,qtcraft:stone_pickaxe,\
qtcraft:diamond_pickaxe,qtcraft:iron_sword,qtcraft:iron_axe,qtcraft:iron_shovel,qtcraft:stick,qtcraft:apple,qtcraft:bread,\
qtcraft:coal,qtcraft:iron_ingot,qtcraft:diamond"

for resolution in 32 64 128; do
    "$HEADLESS" --no-mods --export-textures "$TMP/hd$resolution" --texture-resolution "$resolution" \
        --texture-packs "builtin:qtcraft_hd_$resolution" --texture-keys "$PREVIEW"
    cp "$TMP/hd$resolution/preview.png" "$PACKS/qtcraft_hd_$resolution/preview.png"
done

"$HEADLESS" --no-mods --export-textures "$TMP/storybook" --texture-resolution 32 --texture-packs builtin:qtcraft_hd_32 \
    --texture-keys "$STORYBOOK"
python3 -I "$PROJECT_DIR/scripts/texturepack-style.py" "$TMP/storybook" "$PACKS/storybook_32"
echo "Texture packs written to $PACKS"

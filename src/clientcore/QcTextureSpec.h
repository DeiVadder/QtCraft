#pragma once

#include <QColor>
#include <QString>

#include <optional>

// Data description of a procedurally generated block/item texture.
struct QcTextureSpec
{
    enum class Pattern {
        Noise,
        Speckle,
        Planks,
        Bricks,
        LogSide,
        LogTop,
        Leaves,
        Glass,
        GrassSide,
        Ore,
        Water,
        Tool,
        Lump,
        Pickaxe,
        Axe,
        Shovel,
        Sword,
        Stick,
        Ingot,
        Gem,
        // Small plant: a stem in the secondary colour under a leafy crown in the primary colour.
        Sapling,
        // Eyes (primary with secondary pupils) and a mouth on a transparent background, for a thin
        // box in front of a head.
        Face,
        // Machine front: a stone face (primary) with a dark fire opening (secondary) and a grate.
        Furnace,
        // Torch stretched over a thin box: a flame (secondary) on top of a stick (primary).
        Torch,
        // Torch as a flat icon on a transparent background.
        TorchItem,
        // Two rails with rungs (primary) on a transparent background.
        Ladder,
        // Door halves: vertical boards (primary) in a frame (secondary); the lower one with a
        // handle, the upper one with a window.
        DoorLower,
        DoorUpper,
        // Hatch of boards with a frame and two openings.
        Trapdoor,
        // Chest: boards (primary) with darker edges and a lid seam; the front adds a latch
        // (secondary), the top only has the frame.
        ChestSide,
        ChestFront,
        ChestTop,
        // Molten rock: glowing streaks (secondary) in a darker flow (primary).
        Lava,
        // Blossom (primary) with a light centre on a stem with two leaves (secondary).
        Flower,
        // Blades of grass of different heights (primary, tips secondary).
        TallGrass,
        // Cap (primary, lighter spots) on a pale stem.
        Mushroom,
        // Three jointed stalks (primary) with a few leaves (secondary).
        SugarCane,
        // Glass bottle with a cork; the contents in the secondary colour (transparent: empty).
        Bottle,
        // Metal bucket (primary); the contents seen from above in the secondary colour.
        Bucket,
        // Lamp housing (primary) around a glowing window (secondary) with bars, for a stretched box.
        Lantern,
    };

    Pattern pattern = Pattern::Noise;
    QColor primary = QColor(128, 128, 128);
    QColor secondary = QColor(96, 96, 96);
    double variation = 0.15;
    quint32 seed = 0;
};

std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name);

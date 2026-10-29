// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcVegetationTable.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTreeShape.h"

#include <QList>
#include <QSet>
#include <QStringList>

#include <array>
#include <utility>

// The registry's terrain, biome, feature, ore and pool definitions resolved to block ids for the
// terrain generator. Unresolvable references are noted in `warnings` and replaced by sensible
// fallbacks; omitted optional blocks (empty names) fall back silently.
struct QcTerrainDefinitions
{
    struct Feature
    {
        QcTreeShape::Params shape;
        QSet<QcBlockId> ground;
        bool nearWater = false;
        bool onWater = false;
        int patchSize = 0;
        QcNoise patches{0};
        // Seed of the placement chances, derived from the feature's name.
        quint64 seed = 0;
        // A layer of a biome's vegetation (decision E-814): the plant comes from this table, by
        // weight, the same kind in areas of about patchSize / 2 blocks; empty for content features.
        QcVegetationTable plants;
    };

    // The patches of one vegetation layer of a biome (decision E-814).
    struct VegetationPatches
    {
        QcVegetationTable plants;
        QcVegetationPatches patches;
    };
    // Layers of a biome's vegetation in this order.
    enum VegetationLayer { Grass, Flowers, VegetationLayers };

    struct Biome
    {
        QString name;
        double temperature = 0.5;
        double humidity = 0.5;
        double ruggedness = 0.2;
        double rarity = 0.0;
        double spread = 0.0;
        double baseHeight = 68.0;
        double heightVariation = 6.0;
        double ridges = 0.0;
        int terraceHeight = 0;
        int overhang = 0;
        QcBlockId steep = qcAirBlockId;
        int steepSlope = 3;
        QcBlockId surface = qcAirBlockId;
        QcBlockId subsurface = qcAirBlockId;
        int subsurfaceDepth = 3;
        QcBlockId underwater = qcAirBlockId;
        // Patches of other blocks among `underwater` (decision E-840): block and share.
        QList<std::pair<QcBlockId, double>> underwaterPatches;
        // Delta biomes: patches of other blocks among `surface` on their land (decision E-850).
        QList<std::pair<QcBlockId, double>> surfacePatches;
        QcBlockId beach = qcAirBlockId;
        QcBlockId frozenWater = qcAirBlockId;
        // Only water at most this many blocks deep takes `frozenWater` (decision E-850); 0: all.
        int frozenDepth = 0;
        int snowLine = 0;
        QcBlockId snow = qcAirBlockId;
        // A river biome, which the climate never chooses (decision E-840).
        bool river = false;
        // Land biomes: the index of the river biome of their rivers, -1 without one.
        int riverBiome = -1;
        // A delta biome, which the climate never chooses either (decision E-850).
        bool delta = false;
        // Land biomes: the index of the delta biome of their river deltas, -1 without one.
        int deltaBiome = -1;
        // Index into features and density per column; the vegetation layers come last.
        QList<std::pair<int, double>> features;
        // Giant trees (decision E-822): index into features and chance per cell of
        // QcTerrainDefinitions::giantCell blocks, from their density per column; not in `features`.
        QList<std::pair<int, double>> giants;
        std::array<VegetationPatches, VegetationLayers> patches;

        // Whether the climate chooses this biome (not a river or delta biome).
        [[nodiscard]] bool byClimate() const { return !river && !delta; }
    };

    struct Pool
    {
        QcBlockId liquid = qcAirBlockId;
        int minY = 8;
        int maxY = 40;
        int attempts = 1;
        double chance = 0.0;
        int radius = 3;
        int depth = 2;
        quint64 seed = 0;
        bool surface = false;
        // Surface lakes (decision E-856): the square of chunks planned together, the chunks kept
        // free towards the next square, and the block that walls the liquid in.
        int region = 1;
        int spacing = 0;
        QcBlockId rim = qcAirBlockId;
        // Biome indices it may lie in; empty for all.
        QSet<int> biomes;
    };

    // Freezing by temperature and mixed ground at biome borders (decision E-858).
    struct ColdEdges
    {
        // Without, the biomes' frozenWater freezes all their water.
        bool byTemperature = false;
        // The block water freezes to where its biome names none.
        QcBlockId ice = qcAirBlockId;
        double temperature = 0.0;
        double riverTemperature = 0.0;
        double ragged = 0.0;
        int groundBlend = 0;
    };

    struct Ore
    {
        QcBlockId block = qcAirBlockId;
        QcBlockId replaces = qcAirBlockId;
        int minY = 1;
        int maxY = 64;
        int veins = 8;
        int veinSize = 6;
        quint64 seed = 0;
        // Biome indices a vein may start in; empty for all.
        QSet<int> biomes;
    };

    // Giant trees stand at most one per cell of this many blocks along x and z (decision E-822).
    static constexpr int giantCell = 16;

    QcTerrainDefinitions(const QcContentRegistry &content, quint64 seed);

    int seaLevel = 62;
    QcBlockId stone = qcAirBlockId;
    QcBlockId water = qcAirBlockId;
    QcBlockId bedrock = qcAirBlockId;
    QcBlockId caveLiquid = qcAirBlockId;
    int bedrockLayers = 3;
    QList<Biome> biomes;
    QList<Feature> features;
    QList<Ore> ores;
    QList<Pool> pools;
    ColdEdges coldEdges;
    // Highest patch chance of any biome per vegetation layer (cells above it are skipped at once).
    std::array<double, VegetationLayers> maxPatchChance{};
    // Highest chance of any biome that a giant cell holds a giant tree (cells above it are skipped at once).
    double maxGiantChance = 0.0;
    // Solid blocks the terrain itself is made of (stone and every biome's ground layers, no
    // features, liquids or ice): where a player may spawn.
    QSet<QcBlockId> naturalGround;
    // Whether any biome uses ridges or terraces (their noise is skipped otherwise).
    bool anyRidges = false;
    bool anyTerraces = false;
    // Some biome has a rarity (decision E-821); without one the world's rarity map is not computed.
    bool anyRare = false;
    bool anyOverhangs = false;
    // Rivers run through the land (terrain setting `rivers`, decision E-840).
    bool rivers = false;
    // Wide rivers end in deltas (terrain setting `deltas` with `rivers`, decision E-850).
    bool deltas = false;
    // The share of the wide river mouths that get a delta (terrain setting `deltaShare`).
    double deltaShare = 1.0;
    // What could not be resolved and was replaced by a fallback.
    QStringList warnings;
};

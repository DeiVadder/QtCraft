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

/*! \brief The registry's terrain, biome, feature, ore and pool definitions resolved to block ids
    for the terrain generator.

    Unresolvable references are noted in \c {warnings} and replaced by sensible fallbacks;
    omitted optional blocks (empty names) fall back silently. */
struct QcTerrainDefinitions
{
    /*! \brief A feature (tree, column, plant or pool) with its blocks resolved; the scattered
        plants of a biome's vegetation layers are features too. */
    struct Feature
    {
        /*! The shape and blocks the feature is built from. */
        QcTreeShape::Params shape;
        /*! The blocks the feature may stand on; empty for any. */
        QSet<QcBlockId> ground;
        /*! Only on the shore: ground at sea level with water next to it. */
        bool nearWater = false;
        /*! Only on the surface of the water (plants). */
        bool onWater = false;
        /*! Size in blocks of the patches the feature grows in; 0 scatters it evenly. */
        int patchSize = 0;
        /*! The noise of the patches, seeded from the feature's name. */
        QcNoise patches{0};
        /*! Seed of the placement chances, derived from the feature's name. */
        quint64 seed = 0;
        /*! A layer of a biome's vegetation (decision E-814): the plant comes from this table, by
            weight, the same kind in areas of about patchSize / 2 blocks; empty for content
            features. */
        QcVegetationTable plants;
    };

    /*! \brief The patches of one vegetation layer of a biome (decision E-814). */
    struct VegetationPatches
    {
        /*! The plants of the layer, chosen by weight. */
        QcVegetationTable plants;
        /*! The chance, tries and radius of the layer's patches. */
        QcVegetationPatches patches;
    };
    /*! \brief Layers of a biome's vegetation in this order. */
    enum VegetationLayer {
        Grass, // The grass layer.
        Flowers, // The flower layer.
        VegetationLayers // The number of layers.
    };

    /*! \brief A biome with its blocks resolved: its climate point, the shape of its land, its
        ground blocks, its river and delta biomes and what grows in it. */
    struct Biome
    {
        /*! The biome's content name. */
        QString name;
        /*! The biome's climate point in [0, 1] (QcClimateBiomes). \qcshared */
        double temperature = 0.5;
        double humidity = 0.5;
        double ruggedness = 0.2;
        /*! How rare the biome is (decision E-821); 0 for an ordinary biome. */
        double rarity = 0.0;
        /*! How far the biome reaches beyond its neighbours in climate space (decision E-824). */
        double spread = 0.0;
        /*! Surface height of its land in blocks, and how much it varies. \qcshared */
        double baseHeight = 68.0;
        double heightVariation = 6.0;
        /*! Share of the variation in [0, 1] that comes from sharp ridges. */
        double ridges = 0.0;
        /*! Height of terrace steps in blocks; 0 for none. */
        int terraceHeight = 0;
        /*! Overhang amplitude in blocks; 0 keeps plain columns. */
        int overhang = 0;
        /*! Surface block where the height differs from a neighbour's by at least
            \c {steepSlope} blocks; air keeps the normal surface. \qcshared */
        QcBlockId steep = qcAirBlockId;
        int steepSlope = 3;
        /*! The top block of its land. */
        QcBlockId surface = qcAirBlockId;
        /*! The blocks under the surface, and how many of them. \qcshared */
        QcBlockId subsurface = qcAirBlockId;
        int subsurfaceDepth = 3;
        /*! The ground block under water. */
        QcBlockId underwater = qcAirBlockId;
        /*! Patches of other blocks among \c {underwater} (decision E-840): block and share. */
        QList<std::pair<QcBlockId, double>> underwaterPatches;
        /*! Delta biomes: patches of other blocks among \c {surface} on their land (decision
            E-850). */
        QList<std::pair<QcBlockId, double>> surfacePatches;
        /*! The ground where land meets the sea at most a block above the sea level, a delta's
            banks flush with the water, and in patches the banks of the rivers of this river
            biome. */
        QcBlockId beach = qcAirBlockId;
        /*! The block the top water layer freezes to; the water itself when the biome's water does
            not freeze. */
        QcBlockId frozenWater = qcAirBlockId;
        /*! Only water at most this many blocks deep takes \c {frozenWater} (decision E-850); 0:
            all. */
        int frozenDepth = 0;
        /*! The surface at and above about this height (jittered by a few blocks) takes
            \c {snow}; 0: none. */
        int snowLine = 0;
        /*! The surface block above the snow line. */
        QcBlockId snow = qcAirBlockId;
        /*! A river biome, which the climate never chooses (decision E-840). */
        bool river = false;
        /*! Land biomes: the index of the river biome of their rivers, -1 without one. */
        int riverBiome = -1;
        /*! A delta biome, which the climate never chooses either (decision E-850). */
        bool delta = false;
        /*! Land biomes: the index of the delta biome of their river deltas, -1 without one. */
        int deltaBiome = -1;
        /*! Index into features and density per column; the vegetation layers come last. */
        QList<std::pair<int, double>> features;
        /*! Giant trees (decision E-822): index into features and chance per cell of
            QcTerrainDefinitions::giantCell blocks, from their density per column; not in
            \c {features}. */
        QList<std::pair<int, double>> giants;
        /*! The patches of each vegetation layer (decision E-814). */
        std::array<VegetationPatches, VegetationLayers> patches;

        /*! Returns whether the climate chooses this biome (not a river or delta biome). */
        [[nodiscard]] bool byClimate() const { return !river && !delta; }
    };

    /*! \brief A pool of liquid with its blocks resolved: sunk into a cave floor (QcPoolPlacer),
        or a lake on the surface (QcSurfaceLakes). */
    struct Pool
    {
        /*! The liquid. */
        QcBlockId liquid = qcAirBlockId;
        /*! The lowest and highest start height of a cave pool. \qcshared */
        int minY = 8;
        int maxY = 40;
        /*! Tries per chunk (cave pools) or chunks tried per square (lakes). */
        int attempts = 1;
        /*! The chance of each try (cave pools) or of each square (lakes). */
        double chance = 0.0;
        /*! The basin's radius (cave pools) or reach from its centre (lakes), in blocks. */
        int radius = 3;
        /*! How deep the basin is sunk into the cave floor (cave pools) or about how deep the
            liquid stands (lakes), in blocks. */
        int depth = 2;
        /*! Seed of the pool's random values, derived from its name. */
        quint64 seed = 0;
        /*! A lake on the surface of dry land instead of a cave pool (decision E-856). */
        bool surface = false;
        /*! Surface lakes (decision E-856): the square of chunks planned together, the chunks
            kept free towards the next square, and the block that walls the liquid in (air for the
            terrain's stone). \qcshared */
        int region = 1;
        int spacing = 0;
        QcBlockId rim = qcAirBlockId;
        /*! Biome indices it may lie in; empty for all. */
        QSet<int> biomes;
    };

    /*! \brief Freezing by temperature and mixed ground at biome borders (decision E-858). */
    struct ColdEdges
    {
        /*! Without, the biomes' frozenWater freezes all their water. */
        bool byTemperature = false;
        /*! The block water freezes to where its biome names none. */
        QcBlockId ice = qcAirBlockId;
        /*! Still water freezes below this temperature, river water below
            \c {riverTemperature}. \qcshared */
        double temperature = 0.0;
        double riverTemperature = 0.0;
        /*! Amplitude of the noise added to the temperature: how far the ice's edge frays. */
        double ragged = 0.0;
        /*! Columns take their ground blocks from the biome up to this many blocks away; 0: their
            own. */
        int groundBlend = 0;
    };

    /*! \brief An ore with its blocks resolved: veins grown inside each chunk. */
    struct Ore
    {
        /*! The ore block. */
        QcBlockId block = qcAirBlockId;
        /*! The only block a vein replaces. */
        QcBlockId replaces = qcAirBlockId;
        /*! The lowest and highest height of a vein's blocks. \qcshared */
        int minY = 1;
        int maxY = 64;
        /*! Veins per chunk. */
        int veins = 8;
        /*! Blocks per vein, at most. */
        int veinSize = 6;
        /*! Seed of the ore's veins, derived from its name. */
        quint64 seed = 0;
        /*! Biome indices a vein may start in; empty for all. */
        QSet<int> biomes;
    };

    /*! Giant trees stand at most one per cell of this many blocks along x and z (decision
        E-822). */
    static constexpr int giantCell = 16;

    QcTerrainDefinitions(const QcContentRegistry &content, quint64 seed);

    /*! The sea level: the sea is filled with water up to the block below it (the water
        surface), the first air above the sea. */
    int seaLevel = 62;
    /*! The rock of the terrain. */
    QcBlockId stone = qcAirBlockId;
    /*! The water of the sea, lakes and rivers. */
    QcBlockId water = qcAirBlockId;
    /*! The bottom block (the stone without one). */
    QcBlockId bedrock = qcAirBlockId;
    /*! The liquid that fills the caves deep down; air keeps them dry. */
    QcBlockId caveLiquid = qcAirBlockId;
    /*! The bottom layer is bedrock; the layers above it get thinner up to this many layers. */
    int bedrockLayers = 3;
    /*! The biomes in the order of the content (a stone fallback without any). */
    QList<Biome> biomes;
    /*! The features of the content, then the scattered plants of the biomes' vegetation. */
    QList<Feature> features;
    /*! The ores. */
    QList<Ore> ores;
    /*! The pools, in caves and on the surface. */
    QList<Pool> pools;
    /*! Freezing by temperature and mixed ground at biome borders. */
    ColdEdges coldEdges;
    /*! Highest patch chance of any biome per vegetation layer (cells above it are skipped at
        once). */
    std::array<double, VegetationLayers> maxPatchChance{};
    /*! Highest chance of any biome that a giant cell holds a giant tree (cells above it are
        skipped at once). */
    double maxGiantChance = 0.0;
    /*! Solid blocks the terrain itself is made of (stone and every biome's ground layers, no
        features, liquids or ice): where a player may spawn. */
    QSet<QcBlockId> naturalGround;
    /*! Whether any biome uses ridges or terraces (their noise is skipped otherwise).
        \qcshared */
    bool anyRidges = false;
    bool anyTerraces = false;
    /*! Some biome has a rarity (decision E-821); without one the world's rarity map is not
        computed. */
    bool anyRare = false;
    /*! Whether any biome has overhangs. */
    bool anyOverhangs = false;
    /*! Rivers run through the land (terrain setting \c {rivers}, decision E-840). */
    bool rivers = false;
    /*! Wide rivers end in deltas (terrain setting \c {deltas} with \c {rivers}, decision E-850). */
    bool deltas = false;
    /*! The share of the wide river mouths that get a delta (terrain setting \c {deltaShare}). */
    double deltaShare = 1.0;
    /*! What could not be resolved and was replaced by a fallback. */
    QStringList warnings;
};

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTreeShape.h"

#include <QList>
#include <QSet>
#include <QStringList>

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
        int patchSize = 0;
        QcNoise patches{0};
        // Seed of the placement chances, derived from the feature's name.
        quint64 seed = 0;
    };

    struct Biome
    {
        QString name;
        double temperature = 0.5;
        double humidity = 0.5;
        double ruggedness = 0.2;
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
        QcBlockId beach = qcAirBlockId;
        QcBlockId frozenWater = qcAirBlockId;
        int snowLine = 0;
        QcBlockId snow = qcAirBlockId;
        // Index into features and density per column.
        QList<std::pair<int, double>> features;
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
        // Biome indices it may lie in; empty for all.
        QSet<int> biomes;
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
    // Solid blocks the terrain itself is made of (stone and every biome's ground layers, no
    // features, liquids or ice): where a player may spawn.
    QSet<QcBlockId> naturalGround;
    // Whether any biome uses ridges or terraces (their noise is skipped otherwise).
    bool anyRidges = false;
    bool anyTerraces = false;
    bool anyOverhangs = false;
    // What could not be resolved and was replaced by a fallback.
    QStringList warnings;
};

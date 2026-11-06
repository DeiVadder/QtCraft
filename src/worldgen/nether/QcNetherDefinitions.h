// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcVegetationTable.h"
#include "worldgen/structures/QcStructure.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"
#include "worldgen/terrain/QcTreeShape.h"

#include <QList>
#include <QStringList>

#include <memory>
#include <utility>

/*! \brief The settings of the nether (QcNetherSettings) with blocks, biomes, ores and structures
    resolved from the content and seeds derived from the world's (decision E-1030).

    Unknown names fall back to the filler, or leave their entry out, with a warning. */
struct QcNetherDefinitions
{
    /*! \brief A plant growing in a column from a floor or a roof (a feature of \c {column} type,
        decision E-1046). */
    struct Strand
    {
        /*! The block of the column. */
        QcBlockId block = qcAirBlockId;
        /*! The floors it grows on; empty for any. Roofs need none. */
        QList<QcBlockId> ground;
        /*! The lowest and highest column. \qcshared */
        int minHeight = 1;
        int maxHeight = 1;
        /*! It hangs from a roof. */
        bool fromRoof = false;
        /*! The share of the floors or roofs with one. */
        double density = 0.0;
        /*! The seed of its places. */
        quint64 seed = 0;
    };

    /*! \brief A huge fungus of a biome (a feature of \c {tree} type, decision E-1045). */
    struct Fungus
    {
        /*! Its shape. */
        QcTreeShape::Params params;
        /*! The share of the columns with one. */
        double density = 0.0;
        /*! The seed of its places. */
        quint64 seed = 0;
    };

    /*! \brief A biome: its point on the map of two noises, in 0 to 1, and its ground. */
    struct Biome
    {
        /*! Its content name. */
        QString name;
        /*! Its point (\c {temperature}, \c {humidity}). \qcshared */
        double first = 0.5;
        double second = 0.5;
        /*! The top block of a floor and the blocks below it. \qcshared */
        QcBlockId surface = qcAirBlockId;
        QcBlockId subsurface = qcAirBlockId;
        /*! How many blocks of \c {subsurface} lie below the top. */
        int depth = 3;
        /*! Other top blocks with their share of the columns. */
        QList<std::pair<QcBlockId, double>> patches;
        /*! The size of the patches in blocks; 0 chooses them column by column. */
        double patchScale = 0.0;
        /*! The rock instead of the filler and its bands; air keeps the filler. \qcshared */
        QcBlockId rock = qcAirBlockId;
        QcBlockId rockBand = qcAirBlockId;
        /*! The plants of its floors and the share of the floors with one. \qcshared */
        QcVegetationTable plants;
        double plantDensity = 0.0;
        /*! Its columns of plants. */
        QList<Strand> strands;
        /*! Its huge fungi. */
        QList<Fungus> fungi;
    };

    /*! \brief A bank along the lava with its noise (QcNetherSettings::Shore). */
    struct Shore
    {
        /*! The noise of the bank. */
        QcNoise noise;
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        double scale = 12.0;
        double threshold = 0.2;
        int height = 4;
        int depth = 3;
    };

    /*! \brief A cluster with its blocks and seed (QcNetherSettings::Cluster). */
    struct Cluster
    {
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        QcBlockId on = qcAirBlockId;
        int tries = 0;
        int minY = 0;
        int maxY = 0;
        int size = 0;
        int radius = 0;
        int depth = 0;
        /*! The seed of its places. */
        quint64 seed = 0;
        /*! The names of the biomes it grows in; empty for all. */
        QStringList biomes;
    };

    /*! \brief Posts with their noise (QcNetherSettings::Pillar). */
    struct Pillar
    {
        /*! The noise of the posts. */
        QcNoise noise;
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        QStringList biomes;
        double scale = 18.0;
        double threshold = 0.1;
        int height = 12;
        /*! The seed of the height of each post. */
        quint64 seed = 0;
    };

    /*! \brief Pools with their noise (QcNetherSettings::Pool). */
    struct Pool
    {
        /*! The noise of the pools. */
        QcNoise noise;
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        QcBlockId rim = qcAirBlockId;
        QStringList biomes;
        double scale = 12.0;
        double threshold = 0.2;
        double rimBand = 0.08;
    };

    /*! \brief A spring with its blocks and seed (QcNetherSettings::Spring). */
    struct Spring
    {
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        QcBlockId in = qcAirBlockId;
        int tries = 0;
        int minY = 0;
        int maxY = 0;
        /*! The seed of its places. */
        quint64 seed = 0;
    };

    /*! \brief A fire with its blocks and seed (QcNetherSettings::Fire). */
    struct Fire
    {
        /*! The settings. \qcshared */
        QcBlockId block = qcAirBlockId;
        QList<QcBlockId> on;
        int tries = 0;
        /*! The seed of its places. */
        quint64 seed = 0;
    };

    /*! \brief A structure the generator places by itself (QcNetherSettings::Structure). */
    struct Structure
    {
        /*! The structure. */
        std::shared_ptr<const QcStructure> structure;
        /*! The name /locate takes: the content name, the base game's without "qtcraft:". */
        QString name;
        /*! The size of its regions. */
        int spacing = 256;
        /*! The share of the regions with a copy. */
        double chance = 0.0;
        /*! The seed of its places. */
        quint64 seed = 0;
    };

    /*! Resolves the nether settings of \a content; the seeds follow \a seed. */
    QcNetherDefinitions(const QcContentRegistry &content, quint64 seed);

    /*! The settings as read (the shape and the heights). */
    QcNetherSettings settings;
    /*! The rock, the floor and the roof, and the liquid of the sea. \qcshared */
    QcBlockId filler = qcAirBlockId;
    QcBlockId bedrock = qcAirBlockId;
    QcBlockId liquid = qcAirBlockId;
    /*! The biomes of the content; a netherrack fallback without any. */
    QList<Biome> biomes;
    /*! The ores of the content. */
    QList<QcTerrainDefinitions::Ore> ores;
    /*! The banks along the lava. */
    QList<Shore> shores;
    /*! The clusters on the roofs. */
    QList<Cluster> clusters;
    /*! The springs. */
    QList<Spring> springs;
    /*! The natural fires. */
    QList<Fire> fires;
    /*! The stone posts. */
    QList<Pillar> pillars;
    /*! The still pools. */
    QList<Pool> pools;
    /*! The structures. */
    QList<Structure> structures;
    /*! The arrival station of a demonstration world; null without one. */
    std::shared_ptr<const QcStructure> station;
    /*! What could not be resolved. */
    QStringList warnings;
    /*! The seed of the patches of the ground. */
    quint64 patchSeed = 0;
    /*! The noise of the patches in \c {patchScale} and of the rock bands. \qcshared */
    QcNoise patchNoise;
    QcNoise bandNoise;

    /*! Returns whether \a block is part of the generated ground: the rock, the ground of a biome,
        the banks, ores and clusters. */
    [[nodiscard]] bool isGround(QcBlockId block) const;

private:
    // The structure `name` of `content`; null with a warning when it cannot be built.
    [[nodiscard]] std::shared_ptr<const QcStructure> structureOf(const QcContentRegistry &content, const QString &name);
};

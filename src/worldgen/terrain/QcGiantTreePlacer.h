// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "worldgen/surface/QcSurfacePlanner.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QList>

#include <optional>

class QcChunk;
class QcTerrainGenerator;
struct QcVillagePlan;

/*! \brief Giant trees on a 2×2 trunk (decision E-822): each cell of QcTerrainDefinitions::giantCell
    blocks (aligned with the chunks) holds at most one, with the chance of the biome at a random
    spot in it, on dry, level ground the tree may stand on and that no cave opened.

    Decided from the columns alone, so every chunk a crown reaches into sees the same trees.
    Thread-safe (only reads). */
class QcGiantTreePlacer
{
public:
    /*! \brief One giant tree: where it stands and which feature it is. */
    struct Giant
    {
        /*! The north-west trunk block. */
        QcBlockPos base;
        /*! Index into QcTerrainDefinitions::features. */
        int feature = 0;
    };
    /*! How far a giant reaches sideways from its base, at most. */
    static constexpr int maxReach = 5;

    QcGiantTreePlacer(const QcTerrainGenerator &generator, const QcTerrainDefinitions &definitions, quint64 seed);

    /*! Returns the giants whose crowns may reach into the chunk at \a pos (villages and
        structures not checked). */
    [[nodiscard]] QList<Giant> giantsNear(QcChunkPos pos) const;
    /*! Builds the giants reaching into \a chunk, none near \a village (may be null) or a surface
        structure or coastal tower. */
    void build(QcChunk &chunk, const QcVillagePlan *village) const;

private:
    // Whether `giant` would reach into `village` (may be null) or one of the `structures` footprints.
    [[nodiscard]] static bool blocked(Giant giant, const QcVillagePlan *village, const QList<QcSurfacePlanner::Rect> &structures);
    // The giant of the cell (cellX, cellZ), if it has one.
    [[nodiscard]] std::optional<Giant> giantIn(int cellX, int cellZ) const;
    // The feature of the giant chosen by `roll` among the giants of `biome`, if any.
    [[nodiscard]] std::optional<int> chosen(int biome, double roll) const;
    // The height of the lowest of the four ground columns plus one, when they are dry, level, free
    // of cave openings and of ground the feature may stand on.
    [[nodiscard]] std::optional<int> baseHeight(int x, int z, const QcTerrainDefinitions::Feature &feature) const;

    const QcTerrainGenerator &m_generator;
    const QcTerrainDefinitions &m_definitions;
    quint64 m_seed = 0;
    // The world seed, for the shapes of the trees as QcTerrainGenerator builds them.
    quint64 m_shapeSeed = 0;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/surface/QcSurfacePlanner.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QVarLengthArray>

#include <array>
#include <optional>

class QcTerrainColumnGrid;
class QcTerrainGenerator;

/*! \brief Lakes of liquid on the surface of dry land: the pools with \c {surface} (lava lakes,
    decision E-856), like the reference game since 1.18.

    One per square of \c {region} chunks at most: with \c {chance}, up to \c {attempts} chunks of
    the square are tried in turn (never within \c {spacing} chunks of the next square) and the
    first fitting site taken. A site is planned from the terrain columns alone (never from
    generated chunks), so neighbouring chunks keep their trees off it alike: an irregular basin
    of a few overlapping ellipses inside its chunk, on level ground of an allowed biome away from
    water, its liquid 1–2 blocks below the lowest ground around it. Thread-safe; plans are cached
    by square. */
class QcSurfaceLakes
{
public:
    /*! A pool definition of the terrain. */
    using Pool = QcTerrainDefinitions::Pool;
    /*! A rectangle of world columns. */
    using Rect = QcSurfacePlanner::Rect;

    /*! \brief One ellipse of a basin: its centre (world), half axes and the liquid's depth at
        the centre. \qcfields */
    struct Blob
    {
        double x = 0.0;
        double z = 0.0;
        double radiusX = 1.0;
        double radiusZ = 1.0;
        double depth = 1.0;
    };
    /*! \brief One planned lake: its pool, chunk, liquid level, basin and footprint. */
    struct Site
    {
        /*! Index into the definitions' pools. */
        int pool = 0;
        /*! The chunk the lake lies in. */
        QcChunkPos chunk;
        /*! The height of the liquid's top layer. */
        int level = 0;
        /*! The overlapping ellipses of the basin. */
        QVarLengthArray<Blob, 6> blobs;
        /*! The basin's columns and two more around them (world): no trees grow near it. */
        Rect footprint;
    };

    /*! Reads the pools from \a definitions; \a content tells which blocks are solid or liquid.
        \a terrain must outlive the lakes (the terrain generator owns them). */
    QcSurfaceLakes(const QcTerrainGenerator &terrain, const QcTerrainDefinitions &definitions, std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the lake planned in the chunk at \a pos, before villages and structures are
        asked. */
    [[nodiscard]] std::optional<Site> siteIn(QcChunkPos pos) const;
    /*! Returns the liquid's depth at the column \a x, \a z (world) of the basin of \a site, 0
        outside it. */
    [[nodiscard]] int depthAt(const Site &site, int x, int z) const;
    /*! Writes the lake \a site into its \a chunk, whose terrain \a columns give the ground: the
        basin dug out above the level, liquid below it, \c {rim} where the liquid would touch air
        or soil, rock and no plants on the ground around it. */
    void carve(QcChunk &chunk, const Site &site, const QcTerrainColumnGrid &columns) const;

private:
    // The lake of a square of a pool, planned once.
    [[nodiscard]] std::optional<Site> planOf(int pool, int regionX, int regionZ) const;
    [[nodiscard]] std::optional<Site> plan(int pool, int regionX, int regionZ) const;
    // The site of one attempt in a chunk, if the ground there fits.
    [[nodiscard]] std::optional<Site> siteAt(int pool, QcChunkPos chunk, int attempt) const;
    [[nodiscard]] QVarLengthArray<Blob, 6> blobsOf(const Pool &pool, QcChunkPos chunk, int attempt) const;
    // The level of the liquid when the ground of the basin and around it fits; `basin` in world columns.
    [[nodiscard]] std::optional<int> levelOver(const Pool &pool, const QList<std::pair<int, int>> &basin, double random) const;
    // The lowest and highest ground top of a basin and the ring around it.
    struct GroundSpan
    {
        int lowest = QcWorld::worldHeight;
        int highest = 0;
    };
    // The span of the ground of the basin and the ring around it, if all of it is dry, plain land.
    [[nodiscard]] std::optional<GroundSpan> groundAround(const QList<std::pair<int, int>> &basin) const;
    // The basin's columns and the ring of one column around them.
    [[nodiscard]] static QSet<std::pair<int, int>> withRing(const QList<std::pair<int, int>> &basin);
    // The ground top of a column on dry, plain land: no water, river, coast or overhang.
    [[nodiscard]] std::optional<int> plainGroundAt(int x, int z) const;
    void wallIn(QcChunk &chunk, const Site &site, QcBlockId rim) const;
    // Turns the sides and the bottom of a liquid block (chunk-local) that are neither liquid nor rock into the rim.
    void sealAround(QcChunk &chunk, std::array<int, 3> at, QcBlockId liquid, QcBlockId rim) const;
    // The highest solid block of a chunk column at or below `from` (chunk-local).
    [[nodiscard]] int solidTopAt(const QcChunk &chunk, int x, int z, int from) const;
    void clearAround(QcChunk &chunk, const Site &site, const QcTerrainColumnGrid &columns, QcBlockId rim) const;
    // The ring (Chebyshev distance, world) of a column around the basin: 0 inside, above `clearRing` far.
    [[nodiscard]] int ringAt(const Site &site, int x, int z) const;
    // Fills a first-ring column (chunk-local ground top) with the rim up to above the level.
    void raiseRim(QcChunk &chunk, std::array<int, 3> ground, int level, QcBlockId rim) const;
    // Removes the plants in the two blocks above a ground top (chunk-local).
    void clearPlantsAbove(QcChunk &chunk, std::array<int, 3> ground) const;
    // Whether a block holds a liquid in place (solid, neither liquid nor replaceable) and is no soil.
    bool isRock(QcBlockId block) const;
    double random(const Pool &pool, int x, int salt, int z) const;

    const QcTerrainGenerator &m_terrain;
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Pool> m_pools;
    // The ground blocks of the biomes but stone: they turn to the rim next to the liquid.
    QSet<QcBlockId> m_soil;
    QcBlockId m_stone = qcAirBlockId;
    int m_seaLevel = 62;
    mutable QMutex m_mutex;
    mutable QHash<QPair<int, qint64>, std::optional<Site>> m_plans;
};

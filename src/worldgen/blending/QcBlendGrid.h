// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "worldgen/blending/QcBlendField.h"
#include "worldgen/blending/QcChunkShape.h"

#include <QHash>

#include <array>
#include <memory>
#include <optional>

/*! \brief The shapes of one chunk of a ring in both versions. \qcfields */
struct QcBlendSource
{
    std::shared_ptr<const QcChunkShape> olderShape;
    std::shared_ptr<const QcChunkShape> newerShape;
};

/*! \brief The shapes of the chunks of a ring in both versions. */
using QcBlendSources = QHash<QcChunkPos, QcBlendSource>;

/*! \brief The blended ground and water of every column of a ring (decision E-1041).

    The ground lies between the grounds of the versions by the share of the new one, and so does
    the open surface (water or ground): a column is wet where the surface lies above the ground,
    so a lake flattens out towards a dry side and a river gets shallower. Water that a lower
    neighbour could not hold sinks to its level (settleWater()), the sea stays at sea level and
    reaches the land below it that is open to a column wet in a version (floodLowLand()); a low
    spot dry in both versions stays dry. */
class QcBlendGrid
{
public:
    /*! \brief The blended column. \qcfields */
    struct Cell
    {
        /*! The share of the new version, 0 to 1. */
        float share = 0.0F;
        /*! The ground. */
        int height = 0;
        /*! The top of the water; -1 for a dry column. */
        int water = -1;
        /*! From this height up the caves of the underground are filled: water beside them would
            stand against open air. */
        int sealedFrom = QcWorld::worldHeight;
        /*! The top of the water beside the column; in a column copied whole, what is sealed ends
            here. */
        int sealedTo = -1;
        /*! The ground blocks come from the new version. */
        bool newerSurface = false;
        /*! The column is copied whole from one version (near an end of the ring, or held):
            \c {newerSurface} says from which. */
        bool whole = false;
        /*! The column has water in one of the versions. */
        bool wetInVersion = false;
    };

    /*! Blends the columns of \a sources, the chunks of the ring of \a field, with the sea's surface
        at \a seaTop (-1: no sea) and the patches of the blocks from \a seed. */
    QcBlendGrid(const QcBlendField &field, const QcBlendSources &sources, int seaTop, quint64 seed);

    /*! Returns the column \a x, \a z (world coordinates); null outside the ring. */
    [[nodiscard]] const Cell *cell(int x, int z) const;

private:
    using ChunkCells = std::array<Cell, QcBlendField::columnCount>;

    Cell *cell(int x, int z);
    // Calls `visit` with the world coordinates and the cell of each column.
    template<typename Visit>
    void eachCell(Visit visit);
    // Brings the sea to the blended columns below it that are open to a column wet in a version.
    void floodLowLand();
    // Lets water fall to what its neighbours hold.
    void settleWater();
    // Fits the blended columns to the columns copied whole, which cannot change. Returns whether
    // a column changed: the water beside it settles again.
    [[nodiscard]] bool raiseBanks();
    // Raises the neighbours of column `x`, `z` with less water to `water`.
    [[nodiscard]] bool raiseBanksBeside(int x, int z, int water);
    // Turns the water beside the dry column `x`, `z` of the ground `ground` into a bank.
    [[nodiscard]] bool banksAgainst(int x, int z, int ground);
    // Closes the caves beside the water that settled.
    void sealCaves();
    // Closes the caves beside the wet column `x`, `z`.
    void sealBeside(int x, int z, const Cell &wet);

    int m_seaTop = -1;
    QHash<QcChunkPos, ChunkCells> m_cells;
};

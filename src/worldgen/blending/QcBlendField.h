// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcWorldConstants.h"
#include "common/core/QcXzRect.h"
#include "common/world/QcChunkSet.h"

#include <QList>
#include <QSet>

#include <array>

/*! \brief Where a ring blends two generator versions (decision E-1040): the chunks of the ring
    and how much of each of their columns comes from the new version.

    The share of a column grows smoothly from 0 at the old chunks to 1 at the width of the ring,
    by its straight distance to the nearest old chunk, so the ring has round corners and a slope
    of the blended ground is never steeper than the difference of the versions allows. Held
    rectangles (the structures the old version builds in the ring) count as old, with a narrower
    edge of their own (feather()) around them. */
class QcBlendField
{
public:
    /*! The number of columns of a chunk. */
    static constexpr std::size_t columnCount = static_cast<std::size_t>(QcWorld::chunkSize) * QcWorld::chunkSize;

    /*! The shares of the columns of a chunk, by columnIndex(). */
    using Shares = std::array<float, columnCount>;

    /*! Returns the index of the column \a localX, \a localZ of a chunk. */
    [[nodiscard]] static constexpr int columnIndex(int localX, int localZ) { return localZ * QcWorld::chunkSize + localX; }

    /*! Makes the field of a ring \a widthChunks wide around the chunks \a old, with the rectangles
        \a held. */
    QcBlendField(const QcChunkSet &old, int widthChunks, QList<QcXzRect> held);

    /*! Returns this field with the rectangles \a held and the ring it has: the part of a
        structure's edge that does not fit in is cut off (isCut()). */
    [[nodiscard]] QcBlendField heldWithinRing(QList<QcXzRect> held) const;

    /*! Returns whether the ring did not grow around all that is held. */
    [[nodiscard]] bool isCut() const { return m_cut; }
    /*! Returns the chunks with a column that is not wholly new, without the old ones. */
    [[nodiscard]] const QcChunkSet &ring() const { return m_ring; }
    /*! Returns the rectangles that count as old. */
    [[nodiscard]] const QList<QcXzRect> &held() const { return m_held; }
    /*! Returns whether the chunk \a pos is old. */
    [[nodiscard]] bool isOld(QcChunkPos pos) const { return m_old.contains(pos); }
    /*! Returns the width of the ring in columns. */
    [[nodiscard]] int width() const { return m_widthChunks * QcWorld::chunkSize; }
    /*! Returns how many columns the share takes to reach 1 beside a held rectangle. */
    [[nodiscard]] int feather() const;
    /*! Returns the share of the new version of every column of the chunk \a pos, from 0 to 1. */
    [[nodiscard]] Shares sharesIn(QcChunkPos pos) const;

private:
    // Adds to the ring the chunks nearer to `edge` than `reach` columns.
    void addChunksNear(QcXzRect edge, int reach);

    QcChunkSet m_old;
    int m_widthChunks = 0;
    QList<QcXzRect> m_held;
    // The old chunks with a neighbour that is not old: the only ones a distance can end at.
    QSet<QcChunkPos> m_edge;
    QcChunkSet m_ring;
    bool m_cut = false;
};

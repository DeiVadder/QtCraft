// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"

#include <QList>

/*! \brief A rectangle of columns (or of other cells on the x-z plane, such as regions) from
    (\c {minX}, \c {minZ}) to (\c {maxX}, \c {maxZ}), both corners inclusive.

    Footprints of structures, village areas and the areas a generator or the server looks at
    share this type (decision E-975). */
struct QcXzRect
{
    /*! The lowest x (west edge), inside. */
    int minX = 0;
    /*! The lowest z (north edge), inside. */
    int minZ = 0;
    /*! The highest x (east edge), inside. */
    int maxX = 0;
    /*! The highest z (south edge), inside. */
    int maxZ = 0;

    bool operator==(const QcXzRect &) const = default;

    /*! Returns whether the column \a x, \a z lies in the rectangle. */
    bool contains(int x, int z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
    /*! Returns whether \a other lies within \a gap columns of this rectangle (0: they share at
        least one column). */
    bool intersects(QcXzRect other, int gap = 0) const
    {
        return minX - gap <= other.maxX && maxX + gap >= other.minX && minZ - gap <= other.maxZ && maxZ + gap >= other.minZ;
    }
    /*! Returns the rectangle grown by \a margin columns on every side. */
    QcXzRect grown(int margin) const { return {minX - margin, minZ - margin, maxX + margin, maxZ + margin}; }
    /*! Returns the middle column along x, summed in 64 bits: near the edge of the world (E-883)
        two corners add up beyond int. */
    int middleX() const { return static_cast<int>((qint64{minX} + maxX) / 2); }
    /*! Returns the middle row along z, summed in 64 bits like middleX(). */
    int middleZ() const { return static_cast<int>((qint64{minZ} + maxZ) / 2); }
    /*! Returns the chunks the columns lie in, row by row from the north-west. */
    QList<QcChunkPos> chunks() const
    {
        QList<QcChunkPos> found;
        const QcChunkPos low = QcBlockPos{minX, 0, minZ}.chunk();
        const QcChunkPos high = QcBlockPos{maxX, 0, maxZ}.chunk();
        for (int cz = low.z; cz <= high.z; ++cz) {
            for (int cx = low.x; cx <= high.x; ++cx) {
                found.append({cx, cz});
            }
        }
        return found;
    }
};

Q_DECLARE_TYPEINFO(QcXzRect, Q_PRIMITIVE_TYPE);

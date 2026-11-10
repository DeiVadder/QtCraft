// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/world/QcChunk.h"
#include "worldgen/blending/QcBlendField.h"

#include <QList>

#include <array>

class QcWorldGenerator;

/*! \brief The shape of a generated column for blending (decision E-1041): its ground, the water on
    it and what stands on it. \qcfields */
struct QcColumnShape
{
    /*! The stack from which what stands on the ground is a tree or the like: it keeps its shape
        and takes the crown around it along. */
    static constexpr int rootHeight = 3;

    /*! The highest natural ground; 0 without any. */
    int ground = 0;
    /*! The highest water or ice above the ground; -1 for a dry column. */
    int water = -1;
    /*! The blocks standing on the ground without a gap, neither air nor water. */
    int stack = 0;

    /*! Returns whether a tree or the like stands here. */
    [[nodiscard]] bool isRoot() const { return stack >= rootHeight; }
    /*! Returns the top of what lies open to the sky: the water or else the ground. */
    [[nodiscard]] int surface() const { return water >= 0 ? water : ground; }
};

/*! \brief The shapes of the columns of a generated chunk. */
class QcChunkShape
{
public:
    /*! \brief A column with a tree or the like, in world coordinates. \qcfields */
    struct Root
    {
        int x = 0;
        int z = 0;
        int ground = 0;
    };

    /*! Reads the shape of \a chunk, what is ground and water judged by \a generator. */
    QcChunkShape(const QcChunk &chunk, const QcWorldGenerator &generator);

    /*! Returns the shape of the column \a localX, \a localZ. */
    [[nodiscard]] const QcColumnShape &column(int localX, int localZ) const
    {
        return m_columns[static_cast<std::size_t>(QcBlendField::columnIndex(localX, localZ))];
    }
    /*! Returns the columns with a tree or the like. */
    [[nodiscard]] const QList<Root> &roots() const { return m_roots; }

private:
    std::array<QcColumnShape, static_cast<std::size_t>(QcWorld::chunkSize) * QcWorld::chunkSize> m_columns;
    QList<Root> m_roots;
};

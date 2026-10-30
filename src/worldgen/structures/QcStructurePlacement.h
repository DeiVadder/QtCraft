// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcChunk.h"
#include "worldgen/structures/QcStructure.h"

#include <memory>
#include <utility>

/*! \brief One copy of a structure in the world (decision E-219): the structure's north west bottom
    corner goes to \c {origin} after \c {turns} quarter turns clockwise seen from above, block
    states turn along (QcStateRotation).

    Writing into chunks is deterministic and works chunk by chunk, so a structure may span chunk
    borders; it is safe to call from several threads. */
class QcStructurePlacement
{
public:
    QcStructurePlacement(std::shared_ptr<const QcStructure> structure, QcBlockPos origin, int turns, const QcContentRegistry &content);

    /*! Returns the placed structure. */
    const QcStructure &structure() const { return *m_structure; }
    /*! Returns the north west bottom corner of the turned footprint in the world. */
    QcBlockPos origin() const { return m_origin; }
    /*! Returns the quarter turns clockwise, 0 to 3 (the constructor wraps any count). */
    int turns() const { return m_turns; }
    /*! Returns the extent along world x after turning. */
    int sizeX() const { return m_turns % 2 == 0 ? m_structure->sizeX() : m_structure->sizeZ(); }
    /*! Returns the extent along world z after turning. */
    int sizeZ() const { return m_turns % 2 == 0 ? m_structure->sizeZ() : m_structure->sizeX(); }
    /*! Returns whether the turned footprint covers any column of \a chunk. */
    bool overlaps(QcChunkPos chunk) const;

    /*! Returns the world position of the local cell \a local. */
    QcBlockPos toWorld(QcBlockPos local) const;
    /*! Returns the world position of the local point \a local (cell centres map to cell
        centres). */
    QcVec3 toWorld(const QcVec3 &local) const;
    /*! Returns the state the local cell \a local gets in the world, turned; std::nullopt keeps
        what is there. */
    [[nodiscard]] std::optional<QcBlockId> worldBlock(QcBlockPos local) const;

    /*! Writes the cells inside \a chunk (cells below or above the world are left out). */
    void writeInto(QcChunk &chunk) const;

private:
    // The local cell column of a world column at this offset from the origin: toWorld() inverted.
    std::pair<int, int> localColumn(int offsetX, int offsetZ) const;
    void writeColumn(QcChunk &chunk, int worldX, int worldZ) const;

    std::shared_ptr<const QcStructure> m_structure;
    QcBlockPos m_origin;
    int m_turns = 0;
    // The structure's palette with every state turned.
    QList<QcBlockId> m_palette;
};

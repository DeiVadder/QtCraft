#pragma once

#include "QcChunk.h"
#include "QcStructure.h"
#include "QcVec3.h"

#include <memory>
#include <utility>

// One copy of a structure in the world (decision E-219): the structure's north west bottom corner
// goes to `origin` after `turns` quarter turns clockwise seen from above, block states turn along
// (QcStateRotation). Writing into chunks is deterministic and works chunk by chunk, so a structure
// may span chunk borders; it is safe to call from several threads.
class QcStructurePlacement
{
public:
    QcStructurePlacement(std::shared_ptr<const QcStructure> structure, QcBlockPos origin, int turns, const QcContentRegistry &content);

    const QcStructure &structure() const { return *m_structure; }
    QcBlockPos origin() const { return m_origin; }
    int turns() const { return m_turns; }
    // Extent in the world after turning.
    int sizeX() const { return m_turns % 2 == 0 ? m_structure->sizeX() : m_structure->sizeZ(); }
    int sizeZ() const { return m_turns % 2 == 0 ? m_structure->sizeZ() : m_structure->sizeX(); }
    bool overlaps(QcChunkPos chunk) const;

    // World position of a local cell, and of a local point (cell centres map to cell centres).
    QcBlockPos toWorld(QcBlockPos local) const;
    QcVec3 toWorld(const QcVec3 &local) const;
    // The state a cell gets in the world, turned; std::nullopt keeps what is there.
    [[nodiscard]] std::optional<QcBlockId> worldBlock(QcBlockPos local) const;

    // Writes the cells inside `chunk` (cells below or above the world are left out).
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

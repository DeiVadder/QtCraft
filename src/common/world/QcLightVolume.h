#pragma once

#include "QcBlockAccess.h"

#include "QcChunk.h"

#include <QList>

#include <functional>

#include <array>

// Block light of a box-shaped volume of cells: sources flood outwards breadth-first, every step into
// a cell costs max(1, opacity of that cell) levels, cells of opacity 15 stay dark and no step
// crosses a face that a partial shape covers (QcLightPassage). The result is
// the unique fixpoint "brightest source minus cheapest path", so recomputing a volume after a change
// (a torch removed) gives exactly what an incremental update would. Cells outside read 0.
class QcLightVolume
{
public:
    static constexpr int maxLevel = 15;

    QcLightVolume(int sizeX, int sizeY, int sizeZ);

    int sizeX() const { return m_sizeX; }
    int sizeY() const { return m_sizeY; }
    int sizeZ() const { return m_sizeZ; }
    bool contains(int x, int y, int z) const
    {
        return x >= 0 && x < m_sizeX && y >= 0 && y < m_sizeY && z >= 0 && z < m_sizeZ;
    }

    // `blockedFaces`: faces of the cell light cannot cross (QcLightPassage bits).
    void setOpacity(int x, int y, int z, int opacity, quint8 blockedFaces = 0);
    // A source shines with `level` in its own cell, whatever that cell's opacity.
    void addSource(int x, int y, int z, int level);
    bool hasSources() const { return !m_queue.isEmpty(); }
    // Floods all sources added so far.
    void propagate();
    int level(int x, int y, int z) const { return contains(x, y, z) ? m_levels[index(x, y, z)] : 0; }

    // Block light at one position of a world, from all sources within reach (unloaded cells are
    // dark walls).
    static int blockLightAt(const QcBlockAccess &world, QcBlockPos pos);
    // Whether the sections of the chunks `chunkAt` returns within `reach` of `pos` hold a block that
    // emits light (palette check, no block reads).
    static bool anySourceNear(const QcContentRegistry &content, QcBlockPos pos, int reach,
                              const std::function<const QcChunk *(QcChunkPos)> &chunkAt);

private:
    // Opacity, blocked faces and light source of the world's block `cell` at volume cell (x, y, z).
    void setCell(const QcBlockAccess &world, int x, int y, int z, QcBlockPos cell);
    qsizetype index(int x, int y, int z) const { return (qsizetype(y) * m_sizeZ + z) * m_sizeX + x; }

    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    QList<quint8> m_levels;
    QList<quint8> m_opacity;
    QList<quint8> m_blockedFaces;
    QList<std::array<int, 3>> m_queue;
};

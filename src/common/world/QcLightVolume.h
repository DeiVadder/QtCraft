// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcBlockAccess.h"

#include "common/world/QcChunk.h"

#include <QList>

#include <functional>

#include <array>

/*! \brief Block light of a box-shaped volume of cells: sources flood outwards breadth-first, every
    step into a cell costs max(1, opacity of that cell) levels, cells of opacity 15 stay dark and no
    step crosses a face that a partial shape covers (QcLightPassage).

    The result is the unique fixpoint "brightest source minus cheapest path", so recomputing a
    volume after a change (a torch removed) gives exactly what an incremental update would. Cells
    outside read 0. */
class QcLightVolume
{
public:
    /*! The highest light level. */
    static constexpr int maxLevel = 15;

    QcLightVolume(int sizeX, int sizeY, int sizeZ);

    /*! Returns the volume's size along x. */
    int sizeX() const { return m_sizeX; }
    /*! Returns the volume's size along y. */
    int sizeY() const { return m_sizeY; }
    /*! Returns the volume's size along z. */
    int sizeZ() const { return m_sizeZ; }
    /*! Returns whether the cell (\a x, \a y, \a z) lies in the volume. */
    bool contains(int x, int y, int z) const
    {
        return x >= 0 && x < m_sizeX && y >= 0 && y < m_sizeY && z >= 0 && z < m_sizeZ;
    }

    /*! Sets the opacity of the cell (\a x, \a y, \a z) to \a opacity (clamped to 0..15);
        \a blockedFaces: faces of the cell light cannot cross (QcLightPassage bits). Cells outside
        are ignored. */
    void setOpacity(int x, int y, int z, int opacity, quint8 blockedFaces = 0);
    /*! Adds a source in the cell (\a x, \a y, \a z): it shines with \a level in its own cell,
        whatever that cell's opacity. */
    void addSource(int x, int y, int z, int level);
    /*! Returns whether sources wait to be flooded. */
    bool hasSources() const { return !m_queue.isEmpty(); }
    /*! Floods all sources added so far. */
    void propagate();
    /*! Returns the level of the cell (\a x, \a y, \a z); 0 outside. */
    int level(int x, int y, int z) const { return contains(x, y, z) ? m_levels[index(x, y, z)] : 0; }

    /*! Returns the block light at \a pos of \a world, from all sources within reach (unloaded
        cells are dark walls). */
    static int blockLightAt(const QcBlockAccess &world, QcBlockPos pos);
    /*! Returns whether the sections of the chunks \a chunkAt returns within \a reach of \a pos
        hold a block of \a content that emits light (palette check, no block reads). */
    static bool anySourceNear(const QcContentRegistry &content, QcBlockPos pos, int reach,
                              const std::function<const QcChunk *(QcChunkPos)> &chunkAt);

private:
    // Opacity, blocked faces and light source of the world's block `cell` at volume cell (x, y, z).
    void setCell(const QcBlockAccess &world, int x, int y, int z, QcBlockPos cell);
    qsizetype index(int x, int y, int z) const { return (static_cast<qsizetype>(y) * m_sizeZ + z) * m_sizeX + x; }

    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    QList<quint8> m_levels;
    QList<quint8> m_opacity;
    QList<quint8> m_blockedFaces;
    QList<std::array<int, 3>> m_queue;
};

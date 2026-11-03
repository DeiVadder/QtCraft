// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcSectionLight.h"
#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/meshing/QcMeshNeighbourhood.h"
#include "common/world/QcLightVolume.h"

#include <QList>

#include <array>

/*! \brief Sky and block light of the 3 x 3 chunk neighbourhood around one section, as the chunk
    mesher reads it.

    The volume reaches one chunk around the section horizontally and 16 blocks above and below
    it, which is as far as light travels (levels 15 to 0). Block light comes from the input's
    light snapshots or, without them, from every source in that volume, so it is complete for the
    section either way. */
class QcMeshLighting
{
public:
    /*! Width and depth of the volume in blocks (three chunks). */
    static constexpr int extent = 3 * QcWorld::chunkSize;
    /*! Blocks the volume reaches below and above the section. */
    static constexpr int margin = QcChunkMesher::lightMargin;
    /*! The brightest light level. */
    static constexpr int maxLevel = 15;

    /*! Floods the light of the section \a input describes, reading its blocks through
        \a around. */
    QcMeshLighting(QcMeshNeighbourhood around, const QcMeshInput &input);

    /*! Returns the sky light level of the block at chunk-local (\a lx, \a y, \a lz); outside the
        volume counts as dark, above the world as fully lit. */
    int sky(int lx, int y, int lz) const
    {
        const int x = lx + QcWorld::chunkSize;
        const int z = lz + QcWorld::chunkSize;
        if (x < 0 || x >= extent || z < 0 || z >= extent || y < m_minY || y > m_maxY) {
            return y >= QcWorld::worldHeight ? maxLevel : 0;
        }
        return m_levels[index(x, y, z)];
    }

    /*! Returns the block light level of the block at chunk-local (\a lx, \a y, \a lz); a missing
        neighbour chunk counts as dark. */
    int block(int lx, int y, int lz) const
    {
        if (!m_input.hasBlockLight) {
            return m_blockLight.level(lx + QcWorld::chunkSize, y - m_minY, lz + QcWorld::chunkSize);
        }
        const int cx = lx < 0 ? -1 : (lx >= QcWorld::chunkSize ? 1 : 0);
        const int cz = lz < 0 ? -1 : (lz >= QcWorld::chunkSize ? 1 : 0);
        const auto &light = m_input.blockLight[static_cast<std::size_t>(cz + 1) * 3 + static_cast<std::size_t>(cx + 1)];
        return light ? light->level(lx - cx * QcWorld::chunkSize, y, lz - cz * QcWorld::chunkSize) : 0;
    }

    /*! Returns both channels of the cells of the section \a sectionIndex for shaders that read
        light per cell (decision E-390): block light shares the snapshot of the input when there
        is one, sky light (and block light flooded here) is copied out of the volume. */
    QcSectionLight sectionLight(int sectionIndex) const;

private:
    qsizetype index(int x, int y, int z) const { return (static_cast<qsizetype>(y - m_minY) * extent + z) * extent + x; }

    int columnTop(QcMeshNeighbourhood around, int lx, int lz) const;
    void fillColumn(QcMeshNeighbourhood around, int x, int z, int top);
    void queueSkyEdge(const QList<int> &skyTop, int x, int z);
    void propagate(const QList<int> &skyTop);

    int m_minY = 0;
    int m_maxY = 0;
    int m_height = 0;
    // Sky light, opacity and blocked faces per cell of the volume (index()).
    QList<quint8> m_levels;
    QList<quint8> m_opacity;
    QList<quint8> m_blockedFaces;
    // Cells of the sky light flood (only while the constructor floods).
    QList<std::array<int, 3>> m_queue;
    // Without light snapshots in the input the block light is flooded here.
    bool m_floodsBlockLight = false;
    QcLightVolume m_blockLight;
    const QcMeshInput &m_input;
};

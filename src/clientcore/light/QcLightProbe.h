// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/world/QcLightVolume.h"

#include <QHash>

#include <memory>
#include <optional>

class QcChunk;
class QcClientWorld;

// Sky and block light of the cells around one position, for the debug display (decision E-225):
// sky light shines with 15 into every cell above the highest cell of its column that stops light
// coming straight down (QcLightPassage) and spreads like block light, by the same rules as the
// mesher's light. Computed once in the constructor over a volume that reaches 15 blocks beyond
// the cells asked about, so their values are complete (about 30 000 cells for one cell, 43 000 for
// a radius of 2, half a millisecond and more). Cheap cases skip the volume: no light source within
// reach means block light 0, and a single cell under the open sky has sky light 15.
class QcLightProbe
{
public:
    static constexpr int maxRadius = 3;
    struct Levels
    {
        int sky = -1;
        int block = -1;
    };

    // Light of the cells within `radius` (Chebyshev, at most maxRadius) of `centre`.
    QcLightProbe(const QcClientWorld &world, QcBlockPos centre, int radius = 0);

    // -1 for both outside the probed cells or below the world.
    Levels at(QcBlockPos pos) const;

private:
    // What light needs to know of one cell.
    struct Cell
    {
        int opacity = 0;
        quint8 blockedFaces = 0;
        // Level of the light the cell's block gives off.
        int light = 0;
    };

    static Cell cellAt(const QcChunk *chunk, QcBlockPos pos, const QcContentRegistry &content);
    // The highest cell of the column that sky light coming straight down does not reach;
    // QcWorld::worldHeight for columns of unloaded chunks (dark), -1 for open ones.
    static int columnTop(const QcChunk *chunk, int lx, int lz, const QcContentRegistry &content);
    // Whether the single probed cell sees the sky straight up.
    bool centreUnderOpenSky(const QcClientWorld &world) const;
    void fill(const QcClientWorld &world);
    // Fills one volume column (x, z) from its chunk.
    void fillColumn(const QcChunk *chunk, int x, int z, const QcContentRegistry &content);

    QcBlockPos m_centre;
    int m_radius = 0;
    int m_reach = 0;
    int m_size = 0;
    QcBlockPos m_low;
    // Only the volumes that are needed; without one the level is the cheap case's.
    std::optional<QcLightVolume> m_sky;
    std::optional<QcLightVolume> m_block;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/nether/QcNetherDefinitions.h"
#include "worldgen/terrain/QcNoise.h"

#include <array>
#include <optional>

/*! \brief The rock and the lava of the nether (decision E-1030): a 3D noise on a coarse grid,
    interpolated, closes towards the floor and the roof; caves and caverns open between them, the
    lava fills what lies below the sea level, bedrock seals both ends.

    The same cell comes out whether a chunk or a single question asks (isSolid()), so planning
    needs no chunks. Thread-safe. */
class QcNetherTerrain
{
public:
    /*! Blocks per grid cell along x and z, and along y. \qcshared */
    static constexpr int cellWidth = 4;
    static constexpr int cellHeight = 8;

    /*! Builds the terrain from \a definitions, which outlive it; its noises follow \a seed. */
    QcNetherTerrain(const QcNetherDefinitions &definitions, quint64 seed);

    /*! Writes the bedrock, the rock and the lava into \a chunk. */
    void fill(QcChunk &chunk) const;
    /*! Returns whether rock or bedrock fills the cell \a x, \a y, \a z. */
    [[nodiscard]] bool isSolid(int x, int y, int z) const;
    /*! Returns the index in the definitions' biomes of the column \a x, \a z. */
    [[nodiscard]] int biomeAt(int x, int z) const;
    /*! Returns the lowest height from \a from up at the column \a x, \a z on which \a room free
        cells stand on rock, above the lava and under the roof; none when the column has no such
        floor. */
    [[nodiscard]] std::optional<int> floorAt(int x, int z, int from, int room) const;

private:
    // The noise and the pull of the floor and the roof at a corner of the grid.
    [[nodiscard]] double density(int x, int y, int z) const;
    // Whether the cell `y` of a column is bedrock, in layers thinning out from both ends.
    [[nodiscard]] bool isBedrock(int x, int y, int z) const;
    // The densities at the corners of the grid under the chunk `pos`, x fastest.
    using Corners = std::array<double, std::size_t{5} * 5 * 33>;
    void fillColumn(QcChunk &chunk, const Corners &corners, int localX, int localZ) const;

    const QcNetherDefinitions &m_definitions;
    quint64 m_bedrockSeed = 0;
    QcNoise m_noise;
    QcNoise m_biomeFirst;
    QcNoise m_biomeSecond;
};

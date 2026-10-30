// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <memory>

/*! \brief Small rolling-hills terrain with water below the sea level, a cheap, predictable world
    for tests.

    The seed plays no part.

    Uses the base blocks qtcraft:stone, dirt, grass_block, sand, water. */
class QcTestTerrainGenerator : public QcWorldGenerator
{
public:
    explicit QcTestTerrainGenerator(std::shared_ptr<const QcContentRegistry> content);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;

    /*! Returns the terrain height (top solid block + 1) at the world column \a x, \a z. */
    static int surfaceHeight(int x, int z);

private:
    // Block at height `y` of a column whose surface is at `height`.
    QcBlockId groundBlock(int y, int height) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QcBlockId m_stone = qcAirBlockId;
    QcBlockId m_dirt = qcAirBlockId;
    QcBlockId m_grass = qcAirBlockId;
    QcBlockId m_sand = qcAirBlockId;
    QcBlockId m_water = qcAirBlockId;
};

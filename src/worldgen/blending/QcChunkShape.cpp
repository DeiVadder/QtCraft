// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/blending/QcChunkShape.h"

#include "worldgen/generators/QcWorldGenerator.h"

namespace {

// The shape of the column `x`, `z` of `chunk`.
QcColumnShape shapeOf(const QcChunk &chunk, int x, int z, const QcWorldGenerator &generator)
{
    QcColumnShape shape;
    for (int y = chunk.highestNonAir(x, z); y > 0; --y) {
        const QcBlockId block = chunk.block(x, y, z);
        if (generator.isNaturalGround(block)) {
            shape.ground = y;
            break;
        }
        if (shape.water < 0 && generator.isWaterBlock(block)) {
            shape.water = y;
        }
    }
    for (int y = shape.ground + 1; y < QcWorld::worldHeight; ++y) {
        const QcBlockId block = chunk.block(x, y, z);
        if (block == qcAirBlockId || generator.isWaterBlock(block)) {
            break;
        }
        ++shape.stack;
    }
    return shape;
}

} // namespace

QcChunkShape::QcChunkShape(const QcChunk &chunk, const QcWorldGenerator &generator)
{
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            const QcColumnShape shape = shapeOf(chunk, x, z, generator);
            m_columns[static_cast<std::size_t>(QcBlendField::columnIndex(x, z))] = shape;
            if (shape.isRoot()) {
                m_roots.append({chunk.pos().x * QcWorld::chunkSize + x, chunk.pos().z * QcWorld::chunkSize + z, shape.ground});
            }
        }
    }
}

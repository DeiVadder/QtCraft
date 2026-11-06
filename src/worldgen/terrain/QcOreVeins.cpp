// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcOreVeins.h"

#include "worldgen/terrain/QcNoise.h"

#include <array>

namespace QcOreVeins {

Vein grown(const QcTerrainDefinitions::Ore &ore, int vein, QcChunkPos pos)
{
    static constexpr std::array<std::array<int, 3>, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const auto random = [&](int salt) { return qcPositionRandom(ore.seed, pos.x, vein * 1024 + salt, pos.z); };

    Vein cells;
    cells.append({static_cast<int>(random(0) * QcWorld::chunkSize), ore.minY + static_cast<int>(random(1) * (ore.maxY - ore.minY + 1)),
                  static_cast<int>(random(2) * QcWorld::chunkSize)});
    for (int step = 1, tries = 0; step < ore.veinSize && tries < 4 * ore.veinSize; ++tries) {
        const QcBlockPos from = cells[static_cast<qsizetype>(random(3 + tries * 2) * static_cast<double>(cells.size()))];
        const auto &direction = directions[static_cast<std::size_t>(random(4 + tries * 2) * 6.0)];
        const QcBlockPos next{from.x + direction[0], from.y + direction[1], from.z + direction[2]};
        if (next.x < 0 || next.x >= QcWorld::chunkSize || next.z < 0 || next.z >= QcWorld::chunkSize || next.y < ore.minY
            || next.y > ore.maxY || cells.contains(next)) {
            continue;
        }
        cells.append(next);
        ++step;
    }
    return cells;
}

void place(QcChunk &chunk, const QcTerrainDefinitions::Ore &ore, const Vein &cells)
{
    for (const QcBlockPos &cell : cells) {
        if (cell.y >= 0 && cell.y < QcWorld::worldHeight && chunk.block(cell.x, cell.y, cell.z) == ore.replaces) {
            chunk.setBlock(cell.x, cell.y, cell.z, ore.block);
        }
    }
}

} // namespace QcOreVeins

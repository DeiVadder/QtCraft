// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherGrowth.h"

#include "worldgen/nether/QcNetherTerrain.h"

#include <algorithm>

namespace {

// Fungi keep this far above the sea, where the banks change the ground.
constexpr int fungusAboveSea = 4;
// Free cells a fungus leaves between its cap and a roof.
constexpr int capRoom = 2;

// The world coordinates of the cell `local` of `chunk`.
QcBlockPos worldCell(const QcChunk &chunk, QcBlockPos local)
{
    return {chunk.pos().x * QcWorld::chunkSize + local.x, local.y, chunk.pos().z * QcWorld::chunkSize + local.z};
}

double roll(quint64 seed, QcBlockPos pos, int salt)
{
    return qcPositionRandom(seed + static_cast<quint64>(salt), pos.x, pos.y, pos.z);
}

// How many free cells stand over the floor `y` of `column`.
int roomOver(const std::bitset<QcWorld::worldHeight> &column, int y, int roof)
{
    int room = 0;
    while (y + room + 1 < roof && !column[static_cast<std::size_t>(y) + static_cast<std::size_t>(room) + 1]) {
        ++room;
    }
    return room;
}

// The length a roll of `chance` in 0 to 1 gives `strand`.
int pickLength(const QcNetherDefinitions::Strand &strand, double chance)
{
    return strand.minHeight + static_cast<int>(chance * (strand.maxHeight - strand.minHeight + 1));
}

// Writes a column of `strand` from `start` along `step`, as far as the cells are free.
void writeStrand(QcChunk &chunk, const QcNetherDefinitions::Strand &strand, QcBlockPos start, int step, int length)
{
    for (int cell = 0; cell < length; ++cell) {
        const int y = start.y + cell * step;
        if (chunk.block(start.x, y, start.z) != qcAirBlockId) {
            return;
        }
        chunk.setBlock(start.x, y, start.z, strand.block);
    }
}

} // namespace

QcNetherGrowth::QcNetherGrowth(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain)
    : m_definitions(definitions)
    , m_terrain(terrain)
{
}

void QcNetherGrowth::growFungusColumn(QcChunk &chunk, const QcNetherDefinitions::Fungus &fungus, int x, int z) const
{
    const QcNetherSettings &settings = m_definitions.settings;
    const std::bitset<QcWorld::worldHeight> column = m_terrain.solidColumn(x, z);
    for (int y = settings.liquidLevel + fungusAboveSea; y < settings.roof - 1; ++y) {
        if (!column[static_cast<std::size_t>(y)] || column[static_cast<std::size_t>(y) + 1]) {
            continue;
        }
        const int room = roomOver(column, y, settings.roof) - capRoom;
        if (room < fungus.params.minHeight) {
            continue;
        }

        QcTreeShape::Params params = fungus.params;
        params.maxHeight = std::min(params.maxHeight, room);
        const QcBlockPos base{x, y + 1, z};
        QcTreeShape::buildInChunk(chunk, params, base, roll(fungus.seed, base, 1), fungus.seed + static_cast<quint64>(y));
    }
}

void QcNetherGrowth::growFungi(QcChunk &chunk) const
{
    for (qsizetype index = 0; index < m_definitions.biomes.size(); ++index) {
        for (const QcNetherDefinitions::Fungus &fungus : m_definitions.biomes[index].fungi) {
            growFungiOf(chunk, fungus, index);
        }
    }
}

void QcNetherGrowth::growFungiOf(QcChunk &chunk, const QcNetherDefinitions::Fungus &fungus, qsizetype biome) const
{
    const int startX = chunk.pos().x * QcWorld::chunkSize;
    const int startZ = chunk.pos().z * QcWorld::chunkSize;
    const int reach = QcTreeShape::reach(fungus.params);
    for (int z = startZ - reach; z < startZ + QcWorld::chunkSize + reach; ++z) {
        for (int x = startX - reach; x < startX + QcWorld::chunkSize + reach; ++x) {
            if (qcPositionRandom(fungus.seed, x, 0, z) < fungus.density && m_terrain.biomeAt(x, z) == biome) {
                growFungusColumn(chunk, fungus, x, z);
            }
        }
    }
}

void QcNetherGrowth::growFloor(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, QcBlockPos floor) const
{
    const QcBlockPos world = worldCell(chunk, floor);
    const QcBlockId ground = chunk.block(floor.x, floor.y, floor.z);
    for (const QcNetherDefinitions::Strand &strand : biome.strands) {
        if (strand.fromRoof || !strand.ground.contains(ground) || roll(strand.seed, world, 2) >= strand.density) {
            continue;
        }
        const int length = pickLength(strand, roll(strand.seed, world, 3));
        writeStrand(chunk, strand, floor.offset(0, 1, 0), 1, length);
        return;
    }

    if (biome.plants.isEmpty() || roll(m_definitions.patchSeed, world, 4) >= biome.plantDensity) {
        return;
    }
    const QcVegetationTable::Plant &plant = biome.plants.pick(roll(m_definitions.patchSeed, world, 5));
    if (plant.ground.isEmpty() || plant.ground.contains(ground)) {
        chunk.setBlock(floor.x, floor.y + 1, floor.z, plant.block);
    }
}

void QcNetherGrowth::growRoof(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, QcBlockPos roof) const
{
    const QcBlockPos world = worldCell(chunk, roof);
    for (const QcNetherDefinitions::Strand &strand : biome.strands) {
        if (strand.fromRoof && roll(strand.seed, world, 2) < strand.density) {
            writeStrand(chunk, strand, roof.offset(0, -1, 0), -1, pickLength(strand, roll(strand.seed, world, 3)));
            return;
        }
    }
}

void QcNetherGrowth::growColumn(QcChunk &chunk, int localX, int localZ) const
{
    const QcBlockPos world = worldCell(chunk, {localX, 0, localZ});
    const QcNetherDefinitions::Biome &biome = m_definitions.biomes[m_terrain.biomeAt(world.x, world.z)];
    if (biome.plants.isEmpty() && biome.strands.isEmpty()) {
        return;
    }

    const int level = m_definitions.settings.liquidLevel;
    for (int y = m_definitions.settings.roof - 1; y > level + 1; --y) {
        if (chunk.block(localX, y, localZ) != qcAirBlockId) {
            continue;
        }
        if (m_definitions.isGround(chunk.block(localX, y + 1, localZ))) {
            growRoof(chunk, biome, {localX, y + 1, localZ});
        }
        if (chunk.block(localX, y, localZ) == qcAirBlockId && m_definitions.isGround(chunk.block(localX, y - 1, localZ))) {
            growFloor(chunk, biome, {localX, y - 1, localZ});
        }
    }
}

void QcNetherGrowth::grow(QcChunk &chunk) const
{
    growFungi(chunk);
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            growColumn(chunk, x, z);
        }
    }
}

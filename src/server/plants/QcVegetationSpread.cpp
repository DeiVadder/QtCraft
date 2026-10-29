// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/plants/QcVegetationSpread.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerWorld.h"

#include <QSet>

QcVegetationSpread::QcVegetationSpread(const QcContentRegistry &content)
    : m_content(content)
    , m_fallbackBiome(content.worldgen().terrainSettings().vegetationBiome)
{
    for (const QcBiomeDefinition &biome : content.worldgen().biomes()) {
        m_biomes.insert(biome.name, {QcVegetationTable::resolve(content, biome.vegetation.grass).table,
                                     QcVegetationTable::resolve(content, biome.vegetation.flowers).table});
    }
}

const QcVegetationSpread::Tables *QcVegetationSpread::tablesAt(const QcServerWorld &world, int x, int z) const
{
    const auto found = m_biomes.constFind(world.generator().biomeAt(x, z));
    if (found != m_biomes.constEnd()) {
        return &found.value();
    }
    const auto fallback = m_biomes.constFind(m_fallbackBiome);
    return fallback != m_biomes.constEnd() ? &fallback.value() : nullptr;
}

bool QcVegetationSpread::isWalkable(const QcServerWorld &world, QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const std::optional<QcBlockId> below = world.blockAt(pos.offset(0, -1, 0));
    return block && below && m_content.isValidBlock(*block) && m_content.isValidBlock(*below) && !m_content.block(*block).solid
           && m_content.block(*below).growsVegetation;
}

std::optional<QcBlockPos> QcVegetationSpread::walk(const QcServerWorld &world, QcBlockPos start, int steps, QRandomGenerator &random) const
{
    QcBlockPos pos = start;
    for (int step = 0; step < steps; ++step) {
        // Up or down only now and then: the reference's (rand(3) - 1) * rand(3) / 2.
        const int dy = (random.bounded(3) - 1) * random.bounded(3) / 2;
        pos = pos.offset(random.bounded(3) - 1, dy, random.bounded(3) - 1);
        if (!isWalkable(world, pos)) {
            return std::nullopt;
        }
    }
    return pos;
}

QList<QcVegetationSpread::Planting> QcVegetationSpread::spread(const QcServerWorld &world, QcBlockPos target, QRandomGenerator &random) const
{
    QList<Planting> plantings;
    QSet<QcBlockPos> planned;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const std::optional<QcBlockPos> end = walk(world, target.offset(0, 1, 0), attempt / attemptsPerStep, random);
        if (!end || planned.contains(*end) || world.blockAt(*end) != qcAirBlockId) {
            continue;
        }

        const Tables *tables = tablesAt(world, end->x, end->z);
        if (!tables) {
            continue;
        }
        const bool flower = random.bounded(flowerOdds) == 0 && !tables->flowers.isEmpty();
        const QcVegetationTable &table = flower ? tables->flowers : tables->grass;
        if (table.isEmpty()) {
            continue;
        }
        const QcBlockId plant = table.pick(random.generateDouble()).block;
        if (QcBlockRules::isSupported(world, *end, plant)) {
            plantings.append({*end, plant});
            planned.insert(*end);
        }
    }
    return plantings;
}

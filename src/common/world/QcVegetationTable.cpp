// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcVegetationTable.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWeightedChoice.h"

#include <algorithm>

QcVegetationTable::Resolved QcVegetationTable::resolve(const QcContentRegistry &content, const QcVegetationLayer &layer)
{
    Resolved resolved;
    for (const QcWeightedBlockName &plant : layer.plants) {
        const std::optional<QcBlockId> block = content.blockId(plant.block);
        if (!block) {
            resolved.unknown.append(plant.block);
            continue;
        }
        const QcBlockDefinition &definition = content.block(*block);
        if (definition.solid || definition.liquid) {
            resolved.notPlants.append(plant.block);
            continue;
        }

        Plant entry{*block, plant.weight, {}};
        for (const QString &ground : definition.groundBlocks) {
            if (const std::optional<QcBlockId> id = content.blockId(ground)) {
                entry.ground.insert(*id);
            }
        }
        resolved.table.m_plants.append(entry);
        resolved.table.m_totalWeight += plant.weight;
    }
    return resolved;
}

const QcVegetationTable::Plant &QcVegetationTable::pick(double random) const
{
    const int left = std::clamp(static_cast<int>(random * m_totalWeight), 0, m_totalWeight - 1);
    const auto chosen = qcWeightedFind(m_plants, left, &Plant::weight);
    return chosen != m_plants.end() ? *chosen : m_plants.constLast();
}

#include "common/world/QcVegetationTable.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

QcVegetationTable::Resolved QcVegetationTable::resolve(const QcContentRegistry &content, const QcVegetationLayer &layer)
{
    Resolved resolved;
    for (const QcVegetationPlant &plant : layer.plants) {
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
    int left = std::clamp(int(random * m_totalWeight), 0, m_totalWeight - 1);
    for (const Plant &plant : m_plants) {
        if (left < plant.weight) {
            return plant;
        }
        left -= plant.weight;
    }
    return m_plants.last();
}

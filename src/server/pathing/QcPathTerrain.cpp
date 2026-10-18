#include "server/pathing/QcPathTerrain.h"

#include "common/content/definitions/QcMobDefinition.h"
#include "common/core/QcWorldConstants.h"

QcPathRules QcPathRules::of(const QcMobDefinition &definition)
{
    QcPathRules rules;
    rules.height = definition.height;
    // From the creature's abilities (decision E-423): villagers open wooden doors on their way,
    // climb ladders and never jump off a boardwalk into the water (decision E-411).
    const QcMobAbilities &abilities = definition.abilities;
    rules.opensDoors = abilities.opensDoors;
    rules.climbsLadders = abilities.climbs != QcMobAbilities::Climbing::None;
    rules.climbsWalls = abilities.climbs == QcMobAbilities::Climbing::Walls;
    rules.swims = abilities.swims;
    rules.divesIntoWater = abilities.divesIntoWater;
    // What water hurts keeps out of it (decision E-460).
    rules.avoidsWater = definition.waterDamage > 0.0;
    return rules;
}

quint8 QcPathDirectCells::cell(QcBlockPos pos) const
{
    // The void below the world is no floor and no place to stand; unloaded chunks are never entered.
    if (pos.y < 0) {
        return QcPathCell::make(QcPathCell::Kind::Unknown);
    }
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    return block ? m_table.of(*block) : QcPathCell::make(QcPathCell::Kind::Unknown);
}

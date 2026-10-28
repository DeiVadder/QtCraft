#include "server/pathing/QcPathTerrain.h"

#include "common/content/definitions/QcMobDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "server/pathing/QcPathSteps.h"

#include <array>

QcPathRules QcPathRules::of(const QcMobDefinition &definition)
{
    QcPathRules rules;
    rules.height = definition.height;
    rules.width = static_cast<float>(definition.width);
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

QcPathGap QcPathDirectCells::gap(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    return block ? m_table.gap(*block) : QcPathGap{};
}

namespace QcPathTerrain {

QcBlockPos nodeOf(const QcVec3 &position, QcPathRules rules)
{
    if (rules.width <= 1.0f) {
        return QcPhysics::blockContaining(position);
    }
    const double shift = (rules.footprint() - 1) * 0.5;
    return QcPhysics::blockContaining({position.x - shift, position.y, position.z - shift});
}

QcVec3 pointOf(QcBlockPos node, QcPathRules rules)
{
    const double half = rules.footprint() * 0.5;
    return {node.x + half, static_cast<double>(node.y), node.z + half};
}

namespace {

// Whether a solid block stands beside the columns of a body with its feet cell at `node` on their
// side towards `dx`, `dz` (one of them 1 or -1, the other 0), at the feet or a block above.
bool solidBeside(QcPathDirectCells cells, QcBlockPos node, int cellsWide, std::array<int, 2> towards)
{
    const auto [dx, dz] = towards;
    for (int along = 0; along < cellsWide; ++along) {
        for (int dy = 0; dy <= 1; ++dy) {
            const int x = dx > 0 ? cellsWide : dx < 0 ? -1 : along;
            const int z = dz > 0 ? cellsWide : dz < 0 ? -1 : along;
            if (QcPathCell::kindOf(cells.cell(node.offset(x, dy, z))) == QcPathCell::Kind::Solid) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

QcVec3 climbPoint(QcPathDirectCells cells, QcBlockPos node, QcPathRules rules)
{
    QcVec3 point = pointOf(node, rules);
    const int cellsWide = rules.footprint();
    const double slack = (cellsWide - static_cast<double>(rules.width)) / 2.0;
    // Per axis towards one wall: to the one on the high side, else to the one on the low side (in a
    // shaft with walls on both sides the body touches one of them, never stays between them).
    const auto towards = [&](int dx, int dz) {
        if (solidBeside(cells, node, cellsWide, {dx, dz})) {
            return slack;
        }
        return solidBeside(cells, node, cellsWide, {-dx, -dz}) ? -slack : 0.0;
    };
    point.x += towards(1, 0);
    point.z += towards(0, 1);
    return point;
}

bool clingsOnly(QcPathDirectCells cells, QcBlockPos node, QcPathRules rules)
{
    return everyColumn(node, rules, [&](QcBlockPos column) {
        const QcPathCell::Kind here = QcPathCell::kindOf(cells.cell(column));
        const QcPathCell::Kind below = QcPathCell::kindOf(cells.cell(column.offset(0, -1, 0)));
        return here != QcPathCell::Kind::Low && here != QcPathCell::Kind::Water && below != QcPathCell::Kind::Solid;
    });
}

} // namespace QcPathTerrain

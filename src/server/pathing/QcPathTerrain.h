// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"
#include "server/pathing/QcPathCells.h"

#include <algorithm>
#include <cmath>

struct QcMobDefinition;

// How a creature moves through the cells (decision E-400): its height in cells, how far it drops,
// whether it swims, opens doors, climbs ladders and jumps into deep water.
// Its flags are one bit each, so the rules stay small enough to pass by value.
struct QcPathRules
{
    // Drops deeper than this are refused (the reference's 3; creatures take no fall damage yet, the
    // limit keeps them from leaping off cliffs all the same).
    static constexpr int defaultMaxDrop = 3;
    // Deeper drops, down to this many blocks, only into water at least two blocks deep.
    static constexpr int waterDrop = 12;
    // The highest rise a jump takes on a path: a block with a carpet or a snow layer on it (a jump
    // reaches about 1.45 blocks, 1.25 on mud).
    static constexpr double climbHeight = 1.125;

    double height = 1.8;
    // The body's width: through an open door it keeps to the opening (decision E-905), as long as it
    // overlaps the door's cell. A float and maxDrop short, so the rules stay 16 bytes and pass by value.
    float width = 0.6f;
    qint16 maxDrop = defaultMaxDrop;
    // Water costs a walker much (it is used only without a dry way); a swimmer moves through it freely.
    bool swims : 1 = false;
    // Closed doors and gates block unless the creature opens them: villagers do (decision E-411) and
    // open the doors on their path when QcMobNavigator reports them.
    bool opensDoors : 1 = false;
    // A creature that climbs holds on to a ladder (a place to stand) and goes up and down along it
    // (villagers, decision E-411); to the others a ladder's cell is open like air
    // (QcPathTerrain::kindOf).
    bool climbsLadders : 1 = false;
    // A wall climber (spiders, decision E-451) also holds on to every cell beside a solid block, or
    // whose cell above has one beside it (QcPathTerrain::holdsOn), and climbs up and down there.
    bool climbsWalls : 1 = false;
    // Drops deeper than maxDrop, down to waterDrop, into deep water. Villagers keep to their
    // boardwalks and jetties instead of jumping off them (decision E-411).
    bool divesIntoWater : 1 = true;
    // Never stands in water on its paths (creatures that water hurts, QcMobDefinition::waterDamage,
    // decision E-460).
    bool avoidsWater : 1 = false;

    // A body keeps at least this far from the panel beside it in an open door or trapdoor (decision
    // E-909): the steering lines it up within as much (QcOpeningSteering::laneTolerance).
    static constexpr double panelClearance = 0.05;

    static QcPathRules of(const QcMobDefinition &definition);

    // Cells the body covers along each horizontal axis (decision E-909): one for a body at most a
    // block wide, two for spiders and golems. Its feet cell is the corner cell of its lowest x and z,
    // it walks along the middle of its cells (QcPathTerrain::pointOf).
    int footprint() const { return width <= 1.0f ? 1 : static_cast<int>(std::ceil(width - 1e-4f)); }
    // Whether the body fits through an opening `opening` wide beside a panel.
    bool fitsThrough(float opening) const { return width + 2.0 * panelClearance <= opening + 1e-6; }
};
static_assert(sizeof(QcPathRules) == 16, "QcPathRules passes by value");

// One place a creature can stand in: its feet cell, how high it stands on a low block in that cell
// (a slab: half a block, a carpet an eighth) and whether it is in water.
struct QcPathStand
{
    bool ok = false;
    bool water = false;
    // The floor hurts (magma) or is slippery (ice), or the feet cell slows (cobwebs).
    bool hurts = false;
    bool slippery = false;
    bool slows = false;
    // A cell of the body has a panel (an open door or trapdoor, decision E-909; bodies at most a
    // block wide): steps from and to it check that they do not go through it.
    bool panel = false;
    // Height of the feet above the cell's bottom.
    double height = 0.0;

    double lift() const { return height; }
};

// Standing and passing on the cells of a source with `quint8 cell(QcBlockPos)` and
// `QcPathGap gap(QcBlockPos)` (the cached window of QcPathWindow, or the world directly through
// QcPathDirectCells).
namespace QcPathTerrain {

// The feet cell of a body standing at `position` (decision E-909): the cell containing it, for a
// body wider than a block the corner cell of its columns (QcPathRules::footprint).
QcBlockPos nodeOf(const QcVec3 &position, QcPathRules rules);
// The point a body with its feet cell at `node` walks to: the middle of its columns at the cell's bottom.
QcVec3 pointOf(QcBlockPos node, QcPathRules rules);

// The kind of a cell to a creature: a ladder is open like air to one that does not climb.
inline QcPathCell::Kind kindOf(quint8 cell, QcPathRules rules)
{
    const QcPathCell::Kind kind = QcPathCell::kindOf(cell);
    return kind == QcPathCell::Kind::Ladder && !rules.climbsLadders ? QcPathCell::Kind::Open : kind;
}

// Whether a wall climber holds on in `feet`: a solid block beside the cell or beside the cell above
// (the face of an overhang its body reaches, QcPhysics::clingsToWall).
template<typename Cells>
bool holdsOn(Cells &cells, QcBlockPos feet)
{
    for (const QcBlockPos cell : {feet, feet.offset(0, 1, 0)}) {
        for (const QcBlockPos side : {cell.offset(1, 0, 0), cell.offset(-1, 0, 0), cell.offset(0, 0, 1), cell.offset(0, 0, -1)}) {
            if (QcPathCell::kindOf(cells.cell(side)) == QcPathCell::Kind::Solid) {
                return true;
            }
        }
    }
    return false;
}

// Whether a creature holds on in `feet` like on a ladder: a ladder it climbs, or a wall beside a wall
// climber.
template<typename Cells>
bool climbsAt(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    const QcPathCell::Kind kind = kindOf(cells.cell(feet), rules);
    return kind == QcPathCell::Kind::Ladder || (rules.climbsWalls && kind == QcPathCell::Kind::Open && holdsOn(cells, feet));
}

// Whether a body part may be in the cell: open, water, a door the creature opens; the feet also on a
// low block. A cell above a fence is blocked by the fence's box. An open door or trapdoor only if
// the body fits through beside its panel (decision E-909): cows, sheep, slimes and wider ones do not.
template<typename Cells>
bool passableCell(Cells &cells, QcBlockPos pos, quint8 cell, QcPathRules rules, bool feet)
{
    switch (QcPathCell::kindOf(cell)) {
    case QcPathCell::Kind::Open:
        if ((cell & QcPathCell::panelFlag) != 0 && !rules.fitsThrough(cells.gap(pos).narrowest())) {
            return false;
        }
        break;
    case QcPathCell::Kind::Water:
    case QcPathCell::Kind::Ladder:
        break;
    case QcPathCell::Kind::Low:
        if (!feet) {
            return false;
        }
        break;
    case QcPathCell::Kind::ClosedDoor:
        if (!rules.opensDoors) {
            return false;
        }
        break;
    default:
        return false;
    }
    return !feet || QcPathCell::kindOf(cells.cell(pos.offset(0, -1, 0))) != QcPathCell::Kind::Tall;
}

template<typename Cells>
bool passable(Cells &cells, QcBlockPos pos, QcPathRules rules, bool feet)
{
    return passableCell(cells, pos, cells.cell(pos), rules, feet);
}

// The cells of the body above the feet cell up to the top of a body standing `lift` high: whether
// they are free (`clear`) and whether one of them has a panel.
struct QcPathAbove
{
    bool clear = true;
    bool panel = false;
};

template<typename Cells>
QcPathAbove aboveFeet(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    QcPathAbove above;
    const int top = static_cast<int>(std::ceil(lift + rules.height - 1e-6)) - 1;
    for (int up = 1; up <= top && above.clear; ++up) {
        const QcBlockPos pos = feet.offset(0, up, 0);
        const quint8 cell = cells.cell(pos);
        above.clear = passableCell(cells, pos, cell, rules, false);
        above.panel = above.panel || QcPathCell::hasPanel(cell);
    }
    return above;
}

// Cells of the body above the feet cell are free up to the top of a body standing `lift` high.
template<typename Cells>
bool clearAbove(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    return aboveFeet(cells, feet, lift, rules).clear;
}

// Whether `test` holds for every column of the body standing with its feet cell at `feet`
// (QcPathRules::footprint), given the cell of each column at the feet's level.
template<typename Test>
bool everyColumn(QcBlockPos feet, QcPathRules rules, const Test &test)
{
    if (rules.width <= 1.0f) {
        return test(feet);
    }
    const int cells = rules.footprint();
    for (int dx = 0; dx < cells; ++dx) {
        for (int dz = 0; dz < cells; ++dz) {
            if (!test(feet.offset(dx, 0, dz))) {
                return false;
            }
        }
    }
    return true;
}

// Room for the body standing `lift` high with its feet cell at `feet`: in every column the feet's
// cell and the cells above it up to the body's top (a body wider than a block fills four columns).
template<typename Cells>
bool bodyRoom(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    return everyColumn(feet, rules, [&](QcBlockPos column) { return passable(cells, column, rules, true) && clearAbove(cells, column, lift, rules); });
}

// The floor of the column of `feet` for a body standing in its cell (a floor below, a low block in
// the cell on something that carries it, the surface of water or a ladder it holds on to), without
// looking at the room for the body; not ok without one.
template<typename Cells>
QcPathStand floorAt(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    QcPathStand stand;
    const quint8 here = cells.cell(feet);
    const QcPathCell::Kind kind = kindOf(here, rules);
    if (!passable(cells, feet, rules, true)) {
        return stand;
    }

    const quint8 below = cells.cell(feet.offset(0, -1, 0));
    if (kind == QcPathCell::Kind::Low) {
        // A low block is a floor on whatever carries it (a block, water under a lily pad, a snow
        // layer on snow), not hanging over open or unknown cells.
        const QcPathCell::Kind carrier = QcPathCell::kindOf(below);
        if (carrier == QcPathCell::Kind::Open || carrier == QcPathCell::Kind::Unknown) {
            return stand;
        }
        stand.height = QcPathCell::liftOf(here);
        stand.hurts = (here & QcPathCell::hurtsFlag) != 0;
    } else if (kind == QcPathCell::Kind::Water) {
        // At the surface, or wading on a bed of solid ground.
        const bool surface = QcPathCell::kindOf(cells.cell(feet.offset(0, 1, 0))) != QcPathCell::Kind::Water;
        if (rules.avoidsWater || (!surface && QcPathCell::kindOf(below) != QcPathCell::Kind::Solid)) {
            return stand;
        }
        stand.water = true;
    } else if (QcPathCell::kindOf(below) == QcPathCell::Kind::Solid || climbsAt(cells, feet, rules)) {
        stand.hurts = (below & QcPathCell::hurtsFlag) != 0;
        stand.slippery = QcPathCell::kindOf(below) == QcPathCell::Kind::Solid && (below & QcPathCell::slipperyFlag) != 0;
    } else {
        return stand;
    }

    stand.slows = (here & QcPathCell::slowsFlag) != 0;
    stand.panel = QcPathCell::hasPanel(here);
    stand.ok = true;
    return stand;
}

// Whether the creature can stand with its feet cell at `feet`: a floor (floorAt) and room for the
// body. A body wider than a block (decision E-909) stands on the highest floor of its columns (it
// overhangs the others, as in the physics) and needs room in all of them.
template<typename Cells>
QcPathStand standAt(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    if (rules.width <= 1.0f) {
        QcPathStand stand = floorAt(cells, feet, rules);
        if (stand.ok) {
            const QcPathAbove above = aboveFeet(cells, feet, stand.lift(), rules);
            stand.ok = above.clear;
            stand.panel = stand.panel || above.panel;
        }
        return stand;
    }

    QcPathStand stand;
    const int columns = rules.footprint();
    for (int dx = 0; dx < columns; ++dx) {
        for (int dz = 0; dz < columns; ++dz) {
            const QcPathStand floor = floorAt(cells, feet.offset(dx, 0, dz), rules);
            if (floor.ok && (!stand.ok || floor.height > stand.height)) {
                stand = floor;
            }
        }
    }
    // A wide body never stands beside a panel (passable refuses the cell).
    stand.panel = false;
    stand.ok = stand.ok && bodyRoom(cells, feet, stand.lift(), rules);
    return stand;
}

} // namespace QcPathTerrain

// The cells straight from the world, without a cache (checking a few waypoints of a path).
class QcPathDirectCells
{
public:
    QcPathDirectCells(const QcBlockAccess &world, const QcPathCellTable &table)
        : m_world(world)
        , m_table(table)
    {
    }

    quint8 cell(QcBlockPos pos) const;
    // The gap of an open door or trapdoor in the cell (QcPathCell::hasPanel).
    QcPathGap gap(QcBlockPos pos) const;

private:
    const QcBlockAccess &m_world;
    const QcPathCellTable &m_table;
};

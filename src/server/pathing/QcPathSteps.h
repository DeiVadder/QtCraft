// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/pathing/QcPathTerrain.h"

// What a step between cells needs besides the place it leads to (QcPathTerrain::standAt): room for
// a jump, a way down for a fall, holding on to climb, and no panel in the way (decision E-909).
namespace QcPathTerrain {

// Room above the feet's level in every column of the body up to the top of a body `lift` high (to
// jump up a block), and a panel there (bodies at most a block wide).
template<typename Cells>
QcPathAbove roomAbove(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    if (rules.width <= 1.0f) {
        return aboveFeet(cells, feet, lift, rules);
    }
    return {everyColumn(feet, rules, [&](QcBlockPos column) { return clearAbove(cells, column, lift, rules); }), false};
}

// Room for the body standing `lift` high with its feet cell at `feet` (bodyRoom), and a panel in a
// cell of it (bodies at most a block wide).
template<typename Cells>
QcPathAbove roomAt(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    if (rules.width > 1.0f) {
        return {bodyRoom(cells, feet, lift, rules), false};
    }
    const quint8 cell = cells.cell(feet);
    if (!passableCell(cells, feet, cell, rules, true)) {
        return {false, false};
    }
    const QcPathAbove above = aboveFeet(cells, feet, lift, rules);
    return {above.clear, above.panel || QcPathCell::hasPanel(cell)};
}

// Whether a falling body passes the level of `feet` in every column.
template<typename Cells>
bool fallsThrough(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    return everyColumn(feet, rules, [&](QcBlockPos column) { return passable(cells, column, rules, false); });
}

// Whether a creature holds on in one of the columns of its body like on a ladder.
template<typename Cells>
bool bodyClimbs(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    return !everyColumn(feet, rules, [&](QcBlockPos column) { return !climbsAt(cells, column, rules); });
}

// Whether a step from the feet cell `from` to the cell `to` beside it (on any level) goes through
// a side of a cell that a panel covers, in the column it leaves or the one it enters, at any level
// the body passes standing at most `lift` high (the leaf of a free-standing open door is not walked
// through).
template<typename Cells>
bool crossesPanel(Cells &cells, QcBlockPos from, QcBlockPos to, double lift, QcPathRules rules)
{
    const int dx = to.x - from.x;
    const int dz = to.z - from.z;
    const int top = std::max(from.y, to.y) + static_cast<int>(std::ceil(lift + rules.height - 1e-6));
    for (int y = std::min(from.y, to.y); y < top; ++y) {
        const QcBlockPos leaving{from.x, y, from.z};
        const QcBlockPos entering{to.x, y, to.z};
        if ((QcPathCell::hasPanel(cells.cell(leaving)) && cells.gap(leaving).coversSide(dx, dz))
            || (QcPathCell::hasPanel(cells.cell(entering)) && cells.gap(entering).coversSide(-dx, -dz))) {
            return true;
        }
    }
    return false;
}

// Water at least two blocks deep from `cell` down: a fall of any height into it is safe.
template<typename Cells>
bool deepWater(Cells &cells, QcBlockPos cell)
{
    return QcPathCell::kindOf(cells.cell(cell)) == QcPathCell::Kind::Water
           && QcPathCell::kindOf(cells.cell(cell.offset(0, -1, 0))) == QcPathCell::Kind::Water;
}

// Whether a body that falls `drop` blocks into `landing`, a cell it can stand in, lands safely: at
// most QcPathRules::maxDrop, or into deep water (QcPathRules::divesIntoWater).
template<typename Cells>
bool safeLanding(Cells &cells, QcBlockPos landing, int drop, QcPathRules rules)
{
    return drop <= rules.maxDrop || (rules.divesIntoWater && deepWater(cells, landing));
}

// The point a wall climber wider than a block goes up or down at with its feet cell at `node`:
// pointOf, moved by the room its columns leave beside its body towards a solid block beside them, at
// its feet or a block above. In the middle of its columns a spider or a golem is farther from the
// wall than it reaches (QcPhysics::clingReach), touching the wall it holds on (decision E-909).
QcVec3 climbPoint(QcPathDirectCells cells, QcBlockPos node, QcPathRules rules);
// Whether a body with its feet cell at `node` is held there only by clinging to a wall: no floor
// (a solid block below, a low block, water) in any of its columns.
bool clingsOnly(QcPathDirectCells cells, QcBlockPos node, QcPathRules rules);

} // namespace QcPathTerrain

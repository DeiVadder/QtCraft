#pragma once

#include "QcBlockAccess.h"
#include "QcPathCells.h"

#include <cmath>

struct QcMobDefinition;

// How a creature moves through the cells (decision E-400): its height in cells, how far it drops,
// whether it swims, opens doors, climbs ladders and jumps into deep water.
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
    int maxDrop = defaultMaxDrop;
    // Water costs a walker much (it is used only without a dry way); a swimmer moves through it freely.
    bool swims = false;
    // Closed doors and gates block unless the creature opens them: villagers do (decision E-411) and
    // open the doors on their path when QcMobNavigator reports them.
    bool opensDoors = false;
    // A creature that climbs holds on to a ladder (a place to stand) and goes up and down along it
    // (villagers, decision E-411); to the others a ladder's cell is open like air
    // (QcPathTerrain::kindOf).
    bool climbsLadders = false;
    // Drops deeper than maxDrop, down to waterDrop, into deep water. Villagers keep to their
    // boardwalks and jetties instead of jumping off them (decision E-411).
    bool divesIntoWater = true;

    static QcPathRules of(const QcMobDefinition &definition);
};

// One place a creature can stand in: its feet cell, how high it stands on a low block in that cell
// (a slab: half a block, a carpet an eighth) and whether it is in water.
struct QcPathStand
{
    bool ok = false;
    bool water = false;
    // The floor hurts (magma) or the feet cell slows (cobwebs).
    bool hurts = false;
    bool slows = false;
    // Height of the feet above the cell's bottom.
    double height = 0.0;

    double lift() const { return height; }
};

// Standing and passing on the cells of a source with `quint8 cell(QcBlockPos)` (the cached window
// of QcPathGrid, or the world directly through QcPathDirectCells).
namespace QcPathTerrain {

// The kind of a cell to a creature: a ladder is open like air to one that does not climb.
inline QcPathCell::Kind kindOf(quint8 cell, QcPathRules rules)
{
    const QcPathCell::Kind kind = QcPathCell::kindOf(cell);
    return kind == QcPathCell::Kind::Ladder && !rules.climbsLadders ? QcPathCell::Kind::Open : kind;
}

// Whether a body part may be in the cell: open, water, a door the creature opens; the feet also on a
// low block. A cell above a fence is blocked by the fence's box.
template<typename Cells>
bool passable(Cells &cells, QcBlockPos pos, QcPathRules rules, bool feet)
{
    const QcPathCell::Kind kind = QcPathCell::kindOf(cells.cell(pos));
    switch (kind) {
    case QcPathCell::Kind::Open:
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

// Cells of the body above the feet cell are free up to the top of a body standing `lift` high.
template<typename Cells>
bool clearAbove(Cells &cells, QcBlockPos feet, double lift, QcPathRules rules)
{
    const int top = int(std::ceil(lift + rules.height - 1e-6)) - 1;
    for (int up = 1; up <= top; ++up) {
        if (!passable(cells, feet.offset(0, up, 0), rules, false)) {
            return false;
        }
    }
    return true;
}

// Whether the creature can stand with its feet in `feet`: a floor below (or a low block in the
// cell, the surface of water or a ladder it holds on to) and room for the body.
template<typename Cells>
QcPathStand standAt(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    QcPathStand stand;
    const quint8 here = cells.cell(feet);
    const QcPathCell::Kind kind = kindOf(here, rules);
    if (!passable(cells, feet, rules, true)) {
        return stand;
    }

    const quint8 below = cells.cell(feet.offset(0, -1, 0));
    if (kind == QcPathCell::Kind::Low) {
        stand.height = QcPathCell::liftOf(here);
        stand.hurts = (here & QcPathCell::hurtsFlag) != 0;
    } else if (kind == QcPathCell::Kind::Water) {
        // At the surface, or wading on a bed of solid ground.
        const bool surface = QcPathCell::kindOf(cells.cell(feet.offset(0, 1, 0))) != QcPathCell::Kind::Water;
        if (!surface && QcPathCell::kindOf(below) != QcPathCell::Kind::Solid) {
            return stand;
        }
        stand.water = true;
    } else if (kind == QcPathCell::Kind::Ladder || QcPathCell::kindOf(below) == QcPathCell::Kind::Solid) {
        stand.hurts = (below & QcPathCell::hurtsFlag) != 0;
    } else {
        return stand;
    }

    stand.slows = (here & QcPathCell::slowsFlag) != 0;
    stand.ok = clearAbove(cells, feet, stand.lift(), rules);
    return stand;
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

private:
    const QcBlockAccess &m_world;
    const QcPathCellTable &m_table;
};

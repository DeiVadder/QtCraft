#pragma once

#include "QcBlockAccess.h"
#include "QcPathCells.h"

#include <cmath>

struct QcMobDefinition;

// How a creature moves through the cells (decision E-400): its height in cells, how far it drops,
// whether it swims and whether it opens doors.
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
    // Closed doors and gates block unless the creature opens them; the villagers of stage 37.2 will
    // set this and open the doors on their path (the hook is QcMobNavigator's door step).
    bool opensDoors = false;

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

// Whether a body part may be in the cell: open, water, a door the creature opens; the feet also on a
// low block. A cell above a fence is blocked by the fence's box.
template<typename Cells>
bool passable(Cells &cells, QcBlockPos pos, QcPathRules rules, bool feet)
{
    const QcPathCell::Kind kind = QcPathCell::kindOf(cells.cell(pos));
    switch (kind) {
    case QcPathCell::Kind::Open:
    case QcPathCell::Kind::Water:
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
// cell, or the surface of water) and room for the body.
template<typename Cells>
QcPathStand standAt(Cells &cells, QcBlockPos feet, QcPathRules rules)
{
    QcPathStand stand;
    const quint8 here = cells.cell(feet);
    const QcPathCell::Kind kind = QcPathCell::kindOf(here);
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
    } else if (QcPathCell::kindOf(below) == QcPathCell::Kind::Solid) {
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
// most QcPathRules::maxDrop, or into deep water.
template<typename Cells>
bool safeLanding(Cells &cells, QcBlockPos landing, int drop, QcPathRules rules)
{
    return drop <= rules.maxDrop || deepWater(cells, landing);
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

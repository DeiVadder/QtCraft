#include "QcPistonPlanner.h"

#include "QcBlockRules.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace {

enum class Cell : quint8 { Empty, Moves, Breaks, Blocks };

// What a piston finds in a cell: room, a block it pushes, one that breaks, or one that stops it
// (immovable, extended pistons, unloaded chunks, outside the world).
Cell cellAt(QcSignalPower power, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = power.blockAt(pos);
    if (!block || !pos.isInsideWorldHeight()) {
        return Cell::Blocks;
    }
    if (*block == qcAirBlockId) {
        return Cell::Empty;
    }

    const QcSignalState &state = power.table().at(*block);
    if (state.kind == QcSignalKind::Piston && state.on) {
        return Cell::Blocks;
    }
    switch (state.mobility) {
    case QcPistonMobility::Immovable:
        return Cell::Blocks;
    case QcPistonMobility::Breaks:
        return Cell::Breaks;
    default:
        return Cell::Moves;
    }
}

} // namespace

QList<QcBlockPos> QcPistonPlan::touched() const
{
    QList<QcBlockPos> cells{base, head()};
    const QcBlockFace towards = extend ? facing : QcBlockRules::opposite(facing);
    for (const QcBlockPos &pos : moved) {
        cells.append(pos);
        cells.append(qcNeighbour(pos, towards));
    }
    cells.append(broken);
    return cells;
}

namespace QcPistonPlanner {

[[nodiscard]] std::optional<QcPistonPlan> extension(QcSignalPower power, QcBlockPos base, QcSignalState piston,
                                      const QcSignalDefinition &definition)
{
    if (!piston.facing) {
        return std::nullopt;
    }

    QcPistonPlan plan{base, *piston.facing, true, definition.sticky, {}, {}};
    QcBlockPos cell = plan.head();
    for (;;) {
        const Cell found = cellAt(power, cell);
        if (found == Cell::Empty) {
            return plan;
        }
        if (found == Cell::Breaks) {
            plan.broken.append(cell);
            return plan;
        }
        if (found == Cell::Blocks || plan.moved.size() >= definition.pushLimit) {
            return std::nullopt;
        }
        plan.moved.append(cell);
        cell = qcNeighbour(cell, plan.facing);
    }
}

QcPistonPlan retraction(QcSignalPower power, QcBlockPos base, QcSignalState piston, const QcSignalDefinition &definition,
                        bool letGo)
{
    QcPistonPlan plan{base, piston.facing.value_or(QcBlockFace::North), false, definition.sticky, {}, {}};
    if (!definition.sticky || letGo || !piston.facing) {
        return plan;
    }

    const QcBlockPos pulled = qcNeighbour(plan.head(), plan.facing);
    if (cellAt(power, pulled) == Cell::Moves) {
        plan.moved.append(pulled);
    }
    return plan;
}

QList<QcPistonPlan> resolveConflicts(const QList<QcPistonPlan> &plans)
{
    QHash<QcBlockPos, int> users;
    for (const QcPistonPlan &plan : plans) {
        // A plan counts once per cell, even if it names a cell twice.
        const QList<QcBlockPos> cells = plan.touched();
        const QSet<QcBlockPos> unique(cells.begin(), cells.end());
        for (const QcBlockPos &cell : unique) {
            ++users[cell];
        }
    }

    QList<QcPistonPlan> kept;
    for (const QcPistonPlan &plan : plans) {
        const QList<QcBlockPos> cells = plan.touched();
        const bool alone = std::ranges::all_of(cells, [&users](QcBlockPos cell) { return users.value(cell) == 1; });
        if (alone) {
            kept.append(plan);
        }
    }
    return kept;
}

} // namespace QcPistonPlanner

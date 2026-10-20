#pragma once

#include "server/signal/QcCircuitPlan.h"
#include "server/signal/QcSignalPower.h"
#include "server/signal/QcSignalSchedule.h"

#include <QList>
#include <QSet>

#include <optional>

// Tripwire hooks and the tripwire between them (decision E-782), as decisions of a circuit step on
// the world before it. A hook hangs on the side of a block and its wire leaves it away from that
// block; two hooks facing each other with nothing but tripwire between them (at most the hook's
// `wireReach` cells) form a line: both hooks and every wire of it are `attached`, and both hooks are
// `powered` while any wire of the line is. A wire is powered while something touches it and released
// when nothing has for its `pulseTicks`. Breaking an attached wire that shears did not disarm sets
// its hooks off for their `pulseTicks` before they see the broken line.
class QcTripwires
{
public:
    // `touched`: the wires something touches in this tick; `schedule`: the scheduled ticks, where a
    // hook set off by a torn wire waits for the end of its pulse.
    QcTripwires(const QcSignalPower &power, const QSet<QcBlockPos> &touched, const QcSignalSchedule &schedule);

    // A block changed at or next to the hook at `pos`: its line again.
    void reactHook(QcBlockPos pos, QcCircuitPlan &plan) const;
    // A block changed at or next to the wire at `pos`: the lines of the hooks it lies between.
    void reactWire(QcBlockPos pos, QcCircuitPlan &plan) const;
    // The scheduled tick of the wire `block` at `pos`: released unless something still touches it.
    void scheduledWire(QcBlockPos pos, QcBlockId block, QcCircuitPlan &plan) const;
    // The hook or wire `before` at `pos` is gone: the lines it belonged to again.
    void removed(QcBlockPos pos, QcBlockId before, QcCircuitPlan &plan) const;

private:
    // The cells from a hook along its wire: the wires up to the first other block, the hook facing
    // back at the end of them (none when the line is broken) and whether any wire is powered.
    struct Line
    {
        QcBlockPos hook;
        std::optional<QcBlockPos> partner;
        QList<QcBlockPos> wires;
        bool powered = false;
    };

    // The line of the hook at `hook` facing `facing` (towards its block); the wire runs the other way.
    [[nodiscard]] Line lineOf(QcBlockPos hook, QcBlockFace facing, int reach) const;
    // The hook reached from `from` along `along` over tripwire, facing `along` (its wire comes back).
    [[nodiscard]] std::optional<QcBlockPos> hookAlong(QcBlockPos from, QcBlockFace along) const;
    // Writes the hooks and wires of `line`.
    void decide(const Line &line, QcCircuitPlan &plan) const;
    // Whether the hook at `pos` is powered with the line `line`: while the line is powered, or while
    // the pulse of a torn wire lasts.
    bool hookPowered(QcBlockPos pos, const Line &line, const QcCircuitPlan &plan) const;
    // Sets a flag of the block at `pos` (as the plan has it so far) when it differs.
    void setFlag(QcCircuitPlan &plan, QcBlockPos pos, const QString &property, bool value) const;
    [[nodiscard]] QcBlockId plannedAt(const QcCircuitPlan &plan, QcBlockPos pos) const;
    bool flag(QcBlockId block, const QString &property) const;

    const QcSignalPower &m_power;
    const QSet<QcBlockPos> &m_touched;
    const QcSignalSchedule &m_schedule;
};

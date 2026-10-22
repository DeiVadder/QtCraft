#include "server/signal/QcTripwires.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

namespace {

const QString attachedName = u"attached"_s;
const QString poweredName = u"powered"_s;

} // namespace

QcTripwires::QcTripwires(const QcSignalPower &power, const QSet<QcBlockPos> &touched, const QcSignalSchedule &schedule)
    : m_power(power)
    , m_touched(touched)
    , m_schedule(schedule)
{
}

bool QcTripwires::hookPowered(QcBlockPos pos, const Line &line, const QcCircuitPlan &plan) const
{
    if (line.partner && line.powered) {
        return true;
    }
    return m_schedule.contains(pos) && flag(plannedAt(plan, pos), poweredName);
}

bool QcTripwires::flag(QcBlockId block, const QString &property) const
{
    return m_power.table().content().property(block, property) == u"true"_s;
}

QcBlockId QcTripwires::plannedAt(const QcCircuitPlan &plan, QcBlockPos pos) const
{
    return plan.writes.value(pos, m_power.blockAt(pos).value_or(qcAirBlockId));
}

void QcTripwires::setFlag(QcCircuitPlan &plan, QcBlockPos pos, const QString &property, bool value) const
{
    const QcBlockId block = plannedAt(plan, pos);
    if (flag(block, property) != value) {
        plan.write(pos, m_power.table().withFlag(block, property, value));
    }
}

QcTripwires::Line QcTripwires::lineOf(QcBlockPos hook, QcBlockFace facing, int reach) const
{
    Line line{hook, std::nullopt, {}, false};
    const QcBlockFace along = QcBlockRules::opposite(facing);
    QcBlockPos cell = hook;
    for (int step = 0; step <= reach; ++step) {
        cell = qcNeighbour(cell, along);
        const QcSignalState &state = m_power.at(cell);
        if (state.kind == QcSignalKind::Hook && state.facing == along && !line.wires.isEmpty()) {
            line.partner = cell;
            break;
        }
        if (state.kind != QcSignalKind::Tripwire) {
            break;
        }
        line.wires.append(cell);
        line.powered = line.powered || state.on;
    }
    return line;
}

std::optional<QcBlockPos> QcTripwires::hookAlong(QcBlockPos from, QcBlockFace along) const
{
    QcBlockPos cell = from;
    for (int step = 0; step <= QcSignalDefinition::maxWireReach; ++step) {
        cell = qcNeighbour(cell, along);
        const QcSignalState &state = m_power.at(cell);
        if (state.kind == QcSignalKind::Hook) {
            return state.facing == along ? std::optional(cell) : std::nullopt;
        }
        if (state.kind != QcSignalKind::Tripwire) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void QcTripwires::decide(const Line &line, QcCircuitPlan &plan) const
{
    const bool attached = line.partner.has_value();
    setFlag(plan, line.hook, poweredName, hookPowered(line.hook, line, plan));
    setFlag(plan, line.hook, attachedName, attached);
    if (line.partner) {
        setFlag(plan, *line.partner, poweredName, hookPowered(*line.partner, line, plan));
        setFlag(plan, *line.partner, attachedName, attached);
    }
    for (const QcBlockPos &wire : line.wires) {
        setFlag(plan, wire, attachedName, attached);
    }
}

void QcTripwires::reactHook(QcBlockPos pos, QcCircuitPlan &plan) const
{
    const std::optional<QcBlockId> block = m_power.blockAt(pos);
    const QcSignalState &state = m_power.at(pos);
    if (!block || !state.facing) {
        return;
    }
    const int reach = m_power.table().content().block(*block).signal.wireReach;
    const Line line = lineOf(pos, *state.facing, reach);
    plan.addWork(line.wires.size());
    decide(line, plan);
}

void QcTripwires::reactWire(QcBlockPos pos, QcCircuitPlan &plan) const
{
    // A powered wire always waits for its release; one whose scheduled tick was dropped (it waited on
    // an unloaded chunk and came back as a plain change) gets a new one instead of staying powered.
    const std::optional<QcBlockId> block = m_power.blockAt(pos);
    if (block && flag(*block, poweredName) && !m_schedule.contains(pos)) {
        const int pulse = m_power.table().content().block(*block).signal.pulseTicks;
        plan.schedule(pos, pulse, QcSignalSchedule::Priority::Release);
    }

    // The hooks at both ends of the wire's line, whichever axis it runs along.
    for (const QcBlockFace along : QcSignalTable::horizontal) {
        if (const std::optional<QcBlockPos> hook = hookAlong(pos, along)) {
            reactHook(*hook, plan);
        }
    }
}

void QcTripwires::scheduledWire(QcBlockPos pos, QcBlockId block, QcCircuitPlan &plan) const
{
    if (!flag(block, poweredName)) {
        return;
    }
    if (m_touched.contains(pos)) {
        const int pulse = m_power.table().content().block(block).signal.pulseTicks;
        plan.schedule(pos, pulse, QcSignalSchedule::Priority::Release);
        return;
    }
    setFlag(plan, pos, poweredName, false);
}

void QcTripwires::removed(QcBlockPos pos, QcBlockId before, QcCircuitPlan &plan) const
{
    const QcSignalState &was = m_power.table().at(before);
    if (was.kind == QcSignalKind::Hook && was.facing) {
        // The wires of a hook that is gone hang loose; its partner sees the broken line.
        const QcBlockFace along = QcBlockRules::opposite(*was.facing);
        const Line loose = lineOf(pos, *was.facing, QcSignalDefinition::maxWireReach);
        for (const QcBlockPos &wire : loose.wires) {
            setFlag(plan, wire, attachedName, false);
        }
        if (const std::optional<QcBlockPos> partner = hookAlong(pos, along)) {
            reactHook(*partner, plan);
        }
        return;
    }
    if (was.kind != QcSignalKind::Tripwire) {
        return;
    }

    const bool trips = flag(before, attachedName) && !flag(before, u"disarmed"_s);
    for (const QcBlockFace along : QcSignalTable::horizontal) {
        const std::optional<QcBlockPos> hook = hookAlong(pos, along);
        if (!hook) {
            continue;
        }
        reactHook(*hook, plan);
        if (trips) {
            // A wire torn without shears sets the hook off once; its scheduled tick sees the broken line.
            setFlag(plan, *hook, poweredName, true);
            const QcBlockId block = plannedAt(plan, *hook);
            plan.schedule(*hook, m_power.table().content().block(block).signal.pulseTicks, QcSignalSchedule::Priority::Release);
        }
    }
}

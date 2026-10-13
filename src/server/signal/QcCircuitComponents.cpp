#include "QcCircuitComponents.h"

#include "QcBlockRules.h"
#include "QcContentRegistry.h"
#include "QcMovingBlocks.h"

using namespace Qt::StringLiterals;

using Priority = QcSignalSchedule::Priority;

namespace {

constexpr quint8 faceBit(QcBlockFace face)
{
    return quint8(1U << unsigned(face));
}

// The side to the left of something pointing at `face`, seen from above.
QcBlockFace leftOf(QcBlockFace face)
{
    const int index = QcSignalTable::horizontalIndex(face);
    return index < 0 ? face : QcSignalTable::horizontal[std::size_t((index + 3) % 4)];
}

} // namespace

QcCircuitComponents::QcCircuitComponents(const Context &context)
    : m_context(context)
{
}

const QcSignalDefinition &QcCircuitComponents::definition(QcBlockId block) const
{
    return table().content().block(block).signal;
}

void QcCircuitComponents::react(QcBlockPos pos, QcCircuitPlan &plan)
{
    const std::optional<QcBlockId> block = m_context.power.blockAt(pos);
    if (!block) {
        return;
    }

    const QcSignalState &state = table().at(*block);
    ++plan.work;
    switch (state.kind) {
    case QcSignalKind::Torch:
        reactTorch(pos, state, plan);
        break;
    case QcSignalKind::Repeater:
        reactRepeater(pos, *block, state, plan);
        break;
    case QcSignalKind::Lamp:
        reactLamp(pos, *block, state, plan);
        break;
    case QcSignalKind::Opener:
        reactOpener(pos, *block, state, plan);
        break;
    case QcSignalKind::Piston:
        reactPiston(pos, *block, state, plan);
        break;
    case QcSignalKind::PistonHead:
        reactHead(pos, state, plan);
        break;
    default:
        break;
    }
}

void QcCircuitComponents::scheduled(QcBlockPos pos, QcCircuitPlan &plan)
{
    const std::optional<QcBlockId> block = m_context.power.blockAt(pos);
    if (!block) {
        return;
    }

    const QcSignalState &state = table().at(*block);
    ++plan.work;
    switch (state.kind) {
    case QcSignalKind::Torch:
        switchTorch(pos, *block, state, plan);
        break;
    case QcSignalKind::Repeater:
        switchRepeater(pos, *block, state, plan);
        break;
    case QcSignalKind::Lamp:
        if (state.on && m_context.power.input(pos) == 0) {
            plan.writes.insert(pos, table().counterpartOf(*block));
        }
        break;
    case QcSignalKind::Button:
        if (state.on) {
            plan.writes.insert(pos, table().withFlag(*block, u"powered"_s, false));
        }
        break;
    case QcSignalKind::Plate:
        releasePlate(pos, *block, state, plan);
        break;
    case QcSignalKind::Piston:
        reactPiston(pos, *block, state, plan);
        break;
    case QcSignalKind::Opener:
        reactOpener(pos, *block, state, plan);
        break;
    default:
        break;
    }
}

bool QcCircuitComponents::torchShouldLight(QcBlockPos pos, QcSignalState state) const
{
    const bool burnt = m_context.memory.burntOutUntil.value(pos, 0) > m_context.now;
    const bool powered = state.facing && m_context.power.weakLevel(qcNeighbour(pos, *state.facing)) > 0;
    return !burnt && !powered;
}

void QcCircuitComponents::reactTorch(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    if (torchShouldLight(pos, state) != state.on) {
        plan.schedules.append({pos, torchDelayTicks, Priority::Diode});
    }
}

void QcCircuitComponents::switchTorch(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan)
{
    QcCircuitState &memory = m_context.memory;
    if (memory.burntOutUntil.value(pos, m_context.now + 1) <= m_context.now) {
        memory.burntOutUntil.remove(pos);
    }

    const bool light = torchShouldLight(pos, state);
    if (light == state.on) {
        return;
    }
    plan.writes.insert(pos, table().counterpartOf(block));
    if (light) {
        return;
    }

    // Going out: too often within the window burns the torch out.
    QList<quint64> &offTicks = memory.torchOffTicks[pos];
    offTicks.removeIf([this](quint64 tick) { return tick + burnoutWindowTicks <= m_context.now; });
    offTicks.append(m_context.now);
    if (offTicks.size() >= burnoutToggles) {
        memory.torchOffTicks.remove(pos);
        memory.burntOutUntil.insert(pos, m_context.now + burnoutTicks);
        plan.schedules.append({pos, burnoutTicks, Priority::Release});
        plan.burntOut.append(pos);
    }
}

bool QcCircuitComponents::repeaterLocked(QcBlockPos pos, QcSignalState state) const
{
    if (!state.facing) {
        return false;
    }

    for (const QcBlockFace side : {leftOf(*state.facing), QcBlockRules::opposite(leftOf(*state.facing))}) {
        const QcSignalState &neighbour = m_context.power.at(qcNeighbour(pos, side));
        if (neighbour.kind == QcSignalKind::Repeater && neighbour.on && neighbour.facing == QcBlockRules::opposite(side)) {
            return true;
        }
    }
    return false;
}

void QcCircuitComponents::reactRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool locked = repeaterLocked(pos, state);
    if (locked != state.locked) {
        plan.writes.insert(pos, table().withFlag(block, u"locked"_s, locked));
    }
    if (locked || !state.facing) {
        return;
    }

    const bool input = m_context.power.inputFrom(pos, QcBlockRules::opposite(*state.facing)) > 0;
    if (input != state.on && !m_context.memory.schedule.contains(pos)) {
        plan.schedules.append({pos, state.delay * repeaterStepTicks, Priority::Diode});
    }
}

void QcCircuitComponents::switchRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    if (state.locked || !state.facing) {
        return;
    }

    // As in the reference: an unpowered repeater always turns on for at least its delay, so short
    // pulses come out stretched to the delay.
    const bool input = m_context.power.inputFrom(pos, QcBlockRules::opposite(*state.facing)) > 0;
    if (state.on && !input) {
        plan.writes.insert(pos, table().withFlag(block, u"powered"_s, false));
    } else if (!state.on) {
        plan.writes.insert(pos, table().withFlag(block, u"powered"_s, true));
        if (!input) {
            plan.schedules.append({pos, state.delay * repeaterStepTicks, Priority::Diode});
        }
    }
}

void QcCircuitComponents::reactLamp(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = m_context.power.input(pos) > 0;
    if (powered && !state.on) {
        plan.writes.insert(pos, table().counterpartOf(block));
    } else if (!powered && state.on) {
        const int delay = definition(block).offDelayTicks;
        if (delay > 0) {
            plan.schedules.append({pos, delay, Priority::Release});
        } else {
            plan.writes.insert(pos, table().counterpartOf(block));
        }
    }
}

void QcCircuitComponents::reactOpener(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan)
{
    const QcContentRegistry &content = table().content();
    const bool door = content.block(block).door;
    const QcBlockPos bottom = door && content.property(block, u"half"_s) == u"top"_s ? pos.offset(0, -1, 0) : pos;
    const bool powered = m_context.power.input(bottom) > 0 || (door && m_context.power.input(bottom.offset(0, 1, 0)) > 0);

    QSet<QcBlockPos> &poweredOpeners = m_context.memory.poweredOpeners;
    if (powered == poweredOpeners.contains(bottom)) {
        return;
    }
    if (powered) {
        poweredOpeners.insert(bottom);
    } else {
        poweredOpeners.remove(bottom);
    }
    if (state.on != powered) {
        plan.openings.append({bottom, powered});
    }
}

bool QcCircuitComponents::pistonPowered(QcBlockPos pos, QcSignalState state) const
{
    // Not through its front, where the head goes.
    const quint8 front = state.facing ? faceBit(*state.facing) : 0;
    if (m_context.power.input(pos, front) > 0) {
        return true;
    }

    const std::optional<QcBlockId> block = m_context.power.blockAt(pos);
    const bool quasi = m_context.quasiConnectivity && block && definition(*block).quasiConnectivity;
    return quasi && m_context.power.input(pos.offset(0, 1, 0), faceBit(QcBlockFace::Down)) > 0;
}

void QcCircuitComponents::reactPiston(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    if (!state.facing) {
        return;
    }

    const quint8 acts = m_context.pistonActs.value(pos, 0);
    const bool powered = pistonPowered(pos, state);
    if (powered && !state.on && (acts & extendedThisTick) == 0) {
        if (std::optional<QcPistonPlan> extension = QcPistonPlanner::extension(m_context.power, pos, state, definition(block))) {
            plan.work += extension->moved.size();
            plan.pistons.append(std::move(*extension));
        } else {
            // Blocked (too many blocks, an immovable one, an unloaded chunk): it looks again next tick.
            plan.schedules.append({pos, 1, Priority::Diode});
        }
        return;
    }

    if (!powered && state.on && (acts & retractedThisTick) == 0) {
        // Retracting before the extension ended (a pulse of less than its duration): a sticky piston
        // lets go of its block.
        const auto started = m_context.memory.extending.constFind(pos);
        const bool letGo = started != m_context.memory.extending.constEnd() && m_context.now < *started + QcMovingBlocks::moveTicks;
        plan.pistons.append(QcPistonPlanner::retraction(m_context.power, pos, state, definition(block), letGo));
        return;
    }

    // An extended piston whose head is gone (broken, or the chunk was edited) is retracted.
    const QcBlockPos headPos = qcNeighbour(pos, *state.facing);
    const QcSignalState &head = m_context.power.at(headPos);
    if (state.on && m_context.power.blockAt(headPos) && (head.kind != QcSignalKind::PistonHead || head.facing != state.facing)) {
        plan.writes.insert(pos, table().withFlag(block, u"extended"_s, false));
    }
}

void QcCircuitComponents::reactHead(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    if (!state.facing) {
        return;
    }

    // A head belongs to the extended piston behind it; alone it vanishes.
    const QcBlockPos basePos = qcNeighbour(pos, QcBlockRules::opposite(*state.facing));
    const QcSignalState &base = m_context.power.at(basePos);
    const bool held = base.kind == QcSignalKind::Piston && base.on && base.facing == state.facing;
    if (!held && m_context.power.blockAt(basePos)) {
        plan.writes.insert(pos, qcAirBlockId);
    }
}

void QcCircuitComponents::releasePlate(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const QSet<QcBlockPos> &pressed = definition(block).detectsItems ? m_context.pressedByAnything : m_context.pressedByLiving;
    if (pressed.contains(pos)) {
        plan.schedules.append({pos, plateCheckTicks, Priority::Release});
    } else if (state.on) {
        plan.writes.insert(pos, table().withFlag(block, u"powered"_s, false));
    }
}

#include "server/signal/QcCircuitComponents.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/blocks/QcBlockRules.h"
#include "server/signal/QcMovingBlocks.h"
#include "server/signal/QcTripwires.h"

#include <algorithm>
#include <utility>

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

const QSet<QcBlockPos> &QcCircuitComponents::Plates::pressing(const QcSignalDefinition &signal) const
{
    return signal.detectsCarts ? carts : signal.detectsItems ? anything : living;
}

void QcCircuitComponents::Plates::clear()
{
    anything.clear();
    living.clear();
    carts.clear();
    wires.clear();
}

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
    plan.addWork(1);
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
    case QcSignalKind::Comparator:
        reactComparator(pos, state, plan);
        break;
    case QcSignalKind::Observer:
        reactObserver(pos, state, plan);
        break;
    case QcSignalKind::Hopper:
        reactHopper(pos, *block, state, plan);
        break;
    case QcSignalKind::Dispenser:
        reactDispenser(pos, *block, state, plan);
        break;
    case QcSignalKind::Rail:
        reactRail(pos, *block, plan);
        break;
    case QcSignalKind::Hook:
        QcTripwires(m_context.power, m_context.plates.wires, m_context.entities.schedule()).reactHook(pos, plan);
        break;
    case QcSignalKind::Tripwire:
        QcTripwires(m_context.power, m_context.plates.wires, m_context.entities.schedule()).reactWire(pos, plan);
        break;
    case QcSignalKind::Note:
        reactNote(pos, *block, state, plan);
        break;
    case QcSignalKind::Igniter:
        // An explosive lights as soon as it gets power (decision E-601).
        if (m_context.power.input(pos) > 0) {
            plan.ignite(pos);
        }
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
    plan.addWork(1);
    switch (state.kind) {
    case QcSignalKind::Torch:
        switchTorch(pos, *block, state, plan);
        break;
    case QcSignalKind::Repeater:
        switchRepeater(pos, *block, state, plan);
        break;
    case QcSignalKind::Lamp:
        if (state.on && m_context.power.input(pos) == 0) {
            plan.write(pos, table().counterpartOf(*block));
        }
        break;
    case QcSignalKind::Button:
        if (state.on) {
            plan.write(pos, table().withFlag(*block, u"powered"_s, false));
        }
        break;
    case QcSignalKind::Target:
        if (state.on) {
            plan.write(pos, table().content().withProperty(*block, u"power"_s, u"0"_s));
        }
        break;
    case QcSignalKind::Plate:
        releasePlate(pos, *block, state, plan);
        break;
    case QcSignalKind::Hook:
        QcTripwires(m_context.power, m_context.plates.wires, m_context.entities.schedule()).reactHook(pos, plan);
        break;
    case QcSignalKind::Tripwire:
        QcTripwires(m_context.power, m_context.plates.wires, m_context.entities.schedule()).scheduledWire(pos, *block, plan);
        break;
    case QcSignalKind::Piston:
        reactPiston(pos, *block, state, plan);
        break;
    case QcSignalKind::Opener:
        reactOpener(pos, *block, state, plan);
        break;
    default:
        scheduledMachine(pos, *block, state, plan);
        break;
    }
}

void QcCircuitComponents::scheduledMachine(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    switch (state.kind) {
    case QcSignalKind::Comparator:
        switchComparator(pos, block, state, plan);
        break;
    case QcSignalKind::Observer:
        switchObserver(pos, block, state, plan);
        break;
    case QcSignalKind::Dispenser:
        plan.fire(pos);
        break;
    default:
        break;
    }
}

void QcCircuitComponents::replaced(QcBlockPos pos, QcBlockId before, QcBlockId after, QcCircuitPlan &plan) const
{
    const QcSignalKind was = table().at(before).kind;
    if ((was == QcSignalKind::Hook || was == QcSignalKind::Tripwire) && table().at(after).kind != was) {
        QcTripwires(m_context.power, m_context.plates.wires, m_context.entities.schedule()).removed(pos, before, plan);
    }
}

void QcCircuitComponents::observed(QcBlockPos pos, QcCircuitPlan &plan) const
{
    plan.addWork(1);
    observedChange(pos, plan);
}

bool QcCircuitComponents::torchShouldLight(QcBlockPos pos, QcSignalState state) const
{
    const QcBlockEntities::Memory *memory = m_context.entities.memory(pos);
    const bool burnt = memory && memory->burntOutUntil > m_context.now;
    const bool powered = state.facing && m_context.power.weakLevel(qcNeighbour(pos, *state.facing)) > 0;
    return !burnt && !powered;
}

void QcCircuitComponents::reactTorch(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    if (torchShouldLight(pos, state) != state.on) {
        plan.schedule(pos, torchDelayTicks, Priority::Diode);
    }
}

void QcCircuitComponents::switchTorch(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan)
{
    QcBlockEntities &entities = m_context.entities;
    if (const QcBlockEntities::Memory *ended = entities.memory(pos); ended && ended->burntOutUntil != 0 && ended->burntOutUntil <= m_context.now) {
        entities.memoryAt(pos).burntOutUntil = 0;
    }

    const bool light = torchShouldLight(pos, state);
    if (light == state.on) {
        return;
    }
    plan.write(pos, table().counterpartOf(block));
    if (light) {
        return;
    }

    // Going out: too often within the window burns the torch out.
    QcBlockEntities::Memory &memory = entities.memoryAt(pos);
    QList<quint64> &offTicks = memory.torchOffTicks;
    offTicks.removeIf([this](quint64 tick) { return tick + burnoutWindowTicks <= m_context.now; });
    offTicks.append(m_context.now);
    if (offTicks.size() >= burnoutToggles) {
        memory.torchOffTicks.clear();
        memory.burntOutUntil = m_context.now + burnoutTicks;
        plan.schedule(pos, burnoutTicks, Priority::Release);
        plan.burnOut(pos);
    }
}

bool QcCircuitComponents::repeaterLocked(QcBlockPos pos, QcSignalState state) const
{
    if (!state.facing) {
        return false;
    }

    for (const QcBlockFace side : {leftOf(*state.facing), QcBlockRules::opposite(leftOf(*state.facing))}) {
        const QcSignalState &neighbour = m_context.power.at(qcNeighbour(pos, side));
        // Powered repeaters and comparators pointing at the side lock it (as in the reference).
        const bool diode = neighbour.kind == QcSignalKind::Repeater || neighbour.kind == QcSignalKind::Comparator;
        if (diode && neighbour.power > 0 && neighbour.facing == QcBlockRules::opposite(side)) {
            return true;
        }
    }
    return false;
}

void QcCircuitComponents::reactRepeater(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool locked = repeaterLocked(pos, state);
    if (locked != state.locked) {
        plan.write(pos, table().withFlag(block, u"locked"_s, locked));
    }
    if (locked || !state.facing) {
        return;
    }

    const bool input = m_context.power.inputFrom(pos, QcBlockRules::opposite(*state.facing)) > 0;
    if (input != state.on && !m_context.entities.schedule().contains(pos)) {
        plan.schedule(pos, state.delay * repeaterStepTicks, Priority::Diode);
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
        plan.write(pos, table().withFlag(block, u"powered"_s, false));
    } else if (!state.on) {
        plan.write(pos, table().withFlag(block, u"powered"_s, true));
        if (!input) {
            plan.schedule(pos, state.delay * repeaterStepTicks, Priority::Diode);
        }
    }
}

void QcCircuitComponents::reactLamp(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = m_context.power.input(pos) > 0;
    if (powered && !state.on) {
        plan.write(pos, table().counterpartOf(block));
    } else if (!powered && state.on) {
        const int delay = definition(block).offDelayTicks;
        if (delay > 0) {
            plan.schedule(pos, delay, Priority::Release);
        } else {
            plan.write(pos, table().counterpartOf(block));
        }
    }
}

void QcCircuitComponents::reactNote(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    // The state `powered` remembers the last power, so only a rise plays (as in the reference).
    const bool powered = m_context.power.input(pos) > 0;
    if (powered == state.on) {
        return;
    }
    plan.write(pos, table().withFlag(block, u"powered"_s, powered));
    if (powered) {
        plan.playNote(pos);
    }
}

void QcCircuitComponents::reactOpener(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan)
{
    const QcContentRegistry &content = table().content();
    const bool door = content.block(block).door;
    const QcBlockPos bottom = door && content.property(block, u"half"_s) == u"top"_s ? pos.offset(0, -1, 0) : pos;
    const bool powered = m_context.power.input(bottom) > 0 || (door && m_context.power.input(bottom.offset(0, 1, 0)) > 0);

    const QcBlockEntities::Memory *memory = m_context.entities.memory(bottom);
    if (powered == (memory && memory->powered)) {
        return;
    }
    m_context.entities.memoryAt(bottom).powered = powered;
    if (state.on != powered) {
        plan.open(bottom, powered);
    }
}

bool QcCircuitComponents::pistonPowered(QcBlockPos pos, QcSignalState state) const
{
    // Not through its front, where the head goes.
    return poweredOrQuasi(pos, state.facing ? faceBit(*state.facing) : 0);
}

bool QcCircuitComponents::poweredOrQuasi(QcBlockPos pos, quint8 ignoredFaces) const
{
    if (m_context.power.input(pos, ignoredFaces) > 0) {
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
            plan.addWork(extension->moved.size());
            plan.movePiston(std::move(*extension));
        } else {
            // Blocked (too many blocks, an immovable one, an unloaded chunk): it looks again next tick.
            plan.schedule(pos, 1, Priority::Diode);
        }
        return;
    }

    if (!powered && state.on && (acts & retractedThisTick) == 0) {
        // Retracting before the extension ended (a pulse of less than its duration): a sticky piston
        // lets go of its block.
        const QcBlockEntities::Memory *memory = m_context.entities.memory(pos);
        const bool letGo = memory && memory->extendingSince && m_context.now < *memory->extendingSince + QcMovingBlocks::moveTicks;
        plan.movePiston(QcPistonPlanner::retraction(m_context.power, pos, state, definition(block), letGo));
        return;
    }

    // An extended piston whose head is gone (broken, or the chunk was edited) is retracted.
    const QcBlockPos headPos = qcNeighbour(pos, *state.facing);
    const QcSignalState &head = m_context.power.at(headPos);
    if (state.on && m_context.power.blockAt(headPos) && (head.kind != QcSignalKind::PistonHead || head.facing != state.facing)) {
        plan.write(pos, table().withFlag(block, u"extended"_s, false));
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
        plan.write(pos, qcAirBlockId);
    }
}

void QcCircuitComponents::releasePlate(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    if (m_context.plates.pressing(definition(block)).contains(pos)) {
        plan.schedule(pos, plateCheckTicks, Priority::Release);
    } else if (state.on) {
        plan.write(pos, table().withFlag(block, u"powered"_s, false));
    }
}

int QcCircuitComponents::backInput(QcBlockPos pos, QcBlockFace back) const
{
    const int power = m_context.power.inputFrom(pos, back);
    const QcBlockPos behind = qcNeighbour(pos, back);
    if (const std::optional<int> reading = m_context.readings.at(behind)) {
        return *reading;
    }

    // Through one conductor, as in the reference: only when it does not already get full power.
    // There a frame counts that hangs on the far side of that conductor, facing away (decision
    // E-792; as in the reference, never a frame without a block between); the higher of
    // the frame and the block beyond wins, and an empty frame reads 0.
    if (power < QcSignalDefinition::maxPower && m_context.power.at(behind).conductor) {
        const QcBlockPos beyond = qcNeighbour(behind, back);
        const std::optional<int> block = m_context.readings.at(beyond);
        const std::optional<int> frame = m_context.readings.frameAt(beyond, back);
        if (block || frame) {
            return std::max(block.value_or(0), frame.value_or(0));
        }
    }
    return power;
}

int QcCircuitComponents::sideInput(QcBlockPos pos, QcBlockFace side) const
{
    const QcSignalState &neighbour = m_context.power.at(qcNeighbour(pos, side));
    const QcBlockFace towards = QcBlockRules::opposite(side);
    switch (neighbour.kind) {
    case QcSignalKind::Dust:
        return neighbour.power;
    case QcSignalKind::Repeater:
    case QcSignalKind::Comparator:
        return neighbour.facing == towards ? neighbour.power : 0;
    default:
        return QcSignalPower::strongPower(neighbour, towards);
    }
}

int QcCircuitComponents::comparatorOutput(QcBlockPos pos, QcSignalState state) const
{
    if (!state.facing) {
        return 0;
    }

    const int back = backInput(pos, QcBlockRules::opposite(*state.facing));
    const QcBlockFace left = leftOf(*state.facing);
    const int side = std::max(sideInput(pos, left), sideInput(pos, QcBlockRules::opposite(left)));
    if (state.subtract) {
        return std::max(0, back - side);
    }
    return back >= side ? back : 0;
}

void QcCircuitComponents::reactComparator(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    if (!std::cmp_equal(comparatorOutput(pos, state), state.power) && !m_context.entities.schedule().contains(pos)) {
        plan.schedule(pos, comparatorDelayTicks, Priority::Diode);
    }
}

void QcCircuitComponents::switchComparator(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const int output = comparatorOutput(pos, state);
    if (std::cmp_equal(output, state.power)) {
        return;
    }

    const QcContentRegistry &content = table().content();
    const QcBlockId powered = table().withFlag(block, u"powered"_s, output > 0);
    plan.write(pos, content.withProperty(powered, u"power"_s, QString::number(output)));
}

void QcCircuitComponents::reactObserver(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    // A pulse never stays on (a world loaded in the middle of one, a moved observer).
    if (state.on && !m_context.entities.schedule().contains(pos)) {
        plan.schedule(pos, observerPulseTicks, Priority::Diode);
    }
}

void QcCircuitComponents::observedChange(QcBlockPos pos, QcCircuitPlan &plan) const
{
    if (!m_context.entities.schedule().contains(pos)) {
        plan.schedule(pos, observerPulseTicks, Priority::Diode);
    }
}

void QcCircuitComponents::switchObserver(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    plan.write(pos, table().withFlag(block, u"powered"_s, !state.on));
    if (!state.on) {
        plan.schedule(pos, observerPulseTicks, Priority::Diode);
    }
}

void QcCircuitComponents::reactHopper(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = m_context.power.input(pos) > 0;
    if (powered != state.locked) {
        plan.write(pos, table().withFlag(block, u"enabled"_s, !powered));
    }
}

void QcCircuitComponents::reactDispenser(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = poweredOrQuasi(pos, 0);
    if (powered && !state.on) {
        plan.write(pos, table().withFlag(block, u"triggered"_s, true));
        plan.schedule(pos, dispenserDelayTicks, Priority::Diode);
    } else if (!powered && state.on) {
        plan.write(pos, table().withFlag(block, u"triggered"_s, false));
    }
}

void QcCircuitComponents::reactRail(QcBlockPos pos, QcBlockId block, QcCircuitPlan &plan) const
{
    // A rail is powered while a rail of its line within its reach (itself included) gets power from
    // outside. Only that power counts, never the `powered` state of the rails, so a line never keeps
    // itself on. The rails within reach of this one may change with the power here, so all of them
    // are decided now from the line twice as long (decision E-384).
    const int reach = definition(block).railReach;
    const QcRails::Line line = QcRails::lineOf(m_context.power.world(), m_context.rails, pos, 2 * reach);
    const qsizetype count = line.cells.size();
    plan.addWork(count);
    QList<int> fed(count + 1);
    for (qsizetype i = 0; i < count; ++i) {
        fed[i + 1] = fed[i] + (m_context.power.input(line.cells[i]) > 0 ? 1 : 0);
    }

    const qsizetype first = std::max<qsizetype>(0, line.index - reach);
    const qsizetype last = std::min<qsizetype>(count - 1, line.index + reach);
    for (qsizetype i = first; i <= last; ++i) {
        const qsizetype from = std::max<qsizetype>(0, i - reach);
        const qsizetype to = std::min<qsizetype>(count - 1, i + reach);
        const bool powered = fed[to + 1] - fed[from] > 0;
        const QcBlockId rail = m_context.power.blockAt(line.cells[i]).value_or(qcAirBlockId);
        if (table().at(rail).on != powered) {
            plan.write(line.cells[i], table().withFlag(rail, u"powered"_s, powered));
        }
    }
}

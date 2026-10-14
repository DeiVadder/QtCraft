#include "QcCircuitMachines.h"

#include "QcBlockRules.h"
#include "QcContentRegistry.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

using Priority = QcSignalSchedule::Priority;

namespace {

// The side to the left of something pointing at `face`, seen from above.
QcBlockFace leftOf(QcBlockFace face)
{
    const int index = QcSignalTable::horizontalIndex(face);
    return index < 0 ? face : QcSignalTable::horizontal[std::size_t((index + 3) % 4)];
}

} // namespace

QcCircuitMachines::QcCircuitMachines(const QcCircuitComponents &components, const QcCircuitComponents::Context &context)
    : m_components(components)
    , m_context(context)
{
}

int QcCircuitMachines::backInput(QcBlockPos pos, QcBlockFace back) const
{
    const int power = m_context.power.inputFrom(pos, back);
    const QcBlockPos behind = qcNeighbour(pos, back);
    if (const std::optional<int> reading = m_context.readings.at(behind)) {
        return *reading;
    }

    // Through one conductor, as in the reference: only when it does not already get full power.
    if (power < QcSignalDefinition::maxPower && m_context.power.at(behind).conductor) {
        if (const std::optional<int> reading = m_context.readings.at(qcNeighbour(behind, back))) {
            return *reading;
        }
    }
    return power;
}

int QcCircuitMachines::sideInput(QcBlockPos pos, QcBlockFace side) const
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

int QcCircuitMachines::comparatorOutput(QcBlockPos pos, QcSignalState state) const
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

void QcCircuitMachines::reactComparator(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    if (!std::cmp_equal(comparatorOutput(pos, state), state.power) && !m_context.entities.schedule().contains(pos)) {
        plan.schedules.append({pos, comparatorDelayTicks, Priority::Diode});
    }
}

void QcCircuitMachines::switchComparator(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const int output = comparatorOutput(pos, state);
    if (std::cmp_equal(output, state.power)) {
        return;
    }

    const QcContentRegistry &content = table().content();
    const QcBlockId powered = table().withFlag(block, u"powered"_s, output > 0);
    plan.writes.insert(pos, content.withProperty(powered, u"power"_s, QString::number(output)));
}

void QcCircuitMachines::reactObserver(QcBlockPos pos, QcSignalState state, QcCircuitPlan &plan) const
{
    // A pulse never stays on (a world loaded in the middle of one, a moved observer).
    if (state.on && !m_context.entities.schedule().contains(pos)) {
        plan.schedules.append({pos, observerPulseTicks, Priority::Diode});
    }
}

void QcCircuitMachines::observed(QcBlockPos pos, QcCircuitPlan &plan) const
{
    if (!m_context.entities.schedule().contains(pos)) {
        plan.schedules.append({pos, observerPulseTicks, Priority::Diode});
    }
}

void QcCircuitMachines::switchObserver(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    plan.writes.insert(pos, table().withFlag(block, u"powered"_s, !state.on));
    if (!state.on) {
        plan.schedules.append({pos, observerPulseTicks, Priority::Diode});
    }
}

void QcCircuitMachines::reactHopper(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = m_context.power.input(pos) > 0;
    if (powered != state.locked) {
        plan.writes.insert(pos, table().withFlag(block, u"enabled"_s, !powered));
    }
}

void QcCircuitMachines::reactDispenser(QcBlockPos pos, QcBlockId block, QcSignalState state, QcCircuitPlan &plan) const
{
    const bool powered = m_components.poweredOrQuasi(pos, 0);
    if (powered && !state.on) {
        plan.writes.insert(pos, table().withFlag(block, u"triggered"_s, true));
        plan.schedules.append({pos, dispenserDelayTicks, Priority::Diode});
    } else if (!powered && state.on) {
        plan.writes.insert(pos, table().withFlag(block, u"triggered"_s, false));
    }
}

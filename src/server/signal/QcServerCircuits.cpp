#include "QcServerCircuits.h"

#include "QcBlockRules.h"
#include "QcCircuitComponents.h"
#include "QcDustNetwork.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// A fixed order of positions, so that blocks are set and sent in the same order in every run (the
// result of a step does not depend on it).
bool before(QcBlockPos a, QcBlockPos b)
{
    return std::tuple{a.y, a.z, a.x} < std::tuple{b.y, b.z, b.x};
}

void sortUnique(std::vector<QcBlockPos> &positions)
{
    std::ranges::sort(positions, before);
    const auto [first, last] = std::ranges::unique(positions);
    positions.erase(first, last);
}

// The 27 cells within one block of `pos`.
void addCube(QcBlockPos pos, std::vector<QcBlockPos> &cells)
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                cells.push_back(pos.offset(dx, dy, dz));
            }
        }
    }
}

QcVec3 centreOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

} // namespace

QcServerCircuits::QcServerCircuits(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_table(*state.content)
    , m_mover(state, blocks, sounds, m_table)
{
}

void QcServerCircuits::tick()
{
    m_budget = maxUpdatesPerTick;
    m_pistonActs.clear();
    if (m_state.tickCount % pruneIntervalTicks == 0) {
        m_state.circuits.prune(m_state.tickCount, QcCircuitComponents::burnoutWindowTicks);
    }
    finishMoves();
    pressPlates();
    // Changes may not take the whole budget: due scheduled ticks always get their share.
    settle(scheduleReserve);

    while (m_budget > 0) {
        const auto group = m_state.circuits.schedule.nextGroup(m_state.tickCount);
        if (!group) {
            break;
        }
        runScheduled(group->first, group->second);
        settle(0);
    }
    m_lastWork = maxUpdatesPerTick - m_budget;
}

void QcServerCircuits::finishMoves()
{
    const QList<QcBlockPos> arrived = m_state.movingBlocks.tick(m_state.tickCount);
    for (const QcBlockPos &cell : arrived) {
        if (const std::optional<QcBlockId> block = m_state.world.blockAt(cell)) {
            m_blocks.sendBlock(cell, *block);
        }
    }
}

void QcServerCircuits::noteBlock(QcBlockPos pos)
{
    const QcBlockId block = m_state.world.blockAt(pos).value_or(qcAirBlockId);
    m_state.circuits.noteChange(m_state.world, pos, qcAirBlockId, block);
}

bool QcServerCircuits::use(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    if (!block) {
        return false;
    }

    const QcSignalState &state = m_table.at(*block);
    const QcSignalDefinition &definition = m_table.content().block(*block).signal;
    switch (state.kind) {
    case QcSignalKind::Lever:
        m_blocks.setAndBroadcast(pos, m_table.withFlag(*block, u"powered"_s, !state.on));
        break;
    case QcSignalKind::Button:
        // A pressed button does not click again.
        if (state.on) {
            return true;
        }
        m_blocks.setAndBroadcast(pos, m_table.withFlag(*block, u"powered"_s, true));
        m_state.circuits.schedule.add(pos, m_state.tickCount + quint64(definition.pulseTicks), QcSignalSchedule::Priority::Release);
        break;
    case QcSignalKind::Repeater: {
        const QString delay = QString::number(state.delay % 4 + 1);
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"delay"_s, delay));
        break;
    }
    default:
        return false;
    }

    m_sounds.play(QString::fromLatin1(QcSoundKeys::signalClick), centreOf(pos));
    return true;
}

void QcServerCircuits::pressPlates()
{
    m_pressedByAnything.clear();
    m_pressedByLiving.clear();
    const QcSectionMarks &plates = m_state.world.plateSections();
    if (plates.isEmpty()) {
        return;
    }

    const auto note = [this, &plates](const QcVec3 &feet, bool living) {
        --m_budget;
        const QcBlockPos cell = QcPhysics::blockContaining(feet);
        if (!cell.isInsideWorldHeight() || (plates.sectionsOf(cell.chunk()) & (1U << unsigned(cell.y / QcWorld::sectionHeight))) == 0) {
            return;
        }
        const std::optional<QcBlockId> block = m_state.world.blockAt(cell);
        if (block && m_table.at(*block).kind == QcSignalKind::Plate) {
            m_pressedByAnything.insert(cell);
            if (living) {
                m_pressedByLiving.insert(cell);
            }
        }
    };

    for (const auto &client : m_state.clients) {
        if (client->state == QcServerClient::State::Playing && !client->dead) {
            note(client->motion.position, true);
        }
    }
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        note(mob.motion.position, true);
    }
    for (const QcItemEntityManager::Item &item : m_state.items.items()) {
        note(item.motion.position, false);
    }
    for (const QcArmourStandManager::Stand &stand : m_state.stands.stands()) {
        note(stand.position, false);
    }

    QcCircuitPlan plan;
    for (const QcBlockPos &cell : std::as_const(m_pressedByAnything)) {
        const QcBlockId block = m_state.world.blockAt(cell).value_or(qcAirBlockId);
        const bool counts = m_table.content().block(block).signal.detectsItems || m_pressedByLiving.contains(cell);
        if (counts && !m_table.at(block).on) {
            plan.writes.insert(cell, m_table.withFlag(block, u"powered"_s, true));
            plan.schedules.append({cell, QcCircuitComponents::plateCheckTicks, QcSignalSchedule::Priority::Release});
        }
    }
    apply(plan);
}

void QcServerCircuits::settle(qsizetype reserve)
{
    while (!m_state.circuits.changes.isEmpty() && m_budget > reserve) {
        runWave(m_budget - reserve);
    }
}

void QcServerCircuits::collect(const QList<QcCircuitState::Change> &changes, std::vector<QcBlockPos> &components,
                               std::vector<QcBlockPos> &dust) const
{
    for (const QcCircuitState::Change &change : changes) {
        components.push_back(change.pos);
        if (change.selfOnly) {
            dust.push_back(change.pos);
            continue;
        }
        collectAround(change, components, dust);
    }

    sortUnique(components);
    sortUnique(dust);
}

void QcServerCircuits::collectAround(const QcCircuitState::Change &change, std::vector<QcBlockPos> &components,
                                     std::vector<QcBlockPos> &dust) const
{
    for (const QcBlockFace face : qcAllBlockFaces) {
        components.push_back(qcNeighbour(change.pos, face));
    }

    // A new strength of dust changes no other wire: dust takes no power from blocks dust powers, and
    // connections only depend on where dust lies. Everything else lets the dust within one block
    // reconnect (slopes, cut lines), and the neighbours of what the block powered or now powers
    // take note.
    const bool strengthOnly = m_table.at(change.before).kind == QcSignalKind::Dust && m_table.at(change.after).kind == QcSignalKind::Dust;
    if (!strengthOnly) {
        addCube(change.pos, dust);
    }

    QcSignalPower::Positions targets = QcSignalPower::targets(change.pos, m_table.at(change.before));
    const QcSignalPower::Positions after = QcSignalPower::targets(change.pos, m_table.at(change.after));
    targets.append(after.constData(), after.size());
    for (const QcBlockPos &target : std::as_const(targets)) {
        for (const QcBlockFace face : qcAllBlockFaces) {
            components.push_back(qcNeighbour(target, face));
            if (!strengthOnly) {
                dust.push_back(qcNeighbour(target, face));
            }
        }
    }
}

QList<QcCircuitState::Change> QcServerCircuits::takeChanges(qsizetype limit)
{
    QList<QcCircuitState::Change> changes = std::exchange(m_state.circuits.changes, {});
    const qsizetype room = std::max(limit / workPerChange, minimumStep);
    if (changes.size() > room) {
        // Too many for one step: the first ones in a fixed order of positions now, the rest in the
        // next step. Such a split step is no longer simultaneous (decision E-335).
        std::ranges::stable_sort(changes, [](const QcCircuitState::Change &a, const QcCircuitState::Change &b) { return before(a.pos, b.pos); });
        m_state.circuits.changes = changes.mid(room);
        changes.resize(room);
    }

    // Changes next to a chunk that is not loaded wait for it instead of reading it as air.
    const QcSignalPower power(m_state.world, m_table);
    changes.removeIf([&](const QcCircuitState::Change &change) {
        const std::optional<QcChunkPos> missing = power.unloadedNear(change.pos);
        if (missing) {
            m_state.circuits.waitOn(*missing, change.pos);
        }
        return missing.has_value();
    });
    return changes;
}

void QcServerCircuits::runWave(qsizetype limit)
{
    const QList<QcCircuitState::Change> changes = takeChanges(limit);
    std::vector<QcBlockPos> components;
    std::vector<QcBlockPos> dust;
    collect(changes, components, dust);

    const QcSignalPower power(m_state.world, m_table);
    const QcDustNetwork network(power);
    QcDustNetwork::Result wired;
    network.recompute(dust, wired);

    QcCircuitPlan plan;
    plan.work = changes.size() + wired.visited;
    if (wired.blockedBy) {
        // The wire reaches a chunk that is not loaded: it is recomputed once that chunk is.
        for (const QcBlockPos &pos : dust) {
            if (m_table.at(m_state.world.blockAt(pos).value_or(qcAirBlockId)).kind == QcSignalKind::Dust) {
                m_state.circuits.waitOn(*wired.blockedBy, pos);
            }
        }
    }
    for (const QcDustNetwork::Change &change : std::as_const(wired.changes)) {
        plan.writes.insert(change.pos, change.state);
    }

    QcCircuitComponents reactions({power, m_state.circuits, m_state.tickCount, m_state.info.quasiConnectivity, m_pistonActs,
                                   m_pressedByAnything, m_pressedByLiving});
    for (const QcBlockPos &pos : components) {
        if (const std::optional<QcChunkPos> missing = power.unloadedNear(pos)) {
            if (m_table.at(m_state.world.blockAt(pos).value_or(qcAirBlockId)).kind != QcSignalKind::None) {
                m_state.circuits.waitOn(*missing, pos);
            }
            continue;
        }
        reactions.react(pos, plan);
    }
    apply(plan);
}

void QcServerCircuits::runScheduled(quint64 due, QcSignalSchedule::Priority priority)
{
    QList<QcBlockPos> positions = m_state.circuits.schedule.takeGroup(due, priority);
    std::ranges::sort(positions, before);
    const qsizetype room = std::max(m_budget / workPerChange, minimumStep);
    // A group larger than the budget: the rest stays due with its tick and priority for the next
    // tick (not simultaneous with the first part any more).
    for (qsizetype i = room; i < positions.size(); ++i) {
        m_state.circuits.schedule.add(positions[i], due, priority);
    }
    positions.resize(std::min(positions.size(), room));

    const QcSignalPower power(m_state.world, m_table);
    QcCircuitComponents reactions({power, m_state.circuits, m_state.tickCount, m_state.info.quasiConnectivity, m_pistonActs,
                                   m_pressedByAnything, m_pressedByLiving});
    QcCircuitPlan plan;
    for (const QcBlockPos &pos : std::as_const(positions)) {
        // Next to an unloaded chunk the component waits for it and then looks again.
        if (const std::optional<QcChunkPos> missing = power.unloadedNear(pos)) {
            m_state.circuits.waitOn(*missing, pos);
            ++plan.work;
            continue;
        }
        reactions.scheduled(pos, plan);
    }
    apply(plan);
}

void QcServerCircuits::apply(QcCircuitPlan &plan)
{
    m_budget -= std::max<qsizetype>(1, plan.work);
    for (const QcCircuitPlan::Schedule &schedule : std::as_const(plan.schedules)) {
        m_state.circuits.schedule.add(schedule.pos, m_state.tickCount + quint64(std::max(1, schedule.delay)), schedule.priority);
    }

    applyWrites(plan);
    applyOpenings(plan);
    m_budget -= applyPistons(plan);

    for (const QcBlockPos &pos : std::as_const(plan.burntOut)) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::torchFizz), centreOf(pos));
    }
}

void QcServerCircuits::applyWrites(const QcCircuitPlan &plan)
{
    std::vector<QcBlockPos> cells;
    cells.reserve(std::size_t(plan.writes.size()));
    for (auto it = plan.writes.constBegin(); it != plan.writes.constEnd(); ++it) {
        cells.push_back(it.key());
    }
    std::ranges::sort(cells, before);

    for (const QcBlockPos &cell : cells) {
        const QcBlockId block = plan.writes.value(cell);
        if (m_state.world.blockAt(cell) != block) {
            m_blocks.setAndBroadcast(cell, block);
        }
    }
}

void QcServerCircuits::applyOpenings(const QcCircuitPlan &plan)
{
    for (const QcCircuitPlan::Opening &opening : std::as_const(plan.openings)) {
        const std::optional<QcBlockId> block = m_state.world.blockAt(opening.pos);
        if (!block || m_table.at(*block).kind != QcSignalKind::Opener || m_table.at(*block).on == opening.open) {
            continue;
        }
        m_blocks.toggle(opening.pos, *block);
        if (m_state.world.blockAt(opening.pos) == block) {
            // Someone stands in the way: the opener has not seen the change yet and tries again.
            QSet<QcBlockPos> &powered = m_state.circuits.poweredOpeners;
            if (opening.open) {
                powered.remove(opening.pos);
            } else {
                powered.insert(opening.pos);
            }
            m_state.circuits.schedule.add(opening.pos, m_state.tickCount + openerRetryTicks, QcSignalSchedule::Priority::Release);
        }
    }
}

qsizetype QcServerCircuits::applyPistons(const QcCircuitPlan &plan)
{
    const QList<QcPistonPlan> kept = QcPistonPlanner::resolveConflicts(plan.pistons);
    // Pistons that lost a conflict look again next tick, all of them, so none wins by order.
    for (const QcPistonPlan &dropped : plan.pistons) {
        if (!std::ranges::any_of(kept, [&dropped](const QcPistonPlan &piston) { return piston.base == dropped.base; })) {
            m_state.circuits.schedule.add(dropped.base, m_state.tickCount + 1, QcSignalSchedule::Priority::Diode);
        }
    }

    QList<QcPistonPlan> pistons = kept;
    std::ranges::sort(pistons, [](const QcPistonPlan &a, const QcPistonPlan &b) { return before(a.base, b.base); });
    qsizetype work = 0;
    for (const QcPistonPlan &piston : std::as_const(pistons)) {
        work += m_mover.apply(piston);
        m_pistonActs[piston.base] |= piston.extend ? QcCircuitComponents::extendedThisTick : QcCircuitComponents::retractedThisTick;
    }
    m_mover.applyPushes();
    return work;
}

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
    , m_dispensers(state, blocks, sounds, m_table)
    , m_hoppers(state, m_table)
{
}

void QcServerCircuits::tick()
{
    const QcServerLimits &limits = m_state.config.limits;
    m_budget = qsizetype(limits.signalBudget);
    m_pistonActs.clear();
    if (m_state.tickCount % pruneIntervalTicks == 0) {
        m_state.blockEntities().prune(m_state.tickCount, QcCircuitComponents::burnoutWindowTicks, QcMovingBlocks::moveTicks);
    }
    finishMoves();
    m_state.circuits.takeReaders(m_state.world);
    pressPlates();
    // Changes may not take the whole budget: due scheduled ticks always get their share.
    settle(qsizetype(limits.scheduleReserve()));

    while (m_budget > 0) {
        const auto group = m_state.blockEntities().schedule().nextGroup(m_state.tickCount);
        if (!group) {
            break;
        }
        runScheduled(group->first, group->second);
        settle(0);
    }
    m_lastWork = qsizetype(limits.signalBudget) - m_budget;
    // Hoppers have their own bound (QcServerLimits::hopperBudget).
    m_hoppers.tick();
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
        m_state.blockEntities().schedule().add(pos, m_state.tickCount + quint64(definition.pulseTicks), QcSignalSchedule::Priority::Release);
        break;
    case QcSignalKind::Repeater: {
        const QString delay = QString::number(state.delay % 4 + 1);
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"delay"_s, delay));
        break;
    }
    case QcSignalKind::Comparator:
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"mode"_s, state.subtract ? u"compare"_s : u"subtract"_s));
        break;
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
    const QcSectionMarks &marks = m_state.world.marks();
    if (marks.isEmpty(QcBlockTrait::Plate)) {
        return;
    }

    const auto note = [this, &marks](const QcVec3 &feet, bool living) {
        --m_budget;
        const QcBlockPos cell = QcPhysics::blockContaining(feet);
        if (!cell.isInsideWorldHeight() || (marks.sectionsOf(cell.chunk(), QcBlockTrait::Plate) & (1U << unsigned(cell.y / QcWorld::sectionHeight))) == 0) {
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

void QcServerCircuits::collect(const QList<QcCircuitState::Change> &changes, Collected &collected) const
{
    for (const QcCircuitState::Change &change : changes) {
        collected.components.push_back(change.pos);
        if (change.selfOnly) {
            collected.dust.push_back(change.pos);
            continue;
        }
        collectAround(change, collected.components, collected.dust);
        if (change.before != change.after) {
            collectObservers(change.pos, collected.observers);
            // A block placed or broken two behind a comparator, behind a conductor.
            collectReaders(change.pos, collected.readers);
        }
    }

    sortUnique(collected.components);
    sortUnique(collected.dust);
    sortUnique(collected.readers);
    sortUnique(collected.observers);
}

void QcServerCircuits::collectReaders(QcBlockPos pos, std::vector<QcBlockPos> &readers) const
{
    // A comparator reads the block behind it, or two behind it through a conductor.
    const QList<QcBlockPos> found = QcCircuitState::readersOf(m_state.world, pos);
    readers.insert(readers.end(), found.begin(), found.end());
}

void QcServerCircuits::collectObservers(QcBlockPos pos, std::vector<QcBlockPos> &observers) const
{
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos next = qcNeighbour(pos, face);
        const QcSignalState &state = m_table.at(m_state.world.blockAt(next).value_or(qcAirBlockId));
        if (state.kind == QcSignalKind::Observer && state.facing == QcBlockRules::opposite(face)) {
            observers.push_back(next);
        }
    }
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
    const QcServerLimits &limits = m_state.config.limits;
    const qsizetype room = std::max(limit / qsizetype(limits.signalWorkPerChange), qsizetype(limits.signalMinimumStep));
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
    Collected collected;
    collect(changes, collected);
    const std::vector<QcBlockPos> &components = collected.components;
    const std::vector<QcBlockPos> &dust = collected.dust;

    const QcSignalPower power(m_state.world, m_table);
    const QcDustNetwork network(power);
    QcDustNetwork::Result wired;
    network.recompute(dust, wired);

    QcCircuitPlan plan;
    plan.work = changes.size() + wired.visited;
    // A wire that reaches a chunk that is not loaded is recomputed once that chunk is.
    for (const QcDustNetwork::Result::Blocked &blocked : std::as_const(wired.blocked)) {
        for (const QcBlockPos &pos : blocked.seeds) {
            m_state.circuits.waitOn(blocked.chunk, pos);
        }
    }
    for (const QcDustNetwork::Change &change : std::as_const(wired.changes)) {
        plan.writes.insert(change.pos, change.state);
    }

    const QcComparatorReadings readings(m_state.world, m_state.blockEntities());
    QcCircuitComponents reactions({power, m_state.blockEntities(), m_state.tickCount, m_state.info.quasiConnectivity, m_pistonActs,
                                   m_pressedByAnything, m_pressedByLiving, readings});
    for (const QcBlockPos &pos : components) {
        if (const std::optional<QcChunkPos> missing = power.unloadedNear(pos)) {
            if (m_table.at(m_state.world.blockAt(pos).value_or(qcAirBlockId)).kind != QcSignalKind::None) {
                m_state.circuits.waitOn(*missing, pos);
            }
            continue;
        }
        reactions.react(pos, plan);
    }
    reactReadersAndObservers(collected, reactions, plan);
    apply(plan);
}

void QcServerCircuits::reactReadersAndObservers(const Collected &collected, QcCircuitComponents &reactions, QcCircuitPlan &plan) const
{
    const QcSignalPower power(m_state.world, m_table);
    for (const QcBlockPos &pos : collected.readers) {
        // Already looked at for a block change in this step.
        if (!std::ranges::binary_search(collected.components, pos, before) && !power.unloadedNear(pos)) {
            reactions.react(pos, plan);
        }
    }
    for (const QcBlockPos &pos : collected.observers) {
        if (!power.unloadedNear(pos)) {
            reactions.observed(pos, plan);
        }
    }
}

void QcServerCircuits::runScheduled(quint64 due, QcSignalSchedule::Priority priority)
{
    QList<QcBlockPos> positions = m_state.blockEntities().schedule().takeGroup(due, priority);
    std::ranges::sort(positions, before);
    const QcServerLimits &limits = m_state.config.limits;
    const qsizetype room = std::max(m_budget / qsizetype(limits.signalWorkPerChange), qsizetype(limits.signalMinimumStep));
    // A group larger than the budget: the rest stays due with its tick and priority for the next
    // tick (not simultaneous with the first part any more).
    for (qsizetype i = room; i < positions.size(); ++i) {
        m_state.blockEntities().schedule().add(positions[i], due, priority);
    }
    positions.resize(std::min(positions.size(), room));

    const QcSignalPower power(m_state.world, m_table);
    const QcComparatorReadings readings(m_state.world, m_state.blockEntities());
    QcCircuitComponents reactions({power, m_state.blockEntities(), m_state.tickCount, m_state.info.quasiConnectivity, m_pistonActs,
                                   m_pressedByAnything, m_pressedByLiving, readings});
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
        m_state.blockEntities().schedule().add(schedule.pos, m_state.tickCount + quint64(std::max(1, schedule.delay)), schedule.priority);
    }

    applyWrites(plan);
    applyOpenings(plan);
    m_budget -= applyPistons(plan);
    m_budget -= m_dispensers.fire(plan.fires);

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
            m_state.blockEntities().memoryAt(opening.pos).powered = !opening.open;
            m_state.blockEntities().schedule().add(opening.pos, m_state.tickCount + openerRetryTicks, QcSignalSchedule::Priority::Release);
        }
    }
}

qsizetype QcServerCircuits::applyPistons(const QcCircuitPlan &plan)
{
    const QList<QcPistonPlan> kept = QcPistonPlanner::resolveConflicts(plan.pistons);
    // Pistons that lost a conflict look again next tick, all of them, so none wins by order.
    for (const QcPistonPlan &dropped : plan.pistons) {
        if (!std::ranges::any_of(kept, [&dropped](const QcPistonPlan &piston) { return piston.base == dropped.base; })) {
            m_state.blockEntities().schedule().add(dropped.base, m_state.tickCount + 1, QcSignalSchedule::Priority::Diode);
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

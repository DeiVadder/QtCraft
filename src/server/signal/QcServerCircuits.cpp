// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcServerCircuits.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcOpeners.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/signal/QcCircuitComponents.h"
#include "server/signal/QcDustNetwork.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A fixed order of positions, so that blocks are set and sent in the same order in every run (the
// result of a step does not depend on it).
bool before(QcBlockPos a, QcBlockPos b)
{
    return std::tuple{a.y, a.z, a.x} < std::tuple{b.y, b.z, b.x};
}

// The positions in that order, each once (moved in and out, no copy).
[[nodiscard]] std::vector<QcBlockPos> sortedUnique(std::vector<QcBlockPos> positions)
{
    std::ranges::sort(positions, before);
    const auto [first, last] = std::ranges::unique(positions);
    positions.erase(first, last);
    return positions;
}

// The 27 cells within one block of `pos`.
[[nodiscard]] std::array<QcBlockPos, 27> cubeAround(QcBlockPos pos)
{
    std::array<QcBlockPos, 27> cells{};
    std::size_t next = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                cells[next++] = pos.offset(dx, dy, dz);
            }
        }
    }
    return cells;
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
    , m_table(state.content())
    , m_mover(state, blocks, sounds, m_table)
    , m_dispensers(state, blocks, sounds, m_table)
    , m_hoppers(state, m_table)
    , m_rails(state.content())
{
}

void QcServerCircuits::tick()
{
    const QcServerLimits &limits = m_state.config().limits;
    m_budget = static_cast<qsizetype>(limits.signalBudget);
    m_pistonActs.clear();
    if (m_state.clock().tick() % pruneIntervalTicks == 0) {
        m_state.blockEntities().prune(m_state.clock().tick(), QcCircuitComponents::burnoutWindowTicks, QcMovingBlocks::moveTicks);
    }
    finishMoves();
    m_state.circuits().takeReaders(m_state.world());
    pressPlates();
    // Changes may not take the whole budget: due scheduled ticks always get their share.
    settle(static_cast<qsizetype>(limits.scheduleReserve()));

    while (m_budget > 0) {
        const auto group = m_state.blockEntities().schedule().nextGroup(m_state.clock().tick());
        if (!group) {
            break;
        }
        runScheduled(group->first, group->second);
        settle(0);
    }
    m_lastWork = static_cast<qsizetype>(limits.signalBudget) - m_budget;
    // Hoppers have their own bound (QcServerLimits::hopperBudget).
    m_hoppers.tick();
}

void QcServerCircuits::finishMoves()
{
    const QList<QcBlockPos> arrived = m_state.entities().movingBlocks().tick(m_state.clock().tick());
    for (const QcBlockPos &cell : arrived) {
        if (const std::optional<QcBlockId> block = m_state.world().blockAt(cell)) {
            m_blocks.sendBlock(cell, *block);
        }
    }
}

void QcServerCircuits::noteBlock(QcBlockPos pos)
{
    const QcBlockId block = m_state.world().blockAt(pos).value_or(qcAirBlockId);
    m_state.circuits().noteChange(m_state.world(), pos, qcAirBlockId, block);
}

bool QcServerCircuits::use(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
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
        m_state.blockEntities().schedule().add(pos, m_state.clock().tick() + static_cast<quint64>(definition.pulseTicks), QcSignalSchedule::Priority::Release);
        break;
    case QcSignalKind::Repeater: {
        const QString delay = QString::number(state.delay % 4 + 1);
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"delay"_s, delay));
        break;
    }
    case QcSignalKind::Comparator:
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"mode"_s, state.subtract ? u"compare"_s : u"subtract"_s));
        break;
    case QcSignalKind::Note: {
        const int note = (m_table.content().property(*block, u"note"_s).toInt() + 1) % QcSignalDefinition::noteCount;
        m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"note"_s, QString::number(note)));
        playNote(pos);
        return true;
    }
    default:
        return false;
    }

    m_sounds.play(QString::fromLatin1(QcSoundKeys::signalClick), centreOf(pos));
    return true;
}

bool QcServerCircuits::hitTarget(QcBlockPos pos, int power, bool stuck)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || m_table.at(*block).kind != QcSignalKind::Target) {
        return false;
    }

    const QcSignalDefinition &definition = m_table.content().block(*block).signal;
    const int strength = std::clamp(power, 1, QcSignalDefinition::maxPower);
    m_blocks.setAndBroadcast(pos, m_table.content().withProperty(*block, u"power"_s, QString::number(strength)));
    QcSignalSchedule &schedule = m_state.blockEntities().schedule();
    schedule.remove(pos);
    schedule.add(pos, m_state.clock().tick() + static_cast<quint64>(stuck ? definition.stuckHitTicks : definition.hitTicks),
        QcSignalSchedule::Priority::Release);
    return true;
}

int QcServerCircuits::targetStrength(QcBlockPos pos, QcBlockFace face, const QcVec3 &point)
{
    const std::array<double, 3> local{point.x - pos.x, point.y - pos.y, point.z - pos.z};
    const int normal = face == QcBlockFace::East || face == QcBlockFace::West ? 0 : (face == QcBlockFace::Up || face == QcBlockFace::Down ? 1 : 2);
    double distance = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis != normal) {
            distance = std::max(distance, std::abs(std::clamp(local[static_cast<std::size_t>(axis)], 0.0, 1.0) - 0.5));
        }
    }
    const double share = std::clamp((0.5 - distance) / 0.5, 0.0, 1.0);
    return std::max(1, static_cast<int>(std::ceil(QcSignalDefinition::maxPower * share - 1e-9)));
}

void QcServerCircuits::playNote(QcBlockPos pos)
{
    const QcServerWorld &world = m_state.world();
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || world.blockAt(pos.offset(0, 1, 0)).value_or(qcAirBlockId) != qcAirBlockId) {
        return;
    }
    if (!m_notes.allow(pos, m_state.clock().tick())) {
        return;
    }

    const QcContentRegistry &content = m_table.content();
    const QcBlockId below = world.blockAt(pos.offset(0, -1, 0)).value_or(qcAirBlockId);
    const QcBlockDefinition &ground = content.block(below);
    const QString sound = content.block(*block).signal.instruments.of(ground.name, ground.soundMaterial);
    // Semitones around the middle note: 0.5 (F#3) to 2 (F#5) times the instrument's own pitch.
    const int note = std::clamp(content.property(*block, u"note"_s).toInt(), 0, QcSignalDefinition::noteCount - 1);
    m_sounds.play(sound, centreOf(pos), 1.0f, static_cast<float>(std::exp2(static_cast<double>(note - 12) / 12.0)));
}

void QcServerCircuits::pressPlates()
{
    m_plates.clear();
    QHash<QcBlockPos, quint32> readings;
    const QcSectionMarks &marks = m_state.world().marks();
    if (marks.isEmpty(QcBlockTrait::Plate)) {
        noteCartReadings(readings);
        return;
    }

    const auto note = [this, &marks, &readings](const QcEntity &body) {
        --m_budget;
        const QcBlockPos cell = QcPhysics::blockContaining(body.motion.position);
        if (!cell.isInsideWorldHeight()
            || (marks.sectionsOf(cell.chunk(), QcBlockTrait::Plate)
                & (1U << static_cast<unsigned>(cell.y / QcWorld::sectionHeight))) == 0) {
            return;
        }
        const std::optional<QcBlockId> block = m_state.world().blockAt(cell);
        // Anything touching tripwire sets it off (decision E-782).
        if (block && m_table.at(*block).kind == QcSignalKind::Tripwire) {
            m_plates.wires.insert(cell);
            return;
        }
        if (block && m_table.at(*block).kind == QcSignalKind::Plate) {
            m_plates.anything.insert(cell);
            if (body.kind == QcEntityKind::Player || body.kind == QcEntityKind::Creature) {
                m_plates.living.insert(cell);
            }
            if (body.kind == QcEntityKind::Minecart) {
                m_plates.carts.insert(cell);
            }
            if (body.kind == QcEntityKind::Minecart && !body.as<QcMinecart>().stacks.isEmpty()) {
                const quint32 reading = readings.value(cell, body.id);
                readings.insert(cell, std::min(reading, body.id));
            }
        }
    };

    // Players and creatures press every plate, items, stands and carts those that detect items,
    // only carts a detector rail; falling blocks press none.
    std::as_const(m_state.entities()).forEachBody([&note](const QcEntity &body) {
        if (body.kind != QcEntityKind::FallingBlock) {
            note(body);
        }
    });

    noteCartReadings(std::move(readings));
    QcCircuitPlan plan = pressedPlan();
    apply(plan);
}

QcCircuitPlan QcServerCircuits::pressedPlan() const
{
    QcCircuitPlan plan;
    for (const QcBlockPos &cell : std::as_const(m_plates.anything)) {
        const QcBlockId block = m_state.world().blockAt(cell).value_or(qcAirBlockId);
        if (m_plates.pressing(m_table.content().block(block).signal).contains(cell) && !m_table.at(block).on) {
            plan.write(cell, m_table.withFlag(block, u"powered"_s, true));
            plan.schedule(cell, QcCircuitComponents::plateCheckTicks, QcSignalSchedule::Priority::Release);
        }
    }
    for (const QcBlockPos &cell : std::as_const(m_plates.wires)) {
        const QcBlockId block = m_state.world().blockAt(cell).value_or(qcAirBlockId);
        if (!m_table.at(block).on) {
            plan.write(cell, m_table.withFlag(block, u"powered"_s, true));
            plan.schedule(cell, m_table.content().block(block).signal.pulseTicks, QcSignalSchedule::Priority::Release);
        }
    }
    return plan;
}

void QcServerCircuits::noteCartReadings(QHash<QcBlockPos, quint32> readings)
{
    const auto changed = [this](const QHash<QcBlockPos, quint32> &now, const QHash<QcBlockPos, quint32> &then) {
        for (auto it = now.cbegin(); it != now.cend(); ++it) {
            if (then.value(it.key()) != it.value()) {
                m_state.circuits().noteContents(m_state.world(), it.key());
            }
        }
    };
    changed(readings, m_cartReadings);
    // Cells the last container cart left.
    changed(m_cartReadings, readings);
    m_cartReadings = std::move(readings);
}

QcCircuitComponents::Context QcServerCircuits::componentContext(QcSignalPower power, const QcComparatorReadings &readings) const
{
    return {power, m_state.blockEntities(), m_state.clock().tick(), m_state.info().quasiConnectivity, m_pistonActs, m_plates, readings, m_rails};
}

void QcServerCircuits::settle(qsizetype reserve)
{
    while (!m_state.circuits().changes.isEmpty() && m_budget > reserve) {
        runWave(m_budget - reserve);
    }
}

QcServerCircuits::Collected QcServerCircuits::collect(const QList<QcCircuitState::Change> &changes) const
{
    Collected collected;
    for (const QcCircuitState::Change &change : changes) {
        collected.components.push_back(change.pos);
        if (change.selfOnly) {
            collected.dust.push_back(change.pos);
            continue;
        }
        collected.addAround(change, m_table);
        // Only a component with quasi-connectivity notices the update two out, and only a real change
        // sends it.
        if (m_state.info().quasiConnectivity && change.before != change.after) {
            collected.addTwoOut(change, m_state.world(), m_table);
        }
        if (change.before != change.after) {
            collected.addObservers(change.pos, m_state.world(), m_table);
            // A block placed or broken two behind a comparator, behind a conductor.
            collected.addReaders(change.pos, m_state.world());
        }
    }
    collected.sortUnique();
    return collected;
}

void QcServerCircuits::Collected::sortUnique()
{
    components = sortedUnique(std::move(components));
    dust = sortedUnique(std::move(dust));
    readers = sortedUnique(std::move(readers));
    observers = sortedUnique(std::move(observers));
}

void QcServerCircuits::Collected::addReaders(QcBlockPos pos, const QcServerWorld &world)
{
    const QList<QcBlockPos> found = QcCircuitState::readersOf(world, pos);
    readers.insert(readers.end(), found.begin(), found.end());
}

void QcServerCircuits::Collected::addTwoOut(const QcCircuitState::Change &change, const QcServerWorld &world, const QcSignalTable &table)
{
    const auto spreads = [&](QcBlockId id) {
        const QcSignalKind kind = table.at(id).kind;
        return kind == QcSignalKind::Torch || kind == QcSignalKind::Dust;
    };
    if (!spreads(change.before) && !spreads(change.after)) {
        return;
    }

    // The cells next to a neighbour: all 18 at a distance of two steps. Only components that react
    // to a bare update look: the others see every power they take through the neighbours of what
    // the torch or dust powers (addAround).
    static constexpr std::array<std::array<int, 3>, 18> twoSteps{{
        {2, 0, 0}, {-2, 0, 0}, {0, 2, 0}, {0, -2, 0}, {0, 0, 2}, {0, 0, -2},
        {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0}, {1, 0, 1}, {1, 0, -1},
        {-1, 0, 1}, {-1, 0, -1}, {0, 1, 1}, {0, 1, -1}, {0, -1, 1}, {0, -1, -1},
    }};
    for (const auto &[dx, dy, dz] : twoSteps) {
        const QcBlockPos far = change.pos.offset(dx, dy, dz);
        const QcSignalKind kind = table.at(world.blockAt(far).value_or(qcAirBlockId)).kind;
        if (kind == QcSignalKind::Piston || kind == QcSignalKind::Dispenser) {
            components.push_back(far);
        }
    }
}

void QcServerCircuits::Collected::addObservers(QcBlockPos pos, const QcServerWorld &world, const QcSignalTable &table)
{
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos next = qcNeighbour(pos, face);
        const QcSignalState &state = table.at(world.blockAt(next).value_or(qcAirBlockId));
        if (state.kind == QcSignalKind::Observer && state.facing == QcBlockRules::opposite(face)) {
            observers.push_back(next);
        }
    }
}

void QcServerCircuits::Collected::addAround(const QcCircuitState::Change &change, const QcSignalTable &table)
{
    for (const QcBlockFace face : qcAllBlockFaces) {
        components.push_back(qcNeighbour(change.pos, face));
    }
    // A rail slot's line goes on up and down slopes: the rails one higher or lower beside it look too.
    if (table.at(change.before).kind == QcSignalKind::Rail || table.at(change.after).kind == QcSignalKind::Rail) {
        const std::array<QcBlockPos, 8> slopes = QcRails::slopeNeighbours(change.pos);
        components.insert(components.end(), slopes.begin(), slopes.end());
    }

    // A new strength of dust changes no other wire: dust takes no power from blocks dust powers, and
    // connections only depend on where dust lies. Everything else lets the dust within one block
    // reconnect (slopes, cut lines), and the neighbours of what the block powered or now powers
    // take note.
    const bool strengthOnly = table.at(change.before).kind == QcSignalKind::Dust && table.at(change.after).kind == QcSignalKind::Dust;
    if (!strengthOnly) {
        const std::array<QcBlockPos, 27> cube = cubeAround(change.pos);
        dust.insert(dust.end(), cube.begin(), cube.end());
    }

    QcSignalPower::Positions targets = QcSignalPower::targets(change.pos, table.at(change.before));
    const QcSignalPower::Positions after = QcSignalPower::targets(change.pos, table.at(change.after));
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
    QList<QcCircuitState::Change> changes = std::exchange(m_state.circuits().changes, {});
    const QcServerLimits &limits = m_state.config().limits;
    const qsizetype room = std::max(limit / static_cast<qsizetype>(limits.signalWorkPerChange), static_cast<qsizetype>(limits.signalMinimumStep));
    if (changes.size() > room) {
        // Too many for one step: the first ones in a fixed order of positions now, the rest in the
        // next step. Such a split step is no longer simultaneous (decision E-335).
        std::ranges::stable_sort(changes, [](const QcCircuitState::Change &a, const QcCircuitState::Change &b) { return before(a.pos, b.pos); });
        m_state.circuits().changes = changes.mid(room);
        changes.resize(room);
    }

    // Changes next to a chunk that is not loaded wait for it instead of reading it as air.
    const QcSignalPower power(m_state.world(), m_table);
    changes.removeIf([&](const QcCircuitState::Change &change) {
        const std::optional<QcChunkPos> missing = power.unloadedNear(change.pos);
        if (missing) {
            m_state.circuits().waitOn(*missing, change.pos, change.signal);
        }
        return missing.has_value();
    });
    return changes;
}

void QcServerCircuits::runWave(qsizetype limit)
{
    const QList<QcCircuitState::Change> changes = takeChanges(limit);
    const Collected collected = collect(changes);
    const std::vector<QcBlockPos> &components = collected.components;
    const std::vector<QcBlockPos> &dust = collected.dust;

    const QcSignalPower power(m_state.world(), m_table);
    const QcDustNetwork network(power);
    const QcDustNetwork::Result wired = network.recompute(dust);

    QcCircuitPlan plan;
    plan.work = changes.size() + wired.visited;
    // A wire that reaches a chunk that is not loaded is recomputed once that chunk is; a signal on
    // its way asks for that chunk (decision E-376).
    const bool signal = std::ranges::any_of(changes, &QcCircuitState::Change::signal);
    for (const QcDustNetwork::Result::Blocked &blocked : std::as_const(wired.blocked)) {
        for (const QcBlockPos &pos : blocked.seeds) {
            m_state.circuits().waitOn(blocked.chunk, pos, signal);
        }
    }
    for (const QcDustNetwork::Change &change : std::as_const(wired.changes)) {
        plan.write(change.pos, change.state);
    }

    const QcComparatorReadings readings(m_state.world(), m_state.blockEntities(), m_state.entities().minecarts(), m_state.entities().hangings());
    QcCircuitComponents reactions(componentContext(power, readings));
    for (const QcBlockPos &pos : components) {
        if (const std::optional<QcChunkPos> missing = power.unloadedNear(pos)) {
            if (m_table.at(m_state.world().blockAt(pos).value_or(qcAirBlockId)).kind != QcSignalKind::None) {
                m_state.circuits().waitOn(*missing, pos, signal);
            }
            continue;
        }
        reactions.react(pos, plan);
    }
    for (const QcCircuitState::Change &change : changes) {
        reactions.replaced(change.pos, change.before, change.after, plan);
    }
    reactReadersAndObservers(collected, reactions, plan);
    apply(plan);
}

void QcServerCircuits::reactReadersAndObservers(const Collected &collected, QcCircuitComponents &reactions, QcCircuitPlan &plan) const
{
    const QcSignalPower power(m_state.world(), m_table);
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
    const QcServerLimits &limits = m_state.config().limits;
    const qsizetype room = std::max(m_budget / static_cast<qsizetype>(limits.signalWorkPerChange), static_cast<qsizetype>(limits.signalMinimumStep));
    // A group larger than the budget: the rest stays due with its tick and priority for the next
    // tick (not simultaneous with the first part any more).
    for (qsizetype i = room; i < positions.size(); ++i) {
        m_state.blockEntities().schedule().add(positions[i], due, priority);
    }
    positions.resize(std::min(positions.size(), room));

    const QcSignalPower power(m_state.world(), m_table);
    const QcComparatorReadings readings(m_state.world(), m_state.blockEntities(), m_state.entities().minecarts(), m_state.entities().hangings());
    QcCircuitComponents reactions(componentContext(power, readings));
    QcCircuitPlan plan;
    for (const QcBlockPos &pos : std::as_const(positions)) {
        // Next to an unloaded chunk the component waits for it and then looks again.
        if (const std::optional<QcChunkPos> missing = power.unloadedNear(pos)) {
            m_state.circuits().waitOn(*missing, pos, true);
            plan.addWork(1);
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
        m_state.blockEntities().schedule().add(schedule.pos, m_state.clock().tick() + static_cast<quint64>(std::max(1, schedule.delay)), schedule.priority);
    }

    applyWrites(plan);
    applyOpenings(plan);
    m_budget -= applyPistons(plan);
    m_budget -= m_dispensers.fire(plan.fires);
    for (const QcBlockPos &pos : std::as_const(plan.ignitions)) {
        if (m_light) {
            m_light(pos);
        }
    }

    for (const QcBlockPos &pos : std::as_const(plan.notes)) {
        playNote(pos);
    }
    for (const QcBlockPos &pos : std::as_const(plan.burntOut)) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::torchFizz), centreOf(pos));
    }
}

void QcServerCircuits::applyWrites(const QcCircuitPlan &plan)
{
    std::vector<QcBlockPos> cells;
    cells.reserve(static_cast<std::size_t>(plan.writes.size()));
    for (auto it = plan.writes.constBegin(); it != plan.writes.constEnd(); ++it) {
        cells.push_back(it.key());
    }
    std::ranges::sort(cells, before);

    for (const QcBlockPos &cell : cells) {
        const QcBlockId block = plan.writes.value(cell);
        if (m_state.world().blockAt(cell) != block) {
            m_blocks.setAndBroadcast(cell, block);
        }
    }
}

void QcServerCircuits::applyOpenings(const QcCircuitPlan &plan)
{
    for (const QcCircuitPlan::Opening &opening : std::as_const(plan.openings)) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(opening.pos);
        if (!block || m_table.at(*block).kind != QcSignalKind::Opener || m_table.at(*block).on == opening.open) {
            continue;
        }
        if (!QcOpeners(m_state, m_sounds, m_blocks).toggle(opening.pos, *block)) {
            // Someone stands in the way: the opener has not seen the change yet and tries again.
            m_state.blockEntities().memoryAt(opening.pos).powered = !opening.open;
            m_state.blockEntities().schedule().add(opening.pos, m_state.clock().tick() + openerRetryTicks, QcSignalSchedule::Priority::Release);
        }
    }
}

qsizetype QcServerCircuits::applyPistons(const QcCircuitPlan &plan)
{
    const QList<QcPistonPlan> kept = QcPistonPlanner::resolveConflicts(plan.pistons);
    // Pistons that lost a conflict look again next tick, all of them, so none wins by order.
    for (const QcPistonPlan &dropped : plan.pistons) {
        if (!std::ranges::any_of(kept, [&dropped](const QcPistonPlan &piston) { return piston.base == dropped.base; })) {
            m_state.blockEntities().schedule().add(dropped.base, m_state.clock().tick() + 1, QcSignalSchedule::Priority::Diode);
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

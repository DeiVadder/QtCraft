#include "server/chunks/QcChunkLoading.h"

#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/machines/QcBlockScreens.h"
#include "server/spawners/QcFurnishing.h"
#include "server/storage/QcBlockEntityRecords.h"
#include "server/storage/QcEntityRecords.h"

#include <QElapsedTimer>

#include <algorithm>

namespace {

// Admitting or unloading chunks longer than this in one tick is logged with what it did.
constexpr qint64 slowPhaseMs = 20;

// Sorted, so the same world loads and unloads in the same order.
QList<QcChunkPos> sorted(QList<QcChunkPos> chunks)
{
    std::ranges::sort(chunks, {}, [](QcChunkPos chunk) { return std::pair{chunk.x, chunk.z}; });
    return chunks;
}

QList<QcChunkPos> sorted(const QSet<QcChunkPos> &chunks)
{
    return sorted(QList<QcChunkPos>(chunks.cbegin(), chunks.cend()));
}

template<typename Value>
QList<QcChunkPos> sortedKeys(const QHash<QcChunkPos, Value> &hash)
{
    return sorted(QList<QcChunkPos>(hash.keyBegin(), hash.keyEnd()));
}

} // namespace

QcChunkLoading::QcChunkLoading(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_blocks(blocks)
    , m_updates(updates)
    , m_signals(state, m_tickets, updates)
{
    m_state.world().setArrivalHandler([this](QcChunkIo::Arrival &arrival) { restore(arrival); });
    m_state.world().setGroupLoader([this](QcChunkPos pos) { loadGroupNow(pos); });
}

quint64 QcChunkLoading::upcomingTick() const
{
    return m_state.clock().tick() + (m_simulated ? 1 : 0);
}

void QcChunkLoading::loadGroupNow(QcChunkPos pos)
{
    QcServerWorld &world = m_state.world();
    const QcChunkGroups::Group group = QcChunkGroups::of(pos, [this](QcChunkPos chunk) { return marksForLoading(chunk); }, groupLimit());
    if (group.oversize) {
        warnOversize(group);
    }
    QList<QcChunkIo::Arrival> arrivals;
    const QList<QcChunkPos> members = sorted(group.oversize ? QList<QcChunkPos>{pos} : group.chunks);
    for (const QcChunkPos &member : members) {
        if (world.chunk(member)) {
            continue;
        }
        if (const auto waiting = m_waiting.find(member); waiting != m_waiting.end()) {
            arrivals.append(std::move(*waiting));
            m_waiting.erase(waiting);
            m_waitingMarks.remove(member);
            continue;
        }
        arrivals.append(world.produceNow(member));
    }
    for (QcChunkIo::Arrival &arrival : arrivals) {
        world.admit(std::move(arrival));
    }
}

int QcChunkLoading::groupLimit() const
{
    return static_cast<int>(std::max<qint64>(1, m_state.config().limits.chunkGroupLimit));
}

std::optional<quint16> QcChunkLoading::marksForLoading(QcChunkPos pos) const
{
    if (m_state.world().chunk(pos)) {
        return m_state.world().links().marksOf(pos);
    }
    if (const auto waiting = m_waitingMarks.constFind(pos); waiting != m_waitingMarks.cend()) {
        return *waiting;
    }
    // A chunk never stored has no circuits of its own yet (generated circuits link once it is loaded).
    return m_state.world().linkMarksOf(pos);
}

void QcChunkLoading::warnOversize(const QcChunkGroups::Group &group)
{
    ++m_oversizeGroups;
    const QcChunkPos first = group.chunks.constFirst();
    if (!m_oversizeWarned.contains(first)) {
        m_oversizeWarned.insert(first);
        qCWarning(lcServer) << "Circuits around chunk" << first.x << first.z << "connect more than" << groupLimit()
                            << "chunks (chunkGroupLimit); they load chunk by chunk and wait at the edge of the loaded area";
    }
}

void QcChunkLoading::request(QcChunkPos pos)
{
    QcServerWorld &world = m_state.world();
    // Beyond the technical bound of the world (decision E-883) nothing is generated or saved.
    if (!pos.isInsideWorld() || world.chunk(pos) || m_waiting.contains(pos)) {
        return;
    }
    if (m_state.config().loadChunksAtOnce) {
        loadGroupNow(pos);
        return;
    }
    // Asked for before (streaming asks every tick until it comes): its group was asked for then.
    if (world.isRequested(pos)) {
        return;
    }
    const QcChunkGroups::Group group = QcChunkGroups::of(pos, [this](QcChunkPos chunk) { return marksForLoading(chunk); }, groupLimit());
    if (group.oversize) {
        warnOversize(group);
        world.requestChunk(pos);
        return;
    }
    for (const QcChunkPos &member : group.chunks) {
        if (!world.chunk(member) && !m_waiting.contains(member)) {
            world.requestChunk(member);
        }
    }
}

void QcChunkLoading::admitArrivals()
{
    QElapsedTimer timer;
    timer.start();
    QcServerWorld &world = m_state.world();
    const qsizetype loadedBefore = world.loadedChunkCount();
    std::vector<QcChunkIo::Arrival> arrived = world.collectArrivals();
    for (QcChunkIo::Arrival &arrival : arrived) {
        const QcChunkPos pos = arrival.pos;
        m_waitingMarks.insert(pos, world.links().marksOf(*arrival.blocks));
        m_waiting.emplace(pos, std::move(arrival));
    }

    // A chunk enters with its whole group, all in this tick; missing members are asked for.
    const QList<QcChunkPos> waiting = sortedKeys(m_waiting);
    for (const QcChunkPos &pos : waiting) {
        if (m_waiting.contains(pos)) {
            admitGroup(pos);
        }
    }

    // A long admission is named with what it did, so a stall in this phase can be told from one of
    // the whole process (decision E-378).
    if (const qint64 ms = timer.elapsed(); ms >= slowPhaseMs) {
        qCWarning(lcServer) << "Admitting chunks took" << ms << "ms:" << arrived.size() << "arrived," << world.loadedChunkCount() - loadedBefore
                            << "entered," << m_waiting.size() << "wait for their group";
    }
}

void QcChunkLoading::admitGroup(QcChunkPos pos)
{
    QcServerWorld &world = m_state.world();
    const QcChunkGroups::Group group = QcChunkGroups::of(pos, [this](QcChunkPos chunk) { return marksForLoading(chunk); }, groupLimit());
    if (group.oversize) {
        warnOversize(group);
    }
    const QList<QcChunkPos> members = sorted(group.oversize ? QList<QcChunkPos>{pos} : group.chunks);
    const auto present = [&](QcChunkPos member) { return world.chunk(member) || m_waiting.contains(member); };
    if (!std::ranges::all_of(members, present)) {
        for (const QcChunkPos &member : members) {
            if (!present(member)) {
                world.requestChunk(member);
            }
        }
        return;
    }
    for (const QcChunkPos &member : members) {
        if (const auto it = m_waiting.find(member); it != m_waiting.end()) {
            QcChunkIo::Arrival arrival = std::move(*it);
            m_waiting.erase(it);
            m_waitingMarks.remove(member);
            world.admit(std::move(arrival));
        }
    }
}

void QcChunkLoading::restore(QcChunkIo::Arrival &arrival)
{
    const QcChunkPos pos = arrival.pos;
    QStringList warnings = arrival.warnings;
    warnings.append(QcBlockEntityRecords::apply({arrival.blockEntities}, m_state.blockEntities(), m_state.circuits(), m_state.hoppers(),
                                                upcomingTick()));
    // What this version does not read waits for the next save of the chunk (decision E-440).
    m_state.world().blockEntityExtras().keepParts(pos, std::move(arrival.blockEntities.kept));
    // What was kept is fitted to the blocks (a mod took a block away); spills drop in the next tick.
    if (const std::shared_ptr<const QcChunk> chunk = m_state.world().chunk(pos)) {
        m_state.blockEntities().reconcile(*chunk);
        QcBlockScreens::releaseViewers(m_state, pos);
        // Burning fires go on with their steps (decision E-392); fire blocks left without one are
        // adopted as new sparks in the next fire step.
        m_state.world().fires().restore(arrival.blockEntities.fires, upcomingTick(), *chunk);
        // Spawners go on with their kinds and delays; a chunk not furnished yet whose blocks were just
        // generated gets the generator's chest loot and spawner kinds (decision E-520). Blocks read
        // from disk were generated and furnished when they first came; asking again would plan
        // regions on the tick thread for nothing.
        m_state.world().spawners().restore(arrival.blockEntities.spawners, *chunk);
        m_state.world().campfires().restore(arrival.blockEntities.campfires, *chunk);
        m_state.world().signs().restore(arrival.blockEntities.signs, *chunk);
        if (arrival.blockEntities.furnished) {
            m_state.world().blockEntityExtras().noteFurnished(pos);
        }
        if (arrival.generated) {
            QcFurnishing::furnish(m_state, pos);
        }
    }

    const bool commandMode = m_updates.commandMode();
    m_updates.setCommandMode(true);
    for (const QcBlockPos &update : std::as_const(arrival.blockEntities.blockUpdates)) {
        m_updates.queue(update);
    }
    m_updates.setCommandMode(commandMode);

    warnings.append(QcEntityRecords::apply(arrival.entities, m_state.entities()));
    if (arrival.entities.populated) {
        m_state.entities().creatures().notePopulated(pos);
    }
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcServer) << "Chunk" << pos.x << pos.z << warning;
    }
}

template<typename Visit>
void QcChunkLoading::forPlayerChunks(Visit visit) const
{
    for (const auto &client : m_state.clients()) {
        for (const QcChunkPos &chunk : std::as_const(client->view.sentChunks)) {
            visit(chunk);
        }
        const QcChunkPos centre = QcChunkStreaming::chunkOf(client->motion.position);
        const int radius = client->view.distance > 0 ? client->view.distance : m_state.config().viewDistance;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                visit(QcChunkPos{centre.x + dx, centre.z + dz});
            }
        }
    }
}

template<typename Visit>
void QcChunkLoading::forSpawnChunks(Visit visit) const
{
    const int radius = m_state.info().spawnChunkRadius;
    if (radius <= 0) {
        return;
    }
    const QcChunkPos spawn = m_state.spawnPoint().column().chunk();
    for (int dx = 1 - radius; dx < radius; ++dx) {
        for (int dz = 1 - radius; dz < radius; ++dz) {
            visit(QcChunkPos{spawn.x + dx, spawn.z + dz});
        }
    }
}

template<typename Visit>
void QcChunkLoading::forKeptChunks(Visit visit) const
{
    forSpawnChunks(visit);
    for (const QcChunkPos &chunk : std::as_const(m_state.info().forcedChunks)) {
        visit(chunk);
    }
    const QList<QcChunkPos> loaders = m_state.world().loaders().chunks(static_cast<qsizetype>(m_state.config().limits.chunkLoaders));
    for (const QcChunkPos &chunk : loaders) {
        visit(chunk);
    }
}

void QcChunkLoading::updateTickets()
{
    const quint64 now = m_state.clock().tick();
    QcChunkTickets::Limits limits;
    limits.perTick = static_cast<qsizetype>(m_state.config().limits.chunkTicketsPerTick);
    limits.total = static_cast<qsizetype>(m_state.config().limits.chunkTickets);
    limits.ticks = static_cast<quint64>(m_state.config().limits.chunkTicketTicks);
    m_tickets.setLimits(limits);
    const QSet<QcChunkPos> holding = m_signals.waitingChunks();
    const QSet<QcChunkPos> passing = m_signals.chunksWithSignals();
    m_tickets.expire(now, [this](QcChunkPos chunk) { return m_state.world().chunk(chunk) != nullptr; },
                     [this, &holding, &passing](QcChunkPos chunk) {
                         using Use = QcChunkTickets::Use;
                         if (holding.contains(chunk)) {
                             return Use::Waiting;
                         }
                         return passing.contains(chunk) || m_state.blockEntities().schedule().hasEntriesIn(chunk) ? Use::Busy : Use::Free;
                     });

    // Forced chunks, spawn chunks and those of chunk loaders load like those around a player.
    forKeptChunks([this](QcChunkPos chunk) {
        if (!m_state.world().chunk(chunk) && !m_state.world().isRequested(chunk) && !m_waiting.contains(chunk)) {
            request(chunk);
        }
    });

    // A signal that reached a part of its line that is not loaded asks for that chunk (decision E-376).
    // A refused wish is asked again in the following ticks while the signal waits, so a signal is
    // never left waiting in a chunk that then leaves (decision E-378).
    const QSet<QcChunkPos> fresh = m_state.circuits().takeTicketWishes();
    const QList<QcChunkPos> wishes = sorted(fresh);
    for (const QcChunkPos &chunk : wishes) {
        requestTicket(chunk, now, false);
    }
    const QList<QcChunkPos> waiting = sorted(m_signals.targets());
    for (const QcChunkPos &chunk : waiting) {
        if (!fresh.contains(chunk)) {
            requestTicket(chunk, now, true);
        }
    }
}

void QcChunkLoading::requestStrays(const QSet<QcChunkPos> &strays)
{
    for (const QcChunkPos &chunk : strays) {
        if (!m_state.world().isRequested(chunk)) {
            request(chunk);
        }
    }
}

QSet<QcChunkPos> QcChunkLoading::neededChunks() const
{
    QSet<QcChunkPos> needed;
    const auto add = [&needed](QcChunkPos chunk) { needed.insert(chunk); };
    forPlayerChunks(add);
    forKeptChunks(add);
    needed.unite(m_signals.waitingChunks());
    const QList<QcChunkPos> tickets = m_tickets.chunks();
    for (const QcChunkPos &chunk : tickets) {
        needed.insert(chunk);
    }
    return needed;
}

QcChunkLoading::NeededInputs QcChunkLoading::neededInputs() const
{
    NeededInputs inputs;
    inputs.loadedEpoch = m_state.world().loadedEpoch();
    inputs.links = m_state.world().links().changes();
    inputs.players.reserve(m_state.clients().size());
    for (const auto &client : m_state.clients()) {
        inputs.players.push_back({client.get(), QcChunkStreaming::chunkOf(client->motion.position), client->view.distance, client->view.sentChanges});
    }
    inputs.forced = m_state.info().forcedChunks;
    inputs.spawnRadius = m_state.info().spawnChunkRadius;
    if (inputs.spawnRadius > 0) {
        inputs.spawn = m_state.spawnPoint().column().chunk();
    }
    inputs.loaders = m_state.world().loaders().changes();
    inputs.loaderLimit = m_state.config().limits.chunkLoaders;
    inputs.tickets = m_tickets.changes();
    inputs.waitingSignals = m_signals.waitingChunks();
    return inputs;
}

void QcChunkLoading::unloadUnneeded()
{
    const QcServerWorld &world = m_state.world();
    if (m_state.config().keepChunksLoaded) {
        return;
    }
    // Nothing that decides which chunks are needed or how they are linked changed since a pass that
    // let none leave (the needed set and the groups are costly to build every tick). keepChangedChunks depends on every block set.
    NeededInputs inputs = neededInputs();
    const bool keepChanged = m_state.config().keepChangedChunks;
    if (!keepChanged && m_settled == inputs) {
        return;
    }
    m_settled.reset();
    const QSet<QcChunkPos> needed = neededChunks();
    QSet<QcChunkPos> candidates;
    const QList<QcChunkPos> loaded = world.loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        if (!needed.contains(pos) && !(keepChanged && world.saves().differsFromGenerated(pos))) {
            candidates.insert(pos);
        }
    }
    // A signal on its way finishes before its chunk leaves (decision E-410); a pass that kept one
    // is not settled, the next tick looks again.
    const QSet<QcChunkPos> held = m_signals.passing(candidates, m_circuitChunks);
    candidates.subtract(held);
    const bool holding = !held.isEmpty();
    m_circuitChunks = circuitChunks();
    if (candidates.isEmpty()) {
        if (!holding) {
            m_settled = std::move(inputs);
        }
        return;
    }

    const QSet<QcChunkPos> leaving = leavingGroups(candidates);
    if (leaving.isEmpty() && !holding) {
        m_settled = std::move(inputs);
    }
    unload(leaving);
}

QSet<QcChunkPos> QcChunkLoading::leavingGroups(const QSet<QcChunkPos> &candidates)
{
    // A group leaves only as a whole, when none of its chunks is needed (decision E-375).
    const auto loadedMarks = [this](QcChunkPos chunk) { return this->loadedMarks(chunk); };
    const qsizetype budget = static_cast<qsizetype>(m_state.config().limits.chunkUnloadsPerTick);
    QSet<QcChunkPos> decided;
    QSet<QcChunkPos> leaving;
    const QList<QcChunkPos> ordered = sorted(candidates);
    for (const QcChunkPos &pos : ordered) {
        if (leaving.size() >= budget) {
            break;
        }
        if (decided.contains(pos)) {
            continue;
        }
        const QcChunkGroups::Group group = QcChunkGroups::of(pos, loadedMarks, groupLimit());
        if (group.oversize) {
            warnOversize(group);
            decided.insert(pos);
            leaving.insert(pos);
            continue;
        }
        const bool unneeded = std::ranges::all_of(group.chunks, [&candidates](QcChunkPos member) { return candidates.contains(member); });
        for (const QcChunkPos &member : group.chunks) {
            decided.insert(member);
            if (unneeded) {
                leaving.insert(member);
            }
        }
    }
    return leaving;
}

std::optional<quint16> QcChunkLoading::loadedMarks(QcChunkPos chunk) const
{
    const QcServerWorld &world = m_state.world();
    return world.chunk(chunk) ? std::optional<quint16>(world.links().marksOf(chunk)) : std::nullopt;
}

bool QcChunkLoading::requestTicket(QcChunkPos chunk, quint64 now, bool retry)
{
    if (m_state.world().chunk(chunk) || m_tickets.contains(chunk)) {
        return true;
    }
    // The ticket weighs the group that comes with the chunk.
    const QcChunkGroups::Group group = QcChunkGroups::of(chunk, [this](QcChunkPos pos) { return marksForLoading(pos); }, groupLimit());
    if (!m_tickets.request(chunk, now, group.oversize ? 1 : group.chunks.size(), retry)) {
        return false;
    }
    request(chunk);
    return true;
}

QSet<QcChunkPos> QcChunkLoading::circuitChunks() const
{
    QSet<QcChunkPos> kept = m_signals.waitingChunks();
    const QList<QcChunkPos> tickets = m_tickets.chunks();
    for (const QcChunkPos &chunk : tickets) {
        kept.insert(chunk);
    }

    QSet<QcChunkPos> chunks = kept;
    for (const QcChunkPos &chunk : std::as_const(kept)) {
        if (!m_state.world().chunk(chunk)) {
            continue;
        }
        const QcChunkGroups::Group group = QcChunkGroups::of(chunk, [this](QcChunkPos pos) { return loadedMarks(pos); }, groupLimit());
        if (!group.oversize) {
            chunks.unite(QSet<QcChunkPos>(group.chunks.cbegin(), group.chunks.cend()));
        }
    }
    return chunks;
}

void QcChunkLoading::unload(const QSet<QcChunkPos> &chunks)
{
    if (chunks.isEmpty()) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    QcServerWorld &world = m_state.world();
    // Contents spilled in this tick lie on the ground before the items are taken.
    m_blocks.dropSpills();
    const QHash<QcChunkPos, QcEntityRecord> entities = QcEntityRecords::take(m_state.entities(), chunks);
    const QHash<QcChunkPos, QList<QcBlockPos>> updates = m_updates.takeChunks(chunks);
    const QHash<QcChunkPos, QList<QcBlockPos>> flows = m_state.liquids().takeChunks(chunks);
    const quint64 now = upcomingTick();

    const QList<QcChunkPos> leaving = sorted(chunks);
    for (const QcChunkPos &pos : leaving) {
        QcChunkIo::Departure departure;
        departure.pos = pos;
        departure.blockEntities.chunk = pos;
        departure.blockEntities.kept = world.blockEntityExtras().takeParts(pos);
        departure.blockEntities.takeFrom(m_state.blockEntities(), m_state.circuits(), m_state.hoppers(), now);
        departure.blockEntities.blockUpdates = updates.value(pos);
        departure.blockEntities.blockUpdates.append(flows.value(pos));
        departure.blockEntities.fires = world.fires().takeChunk(pos, now);
        departure.blockEntities.spawners = world.spawners().takeChunk(pos);
        departure.blockEntities.campfires = world.campfires().takeChunk(pos);
        departure.blockEntities.signs = world.signs().takeChunk(pos);
        departure.blockEntities.furnished = world.blockEntityExtras().isFurnished(pos);
        departure.blockEntities.pruneKept();
        departure.entities = entities.value(pos);
        departure.entities.chunk = pos;
        departure.entities.populated = m_state.entities().creatures().isPopulated(pos);
        m_state.entities().creatures().forgetPopulated(pos);
        departure.marks = world.links().marksOf(pos);
        const bool quarantined = world.saves().isQuarantined(pos);
        departure.blocks = world.remove(pos);
        // Back in the world, a signal it took along is held again for a ticket's time (E-410).
        m_signals.forget(pos);
        ++m_unloaded;
        if (!quarantined && world.saves().storage()) {
            world.saves().noteKept(departure);
            world.saves().storage()->keep(std::move(departure));
        }
    }
    if (const qint64 ms = timer.elapsed(); ms >= slowPhaseMs) {
        qCWarning(lcServer) << "Unloading" << chunks.size() << "chunks took" << ms << "ms";
    }
}

void QcChunkLoading::loadStraysNow()
{
    QSet<QcChunkPos> strays;
    const QcServerWorld &world = m_state.world();
    m_state.entities().forEach([&](const QcEntity &entity) {
        if (entity.kind != QcEntityKind::Player && !world.chunk(QcEntities::chunkOf(entity))) {
            strays.insert(QcEntities::chunkOf(entity));
        }
    });
    for (const QcItemEntityManager::Drop &drop : m_state.entities().items().queued()) {
        if (const QcChunkPos chunk = QcPhysics::blockContaining(drop.position).chunk(); !world.chunk(chunk)) {
            strays.insert(chunk);
        }
    }
    const QList<QcChunkPos> ordered = sorted(strays);
    for (const QcChunkPos &chunk : ordered) {
        m_state.world().loadChunk(chunk);
    }
}

QList<QcChunkIo::Departure> QcChunkLoading::departuresForSave()
{
    QcServerWorld &world = m_state.world();
    QHash<QcChunkPos, QcEntityRecord> entities = QcEntityRecords::collect(m_state.entities());
    QHash<QcChunkPos, QcBlockEntityRecord> blockEntities;
    const QList<QcBlockEntityRecord> records =
        QcBlockEntityRecords::collect(m_state.blockEntities(), m_state.circuits(), upcomingTick(), world.blockEntityExtras().keptParts());
    for (const QcBlockEntityRecord &record : records) {
        blockEntities.insert(record.chunk, record);
    }

    QList<QcChunkIo::Departure> departures;
    const QList<QcChunkPos> loaded = sorted(world.loadedChunks());
    departures.reserve(loaded.size());
    for (const QcChunkPos &pos : loaded) {
        if (world.saves().isQuarantined(pos)) {
            continue;
        }
        QcChunkIo::Departure departure;
        departure.pos = pos;
        departure.blocks = world.takeChanged(pos);
        departure.blockEntities = blockEntities.take(pos);
        departure.blockEntities.chunk = pos;
        departure.blockEntities.fires = world.fires().savedOf(pos, upcomingTick());
        departure.blockEntities.spawners = world.spawners().savedOf(pos);
        departure.blockEntities.campfires = world.campfires().savedOf(pos);
        departure.blockEntities.signs = world.signs().savedOf(pos);
        departure.blockEntities.furnished = world.blockEntityExtras().isFurnished(pos);
        // Kept parts go back with the record; tails of entries gone since are dropped for good.
        departure.blockEntities.kept = world.blockEntityExtras().keptParts().value(pos);
        departure.blockEntities.pruneKept();
        world.blockEntityExtras().keepParts(pos, departure.blockEntities.kept);
        departure.entities = entities.take(pos);
        departure.entities.chunk = pos;
        departure.entities.populated = m_state.entities().creatures().isPopulated(pos);
        departure.marks = world.links().marksOf(pos);
        world.saves().noteKept(departure);
        departures.append(std::move(departure));
    }
    if (!blockEntities.isEmpty() || !entities.isEmpty()) {
        qCWarning(lcServer) << "A save found block entities or entities in" << blockEntities.size() + entities.size()
                            << "chunks that are not loaded; they are kept in memory only";
    }
    return departures;
}

QcChunkGroups::Group QcChunkLoading::groupOf(QcChunkPos pos) const
{
    const QcServerWorld &world = m_state.world();
    return QcChunkGroups::of(
        pos, [&world](QcChunkPos chunk) { return world.chunk(chunk) ? std::optional<quint16>(world.links().marksOf(chunk)) : std::nullopt; },
        groupLimit());
}

QcChunkLoading::Numbers QcChunkLoading::numbers() const
{
    Numbers numbers;
    const QcServerWorld &world = m_state.world();
    numbers.loaded = world.loadedChunkCount();
    numbers.stored = world.saves().storedChunkCount();
    numbers.waiting = m_waiting.size() + world.pendingGenerationCount();
    numbers.tickets = m_tickets.count();
    numbers.refusedTickets = m_tickets.refused();
    numbers.forced = m_state.info().forcedChunks.size();
    numbers.spawnChunks = m_state.info().spawnChunkRadius > 0 ? (2 * m_state.info().spawnChunkRadius - 1) * (2 * m_state.info().spawnChunkRadius - 1) : 0;
    numbers.loaders = m_state.world().loaders().count();
    numbers.unsaved = world.saves().storage() ? world.saves().storage()->pendingChunks() : 0;
    numbers.unloaded = m_unloaded;
    numbers.oversizeGroups = m_oversizeGroups;

    // Groups of more than one chunk among the loaded ones.
    QSet<QcChunkPos> seen;
    const QList<QcChunkPos> loaded = world.loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        if (seen.contains(pos) || world.links().marksOf(pos) == 0) {
            continue;
        }
        const QcChunkGroups::Group group = groupOf(pos);
        for (const QcChunkPos &member : group.chunks) {
            seen.insert(member);
        }
        if (group.chunks.size() > 1) {
            ++numbers.groups;
            numbers.largestGroup = std::max(numbers.largestGroup, group.chunks.size());
        }
    }
    return numbers;
}

#include "server/storage/QcBlockEntityRecords.h"

#include "server/blockentities/QcBlockEntities.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcHopperQueue.h"

#include <QSet>

using namespace Qt::StringLiterals;

#include <algorithm>

namespace {

// Ticks are saved as 32 bits; longer waits are cut to this.
constexpr quint64 maxTicks = quint64(1) << 20;

quint32 ticksLeft(quint64 due, quint64 now)
{
    return quint32(std::min(due - std::min(due, now), maxTicks));
}

// The entries of one chunk, read straight from the tables through the chunk's index.
void collectEntries(const QcBlockEntities &entities, const QcBlockEntities::ChunkIndex &index, quint64 now, QcBlockEntityRecord &record)
{
    record.containers.reserve(index.containers.size());
    for (const QcBlockPos &pos : index.containers) {
        const QcBlockEntities::Container &container = *entities.container(pos);
        const bool filled = std::ranges::any_of(container.stacks, [](QcItemStack stack) { return !stack.isEmpty(); });
        if (filled || container.readyAt > now) {
            record.containers.append(container);
            record.containers.last().readyAt = ticksLeft(container.readyAt, now);
        }
    }
    record.machines.reserve(index.machines.size());
    for (const QcBlockPos &pos : index.machines) {
        if (const QcBlockEntities::Machine &machine = *entities.machine(pos); !machine.isIdle()) {
            record.machines.append(machine);
        }
    }
    record.saplings.reserve(index.saplings.size());
    for (const QcBlockPos &pos : index.saplings) {
        record.saplings.append(*entities.sapling(pos));
    }
    for (const QcBlockPos &pos : index.memories) {
        const QcBlockEntities::Memory &kept = *entities.memory(pos);
        QcBlockEntityRecord::Memory memory{pos, ticksLeft(kept.burntOutUntil, now), kept.powered, std::nullopt};
        if (kept.extendingSince) {
            memory.extendingAge = quint32(std::min(now - std::min(now, *kept.extendingSince), maxTicks));
        }
        if (memory.burntOutLeft > 0 || memory.powered || memory.extendingAge) {
            record.memories.append(memory);
        }
    }
}

// Containers, machines and saplings of a record.
void applyContents(const QcBlockEntityRecord &record, QcBlockEntities &entities, QcHopperQueue &hoppers, quint64 now,
                   QStringList &warnings)
{
    for (QcBlockEntities::Container container : record.containers) {
        // A cooldown is never longer than the hoppers' (damaged values, a lowered limit).
        const quint64 left = std::min<quint64>(container.readyAt, quint64(std::max(0, hoppers.cooldownTicks)));
        container.readyAt = left > 0 ? now + left : 0;
        if (entities.putContainer(container) && left > 0) {
            hoppers.wakeAt(container.pos, now + left);
        }
    }
    for (const QcBlockEntities::Machine &machine : record.machines) {
        entities.putMachine(machine);
    }
    for (const QcBlockEntities::Sapling &sapling : record.saplings) {
        if (!entities.putSapling(sapling)) {
            warnings.append(u"A growing sapling at %1 %2 %3 shares its position or order with another one and was left out"_s
                                .arg(sapling.pos.x).arg(sapling.pos.y).arg(sapling.pos.z));
        }
    }
}

// What components remember, their scheduled ticks and the positions the circuits look at.
void applyCircuits(const QcBlockEntityRecord &record, QcBlockEntities &entities, QcCircuitState &circuits, quint64 now)
{
    for (const QcBlockEntityRecord::Memory &saved : record.memories) {
        QcBlockEntities::Memory &memory = entities.memoryAt(saved.pos);
        memory.burntOutUntil = saved.burntOutLeft > 0 ? now + saved.burntOutLeft : 0;
        memory.powered = saved.powered;
        if (saved.extendingAge) {
            memory.extendingSince = now - std::min<quint64>(now, *saved.extendingAge);
        }
    }
    for (const QcBlockEntityRecord::Scheduled &entry : record.scheduled) {
        entities.schedule().add(entry.pos, now + entry.ticksLeft, entry.priority);
    }
    // Looked at in the next step as the changes they were; where they read a chunk that is not
    // loaded they wait for it (QcServerCircuits::takeChanges), so the chunks around need not be there.
    for (const QcBlockPos &pos : record.wakes) {
        circuits.changes.append({pos, qcAirBlockId, qcAirBlockId, false, false});
    }
    for (const QcBlockPos &pos : record.signalWakes) {
        circuits.changes.append({pos, qcAirBlockId, qcAirBlockId, false, true});
    }
}

// Entries of a hash in a fixed order, so the same world gives the same file.
template<typename Entry>
void sortByPos(QList<Entry> &entries)
{
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) { return a.pos < b.pos; });
}

} // namespace

namespace QcBlockEntityRecords {

QList<QcBlockEntityRecord> collect(const QcBlockEntities &entities, const QcCircuitState &circuits, quint64 now)
{
    const QHash<QcChunkPos, QcCircuitState::Pending> pending = circuits.pendingByChunk();
    QSet<QcChunkPos> chunks;
    for (const QList<QcChunkPos> &keys : {entities.chunks(), entities.schedule().chunks(), pending.keys()}) {
        chunks.unite(QSet<QcChunkPos>(keys.begin(), keys.end()));
    }

    QList<QcBlockEntityRecord> records;
    for (const QcChunkPos &pos : std::as_const(chunks)) {
        QcBlockEntityRecord record;
        record.chunk = pos;
        if (const QcBlockEntities::ChunkIndex *index = entities.chunkIndex(pos)) {
            collectEntries(entities, *index, now, record);
        }
        const QList<QcSignalSchedule::Entry> scheduled = entities.schedule().entriesIn(pos);
        for (const QcSignalSchedule::Entry &entry : scheduled) {
            record.scheduled.append({entry.pos, ticksLeft(entry.due, now), entry.priority});
        }
        const QcCircuitState::Pending waits = pending.value(pos);
        record.wakes = waits.looks;
        record.signalWakes = waits.onTheirWay;
        sortByPos(record.containers);
        sortByPos(record.machines);
        sortByPos(record.saplings);
        sortByPos(record.memories);
        if (!record.isEmpty()) {
            records.append(std::move(record));
        }
    }
    std::ranges::sort(records, [](const QcBlockEntityRecord &a, const QcBlockEntityRecord &b) {
        return std::pair{a.chunk.x, a.chunk.z} < std::pair{b.chunk.x, b.chunk.z};
    });
    return records;
}

QcBlockEntityRecord take(QcChunkPos chunk, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers, quint64 now)
{
    QcBlockEntityRecord record;
    record.chunk = chunk;
    if (const QcBlockEntities::ChunkIndex *index = entities.chunkIndex(chunk)) {
        collectEntries(entities, *index, now, record);
    }
    const QList<QcSignalSchedule::Entry> scheduled = entities.schedule().entriesIn(chunk);
    for (const QcSignalSchedule::Entry &entry : scheduled) {
        record.scheduled.append({entry.pos, ticksLeft(entry.due, now), entry.priority});
    }
    const QcCircuitState::Pending waits = circuits.takeChunk(chunk);
    record.wakes = waits.looks;
    record.signalWakes = waits.onTheirWay;
    sortByPos(record.containers);
    sortByPos(record.machines);
    sortByPos(record.saplings);
    sortByPos(record.memories);

    entities.forgetChunk(chunk);
    hoppers.forgetChunk(chunk);
    return record;
}

QStringList apply(const QList<QcBlockEntityRecord> &records, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers,
                  quint64 now)
{
    QStringList warnings;
    for (const QcBlockEntityRecord &record : records) {
        applyContents(record, entities, hoppers, now, warnings);
        applyCircuits(record, entities, circuits, now);
    }
    return warnings;
}

} // namespace QcBlockEntityRecords

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcBlockEntityRecords.h"

#include "server/blockentities/QcBlockEntities.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcHopperQueue.h"

#include <QSet>

using namespace Qt::StringLiterals;

#include <algorithm>

namespace {

// Ticks are saved as 32 bits; longer waits are cut to this.
constexpr quint64 maxTicks = quint64{1} << 20;

quint32 ticksLeft(quint64 due, quint64 now)
{
    return static_cast<quint32>(std::min(due - std::min(due, now), maxTicks));
}

using Table = QcBlockEntityRecord::Table;

// Whether `parts` holds a tail of a newer version for the entry of `table` at `pos`: such an entry is
// saved even when it holds nothing this version would save, so the tail goes back with it.
bool hasTail(const QcBlockEntityKept *parts, Table table, QcBlockPos pos)
{
    return parts && parts->entryTail(static_cast<quint16>(table), pos);
}

// The entries of one chunk with something to save.
struct Entries
{
    QList<QcBlockEntities::Container> containers;
    QList<QcBlockEntities::Machine> machines;
    QList<QcBlockEntities::Sapling> saplings;
    QList<QcBlockEntityRecord::Memory> memories;
};

// The entries of one chunk, read straight from the tables through the chunk's index.
[[nodiscard]] Entries entriesOf(const QcBlockEntities &entities, const QcBlockEntities::ChunkIndex &index, quint64 now,
                                const QcBlockEntityKept *parts)
{
    Entries record;
    record.containers.reserve(index.containers.size());
    for (const QcBlockPos &pos : index.containers) {
        const QcBlockEntities::Container &container = *entities.container(pos);
        // Unlike isPlain(), a cooldown that ran out is not saved.
        if (container.isFilled() || container.readyAt > now || !container.name.isEmpty() || hasTail(parts, Table::Containers, pos)) {
            record.containers.append(container);
            record.containers.last().readyAt = ticksLeft(container.readyAt, now);
        }
    }
    record.machines.reserve(index.machines.size());
    for (const QcBlockPos &pos : index.machines) {
        if (const QcBlockEntities::Machine &machine = *entities.machine(pos); !machine.isIdle() || hasTail(parts, Table::Machines, pos)) {
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
            memory.extendingAge = static_cast<quint32>(std::min(now - std::min(now, *kept.extendingSince), maxTicks));
        }
        if (memory.burntOutLeft > 0 || memory.powered || memory.extendingAge || hasTail(parts, Table::Memories, pos)) {
            record.memories.append(memory);
        }
    }
    return record;
}

// Containers, machines and saplings of a record; the warnings about saplings left out.
[[nodiscard]] QStringList applyContents(const QcBlockEntityRecord &record, QcBlockEntities &entities, QcHopperQueue &hoppers, quint64 now)
{
    QStringList warnings;
    for (QcBlockEntities::Container container : record.containers) {
        // A cooldown is never longer than the hoppers' (damaged values, a lowered limit).
        const quint64 left = std::min<quint64>(container.readyAt, static_cast<quint64>(std::max(0, hoppers.cooldownTicks)));
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
    return warnings;
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
        circuits.lookAgain(pos, false);
    }
    for (const QcBlockPos &pos : record.signalWakes) {
        circuits.lookAgain(pos, true);
    }
}

// Entries of a hash in a fixed order, so the same world gives the same file.
template<typename Entry>
[[nodiscard]] QList<Entry> sortedByPos(QList<Entry> entries)
{
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) { return a.pos < b.pos; });
    return entries;
}

// The record of chunk `pos` without its kept parts, blockUpdates, fires and spawners: the entries
// (decided with `parts`), the scheduled ticks and the positions the circuits wait to look at.
[[nodiscard]] QcBlockEntityRecord recordOf(const QcBlockEntities &entities, QcChunkPos pos, quint64 now, const QcBlockEntityKept *parts,
                                           const QcCircuitState::Pending &waits)
{
    QcBlockEntityRecord record;
    record.chunk = pos;
    if (const QcBlockEntities::ChunkIndex *index = entities.chunkIndex(pos)) {
        Entries entries = entriesOf(entities, *index, now, parts);
        record.containers = sortedByPos(std::move(entries.containers));
        record.machines = sortedByPos(std::move(entries.machines));
        record.saplings = sortedByPos(std::move(entries.saplings));
        record.memories = sortedByPos(std::move(entries.memories));
    }
    const QList<QcSignalSchedule::Entry> scheduled = entities.schedule().entriesIn(pos);
    for (const QcSignalSchedule::Entry &entry : scheduled) {
        record.scheduled.append({entry.pos, ticksLeft(entry.due, now), entry.priority});
    }
    record.wakes = waits.looks;
    record.signalWakes = waits.onTheirWay;
    return record;
}

} // namespace

void QcBlockEntityRecord::takeFrom(QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers, quint64 now)
{
    QcBlockEntityRecord taken = recordOf(entities, chunk, now, &kept, circuits.takeChunk(chunk));
    containers = std::move(taken.containers);
    machines = std::move(taken.machines);
    saplings = std::move(taken.saplings);
    memories = std::move(taken.memories);
    scheduled = std::move(taken.scheduled);
    wakes = std::move(taken.wakes);
    signalWakes = std::move(taken.signalWakes);

    entities.forgetChunk(chunk);
    hoppers.forgetChunk(chunk);
}

void QcBlockEntityRecord::pruneKept()
{
    QHash<QcBlockPos, QcEntityComponents> &entries = kept.entries;
    if (entries.isEmpty()) {
        return;
    }
    QHash<quint16, QSet<QcBlockPos>> present;
    const auto note = [&present](Table table, const auto &list) {
        QSet<QcBlockPos> &positions = present[static_cast<quint16>(table)];
        for (const auto &entry : list) {
            positions.insert(entry.pos);
        }
    };
    note(Table::Containers, containers);
    note(Table::Machines, machines);
    note(Table::Saplings, saplings);
    note(Table::Memories, memories);
    note(Table::Scheduled, scheduled);
    note(Table::Fires, fires);
    note(Table::Spawners, spawners);
    note(Table::Campfires, campfires);
    note(Table::Signs, signs);

    for (auto it = entries.begin(); it != entries.end();) {
        const QcBlockPos pos = it.key();
        it->removeIf([&present, pos](const QcEntityComponent &tail) { return !present.value(tail.id).contains(pos); });
        it = it->isEmpty() ? entries.erase(it) : std::next(it);
    }
}

namespace QcBlockEntityRecords {

QList<QcBlockEntityRecord> collect(const QcBlockEntities &entities, const QcCircuitState &circuits, quint64 now,
                                   const QHash<QcChunkPos, QcBlockEntityKept> &kept)
{
    const QHash<QcChunkPos, QcCircuitState::Pending> pending = circuits.pendingByChunk();
    QSet<QcChunkPos> chunks;
    for (const QList<QcChunkPos> &keys : {entities.chunks(), entities.schedule().chunks(), pending.keys()}) {
        chunks.unite(QSet<QcChunkPos>(keys.begin(), keys.end()));
    }

    QList<QcBlockEntityRecord> records;
    for (const QcChunkPos &pos : std::as_const(chunks)) {
        const auto parts = kept.constFind(pos);
        QcBlockEntityRecord record = recordOf(entities, pos, now, parts != kept.constEnd() ? &*parts : nullptr, pending.value(pos));
        if (!record.isEmpty()) {
            records.append(std::move(record));
        }
    }
    std::ranges::sort(records, [](const QcBlockEntityRecord &a, const QcBlockEntityRecord &b) {
        return std::pair{a.chunk.x, a.chunk.z} < std::pair{b.chunk.x, b.chunk.z};
    });
    return records;
}

QStringList apply(const QList<QcBlockEntityRecord> &records, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers,
                  quint64 now)
{
    QStringList warnings;
    for (const QcBlockEntityRecord &record : records) {
        warnings.append(applyContents(record, entities, hoppers, now));
        applyCircuits(record, entities, circuits, now);
    }
    return warnings;
}

} // namespace QcBlockEntityRecords

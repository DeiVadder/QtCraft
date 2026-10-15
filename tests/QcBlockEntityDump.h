#pragma once

#include "QcServer.h"

#include <QStringList>

#include <algorithm>

// The block entities of a server as text lines, one per entry (decision E-357): containers with
// contents, machines that are not idle, sapling timers, scheduled ticks, burnt-out torches, powered
// openers, extending pistons and hopper cooldowns, ticks counted from now. The same lines came
// from the code before the register (tests/gameplay/test_block_entity_compare.cpp in its first
// version), so two builds and a save before and after can be compared line by line.
namespace QcBlockEntityDump {

inline QString stackText(QcItemStack stack, const QcContentRegistry &content)
{
    return stack.isEmpty() ? QStringLiteral("-") : QStringLiteral("%1x%2/%3").arg(content.item(stack.item).name).arg(stack.count).arg(stack.damage);
}

inline QString posText(QcBlockPos pos)
{
    return QStringLiteral("%1 %2 %3").arg(pos.x).arg(pos.y).arg(pos.z);
}

inline void addChunk(const QcBlockEntities::Chunk &chunk, quint64 now, const QcContentRegistry &content, QStringList &lines)
{
    for (const QcBlockEntities::Container &container : chunk.containers) {
        QStringList stacks;
        for (const QcItemStack &stack : container.stacks) {
            stacks.append(stackText(stack, content));
        }
        if (std::ranges::any_of(container.stacks, [](QcItemStack stack) { return !stack.isEmpty(); })) {
            lines.append(QStringLiteral("container %1: %2").arg(posText(container.pos), stacks.join(u' ')));
        }
        if (container.readyAt > now) {
            lines.append(QStringLiteral("cooldown %1: +%2").arg(posText(container.pos)).arg(qint64(container.readyAt) - qint64(now)));
        }
    }
    for (const QcBlockEntities::Machine &machine : chunk.machines) {
        if (machine.isIdle()) {
            continue;
        }
        QStringList stacks;
        for (const QcItemStack &stack : machine.stacks) {
            stacks.append(stackText(stack, content));
        }
        lines.append(QStringLiteral("machine %1: %2 burn %3/%4 progress %5")
                         .arg(posText(machine.pos), stacks.join(u' '))
                         .arg(machine.burnRemaining, 0, 'f', 6)
                         .arg(machine.burnSeconds, 0, 'f', 6)
                         .arg(machine.progress, 0, 'f', 6));
    }
    for (const QcBlockEntities::Sapling &sapling : chunk.saplings) {
        lines.append(QStringLiteral("sapling %1: %2").arg(posText(sapling.pos)).arg(sapling.remaining, 0, 'f', 6));
    }
    for (auto it = chunk.memories.constBegin(); it != chunk.memories.constEnd(); ++it) {
        if (it->burntOutUntil != 0) {
            lines.append(QStringLiteral("burnt %1: +%2").arg(posText(it.key())).arg(qint64(it->burntOutUntil) - qint64(now)));
        }
        if (it->powered) {
            lines.append(QStringLiteral("powered %1").arg(posText(it.key())));
        }
        if (it->extendingSince) {
            lines.append(QStringLiteral("extending %1: %2").arg(posText(it.key())).arg(qint64(*it->extendingSince) - qint64(now)));
        }
    }
}

// What a chunk that is not loaded keeps (stage 31.3): its waits stand still while it is away and
// are shown as left when it went.
inline void addRecord(const QcBlockEntityRecord &record, const QcContentRegistry &content, QStringList &lines)
{
    QcBlockEntities::Chunk chunk;
    for (const QcBlockEntities::Container &container : record.containers) {
        chunk.containers.insert(container.pos, container);
    }
    for (const QcBlockEntities::Machine &machine : record.machines) {
        chunk.machines.insert(machine.pos, machine);
    }
    for (const QcBlockEntities::Sapling &sapling : record.saplings) {
        chunk.saplings.insert(sapling.pos, sapling);
    }
    for (const QcBlockEntityRecord::Memory &saved : record.memories) {
        QcBlockEntities::Memory memory;
        memory.burntOutUntil = saved.burntOutLeft;
        memory.powered = saved.powered;
        if (saved.extendingAge) {
            memory.extendingSince = -qint64(*saved.extendingAge);
        }
        chunk.memories.insert(saved.pos, memory);
    }
    addChunk(chunk, 0, content, lines);
    for (const QcBlockEntityRecord::Scheduled &entry : record.scheduled) {
        lines.append(QStringLiteral("scheduled %1: +%2 %3").arg(posText(entry.pos)).arg(entry.ticksLeft).arg(int(entry.priority)));
    }
}

inline QStringList lines(QcServer &server, const QcContentRegistry &content)
{
    QStringList lines;
    const quint64 now = server.tickCount();
    const QcBlockEntities &entities = server.blockEntities();
    const QList<QcChunkPos> chunks = entities.chunks();
    for (const QcChunkPos &pos : chunks) {
        addChunk(*entities.chunk(pos), now, content, lines);
    }
    if (QcChunkIo *storage = server.world().storage()) {
        const QList<QcChunkPos> stored = server.world().storedChunks();
        for (const QcChunkPos &pos : stored) {
            if (!server.world().chunk(pos)) {
                addRecord(storage->readNow(pos).blockEntities, content, lines);
            }
        }
    }
    const QList<QcSignalSchedule::Entry> scheduled = entities.schedule().entries();
    for (const QcSignalSchedule::Entry &entry : scheduled) {
        lines.append(QStringLiteral("scheduled %1: +%2 %3").arg(posText(entry.pos)).arg(qint64(entry.due) - qint64(now)).arg(int(entry.priority)));
    }
    std::ranges::sort(lines);
    return lines;
}

} // namespace QcBlockEntityDump

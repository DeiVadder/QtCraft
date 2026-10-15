#include "QcBlockEntityCodec.h"

#include "QcSaveFile.h"
#include "QcStackReader.h"
#include "QcStreamLimits.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Entries of one kind in one chunk at most: one per cell.
constexpr quint32 maxPerChunk = QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight;
// Positions the circuits wait to look at, per chunk (QcServerLimits::signalMaxPendingChanges at most).
constexpr quint32 maxWakes = 1u << 24;
constexpr double maxSeconds = 100000.0;
// Waits are saved as ticks; longer ones are damaged values and cut to this (QcBlockEntityRecords).
constexpr quint32 maxTicks = 1u << 20;
// Planting orders count up from 0; one near the top could make the next order wrap.
constexpr quint64 maxOrder = quint64(1) << 62;

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z);
}

// A position inside the chunk of the record being read.
bool readPos(QDataStream &stream, QcChunkPos chunk, QcBlockPos &pos)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    pos = {x, y, z};
    return stream.status() == QDataStream::Ok && pos.isInsideWorldHeight() && pos.chunk() == chunk;
}

// Reads a list of `count` entries with `read`, which fails on damaged data; positions must not repeat.
template<typename Read>
[[nodiscard]] bool readEntries(QDataStream &stream, quint32 limit, Read read)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, limit, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        if (!read()) {
            return false;
        }
    }
    return true;
}

// Reads the entries of one record from a region file.
class RecordReader
{
public:
    RecordReader(QDataStream &stream, QcStackReader &stacks, QcBlockEntityRecord &record)
        : m_stream(stream)
        , m_stacks(stacks)
        , m_record(record)
    {
    }

    bool read()
    {
        return readEntries(m_stream, maxPerChunk, [this] { return container(); }) && readEntries(m_stream, maxPerChunk, [this] { return machine(); })
               && readEntries(m_stream, maxPerChunk, [this] { return sapling(); }) && readEntries(m_stream, maxPerChunk, [this] { return memory(); })
               && readEntries(m_stream, maxPerChunk, [this] { return scheduled(); }) && readEntries(m_stream, maxWakes, [this] { return wake(); })
               && readEntries(m_stream, maxWakes, [this] { return signalWake(); }) && readEntries(m_stream, maxWakes, [this] { return update(); });
    }

private:
    // Each position once per kind.
    bool fresh(QSet<QcBlockPos> &seen, QcBlockPos pos)
    {
        if (seen.contains(pos)) {
            return false;
        }
        seen.insert(pos);
        return true;
    }

    bool container()
    {
        QcBlockEntities::Container container;
        quint32 slotCount = 0;
        if (!readPos(m_stream, m_record.chunk, container.pos) || !QcStreamLimits::readCount(m_stream, QcBlockDefinition::maxContainerSlots, slotCount)
            || slotCount == 0) {
            return false;
        }
        container.stacks.resize(slotCount);
        for (QcItemStack &stack : container.stacks) {
            stack = m_stacks.read(m_stream);
        }
        quint32 cooldownLeft = 0;
        m_stream >> cooldownLeft;
        container.readyAt = std::min(cooldownLeft, maxTicks);
        m_record.containers.append(std::move(container));
        return m_stream.status() == QDataStream::Ok && fresh(m_containers, m_record.containers.constLast().pos);
    }

    bool machine()
    {
        QcBlockEntities::Machine machine;
        if (!readPos(m_stream, m_record.chunk, machine.pos)) {
            return false;
        }
        for (QcItemStack &stack : machine.stacks) {
            stack = m_stacks.read(m_stream);
        }
        m_stream >> machine.burnRemaining >> machine.burnSeconds >> machine.progress;
        if (m_stream.status() != QDataStream::Ok || !std::isfinite(machine.burnRemaining) || !std::isfinite(machine.burnSeconds)
            || !std::isfinite(machine.progress)) {
            return false;
        }
        machine.burnSeconds = std::clamp(machine.burnSeconds, 0.0, maxSeconds);
        machine.burnRemaining = std::clamp(machine.burnRemaining, 0.0, machine.burnSeconds);
        machine.progress = std::clamp(machine.progress, 0.0, maxSeconds);
        machine.progressItem = machine.stacks[QcBlockEntities::Machine::inputSlot].item;
        m_record.machines.append(machine);
        return fresh(m_machines, machine.pos);
    }

    bool sapling()
    {
        QcBlockEntities::Sapling sapling;
        quint64 order = 0;
        const bool placed = readPos(m_stream, m_record.chunk, sapling.pos);
        m_stream >> sapling.remaining >> order;
        sapling.order = order;
        if (!placed || m_stream.status() != QDataStream::Ok || !std::isfinite(sapling.remaining) || order >= maxOrder) {
            return false;
        }
        sapling.remaining = std::clamp(sapling.remaining, 0.0, 86400.0);
        m_record.saplings.append(sapling);
        return fresh(m_saplings, sapling.pos);
    }

    bool memory()
    {
        QcBlockEntityRecord::Memory memory;
        quint8 powered = 0;
        quint8 extending = 0;
        quint32 age = 0;
        const bool placed = readPos(m_stream, m_record.chunk, memory.pos);
        m_stream >> memory.burntOutLeft >> powered >> extending >> age;
        memory.burntOutLeft = std::min(memory.burntOutLeft, maxTicks);
        memory.powered = powered != 0;
        if (extending != 0) {
            memory.extendingAge = std::min(age, maxTicks);
        }
        m_record.memories.append(memory);
        return placed && m_stream.status() == QDataStream::Ok && fresh(m_memories, memory.pos);
    }

    bool scheduled()
    {
        QcBlockEntityRecord::Scheduled entry;
        quint8 priority = 0;
        const bool placed = readPos(m_stream, m_record.chunk, entry.pos);
        m_stream >> entry.ticksLeft >> priority;
        entry.ticksLeft = std::min(entry.ticksLeft, maxTicks);
        entry.priority = QcSignalSchedule::Priority(priority);
        m_record.scheduled.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && priority < QcSignalSchedule::priorityCount && fresh(m_scheduled, entry.pos);
    }

    bool wake()
    {
        QcBlockPos pos;
        const bool placed = readPos(m_stream, m_record.chunk, pos);
        m_record.wakes.append(pos);
        return placed;
    }

    bool signalWake()
    {
        QcBlockPos pos;
        const bool placed = readPos(m_stream, m_record.chunk, pos);
        m_record.signalWakes.append(pos);
        return placed;
    }

    bool update()
    {
        QcBlockPos pos;
        const bool placed = readPos(m_stream, m_record.chunk, pos);
        m_record.blockUpdates.append(pos);
        return placed;
    }

    QDataStream &m_stream;
    QcStackReader &m_stacks;
    QcBlockEntityRecord &m_record;
    QSet<QcBlockPos> m_containers;
    QSet<QcBlockPos> m_machines;
    QSet<QcBlockPos> m_saplings;
    QSet<QcBlockPos> m_memories;
    QSet<QcBlockPos> m_scheduled;
};

void writePositions(QDataStream &stream, const QList<QcBlockPos> &positions)
{
    const qsizetype count = std::min<qsizetype>(positions.size(), maxWakes);
    stream << quint32(count);
    for (qsizetype i = 0; i < count; ++i) {
        writePos(stream, positions[i]);
    }
}

} // namespace

namespace QcBlockEntityCodec {

QByteArray encode(const QcBlockEntityRecord &record, const QcContentRegistry &content)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(record.containers.size());
    for (const QcBlockEntities::Container &container : record.containers) {
        writePos(stream, container.pos);
        stream << quint32(container.stacks.size());
        for (const QcItemStack &stack : container.stacks) {
            QcSaveFile::writeStack(stream, stack, content);
        }
        stream << quint32(container.readyAt);
    }
    stream << quint32(record.machines.size());
    for (const QcBlockEntities::Machine &machine : record.machines) {
        writePos(stream, machine.pos);
        for (const QcItemStack &stack : machine.stacks) {
            QcSaveFile::writeStack(stream, stack, content);
        }
        stream << machine.burnRemaining << machine.burnSeconds << machine.progress;
    }
    stream << quint32(record.saplings.size());
    for (const QcBlockEntities::Sapling &sapling : record.saplings) {
        writePos(stream, sapling.pos);
        stream << sapling.remaining << quint64(sapling.order);
    }
    stream << quint32(record.memories.size());
    for (const QcBlockEntityRecord::Memory &memory : record.memories) {
        writePos(stream, memory.pos);
        stream << memory.burntOutLeft << quint8(memory.powered) << quint8(memory.extendingAge.has_value()) << memory.extendingAge.value_or(0);
    }
    stream << quint32(record.scheduled.size());
    for (const QcBlockEntityRecord::Scheduled &entry : record.scheduled) {
        writePos(stream, entry.pos);
        stream << entry.ticksLeft << quint8(entry.priority);
    }
    writePositions(stream, record.wakes);
    writePositions(stream, record.signalWakes);
    writePositions(stream, record.blockUpdates);
    return blob;
}

[[nodiscard]] QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                     QStringList &warnings, QSet<QString> &reported)
{
    QDataStream stream(blob);
    stream.setVersion(QcSaveFile::streamVersion);
    QcBlockEntityRecord record;
    record.chunk = chunk;
    QcStackReader stacks(content, u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z), warnings, &reported);
    if (!RecordReader(stream, stacks, record).read() || stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"damaged block entities of chunk %1, %2"_s.arg(chunk.x).arg(chunk.z));
    }
    return record;
}

} // namespace QcBlockEntityCodec

#include "QcBlockEntityStorage.h"

#include "QcRegionStorage.h"
#include "QcSaveFile.h"
#include "QcStackReader.h"
#include "QcStreamLimits.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView magic("QCBE");
constexpr quint16 version = 1;
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
bool readEntries(QDataStream &stream, quint32 limit, Read read)
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
               && readEntries(m_stream, maxPerChunk, [this] { return scheduled(); }) && readEntries(m_stream, maxWakes, [this] { return wake(); });
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

    QDataStream &m_stream;
    QcStackReader &m_stacks;
    QcBlockEntityRecord &m_record;
    QSet<QcBlockPos> m_containers;
    QSet<QcBlockPos> m_machines;
    QSet<QcBlockPos> m_saplings;
    QSet<QcBlockPos> m_memories;
    QSet<QcBlockPos> m_scheduled;
};

} // namespace

QcBlockEntityStorage::QcBlockEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
    , m_files(std::move(directory), QString::fromLatin1(folder), u"qcb"_s, magic, version)
{
}

void QcBlockEntityStorage::writeRecord(QDataStream &stream, const QcBlockEntityRecord &record) const
{
    stream << qint32(record.chunk.x) << qint32(record.chunk.z);
    stream << quint32(record.containers.size());
    for (const QcBlockEntities::Container &container : record.containers) {
        writePos(stream, container.pos);
        stream << quint32(container.stacks.size());
        for (const QcItemStack &stack : container.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
        }
        stream << quint32(container.readyAt);
    }
    stream << quint32(record.machines.size());
    for (const QcBlockEntities::Machine &machine : record.machines) {
        writePos(stream, machine.pos);
        for (const QcItemStack &stack : machine.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
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
    const qsizetype wakes = std::min<qsizetype>(record.wakes.size(), maxWakes);
    stream << quint32(wakes);
    for (qsizetype i = 0; i < wakes; ++i) {
        writePos(stream, record.wakes[i]);
    }
}

QByteArray QcBlockEntityStorage::payloadOf(const QList<const QcBlockEntityRecord *> &records) const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(records.size());
    for (const QcBlockEntityRecord *record : records) {
        writeRecord(stream, *record);
    }
    return payload;
}

QcStatus QcBlockEntityStorage::write(const QList<QcBlockEntityRecord> &records) const
{
    // By region, each region's chunks in a fixed order, so unchanged contents give the same bytes.
    QHash<QcChunkPos, QList<const QcBlockEntityRecord *>> regions;
    for (const QcBlockEntityRecord &record : records) {
        if (!record.isEmpty()) {
            regions[QcRegionStorage::regionOf(record.chunk)].append(&record);
        }
    }

    QHash<QcChunkPos, QByteArray> payloads;
    for (auto it = regions.begin(); it != regions.end(); ++it) {
        std::ranges::sort(*it, [](const QcBlockEntityRecord *a, const QcBlockEntityRecord *b) {
            return std::pair{a->chunk.x, a->chunk.z} < std::pair{b->chunk.x, b->chunk.z};
        });
        payloads.insert(it.key(), payloadOf(*it));
    }
    return m_files.write(payloads);
}

QcResult<QList<QcBlockEntityRecord>> QcBlockEntityStorage::read(QStringList &warnings) const
{
    const QcResult<QList<std::pair<QString, QByteArray>>> files = m_files.read();
    if (!files) {
        return qcError(files.error());
    }

    QList<QcBlockEntityRecord> records;
    for (const auto &[path, payload] : *files) {
        if (const QcStatus read = readRegion(path, payload, records, warnings); !read) {
            m_files.noteDamaged(path);
            return qcError(read.error());
        }
    }
    return records;
}

QcStatus QcBlockEntityStorage::readRegion(const QString &path, const QByteArray &payload, QList<QcBlockEntityRecord> &records,
                                          QStringList &warnings) const
{
    QDataStream stream(payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, quint32(QcRegionStorage::regionSize * QcRegionStorage::regionSize), count)) {
        return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
    }

    QcStackReader stacks(*m_content, path, warnings);
    std::optional<QcChunkPos> region;
    QSet<QcChunkPos> chunks;
    for (quint32 i = 0; i < count; ++i) {
        QcBlockEntityRecord record;
        qint32 x = 0;
        qint32 z = 0;
        stream >> x >> z;
        record.chunk = {x, z};
        region = region.value_or(QcRegionStorage::regionOf(record.chunk));
        const bool fits = std::abs(qint64(x)) <= QcWorld::maxHorizontalChunk && std::abs(qint64(z)) <= QcWorld::maxHorizontalChunk
                          && m_files.isFileOf(path, *region)
                          && QcRegionStorage::regionOf(record.chunk) == *region && !chunks.contains(record.chunk);
        if (stream.status() != QDataStream::Ok || !fits || !RecordReader(stream, stacks, record).read()) {
            return qcError(u"%1 is damaged (chunk %2)"_s.arg(path).arg(i));
        }
        chunks.insert(record.chunk);
        records.append(std::move(record));
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    if (region) {
        m_files.remember(*region, payload);
    }
    return {};
}

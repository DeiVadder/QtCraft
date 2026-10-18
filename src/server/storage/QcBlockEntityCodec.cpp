#include "server/storage/QcBlockEntityCodec.h"

#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcRegionFile.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Table = QcBlockEntityRecord::Table;

// The version every table and every entry of this version writes (decision E-440).
constexpr quint16 tableVersion = 1;
constexpr quint16 entryVersion = 1;
// Bytes of one table at most: far more than one component of an entity (a chunk full of chests, a
// circuit's long list of waiting positions), but never more than the blob of the whole chunk may
// hold in its region file (encodeWithin keeps every saved blob within it).
constexpr quint32 maxTableLength = QcRegionFile::maxBlobBytes;

// Entries of one kind in one chunk at most: one per cell.
constexpr quint32 maxPerChunk = QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight;
// Kept tails of entries of a newer version per chunk, over all tables; beyond them they are dropped
// with a warning.
constexpr quint32 maxKeptTails = maxPerChunk;
// Positions the circuits wait to look at, per chunk (QcServerLimits::signalMaxPendingChanges at most).
constexpr quint32 maxWakes = 1u << 24;
constexpr double maxSeconds = 100000.0;
// Waits are saved as ticks; longer ones are damaged values and cut to this (QcBlockEntityRecords).
constexpr quint32 maxTicks = 1u << 20;
// Planting orders count up from 0; one near the top could make the next order wrap.
constexpr quint64 maxOrder = quint64(1) << 62;
// Characters of a content name at most (a spawner's kind).
constexpr qsizetype maxNameLength = 256;

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

    // The tables of the record (QcComponentFrames, decision E-440); unknown tables and the tails of
    // newer versions go to the record's kept parts. False for damaged data.
    bool read(int &dropped)
    {
        // The reader of a table of entries, which needs the table's length.
        const auto entriesOf = [this](Table table, bool (RecordReader::*read)()) {
            return [this, table, read](QDataStream &, quint16, quint32 length) { return entries(table, read, length); };
        };
        const auto containers = entriesOf(Table::Containers, &RecordReader::container);
        const auto machines = entriesOf(Table::Machines, &RecordReader::machine);
        const auto saplings = entriesOf(Table::Saplings, &RecordReader::sapling);
        const auto memories = entriesOf(Table::Memories, &RecordReader::memory);
        const auto scheduled = entriesOf(Table::Scheduled, &RecordReader::scheduled);
        const auto wakes = [this](QDataStream &, quint16) { return positions(m_record.wakes); };
        const auto signalWakes = [this](QDataStream &, quint16) { return positions(m_record.signalWakes); };
        const auto updates = [this](QDataStream &, quint16) { return positions(m_record.blockUpdates); };
        const auto fires = entriesOf(Table::Fires, &RecordReader::fire);
        const auto spawners = entriesOf(Table::Spawners, &RecordReader::spawner);
        const auto furnished = [this](QDataStream &, quint16) {
            m_record.furnished = true;
            return true;
        };
        return QcComponentFrames::read(m_stream,
                                       {known(Table::Containers, containers), known(Table::Machines, machines), known(Table::Saplings, saplings),
                                        known(Table::Memories, memories), known(Table::Scheduled, scheduled), known(Table::Wakes, wakes),
                                        known(Table::SignalWakes, signalWakes), known(Table::BlockUpdates, updates), known(Table::Fires, fires),
                                        known(Table::Spawners, spawners), known(Table::Furnished, furnished)},
                                       m_record.kept.tables, dropped, maxTableLength);
    }

    // Tails of entries dropped beyond maxKeptTails.
    int droppedTails() const { return m_droppedTails; }

private:
    static QcComponentFrames::Known known(Table table, QcComponentFrames::FieldReader read) { return {quint16(table), tableVersion, read}; }

    // A table of `length` bytes of entries, each framed as `quint16 version, quint32 length` and read
    // by `read`.
    bool entries(Table table, bool (RecordReader::*read)(), quint32 length)
    {
        const qint64 end = m_stream.device()->pos() + qint64(length);
        return readEntries(m_stream, maxPerChunk, [this, table, read, end] { return entry(table, read, end); });
    }

    // One framed entry, which must end before `end` (its table's end). Of a newer version the bytes
    // after the known fields are kept as the entry's tail (by its position); an entry of this version
    // must hold exactly its fields.
    bool entry(Table table, bool (RecordReader::*read)(), qint64 end)
    {
        quint16 version = 0;
        quint32 length = 0;
        m_stream >> version >> length;
        if (m_stream.status() != QDataStream::Ok || version == 0 || m_stream.device()->pos() + qint64(length) > end) {
            return false;
        }
        const qint64 start = m_stream.device()->pos();
        if (!(this->*read)() || m_stream.status() != QDataStream::Ok) {
            return false;
        }
        const qint64 used = m_stream.device()->pos() - start;
        if (std::cmp_greater(used, length) || (version <= entryVersion && std::cmp_not_equal(used, length))) {
            return false;
        }
        if (version <= entryVersion) {
            return true;
        }
        if (m_tails >= maxKeptTails) {
            ++m_droppedTails;
            return m_stream.skipRawData(int(qint64(length) - used)) == int(qint64(length) - used);
        }
        QcEntityComponent tail{quint16(table), version, true, {}};
        if (!QcComponentFrames::readRaw(m_stream, qint64(length) - used, tail.payload)) {
            return false;
        }
        ++m_tails;
        m_record.kept.entries[m_at].append(std::move(tail));
        return true;
    }

    // A table of positions without frames (they have no fields to add).
    bool positions(QList<QcBlockPos> &list)
    {
        return readEntries(m_stream, maxWakes, [this, &list] {
            QcBlockPos pos;
            const bool placed = readPos(m_stream, m_record.chunk, pos);
            list.append(pos);
            return placed;
        });
    }

    // Reads the position of the entry being read.
    bool at(QcBlockPos &pos)
    {
        const bool placed = readPos(m_stream, m_record.chunk, pos);
        m_at = pos;
        return placed;
    }

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
        if (!at(container.pos) || !QcStreamLimits::readCount(m_stream, QcBlockDefinition::maxContainerSlots, slotCount)
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
        if (!at(machine.pos)) {
            return false;
        }
        for (QcItemStack &stack : machine.stacks) {
            stack = m_stacks.read(m_stream);
        }
        m_stream >> machine.burnRemaining >> machine.burnSeconds >> machine.progress >> machine.experience;
        if (m_stream.status() != QDataStream::Ok || !std::isfinite(machine.burnRemaining) || !std::isfinite(machine.burnSeconds)
            || !std::isfinite(machine.progress) || !std::isfinite(machine.experience)) {
            return false;
        }
        machine.burnSeconds = std::clamp(machine.burnSeconds, 0.0, maxSeconds);
        machine.burnRemaining = std::clamp(machine.burnRemaining, 0.0, machine.burnSeconds);
        machine.progress = std::clamp(machine.progress, 0.0, maxSeconds);
        machine.experience = std::clamp(machine.experience, 0.0, QcBlockEntities::Machine::maxExperience);
        machine.progressItem = machine.stacks[QcBlockEntities::Machine::inputSlot].item;
        m_record.machines.append(machine);
        return fresh(m_machines, machine.pos);
    }

    bool sapling()
    {
        QcBlockEntities::Sapling sapling;
        quint64 order = 0;
        const bool placed = at(sapling.pos);
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
        const bool placed = at(memory.pos);
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
        const bool placed = at(entry.pos);
        m_stream >> entry.ticksLeft >> priority;
        entry.ticksLeft = std::min(entry.ticksLeft, maxTicks);
        entry.priority = QcSignalSchedule::Priority(priority);
        m_record.scheduled.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && priority < QcSignalSchedule::priorityCount && fresh(m_scheduled, entry.pos);
    }

    // A burning fire (decision E-392); its origin may lie in another chunk.
    bool fire()
    {
        QcFireQueue::Saved entry;
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        quint8 age = 0;
        const bool placed = at(entry.pos);
        m_stream >> x >> y >> z >> age >> entry.ticksLeft;
        entry.origin = {x, y, z};
        entry.age = std::min<quint8>(age, QcFireQueue::maxAge);
        entry.ticksLeft = std::min(entry.ticksLeft, maxTicks);
        m_record.fires.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && fresh(m_fires, entry.pos);
    }

    // A monster spawner (decision E-520): its kind by name (any name: an unknown kind waits for its
    // mod), the ticks to its next cycle and whether its creatures stay put.
    bool spawner()
    {
        QcSpawners::Entry entry;
        quint8 confined = 0;
        const bool placed = at(entry.pos);
        m_stream >> entry.mob >> entry.delay >> confined;
        entry.delay = std::min(entry.delay, QcSpawners::maxDelay);
        entry.confined = confined != 0;
        m_record.spawners.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && entry.mob.size() <= maxNameLength && fresh(m_spawners, entry.pos);
    }

    QDataStream &m_stream;
    QcStackReader &m_stacks;
    QcBlockEntityRecord &m_record;
    // The position of the entry being read (its tail is kept by it).
    QcBlockPos m_at;
    QSet<QcBlockPos> m_containers;
    QSet<QcBlockPos> m_machines;
    QSet<QcBlockPos> m_saplings;
    QSet<QcBlockPos> m_memories;
    QSet<QcBlockPos> m_scheduled;
    QSet<QcBlockPos> m_fires;
    QSet<QcBlockPos> m_spawners;
    quint32 m_tails = 0;
    int m_droppedTails = 0;
};

void writePositions(QDataStream &stream, const QList<QcBlockPos> &positions)
{
    const qsizetype count = std::min<qsizetype>(positions.size(), maxWakes);
    stream << quint32(count);
    for (qsizetype i = 0; i < count; ++i) {
        writePos(stream, positions[i]);
    }
}

// Writes the entries of one table into `blob`, each framed as `quint16 version, quint32 length`, its
// fields and its kept tail (written back under the tail's newer version).
class EntryWriter
{
public:
    EntryWriter(QDataStream &stream, const QcBlockEntityKept &kept, Table table)
        : m_stream(stream)
        , m_kept(kept)
        , m_table(quint16(table))
    {
    }

    // `write()` writes the fields of the entry at `pos`.
    template<typename Write>
    void entry(QcBlockPos pos, Write write)
    {
        const QcEntityComponent *tail = m_kept.entryTail(m_table, pos);
        if (tail && tail->version <= entryVersion) {
            tail = nullptr;
        }
        m_stream << (tail ? tail->version : entryVersion) << quint32(0);
        const qint64 start = m_stream.device()->pos();
        write();
        if (tail) {
            m_stream.writeRawData(tail->payload.constData(), int(tail->payload.size()));
        }
        QcComponentFrames::patchLength(m_stream, start - qint64(sizeof(quint32)), quint32(m_stream.device()->pos() - start));
    }

private:
    QDataStream &m_stream;
    const QcBlockEntityKept &m_kept;
    quint16 m_table;
};

// Writes the tables of a record; an empty table is left out unless a newer version's tail of it is kept.
class RecordWriter
{
public:
    RecordWriter(QDataStream &stream, const QcBlockEntityRecord &record, const QcContentRegistry &content)
        : m_stream(stream)
        , m_record(record)
        , m_content(content)
        , m_tables(stream, record.kept.tables)
    {
    }

    void write()
    {
        entries(Table::Containers, m_record.containers, [this](const QcBlockEntities::Container &container) {
            writePos(m_stream, container.pos);
            m_stream << quint32(container.stacks.size());
            for (const QcItemStack &stack : container.stacks) {
                QcSaveFile::writeStack(m_stream, stack, m_content);
            }
            m_stream << quint32(container.readyAt);
        });
        entries(Table::Machines, m_record.machines, [this](const QcBlockEntities::Machine &machine) {
            writePos(m_stream, machine.pos);
            for (const QcItemStack &stack : machine.stacks) {
                QcSaveFile::writeStack(m_stream, stack, m_content);
            }
            m_stream << machine.burnRemaining << machine.burnSeconds << machine.progress << machine.experience;
        });
        entries(Table::Saplings, m_record.saplings, [this](const QcBlockEntities::Sapling &sapling) {
            writePos(m_stream, sapling.pos);
            m_stream << sapling.remaining << quint64(sapling.order);
        });
        entries(Table::Memories, m_record.memories, [this](const QcBlockEntityRecord::Memory &memory) {
            writePos(m_stream, memory.pos);
            m_stream << memory.burntOutLeft << quint8(memory.powered) << quint8(memory.extendingAge.has_value()) << memory.extendingAge.value_or(0);
        });
        entries(Table::Scheduled, m_record.scheduled, [this](const QcBlockEntityRecord::Scheduled &entry) {
            writePos(m_stream, entry.pos);
            m_stream << entry.ticksLeft << quint8(entry.priority);
        });
        positions(Table::Wakes, m_record.wakes);
        positions(Table::SignalWakes, m_record.signalWakes);
        positions(Table::BlockUpdates, m_record.blockUpdates);
        entries(Table::Fires, m_record.fires, [this](const QcFireQueue::Saved &fire) {
            writePos(m_stream, fire.pos);
            writePos(m_stream, fire.origin);
            m_stream << fire.age << fire.ticksLeft;
        });
        entries(Table::Spawners, m_record.spawners, [this](const QcSpawners::Entry &spawner) {
            writePos(m_stream, spawner.pos);
            m_stream << spawner.mob << spawner.delay << quint8(spawner.confined);
        });
        if (m_record.furnished || hasTail(Table::Furnished)) {
            m_tables.add(quint16(Table::Furnished), tableVersion, [](QDataStream &) {});
        }
        m_tables.finish();
    }

private:
    bool hasTail(Table table) const
    {
        return std::ranges::any_of(m_record.kept.tables, [table](const QcEntityComponent &kept) { return kept.tail && kept.id == quint16(table); });
    }

    template<typename Entry, typename Write>
    void entries(Table table, const QList<Entry> &list, Write write)
    {
        if (list.isEmpty() && !hasTail(table)) {
            return;
        }
        m_tables.add(quint16(table), tableVersion, [&](QDataStream &stream) {
            stream << quint32(list.size());
            EntryWriter writer(stream, m_record.kept, table);
            for (const Entry &entry : list) {
                writer.entry(entry.pos, [&] { write(entry); });
            }
        });
    }

    void positions(Table table, const QList<QcBlockPos> &list)
    {
        if (list.isEmpty() && !hasTail(table)) {
            return;
        }
        m_tables.add(quint16(table), tableVersion, [&list](QDataStream &stream) { writePositions(stream, list); });
    }

    QDataStream &m_stream;
    const QcBlockEntityRecord &m_record;
    const QcContentRegistry &m_content;
    QcComponentFrames::Writer m_tables;
};

} // namespace

namespace QcBlockEntityCodec {

QByteArray encode(const QcBlockEntityRecord &record, const QcContentRegistry &content)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    RecordWriter(stream, record, content).write();
    return blob;
}

QcResult<QByteArray> encodeWithin(const QcBlockEntityRecord &record, const QcContentRegistry &content, QStringList &warnings, quint32 limit)
{
    QByteArray blob = encode(record, content);
    if (std::cmp_less_equal(blob.size(), limit)) {
        return blob;
    }

    const QString where = u"chunk %1, %2"_s.arg(record.chunk.x).arg(record.chunk.z);
    const qsizetype size = blob.size();
    if (!record.kept.isEmpty()) {
        QcBlockEntityRecord trimmed = record;
        trimmed.kept = {};
        blob = encode(trimmed, content);
        if (std::cmp_less_equal(blob.size(), limit)) {
            const QString text = u"%1: block entities of %2 bytes exceed %3; what a newer version or a mod stored with them is dropped"_s;
            warnings.append(text.arg(where).arg(size).arg(limit));
            return blob;
        }
    }
    return qcError(u"%1: block entities of %2 bytes exceed %3 and are not saved"_s.arg(where).arg(blob.size()).arg(limit));
}

[[nodiscard]] QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                     QStringList &warnings, QSet<QString> &reported)
{
    QDataStream stream(blob);
    stream.setVersion(QcSaveFile::streamVersion);
    QcBlockEntityRecord record;
    record.chunk = chunk;
    const QString where = u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z);
    QcStackReader stacks(content, where, warnings, &reported);
    int dropped = 0;
    RecordReader reader(stream, stacks, record);
    if (!reader.read(dropped) || stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"damaged block entities of chunk %1, %2"_s.arg(chunk.x).arg(chunk.z));
    }
    if (dropped > 0) {
        const QString text = u"%1: block entities with more than %2 unknown tables; %3 of them dropped"_s;
        warnings.append(text.arg(where).arg(QcComponentFrames::maxKept).arg(dropped));
    }
    if (reader.droppedTails() > 0) {
        const QString text = u"%1: more than %2 entries of a newer version; what %3 of them added is dropped"_s;
        warnings.append(text.arg(where).arg(maxKeptTails).arg(reader.droppedTails()));
    }
    return record;
}

} // namespace QcBlockEntityCodec

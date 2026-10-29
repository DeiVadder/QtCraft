// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcBlockEntityCodec.h"

#include "common/core/QcNameText.h"
#include "common/gameplay/QcAnvil.h"
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
// Signs write version 2: the colour of the text after the lines (decision E-830). Older readers keep
// it as the tail of a newer version.
constexpr quint16 signEntryVersion = 2;
// Containers and machines write version 2: their custom name after the fields (decision E-873).
// Older readers keep it as the tail of a newer version; version 1 entries have no name.
constexpr quint16 namedEntryVersion = 2;

// The version the entries of `table` are written in.
constexpr quint16 entryVersionOf(quint16 table)
{
    switch (static_cast<QcBlockEntityRecord::Table>(table)) {
    case QcBlockEntityRecord::Table::Signs:
        return signEntryVersion;
    case QcBlockEntityRecord::Table::Containers:
    case QcBlockEntityRecord::Table::Machines:
        return namedEntryVersion;
    default:
        return entryVersion;
    }
}
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
constexpr quint64 maxOrder = quint64{1} << 62;
// Characters of a content name at most (a spawner's kind).
constexpr qsizetype maxNameLength = 256;
// UTF-16 units of a saved line of sign text at most (cleaned to QcSignDefinition::maxLineLength
// characters when read).
constexpr qsizetype maxLineUnits = 256;

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << static_cast<qint32>(pos.x) << static_cast<qint32>(pos.y) << static_cast<qint32>(pos.z);
}

// A position inside the chunk of the record being read; std::nullopt for one outside it.
[[nodiscard]] std::optional<QcBlockPos> readPos(QDataStream &stream, QcChunkPos chunk)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    const QcBlockPos pos{x, y, z};
    if (stream.status() != QDataStream::Ok || !pos.isInsideWorldHeight() || pos.chunk() != chunk) {
        return std::nullopt;
    }
    return pos;
}

// Positions of one kind of entry read so far: each position once per kind.
class SeenPositions
{
public:
    // False when `pos` was seen before.
    [[nodiscard]] bool add(QcBlockPos pos)
    {
        const qsizetype before = m_seen.size();
        m_seen.insert(pos);
        return m_seen.size() > before;
    }

private:
    QSet<QcBlockPos> m_seen;
};

// Reads a list of `count` entries with `read`, which fails on damaged data; positions must not repeat.
template<typename Read>
[[nodiscard]] bool readEntries(QDataStream &stream, quint32 limit, Read read)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, limit);
    if (!countRead) {
        return false;
    }
    const quint32 count = *countRead;
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
    // newer versions go to the record's kept parts. The number of unknown tables dropped beyond
    // QcComponentFrames::maxKept; nullopt for damaged data.
    [[nodiscard]] std::optional<int> read()
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
        const auto wakes = [this](QDataStream &, quint16) {
            const std::optional<QList<QcBlockPos>> read = positions();
            m_record.wakes = read.value_or(QList<QcBlockPos>());
            return read.has_value();
        };
        const auto signalWakes = [this](QDataStream &, quint16) {
            const std::optional<QList<QcBlockPos>> read = positions();
            m_record.signalWakes = read.value_or(QList<QcBlockPos>());
            return read.has_value();
        };
        const auto updates = [this](QDataStream &, quint16) {
            const std::optional<QList<QcBlockPos>> read = positions();
            m_record.blockUpdates = read.value_or(QList<QcBlockPos>());
            return read.has_value();
        };
        const auto fires = entriesOf(Table::Fires, &RecordReader::fire);
        const auto spawners = entriesOf(Table::Spawners, &RecordReader::spawner);
        const auto campfires = entriesOf(Table::Campfires, &RecordReader::campfire);
        const auto signs = entriesOf(Table::Signs, &RecordReader::sign);
        const auto furnished = [this](QDataStream &, quint16) {
            m_record.furnished = true;
            return true;
        };
        std::optional<QcComponentFrames::ReadComponents> tables = QcComponentFrames::read(
            m_stream,
            {known(Table::Containers, containers), known(Table::Machines, machines), known(Table::Saplings, saplings),
             known(Table::Memories, memories), known(Table::Scheduled, scheduled), known(Table::Wakes, wakes),
             known(Table::SignalWakes, signalWakes), known(Table::BlockUpdates, updates), known(Table::Fires, fires),
             known(Table::Spawners, spawners), known(Table::Furnished, furnished), known(Table::Campfires, campfires),
             known(Table::Signs, signs)},
            maxTableLength);
        if (!tables) {
            return std::nullopt;
        }
        m_record.kept.tables.append(std::move(tables->kept));
        return tables->dropped;
    }

    // Tails of entries dropped beyond maxKeptTails.
    int droppedTails() const { return m_droppedTails; }

private:
    static QcComponentFrames::Known known(Table table, QcComponentFrames::FieldReader read) { return {static_cast<quint16>(table), tableVersion, read}; }

    // A table of `length` bytes of entries, each framed as `quint16 version, quint32 length` and read
    // by `read`.
    bool entries(Table table, bool (RecordReader::*read)(), quint32 length)
    {
        const qint64 end = m_stream.device()->pos() + static_cast<qint64>(length);
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
        if (m_stream.status() != QDataStream::Ok || version == 0 || m_stream.device()->pos() + static_cast<qint64>(length) > end) {
            return false;
        }
        const qint64 start = m_stream.device()->pos();
        m_version = version;
        if (!(this->*read)() || m_stream.status() != QDataStream::Ok) {
            return false;
        }
        const qint64 used = m_stream.device()->pos() - start;
        const quint16 known = entryVersionOf(static_cast<quint16>(table));
        if (std::cmp_greater(used, length) || (version <= known && std::cmp_not_equal(used, length))) {
            return false;
        }
        if (version <= known) {
            return true;
        }
        if (m_tails >= maxKeptTails) {
            ++m_droppedTails;
            return m_stream.skipRawData(static_cast<int>(static_cast<qint64>(length) - used)) == static_cast<int>(static_cast<qint64>(length) - used);
        }
        std::optional<QByteArray> payload = QcComponentFrames::readRaw(m_stream, static_cast<qint64>(length) - used);
        if (!payload) {
            return false;
        }
        ++m_tails;
        m_record.kept.entries[m_at].append(QcEntityComponent{static_cast<quint16>(table), version, true, std::move(*payload)});
        return true;
    }

    // A table of positions without frames (they have no fields to add); std::nullopt for a damaged one.
    [[nodiscard]] std::optional<QList<QcBlockPos>> positions()
    {
        QList<QcBlockPos> list;
        const bool read = readEntries(m_stream, maxWakes, [this, &list] {
            const std::optional<QcBlockPos> pos = readPos(m_stream, m_record.chunk);
            if (pos) {
                list.append(*pos);
            }
            return pos.has_value();
        });
        return read ? std::optional(std::move(list)) : std::nullopt;
    }

    // The position of the entry being read (kept for its tail); std::nullopt outside the record's chunk.
    [[nodiscard]] std::optional<QcBlockPos> at()
    {
        const std::optional<QcBlockPos> pos = readPos(m_stream, m_record.chunk);
        if (pos) {
            m_at = *pos;
        }
        return pos;
    }

    bool container()
    {
        QcBlockEntities::Container container;
        const std::optional<QcBlockPos> pos = at();
        if (!pos) {
            return false;
        }
        container.pos = *pos;
        const std::optional<quint32> slotCount = QcStreamLimits::readCount(m_stream, QcBlockDefinition::maxContainerSlots);
        if (!slotCount || *slotCount == 0) {
            return false;
        }
        container.stacks.resize(*slotCount);
        for (QcItemStack &stack : container.stacks) {
            stack = m_stacks.read(m_stream);
        }
        quint32 cooldownLeft = 0;
        m_stream >> cooldownLeft;
        container.readyAt = std::min(cooldownLeft, maxTicks);
        const std::optional<QString> name = readName();
        if (!name) {
            return false;
        }
        container.name = *name;
        m_record.containers.append(std::move(container));
        return m_stream.status() == QDataStream::Ok && m_containers.add(m_record.containers.constLast().pos);
    }

    // The custom name of a container or machine (decision E-873) from version 2 on, cleaned like
    // a name written at the anvil (empty before version 2); std::nullopt when it is damaged.
    std::optional<QString> readName()
    {
        if (m_version < namedEntryVersion) {
            return QString();
        }
        // QDataStream reads a string in steps, so a damaged length fails at the end of the entry's
        // frame without allocating it; the bound is checked before cleaning.
        QString text;
        m_stream >> text;
        if (m_stream.status() != QDataStream::Ok || text.size() > maxLineUnits) {
            return std::nullopt;
        }
        return QcAnvil::nameFrom(text);
    }

    bool machine()
    {
        QcBlockEntities::Machine machine;
        const std::optional<QcBlockPos> pos = at();
        if (!pos) {
            return false;
        }
        machine.pos = *pos;
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
        const std::optional<QString> name = readName();
        if (!name) {
            return false;
        }
        machine.name = *name;
        m_record.machines.append(machine);
        return m_machines.add(machine.pos);
    }

    bool sapling()
    {
        QcBlockEntities::Sapling sapling;
        quint64 order = 0;
        const std::optional<QcBlockPos> placed = at();
        sapling.pos = placed.value_or(QcBlockPos());
        m_stream >> sapling.remaining >> order;
        sapling.order = order;
        if (!placed || m_stream.status() != QDataStream::Ok || !std::isfinite(sapling.remaining) || order >= maxOrder) {
            return false;
        }
        sapling.remaining = std::clamp(sapling.remaining, 0.0, 86400.0);
        m_record.saplings.append(sapling);
        return m_saplings.add(sapling.pos);
    }

    bool memory()
    {
        QcBlockEntityRecord::Memory memory;
        quint8 powered = 0;
        quint8 extending = 0;
        quint32 age = 0;
        const std::optional<QcBlockPos> placed = at();
        memory.pos = placed.value_or(QcBlockPos());
        m_stream >> memory.burntOutLeft >> powered >> extending >> age;
        memory.burntOutLeft = std::min(memory.burntOutLeft, maxTicks);
        memory.powered = powered != 0;
        if (extending != 0) {
            memory.extendingAge = std::min(age, maxTicks);
        }
        m_record.memories.append(memory);
        return placed && m_stream.status() == QDataStream::Ok && m_memories.add(memory.pos);
    }

    bool scheduled()
    {
        QcBlockEntityRecord::Scheduled entry;
        quint8 priority = 0;
        const std::optional<QcBlockPos> placed = at();
        entry.pos = placed.value_or(QcBlockPos());
        m_stream >> entry.ticksLeft >> priority;
        entry.ticksLeft = std::min(entry.ticksLeft, maxTicks);
        entry.priority = static_cast<QcSignalSchedule::Priority>(priority);
        m_record.scheduled.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && priority < QcSignalSchedule::priorityCount && m_scheduled.add(entry.pos);
    }

    // A burning fire (decision E-392); its origin may lie in another chunk.
    bool fire()
    {
        QcFireQueue::Saved entry;
        qint32 x = 0;
        qint32 y = 0;
        qint32 z = 0;
        quint8 age = 0;
        const std::optional<QcBlockPos> placed = at();
        entry.pos = placed.value_or(QcBlockPos());
        m_stream >> x >> y >> z >> age >> entry.ticksLeft;
        entry.origin = {x, y, z};
        entry.age = std::min<quint8>(age, QcFireQueue::maxAge);
        entry.ticksLeft = std::min(entry.ticksLeft, maxTicks);
        m_record.fires.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && m_fires.add(entry.pos);
    }

    // A monster spawner (decision E-520): its kind by name (any name: an unknown kind waits for its
    // mod), the ticks to its next cycle and whether its creatures stay put.
    bool spawner()
    {
        QcSpawners::Entry entry;
        quint8 confined = 0;
        const std::optional<QcBlockPos> placed = at();
        entry.pos = placed.value_or(QcBlockPos());
        m_stream >> entry.mob >> entry.delay >> confined;
        entry.delay = std::min(entry.delay, QcSpawners::maxDelay);
        entry.confined = confined != 0;
        m_record.spawners.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && entry.mob.size() <= maxNameLength && m_spawners.add(entry.pos);
    }

    // The items on a campfire (decision E-772), each with the seconds it has cooked.
    bool campfire()
    {
        QcCampfires::Entry entry;
        const std::optional<QcBlockPos> placed = at();
        entry.pos = placed.value_or(QcBlockPos());
        for (QcCampfires::Slot &slot : entry.places) {
            slot.stack = m_stacks.read(m_stream);
            m_stream >> slot.cooked;
            slot.cooked = std::isfinite(slot.cooked) ? std::clamp(slot.cooked, 0.0, maxSeconds) : 0.0;
        }
        m_record.campfires.append(entry);
        return placed && m_stream.status() == QDataStream::Ok && m_campfires.add(entry.pos);
    }

    // A sign's text (decision E-766): its lines, each cleaned like a player's edit (control
    // characters gone, at most QcSignDefinition::maxLineLength characters; QcSignTexts::restore
    // cleans them again to the block's own line count and length). Like a damaged spawner, a
    // damaged sign (more than QcSignDefinition::maxLines lines, a line longer than maxLineUnits)
    // fails the whole record: the chunk's block entities are reported damaged.
    bool sign()
    {
        QcSignTexts::Entry entry;
        const std::optional<QcBlockPos> placed = at();
        entry.pos = placed.value_or(QcBlockPos());
        quint8 count = 0;
        m_stream >> count;
        if (!placed || m_stream.status() != QDataStream::Ok || count > QcSignDefinition::maxLines) {
            return false;
        }

        for (quint8 line = 0; line < count; ++line) {
            // QDataStream reads a string in steps, so a damaged length fails at the end of the
            // entry's frame without allocating it; the bound is checked before cleaning.
            QString text;
            m_stream >> text;
            if (m_stream.status() != QDataStream::Ok || text.size() > maxLineUnits) {
                return false;
            }
            entry.lines[line] = QcNameText::clean(text, QcSignDefinition::maxLineLength);
        }
        // The colour (decision E-830) from version 2 on; restore() turns an unknown one into the
        // default.
        if (m_version >= signEntryVersion) {
            m_stream >> entry.color;
            if (m_stream.status() != QDataStream::Ok || entry.color.size() > QcSignText::maxColorUnits) {
                return false;
            }
        }
        m_record.signs.append(entry);
        return m_signs.add(entry.pos);
    }

    QDataStream &m_stream;
    QcStackReader &m_stacks;
    QcBlockEntityRecord &m_record;
    // The position and version of the entry being read (its tail is kept by its position).
    QcBlockPos m_at;
    quint16 m_version = 0;
    SeenPositions m_containers;
    SeenPositions m_machines;
    SeenPositions m_saplings;
    SeenPositions m_memories;
    SeenPositions m_scheduled;
    SeenPositions m_fires;
    SeenPositions m_spawners;
    SeenPositions m_campfires;
    SeenPositions m_signs;
    quint32 m_tails = 0;
    int m_droppedTails = 0;
};

void writePositions(QDataStream &stream, const QList<QcBlockPos> &positions)
{
    const qsizetype count = std::min<qsizetype>(positions.size(), maxWakes);
    stream << static_cast<quint32>(count);
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
        , m_table(static_cast<quint16>(table))
    {
    }

    // `write()` writes the fields of the entry at `pos`.
    template<typename Write>
    void entry(QcBlockPos pos, Write write)
    {
        const QcEntityComponent *tail = m_kept.entryTail(m_table, pos);
        const quint16 version = entryVersionOf(m_table);
        if (tail && tail->version <= version) {
            tail = nullptr;
        }
        m_stream << (tail ? tail->version : version) << quint32{0};
        const qint64 start = m_stream.device()->pos();
        write();
        if (tail) {
            m_stream.writeRawData(tail->payload.constData(), static_cast<int>(tail->payload.size()));
        }
        QcComponentFrames::patchLength(m_stream, start - static_cast<qint64>(sizeof(quint32)), static_cast<quint32>(m_stream.device()->pos() - start));
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
            m_stream << static_cast<quint32>(container.stacks.size());
            for (const QcItemStack &stack : container.stacks) {
                QcSaveFile::writeStack(m_stream, stack, m_content);
            }
            m_stream << static_cast<quint32>(container.readyAt) << container.name;
        });
        entries(Table::Machines, m_record.machines, [this](const QcBlockEntities::Machine &machine) {
            writePos(m_stream, machine.pos);
            for (const QcItemStack &stack : machine.stacks) {
                QcSaveFile::writeStack(m_stream, stack, m_content);
            }
            m_stream << machine.burnRemaining << machine.burnSeconds << machine.progress << machine.experience << machine.name;
        });
        entries(Table::Saplings, m_record.saplings, [this](const QcBlockEntities::Sapling &sapling) {
            writePos(m_stream, sapling.pos);
            m_stream << sapling.remaining << sapling.order;
        });
        entries(Table::Memories, m_record.memories, [this](const QcBlockEntityRecord::Memory &memory) {
            writePos(m_stream, memory.pos);
            m_stream << memory.burntOutLeft << static_cast<quint8>(memory.powered) << static_cast<quint8>(memory.extendingAge.has_value())
                << memory.extendingAge.value_or(0);
        });
        entries(Table::Scheduled, m_record.scheduled, [this](const QcBlockEntityRecord::Scheduled &entry) {
            writePos(m_stream, entry.pos);
            m_stream << entry.ticksLeft << static_cast<quint8>(entry.priority);
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
            m_stream << spawner.mob << spawner.delay << static_cast<quint8>(spawner.confined);
        });
        entries(Table::Campfires, m_record.campfires, [this](const QcCampfires::Entry &campfire) {
            writePos(m_stream, campfire.pos);
            for (const QcCampfires::Slot &slot : campfire.places) {
                QcSaveFile::writeStack(m_stream, slot.stack, m_content);
                m_stream << slot.cooked;
            }
        });
        entries(Table::Signs, m_record.signs, [this](const QcSignTexts::Entry &sign) {
            writePos(m_stream, sign.pos);
            m_stream << static_cast<quint8>(sign.lines.size());
            for (const QString &line : sign.lines) {
                m_stream << line;
            }
            m_stream << sign.color;
        });
        if (m_record.furnished || hasTail(Table::Furnished)) {
            m_tables.add(static_cast<quint16>(Table::Furnished), tableVersion, [](QDataStream &) {});
        }
        m_tables.finish();
    }

private:
    bool hasTail(Table table) const
    {
        return std::ranges::any_of(m_record.kept.tables,
            [table](const QcEntityComponent &kept) { return kept.tail && kept.id == static_cast<quint16>(table); });
    }

    template<typename Entry, typename Write>
    void entries(Table table, const QList<Entry> &list, Write write)
    {
        if (list.isEmpty() && !hasTail(table)) {
            return;
        }
        m_tables.add(static_cast<quint16>(table), tableVersion, [&](QDataStream &stream) {
            stream << static_cast<quint32>(list.size());
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
        m_tables.add(static_cast<quint16>(table), tableVersion, [&list](QDataStream &stream) { writePositions(stream, list); });
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

QcResult<Fitted> encodeWithin(const QcBlockEntityRecord &record, const QcContentRegistry &content, quint32 limit)
{
    QByteArray blob = encode(record, content);
    if (std::cmp_less_equal(blob.size(), limit)) {
        return Fitted{std::move(blob), std::nullopt};
    }

    const QString where = u"chunk %1, %2"_s.arg(record.chunk.x).arg(record.chunk.z);
    const qsizetype size = blob.size();
    if (!record.kept.isEmpty()) {
        QcBlockEntityRecord trimmed = record;
        trimmed.kept = {};
        blob = encode(trimmed, content);
        if (std::cmp_less_equal(blob.size(), limit)) {
            const QString text = u"%1: block entities of %2 bytes exceed %3; what a newer version or a mod stored with them is dropped"_s;
            return Fitted{std::move(blob), text.arg(where).arg(size).arg(limit)};
        }
    }
    return qcError(u"%1: block entities of %2 bytes exceed %3 and are not saved"_s.arg(where).arg(blob.size()).arg(limit));
}

[[nodiscard]] QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                                   QcLoadWarnings &warnings)
{
    QDataStream stream(blob);
    stream.setVersion(QcSaveFile::streamVersion);
    QcBlockEntityRecord record;
    record.chunk = chunk;
    const QString where = u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z);
    QcStackReader stacks(content, where, warnings);
    RecordReader reader(stream, stacks, record);
    const std::optional<int> dropped = reader.read();
    if (!dropped || stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"damaged block entities of chunk %1, %2"_s.arg(chunk.x).arg(chunk.z));
    }
    if (*dropped > 0) {
        const QString text = u"%1: block entities with more than %2 unknown tables; %3 of them dropped"_s;
        warnings.add(text.arg(where).arg(QcComponentFrames::maxKept).arg(*dropped));
    }
    if (reader.droppedTails() > 0) {
        const QString text = u"%1: more than %2 entries of a newer version; what %3 of them added is dropped"_s;
        warnings.add(text.arg(where).arg(maxKeptTails).arg(reader.droppedTails()));
    }
    return record;
}

} // namespace QcBlockEntityCodec

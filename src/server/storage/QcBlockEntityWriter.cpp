// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcBlockEntityWriter.h"

#include "server/storage/QcBlockEntityFormat.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcSaveFile.h"

#include <QIODevice>

#include <algorithm>

namespace {

using Table = QcBlockEntityRecord::Table;

using namespace QcBlockEntityFormat;

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << static_cast<qint32>(pos.x) << static_cast<qint32>(pos.y) << static_cast<qint32>(pos.z);
}

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
        entries(Table::Machines, m_record.machines, [this](const QcBlockEntities::Machine &machine) { writeMachine(machine); });
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
        entries(Table::Beacons, m_record.beacons, [this](const QcBeacons::Saved &beacon) {
            writePos(m_stream, beacon.pos);
            m_stream << beacon.primary << beacon.secondary;
        });
        if (m_record.furnished || hasTail(Table::Furnished)) {
            m_tables.add(static_cast<quint16>(Table::Furnished), tableVersion, [](QDataStream &) {});
        }
        m_tables.finish();
    }

private:
    void writeMachine(const QcBlockEntities::Machine &machine)
    {
        writePos(m_stream, machine.pos);
        const QList<QcItemStack> extra = machine.stacks.mid(QcBlockEntities::Machine::slotCount);
        const QList<QcItemStack> furnaceSlots = machine.stacks.first(QcBlockEntities::Machine::slotCount);
        for (const QcItemStack &stack : furnaceSlots) {
            QcSaveFile::writeStack(m_stream, stack, m_content);
        }
        m_stream << machine.burnRemaining << machine.burnSeconds << machine.progress << machine.experience << machine.name;
        m_stream << static_cast<quint8>(extra.size());
        for (const QcItemStack &stack : extra) {
            QcSaveFile::writeStack(m_stream, stack, m_content);
        }
    }

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

namespace QcBlockEntityWriter {

void write(QDataStream &stream, const QcBlockEntityRecord &record, const QcContentRegistry &content)
{
    RecordWriter(stream, record, content).write();
}

} // namespace QcBlockEntityWriter

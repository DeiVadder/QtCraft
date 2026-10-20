#include "QcServerTestFixture.h"
#include "QcStoredWorld.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcHopperQueue.h"
#include "server/storage/QcBlockEntityRecords.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcSaveFile.h"

#include <QRegularExpression>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

using Table = QcBlockEntityRecord::Table;

// The bytes `write(QDataStream &)` writes, as the codec's streams do.
template<typename Write>
QByteArray bytes(Write write)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    write(stream);
    return blob;
}

// One frame of a table (`id`) or, with `id` 0, of an entry: `[id,] version, length, payload`.
void frame(QDataStream &out, quint16 id, quint16 version, const QByteArray &payload, std::optional<quint32> length = std::nullopt)
{
    if (id != 0) {
        out << id;
    }
    out << version << length.value_or(quint32(payload.size()));
    out.writeRawData(payload.constData(), int(payload.size()));
}

// The fields of a sapling entry of version 1: position, seconds left, planting order.
QByteArray saplingFields(QcBlockPos pos)
{
    return bytes([pos](QDataStream &out) { out << qint32(pos.x) << qint32(pos.y) << qint32(pos.z) << 12.5 << quint64(7); });
}

// What can be wrong with a chunk's block entities.
enum class Damage {
    None,
    TableBeyondData,
    TableOf4GB,
    TableVersionZero,
    TableTwice,
    TableOfThisVersionTooLong,
    TooManyTables,
    EntryBeyondData,
    EntryBeyondTable,
    EntryVersionZero,
    EntryOfThisVersionTooLong,
    EntryTooShort,
};

// The block entities of a chunk as a later version (or a mod) would write them: a sapling table of
// version 2 with one entry of version 2 (fields of version 1, then "xyz") and the table's own new
// field ("TT"), and a table of an id this version does not know; or one of them damaged.
QByteArray futureBlob(QcBlockPos sapling, Damage damage)
{
    QByteArray entry = saplingFields(sapling);
    quint16 entryVersion = 2;
    std::optional<quint32> entryLength;
    switch (damage) {
    case Damage::EntryBeyondData:
        entryLength = 1u << 30;
        break;
    case Damage::EntryBeyondTable:
        // Into the table that follows: within the data, beyond its own table.
        entryLength = quint32(entry.size()) + 3 + 2 + 1;
        break;
    case Damage::EntryVersionZero:
        entryVersion = 0;
        break;
    case Damage::EntryOfThisVersionTooLong:
        entryVersion = 1;
        break;
    case Damage::EntryTooShort:
        entryLength = 10;
        break;
    default:
        break;
    }
    entry += "xyz";
    const QByteArray table = bytes([&](QDataStream &out) {
        out << quint32(1);
        frame(out, 0, entryVersion, entry, entryLength);
        out.writeRawData("TT", 2);
    });

    return bytes([&](QDataStream &out) {
        const quint16 count = damage == Damage::TooManyTables ? 65 : damage == Damage::TableTwice ? 3 : 2;
        out << count;
        const quint16 tableVersion = damage == Damage::TableVersionZero ? 0 : damage == Damage::TableOfThisVersionTooLong ? 1 : 2;
        std::optional<quint32> tableLength;
        if (damage == Damage::TableBeyondData) {
            tableLength = quint32(table.size() + 100);
        } else if (damage == Damage::TableOf4GB) {
            tableLength = 0xffffffffu;
        }
        frame(out, quint16(Table::Saplings), tableVersion, table, tableLength);
        if (damage == Damage::TableTwice) {
            frame(out, quint16(Table::Saplings), 2, table);
        }
        frame(out, 77, 5, "mod");
        for (quint16 i = 2; i < count && damage == Damage::TooManyTables; ++i) {
            frame(out, quint16(100 + i), 1, "x");
        }
    });
}

} // namespace

// Block entities saved as versioned tables of framed entries (package 37.R2b, decision E-440):
// unknown tables and what a newer version appends to a table or an entry come back unchanged,
// through the codec and through a running server; damaged frames are refused without crashing.
class TestBlockEntityComponents : public QcServerTestFixture
{
    Q_OBJECT

private:
    static QcEntityComponent tail(Table table, quint16 version, const QByteArray &payload)
    {
        return {quint16(table), version, true, payload};
    }

    QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, QcLoadWarnings &warnings) const
    {
        return QcBlockEntityCodec::decode(blob, chunk, *m_content, warnings);
    }

    // The stored record of `chunk` of the world in `directory` (empty when there is none).
    QcBlockEntityRecord storedRecord(const QString &directory, QcChunkPos chunk) const
    {
        const QcResult<QcStoredWorld> stored = QcStoredWorld::read(directory, *m_content);
        if (!stored) {
            qWarning() << stored.error();
            return {};
        }
        const auto it = std::ranges::find(stored->blockEntities, chunk, &QcBlockEntityRecord::chunk);
        return it != stored->blockEntities.end() ? *it : QcBlockEntityRecord{};
    }

    // Replaces the stored block entities of `record.chunk`, keeping the chunk's circuit marks.
    bool store(const QString &directory, const QcBlockEntityRecord &record) const
    {
        QcChunkStore store(directory);
        if (!store.open()) {
            return false;
        }
        const QcResult<QcChunkStore::Blobs> blobs = store.read(record.chunk);
        const quint16 marks = blobs ? blobs->marks : 0;
        store.put(QcChunkStore::Kind::BlockEntities, record.chunk, QcBlockEntityCodec::encode(record, *m_content), marks);
        return store.commit({}).has_value();
    }

private slots:
    void aFutureVersionComesBackUnchanged()
    {
        const QcBlockPos pos{3, 64, 5};
        const QByteArray blob = futureBlob(pos, Damage::None);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = decode(blob, {0, 0}, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QVERIFY(warnings.texts().isEmpty());

        // The fields this version knows are read, the rest is kept.
        QCOMPARE(read->saplings.size(), 1);
        QCOMPARE(read->saplings[0].pos, pos);
        QCOMPARE(read->saplings[0].remaining, 12.5);
        QCOMPARE(read->saplings[0].order, quint64(7));
        const QcEntityComponents tables{{77, 5, false, "mod"}, tail(Table::Saplings, 2, "TT")};
        QCOMPARE(read->kept.tables.size(), 2);
        for (const QcEntityComponent &table : tables) {
            QVERIFY(read->kept.tables.contains(table));
        }
        QCOMPARE(read->kept.entries.size(), 1);
        QCOMPARE(read->kept.entries.value(pos), QcEntityComponents{tail(Table::Saplings, 2, "xyz")});

        // Written again byte for byte, even after the known fields changed.
        QCOMPARE(QcBlockEntityCodec::encode(*read, *m_content), blob);
        QcBlockEntityRecord changed = *read;
        changed.saplings[0].remaining = 3.0;
        const QcResult<QcBlockEntityRecord> again = decode(QcBlockEntityCodec::encode(changed, *m_content), {0, 0}, warnings);
        QVERIFY(again.has_value());
        QCOMPARE(again->saplings[0].remaining, 3.0);
        QCOMPARE(again->kept, read->kept);
    }

    void damagedFramesAreRefused_data()
    {
        QTest::addColumn<Damage>("damage");
        QTest::newRow("table longer than the data") << Damage::TableBeyondData;
        QTest::newRow("table of 4 GB") << Damage::TableOf4GB;
        QTest::newRow("table of version 0") << Damage::TableVersionZero;
        QTest::newRow("table twice") << Damage::TableTwice;
        QTest::newRow("table of this version with more than its fields") << Damage::TableOfThisVersionTooLong;
        QTest::newRow("65 tables") << Damage::TooManyTables;
        QTest::newRow("entry longer than the data") << Damage::EntryBeyondData;
        QTest::newRow("entry longer than its table") << Damage::EntryBeyondTable;
        QTest::newRow("entry of version 0") << Damage::EntryVersionZero;
        QTest::newRow("entry of this version with more than its fields") << Damage::EntryOfThisVersionTooLong;
        QTest::newRow("entry shorter than its fields") << Damage::EntryTooShort;
    }

    void damagedFramesAreRefused()
    {
        QFETCH(Damage, damage);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = decode(futureBlob({3, 64, 5}, damage), {0, 0}, warnings);
        QVERIFY(!read.has_value());
        QVERIFY(read.error().contains(u"damaged block entities of chunk 0, 0"_s));

        // Cut anywhere, the undamaged blob never crashes and never passes as complete.
        const QByteArray blob = futureBlob({3, 64, 5}, Damage::None);
        for (qsizetype size = 0; size < blob.size(); ++size) {
            QVERIFY(!decode(blob.left(size), {0, 0}, warnings).has_value());
        }
    }

    void unknownTablesBeyondWhatAChunkKeepsAreDropped()
    {
        const QByteArray blob = bytes([](QDataStream &out) {
            out << quint16(QcComponentFrames::maxComponents);
            for (quint16 i = 0; i < QcComponentFrames::maxComponents; ++i) {
                frame(out, quint16(1000 + i), 1, "mod");
            }
        });
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = decode(blob, {1, -1}, warnings);
        QVERIFY(read.has_value());
        QCOMPARE(read->kept.tables.size(), qsizetype(QcComponentFrames::maxKept));
        QCOMPARE(warnings.texts().size(), 1);
        QVERIFY2(warnings.texts()[0].contains(u"16 of them dropped"_s), qPrintable(warnings.texts()[0]));

        // What is kept, written again together with every known table, stays readable.
        QcBlockEntityRecord record = *read;
        record.saplings = {{{17, 64, -3}, 2.0, 1}};
        record.blockUpdates = {{18, 64, -4}};
        record.fires = {{{19, 70, -5}, {19, 70, -5}, 1, 20}};
        const QcResult<QcBlockEntityRecord> again = decode(QcBlockEntityCodec::encode(record, *m_content), {1, -1}, warnings);
        QVERIFY(again.has_value());
        QCOMPARE(again->kept, record.kept);
        QCOMPARE(again->fires, record.fires);
    }

    void emptyTablesWithTailsComeBackUnchanged()
    {
        // A sapling table of version 2 without entries and a positions table of version 2, each
        // with what the newer version appended after its list.
        const QcBlockPos wake{1, 70, -2};
        const QByteArray blob = bytes([wake](QDataStream &out) {
            out << quint16(2);
            frame(out, quint16(Table::Saplings), 2, bytes([](QDataStream &table) {
                      table << quint32(0);
                      table.writeRawData("TT", 2);
                  }));
            frame(out, quint16(Table::Wakes), 2, bytes([wake](QDataStream &table) {
                      table << quint32(1) << qint32(wake.x) << qint32(wake.y) << qint32(wake.z);
                      table.writeRawData("WW", 2);
                  }));
        });
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = decode(blob, {0, -1}, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QVERIFY(read->saplings.isEmpty());
        QCOMPARE(read->wakes, QList<QcBlockPos>{wake});
        QCOMPARE(read->kept.tables.size(), 2);
        QVERIFY(read->kept.tables.contains(tail(Table::Saplings, 2, "TT")));
        QVERIFY(read->kept.tables.contains(tail(Table::Wakes, 2, "WW")));
        QVERIFY(!read->isEmpty());
        QCOMPARE(QcBlockEntityCodec::encode(*read, *m_content), blob);
    }

    void tailsBeyondWhatAChunkKeepsAreDropped()
    {
        // Every cell of a chunk a sapling of version 2 with a tail, and one memory more.
        const auto entry = [](QDataStream &out, const QByteArray &fields) {
            frame(out, 0, 2, fields + "t");
        };
        const QByteArray saplings = bytes([&entry](QDataStream &out) {
            out << quint32(QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight);
            for (int y = 0; y < QcWorld::worldHeight; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        entry(out, saplingFields({x, y, z}));
                    }
                }
            }
        });
        const QByteArray memories = bytes([&entry](QDataStream &out) {
            out << quint32(1);
            // Position, burnout ticks left, powered, extending, age.
            entry(out, bytes([](QDataStream &fields) {
                      fields << qint32(1) << qint32(2) << qint32(3) << quint32(0) << quint8(0) << quint8(0) << quint32(0);
                  }));
        });
        const QByteArray blob = bytes([&](QDataStream &out) {
            out << quint16(2);
            frame(out, quint16(Table::Saplings), 1, saplings);
            frame(out, quint16(Table::Memories), 1, memories);
        });
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = decode(blob, {0, 0}, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->memories.size(), 1);
        QCOMPARE(read->kept.entries.size(), qsizetype(QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight));
        // The memory's tail came last and was dropped; the sapling's at its cell is kept.
        QCOMPARE(read->kept.entries.value({1, 2, 3}), QcEntityComponents{tail(Table::Saplings, 2, "t")});
        QCOMPARE(warnings.texts().size(), 1);
        QVERIFY2(warnings.texts()[0].contains(u"what 1 of them added is dropped"_s), qPrintable(warnings.texts()[0]));
    }

    void blobsBeyondTheLimitAreNeverWritten()
    {
        // A region file refuses its whole index over a blob beyond QcRegionFile::maxBlobBytes, so
        // the codec keeps every blob within its limit: first by dropping the kept parts, else not at all.
        QcBlockEntityRecord record;
        record.chunk = {2, 3};
        record.saplings = {{{33, 64, 49}, 2.0, 1}};
        const QByteArray plain = QcBlockEntityCodec::encode(record, *m_content);
        record.kept.tables = {{4711, 1, false, QByteArray(500, 'm')}};
        const QByteArray full = QcBlockEntityCodec::encode(record, *m_content);

        const QcResult<QcBlockEntityCodec::Fitted> fits = QcBlockEntityCodec::encodeWithin(record, *m_content, quint32(full.size()));
        QVERIFY(fits.has_value());
        QCOMPARE(fits->blob, full);
        QVERIFY(!fits->warning);

        const QcResult<QcBlockEntityCodec::Fitted> trimmed = QcBlockEntityCodec::encodeWithin(record, *m_content, quint32(plain.size()));
        QVERIFY(trimmed.has_value());
        QCOMPARE(trimmed->blob, plain);
        QVERIFY(trimmed->warning);
        QVERIFY2(trimmed->warning->contains(u"chunk 2, 3"_s), qPrintable(*trimmed->warning));

        const QcResult<QcBlockEntityCodec::Fitted> refused = QcBlockEntityCodec::encodeWithin(record, *m_content, quint32(plain.size() - 1));
        QVERIFY(!refused.has_value());
        QVERIFY2(refused.error().contains(u"not saved"_s), qPrintable(refused.error()));

        // Saved through the storage, an oversized record leaves what was stored before; the region
        // stays readable.
        QTemporaryDir directory;
        {
            QcChunkIo io(directory.path(), m_content);
            QVERIFY(io.open().has_value());
            io.setBlockEntityLimit(quint32(plain.size()));
            QcChunkIo::Departure departure;
            departure.pos = record.chunk;
            departure.blockEntities = record;
            departure.blockEntities.kept = {};
            io.save({departure}, {});
            QcBlockEntityRecord grown = departure.blockEntities;
            grown.saplings.append({{34, 64, 49}, 3.0, 2});
            departure.blockEntities = grown;
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"chunk 2, 3: block entities of \\d+ bytes exceed \\d+ and are not saved"_s));
            io.save({departure}, {});
            io.waitForIdle();
            for (const QcChunkIo::SaveResult &saved : io.takeSaves()) {
                QVERIFY(saved.status.has_value());
            }
        }
        QcChunkIo again(directory.path(), m_content);
        QVERIFY(again.open().has_value());
        const QcChunkIo::Arrival arrival = again.readNow(record.chunk);
        QVERIFY2(arrival.error.isEmpty(), qPrintable(arrival.error));
        QCOMPARE(arrival.blockEntities.saplings.size(), 1);
        QCOMPARE(arrival.blockEntities.saplings[0].pos, record.saplings[0].pos);
    }

    void tailsGoOnlyWithTheirEntries()
    {
        // A chunk leaving the world (QcBlockEntityRecord::takeFrom): an empty container and a memory
        // with nothing to save are kept for their tails; the tail of an entry gone is dropped.
        QcBlockEntities entities(m_content);
        QcCircuitState circuits;
        QcHopperQueue hoppers;
        const QcBlockPos withTail{2, 64, 2};
        const QcBlockPos without{3, 64, 2};
        const QcBlockPos memory{4, 64, 2};
        const QcBlockPos gone{5, 64, 2};
        QVERIFY(entities.putContainer({withTail, QList<QcItemStack>(5), 0}));
        QVERIFY(entities.putContainer({without, QList<QcItemStack>(5), 0}));
        entities.memoryAt(memory);

        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        record.kept.entries = {{withTail, {tail(Table::Containers, 2, "lock")}},
                               {memory, {tail(Table::Memories, 3, "m")}},
                               {gone, {tail(Table::Machines, 2, "furnace")}}};
        const QHash<QcChunkPos, QcBlockEntityKept> parts{{record.chunk, record.kept}};

        // Saving collects the same entries.
        QcBlockEntityRecord collected = QcBlockEntityRecords::collect(entities, circuits, 100, parts).value(0);
        collected.kept = record.kept;
        collected.pruneKept();

        record.takeFrom(entities, circuits, hoppers, 100);
        record.pruneKept();
        for (const QcBlockEntityRecord &taken : {record, collected}) {
            QCOMPARE(taken.containers.size(), 1);
            QCOMPARE(taken.containers[0].pos, withTail);
            QCOMPARE(taken.memories.size(), 1);
            QCOMPARE(taken.kept.entries.size(), 2);
            QVERIFY(!taken.kept.entries.contains(gone));
        }
        QVERIFY(!entities.chunkIndex(record.chunk));
    }

    // Three chests at `filled` and above it (filled, empty, broken) in a world in `config` as a later
    // version with a mod saved them: an unknown table and tails of the container table and of each
    // chest (the empty one, which this version would not save otherwise, saved for its tail).
    // Returns the stored record; the server is gone.
    QcBlockEntityRecord writeModWorld(const QcServerConfig &config, QcBlockPos filled)
    {
        join(config);
        const QcBlockPos empty = filled.offset(0, 1, 0);
        const QcBlockPos broken = filled.offset(0, 2, 0);
        m_server->world().loadChunk(filled.chunk());
        for (const QcBlockPos &pos : {filled, empty, broken}) {
            m_server->world().setBlock(pos, id("qtcraft:chest"));
        }
        m_server->blockEntities().container(filled)->stacks[0] = {item("qtcraft:diamond"), 5, 0};
        m_server->blockEntities().container(broken)->stacks[0] = {item("qtcraft:stick"), 1, 0};
        if (!m_server->save()) {
            return {};
        }
        m_client.reset();
        m_server.reset();

        QcBlockEntityRecord record = storedRecord(config.worldDirectory, filled.chunk());
        record.containers.append({empty, QList<QcItemStack>(27), 0});
        record.kept.tables = {tail(Table::Containers, 4, "sorted"), {4711, 2, false, "beehive"}};
        record.kept.entries = {{filled, {tail(Table::Containers, 3, "lock:Raw")}},
                               {empty, {tail(Table::Containers, 2, "name:Pantry")}},
                               {broken, {tail(Table::Containers, 2, "name:Junk")}}};
        if (!store(config.worldDirectory, record)) {
            return {};
        }
        return record;
    }

    void modPartsSurviveARunningServer()
    {
        // The server holds the parts of a later version per chunk, saves them back unchanged, drops
        // the tail of a chest broken in between and does not give a chest rebuilt at the place of
        // another the tail of the old one.
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        const QcBlockPos filled = m_feet.offset(0, 5, 4);
        const QcBlockPos empty = filled.offset(0, 1, 0);
        const QcBlockPos broken = filled.offset(0, 2, 0);
        const QcBlockEntityRecord record = writeModWorld(config, filled);
        QCOMPARE(record.containers.size(), 3);
        const QcEntityComponents &tables = record.kept.tables;
        const QcChunkPos chunk = filled.chunk();

        join(config);
        loadAround(filled);
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk), record.kept);
        QCOMPARE(m_server->blockEntities().container(filled)->stacks[0].count, 5);
        QVERIFY(m_server->world().setBlock(broken, qcAirBlockId));
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk).entries.size(), 2);
        // Rebuilt before any save: the new chest starts without the old one's tail.
        QVERIFY(m_server->world().setBlock(broken, id("qtcraft:chest")));
        QVERIFY(m_server->blockEntities().container(broken));
        QVERIFY(!m_server->world().blockEntityExtras().keptParts().value(chunk).entries.contains(broken));
        QVERIFY(m_server->save().has_value());
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk).entries.size(), 2);
        m_client.reset();
        m_server.reset();

        const QcBlockEntityRecord saved = storedRecord(directory.path(), chunk);
        QCOMPARE(saved.kept.tables.size(), tables.size());
        for (const QcEntityComponent &table : tables) {
            QVERIFY(saved.kept.tables.contains(table));
        }
        QCOMPARE(saved.kept.entries.size(), 2);
        QCOMPARE(saved.kept.entries.value(filled), record.kept.entries.value(filled));
        QCOMPARE(saved.kept.entries.value(empty), record.kept.entries.value(empty));
        QVERIFY(std::ranges::find(saved.containers, empty, &QcBlockEntities::Container::pos) != saved.containers.end());
        QVERIFY(!saved.kept.entries.contains(broken));
    }

    void modPartsLeaveWithTheirChunkAndGoWithRegenerate()
    {
        // A chunk that leaves the running world and comes back keeps the parts; generated again,
        // it has none.
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        const QcBlockPos filled = m_feet.offset(4 * QcWorld::chunkSize, 5, 4);
        const QcBlockEntityRecord record = writeModWorld(config, filled);
        QCOMPARE(record.containers.size(), 3);
        const QcChunkPos chunk = filled.chunk();

        m_unloadChunks = true;
        join(config);
        m_unloadChunks = false;
        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk), record.kept);
        for (int i = 0; i < 200 && m_server->world().chunk(chunk); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk));
        QVERIFY(!m_server->world().blockEntityExtras().keptParts().contains(chunk));

        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk), record.kept);
        QCOMPARE(m_server->blockEntities().container(filled)->stacks[0].count, 5);
        m_client.reset();
        m_server.reset();

        // Regenerated in a world that keeps its chunks loaded.
        join(config);
        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->world().blockEntityExtras().keptParts().value(chunk), record.kept);
        const QcBlockPos corner{chunk.x * QcWorld::chunkSize, 0, chunk.z * QcWorld::chunkSize};
        chat(u"/regenerate %1 %2"_s.arg(corner.x).arg(corner.z));
        chat(u"/confirm"_s);
        ticks(1);
        QVERIFY(!m_server->blockEntities().container(filled));
        QVERIFY(!m_server->world().blockEntityExtras().keptParts().contains(chunk));
    }
};

QTEST_GUILESS_MAIN(TestBlockEntityComponents)
#include "test_block_entity_components.moc"

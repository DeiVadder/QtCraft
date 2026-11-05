// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "fixtures/QcTestTerrainServer.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "server/storage/QcChunkCodec.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcGeneratedChunks.h"
#include "server/storage/QcRegionFile.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

// The index as a fresh store opens it from `directory`.
QSet<QcChunkPos> indexOn(const QString &directory)
{
    QcChunkStore store(directory);
    if (!store.open()) {
        QcTestUtil::failCase(u"The store in %1 does not open"_s.arg(directory));
    }
    return store.generated().chunks();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        QcTestUtil::failCase(u"Cannot write %1"_s.arg(path));
    }
}

// The payload of the bytes of an index file.
QByteArray payloadOf(const QByteArray &bytes)
{
    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(bytes, u"test"_s, QcGeneratedChunks::magic, QcGeneratedChunks::formatVersion);
    return payload ? payload->bytes : QByteArray();
}

// A payload naming `regions`, each with its first chunk.
QByteArray forgedPayload(const QList<std::pair<qint32, qint32>> &regions)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << static_cast<quint32>(regions.size());
    for (const auto &[x, z] : regions) {
        QByteArray bitmap(QcRegionFile::chunksPerRegion / 8, '\0');
        bitmap[0] = 1;
        stream << x << z;
        stream.writeRawData(bitmap.constData(), static_cast<int>(bitmap.size()));
    }
    return payload;
}

} // namespace

// The index of generated chunks (package 79.52, decision E-946): every chunk that was in the world
// is remembered across saves, changed or not; it is written with the save's journal, so a save that
// breaks off loses nothing an earlier save wrote, and a world without the index opens with an
// empty one.
class TestGeneratedChunks : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    std::unique_ptr<QcServer> server(const QString &directory) const
    {
        return QcTestUtil::openTestTerrainWorld(directory, m_content, u"Indexed"_s, 7);
    }

    // Loads the chunks from `from` to `to` (inclusive) into `server`.
    static void explore(QcServer &server, QcChunkPos from, QcChunkPos to)
    {
        for (int x = from.x; x <= to.x; ++x) {
            for (int z = from.z; z <= to.z; ++z) {
                server.world().loadChunk({x, z});
            }
        }
    }

    static QSet<QcChunkPos> loadedOf(QcServer &server)
    {
        const QList<QcChunkPos> loaded = server.world().loadedChunks();
        return {loaded.cbegin(), loaded.cend()};
    }

private slots:
    // The file keeps chunks on both sides of region borders and of the origin.
    void theIndexRoundTripsThroughItsFile()
    {
        QcGeneratedChunks index;
        const QList<QcChunkPos> chunks{{0, 0}, {31, 31}, {32, 0}, {-1, -1}, {-33, 5}, {-32, -64}, {1000, -1000}};
        for (const QcChunkPos &chunk : chunks) {
            index.note(chunk);
        }
        QVERIFY(index.isUnsaved());
        QCOMPARE(index.regionCount(), qsizetype{6});
        const QcResult<QcGeneratedChunks> read = QcGeneratedChunks::parse(payloadOf(index.bytes()), u"test"_s);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->chunks(), (QSet<QcChunkPos>{chunks.cbegin(), chunks.cend()}));
        QVERIFY(!read->isUnsaved());
        QVERIFY(read->contains({-33, 5}));
        QVERIFY(!read->contains({-34, 5}));
    }

    // A region beyond the world (or twice) is a forged or damaged file, not chunks far out.
    void regionsBeyondTheWorldAreRefused()
    {
        const int beyond = QcWorld::maxHorizontalChunk / QcRegionFile::regionSize + 1;
        QVERIFY(QcGeneratedChunks::parse(forgedPayload({{0, 0}}), u"test"_s).has_value());
        QVERIFY(!QcGeneratedChunks::parse(forgedPayload({{beyond, 0}}), u"test"_s).has_value());
        QVERIFY(!QcGeneratedChunks::parse(forgedPayload({{0, -beyond - 1}}), u"test"_s).has_value());
        QVERIFY(!QcGeneratedChunks::parse(forgedPayload({{std::numeric_limits<qint32>::max(), 0}}), u"test"_s).has_value());
        QVERIFY(!QcGeneratedChunks::parse(forgedPayload({{1, 1}, {1, 1}}), u"test"_s).has_value());
    }

    // Exploring without changing anything writes no blocks, yet the index holds every chunk that
    // was loaded; a world without the index (an older one) opens with an empty one and starts it.
    void exploredChunksAreIndexedWithoutChanges()
    {
        QTemporaryDir dir;
        QSet<QcChunkPos> first;
        {
            auto world = server(dir.path());
            explore(*world, {-34, -2}, {-29, 2});
            QVERIFY(world->save().has_value());
            first = loadedOf(*world);
            QVERIFY(first.contains({-34, -2}));
            QVERIFY(!world->world().saves().hasStored({-34, -2}));
        }
        QCOMPARE(indexOn(dir.path()), first);

        // Reopened: what was indexed stays, newly explored chunks join.
        QSet<QcChunkPos> second;
        {
            auto world = server(dir.path());
            explore(*world, {40, 40}, {42, 41});
            QVERIFY(world->save().has_value());
            second = loadedOf(*world);
        }
        QCOMPARE(indexOn(dir.path()), first | second);

        // Without the file the world opens; the index starts with what the next save holds.
        QVERIFY(QFile::remove(QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString())));
        QVERIFY(indexOn(dir.path()).isEmpty());
        QSet<QcChunkPos> third;
        {
            auto world = server(dir.path());
            explore(*world, {5, 5}, {5, 6});
            QVERIFY(world->save().has_value());
            third = loadedOf(*world);
        }
        QCOMPARE(indexOn(dir.path()), third);
    }

    // Chunks that left the world before the save count too.
    void unloadedChunksAreIndexed()
    {
        QTemporaryDir dir;
        {
            QcChunkIo io(dir.path(), m_content);
            QVERIFY(io.open().has_value());
            QcChunkIo::Departure departure;
            departure.pos = {3, -4};
            io.keep(departure);
            io.save({}, {});
            io.waitForIdle();
            const std::vector<QcChunkIo::SaveResult> saves = io.takeSaves();
            QCOMPARE(saves.size(), std::size_t{1});
            QVERIFY(saves.front().status.has_value());
        }
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{3, -4}}));
    }

    // A save that breaks off before its journal leaves the index of the save before; one that breaks
    // off after it is finished on opening. Either way nothing saved before is lost.
    void aBrokenOffSaveLosesNoEntry()
    {
        QTemporaryDir dir;
        const QString file = QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString());
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.noteGenerated({0, 0});
            store.noteGenerated({-40, 7});
            QVERIFY(store.commit({}).has_value());
        }

        // Before the journal: a half staged index lies around.
        QFile staged(file + u".next"_s);
        QVERIFY(staged.open(QIODevice::WriteOnly));
        staged.write("half an index");
        staged.close();
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{0, 0}, {-40, 7}}));
        QVERIFY(!QFile::exists(staged.fileName()));

        // After the journal: every rename is refused, the journal finishes the save.
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.noteGenerated({1, 0});
            store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*QcTestTerrainGenerator(m_content).generate({1, 0}), *m_content));
            store.failRenamesAfter(0);
            QVERIFY(!store.commit({}).has_value());
        }
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"save.journal"_s)));
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{0, 0}, {-40, 7}, {1, 0}}));

        // A store that failed a commit writes its whole index with the next one.
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.noteGenerated({2, 0});
            store.failRenamesAfter(0);
            QVERIFY(!store.commit({}).has_value());
            store.noteGenerated({3, 0});
            QVERIFY(store.commit({}).has_value());
        }
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{0, 0}, {-40, 7}, {1, 0}, {2, 0}, {3, 0}}));
    }

    // A damaged index does not keep the world from opening: it is kept aside (the original removed
    // once the copy exists), the player is told, and the index starts empty.
    void aDamagedIndexIsKeptAside()
    {
        QTemporaryDir dir;
        const QString file = QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString());
        writeFile(file, "QCGC and then nothing that fits");
        {
            auto world = server(dir.path());
            const QString warnings = world->openWarnings().join(u'\n');
            QVERIFY2(warnings.contains(u"the damaged file was kept as generated.qcg.damaged"_s), qPrintable(warnings));
            QVERIFY(!QFile::exists(file));
            explore(*world, {2, 2}, {2, 2});
            QVERIFY(world->save().has_value());
        }
        QVERIFY(QFile::exists(file + u".damaged"_s));
        QCOMPARE(readFile(file + u".damaged"_s), QByteArray("QCGC and then nothing that fits"));

        // The world opened and saved again: a fresh index replaced the damaged one.
        QVERIFY(indexOn(dir.path()).contains({2, 2}));
    }

    // With every name for a copy taken, the damaged file stays and the warning says the next save
    // replaces it; no older copy is overwritten.
    void aDamagedIndexWithoutRoomForACopySaysSo()
    {
        QTemporaryDir dir;
        const QString file = QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString());
        writeFile(file, "damaged");
        for (int attempt = 1; attempt <= QcSaveFile::maxDamagedCopies; ++attempt) {
            writeFile(attempt == 1 ? file + u".damaged"_s : u"%1.damaged.%2"_s.arg(file).arg(attempt), "older copy");
        }
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        const QString warnings = store.openWarnings().join(u'\n');
        QVERIFY2(warnings.contains(u"are taken: the next save replaces the damaged file"_s), qPrintable(warnings));
        QCOMPARE(readFile(file), QByteArray("damaged"));
        QCOMPARE(readFile(file + u".damaged.16"_s), QByteArray("older copy"));
    }

    // A newer build's index is left alone; this build keeps what it adds in its own file and reads
    // it back while the newer file is there.
    void aNewerIndexIsLeftAlone()
    {
        QTemporaryDir dir;
        const QString file = QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString());
        const QByteArray newer = QcSaveFile::bytes(QcGeneratedChunks::magic, QByteArray("whatever version 2 holds"), 2);
        writeFile(file, newer);
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            const QString warnings = store.openWarnings().join(u'\n');
            QVERIFY2(warnings.contains(u"has format version 2, newer than"_s), qPrintable(warnings));
            QCOMPARE(store.generatedFile(), QcGeneratedChunks::olderFileName.toString());
            store.noteGenerated({4, 4});
            QVERIFY(store.commit({}).has_value());
        }
        QCOMPARE(readFile(file), newer);
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{4, 4}}));
        QVERIFY(!QFile::exists(file + u".damaged"_s));
    }

    // A save whose journal cannot be written leaves the index of the save before; the entries wait
    // for the next save.
    void aSaveWithoutItsJournalKeepsTheOldIndex()
    {
        QTemporaryDir dir;
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        store.noteGenerated({0, 0});
        QVERIFY(store.commit({}).has_value());

        const QString file = QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString());
        const QByteArray before = readFile(file);
        QVERIFY(QDir(dir.path()).mkdir(u"save.journal"_s));
        store.noteGenerated({1, 1});
        QVERIFY(!store.commit({}).has_value());
        QCOMPARE(readFile(file), before);
        QVERIFY(!QFile::exists(file + u".next"_s));
        QVERIFY(QDir(dir.path()).rmdir(u"save.journal"_s));
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{0, 0}}));

        QVERIFY(store.commit({}).has_value());
        QCOMPARE(indexOn(dir.path()), (QSet<QcChunkPos>{{0, 0}, {1, 1}}));
    }
};

QTEST_GUILESS_MAIN(TestGeneratedChunks)
#include "test_generated_chunks.moc"

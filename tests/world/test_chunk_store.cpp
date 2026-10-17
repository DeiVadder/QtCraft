#include "common/content/loader/QcBaseContent.h"
#include "common/core/QcCrc32.h"
#include "QcTestUtil.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcChunkCodec.h"
#include "server/storage/QcChunkIo.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcRegionFile.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> baseContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

// The chunk storage of a world (stage 31.3, decision E-373): indexed region files read chunk by
// chunk, damaged chunks found on their own, a save written as one through its journal (a save that
// broke off before the journal leaves the previous one, one that broke off after it is finished on
// the next open), and QcChunkIo reading and writing in the background in the order of the requests.
class TestChunkStore : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = baseContent();

    std::shared_ptr<QcChunk> generated(QcChunkPos pos) const { return QcTestTerrainGenerator(m_content).generate(pos); }

    // A journal naming files replaced with `contents` (version 2: size and checksum of each head).
    static void writeJournal(const QDir &dir, const QList<std::pair<QString, QByteArray>> &contents)
    {
        QByteArray steps;
        QDataStream stream(&steps, QIODevice::WriteOnly);
        stream.setVersion(QcSaveFile::streamVersion);
        stream << quint32(contents.size());
        for (const auto &[name, content] : contents) {
            stream << name << false << qint64(content.size()) << qcCrc32(content.left(qsizetype(64) * 1024));
        }
        QVERIFY(QcSaveFile::write(dir.filePath(u"save.journal"_s), "QCSJ", steps, 2).has_value());
    }

private slots:
    void regionFilesKeepEveryChunkApart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"r.0.0.qcr"_s);
        const QList<QcRegionFile::Blob> blobs{{{3, 4}, 0x13, "three-four", std::nullopt}, {{0, 0}, 0, "origin", std::nullopt}, {{31, 31}, 7, QByteArray(5000, 'x'), std::nullopt}};
        const QcResult<QcRegionFile::Index> written = QcRegionFile::write(path, "TEST", 1, blobs);
        QVERIFY(written.has_value());

        const QcResult<QcRegionFile::Index> index = QcRegionFile::readIndex(path, {0, 0}, "TEST", 1);
        QVERIFY2(index.has_value(), qcErrorText(index).constData());
        QCOMPARE(index->size(), 3);
        QCOMPARE(index->value({3, 4}).marks, quint16(0x13));
        QCOMPARE(*QcRegionFile::readBlob(path, index->value({3, 4})), QByteArray("three-four"));
        QCOMPARE(*QcRegionFile::readBlob(path, index->value({31, 31})), QByteArray(5000, 'x'));

        // The same file under the name of another region holds that region's chunks.
        const QcResult<QcRegionFile::Index> elsewhere = QcRegionFile::readIndex(path, {-1, 2}, "TEST", 1);
        QVERIFY(elsewhere->contains({-29, 68}));

        // A damaged blob fails alone; a damaged index fails the file; another version is refused.
        QByteArray data = readFile(path);
        data[qsizetype(index->value({3, 4}).offset)] = 'X';
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QVERIFY(!QcRegionFile::readBlob(path, index->value({3, 4})).has_value());
        QVERIFY(QcRegionFile::readBlob(path, index->value({0, 0})).has_value());
        data[16] = char(data[16] ^ 0x40);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QVERIFY(!QcRegionFile::readIndex(path, {0, 0}, "TEST", 1).has_value());
        QVERIFY(QcRegionFile::write(path, "TEST", 1, blobs).has_value());
        QVERIFY(QcRegionFile::readIndex(path, {0, 0}, "TEST", 2).error().contains(u"older development version"_s));
    }

    void chunksRoundTripThroughTheStore()
    {
        QTemporaryDir dir;
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        const std::shared_ptr<QcChunk> chunk = generated({5, -3});
        chunk->setBlock(1, 70, 1, *m_content->blockId(u"qtcraft:glass"_s));
        store.put(QcChunkStore::Kind::Blocks, {5, -3}, QcChunkCodec::encode(*chunk, *m_content));
        QcBlockEntityRecord record;
        record.chunk = {5, -3};
        record.blockUpdates = {{81, 64, -47}};
        store.put(QcChunkStore::Kind::BlockEntities, {5, -3}, QcBlockEntityCodec::encode(record, *m_content), 0x21);

        // Read from memory before the commit, from disk after it, also by a store opened afresh.
        QVERIFY(store.read({5, -3})->data[0].has_value());
        QVERIFY(store.commit({}).has_value());
        QCOMPARE(store.pendingChunks(), 0);
        QcChunkStore again(dir.path());
        QVERIFY(again.open().has_value());
        QCOMPARE(again.catalog().value({5, -3}).marks, quint16(0x21));
        QVERIFY(again.catalog().value({5, -3}).has(QcChunkStore::Kind::Blocks));
        const QcResult<QcChunkStore::Blobs> blobs = again.read({5, -3});
        QVERIFY(blobs.has_value());
        QStringList warnings;
        QSet<QString> reported;
        const QcResult<std::shared_ptr<QcChunk>> back = QcChunkCodec::decode(*blobs->data[0], {5, -3}, *m_content, warnings, reported);
        QVERIFY(back.has_value());
        QCOMPARE((*back)->block(1, 70, 1), *m_content->blockId(u"qtcraft:glass"_s));
        const QcResult<QcBlockEntityRecord> entries = QcBlockEntityCodec::decode(*blobs->data[1], {5, -3}, *m_content, warnings, reported);
        QCOMPARE(entries->blockUpdates, record.blockUpdates);

        // Data at the wrong place is damage.
        QVERIFY(!QcChunkCodec::decode(*blobs->data[0], {6, -3}, *m_content, warnings, reported).has_value());
        QVERIFY(!QcBlockEntityCodec::decode(*blobs->data[1], {6, -3}, *m_content, warnings, reported).has_value());
    }

    // A save is written as one (decision E-373): before its journal nothing of it counts, after it
    // the save is finished when the world opens again.
    void aSaveIsWrittenAsOne()
    {
        QTemporaryDir dir;
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.put(QcChunkStore::Kind::Blocks, {0, 0}, QcChunkCodec::encode(*generated({0, 0}), *m_content));
            QVERIFY(store.commit({{u"world.json"_s, QByteArray("first")}}).has_value());
        }
        const QString region = dir.filePath(u"regions/r.0.0.qcr"_s);
        const QByteArray firstRegion = readFile(region);

        // Broke off before the journal: staged files lie around, the previous save stays.
        QFile staged(region + u".next"_s);
        QVERIFY(staged.open(QIODevice::WriteOnly));
        staged.write("half a region");
        staged.close();
        QFile stagedInfo(dir.filePath(u"world.json.next"_s));
        QVERIFY(stagedInfo.open(QIODevice::WriteOnly));
        stagedInfo.write("second");
        stagedInfo.close();
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            QCOMPARE(store.catalog().size(), 1);
        }
        QCOMPARE(readFile(region), firstRegion);
        QCOMPARE(readFile(dir.filePath(u"world.json"_s)), QByteArray("first"));
        QVERIFY(!QFile::exists(region + u".next"_s));

        // Broke off after the journal: the save is finished on opening.
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*generated({1, 0}), *m_content));
            QVERIFY(store.commit({}).has_value());
        }
        const QByteArray secondRegion = readFile(region);
        QVERIFY(QFile::copy(region, region + u".next"_s));
        QVERIFY(stagedInfo.open(QIODevice::WriteOnly));
        stagedInfo.write("third");
        stagedInfo.close();
        QFile(region).resize(10);
        writeJournal(QDir(dir.path()), {{u"regions/r.0.0.qcr"_s, secondRegion}, {u"world.json"_s, QByteArray("third")}});
        {
            QcChunkStore store(dir.path());
            QVERIFY2(store.open().has_value(), "the journal finishes the save");
            QCOMPARE(store.catalog().size(), 2);
        }
        QCOMPARE(readFile(region), secondRegion);
        QCOMPARE(readFile(dir.filePath(u"world.json"_s)), QByteArray("third"));
        QVERIFY(!QFile::exists(dir.filePath(u"save.journal"_s)));
    }

    // Finishing a journal tells a file already renamed from one whose staged file was lost: the
    // first finishes, the second refuses the world instead of applying half a save (review of 31.3).
    void aJournalWhoseStagedFileIsLostIsRefused()
    {
        QTemporaryDir dir;
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            QVERIFY(store.commit({{u"world.json"_s, QByteArray("first")}}).has_value());
        }

        // Already renamed: the file in place is the one the journal names.
        writeJournal(QDir(dir.path()), {{u"world.json"_s, QByteArray("first")}});
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
        }
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(u"save.journal"_s)));

        // Lost: the old file is still there.
        writeJournal(QDir(dir.path()), {{u"world.json"_s, QByteArray("second")}});
        QcChunkStore store(dir.path());
        const QcStatus opened = store.open();
        QVERIFY(!opened.has_value());
        QVERIFY2(opened.error().contains(u"is not the file the save wrote"_s), qPrintable(opened.error()));
        QCOMPARE(readFile(QDir(dir.path()).filePath(u"world.json"_s)), QByteArray("first"));
    }

    // A save that cannot be written keeps everything handed over for the next one.
    void aFailedSaveLosesNothing()
    {
        QTemporaryDir dir;
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        QFile blocker(dir.filePath(u"regions"_s));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        store.put(QcChunkStore::Kind::Blocks, {0, 0}, QcChunkCodec::encode(*generated({0, 0}), *m_content));
        QVERIFY(!store.commit({{u"world.json"_s, QByteArray("x")}}).has_value());
        QVERIFY(!QFile::exists(dir.filePath(u"world.json"_s)));
        QVERIFY(store.read({0, 0})->data[0].has_value());
        QVERIFY(QFile::remove(blocker.fileName()));
        QVERIFY(store.commit({}).has_value());
        QVERIFY(QFile::exists(dir.filePath(u"regions/r.0.0.qcr"_s)));
    }

    // A commit that breaks off between its renames leaves the store stale (review of 31.3): the next
    // read or commit finishes the save from the journal and reads the indexes again, so no chunk of a
    // region renamed in between is read at old offsets or rewritten with a fresh checksum.
    void aCommitThatBreaksOffBetweenRenamesCorruptsNothing()
    {
        QTemporaryDir dir;
        const QcBlockId glass = *m_content->blockId(u"qtcraft:glass"_s);
        const auto chunkWith = [&](QcChunkPos pos, int y) {
            std::shared_ptr<QcChunk> chunk = generated(pos);
            chunk->setBlock(0, y, 0, glass);
            return QcChunkCodec::encode(*chunk, *m_content);
        };
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        // Two regions, several chunks each.
        const QList<QcChunkPos> chunks{{0, 0}, {1, 0}, {2, 0}, {40, 0}, {41, 0}};
        for (const QcChunkPos &pos : chunks) {
            store.put(QcChunkStore::Kind::Blocks, pos, chunkWith(pos, 100));
        }
        QVERIFY(store.commit({}).has_value());

        // The second save changes a chunk of each region (bigger blobs shift the offsets) and breaks
        // off after its first rename.
        store.put(QcChunkStore::Kind::Blocks, {0, 0}, chunkWith({0, 0}, 101));
        store.put(QcChunkStore::Kind::Blocks, {40, 0}, chunkWith({40, 0}, 102));
        std::shared_ptr<QcChunk> big = generated({1, 0});
        for (int y = 90; y < 120; ++y) {
            big->setBlock(y % 16, y, 3, glass);
        }
        store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*big, *m_content));
        store.failRenamesAfter(1);
        QVERIFY(!store.commit({}).has_value());

        // Reading and saving again work from what is on disk.
        QVERIFY(store.read({2, 0}).has_value());
        store.put(QcChunkStore::Kind::Blocks, {41, 0}, chunkWith({41, 0}, 103));
        QVERIFY(store.commit({}).has_value());

        QcChunkStore again(dir.path());
        QVERIFY(again.open().has_value());
        const QHash<QcChunkPos, int> expected{{{0, 0}, 101}, {{2, 0}, 100}, {{40, 0}, 102}, {{41, 0}, 103}};
        for (const QcChunkPos &pos : chunks) {
            const QcResult<QcChunkStore::Blobs> blobs = again.read(pos);
            QVERIFY2(blobs.has_value(), qcErrorText(blobs).constData());
            QStringList warnings;
            QSet<QString> reported;
            const QcResult<std::shared_ptr<QcChunk>> chunk = QcChunkCodec::decode(*blobs->data[0], pos, *m_content, warnings, reported);
            QVERIFY2(chunk.has_value(), qPrintable(u"chunk %1, %2"_s.arg(pos.x).arg(pos.z)));
            if (expected.contains(pos)) {
                QCOMPARE((*chunk)->block(0, expected.value(pos), 0), glass);
            }
        }
        const QcResult<QcChunkStore::Blobs> grown = again.read({1, 0});
        QStringList warnings;
        QSet<QString> reported;
        QCOMPARE((*QcChunkCodec::decode(*grown->data[0], {1, 0}, *m_content, warnings, reported))->block(95 % 16, 95, 3), glass);
    }

    // A damaged blob kept while its region is written again keeps its old checksum, so it stays
    // recognisably damaged (decision E-373).
    void aDamagedBlobStaysDamagedWhenItsRegionIsRewritten()
    {
        QTemporaryDir dir;
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.put(QcChunkStore::Kind::Blocks, {0, 0}, QcChunkCodec::encode(*generated({0, 0}), *m_content));
            store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*generated({1, 0}), *m_content));
            QVERIFY(store.commit({}).has_value());
        }
        const QString path = dir.filePath(u"regions/r.0.0.qcr"_s);
        const QcResult<QcRegionFile::Index> index = QcRegionFile::readIndex(path, {0, 0}, "QCRG", 3);
        QByteArray data = readFile(path);
        data[qsizetype(index->value({0, 0}).offset) + 5] ^= 0x10;
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();

        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        QVERIFY(!store.read({0, 0}).has_value());
        store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*generated({1, 0}), *m_content));
        std::shared_ptr<QcChunk> changed = generated({1, 0});
        changed->setBlock(1, 100, 1, *m_content->blockId(u"qtcraft:glass"_s));
        store.put(QcChunkStore::Kind::Blocks, {1, 0}, QcChunkCodec::encode(*changed, *m_content));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"chunk 0, 0 is damaged and kept as it is"_s));
        QVERIFY(store.commit({}).has_value());
        QcChunkStore again(dir.path());
        QVERIFY(again.open().has_value());
        QVERIFY(!again.read({0, 0}).has_value());
        QVERIFY(again.read({1, 0}).has_value());
    }

    // QcChunkIo works in the order of the requests: a chunk read after it was handed over gets what
    // was handed over, also when reads go before other writing work.
    void backgroundReadsSeeWhatWasHandedOver()
    {
        QTemporaryDir dir;
        QcChunkIo io(dir.path(), m_content);
        QVERIFY(io.open().has_value());
        const QcBlockId glass = *m_content->blockId(u"qtcraft:glass"_s);
        for (int i = 0; i < 40; ++i) {
            QcChunkIo::Departure departure;
            departure.pos = {i, 0};
            std::shared_ptr<QcChunk> chunk = generated({i, 0});
            chunk->setBlock(0, 100, 0, glass);
            departure.blocks = chunk;
            departure.entities.populated = true;
            io.keep(std::move(departure));
            io.read({i, 0}, quint64(i) + 1);
        }
        io.waitForIdle();
        const std::vector<QcChunkIo::Arrival> arrivals = io.takeArrivals();
        QCOMPARE(arrivals.size(), std::size_t(40));
        for (const QcChunkIo::Arrival &arrival : arrivals) {
            QVERIFY(arrival.error.isEmpty());
            QVERIFY(arrival.blocks);
            QCOMPARE(arrival.blocks->block(0, 100, 0), glass);
            QVERIFY(arrival.entities.populated);
        }
        const quint64 save = io.save({}, {{u"world.json"_s, QByteArray("{}")}});
        io.waitForIdle();
        const std::vector<QcChunkIo::SaveResult> saves = io.takeSaves();
        QCOMPARE(saves.size(), std::size_t(1));
        QCOMPARE(saves.front().id, save);
        QVERIFY(saves.front().status.has_value());
        QCOMPARE(io.pendingChunks(), 0);
        QCOMPARE(io.readNow({7, 0}).blocks->block(0, 100, 0), glass);
    }
};

QTEST_GUILESS_MAIN(TestChunkStore)
#include "test_chunk_store.moc"

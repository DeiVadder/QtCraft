#include "QcTestUtil.h"
#include "QcWorldStorage.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QcBlockDefinition block(const QString &name)
{
    QcBlockDefinition definition;
    definition.name = name;
    definition.faceTextures.fill(name);
    return definition;
}

// Registry with the given blocks in this order, plus block items.
std::shared_ptr<QcContentRegistry> registryWith(const QStringList &names)
{
    auto content = std::make_shared<QcContentRegistry>();
    for (const QString &name : names) {
        if (!content->registerBlock(block(name))) {
            qFatal("register failed");
        }
    }
    content->createBlockItems();
    return content;
}

} // namespace

class TestWorldStorage : public QObject
{
    Q_OBJECT

private slots:
    void worldInfoRoundTrip()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), registryWith({}));
        QcWorldInfo info;
        info.name = u"Mein Welt ü"_s;
        info.seed = -9007199254740993; // not representable as a JSON double
        info.created = QDateTime(QDate(2026, 10, 7), QTime(12, 0), QTimeZone::UTC);
        info.lastPlayed = info.created.addSecs(3600);
        info.worldTime = 1234.5;
        QVERIFY(storage.writeInfo(info).has_value());
        const auto read = storage.readInfo();
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->name, info.name);
        QCOMPARE(read->seed, info.seed);
        QCOMPARE(read->created, info.created);
        QCOMPARE(read->lastPlayed, info.lastPlayed);
        QCOMPARE(read->worldTime, 1234.5);
    }

    void chunksResolveByNameWhenContentChanges()
    {
        QTemporaryDir dir;
        const auto before = registryWith({u"a:stone"_s, u"a:dirt"_s, u"mod:gem"_s});
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{-33, 40});
        chunk->setBlock(1, 2, 3, *before->blockId(u"a:stone"_s));
        chunk->setBlock(4, 70, 5, *before->blockId(u"a:dirt"_s));
        chunk->setBlock(6, 200, 7, *before->blockId(u"mod:gem"_s));
        QVERIFY(QcWorldStorage(dir.path(), before).writeChunks({{chunk->pos(), chunk}}).has_value());

        // The new content registers blocks in another order and lost the mod block.
        const auto after = registryWith({u"a:dirt"_s, u"b:new"_s, u"a:stone"_s});
        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), after).readChunks(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->size(), 1);
        const auto loaded = read->value({-33, 40});
        QVERIFY(loaded);
        QCOMPARE(loaded->block(1, 2, 3), *after->blockId(u"a:stone"_s));
        QCOMPARE(loaded->block(4, 70, 5), *after->blockId(u"a:dirt"_s));
        QCOMPARE(loaded->block(6, 200, 7), qcAirBlockId);
        QCOMPARE(loaded->block(0, 0, 0), qcAirBlockId);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.constFirst().contains(u"mod:gem"_s));
    }

    void chunksOfSeveralRegionsRoundTrip()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks;
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{31, 31}, QcChunkPos{32, 0}, QcChunkPos{-1, -1}}) {
            auto chunk = std::make_shared<QcChunk>(pos);
            chunk->setBlock(pos.x & 15, 10, pos.z & 15, 1);
            chunks.insert(pos, chunk);
        }
        const QcWorldStorage storage(dir.path(), content);
        QVERIFY(storage.writeChunks(chunks).has_value());
        QCOMPARE(QDir(dir.filePath(u"regions"_s)).entryList({u"*.qcr"_s}).size(), 3);
        QStringList warnings;
        const auto read = storage.readChunks(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->size(), 4);
        QCOMPARE(read->value({31, 31})->block(15, 10, 15), 1u);
        QCOMPARE(read->value({-1, -1})->block(15, 10, 15), 1u);
    }

    void damagedFilesFailWithAMessage_data()
    {
        QTest::addColumn<int>("offset");
        QTest::addColumn<QByteArray>("replacement");
        QTest::newRow("magic") << 0 << QByteArray("XXXX");
        QTest::newRow("version") << 4 << QByteArray("\x00\x09", 2);
        QTest::newRow("payload bit flip") << 20 << QByteArray("\x7f", 1);
        QTest::newRow("truncated") << -1 << QByteArray();
    }

    void damagedFilesFailWithAMessage()
    {
        QFETCH(int, offset);
        QFETCH(QByteArray, replacement);
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(0, 0, 0, 1);
        const QcWorldStorage storage(dir.path(), content);
        QVERIFY(storage.writeChunks({{chunk->pos(), chunk}}).has_value());
        const QString path = dir.filePath(u"regions/r.0.0.qcr"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray data = file.readAll();
        if (offset < 0) {
            data.chop(5);
        } else {
            data.replace(offset, replacement.size(), replacement);
        }
        file.resize(0);
        file.seek(0);
        file.write(data);
        file.close();
        QStringList warnings;
        const auto read = storage.readChunks(warnings);
        QVERIFY(!read.has_value());
        QVERIFY2(read.error().contains(path), qPrintable(read.error()));
    }

    void playerRoundTripAndUnknownItems()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s, u"mod:gem"_s});
        QcPlayerData player;
        player.motion.position = {10.5, 70.0, -3.25};
        player.motion.onGround = true;
        player.yaw = 135.0f;
        player.pitch = -20.0f;
        player.inventory.setSlot(0, {*content->itemId(u"a:stone"_s), 42, 0});
        player.inventory.setSlot(20, {*content->itemId(u"mod:gem"_s), 3, 0});
        player.inventory.setSelectedHotbarSlot(4);
        QVERIFY(QcWorldStorage(dir.path(), content).writePlayer(u"Jonas"_s, player).has_value());

        const auto withoutMod = registryWith({u"a:stone"_s});
        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), withoutMod).readPlayer(u"Jonas"_s, warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QVERIFY(read->has_value());
        const QcPlayerData &loaded = **read;
        QCOMPARE(loaded.motion.position, player.motion.position);
        QCOMPARE(loaded.yaw, 135.0f);
        QCOMPARE(loaded.pitch, -20.0f);
        QCOMPARE(loaded.inventory.selectedHotbarSlot(), 4);
        QCOMPARE(loaded.inventory.slot(0).item, *withoutMod->itemId(u"a:stone"_s));
        QCOMPARE(loaded.inventory.slot(0).count, 42);
        QVERIFY(loaded.inventory.slot(20).isEmpty());
        QCOMPARE(warnings.size(), 1);
    }

    void missingPlayerIsNotAnError()
    {
        QTemporaryDir dir;
        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), registryWith({})).readPlayer(u"Nobody"_s, warnings);
        QVERIFY(read.has_value());
        QVERIFY(!read->has_value());
    }
};

QTEST_GUILESS_MAIN(TestWorldStorage)
#include "test_world_storage.moc"

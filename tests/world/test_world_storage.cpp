#include "common/core/QcCrc32.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocol.h"
#include "QcTestUtil.h"
#include "QcTestWorldFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

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

// A block with states: `facing` in the given order, optionally a second property.
QcBlockDefinition stairs(const QStringList &facings, bool withHalf)
{
    QcBlockDefinition definition = block(u"a:stairs"_s);
    definition.properties.append({u"facing"_s, facings});
    if (withHalf) {
        definition.properties.append({u"half"_s, {u"bottom"_s, u"top"_s}});
    }
    return definition;
}

} // namespace

class TestWorldStorage : public QObject
{
    Q_OBJECT

private slots:
    void worldInfoRoundTrip()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), registryWith({}));

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
        QVERIFY(QcTestWorldFiles(dir.path(), before).writeChunks({{chunk->pos(), chunk}}).has_value());

        // The new content registers blocks in another order and lost the mod block.
        const auto after = registryWith({u"a:dirt"_s, u"b:new"_s, u"a:stone"_s});
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), after).readChunks(warnings);
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

        const QcTestWorldFiles storage(dir.path(), content);
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
        const QcTestWorldFiles storage(dir.path(), content);
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
        QVERIFY(QcTestWorldFiles(dir.path(), content).writePlayer(u"Jonas"_s, player).has_value());

        const auto withoutMod = registryWith({u"a:stone"_s});
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), withoutMod).readPlayer(u"Jonas"_s, warnings);
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

    void gameRulesRoundTripAndDefaultForOldWorlds()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), registryWith({}));

        QcWorldInfo info;
        info.name = u"Rules"_s;
        info.keepInventory = false;
        info.randomTicks = false;
        QVERIFY(storage.writeInfo(info).has_value());
        QCOMPARE(storage.readInfo()->keepInventory, false);
        QCOMPARE(storage.readInfo()->randomTicks, false);

        // Worlds saved before game rules existed keep the inventory on death, as they always did.
        QFile file(dir.filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray text = file.readAll();
        file.close();
        text.replace("\"gameRules\"", "\"unused\"");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
        file.close();
        QCOMPARE(storage.readInfo()->keepInventory, true);
        QCOMPARE(storage.readInfo()->randomTicks, true);
    }

    void implausiblePlayerDataIsRejected_data()
    {
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("y");
        QTest::addColumn<float>("yaw");
        QTest::addColumn<float>("pitch");
        QTest::newRow("far away") << 1e300 << 70.0 << 0.0f << 0.0f;
        QTest::newRow("beyond the world edge") << 40'000'000.0 << 70.0 << 0.0f << 0.0f;
        QTest::newRow("below the void floor") << 0.0 << -1000.0 << 0.0f << 0.0f;
        QTest::newRow("nan yaw") << 0.0 << 70.0 << std::numeric_limits<float>::quiet_NaN() << 0.0f;
        QTest::newRow("pitch over 90") << 0.0 << 70.0 << 0.0f << 120.0f;
    }

    void implausiblePlayerDataIsRejected()
    {
        QFETCH(double, x);
        QFETCH(double, y);
        QFETCH(float, yaw);
        QFETCH(float, pitch);

        QTemporaryDir dir;
        const auto content = registryWith({});
        QcPlayerData player;
        player.motion.position = {x, y, 0.5};
        player.yaw = yaw;
        player.pitch = pitch;
        QVERIFY(QcTestWorldFiles(dir.path(), content).writePlayer(u"Odd"_s, player).has_value());

        QStringList warnings;
        QVERIFY(!QcTestWorldFiles(dir.path(), content).readPlayer(u"Odd"_s, warnings).has_value());
    }

    void playerFallingThroughTheVoidLoads()
    {
        // Saved while falling far below the void limit (decision E-288): still the same player.
        QTemporaryDir dir;
        const auto content = registryWith({});
        QcPlayerData player;
        player.motion.position = {0.5, QcWorld::voidFloorY, 0.5};
        player.motion.velocity = {0.0, -QcPhysics::terminalVelocity, 0.0};
        QVERIFY(QcTestWorldFiles(dir.path(), content).writePlayer(u"Faller"_s, player).has_value());

        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), content).readPlayer(u"Faller"_s, warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QVERIFY(read->has_value());
        QCOMPARE((*read)->motion.position.y, QcWorld::voidFloorY);
    }

    void negativeHealthIsClamped()
    {
        QTemporaryDir dir;
        const auto content = registryWith({});
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.health = -7.0;
        QVERIFY(QcTestWorldFiles(dir.path(), content).writePlayer(u"Hurt"_s, player).has_value());

        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), content).readPlayer(u"Hurt"_s, warnings);
        QVERIFY(read.has_value() && read->has_value());
        QCOMPARE((*read)->health, 0.0);
    }

    void chunksBeyondTheWorldEdgeAreRejected()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{QcWorld::maxHorizontalChunk + 10, 0});
        chunk->setBlock(0, 0, 0, 1);
        const QcTestWorldFiles storage(dir.path(), content);
        QVERIFY(storage.writeChunks({{chunk->pos(), chunk}}).has_value());
        QStringList warnings;
        QVERIFY(!storage.readChunks(warnings).has_value());
    }

    void foodRoundTripsAndOlderPlayerFilesAreRefused()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        const QcTestWorldFiles storage(dir.path(), content);
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.food = 7;
        player.exhaustion = 2.5;
        QVERIFY(storage.writePlayer(u"Hungry"_s, player).has_value());

        QStringList warnings;
        const auto read = storage.readPlayer(u"Hungry"_s, warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE((*read)->food, 7);
        QCOMPARE((*read)->exhaustion, 2.5);

        // A file of an older development version (version 1, before hunger) is refused, not converted.
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << 0.5 << 70.0 << 0.5 << 0.0 << 0.0 << 0.0 << true << 90.0f << 0.0f << 12.0 << qint32(2)
               << quint32(QcInventory::slotCount);
        for (int i = 0; i < QcInventory::slotCount; ++i) {
            stream << (i == 0 ? u"a:stone"_s : QString()) << qint32(i == 0 ? 5 : 0) << qint32(0);
        }
        QByteArray file("QCPL");
        QDataStream header(&file, QIODevice::Append);
        header.setVersion(QDataStream::Qt_6_8);
        header << quint16(1) << quint32(payload.size()) << qcCrc32(payload);
        file.append(payload);
        QFile old(dir.filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Old"_s)));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(file);
        old.close();

        const auto oldRead = storage.readPlayer(u"Old"_s, warnings);
        QVERIFY(!oldRead.has_value());
        QVERIFY2(oldRead.error().contains(u"older development version"_s), qPrintable(oldRead.error()));

        // Food beyond the range is clamped.
        player.food = 99;
        QVERIFY(storage.writePlayer(u"Hungry"_s, player).has_value());
        QCOMPARE((*storage.readPlayer(u"Hungry"_s, warnings))->food, 20);
    }

    void playerNamesDifferingInCaseGetTheirOwnFiles()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), registryWith({u"a:stone"_s}));
        QcPlayerData bob;
        bob.motion.position = {0.5, 70.0, 0.5};
        bob.health = 5.0;
        QcPlayerData lowerBob = bob;
        lowerBob.health = 9.0;
        QVERIFY(storage.writePlayer(u"Bob"_s, bob).has_value());
        QVERIFY(storage.writePlayer(u"bob"_s, lowerBob).has_value());
        // "a.b" and "a_b" sanitize to the same readable part.
        QVERIFY(storage.writePlayer(u"a.b"_s, bob).has_value());
        QVERIFY(storage.writePlayer(u"a_b"_s, lowerBob).has_value());

        // Four files whose names differ even when case is ignored (macOS, Windows).
        QStringList files = QDir(dir.filePath(u"players"_s)).entryList(QDir::Files);
        QCOMPARE(files.size(), 4);
        for (QString &file : files) {
            file = file.toLower();
        }
        files.removeDuplicates();
        QCOMPARE(files.size(), 4);
        QVERIFY(QcWorldStorage::playerFileName(u"Bob"_s).startsWith(u"bob-"_s));

        QStringList warnings;
        QCOMPARE((*storage.readPlayer(u"Bob"_s, warnings))->health, 5.0);
        QCOMPARE((*storage.readPlayer(u"bob"_s, warnings))->health, 9.0);
        QCOMPARE((*storage.readPlayer(u"a.b"_s, warnings))->health, 5.0);
        QCOMPARE((*storage.readPlayer(u"a_b"_s, warnings))->health, 9.0);
        QVERIFY(!*storage.readPlayer(u"BOB"_s, warnings));
    }

    void playerFilesUnderTheNamesOfOlderVersionsAreNotRead()
    {
        // Older development versions named the file after the name as typed (players/Alice.qcp);
        // such files are not looked for any more (decision E-356) and stay untouched.
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), registryWith({u"a:stone"_s}));
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.health = 7.0;
        QVERIFY(storage.writePlayer(u"Alice"_s, player).has_value());
        const QDir players(dir.filePath(u"players"_s));
        QVERIFY(QFile::rename(players.filePath(QcWorldStorage::playerFileName(u"Alice"_s)), players.filePath(u"Alice.qcp"_s)));

        QStringList warnings;
        const auto read = storage.readPlayer(u"Alice"_s, warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QVERIFY(!read->has_value());
        QVERIFY(warnings.isEmpty());
        QCOMPARE(players.entryList(QDir::Files), QStringList{u"Alice.qcp"_s});
    }

    void blockStatesRoundTripByName()
    {
        QTemporaryDir dir;
        auto before = std::make_shared<QcContentRegistry>();
        QVERIFY(before->registerBlock(block(u"a:stone"_s)));
        QVERIFY(before->registerBlock(stairs({u"north"_s, u"east"_s, u"south"_s, u"west"_s}, true)));
        const QcBlockId east = *before->stateId(u"a:stairs[facing=east,half=top]"_s);
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{2, -3});
        chunk->setBlock(1, 64, 1, east);
        chunk->setBlock(2, 64, 1, *before->blockId(u"a:stone"_s));
        QVERIFY(QcTestWorldFiles(dir.path(), before).writeChunks({{chunk->pos(), chunk}}).has_value());

        // Later content lists the values in another order and drops the half property: runtime
        // ids differ, the saved state still means facing east.
        auto after = std::make_shared<QcContentRegistry>();
        QVERIFY(after->registerBlock(stairs({u"west"_s, u"south"_s, u"east"_s, u"north"_s}, false)));
        QVERIFY(after->registerBlock(block(u"a:stone"_s)));
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), after).readChunks(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        const auto loaded = read->value({2, -3});
        QCOMPARE(after->stateName(loaded->block(1, 64, 1)), u"a:stairs[facing=east]"_s);
        QCOMPARE(after->stateName(loaded->block(2, 64, 1)), u"a:stone"_s);

        // A property the block does not have is reported, not fatal.
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(u"half=top"_s), qPrintable(warnings.first()));
    }

    void regionFilesOfOlderVersionsAreRefused()
    {
        // A region file of format 1 (plain block names), written here byte by byte: refused, not
        // converted (decision E-356).
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath(u"regions"_s)));
        QcChunk chunk({0, 0});
        chunk.setBlock(5, 70, 4, 1);

        QByteArray payload;
        {
            QDataStream stream(&payload, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << quint32(2) << u"a:air_like"_s << u"a:stone"_s << quint32(1);
            QcProtocol::writeChunk(stream, chunk);
        }

        QFile file(dir.filePath(u"regions/r.0.0.qcr"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream header(&file);
        header.setVersion(QDataStream::Qt_6_8);
        header.writeRawData("QCRG", 4);
        header << quint16(1) << quint32(payload.size()) << qcCrc32(payload);
        header.writeRawData(payload.constData(), int(payload.size()));
        file.close();

        auto content = std::make_shared<QcContentRegistry>();
        QVERIFY(content->registerBlock(block(u"a:stone"_s)));
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), content).readChunks(warnings);
        QVERIFY(!read.has_value());
        QVERIFY2(read.error().contains(u"older development version"_s), qPrintable(read.error()));
    }

    void blockUpdatesRoundTrip()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), registryWith({u"a:stone"_s}));
        QVERIFY(storage.readBlockUpdates()->isEmpty());
        const QList<QcBlockPos> positions{{1, 2, 3}, {-40, 255, 7}, {0, 0, -1}};
        QVERIFY(storage.writeBlockUpdates(positions).has_value());
        QCOMPARE(*storage.readBlockUpdates(), positions);

        // An empty queue removes the file.
        QVERIFY(storage.writeBlockUpdates({}).has_value());
        QVERIFY(!QFile::exists(dir.filePath(u"blockupdates.qce"_s)));
        QVERIFY(storage.readBlockUpdates()->isEmpty());

        // Positions outside the world make the file damaged.
        QVERIFY(storage.writeBlockUpdates({{0, 256, 0}}).has_value());
        QVERIFY(!storage.readBlockUpdates().has_value());
    }

    void missingPlayerIsNotAnError()
    {
        QTemporaryDir dir;
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), registryWith({})).readPlayer(u"Nobody"_s, warnings);
        QVERIFY(read.has_value());
        QVERIFY(!read->has_value());
    }
};

QTEST_GUILESS_MAIN(TestWorldStorage)
#include "test_world_storage.moc"

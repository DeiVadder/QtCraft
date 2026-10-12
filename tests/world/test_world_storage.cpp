#include "QcCrc32.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcTestUtil.h"
#include "QcWorldConstants.h"
#include "QcWorldStorage.h"

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

    void gameRulesRoundTripAndDefaultForOldWorlds()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), registryWith({}));

        QcWorldInfo info;
        info.name = u"Rules"_s;
        info.keepInventory = false;
        QVERIFY(storage.writeInfo(info).has_value());
        QCOMPARE(storage.readInfo()->keepInventory, false);

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
    }

    void entitiesRoundTripAndUnknownItems()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s, u"mod:gem"_s});
        const QcWorldStorage storage(dir.path(), content);
        QStringList warnings;
        QVERIFY(storage.readEntities(warnings)->items.isEmpty());

        QcWorldEntities entities;
        entities.items.append({{*content->itemId(u"a:stone"_s), 12, 0}, {1.5, 64.0, -2.5}, {0.5, -1.0, 0.0}, 0.25, 42.0});
        entities.items.append({{*content->itemId(u"mod:gem"_s), 3, 0}, {4.5, 70.0, 4.5}, {}, 0.0, 1.0});
        entities.saplings.append({{7, 65, -9}, 33.5});
        QVERIFY(storage.writeEntities(entities).has_value());

        const auto withoutMod = registryWith({u"a:stone"_s});
        const auto read = QcWorldStorage(dir.path(), withoutMod).readEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->items.size(), 1);
        const QcItemEntityManager::Drop &stone = read->items.first();
        QCOMPARE(stone.stack, (QcItemStack{*withoutMod->itemId(u"a:stone"_s), 12, 0}));
        QCOMPARE(stone.position, (QcVec3{1.5, 64.0, -2.5}));
        QCOMPARE(stone.velocity, (QcVec3{0.5, -1.0, 0.0}));
        QCOMPARE(stone.pickupDelay, 0.25);
        QCOMPARE(stone.age, 42.0);
        QCOMPARE(read->saplings.size(), 1);
        QCOMPARE(read->saplings.first().pos, (QcBlockPos{7, 65, -9}));
        QCOMPARE(read->saplings.first().remaining, 33.5);
        QCOMPARE(warnings.size(), 1);

        // A flipped byte fails the checksum.
        QFile file(dir.filePath(u"entities.qce"_s));
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.seek(file.size() - 3);
        file.write("\x7f");
        file.close();
        QVERIFY(!storage.readEntities(warnings).has_value());
    }

    void mobsRoundTripWithPopulatedChunksAndUnknownKinds()
    {
        QTemporaryDir dir;
        auto content = std::make_shared<QcContentRegistry>(*registryWith({u"a:stone"_s}));
        QcMobDefinition pig;
        pig.name = u"a:pig"_s;
        pig.maxHealth = 10.0;
        content->registerMob(pig);
        QcMobDefinition beetle;
        beetle.name = u"mod:beetle"_s;
        content->registerMob(beetle);

        const QcWorldStorage storage(dir.path(), content);
        QStringList warnings;

        // Nothing saved yet: empty, no error.
        QVERIFY(storage.readMobs(warnings)->mobs.isEmpty());
        QcWorldMobs mobs;
        mobs.mobs.append({*content->mobIndex(u"a:pig"_s), {10.5, 70.0, -3.5}, 90.0f, 7.5});
        mobs.mobs.append({*content->mobIndex(u"mod:beetle"_s), {1.0, 65.0, 1.0}, 0.0f, 1.0});
        mobs.populatedChunks = {{0, 0}, {-3, 7}, {100000, -100000}};
        QVERIFY(storage.writeMobs(mobs).has_value());

        // Read without the mod: the beetle is dropped with one warning, the rest comes back.
        auto withoutMod = std::make_shared<QcContentRegistry>(*registryWith({u"a:stone"_s}));
        withoutMod->registerMob(pig);
        const auto read = QcWorldStorage(dir.path(), withoutMod).readMobs(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->mobs.size(), 1);
        QCOMPARE(read->mobs.first().type, *withoutMod->mobIndex(u"a:pig"_s));
        QCOMPARE(read->mobs.first().position, (QcVec3{10.5, 70.0, -3.5}));
        QCOMPARE(read->mobs.first().yaw, 90.0f);
        QCOMPARE(read->mobs.first().health, 7.5);
        QCOMPARE(read->populatedChunks, mobs.populatedChunks);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(u"mod:beetle"_s));

        // An implausible position is rejected, a flipped byte fails the checksum.
        mobs.mobs = {{0, {qInf(), 70.0, 0.0}, 0.0f, 1.0}};
        QVERIFY(storage.writeMobs(mobs).has_value());
        QVERIFY(!storage.readMobs(warnings).has_value());

        mobs.mobs.clear();
        QVERIFY(storage.writeMobs(mobs).has_value());
        QFile file(dir.filePath(u"mobs.qce"_s));
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.seek(file.size() - 3);
        file.write("\x7f");
        file.close();
        QVERIFY(!storage.readMobs(warnings).has_value());
    }

    void fallingBlocksRoundTripAndOldEntityFilesLoad()
    {
        QTemporaryDir dir;
        auto content = registryWith({u"a:sand"_s, u"a:gravel"_s});
        const QcWorldStorage storage(dir.path(), content);

        QcWorldEntities entities;
        QcFallingBlockManager::Falling sand;
        sand.block = *content->blockId(u"a:sand"_s);
        sand.motion.position = {3.5, 71.25, -2.5};
        sand.motion.velocity = {0.0, -4.5, 0.0};
        sand.age = 0.6;
        entities.fallingBlocks = {sand};
        QVERIFY(storage.writeEntities(entities).has_value());

        QStringList warnings;
        const auto read = storage.readEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->fallingBlocks.size(), qsizetype(1));
        QCOMPARE(read->fallingBlocks[0].block, sand.block);
        QCOMPARE(read->fallingBlocks[0].motion.position, sand.motion.position);
        QCOMPARE(read->fallingBlocks[0].motion.velocity, sand.motion.velocity);
        QCOMPARE(read->fallingBlocks[0].age, 0.6);

        // Without the block (a removed mod) the falling block is dropped with a warning.
        const auto without = QcWorldStorage(dir.path(), registryWith({u"a:gravel"_s})).readEntities(warnings);
        QVERIFY(without.has_value());
        QVERIFY(without->fallingBlocks.isEmpty());
        QVERIFY(warnings.join(u'\n').contains(u"a:sand"_s));

        // A version 1 file (items and saplings only) still loads.
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << quint32(0) << quint32(1) << qint32(1) << qint32(70) << qint32(2) << 33.0;
        QByteArray file("QCEN");
        QDataStream header(&file, QIODevice::Append);
        header.setVersion(QDataStream::Qt_6_8);
        header << quint16(1) << quint32(payload.size()) << qcCrc32(payload);
        file.append(payload);
        QFile old(dir.filePath(u"entities.qce"_s));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(file);
        old.close();

        const auto oldRead = storage.readEntities(warnings);
        QVERIFY2(oldRead.has_value(), qcErrorText(oldRead).constData());
        QCOMPARE(oldRead->saplings.size(), qsizetype(1));
        QCOMPARE(oldRead->saplings[0].remaining, 33.0);
        QVERIFY(oldRead->fallingBlocks.isEmpty());
    }

    void overfullStacksAreSplitOnLoad()
    {
        QTemporaryDir dir;
        auto content = std::make_shared<QcContentRegistry>();
        QcItemDefinition tool;
        tool.name = u"a:tool"_s;
        tool.maxStack = 1;
        QVERIFY(content->registerItem(tool).has_value());

        const QcWorldStorage storage(dir.path(), content);
        QcWorldEntities entities;
        entities.items.append({{*content->itemId(u"a:tool"_s), 5, 0}, {0.5, 64.0, 0.5}, {}, 0.0, 0.0});
        QVERIFY(storage.writeEntities(entities).has_value());

        QStringList warnings;
        const auto read = storage.readEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->items.size(), 5);
        QCOMPARE(read->items.last().stack.count, 1);

        // Large stacks of items that allow them come back whole.
        QcItemDefinition sand;
        sand.name = u"a:sand"_s;
        sand.maxStack = 500;
        QVERIFY(content->registerItem(sand).has_value());

        entities.items = {{{*content->itemId(u"a:sand"_s), 300, 0}, {0.5, 64.0, 0.5}, {}, 0.0, 0.0}};
        QVERIFY(storage.writeEntities(entities).has_value());
        const auto large = storage.readEntities(warnings);
        QCOMPARE(large->items.size(), 1);
        QCOMPARE(large->items.first().stack.count, 300);
    }

    void implausibleEntitiesAreRejected()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        const QcWorldStorage storage(dir.path(), content);

        QcWorldEntities entities;
        entities.items.append({{*content->itemId(u"a:stone"_s), 1, 0}, {std::numeric_limits<double>::quiet_NaN(), 64.0, 0.0}, {}, 0.0, 0.0});
        QVERIFY(storage.writeEntities(entities).has_value());
        QStringList warnings;
        QVERIFY(!storage.readEntities(warnings).has_value());

        entities.items.clear();
        entities.saplings.append({{0, 9999, 0}, 1.0});
        QVERIFY(storage.writeEntities(entities).has_value());
        QVERIFY(!storage.readEntities(warnings).has_value());

        // Out-of-range values are clamped instead.
        entities.saplings.clear();
        entities.items.append({{*content->itemId(u"a:stone"_s), 500, 0}, {0.5, 64.0, 0.5}, {}, 99.0, 1e6});
        QVERIFY(storage.writeEntities(entities).has_value());
        const auto read = storage.readEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->items.first().stack.count, 64);
        QCOMPARE(read->items.first().age, QcItemEntityManager::despawnSeconds);
        QCOMPARE(read->items.first().pickupDelay, QcItemEntityManager::thrownPickupDelay);
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
        QVERIFY(QcWorldStorage(dir.path(), content).writePlayer(u"Odd"_s, player).has_value());

        QStringList warnings;
        QVERIFY(!QcWorldStorage(dir.path(), content).readPlayer(u"Odd"_s, warnings).has_value());
    }

    void playerFallingThroughTheVoidLoads()
    {
        // Saved while falling far below the void limit (decision E-288): still the same player.
        QTemporaryDir dir;
        const auto content = registryWith({});
        QcPlayerData player;
        player.motion.position = {0.5, QcWorld::voidFloorY, 0.5};
        player.motion.velocity = {0.0, -QcPhysics::terminalVelocity, 0.0};
        QVERIFY(QcWorldStorage(dir.path(), content).writePlayer(u"Faller"_s, player).has_value());

        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), content).readPlayer(u"Faller"_s, warnings);
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
        QVERIFY(QcWorldStorage(dir.path(), content).writePlayer(u"Hurt"_s, player).has_value());

        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), content).readPlayer(u"Hurt"_s, warnings);
        QVERIFY(read.has_value() && read->has_value());
        QCOMPARE((*read)->health, 0.0);
    }

    void chunksBeyondTheWorldEdgeAreRejected()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{QcWorld::maxHorizontalChunk + 10, 0});
        chunk->setBlock(0, 0, 0, 1);
        const QcWorldStorage storage(dir.path(), content);
        QVERIFY(storage.writeChunks({{chunk->pos(), chunk}}).has_value());
        QStringList warnings;
        QVERIFY(!storage.readChunks(warnings).has_value());
    }

    void foodRoundTripsAndOldPlayersComeBackFull()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        const QcWorldStorage storage(dir.path(), content);
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

        // A version 1 file, written as before hunger existed: the same payload without the food.
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
        QFile old(dir.filePath(u"players/Old.qcp"_s));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(file);
        old.close();

        const auto oldRead = storage.readPlayer(u"Old"_s, warnings);
        QVERIFY2(oldRead.has_value(), qcErrorText(oldRead).constData());
        QCOMPARE((*oldRead)->health, 12.0);
        QCOMPARE((*oldRead)->inventory.slot(0).count, 5);
        QCOMPARE((*oldRead)->food, 20);
        QCOMPARE((*oldRead)->exhaustion, 0.0);

        // Food beyond the range is clamped.
        player.food = 99;
        QVERIFY(storage.writePlayer(u"Hungry"_s, player).has_value());
        QCOMPARE((*storage.readPlayer(u"Hungry"_s, warnings))->food, 20);
    }

    void playerNamesDifferingInCaseGetTheirOwnFiles()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), registryWith({u"a:stone"_s}));
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

    void playerFilesOfOlderVersionsAreRenamedForTheirExactName()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), registryWith({u"a:stone"_s}));
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.health = 7.0;
        QVERIFY(storage.writePlayer(u"Alice"_s, player).has_value());

        // Older versions named the file after the name as typed: players/Alice.qcp.
        const QDir players(dir.filePath(u"players"_s));
        const QString current = players.filePath(QcWorldStorage::playerFileName(u"Alice"_s));
        QVERIFY(QFile::rename(current, players.filePath(u"Alice.qcp"_s)));

        // Another case of the name does not take the file over, even where the file system ignores case.
        QStringList warnings;
        QVERIFY(!*storage.readPlayer(u"alice"_s, warnings));
        QVERIFY(players.entryList(QDir::Files).contains(u"Alice.qcp"_s));

        const auto read = storage.readPlayer(u"Alice"_s, warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE((*read)->health, 7.0);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(players.entryList(QDir::Files), QStringList{QcWorldStorage::playerFileName(u"Alice"_s)});
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
        QVERIFY(QcWorldStorage(dir.path(), before).writeChunks({{chunk->pos(), chunk}}).has_value());

        // Later content lists the values in another order and drops the half property: runtime
        // ids differ, the saved state still means facing east.
        auto after = std::make_shared<QcContentRegistry>();
        QVERIFY(after->registerBlock(stairs({u"west"_s, u"south"_s, u"east"_s, u"north"_s}, false)));
        QVERIFY(after->registerBlock(block(u"a:stone"_s)));
        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), after).readChunks(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        const auto loaded = read->value({2, -3});
        QCOMPARE(after->stateName(loaded->block(1, 64, 1)), u"a:stairs[facing=east]"_s);
        QCOMPARE(after->stateName(loaded->block(2, 64, 1)), u"a:stone"_s);

        // A property the block does not have is reported, not fatal.
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(u"half=top"_s), qPrintable(warnings.first()));
    }

    void oldRegionFilesStillLoad()
    {
        // A region file of format 1, written here byte by byte: plain block names, no states.
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath(u"regions"_s)));
        QcChunk chunk({0, 0});
        chunk.setBlock(3, 70, 4, 0);
        chunk.setBlock(5, 70, 4, 1);
        chunk.setBlock(6, 70, 4, 2);

        QByteArray payload;
        {
            QDataStream stream(&payload, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << quint32(3) << u"a:air_like"_s << u"a:stone"_s << u"a:stairs"_s << quint32(1);
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
        QVERIFY(content->registerBlock(stairs({u"north"_s, u"east"_s, u"south"_s, u"west"_s}, true)));
        QStringList warnings;
        const auto read = QcWorldStorage(dir.path(), content).readChunks(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        const auto loaded = read->value({0, 0});
        QVERIFY(loaded);
        QCOMPARE(content->stateName(loaded->block(5, 70, 4)), u"a:stone"_s);

        // A plain name of a block with states loads as its default state.
        QCOMPARE(content->stateName(loaded->block(6, 70, 4)), u"a:stairs[facing=north,half=bottom]"_s);
        QCOMPARE(loaded->block(3, 70, 4), qcAirBlockId);

        // The unknown name from palette slot 0 becomes air with a warning.
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(u"a:air_like"_s));

        // Saving again writes format 2 with states, which reads back the same.
        QVERIFY(QcWorldStorage(dir.path(), content).writeChunks({{loaded->pos(), loaded}}).has_value());
        QFile again(dir.filePath(u"regions/r.0.0.qcr"_s));
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.read(6).mid(4), QByteArray("\x00\x02", 2));
        again.close();

        warnings.clear();
        const auto reread = QcWorldStorage(dir.path(), content).readChunks(warnings);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(reread->value({0, 0})->block(6, 70, 4), loaded->block(6, 70, 4));
    }

    void containersRoundTripAndUnknownItems()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s, u"mod:gem"_s});
        const QcWorldStorage storage(dir.path(), content);
        QStringList warnings;
        QVERIFY(storage.readContainers(warnings)->isEmpty());

        QcContainerManager::Container chest{{4, 70, -9}, QList<QcItemStack>(27)};
        chest.stacks[0] = {*content->itemId(u"a:stone"_s), 64, 0};
        chest.stacks[26] = {*content->itemId(u"mod:gem"_s), 3, 0};
        QcContainerManager::Container small{{-4, 10, 9}, QList<QcItemStack>(5)};
        QVERIFY(storage.writeContainers({chest, small}).has_value());

        const auto withoutMod = registryWith({u"a:stone"_s});
        const auto read = QcWorldStorage(dir.path(), withoutMod).readContainers(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->size(), 2);
        QCOMPARE(read->at(0).pos, chest.pos);
        QCOMPARE(read->at(0).stacks.size(), 27);
        QCOMPARE(read->at(0).stacks[0], (QcItemStack{*withoutMod->itemId(u"a:stone"_s), 64, 0}));
        QVERIFY(read->at(0).stacks[26].isEmpty());
        QCOMPARE(read->at(1).stacks.size(), 5);
        QCOMPARE(warnings.size(), 1);

        // A damaged file stops loading with a message.
        QFile file(dir.filePath(u"containers.qce"_s));
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.seek(20);
        file.write("\x7f", 1);
        file.close();
        const auto damaged = storage.readContainers(warnings);
        QVERIFY(!damaged.has_value());
        QVERIFY(damaged.error().contains(u"containers.qce"_s));

        // A container without slots cannot come from a storage block: the file is damaged.
        QVERIFY(storage.writeContainers({chest, {{1, 2, 3}, {}}}).has_value());
        const auto empty = storage.readContainers(warnings);
        QVERIFY(!empty.has_value());
        QVERIFY(empty.error().contains(u"container 1"_s));
    }

    void blockUpdatesRoundTrip()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), registryWith({u"a:stone"_s}));
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

    void machinesRoundTripAndUnknownItems()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s, u"mod:gem"_s});
        const QcWorldStorage storage(dir.path(), content);
        QStringList warnings;
        QVERIFY(storage.readMachines(warnings)->isEmpty());

        QcMachineManager::Machine furnace;
        furnace.pos = {-12, 64, 7};
        furnace.stacks = {QcItemStack{*content->itemId(u"a:stone"_s), 9, 0}, QcItemStack{*content->itemId(u"mod:gem"_s), 2, 0},
                          QcItemStack{}};
        furnace.burnRemaining = 12.5;
        furnace.burnSeconds = 80.0;
        furnace.progress = 3.25;

        QcMachineManager::Machine empty;
        empty.pos = {100, 3, -100};
        QVERIFY(storage.writeMachines({furnace, empty}).has_value());

        const auto withoutMod = registryWith({u"a:stone"_s});
        const auto read = QcWorldStorage(dir.path(), withoutMod).readMachines(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->size(), 2);
        const QcMachineManager::Machine &loaded = read->at(0);
        QCOMPARE(loaded.pos, furnace.pos);
        QCOMPARE(loaded.stacks[0], (QcItemStack{*withoutMod->itemId(u"a:stone"_s), 9, 0}));
        QVERIFY(loaded.stacks[1].isEmpty());
        QCOMPARE(loaded.burnRemaining, 12.5);
        QCOMPARE(loaded.burnSeconds, 80.0);
        QCOMPARE(loaded.progress, 3.25);
        QCOMPARE(read->at(1).pos, empty.pos);
        QCOMPARE(warnings.size(), 1);
    }

    void damagedMachineFilesFailWithAMessage_data()
    {
        QTest::addColumn<int>("cut");
        QTest::addColumn<int>("flipAt");
        QTest::newRow("truncated") << 6 << -1;
        QTest::newRow("only the header") << -1 << 14;
        QTest::newRow("bit flip") << 0 << 20;
    }

    void damagedMachineFilesFailWithAMessage()
    {
        QFETCH(int, cut);
        QFETCH(int, flipAt);

        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        const QcWorldStorage storage(dir.path(), content);
        QcMachineManager::Machine furnace;
        furnace.pos = {1, 64, 1};
        furnace.stacks[0] = {*content->itemId(u"a:stone"_s), 3, 0};
        QVERIFY(storage.writeMachines({furnace}).has_value());

        const QString path = dir.filePath(u"machines.qce"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray data = file.readAll();
        if (cut > 0) {
            data.chop(cut);
        } else if (cut < 0) {
            data = data.left(flipAt);
        } else {
            data[flipAt] = char(data[flipAt] ^ 0x40);
        }
        file.resize(0);
        file.seek(0);
        file.write(data);
        file.close();

        QStringList warnings;
        const auto read = storage.readMachines(warnings);
        QVERIFY(!read.has_value());
        QVERIFY2(read.error().contains(path), qPrintable(read.error()));
    }

    void implausibleMachinesAreRejected()
    {
        QTemporaryDir dir;
        const auto content = registryWith({u"a:stone"_s});
        const QcWorldStorage storage(dir.path(), content);
        QcMachineManager::Machine machine;
        machine.pos = {1, 64, 1};
        QStringList warnings;

        // Two machines at one position, positions outside the world and broken times are damage.
        QVERIFY(storage.writeMachines({machine, machine}).has_value());
        QVERIFY(!storage.readMachines(warnings).has_value());

        machine.pos = {1, -3, 1};
        QVERIFY(storage.writeMachines({machine}).has_value());
        QVERIFY(!storage.readMachines(warnings).has_value());

        machine.pos = {1, 64, 1};
        machine.progress = std::numeric_limits<double>::infinity();
        QVERIFY(storage.writeMachines({machine}).has_value());
        QVERIFY(!storage.readMachines(warnings).has_value());

        // Out-of-range values are clamped instead.
        machine.progress = -5.0;
        machine.burnSeconds = 10.0;
        machine.burnRemaining = 50.0;
        machine.stacks[0] = {*content->itemId(u"a:stone"_s), 500, 0};
        QVERIFY(storage.writeMachines({machine}).has_value());
        const auto read = storage.readMachines(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->first().progress, 0.0);
        QCOMPARE(read->first().burnRemaining, 10.0);
        QCOMPARE(read->first().stacks[0].count, 64);
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

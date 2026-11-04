// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSession.h"
#include "common/content/worldgen/QcDimensionParser.h"
#include "fixtures/QcTestUtil.h"
#include "fixtures/QcTestWorldFiles.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerState.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// The test dimensions of decision E-978 (only in tests): an empty one and one like the end, with
// no floor, a fixed arrival point and a platform.
const QJsonObject emptyDimension{{u"name"_s, u"test:empty"_s},
                                 {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
                                 {u"sky"_s, u"nether"_s},
                                 {u"skyLight"_s, false},
                                 {u"dayCycle"_s, false},
                                 {u"weather"_s, false},
                                 {u"ceiling"_s, 127},
                                 {u"coordinateScale"_s, 8},
                                 {u"ambientLight"_s, 2},
                                 {u"fogColour"_s, u"#330808"_s},
                                 {u"waterEvaporates"_s, true},
                                 {u"bedsExplode"_s, true},
                                 {u"compassesSpin"_s, true},
                                 {u"liquids"_s, QJsonObject{{u"qtcraft:lava"_s, QJsonObject{{u"distance"_s, 7}, {u"ticks"_s, 10}}}}}};

const QJsonObject endLikeDimension{{u"name"_s, u"test:end_like"_s},
                                   {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
                                   {u"sky"_s, u"end"_s},
                                   {u"skyLight"_s, false},
                                   {u"dayCycle"_s, false},
                                   {u"weather"_s, false},
                                   {u"floor"_s, u"void"_s},
                                   {u"voidDamage"_s, 4},
                                   {u"arrival"_s, QJsonObject{{u"x"_s, 100}, {u"y"_s, 48}, {u"z"_s, 0}, {u"platform"_s, u"stone"_s}, {u"platformRadius"_s, 2}}}};

QcDimensionDefinition parsed(const QJsonObject &json)
{
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    return std::move(*dimension);
}

// The base content with the two test dimensions.
std::shared_ptr<QcContentRegistry> contentWithTestDimensions()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    content->worldgen().registerDimension(parsed(emptyDimension));
    content->worldgen().registerDimension(parsed(endLikeDimension));
    return content;
}

QcServerConfig configIn(const QString &directory)
{
    QcServerConfig config;
    config.worldName = u"Dimensions"_s;
    config.seed = 4711;
    config.viewDistance = 2;
    config.worldDirectory = directory;
    return config;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

void writeJson(const QString &path, const QJsonObject &json)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson()) < 0) {
        qFatal("cannot write %s", qPrintable(path));
    }
}

// The versions world.json at `directory` keeps of the other dimensions.
QJsonObject storedVersions(const QString &directory)
{
    return readJson(QDir(directory).filePath(u"world.json"_s)).value(u"generator"_s).toObject().value(u"dimensions"_s).toObject();
}

// The stored version of the dimension `name` in world.json at `directory`, 0 for none.
int storedVersion(const QString &directory, const QString &name)
{
    return storedVersions(directory).value(name).toObject().value(u"version"_s).toInt();
}

// `count` entries of "generator"/"dimensions", as dimensions of removed mods leave them.
QJsonObject versionEntries(qsizetype count)
{
    QJsonObject entries;
    for (qsizetype i = 0; i < count; ++i) {
        entries.insert(u"mod:gone_%1"_s.arg(i), QJsonObject{{u"version"_s, 1}});
    }
    return entries;
}

// The same as a map, for QcWorldInfo.
QMap<QString, int> versionMap(qsizetype count)
{
    QMap<QString, int> versions;
    for (qsizetype i = 0; i < count; ++i) {
        versions.insert(u"mod:gone_%1"_s.arg(i), 1);
    }
    return versions;
}

// A saved world of a game without the test dimensions, whose setup is done: it names no version of
// them, like a world older than a dimension.
void makeWorldWithoutDimensions(const QString &directory)
{
    const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
    QcLocalServerHost host(configIn(directory), content, std::make_unique<QcTestTerrainGenerator>(content));
    if (!host.startError().isEmpty() || !host.shutdown().has_value()) {
        qFatal("cannot make the test world: %s", qPrintable(host.startError()));
    }
}

QString parseError(const QJsonObject &json)
{
    const QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    return dimension ? QString() : dimension.error();
}

} // namespace

// Dimensions as data, opened by the server state, and what a world keeps of them (decisions E-978,
// E-979).
class TestDimensions : public QObject
{
    Q_OBJECT

private slots:
    void theFieldsOfADimensionParse()
    {
        const QcDimensionDefinition empty = parsed(emptyDimension);
        QCOMPARE(empty.name, u"test:empty"_s);
        QCOMPARE(empty.translationKey, u"dimension.test.empty"_s);
        QCOMPARE(empty.generatorType, u"void"_s);
        QCOMPARE(empty.ceiling, 127);
        QCOMPARE(empty.coordinateScale, 8.0);
        QCOMPARE(empty.sky, QcDimensionDefinition::Sky::Nether);
        QCOMPARE(empty.fogColour, std::optional<quint32>(0x330808));
        QCOMPARE(empty.ambientLight, 2);
        QVERIFY(!empty.skyLight && !empty.dayCycle && !empty.weather);
        QVERIFY(empty.waterEvaporates && empty.bedsExplode && empty.compassesSpin);
        QCOMPARE(empty.liquidFlows.size(), 1);
        QCOMPARE(empty.liquidFlows.front().liquid, u"qtcraft:lava"_s);
        QCOMPARE(empty.liquidFlows.front().distance, std::optional<int>(7));
        QCOMPARE(empty.liquidFlows.front().ticks, std::optional<int>(10));
        QCOMPARE(empty.floor, QcDimensionDefinition::Floor::Bedrock);
        QVERIFY(!empty.arrival);

        const QcDimensionDefinition endLike = parsed(endLikeDimension);
        QCOMPARE(endLike.sky, QcDimensionDefinition::Sky::End);
        QCOMPARE(endLike.floor, QcDimensionDefinition::Floor::Void);
        QCOMPARE(endLike.voidDamage, 4.0);
        QVERIFY(endLike.arrival.has_value());
        QCOMPARE(endLike.arrival->position, (QcBlockPos{100, 48, 0}));
        QCOMPARE(endLike.arrival->platform, u"test:stone"_s);
        QCOMPARE(endLike.arrival->platformRadius, 2);
    }

    void aDimensionWithoutFieldsIsLikeTheOverworld()
    {
        const QcDimensionDefinition plain = parsed({{u"name"_s, u"qtcraft:overworld"_s}});
        const QcDimensionDefinition defaults = QcDimensionDefinition::defaultOverworld();
        QCOMPARE(plain.name, defaults.name);
        QCOMPARE(plain.translationKey, defaults.translationKey);
        QCOMPARE(plain.generatorType, defaults.generatorType);
        QCOMPARE(plain.sky, defaults.sky);
        QCOMPARE(plain.coordinateScale, defaults.coordinateScale);
        QCOMPARE(plain.skyLight, defaults.skyLight);
        QCOMPARE(plain.floor, defaults.floor);
        QVERIFY(!plain.fogColour && !plain.arrival && plain.liquidFlows.isEmpty());
    }

    void wrongFieldsAreRefused_data()
    {
        QTest::addColumn<QJsonObject>("json");
        QTest::addColumn<QString>("reason");
        QTest::newRow("no name") << QJsonObject{{u"sky"_s, u"end"_s}} << u"'name' is required"_s;
        QTest::newRow("name too long") << QJsonObject{{u"name"_s, QString(QcDimensionDefinition::maxNameLength + 1, u'x')}} << u"too long"_s;
        QTest::newRow("slash") << QJsonObject{{u"name"_s, u"a/b"_s}} << u"'/' or '.'"_s;
        QTest::newRow("dot") << QJsonObject{{u"name"_s, u".."_s}} << u"'/' or '.'"_s;
        QTest::newRow("sky") << QJsonObject{{u"name"_s, u"x"_s}, {u"sky"_s, u"blue"_s}} << u"'sky'"_s;
        QTest::newRow("floor") << QJsonObject{{u"name"_s, u"x"_s}, {u"floor"_s, u"lava"_s}} << u"'floor'"_s;
        QTest::newRow("scale") << QJsonObject{{u"name"_s, u"x"_s}, {u"coordinateScale"_s, 0}} << u"coordinateScale"_s;
        QTest::newRow("ceiling") << QJsonObject{{u"name"_s, u"x"_s}, {u"ceiling"_s, 300}} << u"ceiling"_s;
        QTest::newRow("world type elsewhere") << QJsonObject{{u"name"_s, u"x"_s}, {u"generator"_s, QJsonObject{{u"type"_s, u"world"_s}}}}
                                              << u"world's own type"_s;
        QTest::newRow("no generator elsewhere") << QJsonObject{{u"name"_s, u"x"_s}} << u"world's own type"_s;
        QTest::newRow("liquid values") << QJsonObject{{u"name"_s, u"x"_s}, {u"liquids"_s, QJsonObject{{u"lava"_s, QJsonObject{{u"ticks"_s, 0}}}}}}
                                       << u"ticks"_s;
        QTest::newRow("arrival") << QJsonObject{{u"name"_s, u"x"_s}, {u"arrival"_s, QJsonObject{{u"y"_s, 400}}}} << u"arrival"_s;
    }

    void wrongFieldsAreRefused()
    {
        QFETCH(QJsonObject, json);
        QFETCH(QString, reason);
        // Other dimensions than the overworld need a generator of their own.
        if (!json.contains(u"generator"_s) && reason != u"world's own type"_s) {
            json.insert(u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}});
        }
        const QString error = parseError(json);
        QVERIFY2(error.contains(reason), qPrintable(error));
    }

    void theLongestNameFitsWhatTheWorldKeeps()
    {
        const QString prefix = u"test:"_s;
        const QString name = prefix + QString(QcDimensionDefinition::maxNameLength - prefix.size(), u'x');
        const QcDimensionDefinition longest = parsed({{u"name"_s, name}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}}}});
        QCOMPARE(longest.name, name);
        const QString overLimit = parseError({{u"name"_s, QString(name + u'x')}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}}}});
        QVERIFY2(overLimit.contains(u"too long"_s), qPrintable(overLimit));

        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcWorldInfo info;
        info.name = u"Longest"_s;
        info.dimensionGeneratorVersions = {{name, 2}};
        QVERIFY(storage.writeInfo(info).has_value());
        const QcResult<QcWorldInfo> read = storage.readInfo();
        QVERIFY2(read, qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->dimensionGeneratorVersions.value(name), 2);

        QcPlayerData player;
        player.dimension = name;
        const QcPlayerStorage::Result readPlayer = storage.parsePlayer(u"Far"_s, storage.playerBytes(player));
        QVERIFY(readPlayer && readPlayer->player);
        QCOMPARE(readPlayer->player->dimension, name);
    }

    void theBaseGameHasTheOverworldAsData()
    {
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        const QcDimensionDefinition *overworld = content->worldgen().dimension(QString::fromLatin1(QcDimensionDefinition::overworldName));
        QVERIFY(overworld != nullptr);
        QCOMPARE(&content->worldgen().overworld(), overworld);
        QCOMPARE(overworld->generatorType, QString::fromLatin1(QcDimensionDefinition::worldGenerator));
        QCOMPARE(overworld->sky, QcDimensionDefinition::Sky::Overworld);
        // The base game of 1.1.0 brings the nether with stage 83.3; until then the overworld only.
        QCOMPARE(content->worldgen().dimensions().size(), 1);
    }

    void contentWithoutDimensionsPlaysTheDefaultOverworld()
    {
        const auto content = std::make_shared<QcContentRegistry>();
        QVERIFY(content->worldgen().dimensions().isEmpty());
        QCOMPARE(content->worldgen().overworld().name, QString::fromLatin1(QcDimensionDefinition::overworldName));
    }

    void theStateOpensOtherDimensionsBesideTheOverworld()
    {
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        QcServerState state(configIn({}), content, std::make_unique<QcTestTerrainGenerator>(content));
        QCOMPARE(state.dimensions().all().size(), std::size_t{1});
        QCOMPARE(&state.world(), &state.dimensions().overworld().world());

        QcResult<QcServerDimension *> empty = state.openDimension(u"test:empty"_s);
        QVERIFY2(empty, qPrintable(empty ? QString() : empty.error()));
        QCOMPARE((*empty)->name(), u"test:empty"_s);
        QVERIFY(&(*empty)->world() != &state.world());
        // Opening again gives the same dimension.
        QCOMPARE(*state.openDimension(u"test:empty"_s), *empty);
        QCOMPARE(state.dimensions().all().size(), std::size_t{2});

        // Each world has its own chunks: a block set in one is not in the other.
        QcServerWorld &other = (*empty)->world();
        QVERIFY(other.loadChunk({0, 0}) != nullptr);
        QVERIFY(state.world().loadChunk({0, 0}) != nullptr);
        const QcBlockId stone = *content->blockId(u"qtcraft:stone"_s);
        QVERIFY(other.setBlock({3, 200, 3}, stone));
        QCOMPARE(other.blockAt({3, 200, 3}), std::optional<QcBlockId>(stone));
        QCOMPARE(state.world().blockAt({3, 200, 3}), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(other.blockAt({0, 1, 0}), std::optional<QcBlockId>(stone));
        QCOMPARE(other.blockAt({5, 1, 5}), std::optional<QcBlockId>(qcAirBlockId));
    }

    void anOpenedDimensionKeepsItsGeneratorVersion()
    {
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        QcServerState state(configIn({}), content, std::make_unique<QcTestTerrainGenerator>(content));
        state.info().generator = QString::fromLatin1(QcWorldGenerators::terrain);
        state.info().dimensionGeneratorVersions.clear();

        // A world older than the dimension gets the newest frozen version, for good. Compared with
        // the constant, not a number: lastFrozen moves with each release (Series has fixed numbers
        // in theVersionOfADimensionComesFromTheWorld).
        QcResult<QcServerDimension *> endLike = state.openDimension(u"test:end_like"_s);
        QVERIFY(endLike);
        QCOMPARE((*endLike)->generatorVersion(), QcGeneratorVersion::lastFrozen);
        QCOMPARE(state.info().dimensionGeneratorVersions.value(u"test:end_like"_s), QcGeneratorVersion::lastFrozen);
        QVERIFY(!state.info().dimensionGeneratorVersions.contains(QString::fromLatin1(QcDimensionDefinition::overworldName)));
    }

    void aDimensionOpenedLateKeepsItsVersionAcrossRestarts()
    {
        QTemporaryDir dir;
        makeWorldWithoutDimensions(dir.path());
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        const QString name = u"test:empty"_s;

        // The first opening records the version in the saved world.
        {
            QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY(server.openWorld().has_value());
            const QcResult<QcServerDimension *> opened = server.openDimension(name);
            QVERIFY2(opened, qPrintable(opened ? QString() : opened.error()));
            QCOMPARE((*opened)->generatorVersion(), QcGeneratorVersion::lastFrozen);
            QVERIFY(server.save().has_value());
        }
        QCOMPARE(storedVersion(dir.path(), name), QcGeneratorVersion::lastFrozen);

        // A run that does not open it keeps the entry.
        {
            QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY(server.openWorld().has_value());
            QVERIFY(server.save().has_value());
        }
        QCOMPARE(storedVersion(dir.path(), name), QcGeneratorVersion::lastFrozen);

        // What is stored decides from then on, not what a late dimension would get: a version other
        // than lastFrozen shows that the entry is read back.
        const int stored = QcGeneratorVersion::lastFrozen == 1 ? 2 : 1;
        const QString worldJson = QDir(dir.path()).filePath(u"world.json"_s);
        QJsonObject json = readJson(worldJson);
        QJsonObject generator = json.value(u"generator"_s).toObject();
        generator.insert(u"dimensions"_s, QJsonObject{{name, QJsonObject{{u"version"_s, stored}}}});
        json.insert(u"generator"_s, generator);
        writeJson(worldJson, json);
        {
            QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY(server.openWorld().has_value());
            const QcResult<QcServerDimension *> opened = server.openDimension(name);
            QVERIFY2(opened, qPrintable(opened ? QString() : opened.error()));
            QCOMPARE((*opened)->generatorVersion(), stored);
            QVERIFY(server.save().has_value());
        }
        QCOMPARE(storedVersion(dir.path(), name), stored);
    }

    void everyOpenDimensionHasTheWorldLimits()
    {
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        QcServerConfig config = configIn({});
        config.limits.maxContainers = 11;
        config.limits.fires = 12;
        config.limits.villages = 13;
        QcServerState state(std::move(config), content, std::make_unique<QcTestTerrainGenerator>(content));

        // Opened after the limits were set ...
        QcResult<QcServerDimension *> opened = state.openDimension(u"test:empty"_s);
        QVERIFY(opened);
        QcServerWorld &world = (*opened)->world();
        QCOMPARE(world.blockEntities().limits().containers, qsizetype{11});
        QCOMPARE(world.fires().limits().total, qsizetype{12});
        QCOMPARE(world.villages().limits().villages, qsizetype{13});

        // ... and when they change (a world's own limits replace the config's on opening it).
        state.config().limits.maxContainers = 21;
        state.config().limits.fires = 22;
        state.config().limits.villages = 23;
        state.applyLimits();
        for (const QcServerWorld *each : {&world, &state.world()}) {
            QCOMPARE(each->blockEntities().limits().containers, qsizetype{21});
            QCOMPARE(each->fires().limits().total, qsizetype{22});
            QCOMPARE(each->villages().limits().villages, qsizetype{23});
        }
    }

    void dimensionsThatCannotOpenAreRefused()
    {
        const std::shared_ptr<QcContentRegistry> content = contentWithTestDimensions();
        QcDimensionDefinition broken = parsed({{u"name"_s, u"broken"_s}, {u"generator"_s, QJsonObject{{u"type"_s, u"nether-of-a-mod"_s}}}});
        content->worldgen().registerDimension(broken);
        QcServerState state(configIn({}), content, std::make_unique<QcTestTerrainGenerator>(content));

        const QcResult<QcServerDimension *> unknown = state.openDimension(u"test:missing"_s);
        QVERIFY(!unknown && unknown.error().contains(u"no dimension"_s));
        const QcResult<QcServerDimension *> badType = state.openDimension(u"test:broken"_s);
        QVERIFY2(!badType && badType.error().contains(u"nether-of-a-mod"_s), qPrintable(badType ? QString() : badType.error()));
        // A failed opening leaves nothing for good: the mod's next version may open it.
        QVERIFY(!state.info().dimensionGeneratorVersions.contains(u"test:broken"_s));

        // A version this game does not generate, named by world.json, is refused with the reason.
        state.info().dimensionGeneratorVersions.insert(u"test:empty"_s, QcGeneratorVersion::current + 1);
        const QcResult<QcServerDimension *> newer = state.openDimension(u"test:empty"_s);
        QVERIFY2(!newer && newer.error().contains(u"version %1"_s.arg(QcGeneratorVersion::current + 1)), qPrintable(newer ? QString() : newer.error()));
        QCOMPARE(state.dimensions().all().size(), std::size_t{1});
    }

    void theVersionOfADimensionComesFromTheWorld()
    {
        const QcDimensionDefinition empty = parsed(emptyDimension);
        QcWorldInfo info;
        info.generator = QString::fromLatin1(QcWorldGenerators::terrain);
        info.generatorVersion = 1;
        QCOMPARE(QcDimensions::versionIn(info, QcDimensionDefinition::defaultOverworld()), 1);
        QCOMPARE(QcDimensions::versionIn(info, empty), QcGeneratorVersion::lastFrozen);
        info.dimensionGeneratorVersions.insert(empty.name, 2);
        QCOMPARE(QcDimensions::versionIn(info, empty), 2);
        // The showcase is not versioned: the current version whatever world.json says.
        info.generator = QString::fromLatin1(QcWorldGenerators::showcase);
        info.dimensionGeneratorVersions.insert(empty.name, 1);
        QCOMPARE(QcDimensions::versionIn(info, empty), QcGeneratorVersion::current);

        // A series without a frozen version gives its open one to late dimensions.
        constexpr QcGeneratorVersion::Series open{1, 1, 0};
        QCOMPARE(open.forLateDimension(), 1);
        constexpr QcGeneratorVersion::Series frozen{1, 3, 2};
        QCOMPARE(frozen.forLateDimension(), 2);
    }

    void newWorldsGiveEveryDimensionTheCurrentVersion()
    {
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        QcWorldInfo info;
        info.generator = QString::fromLatin1(QcWorldGenerators::terrain);
        const QMap<QString, int> versions = QcDimensions::versionsForNewWorld(*content, info);
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions.value(u"test:empty"_s), QcGeneratorVersion::current);
        info.generator = QString::fromLatin1(QcWorldGenerators::showcase);
        QVERIFY(QcDimensions::versionsForNewWorld(*content, info).isEmpty());
    }

    void aWorldNeverKeepsMoreVersionsThanItCanRead()
    {
        // More dimensions than world.json holds: a new world records as many as it can read back.
        const std::shared_ptr<QcContentRegistry> content = contentWithTestDimensions();
        for (qsizetype i = 0; i < QcWorldInfo::maxDimensionVersions + 10; ++i) {
            content->worldgen().registerDimension(parsed({{u"name"_s, u"many_%1"_s.arg(i)}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}}}}));
        }
        QcWorldInfo info;
        info.name = u"Crowded"_s;
        info.generator = QString::fromLatin1(QcWorldGenerators::terrain);
        info.dimensionGeneratorVersions = QcDimensions::versionsForNewWorld(*content, info);
        QCOMPARE(info.dimensionGeneratorVersions.size(), QcWorldInfo::maxDimensionVersions);
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), content);
        QVERIFY(storage.writeInfo(info).has_value());
        const QcResult<QcWorldInfo> read = storage.readInfo();
        QVERIFY2(read, qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->dimensionGeneratorVersions, info.dimensionGeneratorVersions);
    }

    void aWorldFullOfVersionsRefusesANewDimensionAndStaysOpen()
    {
        // Entries of removed mods stay, so a full list refuses the next dimension with the reason.
        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        QcServerState state(configIn({}), content, std::make_unique<QcTestTerrainGenerator>(content));
        state.info().generator = QString::fromLatin1(QcWorldGenerators::terrain);
        state.info().dimensionGeneratorVersions = versionMap(QcWorldInfo::maxDimensionVersions);
        const QcResult<QcServerDimension *> refused = state.openDimension(u"test:empty"_s);
        QVERIFY2(!refused && refused.error().contains(u"keeps the versions"_s), qPrintable(refused ? QString() : refused.error()));
        QCOMPARE(state.info().dimensionGeneratorVersions.size(), QcWorldInfo::maxDimensionVersions);

        // One that has its entry already still opens.
        state.info().dimensionGeneratorVersions.remove(u"mod:gone_0"_s);
        state.info().dimensionGeneratorVersions.insert(u"test:empty"_s, 2);
        const QcResult<QcServerDimension *> kept = state.openDimension(u"test:empty"_s);
        QVERIFY2(kept, qPrintable(kept ? QString() : kept.error()));
        QCOMPARE(state.info().dimensionGeneratorVersions.size(), QcWorldInfo::maxDimensionVersions);

        // The full list reads back: the world is not damaged by it.
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), content);
        QcWorldInfo info = state.info();
        info.name = u"Full"_s;
        QVERIFY(storage.writeInfo(info).has_value());
        QVERIFY(storage.readInfo().has_value());
    }

    void eachDimensionHasItsOwnFolder()
    {
        const QString world = u"/worlds/a"_s;
        const auto folderOf = [&world](const QString &name) {
            QcDimensionDefinition dimension;
            dimension.name = name;
            return QcWorldStorage::dimensionDirectory(world, dimension);
        };
        QCOMPARE(folderOf(QString::fromLatin1(QcDimensionDefinition::overworldName)), world);
        QCOMPARE(folderOf(u"qtcraft:nether"_s), u"/worlds/a/dimensions/qtcraft/nether"_s);
        QCOMPARE(folderOf(u"mod:sky_isles"_s), u"/worlds/a/dimensions/mod/sky_isles"_s);
        // Windows opens a device for these names, whatever the case.
        QCOMPARE(folderOf(u"mod:con"_s), u"/worlds/a/dimensions/mod/con~"_s);
        QCOMPARE(folderOf(u"nul:LPT1"_s), u"/worlds/a/dimensions/nul~/LPT1~"_s);
        QCOMPARE(folderOf(u"mod:Com9"_s), u"/worlds/a/dimensions/mod/Com9~"_s);
        QCOMPARE(folderOf(u"mod:console"_s), u"/worlds/a/dimensions/mod/console"_s);
        QCOMPARE(folderOf(u"mod:com10"_s), u"/worlds/a/dimensions/mod/com10"_s);
    }

    void worldJsonKeepsTheVersionsOfTheDimensions()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcWorldInfo info;
        info.name = u"Kept"_s;
        info.generatorVersion = 1;
        info.dimensionGeneratorVersions = {{u"test:empty"_s, 2}, {u"mod:gone"_s, 7}};
        QVERIFY(storage.writeInfo(info).has_value());

        const QJsonObject json = readJson(QDir(dir.path()).filePath(u"world.json"_s));
        QCOMPARE(json.value(u"formatVersion"_s).toInt(), QcWorldInfo::formatVersion);
        const QJsonObject generator = json.value(u"generator"_s).toObject();
        QCOMPARE(generator.value(u"version"_s).toInt(), 1);
        QCOMPARE(generator.value(u"dimensions"_s).toObject().value(u"test:empty"_s).toObject().value(u"version"_s).toInt(), 2);

        // A dimension the game does not know (a removed mod) keeps its entry.
        const QcResult<QcWorldInfo> read = storage.readInfo();
        QVERIFY(read);
        QCOMPARE(read->generatorVersion, 1);
        QCOMPARE(read->dimensionGeneratorVersions, info.dimensionGeneratorVersions);
    }

    void damagedVersionsOfDimensionsAreRefused_data()
    {
        QTest::addColumn<QJsonValue>("dimensions");
        QTest::newRow("no object") << QJsonValue(3);
        QTest::newRow("no number") << QJsonValue(QJsonObject{{u"test:empty"_s, QJsonObject{{u"version"_s, u"2"_s}}}});
        QTest::newRow("fraction") << QJsonValue(QJsonObject{{u"test:empty"_s, QJsonObject{{u"version"_s, 1.5}}}});
        QTest::newRow("zero") << QJsonValue(QJsonObject{{u"test:empty"_s, QJsonObject{{u"version"_s, 0}}}});
        QTest::newRow("too high") << QJsonValue(QJsonObject{{u"test:empty"_s, QJsonObject{{u"version"_s, QcGeneratorVersion::maxStored + 1}}}});
        QTest::newRow("too many") << QJsonValue(versionEntries(QcWorldInfo::maxDimensionVersions + 1));
        QTest::newRow("name too long") << QJsonValue(QJsonObject{{QString(QcDimensionDefinition::maxNameLength + 1, u'x'), QJsonObject{{u"version"_s, 1}}}});
    }

    void damagedVersionsOfDimensionsAreRefused()
    {
        QFETCH(QJsonValue, dimensions);
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcWorldInfo info;
        info.name = u"Damaged"_s;
        QVERIFY(storage.writeInfo(info).has_value());
        const QString path = QDir(dir.path()).filePath(u"world.json"_s);
        QJsonObject json = readJson(path);
        QJsonObject generator = json.value(u"generator"_s).toObject();
        generator.insert(u"dimensions"_s, dimensions);
        json.insert(u"generator"_s, generator);
        writeJson(path, json);

        const QcResult<QcWorldInfo> read = storage.readInfo();
        QVERIFY(!read);
        QVERIFY2(read.error().contains(u"damaged"_s), qPrintable(read.error()));
    }

    void playersKeepTheirDimension()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcPlayerData player;
        QCOMPARE(player.dimension, QString::fromLatin1(QcDimensionDefinition::overworldName));
        player.dimension = u"test:end_like"_s;
        QVERIFY(storage.writePlayer(u"Traveller"_s, player).has_value());
        const QcPlayerStorage::Result read = storage.readPlayer(u"Traveller"_s);
        QVERIFY(read && read->player);
        QCOMPARE(read->player->dimension, u"test:end_like"_s);
    }

    void playerFilesBeforeDimensionsAreInTheOverworld()
    {
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcPlayerData player;
        player.dimension = u"test:empty"_s;
        // A file of version 10 is the same payload without the dimension, the flag of its return and
        // the extra health and the saturation (version 12) at its end.
        const QByteArray file = storage.playerBytes(player);
        const QByteArrayView magic = QByteArrayView(file).first(4);
        const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(file, u"player"_s, magic, 12);
        QVERIFY(payload && payload->version == 12);
        const qsizetype dimensionBytes = 4 + 2 * player.dimension.size() + 1 + static_cast<qsizetype>(2 * sizeof(double));
        const QByteArray older = QcSaveFile::bytes(magic, payload->bytes.chopped(dimensionBytes), 10);

        const QcPlayerStorage::Result read = storage.parsePlayer(u"Old"_s, older);
        QVERIFY2(read && read->player, qPrintable(read ? QString() : read.error().error));
        QCOMPARE(read->player->dimension, QString::fromLatin1(QcDimensionDefinition::overworldName));
    }

    void aPlayerFileWithAWrongDimensionNameIsDamaged_data()
    {
        QTest::addColumn<QString>("dimension");
        QTest::newRow("empty") << QString();
        QTest::newRow("too long") << QString(QcDimensionDefinition::maxNameLength + 1, u'x');
    }

    void aPlayerFileWithAWrongDimensionNameIsDamaged()
    {
        QFETCH(QString, dimension);
        QTemporaryDir dir;
        const QcTestWorldFiles storage(dir.path(), contentWithTestDimensions());
        QcPlayerData player;
        player.dimension = dimension;
        const QByteArray file = storage.playerBytes(player);
        QCOMPARE(QcSaveFile::parse(file, u"player"_s, QByteArrayView(file).first(4), 12)->version, 12);

        const QcPlayerStorage::Result read = storage.parsePlayer(u"Lost"_s, file);
        QVERIFY(!read);
        QVERIFY2(read.error().error.contains(u"damaged"_s), qPrintable(read.error().error));
    }

    void aWorldOfTheLibraryRecordsItsDimensions()
    {
        // The game makes new worlds through the library, which writes world.json without the content
        // before the server starts.
        QTemporaryDir root;
        const QcWorldLibrary library(root.path());
        const QcResult<QcWorldLibrary::Entry> entry = library.create(u"Made in the menu"_s, 4711);
        QVERIFY2(entry, qPrintable(entry ? QString() : entry.error()));
        QVERIFY(storedVersions(entry->directory).isEmpty());

        const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
        {
            QcLocalServerHost host(configIn(entry->directory), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
            QVERIFY(host.shutdown().has_value());
        }
        QCOMPARE(storedVersions(entry->directory).keys(), (QStringList{u"test:empty"_s, u"test:end_like"_s}));
        QCOMPARE(storedVersion(entry->directory, u"test:empty"_s), QcGeneratorVersion::current);
    }

    void aNewWorldRecordsItsDimensionsAndAnOldOneOpensUnchanged()
    {
        QTemporaryDir dir;
        const QString worldJson = QDir(dir.path()).filePath(u"world.json"_s);
        // A world of 1.0.0: only the overworld, format 19, no versions of other dimensions.
        {
            const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
            QcLocalServerHost host(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
            QVERIFY(host.shutdown().has_value());
        }
        QJsonObject json = readJson(worldJson);
        QVERIFY(!json.value(u"generator"_s).toObject().contains(u"dimensions"_s));
        json.insert(u"formatVersion"_s, 19);
        writeJson(worldJson, json);
        const int overworldVersion = json.value(u"generator"_s).toObject().value(u"version"_s).toInt();

        // Opened by a game with more dimensions: nothing is converted, the overworld keeps its version
        // and the other dimensions get theirs only when they are first opened.
        {
            const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
            QcLocalServerHost host(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
            QVERIFY(host.shutdown().has_value());
        }
        json = readJson(worldJson);
        QCOMPARE(json.value(u"formatVersion"_s).toInt(), QcWorldInfo::formatVersion);
        QCOMPARE(json.value(u"generator"_s).toObject().value(u"version"_s).toInt(), overworldVersion);
        QVERIFY(!json.value(u"generator"_s).toObject().contains(u"dimensions"_s));
        QVERIFY(!QDir(dir.path()).exists(QString::fromLatin1(QcWorldStorage::dimensionsFolder)));

        // A new world gives every dimension the current version.
        QTemporaryDir fresh;
        {
            const std::shared_ptr<const QcContentRegistry> content = contentWithTestDimensions();
            QcLocalServerHost host(configIn(fresh.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
            QVERIFY(host.shutdown().has_value());
        }
        const QJsonObject versions = readJson(QDir(fresh.path()).filePath(u"world.json"_s)).value(u"generator"_s).toObject().value(u"dimensions"_s).toObject();
        QCOMPARE(versions.keys(), (QStringList{u"test:empty"_s, u"test:end_like"_s}));
        QCOMPARE(versions.value(u"test:empty"_s).toObject().value(u"version"_s).toInt(), QcGeneratorVersion::current);
    }
};

QTEST_GUILESS_MAIN(TestDimensions)
#include "test_dimensions.moc"

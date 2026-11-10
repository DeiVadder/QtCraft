// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSession.h"
#include "common/content/worldgen/QcDimensionParser.h"
#include "fixtures/QcTestUtil.h"
#include "fixtures/QcTestWorldFiles.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerState.h"
#include "server/storage/QcChunkStore.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr auto emptyName = "test:empty";
constexpr auto overworld = QcDimensionDefinition::overworldName;

QJsonObject voidDimension(const QString &name)
{
    return {{u"name"_s, name},
            {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
            {u"weather"_s, false}};
}

QcDimensionDefinition parsed(const QJsonObject &json)
{
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    return std::move(*dimension);
}

// The base content with an empty test dimension (decision E-978).
std::shared_ptr<QcContentRegistry> contentWithDimension()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    content->worldgen().registerDimension(parsed(voidDimension(QString::fromLatin1(emptyName))));
    return content;
}

// The files of a save that waits for its renames, below `root`.
QStringList stagedFiles(const QString &root)
{
    QStringList files;
    QDirIterator it(root, {u"*.next"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    files.sort();
    return files;
}

QcServerConfig configIn(const QString &directory)
{
    QcServerConfig config;
    config.worldName = u"Play"_s;
    config.seed = 4711;
    config.viewDistance = 2;
    config.worldDirectory = directory;
    config.keepChunksLoaded = true;
    return config;
}

QcBlockId blockOf(const QcContentRegistry &content, const QString &name)
{
    const std::optional<QcBlockId> block = content.blockId(name);
    if (!block) {
        qFatal("no block %s", qPrintable(name));
    }
    return *block;
}

// Joins `name` on `host`, plays until he is in the world and shuts the host down.
QString joinOnce(QcLocalServerHost &host, const QString &name)
{
    QcClientSession session(host.connectClient(), name);
    session.start();
    if (!QTest::qWaitFor([&session] { return session.state() == QcClientSession::State::Playing; }, 20000)) {
        return u"never played"_s;
    }
    return host.shutdown() ? QString() : u"no clean stop"_s;
}

// Plays `name` once in the world at `directory` with `content`; returns his file afterwards.
std::optional<QcPlayerData> playOnce(const QString &directory, const std::shared_ptr<const QcContentRegistry> &content, const QString &name)
{
    QcLocalServerHost host(configIn(directory), content, std::make_unique<QcTestTerrainGenerator>(content));
    if (!host.startError().isEmpty()) {
        qWarning() << host.startError();
        return std::nullopt;
    }
    if (const QString failure = joinOnce(host, name); !failure.isEmpty()) {
        qWarning() << failure;
        return std::nullopt;
    }
    const QcPlayerStorage::Result stored = QcTestWorldFiles(directory, content).readPlayer(name);
    return stored && stored->player ? stored->player : std::nullopt;
}

} // namespace

// Dimensions other than the overworld played, saved and loaded (decisions E-988, E-989).
class TestDimensionPlay : public QObject
{
    Q_OBJECT

private slots:
    void aDimensionKeepsWhatHappensInItAcrossRestarts()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        const QString name = QString::fromLatin1(emptyName);
        const QcBlockId glass = blockOf(*content, u"qtcraft:glass"_s);
        const QcBlockPos pos{5, 40, 7};
        const std::optional<QcCreatureKindId> pig = content->mobKind(u"qtcraft:pig"_s);
        QVERIFY(pig);
        {
            QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY(server.openWorld().has_value());
            QcResult<QcServerDimension *> opened = server.openDimension(name);
            QVERIFY2(opened, qPrintable(opened ? QString() : opened.error()));
            QcServerDimension &dimension = **opened;
            QVERIFY(dimension.world().loadChunk(pos.chunk()) != nullptr);
            QVERIFY(dimension.world().setBlock(pos, glass));
            const QcVec3 feet{5.5, 41.0, 7.5};
            QVERIFY(dimension.entities().creatures().spawn(*pig, feet, dimension.entities().newId(), false, true) != 0);
            QVERIFY(server.save().has_value());
        }

        const QDir folder(QDir(dir.path()).filePath(u"dimensions/test/empty"_s));
        QVERIFY(folder.exists(u"regions"_s));
        QVERIFY(folder.exists(QString::fromLatin1(QcWorldStorage::dimensionFileName)));

        QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        // The overworld at the same place never saw it.
        QVERIFY(server.world().loadChunk(pos.chunk()) != nullptr);
        QVERIFY(server.world().blockAt(pos) != glass);

        QcResult<QcServerDimension *> opened = server.openDimension(name);
        QVERIFY(opened);
        QcServerDimension &dimension = **opened;
        QVERIFY(dimension.world().loadChunk(pos.chunk()) != nullptr);
        QCOMPARE(dimension.world().blockAt(pos), glass);
        QCOMPARE(dimension.entities().creatures().mobs().size(), 1);
    }

    void everyOpenDimensionTicks()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        QcResult<QcServerDimension *> opened = server.openDimension(QString::fromLatin1(emptyName));
        QVERIFY(opened);
        QcServerDimension &dimension = **opened;
        const QcChunkPos chunk{0, 0};
        QVERIFY(dimension.world().loadChunk(chunk) != nullptr);
        const std::optional<QcCreatureKindId> pig = content->mobKind(u"qtcraft:pig"_s);
        QVERIFY(pig);
        const QcVec3 feet{8.5, 100.0, 8.5};
        QVERIFY(dimension.entities().creatures().spawn(*pig, feet, dimension.entities().newId(), false, true) != 0);

        for (int i = 0; i < 20; ++i) {
            server.tick();
        }
        QCOMPARE(dimension.entities().creatures().mobs().size(), 1);
        QVERIFY(dimension.entities().creatures().mobs().first().motion.position.y < feet.y - 0.5);
    }

    void entityIdsAreUniqueAcrossDimensions()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());
        QcResult<QcServerDimension *> opened = server.openDimension(QString::fromLatin1(emptyName));
        QVERIFY(opened);
        QcEntities &there = (*opened)->entities();
        QcEntities &here = server.entities();

        // One counter: ids drawn in turn never meet.
        const quint32 first = here.newId();
        const quint32 second = there.newId();
        const quint32 third = here.newId();
        QVERIFY(first != second && second != third && first != third);

        // The lasting uids of creatures too, whichever dimension they were spawned in.
        const std::optional<QcCreatureKindId> pig = content->mobKind(u"qtcraft:pig"_s);
        QVERIFY(pig);
        const QcVec3 feet{8.5, 100.0, 8.5};
        const quint32 mine = here.creatures().spawn(*pig, feet, here.newId(), false, true);
        const quint32 theirs = there.creatures().spawn(*pig, feet, there.newId(), false, true);
        QVERIFY(mine != 0 && theirs != 0 && mine != theirs);
        const quint64 mineUid = here.creatures().find(mine)->uid;
        const quint64 theirUid = there.creatures().find(theirs)->uid;
        QVERIFY(mineUid != 0 && theirUid != 0 && mineUid != theirUid);
        QCOMPARE(here.creatures().nextUid(), there.creatures().nextUid());
    }

    void aSaveOfTwoDimensionsIsFinishedFromOneJournal()
    {
        QTemporaryDir dir;
        const QString linkedFolder = QDir(dir.path()).filePath(u"dimensions/test/empty"_s);
        const QByteArray first("first");
        const QByteArray second("second");
        const QString file = u"blockupdates.qce"_s;
        QcChunkStore world(dir.path());
        QcChunkStore linked(linkedFolder);
        QVERIFY(world.open().has_value());
        QVERIFY(linked.open().has_value());
        linked.keepFiles({{file, first}});
        QVERIFY(world.commit({{file, first}}, {&linked}).has_value());
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcChunkStore::journalName.toString())));

        // A save that breaks off after its first rename leaves a journal in the world directory only.
        linked.keepFiles({{file, second}});
        world.failRenamesAfter(1);
        QVERIFY(!world.commit({{file, second}}, {&linked}).has_value());
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(QcChunkStore::journalName.toString())));
        QVERIFY(!QFile::exists(QDir(linkedFolder).filePath(QcChunkStore::journalName.toString())));

        // Opening the world finishes both dimensions together.
        QcChunkStore worldAgain(dir.path());
        QVERIFY(worldAgain.open().has_value());
        QcChunkStore linkedAgain(linkedFolder);
        QVERIFY(linkedAgain.open().has_value());
        const QcResult<std::optional<QByteArray>> own = worldAgain.readFile(file);
        const QcResult<std::optional<QByteArray>> other = linkedAgain.readFile(file);
        QVERIFY(own && other && *own && *other);
        QCOMPARE(**own, second);
        QCOMPARE(**other, second);
    }

    void aReadInALinkedStoreLetsTheWorldFinishItsJournal()
    {
        QTemporaryDir dir;
        const QString linkedFolder = QDir(dir.path()).filePath(u"dimensions/test/empty"_s);
        const QByteArray first("first");
        const QByteArray second("second");
        const QString file = u"blockupdates.qce"_s;
        QcChunkStore world(dir.path());
        QcChunkStore linked(linkedFolder);
        QVERIFY(world.open().has_value());
        QVERIFY(linked.open().has_value());
        linked.keepFiles({{file, first}});
        QVERIFY(world.commit({{file, first}}, {&linked}).has_value());
        linked.keepFiles({{file, second}});
        world.failRenamesAfter(1);
        QVERIFY(!world.commit({{file, second}}, {&linked}).has_value());
        world.failRenamesAfter(-1);

        // The first read after the break comes from a dimension: the journal of the world still
        // needs the staged files in its folder, and what it reads is the finished save.
        QVERIFY(linked.read(QcChunkPos{0, 0}).has_value());
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcChunkStore::journalName.toString())));
        QFile finished(QDir(linkedFolder).filePath(file));
        QVERIFY(finished.open(QIODevice::ReadOnly));
        QCOMPARE(finished.readAll(), second);

        // Later saves go on and the world still opens.
        const QcStatus again = world.commit({}, {&linked});
        QVERIFY2(again.has_value(), again ? "" : qPrintable(again.error()));
        QcChunkStore worldAgain(dir.path());
        const QcStatus opened = worldAgain.open();
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error()));
    }

    void aSecondFailureWhileFinishingTheJournalLosesNothing()
    {
        QTemporaryDir dir;
        const QString linkedFolder = QDir(dir.path()).filePath(u"dimensions/test/empty"_s);
        const QString file = u"blockupdates.qce"_s;
        const QString journal = QDir(dir.path()).filePath(QcChunkStore::journalName.toString());
        constexpr QcChunkPos changing{0, 0};
        constexpr QcChunkPos resting{1, 0};
        QcChunkStore world(dir.path());
        QcChunkStore linked(linkedFolder);
        QVERIFY(world.open().has_value());
        QVERIFY(linked.open().has_value());
        for (QcChunkStore *store : {&world, &linked}) {
            store->put(QcChunkStore::Kind::Blocks, changing, QByteArray("old"));
            store->put(QcChunkStore::Kind::Blocks, resting, QByteArray("resting"));
        }
        linked.keepFiles({{file, QByteArray("first")}});
        QVERIFY(world.commit({{file, QByteArray("first")}}, {&linked}).has_value());

        world.put(QcChunkStore::Kind::Blocks, changing, QByteArray("new"));
        linked.put(QcChunkStore::Kind::Blocks, changing, QByteArray("new"));
        linked.keepFiles({{file, QByteArray("second")}});
        world.failRenamesAfter(1);
        QVERIFY(!world.commit({{file, QByteArray("second")}}, {&linked}).has_value());
        const QStringList staged = stagedFiles(dir.path());
        QVERIFY(!staged.isEmpty());

        // Finishing the journal fails too: everything it needs stays, what is known stays known.
        world.failRenamesAfter(0);
        QVERIFY(!linked.read(resting).has_value());
        QVERIFY(QFile::exists(journal));
        QCOMPARE(stagedFiles(dir.path()), staged);
        QVERIFY(world.has(QcChunkStore::Kind::Blocks, resting));
        QVERIFY(linked.has(QcChunkStore::Kind::Blocks, resting));

        // The next read finishes it.
        world.failRenamesAfter(-1);
        const QcResult<QcChunkStore::Blobs> blobs = linked.read(resting);
        QVERIFY2(blobs.has_value(), blobs ? "" : qPrintable(blobs.error()));
        QCOMPARE(*blobs->data[static_cast<std::size_t>(QcChunkStore::Kind::Blocks)], QByteArray("resting"));
        QVERIFY(!QFile::exists(journal));
        QVERIFY(stagedFiles(dir.path()).isEmpty());
        QVERIFY(world.commit({}, {&linked}).has_value());
        QcChunkStore worldAgain(dir.path());
        QVERIFY(worldAgain.open().has_value());
    }

    void aPlayerComesBackToTheDimensionHeWasIn()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcPlayerData saved;
        saved.motion.position = {0.5, 2.0, 0.5};
        saved.dimension = QString::fromLatin1(emptyName);
        QVERIFY(QcTestWorldFiles(dir.path(), content).writePlayer(u"Traveller"_s, saved).has_value());

        const std::optional<QcPlayerData> after = playOnce(dir.path(), content, u"Traveller"_s);
        QVERIFY(after);
        QCOMPARE(after->dimension, QString::fromLatin1(emptyName));
        QVERIFY(!after->returnTo);
        QVERIFY(std::abs(after->motion.position.x - 0.5) < 1.0);
        QVERIFY(QDir(dir.path()).exists(u"dimensions/test/empty/"_s + QString::fromLatin1(QcWorldStorage::dimensionFileName)));
    }

    void aPlayerKeepsHisPlaceWhileAModIsMissing()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> with = contentWithDimension();
        const std::shared_ptr<const QcContentRegistry> without = QcTestUtil::loadContent();
        QcPlayerData saved;
        saved.motion.position = {0.5, 2.0, 0.5};
        saved.yaw = 42.0f;
        saved.dimension = QString::fromLatin1(emptyName);
        QVERIFY(QcTestWorldFiles(dir.path(), with).writePlayer(u"Traveller"_s, saved).has_value());

        // Without the mod he plays from the world spawn; his file names both places.
        std::optional<QcPlayerData> away = playOnce(dir.path(), without, u"Traveller"_s);
        QVERIFY(away);
        QCOMPARE(away->dimension, QString::fromLatin1(overworld));
        QVERIFY(away->returnTo);
        QCOMPARE(away->returnTo->dimension, QString::fromLatin1(emptyName));
        QCOMPARE(away->returnTo->position, saved.motion.position);
        QCOMPARE(away->returnTo->yaw, 42.0f);

        // A second absence with a save in it changes nothing.
        away = playOnce(dir.path(), without, u"Traveller"_s);
        QVERIFY(away && away->returnTo);
        QCOMPARE(away->returnTo->position, saved.motion.position);

        // The mod is back: so is he, and the note is gone.
        const std::optional<QcPlayerData> back = playOnce(dir.path(), with, u"Traveller"_s);
        QVERIFY(back);
        QCOMPARE(back->dimension, QString::fromLatin1(emptyName));
        QVERIFY(!back->returnTo);
        QVERIFY(std::abs(back->motion.position.x - 0.5) < 1.0);
        QCOMPARE(back->yaw, 42.0f);
    }

    void aPlayerTravelsBetweenDimensionsAndRespawnsInTheOverworld()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcServerConfig config = configIn(dir.path());
        config.operators = {u"Traveller"_s};
        QcLocalServerHost host(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY2(host.startError().isEmpty(), qPrintable(host.startError()));
        QcClientSession session(host.connectClient(), u"Traveller"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, 20000);

        const auto isOnThePlatform = [&session] { return std::abs(session.playerMotion().position.x - 0.5) < 2.0 && session.playerMotion().position.y < 10.0; };
        const QcVec3 start = session.playerMotion().position;
        session.sendChat(u"/tp 0.5 3 0.5 in test:empty"_s);
        QTRY_VERIFY_WITH_TIMEOUT(isOnThePlatform(), 20000);

        // Dying there is a real death, and he comes back at the world spawn in the overworld.
        session.sendChat(u"/kill"_s);
        QTRY_VERIFY_WITH_TIMEOUT(session.isDead(), 20000);
        session.respawn();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isDead() && !isOnThePlatform(), 20000);
        QVERIFY(std::abs(session.playerMotion().position.x - start.x) < 2.0);
        QVERIFY(std::abs(session.playerMotion().position.z - start.z) < 2.0);

        // The way back leads to the same platform, and that is where the file puts him.
        session.sendChat(u"/tp 0.5 3 0.5 in test:empty"_s);
        QTRY_VERIFY_WITH_TIMEOUT(isOnThePlatform(), 20000);
        QVERIFY(host.shutdown().has_value());

        const QcPlayerStorage::Result stored = QcTestWorldFiles(dir.path(), content).readPlayer(u"Traveller"_s);
        QVERIFY(stored && stored->player);
        QCOMPARE(stored->player->dimension, QString::fromLatin1(emptyName));
    }

    void playerFilesKeepTheReturn()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcPlayerData player;
        player.returnTo = QcPlayerReturn{u"mod:gone"_s, {1.5, 70.0, -3.5}, 10.0f, -20.0f};
        const QcTestWorldFiles files(dir.path(), content);
        QVERIFY(files.writePlayer(u"Kept"_s, player).has_value());
        const QcPlayerStorage::Result read = files.readPlayer(u"Kept"_s);
        QVERIFY(read && read->player);
        QCOMPARE(read->player->returnTo, player.returnTo);
    }

    void aReturnOutsideTheWorldIsDamagedLikeThePosition()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        const QcTestWorldFiles files(dir.path(), content);
        const auto refused = [&files](const QcPlayerReturn &back) {
            QcPlayerData player;
            player.returnTo = back;
            return files.writePlayer(u"Far"_s, player).has_value() && !files.readPlayer(u"Far"_s);
        };
        QVERIFY(refused({u"mod:gone"_s, {1e12, 70.0, 0.0}, 0.0f, 0.0f}));
        QVERIFY(refused({u"mod:gone"_s, {0.0, 1e9, 0.0}, 0.0f, 0.0f}));
        QVERIFY(refused({u"mod:gone"_s, {0.0, 70.0, 0.0}, 0.0f, 120.0f}));
    }

    void aDimensionIsSetUpOnceAndLeavesTheRulesOfTheWorld()
    {
        QTemporaryDir dir;
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcResult<QcWorldInfo> before = qcError(u"not read"_s);
        {
            QcServer server(configIn(dir.path()), content, std::make_unique<QcTestTerrainGenerator>(content));
            QVERIFY(server.openWorld().has_value());
            QVERIFY(server.save().has_value());
            before = QcTestWorldFiles(dir.path(), content).readInfo();
            QVERIFY(before);
            QVERIFY(server.openDimension(QString::fromLatin1(emptyName)));
            QVERIFY(server.save().has_value());
        }
        // The rules stay what the overworld's generator set.
        const QcResult<QcWorldInfo> after = QcTestWorldFiles(dir.path(), content).readInfo();
        QVERIFY(after);
        QCOMPARE(after->mobSpawning, before->mobSpawning);
        QCOMPARE(after->weatherCycle, before->weatherCycle);
        QCOMPARE(after->daylightCycle, before->daylightCycle);
        const QString path = QDir(dir.path()).filePath(u"dimensions/test/empty/"_s + QString::fromLatin1(QcWorldStorage::dimensionFileName));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(file.readAll()).object().value(u"setupDone"_s).toBool());
    }

    void wrongDefinitionsAreReported_data()
    {
        QTest::addColumn<QJsonObject>("json");
        QTest::addColumn<QString>("expected");
        QJsonObject unknownType = voidDimension(u"test:bad"_s);
        unknownType.insert(u"generator"_s, QJsonObject{{u"type"_s, u"nothing"_s}});
        QTest::newRow("unknown type") << unknownType << u"cannot be generated"_s;
        QJsonObject wrongOptions = voidDimension(u"test:bad"_s);
        wrongOptions.insert(u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=mod:nothing"_s}});
        QTest::newRow("wrong options") << wrongOptions << u"cannot be generated"_s;
        QJsonObject overworldType = voidDimension(QString::fromLatin1(overworld));
        QTest::newRow("overworld with a type") << overworldType << u"as the world was created"_s;
        QJsonObject platform = voidDimension(u"test:bad"_s);
        platform.insert(u"arrival"_s, QJsonObject{{u"x"_s, 0}, {u"y"_s, 40}, {u"z"_s, 0}, {u"platform"_s, u"mod:nothing"_s}});
        QTest::newRow("platform") << platform << u"arrival platform"_s;
        QJsonObject liquid = voidDimension(u"test:bad"_s);
        liquid.insert(u"liquids"_s, QJsonObject{{u"mod:nothing"_s, QJsonObject{{u"distance"_s, 2}}}});
        QTest::newRow("liquid") << liquid << u"unknown liquid"_s;
    }

    void wrongDefinitionsAreReported()
    {
        QFETCH(QJsonObject, json);
        QFETCH(QString, expected);
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QVERIFY(QcDimensions::definitionWarnings(*content).isEmpty());
        content->worldgen().registerDimension(parsed(json));
        const QStringList warnings = QcDimensions::definitionWarnings(*content);
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(expected), qPrintable(warnings.first()));
    }

    void onlyTheOverworldGeneratesAsTheWorld()
    {
        // The parser refuses this in a file; a definition made in code reaches the check.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcDimensionDefinition dimension = parsed(voidDimension(u"test:bad"_s));
        dimension.generatorType = QString::fromLatin1(QcDimensionDefinition::worldGenerator);
        content->worldgen().registerDimension(dimension);
        const QStringList warnings = QcDimensions::definitionWarnings(*content);
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(u"only the overworld"_s), qPrintable(warnings.first()));
    }

    void aWorldFullOfVersionsWarnsWhenItLoads()
    {
        const std::shared_ptr<const QcContentRegistry> content = contentWithDimension();
        QcWorldInfo info;
        QVERIFY(QcDimensions::versionWarnings(*content, info).isEmpty());
        for (int i = 0; i < QcWorldInfo::maxDimensionVersions; ++i) {
            info.dimensionGeneratorVersions.insert(u"mod:gone_%1"_s.arg(i), 1);
        }
        // The test dimension and the nether of the base game find no room.
        const QStringList warnings = QcDimensions::versionWarnings(*content, info);
        QCOMPARE(warnings.size(), 2);
        QVERIFY(warnings.join(u' ').contains(QString::fromLatin1(emptyName)));
        QVERIFY(warnings.join(u' ').contains(u"qtcraft:nether"_s));
    }
};

QTEST_GUILESS_MAIN(TestDimensionPlay)
#include "test_dimension_play.moc"

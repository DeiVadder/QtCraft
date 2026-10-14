#include "QcBaseContent.h"
#include "QcContentPackLoader.h"
#include "QcDayCycle.h"
#include "QcInProcessTransport.h"
#include "QcRawClient.h"
#include "QcServer.h"
#include "QcShowcaseGenerator.h"
#include "QcTestTerrainGenerator.h"
#include "QcWorldGenerators.h"
#include "QcWorldLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

void pump()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeJson(const QString &path, const QJsonObject &json)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(json).toJson());
    }
}

// Base content and a mod with a block of four state properties, names and values 32 characters each:
// its state descriptions are longer than a label may be on the wire.
std::shared_ptr<const QcContentRegistry> loadContentWithLongStates(const QString &modDirectory)
{
    QJsonObject states;
    for (int i = 0; i < 4; ++i) {
        states.insert(QString(31, u'p') + QString::number(i), QJsonArray{QString(32, QChar(u'a' + i)), QString(32, u'z')});
    }
    writeJson(QDir(modDirectory).filePath(u"pack.json"_s), {{u"id"_s, u"longstates"_s}, {u"version"_s, u"1"_s}});
    writeJson(QDir(modDirectory).filePath(u"blocks/gizmo.json"_s),
              {{u"name"_s, u"gizmo"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:stone"_s}}}, {u"states"_s, states}});

    QStringList warnings;
    auto content = std::make_shared<QcContentRegistry>();
    if (!QcBaseContent::loadInto(*content, warnings) || !QcContentPackLoader::load(modDirectory, *content, warnings)) {
        return nullptr;
    }
    warnings += content->finish();
    return content;
}

} // namespace

class TestShowcaseWorld : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

    std::unique_ptr<QcServer> makeServer(const QString &directory, const QString &generator = u"showcase"_s, qint64 seed = 0)
    {
        QcServerConfig config;
        config.worldDirectory = directory;
        config.generator = generator;
        config.seed = seed;
        config.viewDistance = 1;
        config.autosaveSeconds = 0;
        return std::make_unique<QcServer>(config, m_content, QcWorldGenerators::create(generator, m_content, seed));
    }

    int expectedMobs() const { return int(QcShowcaseGenerator(m_content).setup().mobs.size()); }

    // Distinct items in the server's chests.
    static qsizetype storedItems(QcServer &server)
    {
        QSet<QcItemId> stored;
        const QList<QcChunkPos> chunks = server.blockEntities().chunks();
        for (const QcChunkPos &chunk : chunks) {
            const QHash<QcBlockPos, QcBlockEntities::Container> containers = server.blockEntities().chunk(chunk)->containers;
            for (const QcBlockEntities::Container &container : containers) {
                for (const QcItemStack &stack : container.stacks) {
                    stored.insert(stack.item);
                }
            }
        }
        return stored.size();
    }

private slots:
    void newWorldGetsItsSetupOnce()
    {
        QTemporaryDir directory;
        const int mobs = expectedMobs();
        QVERIFY(mobs >= m_content->mobs().size());
        {
            std::unique_ptr<QcServer> server = makeServer(directory.path());
            QVERIFY(server->openWorld());

            // Noon that stands still, no creatures of their own accord, all creatures in their pens.
            QCOMPARE(server->worldTime(), 150.0);
            server->tick();
            server->tick();
            QCOMPARE(server->worldTime(), 150.0);
            QCOMPARE(server->mobs().mobs().size(), mobs);
            QVERIFY(std::ranges::all_of(server->mobs().mobs(), &QcCreature::persistent));
            QVERIFY(!server->mobs().naturalSpawning());

            // The workshop's furnace got its ore, the item chests hold every item.
            bool furnaceFilled = false;
            server->blockEntities().forEachMachine([&](const QcBlockEntities::Machine &machine) {
                furnaceFilled = furnaceFilled || machine.stacks[QcMachineRules::inputSlot].item == *m_content->itemId(u"qtcraft:iron_ore"_s);
            });
            QVERIFY(furnaceFilled);
            QVERIFY(storedItems(*server) >= m_content->itemCount());
            QVERIFY(!server->blockEntities().saplingsInOrder().isEmpty());
            QVERIFY(server->pendingCommandBlockUpdates() + server->waitingBlockUpdates() >= 6);
            QVERIFY(server->stop());
        }

        const QJsonObject info = readJson(QDir(directory.path()).filePath(u"world.json"_s));
        QCOMPARE(info.value(u"formatVersion"_s).toInt(), QcWorldInfo::formatVersion);
        QCOMPARE(info.value(u"generator"_s).toObject().value(u"type"_s).toString(), u"showcase"_s);
        QVERIFY(info.value(u"generator"_s).toObject().value(u"setupDone"_s).toBool());
        QCOMPARE(info.value(u"gameRules"_s).toObject().value(u"daylightCycle"_s).toBool(true), false);
        QCOMPARE(info.value(u"gameRules"_s).toObject().value(u"mobSpawning"_s).toBool(true), false);
        QCOMPARE(info.value(u"gameRules"_s).toObject().value(u"randomTicks"_s).toBool(true), false);

        // Opened again, nothing is set up twice.
        std::unique_ptr<QcServer> again = makeServer(directory.path());
        QVERIFY(again->openWorld());
        QCOMPARE(again->mobs().mobs().size(), mobs);
        QCOMPARE(again->worldTime(), 150.0);
        QVERIFY(again->stop());
    }

    void setupCountsAsDoneOnlyOnceSaved()
    {
        QTemporaryDir directory;
        const QDir world(directory.path());
        const int mobs = expectedMobs();

        // The block entities (chests) cannot be written, a file stands where their folder belongs:
        // the first save breaks off after the creatures were stored.
        const QString blocker = world.filePath(u"blockentities"_s);
        QFile blocking(blocker);
        QVERIFY(blocking.open(QIODevice::WriteOnly));
        blocking.close();
        {
            std::unique_ptr<QcServer> server = makeServer(directory.path());
            QVERIFY(server->openWorld());
            QCOMPARE(server->mobs().mobs().size(), mobs);
            QVERIFY(!server->stop());
        }
        QVERIFY(!QDir(world.filePath(u"entities"_s)).entryList({u"r.*.qen"_s}, QDir::Files).isEmpty());
        QVERIFY(!readJson(world.filePath(u"world.json"_s)).value(u"generator"_s).toObject().value(u"setupDone"_s).toBool(true));
        QVERIFY(QFile::remove(blocker));

        // Opened again, the setup runs once more: every creature once, the chests filled.
        {
            std::unique_ptr<QcServer> again = makeServer(directory.path());
            QVERIFY(again->openWorld());
            QCOMPARE(again->mobs().mobs().size(), mobs);
            QVERIFY(storedItems(*again) >= m_content->itemCount());
            QVERIFY(again->stop());
        }
        QVERIFY(readJson(world.filePath(u"world.json"_s)).value(u"generator"_s).toObject().value(u"setupDone"_s).toBool());

        // From then on everything comes from the files.
        std::unique_ptr<QcServer> third = makeServer(directory.path());
        QVERIFY(third->openWorld());
        QCOMPARE(third->mobs().mobs().size(), mobs);
        QVERIFY(storedItems(*third) >= m_content->itemCount());
        QVERIFY(third->stop());
    }

    void worldOfAnotherTypeDoesNotOpen()
    {
        QTemporaryDir directory;
        {
            std::unique_ptr<QcServer> server = makeServer(directory.path());
            QVERIFY(server->openWorld());
            QVERIFY(server->stop());
        }
        std::unique_ptr<QcServer> terrain = makeServer(directory.path(), u"terrain"_s);
        const QcStatus opened = terrain->openWorld();
        QVERIFY(!opened);
        QVERIFY2(opened.error().contains(u"showcase"_s), qPrintable(opened.error()));
    }

    void libraryWorldsRememberTheirType()
    {
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QcResult<QcWorldLibrary::Entry> showcase = library.create(u"Show"_s, 0, {.generator = u"showcase"_s, .options = {}, .gameMode = QcGameMode::Survival});
        const QcResult<QcWorldLibrary::Entry> terrain = library.create(u"Plain"_s, 5);
        QVERIFY(showcase && terrain);
        QCOMPARE(showcase->info.generator, u"showcase"_s);
        QVERIFY(!showcase->info.setupDone);
        const QList<QcWorldLibrary::Entry> entries = library.list();
        for (const QcWorldLibrary::Entry &entry : entries) {
            QCOMPARE(entry.info.generator, entry.info.name == u"Show"_s ? u"showcase"_s : u"terrain"_s);
            QVERIFY(!entry.info.setupDone);
        }

        // A terrain world's setup is empty: its stored time stays, time and creatures run.
        std::unique_ptr<QcServer> server = makeServer(terrain->directory, u"terrain"_s, 5);
        QVERIFY(server->openWorld());
        QCOMPARE(server->worldTime(), terrain->info.worldTime);
        server->tick();
        QVERIFY(server->worldTime() > terrain->info.worldTime);
        QVERIFY(server->mobs().naturalSpawning());
        QVERIFY(server->stop());
    }

    void worldsOfOtherFormatsAreRefused()
    {
        // world.json of format 1 (before the showcase), 2 (before stage 31.1) and 3 (before stage
        // 31.2): a world of an older development version is refused, not converted (decision E-356),
        // and nothing is written.
        for (const int version : {1, 2, 3}) {
            QTemporaryDir directory;
            const QString path = QDir(directory.path()).filePath(u"world.json"_s);
            const QJsonObject old{{u"formatVersion"_s, version}, {u"name"_s, u"Old"_s}, {u"seed"_s, u"42"_s}, {u"worldTime"_s, 1234.5}};
            writeJson(path, old);
            QVERIFY(!QcWorldStorage(directory.path(), m_content).readInfo());

            QcServerConfig config;
            config.worldDirectory = directory.path();
            config.seed = 42;
            QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY(server.worldTooOld());
            QVERIFY(opened.error().contains(u"older development version"_s));
            QCOMPARE(readJson(path), old);
        }

        // Later formats are refused too, but not as old ones.
        QTemporaryDir directory;
        writeJson(QDir(directory.path()).filePath(u"world.json"_s), {{u"formatVersion"_s, QcWorldInfo::formatVersion + 1}, {u"name"_s, u"New"_s}, {u"seed"_s, u"1"_s}});
        QVERIFY(!QcWorldStorage(directory.path(), m_content).readInfo());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.seed = 1;
        QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(!server.openWorld().has_value());
        QVERIFY(!server.worldTooOld());
    }

    void clientGetsLabelsAndAStandingTime()
    {
        // An unsaved showcase world; the client stands in front of the creatures.
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.viewDistance = 2;
        const auto generator = std::make_shared<QcShowcaseGenerator>(m_content);
        const QcShowcaseAreaInfo creatures = *std::ranges::find(generator->plan().areas, u"creatures"_s, &QcShowcaseAreaInfo::name);
        // In the middle of the area: the whole row of enclosures lies within the view distance, wherever
        // the area falls on the chunk grid.
        config.spawnColumn = std::array<int, 2>{creatures.origin.x + creatures.sizeX / 2, creatures.origin.z + creatures.sizeZ + 2};
        QcServer server(config, m_content, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        QVERIFY(server.openWorld());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server.addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        QcRawClient client(std::move(clientEnd));
        client.hello(u"Viewer"_s);
        pump();
        for (int i = 0; i < 30; ++i) {
            server.tick();
            pump();
        }

        QVERIFY(!client.chunkLabels.isEmpty());
        for (auto it = client.chunkLabels.constBegin(); it != client.chunkLabels.constEnd(); ++it) {
            QCOMPARE(it.value(), generator->labelsIn(it.key()));
        }
        const bool zombieLabel = std::ranges::any_of(client.chunkLabels, [](const QList<QcWorldLabel> &labels) {
            return std::ranges::any_of(labels, [](const QcWorldLabel &label) { return label.text == u"mob.qtcraft.zombie"_s; });
        });
        QVERIFY(zombieLabel);
        QCOMPARE(client.worldTime, 150.0);
        QVERIFY(!client.timeRunning);

        // The rules can be switched back.
        client.connection.send(QcMsg::ChatInput{u"/gamerule daylightCycle true"_s});
        client.connection.send(QcMsg::ChatInput{u"/gamerule mobSpawning"_s});
        pump();
        server.tick();
        pump();
        QVERIFY(client.timeRunning);
        QVERIFY(server.worldTime() > 150.0);
        QVERIFY(client.chatLines.contains(u"mobSpawning is false"_s));
        QVERIFY(server.stop());
    }

    void longLabelsAreCutAndKeepTheConnection()
    {
        QTemporaryDir modDirectory;
        const std::shared_ptr<const QcContentRegistry> content = loadContentWithLongStates(modDirectory.path());
        QVERIFY(content && content->blockId(u"longstates:gizmo"_s));

        // A podium label of the mod block, as planned: cut to the limit with an ellipsis.
        const auto generator = std::make_shared<QcShowcaseGenerator>(content);
        std::optional<QcWorldLabel> planned;
        for (const QList<QcWorldLabel> &labels : generator->plan().labelsByChunk) {
            const auto found = std::ranges::find_if(labels, [](const QcWorldLabel &label) { return label.text.contains(u"gizmo"_s); });
            if (found != labels.end()) {
                planned = *found;
                break;
            }
        }
        QVERIFY(planned);
        // Uncut it would be 4 × (32 + 1 + 32) characters and three ", ": 266.
        QCOMPARE(planned->detail.size(), QcWorldLabel::maxTextLength);
        QVERIFY2(planned->detail.startsWith(QString(31, u'p') + u"0="_s), qPrintable(planned->detail));
        QVERIFY(planned->detail.endsWith(QChar(0x2026)));

        // The client standing at that podium gets the label and stays connected.
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.viewDistance = 1;
        config.spawnColumn = std::array<int, 2>{int(std::floor(planned->position.x)), int(std::floor(planned->position.z)) + 3};
        QcServer server(config, content, QcWorldGenerators::create(u"showcase"_s, content, 0));
        QVERIFY(server.openWorld());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server.addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        QcRawClient client(std::move(clientEnd));
        client.hello(u"Viewer"_s);
        pump();
        for (int i = 0; i < 30; ++i) {
            server.tick();
            pump();
        }

        QVERIFY2(client.connection.isOpen(), qPrintable(client.connection.lastError()));
        const QcBlockPos column{int(std::floor(planned->position.x)), 0, int(std::floor(planned->position.z))};
        QVERIFY(client.chunkLabels.value(column.chunk()).contains(*planned));
        QVERIFY(server.stop());
    }

    void noCreatureSpawnsByItselfWithoutTheRule()
    {
        // The test terrain is grass: with spawning on, animals come with new chunks and around players.
        QcMobManager mobs(m_content, 7);
        mobs.setNaturalSpawning(false);
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        quint32 next = 1;
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world.loadChunk({x, z});
                mobs.populateChunk(world, {x, z}, [&next] { return next++; });
            }
        }
        const QList<QcMobManager::PlayerView> players{{1000, {0.5, 70.0, 0.5}, true}};
        for (int i = 0; i < 400; ++i) {
            mobs.tick(world, players, QcDayCycle::dayLength * 0.75, 0.05, [&next] { return next++; });
        }
        QCOMPARE(mobs.mobs().size(), 0);
        QVERIFY(mobs.populatedChunks().isEmpty());

        mobs.setNaturalSpawning(true);
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                mobs.populateChunk(world, {x, z}, [&next] { return next++; });
            }
        }
        for (int i = 0; i < 400; ++i) {
            mobs.tick(world, players, QcDayCycle::dayLength * 0.75, 0.05, [&next] { return next++; });
        }
        QVERIFY(!mobs.mobs().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestShowcaseWorld)
#include "test_showcase_world.moc"
